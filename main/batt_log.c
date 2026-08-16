#include "batt_log.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "sensors.h"

static const char *TAG = "BATTLOG";

static i2c_master_dev_handle_t s_ina, s_cw;
static batt_sample_t s_ring[BATT_LOG_CAP];
static int s_head = 0;    // 下一个写入位置
static int s_count = 0;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void batt_log_task(void *arg)
{
    (void)arg;
    while (1) {
        batt_sample_t s = { .mv = 0, .ma = 0, .soc = -1 };
        float v = 0, a = 0;
        if (s_ina && ina219_read(s_ina, &v, &a) == ESP_OK) {
            s.mv = (uint16_t)(v * 1000.0f + 0.5f);
            s.ma = (int16_t)(a * 1000.0f);
        }
        float cw_v = 0;
        int soc = -1;
        if (s_cw && cw2015_read(s_cw, &cw_v, &soc) == ESP_OK) {
            s.soc = (int8_t)soc;
        }
        portENTER_CRITICAL(&s_mux);
        s_ring[s_head] = s;
        s_head = (s_head + 1) % BATT_LOG_CAP;
        if (s_count < BATT_LOG_CAP) s_count++;
        portEXIT_CRITICAL(&s_mux);

        vTaskDelay(pdMS_TO_TICKS(BATT_LOG_INTERVAL_S * 1000));
    }
}

void batt_log_start(i2c_master_dev_handle_t ina_vbat,
                    i2c_master_dev_handle_t cw2015)
{
    s_ina = ina_vbat;
    s_cw = cw2015;
    if (xTaskCreate(batt_log_task, "batt_log", 3072, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "batt_log 任务创建失败");
    }
}

int batt_log_get(batt_sample_t *out, int max)
{
    portENTER_CRITICAL(&s_mux);
    int n = s_count < max ? s_count : max;
    // 取最新的 n 个，按旧→新排列
    int start = (s_head - n + BATT_LOG_CAP) % BATT_LOG_CAP;
    for (int i = 0; i < n; i++) {
        out[i] = s_ring[(start + i) % BATT_LOG_CAP];
    }
    portEXIT_CRITICAL(&s_mux);
    return n;
}
