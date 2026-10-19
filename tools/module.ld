/*
 * tools/module.ld
 * Linker script per moduli esterni protetti (.MOD) di NewOS V1
 * (C) Newspoint Software - NewSoft - NewOS V1
 */

SECTIONS {
    . = 0x0;

    /* Header di sicurezza protetto a 64 byte */
    .header : {
        KEEP(*(.header))
    }

    /* Entry point del modulo all'offset fisso 0x40 (64) */
    .text : {
        KEEP(*(.text.entry))
        *(.text*)
    }

    /* Dati in sola lettura (stringhe, costanti) */
    .rodata : {
        *(.rodata*)
    }

    /* Dati inizializzati */
    .data : {
        *(.data*)
    }

    /* Dati non inizializzati */
    .bss : {
        *(.bss*)
        *(COMMON)
    }
}
