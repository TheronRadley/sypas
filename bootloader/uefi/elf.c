/*
 * SYPAS UEFI bootloader — ELF64 kernel image validation and load planning.
 *
 * See elf.h for the contract.  Every arithmetic step that could overflow
 * a uint64_t is checked explicitly; nothing in the file is trusted until
 * it has been proven in-bounds.  This file must stay freestanding (no
 * libc, no UEFI) so the host unit tests exercise the exact boot code.
 */

#include "elf.h"

#define ELF_PAGE      4096ULL
#define ELF_PAGE_MASK (ELF_PAGE - 1)

/* a + b, or false on wraparound */
static int add_ok(uint64_t a, uint64_t b, uint64_t *out)
{
    if (a > UINT64_MAX - b)
        return 0;
    *out = a + b;
    return 1;
}

static int power_of_two(uint64_t n)
{
    return n && !(n & (n - 1));
}

elf_status_t elf_plan_load(const uint8_t *file, uint64_t fsize,
                           elf_load_plan_t *plan)
{
    plan->nsegs = 0;
    plan->phys_base = UINT64_MAX;
    plan->phys_end = 0;

    /* ---- ELF header --------------------------------------------------- */
    if (fsize < sizeof(Elf64_Ehdr))
        return ELF_ERR_TRUNCATED;

    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)file;

    if (eh->e_magic != ELF_MAGIC)
        return ELF_ERR_MAGIC;
    if (eh->e_class != ELFCLASS64)
        return ELF_ERR_CLASS;
    if (eh->e_data != ELFDATA2LSB)
        return ELF_ERR_ENDIAN;
    if (eh->e_iversion != EV_CURRENT || eh->e_version != EV_CURRENT)
        return ELF_ERR_VERSION;
    if (eh->e_machine != EM_X86_64)
        return ELF_ERR_MACHINE;
    if (eh->e_type != ET_EXEC)
        return ELF_ERR_TYPE;
    if (eh->e_ehsize != sizeof(Elf64_Ehdr))
        return ELF_ERR_EHSIZE;

    /* ---- Program header table ----------------------------------------- */
    if (eh->e_phentsize != sizeof(Elf64_Phdr))
        return ELF_ERR_PHENTSIZE;
    if (eh->e_phnum == 0)
        return ELF_ERR_NO_SEGMENTS;

    uint64_t pht_bytes = (uint64_t)eh->e_phnum * sizeof(Elf64_Phdr);
    uint64_t pht_end;
    if (!add_ok(eh->e_phoff, pht_bytes, &pht_end) || pht_end > fsize)
        return ELF_ERR_PHDR_BOUNDS;

    const Elf64_Phdr *ph = (const Elf64_Phdr *)(file + eh->e_phoff);

    /* ---- PT_LOAD segments ---------------------------------------------- */
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD)
            continue;

        if (ph[i].p_filesz > ph[i].p_memsz)
            return ELF_ERR_SEG_SIZES;

        /* ELF permits p_align = 0 or 1.  Otherwise it is a power of two
         * and virtual addresses have the same residue as file offsets.
         * v1 identity mapping below makes this a physical-address
         * congruence too. */
        if (ph[i].p_align > 1 &&
            (!power_of_two(ph[i].p_align) ||
             ((ph[i].p_vaddr - ph[i].p_offset) & (ph[i].p_align - 1))))
            return ELF_ERR_SEG_ALIGN;

        /* SYPAS v1 is explicitly identity-mapped.  This is not a generic
         * ELF rule; it is a kernel-image contract enforced before load. */
        if (ph[i].p_vaddr != ph[i].p_paddr)
            return ELF_ERR_SEG_VADDR;

        /* Preserve exactly the permission intent that VMM will later turn
         * into page permissions.  v1 refuses W+X rather than normalizing a
         * dangerous image silently. */
        if ((ph[i].p_flags & ~(PF_R | PF_W | PF_X)) ||
            ((ph[i].p_flags & (PF_W | PF_X)) == (PF_W | PF_X)))
            return ELF_ERR_SEG_FLAGS;

        /* A zero-byte PT_LOAD does not allocate memory, but all of its
         * structural policy has still been checked above. */
        if (ph[i].p_memsz == 0)
            continue;

        if (plan->nsegs == ELF_MAX_SEGMENTS)
            return ELF_ERR_TOO_MANY_SEGS;

        /* File range: p_offset + p_filesz must stay inside the file. */
        uint64_t file_end;
        if (!add_ok(ph[i].p_offset, ph[i].p_filesz, &file_end))
            return ELF_ERR_SEG_OVERFLOW;
        if (file_end > fsize)
            return ELF_ERR_SEG_BOUNDS;

        /* Memory range: p_paddr + p_memsz, then page rounding, must not
         * wrap the physical address space. */
        uint64_t mem_end, page_end;
        if (!add_ok(ph[i].p_paddr, ph[i].p_memsz, &mem_end))
            return ELF_ERR_SEG_OVERFLOW;
        if (!add_ok(mem_end, ELF_PAGE_MASK, &page_end))
            return ELF_ERR_SEG_OVERFLOW;
        page_end &= ~ELF_PAGE_MASK;

        elf_segment_t *seg = &plan->segs[plan->nsegs];
        seg->paddr     = ph[i].p_paddr;
        seg->page_base = ph[i].p_paddr & ~ELF_PAGE_MASK;
        seg->page_end  = page_end;
        seg->offset    = ph[i].p_offset;
        seg->filesz    = ph[i].p_filesz;
        seg->memsz     = ph[i].p_memsz;
        seg->flags     = ph[i].p_flags;

        /* Page-granular overlap against every accepted segment: the
         * loader allocates whole pages per segment, so sharing a page
         * is a load error, not a tolerable quirk. */
        for (int j = 0; j < plan->nsegs; j++) {
            const elf_segment_t *o = &plan->segs[j];
            if (seg->page_base < o->page_end && o->page_base < seg->page_end)
                return ELF_ERR_SEG_OVERLAP;
        }

        if (seg->page_base < plan->phys_base)
            plan->phys_base = seg->page_base;
        if (seg->page_end > plan->phys_end)
            plan->phys_end = seg->page_end;
        plan->nsegs++;
    }

    if (plan->nsegs == 0)
        return ELF_ERR_NO_SEGMENTS;
    if (plan->phys_end - plan->phys_base > ELF_MAX_KERNEL_SPAN)
        return ELF_ERR_TOO_BIG;

    /* ---- Entry point ----------------------------------------------------
     * The handoff must start in actual file-backed executable code, never
     * in a non-executable segment or a zero-filled BSS tail. */
    plan->entry = eh->e_entry;
    for (int i = 0; i < plan->nsegs; i++) {
        const elf_segment_t *s = &plan->segs[i];
        uint64_t file_end;
        /* Defensive repeat of the earlier proof makes this invariant local
         * to the entry check as well. */
        if (!add_ok(s->paddr, s->filesz, &file_end))
            return ELF_ERR_SEG_OVERFLOW;
        if ((s->flags & PF_X) &&
            plan->entry >= s->paddr && plan->entry < file_end)
            return ELF_OK;
    }
    return ELF_ERR_ENTRY;
}

const char *elf_status_str(elf_status_t st)
{
    switch (st) {
    case ELF_OK:                return "ok";
    case ELF_ERR_TRUNCATED:     return "file truncated";
    case ELF_ERR_MAGIC:         return "not an ELF image";
    case ELF_ERR_CLASS:         return "not ELF64";
    case ELF_ERR_ENDIAN:        return "not little-endian";
    case ELF_ERR_VERSION:       return "bad ELF version";
    case ELF_ERR_MACHINE:       return "not x86_64";
    case ELF_ERR_TYPE:          return "not ET_EXEC";
    case ELF_ERR_EHSIZE:        return "bad e_ehsize";
    case ELF_ERR_PHENTSIZE:     return "bad e_phentsize";
    case ELF_ERR_PHDR_BOUNDS:   return "program headers outside file";
    case ELF_ERR_SEG_BOUNDS:    return "segment data outside file";
    case ELF_ERR_SEG_SIZES:     return "p_filesz > p_memsz";
    case ELF_ERR_SEG_ALIGN:     return "bad PT_LOAD alignment";
    case ELF_ERR_SEG_VADDR:     return "vaddr is not identity-mapped";
    case ELF_ERR_SEG_FLAGS:     return "unsafe PT_LOAD permissions";
    case ELF_ERR_SEG_OVERFLOW:  return "address arithmetic overflow";
    case ELF_ERR_SEG_OVERLAP:   return "PT_LOAD segments overlap";
    case ELF_ERR_TOO_MANY_SEGS: return "too many PT_LOAD segments";
    case ELF_ERR_NO_SEGMENTS:   return "no loadable segments";
    case ELF_ERR_TOO_BIG:       return "kernel span too large";
    case ELF_ERR_ENTRY:         return "entry not executable file data";
    }
    return "unknown";
}
