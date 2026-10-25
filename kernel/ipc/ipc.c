#include <ipc/ipc.h>
#include <arch/x86_64/sched.h>
#include <mm/heap.h>
#include <string.h>

static ipc_msg_t g_msg_queue[IPC_MAX_MESSAGES];
static uint32_t  g_msg_count = 0;

void ipc_init(void) {
    memset(g_msg_queue, 0, sizeof(g_msg_queue));
    g_msg_count = 0;
}

pipe_t* ipc_pipe_create(uint32_t capacity) {
    if (capacity == 0) capacity = IPC_PIPE_DEFAULT_CAPACITY;

    pipe_t* p = (pipe_t*)kmalloc(sizeof(pipe_t));
    if (!p) return NULL;

    p->buffer = (uint8_t*)kmalloc(capacity);
    if (!p->buffer) {
        kfree(p);
        return NULL;
    }

    p->capacity = capacity;
    p->head = 0;
    p->tail = 0;
    p->count = 0;
    p->eof = false;

    return p;
}

void ipc_pipe_destroy(pipe_t* p) {
    if (!p) return;
    if (p->buffer) {
        kfree(p->buffer);
        p->buffer = NULL;
    }
    kfree(p);
}

int32_t ipc_pipe_write(pipe_t* p, const void* data, uint32_t len) {
    if (!p || !p->buffer || p->eof || !data || len == 0) return -1;

    const uint8_t* src = (const uint8_t*)data;
    uint32_t written = 0;

    while (written < len) {
        if (p->count >= p->capacity) {
            // Buffer pieno
            break;
        }

        p->buffer[p->head] = src[written++];
        p->head = (p->head + 1) % p->capacity;
        p->count++;
    }

    return (int32_t)written;
}

int32_t ipc_pipe_read(pipe_t* p, void* out_data, uint32_t len) {
    if (!p || !p->buffer || !out_data || len == 0) return -1;
    if (p->count == 0) {
        return p->eof ? 0 : -1;
    }

    uint8_t* dst = (uint8_t*)out_data;
    uint32_t read_bytes = 0;

    while (read_bytes < len && p->count > 0) {
        dst[read_bytes++] = p->buffer[p->tail];
        p->tail = (p->tail + 1) % p->capacity;
        p->count--;
    }

    return (int32_t)read_bytes;
}

uint32_t ipc_pipe_available(const pipe_t* p) {
    if (!p) return 0;
    return p->count;
}

void ipc_pipe_close(pipe_t* p) {
    if (p) {
        p->eof = true;
    }
}

bool ipc_pipe_is_eof(const pipe_t* p) {
    if (!p) return true;
    return p->eof && (p->count == 0);
}

bool ipc_send_msg(uint32_t target_pid, uint32_t msg_type, const void* data, uint32_t len) {
    if (g_msg_count >= IPC_MAX_MESSAGES) return false;

    task_t* cur = sched_get_current_task();
    uint32_t sender_pid = cur ? cur->pid : 0;

    ipc_msg_t* m = &g_msg_queue[g_msg_count];
    m->sender_pid = sender_pid;
    m->target_pid = target_pid;
    m->msg_type = msg_type;
    m->data_len = (len > IPC_MSG_PAYLOAD_MAX) ? IPC_MSG_PAYLOAD_MAX : len;

    if (data && m->data_len > 0) {
        memcpy(m->data, data, m->data_len);
    } else {
        memset(m->data, 0, sizeof(m->data));
    }

    g_msg_count++;
    return true;
}

bool ipc_recv_msg(uint32_t target_pid, ipc_msg_t* out_msg) {
    if (!out_msg || g_msg_count == 0) return false;

    for (uint32_t i = 0; i < g_msg_count; i++) {
        if (g_msg_queue[i].target_pid == target_pid) {
            *out_msg = g_msg_queue[i];

            // Compatta la coda rimuovendo il messaggio letto
            for (uint32_t j = i; j < g_msg_count - 1; j++) {
                g_msg_queue[j] = g_msg_queue[j + 1];
            }
            g_msg_count--;
            memset(&g_msg_queue[g_msg_count], 0, sizeof(ipc_msg_t));
            return true;
        }
    }

    return false;
}

uint32_t ipc_pending_msg_count(uint32_t target_pid) {
    uint32_t cnt = 0;
    for (uint32_t i = 0; i < g_msg_count; i++) {
        if (g_msg_queue[i].target_pid == target_pid) cnt++;
    }
    return cnt;
}
