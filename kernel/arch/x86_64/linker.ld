/* Peak OS kernel linker script (higher-half x86_64) */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(kernel_entry)

PHDRS
{
    text PT_LOAD FLAGS(5 /* R+X */);
    rodata PT_LOAD FLAGS(4 /* R */);
    data PT_LOAD FLAGS(6 /* R+W */);
}

SECTIONS
{
    . = 0xffffffff80000000;
    __kernel_start = .;

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
        *(COMMON)
        *(.bss .bss.*)
    } :data

    __kernel_end = .;

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
        *(.comment)
    }
}
