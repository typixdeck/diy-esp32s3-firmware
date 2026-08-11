// usb_descriptors.c — UAC2 speaker + CDC composite（AS_PART 模式，app 自带描述符）
// 接口布局：[0] Audio Control + [1] Audio Streaming + [2] CDC Comm + [3] CDC Data
// 端点：UAC OUT 0x01 / UAC FB 0x81 / CDC notify 0x82 / CDC bulk OUT 0x03 / CDC bulk IN 0x83
#include "tusb.h"
#include "uac_descriptors.h"   // TUD_AUDIO_DESCRIPTOR / TUD_AUDIO_DEVICE_DESC_LEN

// AS_PART 模式下 CONFIG_UAC_TUSB_* Kconfig 不暴露，直接硬编码（app 全权控制描述符）
#define UAC_TUSB_VID         0x303A   // Espressif
#define UAC_TUSB_PID         0x80C1   // TypixDeck UAC+CDC composite
#define UAC_TUSB_MANUFACTURER "CyberFold"
#define UAC_TUSB_PRODUCT      "TypixDeck UAC+CDC"
#define UAC_TUSB_SERIAL       "TD0720"

// UAC 占接口 0..NUM_INTERFACES-1（NUM_INTERFACES 来自 uac_descriptors.h：spk-only=2，
// spk+mic=3）。CDC 接在 UAC 后面，避免接口号撞车（之前 MIC=1 时 CDC comm 和 AS_mic 都抢 itf 2）
enum {
    ITF_NUM_AUDIO_CONTROL = 0,
    ITF_NUM_CDC_COMM = NUM_INTERFACES,   // 2（spk）或 3（spk+mic）
    ITF_NUM_CDC_DATA,
    ITF_NUM_TOTAL
};

#define EPNUM_AUDIO_OUT   0x01
#define EPNUM_AUDIO_FB    0x81
#define EPNUM_CDC_NOTIFY  0x82
#define EPNUM_CDC_OUT     0x03
#define EPNUM_CDC_IN      0x83

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + CFG_TUD_AUDIO * TUD_AUDIO_DEVICE_DESC_LEN \
                           + CFG_TUD_CDC * TUD_CDC_DESC_LEN)

//--------------------------------------------------------------------+
// Device Descriptor（IAD 复合设备）
//--------------------------------------------------------------------+
tusb_desc_device_t const desc_device = {
    .bLength         = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB          = 0x0200,
    .bDeviceClass    = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor        = UAC_TUSB_VID,
    .idProduct       = UAC_TUSB_PID,
    .bcdDevice       = 0x0100,
    .iManufacturer   = 0x01,
    .iProduct        = 0x02,
    .iSerialNumber   = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *)&desc_device; }

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+
uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    // UAC2 speaker：AC=itf0, AS=itf0+1=1，EP OUT 0x01，FB 0x81
    TUD_AUDIO_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, 4, EPNUM_AUDIO_OUT, 0, EPNUM_AUDIO_FB),
    // CDC：comm=itf2, data=itf3，notify 0x82(8B)，bulk OUT 0x03 / IN 0x83(64B)
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_COMM, 5, EPNUM_CDC_NOTIFY, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+
char const *string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 },   // 0: English
    UAC_TUSB_MANUFACTURER,           // 1
    UAC_TUSB_PRODUCT,                // 2
    UAC_TUSB_SERIAL,                 // 3
    "uac speaker",                   // 4: UAC AC interface
    "cdc debug",                     // 5: CDC interface
};

static uint16_t _desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    uint8_t chr_count;
    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
        const char *str = string_desc_arr[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) _desc_str[1 + i] = str[i];
    }
    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
