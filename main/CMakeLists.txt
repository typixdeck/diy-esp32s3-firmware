idf_component_register(
    SRCS "main.c" "aw9523.c" "gt911.c" "lcd_spi_init.c" "audio.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_lcd esp_timer esp_driver_i2s esp_hw_support usb_device_uac
)

# 纯 UAC（non-AS_PART）：描述符 / tusb_config 由 components/usb_device_uac 自带。
# main/usb_descriptors.c + main/tusb_config.h 留给以后加回 CDC composite，此刻不编。
