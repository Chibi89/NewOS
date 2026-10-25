#ifndef ARCH_SCHED_H
#define ARCH_SCHED_H

#include <types.h>
#include <arch/x86_64/idt.h>

#define MAX_TASKS              16
#define TASK_STACK_SIZE        16384 // 16 KB per stack task
#define DEFAULT_TIME_SLICE     5     // 5 tick timer = 50ms (a 100 Hz)

typedef enum {
    TASK_STATE_UNUSED = 0,
    TASK_STATE_READY,
    TASK_STATE_RUNNING,
    TASK_STATE_BLOCKED,
    TASK_STATE_SLEEPING,
    TASK_STATE_TERMINATED
} task_state_t;

typedef struct {
    uint32_t         pid;
    char             name[32];
    task_state_t     state;
    uint64_t         rsp;             // Puntatore al frame salvato interrupt_frame_t
    uint8_t*         stack_base;      // Base dello stack allocato
    uint32_t         stack_size;
    uint64_t         sleep_until_ticks;
    uint32_t         priority;        // 1 (bassa) .. 5 (alta)
    uint32_t         time_slice;      // Tick rimanenti nel quanto attuale
    uint64_t         cr3;             // Registro di paginazione PML4 (isolamento memoria)
    uint64_t         total_runtime;   // Numero totale di quanti eseguiti
} task_t;

// Inizializzazione dello Scheduler preemptivo
void sched_init(void);
bool sched_is_active(void);

// Creazione e terminazione task
uint32_t task_create(const char* name, void (*entry_point)(void*), void* arg, uint32_t priority);
void     task_exit(void);
bool     task_kill(uint32_t pid);

// Cambio di contesto e gestione CPU
interrupt_frame_t* sched_schedule(interrupt_frame_t* current_frame);
void               sched_yield(void);
void               sched_sleep(uint32_t ms);

// Informazioni e diagnostica di sistema
task_t*       sched_get_current_task(void);
int           sched_get_task_count(void);
const task_t* sched_get_task(int index);
const char*   sched_state_to_str(task_state_t state);

#endif // ARCH_SCHED_H
