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
 *   - 16-byte stack alignment at the call into C (SysV AMD64)
 *   - frame pointer zeroed so unwinding terminates cleanly
 *
 * The stack is the one the boot protocol hands over: a dedicated
 * 64 KiB loader-allocated stack (bi->stack_base/stack_size, type
 * SYPAS_MEM_LOADER, RSP at its top).  Keeping it is the contract —
 * allocating a second 64 KiB stack in .bss would waste memory and
 * make stack_base/stack_size dead protocol surface.  The kernel
 * switches to a kernel-owned stack when it builds its own page
 * tables (Phase 4).
 *
 * kmain() never returns; if it does, halt forever.
 */

    .section .text
    .globl _kstart
    .type  _kstart, @function
_kstart:
    cli
    cld
    andq    $-16, %rsp
    xorl    %ebp, %ebp
    call    kmain                    /* rdi = bootinfo, untouched */
1:  cli
    hlt
    jmp     1b
    .size _kstart, . - _kstart

    .section .note.GNU-stack, "", @progbits
