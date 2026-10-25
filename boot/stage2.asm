; ==============================================================================
; NewOS V1 64-bit - Stage 2 Bootloader
; (C) Newspoint Software - NewSoft - NewOS V1
; - Rilevamento memoria (E820)
; - Configurazione VESA VBE (Linear Framebuffer 32bpp)
; - Abilitazione A20
; - Transizione: 16-bit Real Mode -> 32-bit Protected Mode -> 64-bit Long Mode
; - Setup Paging a 4 livelli (PML4 -> PDPT -> PD 2MB Huge Pages, 4GB Identity)
; - Caricamento Kernel a 0x100000 (1MB) e passaggio parametri
; ==============================================================================

[BITS 16]
[ORG 0x0000]

stage2_entry:
    cli
    mov ax, 0x1000
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0xFFF0
    sti

    ; SALVA IMMEDIATAMENTE IL DRIVE DI BOOT DL (prima che DX venga modificato!)
    mov [boot_drive], dl

    mov dx, 0x3F8
    mov al, '2'
    out dx, al


    call detect_memory
    mov dx, 0x3F8
    mov al, 'd'
    out dx, al

    call setup_vbe
    mov dx, 0x3F8
    mov al, 'v'
    out dx, al

    call load_kernel_disk
    mov dx, 0x3F8
    mov al, 'k'
    out dx, al

    call enable_a20
    mov dx, 0x3F8
    mov al, 'a'
    out dx, al

    cli
    lgdt [gdt_descriptor]
    mov dx, 0x3F8
    mov al, 'g'
    out dx, al

    ; 6. Transizione a 32-bit Protected Mode (SOLO PE=1, Paging spento)
    mov eax, cr0
    or eax, 1               ; PE bit = 1
    mov cr0, eax

    ; Far jump nel segmento di codice a 32-bit (0x08)
    jmp dword 0x08:(0x10000 + pm32_entry)

; ------------------------------------------------------------------------------
; Rilevamento memoria E820
; ------------------------------------------------------------------------------
detect_memory:
    xor ebx, ebx
    xor ax, ax
    mov es, ax
    mov di, 0x9100          ; Buffer temporaneo a 0x0000:0x9100

    mov dword [es:0x9018], 0 ; total_memory_bytes low
    mov dword [es:0x901C], 0 ; total_memory_bytes high

.e820_loop:
    mov eax, 0xE820
    mov edx, 0x534D4150     ; 'SMAP'
    mov ecx, 24
    int 0x15
    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done

    cmp dword [es:di + 16], 1 ; Type 1 = Usable RAM
    jne .skip_add
    mov eax, [es:di + 8]      ; Length low
    add [es:0x9018], eax
    mov eax, [es:di + 12]     ; Length high
    adc [es:0x901C], eax

.skip_add:
    test ebx, ebx
    jz .e820_done
    add di, 24
    jmp .e820_loop

.e820_done:
    cmp dword [es:0x9018], 0
    jne .ret_mem
    mov dword [es:0x9018], 0x10000000 ; Fallback 256 MB
.ret_mem:
    mov ax, 0x1000
    mov es, ax
    ret

; ------------------------------------------------------------------------------
; Inizializzazione VBE (1024x768x32 LFB, fallback 800x600x32)
; ------------------------------------------------------------------------------
setup_vbe:
    mov ax, 0x1000
    mov es, ax
    mov di, vbe_mode_info

    ; Prova prima modalità 0x144 (1024x768x32bpp LFB)
    mov ax, 0x4F01
    mov cx, 0x144
    int 0x10
    cmp ax, 0x004F
    jne .try_118
    mov bx, 0x4144          ; Bit 14 = LFB
    jmp .apply_mode

.try_118:
    ; Prova modalità 0x118 (1024x768x24bpp LFB)
    mov ax, 0x4F01
    mov cx, 0x118
    int 0x10
    cmp ax, 0x004F
    je .use_118

    ; Fallback a 0x115 (800x600 LFB)
    mov ax, 0x4F01
    mov cx, 0x115
    int 0x10
    mov bx, 0x4115
    jmp .apply_mode

.use_118:
    mov bx, 0x4118          ; Bit 14 = LFB (Linear Frame Buffer)

.apply_mode:
    mov [selected_vbe_mode], bx

    ; Salva informazioni video nella struttura BootInfo (fisico 0x9000, ES=0)
    xor ax, ax
    mov es, ax

    mov eax, [ds:vbe_mode_info + 0x28] ; PhysBasePtr (Framebuffer)
    mov [es:0x9000], eax
    mov dword [es:0x9004], 0           ; High 32 bits = 0

    movzx eax, word [ds:vbe_mode_info + 0x12] ; XResolution (Width)
    mov [es:0x9008], eax

    movzx eax, word [ds:vbe_mode_info + 0x14] ; YResolution (Height)
    mov [es:0x900C], eax

    movzx eax, word [ds:vbe_mode_info + 0x10] ; BytesPerScanLine (Pitch)
    mov [es:0x9010], eax

    movzx eax, byte [ds:vbe_mode_info + 0x19] ; BitsPerPixel (BPP)
    mov [es:0x9014], eax

    ; Imposta la modalità video VBE
    mov ax, 0x4F02
    mov bx, [ds:selected_vbe_mode]
    int 0x10
    ret

; ------------------------------------------------------------------------------
; Caricamento Kernel dal disco (2 chunk da 64 settori = 64 KB, max BIOS 127)
; ------------------------------------------------------------------------------
load_kernel_disk:
    mov ax, 0x1000
    mov ds, ax
    mov es, ax
    mov dl, [boot_drive]

    ; Prova prima caricamento con INT 13h Extensions (AH=42h)
    mov si, dap_kernel_1
    mov ah, 0x42
    int 0x13
    jc .try_chs

    mov si, dap_kernel_2
    mov ah, 0x42
    int 0x13
    jc .try_chs

    mov si, dap_kernel_3
    mov ah, 0x42
    int 0x13
    jc .try_chs

    mov si, dap_kernel_4
    mov ah, 0x42
    int 0x13
    jc .try_chs

    mov si, dap_kernel_5
    mov ah, 0x42
    int 0x13
    jc .try_chs

    mov si, dap_kernel_6
    mov ah, 0x42
    int 0x13
    jc .try_chs

    mov si, dap_kernel_7
    mov ah, 0x42
    int 0x13
    jc .try_chs

    mov si, dap_kernel_8
    mov ah, 0x42
    int 0x13
    jc .try_chs

    mov si, dap_kernel_9
    mov ah, 0x42
    int 0x13
    jc .try_chs

    mov si, dap_kernel_10
    mov ah, 0x42
    int 0x13
    jc .try_chs

    mov si, dap_kernel_11
    mov ah, 0x42
    int 0x13
    jc .try_chs

    mov si, dap_kernel_12
    mov ah, 0x42
    int 0x13
    jc .try_chs

    mov si, dap_kernel_13
    mov ah, 0x42
    int 0x13
    jc .try_chs

    mov si, dap_kernel_14
    mov ah, 0x42
    int 0x13
    jc .try_chs

    mov si, dap_kernel_15
    mov ah, 0x42
    int 0x13
    jc .try_chs
    ret

.try_chs:
    ; Fallback a lettura CHS standard (AH=02h) per CD El Torito e BIOS legacy
    mov ax, 0x1000
    mov ds, ax
    mov dl, [boot_drive]

    ; 1. Ricava geometria del disco
    mov ah, 0x08
    xor di, di
    int 0x13
    jc .chs_fail

    and cl, 0x3F           ; CL = Settori per traccia (1..63)
    test cl, cl
    jz .chs_fail
    mov [chs_spt], cl
    inc dh                 ; DH = Numero di testine
    mov [chs_heads], dh

    ; 2. Legge 960 settori (480 KB) da LBA 66 in 0x2000:0x0000
    mov word [chs_lba], 66
    mov word [chs_count], 960
    mov ax, 0x2000
    mov es, ax
    xor bx, bx

.chs_loop:
    cmp word [chs_count], 0
    jbe .chs_done

    ; Calcola Settore, Testina, Cilindro da chs_lba
    mov ax, [chs_lba]
    xor dx, dx
    movzx si, byte [chs_spt]
    div si                 ; AX = LBA / SPT, DX = LBA % SPT
    inc dx                 ; DX = Settore 1-based (1..SPT)
    mov cl, dl             ; CL = Sector (bits 0-5)

    xor dx, dx
    movzx si, byte [chs_heads]
    div si                 ; AX = Cylinder, DX = Head
    mov dh, dl             ; DH = Head
    mov ch, al             ; CH = Cylinder low 8 bits
    shr ax, 2
    and al, 0xC0
    or cl, al              ; CL bits 6-7 = Cylinder high 2 bits

    ; Quanti settori restano nella traccia corrente: SPT - Sector + 1
    movzx si, byte [chs_spt]
    mov bl, cl
    and bx, 0x3F           ; BX = sector 1-based (da CL)
    sub si, bx
    inc si                 ; SI = settori disponibili fino a fine traccia

    ; batch = min(chs_count, SI)
    mov ax, [chs_count]
    cmp ax, si
    jbe .batch_ok
    mov ax, si
.batch_ok:
    push ax                ; salva batch

    mov ah, 0x02           ; AH = 02h (Read Sectors)
    mov dl, [boot_drive]
    xor bx, bx             ; ES:BX = buffer (BX = 0)
    int 0x13
    jc .chs_pop_fail

    pop ax                 ; ripristina batch letti
    add [chs_lba], ax
    sub [chs_count], ax

    ; Avanza buffer destinazione ES:BX: ES += (batch * 32), BX resta 0
    shl ax, 5              ; batch * 32 paragrafi (512 byte per settore)
    mov dx, es
    add dx, ax
    mov es, dx
    jmp .chs_loop

.chs_pop_fail:
    pop cx
    mov dx, 0x3F8
    mov al, 'F'
    out dx, al
    mov al, ah
    shr al, 4
    add al, '0'
    cmp al, '9'
    jbe .e1
    add al, 7
.e1:
    out dx, al
    mov al, ah
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .e2
    add al, 7
.e2:
    out dx, al
    ret

.chs_fail:
    mov dx, 0x3F8
    mov al, 'P'
    out dx, al
    ret

.chs_done:
    ret

chs_spt:    db 0
chs_heads:  db 0
chs_lba:    dw 0
chs_count:  dw 0

; Disk Address Packets (64 settori ciascuno = 32 KB)
; Formato DAP: byte size(0x10), byte res(0), word count, word offset, word segment, qword lba
align 4
dap_kernel_1:
    db 0x10, 0
    dw 64
    dw 0x0000, 0x2000
    dq 66

dap_kernel_2:
    db 0x10, 0
    dw 64
    dw 0x0000, 0x2800
    dq 130

dap_kernel_3:
    db 0x10, 0
    dw 64
    dw 0x0000, 0x3000
    dq 194

dap_kernel_4:
    db 0x10, 0
    dw 64
    dw 0x0000, 0x3800
    dq 258

dap_kernel_5:
    db 0x10, 0
    dw 64
    dw 0x0000, 0x4000
    dq 322

dap_kernel_6:
    db 0x10, 0
    dw 64
    dw 0x0000, 0x4800
    dq 386

dap_kernel_7:
    db 0x10, 0
    dw 64
    dw 0x0000, 0x5000
    dq 450

dap_kernel_8:
    db 0x10, 0
    dw 64
    dw 0x0000, 0x5800
    dq 514

dap_kernel_9:
    db 0x10, 0
    dw 64
    dw 0x0000, 0x6000
    dq 578

dap_kernel_10:
    db 0x10, 0
    dw 64
    dw 0x0000, 0x6800
    dq 642

dap_kernel_11:
    db 0x10, 0
    dw 64
    dw 0x0000, 0x7000
    dq 706

dap_kernel_12:
    db 0x10, 0
    dw 64
    dw 0x0000, 0x7800
    dq 770

dap_kernel_13:
    db 0x10, 0
    dw 64
    dw 0x0000, 0x8000
    dq 834

dap_kernel_14:
    db 0x10, 0
    dw 64
    dw 0x0000, 0x8800
    dq 898

dap_kernel_15:
    db 0x10, 0
    dw 64
    dw 0x0000, 0x9000
    dq 962

; ------------------------------------------------------------------------------
; Abilitazione Linea A20
; ------------------------------------------------------------------------------
enable_a20:
    in al, 0x92
    or al, 2
    out 0x92, al
    ret

; ------------------------------------------------------------------------------
; GDT Table (32-bit e 64-bit)
; ------------------------------------------------------------------------------
align 16
gdt_start:
    ; 0x00: Null Descriptor
    dq 0x0000000000000000
    ; 0x08: 32-bit Code Segment (Base=0, Limit=4GB, Exec/Read, 32-bit Protected Mode)
    dw 0xFFFF, 0x0000
    db 0x00, 0x9A, 0xCF, 0x00
    ; 0x10: 32-bit Data Segment (Base=0, Limit=4GB, Read/Write)
    dw 0xFFFF, 0x0000
    db 0x00, 0x92, 0xCF, 0x00
    ; 0x18: 64-bit Code Segment (Exec/Read, Long Mode L=1, D=0)
    dq 0x00209A0000000000
    ; 0x20: 64-bit Data Segment (Read/Write)
    dq 0x0000920000000000
    ; 0x28: 64-bit User Data Segment (DPL=3, Present=1, Writable=1)
    dq 0x0000F20000000000
    ; 0x30: 64-bit User Code Segment (DPL=3, Present=1, Exec/Read, L=1, D=0)
    dq 0x0020FA0000000000
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd 0x10000 + gdt_start     ; Indirizzo lineare della GDT

boot_drive:         db 0
selected_vbe_mode:  dw 0

align 16
vbe_mode_info:
    times 256 db 0

; ==============================================================================
; Sezione a 32-bit Protected Mode
; ==============================================================================
[BITS 32]
pm32_entry:
    ; Imposta i registri di segmento a 32-bit (0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    mov dx, 0x3F8
    mov al, 'P'
    out dx, al

    ; 1. Pulisce 24 KB per le tabelle di paginazione da 0x2000 a 0x7FFF
    mov edi, 0x2000
    mov ecx, 6144
    xor eax, eax
    cld
    rep stosd

    ; 2. Configura Paging a 4 Livelli (4 GB Identity Mapping con pagine Huge da 2MB)
    ; PML4 a 0x2000
    mov dword [0x2000], 0x3003         ; PML4[0] -> PDPT a 0x3000

    ; PDPT a 0x3000 (4 ingressi da 1 GB ciascuno)
    mov dword [0x3000], 0x4003         ; 0 - 1 GB (PD0 a 0x4000)
    mov dword [0x3008], 0x5003         ; 1 - 2 GB (PD1 a 0x5000)
    mov dword [0x3010], 0x6003         ; 2 - 3 GB (PD2 a 0x6000)
    mov dword [0x3018], 0x7003         ; 3 - 4 GB (PD3 a 0x7000)

    ; Popola 2048 ingressi da 2MB ciascuno (totale 4GB)
    mov edi, 0x4000
    mov ecx, 2048
    mov eax, 0x00000083                ; Bit 7 = Huge Page (2MB), Writable, Present
    xor edx, edx

.map_pd_loop32:
    mov [edi], eax
    mov [edi + 4], edx
    add edi, 8
    add eax, 0x00200000                ; +2 MB
    adc edx, 0
    loop .map_pd_loop32

    ; 3. Carica CR3 con l'indirizzo della PML4 (0x2000)
    mov eax, 0x2000
    mov cr3, eax

    ; 4. Abilita PAE (Physical Address Extension) in CR4
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; 5. Abilita Long Mode nel registro MSR IA32_EFER (0xC0000080)
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8                     ; LME (Long Mode Enable)
    wrmsr

    ; 6. Abilita Paging (PG bit 31) in CR0 per entrare effettivamente in Long Mode
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    ; 7. Far jump nel segmento di codice a 64-bit (0x18)
    jmp 0x18:(0x10000 + lm64_entry)

; ==============================================================================
; Sezione a 64-bit Long Mode
; ==============================================================================
[BITS 64]
lm64_entry:
    ; Registri di segmento a 64-bit (0x20)
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, 0x200000

    mov dx, 0x3F8
    mov al, 'L'
    out dx, al

    ; Copia il binario del kernel dalla memoria temporanea 0x20000 a 0x100000 (1MB)
    mov rsi, 0x20000
    mov rdi, 0x100000
    mov rcx, (960 * 512) / 8           ; 480 KB in qwords (960 settori)
    cld
    rep movsq

    ; RDI = puntatore alla struttura BootInfo a 0x9000 (primo parametro System V ABI)
    mov rdi, 0x9000

    ; Salta all'entry point del Kernel
    mov rax, 0x100000
    call rax

.hang64:
    hlt
    jmp .hang64
