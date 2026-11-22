#include <drivers/nvme.h>
#include <drivers/pci.h>
#include <drivers/timer.h>
#include <mm/heap.h>
#include <mm/paging.h>
#include <string.h>

#define NVME_REG_CAP   0x00
#define NVME_REG_VS    0x08
#define NVME_REG_CC    0x14
#define NVME_REG_CSTS  0x1C
#define NVME_REG_AQA   0x24
#define NVME_REG_ASQ   0x28
#define NVME_REG_ACQ   0x30

#define NVME_QUEUE_DEPTH 16

typedef struct {
    uint32_t cdw0;
    uint32_t nsid;
    uint64_t rsvd;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed)) nvme_sqe_t;

typedef struct {
    uint32_t dw0;
    uint32_t dw1;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;
} __attribute__((packed)) nvme_cqe_t;

static volatile uint8_t* g_nvme_mmio = NULL;
static uint32_t          g_db_stride = 4;
static bool              g_nvme_present = false;
static uint64_t          g_nvme_sectors = 0;
static uint32_t          g_nvme_sector_sz = 512;
static char              g_nvme_model[48] = "NVMe Controller";

static nvme_sqe_t* g_asq = NULL;
static nvme_cqe_t* g_acq = NULL;
static uint16_t    g_asq_tail = 0;
static uint16_t    g_acq_head = 0;
static uint8_t     g_acq_phase = 1;

static nvme_sqe_t* g_iosq = NULL;
static nvme_cqe_t* g_iocq = NULL;
static uint16_t    g_iosq_tail = 0;
static uint16_t    g_iocq_head = 0;
static uint8_t     g_iocq_phase = 1;
static uint16_t    g_next_cid = 1;

static inline uint32_t nvme_r32(uint32_t off) {
    return *(volatile uint32_t*)(g_nvme_mmio + off);
}

static inline uint64_t nvme_r64(uint32_t off) {
    uint64_t lo = *(volatile uint32_t*)(g_nvme_mmio + off);
    uint64_t hi = *(volatile uint32_t*)(g_nvme_mmio + off + 4);
    return lo | (hi << 32);
}

static inline void nvme_w32(uint32_t off, uint32_t v) {
    *(volatile uint32_t*)(g_nvme_mmio + off) = v;
}

static inline void nvme_w64(uint32_t off, uint64_t v) {
    *(volatile uint32_t*)(g_nvme_mmio + off) = (uint32_t)(v & 0xFFFFFFFFULL);
    *(volatile uint32_t*)(g_nvme_mmio + off + 4) = (uint32_t)(v >> 32);
}

static inline void nvme_ring_sq_db(uint16_t qid, uint16_t tail) {
    nvme_w32(0x1000U + (uint32_t)(2U * qid) * g_db_stride, tail);
}

static inline void nvme_ring_cq_db(uint16_t qid, uint16_t head) {
    nvme_w32(0x1000U + (uint32_t)(2U * qid + 1U) * g_db_stride, head);
}

static bool nvme_submit_cmd(uint16_t qid, nvme_sqe_t* sq, nvme_cqe_t* cq,
                            uint16_t* sq_tail, uint16_t* cq_head, uint8_t* cq_phase,
                            const nvme_sqe_t* cmd) {
    uint16_t cid = g_next_cid++;
    if (g_next_cid == 0) g_next_cid = 1;

    nvme_sqe_t* slot = &sq[*sq_tail];
    *slot = *cmd;
    slot->cdw0 = (cmd->cdw0 & 0x0000FFFFU) | ((uint32_t)cid << 16);

    *sq_tail = (*sq_tail + 1) % NVME_QUEUE_DEPTH;
    nvme_ring_sq_db(qid, *sq_tail);

    for (uint32_t t = 0; t < 2000000; t++) {
        volatile nvme_cqe_t* cqe = &cq[*cq_head];
        uint16_t st = cqe->status;
        if ((st & 1U) == *cq_phase) {
            bool ok = ((st >> 1) & 0x7FFFU) == 0;
            *cq_head = (*cq_head + 1) % NVME_QUEUE_DEPTH;
            if (*cq_head == 0) *cq_phase ^= 1U;
            nvme_ring_cq_db(qid, *cq_head);
            return ok;
        }
    }
    return false;
}

bool nvme_init(void) {
    if (g_nvme_present) return true;

    pci_device_t dev;
    if (!pci_find_class_prog_if(0x01, 0x08, 0x02, &dev)) {
        return false;
    }

    pci_enable_bus_master(&dev);
    uint64_t bar0 = pci_get_bar_addr(&dev, 0);
    if (!bar0) return false;

    g_nvme_mmio = (volatile uint8_t*)paging_map_mmio(bar0, 0x4000);
    if (!g_nvme_mmio) return false;

    uint64_t cap = nvme_r64(NVME_REG_CAP);
    g_db_stride = 4U << (uint32_t)((cap >> 32) & 0xF);

    // 1. Disabilita controller (CC.EN = 0) e attendi CSTS.RDY = 0
    uint32_t cc = nvme_r32(NVME_REG_CC);
    if (cc & 1U) {
        nvme_w32(NVME_REG_CC, cc & ~1U);
        for (int t = 0; t < 500; t++) {
            if ((nvme_r32(NVME_REG_CSTS) & 1U) == 0) break;
            timer_sleep(2);
        }
    }

    // 2. Alloca Admin Submission & Completion Queues
    uint64_t asq_phys = 0, acq_phys = 0;
    g_asq = (nvme_sqe_t*)dma_alloc(4096, 4096, &asq_phys);
    g_acq = (nvme_cqe_t*)dma_alloc(4096, 4096, &acq_phys);
    if (!g_asq || !g_acq) return false;
    g_asq_tail = 0;
    g_acq_head = 0;
    g_acq_phase = 1;

    nvme_w32(NVME_REG_AQA, ((NVME_QUEUE_DEPTH - 1U) << 16) | (NVME_QUEUE_DEPTH - 1U));
    nvme_w64(NVME_REG_ASQ, asq_phys);
    nvme_w64(NVME_REG_ACQ, acq_phys);

    // 3. Abilita controller con IOSQES=6 (64B) e IOCQES=4 (16B)
    uint32_t new_cc = (4U << 20) | (6U << 16) | 1U;
    nvme_w32(NVME_REG_CC, new_cc);
    bool rdy = false;
    for (int t = 0; t < 500; t++) {
        if (nvme_r32(NVME_REG_CSTS) & 1U) {
            rdy = true;
            break;
        }
        timer_sleep(2);
    }
    if (!rdy) return false;

    // 4. Identify Controller (CNS=1) & Identify Namespace 1 (CNS=0)
    uint64_t id_phys = 0;
    uint8_t* id_buf = (uint8_t*)dma_alloc(4096, 4096, &id_phys);
    if (!id_buf) return false;

    nvme_sqe_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = 0x06; // Admin Identify
    cmd.prp1 = id_phys;
    cmd.cdw10 = 1;   // CNS = Identify Controller
    if (nvme_submit_cmd(0, g_asq, g_acq, &g_asq_tail, &g_acq_head, &g_acq_phase, &cmd)) {
        int mlen = 0;
        for (int i = 0; i < 40; i++) {
            char ch = (char)id_buf[24 + i];
            if (ch >= 32 && ch <= 126) g_nvme_model[mlen++] = ch;
        }
        while (mlen > 0 && g_nvme_model[mlen - 1] == ' ') mlen--;
        if (mlen > 0) g_nvme_model[mlen] = '\0';
    }

    memset(id_buf, 0, 4096);
    memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = 0x06; // Admin Identify
    cmd.nsid = 1;
    cmd.prp1 = id_phys;
    cmd.cdw10 = 0;   // CNS = Identify Namespace
    if (nvme_submit_cmd(0, g_asq, g_acq, &g_asq_tail, &g_acq_head, &g_acq_phase, &cmd)) {
        g_nvme_sectors = *(uint64_t*)(id_buf + 0);
        uint8_t flbas = id_buf[26] & 0xF;
        uint32_t lbaf = *(uint32_t*)(id_buf + 128 + (uint32_t)flbas * 4);
        uint8_t lbads = (uint8_t)((lbaf >> 16) & 0xFF);
        if (lbads >= 9 && lbads <= 12) g_nvme_sector_sz = 1U << lbads;
    }
    kfree(id_buf);

    // 5. Crea I/O Completion Queue 1 (Opcode 0x05) & I/O Submission Queue 1 (Opcode 0x01)
    uint64_t iosq_phys = 0, iocq_phys = 0;
    g_iosq = (nvme_sqe_t*)dma_alloc(4096, 4096, &iosq_phys);
    g_iocq = (nvme_cqe_t*)dma_alloc(4096, 4096, &iocq_phys);
    if (!g_iosq || !g_iocq) return false;
    g_iosq_tail = 0;
    g_iocq_head = 0;
    g_iocq_phase = 1;

    memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = 0x05; // Create I/O CQ
    cmd.prp1 = iocq_phys;
    cmd.cdw10 = ((NVME_QUEUE_DEPTH - 1U) << 16) | 1U; // QSIZE | QID=1
    cmd.cdw11 = 1U; // PC=1 (Physically Contiguous)
    if (!nvme_submit_cmd(0, g_asq, g_acq, &g_asq_tail, &g_acq_head, &g_acq_phase, &cmd)) {
        return false;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = 0x01; // Create I/O SQ
    cmd.prp1 = iosq_phys;
    cmd.cdw10 = ((NVME_QUEUE_DEPTH - 1U) << 16) | 1U; // QSIZE | QID=1
    cmd.cdw11 = (1U << 16) | 1U; // CQID=1 | PC=1
    if (!nvme_submit_cmd(0, g_asq, g_acq, &g_asq_tail, &g_acq_head, &g_acq_phase, &cmd)) {
        return false;
    }

    g_nvme_present = true;
    return true;
}

bool nvme_is_present(void) { return g_nvme_present; }
uint64_t nvme_get_capacity_sectors(void) { return g_nvme_sectors; }
uint32_t nvme_get_sector_size(void) { return g_nvme_sector_sz; }
const char* nvme_get_model(void) { return g_nvme_model; }

static bool nvme_rw_chunk(bool is_write, uint64_t lba, uint32_t count, void* buffer) {
    if (!g_nvme_present || !buffer || count == 0) return false;
    uint32_t max_per_page = 4096U / g_nvme_sector_sz;
    if (max_per_page == 0) max_per_page = 1;

    uint64_t bounce_phys = 0;
    uint8_t* bounce = (uint8_t*)dma_alloc(4096, 4096, &bounce_phys);
    if (!bounce) return false;

    uint32_t done = 0;
    bool ok = true;
    while (done < count) {
        uint32_t chunk = count - done;
        if (chunk > max_per_page) chunk = max_per_page;
        uint32_t bytes = chunk * g_nvme_sector_sz;

        if (is_write) {
            memcpy(bounce, (const uint8_t*)buffer + done * g_nvme_sector_sz, bytes);
        }

        nvme_sqe_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.cdw0 = is_write ? 0x01U : 0x02U;
        cmd.nsid = 1;
        cmd.prp1 = bounce_phys;
        cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFFULL);
        cmd.cdw11 = (uint32_t)(lba >> 32);
        cmd.cdw12 = (chunk - 1U) & 0xFFFFU;

        if (!nvme_submit_cmd(1, g_iosq, g_iocq, &g_iosq_tail, &g_iocq_head, &g_iocq_phase, &cmd)) {
            ok = false;
            break;
        }
        if (!is_write) {
            memcpy((uint8_t*)buffer + done * g_nvme_sector_sz, bounce, bytes);
        }
        done += chunk;
        lba += chunk;
    }

    kfree(bounce);
    return ok;
}

bool nvme_read_sectors(uint64_t lba, uint32_t count, void* buffer) {
    return nvme_rw_chunk(false, lba, count, buffer);
}

bool nvme_write_sectors(uint64_t lba, uint32_t count, const void* buffer) {
    return nvme_rw_chunk(true, lba, count, (void*)buffer);
}

bool nvme_deallocate_sectors(uint64_t lba, uint32_t count) {
    if (!g_nvme_present || count == 0) return false;
    typedef struct __attribute__((packed)) {
        uint32_t cattr;
        uint32_t nlb;
        uint64_t slba;
    } nvme_dsm_range_t;

    uint64_t buf_phys = 0;
    uint8_t* buf = (uint8_t*)dma_alloc(4096, 4096, &buf_phys);
    if (!buf) return false;
    memset(buf, 0, 4096);

    nvme_dsm_range_t* rng = (nvme_dsm_range_t*)buf;
    rng->cattr = 0;
    rng->nlb = count;
    rng->slba = lba;

    nvme_sqe_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = 0x09U; // Dataset Management
    cmd.nsid = 1;
    cmd.prp1 = buf_phys;
    cmd.cdw10 = 0;          // 1 range (0-based)
    cmd.cdw11 = (1U << 2);  // AD (Attribute Deallocate)

    bool ok = nvme_submit_cmd(1, g_iosq, g_iocq, &g_iosq_tail, &g_iocq_head, &g_iocq_phase, &cmd);
    kfree(buf);
    return ok;
}
