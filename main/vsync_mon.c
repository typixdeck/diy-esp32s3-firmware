#include "vsync_mon.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_attr.h"

#include "aw9523.h"
#include "board_pins.h"

static const char *TAG = "VSYNC";

// GPIO5 = ESP_LCD_CS = AW9523 INTN（R82 0Ω 直连，R91 10kΩ 上拉）
#define PIN_AW_INTN        PIN_LCD_CS

// 帧周期合法窗口：5ms(200Hz)..200ms(5Hz)——覆盖 Pi 各种 DPI 模式
#define PERIOD_MIN_US      5000
#define PERIOD_MAX_US      200000
// 连续合法帧数达到该值才算"有信号"（防悬空毛刺误判）
#define SIGNAL_STREAK      5
// 超过该时长无中断 = 信号丢失
#define SIGNAL_TIMEOUT_US  500000
// 风暴判定：连续短周期（<PERIOD_MIN_US）次数阈值 → 屏蔽退避
#define STORM_STREAK       50
#define STORM_BACKOFF_MS   3000

static i2c_master_dev_handle_t s_aw;
static TaskHandle_t s_rearm_task;

static volatile int64_t  s_last_edge_us = 0;   // 最近一次中断时间戳
static volatile uint32_t s_period_ema_us = 0;  // EMA 平滑帧周期
static volatile uint32_t s_frames = 0;         // 累计有效帧
static volatile uint32_t s_streak = 0;         // 连续合法帧计数
static volatile uint32_t s_storm_streak = 0;   // 连续短周期计数
static volatile uint32_t s_storms = 0;         // 风暴退避次数
static volatile bool     s_locked = false;     // Pi 出图已确认，探测永久关闭

static void IRAM_ATTR intn_isr(void *arg)
{
    (void)arg;
    int64_t now = esp_timer_get_time();
    int64_t last = s_last_edge_us;
    s_last_edge_us = now;

    if (last > 0) {
        int64_t period = now - last;
        if (period >= PERIOD_MIN_US && period <= PERIOD_MAX_US) {
            // EMA：新值 1/4 权重（整数运算，ISR 安全）
            uint32_t ema = s_period_ema_us;
            s_period_ema_us = ema ? (ema * 3 + (uint32_t)period) / 4
                                  : (uint32_t)period;
            s_frames++;
            if (s_streak < 0xFFFF) s_streak++;
            s_storm_streak = 0;
        } else if (period < PERIOD_MIN_US) {
            s_storm_streak++;
            s_streak = 0;
        } else {
            // 长间隔（信号刚恢复）：不计入 EMA，重新起 streak
            s_streak = 0;
            s_storm_streak = 0;
        }
    }

    BaseType_t hpw = pdFALSE;
    vTaskNotifyGiveFromISR(s_rearm_task, &hpw);
    portYIELD_FROM_ISR(hpw);
}

// 只动 P0_7 一个屏蔽位（1=屏蔽 0=使能），其余 15 脚中断保持全屏蔽
static esp_err_t p0_7_int_enable(bool en)
{
    return aw9523_update_bits(s_aw, AW9523_REG_INT_P0, AW9523_P0_PI_GPIO2,
                              en ? 0 : AW9523_P0_PI_GPIO2);
}

// 高优先级 re-arm 任务：中断来了立刻读 INPUT_P0 清除锁存，把 INTN 低电平
// 窗口（= 面板 CS 被拉低的窗口）压到最短。顺带做风暴退避。
static void rearm_task(void *arg)
{
    (void)arg;
    uint8_t dummy;
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        aw9523_read_reg(s_aw, AW9523_REG_INPUT_P0, &dummy);   // 清中断（重武装）

        // ★ Pi 出图一经确认就永久关闭探测（2026-08-16 用户拍板）：
        //   INTN 与 LCD CS 共线（R82），持续探测 = 每帧压一次 CS，
        //   面板 SPI 口反复打开，迟早被 RGB/PCLK 串扰写坏寄存器（实翻车：
        //   运行中随机反色）。开机交屏判据拿到手后 INTN 必须回到常高。
        if (!s_locked && s_streak >= SIGNAL_STREAK) {
            s_locked = true;
            p0_7_int_enable(false);
            aw9523_read_reg(s_aw, AW9523_REG_INPUT_P0, &dummy);  // 清尾巴
            uint32_t ema = s_period_ema_us;
            ESP_LOGI(TAG, "Pi 出图确认（%.1f fps），vsync 探测永久关闭（保护 LCD CS）",
                     ema ? 1e6f / (float)ema : 0.0f);
            continue;
        }

        if (s_storm_streak >= STORM_STREAK) {
            // 悬空噪声风暴（如 Pi 断电）：屏蔽退避，避免 INTN 频繁压 CS 线
            s_storms++;
            s_storm_streak = 0;
            s_streak = 0;
            s_period_ema_us = 0;
            ESP_LOGW(TAG, "中断风暴（周期<%dus 连续 %d 次），退避 %dms",
                     PERIOD_MIN_US, STORM_STREAK, STORM_BACKOFF_MS);
            p0_7_int_enable(false);
            vTaskDelay(pdMS_TO_TICKS(STORM_BACKOFF_MS));
            aw9523_read_reg(s_aw, AW9523_REG_INPUT_P0, &dummy);
            s_last_edge_us = 0;
            p0_7_int_enable(true);
        }
    }
}

esp_err_t vsync_mon_start(i2c_master_dev_handle_t aw9523)
{
    s_aw = aw9523;

    // GPIO5 此刻已被 lcd_spi_init 释放（spi_bus_free）→ 重配输入 + 下降沿中断。
    // R91 外部 10k 上拉已在板上，内部上拉再并一个无害。
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_AW_INTN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) return err;

    // re-arm 任务优先级要压过音频/UI（但低于 tinyusb），保证 INTN 快速释放
    if (xTaskCreate(rearm_task, "vsync_rearm", 3072, NULL, 10, &s_rearm_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;   // 已装过=OK
    err = gpio_isr_handler_add(PIN_AW_INTN, intn_isr, NULL);
    if (err != ESP_OK) return err;

    // 读一次输入寄存器建立"当前状态"基线并清掉可能残留的中断，再解开 P0_7 屏蔽
    uint8_t dummy;
    aw9523_read_reg(s_aw, AW9523_REG_INPUT_P0, &dummy);
    err = p0_7_int_enable(true);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "VSYNC 探测已启动：AW9523 P0_7(Pi GPIO2/DPI VSYNC) INT → GPIO5");
    return ESP_OK;
}

bool vsync_mon_signal(void)
{
    if (s_locked) return true;   // 已确认出图（探测关闭后视为持续有信号）
    if (s_streak < SIGNAL_STREAK) return false;
    int64_t last = s_last_edge_us;
    return last > 0 && (esp_timer_get_time() - last) < SIGNAL_TIMEOUT_US;
}

float vsync_mon_fps(void)
{
    uint32_t ema = s_period_ema_us;
    if (s_locked) return ema ? 1e6f / (float)ema : 0;   // 冻结的最后测量值
    if (!vsync_mon_signal()) return 0;
    return ema ? 1e6f / (float)ema : 0;
}

bool vsync_mon_locked(void)
{
    return s_locked;
}

uint32_t vsync_mon_frames(void)
{
    return s_frames;
}

int64_t vsync_mon_age_ms(void)
{
    int64_t last = s_last_edge_us;
    if (last <= 0) return -1;
    return (esp_timer_get_time() - last) / 1000;
}

uint32_t vsync_mon_storms(void)
{
    return s_storms;
}
