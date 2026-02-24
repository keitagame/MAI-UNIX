// kernel/main.c - カーネルエントリーポイント
#include "../include/kernel/types.h"
#include "../include/kernel/gdt.h"
#include "../include/kernel/idt.h"
#include "../include/kernel/mm.h"
#include "../include/kernel/proc.h"
#include "../include/kernel/vfs.h"

// Multiboot
#define MBOOT_MAGIC 0x2BADB002
typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint8_t  syms[16];
    uint32_t mmap_length;
    uint32_t mmap_addr;
} PACKED mboot_info_t;

extern void tty_clear(void);
extern void tty_puts(const char* s);
extern void tty_putchar(char c);
extern void serial_init(void);
extern void serial_puts(const char* s);
extern void pit_init(void);
extern void kprintf(const char* fmt, ...);
extern void isr_handler(regs_t* r);
extern void syscall_dispatch(regs_t* r);
extern void shell_main(void);
extern vnode_t* ramfs_create_root(void);
extern void ramfs_mkdir(vnode_t* parent, const char* name);
extern void ramfs_write_file(vnode_t* parent, const char* name, const char* data, size_t size);
extern vnode_t* tty_get_vnode(void);
extern file_t*  file_open(const char* path, int flags);

// isr_handler から syscall をディスパッチ
// (irq.c の isr_handler を上書き)
void isr_handler_ext(regs_t* r) {
    if (r->int_no == 128) {
        syscall_dispatch(r);
    } else {
        // 通常の例外ハンドラに転送 (irq.c で定義)
        void isr_handler(regs_t*);
        isr_handler(r);
    }
}

// ===== 初期ファイルシステム構築 =====
static void build_initfs(void) {
    vfs_init();
    vnode_t* root = ramfs_create_root();
    vfs_mount("/", root);

    // ディレクトリ構造
    ramfs_mkdir(root, "bin");
    ramfs_mkdir(root, "etc");
    ramfs_mkdir(root, "home");
    ramfs_mkdir(root, "tmp");
    ramfs_mkdir(root, "dev");
    ramfs_mkdir(root, "proc");

    // /home/user
    vnode_t* home = root->ops->finddir(root, "home");
    if (home) ramfs_mkdir(home, "user");

    // /etc/motd
    vnode_t* etc = root->ops->finddir(root, "etc");
    if (etc) {
        const char* motd =
            "Welcome to MyOS!\n"
            "A minimal Unix-like OS written in C.\n"
            "Type 'help' for available commands.\n";
        ramfs_write_file(etc, "motd", motd, 100);

        const char* hostname = "myos\n";
        ramfs_write_file(etc, "hostname", hostname, 6);
    }

    // /etc/passwd
    if (etc) {
        const char* passwd = "root:x:0:0:root:/home/root:/bin/sh\n";
        ramfs_write_file(etc, "passwd", passwd, 36);
    }

    // README
    const char* readme =
        "MyOS - Minimal Unix-like Operating System\n"
        "==========================================\n"
        "Built from scratch in C.\n"
        "Features:\n"
        "  - x86 protected mode\n"
        "  - Paging / virtual memory (CoW)\n"
        "  - Round-robin scheduler\n"
        "  - ramfs virtual filesystem\n"
        "  - POSIX-like system calls\n"
        "  - Interactive shell\n";
    ramfs_write_file(root, "README", readme, 300);

    // /dev/tty (char device)
    vnode_t* dev = root->ops->finddir(root, "dev");
    if (dev) {
        vnode_t* tty = tty_get_vnode();
        // devにttyを登録
        ramfs_mkdir(dev, "tty0"); // placeholder
    }
}

// ===== initプロセス =====
static void init_process(void) {
    // stdin/stdout/stderr = tty
    file_t* tty_in  = file_open("/dev/tty0", O_RDONLY);
    file_t* tty_out = file_open("/dev/tty0", O_WRONLY);
    if (!tty_in || !tty_out) {
        // TTY vnode を直接作る
        extern vnode_t* tty_get_vnode(void);
        extern file_t* file_open(const char*, int);
    }

    // FD 0,1,2 = TTY (tty_get_vnode経由)
    extern vnode_t* tty_get_vnode(void);
    vnode_t* tty_vn = tty_get_vnode();

    // file_t を直接生成 (kmalloc)
    for (int i = 0; i < 3; i++) {
        file_t* f = (file_t*)kmalloc(sizeof(file_t));
        f->vnode  = tty_vn;
        f->flags  = (i == 0) ? O_RDONLY : O_WRONLY;
        f->offset = 0;
        f->ref    = 1;
        tty_vn->ref_count++;
        current_proc->fds[i] = f;
    }

    // motd表示
    kprintf("\n");
    file_t* motd = file_open("/etc/motd", O_RDONLY);
    if (motd) {
        char buf[256];
        ssize_t n;
        while ((n = file_read(motd, buf, sizeof(buf)-1)) > 0) {
            buf[n] = 0;
            tty_puts(buf);
        }
        file_close(motd);
    }

    // シェル起動
    shell_main();

    // シェルが終了したら再起動
    while (1) {
        tty_puts("\nShell exited. Halting.\n");
        asm volatile("cli; hlt");
    }
}

// ===== カーネルメイン =====
void kernel_main(uint32_t magic, mboot_info_t* mbi) {
    serial_init();
    tty_clear();

    serial_puts("[BOOT] MyOS kernel starting...\n");
    tty_puts("MyOS booting...\n");

    // メモリ量取得
    uint32_t mem_kb = 1024; // デフォルト: 4MB
    if (magic == MBOOT_MAGIC && mbi) {
        mem_kb = mbi->mem_upper + 1024;
    }
    uint32_t mem_bytes = mem_kb * 1024;
    if (mem_bytes < 4 * 1024 * 1024) mem_bytes = 4 * 1024 * 1024;

    // カーネル終端アドレス (リンカーシンボルを使用)
    extern char _kernel_end[];
    uint32_t kernel_end = (uint32_t)_kernel_end;

    // 初期化シーケンス
    kprintf("[INIT] GDT...\n");
    gdt_init();

    kprintf("[INIT] IDT...\n");
    idt_init();

    kprintf("[INIT] PMM (mem: %d MB)...\n", mem_bytes / 1024 / 1024);
    pmm_init(mem_bytes, kernel_end);

    kprintf("[INIT] VMM...\n");
    vmm_init();

    kprintf("[INIT] Heap...\n");
    heap_init();

    kprintf("[INIT] VFS + ramfs...\n");
    build_initfs();

    kprintf("[INIT] PIT (100Hz)...\n");
    pit_init();

    kprintf("[INIT] Process manager...\n");
    proc_init();

    kprintf("[BOOT] Kernel initialized! Starting init...\n\n");

    // initプロセス起動
    process_t* init = proc_create_kernel(init_process, "init");
    (void)init;

    // idleループ (スケジューラが割り込みで動く)
    asm volatile("sti");
    while (1) {
        asm volatile("hlt");
    }
}
