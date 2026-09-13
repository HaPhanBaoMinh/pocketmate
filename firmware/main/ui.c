#include "ui.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define BAR_H 28
#define MARGIN 8
#define TEXT_W (GFX_WIDTH - 2 * MARGIN)
#define C_BAR GFX_RGB(20, 60, 140)
#define C_BG GFX_BLACK
#define C_TEXT GFX_WHITE
#define C_DIM GFX_RGB(160, 160, 160)
#define C_ACCENT GFX_RGB(255, 200, 40)
#define C_ALERT GFX_RGB(200, 40, 40)
#define C_ARROW GFX_RGB(80, 220, 120)

static int paragraph(gfx_band_t *band, const gfx_font_t *font, int y, uint16_t color, const char *text, int max_lines)
{
    gfx_line_t lines[8];
    if (max_lines > 8) max_lines = 8;
    if (max_lines < 1) return y;
    int n = gfx_wrap(font, text, TEXT_W, lines, max_lines);
    for (int i = 0; i < n; i++) {
        char buf[ANCS_MESSAGE_MAX + 1];
        size_t len = lines[i].length < sizeof(buf) - 1 ? lines[i].length : sizeof(buf) - 1;
        memcpy(buf, lines[i].start, len);
        buf[len] = 0;
        gfx_text(band, font, MARGIN, y, color, buf);
        y += font->line_height;
    }
    return y;
}

static void centered(gfx_band_t *band, const gfx_font_t *font, int y, uint16_t color, const char *text)
{
    int w = gfx_text_width(font, text);
    gfx_text(band, font, (GFX_WIDTH - w) / 2, y, color, text);
}

/* Lower-cases ASCII and the Vietnamese capital Đ (C4 90 -> C4 91); other capitals do not
 * occur inside the keywords, which start with Đ, R, Q, T or a lowercase vowel. */
static unsigned char low(const char *p, unsigned char c)
{
    if (c == 0x90 && (unsigned char)p[-1] == 0xc4) return 0x91;
    return (unsigned char)tolower(c);
}
static bool has(const char *haystack, const char *needle)
{
    size_t n = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && low(p + i, (unsigned char)p[i]) == low(needle + i, (unsigned char)needle[i])) i++;
        if (i == n) return true;
    }
    return false;
}

maneuver_t ui_parse_instruction(const char *m, char *distance, size_t cap)
{
    if (cap) distance[0] = 0;
    if (!m) return MANEUVER_NONE;
    /* Distance: digits (with , or .) followed by " m" or " km". */
    for (const char *p = m; *p; p++) {
        if (!isdigit((unsigned char)*p) || (p > m && isalnum((unsigned char)p[-1]))) continue;
        const char *q = p;
        while (isdigit((unsigned char)*q) || *q == ',' || *q == '.') q++;
        const char *unit = q;
        while (*unit == ' ') unit++;
        size_t ulen = 0;
        if ((unit[0] == 'k' || unit[0] == 'K') && (unit[1] == 'm' || unit[1] == 'M')) ulen = 2;
        else if (unit[0] == 'm' || unit[0] == 'M') ulen = 1;
        if (!ulen || isalpha((unsigned char)unit[ulen])) continue;
        if (cap) snprintf(distance, cap, "%.*s %.*s", (int)(q - p), p, (int)ulen, unit);
        break;
    }
    if (has(m, "quay đầu")) return MANEUVER_UTURN;
    if (has(m, "vòng xuyến") || has(m, "bùng binh")) return MANEUVER_ROUNDABOUT;
    if (has(m, "điểm đến") || has(m, "đã đến") || (has(m, "đến nơi") && !has(m, "đến nơi lúc"))) return MANEUVER_ARRIVE;
    bool left = has(m, "trái"), right = has(m, "phải");
    bool slight = has(m, "nhẹ") || has(m, "chếch") || has(m, "giữ bên") || has(m, "đi theo hướng bên");
    if (left && !right) return slight ? MANEUVER_SLIGHT_LEFT : MANEUVER_LEFT;
    if (right && !left) return slight ? MANEUVER_SLIGHT_RIGHT : MANEUVER_RIGHT;
    if (has(m, "đi thẳng") || has(m, "tiếp tục") || has(m, "đi về hướng") || has(m, "đi dọc")) return MANEUVER_STRAIGHT;
    return MANEUVER_NONE;
}

/* Arrow inside a box centred at (cx, cy) of half-size `r`; line thickness t. */
static void arrow_head(gfx_band_t *b, int x, int y, int dx, int dy, int size, uint16_t c)
{
    /* Head tip at (x,y) pointing along (dx,dy) (unit direction: one of the 8 compass steps). */
    int bx = x - dx * size, by = y - dy * size;        /* base centre */
    int px = -dy, py = dx;                              /* perpendicular */
    gfx_triangle(b, x, y, bx + px * size, by + py * size, bx - px * size, by - py * size, c);
}

static void draw_maneuver(gfx_band_t *b, maneuver_t m, int cx, int cy, int r)
{
    int t = r / 4, h = r / 2;
    uint16_t c = C_ARROW;
    switch (m) {
    case MANEUVER_STRAIGHT:
        gfx_thick_line(b, cx, cy + r, cx, cy - r + h, t, c);
        arrow_head(b, cx, cy - r, 0, -1, h, c);
        break;
    case MANEUVER_LEFT:
    case MANEUVER_RIGHT: {
        int s = m == MANEUVER_LEFT ? -1 : 1;
        gfx_thick_line(b, cx - s * r / 2, cy + r, cx - s * r / 2, cy - r / 3, t, c);
        gfx_thick_line(b, cx - s * r / 2, cy - r / 3, cx + s * (r - h), cy - r / 3, t, c);
        arrow_head(b, cx + s * r, cy - r / 3, s, 0, h, c);
        break;
    }
    case MANEUVER_SLIGHT_LEFT:
    case MANEUVER_SLIGHT_RIGHT: {
        int s = m == MANEUVER_SLIGHT_LEFT ? -1 : 1;
        gfx_thick_line(b, cx - s * r / 3, cy + r, cx - s * r / 3, cy, t, c);
        gfx_thick_line(b, cx - s * r / 3, cy, cx + s * (r / 2), cy - r + h / 2, t, c);
        arrow_head(b, cx + s * r * 3 / 4, cy - r, s, -1, h * 2 / 3, c);
        break;
    }
    case MANEUVER_UTURN:
        gfx_thick_line(b, cx + r / 2, cy + r, cx + r / 2, cy - r / 2, t, c);
        gfx_thick_line(b, cx + r / 2, cy - r / 2, cx - r / 2, cy - r / 2, t, c);
        gfx_thick_line(b, cx - r / 2, cy - r / 2, cx - r / 2, cy + r / 2 - h, t, c);
        arrow_head(b, cx - r / 2, cy + r / 2 + h / 2, 0, 1, h, c);
        break;
    case MANEUVER_ROUNDABOUT:
        for (int i = 0; i < 8; i++) {
            static const int px[8] = {0, 7, 10, 7, 0, -7, -10, -7}, py[8] = {-10, -7, 0, 7, 10, 7, 0, -7};
            gfx_thick_line(b, cx + px[i] * r / 15, cy + py[i] * r / 15, cx + px[(i + 1) % 8] * r / 15, cy + py[(i + 1) % 8] * r / 15, t, c);
        }
        gfx_thick_line(b, cx, cy + r, cx, cy + r * 2 / 3, t, c);
        arrow_head(b, cx + r * 2 / 3, cy - r, 0, -1, h * 2 / 3, c);
        break;
    case MANEUVER_ARRIVE:
        gfx_thick_line(b, cx - r / 2, cy + r, cx - r / 2, cy - r, t, c);
        gfx_triangle(b, cx - r / 2, cy - r, cx + r, cy - r / 2, cx - r / 2, cy, C_ACCENT);
        break;
    default:
        break;
    }
}

void ui_render_band(const ui_model_t *m, gfx_band_t *band)
{
    gfx_fill(band, 0, 0, GFX_WIDTH, GFX_HEIGHT, C_BG);
    gfx_fill(band, 0, 0, GFX_WIDTH, BAR_H, C_BAR);
    gfx_text(band, &font_small, MARGIN, 4, C_TEXT, m->status);

    if (m->passkey[0]) {
        centered(band, &font_medium, 90, C_TEXT, "Mã ghép nối");
        centered(band, &font_large, 130, C_ACCENT, m->passkey);
        centered(band, &font_small, 200, C_DIM, "Nhập mã này trên iPhone");
        return;
    }
    if (!m->has_maps) {
        centered(band, &font_medium, 110, C_TEXT, "Chờ Google Maps");
        centered(band, &font_small, 150, C_DIM, "Mở chỉ đường trên iPhone");
        if (m->other_count) {
            char line[48];
            snprintf(line, sizeof(line), "Thông báo app khác: %u", m->other_count);
            centered(band, &font_small, 190, C_DIM, line);
            if (m->other_app[0]) centered(band, &font_small, 214, C_DIM, m->other_app);
        }
        return;
    }
    int bottom = m->stale ? GFX_HEIGHT - BAR_H : GFX_HEIGHT;
    char distance[24];
    maneuver_t maneuver = ui_parse_instruction(m->message, distance, sizeof(distance));
    /* Google Maps uses a generic title ("Hướng dẫn điều hướng"); only show a specific one. */
    bool generic_title = !m->title[0] || has(m->title, "điều hướng") || has(m->title, "navigation");
    int y = BAR_H + 4;
    if (maneuver != MANEUVER_NONE) {
        draw_maneuver(band, maneuver, distance[0] ? 60 : GFX_WIDTH / 2, y + 52, 44);
        if (distance[0]) {
            const gfx_font_t *f = gfx_text_width(&font_large, distance) <= GFX_WIDTH - 112 - MARGIN ? &font_large : &font_medium;
            gfx_text(band, f, 112, y + 24 + (f == &font_large ? 0 : 10), C_ACCENT, distance);
        }
        y += 108;
    } else if (distance[0]) {
        centered(band, &font_large, y, C_ACCENT, distance);
        y += font_large.line_height + 4;
    }
    if (!generic_title) y = paragraph(band, &font_small, y, C_DIM, m->title, 1) + 2;
    if (m->message[0]) y = paragraph(band, &font_medium, y, C_TEXT, m->message, (bottom - y) / font_medium.line_height);
    if (m->subtitle[0]) y = paragraph(band, &font_small, y, C_DIM, m->subtitle, (bottom - y) / font_small.line_height);
    if (m->stale) {
        char line[48];
        snprintf(line, sizeof(line), "Chưa cập nhật %lus", (unsigned long)m->age_seconds);
        gfx_fill(band, 0, GFX_HEIGHT - BAR_H, GFX_WIDTH, BAR_H, C_ALERT);
        centered(band, &font_small, GFX_HEIGHT - BAR_H + 4, C_TEXT, line);
    }
}
