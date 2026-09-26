#!/usr/bin/env python3
"""
SYPAS build tool: FAT16 filesystem image builder.

Creates the EFI System Partition image that carries the SYPAS bootloader
and kernel.  Written from the FAT specification; produces byte-for-byte
deterministic output (fixed timestamps and volume id) so SYPAS images are
reproducible.

Scope (build tool, not the SYPAS filesystem):
  - FAT16, 512-byte sectors, 1 sector/cluster
  - 8.3 names only (all SYPAS boot files fit 8.3 deliberately)
  - directories + files, no LFN, no deletion

Usage:
  mkfat.py OUTPUT SIZE_MIB SRC=/DST/PATH [SRC=/DST/PATH ...]
"""

import os
import struct
import sys
import time

SEC = 512
SPC = 1                 # sectors per cluster
RESERVED = 4
NFATS = 2
ROOT_ENTRIES = 512
ROOT_SECS = ROOT_ENTRIES * 32 // SEC

# Deterministic stamps via the reproducible-builds convention:
# honor SOURCE_DATE_EPOCH when set; otherwise fall back to a fixed,
# documented epoch (2025-09-25 12:00:00 UTC) so plain `make iso` is
# byte-reproducible without any environment setup.
# Same source + same toolchain + same SOURCE_DATE_EPOCH = same image.
_FALLBACK_EPOCH = 1758801600
_EPOCH = int(os.environ.get("SOURCE_DATE_EPOCH", _FALLBACK_EPOCH))
_TM = time.gmtime(_EPOCH)
DOS_DATE = ((max(_TM.tm_year, 1980) - 1980) << 9) | (_TM.tm_mon << 5) | _TM.tm_mday
DOS_TIME = (_TM.tm_hour << 11) | (_TM.tm_min << 5) | (_TM.tm_sec // 2)
VOLUME_ID = 0x53595041  # "SYPA"

ATTR_DIR = 0x10
ATTR_ARCHIVE = 0x20


def name83(name: str) -> bytes:
    name = name.upper()
    if "." in name and not name.startswith("."):
        base, ext = name.rsplit(".", 1)
    else:
        base, ext = name, ""
    if len(base) > 8 or len(ext) > 3:
        raise ValueError(f"{name!r} does not fit 8.3")
    return base.ljust(8).encode("ascii") + ext.ljust(3).encode("ascii")


def dirent(name83b: bytes, attr: int, cluster: int, size: int) -> bytes:
    return struct.pack(
        "<11sBBBHHHHHHHI",
        name83b, attr,
        0, 0,                    # NT reserved, ctime tenths
        DOS_TIME, DOS_DATE,      # create
        DOS_DATE,                # access
        0,                       # cluster high (FAT16: 0)
        DOS_TIME, DOS_DATE,      # modify
        cluster, size,
    )


class Node:
    def __init__(self, name, data=None):
        self.name = name
        self.data = data          # bytes for files, None for dirs
        self.children = {}        # dirs only
        self.cluster = 0
        self.nclusters = 0


def build(out_path: str, size_mib: int, mappings):
    total_secs = size_mib * 1024 * 1024 // SEC

    # FAT size: iterate until the FAT covers every data cluster.
    fat_secs = 1
    while True:
        data_secs = total_secs - RESERVED - NFATS * fat_secs - ROOT_SECS
        clusters = data_secs // SPC
        need = ((clusters + 2) * 2 + SEC - 1) // SEC
        if need <= fat_secs:
            break
        fat_secs = need
    if not (4085 <= clusters < 65525):
        raise SystemExit(f"cluster count {clusters} out of FAT16 range; "
                         f"adjust image size")
    data_start = RESERVED + NFATS * fat_secs + ROOT_SECS
    cluster_bytes = SPC * SEC

    # ---- Build the tree ------------------------------------------------
    root = Node("")
    for m in mappings:
        src, dst = m.split("=", 1)
        parts = [p for p in dst.strip("/").split("/") if p]
        node = root
        for d in parts[:-1]:
            node = node.children.setdefault(d, Node(d))
        with open(src, "rb") as f:
            node.children[parts[-1]] = Node(parts[-1], f.read())

    # ---- Assign clusters (files and non-root dirs) -----------------------
    next_free = [2]

    def alloc(n):
        start = next_free[0]
        next_free[0] += n
        return start if n else 0

    def assign(node, is_root):
        if node.data is None:
            if not is_root:
                blob = 32 * (2 + len(node.children))
                node.nclusters = max(1, -(-blob // cluster_bytes))
                node.cluster = alloc(node.nclusters)
            for c in node.children.values():
                assign(c, False)
        else:
            node.nclusters = -(-len(node.data) // cluster_bytes)
            node.cluster = alloc(node.nclusters) if node.nclusters else 0

    assign(root, True)
    if next_free[0] - 2 > clusters:
        raise SystemExit("image too small for contents")

    # ---- FAT ----------------------------------------------------------------
    fat = [0] * (clusters + 2)
    fat[0] = 0xFFF8
    fat[1] = 0xFFFF

    def chain(first, count):
        for i in range(count):
            fat[first + i] = first + i + 1 if i + 1 < count else 0xFFFF

    def chain_all(node, is_root):
        if not is_root and node.nclusters:
            chain(node.cluster, node.nclusters)
        if node.data is None:
            for c in node.children.values():
                chain_all(c, False)

    chain_all(root, True)

    # ---- Image assembly ---------------------------------------------------------
    img = bytearray(total_secs * SEC)

    bpb = struct.pack(
        "<3s8sHBHBHHBHHHIIBBBI11s8s",
        b"\xEB\x3C\x90", b"SYPAS1.0",
        SEC, SPC, RESERVED, NFATS, ROOT_ENTRIES,
        total_secs if total_secs < 0x10000 else 0,
        0xF8, fat_secs, 63, 255, 0,
        total_secs if total_secs >= 0x10000 else 0,
        0x80, 0, 0x29, VOLUME_ID,
        b"SYPAS EFI  ", b"FAT16   ",
    )
    img[0:len(bpb)] = bpb
    img[510:512] = b"\x55\xAA"

    fat_blob = b"".join(struct.pack("<H", v) for v in fat)
    for n in range(NFATS):
        off = (RESERVED + n * fat_secs) * SEC
        img[off:off + len(fat_blob)] = fat_blob

    def cluster_off(c):
        return (data_start + (c - 2) * SPC) * SEC

    def render(node, is_root, parent_cluster):
        entries = b""
        if not is_root:
            entries += dirent(b".          ", ATTR_DIR, node.cluster, 0)
            entries += dirent(b"..         ", ATTR_DIR, parent_cluster, 0)
        for c in node.children.values():
            attr = ATTR_DIR if c.data is None else ATTR_ARCHIVE
            size = 0 if c.data is None else len(c.data)
            entries += dirent(name83(c.name), attr, c.cluster, size)

        if is_root:
            off = (RESERVED + NFATS * fat_secs) * SEC
            if len(entries) > ROOT_ENTRIES * 32:
                raise SystemExit("root directory full")
        else:
            off = cluster_off(node.cluster)
        img[off:off + len(entries)] = entries

        for c in node.children.values():
            if c.data is None:
                render(c, False, 0 if is_root else node.cluster)
            elif c.data:
                img[cluster_off(c.cluster):cluster_off(c.cluster) + len(c.data)] = c.data

    render(root, True, 0)

    with open(out_path, "wb") as f:
        f.write(img)
    used = (next_free[0] - 2) * cluster_bytes
    print(f"mkfat: {out_path}: FAT16 {size_mib} MiB, {clusters} clusters, "
          f"{used // 1024} KiB used")


if __name__ == "__main__":
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    build(sys.argv[1], int(sys.argv[2]), sys.argv[3:])
