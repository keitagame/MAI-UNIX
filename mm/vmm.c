// mm/vmm.c - 仮想メモリ管理 (ページング)
#include "../include/kernel/mm.h"
#include "../include/kernel/types.h"

// カーネル空間: 0xC0000000〜 (3GB以降)
#define KERNEL_VIRT_BASE 0xC0000000

static page_directory_t* kernel_dir = NULL;
static page_directory_t* current_dir = NULL;

static void memset32(void* dst, uint32_t val, size_t count) {
    uint32_t* d = (uint32_t*)dst;
    for (size_t i = 0; i < count; i++) d[i] = val;
}

void vmm_map(page_directory_t* pd, uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pd_idx = virt >> 22;
    uint32_t pt_idx = (virt >> 12) & 0x3FF;

    page_table_t* pt;
    if (!(pd->entries[pd_idx] & PAGE_PRESENT)) {
        pt = (page_table_t*)pmm_alloc();
        memset32(pt, 0, 1024);
        pd->entries[pd_idx] = (uint32_t)pt | PAGE_PRESENT | PAGE_WRITE | (flags & PAGE_USER);
    } else {
        pt = (page_table_t*)(pd->entries[pd_idx] & ~0xFFF);
    }

    pt->entries[pt_idx] = (phys & ~0xFFF) | PAGE_PRESENT | flags;

    // TLBフラッシュ
    asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

void vmm_unmap(page_directory_t* pd, uint32_t virt) {
    uint32_t pd_idx = virt >> 22;
    uint32_t pt_idx = (virt >> 12) & 0x3FF;
    if (!(pd->entries[pd_idx] & PAGE_PRESENT)) return;
    page_table_t* pt = (page_table_t*)(pd->entries[pd_idx] & ~0xFFF);
    pt->entries[pt_idx] = 0;
    asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

uint32_t vmm_get_physical(page_directory_t* pd, uint32_t virt) {
    uint32_t pd_idx = virt >> 22;
    uint32_t pt_idx = (virt >> 12) & 0x3FF;
    if (!(pd->entries[pd_idx] & PAGE_PRESENT)) return 0;
    page_table_t* pt = (page_table_t*)(pd->entries[pd_idx] & ~0xFFF);
    return (pt->entries[pt_idx] & ~0xFFF) + (virt & 0xFFF);
}

void vmm_switch(page_directory_t* pd) {
    current_dir = pd;
    asm volatile("mov %0, %%cr3" :: "r"((uint32_t)pd) : "memory");
}

void vmm_init(void) {
    // カーネルページディレクトリ作成
    kernel_dir = (page_directory_t*)pmm_alloc();
    memset32(kernel_dir, 0, 1024);

    // カーネル空間をアイデンティティマップ (物理0〜4MB)
    for (uint32_t addr = 0; addr < 4 * 1024 * 1024; addr += PAGE_SIZE) {
        vmm_map(kernel_dir, addr, addr, PAGE_PRESENT | PAGE_WRITE);
    }

    // ページングを有効化
    uint32_t cr0;
    asm volatile("mov %%cr3, %%eax\n" : : : "eax");
    vmm_switch(kernel_dir);
    asm volatile(
        "mov %%cr0, %0\n"
        "or $0x80000000, %0\n"
        "mov %0, %%cr0\n"
        : "=r"(cr0) : : "memory"
    );
}

page_directory_t* vmm_create_directory(void) {
    page_directory_t* pd = (page_directory_t*)pmm_alloc();
    memset32(pd, 0, 1024);

    // カーネル空間をコピー (上位1GB)
    for (int i = 768; i < 1024; i++) {
        pd->entries[i] = kernel_dir->entries[i];
    }
    return pd;
}

// CoWクローン: ユーザー空間ページをread-onlyにしてCOWフラグ
page_directory_t* vmm_clone(page_directory_t* src) {
    page_directory_t* dst = vmm_create_directory();

    for (int i = 0; i < 768; i++) { // ユーザー空間のみ
        if (!(src->entries[i] & PAGE_PRESENT)) continue;
        page_table_t* src_pt = (page_table_t*)(src->entries[i] & ~0xFFF);
        page_table_t* dst_pt = (page_table_t*)pmm_alloc();
        memset32(dst_pt, 0, 1024);

        for (int j = 0; j < 1024; j++) {
            if (!(src_pt->entries[j] & PAGE_PRESENT)) continue;
            // COW: 親子ともread-only + COWビット
            src_pt->entries[j] &= ~PAGE_WRITE;
            src_pt->entries[j] |= PAGE_COW;
            dst_pt->entries[j]  = src_pt->entries[j];
        }
        dst->entries[i] = (uint32_t)dst_pt | (src->entries[i] & 0xFFF);
    }
    return dst;
}

void vmm_destroy_directory(page_directory_t* pd) {
    for (int i = 0; i < 768; i++) {
        if (!(pd->entries[i] & PAGE_PRESENT)) continue;
        page_table_t* pt = (page_table_t*)(pd->entries[i] & ~0xFFF);
        for (int j = 0; j < 1024; j++) {
            if (pt->entries[j] & PAGE_PRESENT) {
                pmm_free((void*)(pt->entries[j] & ~0xFFF));
            }
        }
        pmm_free(pt);
    }
    pmm_free(pd);
}

page_directory_t* vmm_get_kernel_directory(void) { return kernel_dir; }