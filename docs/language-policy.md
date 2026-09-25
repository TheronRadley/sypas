# SYPAS Language Policy

SYPAS deliberately uses multiple languages, each where it earns its
place. This is guidance, not ideology; deviations require a decision
record in `docs/technology-decisions.md`.

## Decision matrix

| Layer | Preferred language | Status today |
|---|---|---|
| Boot entry | ASM + C | in use (`start.S` + loader C) |
| Bootloader | C + ASM | in use (C17) |
| Kernel core | C | in use (C17) |
| Architecture code | C + ASM | in use (entry/ISR stubs in ASM) |
| Drivers | C / C++ | future |
| Hardware services | C / C++ | future |
| Kernel utilities | C | in use |
| Security-sensitive utilities | Rust where justified | future |
| Graphics | C / C++ / selected Rust | fbcon in C today |
| UI toolkit | C++ or Rust | future |
| Shell | C++ / Rust | future |
| System apps | C++ / Rust | future |
| Tooling | C++ / Rust / suitable high-level language | Python today (build-time only) |
| Tests/automation | suitable tooling language | Python + shell |

## C — primary systems language (C17)

Used for: kernel core, memory management, interrupts, timers, drivers,
boot/runtime infrastructure, and the UEFI loader. Kernel C is
freestanding: `-ffreestanding -nostdlib -mno-red-zone
-mgeneral-regs-only -fno-stack-protector`, no external runtime
dependencies, no floating point/SIMD in kernel paths.

## C++20 — secondary systems language

Not used yet. It enters with the first components where RAII/templates
provide real value (driver frameworks, graphics infrastructure, system
apps). Kernel/privileged C++ will run with `-fno-exceptions -fno-rtti`,
controlled allocation, and a documented runtime contract. C++ is never
chosen merely for comfort.

## x86_64 assembly — minimal and documented

Currently three files, each with ABI documentation in its header:

- `bootloader/uefi/start.S` — PE entry, MS→SysV bridging, self-reloc.
- `kernel/arch/x86_64/entry.S` — kernel entry state establishment.
- `kernel/arch/x86_64/isr.S` — interrupt/exception entry/exit,
  register save/restore contract with `interrupt_frame_t`.

Rule: assembly only where the CPU demands it (mode/privilege
transitions, interrupt frames, instructions C cannot express) or where a
measurement justifies an optimized path. No C rewritten in assembly on
vibes.

## Rust — targeted memory safety

Not in the tree yet, by decision (see DR-6): the current layers (boot
handoff, exception plumbing, a bitmap allocator) are dominated by
inherently-unsafe hardware pokes where Rust's benefit is thin. Rust
becomes the preferred choice starting with parsers and protocol
implementations (SypasFS on-disk structures, package metadata, network
protocols), where memory safety materially reduces risk.

## Higher-level languages

Python is used for **build-time tooling and tests only** (image
assembly, boot-test harness). Nothing in the OS itself depends on a
managed runtime, and the desktop will never require one. Application
languages beyond C/C++/Rust are a post-runtime/toolchain discussion.
