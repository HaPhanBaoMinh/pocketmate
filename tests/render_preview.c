/* Renders the LCD model to a binary PPM on stdout so the layout can be
 * checked on the host: tools/preview.sh <scenario> */
#include <stdio.h>
#include <string.h>
#include "ui.h"

int main(int argc, char **argv)
{
    const char *scenario = argc > 1 ? argv[1] : "maps";
    ui_model_t m = {0};
    strcpy(m.status, "Pocketmate · đã kết nối");
    if (!strcmp(scenario, "passkey")) { strcpy(m.status, "Ghép nối iPhone"); strcpy(m.passkey, "482913"); }
    else if (!strcmp(scenario, "idle")) { m.other_count = 3; }
    else {
        m.has_maps = true;
        strcpy(m.title, "Hướng dẫn điều hướng");
        if (!strcmp(scenario, "real")) strcpy(m.message, "Rẽ phải vào Đinh Bộ Lĩnh");
        else if (!strcmp(scenario, "left")) strcpy(m.message, "Sau 200 m, rẽ trái vào Điện Biên Phủ");
        else if (!strcmp(scenario, "straight")) strcpy(m.message, "Đi thẳng 1,2 km trên Xa lộ Hà Nội");
        else if (!strcmp(scenario, "uturn")) strcpy(m.message, "Quay đầu tại Nguyễn Thị Minh Khai sau 350 m");
        else if (!strcmp(scenario, "slight")) strcpy(m.message, "Rẽ nhẹ sang phải vào Trường Sơn");
        else if (!strcmp(scenario, "arrive")) strcpy(m.message, "Điểm đến ở bên trái, 50 m");
        else if (!strcmp(scenario, "roundabout")) strcpy(m.message, "Tại vòng xuyến, đi theo lối ra thứ 2 vào Phạm Văn Đồng");
        else strcpy(m.message, "Tiếp tục đi thẳng 1,2 km rồi rẽ trái vào Điện Biên Phủ. Đến nơi lúc 14:32.");
        if (!strcmp(scenario, "stale")) { m.stale = true; m.age_seconds = 47; }
    }
    static uint16_t pixels[GFX_WIDTH * 40];
    printf("P6\n%d %d\n255\n", GFX_WIDTH, GFX_HEIGHT);
    for (int y = 0; y < GFX_HEIGHT; y += 40) {
        gfx_band_t band = {.pixels = pixels, .y0 = y, .rows = 40};
        ui_render_band(&m, &band);
        for (int i = 0; i < GFX_WIDTH * 40; i++) {
            uint16_t p = (uint16_t)((pixels[i] << 8) | (pixels[i] >> 8));
            unsigned char rgb[3] = {(p >> 11) << 3, ((p >> 5) & 0x3f) << 2, (p & 0x1f) << 3};
            fwrite(rgb, 1, 3, stdout);
        }
    }
    return 0;
}
