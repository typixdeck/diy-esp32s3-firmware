#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "board_pins.h"
#include "i18n.h"
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
// 语言 + 主题（设置页可切换，NVS 持久化：namespace "ui" / key "lang"/"theme"）
// ---------------------------------------------------------------------------
// 语言顺序 = NVS 里的数值 = 设置页按钮顺序，只能在末尾追加
typedef enum { LANG_EN = 0, LANG_ZH = 1, LANG_TW = 2, LANG_JA = 3, LANG_COUNT } ui_lang_t;
static const char *k_lang_name[LANG_COUNT] = { "English", "简体中文", "繁體中文", "日本語" };
static ui_lang_t s_lang = LANG_EN;

typedef enum {
    TH_CYBER = 0,     // V1 赛博朋克 HUD：霓虹青描边 + 切角边框
    TH_MINIMAL,       // V2 极简暗色：大圆角卡片 + 薄荷绿强调
    TH_TERMINAL,      // V3 复古琥珀终端：单色 + 像素字
    TH_EV,            // V4 EV 仪表：大圆环电量表
    TH_COUNT,
} ui_theme_t;
static ui_theme_t s_theme = TH_CYBER;

// RGB888 → RGB565（编译期常量友好）
#define RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

typedef struct {
    uint16_t bg;          // 页面背景
    uint16_t card;        // 卡片底
    uint16_t card2;       // 卡片内元素底（chip/进度条槽）
    uint16_t frame;       // 卡片描边
    uint16_t text;        // 主文本
    uint16_t text2;       // 次级文本
    uint16_t dim;         // 弱文本
    uint16_t accent;      // 主题强调色
    uint16_t accent2;     // 副强调色（FPS 芯片等）
    uint16_t good, warn, bad;
    uint16_t tabbar;      // Tab 栏底
    uint16_t tab_sel_bg, tab_sel_fg, tab_fg;
} theme_pal_t;

static const theme_pal_t k_pal[TH_COUNT] = {
    [TH_CYBER] = {
        .bg = RGB(5, 10, 18),      .card = RGB(8, 16, 28),   .card2 = RGB(13, 26, 44),
        .frame = RGB(0, 110, 140), .text = RGB(230, 245, 255),
        .text2 = RGB(150, 180, 200), .dim = RGB(90, 110, 130),
        .accent = RGB(0, 229, 255), .accent2 = RGB(255, 45, 120),
        .good = RGB(0, 255, 106),  .warn = RGB(255, 210, 77), .bad = RGB(255, 59, 59),
        .tabbar = RGB(4, 12, 20),  .tab_sel_bg = RGB(10, 32, 46),
        .tab_sel_fg = RGB(0, 229, 255), .tab_fg = RGB(120, 150, 170),
    },
    [TH_MINIMAL] = {
        .bg = RGB(17, 20, 23),     .card = RGB(28, 33, 39),  .card2 = RGB(40, 47, 55),
        .frame = RGB(45, 53, 62),  .text = RGB(242, 245, 247),
        .text2 = RGB(170, 180, 189), .dim = RGB(107, 117, 126),
        .accent = RGB(61, 220, 151), .accent2 = RGB(61, 220, 151),
        .good = RGB(61, 220, 151), .warn = RGB(230, 162, 60), .bad = RGB(224, 90, 90),
        .tabbar = RGB(28, 33, 39), .tab_sel_bg = RGB(61, 220, 151),
        .tab_sel_fg = RGB(10, 16, 13), .tab_fg = RGB(170, 180, 189),
    },
    [TH_TERMINAL] = {
        .bg = RGB(0, 0, 0),        .card = RGB(0, 0, 0),     .card2 = RGB(30, 19, 0),
        .frame = RGB(255, 176, 0), .text = RGB(255, 176, 0),
        .text2 = RGB(192, 128, 0), .dim = RGB(122, 85, 0),
        .accent = RGB(255, 176, 0), .accent2 = RGB(255, 176, 0),
        .good = RGB(255, 176, 0),  .warn = RGB(255, 220, 90), .bad = RGB(255, 80, 0),
        .tabbar = RGB(0, 0, 0),    .tab_sel_bg = RGB(255, 176, 0),
        .tab_sel_fg = RGB(0, 0, 0), .tab_fg = RGB(192, 128, 0),
    },
    [TH_EV] = {
        .bg = RGB(0, 0, 0),        .card = RGB(20, 24, 29),  .card2 = RGB(30, 36, 43),
        .frame = RGB(42, 49, 57),  .text = RGB(240, 244, 248),
        .text2 = RGB(185, 194, 204), .dim = RGB(102, 112, 122),
        .accent = RGB(46, 139, 255), .accent2 = RGB(46, 229, 107),
        .good = RGB(46, 229, 107), .warn = RGB(255, 214, 10), .bad = RGB(255, 69, 58),
        .tabbar = RGB(0, 0, 0),    .tab_sel_bg = RGB(0, 0, 0),
        .tab_sel_fg = RGB(240, 244, 248), .tab_fg = RGB(140, 150, 160),
    },
};

static inline const theme_pal_t *pal(void) { return &k_pal[s_theme]; }

// 多语言取词：代码里保持 (en, 简体) 双参；繁體/日语按 en 键查 i18n.h 表，
// 查不到繁體回退简体、日语回退英文。非英文都需要 TTF；font 分区没刷时强制回英文。
static const char *tr(const char *en, const char *zh)
{
    if (s_lang == LANG_EN || !ttf_font_ready()) return en;
    if (s_lang == LANG_ZH) return zh;
    for (size_t i = 0; i < K_I18N_COUNT; i++) {
        const i18n_entry_t *e = &k_i18n[i];
        if (strcmp(e->en, en) != 0) continue;
        if (e->zh && strcmp(e->zh, zh) != 0) continue;
        const char *t = (s_lang == LANG_TW) ? e->tw : e->ja;
        if (t) return t;
        break;
    }
    return (s_lang == LANG_TW) ? zh : en;
}

static void prefs_load(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS 不可用（%s），语言/主题设置不持久", esp_err_to_name(err));
        return;
    }
    nvs_handle_t h;
    if (nvs_open("ui", NVS_READONLY, &h) == ESP_OK) {
        uint8_t v = 0;
        if (nvs_get_u8(h, "lang", &v) == ESP_OK && v < LANG_COUNT) {
            s_lang = (ui_lang_t)v;
        }
        if (nvs_get_u8(h, "theme", &v) == ESP_OK && v < TH_COUNT) {
            s_theme = (ui_theme_t)v;
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG, "UI 语言：%s 主题：%d", k_lang_name[s_lang], s_theme);
}

static void prefs_save(void)
{
    nvs_handle_t h;
    if (nvs_open("ui", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "lang", (uint8_t)s_lang);
    nvs_set_u8(h, "theme", (uint8_t)s_theme);
    nvs_commit(h);
    nvs_close(h);
}

// 远程调试（CDC THEME_n / TAB_n 命令）：跨任务只置请求，UI 重绘时消费，
// 避免 cdc_stats_task 直接画帧缓冲与 GUI 任务打架
static volatile int s_req_theme = -1, s_req_tab = -1, s_req_lang = -1;
void ui_request_theme(int t) { if (t >= 0 && t < TH_COUNT) s_req_theme = t; }
void ui_request_tab(int t)   { if (t >= 0 && t < UI_TAB_COUNT) s_req_tab = t; }
void ui_request_lang(int l)  { if (l >= 0 && l < LANG_COUNT) s_req_lang = l; }

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

// 宽度受限的文本：超出 maxw 就逐级缩小字号（最小 14px），保证不冲出卡片
static void draw_txt_fit(int x, int y, int size, int maxw, uint16_t color,
                         const char *utf8)
{
    while (size > 14 && txt_w(size, utf8) > maxw) size -= 2;
    draw_txt(x, y, size, color, utf8);
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

// ---------------------------------------------------------------------------
// 主题化绘制原语
// ---------------------------------------------------------------------------
// 圆角矩形填充：角行按圆方程收缩（每行两次浮点 sqrt，只有 2r 行，代价可忽略）
static void fb_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color)
{
    if (r <= 0) { fb_fill_rect(x, y, w, h, color); return; }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    for (int dy = 0; dy < h; dy++) {
        int inset = 0;
        if (dy < r) {
            float t = (float)(r - dy);
            inset = r - (int)(sqrtf((float)r * r - t * t) + 0.5f);
        } else if (dy >= h - r) {
            float t = (float)(dy - (h - 1 - r));
            inset = r - (int)(sqrtf((float)r * r - t * t) + 0.5f);
        }
        fb_fill_rect(x + inset, y + dy, w - 2 * inset, 1, color);
    }
}

// 圆角矩形描边卡片：先描边色填底，再内缩 bw 填卡片色（微量过绘，代码最短）
static void fb_round_card(int x, int y, int w, int h, int r, int bw,
                          uint16_t border, uint16_t fill)
{
    fb_fill_round_rect(x, y, w, h, r, border);
    fb_fill_round_rect(x + bw, y + bw, w - 2 * bw, h - 2 * bw,
                       r > bw ? r - bw : 0, fill);
}

// 1px 矩形描边（终端主题边框）
static void fb_rect_outline(int x, int y, int w, int h, int t, uint16_t color)
{
    fb_fill_rect(x, y, w, t, color);
    fb_fill_rect(x, y + h - t, w, t, color);
    fb_fill_rect(x, y, t, h, color);
    fb_fill_rect(x + w - t, y, t, h, color);
}

// 赛博主题四角亮角标（L 形，臂长 len 粗 t）
static void fb_corner_brackets(int x, int y, int w, int h, int len, int t,
                               uint16_t color)
{
    fb_fill_rect(x, y, len, t, color);             fb_fill_rect(x, y, t, len, color);
    fb_fill_rect(x + w - len, y, len, t, color);   fb_fill_rect(x + w - t, y, t, len, color);
    fb_fill_rect(x, y + h - t, len, t, color);     fb_fill_rect(x, y + h - len, t, len, color);
    fb_fill_rect(x + w - len, y + h - t, len, t, color);
    fb_fill_rect(x + w - t, y + h - len, t, len, color);
}

// 分段电量条（赛博/终端主题）：nseg 段，2px 间隙
static void fb_segment_bar(int x, int y, int w, int h, int nseg, int pct,
                           uint16_t on, uint16_t off)
{
    int seg_w = (w - (nseg - 1) * 3) / nseg;
    int lit = (pct * nseg + 50) / 100;
    for (int i = 0; i < nseg; i++) {
        fb_fill_rect(x + i * (seg_w + 3), y, seg_w, h, i < lit ? on : off);
    }
}

// EV 主题圆环电量表：圆心 (cx,cy)，外/内半径，pct 0..100，从正上方顺时针。
// 颜色沿弧度青→绿渐变；只扫环带 bounding box，环外像素零成本跳过。
static void fb_ring_gauge(int cx, int cy, int r_out, int r_in, int pct,
                          uint16_t track)
{
    float fill_end = (float)pct / 100.0f * 2.0f * (float)M_PI;
    int r_out2 = r_out * r_out, r_in2 = r_in * r_in;
    for (int y = -r_out; y <= r_out; y++) {
        int fy = cy + y;
        if (fy < 0 || fy >= LCD_V_RES) continue;
        uint16_t *row = s_fb + fy * LCD_H_RES;
        for (int x = -r_out; x <= r_out; x++) {
            int fx = cx + x;
            if (fx < 0 || fx >= LCD_H_RES) continue;
            int d2 = x * x + y * y;
            if (d2 > r_out2 || d2 < r_in2) continue;
            // atan2 以正上方为 0、顺时针增长
            float a = atan2f((float)x, (float)-y);
            if (a < 0) a += 2.0f * (float)M_PI;
            if (a <= fill_end && pct > 0) {
                float f = a / (2.0f * (float)M_PI);   // 0..1 沿弧渐变
                int rr = 0;
                int gg = 200 + (int)(55 * f);
                int bb = 180 - (int)(140 * f);
                row[fx] = RGB(rr, gg, bb);
            } else {
                row[fx] = track;
            }
        }
    }
}

static void fb_flush(void)
{
    if (s_panel && s_fb) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_H_RES, LCD_V_RES, s_fb);
    }
}

const uint16_t *ui_framebuffer(void)
{
    return s_fb;
}

// 截图快照：与整页重绘互斥，避免 CDC 任务在 GUI 画到一半时拷走撕裂帧
static SemaphoreHandle_t s_draw_mtx;

bool ui_snapshot(uint16_t *dst)
{
    if (!s_fb || !dst) return false;
    if (s_draw_mtx) xSemaphoreTake(s_draw_mtx, portMAX_DELAY);
    memcpy(dst, s_fb, (size_t)LCD_H_RES * LCD_V_RES * 2);
    if (s_draw_mtx) xSemaphoreGive(s_draw_mtx);
    return true;
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
    } else {
        // 开机字标用的打字机体（Special Elite，OFL），子集内嵌在 app 里
        extern const uint8_t _binary_special_elite_subset_ttf_start[];
        extern const uint8_t _binary_special_elite_subset_ttf_end[];
        ttf_font_add_face(TTF_FACE_DECO, _binary_special_elite_subset_ttf_start,
                          (size_t)(_binary_special_elite_subset_ttf_end -
                                   _binary_special_elite_subset_ttf_start));
    }
    prefs_load();   // NVS 里的语言/主题偏好
    if (!s_draw_mtx) s_draw_mtx = xSemaphoreCreateMutex();
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

// ---------------------------------------------------------------------------
// 开机画面（2026-09-10 重做）：
//   字标 "TypixDeck" 用 Special Elite 打字机体逐字敲出（带闪烁光标），
//   下方 SpinKit "Wave" 五根竖条（MIT，原版 keyframes：0%/40%/100%→0.4，20%→1.0），
//   再下方 "WAITING FOR PI SIGNAL" 宽字距小标。
//   彩蛋：等待 20s/45s/90s 分级提示逐字打出（45s 那条是真实 SOP：SW8 拨 USB6 侧再开机）；
//   Pi 出图瞬间竖条满格变绿 + "PI SIGNAL LOCKED" 定格 350ms 再交屏（ui_boot_signal_locked）；
//   右下角固件版本/编译日期。每帧只重绘字标带、竖条带、提示带三块脏区。
// ---------------------------------------------------------------------------
#define BOOT_TICK_MS   80
#define C_MINT         RGB(93, 202, 165)
#define WM_TEXT        "TypixDeck"
#define WM_SIZE        120
#define WM_Y           190
#define WM_SPACING     6
#define WAVE_N         5
#define WAVE_W         18
#define WAVE_GAP       16
#define WAVE_H         90
#define WAVE_CY        470
#define SUB_Y          556
#define HINT_Y         640
#define HINT_DY        40

// 取 UTF-8 串前 n 个字符（按字符不按字节），写入 out
static void utf8_prefix(const char *src, int n, char *out, size_t cap)
{
    size_t o = 0;
    for (const char *p = src; *p && n > 0; n--) {
        int len = ((*p & 0xE0) == 0xC0) ? 2 : ((*p & 0xF0) == 0xE0) ? 3 : ((*p & 0xF8) == 0xF0) ? 4 : 1;
        if (o + len >= cap) break;
        memcpy(out + o, p, len); o += len; p += len;
    }
    out[o] = 0;
}
static int utf8_len(const char *s)
{
    int n = 0;
    for (; *s; s++) if ((*s & 0xC0) != 0x80) n++;
    return n;
}

// SpinKit wave：t ∈ [0,1) 周期相位 → scaleY
static float wave_scale(float t)
{
    float e;
    if (t < 0.2f)      { e = t / 0.2f;           return 0.4f + 0.6f * (0.5f - 0.5f * cosf(e * (float)M_PI)); }
    else if (t < 0.4f) { e = (t - 0.2f) / 0.2f;  return 1.0f - 0.6f * (0.5f - 0.5f * cosf(e * (float)M_PI)); }
    return 0.4f;
}

static void boot_draw_wave(uint32_t t_ms, uint16_t color, bool full)
{
    int total = WAVE_N * WAVE_W + (WAVE_N - 1) * WAVE_GAP;
    int x0 = (LCD_H_RES - total) / 2;
    fb_fill_rect(x0 - 4, WAVE_CY - WAVE_H / 2 - 4, total + 8, WAVE_H + 8, C_BLACK);
    for (int i = 0; i < WAVE_N; i++) {
        // SpinKit：第 i 根 animation-delay = -(1.1 - 0.1*i)s，即相位领先 0.1s×i
        float t = (float)((t_ms + (uint32_t)i * 100) % 1200) / 1200.0f;
        float sc = full ? 1.0f : wave_scale(t);
        int h = (int)(WAVE_H * sc + 0.5f);
        fb_fill_rect(x0 + i * (WAVE_W + WAVE_GAP), WAVE_CY - h / 2, WAVE_W, h, color);
    }
}

// 逐字打出一行居中文本（typed = 已显示字符数），带方块光标；返回是否已打完
static bool boot_type_line(int face, int y, int size, int spacing, uint16_t color,
                           const char *text, int typed, bool cursor_on)
{
    char buf[160];
    int n = utf8_len(text);
    if (typed > n) typed = n;
    utf8_prefix(text, typed, buf, sizeof(buf));
    int full_w = (face == TTF_FACE_DECO) ? ttf_text_width_face(face, size, spacing, text)
                                         : txt_w(size, text);
    int x = (LCD_H_RES - full_w) / 2;
    fb_fill_rect(0, y - 4, LCD_H_RES, size + size / 3 + 8, C_BLACK);
    int adv;
    if (face == TTF_FACE_DECO) {
        adv = ttf_draw_text_face(face, s_fb, LCD_H_RES, LCD_V_RES, x, y, size, spacing, color, buf);
    } else {
        draw_txt(x, y, size, color, buf);
        adv = txt_w(size, buf);
    }
    if (typed < n || cursor_on)
        fb_fill_rect(x + adv + 4, y + size / 6, size / 2 > 8 ? size / 2 : 8, size, color);
    return typed >= n;
}

static void boot_draw_version(void)
{
    const esp_app_desc_t *d = esp_app_get_description();
    char buf[80];
    snprintf(buf, sizeof(buf), "fw %s · %s", d->version, d->date);
    int w = txt_w(16, buf);
    draw_txt(LCD_H_RES - w - 24, LCD_V_RES - 34, 16, RGB(70, 78, 86), buf);
}

void ui_boot_anim_tick(int frame, bool show_hint)
{
    (void)show_hint;
    static bool s_static_drawn = false;
    static int  s_hint_stage = 0;     // 0 无；1=20s；2=45s；3=90s
    static int  s_hint_typed = 0;
    uint32_t t_ms = (uint32_t)frame * BOOT_TICK_MS;
    bool deco = ttf_face_ready(TTF_FACE_DECO);

    if (!s_static_drawn) {
        s_static_drawn = true;
        fb_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, C_BLACK);
        if (ttf_font_ready()) boot_draw_version();
    }

    // 1. 字标打字机：每帧（80ms）敲一个字母（0.7s 打完），光标 320ms 周期闪 1s 后消失
    int wm_n = utf8_len(WM_TEXT);
    int typed = frame;
    if (typed <= wm_n + 12) {
        if (deco) {
            bool cur = (typed < wm_n) ? true : ((frame % 4) < 2 && typed < wm_n + 12);
            boot_type_line(TTF_FACE_DECO, WM_Y, WM_SIZE, WM_SPACING, C_WHITE, WM_TEXT, typed, cur);
        } else if (typed == 0) {
            if (ttf_font_ready()) draw_txt_centered(WM_Y, 110, C_WHITE, WM_TEXT);
            else fb_draw_text_centered(WM_Y + 30, "TYPIXDECK", 10, C_WHITE);
        }
        // 小标在字标打完那一帧一次画出
        if (typed == wm_n) {
            const char *sub = "WAITING FOR PI SIGNAL";
            if (deco) {
                int w = ttf_text_width_face(TTF_FACE_DECO, 26, 5, sub);
                ttf_draw_text_face(TTF_FACE_DECO, s_fb, LCD_H_RES, LCD_V_RES,
                                   (LCD_H_RES - w) / 2, SUB_Y, 26, 5, C_GRAY, sub);
            } else {
                fb_draw_text_centered(SUB_Y, sub, 3, C_GRAY);
            }
        }
    }

    // 2. SpinKit wave
    boot_draw_wave(t_ms, C_MINT, false);

    // 3. 分级等待提示（逐字打出）
    int want = (t_ms >= 90000) ? 3 : (t_ms >= 45000) ? 2 : (t_ms >= 20000) ? 1 : 0;
    if (want != s_hint_stage) { s_hint_stage = want; s_hint_typed = 0; }
    if (s_hint_stage > 0 && ttf_font_ready()) {
        const char *l1 = (s_hint_stage == 1) ? tr("STILL WAITING FOR PI SIGNAL", "仍在等待树莓派信号")
                       : (s_hint_stage == 2) ? tr("HINT: FLIP SW8 TO THE USB6 SIDE, THEN POWER ON",
                                                  "提示：先把 SW8 拨到 USB6 侧再开机")
                       : tr("STILL HERE. SO IS THE PI, PROBABLY.", "还在等。树莓派大概也在。");
        const char *l2 = tr("PRESS □ KEY FOR SYSTEM MONITOR", "按 □ 键进入系统监控");
        int n1 = utf8_len(l1);
        if (s_hint_typed <= n1 + 8) {
            bool done = boot_type_line(TTF_FACE_MAIN, HINT_Y, 28, 0,
                                       s_hint_stage == 2 ? C_MINT : C_GRAY, l1, s_hint_typed,
                                       s_hint_typed < n1 + 8 && (frame % 4) < 2);
            if (done && s_hint_typed == n1) {
                fb_fill_rect(0, HINT_Y + HINT_DY - 4, LCD_H_RES, 40, C_BLACK);
                draw_txt_centered(HINT_Y + HINT_DY, 24, RGB(110, 118, 126), l2);
            }
            s_hint_typed++;
        }
    }
    fb_flush();
}

// Pi 出图瞬间的定格画面（main 在交屏前调用，随后 delay ~350ms）
void ui_boot_signal_locked(void)
{
    // Pi 热复位时视频信号 1 帧内就到，打字机可能一个字母都没敲出来——
    // 定格帧一律先把字标补完整（无光标），别让用户看到一个孤零零的光标块
    if (ttf_face_ready(TTF_FACE_DECO))
        boot_type_line(TTF_FACE_DECO, WM_Y, WM_SIZE, WM_SPACING, C_WHITE, WM_TEXT, 99, false);
    boot_draw_wave(0, C_MINT, true);
    const char *msg = tr("PI SIGNAL LOCKED", "已锁定树莓派信号");
    fb_fill_rect(0, SUB_Y - 6, LCD_H_RES, 60, C_BLACK);
    if (ttf_face_ready(TTF_FACE_DECO) && s_lang == LANG_EN) {
        int w = ttf_text_width_face(TTF_FACE_DECO, 26, 5, msg);
        ttf_draw_text_face(TTF_FACE_DECO, s_fb, LCD_H_RES, LCD_V_RES,
                           (LCD_H_RES - w) / 2, SUB_Y, 26, 5, C_MINT, msg);
    } else {
        draw_txt_centered(SUB_Y, 28, C_MINT, msg);
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

// ---------------------------------------------------------------------------
// Pi 端遥测（CDC PI_INFO）：单缓冲 + 序号，写方拷贝，读方按 key 取值
// ---------------------------------------------------------------------------
#define PI_INFO_MAX 128
#define PI_INFO_STALE_MS 8000
static char     s_pi_info[PI_INFO_MAX];
static int64_t  s_pi_info_us = 0;
static portMUX_TYPE s_pi_info_mux = portMUX_INITIALIZER_UNLOCKED;

void ui_set_pi_info(const char *kv_line)
{
    if (!kv_line) return;
    portENTER_CRITICAL(&s_pi_info_mux);
    strlcpy(s_pi_info, kv_line, sizeof(s_pi_info));
    s_pi_info_us = esp_timer_get_time();
    portEXIT_CRITICAL(&s_pi_info_mux);
}

static bool pi_info_fresh(void)
{
    return s_pi_info_us > 0 &&
           (esp_timer_get_time() - s_pi_info_us) < PI_INFO_STALE_MS * 1000LL;
}

// 取 "key=" 后面的值（到空格为止），没有返回 false
static bool pi_info_get(const char *key, char *out, size_t n)
{
    char local[PI_INFO_MAX];
    portENTER_CRITICAL(&s_pi_info_mux);
    strlcpy(local, s_pi_info, sizeof(local));
    portEXIT_CRITICAL(&s_pi_info_mux);
    size_t kl = strlen(key);
    for (const char *p = local; (p = strstr(p, key)) != NULL; p++) {
        if ((p == local || p[-1] == ' ') && p[kl] == '=') {
            const char *v = p + kl + 1, *e = v;
            while (*e && *e != ' ') e++;
            size_t len = (size_t)(e - v);
            if (len == 0 || len >= n) return false;
            memcpy(out, v, len); out[len] = 0;
            return true;
        }
    }
    return false;
}

// 右上角 Pi 信号状态芯片（所有页共用，按主题上色）
static void draw_status_chip(void)
{
    const theme_pal_t *p = pal();
    int x = UI_TAB_COUNT * TAB_W + 10, w = LCD_H_RES - x - 10;
    char buf[24];
    float fps = vsync_mon_fps();
    bool sig = fps > 0;
    uint16_t fg = sig ? (s_theme == TH_CYBER ? p->accent2 : p->good) : p->bad;

    switch (s_theme) {
    case TH_CYBER:
        fb_fill_rect(x, 10, w, TAB_BAR_H - 20, p->bg);
        fb_rect_outline(x, 10, w, TAB_BAR_H - 20, 2, fg);
        break;
    case TH_MINIMAL:
        fb_fill_round_rect(x, 12, w, TAB_BAR_H - 24, 12, p->card2);
        break;
    default:   // TERMINAL / EV：无框，纯文字
        fb_fill_rect(x, 8, w, TAB_BAR_H - 16, p->tabbar);
        break;
    }
    if (sig) {
        snprintf(buf, sizeof(buf), "PI %d.%d", (int)fps, (int)(fps * 10) % 10);
        if (s_theme == TH_TERMINAL) {
            fb_draw_text(x + 14, 16, buf, 3, fg);
            char model[16], cpu[16], sub[24];
            if (pi_info_fresh() && pi_info_get("model", model, sizeof(model)) &&
                pi_info_get("cpu", cpu, sizeof(cpu))) {
                snprintf(sub, sizeof(sub), "%s %.0fC", model, strtof(cpu, NULL));
                fb_draw_text(x + 14, 48, sub, 2, p->text2);
            } else {
                fb_draw_text(x + 14, 48, "FPS", 2, p->text2);
            }
        } else {
            draw_txt(x + 14, 10, 28, fg, buf);
            char model[16], cpu[16], sub[40];
            if (pi_info_fresh() && pi_info_get("model", model, sizeof(model)) &&
                pi_info_get("cpu", cpu, sizeof(cpu))) {
                float t = strtof(cpu, NULL);
                snprintf(sub, sizeof(sub), "%s · %.0f°C", model, t);
                draw_txt_fit(x + 14, 42, 18, w - 28,
                             t >= 80.0f ? p->bad : (t >= 70.0f ? p->warn : p->dim), sub);
            } else {
                draw_txt(x + 14, 42, 18, p->dim, tr("FPS LIVE", "FPS 实时"));
            }
        }
    } else {
        if (s_theme == TH_TERMINAL) {
            fb_draw_text(x + 14, 16, "PI RGB", 3, fg);
            fb_draw_text(x + 14, 48, "NO SIG", 2, p->text2);
        } else {
            draw_txt(x + 14, 10, 28, fg, "PI RGB");
            draw_txt(x + 14, 42, 18, fg, tr("NO SIGNAL", "无信号"));
        }
    }
}

static void draw_tab_bar(void)
{
    const theme_pal_t *p = pal();
    fb_fill_rect(0, 0, LCD_H_RES, TAB_BAR_H, p->tabbar);

    for (int i = 0; i < UI_TAB_COUNT; i++) {
        int x = i * TAB_W;
        bool sel = ((ui_tab_t)i == s_tab);
        const char *name = tr(k_tab_en[i], k_tab_zh[i]);
        int tw = txt_w(26, name);

        switch (s_theme) {
        case TH_CYBER:
            if (sel) {
                fb_fill_rect(x + 4, 8, TAB_W - 8, TAB_BAR_H - 16, p->tab_sel_bg);
                fb_rect_outline(x + 4, 8, TAB_W - 8, TAB_BAR_H - 16, 2, p->accent);
                fb_fill_rect(x + 10, TAB_BAR_H - 8, TAB_W - 20, 4, p->accent);
            } else {
                fb_rect_outline(x + 4, 8, TAB_W - 8, TAB_BAR_H - 16, 1, p->frame);
            }
            draw_txt(x + (TAB_W - tw) / 2, 24, 26,
                     sel ? p->tab_sel_fg : p->tab_fg, name);
            break;
        case TH_MINIMAL:
            if (sel) {
                fb_fill_round_rect(x + 6, 10, TAB_W - 12, TAB_BAR_H - 20,
                                   (TAB_BAR_H - 20) / 2, p->tab_sel_bg);
            }
            draw_txt(x + (TAB_W - tw) / 2, 24, 26,
                     sel ? p->tab_sel_fg : p->tab_fg, name);
            break;
        case TH_TERMINAL: {
            // [标签] 文本式 tab，选中反白
            if (sel) fb_fill_rect(x + 4, 14, TAB_W - 8, TAB_BAR_H - 28, p->tab_sel_bg);
            uint16_t fg = sel ? p->tab_sel_fg : p->tab_fg;
            draw_txt(x + (TAB_W - tw) / 2, 24, 26, fg, name);
            if (!sel) {
                draw_txt(x + 6, 24, 26, p->dim, "[");
                draw_txt(x + TAB_W - 20, 24, 26, p->dim, "]");
            }
            break;
        }
        case TH_EV:
        default:
            draw_txt(x + (TAB_W - tw) / 2, 22, 26,
                     sel ? p->tab_sel_fg : p->tab_fg, name);
            if (sel) fb_fill_rect(x + 24, TAB_BAR_H - 8, TAB_W - 48, 5, p->accent);
            break;
        }
    }
    // 底部分隔线（极简主题不要，卡片自身有留白）
    if (s_theme == TH_CYBER)    fb_fill_rect(0, TAB_BAR_H - 2, LCD_H_RES, 2, p->frame);
    if (s_theme == TH_TERMINAL) fb_fill_rect(0, TAB_BAR_H - 2, LCD_H_RES, 2, p->frame);
    if (s_theme == TH_EV)       fb_fill_rect(0, TAB_BAR_H - 1, LCD_H_RES, 1, p->frame);
    draw_status_chip();
}

// 主题化卡片容器 + 标题
static void ui_card(int x, int y, int w, int h, uint16_t accent, const char *title)
{
    const theme_pal_t *p = pal();
    switch (s_theme) {
    case TH_CYBER:
        fb_fill_rect(x, y, w, h, p->card);
        fb_rect_outline(x, y, w, h, 1, p->frame);
        fb_corner_brackets(x, y, w, h, 26, 3, p->accent);
        draw_txt(x + 26, y + 14, 26, p->accent, title);
        break;
    case TH_MINIMAL:
        fb_fill_round_rect(x, y, w, h, 22, p->card);
        draw_txt(x + 28, y + 16, 26, p->text2, title);
        break;
    case TH_TERMINAL: {
        fb_rect_outline(x, y, w, h, 2, p->frame);
        // 标题嵌在上边框：先用背景挖槽再写字
        int tw = txt_w(26, title);
        fb_fill_rect(x + (w - tw) / 2 - 14, y - 2, tw + 28, 6, p->bg);
        draw_txt(x + (w - tw) / 2, y - 14, 26, p->text, title);
        break;
    }
    case TH_EV:
    default:
        fb_fill_round_rect(x, y, w, h, 14, p->card);
        draw_txt(x + 24, y + 12, 24, p->text2, title);
        break;
    }
    (void)accent;
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

// 电池容量行：学习到的总容量 + 按 SOC 折算的剩余电量（两个数字都写清楚，
// 2026-08-21 用户要求；容量 = batt_log 库仑计数自学习值，默认种子 3000mAh）
static void draw_capacity_line(int x, int y, int soc)
{
    char cbuf[96];
    float cap = batt_log_capacity_mah();
    if (soc > 0)
        snprintf(cbuf, sizeof(cbuf),
                 tr("TOTAL %.0fmAh / LEFT %.0fmAh",
                    "总容量 %.0fmAh，剩余 %.0fmAh"),
                 cap, cap * soc / 100.0f);
    else
        snprintf(cbuf, sizeof(cbuf),
                 tr("TOTAL %.0fmAh (EST.)", "估算总容量 %.0fmAh"), cap);
    draw_txt(x, y, 18, pal()->text2, cbuf);
}

// 放电续航估算（仅未插电时显示）：
//   E剩 [Wh] = 容量mAh/1000 × 3.7V × SOC/100
//              容量 = batt_log 库仑计数自学习值（默认种子 3000mAh）
//   P均 [W]  = 近 1 分钟输出功率均值（batt_log 5s/样本，插电时也在积累，
//              所以拔线 5 秒内就有首个估算值，随样本增多收敛）
//   t   [h]  = E剩 / P均
static void draw_runtime_estimate(int x, int y, int soc)
{
    char ebuf[96];
    float avg_w = 0;
    int n = batt_log_avg_discharge_w(60, &avg_w);
    if (n >= 1 && avg_w > 0.05f && soc > 0) {
        float cap_mah = batt_log_capacity_mah();
        float e_wh = cap_mah / 1000.0f * BOARD_BATT_NOMINAL_V * soc / 100.0f;
        float t_h = e_wh / avg_w;
        if (t_h > 99.0f) t_h = 99.0f;
        if (t_h < 1.0f)
            snprintf(ebuf, sizeof(ebuf),
                     tr("EST. %d MIN LEFT (AVG %.2fW)",
                        "预计续航 %d 分钟（均 %.2fW）"),
                     (int)(t_h * 60.0f), avg_w);
        else
            snprintf(ebuf, sizeof(ebuf),
                     tr("EST. %.1f H LEFT (AVG %.2fW)",
                        "预计续航 %.1f 小时（均 %.2fW）"),
                     t_h, avg_w);
        draw_txt(x, y, 18, pal()->text2, ebuf);
    } else {
        draw_txt(x, y, 18, pal()->dim,
                 tr("RUNTIME ESTIMATING...", "续航估算中…"));
    }
}

// dash 页共享的一次采样
typedef struct {
    float vbat_v, vbat_a, vbus_v, vbus_a, cw_v, stc_v, stc_soc;
    float stc_a;            // STC3117 电池真实电流，正=充电 负=放电
    int cw_soc, soc;
    bool vbat_ok, vbus_ok, plugged;
    bool stc_i_ok;          // STC3117 电流可读（MUX 在 ESP 侧）
    uint16_t soc_col;
} dash_data_t;

static void dash_read(dash_data_t *d)
{
    const theme_pal_t *p = pal();
    memset(d, 0, sizeof(*d));
    d->stc_soc = -1;
    d->cw_soc = -1;
    d->vbat_ok = s_ctx.ina_vbat &&
                 ina219_read(s_ctx.ina_vbat, &d->vbat_v, &d->vbat_a) == ESP_OK;
    d->vbus_ok = s_ctx.ina_vbus &&
                 ina219_read(s_ctx.ina_vbus, &d->vbus_v, &d->vbus_a) == ESP_OK;
    if (!s_ctx.cw2015 || cw2015_read(s_ctx.cw2015, &d->cw_v, &d->cw_soc) != ESP_OK)
        d->cw_soc = -1;
    stc3117_ensure_running(s_ctx.stc3117);
    if (!s_ctx.stc3117 || stc3117_read(s_ctx.stc3117, &d->stc_v, &d->stc_soc) != ESP_OK)
        d->stc_soc = -1;
    d->stc_i_ok = s_ctx.stc3117 &&
                  stc3117_read_current(s_ctx.stc3117, &d->stc_a) == ESP_OK;
    // 主 SOC 用 STC3117（带采样电阻库仑计），CW2015 仅回退
    d->soc = d->stc_soc >= 0 ? (int)(d->stc_soc + 0.5f) : d->cw_soc;
    d->plugged = d->vbus_ok && d->vbus_v > 4.0f;
    d->soc_col = d->soc < 0  ? p->dim
               : d->soc < 15 ? p->bad
               : d->soc < 40 ? p->warn : p->good;
}

// 供电状态判定（2026-09-10 重写：以 STC3117 电池真实电流为准）
//
// 三颗电流表各测什么（网表实锤）：
//   INA219 U4  (INA-BAT)：采样电阻 U23 在 VBAT→VBAT_LOAD，充电器 SLM6610 输出
//                          经 R16 回到 VBAT_RAW（电池侧），**绕过** U23——所以
//                          它只看得到系统负载电流，永远看不到充电电流；
//   INA219 U20 (INA-BUS)：U39 在 VBUS_RAW→VBUS_LOAD = 全部 USB 输入；
//   STC3117   (电池)    ：U37 10mΩ 在 BAT_N→GND，正=充电、负=放电，唯一的真实电池电流。
//   交叉验证：USB 输入功率 ≈ 系统负载功率 + 电池充电功率（各自效率损耗内）。
//
// 旧逻辑把 INA-BAT 电流当"电池输出"，充电末段（输入≈负载）就会在临界值附近
// 反复跳"供电可能不足"——2026-09-10 用户实机截图证实。
//
// 状态（带迟滞）：未插电→正在放电；插电 & I_bat>+80mA→充电中；插电 & I_bat<-80mA
// →供电不足电池补差；其余→USB 供电中（电池电流≈0 即已充满/涓流）。
// STC3117 不可读（MUX 在 Pi 侧）时回退功率差判定，同样带迟滞。
enum { PS_NONE = -1, PS_DISCHARGE, PS_CHARGING, PS_USB, PS_DEFICIT };
static int s_ps = PS_NONE;

static int dash_power_state(const dash_data_t *d)
{
    if (!d->plugged) return PS_DISCHARGE;
    if (d->stc_i_ok) {
        float i = d->stc_a;
        float in_hi = 0.08f, out_lo = 0.03f;   // 进入/退出阈值（A）
        if (s_ps == PS_CHARGING) return i > out_lo ? PS_CHARGING
                                      : (i < -in_hi ? PS_DEFICIT : PS_USB);
        if (s_ps == PS_DEFICIT)  return i < -out_lo ? PS_DEFICIT
                                      : (i > in_hi ? PS_CHARGING : PS_USB);
        if (i > in_hi)  return PS_CHARGING;
        if (i < -in_hi) return PS_DEFICIT;
        return PS_USB;
    }
    // 回退：只有功率差可用（负载 - 输入），>0.6W 判不足，<0.2W 恢复
    float gap = (d->vbat_ok ? d->vbat_v * d->vbat_a : 0.0f)
              - (d->vbus_ok ? d->vbus_v * d->vbus_a : 0.0f);
    if (s_ps == PS_DEFICIT) return gap > 0.2f ? PS_DEFICIT : PS_USB;
    return gap > 0.6f ? PS_DEFICIT : PS_USB;
}

// 画状态行 + 三路电流交叉验证行 + 续航/容量行。x 左缘、y0 状态行顶、maxw 可用宽度。
// 返回最后一行之后的 y，供调用方继续排版。
static int dash_power_status(const dash_data_t *d, int x, int y0, int maxw)
{
    const theme_pal_t *p = pal();
    char buf[96];
    int st = dash_power_state(d);
    s_ps = st;

    const char *title; uint16_t col;
    switch (st) {
    case PS_DISCHARGE: title = tr("DISCHARGING", "正在放电");        col = p->warn; break;
    case PS_CHARGING:  title = tr("CHARGING", "充电中");             col = p->good; break;
    case PS_DEFICIT:   title = tr("POWER MAY BE LOW", "供电可能不足"); col = p->warn; break;
    default:
        if (d->stc_i_ok && (d->soc >= 97 || d->vbat_v > 4.15f))
            title = tr("FULL · USB POWERED", "已充满 · USB 供电中");
        else
            title = tr("USB POWERED", "USB 供电中");
        col = p->good; break;
    }
    draw_txt_fit(x, y0, 32, maxw, col, title);

    int y = y0 + 44, dy = 22;
    // 交叉验证三行：USB 输入 / 系统负载 / 电池电流
    if (d->vbus_ok)
        snprintf(buf, sizeof(buf), tr("USB IN   %4.0f mA  %.2f W", "USB 输入  %4.0f mA  %.2f W"),
                 d->vbus_a * 1000.0f, d->vbus_v * d->vbus_a);
    else
        snprintf(buf, sizeof(buf), tr("USB IN   --", "USB 输入  --"));
    draw_txt_fit(x, y, 18, maxw, p->text2, buf); y += dy;
    if (d->vbat_ok)
        snprintf(buf, sizeof(buf), tr("LOAD     %4.0f mA  %.2f W", "系统负载  %4.0f mA  %.2f W"),
                 d->vbat_a * 1000.0f, d->vbat_v * d->vbat_a);
    else
        snprintf(buf, sizeof(buf), tr("LOAD     --", "系统负载  --"));
    draw_txt_fit(x, y, 18, maxw, p->text2, buf); y += dy;
    if (d->stc_i_ok)
        snprintf(buf, sizeof(buf), tr("BATTERY  %+4.0f mA  %+.2f W", "电池电流  %+4.0f mA  %+.2f W"),
                 d->stc_a * 1000.0f, d->stc_a * d->vbat_v);
    else
        snprintf(buf, sizeof(buf), tr("BATTERY  -- (MUX AT PI)", "电池电流  --（MUX 在 Pi 侧）"));
    draw_txt_fit(x, y, 18, maxw,
                 st == PS_DEFICIT ? p->warn : (st == PS_CHARGING ? p->good : p->text2), buf);
    y += dy + 6;

    if (st == PS_DISCHARGE) {
        draw_runtime_estimate(x, y, d->soc);
        y += 22;
    }
    draw_capacity_line(x, y, d->soc);
    return y + 22;
}

// 两颗电量计交叉读数小字
static void dash_gauge_footnotes(const dash_data_t *d, int x, int y, int dy)
{
    const theme_pal_t *p = pal();
    char buf[48];
    if (d->cw_soc >= 0)
        snprintf(buf, sizeof(buf), "CW2015 %.3fV %d%%", d->cw_v, d->cw_soc);
    else
        snprintf(buf, sizeof(buf), "CW2015 --");
    fb_draw_text(x, y, buf, 2, p->dim);
    if (d->stc_soc >= 0)
        snprintf(buf, sizeof(buf), "STC3117 %.3fV %.1f%%", d->stc_v, d->stc_soc);
    else
        snprintf(buf, sizeof(buf), "STC3117 --");
    fb_draw_text(x, y + dy, buf, 2, p->dim);
}

static void dash_footer(uint32_t uptime_s)
{
    const theme_pal_t *p = pal();
    char buf[96];
    snprintf(buf, sizeof(buf),
             tr("UP %lu S · PRESS □ TO RETURN TO PI",
                "已运行 %lu 秒 · 按 □ 键返回树莓派画面"),
             (unsigned long)uptime_s);
    draw_txt_centered(726, 24, s_theme == TH_TERMINAL ? p->text2 : p->dim, buf);
}

// 传感器在位 chip（按主题上壳）
static void dash_sensor_chip(int x, int y, int w, int h, int i)
{
    const theme_pal_t *p = pal();
    bool ok = s_present[i];
    uint16_t dot = ok ? p->good : p->bad;
    switch (s_theme) {
    case TH_CYBER:
        fb_fill_rect(x, y, w, h, p->card2);
        fb_rect_outline(x, y, w, h, 1, p->frame);
        break;
    case TH_MINIMAL:
        fb_fill_round_rect(x, y, w, h, 10, p->card2);
        break;
    default:
        break;   // 终端/EV：无底
    }
    fb_fill_rect(x + 16, y + (h - 12) / 2, 12, 12, dot);
    if (s_theme == TH_MINIMAL)
        draw_txt(x + 42, y + (h - 26) / 2, 20, ok ? p->text2 : p->dim,
                 k_sensors[i].name);
    else
        fb_draw_text(x + 42, y + (h - 14) / 2, k_sensors[i].name, 2,
                     ok ? p->text2 : p->dim);
}

// ---- 卡片式 dash（赛博 / 极简 / 终端三主题共用几何）----
static void draw_page_dash_cards(const dash_data_t *d, uint32_t uptime_s)
{
    const theme_pal_t *p = pal();
    char buf[64];
    bool term = (s_theme == TH_TERMINAL);

    // ---- 电池卡片（左 2/3）----
    int bx = 24, by = CONTENT_Y, bw2 = 648, bh2 = 330;
    ui_card(bx, by, bw2, bh2, p->warn, tr("BATTERY", "电池"));
    if (d->soc >= 0) {
        snprintf(buf, sizeof(buf), "%d%%", d->soc);
        if (term) fb_draw_text(bx + 36, by + 62, buf, 12, d->soc_col);
        else      draw_txt(bx + 36, by + 44, 110, d->soc_col, buf);
    } else {
        if (term) fb_draw_text(bx + 36, by + 62, "--%", 12, p->dim);
        else      draw_txt(bx + 36, by + 44, 110, p->dim, "--%");
    }
    // 电量条
    {
        int gx = bx + 36, gy = by + 196, gw = 300, gh = 32;
        int pct = d->soc > 0 ? d->soc : 0;
        if (s_theme == TH_MINIMAL) {
            fb_fill_round_rect(gx, gy + 6, gw, 18, 9, p->card2);
            if (pct > 0)
                fb_fill_round_rect(gx, gy + 6, gw * pct / 100, 18, 9, p->accent);
        } else {
            fb_segment_bar(gx, gy, gw, gh, 15, pct, d->soc_col, p->card2);
            if (s_theme == TH_CYBER)
                fb_rect_outline(gx - 4, gy - 4, gw + 8, gh + 8, 1, p->frame);
        }
    }
    dash_gauge_footnotes(d, bx + 36, by + 252, 20);

    int rx = bx + 360;
    if (d->vbat_ok) {
        snprintf(buf, sizeof(buf), "%.3f V", d->vbat_v);
        if (term) fb_draw_text(rx, by + 48, buf, 5, p->text);
        else      draw_txt(rx, by + 40, 48, p->text, buf);
        // 电压下方的大字：优先电池真实电流（STC3117），不可读时退回负载电流
        int maxw = bx + bw2 - 24 - rx;
        if (d->stc_i_ok)
            snprintf(buf, sizeof(buf), tr("BATT %+.0f mA · %+.2f W", "电池 %+.0f mA · %+.2f W"),
                     d->stc_a * 1000.0f, d->stc_a * d->vbat_v);
        else
            snprintf(buf, sizeof(buf), tr("LOAD %.0f mA · %.2f W", "负载 %.0f mA · %.2f W"),
                     d->vbat_a * 1000.0f, d->vbat_v * d->vbat_a);
        draw_txt_fit(rx, by + 108, 26, maxw, p->text2, buf);
        dash_power_status(d, rx, by + 150, maxw);
    } else {
        draw_txt(rx, by + 100, 30, p->bad, tr("INA219 READ FAIL", "INA219 读取失败"));
    }

    // ---- USB 供电卡片（右 1/3）----
    int ux = 688, uy = CONTENT_Y, uw = 312, uh = 330;
    ui_card(ux, uy, uw, uh, p->accent, tr("USB POWER", "USB 供电"));
    if (d->vbus_ok) {
        snprintf(buf, sizeof(buf), "%.3f V", d->vbus_v);
        if (term) fb_draw_text(ux + 28, uy + 64, buf, 5, p->text);
        else      draw_txt(ux + 28, uy + 56, 46, p->text, buf);
        snprintf(buf, sizeof(buf), "%.0f mA", d->vbus_a * 1000.0f);
        draw_txt(ux + 28, uy + 136, 30, p->text2, buf);
        snprintf(buf, sizeof(buf), "%.2f W", d->vbus_v * d->vbus_a);
        draw_txt(ux + 28, uy + 182, 30, p->text2, buf);
        const char *st = d->plugged ? tr("CONNECTED", "已连接")
                                    : tr("UNPLUGGED", "未连接");
        uint16_t sc = d->plugged ? p->good : p->dim;
        if (s_theme == TH_MINIMAL) {
            int tw = txt_w(20, st);
            fb_fill_round_rect(ux + 28, uy + 244, tw + 32, 40, 10, p->card2);
            draw_txt(ux + 44, uy + 252, 20, sc, st);
        } else {
            draw_txt(ux + 28, uy + 248, 24, sc, st);
        }
    } else {
        draw_txt(ux + 28, uy + 100, 26, p->bad, tr("READ FAIL", "读取失败"));
    }

    // ---- 传感器在位卡片（底部全宽）----
    snprintf(buf, sizeof(buf), tr("SENSORS %d/%d", "传感器在位 %d/%d"),
             s_present_ok, (int)N_SENSORS);
    ui_card(24, CONTENT_Y + 346, 976, 214, p->good, buf);
    for (int i = 0; i < (int)N_SENSORS; i++) {
        int col = i % 5, row = i / 5;
        dash_sensor_chip(41 + col * 190, CONTENT_Y + 346 + 52 + row * 76,
                         182, 62, i);
    }
    dash_footer(uptime_s);
}

// ---- EV 仪表 dash（大圆环）----
static void draw_page_dash_ev(const dash_data_t *d, uint32_t uptime_s)
{
    const theme_pal_t *p = pal();
    char buf[64];

    // 左：圆环电量表
    int cx = 250, cy = 350;
    fb_ring_gauge(cx, cy, 165, 127, d->soc > 0 ? d->soc : 0, p->card2);
    {
        const char *t = tr("BATTERY", "电池");
        draw_txt(cx - txt_w(26, t) / 2, cy - 108, 26, p->text2, t);
        if (d->soc >= 0) snprintf(buf, sizeof(buf), "%d%%", d->soc);
        else             snprintf(buf, sizeof(buf), "--%%");
        draw_txt(cx - txt_w(88, buf) / 2, cy - 62, 88, p->text, buf);
        if (d->vbat_ok) {
            snprintf(buf, sizeof(buf), "%.3f V", d->vbat_v);
            draw_txt(cx - txt_w(30, buf) / 2, cy + 46, 30, p->text2, buf);
        }
    }

    // 右列读数
    int rx = 520;
    if (d->vbat_ok) {
        // 大字：电池真实电流（STC3117）/ 不可读时负载电流；第二行系统负载功率
        if (d->stc_i_ok)
            snprintf(buf, sizeof(buf), "%+.0f mA", d->stc_a * 1000.0f);
        else
            snprintf(buf, sizeof(buf), "%.0f mA", d->vbat_a * 1000.0f);
        draw_txt(rx, 110, 54, p->text, buf);
        draw_txt(rx + 300, 132, 22, p->dim,
                 d->stc_i_ok ? tr("BATTERY", "电池电流") : tr("LOAD", "负载电流"));
        fb_fill_rect(rx, 182, 460, 1, p->frame);
        snprintf(buf, sizeof(buf), "%.2f W", d->vbat_v * d->vbat_a);
        draw_txt(rx, 196, 54, p->text, buf);
        draw_txt(rx + 300, 218, 22, p->dim, tr("LOAD", "系统负载"));
        fb_fill_rect(rx, 268, 460, 1, p->frame);
        int y = dash_power_status(d, rx, 284, 460);   // 插电时到 ~422
        dash_gauge_footnotes(d, rx, y, 18);            // 两行到 ~458，USB 块从 468 起
    } else {
        draw_txt(rx, 140, 30, p->bad, tr("INA219 READ FAIL", "INA219 读取失败"));
    }

    // USB 供电条状模块
    {
        int ux = rx, uy = 468, uw = 460, uh = 80;
        fb_fill_round_rect(ux, uy, uw, uh, 12, p->card);
        draw_txt(ux + 20, uy + 10, 22, p->text2, tr("USB POWER", "USB 供电"));
        if (d->vbus_ok) {
            snprintf(buf, sizeof(buf), "%.3f V · %.0f mA · %.2f W",
                     d->vbus_v, d->vbus_a * 1000.0f, d->vbus_v * d->vbus_a);
            draw_txt(ux + 20, uy + 42, 22, p->dim, buf);
        }
        const char *st = d->plugged ? tr("CONNECTED", "已连接")
                                    : tr("UNPLUGGED", "未连接");
        draw_txt(ux + uw - txt_w(22, st) - 20, uy + 28, 22,
                 d->plugged ? p->good : p->dim, st);
    }

    // 底部 telltale 传感器块
    snprintf(buf, sizeof(buf), tr("SENSORS %d/%d", "传感器在位 %d/%d"),
             s_present_ok, (int)N_SENSORS);
    draw_txt(36, 556, 20, p->dim, buf);
    for (int i = 0; i < (int)N_SENSORS; i++) {
        int x = 36 + i * 96, y = 586;
        bool ok = s_present[i];
        fb_fill_round_rect(x, y, 88, 66, 10, p->card);
        fb_rect_outline(x, y, 88, 66, 1, ok ? p->frame : p->bad);
        fb_fill_rect(x + 38, y + 44, 12, 12, ok ? p->good : p->bad);
        fb_draw_text(x + 8, y + 14, k_sensors[i].name, 1, ok ? p->text2 : p->bad);
    }
    dash_footer(uptime_s);
}

static void draw_page_dash(uint32_t uptime_s)
{
    dash_data_t d;
    dash_read(&d);
    sensors_probe_maybe();
    if (s_theme == TH_EV) draw_page_dash_ev(&d, uptime_s);
    else                  draw_page_dash_cards(&d, uptime_s);
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
    const theme_pal_t *p = pal();
    fb_fill_rect(GRAPH_X - 2, GRAPH_Y - 2, GRAPH_W + 4, GRAPH_H + 4, p->frame);
    fb_fill_rect(GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H, s_theme == TH_MINIMAL ? p->card : C_BLACK);
    for (int i = 1; i < 4; i++) {
        fb_fill_rect(GRAPH_X, GRAPH_Y + GRAPH_H * i / 4, GRAPH_W, 1, p->card2);
        fb_fill_rect(GRAPH_X + GRAPH_W * i / 4, GRAPH_Y, 1, GRAPH_H, p->card2);
    }
    // 纵轴标注（SOC 尺度）
    fb_draw_text(GRAPH_X - 60, GRAPH_Y - 8, "100", 2, p->dim);
    fb_draw_text(GRAPH_X - 60, GRAPH_Y + GRAPH_H / 2 - 8, "50", 2, p->dim);
    fb_draw_text(GRAPH_X - 48, GRAPH_Y + GRAPH_H - 8, "0", 2, p->dim);

    if (n >= 2) {
        // x 轴固定 1 小时窗口（720 槽），数据不足时靠右对齐（最新在最右）
        for (int i = 1; i < n; i++) {
            int x0 = GRAPH_X + (BATT_LOG_CAP - n + i - 1) * GRAPH_W / (BATT_LOG_CAP - 1);
            int x1 = GRAPH_X + (BATT_LOG_CAP - n + i) * GRAPH_W / (BATT_LOG_CAP - 1);
            const batt_sample_t *a = &s_graph_buf[i - 1], *b = &s_graph_buf[i];
            if (a->soc >= 0 && b->soc >= 0) {
                fb_draw_line(x0, graph_map(a->soc, 0, 100),
                             x1, graph_map(b->soc, 0, 100), 3, p->good);
            }
            if (a->mv && b->mv) {
                fb_draw_line(x0, graph_map(a->mv, 3000, 4400),
                             x1, graph_map(b->mv, 3000, 4400), 2, p->accent);
                fb_draw_line(x0, graph_map(a->ma, -2000, 2000),
                             x1, graph_map(b->ma, -2000, 2000), 2, p->warn);
            }
        }
    } else {
        draw_txt_centered(GRAPH_Y + GRAPH_H / 2 - 20, 36, p->dim,
                          tr("COLLECTING DATA...", "正在采集数据……"));
    }

    // 图例 + 最新读数
    int ly = GRAPH_Y + GRAPH_H + 30;
    fb_fill_rect(GRAPH_X, ly + 4, 24, 12, p->good);
    fb_draw_text(GRAPH_X + 36, ly, "SOC 0-100%", 2, p->text2);
    fb_fill_rect(GRAPH_X + 240, ly + 4, 24, 12, p->accent);
    fb_draw_text(GRAPH_X + 276, ly, "V 3.0-4.4", 2, p->text2);
    fb_fill_rect(GRAPH_X + 460, ly + 4, 24, 12, p->warn);
    fb_draw_text(GRAPH_X + 496, ly, "MA -2000..2000", 2, p->text2);
    snprintf(buf, sizeof(buf), tr("%d/%d SAMPLES", "%d/%d 采样"), n, BATT_LOG_CAP);
    draw_txt(GRAPH_X + 740, ly - 4, 22, p->dim, buf);

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
        draw_txt(GRAPH_X, ly + 40, 34, p->text, buf);
    }
    draw_txt_centered(726, 26, p->dim,
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
    const theme_pal_t *p = pal();
    char buf[48];
    fb_fill_rect(40, 692, 760, 40, p->card);
    if (s_touch_last_x >= 0) {
        snprintf(buf, sizeof(buf), tr("X:%4d Y:%4d  EVENTS:%lu",
                                      "X:%4d Y:%4d  事件:%lu"),
                 s_touch_last_x, s_touch_last_y, (unsigned long)s_touch_events);
    } else {
        snprintf(buf, sizeof(buf), tr("TOUCH THE CANVAS  EVENTS:%lu",
                                      "触摸画布试试  事件:%lu"),
                 (unsigned long)s_touch_events);
    }
    draw_txt(48, 696, 28, p->text, buf);
}

static void draw_page_touch(void)
{
    const theme_pal_t *p = pal();
    draw_txt(CANVAS_X, CONTENT_Y, 28, p->accent,
             tr("GT911 TOUCH TEST - DRAW ON CANVAS", "GT911 触摸测试——在画布上绘制"));
    // 画布
    fb_fill_rect(CANVAS_X - 2, CANVAS_Y - 2, CANVAS_W + 4, CANVAS_H + 4, p->frame);
    fb_fill_rect(CANVAS_X, CANVAS_Y, CANVAS_W, CANVAS_H, C_BLACK);
    // 重放轨迹
    for (int i = 0; i < s_trail_n; i++) {
        fb_fill_rect(s_trail_x[i], s_trail_y[i], 8, 8, trail_color(i));
    }
    // CLEAR 按钮
    if (s_theme == TH_MINIMAL) {
        fb_fill_round_rect(CLEAR_X, CLEAR_Y, CLEAR_W, CLEAR_H, 16, p->card2);
    } else {
        fb_fill_rect(CLEAR_X, CLEAR_Y, CLEAR_W, CLEAR_H, p->card2);
        fb_rect_outline(CLEAR_X, CLEAR_Y, CLEAR_W, CLEAR_H, 2, p->accent);
    }
    const char *clr = tr("CLEAR", "清除");
    draw_txt(CLEAR_X + (CLEAR_W - txt_w(32, clr)) / 2, CLEAR_Y + 14, 32, p->text, clr);
    draw_touch_coords();
}

// ---------------------------------------------------------------------------
// PI SIG 页：VSYNC 探测状态
// ---------------------------------------------------------------------------
static void draw_page_pisig(void)
{
    const theme_pal_t *p = pal();
    char buf[64];
    bool sig = vsync_mon_signal();
    float fps = vsync_mon_fps();

    if (sig) {
        draw_txt_centered(CONTENT_Y + 30, 64, p->good, tr("SIGNAL", "有信号"));
        snprintf(buf, sizeof(buf), "%d.%d", (int)fps, (int)(fps * 10) % 10);
        fb_draw_text_centered(CONTENT_Y + 160, buf, 18, p->text);
        draw_txt_centered(CONTENT_Y + 320, 44, p->text2,
                          vsync_mon_locked()
                              ? tr("FPS (FROZEN, PROBE OFF)", "FPS（冻结值，探测已关）")
                              : tr("FPS (DPI REFRESH RATE)", "FPS（DPI 刷新率）"));
    } else {
        draw_txt_centered(CONTENT_Y + 30, 64, p->bad, tr("NO SIGNAL", "无信号"));
        draw_txt_centered(CONTENT_Y + 190, 36, p->dim,
                          tr("PI RGB OUTPUT NOT DETECTED", "未检测到树莓派 RGB 输出"));
    }

    int y = CONTENT_Y + 420;
    snprintf(buf, sizeof(buf), tr("FRAMES: %lu", "帧计数: %lu"),
             (unsigned long)vsync_mon_frames());
    draw_txt(80, y, 28, p->text2, buf);
    int64_t age = vsync_mon_age_ms();
    if (age >= 0)
        snprintf(buf, sizeof(buf), tr("LAST VSYNC: %lld MS AGO",
                                      "上次 VSYNC: %lld 毫秒前"), (long long)age);
    else
        snprintf(buf, sizeof(buf), "%s", tr("LAST VSYNC: NEVER", "上次 VSYNC: 从未"));
    draw_txt(80, y + 44, 28, p->text2, buf);
    snprintf(buf, sizeof(buf), tr("INT STORMS: %lu", "中断风暴: %lu"),
             (unsigned long)vsync_mon_storms());
    draw_txt(80, y + 88, 28, p->text2, buf);

    // 右列：Pi 端遥测（systemd typixdeck-pi-info 经 CDC 推送）
    {
        int tx = 560, ty = y - 6, tdy = 30;
        char v[24], v2[24];
        if (pi_info_fresh()) {
            draw_txt(tx, ty, 22, p->good, tr("PI TELEMETRY (CDC)", "Pi 遥测（CDC）")); ty += tdy;
            if (pi_info_get("model", v, sizeof(v))) {
                if (pi_info_get("rev", v2, sizeof(v2)))
                    snprintf(buf, sizeof(buf), tr("MODEL: %s REV %s", "型号: %s Rev %s"), v, v2);
                else
                    snprintf(buf, sizeof(buf), tr("MODEL: %s", "型号: %s"), v);
                draw_txt(tx, ty, 22, p->text2, buf); ty += tdy;
            }
            if (pi_info_get("cpu", v, sizeof(v))) {
                float t = strtof(v, NULL);
                snprintf(buf, sizeof(buf), tr("CPU: %.1f°C", "核心温度: %.1f°C"), t);
                draw_txt(tx, ty, 22, t >= 80 ? p->bad : (t >= 70 ? p->warn : p->text2), buf); ty += tdy;
            }
            if (pi_info_get("nvme", v, sizeof(v))) {
                snprintf(buf, sizeof(buf), tr("NVME: %s°C", "NVMe: %s°C"), v);
                draw_txt(tx, ty, 22, p->text2, buf); ty += tdy;
            }
            if (pi_info_get("fan", v, sizeof(v))) {
                snprintf(buf, sizeof(buf), tr("FAN: %s RPM", "风扇: %s RPM"), v);
                draw_txt(tx, ty, 22, p->text2, buf); ty += tdy;
            }
            if (pi_info_get("thr", v, sizeof(v))) {
                bool bad = strcmp(v, "0x0") != 0;
                snprintf(buf, sizeof(buf), tr("THROTTLED: %s", "降频标志: %s"), v);
                draw_txt(tx, ty, 22, bad ? p->warn : p->text2, buf); ty += tdy;
            }
            if (pi_info_get("load", v, sizeof(v))) {
                snprintf(buf, sizeof(buf), tr("LOAD: %s", "负载: %s"), v);
                draw_txt(tx, ty, 22, p->text2, buf); ty += tdy;
            }
        } else {
            draw_txt(tx, ty, 22, p->dim, tr("PI TELEMETRY: OFFLINE", "Pi 遥测: 离线")); ty += tdy;
            draw_txt(tx, ty, 18, p->dim, tr("typixdeck-pi-info.service not running",
                                            "typixdeck-pi-info 服务未运行"));
        }
    }
    draw_txt(80, y + 150, 22, p->dim,
             tr("SENSE PATH: PI GPIO2 (DPI VSYNC) - R83 -",
                "探测链路: Pi GPIO2 (DPI VSYNC) → R83 →"));
    draw_txt(80, y + 180, 22, p->dim,
             tr("AW9523 P0_7 INT - GPIO5 EDGE COUNT",
                "AW9523 P0_7 中断 → GPIO5 沿计数"));
    draw_txt_centered(726, 28, p->dim,
                      tr("PRESS □ TO RETURN TO PI", "按 □ 键返回树莓派画面"));
}

// ---------------------------------------------------------------------------
// 设置页：语言 + 主题（触摸即切换并写 NVS）
// ---------------------------------------------------------------------------
#define LBTN_W    218            // 四个语言按钮与主题按钮同网格
#define LBTN_H    100
#define LBTN_Y    (CONTENT_Y + 78)
#define LBTN_X0   60
#define LBTN_DX   232

#define TBTN_W    218
#define TBTN_H    170
#define TBTN_Y    (CONTENT_Y + 330)
#define TBTN_X0   60
#define TBTN_DX   232

static const char *k_theme_en[TH_COUNT] = { "CYBER", "MINIMAL", "TERMINAL", "EV RING" };
static const char *k_theme_zh[TH_COUNT] = { "赛博",   "极简",     "终端",     "EV 仪表" };

// 每个主题按钮里画一小块风格预览色板
static void draw_theme_swatch(int x, int y, int w, int h, ui_theme_t t)
{
    const theme_pal_t *q = &k_pal[t];
    fb_fill_rect(x, y, w, h, q->bg);
    fb_rect_outline(x, y, w, h, 1, q->frame);
    fb_fill_rect(x + 8, y + 8, w - 16, 10, q->accent);
    fb_fill_rect(x + 8, y + 24, (w - 16) * 2 / 3, 8, q->good);
    fb_fill_rect(x + 8, y + 38, (w - 16) / 2, 8, q->text2);
}

static void draw_page_setup(void)
{
    const theme_pal_t *p = pal();
    draw_txt(60, CONTENT_Y + 14, 32, p->accent, tr("Language 语言", "语言 Language"));

    for (int i = 0; i < LANG_COUNT; i++) {
        int x = LBTN_X0 + i * LBTN_DX;
        bool sel = (s_lang == (ui_lang_t)i);
        if (s_theme == TH_MINIMAL) {
            fb_fill_round_rect(x, LBTN_Y, LBTN_W, LBTN_H, 18,
                               sel ? p->tab_sel_bg : p->card);
        } else {
            fb_fill_rect(x, LBTN_Y, LBTN_W, LBTN_H, sel ? p->card2 : p->card);
            fb_rect_outline(x, LBTN_Y, LBTN_W, LBTN_H, sel ? 3 : 1,
                            sel ? p->accent : p->frame);
        }
        const char *label = k_lang_name[i];
        uint16_t fg = (s_theme == TH_MINIMAL && sel) ? p->tab_sel_fg
                    : sel ? p->text : p->dim;
        draw_txt(x + (LBTN_W - txt_w(34, label)) / 2, LBTN_Y + 31, 34, fg, label);
    }

    draw_txt(60, CONTENT_Y + 230, 32, p->accent, tr("Theme 主题", "主题 Theme"));
    draw_txt(60, CONTENT_Y + 278, 22, p->dim,
             tr("Touch to switch. Saved to flash (NVS).",
                "触摸切换，写入 Flash（NVS）持久保存"));

    for (int i = 0; i < TH_COUNT; i++) {
        int x = TBTN_X0 + i * TBTN_DX;
        bool sel = ((ui_theme_t)i == s_theme);
        if (s_theme == TH_MINIMAL) {
            fb_fill_round_rect(x, TBTN_Y, TBTN_W, TBTN_H, 18, p->card);
            if (sel) fb_rect_outline(x, TBTN_Y, TBTN_W, TBTN_H, 3, p->accent);
        } else {
            fb_fill_rect(x, TBTN_Y, TBTN_W, TBTN_H, p->card);
            fb_rect_outline(x, TBTN_Y, TBTN_W, TBTN_H, sel ? 3 : 1,
                            sel ? p->accent : p->frame);
        }
        draw_theme_swatch(x + 16, TBTN_Y + 16, TBTN_W - 32, 56, (ui_theme_t)i);
        const char *name = tr(k_theme_en[i], k_theme_zh[i]);
        draw_txt(x + (TBTN_W - txt_w(26, name)) / 2, TBTN_Y + 96, 26,
                 sel ? p->text : p->text2, name);
        if (sel)
            draw_txt(x + (TBTN_W - txt_w(20, "*")) / 2, TBTN_Y + 134, 20,
                     p->accent, tr("ACTIVE", "当前"));
    }

    if (!ttf_font_ready()) {
        draw_txt(60, TBTN_Y + TBTN_H + 24, 22, p->bad,
                 "TTF FONT MISSING - CHINESE UNAVAILABLE");
    }
    draw_txt_centered(726, 26, p->dim,
                      tr("PRESS □ TO RETURN TO PI", "按 □ 键返回树莓派画面"));
}

// ---------------------------------------------------------------------------
// 页面调度 + 触摸
// ---------------------------------------------------------------------------
void ui_page_draw(uint32_t uptime_s)
{
    // 消费远程调试请求（CDC THEME_n / TAB_n），在 GUI 任务上下文安全生效
    if (s_req_lang >= 0) {
        if ((ui_lang_t)s_req_lang != s_lang) { s_lang = (ui_lang_t)s_req_lang; prefs_save(); }
        s_req_lang = -1;
    }
    if (s_req_theme >= 0) {
        int t = s_req_theme;
        s_req_theme = -1;
        if (t != (int)s_theme) { s_theme = (ui_theme_t)t; prefs_save(); }
    }
    if (s_req_tab >= 0) {
        int t = s_req_tab;
        s_req_tab = -1;
        s_tab = (ui_tab_t)t;
    }
    s_last_uptime_s = uptime_s;
    if (s_draw_mtx) xSemaphoreTake(s_draw_mtx, portMAX_DELAY);
    fb_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, pal()->bg);
    draw_tab_bar();
    switch (s_tab) {
    case UI_TAB_DASH:  draw_page_dash(uptime_s); break;
    case UI_TAB_BATT:  draw_page_batt(); break;
    case UI_TAB_TOUCH: draw_page_touch(); break;
    case UI_TAB_PISIG: draw_page_pisig(); break;
    case UI_TAB_SETUP: draw_page_setup(); break;
    default: break;
    }
    if (s_draw_mtx) xSemaphoreGive(s_draw_mtx);
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

    // 设置页：语言 / 主题按钮（重复点击同一按钮无副作用）
    if (s_tab == UI_TAB_SETUP) {
        if (y >= LBTN_Y && y < LBTN_Y + LBTN_H) {
            ui_lang_t want = s_lang;
            for (int i = 0; i < LANG_COUNT; i++) {
                int bx = LBTN_X0 + i * LBTN_DX;
                if (x >= bx && x < bx + LBTN_W) { want = (ui_lang_t)i; break; }
            }
            if (want != s_lang) {
                s_lang = want;
                prefs_save();
                ui_page_draw(s_last_uptime_s);
            }
        } else if (y >= TBTN_Y && y < TBTN_Y + TBTN_H) {
            int idx = (x - TBTN_X0) / TBTN_DX;
            int off = (x - TBTN_X0) % TBTN_DX;
            if (x >= TBTN_X0 && idx >= 0 && idx < TH_COUNT && off < TBTN_W &&
                (ui_theme_t)idx != s_theme) {
                s_theme = (ui_theme_t)idx;
                prefs_save();
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
