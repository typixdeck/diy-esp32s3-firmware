#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INSTRUMENT_VOICES 8
#define INSTRUMENT_SAMPLE_RATE 48000
#define INSTRUMENT_TIMBRES 4

typedef struct {
    float phase, step, envelope, velocity;
    uint32_t age;
    uint8_t note, timbre;
    bool used, held;
} instrument_voice_t;

/* Portable DSP state. Its owner must serialize render and note/control calls. */
typedef struct {
    instrument_voice_t voices[INSTRUMENT_VOICES];
    uint32_t sequence;
    uint8_t timbre, volume;
} instrument_engine_t;

void instrument_engine_init(instrument_engine_t *engine);
void instrument_engine_note_on(instrument_engine_t *engine, uint8_t note, uint8_t velocity);
void instrument_engine_note_off(instrument_engine_t *engine, uint8_t note);
void instrument_engine_panic(instrument_engine_t *engine);
void instrument_engine_render(instrument_engine_t *engine, int16_t *stereo, size_t frames);
unsigned instrument_engine_voice_count(const instrument_engine_t *engine);
