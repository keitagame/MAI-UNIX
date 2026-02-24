// kernel/gdt.c - グローバルディスクリプタテーブル
#include "../include/kernel/gdt.h"
#include "../include/kernel/types.h"

#define GDT_ENTRIES 6

static gdt_entry_t gdt[GDT_ENTRIES];
static gdt_ptr_t   gdt_ptr;
static tss_entry_t tss;

extern void flush_tss(void);

static void gdt_set_entry(int n, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[n].base_low    = (base & 0xFFFF);
    gdt[n].base_middle = (base >> 16) & 0xFF;
    gdt[n].base_high   = (base >> 24) & 0xFF;
    gdt[n].limit_low   = (limit & 0xFFFF);
    gdt[n].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[n].access      = access;
}

static void tss_setup(uint16_t ss0, uint32_t esp0) {
    uint32_t base  = (uint32_t)&tss;
    uint32_t limit = base + sizeof(tss_entry_t);

    gdt_set_entry(5, base, limit, 0x89, 0x00);

    for (int i = 0; i < (int)sizeof(tss_entry_t); i++)
        ((uint8_t*)&tss)[i] = 0;

    tss.ss0  = ss0;
    tss.esp0 = esp0;
    tss.iomap_base = sizeof(tss_entry_t);
}

void gdt_set_kernel_stack(uint32_t stack) {
    tss.esp0 = stack;
}

static void gdt_flush(void) {
    asm volatile(
        "lgdt %0\n"
        "ljmp $0x08, $.reload_cs\n"
        ".reload_cs:\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        :: "m"(gdt_ptr) : "eax"
    );
}

void gdt_init(void) {
    gdt_ptr.limit = (sizeof(gdt_entry_t) * GDT_ENTRIES) - 1;
    gdt_ptr.base  = (uint32_t)&gdt;

    gdt_set_entry(0, 0, 0,          0x00, 0x00); // NULL
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // カーネルコード
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // カーネルデータ
    gdt_set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); // ユーザーコード
    gdt_set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); // ユーザーデータ

    tss_setup(0x10, 0);

    gdt_flush();
    flush_tss();
}
