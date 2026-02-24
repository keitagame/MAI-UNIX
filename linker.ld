/* linker.ld - カーネルリンカスクリプト */
ENTRY(_start)

SECTIONS {
    /* カーネルを1MB地点にロード */
    . = 1M;

    .text ALIGN(4K) : {
        *(.multiboot)
        *(.text)
    }

    .rodata ALIGN(4K) : {
        *(.rodata*)
    }

    .data ALIGN(4K) : {
        *(.data)
    }

    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss)
    }

    _kernel_end = .;
}
