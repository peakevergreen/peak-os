/* Peak BIOS El Torito entry — runs at 0x7C00 (no-emulation).
 * Bytes 8..63: El Torito boot info table (patched by xorriso).
 */
.code16
.section .text.entry, "ax"
.global _start
.global bios_int_asm
.global bios_a20_enable
.global boot_drive
.global boot_info_table

.set STACK16, 0x7000

_start:
    jmp start16
    .byte 0x90, 0x90, 0x90, 0x90, 0x90, 0x90

boot_info_table:
    .long 0
    .long 0
    .long 0
    .long 0
    .space 40

start16:
    cli
    cld
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov $STACK16, %sp
    mov %dl, boot_drive

    /* Zero BSS (not present in the El Torito image bytes). */
    mov $_bss_start, %di
    mov $_bss_end, %cx
    sub %di, %cx
    xor %al, %al
    rep stosb

    call bios_a20_enable

    lgdt gdt_desc
    mov %cr0, %eax
    or $1, %eax
    mov %eax, %cr0
    ljmp $0x08, $pm32_entry

.code32
pm32_entry:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs
    mov $0x90000, %esp
    movzbl boot_drive, %eax
    push %eax
    call bios_main32
1:
    hlt
    jmp 1b

.code16
bios_a20_enable:
    in $0x92, %al
    or $2, %al
    out %al, $0x92
    ret

/*
 * void bios_int_asm(uint8_t int_no, struct bios_regs *regs);
 * regs MUST be in low memory (< 64 KiB).
 */
.code32
bios_int_asm:
    push %ebp
    mov %esp, %ebp
    pusha
    push %fs
    push %gs

    mov 8(%ebp), %eax
    mov %al, bios_int_no
    mov 12(%ebp), %eax
    mov %eax, bios_regs_ptr
    mov %esp, bios_save_esp

    cli
    sidt saved_idtr
    lidt real_idtr
    ljmp $0x18, $bios_int_16pm

.code16
bios_int_16pm:
    mov $0x20, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs
    mov %cr0, %eax
    and $~1, %eax
    mov %eax, %cr0
    /* Explicit 16-bit far jump (clang may emit 32-bit ljmp in .code16). */
    .byte 0xea
    .word bios_int_real
    .word 0x0000

bios_int_real:
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %ss
    mov $STACK16, %sp

    mov bios_regs_ptr, %bp
    movw 30(%bp), %ax
    mov %ax, %es
    movw 28(%bp), %ax
    mov %ax, %ds

    movl 0(%bp), %eax
    movl 4(%bp), %ebx
    movl 8(%bp), %ecx
    movl 12(%bp), %edx
    movl 16(%bp), %esi
    movl 20(%bp), %edi
    movl 24(%bp), %ebp

    .byte 0xCD
bios_int_no:
    .byte 0x10

    /* BIOS may clobber DS — save regs with DS=0. */
    pushf
    push %eax
    xor %ax, %ax
    mov %ax, %ds
    pop %eax
    movl %eax, bios_save_eax
    movl %ebx, bios_save_ebx
    movl %ecx, bios_save_ecx
    movl %edx, bios_save_edx
    movl %esi, bios_save_esi
    movl %edi, bios_save_edi
    movl %ebp, bios_save_ebp
    movw %es, bios_save_es
    popw bios_save_flags
    movw $0, bios_save_ds

    mov bios_regs_ptr, %bp
    movl bios_save_eax, %eax
    movl %eax, 0(%bp)
    movl bios_save_ebx, %eax
    movl %eax, 4(%bp)
    movl bios_save_ecx, %eax
    movl %eax, 8(%bp)
    movl bios_save_edx, %eax
    movl %eax, 12(%bp)
    movl bios_save_esi, %eax
    movl %eax, 16(%bp)
    movl bios_save_edi, %eax
    movl %eax, 20(%bp)
    movl bios_save_ebp, %eax
    movl %eax, 24(%bp)
    movw bios_save_ds, %ax
    movw %ax, 28(%bp)
    movw bios_save_es, %ax
    movw %ax, 30(%bp)
    movw bios_save_flags, %ax
    movw %ax, 32(%bp)

    cli
    lgdt gdt_desc
    mov %cr0, %eax
    or $1, %eax
    mov %eax, %cr0
    .byte 0xea
    .word bios_int_back32
    .word 0x0008

.code32
bios_int_back32:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs
    mov bios_save_esp, %esp
    lidt saved_idtr
    pop %gs
    pop %fs
    popa
    pop %ebp
    ret

.align 16
gdt:
    .quad 0
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
    .quad 0x00009A000000FFFF
    .quad 0x000092000000FFFF
    .quad 0x00AF9A000000FFFF
    .quad 0x00CF92000000FFFF

gdt_desc:
    .word gdt_end - gdt - 1
    .long gdt
gdt_end:

real_idtr:
    .word 0x03FF
    .long 0
saved_idtr:
    .word 0
    .long 0

.section .data
boot_drive:
    .byte 0
.align 4
bios_regs_ptr:
    .long 0
bios_save_esp:
    .long 0
bios_save_eax:
    .long 0
bios_save_ebx:
    .long 0
bios_save_ecx:
    .long 0
bios_save_edx:
    .long 0
bios_save_esi:
    .long 0
bios_save_edi:
    .long 0
bios_save_ebp:
    .long 0
bios_save_ds:
    .word 0
bios_save_es:
    .word 0
bios_save_flags:
    .word 0
