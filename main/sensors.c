#include "sensors.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "SENSORS";

// ---------------------------------------------------------------------------
// INA219（寄存器级，POR 默认配置 0x399F：32V 量程 / 12bit 连续采样）
// bus voltage LSB=4mV（寄存器右移 3 位），shunt voltage LSB=10µV，I=Vshunt/10mΩ
// ---------------------------------------------------------------------------
#define INA219_REG_SHUNT_V 0x01
#define INA219_REG_BUS_V   0x02
#define INA219_SHUNT_OHM   0.010f

static esp_err_t reg8_read(i2c_master_dev_handle_t dev, uint8_t reg,
                           uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, buf, len, 100);
}

static esp_err_t ina219_read16(i2c_master_dev_handle_t dev, uint8_t reg, uint16_t *val)
{
    uint8_t b[2];
    ESP_RETURN_ON_ERROR(reg8_read(dev, reg, b, 2), TAG, "ina219 rd");
    *val = ((uint16_t)b[0] << 8) | b[1];
    return ESP_OK;
}

esp_err_t ina219_read(i2c_master_dev_handle_t dev, float *bus_v, float *cur_a)
{
    uint16_t raw;
    ESP_RETURN_ON_ERROR(ina219_read16(dev, INA219_REG_BUS_V, &raw), TAG, "bus_v");
    *bus_v = (float)(raw >> 3) * 0.004f;
    ESP_RETURN_ON_ERROR(ina219_read16(dev, INA219_REG_SHUNT_V, &raw), TAG, "shunt_v");
    *cur_a = (int16_t)raw * 0.00001f / INA219_SHUNT_OHM;
    return ESP_OK;
}

// CW2015：VCELL 14bit LSB 305µV，SOC 整数 %
esp_err_t cw2015_read(i2c_master_dev_handle_t dev, float *v, int *soc)
{
    uint8_t b[2];
    ESP_RETURN_ON_ERROR(reg8_read(dev, 0x02, b, 2), TAG, "cw vcell");
    *v = (float)((((uint16_t)b[0] & 0x3F) << 8) | b[1]) * 305e-6f;
    ESP_RETURN_ON_ERROR(reg8_read(dev, 0x04, b, 2), TAG, "cw soc");
    *soc = b[0];
    return ESP_OK;
}

// CW2015 POR 后可能在 sleep，写 MODE(0x0A)=0x00 唤醒
esp_err_t cw2015_wake(i2c_master_dev_handle_t dev)
{
    uint8_t wake[2] = { 0x0A, 0x00 };
    return i2c_master_transmit(dev, wake, 2, 100);
}

// ---------------------------------------------------------------------------
// STC3117：V LSB 2.20mV，SOC LSB 1/512%
// ⚠️ POR 默认 MODE = VMODE=1（纯电压模式）+ GG_RUN=0（standby，读数冻结）。
// 手册 §6.1.4："Current sensing is available only in mixed mode (VMODE=0)"、
// "The Coulomb counter is inactive if the VMODE bit is set, this is the default
// state at POR"。旧固件只置 GG_RUN 没清 VMODE → 电流寄存器恒 ≈0、SOC 只靠 OCV
// 表且未标定（2026-09-10 实机：放电 1.5A 时 STC 电流读 -2mA、SOC 66%/79% 乱跳）。
// 正确姿势：先写 CC_CNF/VM_CNF（按 10mΩ + 标称容量），再 MODE=GG_RUN（VMODE=0）。
// ---------------------------------------------------------------------------
#include "board_pins.h"
#define STC3117_REG_MODE   0x00
#define STC3117_REG_CC_CNF 0x0F   // 16bit LE：Rsense[mΩ]×Cnom[mAh]/49.556
#define STC3117_REG_VM_CNF 0x11   // 16bit LE：Ri[mΩ]×Cnom[mAh]/977.78（Ri 取 200mΩ）
#define STC3117_VMODE      (1 << 0)   // 1=纯电压模式（POR 默认），0=混合模式（库仑计）
#define STC3117_GG_RUN     (1 << 4)   // 1=运行
#define STC3117_SENSE_MOHM 10
#define STC3117_BATT_RI_MOHM 200

static esp_err_t stc3117_write16(i2c_master_dev_handle_t dev, uint8_t reg, uint16_t v)
{
    uint8_t cmd[3] = { reg, (uint8_t)v, (uint8_t)(v >> 8) };
    return i2c_master_transmit(dev, cmd, 3, 100);
}

void stc3117_ensure_running(i2c_master_dev_handle_t dev)
{
    uint8_t mode = 0;
    if (!dev || reg8_read(dev, STC3117_REG_MODE, &mode, 1) != ESP_OK) return;
    if ((mode & STC3117_GG_RUN) && !(mode & STC3117_VMODE)) return;   // 已在混合模式运行
    uint16_t cc = (uint16_t)(STC3117_SENSE_MOHM * BOARD_BATT_CAPACITY_MAH / 49.556f + 0.5f);
    uint16_t vm = (uint16_t)(STC3117_BATT_RI_MOHM * BOARD_BATT_CAPACITY_MAH / 977.78f + 0.5f);
    uint8_t stop[2] = { STC3117_REG_MODE, 0x00 };                     // 先停（VMODE=0,GG_RUN=0）
    esp_err_t e = i2c_master_transmit(dev, stop, 2, 100);
    if (e == ESP_OK) e = stc3117_write16(dev, STC3117_REG_CC_CNF, cc);
    if (e == ESP_OK) e = stc3117_write16(dev, STC3117_REG_VM_CNF, vm);
    uint8_t run[2] = { STC3117_REG_MODE, STC3117_GG_RUN };            // 混合模式 + 运行
    if (e == ESP_OK) e = i2c_master_transmit(dev, run, 2, 100);
    ESP_LOGI(TAG, "STC3117 mode 0x%02X → 混合模式运行, CC_CNF=%u VM_CNF=%u (%s)",
             mode, cc, vm, esp_err_to_name(e));
}

#define STC3117_SENSE_OHM  0.010f     // U37 10mΩ（BAT_N → GND）

esp_err_t stc3117_read_current(i2c_master_dev_handle_t dev, float *cur_a)
{
    uint8_t b[2];
    ESP_RETURN_ON_ERROR(reg8_read(dev, 0x06, b, 2), TAG, "stc cur");
    int raw = b[0] | (b[1] << 8);
    raw &= 0x3FFF;                         // 14bit 二补码
    if (raw & 0x2000) raw -= 0x4000;
    *cur_a = (float)raw * 5.88e-6f / STC3117_SENSE_OHM;
    return ESP_OK;
}

esp_err_t stc3117_read(i2c_master_dev_handle_t dev, float *v, float *soc)
{
    uint8_t b[2];
    ESP_RETURN_ON_ERROR(reg8_read(dev, 0x08, b, 2), TAG, "stc v");
    *v = (float)(int16_t)(b[0] | (b[1] << 8)) * 2.20e-3f;
    ESP_RETURN_ON_ERROR(reg8_read(dev, 0x02, b, 2), TAG, "stc soc");
    *soc = (float)(uint16_t)(b[0] | (b[1] << 8)) / 512.0f;
    return ESP_OK;
}
