# SYPAS Kernel (v0.1.0)

Scope of this document: what exists today. No forward-written fiction —
future subsystems are listed only in the roadmap section of
docs/architecture.md.

## Entry contract

`kernel/arch/x86_64/entry.S` (`_kstart`) receives control from the
SYPAS bootloader per the boot protocol (RDI = bootinfo, long mode,
interrupts off, firmware identity paging). It establishes:

- DF clear, interrupts masked
- a kernel-owned 64 KiB `.bss` stack, 16-byte aligned
- zeroed RBP (unwind terminator)

then calls `kmain(const sypas_bootinfo_t *)` which never returns.

## Initialization order and why

1. **Serial (COM1)** — first, so every later failure is observable.
   Presence is probed (scratch register + loopback) so machines without
   a UART skip cleanly.
2. **Boot protocol validation** — magic, version, size. Any mismatch
   panics: booting with a misunderstood contract is worse than not
   booting.
3. **CPU identification** — CPUID vendor/brand/family and the feature
   bits the near-term roadmap needs (TSC, APIC, SSE2, NX, 1G pages).
4. **GDT** — ring-0 code/data, segments reloaded via `lretq`.
5. **IDT** — all 32 exception vectors + 16 IRQ vectors + vector 0x80
   (self-test gate, future syscall vector). Exceptions land in
   `panic_with_frame` with a complete register dump.
6. **PIC** — remapped to vectors 32–47, fully masked.
7. **Memory map + PMM** — see docs/memory.md; runs a mandatory
   self-test (uniqueness, pattern write, leak check) and measures
   alloc/free cycle cost.
8. **Framebuffer console** — if the boot protocol reports a usable
   linear framebuffer; otherwise serial-only (still a supported mode).
9. **Interrupt self-tests** — `int $0x80` dispatch counted, then PIT
   unmasked, `sti`, and a 250 ms window proves ≥25 real timer IRQs
   arrive. A kernel that cannot demonstrate interrupt delivery panics
   rather than printing "Interrupts: OK".
10. **Idle** — `hlt` loop; wakes only on interrupts; 10 s heartbeat to
    serial for long-run tests.

## Panic framework

`panic()` / `panic_with_frame()` print: reason, vector + error code,
RIP/CS/RFLAGS/RSP/SS, all GPRs, CR0/CR2/CR3/CR4, and an 8-qword raw
stack window — to both serial and (if up) the framebuffer console in
alert colors, then `cli; hlt` forever. `SYSTEM STATUS: HALTED` is
emitted for machine detection by the boot test harness.

## Interrupt entry ABI

`isr.S` ↔ `interrupt_frame_t` contract: stubs normalize the frame
(dummy error code where the CPU pushes none), save **all** GPRs in
declared order, call `isr_dispatch(frame)` SysV, restore, `iretq`.
Details in the file header.

## Deliberate v1 gaps

- No TSS/IST → a kernel-stack overflow in an exception path
  triple-faults instead of dumping. Fixed when the TSS lands (with
  ring transitions).
- NMI (vector 2) panics; correct masking/latching semantics arrive
  with the APIC work.
- `#PF` panics — there is no legitimate page fault yet since paging is
  the firmware's identity map.
- No SMP: secondary CPUs stay wherever firmware parked them.
- FPU/SIMD state untouched; kernel compiled `-mgeneral-regs-only` so it
  cannot corrupt user FP state that doesn't exist yet.
