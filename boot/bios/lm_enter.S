/* Enter long mode and jump to kernel_entry(bootinfo).
 * params_* filled by C before call. void bios_enter_long_mode(void);
 */
.code32
.section .text
.global bios_enter_long_mode
.global params_pml4, params_entry, params_stack, params_bootinfo

bios_enter_long_mode:
    cli

    mov %cr4, %eax
    or $0x20, %eax
    mov %eax, %cr4

    mov params_pml4, %eax
    mov %eax, %cr3

    mov $0xC0000080, %ecx
    rdmsr
    or $0x100, %eax
    wrmsr

    mov %cr0, %eax
    or $0x80000000, %eax
    mov %eax, %cr0

    ljmp $0x28, $lm64_start

.code64
lm64_start:
    mov $0x30, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs
    /* Enable SSE before any kernel code runs — the kernel is compiled with
     * -msse2, so even early init (fb_init etc.) may use xmm registers.
     * Without CR4.OSFXSR every SSE insn raises #UD (UEFI firmware already
     * enables this; BIOS must do it here). */
    mov %cr0, %rax
    and $~0xC, %rax        /* clear EM (bit 2) and TS (bit 3) */
    or  $0x2, %rax         /* set MP */
    mov %rax, %cr0
    mov %cr4, %rax
    or  $0x600, %rax       /* OSFXSR | OSXMMEXCPT */
    mov %rax, %cr4
    mov params_stack(%rip), %rsp
    mov params_bootinfo(%rip), %rdi
    mov params_entry(%rip), %rax
    xor %ebp, %ebp
    /* SysV ABI: entry %rsp must be ≡ 8 (mod 16), as if reached via call.
     * Push a NULL fake return address so jmp'ing in keeps alignment and
     * terminates backtraces. */
    pushq $0
    jmp *%rax

.section .data
.align 8
params_pml4:
    .quad 0
params_entry:
    .quad 0
params_stack:
    .quad 0
params_bootinfo:
    .quad 0
