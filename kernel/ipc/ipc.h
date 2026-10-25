#ifndef KERNEL_IPC_H
#define KERNEL_IPC_H

#include <types.h>

#define IPC_PIPE_DEFAULT_CAPACITY 4096
#define IPC_MAX_MESSAGES          32
#define IPC_MSG_PAYLOAD_MAX       64

// Struttura Pipe a Stream Circolare (FIFO)
typedef struct {
    uint8_t* buffer;
    uint32_t capacity;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    bool     eof;
} pipe_t;

// Struttura Messaggio IPC orientato a pacchetti
typedef struct {
    uint32_t sender_pid;
    uint32_t target_pid;
    uint32_t msg_type;
    uint32_t data_len;
    uint8_t  data[IPC_MSG_PAYLOAD_MAX];
} ipc_msg_t;

// Inizializzazione sottosistema IPC
void     ipc_init(void);

// Gestione Canali Pipe a Flusso Continuo
pipe_t*  ipc_pipe_create(uint32_t capacity);
void     ipc_pipe_destroy(pipe_t* p);
int32_t  ipc_pipe_write(pipe_t* p, const void* data, uint32_t len);
int32_t  ipc_pipe_read(pipe_t* p, void* out_data, uint32_t len);
uint32_t ipc_pipe_available(const pipe_t* p);
void     ipc_pipe_close(pipe_t* p);
bool     ipc_pipe_is_eof(const pipe_t* p);

// Gestione Code Messaggi Inter-Task
bool     ipc_send_msg(uint32_t target_pid, uint32_t msg_type, const void* data, uint32_t len);
bool     ipc_recv_msg(uint32_t target_pid, ipc_msg_t* out_msg);
uint32_t ipc_pending_msg_count(uint32_t target_pid);

#endif // KERNEL_IPC_H
