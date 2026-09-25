/*
 * SYPAS kernel — entry point.
 *
 * ABI (SYPAS Boot Protocol v1):
 *   Entered from the SYPAS bootloader in 64-bit long mode.
 *   RDI = physical address of sypas_bootinfo_t  (preserved into kmain)
 *   Interrupts disabled, firmware identity paging active.
 *
 * This stub only establishes a known-good execution state:
 *   - direction flag cleared (SysV requires DF=0 at function entry)
 *   - kernel-owned stack (we do not trust the loader stack size forever)
 *   - 16-byte stack alignment at the call into C (SysV AMD64)
 *   - frame pointer zeroed so unwinding terminates cleanly
 * kmain() never returns; if it does, halt forever.
 */

    .section .text
    .globl _kstart
    .type  _kstart, @function
_kstart:
    cli
    cld
    movabs  $kboot_stack_top, %rsp
    andq    $-16, %rsp
    xorl    %ebp, %ebp
    call    kmain                    /* rdi = bootinfo, untouched */
1:  cli
    hlt
    jmp     1b
    .size _kstart, . - _kstart

    /* 64 KiB boot stack in .bss, 16-byte aligned */
    .section .bss
    .align 16
    .globl kboot_stack
kboot_stack:
    .skip 65536
    .globl kboot_stack_top
kboot_stack_top:

    .section .note.GNU-stack, "", @progbits
