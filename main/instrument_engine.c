#include "instrument_engine.h"
#include <math.h>
#include <string.h>

#define SINE_SIZE 1024
static float sine_table[SINE_SIZE + 1];

static float sine(float phase)
{
    float position = phase * SINE_SIZE;
    unsigned index = (unsigned)position;
    float fraction = position - index;
    index &= SINE_SIZE - 1;
    return sine_table[index] + fraction * (sine_table[index + 1] - sine_table[index]);
}

static float blep(float phase, float step)
{
    if (phase < step) {
        float x = phase / step;
        return x + x - x * x - 1.0f;
    }
    if (phase > 1.0f - step) {
        float x = (phase - 1.0f) / step;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}

void instrument_engine_init(instrument_engine_t *engine)
{
    memset(engine, 0, sizeof(*engine));
    engine->volume = 35;
    for (unsigned i = 0; i <= SINE_SIZE; ++i)
        sine_table[i] = sinf(6.28318530718f * i / SINE_SIZE);
}

void instrument_engine_note_on(instrument_engine_t *engine, uint8_t note, uint8_t velocity)
{
    if (note > 127 || velocity > 127) return;
    if (!velocity) { instrument_engine_note_off(engine, note); return; }
    instrument_voice_t *selected = NULL;
    for (unsigned i = 0; i < INSTRUMENT_VOICES; ++i) {
        instrument_voice_t *voice = &engine->voices[i];
        if (voice->used && voice->note == note) { selected = voice; break; }
        if (!voice->used && !selected) selected = voice;
    }
    if (!selected) {
        selected = &engine->voices[0];
        for (unsigned i = 1; i < INSTRUMENT_VOICES; ++i) {
            instrument_voice_t *voice = &engine->voices[i];
            if ((!voice->held && selected->held) ||
                (voice->held == selected->held && voice->age < selected->age)) selected = voice;
        }
    }
    *selected = (instrument_voice_t){
        .step = (440.0f / INSTRUMENT_SAMPLE_RATE) * powf(2.0f, ((int)note - 69) / 12.0f),
        .velocity = velocity / 127.0f, .age = ++engine->sequence,
        .note = note, .timbre = engine->timbre % INSTRUMENT_TIMBRES,
        .used = true, .held = true,
    };
}

void instrument_engine_note_off(instrument_engine_t *engine, uint8_t note)
{
    for (unsigned i = 0; i < INSTRUMENT_VOICES; ++i)
        if (engine->voices[i].note == note) engine->voices[i].held = false;
}

void instrument_engine_panic(instrument_engine_t *engine)
{
    memset(engine->voices, 0, sizeof(engine->voices));
}

unsigned instrument_engine_voice_count(const instrument_engine_t *engine)
{
    unsigned result = 0;
    for (unsigned i = 0; i < INSTRUMENT_VOICES; ++i) result += engine->voices[i].used;
    return result;
}

void instrument_engine_render(instrument_engine_t *engine, int16_t *stereo, size_t frames)
{
    float gain = (engine->volume > 100 ? 100 : engine->volume) / 100.0f * 0.10f;
    for (size_t frame = 0; frame < frames; ++frame) {
        float mix = 0.0f;
        for (unsigned i = 0; i < INSTRUMENT_VOICES; ++i) {
            instrument_voice_t *voice = &engine->voices[i];
            if (!voice->used) continue;
            if (voice->held) {
                if (voice->envelope < 1.0f) {
                    voice->envelope += 1.0f / 192.0f; /* 4 ms attack */
                    if (voice->envelope >= 1.0f) {
                        voice->envelope = 1.0f;
                    }
                }
            } else {
                voice->envelope -= 1.0f / 3360.0f; /* <= 70 ms release */
                if (voice->envelope <= 0.0f) { voice->used = false; continue; }
            }
            float p = voice->phase;
            float sample;
            switch (voice->timbre) {
            case 1: sample = 1.0f - 4.0f * fabsf(p - 0.5f); break;
            case 2: {
                float second = p + 0.5f;
                if (second >= 1.0f) second -= 1.0f;
                sample = (p < 0.5f ? 1.0f : -1.0f) + blep(p, voice->step) - blep(second, voice->step);
                break;
            }
            case 3: {
                float second = p * 2.0f;
                if (second >= 1.0f) second -= 1.0f;
                sample = (sine(p) + 0.3f * sine(second)) / 1.3f;
                voice->velocity *= 0.99994f; /* Plucked voice naturally decays. */
                if (voice->velocity < 0.0001f) { voice->used = false; continue; }
                break;
            }
            default: sample = sine(p); break;
            }
            mix += sample * voice->envelope * voice->velocity;
            voice->phase += voice->step;
            if (voice->phase >= 1.0f) voice->phase -= 1.0f;
        }
        mix *= gain;
        if (mix > 1.0f) mix = 1.0f;
        if (mix < -1.0f) mix = -1.0f;
        int16_t pcm = (int16_t)(mix * 32767.0f);
        stereo[frame * 2] = pcm;
        stereo[frame * 2 + 1] = pcm;
    }
}
