// libc/libc.c - ユーザーランド用最小Cライブラリ
// (カーネルにリンクして、ユーザープログラムはカーネル内で動かす簡易実装)

#include "../include/kernel/types.h"
#include "../include/kernel/vfs.h"
#include "../include/kernel/proc.h"

// システムコール番号
#define SYS_EXIT    1
#define SYS_FORK    2
#define SYS_READ    3
#define SYS_WRITE   4
#define SYS_OPEN    5
#define SYS_CLOSE   6
#define SYS_WAITPID 7
#define SYS_EXECVE  11
#define SYS_CHDIR   12
#define SYS_GETPID  20
#define SYS_MKDIR   39
#define SYS_UNLINK  10
#define SYS_LSEEK   19
#define SYS_KILL    37
#define SYS_DUP2    63
#define SYS_SLEEP   162
#define SYS_READDIR 89
#define SYS_GETCWD  183
#define SYS_GETPPID 64

// ===== 内部で直接関数を呼ぶ (カーネル空間のユーザープログラム) =====
// カーネル内で実行するため、システムコールの代わりに直接呼ぶ

extern file_t* file_open(const char* path, int flags);
extern int     file_close(file_t* f);
extern ssize_t file_read(file_t* f, void* buf, size_t size);
extern ssize_t file_write(file_t* f, const void* buf, size_t size);
extern int     file_readdir(file_t* f, uint32_t index, char* name);
extern void    proc_exit(int code);
extern process_t* proc_fork(void);
extern pid_t   proc_wait(pid_t pid, int* status);
extern void    proc_sleep(uint32_t ms);
extern void    tty_putchar(char c);
extern int     tty_readline(char* buf, int maxlen);
extern void    tty_puts(const char* s);
extern vnode_t* vfs_lookup(const char* path);
extern process_t* current_proc;
extern void    kfree(void* ptr);
extern void*   kmalloc(size_t size);

// ===== 文字列関数 =====
size_t strlen(const char* s) {
    size_t n = 0; while (s[n]) n++; return n;
}

char* strcpy(char* dst, const char* src) {
    char* d = dst; while ((*d++ = *src++)); return dst;
}

char* strncpy(char* dst, const char* src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
    return dst;
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

char* strcat(char* dst, const char* src) {
    char* d = dst + strlen(dst);
    while ((*d++ = *src++));
    return dst;
}

char* strchr(const char* s, int c) {
    while (*s) { if (*s == (char)c) return (char*)s; s++; }
    return (c == 0) ? (char*)s : NULL;
}

char* strrchr(const char* s, int c) {
    const char* last = NULL;
    while (*s) { if (*s == (char)c) last = s; s++; }
    return (char*)last;
}

char* strstr(const char* hay, const char* needle) {
    size_t nl = strlen(needle);
    for (; *hay; hay++) if (strncmp(hay, needle, nl) == 0) return (char*)hay;
    return NULL;
}

void* memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst; const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void* memset(void* dst, int val, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)val;
    return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* p = (const uint8_t*)a, *q = (const uint8_t*)b;
    for (size_t i = 0; i < n; i++) if (p[i] != q[i]) return p[i] - q[i];
    return 0;
}

// ===== 数値変換 =====
int atoi(const char* s) {
    int n = 0, neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return neg ? -n : n;
}

// ===== 出力 =====
// sprintf / printf 簡易実装
static char* itoa_buf(int32_t val, char* buf, int base, int unsign) {
    static const char digits[] = "0123456789abcdef";
    char tmp[32]; int i = 0;
    uint32_t uval = unsign ? (uint32_t)val : (val < 0 ? (uint32_t)(-val) : (uint32_t)val);
    if (uval == 0) tmp[i++] = '0';
    else while (uval) { tmp[i++] = digits[uval % base]; uval /= base; }
    if (!unsign && val < 0) tmp[i++] = '-';
    int j = 0;
    while (i--) buf[j++] = tmp[i];
    buf[j] = 0;
    return buf;
}

int vsnprintf(char* buf, size_t size, const char* fmt, __builtin_va_list ap) {
    size_t pos = 0;
    char tmp[64];

    #define PUT(c) do { if (pos < size-1) buf[pos++] = (c); } while(0)

    while (*fmt) {
        if (*fmt != '%') { PUT(*fmt++); continue; }
        fmt++;
        int zero_pad = 0, width = 0;
        if (*fmt == '0') { zero_pad = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');

        char c = *fmt++;
        const char* s = NULL;
        switch (c) {
        case 'd': {
            int v = __builtin_va_arg(ap, int);
            itoa_buf(v, tmp, 10, 0);
            s = tmp; break;
        }
        case 'u': {
            uint32_t v = __builtin_va_arg(ap, uint32_t);
            itoa_buf((int32_t)v, tmp, 10, 1);
            s = tmp; break;
        }
        case 'x': case 'X': {
            uint32_t v = __builtin_va_arg(ap, uint32_t);
            itoa_buf((int32_t)v, tmp, 16, 1);
            s = tmp; break;
        }
        case 'p': {
            uint32_t v = __builtin_va_arg(ap, uint32_t);
            tmp[0]='0'; tmp[1]='x';
            itoa_buf((int32_t)v, tmp+2, 16, 1);
            s = tmp; break;
        }
        case 's': s = __builtin_va_arg(ap, const char*); if (!s) s="(null)"; break;
        case 'c': { char ch = (char)__builtin_va_arg(ap, int); PUT(ch); continue; }
        case '%': PUT('%'); continue;
        default: PUT('%'); PUT(c); continue;
        }
        if (s) {
            int slen = 0; const char* t = s; while (*t++) slen++;
            while (width > slen) { PUT(zero_pad ? '0' : ' '); width--; }
            while (*s) PUT(*s++);
        }
    }
    buf[pos] = 0;
    return (int)pos;
    #undef PUT
}

int snprintf(char* buf, size_t size, const char* fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int r = vsnprintf(buf, size, fmt, ap);
    __builtin_va_end(ap);
    return r;
}

// カーネルprintfはtty + serial に出力
extern void tty_putchar(char c);
extern void serial_putchar(char c);

static void kputs_both(const char* s) {
    while (*s) {
        tty_putchar(*s);
        (void)*s++;
    }
}

void kprintf(const char* fmt, ...) {
    char buf[512];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    __builtin_va_end(ap);
    kputs_both(buf);
}

int printf(const char* fmt, ...) {
    char buf[512];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    __builtin_va_end(ap);
    // fd=1 (stdout) に書く
    file_t* f = current_proc ? current_proc->fds[1] : NULL;
    if (f) file_write(f, buf, n);
    return n;
}

// ===== ファイルI/O ラッパー =====
int open(const char* path, int flags) {
    file_t* f = file_open(path, flags);
    if (!f) return -1;
    for (int i = 0; i < MAX_FDS; i++) {
        if (!current_proc->fds[i]) {
            current_proc->fds[i] = f;
            return i;
        }
    }
    file_close(f);
    return -1;
}

int close(int fd) {
    if (fd < 0 || fd >= MAX_FDS || !current_proc->fds[fd]) return -1;
    file_close(current_proc->fds[fd]);
    current_proc->fds[fd] = NULL;
    return 0;
}

ssize_t read(int fd, void* buf, size_t count) {
    if (fd < 0 || fd >= MAX_FDS || !current_proc->fds[fd]) return -1;
    return file_read(current_proc->fds[fd], buf, count);
}

ssize_t write(int fd, const void* buf, size_t count) {
    if (fd < 0 || fd >= MAX_FDS || !current_proc->fds[fd]) return -1;
    return file_write(current_proc->fds[fd], buf, count);
}

// ===== malloc =====
void* malloc(size_t size) { return kmalloc(size); }
void  free(void* ptr)     { kfree(ptr); }
void* realloc(void* ptr, size_t size) { return krealloc(ptr, size); }

// ===== プロセス =====
void exit(int code) { proc_exit(code); }
pid_t getpid(void)  { return current_proc->pid; }
void sleep(uint32_t secs) { proc_sleep(secs * 1000); }

// ===== 文字判定 =====
int isspace(int c) { return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v'; }
int isdigit(int c) { return c>='0' && c<='9'; }
int isalpha(int c) { return (c>='a'&&c<='z') || (c>='A'&&c<='Z'); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int toupper(int c) { return (c>='a'&&c<='z') ? c-32 : c; }
int tolower(int c) { return (c>='A'&&c<='Z') ? c+32 : c; }