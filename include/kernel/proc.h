// include/kernel/proc.h
#pragma once
#include "types.h"
#include "mm.h"
#include "vfs.h"

#define MAX_FDS       32
#define MAX_PROCS     64
#define PROC_NAME_LEN 32
#define USER_STACK_TOP 0xBFFFF000
#define USER_STACK_PAGES 4

typedef enum {
    PROC_UNUSED  = 0,
    PROC_RUNNING = 1,
    PROC_READY   = 2,
    PROC_BLOCKED = 3,
    PROC_ZOMBIE  = 4,
    PROC_SLEEPING = 5,
} proc_state_t;

struct file; // 前方宣言

typedef struct process {
    pid_t          pid;
    pid_t          ppid;
    proc_state_t   state;

    // レジスタコンテキスト
    uint32_t       esp;         // カーネルスタック上のESP
    uint32_t       kernel_stack_top;

    // アドレス空間
    page_directory_t* page_dir;

    // ファイルディスクリプタ
    file_t*   fds[MAX_FDS];

    // 待機
    int            exit_code;
    pid_t          wait_pid;    // waitしている対象

    // シグナル
    uint32_t       pending_sigs;
    uint32_t       sig_mask;
    uint32_t       sig_handlers[32];

    // スリープ
    uint32_t       sleep_until; // tick数

    // プログラム名
    char           name[PROC_NAME_LEN];

    // 作業ディレクトリ
    char           cwd[256];

    // カーネルスタック (各プロセス専用)
    uint8_t        kernel_stack[8192];
} process_t;

extern process_t* current_proc;
extern uint32_t   ticks;

void proc_init(void);
process_t* proc_create_kernel(void (*entry)(void), const char* name);
process_t* proc_fork(void);
int        proc_exec(const char* path, char* const argv[]);
void       proc_exit(int code);
pid_t      proc_wait(pid_t pid, int* status);
void       proc_sleep(uint32_t ms);
void       proc_yield(void);
void       proc_kill(pid_t pid, int sig);
void       scheduler_tick(void);
process_t* proc_get(pid_t pid);
