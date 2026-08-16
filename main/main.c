// TypixDeck 0720 — ESP32-S3 / Pi 双源显示切换器 + 电源监视器 + 本地 GUI
//
// 上电即并行启动（不再抑制 CM）：
//   - CM_PMIC_EN(P0_2) 保持输入 Hi-Z，R79 上拉 = CM 上电自启；
//   - AW9523 只驱动 3 根线：MUX_SEL / LCD_RST / TP_RST，其余 13 脚输入 Hi-Z
//     （严禁全端口驱高：背馈未上电 CM，CLAUDE.md 踩坑 #15）；
//   - 开机 MUX 保持 ESP 侧：JD9168S SPI 初始化 + GT911 INT-low 复位 +
//     GT911 触摸初始化，然后播放开机动画（TYPIXDECK + 旋转 spinner）；
//   - VSYNC 探测（vsync_mon）：Pi GPIO2(DPI VSYNC)→R83→AW9523 P0_7 中断→
//     INTN→GPIO5。一旦探测到稳定信号立即切 MUX 交给 Pi，不再自动切回。
//
// SW3（ESP BOOT 键，GPIO0）/ 键盘 □ 键：Pi 刷屏 ↔ ESP 本地 GUI。
//   ESP GUI 是 4 页 Tab（触摸切换）：DASH 遥测 / BATT 电池曲线 /
//   TOUCH 触摸测试 / PI SIG 信号状态，右上角常驻 Pi FPS 状态芯片。
//   注意：MUX 在 ESP 侧期间，Pi 的触摸 I2C/INT 被切断（goodix 会报错，切回恢复）。

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"

#include "board_pins.h"
#include "aw9523.h"
#include "gt911.h"
#include "lcd_spi_init.h"
#include "sensors.h"
#include "batt_log.h"
#include "vsync_mon.h"
#include "ui.h"
#include "audio.h"
#include "usb_device_uac.h"
#include "uac_dbg.h"
#include "tusb.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_system.h"

static const char *TAG = "MAIN";

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_aw9523;
static i2c_master_dev_handle_t s_ina_vbat;
static i2c_master_dev_handle_t s_ina_vbus;
static i2c_master_dev_handle_t s_cw2015;
static i2c_master_dev_handle_t s_stc3117;
static i2c_master_dev_handle_t s_gt911;
static esp_lcd_panel_handle_t s_panel;

// ---------------------------------------------------------------------------
// 传感器句柄
// ---------------------------------------------------------------------------
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
        cw2015_wake(s_cw2015);
    }
    // 开机 GT911 复位窗口期间 MUX 在 ESP 侧，顺手启动 STC3117；
    // 若此刻 MUX 已归还 Pi 侧则静默失败，由遥测页刷新时兜底重试
    stc3117_ensure_running(s_stc3117);
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
    ESP_LOGI(TAG, "RGB 面板已启动");
    return ESP_OK;
}

// LCD 背光：U55 SY7201ABC 升压驱动，EN 脚 PWM 调光（GPIO21 → U70 MUX →
// LCD_BL）。⚠️ 这条线走 MUX：ESP 持屏时才由本 PWM 控制，Pi 持屏时由
// Pi GPIO18 硬件 PWM 控制（两侧各自记档位）。10kHz 在 SY7201 EN 调光
// 频率范围内，且档位 1（10%）的低电平脉宽 90µs 远小于关断阈值 ~2.5ms。
#define BL_LEVELS        10
#define BL_LEDC_TIMER    LEDC_TIMER_0
#define BL_LEDC_CHANNEL  LEDC_CHANNEL_0

static volatile int s_bl_level = BL_LEVELS;   // 1..10，默认最亮

static void backlight_apply(void)
{
    uint32_t duty = (uint32_t)s_bl_level * ((1 << 10) - 1) / BL_LEVELS;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
}

static void backlight_on(void)
{
    ledc_timer_config_t tcfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = BL_LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 10000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&tcfg);
    ledc_channel_config_t ccfg = {
        .gpio_num = PIN_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BL_LEDC_CHANNEL,
        .timer_sel = BL_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&ccfg);
    backlight_apply();
}

// ◯/✤ 键：亮度加/减一档（1..BL_LEVELS 饱和，与 Pi 侧标准亮度键行为一致），
// 返回新档位
static int backlight_adjust(int dir)
{
    int lv = s_bl_level + dir;
    if (lv < 1) lv = 1;
    if (lv > BL_LEVELS) lv = BL_LEVELS;
    s_bl_level = lv;
    backlight_apply();
    return s_bl_level;
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

// MUX 期望归属（aw9523_init 后在 ESP 侧）。AW9523 健康检查在自愈重建后按此
// 恢复 MUX，所以 mux_select 即使当下写失败，总线恢复后 ~1s 内也会被纠正。
static volatile bool s_mux_esp_side = true;

// MUX 切换：P0_0 保持推挽输出，1=ESP 侧，0=Pi 侧（与 R105 下拉同电平）。
// I2C 瞬时失败（ESD 毛刺）重试 3 次——这条写失败会导致屏幕归属卡死，值得抢救。
static esp_err_t mux_select(bool esp_side)
{
    s_mux_esp_side = esp_side;
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 3; i++) {
        err = aw9523_update_bits(s_aw9523, AW9523_REG_OUTPUT_P0,
                                 AW9523_P0_MUX_SEL,
                                 esp_side ? AW9523_P0_MUX_SEL : 0);
        if (err == ESP_OK) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGE(TAG, "MUX 写入失败（重试 3 次）：%s，等健康检查自愈", esp_err_to_name(err));
    return err;
}

// ---------------------------------------------------------------------------
// KeebDeck 键盘 I2C 从机（U32 STM32F042 QMK @0x1F，实现见
// firmware/keebdeck_6r11c/i2c_slave_kbd.c）：
//   reg 0x00=ID(0x6B)  reg 0x01=FIFO 计数  reg 0x02=弹出事件（连续读连续弹）
//   事件字节：bit7=按下, bit6:4=行, bit3:0=列；0x00=FIFO 空
// 键盘常供电、SCL/SDA 恒挂 ESP 总线（不走 MUX），Pi 持屏期间照样收键。
// □ 键 =(0,1)：按下请求切屏（Pi ↔ ESP GUI），与 SW3 等价。
// ---------------------------------------------------------------------------
#define KBD_I2C_ADDR       0x1F
#define KBD_REG_FIFO_COUNT 0x01
#define KBD_REG_FIFO_POP   0x02

static i2c_master_dev_handle_t s_kbd;
static volatile bool s_kbd_toggle_req = false;

// 键盘事件回显文本：kbd_task 生产，cdc_stats_task 每秒冲刷到 CDC
//（遵守并发纪律：tud_cdc_write 只出现在 cdc_stats_task）
static char s_kbd_log[256];
static size_t s_kbd_log_len = 0;
static portMUX_TYPE s_kbd_log_mux = portMUX_INITIALIZER_UNLOCKED;

static void kbd_log_append(const char *s)
{
    size_t n = strlen(s);
    portENTER_CRITICAL(&s_kbd_log_mux);
    if (s_kbd_log_len + n < sizeof(s_kbd_log)) {
        memcpy(s_kbd_log + s_kbd_log_len, s, n);
        s_kbd_log_len += n;
    }
    portEXIT_CRITICAL(&s_kbd_log_mux);
}

// cdc_stats_task 专用：取走并清空回显缓冲，返回取到的字节数
static size_t kbd_log_take(char *dst, size_t cap)
{
    portENTER_CRITICAL(&s_kbd_log_mux);
    size_t n = s_kbd_log_len < cap ? s_kbd_log_len : cap;
    memcpy(dst, s_kbd_log, n);
    s_kbd_log_len = 0;
    portEXIT_CRITICAL(&s_kbd_log_mux);
    return n;
}

static void kbd_task(void *arg)
{
    (void)arg;
    bool online = false;
    while (1) {
        uint8_t reg = KBD_REG_FIFO_COUNT;
        uint8_t cnt = 0;
        esp_err_t err = i2c_master_transmit_receive(s_kbd, &reg, 1, &cnt, 1, 50);
        if (err != ESP_OK) {
            if (online) {
                online = false;
                ESP_LOGW(TAG, "键盘 I2C 掉线");
                kbd_log_append("[KBD offline]\r\n");
            }
            vTaskDelay(pdMS_TO_TICKS(500));   // 离线（如键盘在 DFU）降频重试
            continue;
        }
        if (!online) {
            online = true;
            ESP_LOGI(TAG, "键盘 I2C 在线 @0x%02X", KBD_I2C_ADDR);
            kbd_log_append("[KBD online]\r\n");
        }
        if (cnt > 0) {
            uint8_t ev[16];
            if (cnt > sizeof(ev)) cnt = sizeof(ev);
            reg = KBD_REG_FIFO_POP;
            if (i2c_master_transmit_receive(s_kbd, &reg, 1, ev, cnt, 50) == ESP_OK) {
                for (int i = 0; i < cnt; i++) {
                    if (ev[i] == 0x00) continue;   // 竞态下 FIFO 提前抽干
                    bool pressed = ev[i] & 0x80;
                    int row = (ev[i] >> 4) & 0x07;
                    int col = ev[i] & 0x0F;
                    char line[32];
                    snprintf(line, sizeof(line), "KEY r%d c%d %s\r\n",
                             row, col, pressed ? "DOWN" : "UP");
                    kbd_log_append(line);
                    ESP_LOGI(TAG, "键盘事件 r%d c%d %s", row, col, pressed ? "按下" : "抬起");
                    if (pressed && row == 0 && col == 1) {
                        s_kbd_toggle_req = true;   // □ 键 → 切屏
                    }
                    if (pressed && row == 0 && (col == 7 || col == 8)) {
                        // ◯ (0,7)=亮一档 / ✤ (0,8)=暗一档 → ESP 侧 GPIO21
                        // LEDC（仅 ESP 持屏时背光归 ESP；Pi 持屏时同两键发
                        // 标准亮度键码，由 Pi 调自己的 GPIO18 PWM）
                        int lv = backlight_adjust(col == 7 ? +1 : -1);
                        ESP_LOGI(TAG, "背光档位 → %d/%d", lv, BL_LEVELS);
                        snprintf(line, sizeof(line), "[BL %d/%d]\r\n", lv, BL_LEVELS);
                        kbd_log_append(line);
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

// ---------------------------------------------------------------------------
// USB UAC + CDC composite（CDC = 包统计仪表 + REBOOT_TO_BOOT_MODE 魔串刷机）
//
// ⚠️ 并发纪律（历史教训：tinyusb 任务回调里调 cdc_printf → 重入死锁，
//    控制传输全 STALL，见 HANDOFF_UAC_AUDIO.md 踩坑 #9）：
//    - tinyusb 任务 / ISR 上下文（uac 回调、tud_cdc_rx_cb）只写计数器/标志位；
//    - 所有 tud_cdc_write 集中在独立的 cdc_stats_task 里做（tinyusb 的 FIFO
//      带 FreeRTOS 互斥，跨任务写入是官方支持路径）。
// ---------------------------------------------------------------------------
// UAC: 主机 PCM → ES8389 codec
static volatile uint32_t s_pcm_calls = 0, s_pcm_bytes = 0;
static esp_err_t uac_output_cb(uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;
    if (s_pcm_calls == 0) {
        ESP_LOGI(TAG, "FIRST PCM arrived len=%d (host is streaming)", (int)len);
    }
    s_pcm_calls++;
    s_pcm_bytes += len;
    esp_codec_dev_handle_t codec = audio_codec_handle();
    if (codec) {
        esp_codec_dev_write(codec, buf, len);
    }
    if ((s_pcm_calls % 1000) == 0) {
        ESP_LOGI(TAG, "PCM #%lu calls %lu bytes",
                 (unsigned long)s_pcm_calls, (unsigned long)s_pcm_bytes);
    }
    return ESP_OK;
}
// UAC mic: ES8389 ADC（双 MEMS 麦 MIC3/MIC4）→ 主机。
// 组件 usb_mic_task 每 MIC_INTERVAL_MS 调一次：len = 主机要的字节数，
// esp_codec_dev_read 阻塞到读满（I2S DMA 节奏天然按 48k 走），写 *bytes_read。
static volatile uint32_t s_mic_calls = 0, s_mic_bytes = 0;

// 双声道 RMS 统计（设备端自证左右声道是否都有数据——区分"codec 没出右声道"
// 还是"主机侧丢"）。input_cb 跑在 usb_mic_task 任务上下文，可以做整数乘加；
// 消费方（CDC 统计/HP 抢屏页）读走快照后清零，读撕裂无碍（仅调试仪表）。
static volatile uint64_t s_mic_sumsq[2] = { 0, 0 };
static volatile uint32_t s_mic_nsamp = 0;

// 最近 1 秒窗口的双声道 RMS 快照（cdc_stats_task 每秒 take 一次刷新；
// HP 抢屏页直接读快照，不自己清累计器）
static volatile float s_mic_rms[2] = { 0, 0 };

// 取两声道 RMS（原始 int16 幅度）并清零累计器，同时刷新全局快照
static void mic_rms_take(float rms[2])
{
    uint64_t sl = s_mic_sumsq[0], sr = s_mic_sumsq[1];
    uint32_t n = s_mic_nsamp;
    s_mic_sumsq[0] = 0; s_mic_sumsq[1] = 0; s_mic_nsamp = 0;
    rms[0] = n ? sqrtf((float)(sl / n)) : 0;
    rms[1] = n ? sqrtf((float)(sr / n)) : 0;
    s_mic_rms[0] = rms[0];
    s_mic_rms[1] = rms[1];
}

static esp_err_t uac_input_cb(uint8_t *buf, size_t len, size_t *bytes_read, void *ctx)
{
    (void)ctx;
    esp_codec_dev_handle_t codec = audio_codec_in_handle();
    if (!codec) {
        memset(buf, 0, len);
        *bytes_read = len;
        return ESP_OK;
    }
    if (s_mic_calls == 0) {
        ESP_LOGI(TAG, "FIRST MIC read len=%d (host is capturing)", (int)len);
    }
    int ret = esp_codec_dev_read(codec, buf, len);
    if (ret != ESP_CODEC_DEV_OK) {
        memset(buf, 0, len);   // 读失败发静音，不断流
    }
    // 奇偶采样分离统计（interleaved L/R int16）
    const int16_t *pcm = (const int16_t *)buf;
    size_t frames = len / 4;   // 2ch × 2B
    uint64_t sl = 0, sr = 0;
    for (size_t i = 0; i < frames; i++) {
        int32_t l = pcm[2 * i], r = pcm[2 * i + 1];
        sl += (uint64_t)(l * l);
        sr += (uint64_t)(r * r);
    }
    s_mic_sumsq[0] += sl;
    s_mic_sumsq[1] += sr;
    s_mic_nsamp += frames;
    *bytes_read = len;
    s_mic_calls++;
    s_mic_bytes += len;
    return ESP_OK;
}
static void uac_set_mute_cb(uint32_t mute, void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "host set mute=%lu", (unsigned long)mute);
    esp_codec_dev_set_out_mute(audio_codec_handle(), (bool)mute);
}
static void uac_set_volume_cb(uint32_t volume, void *ctx)
{
    (void)ctx;
    int vol = (int)volume; if (vol > 100) vol = 100;
    ESP_LOGI(TAG, "host set volume=%lu -> codec %d", (unsigned long)volume, vol);
    esp_codec_dev_set_out_vol(audio_codec_handle(), vol);
}

// ---------------------------------------------------------------------------
// CDC：魔串刷机 + 1Hz 包统计输出
// ---------------------------------------------------------------------------
static volatile bool s_reboot_to_boot = false;   // rx_cb 置位，stats 任务执行
static volatile bool s_audio_dump = false;       // AUDIO_DUMP：打印功放/耳机/ES8389 寄存器
static volatile int  s_amp_force = 0;            // AMP_ON=1 / AMP_OFF=-1（诊断用，stats 任务消费后清零）

// HP_DET 插拔事件（hp_amp_task 生产，两处消费）：
//   - cdc_stats_task 打印 "HP_DET changed: x->y"（s_hp_cdc_event 消费后清零）；
//   - app_main 主循环抢屏 2 秒（s_hp_grab_event 消费后清零）。
// raw 电平不预设极性（bit7 原始值 0/1），极性猜测只做展示标注"待确认"。
static volatile int      s_hp_cdc_event = 0;     // 0=无, 1=raw 0->1, 2=raw 1->0
static volatile int      s_hp_grab_event = 0;    // 同上（独立消费，互不干扰）
static volatile uint32_t s_hp_event_ms = 0;      // 事件时间戳（tick ms）

// tinyusb 任务上下文：只收集行、置标志，不调任何 CDC 写 API
void tud_cdc_rx_cb(uint8_t itf)
{
    (void)itf;
    static char line[64];
    static size_t pos = 0;
    while (tud_cdc_available()) {
        char c;
        if (tud_cdc_read(&c, 1) == 0) break;
        if (c == '\r' || c == '\n') {
            line[pos] = '\0';
            if (pos && strstr(line, "REBOOT_TO_BOOT_MODE")) {
                s_reboot_to_boot = true;
            } else if (pos && strstr(line, "AUDIO_DUMP")) {
                s_audio_dump = true;
            } else if (pos && strstr(line, "AMP_ON")) {
                s_amp_force = 1;
            } else if (pos && strstr(line, "AMP_OFF")) {
                s_amp_force = -1;
            }
            pos = 0;
        } else if (pos < sizeof(line) - 1) {
            line[pos++] = c;
        }
    }
}

static void amp_power(bool on);        // 前向声明（诊断魔串用）
static bool headphone_plugged(void);

// 诊断输出：仅在 cdc_stats_task 上下文调用（I2C 驱动带锁，跨任务安全）
static void cdc_audio_dump(void)
{
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "\r\n--- AUDIO_DUMP ---\r\nHP_DET plugged=%d\r\n",
                     headphone_plugged() ? 1 : 0);
    tud_cdc_write(buf, (uint32_t)n);
    // ES8389 关键寄存器：0x00 复位 / 0x10 电源 / 0x20 ADC SP / 0x26-28 ADC 音量 /
    // 0x61/64/69 模拟 / 0x72/73 PGA（InputSel+增益）/ 0x40 DAC SP / 0xF0 misc
    static const uint8_t regs[] = { 0x00, 0x01, 0x02, 0x03, 0x10, 0x20, 0x21, 0x22, 0x23,
                                    0x26, 0x27, 0x28, 0x2A, 0x40, 0x60, 0x61, 0x62, 0x64,
                                    0x69, 0x6D, 0x72, 0x73, 0xF0, 0xF1 };
    esp_codec_dev_handle_t codec = audio_codec_handle();
    if (!codec) {
        const char *msg = "codec handle NULL\r\n";
        tud_cdc_write(msg, strlen(msg));
    } else {
        for (size_t i = 0; i < sizeof(regs); i++) {
            int val = -1;
            esp_codec_dev_read_reg(codec, regs[i], &val);
            n = snprintf(buf, sizeof(buf), "reg[0x%02X]=0x%02X%s", regs[i], val & 0xFF,
                         (i % 6 == 5 || i == sizeof(regs) - 1) ? "\r\n" : " ");
            tud_cdc_write(buf, (uint32_t)n);
            tud_cdc_write_flush();
        }
    }
    const char *end = "--- END ---\r\n";
    tud_cdc_write(end, strlen(end));
    tud_cdc_write_flush();
}

// 独立任务：唯一允许调 tud_cdc_write 的地方
static void cdc_stats_task(void *arg)
{
    (void)arg;
    char buf[320];
    uac_dbg_stats_t prev = { 0 };
    uint32_t prev_calls = 0, prev_bytes = 0, prev_mic_calls = 0;
    uint32_t tick = 0;
    bool greeted = false;

    float rms[2];
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        tick++;
        mic_rms_take(rms);   // 每秒必取（同时刷新 s_mic_rms 供 HP 抢屏页用）

        if (s_reboot_to_boot) {
            if (tud_cdc_connected()) {
                const char *msg = "\r\n>>> REBOOT_TO_BOOT_MODE: entering download mode, run esptool now\r\n";
                tud_cdc_write(msg, strlen(msg));
                tud_cdc_write_flush();
                vTaskDelay(pdMS_TO_TICKS(80));   // 让 CDC 把提示发出去再死
            }
            REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
            esp_restart();
        }

        if (s_amp_force) {
            bool on = s_amp_force > 0;
            s_amp_force = 0;
            amp_power(on);
            if (tud_cdc_connected()) {
                char m[48];
                int k = snprintf(m, sizeof(m), "\r\nAMP force %s\r\n", on ? "ON" : "OFF");
                tud_cdc_write(m, (uint32_t)k);
                tud_cdc_write_flush();
            }
        }

        if (!tud_cdc_connected()) {
            greeted = false;
            continue;
        }
        if (s_audio_dump) {
            s_audio_dump = false;
            cdc_audio_dump();
        }
        if (s_hp_cdc_event) {
            int ev = s_hp_cdc_event;
            s_hp_cdc_event = 0;
            char m[80];
            int k = snprintf(m, sizeof(m), "\r\n>>> HP_DET changed: %s @%lums (raw bit7)\r\n",
                             ev == 1 ? "0->1" : "1->0", (unsigned long)s_hp_event_ms);
            tud_cdc_write(m, (uint32_t)k);
            tud_cdc_write_flush();
        }
        {
            char klog[256];
            size_t kn = kbd_log_take(klog, sizeof(klog));
            if (kn) {
                tud_cdc_write(klog, (uint32_t)kn);
                tud_cdc_write_flush();
            }
        }
        if (!greeted) {
            greeted = true;
            const char *hello = "\r\n=== TypixDeck UAC+CDC stats (1Hz) ===\r\n"
                                "send REBOOT_TO_BOOT_MODE to enter flash mode\r\n";
            tud_cdc_write(hello, strlen(hello));
            tud_cdc_write_flush();
        }

        // 快照 + 增量（写方只自增，读撕裂无碍）
        uac_dbg_stats_t cur = g_uac_dbg;
        uint32_t calls = s_pcm_calls, bytes = s_pcm_bytes;
        uint32_t mcalls = s_mic_calls;
        int n = snprintf(buf, sizeof(buf),
            "[%lu] rx=%lu pkt/s %lu B/s sz=%lu..%lu gap>1.5ms=%lu maxgap=%luus "
            "clr=%lu | cb=%lu/s | tx=%lu pkt/s %lu B/s mic_cb=%lu/s "
            "micL=%.0f micR=%.0f | vsync=%.1ffps | itf open=%lu close=%lu mnt=%lu sus=%lu\r\n",
            (unsigned long)tick,
            (unsigned long)(cur.rx_pkts - prev.rx_pkts),
            (unsigned long)(cur.rx_bytes - prev.rx_bytes),
            (unsigned long)(cur.rx_min == UINT32_MAX ? 0 : cur.rx_min),
            (unsigned long)cur.rx_max,
            (unsigned long)cur.rx_gap_over,
            (unsigned long)cur.rx_max_gap_us,
            (unsigned long)cur.fifo_clear,
            (unsigned long)(calls - prev_calls),
            (unsigned long)(cur.tx_pkts - prev.tx_pkts),
            (unsigned long)(cur.tx_bytes - prev.tx_bytes),
            (unsigned long)(mcalls - prev_mic_calls),
            rms[0], rms[1],
            vsync_mon_fps(),
            (unsigned long)cur.set_itf,
            (unsigned long)cur.itf_close,
            (unsigned long)cur.mount,
            (unsigned long)cur.suspend);
        if (n > 0) {
            tud_cdc_write(buf, (uint32_t)n);
            tud_cdc_write_flush();
        }
        prev = cur;
        prev_calls = calls;
        prev_bytes = bytes;
        prev_mic_calls = mcalls;
        (void)prev_bytes;
    }
}

// audio(ES8389) + USB(UAC+CDC) 初始化放独立任务跑：esp_codec_dev + tusb_init
// 调用栈深，app_main 的 8KB 栈会溢出 → 崩溃重启循环（lcd_mp3 也是独立任务跑的）
static void audio_usb_task(void *arg)
{
    i2c_master_bus_handle_t bus = (i2c_master_bus_handle_t)arg;

    audio_start(bus);

    // non-AS_PART：组件自带描述符，itf 号由组件内部填，不用给 spk_itf_num
    uac_device_config_t uac_cfg = {
        .skip_tinyusb_init = false,
        .output_cb      = uac_output_cb,
        .input_cb        = uac_input_cb,   // 双 MEMS 麦 → ES8389 ADC → 主机
        .set_mute_cb     = uac_set_mute_cb,
        .set_volume_cb   = uac_set_volume_cb,
        .cb_ctx          = NULL,
    };
    if (uac_device_init(&uac_cfg) == ESP_OK) {
        ESP_LOGI(TAG, "USB UAC+CDC 已启动（48k/16bit/stereo 放音+录音 <-> ES8389 + CDC 统计口）");
        // CDC 统计任务在 tusb_init 之后才启动（优先级低于 tinyusb 任务）
        xTaskCreate(cdc_stats_task, "cdc_stats", 4096, NULL, 3, NULL);
    } else {
        ESP_LOGE(TAG, "USB UAC 初始化失败");
    }
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// 功放(NS4150) + 耳机检测
// 功放电源+使能 AW9523 P1_3(AMP_4V6_EN)：HIGH=开(R119 默认上拉)，LOW=关。
//   P1_3 经 R203 同时控 U28(4V6 电源开关) 和 NS4150 EN(U2.1/U40.1)，一拉低全断。
// 耳机检测 AW9523 P1_7(HP_DET)：输入，R166 上拉到 AUDIO_3V3，CN10.1 插入接地
//   → LOW=插了，HIGH=没插。
// 逻辑：插耳机 → 关功放（避免喇叭也响）；拔出 → 开功放。
// ---------------------------------------------------------------------------
static void amp_power(bool on)
{
    // 只动 bit3：先写目标电平再转输出（无毛刺）
    aw9523_update_bits(s_aw9523, AW9523_REG_OUTPUT_P1, AW9523_P1_AMP_4V6_EN,
                       on ? AW9523_P1_AMP_4V6_EN : 0);
    aw9523_update_bits(s_aw9523, AW9523_REG_CONFIG_P1, AW9523_P1_AMP_4V6_EN, 0);
    ESP_LOGI(TAG, "功放 %s", on ? "开" : "关");
}

// HP_DET(P1_7) 原始电平（1=上拉态/0=对地），I2C 读失败返回 -1。
// ⚠️ 不能忽略失败：曾把失败当 0 用，ESD 打挂总线的瞬间凭空产生 "1->0"
//   幻影插拔事件（HP=0 抢屏），实为 I2C 读挂了（2026-08-16 实翻车）。
static int hp_det_raw(void)
{
    uint8_t in = 0;
    if (aw9523_read_reg(s_aw9523, AW9523_REG_INPUT_P1, &in) != ESP_OK) return -1;
    return (in & AW9523_P1_HP_DET) ? 1 : 0;
}

static bool headphone_plugged(void)
{
    return hp_det_raw() == 0;   // 假设 LOW = 插了（R166 上拉，插入接地）——待实测确认
}

// AW9523 健康检查（hp_amp_task 内 1Hz 调用）：
//   - CONFIG_P0 读得出但 ≠ 期望值 0xFE → 芯片被 ESD/毛刺复位回默认态 → 重建配置
//     并按 s_mux_esp_side 恢复 MUX；
//   - 连续 3 次读失败 → 总线疑似被某从机拽死（SDA 卡低）→ i2c_master_bus_reset。
static void aw9523_health_tick(void)
{
    static int fail_streak = 0;
    uint8_t cfg = 0;
    esp_err_t err = aw9523_read_reg(s_aw9523, AW9523_REG_CONFIG_P0, &cfg);
    if (err != ESP_OK) {
        if (++fail_streak >= 3) {
            fail_streak = 0;
            ESP_LOGE(TAG, "AW9523 连续读失败（%s），复位 I2C 总线", esp_err_to_name(err));
            i2c_master_bus_reset(s_i2c_bus);
        }
        return;
    }
    fail_streak = 0;
    if (cfg != (0xFF & ~AW9523_P0_MUX_SEL)) {
        ESP_LOGE(TAG, "AW9523 配置丢失（CONFIG_P0=0x%02X，期望 0xFE）——疑似被静电复位，重建",
                 cfg);
        aw9523_reinit(s_aw9523, s_mux_esp_side);
        return;
    }
    // vsync 探测锁定后 INT_P0 必须保持全屏蔽（INTN 与 LCD CS 共线，重开
    // P0_7 = I2S 码流灌面板）。锁定前不碰：vsync_mon 自己管理该位
    // （含风暴退避的临时屏蔽），此处强写会与之打架。
    if (vsync_mon_locked()) {
        uint8_t intp0 = 0;
        if (aw9523_read_reg(s_aw9523, AW9523_REG_INT_P0, &intp0) == ESP_OK &&
            intp0 != 0xFF) {
            ESP_LOGE(TAG, "AW9523 INT_P0=0x%02X 异常（应全屏蔽）——重写保护 LCD CS", intp0);
            aw9523_write_reg(s_aw9523, AW9523_REG_INT_P0, 0xFF);
        }
    }
}

static void hp_amp_task(void *arg)
{
    (void)arg;
    int raw = hp_det_raw();
    if (raw < 0) raw = 1;                   // 读失败按"未插"起步，等健康检查自愈
    amp_power(raw != 0);                    // 开机按当前状态设一次（raw=1 假设未插→功放开）
    // ★ 喇叭链路左右反接补偿（2026-08-16 用户听测实锤：耳机对、外放反）：
    //   外放（未插耳机）时 DAC 数字互换 L/R，插耳机恢复正常。
    //   根因（CN10 切换触点配对 vs 喇叭装位）与下版 PCB 修改见
    //   docs/typixdeck_speaker_lr_swap_2026-08.md
    //   注意本任务先于 audio_usb_task 里的 audio_start 启动，codec 未就绪时
    //   （返回 -1）在轮询循环里兜底重试，直到首次写入成功。
    bool swap_ok = (audio_set_dac_lr_swap(raw != 0) == 0);
    ESP_LOGI(TAG, "耳机检测启动：HP_DET raw=%d（%s，极性待实测）", raw,
             raw ? "假设未插→功放开+声道互换" : "假设已插→功放关+声道正常");
    int health_tick = 0;
    while (1) {
        if (!swap_ok) swap_ok = (audio_set_dac_lr_swap(raw != 0) == 0);
        if (++health_tick >= 10) {           // 1Hz：AW9523 复位/总线卡死自愈
            health_tick = 0;
            aw9523_health_tick();
        }
        int now = hp_det_raw();
        if (now >= 0 && now != raw) {        // 读失败(-1)不算插拔事件
            vTaskDelay(pdMS_TO_TICKS(50));   // 二次采样一致才算数（去抖）
            if (hp_det_raw() == now) {
                int ev = now ? 1 : 2;        // 1: 0->1, 2: 1->0
                raw = now;
                amp_power(raw != 0);         // 假设极性：raw=0 插→关功放
                swap_ok = (audio_set_dac_lr_swap(raw != 0) == 0);   // 外放=互换，耳机=正常
                s_hp_event_ms = (uint32_t)((int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS);
                s_hp_cdc_event  = ev;
                s_hp_grab_event = ev;
                ESP_LOGI(TAG, "HP_DET changed -> raw=%d", raw);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));      // 10Hz 轮询
    }
}

// ---------------------------------------------------------------------------
// 触摸：GT911 惰性初始化（仅 MUX=ESP 侧时总线可达）+ 轮询
// ---------------------------------------------------------------------------
static bool touch_ensure_init(void)
{
    if (s_gt911) return true;
    if (gt911_init(s_i2c_bus, &s_gt911) == ESP_OK) {
        ESP_LOGI(TAG, "GT911 触摸就绪");
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 显示归属状态机
// ---------------------------------------------------------------------------
typedef enum {
    ST_BOOT_ANIM = 0,   // 开机动画（MUX=ESP），等 Pi VSYNC；一有信号立即交 Pi
    ST_PI,              // Pi 持屏
    ST_ESP_UI,          // ESP 本地 GUI（Tab 界面）
} disp_state_t;

// 开机动画等待多久后追加 NO SIGNAL 提示
#define BOOT_HINT_MS   20000

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "TypixDeck dual-source display switch + power monitor + GUI");

    if (i2c_start() != ESP_OK) {
        ESP_LOGE(TAG, "I2C/AW9523 初始化失败，停止");
        return;
    }
    // MUX 此刻在 ESP 侧（aw9523_init 置 MUX_SEL=1）：完成面板 SPI 初始化
    if (lcd_reset_and_spi_init() != ESP_OK) {
        ESP_LOGE(TAG, "LCD SPI 初始化失败，停止");
        return;
    }
    // GT911 INT-low 干净复位 + 触摸初始化（此窗口 INT/SDA/SCL 走 ESP 侧可达；
    // 失败不阻塞——进 ESP GUI 时再惰性重试）
    esp_err_t err = aw9523_gt911_reset(s_aw9523);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GT911 复位失败（%s），触摸可能异常", esp_err_to_name(err));
    }
    touch_ensure_init();

    if (rgb_panel_start() != ESP_OK) {
        ESP_LOGE(TAG, "RGB 面板启动失败，停止");
        return;
    }
    backlight_on();
    sensors_start();

    ui_ctx_t ui_ctx = {
        .bus = s_i2c_bus,
        .ina_vbat = s_ina_vbat,
        .ina_vbus = s_ina_vbus,
        .cw2015 = s_cw2015,
        .stc3117 = s_stc3117,
    };
    if (ui_init(s_panel, &ui_ctx) != ESP_OK) {
        ESP_LOGE(TAG, "UI framebuffer 分配失败，停止");
        return;
    }
    ui_boot_anim_tick(0, false);   // 先出 logo 再继续（MUX 已在 ESP 侧）

    // VSYNC 探测：必须在 lcd_jd9168s_spi_init 之后（GPIO5 已被 spi_bus_free 释放）
    if (vsync_mon_start(s_aw9523) != ESP_OK) {
        ESP_LOGE(TAG, "VSYNC 探测启动失败（不影响其余功能）");
    }

    // 电池历史采样（INA219 VBAT + CW2015 常连总线，谁持屏都在跑）
    batt_log_start(s_ina_vbat, s_cw2015);

    // 显式开 ES8389 模拟电源：DAC_3V3_EN(P1_0) 推挽驱高（R84 上拉对 U31 不够稳）
    aw9523_update_bits(s_aw9523, AW9523_REG_OUTPUT_P1, AW9523_P1_DAC_3V3_EN, AW9523_P1_DAC_3V3_EN);
    aw9523_update_bits(s_aw9523, AW9523_REG_CONFIG_P1, AW9523_P1_DAC_3V3_EN, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(TAG, "DAC_3V3_EN(P1_0) 已驱高，ES8389 上电");

    // 功放 + 耳机检测：轮询 HP_DET(P1_7)，插耳机关功放、拔出开功放
    xTaskCreate(hp_amp_task, "hp_amp", 4096, NULL, 5, NULL);

    // LCD SPI 初始化已完成 → GPIO47/48 现在重配成 I2S。
    // audio(ES8389) + USB(UAC) 放独立 16KB 任务跑（栈深，避免 app_main 8KB 溢出）
    xTaskCreate(audio_usb_task, "audio_usb", 16384, s_i2c_bus, 5, NULL);

    boot_btn_start();

    // 键盘 I2C 从机：加设备 + 事件轮询任务（□ 键按下置 s_kbd_toggle_req）
    s_kbd = sensor_add(KBD_I2C_ADDR);
    if (s_kbd) {
        xTaskCreate(kbd_task, "kbd", 4096, NULL, 5, NULL);
    }

    // ---- 开机动画阶段：MUX 保持 ESP 侧，等 Pi VSYNC ----
    ESP_LOGI(TAG, "开机动画中：等待 Pi VSYNC（探测到立即交屏），□/SW3 可直接进 ESP GUI");

    disp_state_t state = ST_BOOT_ANIM;
    int prev_lvl = 1;
    int anim_frame = 0;
    int64_t boot_ms0 = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
    int64_t last_draw_ms = 0;
    int64_t hp_grab_until_ms = 0;   // >now 表示 HP_DET 抢屏窗口生效中
    disp_state_t hp_restore_state = ST_PI;

    while (1) {
        int lvl = gpio_get_level(PIN_BOOT_BTN);
        int64_t now_ms = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;

        bool toggle = (prev_lvl == 1 && lvl == 0);   // SW3 按下沿（轮询自带消抖）
        prev_lvl = lvl;
        if (s_kbd_toggle_req) {
            s_kbd_toggle_req = false;
            toggle = true;                            // 键盘 □ 键，与 SW3 等价
        }

        // ---- HP_DET 插拔事件 → ESP 抢屏 2 秒显示状态页，到时恢复 ----
        // （开机动画阶段忽略：动画本来就在 ESP 侧，别打断）
        if (s_hp_grab_event) {
            int ev = s_hp_grab_event;
            s_hp_grab_event = 0;
            if (state != ST_BOOT_ANIM) {
                hp_restore_state = state;
                ui_draw_hp_page(ev == 1 ? 1 : 0, s_mic_rms[0], s_mic_rms[1]);
                mux_select(true);
                // 无条件设恢复窗口：即使 MUX 写失败也要按时恢复画面归属，
                // 不能让 HP 页永久占屏（写失败由健康检查兜底纠正 MUX）
                hp_grab_until_ms = now_ms + 2000;
            }
        }
        if (hp_grab_until_ms) {
            if (toggle) {
                // 手动切换优先：取消抢屏窗口，按原归属翻转
                hp_grab_until_ms = 0;
                state = hp_restore_state;
            } else if (now_ms >= hp_grab_until_ms) {
                hp_grab_until_ms = 0;
                state = hp_restore_state;
                mux_select(state == ST_ESP_UI);
                if (state == ST_ESP_UI) {
                    ui_page_draw(now_ms / 1000);
                    last_draw_ms = now_ms;
                }
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            } else {
                vTaskDelay(pdMS_TO_TICKS(50));     // 抢屏窗口内不跑其它重绘
                continue;
            }
        }

        switch (state) {
        case ST_BOOT_ANIM:
            if (vsync_mon_signal()) {
                // Pi 出图了：立即交屏，此后不再自动切回（除非 □）
                ESP_LOGI(TAG, "检测到 Pi VSYNC（%.1f fps），交屏给 Pi", vsync_mon_fps());
                mux_select(false);
                state = ST_PI;
                break;
            }
            if (toggle) {
                // Pi 还没出图，用户主动进 ESP GUI（MUX 本来就在 ESP 侧）
                state = ST_ESP_UI;
                ui_page_draw(now_ms / 1000);
                last_draw_ms = now_ms;
                break;
            }
            ui_boot_anim_tick(anim_frame++, now_ms - boot_ms0 > BOOT_HINT_MS);
            vTaskDelay(pdMS_TO_TICKS(80));   // ~12fps 旋转
            continue;

        case ST_PI:
            if (toggle) {
                ESP_LOGI(TAG, "切屏 → ESP GUI");
                touch_ensure_init();               // MUX 即将到 ESP 侧，触摸可用
                ui_page_draw(now_ms / 1000);       // 先备好画面再切 MUX
                last_draw_ms = now_ms;
                if (mux_select(true) != ESP_OK) {
                    ESP_LOGE(TAG, "MUX 切换失败");
                }
                state = ST_ESP_UI;
            }
            break;

        case ST_ESP_UI:
            if (toggle) {
                ESP_LOGI(TAG, "切屏 → Pi");
                if (mux_select(false) != ESP_OK) {
                    ESP_LOGE(TAG, "MUX 切换失败");
                }
                state = ST_PI;
                break;
            }
            // 触摸轮询（MUX 在 ESP 侧，GT911 走 ESP I2C）
            if (s_gt911 || touch_ensure_init()) {
                gt911_touch_t t;
                esp_err_t terr = gt911_read(s_gt911, &t);
                if (terr == ESP_OK) {
                    if (t.count > 0) {
                        ui_handle_touch(t.x, t.y, true);
                    } else {
                        ui_handle_touch(0, 0, false);
                    }
                }
            }
            ui_maybe_flush();                      // 触摸轨迹增量冲刷（限频 15Hz）
            if (now_ms - last_draw_ms >= 500) {
                ui_page_draw(now_ms / 1000);
                last_draw_ms = now_ms;
            }
            vTaskDelay(pdMS_TO_TICKS(20));         // 触摸响应 50Hz
            continue;

        default:
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
