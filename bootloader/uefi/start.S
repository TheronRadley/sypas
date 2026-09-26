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
 *   Returns 0 on success, nonzero if the image contains a relocation
 *   type it cannot apply (build error — refuse to run corrupt code).
 *
 * efi_main is declared __attribute__((ms_abi)) in C and is called with
 * a proper Microsoft x64 frame: this wrapper, as the caller,
 * reserves the 32-byte home/shadow area for the four register argument
 * slots that the MS-ABI callee is entitled to spill into, with RSP
 * 16-byte aligned at the call.  We do not rely on any particular
 * firmware tolerating a missing home area.  efi_main's EFI_STATUS is
 * returned to the firmware.
 */

    .text
    .globl _start
    .type  _start, @function
_start:
    /* Firmware CALLed us: RSP % 16 == 8 here. */
    pushq   %rcx                    /* save ImageHandle   (rsp%16 -> 0) */
    pushq   %rdx                    /* save SystemTable   (rsp%16 -> 8) */
    subq    $8, %rsp                /* align for SysV call (rsp%16 -> 0) */

    leaq    ImageBase(%rip), %rdi   /* actual load base */
    leaq    _DYNAMIC(%rip),  %rsi   /* dynamic section  */
    call    sypas_reloc
    testq   %rax, %rax
    jnz     .Lbad_reloc

    addq    $8, %rsp
    popq    %rdx                    /* restore SystemTable  */
    popq    %rcx                    /* restore ImageHandle  */

    /* MS-ABI call: 32-byte home space + 8 to align (rsp%16 was 8). */
    subq    $40, %rsp
    call    efi_main
    addq    $40, %rsp
    ret                             /* EFI_STATUS in RAX -> firmware */

.Lbad_reloc:
    /* Unsupported relocation: the image would run with unrelocated
     * pointers.  Refuse cleanly instead of executing garbage.  We can't
     * print (nothing is relocated yet), so return EFI_LOAD_ERROR. */
    addq    $24, %rsp               /* drop pad + saved rdx/rcx */
    movabsq $0x8000000000000001, %rax   /* EFI_LOAD_ERROR */
    ret
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
