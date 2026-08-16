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
// ⚠️ POR 后处于 standby（MODE.GG_RUN=0），电压/SOC 寄存器冻结在初次转换值。
// CC_CNF/VM_CNF 精确标定交给 Pi 内核驱动；ESP 只确保芯片在跑，写入幂等。
// ---------------------------------------------------------------------------
#define STC3117_REG_MODE   0x00
#define STC3117_GG_RUN     (1 << 4)   // 1=运行；bit0 VMODE=0 混合模式（带库仑计）

void stc3117_ensure_running(i2c_master_dev_handle_t dev)
{
    uint8_t mode = 0;
    if (!dev || reg8_read(dev, STC3117_REG_MODE, &mode, 1) != ESP_OK) return;
    if (!(mode & STC3117_GG_RUN)) {
        uint8_t cmd[2] = { STC3117_REG_MODE, (uint8_t)(mode | STC3117_GG_RUN) };
        if (i2c_master_transmit(dev, cmd, 2, 100) == ESP_OK) {
            ESP_LOGI(TAG, "STC3117 原为 standby（读数冻结），已置 GG_RUN 启动连续转换");
        }
    }
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
