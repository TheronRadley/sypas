/*
 * SYPAS UEFI bootloader, stage: loader.
 *
 * Job (SYPAS Boot Protocol v1):
 *   1. start under UEFI
 *   2. locate and read \SYPAS\KERNEL.ELF from the boot volume
 *   3. load the kernel's PT_LOAD segments at their linked physical address
 *   4. gather framebuffer info (GOP) and the ACPI RSDP
 *   5. obtain the final memory map and call ExitBootServices()
 *   6. translate the memory map into SYPAS boot protocol format
 *   7. jump to the kernel entry with RDI = &sypas_bootinfo
 *
 * Deliberately not a boot manager: no menu, no config files, no editing.
 * Keep it small and reliable; features come later.
 */

#include "efi.h"
#include "elf.h"
#include "../protocols/sypas_bootproto.h"

#define KERNEL_PATH L"\\SYPAS\\KERNEL.ELF"

/* ---- Globals ------------------------------------------------------------ */

static EFI_SYSTEM_TABLE  *ST;
static EFI_BOOT_SERVICES *BS;

static EFI_GUID gLoadedImage = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID gSfs         = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID gFileInfo    = EFI_FILE_INFO_GUID;
static EFI_GUID gGop         = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GUID gAcpi20      = EFI_ACPI_20_TABLE_GUID;
static EFI_GUID gAcpi10      = EFI_ACPI_10_TABLE_GUID;

/* ---- Small helpers (no libc here) ---------------------------------------- */

static void *lmemcpy(void *dst, const void *src, UINTN n)
{
    uint8_t *d = dst; const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

static void *lmemset(void *dst, int c, UINTN n)
{
    uint8_t *d = dst;
    while (n--) *d++ = (uint8_t)c;
    return dst;
}

static int guid_eq(const EFI_GUID *a, const EFI_GUID *b)
{
    const uint64_t *x = (const uint64_t *)a, *y = (const uint64_t *)b;
    return x[0] == y[0] && x[1] == y[1];
}

static void print(CHAR16 *s) { ST->ConOut->OutputString(ST->ConOut, s); }

static void print_hex(uint64_t v)
{
    static const CHAR16 hexdig[] = L"0123456789ABCDEF";
    CHAR16 buf[19];
    buf[0] = L'0'; buf[1] = L'x';
    for (int i = 0; i < 16; i++)
        buf[2 + i] = hexdig[(v >> (60 - 4 * i)) & 0xF];
    buf[18] = 0;
    print(buf);
}

/* Fatal: report, give the operator time to read, then hang. */
static void die(CHAR16 *msg, EFI_STATUS st)
{
    print(L"\r\nSYPAS loader error: ");
    print(msg);
    print(L" (status ");
    print_hex(st);
    print(L")\r\nSystem halted.\r\n");
    for (;;) BS->Stall(1000000);
}

/* die() with an ASCII detail string (elf_status_str lives in shared,
 * UEFI-free code and cannot produce CHAR16). */
static void die_ascii(CHAR16 *msg, const char *detail, EFI_STATUS st)
{
    CHAR16 buf[64];
    int i = 0;
    while (detail[i] && i < 63) { buf[i] = (CHAR16)detail[i]; i++; }
    buf[i] = 0;
    print(L"\r\nSYPAS loader error: ");
    print(msg);
    print(L": ");
    die(buf, st);
}

/* ---- Kernel file loading -------------------------------------------------- */

static void *read_kernel_file(EFI_HANDLE img, UINTN *out_size)
{
    EFI_STATUS st;
    EFI_LOADED_IMAGE_PROTOCOL *li;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
    EFI_FILE_PROTOCOL *root, *f;

    st = BS->HandleProtocol(img, &gLoadedImage, (void **)&li);
    if (EFI_ERROR(st)) die(L"LoadedImage protocol missing", st);

    st = BS->HandleProtocol(li->DeviceHandle, &gSfs, (void **)&fs);
    if (EFI_ERROR(st)) die(L"boot volume has no filesystem", st);

    st = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(st)) die(L"OpenVolume failed", st);

    st = root->Open(root, &f, KERNEL_PATH, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(st)) die(L"\\SYPAS\\KERNEL.ELF not found", st);

    /* file size via EFI_FILE_INFO */
    uint8_t infobuf[sizeof(EFI_FILE_INFO) + 128 * sizeof(CHAR16)];
    UINTN infosz = sizeof(infobuf);
    st = f->GetInfo(f, &gFileInfo, &infosz, infobuf);
    if (EFI_ERROR(st)) die(L"GetInfo failed", st);
    UINTN fsize = ((EFI_FILE_INFO *)infobuf)->FileSize;

    void *buf;
    st = BS->AllocatePool(EfiLoaderData, fsize, &buf);
    if (EFI_ERROR(st)) die(L"out of memory for kernel file", st);

    UINTN rd = fsize;
    st = f->Read(f, &rd, buf);
    if (EFI_ERROR(st) || rd != fsize) die(L"kernel read failed", st);

    f->Close(f);
    root->Close(root);
    *out_size = fsize;
    return buf;
}

/* Load PT_LOAD segments to their linked physical addresses.
 * Returns entry point; fills base/size of the loaded span.
 *
 * All parsing/validation lives in elf_plan_load() (elf.c, unit-tested
 * on the host against malformed images); this function only executes
 * an already-proven plan: allocate, zero, copy. */
static uint64_t load_kernel_elf(const uint8_t *file, UINTN fsize,
                                uint64_t *phys_base, uint64_t *phys_size)
{
    elf_load_plan_t plan;
    elf_status_t es = elf_plan_load(file, fsize, &plan);
    if (es != ELF_OK)
        die_ascii(L"kernel image rejected", elf_status_str(es),
                  EFI_LOAD_ERROR);

    for (int i = 0; i < plan.nsegs; i++) {
        const elf_segment_t *seg = &plan.segs[i];

        EFI_PHYSICAL_ADDRESS addr = seg->page_base;
        EFI_STATUS st = BS->AllocatePages(AllocateAddress, EfiLoaderData,
                                          (seg->page_end - seg->page_base)
                                              / EFI_PAGE_SIZE,
                                          &addr);
        if (EFI_ERROR(st))
            die(L"kernel load address is occupied "
                L"(SYPAS v1 requires its fixed physical base to be free)", st);

        lmemset((void *)seg->page_base, 0, seg->page_end - seg->page_base);
        lmemcpy((void *)seg->paddr, file + seg->offset, seg->filesz);
    }

    *phys_base = plan.phys_base;
    *phys_size = plan.phys_end - plan.phys_base;
    return plan.entry;
}

/* ---- Framebuffer / ACPI --------------------------------------------------- */

static void fill_framebuffer(sypas_bootinfo_t *bi)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    bi->fb_format = SYPAS_FB_NONE;

    if (EFI_ERROR(BS->LocateProtocol(&gGop, 0, (void **)&gop)) || !gop)
        return;

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = gop->Mode->Info;
    switch (mi->PixelFormat) {
    case PixelBlueGreenRedReserved8BitPerColor:
        bi->fb_format = SYPAS_FB_XRGB8888; break;
    case PixelRedGreenBlueReserved8BitPerColor:
        bi->fb_format = SYPAS_FB_XBGR8888; break;
    default:
        return; /* PixelBltOnly / bitmask: unusable after ExitBootServices */
    }

    bi->fb_base   = gop->Mode->FrameBufferBase;
    bi->fb_width  = mi->HorizontalResolution;
    bi->fb_height = mi->VerticalResolution;
    bi->fb_pitch  = mi->PixelsPerScanLine * 4;
}

static uint64_t find_acpi_rsdp(void)
{
    void *v1 = 0;
    for (UINTN i = 0; i < ST->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE *ct = &ST->ConfigurationTable[i];
        if (guid_eq(&ct->VendorGuid, &gAcpi20))
            return (uint64_t)ct->VendorTable;      /* prefer ACPI >= 2.0 */
        if (guid_eq(&ct->VendorGuid, &gAcpi10))
            v1 = ct->VendorTable;
    }
    return (uint64_t)v1;
}

/* ---- Memory map translation ------------------------------------------------ */

static uint32_t efi_to_sypas_memtype(uint32_t t)
{
    switch (t) {
    case EfiConventionalMemory:
        return SYPAS_MEM_USABLE;
    case EfiLoaderCode:
    case EfiLoaderData:
        /* Loader allocations: bootinfo, stack, map buffers, loader image.
         * The kernel image is also EfiLoaderData at this point; the
         * translation loop below splits those ranges out and re-tags
         * them SYPAS_MEM_KERNEL so the handoff map states real
         * ownership. */
        return SYPAS_MEM_LOADER;
    case EfiBootServicesCode:
    case EfiBootServicesData:
    case EfiRuntimeServicesCode:
    case EfiRuntimeServicesData:
        /* v1: the firmware identity page tables the kernel still runs on
         * live in boot-services data.  Keep firmware memory reserved until
         * the kernel builds its own page tables (documented limitation). */
        return SYPAS_MEM_FIRMWARE;
    case EfiACPIReclaimMemory:
        return SYPAS_MEM_ACPI_RECLAIM;
    case EfiACPIMemoryNVS:
        return SYPAS_MEM_ACPI_NVS;
    case EfiMemoryMappedIO:
    case EfiMemoryMappedIOPortSpace:
        return SYPAS_MEM_MMIO;
    case EfiUnacceptedMemoryType:
        /* UEFI 2.9+/2.10 unaccepted memory (TDX/SEV-SNP): not usable
         * until accepted, which v1 does not do. */
        return SYPAS_MEM_RESERVED;
    default:
        return SYPAS_MEM_RESERVED;
    }
}

/* Append [base, base+len) with `type`, coalescing with the previous
 * entry when contiguous and same-typed.  `cap` is the entry capacity of
 * the output buffer; on overflow the count is returned unchanged and
 * *overflow is set.  The caller halts on overflow: a truncated memory
 * map silently misclassifies RAM, which is worse than a visible hang. */
static uint64_t smap_append(sypas_memmap_entry_t *smap, uint64_t count,
                            uint64_t cap, int *overflow,
                            uint64_t base, uint64_t len, uint32_t type)
{
    if (len == 0)
        return count;
    if (count && smap[count - 1].type == type &&
        smap[count - 1].base + smap[count - 1].length == base) {
        smap[count - 1].length += len;
        return count;
    }
    if (count == cap) {
        *overflow = 1;
        return count;
    }
    smap[count].base = base;
    smap[count].length = len;
    smap[count].type = type;
    smap[count].reserved = 0;
    return count + 1;
}

/* ---- Entry ------------------------------------------------------------------ */

EFI_STATUS EFIAPI efi_main(EFI_HANDLE img, EFI_SYSTEM_TABLE *systab)
{
    ST = systab;
    BS = systab->BootServices;
    EFI_STATUS st;

    print(L"SYPAS bootloader 0.1.0 (boot protocol v1)\r\n");

    /* Don't let the firmware watchdog reset us mid-load. */
    BS->SetWatchdogTimer(0, 0, 0, 0);

    /* 1. Kernel file -> memory */
    UINTN fsize;
    void *file = read_kernel_file(img, &fsize);
    print(L"loaded \\SYPAS\\KERNEL.ELF, parsing...\r\n");

    uint64_t kbase, ksize;
    uint64_t entry = load_kernel_elf(file, fsize, &kbase, &ksize);
    BS->FreePool(file);

    /* 2. Fixed allocations for the handoff */
    EFI_PHYSICAL_ADDRESS bi_page = 0, stack_base = 0, mm_buf = 0;

    st = BS->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &bi_page);
    if (EFI_ERROR(st)) die(L"bootinfo alloc failed", st);

    st = BS->AllocatePages(AllocateAnyPages, EfiLoaderData, 16, &stack_base);
    if (EFI_ERROR(st)) die(L"stack alloc failed", st);

    sypas_bootinfo_t *bi = (sypas_bootinfo_t *)bi_page;
    lmemset(bi, 0, sizeof(*bi));
    bi->magic            = SYPAS_BOOT_MAGIC;
    bi->version_major    = SYPAS_BOOT_VERSION_MAJOR;
    bi->version_minor    = SYPAS_BOOT_VERSION_MINOR;
    bi->size             = sizeof(*bi);
    bi->kernel_phys_base = kbase;
    bi->kernel_size      = ksize;
    bi->stack_base       = stack_base;
    bi->stack_size       = 16 * EFI_PAGE_SIZE;
    bi->efi_system_table = (uint64_t)ST;
    bi->acpi_rsdp        = find_acpi_rsdp();
    fill_framebuffer(bi);

    /* 3. Memory map buffers.
     * UEFI says the map may grow between the size query and the final
     * call (our own allocations below, firmware activity), and that
     * DescriptorSize may exceed sizeof(EFI_MEMORY_DESCRIPTOR).  Size the
     * buffer in a loop — allocate, query, and if the firmware still
     * reports EFI_BUFFER_TOO_SMALL, free and grow.  All allocation
     * happens here, strictly before the ExitBootServices sequence,
     * because after a failed ExitBootServices only GetMemoryMap and
     * ExitBootServices may be called. */
    UINTN mmsize = 0, mapkey = 0, dsz = 0;
    UINT32 dver = 0;
    st = BS->GetMemoryMap(&mmsize, 0, &mapkey, &dsz, &dver); /* size query */
    if (st != EFI_BUFFER_TOO_SMALL || dsz == 0)
        die(L"GetMemoryMap size query failed", st);

    UINTN mm_pages = 0, sypas_cap = 0, sypas_pages = 0;
    for (int grow = 0; ; grow++) {
        if (grow == 4)
            die(L"memory map keeps growing", EFI_BUFFER_TOO_SMALL);

        /* Slack: our own map/handoff allocations + firmware churn. */
        mmsize   += 32 * dsz;
        mm_pages  = EFI_SIZE_TO_PAGES(mmsize);
        /* Same allocation also receives the translated SYPAS entries.
         * Splitting the kernel range out of loader ranges can add up to
         * two entries per descriptor in the worst case: reserve 2x. */
        sypas_cap   = 2 * (mmsize / dsz) + 16;
        sypas_pages = EFI_SIZE_TO_PAGES(sypas_cap * sizeof(sypas_memmap_entry_t));

        st = BS->AllocatePages(AllocateAnyPages, EfiLoaderData,
                               mm_pages + sypas_pages, &mm_buf);
        if (EFI_ERROR(st)) die(L"memory map alloc failed", st);

        UINTN sz = mm_pages * EFI_PAGE_SIZE;
        st = BS->GetMemoryMap(&sz, (EFI_MEMORY_DESCRIPTOR *)mm_buf,
                              &mapkey, &dsz, &dver);
        if (!EFI_ERROR(st)) {
            mmsize = sz;
            break;
        }
        if (st != EFI_BUFFER_TOO_SMALL)
            die(L"GetMemoryMap failed", st);
        BS->FreePages(mm_buf, mm_pages + sypas_pages);
        mmsize = sz;
    }

    EFI_MEMORY_DESCRIPTOR *efimap = (EFI_MEMORY_DESCRIPTOR *)mm_buf;
    sypas_memmap_entry_t *smap =
        (sypas_memmap_entry_t *)(mm_buf + mm_pages * EFI_PAGE_SIZE);

    print(L"entering SYPAS kernel...\r\n\r\n");

    /* 4. Final map + ExitBootServices.  The printing above may have
     * changed the map key, and the spec requires the key from the
     * latest GetMemoryMap; after a failed ExitBootServices only these
     * two calls are permitted, so the retry loop does nothing else. */
    for (int attempt = 0; ; attempt++) {
        UINTN sz = mm_pages * EFI_PAGE_SIZE;
        st = BS->GetMemoryMap(&sz, efimap, &mapkey, &dsz, &dver);
        if (EFI_ERROR(st)) die(L"GetMemoryMap failed", st);
        mmsize = sz;

        st = BS->ExitBootServices(img, mapkey);
        if (!EFI_ERROR(st))
            break;
        if (attempt == 3)
            die(L"ExitBootServices failed", st);
    }

    /* --- Boot services are gone. No more firmware calls, no printing. --- */

    /* 5. Translate the memory map, splitting out the kernel image.
     * The kernel was loaded as EfiLoaderData; carve its span out of the
     * loader ranges and tag it SYPAS_MEM_KERNEL so the handoff map is a
     * real ownership map, not documentation. */
    uint64_t kend = kbase + ksize;
    uint64_t count = 0;
    int overflow = 0;
    for (UINTN off = 0; off < mmsize; off += dsz) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)((uint8_t *)efimap + off);
        uint64_t base = d->PhysicalStart;
        uint64_t len  = d->NumberOfPages * EFI_PAGE_SIZE;
        uint64_t end  = base + len;
        uint32_t type = efi_to_sypas_memtype(d->Type);

        if (type == SYPAS_MEM_LOADER && base < kend && kbase < end) {
            uint64_t ov_lo = base > kbase ? base : kbase;
            uint64_t ov_hi = end  < kend  ? end  : kend;
            count = smap_append(smap, count, sypas_cap, &overflow,
                                base, ov_lo - base, SYPAS_MEM_LOADER);
            count = smap_append(smap, count, sypas_cap, &overflow,
                                ov_lo, ov_hi - ov_lo, SYPAS_MEM_KERNEL);
            count = smap_append(smap, count, sypas_cap, &overflow,
                                ov_hi, end - ov_hi, SYPAS_MEM_LOADER);
        } else {
            count = smap_append(smap, count, sypas_cap, &overflow,
                                base, len, type);
        }
    }
    if (overflow) {
        /* Cannot print or return post-ExitBootServices; a truncated map
         * must never reach the kernel.  Halt where a debugger sees it. */
        for (;;) __asm__ volatile("cli; hlt");
    }
    bi->memmap = (uint64_t)smap;
    bi->memmap_count = count;

    /* 6. Jump. RDI = bootinfo, fresh stack, interrupts off. */
    uint64_t stack_top = stack_base + bi->stack_size;
    __asm__ volatile(
        "cli\n\t"
        "movq %0, %%rsp\n\t"
        "xorl %%ebp, %%ebp\n\t"
        "jmp *%2\n\t"
        :
        : "r"(stack_top), "D"(bi), "r"(entry)
        : "memory");

    __builtin_unreachable();
}
