// TypixDeck 0720 — ESP32-S3 / Pi 双源显示切换器 + 电源监视器
//
// 上电即并行启动（不再抑制 CM）：
//   - CM_PMIC_EN(P0_2) 保持输入 Hi-Z，R79 上拉 = CM 上电自启；
//   - AW9523 只驱动 3 根线：MUX_SEL / LCD_RST / TP_RST，其余 13 脚输入 Hi-Z
//     （严禁全端口驱高：背馈未上电 CM，CLAUDE.md 踩坑 #15）；
//   - 开机 MUX 短暂切 ESP 侧：JD9168S SPI 初始化 + GT911 INT-low 复位
//     （Pi 出图/goodix probe 在数秒后，这个窗口 Pi 无感知）；
//   - 完成后 MUX_SEL=0 交给 Pi，ESP 的 RGB 输出照常扫描（被 MUX 隔离，不冲突）。
//
// SW3（ESP BOOT 键，GPIO0）：启动后不再是 strap，当普通按钮轮询。
//   每按一次 toggle MUX_SEL：Pi 刷屏 ↔ ESP 刷屏。
//   切到 ESP 侧时显示 INA219 电压/电流遥测页（VBAT@0x40 / VBUS@0x41，
//   10mΩ 采样电阻），每 500ms 刷新。
//   注意：MUX 在 ESP 侧期间，Pi 的触摸 I2C/INT 被切断（goodix 会报错，切回恢复）。

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/i2c_master.h"

#include "board_pins.h"
#include "aw9523.h"
#include "lcd_spi_init.h"
#include "audio.h"
#include <stdarg.h>
#include "esp_system.h"   // esp_restart()
#include "soc/rtc_cntl_reg.h"
#include "tusb.h"
#include "usb_device_uac.h"

static const char *TAG = "MAIN";

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_aw9523;
static i2c_master_dev_handle_t s_ina_vbat;
static i2c_master_dev_handle_t s_ina_vbus;
static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_fb;

#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_GRAY   0x8410
#define C_LGRAY  0xC618
#define C_DARK   0x10A2   // 深灰背景
#define C_NAVY   0x0951   // 头部深蓝
#define C_GREEN  0x07E0
#define C_DGREEN 0x03E0
#define C_RED    0xF800
#define C_YELL   0xFFE0
#define C_CYAN   0x07FF

static void fb_fill_rect(int x0, int y0, int w, int h, uint16_t color)
{
    if (!s_fb) return;
    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > LCD_H_RES) w = LCD_H_RES - x0;
    if (y0 + h > LCD_V_RES) h = LCD_V_RES - y0;
    if (w <= 0 || h <= 0) return;
    for (int y = y0; y < y0 + h; y++) {
        uint16_t *row = s_fb + y * LCD_H_RES + x0;
        for (int x = 0; x < w; x++) {
            row[x] = color;
        }
    }
}

static const uint8_t *glyph5x7(char c)
{
    static const uint8_t A[] = { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
    static const uint8_t B[] = { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
    static const uint8_t C[] = { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E };
    static const uint8_t D[] = { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E };
    static const uint8_t E[] = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
    static const uint8_t F[] = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 };
    static const uint8_t G[] = { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F };
    static const uint8_t H[] = { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
    static const uint8_t I[] = { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F };
    static const uint8_t J[] = { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C };
    static const uint8_t K[] = { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
    static const uint8_t L[] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
    static const uint8_t M[] = { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 };
    static const uint8_t N[] = { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 };
    static const uint8_t O[] = { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
    static const uint8_t P[] = { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
    static const uint8_t Q[] = { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D };
    static const uint8_t R[] = { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
    static const uint8_t S[] = { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
    static const uint8_t T[] = { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
    static const uint8_t U[] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
    static const uint8_t V[] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 };
    static const uint8_t W[] = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 };
    static const uint8_t X[] = { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 };
    static const uint8_t Y[] = { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 };
    static const uint8_t Z[] = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F };
    static const uint8_t d0[] = { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E };
    static const uint8_t d1[] = { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x1F };
    static const uint8_t d2[] = { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F };
    static const uint8_t d3[] = { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E };
    static const uint8_t d4[] = { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 };
    static const uint8_t d5[] = { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E };
    static const uint8_t d6[] = { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E };
    static const uint8_t d7[] = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
    static const uint8_t d8[] = { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E };
    static const uint8_t d9[] = { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C };
    static const uint8_t dash[] = { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 };
    static const uint8_t dot[]  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C };
    static const uint8_t pct[]  = { 0x19, 0x1A, 0x02, 0x04, 0x08, 0x0B, 0x13 };
    static const uint8_t slash[] = { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 };
    static const uint8_t colon[] = { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 };
    static const uint8_t plus[] = { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 };
    static const uint8_t blank[] = { 0, 0, 0, 0, 0, 0, 0 };
    switch (c) {
    case 'A': return A; case 'B': return B; case 'C': return C; case 'D': return D;
    case 'E': return E; case 'F': return F; case 'G': return G; case 'H': return H;
    case 'I': return I; case 'J': return J; case 'K': return K; case 'L': return L;
    case 'M': return M; case 'N': return N; case 'O': return O; case 'P': return P;
    case 'Q': return Q; case 'R': return R; case 'S': return S; case 'T': return T;
    case 'U': return U; case 'V': return V; case 'W': return W; case 'X': return X;
    case 'Y': return Y; case 'Z': return Z;
    case '0': return d0; case '1': return d1; case '2': return d2; case '3': return d3;
    case '4': return d4; case '5': return d5; case '6': return d6; case '7': return d7;
    case '8': return d8; case '9': return d9;
    case '-': return dash; case '.': return dot; case '%': return pct;
    case '/': return slash; case ':': return colon; case '+': return plus;
    default: return blank;
    }
}

static void fb_draw_text(int x, int y, const char *text, int scale, uint16_t color)
{
    for (const char *p = text; *p; p++, x += 6 * scale) {
        const uint8_t *g = glyph5x7(*p);
        for (int yy = 0; yy < 7; yy++) {
            for (int xx = 0; xx < 5; xx++) {
                if (g[yy] & (1 << (4 - xx))) {
                    fb_fill_rect(x + xx * scale, y + yy * scale, scale, scale, color);
                }
            }
        }
    }
}

// 文本像素宽 = 字符数*6*scale - 尾字符后的 1 格间距
static inline int text_w(const char *text, int scale)
{
    return (int)strlen(text) * 6 * scale - scale;
}

static void fb_draw_text_centered(int y, const char *text, int scale, uint16_t color)
{
    fb_draw_text((LCD_H_RES - text_w(text, scale)) / 2, y, text, scale, color);
}

// ---------------------------------------------------------------------------
// INA219 迷你驱动（寄存器级，POR 默认配置 0x399F：32V 量程 / 12bit 连续采样）
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

static esp_err_t ina219_read(i2c_master_dev_handle_t dev, float *bus_v, float *cur_a)
{
    uint16_t raw;
    ESP_RETURN_ON_ERROR(ina219_read16(dev, INA219_REG_BUS_V, &raw), TAG, "bus_v");
    *bus_v = (float)(raw >> 3) * 0.004f;
    ESP_RETURN_ON_ERROR(ina219_read16(dev, INA219_REG_SHUNT_V, &raw), TAG, "shunt_v");
    *cur_a = (int16_t)raw * 0.00001f / INA219_SHUNT_OHM;
    return ESP_OK;
}

// CW2015 电量计 @0x62（常连 ESP 总线）：VCELL 14bit LSB 305µV，SOC 整数 %
static esp_err_t cw2015_read(i2c_master_dev_handle_t dev, float *v, int *soc)
{
    uint8_t b[2];
    ESP_RETURN_ON_ERROR(reg8_read(dev, 0x02, b, 2), TAG, "cw vcell");
    *v = (float)((((uint16_t)b[0] & 0x3F) << 8) | b[1]) * 305e-6f;
    ESP_RETURN_ON_ERROR(reg8_read(dev, 0x04, b, 2), TAG, "cw soc");
    *soc = b[0];
    return ESP_OK;
}

// STC3117 电量计 @0x70（⚠️ 在 MUX U71 后面，仅 MUX=ESP 侧时可达——
// 遥测页恰好只在 ESP 持屏时刷新，天然满足）：V LSB 2.20mV，SOC LSB 1/512%
static esp_err_t stc3117_read(i2c_master_dev_handle_t dev, float *v, float *soc)
{
    uint8_t b[2];
    ESP_RETURN_ON_ERROR(reg8_read(dev, 0x08, b, 2), TAG, "stc v");
    *v = (float)(int16_t)(b[0] | (b[1] << 8)) * 2.20e-3f;
    ESP_RETURN_ON_ERROR(reg8_read(dev, 0x02, b, 2), TAG, "stc soc");
    *soc = (float)(uint16_t)(b[0] | (b[1] << 8)) / 512.0f;
    return ESP_OK;
}

static i2c_master_dev_handle_t s_cw2015;
static i2c_master_dev_handle_t s_stc3117;

static i2c_master_dev_handle_t sensor_add(uint8_t addr)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t dev = NULL;
    if (i2c_master_bus_add_device(s_i2c_bus, &cfg, &dev) != ESP_OK) {
        ESP_LOGE(TAG, "0x%02X add_device 失败", addr);
    }
    return dev;
}

static void sensors_start(void)
{
    s_ina_vbat = sensor_add(INA219_VBAT_ADDR);
    s_ina_vbus = sensor_add(INA219_VBUS_ADDR);
    s_cw2015   = sensor_add(0x62);
    s_stc3117  = sensor_add(0x70);
    if (s_cw2015) {
        // CW2015 POR 后可能在 sleep，写 MODE(0x0A)=0x00 唤醒
        uint8_t wake[2] = { 0x0A, 0x00 };
        i2c_master_transmit(s_cw2015, wake, 2, 100);
    }
}

// 板上 I2C 器件在位探测（名字 + 主/备地址；0 表示无备用地址）
typedef struct { const char *name; uint8_t addr, alt; } sensor_desc_t;
static const sensor_desc_t k_sensors[] = {
    { "AW9523",  0x5B, 0    },  // U16 IO 扩展器
    { "INA-BAT", 0x40, 0    },  // U4  电池电流计
    { "INA-BUS", 0x41, 0    },  // U20 USB 电流计
    { "CW2015",  0x62, 0    },  // U27 电量计
    { "STC3117", 0x70, 0    },  // U53 电量计（MUX 后）
    { "QMI8658", 0x6A, 0x6B },  // U6  IMU
    { "RX8130",  0x32, 0    },  // U59 RTC
    { "ES8389",  0x10, 0x11 },  // U12 Codec
    { "GT911",   0x5D, 0x14 },  // 触摸（MUX 后）
};
#define N_SENSORS (sizeof(k_sensors) / sizeof(k_sensors[0]))

// ---------------------------------------------------------------------------
// 遥测页 UI
// ---------------------------------------------------------------------------
// 卡片：深黑底 + 左侧彩色竖条 + 标题
static void ui_card(int x, int y, int w, int h, uint16_t accent, const char *title)
{
    fb_fill_rect(x, y, w, h, C_BLACK);
    fb_fill_rect(x, y, 8, h, accent);
    fb_draw_text(x + 30, y + 16, title, 3, accent);
}

static void ui_draw_telemetry(uint32_t uptime_s)
{
    char buf[64];
    float vbat_v = 0, vbat_a = 0, vbus_v = 0, vbus_a = 0;
    float cw_v = 0, stc_v = 0, stc_soc = -1;
    int cw_soc = -1;
    bool vbat_ok = s_ina_vbat && ina219_read(s_ina_vbat, &vbat_v, &vbat_a) == ESP_OK;
    bool vbus_ok = s_ina_vbus && ina219_read(s_ina_vbus, &vbus_v, &vbus_a) == ESP_OK;
    if (!s_cw2015 || cw2015_read(s_cw2015, &cw_v, &cw_soc) != ESP_OK) cw_soc = -1;
    if (!s_stc3117 || stc3117_read(s_stc3117, &stc_v, &stc_soc) != ESP_OK) stc_soc = -1;

    fb_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, C_DARK);

    // ---- 头部 ----
    fb_fill_rect(0, 0, LCD_H_RES, 110, C_NAVY);
    fb_fill_rect(0, 110, LCD_H_RES, 4, C_GREEN);
    fb_draw_text_centered(22, "TYPIXDECK", 7, C_WHITE);
    fb_draw_text_centered(128, "ESP32-S3 SYSTEM MONITOR", 3, C_GRAY);

    // ---- 电池卡片 ----
    int soc = cw_soc >= 0 ? cw_soc : (int)stc_soc;   // 主 SOC 优先 CW2015
    uint16_t soc_col = soc < 0   ? C_GRAY
                     : soc < 15  ? C_RED
                     : soc < 40  ? C_YELL : C_GREEN;
    ui_card(40, 170, 944, 240, C_YELL, "BATTERY");
    if (soc >= 0) {
        snprintf(buf, sizeof(buf), "%d%%", soc);
        fb_draw_text(80, 220, buf, 11, soc_col);
    } else {
        fb_draw_text(80, 240, "--%", 9, C_GRAY);
    }
    // 电量条
    {
        int bx = 80, by = 330, bw = 320, bh = 40;
        fb_fill_rect(bx - 3, by - 3, bw + 6, bh + 6, C_LGRAY);
        fb_fill_rect(bx, by, bw, bh, C_DARK);
        if (soc > 0) fb_fill_rect(bx, by, bw * soc / 100, bh, soc_col);
    }
    if (vbat_ok) {
        snprintf(buf, sizeof(buf), "%.3f V", vbat_v);
        fb_draw_text(490, 215, buf, 6, C_WHITE);
        snprintf(buf, sizeof(buf), "%+.0f MA  %.2f W", vbat_a * 1000.0f, vbat_v * vbat_a);
        fb_draw_text(490, 280, buf, 4, C_LGRAY);
        // 正电流=放电（INA219 IN+=VBAT，IN-=负载侧）
        const char *st = vbat_a > 0.02f ? "DISCHARGING"
                       : vbat_a < -0.02f ? "CHARGING" : "IDLE";
        fb_draw_text(490, 335, st,
                     4, vbat_a < -0.02f ? C_GREEN : (vbat_a > 0.02f ? C_YELL : C_GRAY));
    } else {
        fb_draw_text(490, 260, "INA219 READ FAIL", 4, C_RED);
    }
    // 两颗电量计交叉读数
    if (cw_soc >= 0)
        snprintf(buf, sizeof(buf), "CW2015 %.3fV %d%%", cw_v, cw_soc);
    else
        snprintf(buf, sizeof(buf), "CW2015 --");
    fb_draw_text(80, 382, buf, 2, C_GRAY);
    if (stc_soc >= 0)
        snprintf(buf, sizeof(buf), "STC3117 %.3fV %.1f%%", stc_v, stc_soc);
    else
        snprintf(buf, sizeof(buf), "STC3117 --");
    fb_draw_text(490, 382, buf, 2, C_GRAY);

    // ---- USB 供电卡片 ----
    ui_card(40, 430, 944, 120, C_CYAN, "USB VBUS");
    if (vbus_ok) {
        snprintf(buf, sizeof(buf), "%.3f V", vbus_v);
        fb_draw_text(80, 480, buf, 5, C_WHITE);
        snprintf(buf, sizeof(buf), "%.0f MA", vbus_a * 1000.0f);
        fb_draw_text(430, 480, buf, 5, C_LGRAY);
        snprintf(buf, sizeof(buf), "%.2f W", vbus_v * vbus_a);
        fb_draw_text(720, 480, buf, 5, C_LGRAY);
    } else {
        fb_draw_text(80, 480, "INA219 READ FAIL", 4, C_RED);
    }

    // ---- 传感器在位卡片 ----
    int n_ok = 0;
    bool present[N_SENSORS];
    for (int i = 0; i < N_SENSORS; i++) {
        present[i] = i2c_master_probe(s_i2c_bus, k_sensors[i].addr, 50) == ESP_OK ||
                     (k_sensors[i].alt &&
                      i2c_master_probe(s_i2c_bus, k_sensors[i].alt, 50) == ESP_OK);
        if (present[i]) n_ok++;
    }
    snprintf(buf, sizeof(buf), "SENSORS %d/%d", n_ok, (int)N_SENSORS);
    ui_card(40, 570, 944, 130, C_GREEN, buf);
    for (int i = 0; i < N_SENSORS; i++) {
        int col = i % 5, row = i / 5;
        int x = 80 + col * 182, y = 620 + row * 40;
        fb_fill_rect(x, y + 2, 12, 12, present[i] ? C_GREEN : C_RED);
        fb_draw_text(x + 24, y, k_sensors[i].name, 2,
                     present[i] ? C_LGRAY : C_GRAY);
    }

    // ---- 底部 ----
    snprintf(buf, sizeof(buf), "UPTIME %lu S   PRESS SW3 - BACK TO PI",
             (unsigned long)uptime_s);
    fb_draw_text_centered(725, buf, 3, C_YELL);

    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_H_RES, LCD_V_RES, s_fb);
}

// ---------------------------------------------------------------------------
// 外设初始化
// ---------------------------------------------------------------------------
static esp_err_t i2c_start(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus), TAG, "i2c_new_master_bus");
    ESP_RETURN_ON_ERROR(aw9523_init(s_i2c_bus, &s_aw9523), TAG, "aw9523_init");
    return ESP_OK;
}

static esp_err_t lcd_reset_and_spi_init(void)
{
    ESP_RETURN_ON_ERROR(aw9523_update_bits(s_aw9523, AW9523_REG_OUTPUT_P1,
                                           AW9523_P1_LCD_RST, 0),
                        TAG, "lcd_rst_low");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(aw9523_update_bits(s_aw9523, AW9523_REG_OUTPUT_P1,
                                           AW9523_P1_LCD_RST, AW9523_P1_LCD_RST),
                        TAG, "lcd_rst_high");
    vTaskDelay(pdMS_TO_TICKS(120));
    return lcd_jd9168s_spi_init();
}

static esp_err_t rgb_panel_start(void)
{
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_PLL240M,
        .timings = {
            .pclk_hz = LCD_PCLK_HZ,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_front_porch = LCD_HFP,
            .hsync_pulse_width = LCD_HSYNC_W,
            .hsync_back_porch = LCD_HBP,
            .vsync_front_porch = LCD_VFP,
            .vsync_pulse_width = LCD_VSYNC_W,
            .vsync_back_porch = LCD_VBP,
            .flags = {
                .hsync_idle_low = 0,
                .vsync_idle_low = 0,
                .de_idle_high = 0,
                .pclk_active_neg = 0,
            },
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 1,
        .bounce_buffer_size_px = LCD_H_RES * 16,
        .hsync_gpio_num = PIN_LCD_HSYNC,
        .vsync_gpio_num = PIN_LCD_VSYNC,
        .de_gpio_num = PIN_LCD_DE,
        .pclk_gpio_num = PIN_LCD_PCLK,
        .disp_gpio_num = -1,
        .data_gpio_nums = {
            PIN_LCD_B3, PIN_LCD_B4, PIN_LCD_B5, PIN_LCD_B6, PIN_LCD_B7,
            PIN_LCD_G2, PIN_LCD_G3, PIN_LCD_G4, PIN_LCD_G5, PIN_LCD_G6, PIN_LCD_G7,
            PIN_LCD_R3, PIN_LCD_R4, PIN_LCD_R5, PIN_LCD_R6, PIN_LCD_R7,
        },
        .flags = {
            .fb_in_psram = 1,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&cfg, &s_panel), TAG, "new_rgb_panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel_reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel_init");

    s_fb = heap_caps_malloc(LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_fb) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "RGB 面板已启动，framebuffer=%d KB", LCD_H_RES * LCD_V_RES * 2 / 1024);
    return ESP_OK;
}

static void backlight_on(void)
{
    gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << PIN_LCD_BL,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&bl_cfg);
    gpio_set_level(PIN_LCD_BL, 1);
}

static void boot_btn_start(void)
{
    // GPIO0 启动后不再是 strap；外部 R1 上拉，SW3 按下=低
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_BOOT_BTN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&cfg);
}

// MUX 切换：P0_0 保持推挽输出，1=ESP 侧，0=Pi 侧（与 R105 下拉同电平）
static esp_err_t mux_select(bool esp_side)
{
    return aw9523_update_bits(s_aw9523, AW9523_REG_OUTPUT_P0,
                              AW9523_P0_MUX_SEL,
                              esp_side ? AW9523_P0_MUX_SEL : 0);
}

// ---------------------------------------------------------------------------
// USB UAC + CDC
// ---------------------------------------------------------------------------
// UAC: 主机 PCM → ES8389 codec
static esp_err_t uac_output_cb(uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;
    esp_codec_dev_handle_t codec = audio_codec_handle();
    if (codec) {
        esp_codec_dev_write(codec, buf, len);
    }
    return ESP_OK;
}
static void uac_set_mute_cb(uint32_t mute, void *ctx)
{
    (void)ctx;
    esp_codec_dev_set_out_mute(audio_codec_handle(), (bool)mute);
}
static void uac_set_volume_cb(uint32_t volume, void *ctx)
{
    (void)ctx;
    int vol = (int)volume; if (vol > 100) vol = 100;
    esp_codec_dev_set_out_vol(audio_codec_handle(), vol);
}

// CDC 调试输出（主机可见的 printf）
void cdc_printf(const char *fmt, ...)
{
    if (!tud_cdc_connected()) return;
    char buf[160];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        tud_cdc_write(buf, n > (int)sizeof(buf) ? (int)sizeof(buf) : n);
        tud_cdc_write_flush();
    }
}

// 写 RTC FORCE_DOWNLOAD_BOOT + 复位 → ROM 进下载模式（esptool 无按钮刷机）
// 借鉴 firmware/cm3_usb_wifi_dongle/main/CLI_Commands.c 的 download 命令
static void reboot_to_download(void)
{
    cdc_printf("\r\n>>> Now switch to BOOT (download) mode. Run esptool to flash.\r\n");
    vTaskDelay(pdMS_TO_TICKS(60));   // 让 CDC 把提示发出去再死
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}

// CDC RX 回调：按行扫描 "REBOOT_TO_BOOT_MODE" 魔串
void tud_cdc_rx_cb(uint8_t itf)
{
    (void)itf;
    static char line[80];
    static int len = 0;
    while (tud_cdc_available()) {
        char c;
        if (tud_cdc_read(&c, 1) == 0) break;
        // 简单回显
        if (c != '\r' && c != '\n') {
            if (tud_cdc_connected()) { tud_cdc_write(&c, 1); tud_cdc_write_flush(); }
        }
        if (c == '\r' || c == '\n') {
            line[len] = 0;
            if (strstr(line, "REBOOT_TO_BOOT_MODE")) {
                reboot_to_download();   // 不返回
            } else if (len > 0) {
                cdc_printf("\r\n[echo] %s  (send REBOOT_TO_BOOT_MODE to enter flash mode)\r\n", line);
            }
            len = 0;
        } else if (len < (int)sizeof(line) - 1) {
            line[len++] = c;
        }
    }
}

// audio(ES8389) + USB(UAC+CDC) 初始化放独立任务跑：esp_codec_dev + tusb_init
// 调用栈深，app_main 的 8KB 栈会溢出 → 崩溃重启循环（lcd_mp3 也是独立任务跑的）
static void audio_usb_task(void *arg)
{
    i2c_master_bus_handle_t bus = (i2c_master_bus_handle_t)arg;

    audio_start(bus);

    // AS_PART：必须给 spk_itf_num（AC=itf0, AS_spk=itf1），否则 uac 驱动用野值
    uac_device_config_t uac_cfg = {
        .skip_tinyusb_init = false,
        .output_cb      = uac_output_cb,
        .input_cb        = NULL,
        .set_mute_cb     = uac_set_mute_cb,
        .set_volume_cb   = uac_set_volume_cb,
        .cb_ctx          = NULL,
        .spk_itf_num     = 1,
        .mic_itf_num     = -1,
    };
    if (uac_device_init(&uac_cfg) == ESP_OK) {
        ESP_LOGI(TAG, "USB UAC+CDC 已启动，主机插上线即识别为声卡+CDC串口");
        cdc_printf("\r\n=== TypixDeck UAC+CDC ready ===\r\n"
                   "UAC: 48kHz/16bit/stereo -> ES8389\r\n"
                   "CDC: send 'REBOOT_TO_BOOT_MODE' to enter flash mode\r\n");
    } else {
        ESP_LOGE(TAG, "USB UAC 初始化失败");
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "TypixDeck dual-source display switch + power monitor");

    if (i2c_start() != ESP_OK) {
        ESP_LOGE(TAG, "I2C/AW9523 初始化失败，停止");
        return;
    }
    // MUX 此刻在 ESP 侧（aw9523_init 置 MUX_SEL=1）：完成面板 SPI 初始化
    if (lcd_reset_and_spi_init() != ESP_OK) {
        ESP_LOGE(TAG, "LCD SPI 初始化失败，停止");
        return;
    }
    // GT911 INT-low 干净复位（best-effort，此窗口 INT/SDA/SCL 走 ESP 侧可达；
    // 失败不阻塞——Pi 侧触摸可能异常但显示链路不受影响）
    esp_err_t err = aw9523_gt911_reset(s_aw9523);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GT911 复位失败（%s），Pi 侧触摸可能异常", esp_err_to_name(err));
    }

    if (rgb_panel_start() != ESP_OK) {
        ESP_LOGE(TAG, "RGB 面板启动失败，停止");
        return;
    }
    backlight_on();
    sensors_start();
    ui_draw_telemetry(0);

    // 显式开 ES8389 模拟电源：DAC_3V3_EN(P1_0) 推挽驱高（R84 上拉对 U31 不够稳）
    aw9523_update_bits(s_aw9523, AW9523_REG_OUTPUT_P1, AW9523_P1_DAC_3V3_EN, AW9523_P1_DAC_3V3_EN);
    aw9523_update_bits(s_aw9523, AW9523_REG_CONFIG_P1, AW9523_P1_DAC_3V3_EN, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(TAG, "DAC_3V3_EN(P1_0) 已驱高，ES8389 上电");

    // LCD SPI 初始化已完成 → GPIO47/48 现在重配成 I2S。
    // audio(ES8389) + USB(UAC+CDC) 放独立 16KB 任务跑（栈深，避免 app_main 8KB 溢出）
    xTaskCreate(audio_usb_task, "audio_usb", 16384, s_i2c_bus, 5, NULL);

    // 默认交给 Pi（CM 与 ESP 同时上电，Pi 数秒后出图）
    bool esp_owns = false;
    if (mux_select(false) != ESP_OK) {
        ESP_LOGE(TAG, "MUX 切 Pi 失败");
    }
    ESP_LOGI(TAG, "初始化完成：MUX=Pi 侧，按 SW3(BOOT) 切换 ESP 遥测页");

    boot_btn_start();

    int prev_lvl = 1;
    int64_t last_draw_ms = 0;
    while (1) {
        int lvl = gpio_get_level(PIN_BOOT_BTN);
        int64_t now_ms = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (prev_lvl == 1 && lvl == 0) {          // 按下沿（50ms 轮询自带消抖）
            esp_owns = !esp_owns;
            ESP_LOGI(TAG, "SW3 按下 → 屏幕切到 %s", esp_owns ? "ESP" : "Pi");
            if (esp_owns) {
                ui_draw_telemetry(now_ms / 1000);  // 先备好画面再切 MUX
                last_draw_ms = now_ms;
            }
            if (mux_select(esp_owns) != ESP_OK) {
                ESP_LOGE(TAG, "MUX 切换失败");
            }
        }
        prev_lvl = lvl;

        if (esp_owns && now_ms - last_draw_ms >= 500) {
            ui_draw_telemetry(now_ms / 1000);
            last_draw_ms = now_ms;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
