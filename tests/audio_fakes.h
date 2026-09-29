#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_NOT_FOUND 3
#define ESP_CODEC_DEV_OK 0
#define ESP_CODEC_DEV_WORK_MODE_BOTH 0
#define ESP_CODEC_DEV_TYPE_IN_OUT 0
static void fake_log(const char *tag, const char *fmt, ...) {(void)tag;(void)fmt;}
#define ESP_LOGI fake_log
#define ESP_LOGE fake_log
static const char *esp_err_to_name(int err) {(void)err;return "failure";}
typedef void *i2c_master_bus_handle_t;
typedef struct {bool enabled;} fake_channel_t;
typedef fake_channel_t *i2s_chan_handle_t;
typedef void *esp_codec_dev_handle_t;
typedef struct audio_codec_ctrl_if audio_codec_ctrl_if_t;
struct audio_codec_ctrl_if {
    int (*read_reg)(const audio_codec_ctrl_if_t *,int,int,void *,int);
    int (*write_reg)(const audio_codec_ctrl_if_t *,int,int,const void *,int);
};
typedef struct {int unused;} audio_codec_data_if_t;
typedef struct {int unused;} audio_codec_gpio_if_t;
typedef struct {int unused;} audio_codec_if_t;
typedef struct {bool auto_clear;} i2s_chan_config_t;
typedef struct {
    struct {int mclk_multiple;} clk_cfg;
    int slot_cfg;
    struct {int mclk,bclk,ws,dout,din;struct {bool mclk_inv,bclk_inv,ws_inv;} invert_flags;} gpio_cfg;
} i2s_std_config_t;
#define I2S_CHANNEL_DEFAULT_CONFIG(a,b) ((i2s_chan_config_t){0})
#define I2S_STD_CLK_DEFAULT_CONFIG(hz) {0}
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(a,b) 0
#define I2S_MCLK_MULTIPLE_256 256
typedef struct {int port,addr;void *bus_handle;} audio_codec_i2c_cfg_t;
typedef struct {int port;void *tx_handle,*rx_handle;} audio_codec_i2s_cfg_t;
typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;
    const audio_codec_gpio_if_t *gpio_if;
    int codec_mode,pa_pin;bool pa_reverted,master_mode,use_mclk,digital_mic,invert_mclk,invert_sclk;
    struct {double pa_voltage,codec_dac_voltage;} hw_gain;
    bool no_dac_ref,adc2_copy_left;int mclk_div;
} es8389_codec_cfg_t;
typedef struct {int dev_type;const audio_codec_if_t *codec_if;const audio_codec_data_if_t *data_if;} esp_codec_dev_cfg_t;
typedef struct {int bits_per_sample,channel,channel_mask,sample_rate,mclk_multiple;} esp_codec_dev_sample_info_t;
static int mock_step,mock_fail,mock_live;
static bool mock_absent;
static void *mock_allocations[16];
static int mock_allocated;
static bool fails(void) {return ++mock_step==mock_fail;}
static void *allocate(size_t n) {void *p=calloc(1,n);assert(p);mock_allocations[mock_allocated++]=p;mock_live++;return p;}
static void release(const void *p) {assert(p&&mock_live);for(int i=0;i<mock_allocated;i++)if(mock_allocations[i]==p){mock_allocations[i]=NULL;mock_live--;free((void *)p);return;}assert(!"double delete");}
static esp_err_t i2c_master_probe(void *bus,int addr,int timeout) {(void)bus;(void)addr;(void)timeout;return mock_absent?ESP_FAIL:ESP_OK;}
static esp_err_t i2s_new_channel(const i2s_chan_config_t *c,i2s_chan_handle_t *tx,i2s_chan_handle_t *rx) {(void)c;if(fails())return ESP_FAIL;*tx=allocate(sizeof(**tx));*rx=allocate(sizeof(**rx));return ESP_OK;}
static esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t h,const i2s_std_config_t *c) {(void)c;assert(h);return fails()?ESP_FAIL:ESP_OK;}
static esp_err_t i2s_channel_enable(i2s_chan_handle_t h) {assert(h);if(fails())return ESP_FAIL;h->enabled=true;return ESP_OK;}
static int i2s_channel_disable(i2s_chan_handle_t h) {assert(h&&h->enabled);h->enabled=false;return 0;}
static int i2s_del_channel(i2s_chan_handle_t h) {assert(h&&!h->enabled);release(h);return 0;}
static const audio_codec_ctrl_if_t *audio_codec_new_i2c_ctrl(const audio_codec_i2c_cfg_t *c) {(void)c;return fails()?NULL:allocate(sizeof(audio_codec_ctrl_if_t));}
static const audio_codec_data_if_t *audio_codec_new_i2s_data(const audio_codec_i2s_cfg_t *c) {(void)c;return fails()?NULL:allocate(sizeof(audio_codec_data_if_t));}
static const audio_codec_gpio_if_t *audio_codec_new_gpio(void) {return fails()?NULL:allocate(sizeof(audio_codec_gpio_if_t));}
static const audio_codec_if_t *es8389_codec_new(const es8389_codec_cfg_t *c) {(void)c;return fails()?NULL:allocate(sizeof(audio_codec_if_t));}
static esp_codec_dev_handle_t esp_codec_dev_new(const esp_codec_dev_cfg_t *c) {(void)c;return fails()?NULL:allocate(1);}
static int esp_codec_dev_open(esp_codec_dev_handle_t h,const esp_codec_dev_sample_info_t *s) {assert(h);(void)s;return fails()?ESP_FAIL:ESP_OK;}
static int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t h,int v) {assert(h);(void)v;return fails()?ESP_FAIL:ESP_OK;}
static int esp_codec_dev_set_in_gain(esp_codec_dev_handle_t h,float v) {assert(h);(void)v;return fails()?ESP_FAIL:ESP_OK;}
static void esp_codec_dev_delete(esp_codec_dev_handle_t h) {release(h);}
static int audio_codec_delete_codec_if(const audio_codec_if_t *p) {release(p);return 0;}
static int audio_codec_delete_ctrl_if(const audio_codec_ctrl_if_t *p) {release(p);return 0;}
static int audio_codec_delete_data_if(const audio_codec_data_if_t *p) {release(p);return 0;}
static int audio_codec_delete_gpio_if(const audio_codec_gpio_if_t *p) {release(p);return 0;}
