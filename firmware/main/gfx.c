#include "gfx.h"
#include <string.h>

static inline uint16_t swap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

uint32_t gfx_utf8_next(const char **cursor)
{
    const unsigned char *p = (const unsigned char *)*cursor;
    uint32_t cp;
    int extra;
    if (p[0] < 0x80) { cp = p[0]; extra = 0; }
    else if ((p[0] & 0xe0) == 0xc0) { cp = p[0] & 0x1f; extra = 1; }
    else if ((p[0] & 0xf0) == 0xe0) { cp = p[0] & 0x0f; extra = 2; }
    else if ((p[0] & 0xf8) == 0xf0) { cp = p[0] & 0x07; extra = 3; }
    else { *cursor += 1; return 0xfffd; }
    for (int i = 1; i <= extra; i++) {
        if ((p[i] & 0xc0) != 0x80) { *cursor += 1; return 0xfffd; }
        cp = (cp << 6) | (p[i] & 0x3f);
    }
    *cursor += extra + 1;
    return cp;
}

static const gfx_glyph_t *find_glyph(const gfx_font_t *font, uint32_t cp)
{
    size_t lo = 0, hi = font->count;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (font->glyphs[mid].codepoint < cp) lo = mid + 1;
        else hi = mid;
    }
    if (lo < font->count && font->glyphs[lo].codepoint == cp) return &font->glyphs[lo];
    return cp == '?' ? NULL : find_glyph(font, '?');
}

void gfx_fill(gfx_band_t *band, int x, int y, int w, int h, uint16_t color)
{
    if (!band) return;
    int x0 = x < 0 ? 0 : x, x1 = x + w > GFX_WIDTH ? GFX_WIDTH : x + w;
    int y0 = y < band->y0 ? band->y0 : y;
    int y1 = y + h > band->y0 + band->rows ? band->y0 + band->rows : y + h;
    uint16_t c = swap16(color);
    for (int row = y0; row < y1; row++) {
        uint16_t *line = band->pixels + (row - band->y0) * GFX_WIDTH;
        for (int col = x0; col < x1; col++) line[col] = c;
    }
}

static void draw_glyph(gfx_band_t *band, const gfx_font_t *font, const gfx_glyph_t *g, int x, int y, uint16_t c)
{
    int stride = (g->width + 7) / 8;
    for (int gy = 0; gy < g->height; gy++) {
        int row = y + g->y_offset + gy;
        if (row < band->y0 || row >= band->y0 + band->rows) continue;
        const uint8_t *bits = font->bits + g->bits_offset + gy * stride;
        uint16_t *line = band->pixels + (row - band->y0) * GFX_WIDTH;
        for (int gx = 0; gx < g->width; gx++) {
            int col = x + g->x_offset + gx;
            if (col < 0 || col >= GFX_WIDTH) continue;
            if (bits[gx / 8] & (0x80 >> (gx % 8))) line[col] = c;
        }
    }
}

int gfx_text(gfx_band_t *band, const gfx_font_t *font, int x, int y, uint16_t color, const char *utf8)
{
    int start = x;
    uint16_t c = swap16(color);
    while (*utf8) {
        const gfx_glyph_t *g = find_glyph(font, gfx_utf8_next(&utf8));
        if (!g) continue;
        if (band && g->width) draw_glyph(band, font, g, x, y, c);
        x += g->advance;
    }
    return x - start;
}

static int width_of(const gfx_font_t *font, const char *s, size_t n)
{
    int w = 0;
    const char *end = s + n;
    while (s < end) {
        const gfx_glyph_t *g = find_glyph(font, gfx_utf8_next(&s));
        if (g) w += g->advance;
    }
    return w;
}

int gfx_text_width(const gfx_font_t *font, const char *utf8) { return width_of(font, utf8, strlen(utf8)); }

int gfx_wrap(const gfx_font_t *font, const char *utf8, int max_width, gfx_line_t *lines, int max_lines)
{
    int count = 0;
    const char *p = utf8;
    while (*p && count < max_lines) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *line_start = p, *line_end = p, *scan = p;
        while (*scan) {
            const char *word_end = scan;
            while (*word_end && *word_end != ' ') word_end++;
            if (width_of(font, line_start, word_end - line_start) > max_width) break;
            line_end = word_end;
            scan = word_end;
            while (*scan == ' ') scan++;
        }
        if (line_end == line_start) {
            /* A single word wider than the line: break it between code points. */
            const char *q = p;
            while (*q && *q != ' ') {
                const char *next = q;
                gfx_utf8_next(&next);
                if (width_of(font, line_start, next - line_start) > max_width && q != line_start) break;
                q = next;
            }
            line_end = q;
        }
        lines[count].start = line_start;
        lines[count].length = line_end - line_start;
        count++;
        p = line_end;
    }
    return count;
}

static void swap_int(int *a, int *b) { int t = *a; *a = *b; *b = t; }

void gfx_triangle(gfx_band_t *band, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    if (!band) return;
    if (y0 > y1) { swap_int(&x0, &x1); swap_int(&y0, &y1); }
    if (y0 > y2) { swap_int(&x0, &x2); swap_int(&y0, &y2); }
    if (y1 > y2) { swap_int(&x1, &x2); swap_int(&y1, &y2); }
    int top = y0 < band->y0 ? band->y0 : y0;
    int bottom = y2 >= band->y0 + band->rows ? band->y0 + band->rows - 1 : y2;
    for (int y = top; y <= bottom; y++) {
        /* Long edge y0..y2 and the short edge that contains y. */
        int xa = y2 == y0 ? x0 : x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        int xb;
        if (y < y1) xb = y1 == y0 ? x1 : x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        else xb = y2 == y1 ? x2 : x1 + (x2 - x1) * (y - y1) / (y2 - y1);
        if (xa > xb) swap_int(&xa, &xb);
        gfx_fill(band, xa, y, xb - xa + 1, 1, color);
    }
}

void gfx_thick_line(gfx_band_t *band, int x0, int y0, int x1, int y1, int thickness, uint16_t color)
{
    int dx = x1 - x0, dy = y1 - y0;
    /* Perpendicular offset, integer approximation of thickness/2 along the normal. */
    int len2 = dx * dx + dy * dy;
    if (!len2) { gfx_fill(band, x0 - thickness / 2, y0 - thickness / 2, thickness, thickness, color); return; }
    int len = 1;
    while (len * len < len2) len++;
    int nx = -dy * thickness / (2 * len), ny = dx * thickness / (2 * len);
    gfx_triangle(band, x0 + nx, y0 + ny, x0 - nx, y0 - ny, x1 + nx, y1 + ny, color);
    gfx_triangle(band, x0 - nx, y0 - ny, x1 - nx, y1 - ny, x1 + nx, y1 + ny, color);
}
