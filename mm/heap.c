// mm/heap.c - カーネルヒープ (free-list アロケータ)
#include "../include/kernel/mm.h"
#include "../include/kernel/types.h"

#define HEAP_START 0x01000000  // 16MB
#define HEAP_MAX   0x04000000  // 64MB

typedef struct block_header {
    uint32_t            magic;
    size_t              size;
    int                 free;
    struct block_header *next;
    struct block_header *prev;
} block_header_t;

#define HEAP_MAGIC 0xCAFEBABE

static block_header_t* heap_head = NULL;
static uint32_t heap_brk = HEAP_START;

extern page_directory_t* vmm_get_kernel_directory(void);

static void heap_expand(size_t bytes) {
    page_directory_t* kd = vmm_get_kernel_directory();
    uint32_t needed = (bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for (uint32_t off = 0; off < needed; off += PAGE_SIZE) {
        void* phys = pmm_alloc();
        vmm_map(kd, heap_brk + off, (uint32_t)phys, PAGE_PRESENT | PAGE_WRITE);
    }
    heap_brk += needed;
}

void heap_init(void) {
    heap_expand(PAGE_SIZE);
    heap_head        = (block_header_t*)HEAP_START;
    heap_head->magic = HEAP_MAGIC;
    heap_head->size  = PAGE_SIZE - sizeof(block_header_t);
    heap_head->free  = 1;
    heap_head->next  = NULL;
    heap_head->prev  = NULL;
}

static void coalesce(block_header_t* b) {
    if (b->next && b->next->free) {
        b->size += sizeof(block_header_t) + b->next->size;
        b->next  = b->next->next;
        if (b->next) b->next->prev = b;
    }
    if (b->prev && b->prev->free) {
        b->prev->size += sizeof(block_header_t) + b->size;
        b->prev->next  = b->next;
        if (b->next) b->next->prev = b->prev;
    }
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;
    size = (size + 7) & ~7; // 8バイトアライン

    block_header_t* cur = heap_head;
    while (cur) {
        if (cur->free && cur->size >= size) {
            // 分割できるか？
            if (cur->size >= size + sizeof(block_header_t) + 8) {
                block_header_t* split = (block_header_t*)((uint8_t*)(cur + 1) + size);
                split->magic = HEAP_MAGIC;
                split->size  = cur->size - size - sizeof(block_header_t);
                split->free  = 1;
                split->next  = cur->next;
                split->prev  = cur;
                if (cur->next) cur->next->prev = split;
                cur->next    = split;
                cur->size    = size;
            }
            cur->free = 0;
            return (void*)(cur + 1);
        }
        cur = cur->next;
    }

    // ヒープ拡張
    size_t expand = size + sizeof(block_header_t) + PAGE_SIZE;
    uint32_t old_brk = heap_brk;
    heap_expand(expand);

    block_header_t* newb = (block_header_t*)old_brk;
    newb->magic = HEAP_MAGIC;
    newb->size  = heap_brk - old_brk - sizeof(block_header_t);
    newb->free  = 1;
    newb->next  = NULL;
    newb->prev  = NULL;

    // リストに追加
    block_header_t* last = heap_head;
    while (last->next) last = last->next;
    last->next = newb;
    newb->prev = last;
    coalesce(newb);

    return kmalloc(size);
}

void* kmalloc_aligned(size_t size, size_t align) {
    // シンプル実装: 余分に確保してアライン
    void* ptr = kmalloc(size + align);
    uint32_t addr = (uint32_t)ptr;
    addr = (addr + align - 1) & ~(align - 1);
    return (void*)addr;
}

void kfree(void* ptr) {
    if (!ptr) return;
    block_header_t* hdr = (block_header_t*)ptr - 1;
    if (hdr->magic != HEAP_MAGIC) return; // 二重解放防止
    hdr->free = 1;
    coalesce(hdr);
}

void* krealloc(void* ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    block_header_t* hdr = (block_header_t*)ptr - 1;
    if (hdr->size >= new_size) return ptr;

    void* newp = kmalloc(new_size);
    if (!newp) return NULL;
    uint8_t* s = (uint8_t*)ptr;
    uint8_t* d = (uint8_t*)newp;
    for (size_t i = 0; i < hdr->size; i++) d[i] = s[i];
    kfree(ptr);
    return newp;
}
