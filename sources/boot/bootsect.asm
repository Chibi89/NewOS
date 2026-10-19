; ==============================================================================
; NewOS V1 64-bit - Master Boot Record (MBR) Boot Sector
; Utilizza BIOS INT 13h Extensions (AH=0x42 LBA Read) per caricare Stage 2
; ==============================================================================

[BITS 16]
[ORG 0x7C00]

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    ; Salva il drive di boot fornito dal BIOS in DL
    mov [boot_drive], dl

    ; Debug seriale: 'M' (MBR avviato)
    mov dx, 0x3F8
    mov al, 'M'
    out dx, al

    ; Stampa messaggio di avvio
    mov si, msg_loading
    call print_string

    ; Carica Stage 2 tramite BIOS LBA Extended Read (AH=0x42)
    mov si, dap_stage2
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jnc .load_ok

    ; Fallback a lettura CHS se AH=0x42 non supportato (16 settori alla volta)
    xor ax, ax
    mov dl, [boot_drive]
    int 0x13

    mov ax, 0x1000
    mov es, ax
    xor bx, bx
    mov ah, 0x02
    mov al, 16          ; Legge 16 settori (non supera il limite traccia)
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

.load_ok:
    ; Debug seriale: 'B' (Stage 2 caricato)
    mov dx, 0x3F8
    mov al, 'B'
    out dx, al

    ; Salta a Stage 2 a 0x1000:0x0000
    mov dl, [boot_drive]
    jmp 0x1000:0x0000

disk_error:
    mov si, msg_disk_err
    call print_string
.hang:
    hlt
    jmp .hang

print_string:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0
    int 0x10
    jmp print_string
.done:
    ret

; Disk Address Packet per caricare 64 settori di Stage 2 (32 KB) a 0x1000:0x0000
align 4
dap_stage2:
    db 0x10             ; Dimensione struttura DAP (16 byte)
    db 0                ; Riservato
    dw 64               ; Numero di settori da leggere (64 settori = 32 KB)
    dw 0x0000           ; Offset destinazione
    dw 0x1000           ; Segmento destinazione (0x1000:0x0000)
    dq 1                ; LBA iniziale (LBA 1 = Settore 2)

boot_drive:    db 0
msg_loading:   db "Loading NewOS V1 64-bit...", 13, 10, 0
msg_disk_err:  db "Boot Error: Cannot read Stage 2!", 13, 10, 0

; Tabella delle partizioni MBR (offset 446 / 0x1BE)
times 446 - ($ - $$) db 0

; Partizione 1: exFAT (Type 0x07, Start LBA 2048)
db 0x80                 ; Bootable flag
db 0x00, 0x02, 0x00     ; Start CHS
db 0x07                 ; Partition Type (0x07 = exFAT / NTFS)
db 0xFE, 0xFF, 0xFF     ; End CHS
dd 2048                 ; Start LBA (LBA 2048 = 1 MB)
dd 63488                ; Total Sectors (31 MB)

; Partizioni 2, 3, 4 (vuote, 3 * 16 = 48 bytes)
times 48 db 0

; Firma MBR (0xAA55)
dw 0xAA55
