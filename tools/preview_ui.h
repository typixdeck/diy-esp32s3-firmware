/* Host platform stand-ins, strictly for the framebuffer preview and UI tests. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <time.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NVS_NOT_FOUND 3
#define ESP_ERR_NVS_NO_FREE_PAGES 4
#define ESP_ERR_NVS_NEW_VERSION_FOUND 5
#define ESP_ERR_NOT_FOUND 6
#define ESP_ERR_INVALID_ARG 7
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define portMAX_DELAY 0xffffffff
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
static inline size_t heap_caps_get_free_size(int caps) { return caps==MALLOC_CAP_SPIRAM ? 4*1024*1024 : 48*1024; }
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_ANY 0
#define ESP_PARTITION_MMAP_DATA 0
typedef int portMUX_TYPE;
typedef void *esp_lcd_panel_handle_t;
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef void *esp_codec_dev_handle_t;
typedef void *SemaphoreHandle_t;
typedef int nvs_handle_t;
typedef struct {
    size_t count, capacity, size;
    unsigned char slots[64][32];
} preview_queue_t;
typedef preview_queue_t *QueueHandle_t;
typedef struct {
    char version[32], date[16];
} esp_app_desc_t;
typedef struct {
    size_t size;
    uint32_t address;
} esp_partition_t;
typedef int esp_partition_mmap_handle_t;
extern int64_t preview_now_us;
static inline int64_t esp_timer_get_time(void) {
    return preview_now_us;
}
static inline void *heap_caps_malloc(size_t n, int caps) {
    (void)caps;
    return malloc(n);
}
static inline void *heap_caps_calloc(size_t n, size_t size, int caps) {
    (void)caps;
    return calloc(n, size);
}
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return (void *)1;
}
static inline int xSemaphoreTake(SemaphoreHandle_t s, unsigned timeout) {
    (void)s;
    (void)timeout;
    return 1;
}
static inline int xSemaphoreGive(SemaphoreHandle_t s) {
    (void)s;
    return 1;
}
static inline QueueHandle_t xQueueCreate(size_t cap, size_t size) {
    assert(cap <= 64 && size <= 32);
    QueueHandle_t q = calloc(1, sizeof(*q));
    q->capacity = cap;
    q->size = size;
    return q;
}
static inline int xQueueSend(QueueHandle_t q, const void *data, unsigned wait) {
    (void)wait;
    if (q->count == q->capacity)
        return 0;
    memcpy(q->slots[q->count++], data, q->size);
    return 1;
}
static inline int xQueueReceive(QueueHandle_t q, void *data, unsigned wait) {
    (void)wait;
    if (!q->count)
        return 0;
    memcpy(data, q->slots[0], q->size);
    memmove(q->slots[0], q->slots[1], (--q->count) * sizeof(q->slots[0]));
    return 1;
}
static inline int xQueueReset(QueueHandle_t q) {
    q->count = 0;
    return 1;
}
static inline esp_err_t nvs_flash_init(void) {
    return ESP_OK;
}
static inline esp_err_t nvs_open(const char *name, int flags, nvs_handle_t *out) {
    (void)name;
    (void)flags;
    *out = 1;
    return ESP_OK;
}
typedef struct {
    char key[16];
    uint8_t value;
} preview_nvs_entry_t;
static preview_nvs_entry_t preview_nvs_entries[16];
static int preview_nvs_count, preview_nvs_commits;
static bool preview_nvs_write_fail;
static inline esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *v) {
    (void)h;
    for (int i = 0; i < preview_nvs_count; i++)
        if (!strcmp(key, preview_nvs_entries[i].key)) {
            *v = preview_nvs_entries[i].value;
            return ESP_OK;
        }
    return ESP_ERR_NVS_NOT_FOUND;
}
static inline esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t v) {
    (void)h;
    if (preview_nvs_write_fail)
        return ESP_FAIL;
    int i = 0;
    for (; i < preview_nvs_count; i++)
        if (!strcmp(key, preview_nvs_entries[i].key))
            break;
    assert(i < 16 && strlen(key) < 16);
    if (i == preview_nvs_count)
        preview_nvs_count++;
    strcpy(preview_nvs_entries[i].key, key);
    preview_nvs_entries[i].value = v;
    return ESP_OK;
}
static inline esp_err_t nvs_commit(nvs_handle_t h) {
    (void)h;
    if (preview_nvs_write_fail)
        return ESP_FAIL;
    preview_nvs_commits++;
    return ESP_OK;
}
static inline void nvs_close(nvs_handle_t h) {
    (void)h;
}
static inline const char *esp_err_to_name(esp_err_t err) {
    (void)err;
    return "HOST_FIXTURE";
}
static inline const esp_app_desc_t *esp_app_get_description(void) {
    static const esp_app_desc_t d = {"0.4.1-preview", "2026-09-30"};
    return &d;
}
static inline esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t h, int x0, int y0, int x1,
                                                  int y1, const void *fb) {
    (void)h;
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    (void)fb;
    return ESP_OK;
}
static inline esp_err_t i2c_master_probe(i2c_master_bus_handle_t h, uint16_t addr, int timeout) {
    (void)h;
    (void)timeout;
    return addr == 0x6A || addr == 0x6B ? ESP_ERR_NOT_FOUND : ESP_OK;
}
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label);
esp_err_t esp_partition_mmap(const esp_partition_t *part, size_t offset, size_t size, int flags,
                             const void **out, esp_partition_mmap_handle_t *handle);
