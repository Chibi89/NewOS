#ifndef ARCH_X86_64_SYSCALL_H
#define ARCH_X86_64_SYSCALL_H

#include <types.h>

// Numeri di Syscall NewOS V1 (System Call Table)
#define SYS_YIELD       1
#define SYS_SLEEP       2
#define SYS_GETPID      3
#define SYS_WRITE       4
#define SYS_READ        5
#define SYS_EXIT        6
#define SYS_IPC_SEND    7
#define SYS_IPC_RECV    8
#define SYS_VFS_OPEN    9
#define SYS_VFS_READ    10
#define SYS_VFS_WRITE   11
#define SYS_VFS_CLOSE   12

// Inizializza l'architettura delle Syscall (MSR IA32_EFER, IA32_STAR, IA32_LSTAR, IA32_FMASK e Gate int 0x80)
void syscall_init(void);

// Dispatcher principale invocato dall'assembly stub
int64_t syscall_dispatch(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);

// Wrapper C per effettuare una syscall 64-bit nativa (istruzione SYSCALL)
static inline int64_t sys_call(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3) {
    int64_t ret;
    register uint64_t r10 __asm__("r10") = a3;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// Wrapper C per effettuare una syscall via Interrupt Gate 0x80 (INT 0x80)
static inline int64_t sys_call_int80(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3) {
    int64_t ret;
    register uint64_t r10 __asm__("r10") = a3;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "r"(r10)
        : "memory"
    );
    return ret;
}

#endif // ARCH_X86_64_SYSCALL_H
