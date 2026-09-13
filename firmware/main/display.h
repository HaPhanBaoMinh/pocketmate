/* ILI9341V over SPI on the LCDWiki 2.8" ESP32-S3 display (ES3C28P/ES3N28P). */
#pragma once
#include "esp_err.h"
#include "ui.h"

esp_err_t display_init(void);
/* Renders the whole screen band by band; blocks until the last band is queued. */
void display_present(const ui_model_t *model);
