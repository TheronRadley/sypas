/*
 * SYPAS kernel — framebuffer text console (Phase 23 seed).
 *
 * Direct linear-framebuffer text rendering on the GOP-provided mode.
 * Glyphs are the vendored public-domain 8x8 bitmap font scaled 2x
 * (16x16 cells) for readability on modern panels; scaling is integer
 * and branch-light.  This is bring-up infrastructure: the real SYPAS
 * graphics stack (surfaces/damage/compositor) replaces it in later
 * phases, but this console stays as the panic-of-last-resort output.
 *
 * Colors follow the first cut of the SYPAS visual identity:
 * deep blue-black background, warm off-white text.
 */

#include "../include/kernel.h"
#include "font8x8_basic.h"

#define GLYPH   8
#define SCALE   2
#define CELL_W  (GLYPH * SCALE)
#define CELL_H  (GLYPH * SCALE)
#define MARGIN  8

static volatile u32 *fb;
static u32 fb_width, fb_height, fb_pitch32;
static u32 fb_format;
static u32 cols, rows, cur_col, cur_row;
static u32 fg = 0x00E8E3D5;   /* warm off-white     */
static u32 bg = 0x000B0E14;   /* SYPAS deep blue-black */
static bool ready;

/* The boot protocol reports the memory byte order; SYPAS colors are
 * expressed as 0x00RRGGBB and swizzled once here if the framebuffer is
 * R,G,B,X in memory instead of B,G,R,X. */
static u32 to_native(u32 rgb)
{
    if (fb_format == SYPAS_FB_XBGR8888)
        return (rgb & 0x0000FF00) |
               ((rgb & 0x00FF0000) >> 16) |
               ((rgb & 0x000000FF) << 16);
    return rgb;
}

static void fill_rect(u32 x, u32 y, u32 w, u32 h, u32 native)
{
    for (u32 dy = 0; dy < h; dy++) {
        volatile u32 *line = fb + (u64)(y + dy) * fb_pitch32 + x;
        for (u32 dx = 0; dx < w; dx++)
            line[dx] = native;
    }
}

static void draw_glyph(u32 col, u32 row, char ch)
{
    const u8 *bits = (const u8 *)font8x8_basic[(u8)ch & 0x7F];
    u32 x0 = MARGIN + col * CELL_W;
    u32 y0 = MARGIN + row * CELL_H;
    u32 nfg = to_native(fg), nbg = to_native(bg);

    for (u32 gy = 0; gy < GLYPH; gy++) {
        u8 rowbits = bits[gy];
        for (u32 sy = 0; sy < SCALE; sy++) {
            volatile u32 *line = fb + (u64)(y0 + gy * SCALE + sy) * fb_pitch32 + x0;
            for (u32 gx = 0; gx < GLYPH; gx++) {
                u32 c = (rowbits >> gx) & 1 ? nfg : nbg;
                for (u32 sx = 0; sx < SCALE; sx++)
                    line[gx * SCALE + sx] = c;
            }
        }
    }
}

static void scroll(void)
{
    /* Move rows up one cell; bring-up console, so a straight copy is fine. */
    u64 row_px    = (u64)CELL_H * fb_pitch32;
    volatile u32 *dst = fb + (u64)MARGIN * fb_pitch32;
    volatile u32 *src = dst + row_px;
    u64 count = row_px * (rows - 1);
    for (u64 i = 0; i < count; i++)
        dst[i] = src[i];
    fill_rect(0, MARGIN + (rows - 1) * CELL_H, fb_width, CELL_H, to_native(bg));
}

bool fbcon_init(const sypas_bootinfo_t *bi)
{
    if (bi->fb_format != SYPAS_FB_XRGB8888 &&
        bi->fb_format != SYPAS_FB_XBGR8888)
        return false;
    if (!bi->fb_base || bi->fb_width < 320 || bi->fb_height < 200)
        return false;

    fb         = (volatile u32 *)bi->fb_base;
    fb_width   = bi->fb_width;
    fb_height  = bi->fb_height;
    fb_pitch32 = bi->fb_pitch / 4;
    fb_format  = bi->fb_format;

    cols = (fb_width  - 2 * MARGIN) / CELL_W;
    rows = (fb_height - 2 * MARGIN) / CELL_H;
    cur_col = cur_row = 0;

    fill_rect(0, 0, fb_width, fb_height, to_native(bg));
    ready = true;
    return true;
}

void fbcon_set_colors(u32 new_fg, u32 new_bg)
{
    fg = new_fg;
    bg = new_bg;
}

void fbcon_putc(char c)
{
    if (!ready)
        return;

    if (c == '\n') {
        cur_col = 0;
        cur_row++;
    } else if (c == '\r') {
        cur_col = 0;
    } else if (c >= 0x20) {
        draw_glyph(cur_col, cur_row, c);
        if (++cur_col >= cols) {
            cur_col = 0;
            cur_row++;
        }
    }

    if (cur_row >= rows) {
        scroll();
        cur_row = rows - 1;
    }
}
