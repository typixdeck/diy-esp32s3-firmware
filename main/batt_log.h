// 电池历史采样（后台任务，无论谁持屏都在跑）：
// 每 BATT_LOG_INTERVAL_S 秒采一次 INA219(VBAT) + CW2015（都常连 ESP 总线，
// 不走 MUX），环形缓冲存最近 1 小时，供电池曲线页绘图。
#pragma once

#include <stdint.h>
#include "driver/i2c_master.h"

#define BATT_LOG_INTERVAL_S  5
#define BATT_LOG_CAP         720   // 720 × 5s = 1 小时

typedef struct {
    uint16_t mv;      // 电池电压 mV（INA219 VBAT bus voltage）
    int16_t  ma;      // 电流 mA（正=放电）
    int8_t   soc;     // CW2015 SOC %，-1=读取失败
} batt_sample_t;

void batt_log_start(i2c_master_dev_handle_t ina_vbat,
                    i2c_master_dev_handle_t cw2015);

// 按时间顺序（旧→新）拷出最近 max 个样本，返回实际数量
int batt_log_get(batt_sample_t *out, int max);
