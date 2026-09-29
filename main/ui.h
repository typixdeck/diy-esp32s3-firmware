// TypixDeck 本地 UI（自绘 PSRAM framebuffer，无 LVGL）：
//   - 开机动画：TYPIXDECK 大字 + Ubuntu 式 12 点旋转 spinner，
//     等待 Pi VSYNC 信号（vsync_mon），有信号立即切 MUX 交给 Pi
//   - 四个 Tab：固件 / 树莓派 / 应用 / 设置，
//     触摸或 Tab/方向键导航，Enter 激活，Fn+Tab 返回。
//   - 耳机插拔仅通知，不抢占显示归属。
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum { UI_TAB_SENSORS = 0, UI_TAB_PI, UI_TAB_APPS, UI_TAB_SETUP, UI_TAB_COUNT } ui_tab_t;

// 遥测页需要的数据源（句柄归 main.c 所有）
typedef struct {
    i2c_master_bus_handle_t bus; // 传感器在位探测
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
// Pi 出图瞬间的定格彩蛋（交屏前调用，之后 delay ~350ms 再切 MUX）
void ui_boot_signal_locked(void);

// ---- Tab 界面 ----
ui_tab_t ui_current_tab(void);
// 整页重绘（含图标 Tab 栏、网络/时间状态和当前页内容）
void ui_page_draw(uint32_t uptime_s);
// Slow telemetry refresh avoids full-page redraw while playing the instrument.
void ui_periodic_draw(uint32_t uptime_s);
// 触摸事件入口（GT911 原始坐标 1024×768）。Tab 栏命中会切页并整页重绘；
// 乐器触摸琴键按下/滑动/释放，其他操作以按下沿触发。
void ui_handle_touch(int x, int y, bool pressed);
// 主循环每 tick 调一次：处理输入队列与琴键/颜色编辑脏标记，按需整帧推送
void ui_maybe_flush(void);

// ---- 耳机插拔通知（不改变显示归属） ----
void ui_draw_hp_page(int raw, float rms_l, float rms_r);

// ---- 调试：帧缓冲只读指针（CDC SCREENSHOT 回传用），未初始化返回 NULL ----
const uint16_t *ui_framebuffer(void);
// 帧快照（与整页重绘互斥，不会拷到半张画面）；dst 需容纳 1024*768*2 字节
bool ui_snapshot(uint16_t *dst);

// ---- 调试切页和本地键盘输入 ----
void ui_key_event(int row, int col, bool pressed);
void ui_process_events(void);
void ui_cancel_input(void);
void ui_input_lost(void);
void ui_request_tab(int t);
void ui_request_lang(int l); // CDC LANG_n：远程切语言（写 NVS，截图验证用）

// ---- Pi 端遥测（CDC "EGGFLY_PI_INFO k=v k=v ..."，typixdeck-pi-info.service 每 2s 一行）----
// 键：model=CM4 rev=1.0 cpu=72.1 nvme=41 fan=3200 thr=0x0 load=0.52 up=1234
// 任意任务上下文可调（tinyusb rx 回调），内部拷贝并打时间戳；>8s 无更新视为离线
void ui_set_pi_info(const char *kv_line);

// MUX 归属通知（main 的 mux_select 调用）：ESP 持屏时才探测 MUX 后面的器件
// （STC3117/GT911），且切到 ESP 侧后立刻重新探测一轮
void ui_notify_mux(bool esp_side);
// 保留原主循环接口，无背景装饰动画。
void ui_dash_anim_tick(void);
