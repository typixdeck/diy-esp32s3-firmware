#include "ttf_font.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"

#include <ft2build.h>
#include FT_FREETYPE_H

static const char *TAG = "TTF";

static FT_Library s_lib;
static FT_Face    s_face;
static bool       s_ready;
static int        s_cur_size = -1;   // face 当前 FT_Set_Pixel_Sizes 的字号

// ---------------------------------------------------------------------------
// 字形缓存：开放寻址哈希表，key = codepoint<<8 | size（size ≤ 255）。
// 不做淘汰——本 UI 全部页面的 (字符,字号) 组合 < 千级；万一写满就整表清空重来。
// ---------------------------------------------------------------------------
typedef struct {
    uint32_t key;        // 0 = 空槽
    int16_t  w, h;       // 位图尺寸
    int16_t  left, top;  // FreeType bitmap_left / bitmap_top
    int16_t  adv;        // 水平前进量（像素）
    uint8_t *bmp;        // 8bpp alpha，PSRAM
} glyph_t;

#define CACHE_CAP 2048
static glyph_t *s_cache;      // PSRAM
static int s_cache_n;

static void cache_flush(void)
{
    for (int i = 0; i < CACHE_CAP; i++) {
        if (s_cache[i].bmp) free(s_cache[i].bmp);
    }
    memset(s_cache, 0, CACHE_CAP * sizeof(glyph_t));
    s_cache_n = 0;
}

static glyph_t *cache_get(uint32_t cp, int size)
{
    uint32_t key = (cp << 8) | (uint32_t)(size & 0xFF);
    uint32_t idx = (key * 2654435761u) & (CACHE_CAP - 1);
    for (int probe = 0; probe < CACHE_CAP; probe++, idx = (idx + 1) & (CACHE_CAP - 1)) {
        if (s_cache[idx].key == key) return &s_cache[idx];
        if (s_cache[idx].key == 0) {
            // 未命中：渲染进这个空槽
            if (s_cache_n > CACHE_CAP - 64) {
                ESP_LOGW(TAG, "字形缓存写满（%d），整表清空", s_cache_n);
                cache_flush();
                idx = (key * 2654435761u) & (CACHE_CAP - 1);
            }
            if (size != s_cur_size) {
                if (FT_Set_Pixel_Sizes(s_face, 0, size) != 0) return NULL;
                s_cur_size = size;
            }
            if (FT_Load_Char(s_face, cp, FT_LOAD_RENDER) != 0) return NULL;
            FT_GlyphSlot g = s_face->glyph;
            glyph_t *e = &s_cache[idx];
            e->key  = key;
            e->w    = (int16_t)g->bitmap.width;
            e->h    = (int16_t)g->bitmap.rows;
            e->left = (int16_t)g->bitmap_left;
            e->top  = (int16_t)g->bitmap_top;
            e->adv  = (int16_t)(g->advance.x >> 6);
            e->bmp  = NULL;
            if (e->w > 0 && e->h > 0) {
                e->bmp = heap_caps_malloc((size_t)e->w * e->h,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!e->bmp) { e->key = 0; return NULL; }
                for (int row = 0; row < e->h; row++) {
                    memcpy(e->bmp + row * e->w,
                           g->bitmap.buffer + row * g->bitmap.pitch, e->w);
                }
            }
            s_cache_n++;
            return e;
        }
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// UTF-8 → codepoint（非法序列按 '?' 处理并前进 1 字节）
// ---------------------------------------------------------------------------
static uint32_t utf8_next(const char **p)
{
    const uint8_t *s = (const uint8_t *)*p;
    uint32_t cp;
    int n;
    if (s[0] < 0x80)       { cp = s[0];         n = 1; }
    else if (s[0] < 0xE0)  { cp = s[0] & 0x1F;  n = 2; }
    else if (s[0] < 0xF0)  { cp = s[0] & 0x0F;  n = 3; }
    else                   { cp = s[0] & 0x07;  n = 4; }
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) { *p += 1; return '?'; }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    *p += n;
    return cp;
}

// ---------------------------------------------------------------------------
// 初始化：mmap font 分区 → FT_New_Memory_Face
// ---------------------------------------------------------------------------
esp_err_t ttf_font_init(void)
{
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "font");
    if (!part) {
        ESP_LOGE(TAG, "找不到 font 分区");
        return ESP_ERR_NOT_FOUND;
    }
    const void *ptr = NULL;
    esp_partition_mmap_handle_t mm;
    esp_err_t err = esp_partition_mmap(part, 0, part->size,
                                       ESP_PARTITION_MMAP_DATA, &ptr, &mm);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "font 分区 mmap 失败: %s", esp_err_to_name(err));
        return err;
    }
    // 分区未刷字体（全 0xFF）快速识别
    if (((const uint8_t *)ptr)[0] == 0xFF) {
        ESP_LOGE(TAG, "font 分区内容为空（0xFF），请刷入 TTF：esptool write_flash 0x%lx font.ttf",
                 (unsigned long)part->address);
        return ESP_ERR_INVALID_STATE;
    }
    if (FT_Init_FreeType(&s_lib) != 0) {
        ESP_LOGE(TAG, "FT_Init_FreeType 失败");
        return ESP_FAIL;
    }
    // FreeType 按 sfnt 表内偏移访问，分区尾部 0xFF 填充无害
    if (FT_New_Memory_Face(s_lib, ptr, (FT_Long)part->size, 0, &s_face) != 0) {
        ESP_LOGE(TAG, "FT_New_Memory_Face 失败（TTF 损坏？）");
        return ESP_FAIL;
    }
    s_cache = heap_caps_calloc(CACHE_CAP, sizeof(glyph_t),
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_cache) return ESP_ERR_NO_MEM;
    s_ready = true;
    ESP_LOGI(TAG, "TTF 就绪：%s %s，%ld 字形，分区 %lu KB @0x%lx",
             s_face->family_name ? s_face->family_name : "?",
             s_face->style_name ? s_face->style_name : "",
             (long)s_face->num_glyphs,
             (unsigned long)(part->size / 1024), (unsigned long)part->address);
    return ESP_OK;
}

bool ttf_font_ready(void)
{
    return s_ready;
}

// RGB565 alpha 混合：dst = dst + (fg-dst)*a/255（逐通道）
static inline uint16_t blend565(uint16_t dst, uint16_t fg, uint8_t a)
{
    if (a >= 250) return fg;
    uint32_t dr = (dst >> 11) & 0x1F, dg = (dst >> 5) & 0x3F, db = dst & 0x1F;
    uint32_t fr = (fg >> 11) & 0x1F,  fgc = (fg >> 5) & 0x3F, fb = fg & 0x1F;
    uint32_t r = dr + ((fr - dr) * a + 127) / 255;
    uint32_t g = dg + ((fgc - dg) * a + 127) / 255;
    uint32_t b = db + ((fb - db) * a + 127) / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

int ttf_draw_text(uint16_t *fb, int fb_w, int fb_h,
                  int x, int y, int size, uint16_t color, const char *utf8)
{
    if (!s_ready || !fb || !utf8) return 0;
    // y 是行顶：基线 = y + ascender（该字号下）
    if (size != s_cur_size) {
        if (FT_Set_Pixel_Sizes(s_face, 0, size) != 0) return 0;
        s_cur_size = size;
    }
    int base_y = y + (int)(s_face->size->metrics.ascender >> 6);
    int pen_x = x;
    for (const char *p = utf8; *p; ) {
        uint32_t cp = utf8_next(&p);
        glyph_t *e = cache_get(cp, size);
        if (!e) { pen_x += size / 2; continue; }
        int gx0 = pen_x + e->left;
        int gy0 = base_y - e->top;
        for (int row = 0; row < e->h; row++) {
            int fy = gy0 + row;
            if (fy < 0 || fy >= fb_h) continue;
            const uint8_t *src = e->bmp + row * e->w;
            uint16_t *dst = fb + fy * fb_w;
            for (int col = 0; col < e->w; col++) {
                int fx = gx0 + col;
                if (fx < 0 || fx >= fb_w) continue;
                uint8_t a = src[col];
                if (a) dst[fx] = blend565(dst[fx], color, a);
            }
        }
        pen_x += e->adv;
    }
    return pen_x - x;
}

int ttf_text_width(int size, const char *utf8)
{
    if (!s_ready || !utf8) return 0;
    int w = 0;
    for (const char *p = utf8; *p; ) {
        uint32_t cp = utf8_next(&p);
        glyph_t *e = cache_get(cp, size);
        w += e ? e->adv : size / 2;
    }
    return w;
}
