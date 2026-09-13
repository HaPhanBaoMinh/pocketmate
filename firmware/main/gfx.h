/* Pure C 2D helpers for a 240x320 RGB565 LCD, rendered band by band.
 * No ESP-IDF dependencies so the same code runs in host tests. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GFX_WIDTH 240
#define GFX_HEIGHT 320

typedef struct {
    uint32_t codepoint;
    uint8_t advance, width, height;
    int8_t x_offset, y_offset; /* offsets from the pen position at the top-left of the line box */
    uint32_t bits_offset;
} gfx_glyph_t;
typedef struct {
    uint8_t line_height, ascent;
    uint16_t count;
    const gfx_glyph_t *glyphs;
    const uint8_t *bits;
} gfx_font_t;
extern const gfx_font_t font_small, font_medium, font_large;

/* A band is `rows` full-width scanlines starting at screen row y0.
 * Pixels are stored byte-swapped (big-endian RGB565) as the panel expects. */
typedef struct { uint16_t *pixels; int y0, rows; } gfx_band_t;

#define GFX_RGB(r, g, b) ((uint16_t)((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | ((b) >> 3)))
#define GFX_BLACK GFX_RGB(0, 0, 0)
#define GFX_WHITE GFX_RGB(255, 255, 255)

void gfx_fill(gfx_band_t *band, int x, int y, int w, int h, uint16_t color);
/* Returns the advance in pixels; draws nothing when band is NULL (measure only). */
int gfx_text(gfx_band_t *band, const gfx_font_t *font, int x, int y, uint16_t color, const char *utf8);
int gfx_text_width(const gfx_font_t *font, const char *utf8);
/* Greedy word wrap into at most `max_lines` lines; each entry is a byte range of `utf8`.
 * Returns the number of lines produced. Lines that do not fit are dropped. */
typedef struct { const char *start; size_t length; } gfx_line_t;
int gfx_wrap(const gfx_font_t *font, const char *utf8, int max_width, gfx_line_t *lines, int max_lines);
/* Decodes one UTF-8 code point; invalid bytes decode as U+FFFD and consume 1 byte. */
uint32_t gfx_utf8_next(const char **cursor);
