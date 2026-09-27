# Release process

`VERSION` at the repository root is the sole version source. `make` generates
a small C header from it so the ISO name, loader banner, kernel banner, and
legacy-BIOS diagnostic agree. A development tree uses a `-dev` version and is
not a release.

## Before tagging a release

1. Set `VERSION` to the final release number and review the generated version
   in a clean build.
2. Run `make WERROR=1 test` in the reference environment.
3. Run the cross-checkout determinism check used by CI.
4. Save the exact toolchain report, test JSON, serial transcripts, and
   `sha256sum release/sypas-<version>.iso`.
5. Tag the matching source revision as `v<version>` and attach the ISO,
   `SHA256SUMS`, toolchain report, and test evidence to the GitHub Release.

Until those steps are complete, documentation must say an image *will be
published*, not imply that a GitHub Release exists.
