// userland/exec.c - プログラム実行 (カーネル内組み込みプログラム)
#include "../include/kernel/types.h"
#include "../include/kernel/proc.h"
#include "../include/kernel/vfs.h"

extern void shell_main(void);

typedef struct {
    const char* name;
    void (*entry)(void);
} builtin_program_t;

static builtin_program_t builtins[] = {
    { "/bin/sh",   shell_main },
    { "/bin/bash", shell_main },
    { NULL, NULL }
};

int exec_program(const char* path, char* const argv[]) {
    (void)argv;
    for (int i = 0; builtins[i].name; i++) {
        // 簡易strcmp
        const char* a = path, *b = builtins[i].name;
        while (*a && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) {
            builtins[i].entry();
            return 0;
        }
    }
    return -ENOENT;
}
