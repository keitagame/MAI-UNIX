// drivers/tty.c - VGAテキストモード端末 + キーボード
#include "../include/kernel/types.h"
#include "../include/kernel/vfs.h"
#include "../include/kernel/mm.h"
#include "../include/kernel/proc.h"

// ===== VGA =====
#define VGA_BASE  0xB8000
#define VGA_COLS  80
#define VGA_ROWS  25
#define VGA_WHITE 0x07

static uint16_t* vga = (uint16_t*)VGA_BASE;
static int cur_col = 0, cur_row = 0;

static void vga_scroll(void) {
    for (int r = 0; r < VGA_ROWS - 1; r++)
        for (int c = 0; c < VGA_COLS; c++)
            vga[r * VGA_COLS + c] = vga[(r+1) * VGA_COLS + c];
    for (int c = 0; c < VGA_COLS; c++)
        vga[(VGA_ROWS-1) * VGA_COLS + c] = ' ' | (VGA_WHITE << 8);
    cur_row = VGA_ROWS - 1;
}

static void vga_update_cursor(void) {
    uint16_t pos = cur_row * VGA_COLS + cur_col;
    uint8_t val;
    // PORTへアクセス
    asm volatile("outb %0, %1" :: "a"((uint8_t)0x0F), "d"((uint16_t)0x3D4));
    asm volatile("outb %0, %1" :: "a"((uint8_t)(pos & 0xFF)), "d"((uint16_t)0x3D5));
    asm volatile("outb %0, %1" :: "a"((uint8_t)0x0E), "d"((uint16_t)0x3D4));
    asm volatile("outb %0, %1" :: "a"((uint8_t)((pos >> 8) & 0xFF)), "d"((uint16_t)0x3D5));
    (void)val;
}

void tty_putchar(char c) {
    if (c == '\n') {
        cur_col = 0;
        if (++cur_row >= VGA_ROWS) vga_scroll();
    } else if (c == '\r') {
        cur_col = 0;
    } else if (c == '\b') {
        if (cur_col > 0) {
            cur_col--;
            vga[cur_row * VGA_COLS + cur_col] = ' ' | (VGA_WHITE << 8);
        }
    } else if (c == '\t') {
        int next = (cur_col + 8) & ~7;
        while (cur_col < next) vga[cur_row * VGA_COLS + cur_col++] = ' ' | (VGA_WHITE << 8);
    } else {
        vga[cur_row * VGA_COLS + cur_col++] = (uint8_t)c | (VGA_WHITE << 8);
        if (cur_col >= VGA_COLS) { cur_col = 0; if (++cur_row >= VGA_ROWS) vga_scroll(); }
    }
    vga_update_cursor();
}

void tty_puts(const char* s) { while (*s) tty_putchar(*s++); }

void tty_clear(void) {
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++) vga[i] = ' ' | (VGA_WHITE << 8);
    cur_col = cur_row = 0;
    vga_update_cursor();
}

// ===== シリアル出力 (QEMU デバッグ用) =====
static void serial_putchar(char c) {
    // COM1 ポーリング
    uint8_t status;
    do {
        asm volatile("inb %1, %0" : "=a"(status) : "d"((uint16_t)0x3F8 + 5));
    } while (!(status & 0x20));
    asm volatile("outb %0, %1" :: "a"((uint8_t)c), "d"((uint16_t)0x3F8));
}

void serial_puts(const char* s) { while (*s) { if (*s == '\n') serial_putchar('\r'); serial_putchar(*s++); } }

void serial_init(void) {
    asm volatile("outb %0, %1" :: "a"((uint8_t)0x00), "d"((uint16_t)0x3F9));
    asm volatile("outb %0, %1" :: "a"((uint8_t)0x80), "d"((uint16_t)0x3FB));
    asm volatile("outb %0, %1" :: "a"((uint8_t)0x03), "d"((uint16_t)0x3F8));
    asm volatile("outb %0, %1" :: "a"((uint8_t)0x00), "d"((uint16_t)0x3F9));
    asm volatile("outb %0, %1" :: "a"((uint8_t)0x03), "d"((uint16_t)0x3FB));
    asm volatile("outb %0, %1" :: "a"((uint8_t)0xC7), "d"((uint16_t)0x3FA));
    asm volatile("outb %0, %1" :: "a"((uint8_t)0x0B), "d"((uint16_t)0x3FC));
}

void kprintf(const char* fmt, ...);  // 後で定義

// ===== キーボード =====
#define KB_BUF_SIZE 256

static char kb_buf[KB_BUF_SIZE];
static int  kb_head = 0, kb_tail = 0;

static const char sc_normal[] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', 8,
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' ', 0,
};
static const char sc_shift[] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', 8,
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',
    0,  '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' ', 0,
};

static int shift_held = 0;
static int ctrl_held  = 0;

void keyboard_handler(uint8_t scancode) {
    if (scancode == 0x2A || scancode == 0x36) { shift_held = 1; return; }
    if (scancode == 0xAA || scancode == 0xB6) { shift_held = 0; return; }
    if (scancode == 0x1D) { ctrl_held = 1; return; }
    if (scancode == 0x9D) { ctrl_held = 0; return; }

    if (scancode & 0x80) return; // キー離し

    char c = 0;
    if (scancode < (int)sizeof(sc_normal)) {
        c = shift_held ? sc_shift[scancode] : sc_normal[scancode];
    }
    if (!c) return;

    // Ctrl+C → SIGINT (簡易)
    if (ctrl_held && c == 'c') {
        c = 3; // ETX
    }

    int next = (kb_head + 1) % KB_BUF_SIZE;
    if (next != kb_tail) {
        kb_buf[kb_head] = c;
        kb_head = next;
    }
}

// ブロッキング読み取り
char tty_getchar(void) {
    while (kb_head == kb_tail) {
        asm volatile("hlt"); // 割り込み待ち
    }
    char c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF_SIZE;
    return c;
}

// ラインバッファ付き読み取り (シェル用)
int tty_readline(char* buf, int maxlen) {
    int i = 0;
    while (1) {
        char c = tty_getchar();
        if (c == '\n' || c == '\r') {
            buf[i] = 0;
            tty_putchar('\n');
            return i;
        } else if (c == '\b' && i > 0) {
            i--;
            tty_putchar('\b');
        } else if (c == 3) { // Ctrl+C
            tty_puts("^C\n");
            buf[0] = 0;
            return -1;
        } else if (c >= 32 && i < maxlen - 1) {
            buf[i++] = c;
            tty_putchar(c);
        }
    }
}

// ===== TTY VFS ノード =====
static ssize_t tty_vfs_read(vnode_t* v, off_t off, size_t sz, void* buf) {
    (void)v; (void)off;
    char* b = (char*)buf;
    size_t n = 0;
    // ラインバッファ読み取り
    while (n < sz) {
        char c = tty_getchar();
        b[n++] = c;
        if (c == '\n' || c == '\r') break;
    }
    return (ssize_t)n;
}

static ssize_t tty_vfs_write(vnode_t* v, off_t off, size_t sz, const void* buf) {
    (void)v; (void)off;
    const char* b = (const char*)buf;
    for (size_t i = 0; i < sz; i++) {
        tty_putchar(b[i]);
        serial_putchar(b[i]);
    }
    return (ssize_t)sz;
}

static vnode_ops_t tty_vnode_ops = {
    .read  = tty_vfs_read,
    .write = tty_vfs_write,
};

static vnode_t tty_vnode = {
    .name  = "tty",
    .type  = VFS_CHARDEV,
    .ops   = &tty_vnode_ops,
};

vnode_t* tty_get_vnode(void) { return &tty_vnode; }
