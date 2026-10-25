#include <arch/x86_64/syscall.h>
#include <arch/x86_64/cpu.h>
#include <arch/x86_64/sched.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/io.h>
#include <ipc/ipc.h>
#include <fs/vfs.h>
#include <string.h>

extern void syscall_entry(void);
extern void isr128(void);

#define MSR_EFER   0xC0000080
#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_FMASK  0xC0000084

static inline uint64_t rdmsr64(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr64(uint32_t msr, uint64_t val) {
    uint32_t lo = (uint32_t)(val & 0xFFFFFFFF);
    uint32_t hi = (uint32_t)(val >> 32);
    __asm__ volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

static uint64_t g_syscall_count = 0;

int64_t syscall_dispatch(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    (void)arg4;
    (void)arg5;
    g_syscall_count++;

    switch (sys_num) {
        case SYS_YIELD:
            sched_yield();
            return 0;

        case SYS_SLEEP:
            sched_sleep((uint32_t)arg1);
            return 0;

        case SYS_GETPID: {
            const task_t* cur = sched_get_current_task();
            return cur ? (int64_t)cur->pid : 0;
        }

        case SYS_WRITE: {
            // arg1 = fd/canale (1=stdout, 2=stderr, 3=seriale COM1), arg2 = buffer, arg3 = lunghezza
            const char* buf = (const char*)arg2;
            uint32_t len = (uint32_t)arg3;
            if (!buf) return -1;
            for (uint32_t i = 0; i < len; i++) {
                outb(0x3F8, buf[i]); // Emetti su seriale/console
            }
            return (int64_t)len;
        }

        case SYS_EXIT: {
            const task_t* cur = sched_get_current_task();
            if (cur && cur->pid != 0) {
                task_kill(cur->pid);
                sched_yield();
            }
            return 0;
        }

        case SYS_IPC_SEND: {
            uint32_t target_pid = (uint32_t)arg1;
            uint32_t msg_type = (uint32_t)arg2;
            const void* data = (const void*)arg3;
            uint32_t len = (uint32_t)arg4;
            return ipc_send_msg(target_pid, msg_type, data, len) ? 0 : -1;
        }

        case SYS_IPC_RECV: {
            ipc_msg_t* out_msg = (ipc_msg_t*)arg1;
            if (!out_msg) return -1;
            const task_t* cur = sched_get_current_task();
            uint32_t my_pid = cur ? cur->pid : 0;
            return ipc_recv_msg(my_pid, out_msg) ? 0 : -1;
        }

        case SYS_VFS_OPEN:
            return (int64_t)vfs_open((const char*)arg1);

        case SYS_VFS_READ:
            return (int64_t)vfs_read((vfs_file_t*)arg1, (void*)arg2, (uint32_t)arg3);

        case SYS_VFS_WRITE:
            return (int64_t)vfs_write((vfs_file_t*)arg1, (const void*)arg2, (uint32_t)arg3);

        case SYS_VFS_CLOSE:
            vfs_close((vfs_file_t*)arg1);
            return 0;

        default:
            return -1; // Syscall non supportata
    }
}

static void syscall_int80_handler(interrupt_frame_t* frame) {
    if (!frame) return;
    // Interfaccia int 0x80:
    // RAX = sys_num, RDI = arg1, RSI = arg2, RDX = arg3, R10 = arg4, R8 = arg5
    int64_t res = syscall_dispatch(frame->rax, frame->rdi, frame->rsi, frame->rdx, frame->r10, frame->r8);
    frame->rax = (uint64_t)res;
}

void syscall_init(void) {
    // 1. Abilita Syscall Extensions (SCE bit 0) in MSR IA32_EFER
    uint64_t efer = rdmsr64(MSR_EFER);
    wrmsr64(MSR_EFER, efer | 1);

    // 2. Configura IA32_STAR:
    // Bits 47:32 = Kernel CS (0x18), Kernel SS implicito (0x20)
    // Bits 63:48 = User Base Selector (0x20 | 3 -> SYSRET carichera' SS = 0x28, CS = 0x30)
    uint64_t star = ((uint64_t)(0x20 | 3) << 48) | ((uint64_t)0x18 << 32);
    wrmsr64(MSR_STAR, star);

    // 3. Configura IA32_LSTAR con il puntatore all'entry point assembly a 64-bit
    wrmsr64(MSR_LSTAR, (uint64_t)syscall_entry);

    // 4. Configura IA32_FMASK: maschera bit 9 (IF, Interrupt Flag) all'ingresso della syscall
    wrmsr64(MSR_FMASK, 0x200);

    // 5. Configura IDT Gate 0x80 (128) con DPL=3 (accessibile anche da Ring 3 User Mode)
    idt_set_gate(128, (uint64_t)isr128, 0x18, 0xEE);
    register_interrupt_handler(128, syscall_int80_handler);
}
