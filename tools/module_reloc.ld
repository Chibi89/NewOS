/*
 * tools/module_reloc.ld
 * Linker script per moduli/DLL con auto-relocation R_X86_64_RELATIVE (es. QuickJS)
 */

SECTIONS {
    . = 0x0;

    .header : {
        KEEP(*(.header))
    }

    .text : {
        KEEP(*(.entry))
        KEEP(*(.text.entry))
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data.rel.ro : {
        *(.data.rel.ro*)
    }

    .got : {
        *(.got*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }

    .rela.dyn : {
        *(.rela*)
    }

    /DISCARD/ : {
        *(.dynsym)
        *(.dynstr)
        *(.hash)
        *(.gnu.hash)
        *(.dynamic)
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
