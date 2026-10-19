; ==============================================================================
; NewOS V1 64-bit - Kernel Entry Point
; ==============================================================================

[BITS 64]

global kernel_entry
extern kernel_main

section .text
kernel_entry:
    ; Configura registri di segmento dati 64-bit (0x20)
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Imposta lo stack del kernel a 0x200000 con allineamento a 16 byte
    mov rsp, 0x200000
    and rsp, -16

    ; Abilita FPU e SSE (CR0.EM=0, CR0.MP=1, CR4.OSFXSR=1, CR4.OSXMMEXCPT=1)
    mov rax, cr0
    and ax, 0xFFFB      ; Clear EM (bit 2)
    or ax, 0x0002       ; Set MP (bit 1)
    mov cr0, rax

    mov rax, cr4
    or ax, 3 << 9       ; Set OSFXSR (bit 9) e OSXMMEXCPT (bit 10)
    mov cr4, rax

    fninit

    ; Il registro RDI contiene già il puntatore a BootInfo (passato da Stage 2)
    ; Secondo la System V AMD64 ABI, RDI è il primo parametro di kernel_main
    cld
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang
