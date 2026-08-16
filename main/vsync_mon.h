// Pi DPI VSYNC 探测器（2026-08-16 网表实锤的旁路，无需飞线）：
//
//   Pi GPIO2 (DPI VSYNC, CN1.58) ──┬── U63.9 (MUX NC，Pi 侧 VSYNC 入口)
//                                  └── R83(0Ω) ── AW9523 P0_7 (U16.13)
//   AW9523 INTN (U16.22) ── R82(0Ω) ── ESP GPIO5（R91 10kΩ 上拉）
//
// R83 tap 在 MUX 之前 → 不管 MUX 在哪侧都能探测 Pi 是否在刷屏。
// 原理：只解开 AW9523 P0_7 的中断屏蔽，每帧 VSYNC 跳变 → INTN 拉低 →
// ESP GPIO5 下降沿 ISR 打时间戳；高优先级任务读一次 INPUT_P0 清中断重武装
// （AW9523 中断为锁存型，读输入寄存器清除；若实测为跟随型亦兼容——
// re-arm 读变成无害动作）。相邻中断的时间差＝帧周期 → FPS。
//
// ⚠️ 前置条件：必须在 lcd_jd9168s_spi_init() 完成之后启动（该函数结尾
// spi_bus_free 释放了 GPIO5=LCD CS）。INTN 与面板 CS 共线：INTN 低电平期间
// 若 I2S(GPIO47/48) 正在放音，面板会收到垃圾 SPI 位——re-arm 任务用最高
// 可行优先级把低电平窗口压到 <1ms（占空比 ~3%），随机凑出合法命令帧
// （0xF1 前缀 + 密码锁）概率约等于零，风险量化可接受。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

// 启动探测（内部起 re-arm 任务 + GPIO5 ISR + 解开 AW9523 P0_7 中断屏蔽）
esp_err_t vsync_mon_start(i2c_master_dev_handle_t aw9523);

// 是否有稳定信号（连续 ≥5 帧周期合法且最近 500ms 内有中断）
bool vsync_mon_signal(void);

// 当前帧率（EMA 平滑）；无信号返回 0
float vsync_mon_fps(void);

// 累计有效帧数（开机以来）
uint32_t vsync_mon_frames(void);

// 最近一次 VSYNC 中断距今的毫秒数；从未有过中断返回 -1
int64_t vsync_mon_age_ms(void);

// 中断风暴退避次数（诊断用：Pi 断电悬空噪声触发）
uint32_t vsync_mon_storms(void);

// Pi 出图已确认、探测已永久关闭（INTN 回常高，LCD CS 不再被压）。
// 关闭后 signal 恒为 true、fps 为冻结的最后测量值。
bool vsync_mon_locked(void);
