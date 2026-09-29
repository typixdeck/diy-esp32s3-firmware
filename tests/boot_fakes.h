#pragma once
#include <stddef.h>
#include <stdint.h>
typedef enum {ESP_RST_UNKNOWN,ESP_RST_POWERON,ESP_RST_EXT,ESP_RST_SW,ESP_RST_PANIC,
    ESP_RST_INT_WDT,ESP_RST_TASK_WDT,ESP_RST_WDT,ESP_RST_DEEPSLEEP,ESP_RST_BROWNOUT,
    ESP_RST_SDIO,ESP_RST_USB,ESP_RST_JTAG,ESP_RST_EFUSE,ESP_RST_PWR_GLITCH,ESP_RST_CPU_LOCKUP} esp_reset_reason_t;
static esp_reset_reason_t mock_reset=ESP_RST_POWERON;
#define RTC_NOINIT_ATTR
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
typedef struct {char version[32];} esp_app_desc_t;
static esp_reset_reason_t esp_reset_reason(void) {return mock_reset;}
static const esp_app_desc_t *esp_app_get_description(void) {static const esp_app_desc_t d={"0.3.1"};return &d;}
static size_t heap_caps_get_free_size(int caps) {(void)caps;return 4096;}
static size_t heap_caps_get_largest_free_block(int caps) {(void)caps;return 2048;}
static int64_t esp_timer_get_time(void) {return 123000000;}
