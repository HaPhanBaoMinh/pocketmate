#include "ui.h"
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

static int paragraph(gfx_band_t *band, const gfx_font_t *font, int y, uint16_t color, const char *text, int max_lines)
{
    gfx_line_t lines[8];
    if (max_lines > 8) max_lines = 8;
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
    int y = BAR_H + 6, bottom = m->stale ? GFX_HEIGHT - BAR_H : GFX_HEIGHT;
    if (m->title[0]) y = paragraph(band, &font_medium, y, C_ACCENT, m->title, 2) + 2;
    if (m->subtitle[0]) y = paragraph(band, &font_small, y, C_DIM, m->subtitle, 2) + 2;
    if (m->message[0]) y = paragraph(band, &font_medium, y, C_TEXT, m->message, (bottom - y) / font_medium.line_height);
    if (m->stale) {
        char line[48];
        snprintf(line, sizeof(line), "Chưa cập nhật %lus", (unsigned long)m->age_seconds);
        gfx_fill(band, 0, GFX_HEIGHT - BAR_H, GFX_WIDTH, BAR_H, C_ALERT);
        centered(band, &font_small, GFX_HEIGHT - BAR_H + 4, C_TEXT, line);
    }
}
