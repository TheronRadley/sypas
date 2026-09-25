#!/usr/bin/env python3
"""Independent structural and content validation for the SYPAS boot ISO.

This parser intentionally does not import pycdlib.  It checks the handful of
ISO9660 and El Torito records SYPAS relies on, then walks the embedded FAT16
ESP directly from its BPB/FAT/directory chains.
"""

import argparse
import struct
from pathlib import Path

ISO_SECTOR = 2048
ELTORITO_SECTOR = 512


def u16(blob, off):
    return struct.unpack_from("<H", blob, off)[0]


def u32(blob, off):
    return struct.unpack_from("<I", blob, off)[0]


def expect(condition, message):
    if not condition:
        raise AssertionError(message)


def iso_payload(image, entry, label):
    expect(entry[0] == 0x88, f"{label}: entry is not bootable")
    expect(entry[1] == 0x00, f"{label}: media is not no-emulation")
    count = u16(entry, 6)
    lba = u32(entry, 8)
    expect(count > 0, f"{label}: empty payload")
    start = lba * ISO_SECTOR
    end = start + count * ELTORITO_SECTOR
    expect(end <= len(image), f"{label}: payload extends past ISO")
    return image[start:end], lba, count


def parse_eltorito(image):
    expect(len(image) % ISO_SECTOR == 0, "ISO is not 2048-byte aligned")
    expect(len(image) >= 18 * ISO_SECTOR, "ISO is too short for the volume descriptors")

    pvd = image[16 * ISO_SECTOR:17 * ISO_SECTOR]
    expect(pvd[0] == 1 and pvd[1:6] == b"CD001" and pvd[6] == 1,
           "missing ISO9660 primary volume descriptor at sector 16")

    brvd = image[17 * ISO_SECTOR:18 * ISO_SECTOR]
    expect(brvd[0] == 0 and brvd[1:6] == b"CD001" and brvd[6] == 1,
           "missing El Torito boot record volume descriptor at sector 17")
    expect(brvd[7:39].rstrip(b"\x00 ") == b"EL TORITO SPECIFICATION",
           "boot record has the wrong El Torito system identifier")
    catalog_lba = u32(brvd, 71)
    cat_start = catalog_lba * ISO_SECTOR
    expect(cat_start + ISO_SECTOR <= len(image), "boot catalog is outside ISO")
    catalog = image[cat_start:cat_start + ISO_SECTOR]

    validation = catalog[:32]
    expect(validation[0] == 1, "catalog validation header ID is not 1")
    expect(validation[1] == 0x00, "catalog default platform is not BIOS/x86")
    expect(validation[30:32] == b"\x55\xaa", "catalog validation key bytes are wrong")
    words = struct.unpack("<16H", validation)
    expect(sum(words) % 0x10000 == 0, "catalog validation checksum is wrong")

    default = catalog[32:64]
    bios_payload, bios_lba, bios_count = iso_payload(image, default, "BIOS default")
    expect(u16(default, 2) == 0, "BIOS default does not use the traditional load segment")

    section_header = catalog[64:96]
    expect(section_header[0] in (0x90, 0x91), "missing El Torito section header")
    expect(section_header[1] == 0xEF, "section header is not the EFI platform")
    expect(u16(section_header, 2) == 1, "EFI section does not contain exactly one entry")
    expect(section_header[0] == 0x91, "EFI section is not marked final")

    efi_entry = catalog[96:128]
    esp_payload, esp_lba, esp_count = iso_payload(image, efi_entry, "EFI section")
    return {
        "catalog_lba": catalog_lba,
        "bios": (bios_payload, bios_lba, bios_count),
        "esp": (esp_payload, esp_lba, esp_count),
    }


def short_name(raw):
    name = raw[:8].decode("ascii").rstrip()
    ext = raw[8:11].decode("ascii").rstrip()
    return f"{name}.{ext}" if ext else name


def fat16_files(image):
    """Walk every FAT16 directory chain and return files keyed by /PATH."""
    expect(len(image) >= 512, "embedded ESP is too short")
    expect(image[510:512] == b"\x55\xaa", "ESP boot sector signature is wrong")

    bytes_per_sector = u16(image, 11)
    sectors_per_cluster = image[13]
    reserved = u16(image, 14)
    fat_count = image[16]
    root_entries = u16(image, 17)
    total16 = u16(image, 19)
    fat_sectors = u16(image, 22)
    total_sectors = total16 or u32(image, 32)

    expect(bytes_per_sector == 512, "ESP is not a 512-byte-sector FAT volume")
    expect(sectors_per_cluster == 1, "unexpected FAT16 cluster size")
    expect(fat_count == 2, "unexpected FAT copy count")
    expect(image[54:62] == b"FAT16   ", "ESP is not labelled FAT16")
    expect(total_sectors * bytes_per_sector <= len(image), "ESP BPB exceeds image")

    fat_start = reserved * bytes_per_sector
    root_start = (reserved + fat_count * fat_sectors) * bytes_per_sector
    root_bytes = root_entries * 32
    data_start = root_start + root_bytes
    cluster_bytes = sectors_per_cluster * bytes_per_sector
    fat_end = fat_start + fat_sectors * bytes_per_sector
    expect(fat_end <= len(image) and data_start <= len(image), "invalid FAT16 layout")
    fat = image[fat_start:fat_end]

    def next_cluster(cluster):
        off = cluster * 2
        expect(off + 2 <= len(fat), f"FAT entry {cluster} is outside the FAT")
        return u16(fat, off)

    def chain_bytes(first, size=None):
        if size == 0:
            return b""
        expect(first >= 2, "non-empty file or directory has no starting cluster")
        out = bytearray()
        cluster = first
        seen = set()
        while cluster < 0xFFF8:
            expect(cluster >= 2 and cluster not in seen, "invalid or looping FAT chain")
            seen.add(cluster)
            off = data_start + (cluster - 2) * cluster_bytes
            expect(off + cluster_bytes <= len(image), "FAT chain points outside ESP")
            out.extend(image[off:off + cluster_bytes])
            cluster = next_cluster(cluster)
        if size is not None:
            expect(len(out) >= size, "FAT chain is shorter than directory/file size")
            return bytes(out[:size])
        return bytes(out)

    files = {}

    def read_entries(blob, prefix):
        for off in range(0, len(blob), 32):
            ent = blob[off:off + 32]
            if len(ent) < 32 or ent[0] == 0x00:
                break
            if ent[0] == 0xE5 or ent[11] == 0x0F:
                continue
            name = short_name(ent[:11])
            if name in (".", ".."):
                continue
            path = f"{prefix}/{name}"
            attr = ent[11]
            cluster = u16(ent, 26)
            size = u32(ent, 28)
            if attr & 0x10:
                directory = chain_bytes(cluster)
                read_entries(directory, path)
            else:
                files[path] = chain_bytes(cluster, size)

    read_entries(image[root_start:root_start + root_bytes], "")
    return files


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", required=True)
    ap.add_argument("--bios", required=True)
    ap.add_argument("--esp", required=True)
    ap.add_argument("--bootloader", required=True)
    ap.add_argument("--kernel", required=True)
    args = ap.parse_args()

    image = Path(args.iso).read_bytes()
    expected_bios = Path(args.bios).read_bytes()
    expected_esp = Path(args.esp).read_bytes()
    entries = parse_eltorito(image)

    bios, bios_lba, bios_count = entries["bios"]
    esp, esp_lba, esp_count = entries["esp"]
    expect(bios == expected_bios, "BIOS catalog payload differs from build/biosstub.bin")
    expect(esp == expected_esp, "EFI catalog payload differs from build/esp.img")
    expect(len(bios) == len(expected_bios), "BIOS payload has unexpected padding")
    expect(len(esp) == len(expected_esp), "EFI payload has unexpected padding")

    files = fat16_files(esp)
    expected_loader = Path(args.bootloader).read_bytes()
    expected_kernel = Path(args.kernel).read_bytes()
    expect(files.get("/EFI/BOOT/BOOTX64.EFI") == expected_loader,
           "FAT16 /EFI/BOOT/BOOTX64.EFI differs from build/BOOTX64.EFI")
    expect(files.get("/SYPAS/KERNEL.ELF") == expected_kernel,
           "FAT16 /SYPAS/KERNEL.ELF differs from build/kernel.elf")

    print("validate_media: PASS")
    print(f"  catalog LBA {entries['catalog_lba']}; BIOS LBA {bios_lba} ({bios_count} x 512); "
          f"EFI LBA {esp_lba} ({esp_count} x 512)")
    print("  El Torito: BIOS default + EFI 0xEF section, both bootable/no-emulation")
    print("  FAT16: /EFI/BOOT/BOOTX64.EFI and /SYPAS/KERNEL.ELF match build outputs")


if __name__ == "__main__":
    main()
