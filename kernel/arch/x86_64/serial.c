/*
 * SYPAS kernel — 16550 UART (COM1) debug output.
 *
 * First output path the kernel brings up; everything else can fail and
 * still be diagnosed.  115200 8N1, FIFO enabled.  Presence is verified
 * with a scratch-register + loopback test so machines without a COM1
 * simply skip serial output instead of writing to a dead port forever.
 */

#include "../../include/kernel.h"
#include "../../include/io.h"

#define COM1 0x3F8

#define REG_DATA    0  /* THR/RBR             */
#define REG_IER     1
#define REG_FIFO    2
#define REG_LCR     3
#define REG_MCR     4
#define REG_LSR     5
#define REG_SCRATCH 7

static bool present;

bool serial_init(void)
{
    /* Scratch register probe: absent port reads back bus float (0xFF). */
    outb(COM1 + REG_SCRATCH, 0x5A);
    if (inb(COM1 + REG_SCRATCH) != 0x5A)
        return false;

    outb(COM1 + REG_IER, 0x00);        /* no interrupts, we poll        */
    outb(COM1 + REG_LCR, 0x80);        /* DLAB on                       */
    outb(COM1 + REG_DATA, 0x01);       /* divisor 1 -> 115200 baud      */
    outb(COM1 + REG_IER, 0x00);
    outb(COM1 + REG_LCR, 0x03);        /* 8N1, DLAB off                 */
    outb(COM1 + REG_FIFO, 0xC7);       /* FIFO on, clear, 14-byte trig  */
    outb(COM1 + REG_MCR, 0x1E);        /* loopback for self-test        */
    outb(COM1 + REG_DATA, 0xA5);
    if (inb(COM1 + REG_DATA) != 0xA5)
        return false;
    outb(COM1 + REG_MCR, 0x0F);        /* normal operation, OUT2 set    */

    present = true;
    return true;
}

void serial_putc(char c)
{
    if (!present)
        return;
    if (c == '\n')
        serial_putc('\r');

    /* Bounded wait for THR empty so a wedged UART cannot hang the kernel */
    for (u32 i = 0; i < 100000; i++)
        if (inb(COM1 + REG_LSR) & 0x20)
            break;
    outb(COM1 + REG_DATA, (u8)c);
}
