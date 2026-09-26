/*
 * SYPAS kernel — Interrupt Descriptor Table and dispatch.
 *
 * All 32 CPU exceptions, 16 remapped legacy IRQ vectors (32-47) and the
 * 0x80 self-test gate route through isr.S into isr_dispatch().
 * Unhandled exceptions panic with a full register dump.
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
volatile u64 isr_test_hits;   /* incremented by the 0x80 self-test gate */

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

/* Stub symbols generated in isr.S */
#define DECL_STUB(v) extern void isr_stub_##v(void);
#define STUB(v) isr_stub_##v
DECL_STUB(0)  DECL_STUB(1)  DECL_STUB(2)  DECL_STUB(3)  DECL_STUB(4)
DECL_STUB(5)  DECL_STUB(6)  DECL_STUB(7)  DECL_STUB(8)  DECL_STUB(9)
DECL_STUB(10) DECL_STUB(11) DECL_STUB(12) DECL_STUB(13) DECL_STUB(14)
DECL_STUB(15) DECL_STUB(16) DECL_STUB(17) DECL_STUB(18) DECL_STUB(19)
DECL_STUB(20) DECL_STUB(21) DECL_STUB(22) DECL_STUB(23) DECL_STUB(24)
DECL_STUB(25) DECL_STUB(26) DECL_STUB(27) DECL_STUB(28) DECL_STUB(29)
DECL_STUB(30) DECL_STUB(31) DECL_STUB(32) DECL_STUB(33) DECL_STUB(34)
DECL_STUB(35) DECL_STUB(36) DECL_STUB(37) DECL_STUB(38) DECL_STUB(39)
DECL_STUB(40) DECL_STUB(41) DECL_STUB(42) DECL_STUB(43) DECL_STUB(44)
DECL_STUB(45) DECL_STUB(46) DECL_STUB(47) DECL_STUB(128)

static void (*const stubs[49])(void) = {
    STUB(0),  STUB(1),  STUB(2),  STUB(3),  STUB(4),  STUB(5),  STUB(6),
    STUB(7),  STUB(8),  STUB(9),  STUB(10), STUB(11), STUB(12), STUB(13),
    STUB(14), STUB(15), STUB(16), STUB(17), STUB(18), STUB(19), STUB(20),
    STUB(21), STUB(22), STUB(23), STUB(24), STUB(25), STUB(26), STUB(27),
    STUB(28), STUB(29), STUB(30), STUB(31), STUB(32), STUB(33), STUB(34),
    STUB(35), STUB(36), STUB(37), STUB(38), STUB(39), STUB(40), STUB(41),
    STUB(42), STUB(43), STUB(44), STUB(45), STUB(46), STUB(47), STUB(128),
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
    for (int i = 0; i < 48; i++)
        idt_set(i, stubs[i]);
    idt_set(128, stubs[48]);

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
    } else if (f->vector == 128) {
        isr_test_hits++;
    }
}
