/* Host tests for final UEFI->SYPAS memory-map ownership translation. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "memory_map.h"

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

static EFI_MEMORY_DESCRIPTOR descriptor(uint32_t type, uint64_t base,
                                        uint64_t pages)
{
    EFI_MEMORY_DESCRIPTOR d = {0};
    d.Type = type;
    d.PhysicalStart = base;
    d.NumberOfPages = pages;
    return d;
}

static elf_load_plan_t split_kernel_plan(void)
{
    elf_load_plan_t plan = {0};
    plan.nsegs = 2;
    plan.segs[0].page_base = 0x400000;
    plan.segs[0].page_end = 0x401000;
    plan.segs[1].page_base = 0x402000;
    plan.segs[1].page_end = 0x403000;
    return plan;
}

int main(void)
{
    EFI_MEMORY_DESCRIPTOR input[2];
    sypas_memmap_entry_t output[16];
    elf_load_plan_t kernel = split_kernel_plan();
    uint64_t count = 0;

    input[0] = descriptor(EfiConventionalMemory, 0x100000, 16);
    input[1] = descriptor(EfiLoaderData, 0x400000, 3);
    CHECK(sypas_translate_memory_map(input, sizeof(input), sizeof(input[0]),
                                     &kernel, output, 16, &count, 0) == SYPAS_MAP_OK,
          "well-formed EFI map translated");
    CHECK(count == 4, "exact PT_LOAD ranges split loader descriptor");
    CHECK(output[0].type == SYPAS_MEM_USABLE && output[0].base == 0x100000,
          "usable range retained");
    CHECK(output[1].type == SYPAS_MEM_KERNEL &&
          output[1].base == 0x400000 && output[1].length == 0x1000,
          "first PT_LOAD tagged kernel");
    CHECK(output[2].type == SYPAS_MEM_LOADER &&
          output[2].base == 0x401000 && output[2].length == 0x1000,
          "hole between PT_LOADs remains loader-owned");
    CHECK(output[3].type == SYPAS_MEM_KERNEL &&
          output[3].base == 0x402000 && output[3].length == 0x1000,
          "second PT_LOAD tagged kernel");

    /* Firmware descriptors are canonicalized by physical address before
     * validation: OVMF can enumerate a high MMIO window before lower RAM. */
    elf_load_plan_t no_kernel = {0};
    input[0] = descriptor(EfiConventionalMemory, 0x300000, 1);
    input[1] = descriptor(EfiConventionalMemory, 0x200000, 1);
    CHECK(sypas_translate_memory_map(input, sizeof(input), sizeof(input[0]),
                                     &no_kernel, output, 16, &count, 0) ==
          SYPAS_MAP_OK,
          "unordered firmware descriptors canonicalized");
    CHECK(count == 2 && output[0].base == 0x200000 && output[1].base == 0x300000,
          "canonicalized output is physically ordered");

    input[0] = descriptor(EfiConventionalMemory, 0x200000, 2);
    input[1] = descriptor(EfiConventionalMemory, 0x201000, 1);
    CHECK(sypas_translate_memory_map(input, sizeof(input), sizeof(input[0]),
                                     &no_kernel, output, 16, &count, 0) ==
          SYPAS_MAP_ERR_RANGE_OVERLAP,
          "overlapping firmware ranges rejected after sorting");

    input[0] = descriptor(EfiConventionalMemory, 0x100001, 1);
    CHECK(sypas_translate_memory_map(input, sizeof(input[0]), sizeof(input[0]),
                                     &kernel, output, 16, &count, 0) ==
          SYPAS_MAP_ERR_ADDRESS_ALIGNMENT,
          "unaligned firmware range rejected");

    input[0] = descriptor(EfiConventionalMemory, 0x100000, UINT64_MAX);
    CHECK(sypas_translate_memory_map(input, sizeof(input[0]), sizeof(input[0]),
                                     &kernel, output, 16, &count, 0) ==
          SYPAS_MAP_ERR_ADDRESS_OVERFLOW,
          "firmware page-count overflow rejected");

    input[0] = descriptor(EfiConventionalMemory, 0x100000, 1);
    CHECK(sypas_translate_memory_map(input, sizeof(input[0]), sizeof(input[0]) - 8,
                                     &kernel, output, 16, &count, 0) ==
          SYPAS_MAP_ERR_DESCRIPTOR_SIZE,
          "short descriptor stride rejected");

    input[0] = descriptor(EfiLoaderData, 0x400000, 3);
    CHECK(sypas_translate_memory_map(input, sizeof(input[0]), sizeof(input[0]),
                                     &kernel, output, 2, &count, 0) ==
          SYPAS_MAP_ERR_OUTPUT_FULL,
          "bounded output buffer enforced");

    input[0] = descriptor(EfiConventionalMemory, 0x400000, 3);
    CHECK(sypas_translate_memory_map(input, sizeof(input[0]), sizeof(input[0]),
                                     &kernel, output, 16, &count, 0) ==
          SYPAS_MAP_ERR_KERNEL_NOT_TAGGED,
          "kernel ranges must originate in loader memory");

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
