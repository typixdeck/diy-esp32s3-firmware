// 中文 TTF 渲染（FreeType + 阿里巴巴普惠体 GB2312 子集，font 分区 mmap 直读）
//
// 用法：ttf_font_init() 一次 → ttf_draw_text() 往 RGB565 framebuffer 画 UTF-8
// 文本（中英混排）。字形按 (codepoint,size) 缓存在 PSRAM，首次渲染 ~1-3ms/字，
// 命中后是纯 alpha 混合。仅限单任务使用（本工程 = app_main 主循环）。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// 映射 font 分区并初始化 FreeType。失败（分区空/字体损坏）返回错误码，
// 此后 ttf_draw_text 静默不画，调用方可回退 ASCII 点阵。
esp_err_t ttf_font_init(void);

bool ttf_font_ready(void);
// Check primary-face glyph coverage without allocating/rendering glyph bitmaps.
// Translation selection may fall back when a bundled subset lacks characters.
bool ttf_text_supported(const char *utf8);

// 在 fb（fb_w×fb_h RGB565）的 (x,y) 处画 UTF-8 文本，y 为文本行顶部。
// size 为像素字号（任意值，矢量缩放）。返回绘制后的 x 前进量（像素宽）。
int ttf_draw_text(uint16_t *fb, int fb_w, int fb_h,
                  int x, int y, int size, uint16_t color, const char *utf8);

// 文本像素宽（不画，用于居中/右对齐）
int ttf_text_width(int size, const char *utf8);

// ---- 第二字面（2026-09-10）：开机画面字标用 Special Elite（打字机体，Apache 2.0），
//      子集 TTF 内嵌在 app 里（fonts/special_elite_subset.ttf，EMBED_FILES）----
#define TTF_FACE_MAIN   0     // 阿里巴巴普惠体（font 分区）
#define TTF_FACE_DECO   1     // Special Elite（app 内嵌，仅拉丁）
#define TTF_FACE_COUNT  2
#include <stddef.h>
// 在 ttf_font_init() 之后注册；data 必须常驻（rodata/mmap）
esp_err_t ttf_font_add_face(int face, const uint8_t *data, size_t len);
bool ttf_face_ready(int face);
// spacing = 额外字距（像素，可为负），做打字机式宽字距标题用
int ttf_draw_text_face(int face, uint16_t *fb, int fb_w, int fb_h,
                       int x, int y, int size, int spacing, uint16_t color, const char *utf8);
int ttf_text_width_face(int face, int size, int spacing, const char *utf8);
