#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_codec_dev.h"
#include "instrument_engine.h"

typedef struct {
    bool ready, active;
    uint8_t timbre, volume, voices, last_note;
    int8_t octave;
    int last_error;
} instrument_snapshot_t;

/* Call once, after audio_start. Never changes codec format or board power. */
esp_err_t instrument_start(esp_codec_dev_handle_t codec);
void instrument_set_active(bool active);
bool instrument_active(void);
void instrument_note_on(uint8_t note, uint8_t velocity);
void instrument_note_off(uint8_t note);
void instrument_panic(void);
void instrument_set_timbre(uint8_t timbre); /* sine, triangle, square, pluck */
void instrument_set_volume(uint8_t volume); /* digital gain, 0..100 */
void instrument_set_octave(int octave); /* UI offset -2..2; note_on uses final MIDI pitch */
void instrument_get_snapshot(instrument_snapshot_t *snapshot);

/* All USB DAC calls MUST use these gates (active() alone cannot serialize I/O).
 * USB PCM is consumed/dropped while the local app owns playback. Capture is
 * unchanged. Latest USB volume/mute is restored on local app exit. */
int instrument_write_usb(void *data, size_t bytes);
int instrument_set_usb_volume(int volume);
int instrument_set_usb_mute(bool mute);
