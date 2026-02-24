; boot/boot.asm - Multiboot ブートローダー
MBOOT_MAGIC    equ 0x1BADB002
MBOOT_ALIGN    equ 1 << 0
MBOOT_MEMINFO  equ 1 << 1
MBOOT_FLAGS    equ MBOOT_ALIGN | MBOOT_MEMINFO
MBOOT_CHECKSUM equ -(MBOOT_MAGIC + MBOOT_FLAGS)

section .multiboot
align 4
    dd MBOOT_MAGIC
    dd MBOOT_FLAGS
    dd MBOOT_CHECKSUM

section .bss
align 16
stack_bottom:
    resb 16384  ; 16KB カーネルスタック
stack_top:

section .text
global _start
extern kernel_main

_start:
    ; スタック設定
    mov esp, stack_top

    ; eax = magic, ebx = multiboot info pointer
    push ebx
    push eax

    ; カーネルmain呼び出し
    call kernel_main

    ; ここには戻らないはずだが念のため
.hang:
    cli
    hlt
    jmp .hang
