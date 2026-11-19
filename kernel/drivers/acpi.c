#include <drivers/acpi.h>
#include <arch/x86_64/io.h>
#include <string.h>

typedef struct {
    char     signature[8];
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_addr;
    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t  ext_checksum;
    uint8_t  reserved[3];
} __attribute__((packed)) acpi_rsdp_t;

typedef struct {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_sdt_hdr_t;

typedef struct {
    uint8_t  addr_space_id;
    uint8_t  reg_bit_width;
    uint8_t  reg_bit_offset;
    uint8_t  access_size;
    uint64_t address;
} __attribute__((packed)) acpi_gas_t;

static acpi_info_t g_acpi;
static bool        g_acpi_inited = false;

static bool acpi_checksum_ok(const void* ptr, uint32_t len) {
    const uint8_t* p = (const uint8_t*)ptr;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; i++) sum += p[i];
    return (sum == 0);
}

static const acpi_rsdp_t* acpi_find_rsdp_in_range(uint64_t start, uint64_t end) {
    for (uint64_t addr = start; addr + sizeof(acpi_rsdp_t) <= end; addr += 16) {
        const acpi_rsdp_t* r = (const acpi_rsdp_t*)addr;
        if (memcmp(r->signature, "RSD PTR ", 8) == 0) {
            if (acpi_checksum_ok(r, 20)) {
                if (r->revision >= 2 && r->length >= sizeof(acpi_rsdp_t)) {
                    if (acpi_checksum_ok(r, r->length)) return r;
                } else {
                    return r;
                }
            }
        }
    }
    return NULL;
}

static const acpi_rsdp_t* acpi_find_rsdp(void) {
    uint16_t ebda_seg = *(const volatile uint16_t*)0x040EULL;
    uint64_t ebda_addr = ((uint64_t)ebda_seg) << 4;
    if (ebda_addr >= 0x80000ULL && ebda_addr < 0xA0000ULL) {
        const acpi_rsdp_t* r = acpi_find_rsdp_in_range(ebda_addr, ebda_addr + 1024);
        if (r) return r;
    }
    return acpi_find_rsdp_in_range(0xE0000ULL, 0x100000ULL);
}

static void acpi_parse_s5_from_dsdt(uint64_t dsdt_addr) {
    if (!dsdt_addr) return;
    const acpi_sdt_hdr_t* hdr = (const acpi_sdt_hdr_t*)dsdt_addr;
    if (memcmp(hdr->signature, "DSDT", 4) != 0 || hdr->length < sizeof(acpi_sdt_hdr_t)) return;
    if (hdr->length > 512U * 1024U) return;

    const uint8_t* p = (const uint8_t*)dsdt_addr + sizeof(acpi_sdt_hdr_t);
    uint32_t len = hdr->length - sizeof(acpi_sdt_hdr_t);

    for (uint32_t i = 0; i + 8 < len; i++) {
        if (p[i] == '_' && p[i + 1] == 'S' && p[i + 2] == '5' && p[i + 3] == '_') {
            // Verifica che sia una definizione NameOp (0x08) prima di "_S5_"
            if (i > 0 && p[i - 1] == 0x08) {
                uint32_t off = i + 4;
                if (off < len && p[off] == 0x12) { // PackageOp
                    off++;
                    uint8_t pkg_lead = p[off++];
                    uint32_t skip_len = (pkg_lead >> 6) & 0x3;
                    off += skip_len;
                    if (off + 2 < len) {
                        off++; // NumElements
                        if (p[off] == 0x0A) off++; // BytePrefix
                        g_acpi.slp_typa = (uint16_t)(p[off++] & 0x7) << 10;
                        if (off < len) {
                            if (p[off] == 0x0A) off++;
                            g_acpi.slp_typb = (uint16_t)(p[off] & 0x7) << 10;
                        } else {
                            g_acpi.slp_typb = g_acpi.slp_typa;
                        }
                        g_acpi.has_s5 = true;
                        return;
                    }
                }
            }
        }
    }
}

static void acpi_parse_fadt(const acpi_sdt_hdr_t* hdr) {
    if (!hdr || hdr->length < 80) return;
    const uint8_t* raw = (const uint8_t*)hdr;
    uint32_t dsdt32 = *(const uint32_t*)(raw + 40);
    g_acpi.pm1a_cnt_blk = *(const uint32_t*)(raw + 64);
    g_acpi.pm1b_cnt_blk = *(const uint32_t*)(raw + 68);

    uint64_t dsdt_addr = dsdt32;
    if (hdr->length >= 148) {
        uint64_t x_dsdt = *(const uint64_t*)(raw + 140);
        if (x_dsdt) dsdt_addr = x_dsdt;
    }
    acpi_parse_s5_from_dsdt(dsdt_addr);

    if (hdr->length >= 129) {
        uint32_t flags = *(const uint32_t*)(raw + 112);
        if (flags & (1U << 10)) { // RESET_REG_SUP
            const acpi_gas_t* rg = (const acpi_gas_t*)(raw + 116);
            g_acpi.has_reset_reg = true;
            g_acpi.reset_addr_space = rg->addr_space_id;
            g_acpi.reset_addr = rg->address;
            g_acpi.reset_value = raw[128];
        }
    }
}

static void acpi_parse_madt(const acpi_sdt_hdr_t* hdr) {
    if (!hdr || hdr->length < 44) return;
    const uint8_t* raw = (const uint8_t*)hdr;
    g_acpi.lapic_addr = *(const uint32_t*)(raw + 36);

    uint32_t off = 44;
    while (off + 2 <= hdr->length) {
        uint8_t type = raw[off];
        uint8_t rlen = raw[off + 1];
        if (rlen < 2 || off + rlen > hdr->length) break;

        if (type == 0 && rlen >= 8) { // Processor Local APIC
            uint32_t apic_flags = *(const uint32_t*)(raw + off + 4);
            if ((apic_flags & 1U) && g_acpi.cpu_count < ACPI_MAX_CPUS) {
                g_acpi.cpu_apic_ids[g_acpi.cpu_count++] = raw[off + 3];
            }
        } else if (type == 1 && rlen >= 12) { // I/O APIC
            if (g_acpi.ioapic_addr == 0) {
                g_acpi.ioapic_addr = *(const uint32_t*)(raw + off + 4);
            }
        } else if (type == 5 && rlen >= 12) { // Local APIC Address Override
            g_acpi.lapic_addr = *(const uint64_t*)(raw + off + 4);
        }
        off += rlen;
    }
}

bool acpi_init(void) {
    if (g_acpi_inited) return g_acpi.available;
    g_acpi_inited = true;
    memset(&g_acpi, 0, sizeof(g_acpi));

    const acpi_rsdp_t* rsdp = acpi_find_rsdp();
    if (!rsdp) return false;

    g_acpi.revision = rsdp->revision;
    bool use_xsdt = (rsdp->revision >= 2 && rsdp->xsdt_addr != 0);
    const acpi_sdt_hdr_t* root = (const acpi_sdt_hdr_t*)(use_xsdt ? rsdp->xsdt_addr : (uint64_t)rsdp->rsdt_addr);
    if (!root || root->length < sizeof(acpi_sdt_hdr_t)) return false;
    if (!acpi_checksum_ok(root, root->length)) return false;

    g_acpi.available = true;
    uint32_t entry_sz = use_xsdt ? 8 : 4;
    uint32_t count = (root->length - sizeof(acpi_sdt_hdr_t)) / entry_sz;
    const uint8_t* entries = (const uint8_t*)root + sizeof(acpi_sdt_hdr_t);

    for (uint32_t i = 0; i < count; i++) {
        uint64_t addr = use_xsdt
            ? *(const uint64_t*)(entries + i * 8)
            : (uint64_t)(*(const uint32_t*)(entries + i * 4));
        if (!addr) continue;
        const acpi_sdt_hdr_t* tbl = (const acpi_sdt_hdr_t*)addr;
        if (tbl->length < sizeof(acpi_sdt_hdr_t) || !acpi_checksum_ok(tbl, tbl->length)) continue;

        if (memcmp(tbl->signature, "FACP", 4) == 0) {
            acpi_parse_fadt(tbl);
        } else if (memcmp(tbl->signature, "APIC", 4) == 0) {
            acpi_parse_madt(tbl);
        }
    }
    return true;
}

const acpi_info_t* acpi_get_info(void) {
    if (!g_acpi_inited) acpi_init();
    return &g_acpi;
}

bool acpi_shutdown(void) {
    if (!g_acpi_inited) acpi_init();
    if (!g_acpi.available || !g_acpi.pm1a_cnt_blk || !g_acpi.has_s5) return false;

    uint16_t SLP_EN = (1U << 13);
    outw((uint16_t)g_acpi.pm1a_cnt_blk, g_acpi.slp_typa | SLP_EN);
    if (g_acpi.pm1b_cnt_blk) {
        outw((uint16_t)g_acpi.pm1b_cnt_blk, g_acpi.slp_typb | SLP_EN);
    }
    return true;
}

bool acpi_reboot(void) {
    if (!g_acpi_inited) acpi_init();
    if (!g_acpi.available || !g_acpi.has_reset_reg || !g_acpi.reset_addr) return false;
    if (g_acpi.reset_addr_space == 1) { // System I/O
        outb((uint16_t)g_acpi.reset_addr, g_acpi.reset_value);
        return true;
    } else if (g_acpi.reset_addr_space == 0) { // System Memory
        *(volatile uint8_t*)g_acpi.reset_addr = g_acpi.reset_value;
        return true;
    }
    return false;
}
