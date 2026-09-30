#include "sensors.h"
#include <stdio.h>

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

// A single background owner samples/initializes the STC3117. UI reads a
// validated snapshot; a frame redraw must never re-seed the coulomb counter.
#include "stc_gauge.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
static SemaphoreHandle_t s_gauge_mutex;
static StaticSemaphore_t s_gauge_mutex_storage;
static bool s_gauge_owned = true;
static stc_gauge_t s_gauge;

void sensors_init(void)
{
    s_gauge_mutex = xSemaphoreCreateMutexStatic(&s_gauge_mutex_storage);
    s_gauge_owned = true;
    stc_gauge_invalidate(&s_gauge);
}

bool sensors_mux_begin(void)
{
    return s_gauge_mutex && xSemaphoreTake(s_gauge_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}

void sensors_mux_end(bool esp_side)
{
    s_gauge_owned = esp_side;
    stc_gauge_invalidate(&s_gauge);
    xSemaphoreGive(s_gauge_mutex);
}

void stc3117_poll(i2c_master_dev_handle_t dev, i2c_master_dev_handle_t cw,
                  bool usb_present)
{
    if (!sensors_mux_begin()) return;
    if (s_gauge_owned) {
        float cw_v=0; int cw_soc;
        uint8_t mode=0xff;
        int cw_mv=-1;
        if (cw && reg8_read(cw,0x0a,&mode,1)==ESP_OK && !(mode&0xc0) &&
            cw2015_read(cw,&cw_v,&cw_soc)==ESP_OK)
            cw_mv=(int)(cw_v*1000);
        stc_status_t before=s_gauge.status;
        stc_gauge_poll(&s_gauge,dev,cw_mv,usb_present,esp_timer_get_time()/1000);
        if(s_gauge.status!=before)
            ESP_LOGI(TAG,"STC3117 state=%d (0 wait,1 ready,2 I2C,3 fault,4 stale,5 invalid,6 seeded,7 full)",s_gauge.status);
    } else stc_gauge_invalidate(&s_gauge);
    xSemaphoreGive(s_gauge_mutex);
}

static bool gauge_snapshot_ready(void)
{
    int64_t age=esp_timer_get_time()/1000-s_gauge.last_ms;
    return s_gauge_owned && s_gauge.valid && age>=0 && age<=7500;
}

esp_err_t stc3117_read_current(i2c_master_dev_handle_t dev, float *cur_a)
{
    if(!dev || !cur_a || !sensors_mux_begin()) return ESP_ERR_INVALID_STATE;
    bool ok=gauge_snapshot_ready();
    if(ok) *cur_a=s_gauge.ma/1000.0f;
    xSemaphoreGive(s_gauge_mutex);
    return ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t stc3117_read(i2c_master_dev_handle_t dev, float *v, float *soc)
{
    if(!dev || !v || !soc || !sensors_mux_begin()) return ESP_ERR_INVALID_STATE;
    bool ok=gauge_snapshot_ready();
    if(ok) { *v=s_gauge.mv/1000.0f; *soc=s_gauge.soc_raw/512.0f; }
    xSemaphoreGive(s_gauge_mutex);
    return ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}

/* Read the official shared-RAM marker without creating or repairing it.
 * CRC-8 poly 0x07/init 0, matching the Pi/ESP gauge RAM layout. */
static uint8_t sensors_diag_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

/* Bounded read-only diagnostics. Never call poll/wake here: those
 * write the gauges and would destroy evidence of standby/POR/battery-swap.
 * A second request after >= 4 seconds can establish whether counter advances.
 * STC3117 is reachable only while the existing MUX owner is the ESP. */
size_t sensors_format_diagnostics(i2c_master_dev_handle_t stc, i2c_master_dev_handle_t cw,
                                  char *out, size_t cap)
{
    if (!out || !cap) return 0;
    uint8_t b[19] = {0}, ram[16] = {0}, c = 0;
    float v = 0;
    int soc = -1;
    bool locked=sensors_mux_begin();
    int se = locked && s_gauge_owned && stc ? reg8_read(stc, 0, b, sizeof(b)) : ESP_ERR_INVALID_STATE;
    int re = se == ESP_OK ? reg8_read(stc, 0x20, ram, sizeof(ram)) : ESP_ERR_INVALID_STATE;
    if(locked) xSemaphoreGive(s_gauge_mutex);
    int ce = cw ? cw2015_read(cw, &v, &soc) : ESP_ERR_INVALID_STATE;
    int cm = cw ? reg8_read(cw, 0x0a, &c, 1) : ESP_ERR_INVALID_STATE;
    int ram_ok = re == ESP_OK ?
        ((ram[0] | ram[1] << 8) == 0x53a9 && sensors_diag_crc8(ram, sizeof(ram)) == 0) : -1;
    int seeded = re == ESP_OK ? (ram_ok && ram[10] == 0x5d) : -1;
    int n = snprintf(out, cap,
        "TD_BATT v=2 stc_err=%d mode=%d ctrl=%d counter=%d soc_raw=%d mv=%d current_raw=%d "
        "ocv_raw=%d cc=%d vm=%d ram_err=%d ram_ok=%d seeded=%d "
        "cw_err=%d cw_mv=%d cw_soc=%d cw_mode=%d\r\n",
        se, se == ESP_OK ? b[0] : -1, se == ESP_OK ? b[1] : -1,
        se == ESP_OK ? b[4] | b[5] << 8 : -1, se == ESP_OK ? b[2] | b[3] << 8 : -1,
        se == ESP_OK ? (int)((int16_t)(b[8] | b[9] << 8) * 2.2f) : -1,
        se == ESP_OK ? b[6] | b[7] << 8 : -1, se == ESP_OK ? b[13] | b[14] << 8 : -1,
        se == ESP_OK ? b[15] | b[16] << 8 : -1, se == ESP_OK ? b[17] | b[18] << 8 : -1,
        re, ram_ok, seeded, ce, ce == ESP_OK ? (int)(v * 1000) : -1,
        ce == ESP_OK ? soc : -1, cm == ESP_OK ? c : -1);
    if (n < 0 || (size_t)n >= cap) {
        out[0] = '\0';                  // Never emit a truncated status record.
        return 0;
    }
    return (size_t)n;
}
