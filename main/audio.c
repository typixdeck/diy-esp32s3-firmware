// ES8389 codec 初始化（USB UAC 放音 + 录音全双工路径）
// 关键：GPIO47/48 先在 LCD SPI init 阶段当 SPI 用，这里重配成 I2S DOUT/DIN。
// 放音：UAC 收到的 PCM 由 main.c 的 uac_output_cb 写进 codec；
// 录音：main.c 的 uac_input_cb 从 codec 读 PCM（双 MEMS 麦 → ES8389 ADC → I2S rx）。
#include <stdio.h>
#include <stdatomic.h>
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "esp_check.h"
#include "board_pins.h"
#include "audio.h"

static const char *TAG = "AUDIO";
static i2c_master_bus_handle_t s_bus;
static i2s_chan_handle_t      s_tx;
static i2s_chan_handle_t      s_rx;
static _Atomic(esp_codec_dev_handle_t) s_codec_out;
static _Atomic(esp_codec_dev_handle_t) s_codec_in;
static _Atomic(const audio_codec_ctrl_if_t *) s_ctrl_if;
static bool s_tx_enabled, s_rx_enabled;

static void i2s_cleanup(void)
{
    if (s_rx_enabled) i2s_channel_disable(s_rx);
    if (s_tx_enabled) i2s_channel_disable(s_tx);
    if (s_rx) i2s_del_channel(s_rx);
    if (s_tx) i2s_del_channel(s_tx);
    s_rx = s_tx = NULL;
    s_rx_enabled = s_tx_enabled = false;
}

static esp_err_t i2s_setup(int hz)
{
    // 全双工：tx/rx 同一 port（时钟共享，同 48k/16bit/立体声）
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.auto_clear = true;
    esp_err_t err = i2s_new_channel(&chan, &s_tx, &s_rx);
    if (err != ESP_OK) return err;
    i2s_std_config_t std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK, .bclk = PIN_I2S_BCLK, .ws = PIN_I2S_LRCK,
            .dout = PIN_I2S_DOUT, .din  = PIN_I2S_DIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    if ((err = i2s_channel_init_std_mode(s_tx, &std)) != ESP_OK) return err;
    if ((err = i2s_channel_init_std_mode(s_rx, &std)) != ESP_OK) return err;
    if ((err = i2s_channel_enable(s_tx)) != ESP_OK) return err;
    s_tx_enabled = true;
    if ((err = i2s_channel_enable(s_rx)) != ESP_OK) return err;
    s_rx_enabled = true;
    return ESP_OK;
}

static esp_err_t es8389_setup(int hz, int ch, uint8_t es7bit)
{
    esp_err_t err = ESP_ERR_NO_MEM;
    const audio_codec_data_if_t *data_if = NULL;
    const audio_codec_gpio_if_t *gpio_if = NULL;
    const audio_codec_if_t *codec_if = NULL;
    esp_codec_dev_handle_t codec = NULL;
    // esp_codec_dev 把 addr 右移一位当 7-bit（audio_codec_ctrl_i2c.c:52），故 addr = 7bit<<1
    audio_codec_i2c_cfg_t ctrl = { .port = 0, .addr = es7bit << 1, .bus_handle = s_bus };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&ctrl);
    if (!ctrl_if) goto failed;
    audio_codec_i2s_cfg_t data = { .port = 0, .tx_handle = s_tx, .rx_handle = s_rx };
    data_if = audio_codec_new_i2s_data(&data);
    if (!data_if) goto failed;
    gpio_if = audio_codec_new_gpio();
    if (!gpio_if) goto failed;

    es8389_codec_cfg_t cfg = {
        .ctrl_if = ctrl_if, .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,   // DAC 放音 + ADC 录音（双 MEMS 麦）
        .pa_pin = -1, .pa_reverted = false,
        .master_mode = false,        // ES8389 作 I2S 从
        .use_mclk = false,           // ★ MCLK 未接：codec 从 BCLK 派生内部时钟
        .digital_mic = false, .invert_mclk = false, .invert_sclk = false,
        .hw_gain = { .pa_voltage = 0.0, .codec_dac_voltage = 3.3 },
        // ★ no_dac_ref 必须为 true（右声道无信号的根因，2026-08-15 实锤）：
        //   false 时驱动开 "internal reference signal (ADCL + DACR)" AEC 参考模式
        //   （0x23 bit7 + 0xF0=0x1A）——ADC 数字输出右 slot 被替换成 DAC 回采参考，
        //   MIC2 信号根本进不了 I2S，用户 Audacity 实录右声道纯噪音即此。
        //   true 还顺带修正时钟系数：coeff 表选 {32,1536000,48000}（ratio=bits×ch=32），
        //   与真实 BCLK=48k×32=1.536MHz 匹配；false 时按 ratio=64（3.072MHz）配置，
        //   与硬件失配（疑似左声道噪音来源之一）。
        .no_dac_ref = true,
        // ★ 板 #1 专用 workaround（右声道模拟前端个体故障，右=左数字拷贝）。
        //   2026-08-15 板 #2 对比实锤为板 #1 单板问题：板 #2 真立体声正常，
        //   必须为 false。诊断见 docs/es8389_adc2_right_channel_dead_2026-08.md
        .adc2_copy_left = BOARD1_ADC2_DEAD_WORKAROUND,
        .mclk_div = 256,
    };
    // Constructor may also fail when the codec does not respond on I2C.
    err = ESP_FAIL;
    codec_if = es8389_codec_new(&cfg);
    if (!codec_if) goto failed;

    // ★ 单个 IN_OUT 设备（回退 2026-08-15 拆分实验）：拆成 OUT/IN 双设备后
    //   实测 ADC 读出全零（micL=micR=0），且"单设备 DAC 无声"的说法与事实
    //   矛盾——用户在单设备版本上听到过音乐。保持单设备 + no_dac_ref=true
    //   （真正的右声道修复）为最小已验证组合。
    esp_codec_dev_cfg_t dev = { .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT, .codec_if = codec_if, .data_if = data_if };
    err = ESP_ERR_NO_MEM;
    codec = esp_codec_dev_new(&dev);
    if (!codec) goto failed;

    esp_codec_dev_sample_info_t s = {
        .bits_per_sample = 16, .channel = ch, .channel_mask = 0x03,
        .sample_rate = hz, .mclk_multiple = 256,
    };
    err = esp_codec_dev_open(codec, &s);
    if (err != ESP_CODEC_DEV_OK) goto failed;
    err = esp_codec_dev_set_out_vol(codec, 60);
    if (err != ESP_CODEC_DEV_OK) goto failed;
    err = esp_codec_dev_set_in_gain(codec, 24.0);
    if (err != ESP_CODEC_DEV_OK) goto failed;
    // Publish only fully initialized, lifetime-stable handles to other tasks.
    atomic_store(&s_ctrl_if, ctrl_if);
    atomic_store(&s_codec_in, codec);
    atomic_store(&s_codec_out, codec);
    ESP_LOGI(TAG, "ES8389 @7bit 0x%02X: %dHz %dch (use_mclk=false, slave, IN_OUT single dev, no_dac_ref=1)", es7bit, hz, ch);
    return ESP_OK;
failed:
    if (codec) esp_codec_dev_delete(codec);
    if (codec_if) audio_codec_delete_codec_if(codec_if);
    if (data_if) audio_codec_delete_data_if(data_if);
    if (ctrl_if) audio_codec_delete_ctrl_if(ctrl_if);
    if (gpio_if) audio_codec_delete_gpio_if(gpio_if);
    return err;
}

esp_err_t audio_start(i2c_master_bus_handle_t bus)
{
    if (atomic_load(&s_codec_out)) return ESP_OK;
    if (!bus) return ESP_ERR_INVALID_ARG;
    s_bus = bus;

    // ES8389 AD1 脚悬空 → 地址在 7-bit 0x10/0x12 间漂，逐个探
    static const uint8_t cand[] = { 0x10, 0x11, 0x12, 0x13 };
    uint8_t es7 = 0;
    for (int i = 0; i < (int)(sizeof(cand)/sizeof(cand[0])); i++) {
        if (i2c_master_probe(s_bus, cand[i], 30) == ESP_OK) { es7 = cand[i]; break; }
    }
    if (!es7) { ESP_LOGE(TAG, "ES8389 unavailable; continuing without audio"); return ESP_ERR_NOT_FOUND; }
    ESP_LOGI(TAG, "ES8389 真实 7-bit 地址 = 0x%02X", es7);

    esp_err_t err = i2s_setup(UAC_SAMPLE_RATE);
    if (err == ESP_OK) err = es8389_setup(UAC_SAMPLE_RATE, UAC_CHANNELS, es7);
    if (err != ESP_OK) {
        i2s_cleanup();
        ESP_LOGE(TAG, "Audio disabled: %s", esp_err_to_name(err));
    }
    return err;
}

esp_codec_dev_handle_t audio_codec_handle(void)    { return s_codec_out; }
esp_codec_dev_handle_t audio_codec_in_handle(void) { return s_codec_in; }

// DAC 左右声道数字互换：REG0x44 (DAC MIX CONTROL) bit5=DAC2→DAC1、
// bit4=DAC1→DAC2，两位同置 0x30 即完整 L/R 互换（ES8389_DS Rev1.0）。
// 用途：本板喇叭链路左右反接（耳机不反——CN10 切换触点配对与原理图假设
// 相反，或两喇叭装位互换，电气上不可区分），外放时互换、插耳机时恢复。
// 硬件分析与下版 PCB 修改见 docs/typixdeck_speaker_lr_swap_2026-08.md。
// 幂等：读-改-写，值一致不产生 I2C 写。
int audio_set_dac_lr_swap(bool swap)
{
    const audio_codec_ctrl_if_t *ctrl = atomic_load(&s_ctrl_if);
    if (!ctrl) return -1;
    uint8_t v = 0;
    if (ctrl->read_reg(ctrl, 0x44, 1, &v, 1) != 0) return -1;
    uint8_t nv = swap ? (uint8_t)(v | 0x30) : (uint8_t)(v & ~0x30);
    if (nv == v) return 0;
    int ret = ctrl->write_reg(ctrl, 0x44, 1, &nv, 1);
    ESP_LOGI(TAG, "DAC L/R %s (REG0x44: 0x%02X -> 0x%02X)", swap ? "互换" : "正常", v, nv);
    return ret;
}
