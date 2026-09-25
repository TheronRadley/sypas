#!/usr/bin/env python3
"""
SYPAS build tool: bootable ISO builder.

Wraps the ESP image into an El Torito (EFI platform) ISO9660 image that
UEFI firmware boots directly.  Uses pycdlib (build-time tool only, see
docs/third-party-components.md).  The loader and kernel are also placed
in the ISO data area for inspection.

Usage: mkiso.py OUTPUT ESP_IMAGE [SRC=/ISO/PATH ...]
"""

import sys

import pycdlib


def main(out_path, esp_path, extra):
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=1, vol_ident="SYPAS")

    with open(esp_path, "rb") as f:
        esp = f.read()

    iso.add_fp(_mem(esp), len(esp), "/EFIBOOT.IMG;1")
    iso.add_eltorito(
        "/EFIBOOT.IMG;1",
        efi=True,
        media_name="noemul",
        boot_load_size=-(-len(esp) // 512),
    )

    for m in extra:
        src, dst = m.split("=", 1)
        with open(src, "rb") as f:
            data = f.read()
        iso.add_fp(_mem(data), len(data), dst)

    iso.write(out_path)
    iso.close()
    print(f"mkiso: wrote {out_path}")


def _mem(data):
    import io
    return io.BytesIO(data)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2], sys.argv[3:])
