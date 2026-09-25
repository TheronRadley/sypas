/*
 * SYPAS kernel — minimal formatted output.
 *
 * Fans out to registered console sinks (serial first, framebuffer console
 * once graphics are up).  Formatting is deliberately small: no floating
 * point, no locale, no allocation.  Supported conversions:
 *   %s %c %d %i %u %x %X %p %%  with optional 'l'/'ll' length and
 *   zero-padding + field width (e.g. %08x, %16llx).
 */

#include "../include/kernel.h"

#define MAX_SINKS 4

static console_putc_fn sinks[MAX_SINKS];
static int nsinks;

void console_register(console_putc_fn fn)
{
    if (nsinks < MAX_SINKS)
        sinks[nsinks++] = fn;
}

static void emit(char c)
{
    for (int i = 0; i < nsinks; i++)
        sinks[i](c);
}

static void emit_str(const char *s)
{
    while (*s)
        emit(*s++);
}

static void emit_uint(u64 v, unsigned base, bool upper, int width, bool zero)
{
    static const char *ld = "0123456789abcdef";
    static const char *ud = "0123456789ABCDEF";
    const char *dig = upper ? ud : ld;
    char buf[24];
    int n = 0;

    do {
        buf[n++] = dig[v % base];
        v /= base;
    } while (v);

    for (int pad = width - n; pad > 0; pad--)
        emit(zero ? '0' : ' ');
    while (n--)
        emit(buf[n]);
}

void kvprintf(const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            emit(*fmt);
            continue;
        }
        fmt++;

        bool zero = false;
        int width = 0, longs = 0;

        if (*fmt == '0') { zero = true; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        while (*fmt == 'l') { longs++; fmt++; }

        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            emit_str(s ? s : "(null)");
            break;
        }
        case 'c':
            emit((char)va_arg(ap, int));
            break;
        case 'd': case 'i': {
            i64 v = longs ? va_arg(ap, i64) : va_arg(ap, i32);
            if (v < 0) { emit('-'); v = -v; }
            emit_uint((u64)v, 10, false, width, zero);
            break;
        }
        case 'u': {
            u64 v = longs ? va_arg(ap, u64) : va_arg(ap, u32);
            emit_uint(v, 10, false, width, zero);
            break;
        }
        case 'x': case 'X': {
            u64 v = longs ? va_arg(ap, u64) : va_arg(ap, u32);
            emit_uint(v, 16, *fmt == 'X', width, zero);
            break;
        }
        case 'p':
            emit_str("0x");
            emit_uint((u64)va_arg(ap, void *), 16, false, 16, true);
            break;
        case '%':
            emit('%');
            break;
        default:
            emit('%');
            emit(*fmt);
            break;
        }
    }
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
}
