/*
 * SYPAS UEFI bootloader — PE entry point and self-relocation trampoline.
 *
 * The loader is built as a position-independent ELF shared object and then
 * converted to a PE32+ EFI application with objcopy.  Because objcopy does
 * not process ELF relocations, the image relocates itself at runtime before
 * any C code that touches global pointers runs.
 *
 * ABI notes
 * =========
 * Entry (called by firmware, Microsoft x64 calling convention):
 *   RCX = EFI_HANDLE        ImageHandle
 *   RDX = EFI_SYSTEM_TABLE* SystemTable
 *   RSP = 16-byte aligned before the CALL (so RSP % 16 == 8 here)
 *
 * sypas_reloc(base, dynamic) is System V AMD64:
 *   RDI = image load base, RSI = &_DYNAMIC
 *   Clobbers caller-saved SysV registers; RCX/RDX are caller-saved in
 *   SysV, so they are preserved on the stack across the call.
 *
 * efi_main is declared __attribute__((ms_abi)) in C and receives the
 * original RCX/RDX via a tail jump (the firmware return address is still
 * on the stack, so efi_main returns straight to the firmware).
 */

    .text
    .globl _start
    .type  _start, @function
_start:
    pushq   %rcx                    /* save ImageHandle   (rsp%16 -> 0) */
    pushq   %rdx                    /* save SystemTable   (rsp%16 -> 8) */
    subq    $8, %rsp                /* align for SysV call (rsp%16 -> 0) */

    leaq    ImageBase(%rip), %rdi   /* actual load base */
    leaq    _DYNAMIC(%rip),  %rsi   /* dynamic section  */
    call    sypas_reloc

    addq    $8, %rsp
    popq    %rdx                    /* restore SystemTable  */
    popq    %rcx                    /* restore ImageHandle  */
    jmp     efi_main                /* tail call, ms_abi    */
    .size _start, . - _start

/*
 * Dummy PE base-relocation block.
 *
 * The image is fully self-relocating, so it needs no PE .reloc entries,
 * but some UEFI implementations refuse images whose relocation directory
 * is empty (they treat them as non-relocatable).  A single empty block
 * marks the image as relocatable.
 */
    .data
.dummy_reloc_target:
    .long 0

    .section .reloc, "a"
.reloc_block:
    .long .dummy_reloc_target - .reloc_block   /* PageRVA           */
    .long 10                                   /* BlockSize         */
    .word 0                                    /* one null entry    */

    .section .note.GNU-stack, "", @progbits
