/* Raspberry Pi / QEMU aarch64 entry (physical PC at load address 0x80000). */
.section .text.boot, "ax"
.global _start
.global __boot_dtb
.global __boot_stack_top

.extern boot_shim_main

_start:
    /* PC is physical; ADR yields physical addresses in this boot window. */
    adr x1, __boot_dtb
    str x0, [x1]

    mrs x1, mpidr_el1
    and x1, x1, #0xFF
    cbnz x1, .Lpark

    /* Drop to EL1 from EL3 or EL2 — set SP only after arriving in EL1 */
    mrs x1, CurrentEL
    lsr x1, x1, #2
    cmp x1, #3
    b.eq .Lel3
    cmp x1, #2
    b.eq .Lel2
    b .Lel1

.Lel3:
    msr cptr_el3, xzr
    msr cptr_el2, xzr
    mov x1, #0x401
    msr scr_el3, x1
    mov x1, #(1 << 31)
    msr hcr_el2, x1
    msr sctlr_el2, xzr
    msr sctlr_el1, xzr
    mov x1, #0x3c5
    msr spsr_el3, x1
    adr x1, .Lel1
    msr elr_el3, x1
    eret

.Lel2:
    msr cptr_el2, xzr
    mov x1, #(1 << 31)
    msr hcr_el2, x1
    msr sctlr_el1, xzr
    mov x1, #0x3c5
    msr spsr_el2, x1
    adr x1, .Lel1
    msr elr_el2, x1
    eret

.Lel1:
    adr x1, __boot_stack_top
    mov sp, x1
    msr daifset, #0xf
    /* Enable FP/SIMD for C (compiler may use NEON) */
    mov x1, #(3 << 20)
    msr cpacr_el1, x1
    isb
    /* Early PL011 marker */
    mov x1, #0x3F200000
    movk x1, #0x1000
    str wzr, [x1, #0x30]
    mov w2, #0x7FF
    str w2, [x1, #0x44]
    mov w2, #26
    str w2, [x1, #0x24]
    mov w2, #3
    str w2, [x1, #0x28]
    mov w2, #(3 << 5)
    str w2, [x1, #0x2c]
    mov w2, #0x301
    str w2, [x1, #0x30]
    mov w2, #'P'
    str w2, [x1]
    mov w2, #'k'
    str w2, [x1]
    mov w2, #'\n'
    str w2, [x1]
    bl boot_shim_main
.Lhang:
    wfe
    b .Lhang

.Lpark:
    wfe
    b .Lpark

.section .boot.data, "aw"
.align 4
__boot_dtb:
    .quad 0
.align 16
.space 65536
__boot_stack_top:
