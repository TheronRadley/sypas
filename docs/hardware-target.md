# SYPAS Hardware Targets

## Product target class

Older and lower-end x86_64 machines:

- 2–4 CPU cores
- 2–8 GiB RAM
- integrated graphics (Intel/AMD)
- SATA SSDs and HDDs
- UEFI firmware

Initial architecture: **x86_64**. Initial firmware: **UEFI** (no legacy
BIOS path planned for v1). Virtualization target: **QEMU q35 + OVMF**.

## Verified matrix

Hardware/VM configurations are listed as verified **only after an actual
test run** (automated via `make test-boot`, results in
docs/performance.md).

| Configuration | Firmware | Status |
|---|---|---|
| QEMU q35, TCG, 1 CPU, 1 GiB | OVMF (edk2-stable202411) | ✅ boot test PASS |
| QEMU q35, TCG, 2 CPU, 2 GiB (low-end profile) | OVMF | ✅ boot test PASS |
| QEMU q35, TCG, 4 CPU, 4 GiB (normal profile) | OVMF | ✅ boot test PASS |
| Any physical machine | — | ❌ NOT TESTED |

SMP note: multi-CPU configs currently verify that the kernel boots and
runs correctly on the bootstrap processor while additional CPUs remain
parked by firmware; SYPAS does not start secondary CPUs yet (Phase 8).

## Physical test matrix (planned, per project rules)

At minimum, before any hardware-support claim:

- one Intel machine, one AMD machine
- one older laptop
- one HDD system, one SATA SSD system
- one integrated Intel GPU, one integrated AMD GPU

Until then, every SYPAS document states: physical hardware NOT TESTED.
Universal hardware compatibility is not claimed and will never be
claimed without a matrix behind it.
