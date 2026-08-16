// 电源/电量传感器迷你驱动（从 main.c 抽出）：
//   INA219 ×2（U4 VBAT @0x40 / U20 VBUS @0x41，10mΩ 采样电阻）
//   CW2015 电量计（U27 @0x62，常连 ESP 总线）
//   STC3117 电量计（U53 @0x70，⚠️ 在 MUX U71 后面，仅 MUX=ESP 侧可达）
#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

esp_err_t ina219_read(i2c_master_dev_handle_t dev, float *bus_v, float *cur_a);
esp_err_t cw2015_read(i2c_master_dev_handle_t dev, float *v, int *soc);
esp_err_t cw2015_wake(i2c_master_dev_handle_t dev);
esp_err_t stc3117_read(i2c_master_dev_handle_t dev, float *v, float *soc);
// POR 后 STC3117 处于 standby（读数冻结），置 GG_RUN 启动连续转换（幂等）
void stc3117_ensure_running(i2c_master_dev_handle_t dev);
