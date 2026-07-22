/* Physically contiguous page tables in the boot window (ADR-able from shim). */
.section .boot.data, "aw"
.align 12
.global boot_l0
boot_l0:
    .space 4096
.align 12
.global boot_l1_ident
boot_l1_ident:
    .space 4096
.align 12
.global boot_l1_hhdm
boot_l1_hhdm:
    .space 4096
.align 12
.global boot_l1_k
boot_l1_k:
    .space 4096
/* 4 shared L2 tables: 2 MiB blocks for the whole 0-4 GiB identity/HHDM range,
 * so peripheral MMIO can be mapped Device while DRAM stays Normal. */
.align 12
.global boot_l2_id
boot_l2_id:
    .space 4096 * 4
.align 12
.global boot_l2_k
boot_l2_k:
    .space 4096
/* 16 L3 tables → 32 MiB of 4K kernel mappings (covers .text/.rodata/.data/.bss). */
.align 12
.global boot_l3_k
boot_l3_k:
    .space 4096 * 16
.align 3
.global boot_info
boot_info:
    .space 512
.align 4
.global boot_kstack
boot_kstack:
    .space 65536
.global boot_kstack_top
boot_kstack_top:
