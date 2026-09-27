/* SYPAS UEFI memory-map validation and translation (pure, host-tested). */
#ifndef SYPAS_LOADER_MEMORY_MAP_H
#define SYPAS_LOADER_MEMORY_MAP_H

#include <stdint.h>

#include "efi.h"
#include "elf.h"
#include "../protocols/sypas_bootproto.h"

typedef struct {
    uint64_t descriptor_index;
    uint64_t base;
    uint64_t end;
    uint64_t previous_end;
} sypas_map_diagnostic_t;

typedef enum {
    SYPAS_MAP_OK = 0,
    SYPAS_MAP_ERR_DESCRIPTOR_SIZE,
    SYPAS_MAP_ERR_DESCRIPTOR_ALIGNMENT,
    SYPAS_MAP_ERR_ZERO_LENGTH,
    SYPAS_MAP_ERR_ADDRESS_ALIGNMENT,
    SYPAS_MAP_ERR_ADDRESS_OVERFLOW,
    SYPAS_MAP_ERR_RANGE_OVERLAP,
    SYPAS_MAP_ERR_OUTPUT_FULL,
    SYPAS_MAP_ERR_OUTPUT_OVERFLOW,
    SYPAS_MAP_ERR_KERNEL_NOT_TAGGED,
} sypas_map_status_t;

/*
 * Validate an EFI map and translate it to the SYPAS ownership map.
 *
 * The input comes directly from GetMemoryMap(), including its descriptor
 * stride. Firmware descriptor order is canonicalized in-place before range
 * validation because UEFI implementations may enumerate high MMIO windows
 * before lower physical ranges. The output is guaranteed, on SYPAS_MAP_OK,
 * to be sorted, non-overlapping, page-aligned, overflow-free and fully
 * classified.
 * Kernel ownership is split from only the exact page ranges in `kernel` —
 * never from its diagnostic min..max span.  All buffers are supplied by the
 * caller; this function makes no UEFI calls and is host-unit-testable.
 */
sypas_map_status_t sypas_translate_memory_map(
    EFI_MEMORY_DESCRIPTOR *efi_map, uint64_t map_size,
    uint64_t descriptor_size, const elf_load_plan_t *kernel,
    sypas_memmap_entry_t *out, uint64_t out_capacity,
    uint64_t *out_count, sypas_map_diagnostic_t *diagnostic);

const char *sypas_map_status_str(sypas_map_status_t status);

#endif /* SYPAS_LOADER_MEMORY_MAP_H */
