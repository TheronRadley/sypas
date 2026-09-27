/*
 * SYPAS UEFI bootloader, stage: loader.
 *
 * Job (SYPAS Boot Protocol v1):
 *   1. start under UEFI
 *   2. locate and read \SYPAS\KERNEL.ELF from the boot volume
 *   3. load the kernel's PT_LOAD segments at their linked physical address
 *   4. gather framebuffer info (GOP) and validate the ACPI RSDP
 *   5. obtain, validate and translate the final memory map
 *   6. call ExitBootServices() and jump to the kernel with RDI = bootinfo
 *
 * Deliberately not a boot manager: no menu, no config files, no editing.
 * Keep it small and reliable; features come later.
 */

#include "efi.h"
#include "elf.h"
#include "acpi.h"
#include "memory_map.h"
#include "sypas_version.h"
#include "../protocols/sypas_bootproto.h"

#define KERNEL_PATH L"\\SYPAS\\KERNEL.ELF"
#define MAP_SLACK_DESCRIPTORS 32ULL
#define MAP_ENTRIES_PER_DESCRIPTOR (2ULL * ELF_MAX_SEGMENTS + 1)

/* ---- Globals ------------------------------------------------------------ */

static EFI_SYSTEM_TABLE  *ST;
static EFI_BOOT_SERVICES *BS;

static EFI_GUID gLoadedImage = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID gSfs         = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID gFileInfo    = EFI_FILE_INFO_GUID;
static EFI_GUID gGop         = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GUID gAcpi20      = EFI_ACPI_20_TABLE_GUID;
static EFI_GUID gAcpi10      = EFI_ACPI_10_TABLE_GUID;

/* ---- Small helpers (no libc here) -------------------------------------- */

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

static int add_ok(UINTN a, UINTN b, UINTN *out)
{
    if (a > UINT64_MAX - b)
        return 0;
    *out = a + b;
    return 1;
}

static int mul_ok(UINTN a, UINTN b, UINTN *out)
{
    if (a && b > UINT64_MAX / a)
        return 0;
    *out = a * b;
    return 1;
}

static int pages_for_size(UINTN size, UINTN *pages)
{
    UINTN rounded;
    if (!add_ok(size, EFI_PAGE_SIZE - 1, &rounded))
        return 0;
    *pages = rounded / EFI_PAGE_SIZE;
    return 1;
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

/* Fatal before ExitBootServices: report, give the operator time to read,
 * then hang.  Never call this after an ExitBootServices attempt. */
static void die(CHAR16 *msg, EFI_STATUS st)
{
    print(L"\r\nSYPAS loader error: ");
    print(msg);
    print(L" (status ");
    print_hex(st);
    print(L")\r\nSystem halted.\r\n");
    for (;;) BS->Stall(1000000);
}

/* die() with an ASCII detail string (shared pure modules cannot produce
 * CHAR16). */
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

/* After any ExitBootServices() attempt, do not print, stall, allocate, or
 * otherwise touch firmware outside the permitted GetMemoryMap/EBS recovery
 * pair.  This terminal path deliberately has no firmware dependency. */
static void halt_after_exit_boot_services(void) __attribute__((noreturn));
static void halt_after_exit_boot_services(void)
{
    for (;;)
        __asm__ volatile("cli; hlt");
    __builtin_unreachable();
}

/* ---- Kernel file loading ------------------------------------------------ */

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

    /* File size via EFI_FILE_INFO.  Bound before AllocatePool(): ELF span
     * validation happens after read, so it cannot protect this allocation. */
    uint8_t infobuf[sizeof(EFI_FILE_INFO) + 128 * sizeof(CHAR16)];
    UINTN infosz = sizeof(infobuf);
    st = f->GetInfo(f, &gFileInfo, &infosz, infobuf);
    if (EFI_ERROR(st)) die(L"GetInfo failed", st);
    UINTN fsize = ((EFI_FILE_INFO *)infobuf)->FileSize;
    if (fsize == 0 || fsize > SYPAS_MAX_KERNEL_FILE)
        die(L"kernel file size is outside loader policy", EFI_LOAD_ERROR);

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

/* Validate the complete image before touching a load address, then execute
 * its plan.  Keeping the plan lets final memory-map ownership use exact
 * PT_LOAD page ranges rather than the plan's min..max diagnostic span. */
static uint64_t load_kernel_elf(const uint8_t *file, UINTN fsize,
                                elf_load_plan_t *plan)
{
    elf_status_t es = elf_plan_load(file, fsize, plan);
    if (es != ELF_OK)
        die_ascii(L"kernel image rejected", elf_status_str(es),
                  EFI_LOAD_ERROR);

    for (int i = 0; i < plan->nsegs; i++) {
        const elf_segment_t *seg = &plan->segs[i];
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
    return plan->entry;
}

/* ---- Framebuffer / ACPI ------------------------------------------------ */

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
        if (guid_eq(&ct->VendorGuid, &gAcpi20) &&
            sypas_acpi_validate_rsdp(ct->VendorTable) == SYPAS_ACPI_RSDP_OK)
            return (uint64_t)ct->VendorTable;      /* prefer ACPI >= 2.0 */
        if (guid_eq(&ct->VendorGuid, &gAcpi10) &&
            sypas_acpi_validate_rsdp(ct->VendorTable) == SYPAS_ACPI_RSDP_OK)
            v1 = ct->VendorTable;
    }
    return (uint64_t)v1;
}

/* ---- Entry -------------------------------------------------------------- */

EFI_STATUS EFIAPI efi_main(EFI_HANDLE img, EFI_SYSTEM_TABLE *systab)
{
    ST = systab;
    BS = systab->BootServices;
    EFI_STATUS st;

    print(L"SYPAS bootloader " SYPAS_VERSION_W L" (boot protocol v1)\r\n");

    /* Don't let the firmware watchdog reset us mid-load. */
    BS->SetWatchdogTimer(0, 0, 0, 0);

    /* 1. Kernel file -> validated load plan -> fixed PT_LOAD addresses. */
    UINTN fsize;
    void *file = read_kernel_file(img, &fsize);
    print(L"loaded \\SYPAS\\KERNEL.ELF, parsing...\r\n");

    elf_load_plan_t kernel_plan;
    uint64_t entry = load_kernel_elf(file, fsize, &kernel_plan);
    BS->FreePool(file);

    /* 2. Fixed allocations for the handoff. */
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
    bi->kernel_phys_base = kernel_plan.phys_base;
    bi->kernel_size      = kernel_plan.phys_end - kernel_plan.phys_base;
    bi->stack_base       = stack_base;
    bi->stack_size       = 16 * EFI_PAGE_SIZE;
    bi->efi_system_table = (uint64_t)ST;
    bi->acpi_rsdp        = find_acpi_rsdp();
    fill_framebuffer(bi);

    /* 3. Memory-map buffers.  All allocation happens before the EBS retry
     * sequence.  The output capacity permits an input descriptor to split
     * around every possible exact PT_LOAD range, not merely twice. */
    UINTN mmsize = 0, mapkey = 0, dsz = 0;
    UINT32 dver = 0;
    st = BS->GetMemoryMap(&mmsize, 0, &mapkey, &dsz, &dver);
    if (st != EFI_BUFFER_TOO_SMALL || dsz == 0)
        die(L"GetMemoryMap size query failed", st);

    UINTN mm_pages = 0, sypas_cap = 0, sypas_pages = 0, map_bytes = 0;
    for (int grow = 0; ; grow++) {
        if (grow == 4)
            die(L"memory map keeps growing", EFI_BUFFER_TOO_SMALL);

        UINTN slack, requested, descriptor_capacity, map_entries;
        UINTN sypas_bytes, total_pages;
        if (dsz == 0 ||
            !mul_ok(MAP_SLACK_DESCRIPTORS, dsz, &slack) ||
            !add_ok(mmsize, slack, &requested) ||
            !pages_for_size(requested, &mm_pages) ||
            !mul_ok(mm_pages, EFI_PAGE_SIZE, &map_bytes) ||
            !add_ok(requested, dsz - 1, &descriptor_capacity))
            die(L"memory map size arithmetic overflow", EFI_LOAD_ERROR);
        descriptor_capacity /= dsz; /* ceil(requested / descriptor size) */
        if (!mul_ok(descriptor_capacity, MAP_ENTRIES_PER_DESCRIPTOR,
                    &map_entries) ||
            !add_ok(map_entries, 16, &sypas_cap) ||
            !mul_ok(sypas_cap, sizeof(sypas_memmap_entry_t), &sypas_bytes) ||
            !pages_for_size(sypas_bytes, &sypas_pages) ||
            !add_ok(mm_pages, sypas_pages, &total_pages))
            die(L"memory map size arithmetic overflow", EFI_LOAD_ERROR);

        st = BS->AllocatePages(AllocateAnyPages, EfiLoaderData,
                               total_pages, &mm_buf);
        if (EFI_ERROR(st)) die(L"memory map alloc failed", st);

        UINTN allocated_dsz = dsz;
        UINTN sz = map_bytes;
        st = BS->GetMemoryMap(&sz, (EFI_MEMORY_DESCRIPTOR *)mm_buf,
                              &mapkey, &dsz, &dver);
        if (!EFI_ERROR(st) && dsz == allocated_dsz) {
            mmsize = sz;
            break;
        }
        if (EFI_ERROR(st) && st != EFI_BUFFER_TOO_SMALL)
            die(L"GetMemoryMap failed", st);
        BS->FreePages(mm_buf, total_pages);
        mm_buf = 0;
        mmsize = sz;
        /* A changed descriptor stride is treated exactly like map growth:
         * recompute all output bounds before allocating again. */
    }

    EFI_MEMORY_DESCRIPTOR *efimap = (EFI_MEMORY_DESCRIPTOR *)mm_buf;
    sypas_memmap_entry_t *smap =
        (sypas_memmap_entry_t *)(mm_buf + map_bytes);

    print(L"entering SYPAS kernel...\r\n\r\n");

    /* 4. Final GetMemoryMap -> validation/translation -> ExitBootServices.
     * After a failed EBS only this recovery pair is legal.  A later failure
     * must halt without a console, Stall, allocation, or any other firmware
     * call; all human-readable diagnostics happened before this boundary. */
    for (int attempt = 0; ; attempt++) {
        UINTN sz = map_bytes;
        st = BS->GetMemoryMap(&sz, efimap, &mapkey, &dsz, &dver);
        if (EFI_ERROR(st)) {
            if (attempt == 0)
                die(L"GetMemoryMap failed", st);
            halt_after_exit_boot_services();
        }

        uint64_t smap_count = 0;
        sypas_map_diagnostic_t map_diagnostic;
        sypas_map_status_t map_status = sypas_translate_memory_map(
            efimap, sz, dsz, &kernel_plan, smap, sypas_cap, &smap_count,
            &map_diagnostic);
        if (map_status != SYPAS_MAP_OK) {
            if (attempt == 0) {
                if (map_status == SYPAS_MAP_ERR_RANGE_OVERLAP) {
                    print(L"\r\nmemory map overlap: descriptor ");
                    print_hex(map_diagnostic.descriptor_index);
                    print(L" range ");
                    print_hex(map_diagnostic.base);
                    print(L"..");
                    print_hex(map_diagnostic.end);
                    print(L" follows end ");
                    print_hex(map_diagnostic.previous_end);
                    print(L"\r\n");
                }
                die_ascii(L"memory map rejected", sypas_map_status_str(map_status),
                          EFI_LOAD_ERROR);
            }
            halt_after_exit_boot_services();
        }
        bi->memmap = (uint64_t)smap;
        bi->memmap_count = smap_count;

        st = BS->ExitBootServices(img, mapkey);
        if (!EFI_ERROR(st))
            break;
        if (attempt == 3)
            halt_after_exit_boot_services();
    }

    /* --- Boot services are gone. No more firmware calls. ---------------- */

    /* 5. Jump. RDI = bootinfo, fresh stack, interrupts off. */
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
