# SYPAS Memory Management (Phase 3 state)

## Memory map pipeline

```
UEFI GetMemoryMap
  → loader translates EFI types → SYPAS types (coalescing neighbors)
  → sypas_bootinfo.memmap (physical array, boot protocol v1)
  → kernel pmm_init()
```

SYPAS type semantics are defined in the boot protocol
(docs/boot-protocol.md). Key v1 conservatisms:

- `SYPAS_MEM_FIRMWARE` (EFI boot+runtime services memory) is *not*
  freed even though boot services ended, because the identity page
  tables the kernel still runs on live there. Reclaim happens in
  Phase 4 when the kernel owns page tables.
- `SYPAS_MEM_LOADER` (bootinfo, translated map, kernel stack, loader
  image) stays reserved until the kernel stops referencing those pages.
- Page 0 and the low 1 MiB are never handed out.

## Physical page allocator (kernel/mm/pmm.c)

Design: bitmap, 1 bit per 4 KiB page, next-fit cursor with a
byte-granular fast-skip. See DR-5 in docs/technology-decisions.md for
the trade-off record.

- Bitmap sized to the highest usable physical address
  (2 GiB RAM → 64 KiB bitmap) and carved from the first usable region
  that fits (above 1 MiB).
- API: `pmm_alloc_page` (returns 0 on exhaustion — caller decides
  policy), `pmm_free_page` (panics on unaligned, out-of-range, or
  double free: allocator corruption is never survivable),
  `pmm_get_stats`.
- Mandatory boot self-test: 512 alloc → uniqueness check → pattern
  write/verify → free → exact free-count restoration; measures TSC
  cycles per op. Failure = panic, not warning.

Measured behavior: docs/performance.md (§ PMM).

## Failure modes tested

| Case | Behavior |
|---|---|
| exhaustion | `pmm_alloc_page` returns 0 (self-test reports; callers must handle) |
| double free | panic with page address |
| unaligned free | panic |
| out-of-range free | panic |
| no usable RAM in map | panic at init |
| bitmap doesn't fit any region | panic at init |

## Not implemented yet (Phase 4+)

Kernel page tables, higher-half kernel, W^X, guard pages, user address
spaces, demand paging, kernel heap (Phase 5 builds on this PMM).
