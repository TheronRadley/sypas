/*
 * SYPAS host-side unit tests — loader ELF validator (bootloader/uefi/elf.c).
 *
 * Compiles the exact code the bootloader runs (elf.c is freestanding and
 * UEFI-free) and feeds it valid, malformed, truncated, and adversarial
 * images.  Built with -fsanitize=address,undefined so any out-of-bounds
 * read the validator fails to prevent aborts the test run.
 *
 * Usage: test_elf [path/to/kernel.elf]
 *   With an argument, additionally proves the real build artifact passes
 *   the same validator that will judge it at boot.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "elf.h"

static int failures;

#define CHECK(cond, name)                                                \
    do {                                                                 \
        if (cond) {                                                      \
            printf("PASS %s\n", name);                                   \
        } else {                                                         \
            printf("FAIL %s (%s:%d)\n", name, __FILE__, __LINE__);       \
            failures++;                                                  \
        }                                                                \
    } while (0)

/* ---- Image builder ------------------------------------------------------ */

#define MAX_IMG (1 << 20)

typedef struct {
    uint8_t buf[MAX_IMG];
    uint64_t size;
    Elf64_Ehdr *eh;
    Elf64_Phdr *ph;   /* first program header */
} img_t;

/* A well-formed kernel: nphdr PT_LOADs, 0x1000 apart at 0x400000. */
static void mk_image(img_t *im, int nphdr)
{
    memset(im, 0, sizeof(*im));
    im->eh = (Elf64_Ehdr *)im->buf;
    im->eh->e_magic     = ELF_MAGIC;
    im->eh->e_class     = ELFCLASS64;
    im->eh->e_data      = ELFDATA2LSB;
    im->eh->e_iversion  = EV_CURRENT;
    im->eh->e_version   = EV_CURRENT;
    im->eh->e_type      = ET_EXEC;
    im->eh->e_machine   = EM_X86_64;
    im->eh->e_entry     = 0x400000;
    im->eh->e_phoff     = sizeof(Elf64_Ehdr);
    im->eh->e_phentsize = sizeof(Elf64_Phdr);
    im->eh->e_phnum     = (uint16_t)nphdr;
    im->eh->e_ehsize    = sizeof(Elf64_Ehdr);

    im->ph = (Elf64_Phdr *)(im->buf + im->eh->e_phoff);
    uint64_t data_off = im->eh->e_phoff + (uint64_t)nphdr * sizeof(Elf64_Phdr);
    data_off = (data_off + 0xFFF) & ~0xFFFULL;

    for (int i = 0; i < nphdr; i++) {
        im->ph[i].p_type   = PT_LOAD;
        im->ph[i].p_offset = data_off + (uint64_t)i * 0x1000;
        im->ph[i].p_vaddr  = 0x400000 + (uint64_t)i * 0x1000;
        im->ph[i].p_paddr  = im->ph[i].p_vaddr;
        im->ph[i].p_filesz = 0x800;
        im->ph[i].p_memsz  = 0x800;
        im->ph[i].p_align  = 0x1000;
    }
    im->size = data_off + (uint64_t)nphdr * 0x1000;
}

static elf_status_t run(const img_t *im)
{
    /* Copy into an exactly-sized heap buffer so ASan catches any read
     * past the declared file size. */
    uint8_t *tight = malloc(im->size ? im->size : 1);
    memcpy(tight, im->buf, im->size);
    elf_load_plan_t plan;
    elf_status_t st = elf_plan_load(tight, im->size, &plan);
    free(tight);
    return st;
}

/* ---- Tests --------------------------------------------------------------- */

int main(int argc, char **argv)
{
    img_t im;

    /* Valid image and plan contents */
    mk_image(&im, 2);
    {
        uint8_t *tight = malloc(im.size);
        memcpy(tight, im.buf, im.size);
        elf_load_plan_t plan;
        elf_status_t st = elf_plan_load(tight, im.size, &plan);
        CHECK(st == ELF_OK, "valid kernel accepted");
        CHECK(plan.nsegs == 2, "valid: both PT_LOADs planned");
        CHECK(plan.entry == 0x400000, "valid: entry preserved");
        CHECK(plan.phys_base == 0x400000, "valid: phys_base");
        CHECK(plan.phys_end == 0x402000, "valid: phys_end page-rounded");
        CHECK(plan.segs[0].filesz == 0x800, "valid: filesz preserved");
        free(tight);
    }

    /* Truncated header */
    mk_image(&im, 1);
    im.size = sizeof(Elf64_Ehdr) - 1;
    CHECK(run(&im) == ELF_ERR_TRUNCATED, "truncated ELF header rejected");

    mk_image(&im, 1); im.size = 0;
    CHECK(run(&im) == ELF_ERR_TRUNCATED, "empty file rejected");

    /* Identification fields */
    mk_image(&im, 1); im.eh->e_magic = 0xCAFEBABE;
    CHECK(run(&im) == ELF_ERR_MAGIC, "bad magic rejected");

    mk_image(&im, 1); im.eh->e_class = 1;             /* ELFCLASS32 */
    CHECK(run(&im) == ELF_ERR_CLASS, "ELF32 rejected");

    mk_image(&im, 1); im.eh->e_data = 2;              /* big-endian */
    CHECK(run(&im) == ELF_ERR_ENDIAN, "big-endian rejected");

    mk_image(&im, 1); im.eh->e_version = 0;
    CHECK(run(&im) == ELF_ERR_VERSION, "bad e_version rejected");

    mk_image(&im, 1); im.eh->e_machine = 40;          /* ARM */
    CHECK(run(&im) == ELF_ERR_MACHINE, "wrong machine rejected");

    mk_image(&im, 1); im.eh->e_type = 3;              /* ET_DYN */
    CHECK(run(&im) == ELF_ERR_TYPE, "ET_DYN rejected");

    /* Program header table */
    mk_image(&im, 1); im.eh->e_phentsize = sizeof(Elf64_Phdr) + 8;
    CHECK(run(&im) == ELF_ERR_PHENTSIZE, "bad e_phentsize rejected");

    mk_image(&im, 1); im.eh->e_phnum = 0;
    CHECK(run(&im) == ELF_ERR_NO_SEGMENTS, "e_phnum == 0 rejected");

    mk_image(&im, 1); im.eh->e_phoff = im.size;
    CHECK(run(&im) == ELF_ERR_PHDR_BOUNDS, "phdr table past EOF rejected");

    mk_image(&im, 1); im.eh->e_phoff = UINT64_MAX - 8;
    CHECK(run(&im) == ELF_ERR_PHDR_BOUNDS, "phoff overflow rejected");

    mk_image(&im, 1);
    im.eh->e_phoff = im.size - sizeof(Elf64_Phdr) / 2;
    CHECK(run(&im) == ELF_ERR_PHDR_BOUNDS, "partial phdr at EOF rejected");

    /* Segment file ranges */
    mk_image(&im, 1); im.ph[0].p_offset = im.size;
    CHECK(run(&im) == ELF_ERR_SEG_BOUNDS, "segment data past EOF rejected");

    mk_image(&im, 1); im.ph[0].p_filesz = im.size;    /* tail past EOF */
    im.ph[0].p_memsz = im.size;
    CHECK(run(&im) == ELF_ERR_SEG_BOUNDS, "segment tail past EOF rejected");

    mk_image(&im, 1); im.ph[0].p_filesz = im.ph[0].p_memsz + 1;
    CHECK(run(&im) == ELF_ERR_SEG_SIZES, "filesz > memsz rejected");

    mk_image(&im, 1);
    im.ph[0].p_offset = UINT64_MAX - 0x10;
    im.ph[0].p_filesz = 0x20;
    im.ph[0].p_memsz  = 0x20;
    CHECK(run(&im) == ELF_ERR_SEG_OVERFLOW, "offset+filesz overflow rejected");

    /* Physical address arithmetic */
    mk_image(&im, 1);
    im.ph[0].p_paddr  = UINT64_MAX - 0x100;
    im.ph[0].p_memsz  = 0x200;
    im.ph[0].p_filesz = 0x100;
    im.eh->e_entry    = im.ph[0].p_paddr;
    CHECK(run(&im) == ELF_ERR_SEG_OVERFLOW, "paddr+memsz overflow rejected");

    mk_image(&im, 1);
    im.ph[0].p_paddr = UINT64_MAX - 0xFFF;   /* survives +memsz, dies on
                                                page rounding */
    im.ph[0].p_memsz = 0x800;
    im.ph[0].p_filesz = 0x800;
    im.eh->e_entry   = im.ph[0].p_paddr;
    CHECK(run(&im) == ELF_ERR_SEG_OVERFLOW, "page-rounding overflow rejected");

    /* Zero-length loadable segments are skipped, not loaded */
    mk_image(&im, 2); im.ph[1].p_memsz = 0; im.ph[1].p_filesz = 0;
    CHECK(run(&im) == ELF_OK, "zero-length segment skipped");

    mk_image(&im, 1); im.ph[0].p_memsz = 0; im.ph[0].p_filesz = 0;
    CHECK(run(&im) == ELF_ERR_NO_SEGMENTS, "only zero-length segments rejected");

    /* Page-granular overlap */
    mk_image(&im, 2); im.ph[1].p_paddr = im.ph[0].p_paddr + 0x800;
    im.ph[1].p_vaddr = im.ph[1].p_paddr;
    CHECK(run(&im) == ELF_ERR_SEG_OVERLAP, "overlapping PT_LOADs rejected");

    mk_image(&im, 2); im.ph[1].p_paddr = im.ph[0].p_paddr;  /* identical */
    im.ph[1].p_vaddr = im.ph[1].p_paddr;
    CHECK(run(&im) == ELF_ERR_SEG_OVERLAP, "duplicate PT_LOAD rejected");

    /* Segment count limit */
    mk_image(&im, ELF_MAX_SEGMENTS + 1);
    CHECK(run(&im) == ELF_ERR_TOO_MANY_SEGS, "too many PT_LOADs rejected");

    mk_image(&im, ELF_MAX_SEGMENTS);
    CHECK(run(&im) == ELF_OK, "exactly ELF_MAX_SEGMENTS accepted");

    /* Span limit (a 'kernel' claiming to cover most of RAM) */
    mk_image(&im, 2);
    im.ph[1].p_paddr = im.ph[0].p_paddr + ELF_MAX_KERNEL_SPAN + 0x1000;
    im.ph[1].p_vaddr = im.ph[1].p_paddr;
    CHECK(run(&im) == ELF_ERR_TOO_BIG, "excessive load span rejected");

    /* Entry point must land inside a loaded segment */
    mk_image(&im, 1); im.eh->e_entry = 0x500000;
    CHECK(run(&im) == ELF_ERR_ENTRY, "entry outside segments rejected");

    mk_image(&im, 1); im.eh->e_entry = 0x400000 + 0x800;  /* == end */
    CHECK(run(&im) == ELF_ERR_ENTRY, "entry at segment end rejected");

    mk_image(&im, 1);
    im.eh->e_entry = 0x400000 + 0x7FF;                    /* last byte */
    CHECK(run(&im) == ELF_OK, "entry at last segment byte accepted");

    /* The real build artifact must satisfy the same validator */
    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        if (!f) {
            printf("FAIL open %s\n", argv[1]);
            failures++;
        } else {
            static uint8_t kbuf[8 << 20];
            size_t n = fread(kbuf, 1, sizeof(kbuf), f);
            fclose(f);
            elf_load_plan_t plan;
            elf_status_t st = elf_plan_load(kbuf, n, &plan);
            printf("     %s: %s, %d segments, span %llx..%llx, entry %llx\n",
                   argv[1], elf_status_str(st), plan.nsegs,
                   (unsigned long long)plan.phys_base,
                   (unsigned long long)plan.phys_end,
                   (unsigned long long)plan.entry);
            CHECK(st == ELF_OK, "real kernel.elf accepted");
            /* Truncation sweep: any prefix that cuts into data the load
             * plan needs (headers or PT_LOAD file content) must be
             * rejected.  Prefixes that only drop trailing section
             * headers / debug info are legitimately loadable. */
            uint64_t needed = 0;
            for (int i = 0; i < plan.nsegs; i++)
                if (plan.segs[i].offset + plan.segs[i].filesz > needed)
                    needed = plan.segs[i].offset + plan.segs[i].filesz;
            int bad = 0;
            for (size_t cut = 0; cut < needed; cut += 977) /* prime stride */
                if (elf_plan_load(kbuf, cut, &plan) == ELF_OK)
                    bad++;
            CHECK(bad == 0, "load-critical truncations all rejected");
        }
    }

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
