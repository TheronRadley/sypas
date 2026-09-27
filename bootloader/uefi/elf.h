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
 * address, alignment and permission in the file has been proven valid for
 * the SYPAS v1 kernel format.  On any other return value the caller must
 * not touch memory based on the file's contents.
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

#define PF_X 1
#define PF_W 2
#define PF_R 4

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

/* Refuse kernel files larger than this before allocating a pool buffer.
 * The loaded span is independently capped below: the two limits are
 * deliberately separate because debug/section data need not be mapped. */
#define SYPAS_MAX_KERNEL_FILE (64ULL * 1024 * 1024)

/* Refuse kernels above this mapped span: a bigger "kernel" is a corrupt
 * header, not a kernel (current kernel image is < 1 MiB). */
#define ELF_MAX_KERNEL_SPAN (256ULL * 1024 * 1024)

typedef struct {
    uint64_t paddr;      /* destination physical address (unaligned)      */
    uint64_t page_base;  /* paddr rounded down to a page                   */
    uint64_t page_end;   /* paddr + memsz rounded up to a page             */
    uint64_t offset;     /* source offset in the file                      */
    uint64_t filesz;     /* bytes to copy from the file                    */
    uint64_t memsz;      /* bytes occupied in memory (>= filesz)           */
    uint32_t flags;      /* ELF PF_R/PF_W/PF_X, retained for VMM policy    */
} elf_segment_t;

typedef struct {
    uint64_t      entry;       /* executable, file-backed e_entry           */
    uint64_t      phys_base;   /* lowest page_base (diagnostic span only)    */
    uint64_t      phys_end;    /* highest page_end (diagnostic span only)    */
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
    ELF_ERR_EHSIZE,          /* e_ehsize != sizeof(Elf64_Ehdr)            */
    ELF_ERR_PHENTSIZE,       /* e_phentsize != sizeof(Elf64_Phdr)         */
    ELF_ERR_PHDR_BOUNDS,     /* program header table outside the file     */
    ELF_ERR_SEG_BOUNDS,      /* segment file range outside the file       */
    ELF_ERR_SEG_SIZES,       /* p_filesz > p_memsz                        */
    ELF_ERR_SEG_ALIGN,       /* bad p_align/congruence                    */
    ELF_ERR_SEG_VADDR,       /* v1 requires p_vaddr == p_paddr            */
    ELF_ERR_SEG_FLAGS,       /* unsupported permission bits or W+X        */
    ELF_ERR_SEG_OVERFLOW,    /* address/size arithmetic overflows         */
    ELF_ERR_SEG_OVERLAP,     /* two PT_LOADs share a physical page        */
    ELF_ERR_TOO_MANY_SEGS,   /* more than ELF_MAX_SEGMENTS PT_LOADs       */
    ELF_ERR_NO_SEGMENTS,     /* no loadable segment                       */
    ELF_ERR_TOO_BIG,         /* loaded span exceeds ELF_MAX_KERNEL_SPAN   */
    ELF_ERR_ENTRY,           /* entry is not file-backed executable code  */
} elf_status_t;

/* Validate `file` (fsize bytes) as a SYPAS v1 kernel image and fill *plan.
 * Reads only within [file, file + fsize).  *plan is meaningful only when
 * ELF_OK is returned. */
elf_status_t elf_plan_load(const uint8_t *file, uint64_t fsize,
                           elf_load_plan_t *plan);

/* Stable diagnostic string for the loader and host tests. */
const char *elf_status_str(elf_status_t st);

#endif /* SYPAS_LOADER_ELF_H */
