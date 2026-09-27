/*
 * SYPAS kernel — Interrupt Descriptor Table and dispatch.
 *
 * All 256 vectors receive a real gate.  Exceptions, legacy PIC IRQs and the
 * private software self-test vector have dedicated behaviour; anything else
 * enters a diagnostic panic with the vector preserved in interrupt_frame_t.
 */

#include "../../include/kernel.h"

#define IDT_ENTRIES 256

struct idt_entry {
    u16 offset_lo;
    u16 selector;
    u8  ist;
    u8  type_attr;
    u16 offset_mid;
    u32 offset_hi;
    u32 zero;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr   idtp;

static void (*irq_handlers[16])(interrupt_frame_t *);
volatile u64 isr_test_hits;   /* incremented by the private self-test gate */

/* Defined by isr.S.  One stub per vector keeps unexpected-vector reports
 * truthful without needing a second decoding convention in the dispatcher. */
extern void (*const isr_stub_table[IDT_ENTRIES])(void);

static const char *exception_names[32] = {
    "#DE divide error",            "#DB debug",
    "NMI non-maskable interrupt",  "#BP breakpoint",
    "#OF overflow",                "#BR bound range exceeded",
    "#UD invalid opcode",          "#NM device not available",
    "#DF double fault",            "coprocessor segment overrun",
    "#TS invalid TSS",             "#NP segment not present",
    "#SS stack-segment fault",     "#GP general protection fault",
    "#PF page fault",              "reserved(15)",
    "#MF x87 floating-point",      "#AC alignment check",
    "#MC machine check",           "#XM SIMD floating-point",
    "#VE virtualization",          "#CP control protection",
    "reserved(22)", "reserved(23)", "reserved(24)", "reserved(25)",
    "reserved(26)", "reserved(27)", "#HV hypervisor injection",
    "#VC VMM communication",       "#SX security",  "reserved(31)",
};

static void idt_set(int vec, void (*stub)(void))
{
    u64 addr = (u64)stub;
    idt[vec].offset_lo  = addr & 0xFFFF;
    idt[vec].selector   = 0x08;          /* kernel code segment */
    idt[vec].ist        = 0;             /* no IST until TSS exists */
    idt[vec].type_attr  = 0x8E;          /* present, ring0, interrupt gate */
    idt[vec].offset_mid = (addr >> 16) & 0xFFFF;
    idt[vec].offset_hi  = (u32)(addr >> 32);
    idt[vec].zero       = 0;
}

void idt_init(void)
{
    for (int i = 0; i < IDT_ENTRIES; i++)
        idt_set(i, isr_stub_table[i]);

    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (u64)&idt;
    __asm__ volatile("lidt %0" : : "m"(idtp));
}

void irq_install(u8 irq, void (*handler)(interrupt_frame_t *))
{
    if (irq < 16)
        irq_handlers[irq] = handler;
}

void isr_dispatch(interrupt_frame_t *f)
{
    if (f->vector < 32) {
        panic_with_frame(exception_names[f->vector], f);
    } else if (f->vector < 48) {
        u8 irq = (u8)(f->vector - 32);

        /* Spurious IRQ7/IRQ15 first: pic_handle_spurious() checks the
         * in-service register and performs the asymmetric EOI rules
         * itself (none for 7, master-only for 15).  A spurious vector
         * must not reach a handler or get a normal EOI. */
        if (pic_handle_spurious(irq))
            return;

        if (irq_handlers[irq])
            irq_handlers[irq](f);
        pic_send_eoi(irq);
    } else if (f->vector == SYPAS_TEST_VECTOR) {
        isr_test_hits++;
    } else {
        panic_with_frame("unexpected interrupt vector", f);
    }
}
