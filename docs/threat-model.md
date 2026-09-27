# Boot and early-kernel threat model

SYPAS is not yet a complete security boundary. This document records what the
BOOT-2 hardening work does and does not defend.

## Inputs treated as untrusted or fallible

- `KERNEL.ELF` bytes from the boot medium;
- UEFI memory-map descriptors and their arithmetic; and
- loader dynamic relocation metadata, despite being built by SYPAS.

The loader bounds the kernel file before allocation, validates the ELF image
before loading it, requires identity mapping and non-W+X PT_LOAD segments,
and passes exact loaded ranges into the ownership map. The map translator
requires sorted, page-aligned, non-overlapping, overflow-free descriptors.
The relocator bounds every table and relocation target to its own loaded
image and fails closed.

## Inputs trusted by platform contract for now

The loader dereferences UEFI configuration-table pointers, including the
RSDP pointer. It validates the RSDP signature, revision, length and checksums,
but cannot safely probe an arbitrary physical address before it owns page
tables. Malicious firmware remains outside the current threat model.

## Not provided yet

- Secure Boot policy or measured boot;
- disk/kernel signatures;
- kernel virtual-memory isolation or W^X page permissions;
- user/kernel privilege separation; and
- DMA/IOMMU isolation.

MEM-1 begins the transition from validated boot input to protected virtual
memory. Secure/measured boot policy will be designed separately rather than
bolted onto the loader during bring-up.
