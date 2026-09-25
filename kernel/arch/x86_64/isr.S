/*
 * SYPAS kernel — interrupt/exception entry stubs.
 *
 * ABI contract with idt.c / the C dispatcher:
 *   - Each vector stub pushes a dummy error code when the CPU does not
 *     supply one, then the vector number, then jumps to the common stub.
 *   - The common stub saves all general-purpose registers in the order
 *     matching interrupt_frame_t (kernel.h) and calls
 *     isr_dispatch(interrupt_frame_t *frame)   [System V AMD64: RDI]
 *   - On return every register is restored and iretq resumes the
 *     interrupted context.  SysV callee-saved guarantees are irrelevant
 *     here: we save/restore *everything* because the interrupted code
 *     never consented to a call.
 *
 * Vectors with CPU-pushed error codes: 8, 10, 11, 12, 13, 14, 17, 21, 29, 30.
 */

    .section .text

.macro ISR_NOERR vec
    .globl isr_stub_\vec
isr_stub_\vec:
    pushq   $0              /* fake error code */
    pushq   $\vec
    jmp     isr_common
.endm

.macro ISR_ERR vec
    .globl isr_stub_\vec
isr_stub_\vec:
    pushq   $\vec           /* CPU already pushed the error code */
    jmp     isr_common
.endm

/* CPU exceptions 0-31 */
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

/* Remapped legacy IRQs 32-47 */
ISR_NOERR 32
ISR_NOERR 33
ISR_NOERR 34
ISR_NOERR 35
ISR_NOERR 36
ISR_NOERR 37
ISR_NOERR 38
ISR_NOERR 39
ISR_NOERR 40
ISR_NOERR 41
ISR_NOERR 42
ISR_NOERR 43
ISR_NOERR 44
ISR_NOERR 45
ISR_NOERR 46
ISR_NOERR 47

/* Vector 0x80: software-interrupt self-test gate (future syscall vector) */
ISR_NOERR 128

isr_common:
    pushq   %rax
    pushq   %rbx
    pushq   %rcx
    pushq   %rdx
    pushq   %rsi
    pushq   %rdi
    pushq   %rbp
    pushq   %r8
    pushq   %r9
    pushq   %r10
    pushq   %r11
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15

    movq    %rsp, %rdi      /* frame pointer -> arg0 */
    cld                     /* SysV: DF must be clear */
    call    isr_dispatch

    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %r11
    popq    %r10
    popq    %r9
    popq    %r8
    popq    %rbp
    popq    %rdi
    popq    %rsi
    popq    %rdx
    popq    %rcx
    popq    %rbx
    popq    %rax
    addq    $16, %rsp       /* drop vector + error code */
    iretq

    .section .note.GNU-stack, "", @progbits
