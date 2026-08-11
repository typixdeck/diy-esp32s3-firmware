idf_component_register(
    SRCS "main.c" "aw9523.c" "gt911.c" "lcd_spi_init.c" "audio.c" "usb_descriptors.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_lcd esp_timer esp_driver_i2s esp_hw_support espressif__tinyusb espressif__usb_device_uac
)

# CONFIG_USB_DEVICE_UAC_AS_PART=y：组件不自带 tusb_config.h / usb_descriptors.c，
# 我们在 main/ 里自己写（加了 CDC composite）。把 main/ 喂给 tinyusb 的 include，
# 并带上 usb_device_uac 组件的 tusb_uac 头文件（uac_descriptors.h / tusb_config_uac.h），
# 再把 usb_descriptors.c 编进 tinyusb（描述符回调要和 tinyusb 一起编）。
idf_build_get_property(build_components BUILD_COMPONENTS)
if(tinyusb IN_LIST build_components)
    set(tinyusb_name tinyusb)
else()
    set(tinyusb_name espressif__tinyusb)
endif()
idf_component_get_property(tusb_lib ${tinyusb_name} COMPONENT_LIB)
idf_component_get_property(uac_dir espressif__usb_device_uac COMPONENT_DIR)
target_include_directories(${tusb_lib} PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}"
    "${uac_dir}/tusb_uac")
target_sources(${tusb_lib} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c")
