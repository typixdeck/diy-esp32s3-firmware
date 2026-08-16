idf_component_register(
    SRCS "main.c" "aw9523.c" "gt911.c" "lcd_spi_init.c" "audio.c"
         "sensors.c" "batt_log.c" "vsync_mon.c" "ui.c" "ttf_font.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_lcd esp_timer esp_driver_i2s esp_hw_support usb_device_uac
             espressif__tinyusb esp_partition nvs_flash
)

# UAC+CDC composite（仍是 non-AS_PART 路径）：描述符 / tusb_config 由
# components/usb_device_uac 的 tusb/ 目录自带，CDC 接口直接加在那里。
# main.c 里的 cdc_stats_task 调 tud_cdc_*，所以 main 需要 tinyusb 头文件。
# main/usb_descriptors.c + main/tusb_config.h 是历史 AS_PART 版遗留，不编。
