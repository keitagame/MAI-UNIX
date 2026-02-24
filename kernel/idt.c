// kernel/idt.c - 割り込みディスクリプタテーブル
#include "../include/kernel/idt.h"
#include "../include/kernel/types.h"

#define IDT_ENTRIES 256

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t   idt_ptr;

// ISRスタブの前方宣言
extern void isr0(void); extern void isr1(void); extern void isr2(void);
extern void isr3(void); extern void isr4(void); extern void isr5(void);
extern void isr6(void); extern void isr7(void); extern void isr8(void);
extern void isr9(void); extern void isr10(void);extern void isr11(void);
extern void isr12(void);extern void isr13(void);extern void isr14(void);
extern void isr15(void);extern void isr16(void);extern void isr17(void);
extern void isr18(void);extern void isr19(void);extern void isr128(void);

extern void irq0(void); extern void irq1(void); extern void irq2(void);
extern void irq3(void); extern void irq4(void); extern void irq5(void);
extern void irq6(void); extern void irq7(void); extern void irq8(void);
extern void irq9(void); extern void irq10(void);extern void irq11(void);
extern void irq12(void);extern void irq13(void);extern void irq14(void);
extern void irq15(void);

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].offset_low  = base & 0xFFFF;
    idt[num].offset_high = (base >> 16) & 0xFFFF;
    idt[num].selector    = sel;
    idt[num].zero        = 0;
    idt[num].type_attr   = flags;
}

static void pic_remap(void) {
    // PIC初期化 (IRQ 0-15 → INT 32-47)
    asm volatile("outb %0, %1" :: "a"((uint8_t)0x11), "d"((uint16_t)0x20));
    asm volatile("outb %0, %1" :: "a"((uint8_t)0x11), "d"((uint16_t)0xA0));
    asm volatile("outb %0, %1" :: "a"((uint8_t)0x20), "d"((uint16_t)0x21));
    asm volatile("outb %0, %1" :: "a"((uint8_t)0x28), "d"((uint16_t)0xA1));
    asm volatile("outb %0, %1" :: "a"((uint8_t)0x04), "d"((uint16_t)0x21));
    asm volatile("outb %0, %1" :: "a"((uint8_t)0x02), "d"((uint16_t)0xA1));
    asm volatile("outb %0, %1" :: "a"((uint8_t)0x01), "d"((uint16_t)0x21));
    asm volatile("outb %0, %1" :: "a"((uint8_t)0x01), "d"((uint16_t)0xA1));
    asm volatile("outb %0, %1" :: "a"((uint8_t)0x00), "d"((uint16_t)0x21));
    asm volatile("outb %0, %1" :: "a"((uint8_t)0x00), "d"((uint16_t)0xA1));
}

void idt_init(void) {
    idt_ptr.limit = (sizeof(idt_entry_t) * IDT_ENTRIES) - 1;
    idt_ptr.base  = (uint32_t)&idt;

    for (int i = 0; i < IDT_ENTRIES; i++) idt_set_gate(i, 0, 0, 0);

    // 例外
    idt_set_gate(0,  (uint32_t)isr0,  0x08, 0x8E);
    idt_set_gate(1,  (uint32_t)isr1,  0x08, 0x8E);
    idt_set_gate(2,  (uint32_t)isr2,  0x08, 0x8E);
    idt_set_gate(3,  (uint32_t)isr3,  0x08, 0x8E);
    idt_set_gate(4,  (uint32_t)isr4,  0x08, 0x8E);
    idt_set_gate(5,  (uint32_t)isr5,  0x08, 0x8E);
    idt_set_gate(6,  (uint32_t)isr6,  0x08, 0x8E);
    idt_set_gate(7,  (uint32_t)isr7,  0x08, 0x8E);
    idt_set_gate(8,  (uint32_t)isr8,  0x08, 0x8E);
    idt_set_gate(9,  (uint32_t)isr9,  0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);

    // IRQ
    idt_set_gate(32, (uint32_t)irq0,  0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1,  0x08, 0x8E);
    idt_set_gate(34, (uint32_t)irq2,  0x08, 0x8E);
    idt_set_gate(35, (uint32_t)irq3,  0x08, 0x8E);
    idt_set_gate(36, (uint32_t)irq4,  0x08, 0x8E);
    idt_set_gate(37, (uint32_t)irq5,  0x08, 0x8E);
    idt_set_gate(38, (uint32_t)irq6,  0x08, 0x8E);
    idt_set_gate(39, (uint32_t)irq7,  0x08, 0x8E);
    idt_set_gate(40, (uint32_t)irq8,  0x08, 0x8E);
    idt_set_gate(41, (uint32_t)irq9,  0x08, 0x8E);
    idt_set_gate(42, (uint32_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint32_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint32_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint32_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint32_t)irq15, 0x08, 0x8E);

    // システムコール (int 0x80) - DPL=3でユーザーから呼べる
    idt_set_gate(128, (uint32_t)isr128, 0x08, 0xEE);

    pic_remap();

    asm volatile("lidt %0" :: "m"(idt_ptr));
    asm volatile("sti");
}
