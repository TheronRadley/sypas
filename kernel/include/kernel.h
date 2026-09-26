/* SYPAS kernel — core interfaces shared across subsystems. */
#ifndef SYPAS_KERNEL_H
#define SYPAS_KERNEL_H

#include "types.h"
#include "../../bootloader/protocols/sypas_bootproto.h"

#define SYPAS_KERNEL_VERSION "0.1.0"

/* ---- Console output ---------------------------------------------------- */
/* kprintf fans out to every registered sink (serial always; framebuffer
 * console once initialized). Supported: %s %c %d %i %u %x %p %llx %llu %lld
 * with optional 0-padding and width (e.g. %08x). No floating point. */
void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void kvprintf(const char *fmt, va_list ap);

typedef void (*console_putc_fn)(char c);
void console_register(console_putc_fn fn);

/* ---- Panic -------------------------------------------------------------- */
void panic(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

/* Saved register state at interrupt/exception entry (pushed by isr.S). */
typedef struct interrupt_frame {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 vector, error_code;
    u64 rip, cs, rflags, rsp, ss;   /* pushed by the CPU */
} interrupt_frame_t;

void panic_with_frame(const char *reason, const interrupt_frame_t *f)
    __attribute__((noreturn));

/* ---- Serial (COM1) ------------------------------------------------------- */
bool serial_init(void);
void serial_putc(char c);

/* ---- Arch: descriptor tables, interrupts --------------------------------- */
void gdt_init(void);
void idt_init(void);
void irq_install(u8 irq, void (*handler)(interrupt_frame_t *));
void pic_init(void);
void pic_unmask(u8 irq);
void pic_send_eoi(u8 irq);
bool pic_handle_spurious(u8 irq);   /* true = spurious, fully handled  */
u64  pic_spurious_count(void);

/* ---- Timer ----------------------------------------------------------------- */
#define PIT_HZ 100
void pit_init(void);
u64  timer_ticks(void);          /* PIT ticks since pit_init            */
u64  timer_ms(void);             /* milliseconds since pit_init         */
void sleep_ticks(u64 ticks);     /* hlt-based wait, needs IF=1          */

/* ---- CPU --------------------------------------------------------------------- */
typedef struct cpu_info {
    char vendor[13];
    char brand[49];
    u32  family, model, stepping;
    bool has_sse2, has_apic, has_tsc, has_1gb_pages, has_nx;
} cpu_info_t;
void cpu_identify(cpu_info_t *info);
static inline u64 rdtsc(void)
{
    u32 lo, hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}

/* ---- Physical memory manager --------------------------------------------------- */
typedef struct pmm_stats {
    u64 total_pages;      /* pages tracked by the allocator            */
    u64 free_pages;
    u64 usable_bytes;     /* total SYPAS_MEM_USABLE bytes at boot      */
    u64 reserved_bytes;   /* everything else in the map                */
    u64 bitmap_bytes;
} pmm_stats_t;

void    pmm_init(const sypas_bootinfo_t *bi);
paddr_t pmm_alloc_page(void);          /* returns 0 on exhaustion */
void    pmm_free_page(paddr_t page);
void    pmm_get_stats(pmm_stats_t *out);
bool    pmm_selftest(void);

/* ---- Framebuffer console --------------------------------------------------------- */
bool fbcon_init(const sypas_bootinfo_t *bi);
void fbcon_putc(char c);
void fbcon_set_colors(u32 fg, u32 bg);

#endif
