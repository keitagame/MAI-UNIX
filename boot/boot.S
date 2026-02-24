/* boot/boot.S - Multiboot ブートローダー (GAS AT&T syntax) */
.set MBOOT_MAGIC,    0x1BADB002
.set MBOOT_ALIGN,    1 << 0
.set MBOOT_MEMINFO,  1 << 1
.set MBOOT_FLAGS,    MBOOT_ALIGN | MBOOT_MEMINFO
.set MBOOT_CHECKSUM, -(MBOOT_MAGIC + MBOOT_FLAGS)

.section .multiboot
.align 4
.long MBOOT_MAGIC
.long MBOOT_FLAGS
.long MBOOT_CHECKSUM

.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:

.section .text
.global _start
.extern kernel_main

_start:
    movl $stack_top, %esp
    pushl %ebx
    pushl %eax
    call kernel_main
.hang:
    cli
    hlt
    jmp .hang

.section .note.GNU-stack,"",@progbits