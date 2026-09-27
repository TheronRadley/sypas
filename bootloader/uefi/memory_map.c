/*
 * UEFI memory-map translation for the SYPAS boot protocol.
 *
 * Firmware supplies this data, so validate all range arithmetic before
 * converting it into the allocator's trusted input.  This module does not
 * call firmware and intentionally has no globals, which lets test_memorymap
 * exercise the same code that the loader uses at handoff.
 */

#include "memory_map.h"

#define PAGE_SIZE 4096ULL

static int add_ok(uint64_t a, uint64_t b, uint64_t *out)
{
    if (a > UINT64_MAX - b)
        return 0;
    *out = a + b;
    return 1;
}

static int mul_ok(uint64_t a, uint64_t b, uint64_t *out)
{
    if (a && b > UINT64_MAX / a)
        return 0;
    *out = a * b;
    return 1;
}

static uint32_t efi_to_sypas_memtype(uint32_t type)
{
    switch (type) {
    case EfiConventionalMemory:
        return SYPAS_MEM_USABLE;
    case EfiLoaderCode:
    case EfiLoaderData:
        return SYPAS_MEM_LOADER;
    case EfiBootServicesCode:
    case EfiBootServicesData:
    case EfiRuntimeServicesCode:
    case EfiRuntimeServicesData:
        /* The firmware's active identity map may live here.  Do not make
         * this allocatable until the kernel owns CR3. */
        return SYPAS_MEM_FIRMWARE;
    case EfiACPIReclaimMemory:
        return SYPAS_MEM_ACPI_RECLAIM;
    case EfiACPIMemoryNVS:
        return SYPAS_MEM_ACPI_NVS;
    case EfiMemoryMappedIO:
    case EfiMemoryMappedIOPortSpace:
        return SYPAS_MEM_MMIO;
    case EfiUnacceptedMemoryType:
    default:
        return SYPAS_MEM_RESERVED;
    }
}

/* Append a proven [base, end) range.  Coalescing is checked too: it must
 * never turn a valid pair of firmware ranges into an overflowing output. */
static sypas_map_status_t append(sypas_memmap_entry_t *out, uint64_t *count,
                                 uint64_t cap, uint64_t base, uint64_t end,
                                 uint32_t type)
{
    if (base == end)
        return SYPAS_MAP_OK;

    uint64_t length = end - base;
    if (*count) {
        sypas_memmap_entry_t *previous = &out[*count - 1];
        uint64_t previous_end;
        if (!add_ok(previous->base, previous->length, &previous_end))
            return SYPAS_MAP_ERR_OUTPUT_OVERFLOW;
        if (previous_end > base)
            return SYPAS_MAP_ERR_OUTPUT_OVERFLOW;
        if (previous->type == type && previous_end == base) {
            uint64_t combined;
            if (!add_ok(previous->length, length, &combined))
                return SYPAS_MAP_ERR_OUTPUT_OVERFLOW;
            previous->length = combined;
            return SYPAS_MAP_OK;
        }
    }

    if (*count == cap)
        return SYPAS_MAP_ERR_OUTPUT_FULL;

    out[*count].base = base;
    out[*count].length = length;
    out[*count].type = type;
    out[*count].reserved = 0;
    (*count)++;
    return SYPAS_MAP_OK;
}

/* Find the first segment page range which intersects [cursor, end), ordered
 * by physical page base.  ELF program headers need not be sorted, so never
 * let their on-disk order leak into the SYPAS map contract. */
static const elf_segment_t *next_kernel_segment(const elf_load_plan_t *kernel,
                                                uint64_t cursor, uint64_t end)
{
    const elf_segment_t *best = 0;
    for (int i = 0; i < kernel->nsegs; i++) {
        const elf_segment_t *segment = &kernel->segs[i];
        if (segment->page_end <= cursor || segment->page_base >= end)
            continue;
        if (!best || segment->page_base < best->page_base)
            best = segment;
    }
    return best;
}

sypas_map_status_t sypas_translate_memory_map(
    const EFI_MEMORY_DESCRIPTOR *efi_map, uint64_t map_size,
    uint64_t descriptor_size, const elf_load_plan_t *kernel,
    sypas_memmap_entry_t *out, uint64_t out_capacity,
    uint64_t *out_count, sypas_map_diagnostic_t *diagnostic)
{
    if (diagnostic)
        *diagnostic = (sypas_map_diagnostic_t){0};
    if (!efi_map || !kernel || !out || !out_count ||
        descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR) ||
        descriptor_size % sizeof(uint64_t) ||
        map_size % descriptor_size)
        return SYPAS_MAP_ERR_DESCRIPTOR_SIZE;

    uint64_t previous_end = 0;
    uint64_t count = 0;
    uint64_t expected_kernel_bytes = 0;
    uint64_t tagged_kernel_bytes = 0;

    /* A plan comes only from elf_plan_load(), but retain this local sanity
     * guard because its ranges control ownership in the final handoff map. */
    for (int i = 0; i < kernel->nsegs; i++) {
        const elf_segment_t *segment = &kernel->segs[i];
        uint64_t bytes;
        if ((segment->page_base & (PAGE_SIZE - 1)) ||
            (segment->page_end & (PAGE_SIZE - 1)) ||
            segment->page_base >= segment->page_end ||
            !add_ok(expected_kernel_bytes,
                    segment->page_end - segment->page_base, &bytes))
            return SYPAS_MAP_ERR_ADDRESS_OVERFLOW;
        expected_kernel_bytes = bytes;
    }

    for (uint64_t offset = 0; offset < map_size; offset += descriptor_size) {
        const EFI_MEMORY_DESCRIPTOR *descriptor =
            (const EFI_MEMORY_DESCRIPTOR *)((const uint8_t *)efi_map + offset);
        uint64_t length, end;

        if ((uint64_t)(uintptr_t)descriptor & (sizeof(uint64_t) - 1))
            return SYPAS_MAP_ERR_DESCRIPTOR_ALIGNMENT;
        if (descriptor->NumberOfPages == 0)
            return SYPAS_MAP_ERR_ZERO_LENGTH;
        if (descriptor->PhysicalStart & (PAGE_SIZE - 1))
            return SYPAS_MAP_ERR_ADDRESS_ALIGNMENT;
        if (!mul_ok(descriptor->NumberOfPages, PAGE_SIZE, &length) ||
            !add_ok(descriptor->PhysicalStart, length, &end))
            return SYPAS_MAP_ERR_ADDRESS_OVERFLOW;
        if (offset && descriptor->PhysicalStart < previous_end) {
            if (diagnostic) {
                diagnostic->descriptor_index = offset / descriptor_size;
                diagnostic->base = descriptor->PhysicalStart;
                diagnostic->end = end;
                diagnostic->previous_end = previous_end;
            }
            return SYPAS_MAP_ERR_UNSORTED;
        }
        previous_end = end;

        uint32_t type = efi_to_sypas_memtype(descriptor->Type);
        uint64_t cursor = descriptor->PhysicalStart;

        /* Exact segment ranges matter.  A hole between two PT_LOADs stays
         * LOADER (or its source classification), even when it lies within
         * the plan's min..max diagnostic span. */
        if (type == SYPAS_MEM_LOADER) {
            for (;;) {
                const elf_segment_t *segment =
                    next_kernel_segment(kernel, cursor, end);
                if (!segment)
                    break;

                uint64_t lo = segment->page_base > cursor
                    ? segment->page_base : cursor;
                uint64_t hi = segment->page_end < end
                    ? segment->page_end : end;
                sypas_map_status_t status = append(out, &count, out_capacity,
                                                   cursor, lo, type);
                if (status != SYPAS_MAP_OK)
                    return status;
                status = append(out, &count, out_capacity, lo, hi,
                                SYPAS_MEM_KERNEL);
                if (status != SYPAS_MAP_OK)
                    return status;
                if (!add_ok(tagged_kernel_bytes, hi - lo, &tagged_kernel_bytes))
                    return SYPAS_MAP_ERR_OUTPUT_OVERFLOW;
                cursor = hi;
            }
        }

        sypas_map_status_t status = append(out, &count, out_capacity,
                                           cursor, end, type);
        if (status != SYPAS_MAP_OK)
            return status;
    }

    if (tagged_kernel_bytes != expected_kernel_bytes)
        return SYPAS_MAP_ERR_KERNEL_NOT_TAGGED;

    *out_count = count;
    return SYPAS_MAP_OK;
}

const char *sypas_map_status_str(sypas_map_status_t status)
{
    switch (status) {
    case SYPAS_MAP_OK:                    return "ok";
    case SYPAS_MAP_ERR_DESCRIPTOR_SIZE:   return "bad memory-map descriptor size";
    case SYPAS_MAP_ERR_DESCRIPTOR_ALIGNMENT:
        return "unaligned memory-map descriptor";
    case SYPAS_MAP_ERR_ZERO_LENGTH:       return "zero-length memory-map range";
    case SYPAS_MAP_ERR_ADDRESS_ALIGNMENT: return "unaligned memory-map range";
    case SYPAS_MAP_ERR_ADDRESS_OVERFLOW:  return "memory-map address overflow";
    case SYPAS_MAP_ERR_UNSORTED:          return "overlapping memory-map ranges";
    case SYPAS_MAP_ERR_OUTPUT_FULL:       return "translated memory map is full";
    case SYPAS_MAP_ERR_OUTPUT_OVERFLOW:   return "translated memory-map overflow";
    case SYPAS_MAP_ERR_KERNEL_NOT_TAGGED: return "kernel ranges absent from loader map";
    }
    return "unknown memory-map error";
}
