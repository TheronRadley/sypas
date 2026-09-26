/*
 * SYPAS UEFI bootloader — runtime self-relocation.
 *
 * Applies R_X86_64_RELATIVE relocations from the ELF .dynamic section so
 * the objcopy-converted PE image works at whatever address the firmware
 * loaded it.  Runs before anything else; must not touch globals that need
 * relocation themselves (it only reads _DYNAMIC via a PC-relative pointer
 * handed in by start.S).
 *
 * Called from start.S with the System V AMD64 convention:
 *   base    = actual image load address (ImageBase symbol)
 *   dynamic = address of the _DYNAMIC array
 *
 * Returns 0 on success.  Returns nonzero if the dynamic section is
 * malformed or contains a relocation type this code cannot apply:
 * running with unapplied relocations means executing corrupt pointers,
 * so start.S turns a nonzero return into EFI_LOAD_ERROR back to the
 * firmware instead of jumping into efi_main.
 */

#include <stdint.h>

#define DT_NULL    0
#define DT_RELA    7
#define DT_RELASZ  8
#define DT_RELAENT 9

#define R_X86_64_RELATIVE 8

typedef struct {
    int64_t d_tag;
    uint64_t d_val;
} Elf64_Dyn;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} Elf64_Rela;

uint64_t sypas_reloc(uint64_t base, const Elf64_Dyn *dynamic)
{
    uint64_t rela = 0, relasz = 0, relaent = sizeof(Elf64_Rela);

    for (const Elf64_Dyn *d = dynamic; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
        case DT_RELA:    rela    = d->d_val; break;
        case DT_RELASZ:  relasz  = d->d_val; break;
        case DT_RELAENT: relaent = d->d_val; break;
        }
    }

    if (!rela || !relasz)
        return 0;                      /* nothing to relocate */

    /* .dynamic sanity: entry size must be what we compiled against and
     * the table size must be a whole number of entries. */
    if (relaent != sizeof(Elf64_Rela) || relasz % relaent != 0)
        return 1;

    /* DT_RELA holds the link-time address; the image links at 0, so the
     * runtime location is base + rela. */
    const Elf64_Rela *r   = (const Elf64_Rela *)(base + rela);
    const Elf64_Rela *end = (const Elf64_Rela *)(base + rela + relasz);

    for (; r < end; r++) {
        if ((uint32_t)r->r_info != R_X86_64_RELATIVE)
            return 1;   /* -Bsymbolic + PIC must only emit RELATIVE */
        *(uint64_t *)(base + r->r_offset) = base + (uint64_t)r->r_addend;
    }
    return 0;
}
