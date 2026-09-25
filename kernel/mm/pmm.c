/*
 * SYPAS kernel — physical memory manager (bitmap page allocator).
 *
 * Phase 3 allocator: one bit per 4 KiB page over the highest usable
 * physical address, with a next-fit cursor.  Simple, measurable, and easy
 * to verify; a zoned/buddy design replaces it only when benchmarks show
 * the need (docs/technology-decisions.md, DR-7).
 *
 * The bitmap itself is carved from the first usable region large enough
 * to hold it, and those pages are marked allocated before general use.
 * Everything not SYPAS_MEM_USABLE stays permanently marked allocated.
 */

#include "../include/kernel.h"

static u8  *bitmap;          /* 1 = allocated/reserved, 0 = free */
static u64  bitmap_bytes;
static u64  highest_page;    /* number of tracked pages          */
static u64  free_pages;
static u64  total_usable_pages;
static u64  cursor;          /* next-fit scan position           */
static u64  usable_bytes, reserved_bytes;

static inline void bit_set(u64 page)   { bitmap[page >> 3] |=  (1 << (page & 7)); }
static inline void bit_clear(u64 page) { bitmap[page >> 3] &= ~(1 << (page & 7)); }
static inline bool bit_test(u64 page)  { return bitmap[page >> 3] & (1 << (page & 7)); }

void pmm_init(const sypas_bootinfo_t *bi)
{
    const sypas_memmap_entry_t *map = (const void *)bi->memmap;
    u64 n = bi->memmap_count;

    /* Pass 1: extent of tracked physical memory + usable accounting */
    u64 max_addr = 0;
    for (u64 i = 0; i < n; i++) {
        if (map[i].type == SYPAS_MEM_USABLE) {
            usable_bytes += map[i].length;
            if (map[i].base + map[i].length > max_addr)
                max_addr = map[i].base + map[i].length;
        } else if (map[i].type != SYPAS_MEM_MMIO) {
            reserved_bytes += map[i].length;
        }
    }
    if (max_addr == 0)
        panic("pmm: no usable memory in boot memory map");

    highest_page = max_addr / PAGE_SIZE;
    bitmap_bytes = ALIGN_UP(highest_page, 8) / 8;

    /* Pass 2: place the bitmap in the first usable region that fits.
     * Skip the first 1 MiB: legacy region, cheap insurance against
     * firmware quirks. */
    bitmap = 0;
    for (u64 i = 0; i < n; i++) {
        if (map[i].type != SYPAS_MEM_USABLE)
            continue;
        u64 base = map[i].base, len = map[i].length;
        if (base < 0x100000) {
            u64 skip = 0x100000 - base;
            if (len <= skip)
                continue;
            base += skip;
            len  -= skip;
        }
        if (len >= bitmap_bytes) {
            bitmap = (u8 *)base;
            break;
        }
    }
    if (!bitmap)
        panic("pmm: no region large enough for %lu KiB bitmap",
              bitmap_bytes / 1024);

    /* Everything starts reserved; free exactly the usable ranges. */
    for (u64 i = 0; i < bitmap_bytes; i++)
        bitmap[i] = 0xFF;

    for (u64 i = 0; i < n; i++) {
        if (map[i].type != SYPAS_MEM_USABLE)
            continue;
        u64 first = ALIGN_UP(map[i].base, PAGE_SIZE) / PAGE_SIZE;
        u64 last  = ALIGN_DOWN(map[i].base + map[i].length, PAGE_SIZE) / PAGE_SIZE;
        for (u64 p = first; p < last; p++) {
            if (p == 0)
                continue;              /* never hand out page 0 */
            bit_clear(p);
            free_pages++;
        }
    }
    total_usable_pages = free_pages;

    /* Reserve the bitmap's own pages and the low 1 MiB. */
    u64 bm_first = (u64)bitmap / PAGE_SIZE;
    u64 bm_last  = ALIGN_UP((u64)bitmap + bitmap_bytes, PAGE_SIZE) / PAGE_SIZE;
    for (u64 p = bm_first; p < bm_last; p++) {
        if (!bit_test(p)) { bit_set(p); free_pages--; }
    }
    for (u64 p = 0; p < 256 && p < highest_page; p++) {
        if (!bit_test(p)) { bit_set(p); free_pages--; }
    }

    cursor = bm_last;
}

paddr_t pmm_alloc_page(void)
{
    /* Next-fit: resume where the last search stopped; wrap once. */
    for (u64 scanned = 0; scanned < highest_page; scanned++) {
        u64 p = cursor;
        cursor = (cursor + 1) % highest_page;

        /* Fast-skip fully allocated bytes */
        if ((p & 7) == 0 && bitmap[p >> 3] == 0xFF) {
            cursor = (p + 8) % highest_page;
            scanned += 7;
            continue;
        }
        if (!bit_test(p)) {
            bit_set(p);
            free_pages--;
            return p * PAGE_SIZE;
        }
    }
    return 0;   /* exhausted: caller decides policy */
}

void pmm_free_page(paddr_t addr)
{
    u64 p = addr / PAGE_SIZE;
    if (addr & (PAGE_SIZE - 1))
        panic("pmm: free of unaligned address %lx", addr);
    if (p >= highest_page)
        panic("pmm: free of out-of-range address %lx", addr);
    if (!bit_test(p))
        panic("pmm: double free of page %lx", addr);
    bit_clear(p);
    free_pages++;
}

void pmm_get_stats(pmm_stats_t *out)
{
    out->total_pages    = total_usable_pages;
    out->free_pages     = free_pages;
    out->usable_bytes   = usable_bytes;
    out->reserved_bytes = reserved_bytes;
    out->bitmap_bytes   = bitmap_bytes;
}

/*
 * Self-test: allocate a batch, verify uniqueness + writability, free,
 * verify free count restores.  Also measures alloc/free cost in TSC
 * cycles and reports it — that number goes into docs/performance.md.
 */
#define ST_PAGES 512

bool pmm_selftest(void)
{
    static paddr_t got[ST_PAGES];
    u64 before = free_pages;

    u64 t0 = rdtsc();
    for (int i = 0; i < ST_PAGES; i++) {
        got[i] = pmm_alloc_page();
        if (!got[i]) {
            kprintf("[pmm ] selftest: exhausted at %d pages\n", i);
            return false;
        }
    }
    u64 t1 = rdtsc();

    /* uniqueness + write pattern */
    for (int i = 0; i < ST_PAGES; i++) {
        *(volatile u64 *)got[i] = 0x5350415359ULL ^ got[i];
        for (int j = i + 1; j < ST_PAGES; j++)
            if (got[i] == got[j]) {
                kprintf("[pmm ] selftest: duplicate page %lx\n", got[i]);
                return false;
            }
    }
    for (int i = 0; i < ST_PAGES; i++)
        if (*(volatile u64 *)got[i] != (0x5350415359ULL ^ got[i])) {
            kprintf("[pmm ] selftest: pattern mismatch at %lx\n", got[i]);
            return false;
        }

    u64 t2 = rdtsc();
    for (int i = 0; i < ST_PAGES; i++)
        pmm_free_page(got[i]);
    u64 t3 = rdtsc();

    if (free_pages != before) {
        kprintf("[pmm ] selftest: leak: %lu -> %lu pages\n",
                before, free_pages);
        return false;
    }

    kprintf("[pmm ] selftest: %u alloc+free OK, ~%lu cycles/alloc, "
            "~%lu cycles/free (TSC, VM-measured)\n",
            ST_PAGES, (t1 - t0) / ST_PAGES, (t3 - t2) / ST_PAGES);
    return true;
}
