# SYPAS release artifacts

Built ISOs are **not tracked in Git**. Git holds source; binaries are
published as GitHub Release assets on tagged releases. `make iso`
writes the image here (`release/sypas-<version>.iso`), and the path is
gitignored.

## Reproducing a release image

SYPAS images are byte-reproducible:

```
same source
+ same toolchain (docs/toolchain.md)
+ same SOURCE_DATE_EPOCH (or the documented fallback)
= same image
```

`tools/mkfat.py` and `tools/mkiso.py` honor the reproducible-builds
`SOURCE_DATE_EPOCH` convention and fall back to a fixed epoch
(`1758801600` = 2025-09-25 12:00:00 UTC) when it is unset, so a plain
`make iso` is deterministic with no environment setup.

## Checksums

| Artifact | SHA-256 | Notes |
|---|---|---|
| sypas-0.1.0.iso | `3cb2a6d07fbc543f45b3dbed9f0aeb0b0cdcb352510863c2fdfeda2c95e00d61` | last ISO built from the pre-hardening tree (was tracked in Git before this file existed) |

Images built from the current tree differ from 0.1.0 (boot-hardening
changes); the 0.2.0 checksum is recorded when 0.2.0 is tagged and its
ISO is attached to the GitHub Release together with `SHA256SUMS`,
`toolchain.txt`, and the boot-test JSON for that build.
