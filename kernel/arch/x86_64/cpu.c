/*
 * SYPAS kernel — CPU identification via CPUID.
 */

#include "../../include/kernel.h"

static inline void cpuid(u32 leaf, u32 subleaf, u32 *a, u32 *b, u32 *c, u32 *d)
{
    __asm__ volatile("cpuid"
                     : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf), "c"(subleaf));
}

void cpu_identify(cpu_info_t *info)
{
    u32 a, b, c, d;

    /* Vendor string: EBX,EDX,ECX of leaf 0 */
    cpuid(0, 0, &a, &b, &c, &d);
    ((u32 *)info->vendor)[0] = b;
    ((u32 *)info->vendor)[1] = d;
    ((u32 *)info->vendor)[2] = c;
    info->vendor[12] = 0;

    cpuid(1, 0, &a, &b, &c, &d);
    info->stepping = a & 0xF;
    info->model    = ((a >> 4) & 0xF) | ((a >> 12) & 0xF0);
    info->family   = ((a >> 8) & 0xF) + ((a >> 20) & 0xFF);
    info->has_tsc  = d & (1u << 4);
    info->has_apic = d & (1u << 9);
    info->has_sse2 = d & (1u << 26);

    /* Extended features */
    cpuid(0x80000000, 0, &a, &b, &c, &d);
    u32 max_ext = a;

    info->has_nx = false;
    info->has_1gb_pages = false;
    if (max_ext >= 0x80000001) {
        cpuid(0x80000001, 0, &a, &b, &c, &d);
        info->has_nx        = d & (1u << 20);
        info->has_1gb_pages = d & (1u << 26);
    }

    /* Brand string, leaves 0x80000002..4 */
    info->brand[0] = 0;
    if (max_ext >= 0x80000004) {
        u32 *p = (u32 *)info->brand;
        for (u32 leaf = 0x80000002; leaf <= 0x80000004; leaf++) {
            cpuid(leaf, 0, &a, &b, &c, &d);
            *p++ = a; *p++ = b; *p++ = c; *p++ = d;
        }
        info->brand[48] = 0;
    }
}
