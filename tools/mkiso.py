#!/usr/bin/env python3
"""
SYPAS build tool: deterministic dual-entry bootable ISO builder.

The El Torito catalog deliberately makes the legacy BIOS path the default
(initial) entry.  It loads a tiny 16-bit diagnostic image, while the EFI
section entry points at the FAT16 ESP image used by the real UEFI loader.
This does not make SYPAS BIOS-capable: the BIOS image only explains that UEFI
is required and halts.

The ISO data area also contains the boot images as ordinary files, plus any
inspection files supplied on the command line.

Usage: mkiso.py OUTPUT ESP_IMAGE BIOS_STUB [SRC=/ISO/PATH ...]
"""

import io
import sys
import time

# Deterministic output: pycdlib stamps volume/directory dates with "now".
# Freeze the clock (2025-09-25 12:00:00 UTC) so rebuilding identical inputs
# yields a byte-identical ISO.
_FIXED_TIME = 1758801600.0
time.time = lambda: _FIXED_TIME

import pycdlib

ISO_BLOCK = 2048
VIRTUAL_SECTOR = 512


def _mem(data):
    return io.BytesIO(data)


def _read(path):
    with open(path, "rb") as f:
        return f.read()


def _bios_image(data):
    """Return a whole ISO block for the BIOS no-emulation payload.

    The Makefile's linker rule already emits exactly 2048 bytes.  Keeping the
    padding here as well makes this tool's image contract explicit and avoids
    a BIOS loader reading the next ISO extent if it is invoked by hand with a
    shorter diagnostic binary.
    """
    if len(data) > ISO_BLOCK:
        raise ValueError("BIOS diagnostic image must fit in one 2048-byte block")
    return data.ljust(ISO_BLOCK, b"\x00")


def main(out_path, esp_path, bios_path, extra):
    bios = _bios_image(_read(bios_path))
    esp = _read(esp_path)
    if not esp or len(esp) % VIRTUAL_SECTOR:
        raise ValueError("ESP image must be a non-empty multiple of 512 bytes")

    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=1, vol_ident="SYPAS")

    # Add both boot images before creating the catalog.  Their directory
    # records are then available for both the catalog entries and inspection.
    iso.add_fp(_mem(bios), len(bios), "/BIOSBOOT.BIN;1")
    iso.add_fp(_mem(esp), len(esp), "/EFIBOOT.IMG;1")

    # El Torito initial/default entry: x86 BIOS, no emulation.  platform_id=0
    # is recorded in the validation entry and makes this the BIOS default.
    iso.add_eltorito(
        "/BIOSBOOT.BIN;1",
        platform_id=0x00,
        media_name="noemul",
        boot_load_size=len(bios) // VIRTUAL_SECTOR,
        boot_load_seg=0,
        bootable=True,
    )

    # El Torito section entry: UEFI platform, no emulation, FAT16 ESP image.
    # pycdlib emits the required 0x91 final section header and 0xef platform
    # identifier when efi=True.
    iso.add_eltorito(
        "/EFIBOOT.IMG;1",
        efi=True,
        media_name="noemul",
        boot_load_size=len(esp) // VIRTUAL_SECTOR,
        bootable=True,
    )

    for mapping in extra:
        src, dst = mapping.split("=", 1)
        data = _read(src)
        iso.add_fp(_mem(data), len(data), dst)

    iso.write(out_path)
    iso.close()
    print(f"mkiso: wrote {out_path} (BIOS default + EFI section entry)")


if __name__ == "__main__":
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:])
