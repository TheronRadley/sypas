/*
 * SYPAS kernel — initialization.
 *
 * Phase 2 milestone: every status line printed below corresponds to a
 * subsystem that really initialized (or a check that really ran).  If a
 * mandatory step fails the kernel panics instead of pretending.
 *
 * Bring-up order:
 *   serial -> boot protocol validation -> CPU ident -> GDT -> IDT/PIC
 *   -> physical memory -> framebuffer console -> timer -> interrupt
 *   self-test -> idle.
 */

#include "include/kernel.h"

extern volatile u64 isr_test_hits;   /* idt.c, bumped by int $0x80 */

static const char *memtype_name(u32 t)
{
    switch (t) {
    case SYPAS_MEM_USABLE:       return "usable";
    case SYPAS_MEM_RESERVED:     return "reserved";
    case SYPAS_MEM_ACPI_RECLAIM: return "acpi-reclaim";
    case SYPAS_MEM_ACPI_NVS:     return "acpi-nvs";
    case SYPAS_MEM_MMIO:         return "mmio";
    case SYPAS_MEM_LOADER:       return "loader";
    case SYPAS_MEM_KERNEL:       return "kernel";
    case SYPAS_MEM_FIRMWARE:     return "firmware";
    default:                     return "?";
    }
}

void kmain(const sypas_bootinfo_t *bi)
{
    bool serial_ok = serial_init();
    console_register(serial_putc);

    kprintf("\nSYPAS KERNEL %s\n", SYPAS_KERNEL_VERSION);
    kprintf("============\n\nBooting SYPAS...\n\n");
    if (serial_ok)
        kprintf("[uart] COM1 115200 8N1, loopback self-test passed\n");

    /* --- Boot protocol ------------------------------------------------ */
    if (!bi)
        panic("boot: NULL bootinfo");
    if (bi->magic != SYPAS_BOOT_MAGIC)
        panic("boot: bad magic %lx", bi->magic);
    if (bi->version != SYPAS_BOOT_VERSION)
        panic("boot: unsupported protocol version %u", bi->version);
    if (bi->size < sizeof(*bi))
        panic("boot: short bootinfo (%u bytes)", bi->size);
    kprintf("[boot] SYPAS boot protocol v%u, bootinfo at %p\n",
            bi->version, (const void *)bi);
    kprintf("[boot] kernel image: base=%lx size=%lu KiB, stack=%lx\n",
            bi->kernel_phys_base, bi->kernel_size / 1024, bi->stack_base);
    if (bi->cmdline[0])
        kprintf("[boot] cmdline: %s\n", bi->cmdline);

    /* --- CPU ----------------------------------------------------------- */
    cpu_info_t cpu;
    cpu_identify(&cpu);
    kprintf("[cpu ] %s (%s) family %u model %u stepping %u\n",
            cpu.brand[0] ? cpu.brand : "unknown brand",
            cpu.vendor, cpu.family, cpu.model, cpu.stepping);
    kprintf("[cpu ] features: tsc=%d apic=%d sse2=%d nx=%d 1g-pages=%d\n",
            cpu.has_tsc, cpu.has_apic, cpu.has_sse2, cpu.has_nx,
            cpu.has_1gb_pages);

    /* --- Descriptor tables & interrupts controllers --------------------- */
    gdt_init();
    kprintf("[gdt ] kernel GDT loaded (ring0 code/data), segments reloaded\n");

    idt_init();
    kprintf("[idt ] 256-entry IDT: 32 exceptions, 16 IRQs, 1 test gate\n");

    pic_init();
    kprintf("[pic ] 8259 remapped to vectors 32-47, all IRQs masked\n");

    /* --- Memory ---------------------------------------------------------- */
    const sypas_memmap_entry_t *map = (const void *)bi->memmap;
    kprintf("[mem ] boot memory map: %lu entries\n", bi->memmap_count);
    u64 usable = 0;
    for (u64 i = 0; i < bi->memmap_count; i++) {
        if (map[i].type == SYPAS_MEM_USABLE)
            usable += map[i].length;
        /* full map to serial only once fbcon exists this would be noise */
        kprintf("[mem ]   %016lx +%12lx %s\n",
                map[i].base, map[i].length, memtype_name(map[i].type));
    }
    kprintf("[mem ] usable RAM: %lu MiB\n", usable / (1024 * 1024));

    pmm_init(bi);
    pmm_stats_t st;
    pmm_get_stats(&st);
    kprintf("[pmm ] bitmap allocator: %lu pages tracked, %lu free, "
            "bitmap %lu KiB\n",
            st.total_pages, st.free_pages, st.bitmap_bytes / 1024);
    if (!pmm_selftest())
        panic("pmm: self-test failed");

    /* --- Framebuffer console ------------------------------------------------ */
    bool fb_ok = fbcon_init(bi);
    if (fb_ok) {
        console_register(fbcon_putc);
        kprintf("[fb  ] %ux%u, pitch %u, %s, console on framebuffer + serial\n",
                bi->fb_width, bi->fb_height, bi->fb_pitch,
                bi->fb_format == SYPAS_FB_XRGB8888 ? "xrgb8888" : "xbgr8888");
    } else {
        kprintf("[fb  ] no usable linear framebuffer, serial console only\n");
    }
    kprintf("[acpi] RSDP %s at %lx\n",
            bi->acpi_rsdp ? "found" : "NOT found", bi->acpi_rsdp);

    /* --- Interrupt self-tests -------------------------------------------------- */
    u64 hits_before = isr_test_hits;
    __asm__ volatile("int $0x80");
    __asm__ volatile("int $0x80");
    if (isr_test_hits != hits_before + 2)
        panic("idt: software interrupt dispatch failed (%lu hits)",
              isr_test_hits);
    kprintf("[int ] software interrupt dispatch verified (int 0x80 x2)\n");

    pit_init();
    __asm__ volatile("sti");
    u64 t0 = timer_ticks();
    sleep_ticks(25);                       /* 250 ms worth of real IRQs */
    u64 delta = timer_ticks() - t0;
    if (delta < 25)
        panic("pit: timer not ticking (delta=%lu)", delta);
    kprintf("[pit ] 100 Hz timer alive: %lu IRQs in 250 ms window\n", delta);

    /* --- Milestone banner --------------------------------------------------------- */
    kprintf("\nSYPAS\n=====\n\n");
    kprintf("Bootloader: OK   (SYPAS UEFI loader, boot protocol v1)\n");
    kprintf("CPU:        OK   (%s)\n", cpu.vendor);
    kprintf("Memory:     OK   (%lu MiB usable, pmm self-test passed)\n",
            usable / (1024 * 1024));
    kprintf("Interrupts: OK   (exceptions armed, PIT verified)\n");
    kprintf("Framebuffer:%s\n",
            fb_ok ? " OK  (GOP linear framebuffer)" : " ABSENT (serial only)");
    kprintf("\nSYPAS kernel initialized in %lu ms (kernel-side, PIT-timed).\n",
            timer_ms());
    kprintf("\nSYSTEM STATUS: RUNNING\n");

    /* --- Idle: event-driven from day one — hlt until an interrupt.
     * Heartbeat every 10 s to serial to prove liveness in long tests. */
    u64 last_beat = timer_ticks();
    for (;;) {
        __asm__ volatile("hlt");
        u64 now = timer_ticks();
        if (now - last_beat >= 10 * PIT_HZ) {
            last_beat = now;
            kprintf("[idle] uptime %lu s, free pages %lu\n",
                    now / PIT_HZ, ({ pmm_stats_t s; pmm_get_stats(&s); s.free_pages; }));
        }
    }
}
