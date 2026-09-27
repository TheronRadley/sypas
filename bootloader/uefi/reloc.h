/* Bounded, fail-closed self-relocation for the PE-converted loader. */
#ifndef SYPAS_LOADER_RELOC_H
#define SYPAS_LOADER_RELOC_H

#include <stdint.h>

typedef struct {
    int64_t d_tag;
    uint64_t d_val;
} Elf64_Dyn;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} Elf64_Rela;

/* Apply only R_X86_64_RELATIVE relocations inside [base, base + image_size).
 * `dynamic` itself must point into that same image and include DT_NULL before
 * image end.  Returns 0 on success and non-zero on any malformed input. */
uint64_t sypas_reloc(uint64_t base, uint64_t image_size,
                     const Elf64_Dyn *dynamic);

#endif /* SYPAS_LOADER_RELOC_H */
