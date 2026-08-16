// TypixDeck 本地 UI（自绘 PSRAM framebuffer，无 LVGL）：
//   - 开机动画：TYPIXDECK 大字 + Ubuntu 式 12 点旋转 spinner，
//     等待 Pi VSYNC 信号（vsync_mon），有信号立即切 MUX 交给 Pi
//   - Tab 界面（□ 键呼出）：DASH / BATT / TOUCH / PI SIG 四页，
//     顶部 Tab 栏触摸切换，右上角常驻 Pi 信号状态芯片
//   - 耳机插拔抢屏页（原 main.c ui_draw_hp_page 移入）
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "driver/i2c_master.h"

typedef enum {
    UI_TAB_DASH = 0,
    UI_TAB_BATT,
    UI_TAB_TOUCH,
    UI_TAB_PISIG,
    UI_TAB_SETUP,   // 设置页：语言切换（NVS 持久化）
    UI_TAB_COUNT,
} ui_tab_t;

// 遥测页需要的数据源（句柄归 main.c 所有）
typedef struct {
    i2c_master_bus_handle_t bus;        // 传感器在位探测
    i2c_master_dev_handle_t ina_vbat;
    i2c_master_dev_handle_t ina_vbus;
    i2c_master_dev_handle_t cw2015;
    i2c_master_dev_handle_t stc3117;
} ui_ctx_t;

// 分配 framebuffer 并保存上下文；失败返回 ESP_ERR_NO_MEM
esp_err_t ui_init(esp_lcd_panel_handle_t panel, const ui_ctx_t *ctx);

// ---- 开机动画 ----
// frame 递增即旋转；show_hint=true 追加 "NO SIGNAL YET" 提示（等太久时）
void ui_boot_anim_tick(int frame, bool show_hint);

// ---- Tab 界面 ----
ui_tab_t ui_current_tab(void);
// 整页重绘（含 Tab 栏 + 状态芯片 + 当前页内容）
void ui_page_draw(uint32_t uptime_s);
// 触摸事件入口（GT911 原始坐标 1024×768）。Tab 栏命中会切页并整页重绘；
// TOUCH 页画布内画轨迹（增量绘制，内部限频冲刷）
void ui_handle_touch(int x, int y, bool pressed);
// 主循环每 tick 调一次：有增量脏区且距上次冲刷 >66ms 时整帧推送
void ui_maybe_flush(void);

// ---- 耳机插拔抢屏页 ----
void ui_draw_hp_page(int raw, float rms_l, float rms_r);
