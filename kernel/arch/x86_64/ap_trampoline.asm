[BITS 16]
section .text
global ap_trampoline_start
global ap_trampoline_end

ap_trampoline_start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; Carica la GDT per il bootloader AP
    lgdt [cs:ap_gdt_desc - ap_trampoline_start]

    ; Attiva Protected Mode (PE bit 0 in CR0)
    mov eax, cr0
    or al, 1
    mov cr0, eax

    ; Far jump a 32-bit (0x08: Selettore 32-bit code)
    jmp dword 0x08:(0x8000 + (ap_pm32 - ap_trampoline_start))

    align 16
[BITS 32]
ap_pm32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x7C00

    ; Carica PML4 di boot del kernel a 0x2000 in CR3
    mov eax, 0x2000
    mov cr3, eax

    ; Abilita PAE in CR4 (bit 5)
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; Abilita Long Mode in IA32_EFER MSR (0xC0000080, bit 8)
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    ; Abilita Paging (PG bit 31 in CR0)
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    ; Far jump a 64-bit Long Mode (0x18: Selettore 64-bit code)
    jmp 0x18:(0x8000 + (ap_lm64 - ap_trampoline_start))

    align 16
ap_gdt_table:
    dq 0x0000000000000000 ; 0x00: Null
    ; 0x08: 32-bit Code (Base 0, Limit 4GB)
    dw 0xFFFF, 0x0000
    db 0x00, 0x9A, 0xCF, 0x00
    ; 0x10: 32-bit Data (Base 0, Limit 4GB)
    dw 0xFFFF, 0x0000
    db 0x00, 0x92, 0xCF, 0x00
    ; 0x18: 64-bit Code
    dq 0x00209A0000000000
    ; 0x20: 64-bit Data
    dq 0x0000920000000000
ap_gdt_end:

ap_gdt_desc:
    dw (ap_gdt_end - ap_gdt_table - 1)
    dd 0x8000 + (ap_gdt_table - ap_trampoline_start)

    align 16
[BITS 64]
ap_lm64:
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Incrementa atomicamente il contatore dei core AP online a 0x8500
    mov rdi, 0x8500
    lock inc dword [rdi]

.ap_idle_loop:
    hlt
    jmp .ap_idle_loop

ap_trampoline_end:
