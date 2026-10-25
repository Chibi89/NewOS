#include <arch/x86_64/sched.h>
#include <drivers/timer.h>
#include <mm/heap.h>
#include <string.h>

static task_t   g_tasks[MAX_TASKS];
static int      g_current_task_idx = 0;
static bool     g_sched_active = false;
static uint32_t g_next_pid = 1;

const char* sched_state_to_str(task_state_t state) {
    switch (state) {
        case TASK_STATE_READY:      return "READY";
        case TASK_STATE_RUNNING:    return "RUNNING";
        case TASK_STATE_BLOCKED:    return "BLOCKED";
        case TASK_STATE_SLEEPING:   return "SLEEP";
        case TASK_STATE_TERMINATED: return "DEAD";
        default:                    return "UNUSED";
    }
}

bool sched_is_active(void) {
    return g_sched_active;
}

task_t* sched_get_current_task(void) {
    if (!g_sched_active) return NULL;
    return &g_tasks[g_current_task_idx];
}

int sched_get_task_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (g_tasks[i].state != TASK_STATE_UNUSED) count++;
    }
    return count;
}

const task_t* sched_get_task(int index) {
    if (index >= 0 && index < MAX_TASKS) {
        return &g_tasks[index];
    }
    return NULL;
}

static void task_trampoline(void (*fn)(void*), void* arg) {
    if (fn) {
        fn(arg);
    }
    task_exit();
}

void sched_init(void) {
    memset(g_tasks, 0, sizeof(g_tasks));
    g_current_task_idx = 0;
    g_next_pid = 1;

    // Task 0: Thread Principale del Kernel e Window Manager
    task_t* main_task = &g_tasks[0];
    main_task->pid = 0;
    strncpy(main_task->name, "Kernel/Desktop", sizeof(main_task->name) - 1);
    main_task->state = TASK_STATE_RUNNING;
    main_task->priority = 3;
    main_task->time_slice = DEFAULT_TIME_SLICE;
    main_task->stack_base = NULL; // Usa lo stack nativo del kernel 0x200000
    main_task->stack_size = 0;
    main_task->total_runtime = 0;

    // Lettura del CR3 iniziale del kernel
    uint64_t cr3_val = 0;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3_val));
    main_task->cr3 = cr3_val;

    g_sched_active = true;
}

uint32_t task_create(const char* name, void (*entry_point)(void*), void* arg, uint32_t priority) {
    if (!entry_point) return 0;

    // Trova uno slot libero
    int slot = -1;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (g_tasks[i].state == TASK_STATE_UNUSED || g_tasks[i].state == TASK_STATE_TERMINATED) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return 0; // Tabella processi piena

    task_t* t = &g_tasks[slot];
    if (t->stack_base) {
        kfree(t->stack_base);
        t->stack_base = NULL;
    }

    t->stack_base = (uint8_t*)kmalloc(TASK_STACK_SIZE);
    if (!t->stack_base) return 0;
    t->stack_size = TASK_STACK_SIZE;

    t->pid = g_next_pid++;
    strncpy(t->name, name ? name : "Task", sizeof(t->name) - 1);
    t->name[sizeof(t->name) - 1] = '\0';
    t->priority = (priority == 0) ? 3 : priority;
    t->time_slice = DEFAULT_TIME_SLICE;
    t->sleep_until_ticks = 0;
    t->total_runtime = 0;
    t->cr3 = g_tasks[0].cr3;

    // Allineamento stack a 16-byte
    uint8_t* stack_top = t->stack_base + TASK_STACK_SIZE;
    stack_top = (uint8_t*)((uintptr_t)stack_top & ~0x0FULL);

    // Riserva 8 byte per simulare il return RIP di una CALL (System V ABI: RSP % 16 == 8 all'entry)
    stack_top -= 8;
    *(uint64_t*)stack_top = 0;
    uint64_t initial_rsp = (uint64_t)stack_top;

    // Alloca lo spazio per l'interrupt frame iniziale
    stack_top -= sizeof(interrupt_frame_t);
    interrupt_frame_t* f = (interrupt_frame_t*)stack_top;
    memset(f, 0, sizeof(interrupt_frame_t));

    // Selettori segmento GDT per Long Mode a 64-bit (Code: 0x18, Data: 0x20)
    f->ss = 0x20;
    f->rsp = initial_rsp;
    f->rflags = 0x202; // IF=1 (Interrupt abilitati)
    f->cs = 0x18;
    f->rip = (uint64_t)task_trampoline;
    f->int_no = 32;
    f->err_code = 0;

    // Argomenti secondo System V ABI: RDI = 1° arg (fn), RSI = 2° arg (arg)
    f->rdi = (uint64_t)entry_point;
    f->rsi = (uint64_t)arg;

    t->rsp = (uint64_t)stack_top;
    t->state = TASK_STATE_READY;

    return t->pid;
}

void task_exit(void) {
    if (!g_sched_active) return;
    task_t* cur = &g_tasks[g_current_task_idx];
    if (g_current_task_idx != 0) {
        cur->state = TASK_STATE_TERMINATED;
    }
    sched_yield();
    // Non dovrebbe mai ritornare qui
    while (1) {
        __asm__ volatile ("hlt");
    }
}

bool task_kill(uint32_t pid) {
    if (pid == 0 || !g_sched_active) return false;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (g_tasks[i].pid == pid && g_tasks[i].state != TASK_STATE_UNUSED) {
            g_tasks[i].state = TASK_STATE_TERMINATED;
            return true;
        }
    }
    return false;
}

interrupt_frame_t* sched_schedule(interrupt_frame_t* current_frame) {
    if (!g_sched_active) return current_frame;

    uint64_t now_ticks = timer_get_ticks();
    task_t* cur = &g_tasks[g_current_task_idx];
    cur->total_runtime++;

    // 1. Risveglio dei task in stato di sleep
    for (int i = 0; i < MAX_TASKS; i++) {
        if (g_tasks[i].state == TASK_STATE_SLEEPING) {
            if (now_ticks >= g_tasks[i].sleep_until_ticks) {
                g_tasks[i].state = TASK_STATE_READY;
            }
        }
    }

    // 2. Controllo quanto temporale residuo (time-slice)
    if (cur->time_slice > 0 && cur->state == TASK_STATE_RUNNING) {
        cur->time_slice--;
        return current_frame; // Continua l'esecuzione del task corrente
    }

    // 3. Salva lo stack pointer (frame) del task corrente
    cur->rsp = (uint64_t)current_frame;
    if (cur->state == TASK_STATE_RUNNING) {
        cur->state = TASK_STATE_READY;
    }

    // 4. Selezione Round-Robin del prossimo task READY
    int next_idx = -1;
    for (int i = 1; i <= MAX_TASKS; i++) {
        int candidate = (g_current_task_idx + i) % MAX_TASKS;
        if (g_tasks[candidate].state == TASK_STATE_READY) {
            next_idx = candidate;
            break;
        }
    }

    if (next_idx < 0) {
        // Nessun altro task pronto: riprende il task 0 (Desktop/Kernel) o quello corrente
        if (g_tasks[0].state == TASK_STATE_READY || g_tasks[0].state == TASK_STATE_RUNNING) {
            next_idx = 0;
        } else {
            next_idx = g_current_task_idx;
        }
    }

    g_current_task_idx = next_idx;
    task_t* next_task = &g_tasks[next_idx];
    next_task->state = TASK_STATE_RUNNING;
    next_task->time_slice = DEFAULT_TIME_SLICE;

    // Cambio opzionale dello spazio di indirizzamento PML4 (Paging CR3)
    if (next_task->cr3 && next_task->cr3 != cur->cr3) {
        __asm__ volatile ("mov %0, %%cr3" : : "r"(next_task->cr3));
    }

    return (interrupt_frame_t*)next_task->rsp;
}

void sched_yield(void) {
    if (!g_sched_active) return;
    task_t* cur = &g_tasks[g_current_task_idx];
    cur->time_slice = 0;
    __asm__ volatile ("int $32");
}

void sched_sleep(uint32_t ms) {
    if (!g_sched_active) {
        timer_sleep(ms);
        return;
    }
    task_t* cur = &g_tasks[g_current_task_idx];
    cur->state = TASK_STATE_SLEEPING;
    cur->sleep_until_ticks = timer_get_ticks() + ((uint64_t)ms * 100) / 1000;
    sched_yield();
}
