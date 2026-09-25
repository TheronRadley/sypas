# SYPAS Measured Performance

Rule: numbers only with context. Every entry states configuration,
method, and result. No number here is a projection.

## Test environment (all measurements below)

- QEMU 9.2.4, `-machine q35 -cpu max`, **TCG** (no KVM available in the
  dev sandbox — expect faster absolute times under KVM/hardware; TCG
  numbers are still valid for regression comparison).
- OVMF firmware: edk2-stable202411 blobs from the QEMU 9.2.4 tree.
- Host: 2-core x86_64 sandbox, 3.9 GiB RAM (which is why the "4 GiB
  guest" configs run at 3 GiB here).
- Image under test: `release/sypas-0.1.0.iso`
  (sha256 `39347b2c...d373cc`).
- Date: 2026-09-25.

## Boot time (benchmarks/boot/run.py, 5 runs per config)

Method: wall clock from QEMU process start to the serial
`SYSTEM STATUS: RUNNING` marker. Includes OVMF firmware init, which
dominates.

| Config | median | min | max |
|---|---|---|---|
| 2 CPU / 2 GiB | 3.51 s | 3.51 s | 3.71 s |
| 4 CPU / 3 GiB | 3.91 s | 3.81 s | 3.91 s |

Kernel-side split (PIT-timed by the kernel itself): **260 ms** from
kernel entry to RUNNING, of which **250 ms is the deliberate timer
verification window** (counting real PIT interrupts). Actual
initialization work is ~10 ms; the verification window is a correctness
feature, not overhead we will "optimize" away dishonestly.

Storage-class boot budgets (HDD/SSD) become measurable when SYPAS has
storage drivers; not claimed now.

## Physical memory allocator (in-kernel self-test, every boot)

Method: TSC deltas around 512 alloc / 512 free, printed by
`pmm_selftest()` on the serial console.

| Metric | Observed across runs |
|---|---|
| cycles/alloc (next-fit, early-boot pattern) | ~519–1006 |
| cycles/free | ~443–499 |
| self-test | uniqueness + write pattern + exact free-count restore: PASS every boot |

Caveats: TCG "cycles" are not real silicon cycles; the value is a
regression baseline, not a hardware claim. Worst-case (nearly-full
bitmap) is not exercised yet — noted in DR-5.

## Memory footprint at idle (2 GiB guest, from the boot memory map + PMM)

| Item | Measured |
|---|---|
| Kernel image in RAM (text+rodata+data+bss incl. 64 KiB stack) | 96 KiB |
| Loader allocations (bootinfo, memory map, kernel stack, loader image) | 216 KiB |
| PMM bitmap | 63 KiB |
| **Total SYPAS-owned runtime memory** | **~375 KiB** |
| Firmware memory still reserved (v1 identity-paging conservatism, DR-3) | 46.1 MiB |
| Usable RAM reported to allocator | 1998.7 MiB |

The 46 MiB firmware reservation is the current honest cost of not yet
owning page tables; it is reclaimed in Phase 4 and tracked as a budget
item, not hidden.

## Idle CPU (60 s window)

Method: guest boots to RUNNING, then host-side CPU time of the QEMU
process (`/proc/<pid>/stat`, utime+stime) sampled over 60 s.

| Metric | Result |
|---|---|
| QEMU process CPU during guest idle | **0.0 %** (below 1 tick/60 s resolution) |
| Guest wakeups | 100 Hz PIT only (bring-up timer, DR-4) |
| Free-page count drift over 60 s idle | 0 pages (511481 → 511481) |

The kernel idles in `hlt` and renders nothing when nothing changes —
the event-driven contract holds from the first phase.

## Long-run / leak checks

- 60 s idle: zero free-page drift, heartbeats at exact 10 s intervals.
- 1 h / 8 h / 24 h soak runs: **NOT RUN YET** (planned once dynamic
  allocation exists beyond the self-test; the heartbeat exists to make
  them observable).

## Frame timing / graphics

Not applicable yet — no compositor exists, and SYPAS does not fake
graphics metrics. The framebuffer console is measured only by its
boot-time contribution (included in the 260 ms kernel init above).

## Physical hardware

**NOT TESTED.** No hardware numbers are claimed anywhere in SYPAS
documentation until the matrix in docs/hardware-target.md runs.
