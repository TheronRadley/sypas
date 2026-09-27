/*
 * SYPAS UEFI bootloader — bounded self-relocation.
 *
 * objcopy converts a base-0 ELF shared object to PE32+ but does not apply
 * its ELF relocations.  start.S therefore calls this before any relocated C
 * state is used.  Every dynamic-table-derived address is constrained to the
 * loaded image: malformed relocation metadata fails closed rather than
 * turning pre-initialization code into an unbounded memory writer.
 */

#include "reloc.h"

#define DT_NULL    0
#define DT_RELA    7
#define DT_RELASZ  8
#define DT_RELAENT 9

#define R_X86_64_RELATIVE 8

static int add_ok(uint64_t a, uint64_t b, uint64_t *out)
{
    if (a > UINT64_MAX - b)
        return 0;
    *out = a + b;
    return 1;
}

static int add_signed_ok(uint64_t base, int64_t addend, uint64_t *out)
{
    if (addend >= 0)
        return add_ok(base, (uint64_t)addend, out);

    /* Avoid negating INT64_MIN as a signed value. */
    uint64_t magnitude = 0 - (uint64_t)addend;
    if (base < magnitude)
        return 0;
    *out = base - magnitude;
    return 1;
}

uint64_t sypas_reloc(uint64_t base, uint64_t image_size,
                     const Elf64_Dyn *dynamic)
{
    uint64_t image_end;
    if (!image_size || !add_ok(base, image_size, &image_end))
        return 1;

    uint64_t dynamic_addr = (uint64_t)(uintptr_t)dynamic;
    if (dynamic_addr < base || dynamic_addr > image_end ||
        image_end - dynamic_addr < sizeof(Elf64_Dyn))
        return 1;

    uint64_t rela = 0, relasz = 0, relaent = sizeof(Elf64_Rela);
    int have_rela = 0, have_relasz = 0;
    uint64_t max_dynamic_entries =
        (image_end - dynamic_addr) / sizeof(Elf64_Dyn);
    int terminated = 0;

    for (uint64_t i = 0; i < max_dynamic_entries; i++) {
        const Elf64_Dyn *d = &dynamic[i];
        if (d->d_tag == DT_NULL) {
            terminated = 1;
            break;
        }
        switch (d->d_tag) {
        case DT_RELA:
            rela = d->d_val;
            have_rela = 1;
            break;
        case DT_RELASZ:
            relasz = d->d_val;
            have_relasz = 1;
            break;
        case DT_RELAENT:
            relaent = d->d_val;
            break;
        }
    }

    if (!terminated || have_rela != have_relasz)
        return 1;
    if (!have_rela)
        return 0;                      /* no relocation table */
    if (relaent != sizeof(Elf64_Rela) || relasz % relaent != 0)
        return 1;
    if (rela > image_size || relasz > image_size - rela)
        return 1;

    const Elf64_Rela *table = (const Elf64_Rela *)(uintptr_t)(base + rela);
    uint64_t count = relasz / relaent;
    for (uint64_t i = 0; i < count; i++) {
        const Elf64_Rela *r = &table[i];
        uint64_t target, value;

        if ((uint32_t)r->r_info != R_X86_64_RELATIVE)
            return 1;
        if (r->r_offset > image_size ||
            sizeof(uint64_t) > image_size - r->r_offset)
            return 1;
        if (!add_ok(base, r->r_offset, &target) ||
            !add_signed_ok(base, r->r_addend, &value))
            return 1;
        *(uint64_t *)(uintptr_t)target = value;
    }
    return 0;
}
