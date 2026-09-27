# SYPAS release artifacts

Built ISOs are **not tracked in Git**. Git holds source; `make iso` writes
`release/sypas-<version>.iso`, where `<version>` comes only from the repository
root `VERSION` file. This directory is intentionally empty in a fresh clone.

There is **no published tagged SYPAS release in this source tree yet**. A
`-dev` image is a development artifact, not a historical release and not a
substitute for a GitHub Release asset.

## Reproducing an image

SYPAS image tooling honors the reproducible-builds `SOURCE_DATE_EPOCH`
convention and falls back to a fixed epoch (`1758801600`, 2025-09-25
12:00:00 UTC) when it is unset. The release-grade claim is:

```text
same committed source
+ same toolchain
+ same SOURCE_DATE_EPOCH
+ different checkout directory
= identical image
```

`make test-repro` performs the distinct-checkout comparison from a committed
working tree. CI performs the equivalent check and records the SHA-256 only
when its workflow actually runs.

## Publishing a release

Follow `docs/release-process.md`: set the final `VERSION`, test, tag the exact
source revision, and attach the ISO with `SHA256SUMS`, toolchain report, and
boot-test evidence. Do not add the ISO itself to Git.
