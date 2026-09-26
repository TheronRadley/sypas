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

/* a + b, or 0/false on wraparound */
static int add_ok(uint64_t a, uint64_t b, uint64_t *out)
{
    if (a > UINT64_MAX - b)
        return 0;
    *out = a + b;
    return 1;
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
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0)
            continue;

        if (plan->nsegs == ELF_MAX_SEGMENTS)
            return ELF_ERR_TOO_MANY_SEGS;

        if (ph[i].p_filesz > ph[i].p_memsz)
            return ELF_ERR_SEG_SIZES;

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
     * v1 links vaddr == paddr; the entry must land inside a loaded
     * segment or the jump at handoff is into unowned memory. */
    plan->entry = eh->e_entry;
    for (int i = 0; i < plan->nsegs; i++) {
        const elf_segment_t *s = &plan->segs[i];
        if (plan->entry >= s->paddr && plan->entry < s->paddr + s->memsz)
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
    case ELF_ERR_PHENTSIZE:     return "bad e_phentsize";
    case ELF_ERR_PHDR_BOUNDS:   return "program headers outside file";
    case ELF_ERR_SEG_BOUNDS:    return "segment data outside file";
    case ELF_ERR_SEG_SIZES:     return "p_filesz > p_memsz";
    case ELF_ERR_SEG_OVERFLOW:  return "address arithmetic overflow";
    case ELF_ERR_SEG_OVERLAP:   return "PT_LOAD segments overlap";
    case ELF_ERR_TOO_MANY_SEGS: return "too many PT_LOAD segments";
    case ELF_ERR_NO_SEGMENTS:   return "no loadable segments";
    case ELF_ERR_TOO_BIG:       return "kernel span too large";
    case ELF_ERR_ENTRY:         return "entry point outside segments";
    }
    return "unknown";
}
