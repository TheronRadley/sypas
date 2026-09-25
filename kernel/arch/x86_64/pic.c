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
