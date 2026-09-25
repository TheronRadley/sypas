/*
 * SYPAS kernel linker script.
 *
 * v1: statically linked at physical 4 MiB, entered under the firmware's
 * identity mapping (SYPAS Boot Protocol v1).  The loader allocates this
 * exact range with AllocatePages(AllocateAddress); a higher-half virtual
 * layout arrives with the kernel's own page tables in Phase 4.
 */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_kstart)

KERNEL_BASE = 0x400000;

SECTIONS
{
    . = KERNEL_BASE;

    .text : ALIGN(4096) {
        __kernel_text_start = .;
        *(.text)
        *(.text.*)
        __kernel_text_end = .;
    }

    .rodata : ALIGN(4096) {
        *(.rodata)
        *(.rodata.*)
    }

    .data : ALIGN(4096) {
        *(.data)
        *(.data.*)
    }

    .bss : ALIGN(4096) {
        __kernel_bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        __kernel_bss_end = .;
    }

    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}
