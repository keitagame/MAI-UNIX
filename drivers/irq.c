// drivers/irq.c - タイマー・キーボード割り込み処理
#include "../include/kernel/idt.h"
#include "../include/kernel/proc.h"
#include "../include/kernel/mm.h"
#include "../include/kernel/types.h"

extern void keyboard_handler(uint8_t sc);
extern void scheduler_tick(void);

// PIC EOI
static inline void pic_eoi(int irq) {
    if (irq >= 8)
        asm volatile("outb %0, %1" :: "a"((uint8_t)0x20), "d"((uint16_t)0xA0));
    asm volatile("outb %0, %1" :: "a"((uint8_t)0x20), "d"((uint16_t)0x20));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    asm volatile("inb %1, %0" : "=a"(v) : "d"(port));
    return v;
}

void irq_handler(regs_t* r) {
    int irq = r->int_no - 32;

    switch (irq) {
    case 0: // タイマー (100Hz)
        scheduler_tick();
        break;
    case 1: // キーボード
        keyboard_handler(inb(0x60));
        break;
    default:
        break;
    }

    pic_eoi(irq);
}

// 例外ハンドラ
static const char* exception_msgs[] = {
    "Division by zero",
    "Debug",
    "NMI",
    "Breakpoint",
    "Overflow",
    "Bounds exceeded",
    "Invalid opcode",
    "Device not available",
    "Double fault",
    "Coprocessor segment overrun",
    "Invalid TSS",
    "Segment not present",
    "Stack segment fault",
    "General protection fault",
    "Page fault",
    "Unknown",
    "FPU error",
    "Alignment check",
    "Machine check",
    "SIMD error",
};

extern void kprintf(const char* fmt, ...);
extern void serial_puts(const char* s);
extern void tty_puts(const char* s);

// CoW (Copy-on-Write) ページフォルト処理


static void handle_page_fault(regs_t* r) {
    uint32_t cr2;
    asm volatile("mov %%cr2, %0" : "=r"(cr2));

    // CoWチェック (簡易)
    // err_code bit1=0 → read fault, bit1=1 → write fault
    // bit0=0 → not present, bit0=1 → protection violation
    if ((r->err_code & 3) == 3) {
        // 書き込み保護違反 → CoW処理
        // ページをコピーして再マップ (簡易: カーネルではシンプルに死ぬ)
    }

    tty_puts("\n*** KERNEL PANIC: Page Fault ***\n");
    kprintf("  Address: 0x%x  EIP: 0x%x  Error: 0x%x\n", cr2, r->eip, r->err_code);
    if (current_proc) {
        kprintf("  Process: %s (pid %d)\n", current_proc->name, current_proc->pid);
    }
    asm volatile("cli; hlt");
}

void isr_handler(regs_t* r) {
    if (r->int_no == 14) {
        handle_page_fault(r);
        return;
    }

    if (r->int_no < 20) {
        tty_puts("\n*** EXCEPTION: ");
        tty_puts(exception_msgs[r->int_no]);
        tty_puts(" ***\n");
        kprintf("  EIP: 0x%x  CS: 0x%x  EFLAGS: 0x%x\n", r->eip, r->cs, r->eflags);
        if (current_proc)
            kprintf("  Process: %s (pid %d)\n", current_proc->name, current_proc->pid);
        asm volatile("cli; hlt");
    }
}

void pit_init(void) {
    // PIT チャンネル0, 100Hz = 1193180 / 100 = 11931
    uint32_t divisor = 1193180 / 100;
    asm volatile("outb %0, %1" :: "a"((uint8_t)0x36), "d"((uint16_t)0x43));
    asm volatile("outb %0, %1" :: "a"((uint8_t)(divisor & 0xFF)), "d"((uint16_t)0x40));
    asm volatile("outb %0, %1" :: "a"((uint8_t)((divisor >> 8) & 0xFF)), "d"((uint16_t)0x40));
}
