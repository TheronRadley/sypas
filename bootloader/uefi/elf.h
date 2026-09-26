/*
 * SYPAS UEFI bootloader — ELF64 kernel image validation and load planning.
 *
 * Pure code: no UEFI calls, no globals, no allocation.  The loader feeds
 * the raw kernel file through elf_plan_load() and only ever allocates or
 * copies what the returned plan describes.  Because this module is
 * freestanding it is also compiled on the build host and unit-tested
 * against malformed images (tests/unit/test_elf.c) — the boot path and
 * the test path run the same code.
 *
 * Contract: elf_plan_load() returns ELF_OK only if every offset, size,
 * and address in the file has been proven in-bounds and overflow-free.
 * On any other return value the caller must not touch memory based on
 * the file's contents.
 */

#ifndef SYPAS_LOADER_ELF_H
#define SYPAS_LOADER_ELF_H

#include <stdint.h>

/* ---- ELF64 on-disk structures ------------------------------------------ */

#define ELF_MAGIC   0x464C457FU /* "\x7fELF" */
#define ELFCLASS64  2
#define ELFDATA2LSB 1
#define EV_CURRENT  1
#define ET_EXEC     2
#define EM_X86_64   62
#define PT_LOAD     1

typedef struct {
    uint32_t e_magic;
    uint8_t  e_class, e_data, e_iversion, e_osabi, e_abiversion, e_pad[7];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint64_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type, p_flags;
    uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} Elf64_Phdr;

/* ---- Load plan ----------------------------------------------------------- */

/* Enough for any sane kernel link; the SYPAS kernel uses 2-4 PT_LOADs. */
#define ELF_MAX_SEGMENTS 16

/* Refuse kernels above this span: a bigger "kernel" is a corrupt header,
 * not a kernel (current kernel image is < 1 MiB). */
#define ELF_MAX_KERNEL_SPAN (256ULL * 1024 * 1024)

typedef struct {
    uint64_t paddr;      /* destination physical address (unaligned)      */
    uint64_t page_base;  /* paddr rounded down to a page                   */
    uint64_t page_end;   /* paddr + memsz rounded up to a page             */
    uint64_t offset;     /* source offset in the file                      */
    uint64_t filesz;     /* bytes to copy from the file                    */
    uint64_t memsz;      /* bytes occupied in memory (>= filesz)           */
} elf_segment_t;

typedef struct {
    uint64_t      entry;       /* e_entry, proven inside a PT_LOAD        */
    uint64_t      phys_base;   /* lowest page_base                        */
    uint64_t      phys_end;    /* highest page_end                        */
    int           nsegs;
    elf_segment_t segs[ELF_MAX_SEGMENTS];
} elf_load_plan_t;

typedef enum {
    ELF_OK = 0,
    ELF_ERR_TRUNCATED,       /* file smaller than the ELF header          */
    ELF_ERR_MAGIC,           /* not an ELF file                           */
    ELF_ERR_CLASS,           /* not ELFCLASS64                            */
    ELF_ERR_ENDIAN,          /* not little-endian                         */
    ELF_ERR_VERSION,         /* bad e_version / EI_VERSION                */
    ELF_ERR_MACHINE,         /* not EM_X86_64                             */
    ELF_ERR_TYPE,            /* not ET_EXEC                               */
    ELF_ERR_PHENTSIZE,       /* e_phentsize != sizeof(Elf64_Phdr)         */
    ELF_ERR_PHDR_BOUNDS,     /* program header table outside the file     */
    ELF_ERR_SEG_BOUNDS,      /* segment file range outside the file       */
    ELF_ERR_SEG_SIZES,       /* p_filesz > p_memsz                        */
    ELF_ERR_SEG_OVERFLOW,    /* address/size arithmetic overflows         */
    ELF_ERR_SEG_OVERLAP,     /* two PT_LOADs share a physical page        */
    ELF_ERR_TOO_MANY_SEGS,   /* more than ELF_MAX_SEGMENTS PT_LOADs       */
    ELF_ERR_NO_SEGMENTS,     /* no loadable segment                       */
    ELF_ERR_TOO_BIG,         /* loaded span exceeds ELF_MAX_KERNEL_SPAN   */
    ELF_ERR_ENTRY,           /* e_entry not inside any PT_LOAD            */
} elf_status_t;

/* Validate `file` (fsize bytes) as a SYPAS kernel image and fill *plan.
 * Reads only within [file, file + fsize).  *plan is valid iff ELF_OK. */
elf_status_t elf_plan_load(const uint8_t *file, uint64_t fsize,
                           elf_load_plan_t *plan);

/* Short human-readable name for a status (for error reporting). */
const char *elf_status_str(elf_status_t st);

#endif /* SYPAS_LOADER_ELF_H */
