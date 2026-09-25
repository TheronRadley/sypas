# SYPAS Performance Budget

Budgets are targets to engineer against and measure toward — not claims.
Current measured numbers live in docs/performance.md; a budget line
without a measurement is marked "not yet measurable".

## Long-term desktop targets (reference system: 4 CPU / 4 GiB / iGPU)

| Metric | Goal | Status |
|---|---|---|
| Idle desktop RAM (full shell) | < 1 GiB, target ~700–800 MiB | not yet measurable (no desktop) |
| Core system RAM (kernel + services, no shell) | well below desktop budget | kernel today: ~1.2 MiB image + ~193 KiB runtime allocations (measured) |
| Idle CPU | near-zero sustained background activity | kernel idles in `hlt`, 100 timer IRQs/s (bring-up timer) |
| Rendering | stable 60 FPS on capable hardware; 16.67 ms frame budget | not yet measurable (no compositor) |
| Frame pacing | judged on frame time variance / 1% lows / missed frames, never average FPS alone | policy set now |
| Input latency | input never waits on animation | policy set now |

## Boot-time budgets

Measured separately per storage class once storage drivers exist
(HDD / SATA SSD / NVMe). No "one second boot" promises.

Current measurable slice (QEMU/OVMF, TCG, see docs/performance.md):
firmware+loader+kernel to `SYSTEM STATUS: RUNNING` ≈ 2.9–3.4 s wall
clock, of which ~2.5 s is OVMF firmware and deliberate 250 ms of
self-test waits; kernel-side init is ~350 ms (PIT-timed, dominated by
the timer verification window).

## Rules that keep budgets honest

1. Every optimization PR states its measured before/after.
2. No component may poll where it can block on an event.
3. The compositor (future) renders only on damage/animation/present —
   never a permanent frame loop.
4. Perceived speed counts, but is verified with frame/latency traces,
   not adjectives.
