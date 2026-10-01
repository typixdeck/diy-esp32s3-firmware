#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "batt_log.h"
#include "builtin_apps.h"
#include "board_pins.h"
#include "freertos/queue.h"
#include "i18n.h"
#include "instrument.h"
#include "net_service.h"
#include "pi_link.h"
#include "pi_share.h"
#include "sensors.h"
#include "ttf_font.h"
#include "vsync_mon.h"
#include <time.h>

static const char *TAG = "UI";

// ---------------------------------------------------------------------------
// framebuffer 原语（RGB565）
// ---------------------------------------------------------------------------
#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_GRAY 0x8410
#define C_LGRAY 0xC618
#define C_DARK 0x10A2 // 深灰背景
#define C_NAVY 0x0951 // 头部深蓝
#define C_GREEN 0x07E0
#define C_DGREEN 0x03E0
#define C_RED 0xF800
#define C_YELL 0xFFE0
#define C_CYAN 0x07FF
#define C_ORANGE 0xFD20

static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_fb;
static ui_ctx_t s_ctx;

static void fb_fill_rect(int x0, int y0, int w, int h, uint16_t color) {
    if (!s_fb)
        return;
    if (x0 < 0) {
        w += x0;
        x0 = 0;
    }
    if (y0 < 0) {
        h += y0;
        y0 = 0;
    }
    if (x0 + w > LCD_H_RES)
        w = LCD_H_RES - x0;
    if (y0 + h > LCD_V_RES)
        h = LCD_V_RES - y0;
    if (w <= 0 || h <= 0)
        return;
    for (int y = y0; y < y0 + h; y++) {
        uint16_t *row = s_fb + y * LCD_H_RES + x0;
        for (int x = 0; x < w; x++) {
            row[x] = color;
        }
    }
}

static const uint8_t *glyph5x7(char c) {
    static const uint8_t A[] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t B[] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
    static const uint8_t C[] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
    static const uint8_t D[] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
    static const uint8_t E[] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    static const uint8_t F[] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
    static const uint8_t G[] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F};
    static const uint8_t H[] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t I[] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F};
    static const uint8_t J[] = {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C};
    static const uint8_t K[] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    static const uint8_t L[] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    static const uint8_t M[] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
    static const uint8_t N[] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    static const uint8_t O[] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const uint8_t P[] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
    static const uint8_t Q[] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
    static const uint8_t R[] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    static const uint8_t S[] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
    static const uint8_t T[] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t U[] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const uint8_t V[] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
    static const uint8_t W[] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11};
    static const uint8_t X[] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
    static const uint8_t Y[] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t Z[] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
    static const uint8_t d0[] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    static const uint8_t d1[] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x1F};
    static const uint8_t d2[] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
    static const uint8_t d3[] = {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E};
    static const uint8_t d4[] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
    static const uint8_t d5[] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E};
    static const uint8_t d6[] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E};
    static const uint8_t d7[] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    static const uint8_t d8[] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
    static const uint8_t d9[] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C};
    static const uint8_t dash[] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
    static const uint8_t dot[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
    static const uint8_t pct[] = {0x19, 0x1A, 0x02, 0x04, 0x08, 0x0B, 0x13};
    static const uint8_t slash[] = {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10};
    static const uint8_t colon[] = {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00};
    static const uint8_t plus[] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
    static const uint8_t blank[] = {0, 0, 0, 0, 0, 0, 0};
    switch (c) {
    case 'A':
        return A;
    case 'B':
        return B;
    case 'C':
        return C;
    case 'D':
        return D;
    case 'E':
        return E;
    case 'F':
        return F;
    case 'G':
        return G;
    case 'H':
        return H;
    case 'I':
        return I;
    case 'J':
        return J;
    case 'K':
        return K;
    case 'L':
        return L;
    case 'M':
        return M;
    case 'N':
        return N;
    case 'O':
        return O;
    case 'P':
        return P;
    case 'Q':
        return Q;
    case 'R':
        return R;
    case 'S':
        return S;
    case 'T':
        return T;
    case 'U':
        return U;
    case 'V':
        return V;
    case 'W':
        return W;
    case 'X':
        return X;
    case 'Y':
        return Y;
    case 'Z':
        return Z;
    case '0':
        return d0;
    case '1':
        return d1;
    case '2':
        return d2;
    case '3':
        return d3;
    case '4':
        return d4;
    case '5':
        return d5;
    case '6':
        return d6;
    case '7':
        return d7;
    case '8':
        return d8;
    case '9':
        return d9;
    case '-':
        return dash;
    case '.':
        return dot;
    case '%':
        return pct;
    case '/':
        return slash;
    case ':':
        return colon;
    case '+':
        return plus;
    default:
        return blank;
    }
}

static void fb_draw_text(int x, int y, const char *text, int scale, uint16_t color) {
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
static inline int text_w(const char *text, int scale) {
    return (int)strlen(text) * 6 * scale - scale;
}

static void fb_draw_text_centered(int y, const char *text, int scale, uint16_t color) {
    fb_draw_text((LCD_H_RES - text_w(text, scale)) / 2, y, text, scale, color);
}

// ---------------------------------------------------------------------------
// 语言、主题色和亮暗背景（NVS: ui/lang, ui/accent, ui/light）。旧 theme 键忽略。
// ---------------------------------------------------------------------------
// 语言顺序 = NVS 里的数值 = 设置页按钮顺序，只能在末尾追加
typedef enum { LANG_EN = 0, LANG_ZH = 1, LANG_TW = 2, LANG_JA = 3, LANG_COUNT } ui_lang_t;
static const char *k_lang_name[LANG_COUNT] = {"English", "简体中文", "繁體中文", "日本語"};
static ui_lang_t s_lang = LANG_ZH;

#define RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
typedef struct {
    uint16_t bg, card, card2, frame, text, text2, dim, accent, accent2, good, warn, bad, tabbar,
        tab_sel_bg, tab_sel_fg, tab_fg;
} theme_pal_t;
static uint8_t s_accent = 0;
/* Existing accent indices 0..7 remain compatible; 8 selects the custom RGB color. */
#define CUSTOM_ACCENT 8
static uint8_t s_custom_rgb[3] = {8, 112, 224};
static uint8_t s_color_draft[3];
static bool s_color_editor;
static bool s_light = false;
static bool s_prefs_failed;
static const uint16_t k_accents[] = {RGB(8, 112, 224),  RGB(0, 173, 143), RGB(137, 85, 232),
                                     RGB(227, 64, 127), RGB(235, 135, 0), RGB(0, 162, 192),
                                     RGB(205, 83, 37),  RGB(112, 146, 41)};
static theme_pal_t s_palette;
static const theme_pal_t *pal(void) {
    return &s_palette;
}
static uint16_t color_ink(uint16_t color) {
    /* Relative luminance selects readable text even for arbitrary custom colors. */
    float rgb[3] = {((color >> 11) & 31) / 31.0f, ((color >> 5) & 63) / 63.0f,
                    (color & 31) / 31.0f};
    for (int i = 0; i < 3; i++)
        rgb[i] = rgb[i] <= .04045f ? rgb[i] / 12.92f : powf((rgb[i] + .055f) / 1.055f, 2.4f);
    return .2126f * rgb[0] + .7152f * rgb[1] + .0722f * rgb[2] > .179f ? C_BLACK : C_WHITE;
}
static uint16_t tint(uint16_t base,uint16_t accent,unsigned amount) {
    unsigned br=(base>>11)&31,bg=(base>>5)&63,bb=base&31;
    unsigned ar=(accent>>11)&31,ag=(accent>>5)&63,ab=accent&31;
    return ((br*(100-amount)+ar*amount)/100)<<11 |
           ((bg*(100-amount)+ag*amount)/100)<<5 | ((bb*(100-amount)+ab*amount)/100);
}
static void palette_update(void) {
    uint16_t raw=s_accent==CUSTOM_ACCENT?RGB(s_custom_rgb[0],s_custom_rgb[1],s_custom_rgb[2]):k_accents[s_accent];
    uint16_t accent=raw;
    /* Keep white selected labels readable, including yellow/white custom colors. */
    if (!s_light) while(color_ink(accent)==C_BLACK) accent=tint(accent,C_BLACK,10);
    s_palette=(theme_pal_t){
        .bg=s_light?tint(RGB(247,247,247),raw,4):tint(RGB(10,10,10),raw,8),
        .card=s_light?tint(C_WHITE,raw,3):tint(RGB(18,18,18),raw,10),
        .card2=s_light?tint(RGB(244,244,244),raw,12):tint(RGB(26,26,26),raw,14),
        .frame=s_light?tint(RGB(200,200,200),raw,25):tint(RGB(51,51,51),raw,22),
        .text=s_light?RGB(23,34,48):C_WHITE,
        .text2=s_light?RGB(65,80,98):RGB(205,205,205),
        .dim=s_light?RGB(86,100,118):RGB(160,160,160),
        .accent=accent,.accent2=s_light?tint(raw,C_BLACK,18):tint(raw,C_WHITE,36),
        .tab_sel_fg=s_light?color_ink(accent):C_WHITE,
        .good=s_light?RGB(0,115,79):RGB(81,214,153),
        .warn=s_light?RGB(151,91,0):RGB(244,189,87),
        .bad=s_light?RGB(177,44,51):RGB(249,117,123)};
}

// 多语言取词：代码里保持 (en, 简体) 双参；繁體/日语按 en 键查 i18n.h 表，
// 查不到繁體回退简体、日语回退英文。非英文都需要 TTF；font 分区没刷时强制回英文。
static const char *tr(const char *en, const char *zh) {
    if (s_lang == LANG_EN || !ttf_font_ready())
        return en;
    if (s_lang == LANG_ZH)
        return ttf_text_supported(zh) ? zh : en;
    for (size_t i = 0; i < K_I18N_COUNT; i++) {
        const i18n_entry_t *e = &k_i18n[i];
        if (strcmp(e->en, en) != 0)
            continue;
        if (e->zh && strcmp(e->zh, zh) != 0)
            continue;
        const char *t = (s_lang == LANG_TW) ? e->tw : e->ja;
        if (t && ttf_text_supported(t))
            return t;
        break;
    }
    return (s_lang == LANG_TW && ttf_text_supported(zh)) ? zh : en;
}

static void prefs_load(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* Preserve other namespaces (including Wi-Fi). Fail read-only on incompatible NVS. */
    }
    if (err != ESP_OK) {
        s_prefs_failed = true;
        ESP_LOGW(TAG, "NVS 不可用（%s），界面设置不持久", esp_err_to_name(err));
        return;
    }
    nvs_handle_t h;
    if (nvs_open("ui", NVS_READONLY, &h) == ESP_OK) {
        uint8_t v = 0;
        if (nvs_get_u8(h, "lang", &v) == ESP_OK && v < LANG_COUNT) {
            s_lang = (ui_lang_t)v;
        }
        if (nvs_get_u8(h, "accent", &v) == ESP_OK && v <= CUSTOM_ACCENT)
            s_accent = v;
        const char *keys[] = {"custom_r", "custom_g", "custom_b"};
        for (int i = 0; i < 3; i++)
            if (nvs_get_u8(h, keys[i], &v) == ESP_OK)
                s_custom_rgb[i] = v;
        if (nvs_get_u8(h, "light", &v) == ESP_OK)
            s_light = (v == 1);
        nvs_close(h);
    }
    palette_update();
}

static void prefs_save(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("ui", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        s_prefs_failed = true;
        return;
    }
    err = nvs_set_u8(h, "lang", s_lang);
    if (err == ESP_OK)
        err = nvs_set_u8(h, "accent", s_accent);
    if (err == ESP_OK)
        err = nvs_set_u8(h, "light", s_light);
    const char *keys[] = {"custom_r", "custom_g", "custom_b"};
    for (int i = 0; i < 3 && err == ESP_OK; i++)
        err = nvs_set_u8(h, keys[i], s_custom_rgb[i]);
    if (err == ESP_OK)
        err = nvs_commit(h);
    nvs_close(h);
    s_prefs_failed = err != ESP_OK;
}

// 远程调试（CDC LANG_n / TAB_n 命令）：跨任务只置请求，UI 重绘时消费，
// 避免 cdc_stats_task 直接画帧缓冲与 GUI 任务打架
static volatile int s_req_tab = -1, s_req_lang = -1;

void ui_request_tab(int t) {
    if (t >= 0 && t < UI_TAB_COUNT)
        s_req_tab = t;
}
void ui_request_lang(int l) {
    if (l >= 0 && l < LANG_COUNT)
        s_req_lang = l;
}

// ---------------------------------------------------------------------------
// TTF 优先的文本绘制（阿里巴巴普惠体，中英混排）。TTF 未就绪（font 分区没刷）
// 时回退 5x7 点阵——中文字符显示空白但布局不崩，ASCII 正常。
// size 是 TTF 像素字号；回退时粗换算 scale ≈ size/8。
// ---------------------------------------------------------------------------
static void draw_txt(int x, int y, int size, uint16_t color, const char *utf8) {
    /* Status dots/icons retain semantic color; dark-mode UI text stays neutral. */
    const theme_pal_t *p=pal();
    if (!s_light && (color==p->accent || color==p->accent2 || color==p->good || color==p->warn || color==p->bad)) color=p->text;
    if (ttf_font_ready()) {
        ttf_draw_text(s_fb, LCD_H_RES, LCD_V_RES, x, y, size, color, utf8);
    } else {
        fb_draw_text(x, y, utf8, size > 8 ? size / 8 : 1, color);
    }
}

static int txt_w(int size, const char *utf8) {
    if (ttf_font_ready())
        return ttf_text_width(size, utf8);
    return text_w(utf8, size > 8 ? size / 8 : 1);
}

static void draw_txt_centered(int y, int size, uint16_t color, const char *utf8) {
    draw_txt((LCD_H_RES - txt_w(size, utf8)) / 2, y, size, color, utf8);
}

// 宽度受限的文本：超出 maxw 就逐级缩小字号（最小 14px），保证不冲出卡片
static void draw_txt_fit(int x, int y, int size, int maxw, uint16_t color, const char *utf8) {
    while (size > 14 && txt_w(size, utf8) > maxw)
        size -= 2;
    draw_txt(x, y, size, color, utf8);
}

// 粗线段（曲线用）：Bresenham，每点画 thick×thick 方块
static void fb_draw_line(int x0, int y0, int x1, int y1, int thick, uint16_t color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (1) {
        fb_fill_rect(x0, y0, thick, thick, color);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

// ---------------------------------------------------------------------------
// 主题化绘制原语
// ---------------------------------------------------------------------------
// 圆角矩形填充：角行按圆方程收缩（每行两次浮点 sqrt，只有 2r 行，代价可忽略）
static void fb_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color) {
    if (r <= 0) {
        fb_fill_rect(x, y, w, h, color);
        return;
    }
    if (r > w / 2)
        r = w / 2;
    if (r > h / 2)
        r = h / 2;
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
static void fb_round_card(int x, int y, int w, int h, int r, int bw, uint16_t border,
                          uint16_t fill) {
    fb_fill_round_rect(x, y, w, h, r, border);
    fb_fill_round_rect(x + bw, y + bw, w - 2 * bw, h - 2 * bw, r > bw ? r - bw : 0, fill);
}

/* Native line artwork in a 40-unit coordinate space: no bitmap icon assets. */
typedef enum {
    ICON_SENSOR,
    ICON_PI,
    ICON_APPS,
    ICON_SETTINGS,
    ICON_WIFI,
    ICON_CLOCK,
    ICON_BATTERY,
    ICON_BOLT,
    ICON_PULSE,
    ICON_USB,
    ICON_CHART,
    ICON_TOUCH,
    ICON_DISPLAY,
    ICON_LANGUAGE,
    ICON_PALETTE,
    ICON_BACKGROUND,
    ICON_BACK,
    ICON_SINE,
    ICON_TRIANGLE,
    ICON_SQUARE,
    ICON_PLUCK,
    ICON_LAYERS,
    ICON_VOLUME,
    ICON_KEYBOARD,
    ICON_STOP
} icon_t;
static void icon_line(int x, int y, int size, int x0, int y0, int x1, int y1, uint16_t c) {
    fb_draw_line(x + x0 * size / 40, y + y0 * size / 40, x + x1 * size / 40, y + y1 * size / 40,
                 size >= 32 ? 2 : 1, c);
}
static void icon_arc(int x, int y, int size, int cx, int cy, int r, int from, int to, uint16_t c) {
    int px = 0, py = 0;
    for (int a = from; a <= to; a += 6) {
        float rad = a * (float)M_PI / 180;
        int xx = x + (int)((cx + r * cosf(rad)) * size / 40);
        int yy = y + (int)((cy + r * sinf(rad)) * size / 40);
        if (a != from)
            fb_draw_line(px, py, xx, yy, size >= 32 ? 2 : 1, c);
        px = xx;
        py = yy;
    }
}
static void draw_icon(icon_t icon, int x, int y, int size, uint16_t c) {
#define IL(a, b, d, e) icon_line(x, y, size, a, b, d, e, c)
#define IA(a, b, r, f, t) icon_arc(x, y, size, a, b, r, f, t, c)
    switch (icon) {
    case ICON_SENSOR:
        IL(9, 9, 31, 9);
        IL(31, 9, 31, 31);
        IL(31, 31, 9, 31);
        IL(9, 31, 9, 9);
        for (int i = 13; i <= 27; i += 7) {
            IL(i, 3, i, 8);
            IL(i, 32, i, 37);
            IL(3, i, 8, i);
            IL(32, i, 37, i);
        }
        break;
    case ICON_PI:
        IA(14, 6, 6, 190, 360);
        IA(26, 6, 6, 180, 350);
        IL(8, 5, 19, 12);
        IL(32, 5, 21, 12);
        IA(14, 17, 6, 0, 360);
        IA(26, 17, 6, 0, 360);
        IA(10, 26, 5, 0, 360);
        IA(20, 26, 6, 0, 360);
        IA(30, 26, 5, 0, 360);
        IA(15, 34, 5, 0, 360);
        IA(25, 34, 5, 0, 360);
        break;
    case ICON_APPS:
        for (int i = 0; i < 4; i++) {
            int a = 4 + (i % 2) * 19, b = 4 + (i / 2) * 19;
            IL(a, b, a + 12, b);
            IL(a + 12, b, a + 12, b + 12);
            IL(a + 12, b + 12, a, b + 12);
            IL(a, b + 12, a, b);
        }
        break;
    case ICON_SETTINGS:
        for (int a = 0; a < 360; a += 15) {
            float t = a * (float)M_PI / 180, u = (a + 15) * (float)M_PI / 180;
            int r = (a / 15) % 3 == 1 ? 18 : 15, r2 = ((a + 15) / 15) % 3 == 1 ? 18 : 15;
            IL(20 + (int)(cosf(t) * r), 20 + (int)(sinf(t) * r), 20 + (int)(cosf(u) * r2),
               20 + (int)(sinf(u) * r2));
        }
        IA(20, 20, 6, 0, 360);
        break;
    case ICON_WIFI:
        IA(20, 36, 29, 228, 312);
        IA(20, 36, 20, 228, 312);
        IA(20, 36, 10, 228, 312);
        IA(20, 33, 1, 0, 360);
        break;
    case ICON_CLOCK:
        IA(20, 20, 17, 0, 360);
        IL(20, 8, 20, 20);
        IL(20, 20, 28, 24);
        break;
    case ICON_BATTERY:
        IL(11, 6, 29, 6);
        IL(29, 6, 29, 36);
        IL(29, 36, 11, 36);
        IL(11, 36, 11, 6);
        IL(16, 3, 24, 3);
        IL(16, 3, 16, 6);
        IL(24, 3, 24, 6);
        fb_fill_rect(x + 15 * size / 40, y + 21 * size / 40, 11 * size / 40, 11 * size / 40, c);
        break;
    case ICON_BOLT:
        IL(24, 2, 8, 23);
        IL(8, 23, 19, 23);
        IL(19, 23, 15, 38);
        IL(15, 38, 33, 16);
        IL(33, 16, 22, 16);
        IL(22, 16, 24, 2);
        break;
    case ICON_PULSE:
        IL(3, 22, 11, 22);
        IL(11, 22, 17, 4);
        IL(17, 4, 24, 37);
        IL(24, 37, 29, 22);
        IL(29, 22, 38, 22);
        break;
    case ICON_USB:
        IL(20, 35, 20, 3);
        IL(20, 3, 16, 9);
        IL(20, 3, 24, 9);
        IL(20, 27, 10, 19);
        IL(10, 19, 10, 13);
        IL(20, 22, 31, 15);
        IL(31, 15, 31, 10);
        IA(20, 35, 3, 0, 360);
        IA(10, 12, 2, 0, 360);
        IL(28, 8, 34, 8);
        IL(34, 8, 34, 12);
        IL(34, 12, 28, 12);
        IL(28, 12, 28, 8);
        break;
    case ICON_CHART:
        IL(8, 22, 8, 36);
        IL(9, 22, 9, 36);
        IL(20, 6, 20, 36);
        IL(21, 6, 21, 36);
        IL(32, 15, 32, 36);
        IL(33, 15, 33, 36);
        break;
    case ICON_TOUCH:
        IA(17, 12, 7, 174, 360);
        IL(13, 24, 13, 12);
        IA(17, 12, 4, 180, 360);
        IL(21, 12, 21, 21);
        IL(21, 21, 31, 24);
        IL(31, 24, 31, 32);
        IA(22, 30, 9, 12, 174);
        IL(13, 32, 7, 23);
        IA(10, 22, 3, 180, 330);
        IL(12, 22, 15, 27);
        break;
    case ICON_DISPLAY:
        IL(4, 5, 36, 5);
        IL(36, 5, 36, 29);
        IL(36, 29, 4, 29);
        IL(4, 29, 4, 5);
        IL(20, 29, 20, 35);
        IL(13, 35, 27, 35);
        break;
    case ICON_LANGUAGE:
        IA(20, 20, 17, 0, 360);
        IL(3, 20, 37, 20);
        IL(7, 10, 33, 10);
        IL(7, 30, 33, 30);
        IA(32, 20, 20, 132, 228);
        IA(8, 20, 20, 312, 408);
        break;
    case ICON_PALETTE:
        IA(20, 19, 17, 90, 420);
        IL(28, 34, 20, 34);
        IL(20, 34, 17, 28);
        IL(17, 28, 22, 23);
        IL(22, 23, 33, 24);
        IA(12, 16, 2, 0, 360);
        IA(20, 9, 2, 0, 360);
        IA(29, 14, 2, 0, 360);
        break;
    case ICON_BACKGROUND:
        IA(20, 20, 17, 0, 360);
        IL(20, 3, 20, 37);
        for (int i = 21; i < 36; i++) {
            int h = (int)sqrtf(16 * 16 - (i - 20) * (i - 20));
            IL(i, 20 - h, i, 20 + h);
        }
        break;
    case ICON_BACK:
        IL(25, 7, 12, 20);
        IL(12, 20, 25, 33);
        break;
    case ICON_SINE:
    case ICON_PLUCK:
        for (int i = 1; i < 38; i++) {
            float a = (i - 1) / 37.0f, b = i / 37.0f;
            float va = sinf(a * (icon == ICON_PLUCK ? 6 : 2) * (float)M_PI),
                  vb = sinf(b * (icon == ICON_PLUCK ? 6 : 2) * (float)M_PI);
            if (icon == ICON_PLUCK) {
                va *= expf(-a * 3.3f);
                vb *= expf(-b * 3.3f);
            }
            IL(i - 1, 20 - (int)(va * 16), i, 20 - (int)(vb * 16));
        }
        break;
    case ICON_TRIANGLE:
        IL(1, 32, 20, 7);
        IL(20, 7, 39, 32);
        break;
    case ICON_SQUARE:
        IL(1, 30, 6, 30);
        IL(6, 30, 6, 8);
        IL(6, 8, 16, 8);
        IL(16, 8, 16, 30);
        IL(16, 30, 25, 30);
        IL(25, 30, 25, 8);
        IL(25, 8, 35, 8);
        IL(35, 8, 35, 30);
        IL(35, 30, 39, 30);
        break;
    case ICON_LAYERS:
        IL(3, 12, 20, 3);
        IL(20, 3, 37, 12);
        IL(37, 12, 20, 22);
        IL(20, 22, 3, 12);
        IL(3, 21, 20, 30);
        IL(20, 30, 37, 21);
        IL(3, 29, 20, 38);
        IL(20, 38, 37, 29);
        break;
    case ICON_VOLUME:
        IL(3, 14, 12, 14);
        IL(12, 14, 23, 5);
        IL(23, 5, 23, 35);
        IL(23, 35, 12, 26);
        IL(12, 26, 3, 26);
        IL(3, 26, 3, 14);
        IA(22, 20, 10, 312, 408);
        IA(22, 20, 17, 312, 408);
        break;
    case ICON_KEYBOARD:
        IL(1, 7, 39, 7);
        IL(39, 7, 39, 33);
        IL(39, 33, 1, 33);
        IL(1, 33, 1, 7);
        for (int i = 8; i <= 32; i += 8) {
            IL(i, 14, i + 1, 14);
            IL(i, 21, i + 1, 21);
        }
        IL(12, 27, 28, 27);
        break;
    case ICON_STOP:
        fb_fill_round_rect(x + 10 * size / 40, y + 10 * size / 40, size / 2, size / 2, 2, c);
        break;
    }
#undef IL
#undef IA
}
static void draw_check(int x, int y, int size, uint16_t color) {
    fb_draw_line(x, y + size / 2, x + size / 3, y + size - 1, 2, color);
    fb_draw_line(x + size / 3, y + size - 1, x + size, y, 2, color);
}

// EV 主题圆环电量表：圆心 (cx,cy)，外/内半径，pct 0..100，从正上方顺时针。
// 颜色沿弧度青→绿渐变；只扫环带 bounding box，环外像素零成本跳过。
static void fb_flush(void) {
    if (s_panel && s_fb) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_H_RES, LCD_V_RES, s_fb);
    }
}

const uint16_t *ui_framebuffer(void) {
    return s_fb;
}

// 截图快照：与整页重绘互斥，避免 CDC 任务在 GUI 画到一半时拷走撕裂帧
static SemaphoreHandle_t s_draw_mtx;

bool ui_snapshot(uint16_t *dst) {
    if (!s_fb || !dst)
        return false;
    if (s_draw_mtx)
        xSemaphoreTake(s_draw_mtx, portMAX_DELAY);
    memcpy(dst, s_fb, (size_t)LCD_H_RES * LCD_V_RES * 2);
    if (s_draw_mtx)
        xSemaphoreGive(s_draw_mtx);
    return true;
}

// ---------------------------------------------------------------------------
// 初始化
// ---------------------------------------------------------------------------
esp_err_t ui_init(esp_lcd_panel_handle_t panel, const ui_ctx_t *ctx) {
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
        // 开机字标用的打字机体（Special Elite，Apache-2.0），子集内嵌在 app 里
        extern const uint8_t _binary_special_elite_subset_ttf_start[];
        extern const uint8_t _binary_special_elite_subset_ttf_end[];
        ttf_font_add_face(TTF_FACE_DECO, _binary_special_elite_subset_ttf_start,
                          (size_t)(_binary_special_elite_subset_ttf_end -
                                   _binary_special_elite_subset_ttf_start));
    }
    palette_update();
    prefs_load();
    if (!s_draw_mtx)
        s_draw_mtx = xSemaphoreCreateMutex();
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// 开机动画：TYPIXDECK + 12 点旋转 spinner（Ubuntu 风格拖尾渐隐）
// ---------------------------------------------------------------------------
#define SPIN_DOTS 12
#define SPIN_CX (LCD_H_RES / 2)
#define SPIN_CY 470
#define SPIN_R 80
#define SPIN_DOT 16

// RGB565 灰度（0..255）

// ---------------------------------------------------------------------------
// 开机画面（2026-09-10 重做）：
//   字标 "TypixDeck" 用 Special Elite 打字机体逐字敲出（带闪烁光标），
//   下方 SpinKit "Wave" 五根竖条（MIT，原版 keyframes：0%/40%/100%→0.4，20%→1.0），
//   再下方 "WAITING FOR PI SIGNAL" 宽字距小标。
//   彩蛋：等待 20s/45s/90s 分级提示逐字打出（45s 那条是真实 SOP：SW8 拨 USB6 侧再开机）；
//   Pi 出图瞬间竖条满格变绿 + "PI SIGNAL LOCKED" 定格 350ms 再交屏（ui_boot_signal_locked）；
//   右下角固件版本/编译日期。每帧只重绘字标带、竖条带、提示带三块脏区。
// ---------------------------------------------------------------------------
#define BOOT_TICK_MS 80
#define C_MINT RGB(93, 202, 165)
#define WM_TEXT "TypixDeck"
#define WM_SIZE 120
#define WM_Y 190
#define WM_SPACING 6
#define WAVE_N 5
#define WAVE_W 18
#define WAVE_GAP 16
#define WAVE_H 90
#define WAVE_CY 470
#define SUB_Y 556
#define HINT_Y 640
#define HINT_DY 40

// 取 UTF-8 串前 n 个字符（按字符不按字节），写入 out
static void utf8_prefix(const char *src, int n, char *out, size_t cap) {
    size_t o = 0;
    for (const char *p = src; *p && n > 0; n--) {
        int len = ((*p & 0xE0) == 0xC0)   ? 2
                  : ((*p & 0xF0) == 0xE0) ? 3
                  : ((*p & 0xF8) == 0xF0) ? 4
                                          : 1;
        if (o + len >= cap)
            break;
        memcpy(out + o, p, len);
        o += len;
        p += len;
    }
    out[o] = 0;
}
static int utf8_len(const char *s) {
    int n = 0;
    for (; *s; s++)
        if ((*s & 0xC0) != 0x80)
            n++;
    return n;
}

// SpinKit wave：t ∈ [0,1) 周期相位 → scaleY
static float wave_scale(float t) {
    float e;
    if (t < 0.2f) {
        e = t / 0.2f;
        return 0.4f + 0.6f * (0.5f - 0.5f * cosf(e * (float)M_PI));
    } else if (t < 0.4f) {
        e = (t - 0.2f) / 0.2f;
        return 1.0f - 0.6f * (0.5f - 0.5f * cosf(e * (float)M_PI));
    }
    return 0.4f;
}

static void boot_draw_wave(uint32_t t_ms, uint16_t color, bool full) {
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
static bool boot_type_line(int face, int y, int size, int spacing, uint16_t color, const char *text,
                           int typed, bool cursor_on) {
    char buf[160];
    int n = utf8_len(text);
    if (typed > n)
        typed = n;
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

static void boot_draw_version(void) {
    const esp_app_desc_t *d = esp_app_get_description();
    char buf[80];
    snprintf(buf, sizeof(buf), "fw %s · %s", d->version, d->date);
    int w = txt_w(16, buf);
    draw_txt(LCD_H_RES - w - 24, LCD_V_RES - 34, 16, RGB(70, 78, 86), buf);
}

void ui_boot_anim_tick(int frame, bool show_hint) {
    (void)show_hint;
    static bool s_static_drawn = false;
    static int s_hint_stage = 0; // 0 无；1=20s；2=45s；3=90s
    static int s_hint_typed = 0;
    uint32_t t_ms = (uint32_t)frame * BOOT_TICK_MS;
    bool deco = ttf_face_ready(TTF_FACE_DECO);

    if (!s_static_drawn) {
        s_static_drawn = true;
        fb_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, C_BLACK);
        if (ttf_font_ready())
            boot_draw_version();
    }

    // 1. 字标打字机：每帧（80ms）敲一个字母（0.7s 打完），光标 320ms 周期闪 1s 后消失
    int wm_n = utf8_len(WM_TEXT);
    int typed = frame;
    if (typed <= wm_n + 12) {
        if (deco) {
            bool cur = (typed < wm_n) ? true : ((frame % 4) < 2 && typed < wm_n + 12);
            boot_type_line(TTF_FACE_DECO, WM_Y, WM_SIZE, WM_SPACING, C_WHITE, WM_TEXT, typed, cur);
        } else if (typed == 0) {
            if (ttf_font_ready())
                draw_txt_centered(WM_Y, 110, C_WHITE, WM_TEXT);
            else
                fb_draw_text_centered(WM_Y + 30, "TYPIXDECK", 10, C_WHITE);
        }
        // 小标在字标打完那一帧一次画出
        if (typed == wm_n) {
            const char *sub = "WAITING FOR PI SIGNAL";
            if (deco) {
                int w = ttf_text_width_face(TTF_FACE_DECO, 26, 5, sub);
                ttf_draw_text_face(TTF_FACE_DECO, s_fb, LCD_H_RES, LCD_V_RES, (LCD_H_RES - w) / 2,
                                   SUB_Y, 26, 5, C_GRAY, sub);
            } else {
                fb_draw_text_centered(SUB_Y, sub, 3, C_GRAY);
            }
        }
    }

    // 2. SpinKit wave
    boot_draw_wave(t_ms, C_MINT, false);

    // 3. 分级等待提示（逐字打出）
    int want = (t_ms >= 90000) ? 3 : (t_ms >= 45000) ? 2 : (t_ms >= 20000) ? 1 : 0;
    if (want != s_hint_stage) {
        s_hint_stage = want;
        s_hint_typed = 0;
    }
    if (s_hint_stage > 0 && ttf_font_ready()) {
        const char *l1 =
            (s_hint_stage == 1) ? tr("STILL WAITING FOR PI SIGNAL", "仍在等待树莓派信号")
            : (s_hint_stage == 2)
                ? tr("HINT: FLIP SW8 TO THE USB6 SIDE, THEN POWER ON",
                     "提示：先把 SW8 拨到 USB6 侧再开机")
                : tr("STILL HERE. SO IS THE PI, PROBABLY.", "还在等。树莓派大概也在。");
        const char *l2 = tr("PRESS □ KEY FOR SYSTEM MONITOR", "按 □ 键进入系统监控");
        int n1 = utf8_len(l1);
        if (s_hint_typed <= n1 + 8) {
            bool done =
                boot_type_line(TTF_FACE_MAIN, HINT_Y, 28, 0, s_hint_stage == 2 ? C_MINT : C_GRAY,
                               l1, s_hint_typed, s_hint_typed < n1 + 8 && (frame % 4) < 2);
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
void ui_boot_signal_locked(void) {
    // Pi 热复位时视频信号 1 帧内就到，打字机可能一个字母都没敲出来——
    // 定格帧一律先把字标补完整（无光标），别让用户看到一个孤零零的光标块
    if (ttf_face_ready(TTF_FACE_DECO))
        boot_type_line(TTF_FACE_DECO, WM_Y, WM_SIZE, WM_SPACING, C_WHITE, WM_TEXT, 99, false);
    boot_draw_wave(0, C_MINT, true);
    const char *msg = tr("PI SIGNAL LOCKED", "已锁定树莓派信号");
    fb_fill_rect(0, SUB_Y - 6, LCD_H_RES, 60, C_BLACK);
    if (ttf_face_ready(TTF_FACE_DECO) && s_lang == LANG_EN) {
        int w = ttf_text_width_face(TTF_FACE_DECO, 26, 5, msg);
        ttf_draw_text_face(TTF_FACE_DECO, s_fb, LCD_H_RES, LCD_V_RES, (LCD_H_RES - w) / 2, SUB_Y,
                           26, 5, C_MINT, msg);
    } else {
        draw_txt_centered(SUB_Y, 28, C_MINT, msg);
    }
    fb_flush();
}

#define TAB_BAR_H 90
#define TAB_W 196
static ui_tab_t s_tab = UI_TAB_SENSORS;
static uint32_t s_last_uptime_s;
static bool s_confirm_shutdown;
static int s_app; /* 0 launcher, 1 instrument, 2 clock, 3 calculator, 4 calendar, 5 2048 */
static calculator_t s_calculator;
static calendar_date_t s_calendar;
static game2048_t s_game;
static bool s_game_started, s_game_restart;
static int s_ap_page;
static int s_settings; /* 0 appearance, 1 Wi-Fi, 2 time */
static bool s_touch_down, s_shift, s_shift_left, s_shift_right, s_fn, s_sym;
static int s_touch_note = -1, s_focus = -1;
static bool s_input_dirty, s_piano_dirty;
static uint32_t s_full_draws, s_piano_draws;
static int s_color_drag = -1;
static int8_t s_held_notes[6][11];
static uint8_t s_note_refs[128];
static bool s_notes_initialized;
static const char *s_notice;
static int64_t s_notice_until;
static uint32_t s_touch_events;
static int s_touch_x = -1, s_touch_y = -1;
typedef struct {
    uint8_t row, col;
    bool pressed;
} key_event_t;
static QueueHandle_t s_keys;
static volatile bool s_key_overflow;
typedef struct {
    int x, y, w, h, id;
    bool enabled;
} button_t;
static button_t s_buttons[80];
static int s_button_count;
enum {
    ACT_TAB = 1,
    ACT_INSTRUMENT = 10,
    ACT_CLOCK,
    ACT_BACK,
    ACT_CALCULATOR,
    ACT_CALENDAR,
    ACT_GAME,
    ACT_TIMBRE = 20,
    ACT_OCT_DOWN = 25,
    ACT_OCT_UP,
    ACT_VOL_DOWN,
    ACT_VOL_UP,
    ACT_PANIC,
    ACT_SETTINGS = 30,
    ACT_LANG = 40,
    ACT_COLOR = 50,
    ACT_DARK = 60,
    ACT_LIGHT,
    ACT_CUSTOM,
    ACT_CUSTOM_APPLY,
    ACT_CUSTOM_CANCEL,
    ACT_WIFI = 70,
    ACT_SCAN,
    ACT_CANCEL,
    ACT_FORGET,
    ACT_MANUAL,
    ACT_AP_NEXT,
    ACT_AP = 80,
    ACT_SYNC = 90,
    ACT_SERVER,
    ACT_TZ_DOWN,
    ACT_TZ_UP,
    ACT_CALC_CLEAR = 500,
    ACT_CALC_DELETE,
    ACT_CALC_EQUALS,
    ACT_CALC_CHAR = 520, /* ASCII character offset; ends at 647 */
    ACT_CAL_PREV = 650,
    ACT_CAL_NEXT,
    ACT_CAL_YEAR_PREV,
    ACT_CAL_YEAR_NEXT,
    ACT_CAL_TODAY,
    ACT_GAME_MOVE = 660,
    ACT_GAME_RESTART = 670,
    ACT_GAME_CONFIRM,
    ACT_GAME_CANCEL,
    ACT_GAME_CONTINUE,
    ACT_RGB_DOWN = 300,
    ACT_RGB_UP = 310,
    ACT_EDIT_CHAR = 1000,
    ACT_EDIT_SHIFT = 1200,
    ACT_EDIT_BACK,
    ACT_EDIT_SPACE,
    ACT_EDIT_OK,
    ACT_EDIT_CANCEL
};
static void notice(const char *s) {
    s_notice = s;
    s_notice_until = esp_timer_get_time() + 5000000;
}
static void button(int id, int x, int y, int w, int h, const char *label, bool selected,
                   bool enabled) {
    const theme_pal_t *p = pal();
    int idx = s_button_count;
    if (idx < 80)
        s_buttons[s_button_count++] = (button_t){x, y, w, h, id, enabled};
    fb_round_card(x, y, w, h, 9, (idx == s_focus) ? 3 : 1,
                  idx == s_focus ? p->accent2
                  : selected     ? p->accent2
                                 : p->frame,
                  selected ? p->accent : p->card);
    uint16_t c = !enabled ? p->dim : selected ? p->tab_sel_fg : p->text;
    int size = 24;
    while (size > 14 && txt_w(size, label) > w - 20)
        size -= 2;
    draw_txt(x + (w - txt_w(size, label)) / 2, y + (h - size) / 2, size, c, label);
}
static void icon_button(int id, int x, int y, int w, int h, icon_t icon, const char *label,
                        bool selected, bool enabled) {
    button(id, x, y, w, h, "", selected, enabled);
    const theme_pal_t *p = pal();
    uint16_t c = !enabled ? p->dim : selected ? p->tab_sel_fg : p->text;
    int size = 24, icon_size = h > 70 ? 42 : 32;
    while (size > 14 && txt_w(size, label) + icon_size + 18 > w - 24)
        size -= 2;
    int left = x + (w - txt_w(size, label) - icon_size - 18) / 2;
    draw_icon(icon, left, y + (h - icon_size) / 2, icon_size, c);
    draw_txt(left + icon_size + 18, y + (h - size) / 2, size, c, label);
}
static void choice_button(int id, int x, int y, int w, int h, const char *label, bool selected) {
    button(id, x, y, w, h, "", selected, true);
    const theme_pal_t *p = pal();
    int size = 24;
    while (size > 14 && txt_w(size, label) > w - 62)
        size -= 2;
    draw_txt(x + (w - txt_w(size, label)) / 2, y + (h - size) / 2, size,
             selected ? p->tab_sel_fg : p->text, label);
    if (selected)
        draw_check(x + w - 30, y + h / 2 - 7, 12, p->tab_sel_fg);
}
static void tile(int x, int y, int w, const char *label, const char *value, const char *detail) {
    const theme_pal_t *p = pal();
    fb_round_card(x, y, w, 130, 9, 1, p->frame, p->card);
    draw_txt(x + 18, y + 14, 20, p->text2, label);
    draw_txt_fit(x + 18, y + 47, 32, w - 36, p->text, value);
    if (detail)
        draw_txt_fit(x + 18, y + 94, 17, w - 36, p->dim, detail);
}
static void section(int y, const char *label) {
    draw_txt(24, y, 23, pal()->text, label);
}
ui_tab_t ui_current_tab(void) {
    return s_tab;
}
#define PI_INFO_MAX 128
static char s_pi_info[PI_INFO_MAX];
static int64_t s_pi_info_us;
static portMUX_TYPE s_pi_info_mux = portMUX_INITIALIZER_UNLOCKED;
void ui_set_pi_info(const char *line) {
    if (!line)
        return;
    portENTER_CRITICAL(&s_pi_info_mux);
    strlcpy(s_pi_info, line, sizeof(s_pi_info));
    s_pi_info_us = esp_timer_get_time();
    portEXIT_CRITICAL(&s_pi_info_mux);
}
static bool pi_info_fresh(void) {
    portENTER_CRITICAL(&s_pi_info_mux);
    int64_t t = s_pi_info_us;
    portEXIT_CRITICAL(&s_pi_info_mux);
    return t > 0 && esp_timer_get_time() - t < 8000000;
}
static bool pi_info_get(const char *key, char *out, size_t n) {
    char local[PI_INFO_MAX];
    portENTER_CRITICAL(&s_pi_info_mux);
    strlcpy(local, s_pi_info, sizeof(local));
    portEXIT_CRITICAL(&s_pi_info_mux);
    size_t kl = strlen(key);
    for (const char *p = local; (p = strstr(p, key)); p++)
        if ((p == local || p[-1] == ' ') && p[kl] == '=') {
            const char *v = p + kl + 1, *e = v;
            while (*e && *e != ' ')
                e++;
            size_t len = e - v;
            if (!len || len >= n)
                return false;
            memcpy(out, v, len);
            out[len] = 0;
            return true;
        }
    return false;
}
typedef struct {
    const char *name;
    uint8_t addr[4];
    bool behind_mux;
} sensor_desc_t;
static const sensor_desc_t k_sensors[] = {
    {"AW9523", {0x5B}, false},        // U16 IO 扩展器
    {"INA-BAT", {0x40}, false},       // U4  电池电流计
    {"INA-BUS", {0x41}, false},       // U20 USB 电流计
    {"CW2015", {0x62}, false},        // U27 电量计
    {"STC3117", {0x70}, true},        // U53 电量计（MUX 后）
    {"QMI8658", {0x6A, 0x6B}, false}, // U6  IMU（0720 未贴）
    {"RX8130", {0x32}, false},        // U59 RTC
    {"ES8389",
     {0x10, 0x11, 0x12, 0x13},
     false},                       // U12 Codec：AD1 悬空，地址在 0x10-0x13 漂（同 audio.c）
    {"GT911", {0x5D, 0x14}, true}, // 触摸（MUX 后）
    {"KBD6R11", {0x1F}, false},    // U32 键盘 STM32（QMK I2C 从机）
};
#define N_SENSORS (sizeof(k_sensors) / sizeof(k_sensors[0]))

// 在位探测缓存（探测一轮离线器件各吃 50ms 超时，不能每次重绘都做）
static bool s_present[N_SENSORS];
static int s_present_ok = 0;
static int64_t s_present_ts_us = -1;

static bool s_mux_esp = true; // main 通过 ui_notify_mux 维护；开机 MUX 在 ESP 侧

void ui_notify_mux(bool esp_side) {
    if (esp_side && !s_mux_esp)
        s_present_ts_us = -1; // 切回 ESP 侧：立刻重探一轮
    if (!esp_side)
        ui_cancel_input();
    s_mux_esp = esp_side;
}

// 只在 ESP 持屏时探测：MUX 在 Pi 侧时 STC3117/GT911 必然无应答，探了只会误报
// （用户反馈的 "6/10" 就是切屏瞬间探到的假结果），沿用上一轮结果即可。
static void sensors_probe_maybe(void) {
    int64_t now = esp_timer_get_time();
    if (!s_mux_esp)
        return;
    if (s_present_ts_us >= 0 && now - s_present_ts_us < 5 * 1000000LL)
        return;
    s_present_ts_us = now;
    s_present_ok = 0;
    for (int i = 0; i < N_SENSORS; i++) {
        bool ok = false;
        for (int a = 0; a < 4 && k_sensors[i].addr[a] && !ok; a++)
            ok = i2c_master_probe(s_ctx.bus, k_sensors[i].addr[a], 50) == ESP_OK;
        s_present[i] = ok;
        if (ok)
            s_present_ok++;
    }
}

// dash 页共享的一次采样
typedef struct {
    float vbat_v, vbat_a, vbus_v, vbus_a, cw_v, stc_v, stc_soc;
    float stc_a; // STC3117 电池真实电流，正=充电 负=放电
    int cw_soc, soc;
    bool vbat_ok, vbus_ok, plugged;
    bool stc_i_ok; // STC3117 电流可读（MUX 在 ESP 侧）
    uint16_t soc_col;
} dash_data_t;

static dash_data_t s_dash_last; // 动画 tick 用的最近一次采样
static void dash_read(dash_data_t *d) {
    const theme_pal_t *p = pal();
    memset(d, 0, sizeof(*d));
    d->stc_soc = -1;
    d->cw_soc = -1;
    d->vbat_ok = s_ctx.ina_vbat && ina219_read(s_ctx.ina_vbat, &d->vbat_v, &d->vbat_a) == ESP_OK;
    d->vbus_ok = s_ctx.ina_vbus && ina219_read(s_ctx.ina_vbus, &d->vbus_v, &d->vbus_a) == ESP_OK;
    if (!s_ctx.cw2015 || cw2015_read(s_ctx.cw2015, &d->cw_v, &d->cw_soc) != ESP_OK)
        d->cw_soc = -1;
    if (!s_mux_esp || !s_ctx.stc3117 ||
        stc3117_read(s_ctx.stc3117, &d->stc_v, &d->stc_soc) != ESP_OK)
        d->stc_soc = -1;
    d->stc_i_ok =
        s_mux_esp && s_ctx.stc3117 && stc3117_read_current(s_ctx.stc3117, &d->stc_a) == ESP_OK;
    // 主 SOC 只用 STC3117（带采样电阻库仑计，2026-09-11 起不再回退 CW2015：
    // 切屏瞬间 STC 还在 MUX 另一侧，回退会闪一下 CW2015 的另一个百分比）。
    // 新鲜有效样本到来前显示 "--"；切屏后至少两个采样确认转换计数前进。
    d->soc = d->stc_soc >= 0 ? (int)(d->stc_soc + 0.5f) : -1;
    d->plugged = d->vbus_ok && d->vbus_v > 4.0f;
    d->soc_col = d->soc < 0 ? p->dim : d->soc < 15 ? p->bad : d->soc < 40 ? p->warn : p->good;
    s_dash_last = *d;
}

/* Hardware routes are schematic relationships, not a claim that every link is live. */
#define HISTORY_X 100
#define HISTORY_Y 320
#define HISTORY_W 852
#define HISTORY_H 285
static int s_topo_detail; /* 1 battery, 2 load, 3 input, 4 interfaces */
static int s_history_metric;
static int history_sample_x(int index, int count) {
    int age = (count - 1 - index) * BATT_LOG_INTERVAL_S;
    return HISTORY_X + HISTORY_W - age * HISTORY_W / (BATT_LOG_CAP * BATT_LOG_INTERVAL_S);
}
static float history_value(const batt_sample_t *s, int metric) {
    if (metric == 0) return s->soc >= 0 && s->soc <= 100 ? s->soc : -1;
    if (metric == 1) return s->mv ? s->mv / 1000.f : -1;
    if (metric == 2) return s->mv && s->ma >= 0 ? s->mv * s->ma / 1000000.f : -1;
    return s->bus_mv && s->bus_ma >= 0 ? s->bus_mv * s->bus_ma / 1000000.f : -1;
}
static void draw_topo_detail(void) {
    const theme_pal_t *p = pal();
    button(410, 24, 105, 140, 48, tr("Back", "返回"), false, true);
    if (s_topo_detail == 4) {
        draw_txt(192, 112, 27, p->text, tr("I2C devices", "I2C 设备"));
        for (int i = 0; i < N_SENSORS; i++) {
            int x = 24 + i % 2 * 500, y = 183 + i / 2 * 102;
            fb_round_card(x, y, 476, 86, 10, 1, p->frame, p->card);
            draw_txt(x + 18, y + 14, 24, p->text, k_sensors[i].name);
            char b[80];
            snprintf(b, sizeof(b), "0x%02X%s", k_sensors[i].addr[0],
                     k_sensors[i].behind_mux ? " / MUX" : "");
            draw_txt(x + 18, y + 53, 17, p->text2, b);
            bool stale = k_sensors[i].behind_mux && !s_mux_esp;
            draw_txt_fit(x + 255, y + 31, 20, 204,
                         !stale && s_present[i] ? p->good : p->dim,
                         stale ? tr("Unavailable", "不可读") : s_present[i]
                           ? tr("Detected", "已检测") : tr("Not detected", "未检测"));
        }
        return;
    }
    draw_txt(192, 112, 27, p->text, tr("History / last hour", "历史曲线 / 最近一小时"));
    const char *names[] = {tr("Battery %", "电量 %"), tr("Voltage V", "电压 V"),
                          tr("System W", "整机 W"), tr("USB input W", "USB 输入 W")};
    for (int i = 0; i < 4; i++)
        button(420 + i, 24 + i * 248, 177, 232, 55, names[i], s_history_metric == i, true);
    fb_round_card(24, 260, 976, 409, 12, 1, p->frame, p->card);
    static batt_sample_t *samples;
    if (!samples) samples=heap_caps_malloc(sizeof(*samples)*BATT_LOG_CAP,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!samples) { draw_txt_centered(420,24,p->warn,tr("Not enough memory","内存不足")); return; }
    int n = batt_log_get(samples, BATT_LOG_CAP), valid = 0;
    draw_txt(44,278,17,p->text2,s_history_metric==0 ? "STC3117 / CW2015 fallback"
        : s_history_metric==3 ? "INA219 / VBUS" : "INA219 / VBAT");
    float high = s_history_metric == 0 ? 100 : s_history_metric == 1 ? 5 : 1;
    for (int i = 0; i < n; i++) {
        float value = history_value(&samples[i], s_history_metric);
        if (value >= 0) { valid++; if (value > high) high = ceilf(value); }
    }
    for (int i = 0; i <= 4; i++) {
        int y = HISTORY_Y + i * HISTORY_H / 4;
        char b[24]; snprintf(b, sizeof(b), "%.1f", high * (4-i) / 4);
        draw_txt(HISTORY_X - 14 - txt_w(17, b), y - 8, 17, p->text2, b);
        for (int x = HISTORY_X; x < HISTORY_X + HISTORY_W; x += 10)
            fb_fill_rect(x, y, 4, 1, p->frame);
        int x = HISTORY_X + i * HISTORY_W / 4;
        snprintf(b, sizeof(b), "-%dm", 60 - 15 * i);
        draw_txt(x - 19, HISTORY_Y + HISTORY_H + 24, 17, p->text2,
                 i == 4 ? tr("Now", "现在") : b);
    }
    int px = 0, py = 0; bool have = false;
    for (int i = 0; i < n; i++) {
        float value = history_value(&samples[i], s_history_metric);
        if (value < 0) { have = false; continue; }
        int x = history_sample_x(i, n), y = HISTORY_Y + HISTORY_H - value * HISTORY_H / high;
        if (have) fb_draw_line(px, py, x, y, 2, p->accent);
        else fb_fill_rect(x, y, 3, 3, p->accent);
        px = x; py = y; have = true;
    }
    if (valid < 2)
        draw_txt(338, 452, 24, p->dim, tr("Collecting samples…", "正在积累采样…"));
    draw_txt_fit(28, 697, 19, 965, p->text2,
                 tr("5 s samples · RAM history resets on restart · gaps mean no reading",
                    "每 5 秒采样 · 重启后重新记录 · 断线表示无读数"));
}
static void topo_node(int id, int x, int y, int w, int h, icon_t icon,
                      const char *title, const char *value, const char *sub, uint16_t status) {
    const theme_pal_t *p = pal();
    button(id, x, y, w, h, "", false, true);
    draw_icon(icon, x + 14, y + 14, 24, p->accent2);
    draw_txt_fit(x + 48, y + 15, 18, w - 66, p->text2, title);
    fb_fill_round_rect(x + w - 16, y + 16, 6, 6, 3, status);
    draw_txt_fit(x + 14, y + 47, 26, w - 28, p->text, value);
    if (sub) draw_txt_fit(x + 14, y + h - 26, 15, w - 28, p->text2, sub);
}
static void draw_sensors(void) {
    const theme_pal_t *p = pal();
    dash_data_t d; dash_read(&d); sensors_probe_maybe();
    if (s_topo_detail) { draw_topo_detail(); return; }
    net_snapshot_t net; net_service_get_snapshot(&net);
    pi_link_snapshot_t pi; pi_share_get_preferred_status(&pi);
    uint16_t power = p->warn, usb = p->accent2, bus = p->good, video = RGB(164, 133, 224);
    draw_txt(24, 103, 22, p->text, "TYPIXDECK / LIVE");
    const char *legend[] = {tr("Power", "供电"), "USB/I2S", "I2C", tr("Display", "显示")};
    uint16_t colors[] = {power, usb, bus, video};
    for (int i = 0; i < 4; i++) {
        int x = 530 + i * 118;
        fb_fill_rect(x, 115, 20, 3, colors[i]);
        draw_txt(x + 28, 105, 17, p->text2, legend[i]);
    }
    /* Power sources meet at PMIC; the load reading belongs to INA-BAT, not the cell. */
    fb_draw_line(222, 196, 267, 196, 2, power);
    fb_draw_line(222, 341, 247, 341, 2, power);
    fb_draw_line(247, 341, 247, 215, 2, power);
    fb_draw_line(247, 215, 267, 215, 2, power);
    fb_draw_line(371, 254, 371, 286, 2, power);
    fb_draw_line(371, 286, 879, 286, 2, power);
    fb_draw_line(879, 286, 879, 268, 2, power);
    fb_draw_line(414, 286, 414, 314, 2, power);
    /* CM USB host -> switch/hub -> ESP CDC/UAC and keyboard HID. */
    fb_draw_line(706, 201, 756, 201, 2, usb);
    fb_draw_line(599, 254, 599, 314, 2, usb);
    fb_draw_line(500, 222, 488, 222, 2, usb);
    fb_draw_line(488, 222, 488, 272, 2, usb);
    fb_draw_line(488, 272, 280, 272, 2, usb);
    fb_draw_line(280, 272, 280, 548, 2, usb);
    fb_draw_line(280, 548, 130, 548, 2, usb);
    fb_draw_line(130, 548, 130, 574, 2, usb);
    /* Display is switched, never HDMI captured by the ESP. */
    fb_draw_line(946, 268, 946, 314, 2, video);
    fb_draw_line(630, 356, 756, 356, 2, video);
    draw_txt(658, 330, 15, p->text2, "RGB");
    fb_draw_line(630, 416, 716, 416, 2, usb);
    fb_draw_line(716, 416, 716, 495, 2, usb);
    fb_draw_line(716, 495, 756, 495, 2, usb);
    draw_txt(662, 390, 15, usb, "I2S");
    /* Shared ESP I2C bus, with muxed touch/gauge branch. */
    fb_draw_line(500, 460, 500, 556, 2, bus);
    fb_draw_line(60, 556, 904, 556, 2, bus);
    const int taps[] = {60, 386, 633, 904};
    for (int i = 0; i < 4; i++) fb_draw_line(taps[i], 556, taps[i], 574, 2, bus);
    draw_txt(407, 494, 16, bus, "I2C / 6,7");
    char v[64], sub[80];
    draw_txt(24,456,17,p->text2,tr("Free memory","可用内存"));
    snprintf(v,sizeof(v),"RAM %u KB",(unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)/1024));
    draw_txt(24,484,18,p->text,v);
    snprintf(v,sizeof(v),"PSRAM %.1f MB",heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1048576.f);
    draw_txt(24,510,17,p->text2,v);
    if (d.vbus_ok) snprintf(v, sizeof(v), "%.2f W", d.vbus_v * d.vbus_a);
    else strcpy(v, "-- W");
    snprintf(sub, sizeof(sub), d.vbus_ok ? "%.2f V / %.0f mA" : "-- V / -- mA", d.vbus_v, d.vbus_a * 1000);
    topo_node(403, 20, 150, 202, 110, ICON_USB, tr("USB power", "USB 电源"), v, sub,
              d.vbus_ok ? p->good : p->dim);
    if (d.soc >= 0) snprintf(v, sizeof(v), "%d%% / %.2f V", d.soc, d.stc_v);
    else strcpy(v, "--% / -- V");
    if (d.stc_i_ok) snprintf(sub, sizeof(sub), "STC3117 / %+.0f mA", d.stc_a * 1000);
    else strcpy(sub, "STC3117 / -- mA");
    topo_node(401, 20, 302, 202, 118, ICON_BATTERY, tr("Battery", "电池"), v, sub, d.soc_col);
    if (d.vbat_ok) snprintf(v, sizeof(v), "%.2f W", d.vbat_v * d.vbat_a);
    else strcpy(v, "-- W");
    topo_node(402, 268, 150, 208, 104, ICON_BOLT, tr("Power / PMIC", "电源 / PMIC"), v,
              "INA-BAT / INA-BUS", d.vbat_ok ? p->good : p->dim);
    topo_node(404, 500, 150, 206, 104, ICON_USB, "USB HUB", "FE2.1", "SW8 / HOST-DEVICE", p->dim);
    topo_node(405, 756, 150, 248, 118, ICON_PI, tr("Raspberry Pi", "树莓派"),
              pi.online ? tr("Online", "在线") : tr("Unknown", "未知"),
              tr("TF / USB / DPI", "TF / USB / DPI"), pi.online ? p->good : p->dim);
    snprintf(v, sizeof(v), "DIY %s", esp_app_get_description()->version);
    snprintf(sub, sizeof(sub), "Wi-Fi / %s", net.connected ? net.ip : "--");
    topo_node(406, 392, 314, 238, 146, ICON_SENSOR, "ESP32-S3", v, sub, p->good);
    if (s_touch_x>=0 && s_touch_y>=0) snprintf(sub,sizeof(sub),"Touch %d,%d / %lu",s_touch_x,s_touch_y,(unsigned long)s_touch_events);
    else strcpy(sub,"Touch --,-- / I2C MUX");
    topo_node(404, 756, 314, 248, 118, ICON_DISPLAY, "LCD / MUX / GT911",
              s_mux_esp ? "ESP32 → LCD" : "Pi → LCD", sub, p->accent2);
    topo_node(407, 756, 444, 248, 104, ICON_VOLUME, "ES8389 / I2S",
              s_present[7] ? tr("Detected", "已检测") : tr("Not detected", "未检测"), "SPK / MIC / Headphones",
              s_present[7] ? p->good : p->dim);
    topo_node(404, 20, 574, 232, 104, ICON_KEYBOARD, "STM32 / Keyboard", "USB HID + I2C",
              "KBD6R11 / 0x1F", s_present[9] ? p->good : p->dim);
    topo_node(404, 268, 574, 232, 104, ICON_SENSOR, "AW9523", tr("I/O control", "I/O 控制"),
              "MUX / IRQ / 0x5B", s_present[0] ? p->good : p->dim);
    snprintf(v, sizeof(v), "%d / 3", s_present[1] + s_present[2] + s_present[3]);
    topo_node(404, 516, 574, 232, 104, ICON_PULSE, tr("Power monitors", "电源监测"), v,
              "INA × 2 / CW2015", p->dim);
    snprintf(v, sizeof(v), "RTC %s / IMU %s", s_present[6] ? "OK" : "--", s_present[5] ? "OK" : "--");
    topo_node(404, 764, 574, 240, 104, ICON_CLOCK, "RX8130 / QMI8658", v, "I2C / 0x32 / 0x6A", p->dim);
    snprintf(v, sizeof(v), "%s %lu:%02lu · %d/%u I2C", tr("Up", "运行"),
             (unsigned long)s_last_uptime_s / 3600, (unsigned long)s_last_uptime_s / 60 % 60,
             s_present_ok, (unsigned)N_SENSORS);
    draw_txt(24, 713, 18, p->text2, v);
    draw_txt(560, 713, 18, p->text2, tr("Tap power nodes for history", "点击电源节点查看历史"));
}
static int s_pi_page, s_file_page;
static void draw_pi_sharing(void) {
    const theme_pal_t *p=pal();
    pi_share_snapshot_t share; pi_share_get_snapshot(&share);
    button(430,24,106,140,48,tr("Back","返回"),false,true);
    draw_txt(192,113,26,p->text,s_pi_page==1 ? tr("Shared files","共享文件") : tr("Pi screenshot","树莓派截图"));
    button(431,760,106,240,48,share.busy ? tr("Cancel","取消") : tr("Refresh","刷新"),false,true);
    if (!share.configured) {
        draw_txt_centered(300,26,p->dim,tr("Pair the companion over USB first","请先通过 USB 配对伴随服务"));
        return;
    }
    if (share.error) {
        const char *errors[]={"",tr("Wi-Fi offline","Wi-Fi 未连接"),tr("Sync the clock first","请先同步时间"),
            tr("Service unavailable / request failed","服务不可用 / 请求失败"),tr("Not enough memory","内存不足"),
            tr("Pairing could not be saved","配对无法保存"),tr("Cancelled","已取消")};
        draw_txt_fit(26,177,20,975,p->warn,share.error<7 ? errors[share.error] : errors[3]);
            char cause[96]; snprintf(cause,sizeof(cause),"%s %d / HTTP %d / SDK %d / %lu ms",
            tr("Stage","阶段"),share.phase,share.http_status,share.sdk_error,(unsigned long)share.elapsed_ms);
        draw_txt(26,207,16,p->dim,cause);
    } else draw_txt(26,177,18,p->text2,share.busy ? tr("Loading…","正在读取…") : tr("Read only / paired HTTPS","只读 / 已配对 HTTPS"));
    if (s_pi_page==2 && share.kind==SHARE_SCREEN && share.bytes==320*240*2) {
        static uint16_t *pixels;
        if (!pixels) pixels=heap_caps_malloc(320*240*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if (pixels && pi_share_copy_data(0,pixels,320*240*2)==320*240*2) {
            for(int y=0;y<480;y++) for(int x=0;x<640;x++) s_fb[(y+230)*LCD_H_RES+x+192]=pixels[(y/2)*320+x/2];
        } else draw_txt_centered(360,24,p->warn,tr("Not enough memory","内存不足"));
        return;
    }
    if (s_pi_page==1 && share.kind==SHARE_FILE) {
        draw_txt_fit(26,220,24,975,p->text,share.name);
        char text[1281]; size_t count=pi_share_copy_data(0,text,sizeof(text)-1);text[count]=0;
        bool readable=true;
        for(size_t i=0;i<count;i++) if ((unsigned char)text[i]>=127 || (text[i]<32 && text[i]!='\n' && text[i]!='\r' && text[i]!='\t')) readable=false;
        if (readable) {
            char row[90]; size_t at=0;
            for(int line=0;line<13 && at<count;line++) {
                size_t n=0;
                while(at<count && text[at]!='\n' && n<80) { char c=text[at++];row[n++]=(c=='\r'||c=='\t')?' ':c; }
                if (at<count && text[at]=='\n') at++;
                row[n]=0;draw_txt_fit(28,266+line*30,20,960,p->text2,row);
            }
        } else draw_txt(28,310,23,p->text2,tr("Binary cached in RAM; no preview","二进制已缓存到内存，无法预览"));
        draw_txt(28,716,17,p->dim,tr("RAM cache only · cleared on restart","仅内存缓存 · 重启后清除"));
        return;
    }
    if (s_pi_page==1) {
        if (!share.count && !share.busy && !share.error) {
            draw_txt_centered(360,26,p->dim,tr("Shared folder is empty","共享目录为空"));
            draw_txt_centered(405,22,p->text2,"Pi: ~/TypixDeck/shared/");
        }
        for(int i=0;i<6;i++) {
            int index=s_file_page*6+i;if(index>=share.count) break;
            char b[80];snprintf(b,sizeof(b),"%s    %lu KB",share.files[index].name,(unsigned long)(share.files[index].size+1023)/1024);
            button(440+index,24,230+i*69,976,58,b,false,!share.busy);
        }
        if (share.count>6) button(432,760,674,240,54,tr("Next page","下一页"),false,!share.busy);
    }
}
static void draw_pi(void) {
    if (s_pi_page) { draw_pi_sharing(); return; }
    const theme_pal_t *p=pal();
    pi_link_snapshot_t link; pi_link_get_snapshot(&link);
    pi_link_snapshot_t serial=link;
    pi_share_get_preferred_status(&link);
    bool can_shutdown=serial.online && serial.can_shutdown && !serial.shutdown_pending;
    if (!link.online && pi_info_fresh()) {
        char raw[32]; link.online=true; link.transport=1;
        if (pi_info_get("cpu",raw,sizeof(raw))) { float f=strtof(raw,NULL); if (isfinite(f)&&f>=0&&f<200) link.cpu_millicelsius=f*1000; }
        if (pi_info_get("freq",raw,sizeof(raw))) { float f=strtof(raw,NULL); if (isfinite(f)&&f>=0&&f<10000) link.cpu_khz=f*1000; }
    }
    pi_share_snapshot_t share; pi_share_get_snapshot(&share);
    net_snapshot_t net; net_service_get_snapshot(&net);
    char value[64],detail[80];
    snprintf(detail,sizeof(detail),"%s / %s",link.transport==2 ? "Wi-Fi / HTTPS" : link.transport==1 ? "USB / CDC" : "--",
             link.online && link.ip[0] ? link.ip : "--");
    tile(24,98,480,tr("Raspberry Pi","树莓派"),link.online ? tr("Online","在线") : tr("Unknown / offline","未知 / 离线"),detail);
    if (link.online && link.cpu_millicelsius>=0) snprintf(value,sizeof(value),"%.1f °C",link.cpu_millicelsius/1000.f);
    else strcpy(value,"--");
    tile(520,98,232,tr("CPU temperature","CPU 温度"),value,NULL);
    if (link.online && link.cpu_khz>=0) snprintf(value,sizeof(value),"%ld MHz",(long)link.cpu_khz/1000);
    else strcpy(value,"--");
    tile(768,98,232,tr("CPU frequency","CPU 频率"),value,NULL);
    if (link.online && link.mem_mib>=0) snprintf(value,sizeof(value),"%ld MiB",(long)link.mem_mib);
    else strcpy(value,"--");
    tile(24,244,312,tr("Available RAM","可用内存"),value,NULL);
    if (link.online && link.disk_mib>=0) snprintf(value,sizeof(value),"%.1f GiB",link.disk_mib/1024.f);
    else strcpy(value,"--");
    tile(352,244,312,tr("Available storage","可用存储"),value,NULL);
    if (link.online) snprintf(value,sizeof(value),"%lu:%02lu",(unsigned long)link.uptime_s/3600,(unsigned long)link.uptime_s/60%60);
    else strcpy(value,"--");
    tile(680,244,320,tr("Uptime","运行时间"),value,NULL);
    draw_txt(28,397,19,p->text2,tr("ESP32 Wi-Fi","ESP32 Wi-Fi"));
    draw_txt(230,397,21,p->text,net.connected ? net.ip : tr("Not connected","未连接"));
    draw_txt(590,397,19,p->text2,share.configured ? tr("Companion paired","伴随服务已配对") : tr("Companion not paired","伴随服务未配对"));
    button(433,24,440,310,65,tr("Shared files","共享文件"),false,share.configured);
    button(434,350,440,310,65,tr("Screenshot","屏幕截图"),false,share.configured);
    button(435,676,440,324,65,tr("Forget pairing","忘记配对"),false,share.configured);
    button(200,24,529,310,65,tr("Sleep · unavailable","休眠 · 暂不可用"),false,false);
    button(201,350,529,310,65,tr("Start · unavailable","启动 · 暂不可用"),false,false);
    /* Wi-Fi is read-only; existing authorized CDC shutdown is never extended to LAN. */
    button(202,676,529,324,65,tr("Shut down","关机"),false,can_shutdown);
    if (s_confirm_shutdown) {
        draw_txt(28,620,22,p->warn,tr("Shut down Linux? Unsaved work may be lost.","关闭 Linux？未保存的工作可能丢失。"));
        button(203,24,671,470,60,tr("Cancel","取消"),false,true);
        button(204,514,671,486,60,tr("Confirm shutdown","确认关机"),false,can_shutdown);
    } else {
        if (serial.shutdown_result != PI_LINK_SHUTDOWN_NONE) {
            const char *results[]={"",tr("Waiting for system response","等待系统响应"),
                tr("Accepted; power off not verified","已受理，尚未验证掉电"),tr("Shutdown denied","关机被拒绝"),
                tr("Shutdown failed","关机失败"),tr("Request timed out","请求超时"),tr("Disconnected","连接中断")};
            draw_txt_fit(28,603,18,970,p->warn,results[serial.shutdown_result]);
        }
        draw_txt_fit(28,635,19,970,p->text2,
                     tr("USB CDC first · Wi-Fi fallback · no heartbeat does not mean power off",
                        "优先 USB CDC · Wi-Fi 备用 · 无心跳不代表已关机"));
        snprintf(value,sizeof(value),"DPI %.1f Hz / HDMI FPS --",vsync_mon_fps());
        draw_txt(28,675,19,p->dim,value);
        draw_txt(28,711,17,p->dim,tr("DPI: last startup measurement, not live FPS","DPI：启动时测量值，非实时 FPS"));
    }
}
#define PIANO_X 40
#define PIANO_Y 380
#define PIANO_W 944
#define PIANO_H 220
#define PIANO_BLACK_W 70
#define PIANO_BLACK_H 148
static const uint8_t k_white_notes[] = {0, 2, 4, 5, 7, 9, 11};
static const uint8_t k_black_notes[] = {1, 3, 6, 8, 10};
static const uint8_t k_black_boundaries[] = {1, 2, 4, 5, 6};
static int piano_black_x(int index) {
    return PIANO_X + k_black_boundaries[index] * PIANO_W / 7 - PIANO_BLACK_W / 2;
}
/* Black keys are painted and hit-tested above the white-key regions. */
static int piano_hit(int x, int y) {
    if (x < PIANO_X || x >= PIANO_X + PIANO_W || y < PIANO_Y || y >= PIANO_Y + PIANO_H)
        return -1;
    if (y < PIANO_Y + PIANO_BLACK_H)
        for (int i = 0; i < 5; i++)
            if (x >= piano_black_x(i) && x < piano_black_x(i) + PIANO_BLACK_W)
                return k_black_notes[i];
    for (int i = 0; i < 7; i++) {
        int left = PIANO_X + i * PIANO_W / 7, right = PIANO_X + (i + 1) * PIANO_W / 7 - 4;
        if (x >= left && x < right)
            return k_white_notes[i];
    }
    return -1;
}
static void draw_piano(const instrument_snapshot_t *st) {
    const theme_pal_t *p = pal();
    static const char *white[] = {"C", "D", "E", "F", "G", "A", "B"};
    static const char *black[] = {"C#", "D#", "F#", "G#", "A#"};
    for (int i = 0; i < 7; i++) {
        int x = PIANO_X + i * PIANO_W / 7, w = PIANO_X + (i + 1) * PIANO_W / 7 - x - 4;
        bool held = s_note_refs[60 + st->octave * 12 + k_white_notes[i]] > 0;
        fb_round_card(x, PIANO_Y, w, PIANO_H, 7, 1, held ? p->accent2 : RGB(214, 228, 240),
                      held ? p->accent : RGB(235, 242, 248));
        draw_txt(x + (w - txt_w(29, white[i])) / 2, PIANO_Y + PIANO_H - 48, 29,
                 held ? p->tab_sel_fg : RGB(12, 23, 36), white[i]);
    }
    for (int i = 0; i < 5; i++) {
        int x = piano_black_x(i);
        bool held = s_note_refs[60 + st->octave * 12 + k_black_notes[i]] > 0;
        fb_fill_round_rect(x - 3, PIANO_Y, PIANO_BLACK_W + 6, PIANO_BLACK_H + 5, 6, RGB(4, 11, 18));
        fb_round_card(x, PIANO_Y, PIANO_BLACK_W, PIANO_BLACK_H, 6, 1,
                      held ? p->accent2 : RGB(66, 87, 106), held ? p->accent : RGB(22, 35, 49));
        draw_txt(x + (PIANO_BLACK_W - txt_w(22, black[i])) / 2, PIANO_Y + PIANO_BLACK_H - 38, 22,
                 held ? p->tab_sel_fg : C_WHITE, black[i]);
    }
}
static bool app_today(calendar_date_t *date) {
    net_snapshot_t net;
    net_service_get_snapshot(&net);
    return calendar_today(time(NULL), net.timezone_offset_minutes, net.time_valid, date);
}
static void draw_calculator(void) {
    const theme_pal_t *p = pal();
    fb_round_card(40, 188, 944, 136, 9, 1, p->frame, p->card2);
    char line[33];
    snprintf(line, sizeof(line), "%.32s", s_calculator.input);
    draw_txt(60, 201, 24, p->text2, line[0] ? line : "0");
    if (strlen(s_calculator.input) > 32)
        draw_txt(60, 233, 24, p->text2, s_calculator.input + 32);
    const char *result = s_calculator.result;
    if (s_calculator.error == CALC_SYNTAX) result = tr("Invalid expression", "表达式错误");
    if (s_calculator.error == CALC_DIV_ZERO) result = tr("Cannot divide by zero", "不能除以零");
    if (s_calculator.error == CALC_RANGE) result = tr("Result or nesting out of range", "结果或括号超出范围");
    if (s_calculator.error == CALC_TOO_LONG) result = tr("Input limit: 63 characters", "最多输入 63 个字符");
    draw_txt_fit(60, 273, 30, 904, s_calculator.error ? p->warn : p->text, result);
    static const char *labels[] = {"7","8","9","/","C", "4","5","6","*","DEL",
                                    "1","2","3","-","(", "0",".","=","+",")"};
    for (int i = 0; i < 20; i++) {
        int id = i == 4 ? ACT_CALC_CLEAR : i == 9 ? ACT_CALC_DELETE :
                 i == 17 ? ACT_CALC_EQUALS : ACT_CALC_CHAR + labels[i][0];
        button(id, 40 + (i % 5) * 191, 343 + (i / 5) * 80, 180, 68, labels[i], i == 17, true);
    }
    draw_txt(44, 690, 20, p->text2,
             tr("Type numbers / Shift / Sym · Enter = · Backspace · C clear", "数字 / Shift / Sym 输入 · 回车计算 · 退格 · C 清空"));
}
static void draw_calendar(void) {
    const theme_pal_t *p = pal();
    calendar_date_t today;
    bool valid = app_today(&today);
    char b[96];
    snprintf(b, sizeof(b), "%04d / %02d", s_calendar.year, s_calendar.month);
    draw_txt_centered(191, 38, p->text, b);
    button(ACT_CAL_YEAR_PREV, 44, 189, 100, 56, "<<", false, s_calendar.year > 1);
    button(ACT_CAL_PREV, 154, 189, 100, 56, "<", false, s_calendar.year > 1 || s_calendar.month > 1);
    button(ACT_CAL_NEXT, 770, 189, 100, 56, ">", false, s_calendar.year < 9999 || s_calendar.month < 12);
    button(ACT_CAL_YEAR_NEXT, 880, 189, 100, 56, ">>", false, s_calendar.year < 9999);
    static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    for (int i = 0; i < 7; i++)
        draw_txt(70 + i * 135, 266, 22, p->text2, days[i]);
    int first = calendar_weekday(s_calendar.year, s_calendar.month, 1);
    int count = calendar_days(s_calendar.year, s_calendar.month);
    for (int i = 0; i < 42; i++) {
        int day = i - first + 1, x = 44 + (i % 7) * 135, y = 307 + (i / 7) * 53;
        bool marked = valid && today.year == s_calendar.year && today.month == s_calendar.month && day == today.day;
        if (day < 1 || day > count) continue;
        fb_round_card(x, y, 126, 45, 7, 1, marked ? p->accent2 : p->frame, marked ? p->accent : p->card2);
        snprintf(b, sizeof(b), "%d", day);
        draw_txt(x + (126 - txt_w(24, b)) / 2, y + 8, 24, marked ? p->tab_sel_fg : p->text, b);
    }
    button(ACT_CAL_TODAY, 44, 646, 180, 60, tr("Today (T)", "今天 (T)"), false, valid);
    if (valid) {
        snprintf(b, sizeof(b), "%s %04d-%02d-%02d", tr("Today", "今天"), today.year, today.month, today.day);
        draw_txt(248, 644, 23, p->text2, b);
    } else draw_txt(248, 644, 23, p->warn, tr("Time has not been synchronized", "时间尚未同步"));
    draw_txt(248, 686, 20, p->text2, tr("Left/Right: month · Up/Down: year", "左右翻月 · 上下翻年"));
}
static void draw_game(void) {
    const theme_pal_t *p = pal();
    char b[64];
    for (int i = 0; i < 16; i++) {
        int x = 48 + (i % 4) * 119, y = 197 + (i / 4) * 119;
        uint8_t power = s_game.cells[i];
        uint16_t fill = power ? (power >= 11 ? p->accent : p->card2) : p->bg;
        fb_round_card(x, y, 108, 108, 9, 2, power ? p->accent2 : p->frame, fill);
        if (power) {
            snprintf(b, sizeof(b), "%lu", (unsigned long)(UINT32_C(1) << power));
            int size = 38;
            while (size > 14 && txt_w(size, b) > 96) size -= 2;
            draw_txt(x + (108 - txt_w(size, b)) / 2, y + (108 - size) / 2, size,
                     power >= 11 ? p->tab_sel_fg : p->text, b);
        }
    }
    snprintf(b, sizeof(b), "%s  %lu", tr("Score", "得分"), (unsigned long)s_game.score);
    draw_txt(554, 198, 32, p->text, b);
    const char *status = s_game.over ? tr("Game over", "游戏结束") :
        s_game.won && !s_game.continued ? tr("2048! You win", "2048！你赢了") :
        s_game.won ? tr("Keep going", "继续挑战") : tr("Join tiles to reach 2048", "合并数字，达到 2048");
    draw_txt(554, 259, 26, s_game.over ? p->warn : p->accent2, status);
    if (s_game_restart) {
        draw_txt(554, 345, 26, p->text, tr("Start a new game?", "开始新游戏？"));
        button(ACT_GAME_CONFIRM, 554, 416, 198, 68, tr("Restart", "重新开始"), true, true);
        button(ACT_GAME_CANCEL, 768, 416, 196, 68, tr("Cancel", "取消"), false, true);
    } else {
        bool enabled = !s_game.over && (!s_game.won || s_game.continued);
        button(ACT_GAME_MOVE + GAME_UP, 697, 330, 120, 72, tr("Up", "上"), false, enabled);
        button(ACT_GAME_MOVE + GAME_LEFT, 563, 416, 120, 72, tr("Left", "左"), false, enabled);
        button(ACT_GAME_MOVE + GAME_DOWN, 697, 416, 120, 72, tr("Down", "下"), false, enabled);
        button(ACT_GAME_MOVE + GAME_RIGHT, 831, 416, 120, 72, tr("Right", "右"), false, enabled);
        if (s_game.won && !s_game.continued && !s_game.over)
            button(ACT_GAME_CONTINUE, 563, 516, 388, 64, tr("Continue", "继续"), true, true);
        button(ACT_GAME_RESTART, 563, 600, 388, 64, tr("New game (R)", "新游戏 (R)"), false, true);
    }
    draw_txt(48, 699, 21, p->text2, tr("Arrow keys / WASD or touch arrows · Fn+Tab: back", "方向键 / WASD 或触摸方向按钮 · Fn+Tab 返回"));
}
static void draw_apps(void) {
    const theme_pal_t *p = pal();
    char b[100];
    if (!s_app) {
        section(111, tr("Apps", "应用"));
        const char *labels[] = {tr("MIDI instrument", "MIDI 小乐器"), tr("Clock", "时钟"),
                                tr("Calculator", "计算器"), tr("Calendar", "日历"), "2048"};
        const int ids[] = {ACT_INSTRUMENT, ACT_CLOCK, ACT_CALCULATOR, ACT_CALENDAR, ACT_GAME};
        const icon_t icons[] = {ICON_KEYBOARD, ICON_CLOCK, ICON_APPS, ICON_CHART, ICON_LAYERS};
        for (int i = 0; i < 5; i++) {
            int x = 20 + (i % 3) * 332, y = 157 + (i / 3) * 233;
            button(ids[i], x, y, 320, 216, "", false, true);
            fb_fill_round_rect(x + 124, y + 25, 72, 72, 16, p->card2);
            draw_icon(icons[i], x + 140, y + 41, 40, p->accent2);
            draw_txt(x + (320 - txt_w(28, labels[i])) / 2, y + 124, 28, p->text, labels[i]);
            const char *sub = i == 1 ? tr("Network time (NTP)", "网络时间（NTP）") :
                i == 3 ? tr("Month view", "月历") : tr("Runs locally", "本地运行");
            draw_txt(x + (320 - txt_w(20, sub)) / 2, y + 171, 20, p->text2, sub);
        }
        draw_txt(44, 643, 24, p->text, tr("Runs locally on ESP32-S3", "在 ESP32-S3 上独立运行"));
        draw_txt(44, 690, 21, p->text2,
                 tr("Tab: select · Enter: open · Fn+Tab: back", "Tab 选择 · 回车打开 · Fn+Tab 返回"));
        return;
    }
    fb_round_card(20, 98, 984, 651, 9, 1, p->frame, p->card);
    icon_button(ACT_BACK, 40, 116, 134, 52, ICON_BACK, tr("Back", "返回"), false, true);
    const char *title = s_app == 2 ? tr("Clock", "时钟") : s_app == 3 ? tr("Calculator", "计算器") :
                        s_app == 4 ? tr("Calendar", "日历") : s_app == 5 ? "2048" : tr("MIDI instrument", "MIDI 小乐器");
    draw_txt(196, 125, 32, p->text, title);
    if (s_app == 3) { draw_calculator(); return; }
    if (s_app == 4) { draw_calendar(); return; }
    if (s_app == 5) { draw_game(); return; }
    if (s_app == 2) {
        net_snapshot_t net;
        net_service_get_snapshot(&net);
        time_t t = time(NULL) + (time_t)net.timezone_offset_minutes * 60;
        struct tm tm;
        gmtime_r(&t, &tm);
        draw_icon(ICON_CLOCK, 486, 207, 52, p->accent2);
        if (net.time_valid) {
            strftime(b, sizeof(b), "%H:%M:%S", &tm);
            draw_txt_centered(292, 100, p->text, b);
            strftime(b, sizeof(b), "%Y-%m-%d", &tm);
            draw_txt_centered(426, 32, p->text2, b);
        } else {
            draw_txt_centered(292, 100, p->text, "--:--:--");
            draw_txt_centered(426, 26, p->dim,
                              tr("Time has not been synchronized", "时间尚未同步"));
        }
        snprintf(b, sizeof(b), "UTC%c%02d:%02d", net.timezone_offset_minutes < 0 ? '-' : '+',
                 abs(net.timezone_offset_minutes) / 60, abs(net.timezone_offset_minutes) % 60);
        draw_txt_centered(492, 23, p->accent2, b);
        button(ACT_SYNC, 350, 568, 324, 68,
               net.ntp_busy ? tr("Synchronizing…", "正在对时…") : tr("Sync now", "立即对时"), false,
               net.connected && !net.ntp_busy);
        return;
    }
    instrument_snapshot_t st;
    instrument_get_snapshot(&st);
    if (st.ready && !st.active)
        button(ACT_INSTRUMENT, 766, 116, 218, 52, tr("Enable audio", "启用音源"), false, true);
    else {
        const char *status =
            st.active ? tr("Local audio", "本地音源") : tr("Audio unavailable", "音频未就绪");
        draw_txt(976 - txt_w(22, status), 132, 22, st.active ? p->good : p->warn, status);
    }
    const char *names[] = {tr("Sine", "正弦"), tr("Triangle", "三角"), tr("Square", "方波"),
                           tr("Pluck", "拨弦")};
    for (int i = 0; i < 4; i++)
        icon_button(ACT_TIMBRE + i, 40 + i * 239, 184, 227, 80, ICON_SINE + i, names[i],
                    st.timbre == i, st.ready);
    fb_round_card(40, 282, 466, 78, 9, 1, p->frame, p->card);
    fb_round_card(522, 282, 462, 78, 9, 1, p->frame, p->card);
    draw_icon(ICON_LAYERS, 62, 302, 36, p->accent2);
    snprintf(b, sizeof(b), "%s  %+d", tr("Octave", "八度"), st.octave);
    draw_txt_fit(119, 310, 27, 170, p->text, b);
    button(ACT_OCT_DOWN, 312, 293, 80, 56, "-", false, st.octave > -2);
    button(ACT_OCT_UP, 404, 293, 80, 56, "+", false, st.octave < 2);
    draw_icon(ICON_VOLUME, 545, 302, 36, p->accent2);
    snprintf(b, sizeof(b), "%s  %d", tr("Volume", "音量"), st.volume);
    draw_txt_fit(600, 310, 27, 178, p->text, b);
    button(ACT_VOL_DOWN, 790, 293, 80, 56, "-", false, st.volume > 0);
    button(ACT_VOL_UP, 882, 293, 80, 56, "+", false, st.volume < 100);
    draw_piano(&st);
    fb_round_card(36, 625, 952, 106, 9, 1, p->frame, p->card);
    draw_icon(ICON_KEYBOARD, 58, 655, 56, p->accent2);
    draw_txt_fit(139, 646, 20, 590, p->text2,
                 tr("A S D F G H J K L = notes · Q W E R T Y U = sharps",
                    "A S D F G H J K L 弹奏 · Q W E R T Y U 升半音"));
    draw_txt_fit(139, 682, 19, 590, p->text2,
                 tr("Arrow left/right: octave · up/down: timbre · Enter: stop",
                    "左右键切八度 · 上下键切音色 · 回车停止发声"));
    icon_button(ACT_PANIC, 744, 641, 228, 74, ICON_STOP, tr("Stop all notes", "停止发声"), false,
                true);
}
/* Inline text editor. Password is masked and never exposed through logging. */
static int s_editor; /* 1 SSID, 2 password, 3 NTP server */
static char s_edit[65], s_ssid[33];
static void erase_secret(char *p, size_t n) {
    volatile char *v = p;
    while (n--)
        *v++ = 0;
}
static void editor_open(int kind, const char *initial) {
    s_editor = kind;
    erase_secret(s_edit, sizeof(s_edit));
    if (initial)
        strlcpy(s_edit, initial, sizeof(s_edit));
    s_focus = -1;
}
static void editor_add(char c) {
    size_t n = strlen(s_edit), max = s_editor == 1 ? 32 : s_editor == 2 ? 64 : 63;
    if (n < max) {
        s_edit[n] = c;
        s_edit[n + 1] = 0;
    }
}
static void draw_editor(void) {
    const theme_pal_t *p = pal();
    section(106, s_editor == 1   ? tr("Network name", "网络名称")
                 : s_editor == 2 ? tr("Wi-Fi password", "Wi-Fi 密码")
                                 : tr("NTP server", "NTP 服务器"));
    char value[70];
    if (s_editor == 2) {
        memset(value, '*', strlen(s_edit));
        value[strlen(s_edit)] = 0;
    } else
        strlcpy(value, s_edit, sizeof(value));
    fb_round_card(24, 152, 976, 76, 12, 2, p->accent, p->card);
    draw_txt_fit(42, 176, 26, 940, p->text, value);
    draw_icon(ICON_KEYBOARD, 460, 303, 96, p->accent2);
    draw_txt_centered(447, 26, p->text, tr("Type on the physical keyboard", "使用实体键盘输入"));
    draw_txt_centered(497, 20, p->text2, "Enter / Confirm     Fn+Tab / Cancel");
    draw_txt_centered(534, 18, p->text2, "Shift / ABC     Sym / !@#");
    button(ACT_EDIT_CANCEL, 24, 649, 260, 66, tr("Cancel", "取消"), false, true);
    button(ACT_EDIT_OK, 696, 649, 304, 66, tr("Confirm", "确认"), true, true);
}
#define COLOR_TRACK_X 266
#define COLOR_TRACK_W 488
#define COLOR_ROW_Y 295
#define COLOR_ROW_STEP 99
static void draw_color_editor(void) {
    const theme_pal_t *p = pal();
    fb_round_card(20, 174, 984, 571, 9, 1, p->frame, p->card);
    draw_icon(ICON_PALETTE, 43, 195, 36, p->accent2);
    draw_txt(100, 200, 26, p->text, tr("Custom color", "自定义主题色"));
    draw_txt(100, 242, 19, p->text2, tr("Adjust RGB channels, then apply", "调整 RGB 数值后应用"));
    uint16_t color = RGB(s_color_draft[0], s_color_draft[1], s_color_draft[2]);
    fb_round_card(810, 195, 170, 77, 10, 1, p->frame, color);
    char b[32];
    snprintf(b, sizeof(b), "#%02X%02X%02X", s_color_draft[0], s_color_draft[1], s_color_draft[2]);
    draw_txt(895 - txt_w(23, b) / 2, 220, 23, color_ink(color), b);
    const char *names[] = {"R", "G", "B"};
    for (int i = 0; i < 3; i++) {
        int y = COLOR_ROW_Y + i * COLOR_ROW_STEP;
        fb_round_card(40, y, 944, 82, 9, 1, p->frame, p->bg);
        draw_txt(78, y + 25, 28, p->text, names[i]);
        button(ACT_RGB_DOWN + i, 168, y + 13, 76, 56, "-", false, s_color_draft[i] > 0);
        button(ACT_RGB_UP + i, 784, y + 13, 76, 56, "+", false, s_color_draft[i] < 255);
        for (int dx = 0; dx < COLOR_TRACK_W; dx++) {
            uint8_t rgb[3] = {s_color_draft[0], s_color_draft[1], s_color_draft[2]};
            rgb[i] = dx * 255 / (COLOR_TRACK_W - 1);
            fb_fill_rect(COLOR_TRACK_X + dx, y + 33, 1, 16, RGB(rgb[0], rgb[1], rgb[2]));
        }
        int cx = COLOR_TRACK_X + s_color_draft[i] * (COLOR_TRACK_W - 1) / 255;
        fb_round_card(cx - 7, y + 26, 14, 30, 7, 2, p->text, p->card);
        snprintf(b, sizeof(b), "%03d", s_color_draft[i]);
        draw_txt(887, y + 26, 25, p->text, b);
    }
    button(ACT_CUSTOM_CANCEL, 40, 655, 300, 62, tr("Cancel", "取消"), false, true);
    button(ACT_CUSTOM_APPLY, 684, 655, 300, 62, tr("Apply color", "应用主题色"), true, true);
}
static void appearance_heading(int y, icon_t icon, const char *title, const char *subtitle) {
    draw_icon(icon, 43, y, 36, pal()->accent2);
    draw_txt(101, y + 1, 25, pal()->text, title);
    draw_txt(101, y + 35, 18, pal()->text2, subtitle);
}
static void draw_settings(void) {
    const theme_pal_t *p = pal();
    net_snapshot_t n;
    net_service_get_snapshot(&n);
    char b[160];
    if (s_editor) {
        draw_editor();
        return;
    }
    const char *sub[] = {tr("Appearance", "外观"), "Wi-Fi", tr("Time", "时间")};
    const icon_t sub_icons[] = {ICON_DISPLAY, ICON_WIFI, ICON_CLOCK};
    for (int i = 0; i < 3; i++)
        icon_button(ACT_SETTINGS + i, 20 + i * 332, 100, 320, 56, sub_icons[i], sub[i],
                    s_settings == i, true);
    if (s_color_editor) {
        draw_color_editor();
        return;
    }
    if (s_settings == 0) {
        fb_round_card(20, 174, 984, 176, 9, 1, p->frame, p->card);
        appearance_heading(195, ICON_LANGUAGE, tr("Language", "语言"),
                           tr("Choose a display language", "选择系统显示语言"));
        for (int i = 0; i < LANG_COUNT; i++)
            choice_button(ACT_LANG + i, 40 + i * 240, 261, 224, 67, k_lang_name[i], s_lang == i);
        fb_round_card(20, 366, 984, 191, 9, 1, p->frame, p->card);
        appearance_heading(386, ICON_PALETTE, tr("Accent color", "主题色"),
                           tr("Choose an interface color", "选择界面主题色"));
        button(ACT_CUSTOM, 40, 454, 96, 82, "", s_accent == CUSTOM_ACCENT, true);
        draw_txt(88 - txt_w(30, "+") / 2, 461, 30,
                 s_accent == CUSTOM_ACCENT ? p->tab_sel_fg : p->text, "+");
        const char *custom = tr("Custom", "自定义");
        draw_txt(88 - txt_w(17, custom) / 2, 504, 17,
                 s_accent == CUSTOM_ACCENT ? p->tab_sel_fg : p->text, custom);
        for (int i = 0; i < 8; i++) {
            int x = 146 + i * 106;
            button(ACT_COLOR + i, x, 454, 96, 82, "", false, true);
            fb_fill_round_rect(x + 15, 471, 66, 49, 11, k_accents[i]);
            if (i == s_accent)
                draw_check(x + 40, 489, 14, color_ink(k_accents[i]));
        }
        fb_round_card(20, 573, 984, 172, 9, 1, p->frame, p->card);
        appearance_heading(593, ICON_BACKGROUND, tr("Background", "背景"),
                           tr("Choose light or dark mode", "选择界面背景模式"));
        choice_button(ACT_DARK, 40, 661, 463, 62, tr("Dark", "暗色"), !s_light);
        choice_button(ACT_LIGHT, 521, 661, 463, 62, tr("Light", "亮色"), s_light);
        return;
    }
    if (s_settings == 1) {
        button(ACT_WIFI, 24, 182, 210, 60,
               n.enabled ? tr("Wi-Fi on", "Wi-Fi 已开启") : tr("Wi-Fi off", "Wi-Fi 已关闭"),
               n.enabled, true);
        button(ACT_SCAN, 250, 182, 210, 60, tr("Scan", "扫描"), false, n.enabled);
        button(ACT_CANCEL, 476, 182, 210, 60, tr("Cancel", "取消"), false, n.enabled);
        button(ACT_FORGET, 702, 182, 298, 60, tr("Forget network", "忘记网络"), false,
               n.saved_network);
        snprintf(b, sizeof(b), "%s%s  %s",
                 n.connected ? tr("Connected: ", "已连接：") : tr("Network: ", "网络："), n.ssid,
                 n.connected ? n.ip : "");
        draw_txt_fit(28, 265, 24, 965, p->text, b);
        if (s_ap_page * 5 >= n.ap_count)
            s_ap_page = 0;
        for (int i = s_ap_page * 5; i < n.ap_count && i < s_ap_page * 5 + 5; i++) {
            snprintf(b, sizeof(b), "%s   %d dBm  %s", n.aps[i].ssid, n.aps[i].rssi,
                     n.aps[i].secured ? "*" : "");
            button(ACT_AP + i, 24, 312 + (i % 5) * 62, 976, 54, b, false, true);
        }
        button(ACT_MANUAL, 24, 647, 310, 60, tr("Enter network name", "输入网络名称"), false,
               n.enabled);
        const char *states[] = {tr("Off", "已关闭"),          tr("Starting", "启动中"),
                                tr("Ready", "就绪"),          tr("Scanning…", "扫描中…"),
                                tr("Connecting…", "连接中…"), tr("Connected", "已连接"),
                                tr("Failed", "失败")};
        const char *errors[] = {"",
                                tr("Initialization failed", "初始化失败"),
                                tr("Connection failed", "连接失败"),
                                tr("Timed out; retry", "超时，请重试"),
                                tr("Check password", "请检查密码"),
                                tr("Wi-Fi not connected", "Wi-Fi 未连接"),
                                tr("Could not save settings", "无法保存设置")};
        snprintf(b, sizeof(b), "%s %s", n.state <= NET_ERROR ? states[n.state] : "--",
                 n.last_error <= NET_ERROR_STORAGE ? errors[n.last_error] : "--");
        if ((n.last_error == NET_ERROR_INIT || n.last_error == NET_ERROR_WIFI) && n.init_error)
            snprintf(b, sizeof(b), "%s (0x%x)",
                     n.init_error == ESP_ERR_NO_MEM ? tr("Not enough memory", "内存不足")
                                                  : tr("Initialization failed", "初始化失败"),
                     (unsigned)n.init_error);
        draw_txt_fit(350, 666, 20, 430, p->dim, b);
        if (n.ap_count > 5)
            button(ACT_AP_NEXT, 816, 647, 184, 60, tr("More", "更多"), false, true);
        return;
    }
    section(190, tr("Network time (NTP)", "网络时间（NTP）"));
    button(ACT_SERVER, 24, 236, 976, 66, n.ntp_server, false, true);
    snprintf(b, sizeof(b), "UTC%c%02d:%02d", n.timezone_offset_minutes < 0 ? '-' : '+',
             abs(n.timezone_offset_minutes) / 60, abs(n.timezone_offset_minutes) % 60);
    section(338, tr("Time zone", "时区"));
    button(ACT_TZ_DOWN, 24, 384, 170, 62, "-", false, true);
    draw_txt(231, 402, 28, p->text, b);
    button(ACT_TZ_UP, 480, 384, 170, 62, "+", false, true);
    button(ACT_SYNC, 24, 510, 976, 74,
           n.ntp_busy ? tr("Synchronizing…", "正在对时…") : tr("Sync now", "立即对时"), false,
           n.connected && !n.ntp_busy);
    draw_txt(28, 620, 22, n.time_valid ? p->good : p->dim,
             n.time_valid
                 ? tr("Clock synchronized; continues while powered", "时钟已同步，上电期间持续计时")
                 : tr("Connect Wi-Fi to synchronize the clock", "连接 Wi-Fi 后可同步时间"));
    if (n.last_error != NET_ERROR_NONE) {
        const char *err =
            n.last_error == NET_ERROR_TIMEOUT
                ? tr("Synchronization timed out; retry", "对时超时，请重试")
            : n.last_error == NET_ERROR_STORAGE
                ? tr("Time settings could not be saved", "时间设置无法保存")
                : tr("Network unavailable; check Wi-Fi settings", "网络不可用，请检查 Wi-Fi 设置");
        draw_txt_fit(28, 665, 21, 970, p->warn, err);
    } else if (n.last_sync > 0) {
        time_t last = (time_t)n.last_sync + n.timezone_offset_minutes * 60;
        struct tm tm;
        gmtime_r(&last, &tm);
        strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &tm);
        draw_txt(28, 665, 20, p->dim, b);
    }
}
static void ui_note_on(int note) {
    if (note < 0 || note > 127 || !instrument_active())
        return;
    if (s_note_refs[note]++ == 0)
        instrument_note_on(note, 100);
    s_piano_dirty = true;
}
static void ui_note_off(int note) {
    if (note < 0 || note > 127 || !s_note_refs[note])
        return;
    if (--s_note_refs[note] == 0)
        instrument_note_off(note);
    s_piano_dirty = true;
}
static void clear_notes(void) {
    memset(s_note_refs, 0, sizeof(s_note_refs));
    instrument_panic();
    memset(s_held_notes, -1, sizeof(s_held_notes));
    s_notes_initialized = true;
    s_touch_note = -1;
    s_input_dirty = true;
}
void ui_cancel_input(void) {
    clear_notes();
    instrument_set_active(false);
    s_touch_down = false;
    s_shift = s_shift_left = s_shift_right = s_fn = s_sym = false;
    if (s_keys)
        xQueueReset(s_keys);
    s_editor = 0;
    s_color_editor = false;
    s_color_drag = -1;
    s_confirm_shutdown = false;
    erase_secret(s_edit, sizeof(s_edit));
}
static void change_tab(int t) {
    if (t < 0 || t >= UI_TAB_COUNT)
        return;
    ui_cancel_input();
    s_tab = t;
    s_topo_detail = 0;
    s_pi_page = 0;
    s_app = 0;
    s_focus = -1;
}
static void activate(int id) {
    net_snapshot_t n;
    net_service_get_snapshot(&n);
    instrument_snapshot_t st;
    instrument_get_snapshot(&st);
    bool queued = true;
    if (id >= ACT_TAB && id < ACT_TAB + 4) {
        change_tab(id - ACT_TAB);
        return;
    }
    if (id >= ACT_SETTINGS && id < ACT_SETTINGS + 3) {
        s_color_editor = false;
        s_settings = id - ACT_SETTINGS;
        s_focus = -1;
        return;
    }
    if (id >= ACT_LANG && id < ACT_LANG + LANG_COUNT) {
        s_lang = id - ACT_LANG;
        prefs_save();
        return;
    }
    if (id >= ACT_COLOR && id < ACT_COLOR + 8) {
        s_accent = id - ACT_COLOR;
        palette_update();
        prefs_save();
        return;
    }
    if ((id >= ACT_RGB_DOWN && id < ACT_RGB_DOWN + 3) ||
        (id >= ACT_RGB_UP && id < ACT_RGB_UP + 3)) {
        bool up = id >= ACT_RGB_UP;
        int channel = id - (up ? ACT_RGB_UP : ACT_RGB_DOWN);
        int value = s_color_draft[channel] + (up ? 1 : -1);
        s_color_draft[channel] = value < 0 ? 0 : value > 255 ? 255 : value;
        return;
    }
    if (id >= ACT_TIMBRE && id < ACT_TIMBRE + 4) {
        clear_notes();
        instrument_set_timbre(id - ACT_TIMBRE);
        return;
    }
    if (id >= ACT_AP && id < ACT_AP + 8) {
        int i = id - ACT_AP;
        if (i < n.ap_count) {
            strlcpy(s_ssid, n.aps[i].ssid, sizeof(s_ssid));
            if (n.aps[i].secured)
                editor_open(2, NULL);
            else {
                queued = net_service_connect(s_ssid, "");
                if (!queued)
                    notice(tr("Request not accepted; retry", "请求未受理，请重试"));
            }
        }
        return;
    }
    if (id >= 401 && id <= 404) {
        s_topo_detail = id - 400;
        s_history_metric = id == 401 ? 0 : id == 402 ? 2 : 3;
        s_focus = -1;
        return;
    }
    if (id >= 420 && id <= 423) { s_history_metric = id - 420; return; }
    if (id == 410) { s_topo_detail = 0; s_focus = -1; return; }
    if (id == 405) { change_tab(UI_TAB_PI); return; }
    if (id == 406) { change_tab(UI_TAB_SETUP); s_settings = 1; return; }
    if (id == 407) { change_tab(UI_TAB_APPS); return; }
    if (id >= 440 && id < 440 + PI_SHARE_FILES) {
        pi_share_snapshot_t share; pi_share_get_snapshot(&share);
        if (id - 440 < share.count) pi_share_request(SHARE_FILE,share.files[id-440].name);
        return;
    }
    if (id == 430) { pi_share_cancel(); s_pi_page=0; return; }
    if (id == 431) {
        pi_share_snapshot_t share; pi_share_get_snapshot(&share);
        if (share.busy) pi_share_cancel();
        else pi_share_request(s_pi_page==1 ? SHARE_LIST : SHARE_SCREEN,NULL);
        return;
    }
    if (id == 432) {
        pi_share_snapshot_t share; pi_share_get_snapshot(&share);
        s_file_page=share.count ? (s_file_page+1)%((share.count+5)/6) : 0; return;
    }
    if (id == 433 || id == 434) {
        s_pi_page=id==433 ? 1 : 2; s_file_page=0;
        pi_share_request(s_pi_page==1 ? SHARE_LIST : SHARE_SCREEN,NULL); return;
    }
    if (id == 435) { pi_share_forget(); return; }
    if (id >= ACT_CALC_CHAR && id < ACT_CALC_CHAR + 128) {
        calculator_input(&s_calculator, (char)(id - ACT_CALC_CHAR));
        return;
    }
    if (id >= ACT_GAME_MOVE && id <= ACT_GAME_MOVE + GAME_DOWN) {
        if (!s_game_restart) game2048_move(&s_game, (game_direction_t)(id - ACT_GAME_MOVE));
        return;
    }
    switch (id) {
    case ACT_CUSTOM:
        memcpy(s_color_draft, s_custom_rgb, sizeof(s_color_draft));
        s_color_editor = true;
        s_focus = -1;
        break;
    case ACT_CUSTOM_APPLY:
        memcpy(s_custom_rgb, s_color_draft, sizeof(s_custom_rgb));
        s_accent = CUSTOM_ACCENT;
        palette_update();
        prefs_save();
        s_color_editor = false;
        s_focus = -1;
        break;
    case ACT_CUSTOM_CANCEL:
        s_color_editor = false;
        s_focus = -1;
        break;
    case 202:
        s_confirm_shutdown = true;
        break;
    case 203:
        s_confirm_shutdown = false;
        break;
    case 204:
        s_confirm_shutdown = false;
        queued = pi_link_request_shutdown();
        break;
    case ACT_CALCULATOR:
        s_app = 3; s_focus = -1;
        break;
    case ACT_CALENDAR:
        s_app = 4; s_focus = -1;
        if (!s_calendar.year && !app_today(&s_calendar)) s_calendar = (calendar_date_t){2000, 1, 1};
        break;
    case ACT_GAME:
        s_app = 5; s_focus = -1; s_game_restart = false;
        if (!s_game_started) {
            game2048_start(&s_game, (uint32_t)esp_timer_get_time());
            s_game_started = true;
        }
        break;
    case ACT_CALC_CLEAR: calculator_clear(&s_calculator); break;
    case ACT_CALC_DELETE: calculator_backspace(&s_calculator); break;
    case ACT_CALC_EQUALS: calculator_equals(&s_calculator); break;
    case ACT_CAL_PREV: calendar_shift(&s_calendar, -1); break;
    case ACT_CAL_NEXT: calendar_shift(&s_calendar, 1); break;
    case ACT_CAL_YEAR_PREV: calendar_shift(&s_calendar, -12); break;
    case ACT_CAL_YEAR_NEXT: calendar_shift(&s_calendar, 12); break;
    case ACT_CAL_TODAY: app_today(&s_calendar); break;
    case ACT_GAME_RESTART: s_game_restart = true; s_focus = -1; break;
    case ACT_GAME_CONFIRM:
        game2048_start(&s_game, (uint32_t)esp_timer_get_time());
        s_game_restart = false; s_focus = -1;
        break;
    case ACT_GAME_CANCEL: s_game_restart = false; s_focus = -1; break;
    case ACT_GAME_CONTINUE: game2048_continue(&s_game); s_focus = -1; break;
    case ACT_INSTRUMENT:
        s_app = 1;
        clear_notes();
        instrument_set_active(true);
        s_focus = -1;
        break;
    case ACT_CLOCK:
        s_app = 2;
        s_focus = -1;
        break;
    case ACT_BACK:
        ui_cancel_input();
        s_app = 0;
        s_focus = -1;
        break;
    case ACT_OCT_DOWN:
        clear_notes();
        instrument_set_octave(st.octave - 1);
        break;
    case ACT_OCT_UP:
        clear_notes();
        instrument_set_octave(st.octave + 1);
        break;
    case ACT_VOL_DOWN:
        instrument_set_volume(st.volume >= 5 ? st.volume - 5 : 0);
        break;
    case ACT_VOL_UP:
        instrument_set_volume(st.volume <= 95 ? st.volume + 5 : 100);
        break;
    case ACT_PANIC:
        clear_notes();
        break;
    case ACT_DARK:
    case ACT_LIGHT:
        s_light = id == ACT_LIGHT;
        palette_update();
        prefs_save();
        break;
    case ACT_WIFI:
        queued = net_service_set_enabled(!n.enabled);
        break;
    case ACT_SCAN:
        s_ap_page = 0;
        queued = net_service_scan();
        break;
    case ACT_AP_NEXT:
        s_ap_page = 1 - s_ap_page;
        break;
    case ACT_CANCEL:
        queued = net_service_cancel();
        break;
    case ACT_FORGET:
        queued = net_service_forget();
        break;
    case ACT_MANUAL:
        editor_open(1, NULL);
        break;
    case ACT_SERVER:
        editor_open(3, n.ntp_server);
        break;
    case ACT_SYNC:
        queued = net_service_request_ntp();
        break;
    case ACT_TZ_DOWN:
    case ACT_TZ_UP: {
        int z = n.timezone_offset_minutes + (id == ACT_TZ_UP ? 30 : -30);
        if (z >= -720 && z <= 840)
            queued = net_service_set_time_config(n.ntp_server, z);
        break;
    }
    case ACT_EDIT_BACK: {
        size_t len = strlen(s_edit);
        if (len) {
            do {
                len--;
            } while (len > 0 && ((unsigned char)s_edit[len] & 0xC0) == 0x80);
            erase_secret(s_edit + len, sizeof(s_edit) - len);
        }
        break;
    }
    case ACT_EDIT_CANCEL:
        s_editor = 0;
        erase_secret(s_edit, sizeof(s_edit));
        break;
    case ACT_EDIT_OK:
        if (s_editor == 1) {
            if (!s_edit[0]) {
                notice(tr("Enter a network name", "请输入网络名称"));
                break;
            }
            strlcpy(s_ssid, s_edit, sizeof(s_ssid));
            editor_open(2, NULL);
            break;
        }
        if (s_editor == 2)
            queued = net_service_connect(s_ssid, s_edit);
        if (s_editor == 3)
            queued = net_service_set_time_config(s_edit, n.timezone_offset_minutes);
        if (queued) {
            s_editor = 0;
            s_confirm_shutdown = false;
            erase_secret(s_edit, sizeof(s_edit));
        } else
            notice(tr("Check input or retry when idle", "请检查输入，或稍后重试"));
        break;
    default:
        break;
    }
    if (!queued)
        notice(
            tr("Request not accepted; check input or retry", "请求未受理，请检查输入或稍后重试"));
}
void ui_page_draw(uint32_t uptime_s) {
    if (!s_notes_initialized)
        clear_notes();
    if (s_req_tab >= 0) {
        int t = s_req_tab;
        s_req_tab = -1;
        change_tab(t);
    }
    if (s_req_lang >= 0) {
        s_lang = s_req_lang;
        s_req_lang = -1;
        prefs_save();
    }
    s_last_uptime_s = uptime_s;
    s_full_draws++;
    s_input_dirty = s_piano_dirty = false;
    if (s_draw_mtx)
        xSemaphoreTake(s_draw_mtx, portMAX_DELAY);
    const theme_pal_t *p = pal();
    fb_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, p->bg);
    s_button_count = 0;
    const char *names[] = {tr("Firmware", "固件"), tr("Raspberry Pi", "树莓派"),
                           tr("Apps", "应用"), tr("Settings", "设置")};
    for (int i = 0; i < 4; i++)
        icon_button(ACT_TAB + i, 20 + i * 208, 18, TAB_W, 62, ICON_SENSOR + i, names[i], s_tab == i,
                    true);
    net_snapshot_t header_net;
    net_service_get_snapshot(&header_net);
    draw_icon(ICON_WIFI, 865, 33, 34, header_net.connected ? p->accent2 : p->dim);
    if (!header_net.connected)
        fb_draw_line(867, 37, 896, 62, 2, p->dim);
    char header_time[16] = "--:--";
    if (header_net.time_valid) {
        time_t now = time(NULL) + (time_t)header_net.timezone_offset_minutes * 60;
        struct tm tm;
        gmtime_r(&now, &tm);
        strftime(header_time, sizeof(header_time), "%H:%M", &tm);
    }
    draw_txt(1002 - txt_w(25, header_time), 35, 25, p->text, header_time);
    switch (s_tab) {
    case UI_TAB_SENSORS:
        draw_sensors();
        break;
    case UI_TAB_PI:
        draw_pi();
        break;
    case UI_TAB_APPS:
        draw_apps();
        break;
    case UI_TAB_SETUP:
        draw_settings();
        break;
    default:
        break;
    }
    if (s_prefs_failed) {
        fb_fill_rect(0, 730, 1024, 38, p->card2);
        draw_txt(24, 738, 19, p->warn,
                 tr("Settings could not be saved", "设置无法保存，重启后可能丢失"));
    }
    if (s_notice && esp_timer_get_time() < s_notice_until) {
        fb_fill_rect(0, 730, 1024, 38, p->card2);
        draw_txt_fit(24, 738, 19, 976, p->text, s_notice);
    }
    if (s_draw_mtx) {
        xSemaphoreGive(s_draw_mtx);
    }
    fb_flush();
}
void ui_handle_touch(int x, int y, bool pressed) {
    if (!s_mux_esp)
        return;
    if (!pressed) {
        s_touch_down = false;
        s_color_drag = -1;
        if (s_touch_note >= 0) {
            ui_note_off(s_touch_note);
            s_touch_note = -1;
            s_piano_dirty = true;
        }
        return;
    }
    if (x < 0 || x >= LCD_H_RES || y < 0 || y >= LCD_V_RES) {
        if (s_touch_note >= 0) {
            ui_note_off(s_touch_note);
            s_touch_note = -1;
            s_piano_dirty = true;
        }
        return;
    }
    s_touch_x = x;
    s_touch_y = y;
    bool first = !s_touch_down;
    s_touch_down = true;
    if (first)
        s_touch_events++;
    if (s_tab == UI_TAB_SETUP && s_color_editor) {
        if (first && x >= COLOR_TRACK_X && x < COLOR_TRACK_X + COLOR_TRACK_W)
            for (int i = 0; i < 3; i++)
                if (y >= COLOR_ROW_Y + i * COLOR_ROW_STEP + 13 &&
                    y < COLOR_ROW_Y + i * COLOR_ROW_STEP + 69)
                    s_color_drag = i;
        if (s_color_drag >= 0) {
            int value = (x - COLOR_TRACK_X) * 255 / (COLOR_TRACK_W - 1);
            s_color_draft[s_color_drag] = value < 0 ? 0 : value > 255 ? 255 : value;
            s_input_dirty = true;
            return;
        }
    }
    int key = piano_hit(x, y);
    if (s_tab == UI_TAB_APPS && s_app == 1 && key >= 0) {
        instrument_snapshot_t st;
        instrument_get_snapshot(&st);
        if (!st.active)
            return;
        int note = 60 + st.octave * 12 + key;
        if (note != s_touch_note) {
            if (s_touch_note >= 0)
                ui_note_off(s_touch_note);
            s_touch_note = note;
            ui_note_on(note);
            s_piano_dirty = true;
        }
        return;
    }
    if (s_touch_note >= 0) {
        ui_note_off(s_touch_note);
        s_touch_note = -1;
        s_piano_dirty = true;
    }
    if (!first)
        return;
    for (int i = 0; i < s_button_count; i++) {
        button_t *b = &s_buttons[i];
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) {
            if (b->enabled) {
                s_focus = i;
                activate(b->id);
                /* Page transitions clear input; retain this physical contact's debounce. */
                s_touch_down = true;
                ui_page_draw(s_last_uptime_s);
            }
            break;
        }
    }
}
void ui_input_lost(void) {
    s_key_overflow = true;
    instrument_panic();
}
void ui_key_event(int row, int col, bool pressed) {
    if (!s_mux_esp || row < 0 || row >= 6 || col < 0 || col >= 11)
        return;
    if (!s_keys) {
        return;
    }
    key_event_t e = {row, col, pressed};
    if (xQueueSend(s_keys, &e, 0) != pdTRUE)
        s_key_overflow = true;
}
static char typed_character(int r, int c) {
    char ch = 0;
    if (r == 1 && c < 10)
        ch = "1234567890"[c];
    if (r == 2 && c > 0)
        ch = "qwertyuiop"[c - 1];
    if (r == 3 && c > 0 && c < 10)
        ch = "asdfghjkl"[c - 1];
    if (r == 4 && c > 0 && c < 9)
        ch = "zxcvbnm;"[c - 1];
    if (r == 5 && c >= 3 && c <= 7)
        ch = ' ';
    if (ch) {
        if (s_sym) {
            if (r == 2 && c >= 1 && c <= 8)
                ch = "/?`~-_=+"[c - 1];
            if (r == 3 && c >= 2 && c <= 9)
                ch = ",.\\|[]{}"[c - 2];
            if (r == 4 && c >= 4 && c <= 8)
                ch = "<>'\":"[c - 4];
        } else if (s_shift) {
            if (ch >= 'a' && ch <= 'z')
                ch -= 'a' - 'A';
            else if (r == 1 && c < 10)
                ch = "!@#$%^&*()"[c];
            else if (ch == ';')
                ch = ':';
        }
    }
    return ch;
}
void ui_process_events(void) {
    if (!s_keys) {
        s_keys = xQueueCreate(64, sizeof(key_event_t));
        return;
    }
    if (s_key_overflow) {
        s_key_overflow = false;
        ui_cancel_input();
        notice(tr("Input overflow: notes stopped", "输入拥塞，已停止发声"));
    }
    key_event_t e;
    bool dirty = s_input_dirty;
    while (xQueueReceive(s_keys, &e, 0) == pdTRUE) {
        int r = e.row, c = e.col;
        if (r == 4 && (c == 0 || c == 10)) {
            if (c == 0)
                s_shift_left = e.pressed;
            else
                s_shift_right = e.pressed;
            s_shift = s_shift_left || s_shift_right;
            continue;
        }
        if (r == 5 && c == 0) {
            s_sym = e.pressed;
            continue;
        }
        if (r == 3 && c == 0) {
            s_fn = e.pressed;
            continue;
        }
        if (!e.pressed) {
            if (s_held_notes[r][c] >= 0) {
                ui_note_off(s_held_notes[r][c]);
                s_held_notes[r][c] = -1;
                s_piano_dirty = true;
            }
            continue;
        }
        if (s_editor) {
            char ch = typed_character(r, c);
            if (ch) {
                editor_add(ch);
            } else if (r == 1 && c == 10)
                activate(ACT_EDIT_BACK);
            else if (r == 3 && c == 10)
                activate(ACT_EDIT_OK);
            else if (r == 2 && c == 0 && s_fn)
                activate(ACT_EDIT_CANCEL);
            dirty = true;
            continue;
        }
        if (r == 2 && c == 0 && s_fn) {
            if (s_color_editor)
                activate(ACT_CUSTOM_CANCEL);
            else if (s_pi_page)
                activate(430);
            else if (s_topo_detail)
                activate(410);
            else if (s_app)
                activate(ACT_BACK);
            else
                change_tab(UI_TAB_APPS);
            dirty = true;
            continue;
        }
        if (s_tab == UI_TAB_APPS && s_app >= 3) {
            int action = -1;
            if (s_app == 3) {
                char ch = typed_character(r, c);
                if (ch && strchr("0123456789.+-*/()", ch)) action = ACT_CALC_CHAR + ch;
                else if (r == 1 && c == 10) action = ACT_CALC_DELETE;
                else if (ch == 'c' || ch == 'C') action = ACT_CALC_CLEAR;
                else if ((r == 3 && c == 10 && s_focus < 0) || ch == '=') action = ACT_CALC_EQUALS;
            } else if (s_app == 4) {
                if (r == 5 && c == 8) action = ACT_CAL_PREV;
                if (r == 5 && c == 10) action = ACT_CAL_NEXT;
                if (r == 4 && c == 9) action = ACT_CAL_YEAR_PREV;
                if (r == 5 && c == 9) action = ACT_CAL_YEAR_NEXT;
                if (r == 2 && c == 5) action = ACT_CAL_TODAY;
            } else {
                if ((r == 5 && c == 8) || (r == 3 && c == 1)) action = ACT_GAME_MOVE + GAME_LEFT;
                if ((r == 5 && c == 10) || (r == 3 && c == 3)) action = ACT_GAME_MOVE + GAME_RIGHT;
                if ((r == 4 && c == 9) || (r == 2 && c == 2)) action = ACT_GAME_MOVE + GAME_UP;
                if ((r == 5 && c == 9) || (r == 3 && c == 2)) action = ACT_GAME_MOVE + GAME_DOWN;
                if (r == 2 && c == 4) action = ACT_GAME_RESTART;
                if (r == 3 && c == 10 && s_focus < 0 && s_game.won && !s_game_restart) action = ACT_GAME_CONTINUE;
            }
            if (action >= 0) {
                activate(action); s_focus = -1; dirty = true; continue;
            }
        }
        if (s_tab == UI_TAB_APPS && s_app == 1) {
            instrument_snapshot_t st;
            instrument_get_snapshot(&st);
            int off = -1;
            if (r == 3 && c >= 1 && c <= 9) {
                static const int offsets[] = {0, 2, 4, 5, 7, 9, 11, 12, 14};
                off = offsets[c - 1];
            }
            if (r == 2 && c >= 1 && c <= 7) {
                static const int offsets[] = {1, 3, 6, 8, 10, 13, 15};
                off = offsets[c - 1];
            }
            if (off >= 0) {
                if (!st.active)
                    continue;
                int note = 60 + st.octave * 12 + off;
                if (s_held_notes[r][c] < 0) {
                    s_held_notes[r][c] = note;
                    ui_note_on(note);
                    s_piano_dirty = true;
                }
                continue;
            }
            if (r == 5 && c == 8)
                activate(ACT_OCT_DOWN);
            else if (r == 5 && c == 10)
                activate(ACT_OCT_UP);
            else if (r == 4 && c == 9)
                activate(ACT_TIMBRE + (st.timbre + 1) % 4);
            else if (r == 5 && c == 9)
                activate(ACT_TIMBRE + (st.timbre + 3) % 4);
            else if (r == 3 && c == 10)
                activate(ACT_PANIC);
            else if (r == 0 && c == 2)
                activate(ACT_VOL_UP);
            else if (r == 0 && c == 3)
                activate(ACT_VOL_DOWN);
            else if (r != 2 || c != 0)
                continue;
        }
        if ((r == 2 && c == 0) || (r == 5 && c == 9) || (r == 5 && c == 10) || (r == 4 && c == 9) ||
            (r == 5 && c == 8)) {
            int dir = (r == 4 || c == 8 || s_shift) ? -1 : 1;
            for (int i = 0; i < s_button_count; i++) {
                s_focus = (s_focus + dir + s_button_count) % s_button_count;
                if (s_buttons[s_focus].enabled)
                    break;
            }
        } else if (r == 3 && c == 10 && s_focus >= 0 && s_focus < s_button_count)
            activate(s_buttons[s_focus].id);
        dirty = true;
    }
    if (dirty)
        ui_page_draw(s_last_uptime_s);
    else if (s_piano_dirty && s_tab == UI_TAB_APPS && s_app == 1) {
        instrument_snapshot_t st; instrument_get_snapshot(&st);
        if (s_draw_mtx) xSemaphoreTake(s_draw_mtx, portMAX_DELAY);
        draw_piano(&st);
        s_piano_dirty = false;
        s_piano_draws++;
        if (s_draw_mtx) xSemaphoreGive(s_draw_mtx);
        fb_flush();
    }
}
void ui_periodic_draw(uint32_t uptime_s) {
    /* During performance, only the minute boundary needs a full header refresh.
       Note changes render the small piano region via ui_process_events. */
    if (s_tab == UI_TAB_APPS && s_app == 1 && !s_input_dirty && s_req_tab < 0 && s_req_lang < 0
        && uptime_s / 60 == s_last_uptime_s / 60) {
        s_last_uptime_s = uptime_s;
        return;
    }
    ui_page_draw(uptime_s);
}
void ui_maybe_flush(void) {
    ui_process_events();
}
void ui_dash_anim_tick(void) {
}
void ui_draw_hp_page(int raw, float rms_l, float rms_r) {
    (void)rms_l;
    (void)rms_r;
    notice(raw ? tr("Speakers", "扬声器") : tr("Headphones", "耳机"));
    if (s_mux_esp)
        ui_page_draw(s_last_uptime_s);
}
