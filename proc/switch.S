/* proc/switch.S - context switch (GAS AT&T syntax) */

/* void context_switch(uint32_t *old_esp, uint32_t new_esp) */
.global context_switch
context_switch:
    movl 4(%esp), %eax
    movl 8(%esp), %ecx
    pushl %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl %esp, (%eax)
    movl %ecx, %esp
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    ret

/* void flush_tss(void) */
.global flush_tss
flush_tss:
    movw $0x2B, %ax
    ltr %ax
    ret

.section .note.GNU-stack,"",@progbits