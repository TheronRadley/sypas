/* Host tests for the bounded, fail-closed UEFI self-relocator. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "reloc.h"

#define DT_NULL 0
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define R_X86_64_RELATIVE 8

static int failures;

#define CHECK(condition, name)                                            \
    do {                                                                   \
        if (condition)                                                     \
            printf("PASS %s\n", name);                                   \
        else {                                                             \
            printf("FAIL %s (%s:%d)\n", name, __FILE__, __LINE__);      \
            failures++;                                                    \
        }                                                                  \
    } while (0)

#define IMAGE_SIZE 512
#define DYNAMIC_OFFSET 32
#define RELA_OFFSET 128
#define TARGET_OFFSET 320

static void init_image(uint8_t image[IMAGE_SIZE], Elf64_Dyn **dynamic,
                       Elf64_Rela **rela)
{
    memset(image, 0, IMAGE_SIZE);
    *dynamic = (Elf64_Dyn *)(image + DYNAMIC_OFFSET);
    *rela = (Elf64_Rela *)(image + RELA_OFFSET);
}

static void install_table(Elf64_Dyn *dynamic, uint64_t count)
{
    dynamic[0] = (Elf64_Dyn){ .d_tag = DT_RELA, .d_val = RELA_OFFSET };
    dynamic[1] = (Elf64_Dyn){ .d_tag = DT_RELASZ,
                              .d_val = count * sizeof(Elf64_Rela) };
    dynamic[2] = (Elf64_Dyn){ .d_tag = DT_RELAENT,
                              .d_val = sizeof(Elf64_Rela) };
    dynamic[3] = (Elf64_Dyn){ .d_tag = DT_NULL, .d_val = 0 };
}

int main(void)
{
    uint8_t image[IMAGE_SIZE];
    Elf64_Dyn *dynamic;
    Elf64_Rela *rela;
    uint64_t base = (uint64_t)(uintptr_t)image;

    init_image(image, &dynamic, &rela);
    dynamic[0].d_tag = DT_NULL;
    CHECK(sypas_reloc(base, IMAGE_SIZE, dynamic) == 0,
          "no relocation table accepted");

    init_image(image, &dynamic, &rela);
    install_table(dynamic, 1);
    rela[0] = (Elf64_Rela){ .r_offset = TARGET_OFFSET,
                            .r_info = R_X86_64_RELATIVE,
                            .r_addend = 0x44 };
    CHECK(sypas_reloc(base, IMAGE_SIZE, dynamic) == 0,
          "one valid RELATIVE relocation applied");
    CHECK(*(uint64_t *)(image + TARGET_OFFSET) == base + 0x44,
          "one valid RELATIVE value");

    init_image(image, &dynamic, &rela);
    install_table(dynamic, 2);
    rela[0] = (Elf64_Rela){ .r_offset = TARGET_OFFSET,
                            .r_info = R_X86_64_RELATIVE, .r_addend = 1 };
    rela[1] = (Elf64_Rela){ .r_offset = TARGET_OFFSET + 8,
                            .r_info = R_X86_64_RELATIVE, .r_addend = 2 };
    CHECK(sypas_reloc(base, IMAGE_SIZE, dynamic) == 0,
          "multiple RELATIVE relocations applied");
    CHECK(*(uint64_t *)(image + TARGET_OFFSET) == base + 1 &&
          *(uint64_t *)(image + TARGET_OFFSET + 8) == base + 2,
          "multiple relocation values");

    init_image(image, &dynamic, &rela);
    install_table(dynamic, 1);
    dynamic[2].d_val = sizeof(Elf64_Rela) - 1;
    CHECK(sypas_reloc(base, IMAGE_SIZE, dynamic) != 0,
          "bad DT_RELAENT rejected");

    init_image(image, &dynamic, &rela);
    install_table(dynamic, 1);
    dynamic[1].d_val = sizeof(Elf64_Rela) - 1;
    CHECK(sypas_reloc(base, IMAGE_SIZE, dynamic) != 0,
          "non-integral DT_RELASZ rejected");

    init_image(image, &dynamic, &rela);
    install_table(dynamic, 1);
    rela[0] = (Elf64_Rela){ .r_offset = TARGET_OFFSET,
                            .r_info = 1, .r_addend = 0 };
    CHECK(sypas_reloc(base, IMAGE_SIZE, dynamic) != 0,
          "unsupported relocation rejected");

    init_image(image, &dynamic, &rela);
    install_table(dynamic, 1);
    rela[0] = (Elf64_Rela){ .r_offset = UINT64_MAX,
                            .r_info = R_X86_64_RELATIVE, .r_addend = 0 };
    CHECK(sypas_reloc(base, IMAGE_SIZE, dynamic) != 0,
          "relocation offset overflow rejected");

    init_image(image, &dynamic, &rela);
    install_table(dynamic, 1);
    rela[0] = (Elf64_Rela){ .r_offset = IMAGE_SIZE - 4,
                            .r_info = R_X86_64_RELATIVE, .r_addend = 0 };
    CHECK(sypas_reloc(base, IMAGE_SIZE, dynamic) != 0,
          "relocation target outside image rejected");

    init_image(image, &dynamic, &rela);
    install_table(dynamic, 1);
    dynamic[0].d_val = IMAGE_SIZE - 8;
    CHECK(sypas_reloc(base, IMAGE_SIZE, dynamic) != 0,
          "relocation table outside image rejected");

    init_image(image, &dynamic, &rela);
    for (size_t i = 0; i < (IMAGE_SIZE - DYNAMIC_OFFSET) / sizeof(*dynamic); i++)
        dynamic[i].d_tag = DT_RELA;
    CHECK(sypas_reloc(base, IMAGE_SIZE, dynamic) != 0,
          "unterminated dynamic table rejected");

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
