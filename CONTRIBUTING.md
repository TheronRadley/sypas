# Contributing to SYPAS

SYPAS accepts focused contributions that preserve its explicit boot and kernel
contracts. Before opening a pull request:

1. Run `make WERROR=1 test` (or explain any QEMU/OVMF tier skipped by your
   environment; `make doctor` reports that state).
2. Add or update host tests for parser, ABI, or arithmetic changes.
3. Update the normative document when a boot-protocol, memory-ownership, or
   build/release contract changes.
4. Keep generated images and `build/` artifacts out of Git.
5. Do not introduce a new runtime dependency without recording it in
   `docs/third-party-components.md` and explaining its ownership boundary.

For architectural work, start a discussion before writing a large subsystem.
The project deliberately builds the protected-kernel foundations in dependency
order rather than accepting premature drivers, desktop work, or syscall ABI
surface.
