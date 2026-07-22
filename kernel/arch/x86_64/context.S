/* Cooperative kernel context switch (x86_64 SysV).
 * void context_switch(uint64_t **old_sp, uint64_t *new_sp);
 * Saves callee-saved regs + rsp into *old_sp, loads new_sp, restores, returns.
 */
.global context_switch
context_switch:
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, (%rdi)
    mov %rsi, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx
    ret
