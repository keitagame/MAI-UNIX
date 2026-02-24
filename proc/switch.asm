; proc/switch.asm - コンテキストスイッチ
global context_switch

; void context_switch(uint32_t *old_esp, uint32_t new_esp);
context_switch:
    ; 引数取得
    mov eax, [esp + 4]   ; old_esp
    mov ecx, [esp + 8]   ; new_esp

    ; 現在のレジスタをスタックに保存
    push ebp
    push ebx
    push esi
    push edi

    ; 現在のESPを保存
    mov [eax], esp

    ; 新しいESPに切り替え
    mov esp, ecx

    ; 新しいプロセスのレジスタを復元
    pop edi
    pop esi
    pop ebx
    pop ebp

    ret

; void switch_to_user(uint32_t entry, uint32_t user_stack);
global switch_to_user
switch_to_user:
    mov ecx, [esp + 4]  ; entry point
    mov edx, [esp + 8]  ; user stack

    ; ユーザーデータセグメント
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; iretのためのスタックフレーム構築
    push 0x23       ; SS (user data)
    push edx        ; ESP (user stack)
    push 0x200      ; EFLAGS (IF=1)
    push 0x1B       ; CS (user code)
    push ecx        ; EIP

    iret

; void flush_tss();
global flush_tss
flush_tss:
    mov ax, 0x2B   ; TSS selector
    ltr ax
    ret
