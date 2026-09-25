/*
 * SYPAS Boot Protocol v1
 * ======================
 *
 * This header is the single source of truth for the contract between the
 * SYPAS bootloader and the SYPAS kernel.  It is shared verbatim by both
 * sides.  See docs/boot-protocol.md for the normative description.
 *
 * Rules:
 *  - Every field is fixed-width little-endian.
 *  - The structure may only ever grow.  `size` tells the kernel how much of
 *    the structure the bootloader actually filled in.
 *  - `version` is bumped on any semantic change.  The kernel must refuse a
 *    major version it does not understand.
 *  - All pointers are physical addresses, valid under the identity mapping
 *    that is active when the kernel is entered (see execution environment
 *    below).
 *
 * Execution environment at kernel entry (v1):
 *  - CPU in 64-bit long mode, interrupts disabled, direction flag clear.
 *  - Paging enabled with the firmware's identity mapping still active
 *    (UEFI maps all system memory 1:1).  The kernel must build its own
 *    page tables before releasing any EFI_BOOT_SERVICES_* memory.
 *  - RSP points to the top of a dedicated 64 KiB stack allocated by the
 *    bootloader (type SYPAS_MEM_LOADER in the memory map).
 *  - RDI holds the physical address of the sypas_bootinfo structure.
 *  - No other register content is defined.
 *  - ExitBootServices() has been called: firmware boot services are gone,
 *    runtime services still exist but are NOT remapped (kernel may not call
 *    them in v1).
 */

#ifndef SYPAS_BOOTPROTO_H
#define SYPAS_BOOTPROTO_H

#include <stdint.h>

/* "SYPASBP1" as a little-endian 64-bit integer */
#define SYPAS_BOOT_MAGIC   0x3150425341505953ULL
#define SYPAS_BOOT_VERSION 1

/* ---- Memory map ------------------------------------------------------- */

enum sypas_mem_type {
    SYPAS_MEM_USABLE       = 1, /* free RAM, kernel may use immediately     */
    SYPAS_MEM_RESERVED     = 2, /* never touch                              */
    SYPAS_MEM_ACPI_RECLAIM = 3, /* ACPI tables, reclaimable after parsing   */
    SYPAS_MEM_ACPI_NVS     = 4, /* ACPI NVS, never touch                    */
    SYPAS_MEM_MMIO         = 5, /* memory-mapped I/O                        */
    SYPAS_MEM_LOADER       = 6, /* bootloader allocations: bootinfo, memory
                                   map, kernel stack, loader image.
                                   Reclaimable once the kernel no longer
                                   references them (kernel keeps them
                                   reserved in v1).                         */
    SYPAS_MEM_KERNEL       = 7, /* the loaded kernel image                  */
    SYPAS_MEM_FIRMWARE     = 8, /* EFI boot-services + runtime memory.
                                   v1: keep reserved (the active identity
                                   page tables live here).  Reclaimable for
                                   the boot-services part once the kernel
                                   owns its own page tables.               */
};

typedef struct sypas_memmap_entry {
    uint64_t base;      /* physical start, page aligned                    */
    uint64_t length;    /* bytes, page aligned                             */
    uint32_t type;      /* enum sypas_mem_type                             */
    uint32_t reserved;  /* must be 0                                       */
} sypas_memmap_entry_t;

/* ---- Framebuffer ------------------------------------------------------ */

enum sypas_fb_format {
    SYPAS_FB_NONE     = 0,  /* no linear framebuffer available             */
    SYPAS_FB_XRGB8888 = 1,  /* byte order in memory: B, G, R, X            */
    SYPAS_FB_XBGR8888 = 2,  /* byte order in memory: R, G, B, X            */
};

/* ---- Boot information structure --------------------------------------- */

typedef struct sypas_bootinfo {
    uint64_t magic;             /* SYPAS_BOOT_MAGIC                        */
    uint32_t version;           /* SYPAS_BOOT_VERSION                      */
    uint32_t size;              /* bytes of this struct that are valid     */

    /* Memory */
    uint64_t memmap;            /* phys addr of sypas_memmap_entry array   */
    uint64_t memmap_count;

    /* Framebuffer (from UEFI GOP; format SYPAS_FB_NONE if unavailable) */
    uint64_t fb_base;           /* physical address                        */
    uint32_t fb_width;          /* pixels                                  */
    uint32_t fb_height;         /* pixels                                  */
    uint32_t fb_pitch;          /* bytes per scanline                      */
    uint32_t fb_format;         /* enum sypas_fb_format                    */

    /* Platform */
    uint64_t acpi_rsdp;         /* phys addr of RSDP, 0 if not found       */
    uint64_t efi_system_table;  /* phys addr of EFI system table           */

    /* Kernel image */
    uint64_t kernel_phys_base;  /* lowest PT_LOAD physical address         */
    uint64_t kernel_size;       /* bytes spanned by loaded segments        */

    /* Stack handed to the kernel */
    uint64_t stack_base;        /* physical base of kernel boot stack      */
    uint64_t stack_size;        /* bytes                                   */

    /* Boot command line (NUL terminated, may be empty) */
    char     cmdline[256];
} sypas_bootinfo_t;

#endif /* SYPAS_BOOTPROTO_H */
