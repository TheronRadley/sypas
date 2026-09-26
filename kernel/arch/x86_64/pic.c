/*
 * SYPAS kernel — legacy 8259A PIC support.
 *
 * v1 uses the dual 8259 because it is simple, universal, and enough to
 * prove real interrupt delivery (PIT ticks).  The APIC/IOAPIC transition
 * happens in the SMP phase; this module then survives only as the
 * "mask everything" quirk handler.
 *
 * IRQ 0-7  -> vectors 32-39
 * IRQ 8-15 -> vectors 40-47
 */

#include "../../include/kernel.h"
#include "../../include/io.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_INIT 0x11   /* init + ICW4 needed */
#define ICW4_8086 0x01
#define PIC_EOI   0x20
#define OCW3_ISR  0x0B   /* next read returns the In-Service Register */

static u64 spurious_irqs;   /* counted for diagnostics */

void pic_init(void)
{
    /* Remap: master to 0x20, slave to 0x28 */
    outb(PIC1_CMD, ICW1_INIT); io_wait();
    outb(PIC2_CMD, ICW1_INIT); io_wait();
    outb(PIC1_DATA, 0x20);     io_wait();   /* master vector offset */
    outb(PIC2_DATA, 0x28);     io_wait();   /* slave vector offset  */
    outb(PIC1_DATA, 0x04);     io_wait();   /* slave on IRQ2        */
    outb(PIC2_DATA, 0x02);     io_wait();   /* cascade identity     */
    outb(PIC1_DATA, ICW4_8086); io_wait();
    outb(PIC2_DATA, ICW4_8086); io_wait();

    /* Mask everything; subsystems unmask what they own. */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_unmask(u8 irq)
{
    u16 port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    u8 bit   = irq < 8 ? irq : irq - 8;
    outb(port, inb(port) & ~(1 << bit));
    if (irq >= 8)
        pic_unmask(2);   /* cascade line must be open too */
}

void pic_send_eoi(u8 irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

static u8 pic_read_isr(u16 cmd_port)
{
    outb(cmd_port, OCW3_ISR);
    return inb(cmd_port);
}

/*
 * Spurious IRQ detection (8259A datasheet behavior): a masked or
 * de-asserted line can still deliver the lowest-priority vector — IRQ7
 * on the master, IRQ15 on the slave — with NO in-service bit set.
 *
 * Handling differs from a real interrupt and from each other:
 *   spurious IRQ7  -> no EOI at all (nothing is in service)
 *   spurious IRQ15 -> EOI the MASTER only (the cascade line IRQ2 was
 *                     genuinely raised on the master; the slave has
 *                     nothing in service)
 *
 * Returns true if the IRQ was spurious and fully handled here; the
 * dispatcher must then neither run a handler nor send a normal EOI.
 */
bool pic_handle_spurious(u8 irq)
{
    if (irq == 7) {
        if (pic_read_isr(PIC1_CMD) & 0x80)
            return false;                 /* real IRQ7 */
        spurious_irqs++;
        return true;                      /* no EOI */
    }
    if (irq == 15) {
        if (pic_read_isr(PIC2_CMD) & 0x80)
            return false;                 /* real IRQ15 */
        spurious_irqs++;
        outb(PIC1_CMD, PIC_EOI);          /* master saw IRQ2 for real */
        return true;
    }
    return false;
}

u64 pic_spurious_count(void)
{
    return spurious_irqs;
}
