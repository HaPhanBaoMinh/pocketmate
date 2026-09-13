idf_component_register(SRCS "main.c" "ancs_protocol.c" "gfx.c" "fonts.c" "ui.c" "display.c"
    INCLUDE_DIRS "." PRIV_REQUIRES bt nvs_flash esp_timer json esp_lcd esp_driver_spi esp_driver_gpio)
