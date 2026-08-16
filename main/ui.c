#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "board_pins.h"
#include "batt_log.h"
#include "sensors.h"
#include "ttf_font.h"
#include "vsync_mon.h"

static const char *TAG = "UI";

// ---------------------------------------------------------------------------
// framebuffer 原语（RGB565）
// ---------------------------------------------------------------------------
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
#define C_ORANGE 0xFD20

static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_fb;
static ui_ctx_t s_ctx;

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
// 语言（默认英文，设置页可切中文，NVS 持久化：namespace "ui" / key "lang"）
// ---------------------------------------------------------------------------
typedef enum { LANG_EN = 0, LANG_ZH = 1 } ui_lang_t;
static ui_lang_t s_lang = LANG_EN;

// 双语取词：中文需要 TTF；font 分区没刷时强制回英文（中文会画成空白）
static const char *tr(const char *en, const char *zh)
{
    return (s_lang == LANG_ZH && ttf_font_ready()) ? zh : en;
}

static void lang_load(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS 不可用（%s），语言设置不持久", esp_err_to_name(err));
        return;
    }
    nvs_handle_t h;
    if (nvs_open("ui", NVS_READONLY, &h) == ESP_OK) {
        uint8_t v = 0;
        if (nvs_get_u8(h, "lang", &v) == ESP_OK && v <= LANG_ZH) {
            s_lang = (ui_lang_t)v;
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG, "UI 语言：%s", s_lang == LANG_ZH ? "中文" : "English");
}

static void lang_save(void)
{
    nvs_handle_t h;
    if (nvs_open("ui", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "lang", (uint8_t)s_lang);
    nvs_commit(h);
    nvs_close(h);
}

// ---------------------------------------------------------------------------
// TTF 优先的文本绘制（阿里巴巴普惠体，中英混排）。TTF 未就绪（font 分区没刷）
// 时回退 5x7 点阵——中文字符显示空白但布局不崩，ASCII 正常。
// size 是 TTF 像素字号；回退时粗换算 scale ≈ size/8。
// ---------------------------------------------------------------------------
static void draw_txt(int x, int y, int size, uint16_t color, const char *utf8)
{
    if (ttf_font_ready()) {
        ttf_draw_text(s_fb, LCD_H_RES, LCD_V_RES, x, y, size, color, utf8);
    } else {
        fb_draw_text(x, y, utf8, size > 8 ? size / 8 : 1, color);
    }
}

static int txt_w(int size, const char *utf8)
{
    if (ttf_font_ready()) return ttf_text_width(size, utf8);
    return text_w(utf8, size > 8 ? size / 8 : 1);
}

static void draw_txt_centered(int y, int size, uint16_t color, const char *utf8)
{
    draw_txt((LCD_H_RES - txt_w(size, utf8)) / 2, y, size, color, utf8);
}

// 粗线段（曲线用）：Bresenham，每点画 thick×thick 方块
static void fb_draw_line(int x0, int y0, int x1, int y1, int thick, uint16_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (1) {
        fb_fill_rect(x0, y0, thick, thick, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

static void fb_flush(void)
{
    if (s_panel && s_fb) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_H_RES, LCD_V_RES, s_fb);
    }
}

// ---------------------------------------------------------------------------
// 初始化
// ---------------------------------------------------------------------------
esp_err_t ui_init(esp_lcd_panel_handle_t panel, const ui_ctx_t *ctx)
{
    s_panel = panel;
    s_ctx = *ctx;
    s_fb = heap_caps_malloc(LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_fb) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "UI framebuffer=%d KB", LCD_H_RES * LCD_V_RES * 2 / 1024);
    // 中文 TTF（font 分区）——失败不致命，draw_txt 自动回退 ASCII 点阵
    esp_err_t ferr = ttf_font_init();
    if (ferr != ESP_OK) {
        ESP_LOGW(TAG, "TTF 字体不可用（%s），中文将无法显示", esp_err_to_name(ferr));
    }
    lang_load();   // NVS 里的语言偏好（默认英文）
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// 开机动画：TYPIXDECK + 12 点旋转 spinner（Ubuntu 风格拖尾渐隐）
// ---------------------------------------------------------------------------
#define SPIN_DOTS   12
#define SPIN_CX     (LCD_H_RES / 2)
#define SPIN_CY     470
#define SPIN_R      80
#define SPIN_DOT    16

// RGB565 灰度（0..255）
static uint16_t gray565(int lum)
{
    if (lum < 0) lum = 0;
    if (lum > 255) lum = 255;
    return (uint16_t)(((lum >> 3) << 11) | ((lum >> 2) << 5) | (lum >> 3));
}

void ui_boot_anim_tick(int frame, bool show_hint)
{
    static bool s_static_drawn = false;
    static bool s_hint_drawn = false;
    if (!s_static_drawn) {
        s_static_drawn = true;
        fb_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, C_BLACK);
        if (ttf_font_ready()) {
            draw_txt_centered(180, 110, C_WHITE, "TypixDeck");
        } else {
            fb_draw_text_centered(220, "TYPIXDECK", 10, C_WHITE);
        }
    }
    // spinner：只重绘转盘正方形区域
    int box = SPIN_R + SPIN_DOT + 8;
    fb_fill_rect(SPIN_CX - box, SPIN_CY - box, box * 2, box * 2, C_BLACK);
    for (int i = 0; i < SPIN_DOTS; i++) {
        // 相位差决定亮度：领头最亮，逆序渐隐
        int lag = (frame - i) % SPIN_DOTS;
        if (lag < 0) lag += SPIN_DOTS;
        int lum = 255 - lag * (220 / SPIN_DOTS);
        float ang = (float)i * 2.0f * (float)M_PI / SPIN_DOTS - (float)M_PI / 2;
        int x = SPIN_CX + (int)(cosf(ang) * SPIN_R) - SPIN_DOT / 2;
        int y = SPIN_CY + (int)(sinf(ang) * SPIN_R) - SPIN_DOT / 2;
        fb_fill_rect(x, y, SPIN_DOT, SPIN_DOT, gray565(lum));
    }
    if (show_hint && !s_hint_drawn) {
        s_hint_drawn = true;
        draw_txt_centered(636, 30, C_GRAY, tr("WAITING FOR PI VIDEO SIGNAL - NONE YET",
                                              "等待树莓派视频信号——暂无信号"));
        draw_txt_centered(686, 30, C_GRAY, tr("PRESS □ KEY FOR SYSTEM MONITOR",
                                              "按 □ 键进入系统监控"));
    }
    fb_flush();
}

// ---------------------------------------------------------------------------
// Tab 框架
// ---------------------------------------------------------------------------
#define TAB_BAR_H     78
#define TAB_W         170            // 5 页 ×170 = 850，右侧留给状态芯片
#define CONTENT_Y     (TAB_BAR_H + 12)

static ui_tab_t s_tab = UI_TAB_DASH;
static uint32_t s_last_uptime_s = 0;

static const char *k_tab_en[UI_TAB_COUNT] = { "DASH", "BATTERY", "TOUCH", "PI SIG", "SETUP" };
static const char *k_tab_zh[UI_TAB_COUNT] = { "仪表盘", "电池曲线", "触摸测试", "PI 信号", "设置" };

ui_tab_t ui_current_tab(void)
{
    return s_tab;
}

// 右上角 Pi 信号状态芯片（所有页共用）
static void draw_status_chip(void)
{
    int x = UI_TAB_COUNT * TAB_W + 8, w = LCD_H_RES - x - 8;
    fb_fill_rect(x, 8, w, TAB_BAR_H - 16, C_BLACK);
    char buf[24];
    float fps = vsync_mon_fps();
    if (fps > 0) {
        snprintf(buf, sizeof(buf), "PI %d.%d", (int)fps, (int)(fps * 10) % 10);
        fb_draw_text(x + 16, 14, buf, 3, C_GREEN);
        draw_txt(x + 16, 42, 24, C_DGREEN, tr("FPS LIVE", "FPS 实时"));
    } else {
        fb_draw_text(x + 16, 14, "PI RGB", 3, C_RED);
        draw_txt(x + 16, 42, 24, C_RED, tr("NO SIGNAL", "无信号"));
    }
}

static void draw_tab_bar(void)
{
    fb_fill_rect(0, 0, LCD_H_RES, TAB_BAR_H, C_NAVY);
    for (int i = 0; i < UI_TAB_COUNT; i++) {
        int x = i * TAB_W;
        if ((ui_tab_t)i == s_tab) {
            fb_fill_rect(x, 0, TAB_W, TAB_BAR_H, C_DARK);
            fb_fill_rect(x, TAB_BAR_H - 8, TAB_W, 8, C_CYAN);
        }
        uint16_t col = ((ui_tab_t)i == s_tab) ? C_WHITE : C_GRAY;
        const char *name = tr(k_tab_en[i], k_tab_zh[i]);
        draw_txt(x + (TAB_W - txt_w(28, name)) / 2, 24, 28, col, name);
    }
    fb_fill_rect(0, TAB_BAR_H, LCD_H_RES, 4, C_GREEN);
    draw_status_chip();
}

// 卡片：深黑底 + 左侧彩色竖条 + 标题
static void ui_card(int x, int y, int w, int h, uint16_t accent, const char *title)
{
    fb_fill_rect(x, y, w, h, C_BLACK);
    fb_fill_rect(x, y, 8, h, accent);
    draw_txt(x + 30, y + 12, 26, accent, title);
}

// ---------------------------------------------------------------------------
// DASH 页（原遥测页，内容下移让出 Tab 栏）
// ---------------------------------------------------------------------------
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
    { "KBD6R11", 0x1F, 0    },  // U32 键盘 STM32（QMK I2C 从机）
};
#define N_SENSORS (sizeof(k_sensors) / sizeof(k_sensors[0]))

// 在位探测缓存（探测一轮离线器件各吃 50ms 超时，不能每次重绘都做）
static bool s_present[N_SENSORS];
static int s_present_ok = 0;
static int64_t s_present_ts_us = -1;

static void sensors_probe_maybe(void)
{
    int64_t now = esp_timer_get_time();
    if (s_present_ts_us >= 0 && now - s_present_ts_us < 5 * 1000000LL) return;
    s_present_ts_us = now;
    s_present_ok = 0;
    for (int i = 0; i < N_SENSORS; i++) {
        s_present[i] = i2c_master_probe(s_ctx.bus, k_sensors[i].addr, 50) == ESP_OK ||
                       (k_sensors[i].alt &&
                        i2c_master_probe(s_ctx.bus, k_sensors[i].alt, 50) == ESP_OK);
        if (s_present[i]) s_present_ok++;
    }
}

static void draw_page_dash(uint32_t uptime_s)
{
    char buf[64];
    float vbat_v = 0, vbat_a = 0, vbus_v = 0, vbus_a = 0;
    float cw_v = 0, stc_v = 0, stc_soc = -1;
    int cw_soc = -1;
    bool vbat_ok = s_ctx.ina_vbat && ina219_read(s_ctx.ina_vbat, &vbat_v, &vbat_a) == ESP_OK;
    bool vbus_ok = s_ctx.ina_vbus && ina219_read(s_ctx.ina_vbus, &vbus_v, &vbus_a) == ESP_OK;
    if (!s_ctx.cw2015 || cw2015_read(s_ctx.cw2015, &cw_v, &cw_soc) != ESP_OK) cw_soc = -1;
    stc3117_ensure_running(s_ctx.stc3117);   // 兜底：POR/BATFAIL 后重新拉起 GG_RUN
    if (!s_ctx.stc3117 || stc3117_read(s_ctx.stc3117, &stc_v, &stc_soc) != ESP_OK) stc_soc = -1;

    // ---- 电池卡片 ----
    int soc = cw_soc >= 0 ? cw_soc : (int)stc_soc;   // 主 SOC 优先 CW2015
    uint16_t soc_col = soc < 0   ? C_GRAY
                     : soc < 15  ? C_RED
                     : soc < 40  ? C_YELL : C_GREEN;
    ui_card(40, CONTENT_Y + 10, 944, 240, C_YELL, tr("BATTERY", "电池"));
    if (soc >= 0) {
        snprintf(buf, sizeof(buf), "%d%%", soc);
        fb_draw_text(80, CONTENT_Y + 60, buf, 11, soc_col);
    } else {
        fb_draw_text(80, CONTENT_Y + 80, "--%", 9, C_GRAY);
    }
    // 电量条
    {
        int bx = 80, by = CONTENT_Y + 170, bw = 320, bh = 40;
        fb_fill_rect(bx - 3, by - 3, bw + 6, bh + 6, C_LGRAY);
        fb_fill_rect(bx, by, bw, bh, C_DARK);
        if (soc > 0) fb_fill_rect(bx, by, bw * soc / 100, bh, soc_col);
    }
    if (vbat_ok) {
        snprintf(buf, sizeof(buf), "%.3f V", vbat_v);
        fb_draw_text(490, CONTENT_Y + 55, buf, 6, C_WHITE);
        snprintf(buf, sizeof(buf), "%+.0f mA  %.2f W", vbat_a * 1000.0f, vbat_v * vbat_a);
        draw_txt(490, CONTENT_Y + 120, 30, C_LGRAY, buf);
        // 三态供电判定（2026-08-16 用户口径）：
        //   1. VBUS < 4.0V（未插电源）  → 正在放电（黄）
        //   2. 插电且电池不再净放电      → 正在充电（绿）
        //   3. 插电但电池仍净放电        → 供电不足（红）——5V 输入到顶也
        //      喂不饱整机，电池在补差额（实测 VBUS 4.7~5.0V 随线长变化，
        //      判"插电"用 4.0V 阈值；正电流=放电，INA219 IN+=VBAT）
        bool plugged = vbus_ok && vbus_v > 4.0f;
        float p_bat = vbat_v * vbat_a;   // 正 = 电池净放电功率
        if (!plugged) {
            draw_txt(490, CONTENT_Y + 165, 34, C_YELL, tr("DISCHARGING", "正在放电"));
        } else if (p_bat > 0.15f) {
            draw_txt(490, CONTENT_Y + 160, 34, C_RED,
                     tr("POWER DEFICIT!", "供电不足！"));
            snprintf(buf, sizeof(buf),
                     tr("PLUGGED IN, BATTERY STILL DRAINS %.1fW (INPUT MAXED)",
                        "已插电但电池仍在放 %.1fW，输入已到上限"),
                     p_bat);
            draw_txt(490, CONTENT_Y + 202, 20, C_RED, buf);
        } else {
            draw_txt(490, CONTENT_Y + 165, 34, C_GREEN, tr("CHARGING", "正在充电"));
        }
    } else {
        draw_txt(490, CONTENT_Y + 100, 32, C_RED,
                 tr("INA219 READ FAIL", "INA219 读取失败"));
    }
    // 两颗电量计交叉读数
    if (cw_soc >= 0)
        snprintf(buf, sizeof(buf), "CW2015 %.3fV %d%%", cw_v, cw_soc);
    else
        snprintf(buf, sizeof(buf), "CW2015 --");
    fb_draw_text(80, CONTENT_Y + 222, buf, 2, C_GRAY);
    if (stc_soc >= 0)
        snprintf(buf, sizeof(buf), "STC3117 %.3fV %.1f%%", stc_v, stc_soc);
    else
        snprintf(buf, sizeof(buf), "STC3117 --");
    fb_draw_text(490, CONTENT_Y + 222, buf, 2, C_GRAY);

    // ---- USB 供电卡片 ----
    ui_card(40, CONTENT_Y + 270, 944, 120, C_CYAN, tr("USB POWER", "USB 供电"));
    if (vbus_ok) {
        snprintf(buf, sizeof(buf), "%.3f V", vbus_v);
        fb_draw_text(80, CONTENT_Y + 320, buf, 5, C_WHITE);
        snprintf(buf, sizeof(buf), "%.0f mA", vbus_a * 1000.0f);
        draw_txt(430, CONTENT_Y + 318, 36, C_LGRAY, buf);
        snprintf(buf, sizeof(buf), "%.2f W", vbus_v * vbus_a);
        draw_txt(720, CONTENT_Y + 318, 36, C_LGRAY, buf);
    } else {
        draw_txt(80, CONTENT_Y + 318, 32, C_RED,
                 tr("INA219 READ FAIL", "INA219 读取失败"));
    }

    // ---- 传感器在位卡片 ----
    sensors_probe_maybe();
    snprintf(buf, sizeof(buf), tr("SENSORS %d/%d", "传感器在位 %d/%d"),
             s_present_ok, (int)N_SENSORS);
    ui_card(40, CONTENT_Y + 410, 944, 130, C_GREEN, buf);
    for (int i = 0; i < N_SENSORS; i++) {
        int col = i % 5, row = i / 5;
        int x = 80 + col * 182, y = CONTENT_Y + 460 + row * 40;
        fb_fill_rect(x, y + 2, 12, 12, s_present[i] ? C_GREEN : C_RED);
        fb_draw_text(x + 24, y, k_sensors[i].name, 2,
                     s_present[i] ? C_LGRAY : C_GRAY);
    }

    // ---- 底部 ----
    snprintf(buf, sizeof(buf),
             tr("UP %lu S · PRESS □ TO RETURN TO PI",
                "已运行 %lu 秒 · 按 □ 键返回树莓派画面"),
             (unsigned long)uptime_s);
    draw_txt_centered(726, 28, C_YELL, buf);
}

// ---------------------------------------------------------------------------
// BATT 页：电池曲线（SOC% 绿 / 电压 青 / 电流 黄，1 小时窗口 5s 步进）
// ---------------------------------------------------------------------------
#define GRAPH_X   70
#define GRAPH_Y   (CONTENT_Y + 30)
#define GRAPH_W   900
#define GRAPH_H   420

static batt_sample_t s_graph_buf[BATT_LOG_CAP];

// 值→图内 y 坐标（value 在 [lo,hi] 内线性映射，超界饱和）
static int graph_map(float value, float lo, float hi)
{
    float t = (value - lo) / (hi - lo);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return GRAPH_Y + GRAPH_H - (int)(t * GRAPH_H);
}

static void draw_page_batt(void)
{
    char buf[64];
    int n = batt_log_get(s_graph_buf, BATT_LOG_CAP);

    // 图框 + 网格（25% 步进横线，15 分钟竖线）
    fb_fill_rect(GRAPH_X - 4, GRAPH_Y - 4, GRAPH_W + 8, GRAPH_H + 8, C_LGRAY);
    fb_fill_rect(GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H, C_BLACK);
    for (int i = 1; i < 4; i++) {
        fb_fill_rect(GRAPH_X, GRAPH_Y + GRAPH_H * i / 4, GRAPH_W, 1, C_DARK);
        fb_fill_rect(GRAPH_X + GRAPH_W * i / 4, GRAPH_Y, 1, GRAPH_H, C_DARK);
    }
    // 纵轴标注（SOC 尺度）
    fb_draw_text(GRAPH_X - 60, GRAPH_Y - 8, "100", 2, C_DGREEN);
    fb_draw_text(GRAPH_X - 60, GRAPH_Y + GRAPH_H / 2 - 8, "50", 2, C_DGREEN);
    fb_draw_text(GRAPH_X - 48, GRAPH_Y + GRAPH_H - 8, "0", 2, C_DGREEN);

    if (n >= 2) {
        // x 轴固定 1 小时窗口（720 槽），数据不足时靠右对齐（最新在最右）
        for (int i = 1; i < n; i++) {
            int x0 = GRAPH_X + (BATT_LOG_CAP - n + i - 1) * GRAPH_W / (BATT_LOG_CAP - 1);
            int x1 = GRAPH_X + (BATT_LOG_CAP - n + i) * GRAPH_W / (BATT_LOG_CAP - 1);
            const batt_sample_t *a = &s_graph_buf[i - 1], *b = &s_graph_buf[i];
            if (a->soc >= 0 && b->soc >= 0) {
                fb_draw_line(x0, graph_map(a->soc, 0, 100),
                             x1, graph_map(b->soc, 0, 100), 3, C_GREEN);
            }
            if (a->mv && b->mv) {
                fb_draw_line(x0, graph_map(a->mv, 3000, 4400),
                             x1, graph_map(b->mv, 3000, 4400), 2, C_CYAN);
                fb_draw_line(x0, graph_map(a->ma, -2000, 2000),
                             x1, graph_map(b->ma, -2000, 2000), 2, C_YELL);
            }
        }
    } else {
        draw_txt_centered(GRAPH_Y + GRAPH_H / 2 - 20, 36, C_GRAY,
                          tr("COLLECTING DATA...", "正在采集数据……"));
    }

    // 图例 + 最新读数
    int ly = GRAPH_Y + GRAPH_H + 30;
    fb_fill_rect(GRAPH_X, ly + 4, 24, 12, C_GREEN);
    fb_draw_text(GRAPH_X + 36, ly, "SOC 0-100%", 2, C_LGRAY);
    fb_fill_rect(GRAPH_X + 240, ly + 4, 24, 12, C_CYAN);
    fb_draw_text(GRAPH_X + 276, ly, "V 3.0-4.4", 2, C_LGRAY);
    fb_fill_rect(GRAPH_X + 460, ly + 4, 24, 12, C_YELL);
    fb_draw_text(GRAPH_X + 496, ly, "MA -2000..2000", 2, C_LGRAY);
    snprintf(buf, sizeof(buf), tr("%d/%d SAMPLES", "%d/%d 采样"), n, BATT_LOG_CAP);
    draw_txt(GRAPH_X + 740, ly - 4, 22, C_GRAY, buf);

    if (n > 0) {
        const batt_sample_t *last = &s_graph_buf[n - 1];
        if (last->soc >= 0)
            snprintf(buf, sizeof(buf), tr("NOW: %d%%  %d.%03dV  %+dmA",
                                          "当前: %d%%  %d.%03dV  %+dmA"),
                     last->soc, last->mv / 1000, last->mv % 1000, last->ma);
        else
            snprintf(buf, sizeof(buf), tr("NOW: --%%  %d.%03dV  %+dmA",
                                          "当前: --%%  %d.%03dV  %+dmA"),
                     last->mv / 1000, last->mv % 1000, last->ma);
        draw_txt(GRAPH_X, ly + 40, 34, C_WHITE, buf);
    }
    draw_txt_centered(726, 26, C_GRAY,
                      tr("1 HOUR WINDOW / 5S STEP", "1 小时窗口 / 5 秒步进"));
}

// ---------------------------------------------------------------------------
// TOUCH 页：触摸测试（画布轨迹 + 实时坐标 + CLEAR 按钮）
// ---------------------------------------------------------------------------
#define CANVAS_X   40
#define CANVAS_Y   (CONTENT_Y + 46)
#define CANVAS_W   944
#define CANVAS_H   500
#define CLEAR_X    820
#define CLEAR_Y    690
#define CLEAR_W    164
#define CLEAR_H    64

#define TRAIL_CAP  4096
static uint16_t s_trail_x[TRAIL_CAP], s_trail_y[TRAIL_CAP];
static int s_trail_n = 0;
static int s_touch_last_x = -1, s_touch_last_y = -1;
static uint32_t s_touch_events = 0;

static uint16_t trail_color(int i)
{
    switch ((i / 64) % 4) {
    case 0: return C_GREEN;
    case 1: return C_CYAN;
    case 2: return C_YELL;
    default: return C_ORANGE;
    }
}

static void draw_touch_coords(void)
{
    char buf[48];
    fb_fill_rect(40, 692, 760, 40, C_DARK);
    if (s_touch_last_x >= 0) {
        snprintf(buf, sizeof(buf), tr("X:%4d Y:%4d  EVENTS:%lu",
                                      "X:%4d Y:%4d  事件:%lu"),
                 s_touch_last_x, s_touch_last_y, (unsigned long)s_touch_events);
    } else {
        snprintf(buf, sizeof(buf), tr("TOUCH THE CANVAS  EVENTS:%lu",
                                      "触摸画布试试  事件:%lu"),
                 (unsigned long)s_touch_events);
    }
    draw_txt(48, 696, 28, C_WHITE, buf);
}

static void draw_page_touch(void)
{
    draw_txt(CANVAS_X, CONTENT_Y, 28, C_CYAN,
             tr("GT911 TOUCH TEST - DRAW ON CANVAS", "GT911 触摸测试——在画布上绘制"));
    // 画布
    fb_fill_rect(CANVAS_X - 4, CANVAS_Y - 4, CANVAS_W + 8, CANVAS_H + 8, C_LGRAY);
    fb_fill_rect(CANVAS_X, CANVAS_Y, CANVAS_W, CANVAS_H, C_BLACK);
    // 重放轨迹
    for (int i = 0; i < s_trail_n; i++) {
        fb_fill_rect(s_trail_x[i], s_trail_y[i], 8, 8, trail_color(i));
    }
    // CLEAR 按钮
    fb_fill_rect(CLEAR_X, CLEAR_Y, CLEAR_W, CLEAR_H, C_NAVY);
    fb_fill_rect(CLEAR_X, CLEAR_Y, CLEAR_W, 4, C_CYAN);
    const char *clr = tr("CLEAR", "清除");
    draw_txt(CLEAR_X + (CLEAR_W - txt_w(32, clr)) / 2, CLEAR_Y + 16, 32, C_WHITE, clr);
    draw_touch_coords();
}

// ---------------------------------------------------------------------------
// PI SIG 页：VSYNC 探测状态
// ---------------------------------------------------------------------------
static void draw_page_pisig(void)
{
    char buf[64];
    bool sig = vsync_mon_signal();
    float fps = vsync_mon_fps();

    if (sig) {
        draw_txt_centered(CONTENT_Y + 30, 64, C_GREEN, tr("SIGNAL", "有信号"));
        snprintf(buf, sizeof(buf), "%d.%d", (int)fps, (int)(fps * 10) % 10);
        fb_draw_text_centered(CONTENT_Y + 160, buf, 18, C_WHITE);
        draw_txt_centered(CONTENT_Y + 320, 44, C_LGRAY,
                          vsync_mon_locked()
                              ? tr("FPS (FROZEN, PROBE OFF)", "FPS（冻结值，探测已关）")
                              : tr("FPS (DPI REFRESH RATE)", "FPS（DPI 刷新率）"));
    } else {
        draw_txt_centered(CONTENT_Y + 30, 64, C_RED, tr("NO SIGNAL", "无信号"));
        draw_txt_centered(CONTENT_Y + 190, 36, C_GRAY,
                          tr("PI RGB OUTPUT NOT DETECTED", "未检测到树莓派 RGB 输出"));
    }

    int y = CONTENT_Y + 420;
    snprintf(buf, sizeof(buf), tr("FRAMES: %lu", "帧计数: %lu"),
             (unsigned long)vsync_mon_frames());
    draw_txt(80, y, 28, C_LGRAY, buf);
    int64_t age = vsync_mon_age_ms();
    if (age >= 0)
        snprintf(buf, sizeof(buf), tr("LAST VSYNC: %lld MS AGO",
                                      "上次 VSYNC: %lld 毫秒前"), (long long)age);
    else
        snprintf(buf, sizeof(buf), "%s", tr("LAST VSYNC: NEVER", "上次 VSYNC: 从未"));
    draw_txt(80, y + 44, 28, C_LGRAY, buf);
    snprintf(buf, sizeof(buf), tr("INT STORMS: %lu", "中断风暴: %lu"),
             (unsigned long)vsync_mon_storms());
    draw_txt(80, y + 88, 28, C_LGRAY, buf);

    draw_txt(80, y + 150, 22, C_GRAY,
             tr("SENSE PATH: PI GPIO2 (DPI VSYNC) - R83 -",
                "探测链路: Pi GPIO2 (DPI VSYNC) → R83 →"));
    draw_txt(80, y + 180, 22, C_GRAY,
             tr("AW9523 P0_7 INT - GPIO5 EDGE COUNT",
                "AW9523 P0_7 中断 → GPIO5 沿计数"));
    draw_txt_centered(726, 28, C_YELL,
                      tr("PRESS □ TO RETURN TO PI", "按 □ 键返回树莓派画面"));
}

// ---------------------------------------------------------------------------
// 设置页：语言切换（触摸两个大按钮，选中即写 NVS）
// ---------------------------------------------------------------------------
#define LBTN_W    360
#define LBTN_H    120
#define LBTN_Y    (CONTENT_Y + 170)
#define LBTN_EN_X 120
#define LBTN_ZH_X 544

static void draw_page_setup(void)
{
    draw_txt(60, CONTENT_Y + 20, 36, C_CYAN, tr("Language 语言", "语言 Language"));
    draw_txt(60, CONTENT_Y + 84, 24, C_GRAY,
             tr("Touch to switch. Saved to flash (NVS).",
                "触摸切换，选择会写入 Flash（NVS）持久保存"));

    for (int i = 0; i < 2; i++) {
        int x = (i == 0) ? LBTN_EN_X : LBTN_ZH_X;
        bool sel = (s_lang == ((i == 0) ? LANG_EN : LANG_ZH));
        fb_fill_rect(x - 5, LBTN_Y - 5, LBTN_W + 10, LBTN_H + 10,
                     sel ? C_CYAN : C_LGRAY);
        fb_fill_rect(x, LBTN_Y, LBTN_W, LBTN_H, sel ? C_NAVY : C_BLACK);
        const char *label = (i == 0) ? "English" : "中文";
        draw_txt(x + (LBTN_W - txt_w(44, label)) / 2, LBTN_Y + 36, 44,
                 sel ? C_WHITE : C_GRAY, label);
    }

    if (!ttf_font_ready()) {
        draw_txt(60, LBTN_Y + LBTN_H + 60, 24, C_RED,
                 "TTF FONT MISSING - CHINESE UNAVAILABLE");
    }
    draw_txt_centered(726, 28, C_YELL,
                      tr("PRESS □ TO RETURN TO PI", "按 □ 键返回树莓派画面"));
}

// ---------------------------------------------------------------------------
// 页面调度 + 触摸
// ---------------------------------------------------------------------------
void ui_page_draw(uint32_t uptime_s)
{
    s_last_uptime_s = uptime_s;
    fb_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, C_DARK);
    draw_tab_bar();
    switch (s_tab) {
    case UI_TAB_DASH:  draw_page_dash(uptime_s); break;
    case UI_TAB_BATT:  draw_page_batt(); break;
    case UI_TAB_TOUCH: draw_page_touch(); break;
    case UI_TAB_PISIG: draw_page_pisig(); break;
    case UI_TAB_SETUP: draw_page_setup(); break;
    default: break;
    }
    fb_flush();
}

static bool s_dirty = false;
static int64_t s_last_flush_us = 0;

void ui_maybe_flush(void)
{
    if (!s_dirty) return;
    int64_t now = esp_timer_get_time();
    if (now - s_last_flush_us < 66000) return;   // 限频 ~15Hz
    s_last_flush_us = now;
    s_dirty = false;
    fb_flush();
}

void ui_handle_touch(int x, int y, bool pressed)
{
    if (!pressed) {
        s_touch_last_x = s_touch_last_y = -1;
        return;
    }
    s_touch_events++;

    // Tab 栏命中：切页
    if (y < TAB_BAR_H) {
        int idx = x / TAB_W;
        if (idx >= 0 && idx < UI_TAB_COUNT && (ui_tab_t)idx != s_tab) {
            s_tab = (ui_tab_t)idx;
            ui_page_draw(s_last_uptime_s);
        }
        return;
    }

    // 设置页：语言按钮（重复点击同一按钮无副作用）
    if (s_tab == UI_TAB_SETUP) {
        if (y >= LBTN_Y && y < LBTN_Y + LBTN_H) {
            ui_lang_t want;
            if (x >= LBTN_EN_X && x < LBTN_EN_X + LBTN_W)      want = LANG_EN;
            else if (x >= LBTN_ZH_X && x < LBTN_ZH_X + LBTN_W) want = LANG_ZH;
            else return;
            if (want != s_lang) {
                s_lang = want;
                lang_save();
                ui_page_draw(s_last_uptime_s);
            }
        }
        return;
    }

    if (s_tab != UI_TAB_TOUCH) return;

    // CLEAR 按钮
    if (x >= CLEAR_X && x < CLEAR_X + CLEAR_W && y >= CLEAR_Y && y < CLEAR_Y + CLEAR_H) {
        s_trail_n = 0;
        ui_page_draw(s_last_uptime_s);
        return;
    }
    // 画布内：记轨迹 + 增量画点
    if (x >= CANVAS_X && x < CANVAS_X + CANVAS_W - 8 &&
        y >= CANVAS_Y && y < CANVAS_Y + CANVAS_H - 8) {
        if (s_trail_n < TRAIL_CAP) {
            s_trail_x[s_trail_n] = (uint16_t)x;
            s_trail_y[s_trail_n] = (uint16_t)y;
            fb_fill_rect(x, y, 8, 8, trail_color(s_trail_n));
            s_trail_n++;
        }
        s_touch_last_x = x;
        s_touch_last_y = y;
        draw_touch_coords();
        s_dirty = true;
    }
}

// ---------------------------------------------------------------------------
// 耳机插拔抢屏页（从 main.c 移入，MIC RMS 由调用方传入）
// ---------------------------------------------------------------------------
void ui_draw_hp_page(int raw, float rms_l, float rms_r)
{
    char buf[64];
    fb_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, C_DARK);
    fb_fill_rect(0, 0, LCD_H_RES, 110, C_NAVY);
    fb_fill_rect(0, 110, LCD_H_RES, 4, C_CYAN);
    draw_txt_centered(26, 44, C_WHITE, tr("HEADPHONE EVENT", "耳机插拔事件"));

    snprintf(buf, sizeof(buf), "HP-DET=%d", raw);
    fb_draw_text_centered(180, buf, 14, raw ? C_GREEN : C_YELL);

    // 极性猜测：R166 上拉到 AUDIO_3V3，假设插入接地 → LOW=插入（待实测确认）
    draw_txt_centered(330, 52, C_LGRAY,
                      raw ? tr("GUESS: UNPLUGGED", "推测：已拔出")
                          : tr("GUESS: PLUGGED", "推测：已插入"));
    draw_txt_centered(408, 26, C_GRAY, tr("POLARITY UNCONFIRMED", "极性待实测确认"));

    // 双声道 RMS 仪表（最近 1 秒窗口，主机在录音时才有数据流）
    snprintf(buf, sizeof(buf), "MIC RMS L:%5.0f R:%5.0f", rms_l, rms_r);
    fb_draw_text_centered(500, buf, 5, C_CYAN);
    // 简易电平条（满量程按 4000 归一，方便看语音级信号）
    int bw = 700, bh = 36, bx = (LCD_H_RES - bw) / 2;
    for (int ch = 0; ch < 2; ch++) {
        int by = 580 + ch * 60;
        float v = ch == 0 ? rms_l : rms_r;
        int fill = (int)(v / 4000.0f * bw);
        if (fill > bw) fill = bw;
        fb_fill_rect(bx - 3, by - 3, bw + 6, bh + 6, C_LGRAY);
        fb_fill_rect(bx, by, bw, bh, C_BLACK);
        if (fill > 0) fb_fill_rect(bx, by, fill, bh, ch == 0 ? C_GREEN : C_CYAN);
        fb_draw_text(bx - 40, by + 6, ch == 0 ? "L" : "R", 3, C_WHITE);
    }

    draw_txt_centered(720, 28, C_YELL, tr("AUTO RETURN IN 2S", "2 秒后自动返回"));
    fb_flush();
}
