// proc/proc.c - プロセス管理
#include "../include/kernel/proc.h"
#include "../include/kernel/mm.h"
#include "../include/kernel/gdt.h"
#include "../include/kernel/types.h"

process_t  proc_table[MAX_PROCS];
process_t* current_proc = NULL;
uint32_t   ticks = 0;

extern void context_switch(uint32_t* old_esp, uint32_t new_esp);
extern void switch_to_user(uint32_t entry, uint32_t user_stack);

// カーネル文字列関数
static void kstrcpy(char* dst, const char* src) {
    while ((*dst++ = *src++));
}
static size_t kstrlen(const char* s) {
    size_t n = 0; while (s[n]) n++; return n;
}
static void kmemset(void* ptr, int val, size_t n) {
    uint8_t* p = (uint8_t*)ptr;
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)val;
}
static void kmemcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

static process_t* alloc_proc(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_table[i].state == PROC_UNUSED) {
            kmemset(&proc_table[i], 0, sizeof(process_t));
            return &proc_table[i];
        }
    }
    return NULL;
}

void proc_init(void) {
    kmemset(proc_table, 0, sizeof(proc_table));

    // idle/init プロセス (pid=0, カーネル)
    process_t* idle = &proc_table[0];
    idle->pid   = 0;
    idle->ppid  = 0;
    idle->state = PROC_RUNNING;
    idle->page_dir = vmm_get_kernel_directory();
    kstrcpy(idle->name, "idle");
    kstrcpy(idle->cwd, "/");

    current_proc = idle;
}

process_t* proc_create_kernel(void (*entry)(void), const char* name) {
    process_t* p = alloc_proc();
    if (!p) return NULL;

    static pid_t next_pid = 1;
    p->pid   = next_pid++;
    p->ppid  = current_proc ? current_proc->pid : 0;
    p->state = PROC_READY;
    p->page_dir = vmm_get_kernel_directory();
    kstrcpy(p->name, name);
    kstrcpy(p->cwd, "/");

    // カーネルスタックの初期化
    uint32_t stack_top = (uint32_t)&p->kernel_stack[8192];
    // 戻りアドレスとしてentryを積む
    stack_top -= 4;
    *(uint32_t*)stack_top = (uint32_t)entry;
    // context_switchでpopするレジスタ (edi,esi,ebx,ebp)
    stack_top -= 16;
    kmemset((void*)stack_top, 0, 16);
    p->esp = stack_top;
    p->kernel_stack_top = (uint32_t)&p->kernel_stack[8192];

    return p;
}

// スケジューラ (Round-Robin)
void scheduler_tick(void) {
    ticks++;

    // スリープ解除チェック
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_table[i].state == PROC_SLEEPING && ticks >= proc_table[i].sleep_until) {
            proc_table[i].state = PROC_READY;
        }
    }

    // 次のREADYプロセスを探す
    int cur_idx = current_proc - proc_table;
    int next_idx = -1;

    for (int i = 1; i <= MAX_PROCS; i++) {
        int idx = (cur_idx + i) % MAX_PROCS;
        if (proc_table[idx].state == PROC_READY) {
            next_idx = idx;
            break;
        }
    }

    if (next_idx < 0 || &proc_table[next_idx] == current_proc) return;

    process_t* prev = current_proc;
    process_t* next = &proc_table[next_idx];

    if (prev->state == PROC_RUNNING) prev->state = PROC_READY;
    next->state = PROC_RUNNING;
    current_proc = next;

    // TSS のカーネルスタック更新
    gdt_set_kernel_stack(next->kernel_stack_top);

    // アドレス空間切り替え
    vmm_switch(next->page_dir);

    // コンテキストスイッチ
    context_switch(&prev->esp, next->esp);
}

void proc_yield(void) {
    if (current_proc->state == PROC_RUNNING)
        current_proc->state = PROC_READY;
    scheduler_tick();
}

void proc_sleep(uint32_t ms) {
    // PIT 100Hz → 1tick=10ms
    current_proc->sleep_until = ticks + (ms / 10 + 1);
    current_proc->state = PROC_SLEEPING;
    proc_yield();
}

process_t* proc_get(pid_t pid) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_table[i].pid == pid && proc_table[i].state != PROC_UNUSED)
            return &proc_table[i];
    }
    return NULL;
}

process_t* proc_fork(void) {
    process_t* child = alloc_proc();
    if (!child) return NULL;

    static pid_t next_pid = 100;
    // 親をコピー
    kmemcpy(child, current_proc, sizeof(process_t));
    child->pid   = next_pid++;
    child->ppid  = current_proc->pid;
    child->state = PROC_READY;

    // アドレス空間クローン (CoW)
    child->page_dir = vmm_clone(current_proc->page_dir);

    // カーネルスタック再初期化 (forkから戻るようにセットアップ)
    uint32_t stack_top = (uint32_t)&child->kernel_stack[8192];
    // fork_returnというラベルから再開させる
    // 実際はesp保存点からコピーして子はeax=0で返す
    uint32_t stack_offset = current_proc->kernel_stack_top - current_proc->esp;
    stack_top -= stack_offset;
    kmemcpy((void*)stack_top,
            (void*)current_proc->esp,
            stack_offset);
    child->esp = stack_top;
    child->kernel_stack_top = (uint32_t)&child->kernel_stack[8192];

    return child;
}

void proc_exit(int code) {
    current_proc->state     = PROC_ZOMBIE;
    current_proc->exit_code = code;

    // 親を起こす
    process_t* parent = proc_get(current_proc->ppid);
    if (parent && parent->state == PROC_BLOCKED) {
        parent->state = PROC_READY;
    }

    // アドレス空間解放
    if (current_proc->page_dir != vmm_get_kernel_directory()) {
        vmm_destroy_directory(current_proc->page_dir);
        current_proc->page_dir = vmm_get_kernel_directory();
    }

    // FDクローズ
    // (VFS側でやるが、ここではスキップ)

    proc_yield();
    // ここには戻らない
    while(1) asm volatile("hlt");
}

pid_t proc_wait(pid_t pid, int* status) {
    while (1) {
        for (int i = 0; i < MAX_PROCS; i++) {
            process_t* p = &proc_table[i];
            if (p->state != PROC_ZOMBIE) continue;
            if (p->ppid != current_proc->pid) continue;
            if (pid != -1 && p->pid != pid) continue;

            pid_t ret = p->pid;
            if (status) *status = p->exit_code;
            p->state = PROC_UNUSED;
            return ret;
        }
        // 子がいないなら-1
        int has_child = 0;
        for (int i = 0; i < MAX_PROCS; i++) {
            if (proc_table[i].ppid == current_proc->pid &&
                proc_table[i].state != PROC_UNUSED) {
                has_child = 1; break;
            }
        }
        if (!has_child) return -1;

        current_proc->state = PROC_BLOCKED;
        proc_yield();
    }
}

void proc_kill(pid_t pid, int sig) {
    process_t* p = proc_get(pid);
    if (!p) return;
    p->pending_sigs |= (1u << sig);
    if (p->state == PROC_BLOCKED || p->state == PROC_SLEEPING)
        p->state = PROC_READY;
}
