// include/kernel/mm.h
#pragma once
#include "types.h"

#define PAGE_SIZE 4096

// 物理メモリ管理
void  pmm_init(uint32_t mem_size, uint32_t kernel_end);
void* pmm_alloc(void);
void  pmm_free(void* addr);

// 仮想メモリ管理
#define PAGE_PRESENT  0x001
#define PAGE_WRITE    0x002
#define PAGE_USER     0x004
#define PAGE_COW      0x200  // ソフトウェアビット: Copy-on-Write

typedef uint32_t page_t;

typedef struct {
    page_t entries[1024];
} page_table_t;

typedef struct {
    page_t entries[1024];
} page_directory_t;

void              vmm_init(void);
page_directory_t* vmm_create_directory(void);
void              vmm_destroy_directory(page_directory_t* pd);
void              vmm_map(page_directory_t* pd, uint32_t virt, uint32_t phys, uint32_t flags);
void              vmm_unmap(page_directory_t* pd, uint32_t virt);
uint32_t          vmm_get_physical(page_directory_t* pd, uint32_t virt);
void              vmm_switch(page_directory_t* pd);
page_directory_t* vmm_clone(page_directory_t* src);
page_directory_t* vmm_get_kernel_directory(void);

// カーネルヒープ
void  heap_init(void);
void* kmalloc(size_t size);
void* kmalloc_aligned(size_t size, size_t align);
void  kfree(void* ptr);
void* krealloc(void* ptr, size_t new_size);
