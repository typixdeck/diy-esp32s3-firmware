// tusb_config.h — AS_PART 模式（CONFIG_USB_DEVICE_UAC_AS_PART=y）
// UAC2 speaker + CDC composite。UAC 参数从组件的 tusb_config_uac.h 取（映射 CONFIG_UAC_*）。
// CDC 在此打开。tinyusb 编译时找这个文件（main/CMakeLists.txt 把 main/ 加进 tinyusb include）。
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "sdkconfig.h"
#include "uac_config.h"
#include "uac_descriptors.h"
#include "tusb_config_uac.h"   // 组件提供：CFG_TUD_AUDIO=1 + 所有 audio 参数

// ---- CDC（composite 第二类：调试串口 + REBOOT_TO_BOOT_MODE 魔串）----
#define CFG_TUD_CDC               1
#define CFG_TUD_CDC_RX_BUFSIZE    256
#define CFG_TUD_CDC_TX_BUFSIZE    512
#define CFG_TUD_CDC_FLUSH_ON_FIFO_FULL 1

// ---- 板级 / USB 速度 ----
// ESP32-S3-PICO 内置 PHY 是 Full-Speed（无外置 ULPI），所以走 FS。
#ifdef CONFIG_TINYUSB_RHPORT_HS
#  define CFG_TUSB_RHPORT0_MODE   OPT_MODE_DEVICE | OPT_MODE_HIGH_SPEED
#  define CONFIG_USB_HS           1
#else
#  define CFG_TUSB_RHPORT0_MODE   OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED
#  define CONFIG_USB_HS           0
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS    OPT_OS_FREERTOS
#endif
#ifndef ESP_PLATFORM
#define ESP_PLATFORM   1
#endif
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif
#define CFG_TUD_ENABLED          1
#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE   64
#endif

#ifdef __cplusplus
}
#endif
#endif
