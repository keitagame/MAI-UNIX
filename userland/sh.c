// userland/sh.c - シェル実装
#include "../include/kernel/types.h"
#include "../include/kernel/vfs.h"
#include "../include/kernel/proc.h"
#include "../include/kernel/mm.h"

// libc関数プロトタイプ
extern size_t strlen(const char* s);
extern char*  strcpy(char* d, const char* s);
extern char*  strncpy(char* d, const char* s, size_t n);
extern int    strcmp(const char* a, const char* b);
extern int    strncmp(const char* a, const char* b, size_t n);
extern char*  strcat(char* d, const char* s);
extern char*  strchr(const char* s, int c);
extern char*  strrchr(const char* s, int c);
extern void*  memset(void* d, int v, size_t n);
extern void*  memcpy(void* d, const void* s, size_t n);
extern void*  malloc(size_t sz);
extern void   free(void* p);
extern int    printf(const char* fmt, ...);
extern int    snprintf(char* buf, size_t size, const char* fmt, ...);
extern int    open(const char* path, int flags);
extern int    close(int fd);
extern ssize_t read(int fd, void* buf, size_t sz);
extern ssize_t write(int fd, const void* buf, size_t sz);
extern void   exit(int code);
extern pid_t  getpid(void);
extern void   sleep(uint32_t secs);
extern int    tty_readline(char* buf, int maxlen);
extern void   tty_puts(const char* s);
extern void   tty_putchar(char c);
extern int    isspace(int c);
extern pid_t  proc_wait(pid_t pid, int* status);
extern process_t* proc_fork(void);
extern int    file_readdir(file_t* f, uint32_t index, char* name);
extern file_t* file_open(const char* path, int flags);
extern int    file_close(file_t* f);
extern vnode_t* vfs_lookup(const char* path);
extern process_t* current_proc;

#define MAX_ARGS 32
#define MAX_LINE 512
#define MAX_PATH 256

// ===== 組み込みコマンド =====

// cat: ファイル内容表示
static int cmd_cat(int argc, char** argv) {
    if (argc < 2) {
        // stdinから読む
        char buf[256];
        ssize_t n;
        while ((n = read(0, buf, sizeof(buf)-1)) > 0) {
            buf[n] = 0;
            write(1, buf, n);
        }
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("cat: %s: No such file\n", argv[i]);
            continue;
        }
        char buf[512];
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            write(1, buf, n);
        }
        close(fd);
    }
    return 0;
}

// ls: ディレクトリ一覧
static int cmd_ls(int argc, char** argv) {
    const char* path = (argc >= 2) ? argv[1] : current_proc->cwd;

    vnode_t* dir = vfs_lookup(path);
    if (!dir) { printf("ls: %s: No such directory\n", path); return 1; }
    if (dir->type != VFS_DIR) { printf("ls: %s: Not a directory\n", path); return 1; }

    file_t* f = file_open(path, O_RDONLY);
    if (!f) { printf("ls: %s: Cannot open\n", path); return 1; }

    char name[VFS_NAME_LEN];
    uint32_t idx = 0;
    while (file_readdir(f, idx++, name) == 0) {
        // ファイルかディレクトリかチェック
        char full[MAX_PATH];
        strcpy(full, path);
        if (full[strlen(full)-1] != '/') strcat(full, "/");
        strcat(full, name);
        vnode_t* node = vfs_lookup(full);
        if (node && node->type == VFS_DIR) {
            printf("\033[1;34m%s/\033[0m  ", name);
        } else {
            printf("%s  ", name);
        }
    }
    tty_putchar('\n');
    file_close(f);
    return 0;
}

// pwd: 現在ディレクトリ
static int cmd_pwd(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("%s\n", current_proc->cwd);
    return 0;
}

// cd: ディレクトリ移動
static int cmd_cd(int argc, char** argv) {
    const char* path = (argc >= 2) ? argv[1] : "/";
    vnode_t* node = vfs_lookup(path);
    if (!node || node->type != VFS_DIR) {
        printf("cd: %s: No such directory\n", path);
        return 1;
    }
    if (path[0] == '/') {
        strncpy(current_proc->cwd, path, sizeof(current_proc->cwd)-1);
    } else {
        char* cwd = current_proc->cwd;
        if (cwd[strlen(cwd)-1] != '/') strcat(cwd, "/");
        strcat(cwd, path);
    }
    // 末尾スラッシュ除去 (root以外)
    size_t len = strlen(current_proc->cwd);
    if (len > 1 && current_proc->cwd[len-1] == '/')
        current_proc->cwd[len-1] = 0;
    return 0;
}

// mkdir: ディレクトリ作成
static int cmd_mkdir(int argc, char** argv) {
    if (argc < 2) { printf("mkdir: missing operand\n"); return 1; }
    for (int i = 1; i < argc; i++) {
        // 親を特定
        const char* path = argv[i];
        char parent_path[MAX_PATH];
        const char* slash = strrchr(path, '/');
        if (!slash) { strcpy(parent_path, current_proc->cwd); slash = path - 1; }
        else {
            size_t plen = slash - path;
            memcpy(parent_path, path, plen); parent_path[plen] = 0;
            if (plen == 0) strcpy(parent_path, "/");
        }
        vnode_t* parent = vfs_lookup(parent_path);
        if (!parent || !parent->ops || !parent->ops->create) {
            printf("mkdir: %s: Cannot create\n", path);
            continue;
        }
        parent->ops->create(parent, slash + 1, VFS_DIR);
    }
    return 0;
}

// rm: ファイル削除
static int cmd_rm(int argc, char** argv) {
    if (argc < 2) { printf("rm: missing operand\n"); return 1; }
    for (int i = 1; i < argc; i++) {
        const char* path = argv[i];
        char parent_path[MAX_PATH];
        const char* slash = strrchr(path, '/');
        if (!slash) { strcpy(parent_path, current_proc->cwd); slash = path - 1; }
        else {
            size_t plen = slash - path;
            memcpy(parent_path, path, plen); parent_path[plen] = 0;
            if (plen == 0) strcpy(parent_path, "/");
        }
        vnode_t* parent = vfs_lookup(parent_path);
        if (!parent || !parent->ops || !parent->ops->unlink) {
            printf("rm: %s: No such file\n", path);
            continue;
        }
        if (parent->ops->unlink(parent, slash + 1) < 0)
            printf("rm: %s: Cannot remove\n", path);
    }
    return 0;
}

// echo: テキスト出力
static int cmd_echo(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) tty_putchar(' ');
        tty_puts(argv[i]);
    }
    tty_putchar('\n');
    return 0;
}

// uname: OS情報
static int cmd_uname(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("MyOS 1.0.0 x86 2024\n");
    return 0;
}

// ps: プロセス一覧
static int cmd_ps(int argc, char** argv) {
    (void)argc; (void)argv;
    extern process_t proc_table[];
    printf("  PID  PPID  STATE   NAME\n");
    printf("--------------------------------\n");
    for (int i = 0; i < MAX_PROCS; i++) {
        process_t* p = &proc_table[i];
        if (p->state == PROC_UNUSED) continue;
        const char* state_str = "?";
        switch (p->state) {
            case PROC_RUNNING:  state_str = "RUN  "; break;
            case PROC_READY:    state_str = "READY"; break;
            case PROC_BLOCKED:  state_str = "BLOCK"; break;
            case PROC_ZOMBIE:   state_str = "ZOMBI"; break;
            case PROC_SLEEPING: state_str = "SLEEP"; break;
            default: break;
        }
        printf("  %3d  %4d  %s  %s\n", p->pid, p->ppid, state_str, p->name);
    }
    return 0;
}

// help: コマンド一覧
static int cmd_help(int argc, char** argv) {
    (void)argc; (void)argv;
    tty_puts("MyOS Shell コマンド一覧:\n");
    tty_puts("  cat [file...]   - ファイル内容を表示\n");
    tty_puts("  cd [dir]        - ディレクトリ移動\n");
    tty_puts("  echo [args...]  - テキスト表示\n");
    tty_puts("  exit [code]     - シェル終了\n");
    tty_puts("  help            - このヘルプ\n");
    tty_puts("  ls [dir]        - ディレクトリ一覧\n");
    tty_puts("  mkdir <dir>     - ディレクトリ作成\n");
    tty_puts("  ps              - プロセス一覧\n");
    tty_puts("  pwd             - 現在のディレクトリ\n");
    tty_puts("  rm <file>       - ファイル削除\n");
    tty_puts("  sleep <secs>    - 指定秒スリープ\n");
    tty_puts("  uname           - OS情報\n");
    tty_puts("  write <file>    - テキストをファイルに書く\n");
    return 0;
}

// sleep: スリープ
static int cmd_sleep(int argc, char** argv) {
    if (argc < 2) { printf("sleep: missing operand\n"); return 1; }
    int secs = 0;
    const char* s = argv[1];
    while (*s >= '0' && *s <= '9') secs = secs * 10 + (*s++ - '0');
    sleep(secs);
    return 0;
}

// write: テキストをファイルに書く
static int cmd_write(int argc, char** argv) {
    if (argc < 2) { printf("write: usage: write <file> <text>\n"); return 1; }
    const char* path = argv[1];

    char buf[MAX_LINE] = "";
    for (int i = 2; i < argc; i++) {
        if (i > 2) strcat(buf, " ");
        strcat(buf, argv[i]);
    }
    if (argc == 2) {
        // stdin から
        tty_puts("Input text (empty line to end):\n");
        char line[256];
        while (tty_readline(line, sizeof(line)) >= 0 && strlen(line) > 0) {
            strcat(buf, line);
            strcat(buf, "\n");
        }
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("write: %s: Cannot open\n", path); return 1; }
    write(fd, buf, strlen(buf));
    close(fd);
    return 0;
}

// ===== コマンドテーブル =====
typedef struct { const char* name; int (*func)(int, char**); } cmd_entry_t;

static cmd_entry_t commands[] = {
    { "cat",   cmd_cat   },
    { "cd",    cmd_cd    },
    { "echo",  cmd_echo  },
    { "help",  cmd_help  },
    { "ls",    cmd_ls    },
    { "mkdir", cmd_mkdir },
    { "ps",    cmd_ps    },
    { "pwd",   cmd_pwd   },
    { "rm",    cmd_rm    },
    { "sleep", cmd_sleep },
    { "uname", cmd_uname },
    { "write", cmd_write },
    { NULL, NULL }
};

// ===== コマンドライン解析 =====
static int parse_args(char* line, char** argv) {
    int argc = 0;
    char* p = line;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        argv[argc++] = p;
        if (argc >= MAX_ARGS - 1) break;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) *p++ = 0;
    }
    argv[argc] = NULL;
    return argc;
}

// ===== シェルメイン =====
void shell_main(void) {
    char line[MAX_LINE];
    char* argv[MAX_ARGS];

    tty_puts("\n");
    tty_puts("  ___  ___      ___  ___ \n");
    tty_puts(" |\\  \\|\\  \\    /  /|/  /|\n");
    tty_puts(" \\ \\  \\ \\  \\  /  / /  / /\n");
    tty_puts("  \\ \\__\\ \\__\\/  / /  / / \n");
    tty_puts("   \\|__|\\|__|\\__\\/__/ /  \n");
    tty_puts("             \\|___|__|/   \n");
    tty_puts("\n");
    tty_puts(" MyOS Shell v1.0 - type 'help' for commands\n\n");

    while (1) {
        // プロンプト表示
        printf("\033[1;32mroot@myos\033[0m:\033[1;34m%s\033[0m$ ", current_proc->cwd);

        int n = tty_readline(line, sizeof(line));
        if (n < 0) continue;   // Ctrl+C
        if (n == 0) continue;  // 空行

        // パース
        int argc = parse_args(line, argv);
        if (argc == 0) continue;

        // exit は特別扱い
        if (strcmp(argv[0], "exit") == 0) {
            int code = (argc >= 2) ? 0 : 0;
            if (argc >= 2) {
                const char* s = argv[1];
                while (*s >= '0' && *s <= '9') code = code * 10 + (*s++ - '0');
            }
            printf("Goodbye!\n");
            exit(code);
            return;
        }

        // 組み込みコマンド検索
        int found = 0;
        for (int i = 0; commands[i].name; i++) {
            if (strcmp(argv[0], commands[i].name) == 0) {
                commands[i].func(argc, argv);
                found = 1;
                break;
            }
        }
        if (!found) {
            printf("sh: %s: command not found\n", argv[0]);
        }
    }
}
