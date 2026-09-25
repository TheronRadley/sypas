/*
 * SYPAS kernel — Global Descriptor Table.
 *
 * v1 layout (ring 0 only; user segments and TSS arrive with the process
 * model in a later phase):
 *   0x00  null
 *   0x08  kernel code  (64-bit, execute/read)
 *   0x10  kernel data  (read/write)
 */

#include "../../include/kernel.h"

#define GDT_KCODE 0x08
#define GDT_KDATA 0x10

struct gdt_entry {
    u16 limit_lo;
    u16 base_lo;
    u8  base_mid;
    u8  access;
    u8  gran;
    u8  base_hi;
} __attribute__((packed));

struct gdt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

static struct gdt_entry gdt[3];
static struct gdt_ptr   gp;

static void gdt_set(int i, u8 access, u8 gran)
{
    /* Base/limit are ignored in long mode; flags are what matters. */
    gdt[i].limit_lo = 0xFFFF;
    gdt[i].base_lo  = 0;
    gdt[i].base_mid = 0;
    gdt[i].access   = access;
    gdt[i].gran     = gran;
    gdt[i].base_hi  = 0;
}

void gdt_init(void)
{
    gdt_set(0, 0, 0);
    gdt_set(1, 0x9A, 0xA0);   /* present|ring0|code|RX,  L=1        */
    gdt_set(2, 0x92, 0xC0);   /* present|ring0|data|RW,  G=1, D/B=1 */

    gp.limit = sizeof(gdt) - 1;
    gp.base  = (u64)&gdt;

    /* Load GDT, then reload CS via far-return and data segments directly.
     * lretq pops RIP then CS: push the new selector, push the target. */
    __asm__ volatile(
        "lgdt %0\n\t"
        "pushq %[kcode]\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "movl %[kdata], %%eax\n\t"
        "movl %%eax, %%ds\n\t"
        "movl %%eax, %%es\n\t"
        "movl %%eax, %%ss\n\t"
        "xorl %%eax, %%eax\n\t"       /* fs/gs: null until per-CPU data */
        "movl %%eax, %%fs\n\t"
        "movl %%eax, %%gs\n\t"
        :
        : "m"(gp), [kcode] "i"(GDT_KCODE), [kdata] "i"(GDT_KDATA)
        : "rax", "memory");
}
