// 电源/电量传感器迷你驱动（从 main.c 抽出）：
//   INA219 ×2（U4 VBAT @0x40 / U20 VBUS @0x41，10mΩ 采样电阻）
//   CW2015 电量计（U27 @0x62，常连 ESP 总线）
//   STC3117 电量计（U53 @0x70，⚠️ 在 MUX U71 后面，仅 MUX=ESP 侧可达）
#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>
#include "driver/i2c_master.h"

esp_err_t ina219_read(i2c_master_dev_handle_t dev, float *bus_v, float *cur_a);
esp_err_t cw2015_read(i2c_master_dev_handle_t dev, float *v, int *soc);
esp_err_t cw2015_wake(i2c_master_dev_handle_t dev);
esp_err_t stc3117_read(i2c_master_dev_handle_t dev, float *v, float *soc);
// 电池真实电流（BAT_N 上 U37 10mΩ 采样，REG 0x06/0x07，14bit 二补码，LSB 5.88µV）
// 正=充电、负=放电（按 CG 脚接电池负极、Rsense 到 GND 的标准接法；2026-09-10 实机核对）
esp_err_t stc3117_read_current(i2c_master_dev_handle_t dev, float *cur_a);
// Initialize once before tasks. Serialize complete gauge transactions with MUX
// changes / bus recovery. End always invalidates cached samples and EOC timing.
void sensors_init(void);
bool sensors_mux_begin(void);
void sensors_mux_end(bool esp_side);
// Only the 5 s battery task calls this; UI/diagnostics are read-only.
void stc3117_poll(i2c_master_dev_handle_t dev, i2c_master_dev_handle_t cw, bool usb_present);

size_t sensors_format_diagnostics(i2c_master_dev_handle_t stc, i2c_master_dev_handle_t cw, char *out, size_t capacity);
