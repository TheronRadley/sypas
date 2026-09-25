/*
 * SYPAS kernel — panic and diagnostic dump.
 *
 * A panic must tell the truth about the machine state: reason, vector,
 * error code, instruction pointer, full GPR set, control registers and a
 * short raw stack window.  Output goes to every registered console.
 */

#include "../include/kernel.h"

static void dump_frame(const interrupt_frame_t *f)
{
    u64 cr0, cr2, cr3, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));

    kprintf("vector=%lu error=0x%lx\n", f->vector, f->error_code);
    kprintf("RIP=%016lx CS=%04lx RFLAGS=%016lx\n", f->rip, f->cs, f->rflags);
    kprintf("RSP=%016lx SS=%04lx\n", f->rsp, f->ss);
    kprintf("RAX=%016lx RBX=%016lx RCX=%016lx\n", f->rax, f->rbx, f->rcx);
    kprintf("RDX=%016lx RSI=%016lx RDI=%016lx\n", f->rdx, f->rsi, f->rdi);
    kprintf("RBP=%016lx R8 =%016lx R9 =%016lx\n", f->rbp, f->r8, f->r9);
    kprintf("R10=%016lx R11=%016lx R12=%016lx\n", f->r10, f->r11, f->r12);
    kprintf("R13=%016lx R14=%016lx R15=%016lx\n", f->r13, f->r14, f->r15);
    kprintf("CR0=%016lx CR2=%016lx\nCR3=%016lx CR4=%016lx\n",
            cr0, cr2, cr3, cr4);

    /* Raw stack window (8 qwords) — enough to eyeball a call chain. */
    kprintf("stack:");
    const u64 *sp = (const u64 *)f->rsp;
    for (int i = 0; i < 8; i++)
        kprintf(" %016lx", sp[i]);
    kprintf("\n");
}

static void halt_forever(void) __attribute__((noreturn));
static void halt_forever(void)
{
    for (;;)
        __asm__ volatile("cli; hlt");
    __builtin_unreachable();
}

void panic_with_frame(const char *reason, const interrupt_frame_t *f)
{
    __asm__ volatile("cli");
    fbcon_set_colors(0x00FFB4AB, 0x00330A0A);   /* readable alert colors */
    kprintf("\n================ SYPAS KERNEL PANIC ================\n");
    kprintf("reason : %s\n", reason);
    dump_frame(f);
    kprintf("SYSTEM STATUS: HALTED\n");
    kprintf("====================================================\n");
    halt_forever();
}

void panic(const char *fmt, ...)
{
    __asm__ volatile("cli");
    fbcon_set_colors(0x00FFB4AB, 0x00330A0A);
    kprintf("\n================ SYPAS KERNEL PANIC ================\n");
    kprintf("reason : ");
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    kprintf("\nSYSTEM STATUS: HALTED\n");
    kprintf("====================================================\n");
    halt_forever();
}
