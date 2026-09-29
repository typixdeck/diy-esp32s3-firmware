#include "boot_diag.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define DIAG_MAGIC 0x54443031u
typedef struct { uint32_t value, inverse; } checkpoint_t;
typedef struct {
    uint32_t magic;
    checkpoint_t boots;
    checkpoint_t stages[BOOT_SLOT_COUNT];
} retained_t;
static RTC_NOINIT_ATTR retained_t s_retained;
static retained_t s_previous;
static esp_reset_reason_t s_reset;
static int s_audio_error, s_usb_error;
static portMUX_TYPE s_diag_lock = portMUX_INITIALIZER_UNLOCKED;

static checkpoint_t point(uint32_t value)
{
    return (checkpoint_t){value, ~value};
}

static bool valid(checkpoint_t value, uint32_t max)
{
    return value.value <= max && value.inverse == ~value.value;
}

void boot_diag_init(void)
{
    s_reset = esp_reset_reason();
    bool warm = s_reset == ESP_RST_SW || s_reset == ESP_RST_PANIC ||
        s_reset == ESP_RST_INT_WDT || s_reset == ESP_RST_TASK_WDT || s_reset == ESP_RST_WDT;
    // Uninitialized RTC RAM, cold power-on and torn writes are not evidence.
    bool ok = warm && s_retained.magic == DIAG_MAGIC &&
        valid(s_retained.boots, 65535) && s_retained.boots.value &&
        valid(s_retained.stages[BOOT_MAIN], BOOT_RUNNING) &&
        valid(s_retained.stages[BOOT_AUDIO], AUDIO_READY);
    memset(&s_previous, 0, sizeof(s_previous));
    if (ok) s_previous = s_retained;
    s_retained = (retained_t){0};
    s_retained.boots = point(ok && s_previous.boots.value < 65535 ?
        s_previous.boots.value + 1 : ok ? 65535 : 1);
    for (int i = 0; i < BOOT_SLOT_COUNT; ++i) s_retained.stages[i] = point(0);
    s_retained.magic = DIAG_MAGIC;
    s_audio_error = s_usb_error = 0;
    boot_diag_stage(BOOT_MAIN, BOOT_START);
}

void boot_diag_stage(boot_slot_t slot, unsigned stage)
{
    if (slot < 0 || slot >= BOOT_SLOT_COUNT ||
        stage > (slot == BOOT_MAIN ? BOOT_RUNNING : AUDIO_READY)) return;
    portENTER_CRITICAL(&s_diag_lock);
    s_retained.stages[slot] = point(stage);
    portEXIT_CRITICAL(&s_diag_lock);
}

void boot_diag_audio_result(int error)
{
    portENTER_CRITICAL(&s_diag_lock);
    s_audio_error = error;
    portEXIT_CRITICAL(&s_diag_lock);
}

void boot_diag_usb_result(int error)
{
    portENTER_CRITICAL(&s_diag_lock);
    s_usb_error = error;
    portEXIT_CRITICAL(&s_diag_lock);
}

static const char *reset_name(void)
{
    switch (s_reset) {
    case ESP_RST_POWERON: return "poweron";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt-wdt";
    case ESP_RST_TASK_WDT: return "task-wdt";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    case ESP_RST_USB: return "usb";
    case ESP_RST_JTAG: return "jtag";
    case ESP_RST_EFUSE: return "efuse";
    case ESP_RST_PWR_GLITCH: return "power-glitch";
    case ESP_RST_CPU_LOCKUP: return "cpu-lockup";
    default: return "unknown";
    }
}

size_t boot_diag_format(char *out, size_t capacity)
{
    if (!out || !capacity) return 0;
    retained_t current;
    int audio_error, usb_error;
    portENTER_CRITICAL(&s_diag_lock);
    current = s_retained;
    audio_error = s_audio_error;
    usb_error = s_usb_error;
    portEXIT_CRITICAL(&s_diag_lock);
    int n = snprintf(out, capacity,
        "TD_DIAG v=1 fw=%.31s reset=%s retained=%u boots=%lu "
        "prev_main=%lu prev_audio=%lu main=%lu audio=%lu "
        "audio_err=%d usb_err=%d heap=%u largest=%u up=%lu\r\n",
        esp_app_get_description()->version, reset_name(), s_previous.magic == DIAG_MAGIC,
        (unsigned long)current.boots.value,
        (unsigned long)s_previous.stages[BOOT_MAIN].value,
        (unsigned long)s_previous.stages[BOOT_AUDIO].value,
        (unsigned long)current.stages[BOOT_MAIN].value,
        (unsigned long)current.stages[BOOT_AUDIO].value, audio_error, usb_error,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned long)(esp_timer_get_time() / 1000000));
    return n < 0 ? 0 : (size_t)n < capacity ? (size_t)n : capacity - 1;
}
