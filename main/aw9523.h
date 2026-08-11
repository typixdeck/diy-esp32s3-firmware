// AW9523B IO 扩展器驱动（U16 @ 0x5B）
#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

// 寄存器
#define AW9523_REG_INPUT_P0   0x00
#define AW9523_REG_INPUT_P1   0x01
#define AW9523_REG_OUTPUT_P0  0x02
#define AW9523_REG_OUTPUT_P1  0x03
#define AW9523_REG_CONFIG_P0  0x04  // 1=输入 0=输出
#define AW9523_REG_CONFIG_P1  0x05
#define AW9523_REG_INT_P0     0x06  // 1=屏蔽 0=使能
#define AW9523_REG_INT_P1     0x07
#define AW9523_REG_ID         0x10  // 读出应为 0x23
#define AW9523_REG_GCR        0x11  // bit4 GPOMD: 1=P0 推挽 0=开漏
#define AW9523_REG_LEDMODE_P0 0x12  // 1=GPIO 0=LED
#define AW9523_REG_LEDMODE_P1 0x13
#define AW9523_REG_SOFT_RESET 0x7F  // 写 0x00 软复位

#define AW9523_CHIP_ID        0x23

// 板上功能位——引脚号→端口映射依据 AW9523B datasheet QFN-24 引脚图：
// pin1-4=P1_0..P1_3, pin5-8=P0_0..P0_3, pin9=GND, pin10-13=P0_4..P0_7,
// pin14-17=P1_4..P1_7, pin18=AD0, pin21=VCC, pin22=INTN, pin23=RSTN, pin24=AD1
// （AD0=AD1=3V3 → 0x5B，与实测吻合，映射已验证）
//
// ⚠️ 全 16 脚功能已按网表逐一核实（2026-08-06），除下面显式驱动的 4 脚外，
//    其余全部必须保持输入 Hi-Z：驱高会背馈未上电的 CM（EXTRST/GPIO26/GPIO19/
//    GPIO2）、禁充（CHG_DIS）、顶死耳机检测/键盘中断。详见 CLAUDE.md 踩坑 #15。
//
// ---- P0 ----
#define AW9523_P0_MUX_SEL     (1 << 0)  // P0_0 (pin5)  → R202 → TPG4899 SEL（1=ESP 侧，R105 下拉=Pi 侧）【输出】
#define AW9523_P0_CHG_DIS     (1 << 1)  // P0_1 (pin6)  → R201 → CHG_DIS（R187 下拉=允许充电；驱高=禁充！）【输入】
#define AW9523_P0_CM_PMIC_EN  (1 << 2)  // P0_2 (pin7)  → Q4/R210 → PI_GLOBAL_EN(CN1.99)；低=断电 高/Hi-Z=上电【输出】
#define AW9523_P0_CM_EXTRST   (1 << 3)  // P0_3 (pin8)  → R64 → EXTRST(CN1.100)；CM 内部上拉，别驱动【输入】
#define AW9523_P0_SAFE_SHDN   (1 << 4)  // P0_4 (pin10) → R69 → Pi GPIO26（关机通知信号）【输入】
#define AW9523_P0_SAFE_PWROFF (1 << 5)  // P0_5 (pin11) → R70 → Pi GPIO19（掉电通知信号）【输入】
#define AW9523_P0_GAUGE_ALM   (1 << 6)  // P0_6 (pin12) ← 电量计告警（R88 上拉）【输入】
#define AW9523_P0_PI_GPIO2    (1 << 7)  // P0_7 (pin13) → R83 → Pi GPIO2/SDA1（触摸 SDA 的 Pi 侧！）【输入】
// ---- P1 ----
#define AW9523_P1_DAC_3V3_EN  (1 << 0)  // P1_0 (pin1)  → U31.3 DAC 电源（R84 上拉=默认开）【输入】
#define AW9523_P1_LCD_RST     (1 << 1)  // P1_1 (pin2)  → R185 → LCD_RST（R101 上拉）【输出】
#define AW9523_P1_KBD_INT     (1 << 2)  // P1_2 (pin3)  → R205 → KBD_I2C_INT（键盘 STM32 是驱动方）【输入】
#define AW9523_P1_AMP_4V6_EN  (1 << 3)  // P1_3 (pin4)  → U28.4 功放电源（R119 上拉=默认开）【输入】
#define AW9523_P1_TP_RST      (1 << 4)  // P1_4 (pin14) → R78 → GT911 复位（R43 上拉）【输出】
#define AW9523_P1_CAM_3V3_EN  (1 << 5)  // P1_5 (pin15) → U34.3 摄像头电源（R124 上拉=默认开）【输入】
#define AW9523_P1_TP_INT      (1 << 6)  // P1_6 (pin16) ← 触摸中断走 MUX（仅复位期间短暂输出低）【输入】
#define AW9523_P1_HP_DET      (1 << 7)  // P1_7 (pin17) → R86 → HP_DET 耳机检测(CN10.1)【输入】

esp_err_t aw9523_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t *out_dev);
// GT911 规范复位（INT 拉低贯穿 → 地址 0x5D，固件干净启动），调用后再 gt911_init
esp_err_t aw9523_gt911_reset(i2c_master_dev_handle_t dev);
esp_err_t aw9523_read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *val);
esp_err_t aw9523_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val);
esp_err_t aw9523_update_bits(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t mask, uint8_t val);
