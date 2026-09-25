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
#include "../protocols/sypas_bootproto.h"

#define KERNEL_PATH L"\\SYPAS\\KERNEL.ELF"

/* ---- ELF64 ------------------------------------------------------------- */

#define ELF_MAGIC   0x464C457FU /* "\x7fELF" */
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
 * Returns entry point; fills base/size of the loaded span. */
static uint64_t load_kernel_elf(const uint8_t *file, UINTN fsize,
                                uint64_t *phys_base, uint64_t *phys_size)
{
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)file;

    if (fsize < sizeof(*eh) || eh->e_magic != ELF_MAGIC)
        die(L"kernel is not an ELF image", EFI_LOAD_ERROR);
    if (eh->e_class != 2 || eh->e_machine != EM_X86_64 || eh->e_type != ET_EXEC)
        die(L"kernel is not an x86_64 executable", EFI_LOAD_ERROR);

    uint64_t lo = ~0ULL, hi = 0;
    const Elf64_Phdr *ph = (const Elf64_Phdr *)(file + eh->e_phoff);

    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0)
            continue;

        uint64_t seg_start = ph[i].p_paddr & ~0xFFFULL;
        uint64_t seg_end   = (ph[i].p_paddr + ph[i].p_memsz + 0xFFF) & ~0xFFFULL;

        EFI_PHYSICAL_ADDRESS addr = seg_start;
        EFI_STATUS st = BS->AllocatePages(AllocateAddress, EfiLoaderData,
                                          (seg_end - seg_start) / EFI_PAGE_SIZE,
                                          &addr);
        if (EFI_ERROR(st))
            die(L"kernel load address is occupied "
                L"(SYPAS v1 requires its fixed physical base to be free)", st);

        lmemset((void *)seg_start, 0, seg_end - seg_start);
        lmemcpy((void *)ph[i].p_paddr, file + ph[i].p_offset, ph[i].p_filesz);

        if (seg_start < lo) lo = seg_start;
        if (seg_end   > hi) hi = seg_end;
    }

    if (hi == 0)
        die(L"kernel has no loadable segments", EFI_LOAD_ERROR);

    *phys_base = lo;
    *phys_size = hi - lo;
    return eh->e_entry;
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
        /* All loader allocations, incl. the kernel image itself; the kernel
         * re-tags its own image using kernel_phys_base/kernel_size. */
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
    default:
        return SYPAS_MEM_RESERVED;
    }
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
    bi->version          = SYPAS_BOOT_VERSION;
    bi->size             = sizeof(*bi);
    bi->kernel_phys_base = kbase;
    bi->kernel_size      = ksize;
    bi->stack_base       = stack_base;
    bi->stack_size       = 16 * EFI_PAGE_SIZE;
    bi->efi_system_table = (uint64_t)ST;
    bi->acpi_rsdp        = find_acpi_rsdp();
    fill_framebuffer(bi);

    /* 3. Memory map buffers.
     * Allocate ONCE with generous slack, then never allocate again: any
     * allocation after GetMemoryMap invalidates the map key. */
    UINTN mmsize = 0, mapkey = 0, dsz = 0;
    UINT32 dver = 0;
    BS->GetMemoryMap(&mmsize, 0, &mapkey, &dsz, &dver);   /* query size */
    mmsize += 16 * dsz;
    UINTN mm_pages = EFI_SIZE_TO_PAGES(mmsize);
    /* Same buffer also receives the translated SYPAS entries afterwards;
     * reserve room for both regions. */
    UINTN sypas_pages =
        EFI_SIZE_TO_PAGES((mmsize / dsz + 16) * sizeof(sypas_memmap_entry_t));
    st = BS->AllocatePages(AllocateAnyPages, EfiLoaderData,
                           mm_pages + sypas_pages, &mm_buf);
    if (EFI_ERROR(st)) die(L"memory map alloc failed", st);

    EFI_MEMORY_DESCRIPTOR *efimap = (EFI_MEMORY_DESCRIPTOR *)mm_buf;
    sypas_memmap_entry_t *smap =
        (sypas_memmap_entry_t *)(mm_buf + mm_pages * EFI_PAGE_SIZE);

    print(L"entering SYPAS kernel...\r\n\r\n");

    /* 4. Final map + ExitBootServices (retry once: printing or firmware
     * activity may have changed the map key). */
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

    /* 5. Translate the memory map (coalescing adjacent same-type ranges). */
    uint64_t count = 0;
    for (UINTN off = 0; off < mmsize; off += dsz) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)((uint8_t *)efimap + off);
        uint64_t base = d->PhysicalStart;
        uint64_t len  = d->NumberOfPages * EFI_PAGE_SIZE;
        uint32_t type = efi_to_sypas_memtype(d->Type);
        if (len == 0)
            continue;
        if (count && smap[count-1].type == type &&
            smap[count-1].base + smap[count-1].length == base) {
            smap[count-1].length += len;
        } else {
            smap[count].base = base;
            smap[count].length = len;
            smap[count].type = type;
            smap[count].reserved = 0;
            count++;
        }
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
