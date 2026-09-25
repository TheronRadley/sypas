/*
 * SYPAS kernel — 8253/8254 PIT timer.
 *
 * v1 time source: PIT channel 0 in rate-generator mode at PIT_HZ (100 Hz).
 * This is a bring-up timer, not the final SYPAS time architecture: the
 * time subsystem later moves to TSC-deadline / HPET with tickless idle
 * (see docs/technology-decisions.md).  100 Hz keeps idle overhead low
 * (100 wakeups/s) while giving 10 ms sleep granularity for boot code.
 */

#include "../../include/kernel.h"
#include "../../include/io.h"

#define PIT_CH0   0x40
#define PIT_CMD   0x43
#define PIT_INPUT_HZ 1193182UL

static volatile u64 ticks;

static void pit_irq(interrupt_frame_t *f)
{
    (void)f;
    ticks++;
}

void pit_init(void)
{
    u16 divisor = (u16)(PIT_INPUT_HZ / PIT_HZ);   /* 11931 -> 100.007 Hz */

    outb(PIT_CMD, 0x34);                 /* ch0, lobyte/hibyte, mode 2 */
    outb(PIT_CH0, divisor & 0xFF);
    outb(PIT_CH0, divisor >> 8);

    irq_install(0, pit_irq);
    pic_unmask(0);
}

u64 timer_ticks(void) { return ticks; }

u64 timer_ms(void) { return ticks * (1000 / PIT_HZ); }

/* Event-driven wait: halt until the next interrupt, no busy spinning. */
void sleep_ticks(u64 n)
{
    u64 end = ticks + n;
    while (ticks < end)
        __asm__ volatile("hlt");
}
