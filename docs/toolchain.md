# SYPAS Toolchain

## Reference development environment

These are the versions used for the BOOT-2 reference measurements. They are a
reference environment, not a claim that every CI run uses these exact package
builds.

| Tool | Reference version | Role |
|---|---:|---|
| gcc | 12.2 (Debian 12.2.0-14) | C compiler and assembler driver |
| GNU binutils | 2.40 | link, inspect and convert ELF→PE32+ |
| GNU make | 4.3 | build orchestration |
| Python | 3.11 | image tools and tests |
| QEMU | 9.2.4 | reference q35/OVMF test platform |
| OVMF / edk2 | edk2-stable202411 | UEFI firmware for the reference platform |
| pycdlib | 1.21.0 | ISO assembly (pinned in requirements) |
| Unicorn | 2.1.4 | BIOS diagnostic-stub test (pinned in requirements) |

Install Python test/build dependencies with:

```text
pip install -r tools/requirements-dev.txt
```

The loader and kernel use host GCC in freestanding mode, so native x86_64 is a
real prerequisite. `make doctor` marks another host architecture as a core
blocker rather than pretending the native build will work. A dedicated
cross-toolchain is a future MEM/ABI-era portability project.

## CI environment (intentionally distinct)

CI runs on `ubuntu-24.04` and installs `gcc`, `make`, `binutils`,
`qemu-system-x86`, and `ovmf` from Ubuntu APT at workflow time. Those APT
versions are **not pinned** to the reference versions above; the workflow
records their actual versions in `ci-artifacts/toolchain.txt`. Pinned Python
packages are shared between reference and CI environments.

This distinction is deliberate and honest: CI is compatibility coverage, not
yet a hermetic reproducible container. A version-pinned container/dev image is
the future release-grade replacement.

## Bootloader build pipeline

UEFI applications are PE32+ with MS-ABI entry points. SYPAS builds one without
mingw or gnu-efi:

1. compile PIC freestanding C with `-fshort-wchar -mno-red-zone`; EFI function
   pointers are declared `__attribute__((ms_abi))`;
2. link a base-0 ELF shared object (`-shared -Bsymbolic`);
3. convert it with `objcopy --target efi-app-x86_64`;
4. self-apply only bounded `R_X86_64_RELATIVE` relocations before C state is
   used, failing closed on malformed metadata or any other relocation type;
5. cross the firmware→C boundary with caller-allocated Microsoft x64 shadow
   space and 16-byte stack alignment.

`efi.h` implements the UEFI 2.10 x86_64 subset SYPAS uses and carries
compile-time assertions for table, protocol, and required-function offsets.

## Kernel build flags

The kernel is freestanding, non-PIE, linked at physical 4 MiB, and compiled
with `-mgeneral-regs-only` until FPU context management exists. Debug-info
paths use `-ffile-prefix-map` / `-fdebug-prefix-map` so checkout paths do not
change the runtime ELF payload.

## Test platform and discovery

The QEMU machine matrix is only `tests/config/matrix.json`; tests, run scripts
and docs must not duplicate its CPU/RAM values. `tools/resolve_paths.py` is the
single QEMU/OVMF discovery policy used by Make, `make doctor`, and
`scripts/run-qemu.sh`. Explicit `QEMU`, `OVMF_CODE`, and `OVMF_VARS` values
always override it.

## Reproducibility

`tools/mkfat.py` and `tools/mkiso.py` honor `SOURCE_DATE_EPOCH`, falling back
to the documented fixed epoch. The release-grade target is:

```text
same committed source + same toolchain + same SOURCE_DATE_EPOCH
+ different checkout directory = identical image
```

`make test-repro` and CI use a separate checkout to exercise that claim. The
claim applies only when the test actually passes for the revision; it is not a
substitute for a visible CI result.

## CI configured work

`.github/workflows/ci.yml` is configured to report tool versions, run doctor,
build with `-Werror`, run host unit/media tests, compare a separate-checkout
build, boot the UEFI matrix, run fault injection, and upload test artifacts.
Until GitHub exposes a successful run for a commit, describe this as
“CI configured”, not “CI passed”.

## Not yet in place

- version-pinned container/dev image;
- static-analysis policy (cppcheck/clang-tidy); and
- documented GDB workflow (planned as part of MEM-1).
