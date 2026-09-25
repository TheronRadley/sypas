/*
 * Linker script for the SYPAS UEFI bootloader.
 *
 * The loader links as a base-0 ELF shared object; objcopy then converts
 * it to PE32+ (efi-app-x86_64).  Section layout keeps everything the
 * runtime needs (.dynamic, .rela, symbol tables) inside sections that
 * objcopy carries over, page-aligned as PE requires.
 */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0;
    ImageBase = .;

    . = 0x1000;
    .text : {
        *(.text)
        *(.text.*)
    }

    . = ALIGN(0x1000);
    .reloc : {
        *(.reloc)
    }

    . = ALIGN(0x1000);
    .data : {
        *(.rodata)
        *(.rodata.*)
        *(.data)
        *(.data.*)
        *(.got)
        *(.got.plt)
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    . = ALIGN(0x1000);
    .dynamic : { *(.dynamic) }

    . = ALIGN(0x1000);
    .rela : {
        *(.rela.dyn)
        *(.rela.text*)
        *(.rela.data*)
        *(.rela.got)
    }

    . = ALIGN(0x1000);
    .dynsym  : { *(.dynsym) }
    . = ALIGN(0x1000);
    .dynstr  : { *(.dynstr) }
    . = ALIGN(0x1000);
    .hash    : { *(.hash) }
    .gnu.hash : { *(.gnu.hash) }

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}
