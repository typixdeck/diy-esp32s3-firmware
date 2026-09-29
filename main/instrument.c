#include "instrument.h"
#include <limits.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define BLOCK_FRAMES 256
static SemaphoreHandle_t s_lock;
static esp_codec_dev_handle_t s_codec;
static instrument_engine_t s_engine;
static instrument_snapshot_t s_status;
static int s_usb_volume = 60;
static bool s_usb_mute;

static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

static void worker(void *unused)
{
    (void)unused;
    int16_t samples[BLOCK_FRAMES * 2];
    for (;;) {
        lock();
        bool active = s_status.active;
        if (active) {
            instrument_engine_render(&s_engine, samples, BLOCK_FRAMES);
            int result = esp_codec_dev_write(s_codec, samples, sizeof(samples));
            if (result != ESP_CODEC_DEV_OK) {
                s_status.last_error = result;
                s_status.active = false;
                instrument_engine_panic(&s_engine);
                esp_codec_dev_set_out_vol(s_codec, s_usb_volume);
                esp_codec_dev_set_out_mute(s_codec, s_usb_mute);
            }
        }
        unlock();
        /* I2S write paces active audio. Yield one tick so control calls and USB
         * cannot starve while DMA initially accepts several blocks at once. */
        vTaskDelay(active ? 1 : pdMS_TO_TICKS(5));
    }
}

esp_err_t instrument_start(esp_codec_dev_handle_t codec)
{
    if (s_lock) return s_status.ready ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (!codec) return ESP_ERR_INVALID_ARG;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    s_codec = codec;
    instrument_engine_init(&s_engine);
    s_status = (instrument_snapshot_t){ .ready = true, .volume = 35, .last_note = 60 };
    if (xTaskCreate(worker, "instrument", 4096, NULL, 6, NULL) != pdPASS) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        s_codec = NULL;
        s_status.ready = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void instrument_set_active(bool active)
{
    if (!s_lock) return;
    lock();
    if (active != s_status.active) {
        instrument_engine_panic(&s_engine);
        /* A silence block follows already queued local samples before allowing
         * a USB writer in. No DAC power/mute pulse is used to stop a note. */
        int result = ESP_CODEC_DEV_OK;
        if (!active) {
            int16_t silence[BLOCK_FRAMES * 2] = {0};
            result = esp_codec_dev_write(s_codec, silence, sizeof(silence));
        }
        /* Local PCM already owns the volume/headroom (8 x 0.10 peak max).
         * Codec 60 applies another -20 dB; unity avoids double attenuation. */
        int volume_result = esp_codec_dev_set_out_vol(s_codec, active ? 100 : s_usb_volume);
        int mute_result = esp_codec_dev_set_out_mute(s_codec, active ? false : s_usb_mute);
        if (result == ESP_CODEC_DEV_OK) result = volume_result;
        if (result == ESP_CODEC_DEV_OK) result = mute_result;
        s_status.last_error = result;
        s_status.active = active && result == ESP_CODEC_DEV_OK;
        if (active && result != ESP_CODEC_DEV_OK) {
            esp_codec_dev_set_out_vol(s_codec, s_usb_volume);
            esp_codec_dev_set_out_mute(s_codec, s_usb_mute);
        }
    }
    unlock();
}

bool instrument_active(void)
{
    if (!s_lock) return false;
    lock();
    bool value = s_status.active;
    unlock();
    return value;
}

void instrument_note_on(uint8_t note, uint8_t velocity)
{
    if (!s_lock || note > 127 || velocity > 127) return;
    lock();
    if (s_status.active) {
        instrument_engine_note_on(&s_engine, note, velocity);
        s_status.last_note = note;
    }
    unlock();
}

void instrument_note_off(uint8_t note)
{
    if (!s_lock) return;
    lock();
    instrument_engine_note_off(&s_engine, note);
    unlock();
}

void instrument_panic(void)
{
    if (!s_lock) return;
    lock();
    instrument_engine_panic(&s_engine);
    unlock();
}

void instrument_set_timbre(uint8_t timbre)
{
    if (!s_lock || timbre >= INSTRUMENT_TIMBRES) return;
    lock();
    instrument_engine_panic(&s_engine);
    s_status.timbre = s_engine.timbre = timbre;
    unlock();
}

void instrument_set_volume(uint8_t volume)
{
    if (!s_lock) return;
    lock();
    s_status.volume = s_engine.volume = volume > 100 ? 100 : volume;
    unlock();
}

void instrument_set_octave(int octave)
{
    if (!s_lock) return;
    lock();
    instrument_engine_panic(&s_engine);
    s_status.octave = octave < -2 ? -2 : (octave > 2 ? 2 : octave);
    unlock();
}

void instrument_get_snapshot(instrument_snapshot_t *snapshot)
{
    if (!snapshot) return;
    if (!s_lock) { memset(snapshot, 0, sizeof(*snapshot)); return; }
    lock();
    *snapshot = s_status;
    snapshot->voices = instrument_engine_voice_count(&s_engine);
    unlock();
}

int instrument_write_usb(void *data, size_t bytes)
{
    if (!s_lock || !data || bytes > INT_MAX) return ESP_CODEC_DEV_INVALID_ARG;
    lock();
    int result = s_status.active ? ESP_CODEC_DEV_OK : esp_codec_dev_write(s_codec, data, (int)bytes);
    unlock();
    return result;
}

int instrument_set_usb_volume(int volume)
{
    if (!s_lock) return ESP_CODEC_DEV_INVALID_ARG;
    lock();
    s_usb_volume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    int result = s_status.active ? ESP_CODEC_DEV_OK : esp_codec_dev_set_out_vol(s_codec, s_usb_volume);
    unlock();
    return result;
}

int instrument_set_usb_mute(bool mute)
{
    if (!s_lock) return ESP_CODEC_DEV_INVALID_ARG;
    lock();
    s_usb_mute = mute;
    int result = s_status.active ? ESP_CODEC_DEV_OK : esp_codec_dev_set_out_mute(s_codec, mute);
    unlock();
    return result;
}
