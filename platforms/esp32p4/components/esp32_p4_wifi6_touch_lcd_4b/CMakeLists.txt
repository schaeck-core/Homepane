
idf_component_register(
    SRCS "esp32_p4_wifi6_touch_lcd_4b.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES driver esp_lcd esp_lcd_touch
    PRIV_REQUIRES usb spiffs fatfs
)
