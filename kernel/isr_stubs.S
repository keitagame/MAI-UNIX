/* kernel/isr_stubs.S - interrupt stubs (GAS AT&T syntax) */
.extern isr_handler_ext
.extern irq_handler

.macro ISR_NOERRCODE num
.global isr\num
isr\num:
    cli
    pushl $0
    pushl $\num
    jmp isr_common
.endm

.macro ISR_ERRCODE num
.global isr\num
isr\num:
    cli
    pushl $\num
    jmp isr_common
.endm

.macro IRQ irq_num, int_num
.global irq\irq_num
irq\irq_num:
    cli
    pushl $0
    pushl $\int_num
    jmp irq_common
.endm

ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19

.global isr128
isr128:
    cli
    pushl $0
    pushl $128
    jmp isr_common

IRQ 0,  32
IRQ 1,  33
IRQ 2,  34
IRQ 3,  35
IRQ 4,  36
IRQ 5,  37
IRQ 6,  38
IRQ 7,  39
IRQ 8,  40
IRQ 9,  41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

isr_common:
    pushal
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    pushl %esp
    call isr_handler_ext
    addl $4, %esp
    popl %gs
    popl %fs
    popl %es
    popl %ds
    popal
    addl $8, %esp
    sti
    iret

irq_common:
    pushal
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    pushl %esp
    call irq_handler
    addl $4, %esp
    popl %gs
    popl %fs
    popl %es
    popl %ds
    popal
    addl $8, %esp
    sti
    iret

.section .note.GNU-stack,"",@progbits