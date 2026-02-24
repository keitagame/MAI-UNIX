// mm/pmm.c - 物理メモリ管理 (ビットマップ)
#include "../include/kernel/mm.h"
#include "../include/kernel/types.h"

#define MAX_MEM_MB  256
#define BITMAP_LEN  (MAX_MEM_MB * 1024 * 1024 / PAGE_SIZE / 32)

static uint32_t bitmap[BITMAP_LEN];
static uint32_t total_pages;
static uint32_t used_pages;

static void set_bit(uint32_t page) {
    bitmap[page / 32] |= (1U << (page % 32));
}

static void clear_bit(uint32_t page) {
    bitmap[page / 32] &= ~(1U << (page % 32));
}

static int test_bit(uint32_t page) {
    return (bitmap[page / 32] >> (page % 32)) & 1;
}

void pmm_init(uint32_t mem_size, uint32_t kernel_end) {
    total_pages = mem_size / PAGE_SIZE;
    used_pages  = 0;

    // 全ページを使用済みにする
    for (uint32_t i = 0; i < BITMAP_LEN; i++) bitmap[i] = 0xFFFFFFFF;

    // 利用可能なページを解放 (1MB以降〜mem_size)
    uint32_t start_page = ((kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)) / PAGE_SIZE;
    uint32_t end_page   = total_pages;

    for (uint32_t i = start_page; i < end_page; i++) {
        clear_bit(i);
    }

    used_pages = start_page;
}

void* pmm_alloc(void) {
    for (uint32_t i = 0; i < total_pages; i++) {
        if (!test_bit(i)) {
            set_bit(i);
            used_pages++;
            return (void*)(i * PAGE_SIZE);
        }
    }
    return NULL;
}

void pmm_free(void* addr) {
    uint32_t page = (uint32_t)addr / PAGE_SIZE;
    if (test_bit(page)) {
        clear_bit(page);
        used_pages--;
    }
}
