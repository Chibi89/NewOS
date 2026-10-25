; ==============================================================================
; NewOS V1 64-bit - Syscall Assembly Entry Stub (MSR IA32_LSTAR)
; ==============================================================================

[BITS 64]
global syscall_entry
extern syscall_dispatch

syscall_entry:
    ; Quando viene eseguita l'istruzione SYSCALL:
    ; RCX = RIP dell'istruzione successiva
    ; R11 = RFLAGS originale
    ; RAX = Numero di Syscall (sys_num)
    ; RDI = Argomento 1
    ; RSI = Argomento 2
    ; RDX = Argomento 3
    ; R10 = Argomento 4
    ; R8  = Argomento 5
    ; R9  = Argomento 6

    ; Salvataggio registri callee-saved e contesti CPU
    push rcx
    push r11
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; Predisposizione argomenti per System V ABI:
    ; syscall_dispatch(sys_num, arg1, arg2, arg3, arg4, arg5)
    ; RDI = sys_num (RAX)
    ; RSI = arg1 (RDI originale)
    ; RDX = arg2 (RSI originale)
    ; RCX = arg3 (RDX originale)
    ; R8  = arg4 (R10 originale)
    ; R9  = arg5 (R8 originale)
    mov r9, r8
    mov r8, r10
    mov rcx, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax

    cld
    call syscall_dispatch

    ; RAX contiene ora il valore di ritorno della syscall

    ; Ripristino registri salvati
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    pop r11
    pop rcx

    ; Ritorno immediato allo user-space o kernel caller tramite SYSRETQ
    o64 sysret
