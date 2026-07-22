/* Peak OS aarch64 higher-half kernel; physical load at 0x80000 (Pi kernel8.img). */
OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_start)

PHYS_BASE = 0x80000;
VIRT_BASE = 0xffffffff80000000;
HHDM      = 0xffff800000000000;

PHDRS
{
    boot PT_LOAD FLAGS(7);
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = VIRT_BASE;
    __kernel_start = .;
    __kernel_phys_start = PHYS_BASE;

    .boot : AT(PHYS_BASE) ALIGN(0x1000) {
        KEEP(*(.text.boot))
        . = ALIGN(16);
        KEEP(*(.boot.data))
        . = ALIGN(0x1000);
        /* Guard so kernel .text does not share stack-top address */
        . += 0x1000;
    } :boot

    .text : ALIGN(0x1000) {
        *(.text .text.*)
    } :text

    .rodata : ALIGN(0x1000) {
        *(.rodata .rodata.*)
    } :rodata

    .data : ALIGN(0x1000) {
        *(.data .data.*)
    } :data

    .bss : ALIGN(0x1000) {
        __bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        __bss_end = .;
    } :data

    __kernel_end = .;
    __kernel_phys_end = PHYS_BASE + (__kernel_end - VIRT_BASE);

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
        *(.comment)
    }
}
