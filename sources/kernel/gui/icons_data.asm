; ==============================================================================
; NewOS V1 - Repository Icone Native Incorporato
; Include direttamente il pacchetto icone ad alta definizione nel segmento .rodata
; ==============================================================================
[BITS 64]
DEFAULT REL

SECTION .rodata
GLOBAL embedded_icons_bin
GLOBAL embedded_icons_size

embedded_icons_bin:
    incbin "resources/icons.bin"
embedded_icons_end:

embedded_icons_size:
    dd embedded_icons_end - embedded_icons_bin
