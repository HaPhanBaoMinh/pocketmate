#include "display.h"
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TAG "DISPLAY"
/* Pin map from LCDWiki "2.8inch ESP32-S3 Display" ESP-IDF demo instructions. */
#define PIN_CS 10
#define PIN_DC 46
#define PIN_SCK 12
#define PIN_MOSI 11
#define PIN_MISO 13
#define PIN_BL 45
#define SPI_HOST SPI2_HOST
#define PCLK_HZ (40 * 1000 * 1000)
#define BAND_ROWS 40
#define BAND_BYTES (GFX_WIDTH * BAND_ROWS * 2)

static esp_lcd_panel_io_handle_t io;
static uint16_t *bands[2];
static SemaphoreHandle_t done;
static bool ok;

static bool IRAM_ATTR on_done(esp_lcd_panel_io_handle_t h, esp_lcd_panel_io_event_data_t *d, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(done, &woken);
    return woken == pdTRUE;
}

typedef struct { uint8_t cmd; uint8_t len; uint8_t data[15]; uint16_t delay_ms; } step_t;
/* Vendor sequence from lcdwiki.com/res/ES3C28P/ILI9341V_Init.txt; the panel reset
 * pin is tied to chip EN, so a software reset replaces the GPIO reset pulse. */
static const step_t init_steps[] = {
    {0x01, 0, {0}, 150},
    {0xCF, 3, {0x00, 0xC1, 0x30}, 0},
    {0xED, 4, {0x64, 0x03, 0x12, 0x81}, 0},
    {0xE8, 3, {0x85, 0x00, 0x78}, 0},
    {0xCB, 5, {0x39, 0x2C, 0x00, 0x34, 0x02}, 0},
    {0xF7, 1, {0x20}, 0},
    {0xEA, 2, {0x00, 0x00}, 0},
    {0xC0, 1, {0x13}, 0},
    {0xC1, 1, {0x13}, 0},
    {0xC5, 2, {0x22, 0x35}, 0},
    {0xC7, 1, {0xBD}, 0},
    {0x21, 0, {0}, 0},               /* IPS panel: inversion on */
    {0x36, 1, {0x08}, 0},            /* portrait, BGR order */
    {0xB6, 2, {0x0A, 0xA2}, 0},
    {0x3A, 1, {0x55}, 0},            /* RGB565 */
    {0xF6, 2, {0x01, 0x30}, 0},
    {0xB1, 2, {0x00, 0x1B}, 0},
    {0xF2, 1, {0x00}, 0},
    {0x26, 1, {0x01}, 0},
    {0xE0, 15, {0x0F, 0x35, 0x31, 0x0B, 0x0E, 0x06, 0x49, 0xA7, 0x33, 0x07, 0x0F, 0x03, 0x0C, 0x0A, 0x00}, 0},
    {0xE1, 15, {0x00, 0x0A, 0x0F, 0x04, 0x11, 0x08, 0x36, 0x58, 0x4D, 0x07, 0x10, 0x0C, 0x32, 0x34, 0x0F}, 0},
    {0x11, 0, {0}, 120},
    {0x29, 0, {0}, 20},
};

esp_err_t display_init(void)
{
    gpio_config_t bl = {.pin_bit_mask = 1ULL << PIN_BL, .mode = GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&bl), TAG, "backlight gpio");
    gpio_set_level(PIN_BL, 0);

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI, .miso_io_num = PIN_MISO, .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = BAND_BYTES,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");
    done = xSemaphoreCreateCounting(2, 0);
    for (int i = 0; i < 2; i++) {
        bands[i] = heap_caps_malloc(BAND_BYTES, MALLOC_CAP_DMA);
        if (!bands[i]) return ESP_ERR_NO_MEM;
    }
    /* The ILI9341 accepts writes at 40 MHz but reads only below ~6.6 MHz, so
     * initialisation and the identity check run on a slow device first. */
    esp_lcd_panel_io_spi_config_t cfg = {
        .cs_gpio_num = PIN_CS, .dc_gpio_num = PIN_DC, .spi_mode = 0, .pclk_hz = 5 * 1000 * 1000,
        .trans_queue_depth = 2, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI_HOST, &cfg, &io), TAG, "panel io (slow)");
    for (size_t i = 0; i < sizeof(init_steps) / sizeof(init_steps[0]); i++) {
        const step_t *s = &init_steps[i];
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, s->cmd, s->len ? s->data : NULL, s->len), TAG, "init cmd");
        if (s->delay_ms) vTaskDelay(pdMS_TO_TICKS(s->delay_ms));
    }
    /* RDDPM 0x0A reads 0x9C (booster on, normal mode, sleep out, display on) from a live
     * ILI9341. RDDID 0xD3 would need an extra dummy clock that esp_lcd does not emit, so it
     * is logged for reference only. */
    uint8_t id[4] = {0}, pm[2] = {0};
    esp_err_t r1 = esp_lcd_panel_io_rx_param(io, 0xD3, id, sizeof(id));
    esp_err_t r2 = esp_lcd_panel_io_rx_param(io, 0x0A, pm, sizeof(pm));
    ESP_LOGI(TAG, "RDDID 0xD3 -> %02x %02x %02x %02x (%s); RDDPM 0x0A -> %02x %02x (%s)",
             id[0], id[1], id[2], id[3], esp_err_to_name(r1), pm[0], pm[1], esp_err_to_name(r2));
    bool identified = pm[0] == 0x9c || (id[1] == 0x93 && id[2] == 0x41) || (id[2] == 0x93 && id[3] == 0x41);
    ESP_LOGI(TAG, "panel %s", identified ? "answered on SPI: ILI9341 awake and on" : "NOT answering: check wiring/pin map");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_del(io), TAG, "panel io del");
    cfg.pclk_hz = PCLK_HZ;
    cfg.on_color_trans_done = on_done;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI_HOST, &cfg, &io), TAG, "panel io (fast)");
    ok = true;
    return ESP_OK;
}

void display_present(const ui_model_t *model)
{
    if (!ok) return;
    static bool lit;
    int pending = 0, index = 0;
    for (int y = 0; y < GFX_HEIGHT; y += BAND_ROWS, index ^= 1) {
        if (pending == 2) { xSemaphoreTake(done, portMAX_DELAY); pending--; }
        gfx_band_t band = {.pixels = bands[index], .y0 = y, .rows = BAND_ROWS};
        ui_render_band(model, &band);
        uint8_t col[4] = {0, 0, (GFX_WIDTH - 1) >> 8, (GFX_WIDTH - 1) & 0xff};
        uint8_t row[4] = {y >> 8, y & 0xff, (y + BAND_ROWS - 1) >> 8, (y + BAND_ROWS - 1) & 0xff};
        esp_lcd_panel_io_tx_param(io, 0x2A, col, 4);
        esp_lcd_panel_io_tx_param(io, 0x2B, row, 4);
        if (esp_lcd_panel_io_tx_color(io, 0x2C, bands[index], BAND_BYTES) == ESP_OK) pending++;
    }
    while (pending) { xSemaphoreTake(done, portMAX_DELAY); pending--; }
    if (!lit) { gpio_set_level(PIN_BL, 1); lit = true; }
}
