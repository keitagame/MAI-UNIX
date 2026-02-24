// syscall/syscall.c - システムコール実装
#include "../include/kernel/idt.h"
#include "../include/kernel/proc.h"
#include "../include/kernel/vfs.h"
#include "../include/kernel/mm.h"
#include "../include/kernel/types.h"

// システムコール番号 (Linux互換)
#define SYS_EXIT    1
#define SYS_FORK    2
#define SYS_READ    3
#define SYS_WRITE   4
#define SYS_OPEN    5
#define SYS_CLOSE   6
#define SYS_WAITPID 7
#define SYS_EXECVE  11
#define SYS_GETPID  20
#define SYS_GETPPID 64
#define SYS_SLEEP   162
#define SYS_CHDIR   12
#define SYS_GETCWD  183
#define SYS_READDIR 89
#define SYS_STAT    106
#define SYS_MKDIR   39
#define SYS_UNLINK  10
#define SYS_LSEEK   19
#define SYS_BRK     45
#define SYS_KILL    37
#define SYS_DUP2    63

extern void tty_putchar(char c);
extern vnode_t* tty_get_vnode(void);
extern void ramfs_mkdir(vnode_t* parent, const char* name);
extern void kprintf(const char* fmt, ...);
extern void kstrcpy_safe(char* dst, const char* src, size_t max);

// 文字列関数
static size_t kstrlen(const char* s) { size_t n=0; while(s[n]) n++; return n; }
static void   kstrcpy(char* d, const char* s) { while((*d++=*s++)); }
static int    kstrcmp(const char* a, const char* b) {
    while(*a && *a==*b){a++;b++;} return (unsigned char)*a-(unsigned char)*b;
}
static void   kmemset(void* p, int v, size_t n) {
    uint8_t* q=(uint8_t*)p; for(size_t i=0;i<n;i++) q[i]=(uint8_t)v;
}

// ファイルディスクリプタ管理
static file_t* fd_get(int fd) {
    if (fd < 0 || fd >= MAX_FDS || !current_proc) return NULL;
    return current_proc->fds[fd];
}

static int fd_alloc(file_t* f) {
    for (int i = 0; i < MAX_FDS; i++) {
        if (!current_proc->fds[i]) {
            current_proc->fds[i] = f;
            return i;
        }
    }
    return -EMFILE;
}

// ===== システムコール実装 =====

// 1: exit
static int32_t sys_exit(int32_t code) {
    proc_exit(code);
    return 0; // 戻らない
}

// 2: fork
static int32_t sys_fork(regs_t* r) {
    process_t* child = proc_fork();
    if (!child) return -ENOMEM;
    // 子プロセスにはeax=0を返す
    // コンテキストスイッチ後に子から再開する
    // 子のespが指すフレームのeaxを0にする
    regs_t* child_regs = (regs_t*)child->esp;
    child_regs->eax = 0;
    return (int32_t)child->pid;
}

// 3: read
static int32_t sys_read(int fd, void* buf, size_t count) {
    file_t* f = fd_get(fd);
    if (!f) return -EBADF;
    return (int32_t)file_read(f, buf, count);
}

// 4: write
static int32_t sys_write(int fd, const void* buf, size_t count) {
    file_t* f = fd_get(fd);
    if (!f) return -EBADF;
    return (int32_t)file_write(f, buf, count);
}

// 5: open
static int32_t sys_open(const char* path, int flags, uint32_t mode) {
    (void)mode;
    // CWD解決
    char full_path[VFS_PATH_LEN];
    if (path[0] != '/') {
        kstrcpy(full_path, current_proc->cwd);
        if (full_path[kstrlen(full_path)-1] != '/')
            full_path[kstrlen(full_path)] = '/', full_path[kstrlen(full_path)+1] = 0;
        // 末尾に追記
        char* p = full_path + kstrlen(full_path);
        const char* s = path;
        while (*s) *p++ = *s++;
        *p = 0;
    } else {
        kstrcpy(full_path, path);
    }

    file_t* f = file_open(full_path, flags);
    if (!f) return -ENOENT;
    int fd = fd_alloc(f);
    if (fd < 0) { file_close(f); return fd; }
    return fd;
}

// 6: close
static int32_t sys_close(int fd) {
    file_t* f = fd_get(fd);
    if (!f) return -EBADF;
    current_proc->fds[fd] = NULL;
    return file_close(f);
}

// 7: waitpid
static int32_t sys_waitpid(pid_t pid, int* status, int options) {
    (void)options;
    return (int32_t)proc_wait(pid, status);
}

// 11: execve (簡易: カーネル内ELFは非実装、組み込みバイナリを使用)
extern int exec_program(const char* path, char* const argv[]);
static int32_t sys_execve(const char* path, char* const argv[], char* const envp[]) {
    (void)envp;
    return (int32_t)exec_program(path, argv);
}

// 12: chdir
static int32_t sys_chdir(const char* path) {
    vnode_t* node = vfs_lookup(path);
    if (!node || node->type != VFS_DIR) return -ENOENT;
    if (path[0] == '/') kstrcpy(current_proc->cwd, path);
    else {
        char* cwd = current_proc->cwd;
        if (cwd[kstrlen(cwd)-1] != '/') {
            cwd[kstrlen(cwd)] = '/';
            cwd[kstrlen(cwd)+1] = 0;
        }
        const char* p = path;
        char* c = cwd + kstrlen(cwd);
        while (*p) *c++ = *p++;
        *c = 0;
    }
    return 0;
}

// 20: getpid
static int32_t sys_getpid(void) { return (int32_t)current_proc->pid; }
static int32_t sys_getppid(void) { return (int32_t)current_proc->ppid; }

// 39: mkdir
static int32_t sys_mkdir(const char* path, uint32_t mode) {
    (void)mode;
    // 親ディレクトリを見つける
    const char* slash = path + kstrlen(path) - 1;
    while (slash > path && *slash != '/') slash--;

    char parent_path[VFS_PATH_LEN];
    size_t plen = slash - path;
    if (plen == 0) { kstrcpy(parent_path, "/"); }
    else { for(size_t i=0;i<plen;i++) parent_path[i]=path[i]; parent_path[plen]=0; }

    vnode_t* parent = vfs_lookup(parent_path);
    if (!parent) return -ENOENT;
    const char* dname = (*slash == '/') ? slash + 1 : slash;
    if (parent->ops && parent->ops->create)
        return (int32_t)parent->ops->create(parent, dname, VFS_DIR);
    return -EIO;
}

// 10: unlink
static int32_t sys_unlink(const char* path) {
    const char* slash = path + kstrlen(path) - 1;
    while (slash > path && *slash != '/') slash--;
    char parent_path[VFS_PATH_LEN];
    size_t plen = slash - path;
    if (plen == 0) kstrcpy(parent_path, "/");
    else { for(size_t i=0;i<plen;i++) parent_path[i]=path[i]; parent_path[plen]=0; }
    vnode_t* parent = vfs_lookup(parent_path);
    if (!parent || !parent->ops || !parent->ops->unlink) return -ENOENT;
    const char* fname = (*slash == '/') ? slash + 1 : path;
    return (int32_t)parent->ops->unlink(parent, fname);
}

// 19: lseek
static int32_t sys_lseek(int fd, off_t offset, int whence) {
    file_t* f = fd_get(fd);
    if (!f) return -EBADF;
    return (int32_t)file_seek(f, offset, whence);
}

// 37: kill
static int32_t sys_kill(pid_t pid, int sig) {
    proc_kill(pid, sig);
    return 0;
}

// 63: dup2
static int32_t sys_dup2(int oldfd, int newfd) {
    file_t* f = fd_get(oldfd);
    if (!f) return -EBADF;
    if (newfd < 0 || newfd >= MAX_FDS) return -EBADF;
    if (current_proc->fds[newfd]) file_close(current_proc->fds[newfd]);
    current_proc->fds[newfd] = f;
    f->ref++;
    return newfd;
}

// 162: sleep (秒)
static int32_t sys_sleep(uint32_t seconds) {
    proc_sleep(seconds * 1000);
    return 0;
}

// 89: readdir
static int32_t sys_readdir(int fd, uint32_t index, char* name_out) {
    file_t* f = fd_get(fd);
    if (!f) return -EBADF;
    return (int32_t)file_readdir(f, index, name_out);
}

// 183: getcwd
static int32_t sys_getcwd(char* buf, size_t size) {
    if (!buf) return -EINVAL;
    size_t len = kstrlen(current_proc->cwd);
    if (len >= size) return -ENOMEM;
    kstrcpy(buf, current_proc->cwd);
    return (int32_t)len;
}

// ===== ディスパッチャ =====
void syscall_dispatch(regs_t* r) {
    int32_t ret = -ENOSYS;

    switch (r->eax) {
    case SYS_EXIT:    ret = sys_exit((int)r->ebx); break;
    case SYS_FORK:    ret = sys_fork(r); break;
    case SYS_READ:    ret = sys_read((int)r->ebx, (void*)r->ecx, (size_t)r->edx); break;
    case SYS_WRITE:   ret = sys_write((int)r->ebx, (void*)r->ecx, (size_t)r->edx); break;
    case SYS_OPEN:    ret = sys_open((char*)r->ebx, (int)r->ecx, r->edx); break;
    case SYS_CLOSE:   ret = sys_close((int)r->ebx); break;
    case SYS_WAITPID: ret = sys_waitpid((pid_t)r->ebx, (int*)r->ecx, (int)r->edx); break;
    case SYS_EXECVE:  ret = sys_execve((char*)r->ebx, (char**)r->ecx, (char**)r->edx); break;
    case SYS_CHDIR:   ret = sys_chdir((char*)r->ebx); break;
    case SYS_GETPID:  ret = sys_getpid(); break;
    case SYS_GETPPID: ret = sys_getppid(); break;
    case SYS_MKDIR:   ret = sys_mkdir((char*)r->ebx, r->ecx); break;
    case SYS_UNLINK:  ret = sys_unlink((char*)r->ebx); break;
    case SYS_LSEEK:   ret = sys_lseek((int)r->ebx, (off_t)r->ecx, (int)r->edx); break;
    case SYS_KILL:    ret = sys_kill((pid_t)r->ebx, (int)r->ecx); break;
    case SYS_DUP2:    ret = sys_dup2((int)r->ebx, (int)r->ecx); break;
    case SYS_SLEEP:   ret = sys_sleep(r->ebx); break;
    case SYS_READDIR: ret = sys_readdir((int)r->ebx, r->ecx, (char*)r->edx); break;
    case SYS_GETCWD:  ret = sys_getcwd((char*)r->ebx, (size_t)r->ecx); break;
    default: break;
    }

    r->eax = (uint32_t)ret;
}

// isr_handler から syscall ディスパッチ (int 0x80 = 128)
// → drivers/irq.c の isr_handler に追加する形で呼ばれる