/* Run on the development host, no board or ESP-IDF required:
 * cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
 *   -Imain main/instrument_engine.c tests/test_instrument_engine.c -lm \
 *   -o /tmp/typix-instrument-test && /tmp/typix-instrument-test
 */
#include "instrument_engine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int16_t pcm[48000 * 2];

static void test_pitch_and_release(void)
{
    instrument_engine_t engine;
    instrument_engine_init(&engine);
    instrument_engine_render(&engine, pcm, 100);
    for (unsigned i = 0; i < 200; ++i) assert(pcm[i] == 0);
    instrument_engine_note_on(&engine, 69, 127);
    instrument_engine_render(&engine, pcm, 48000);
    unsigned crossings = 0;
    for (unsigned i = 1; i < 48000; ++i) {
        assert(pcm[i * 2] == pcm[i * 2 + 1]);
        if (pcm[(i - 1) * 2] <= 0 && pcm[i * 2] > 0) ++crossings;
    }
    assert(crossings >= 439 && crossings <= 441); /* MIDI A4 = 440 Hz */
    instrument_engine_note_off(&engine, 69);
    instrument_engine_render(&engine, pcm, 4000);
    assert(instrument_engine_voice_count(&engine) == 0);
    for (unsigned i = 3500 * 2; i < 4000 * 2; ++i) assert(pcm[i] == 0);
}

static void test_polyphony_and_panic(void)
{
    instrument_engine_t engine;
    instrument_engine_init(&engine);
    for (unsigned i = 0; i < INSTRUMENT_VOICES; ++i)
        instrument_engine_note_on(&engine, 60 + i, 127);
    assert(instrument_engine_voice_count(&engine) == INSTRUMENT_VOICES);
    instrument_engine_note_on(&engine, 60, 80); /* same note retriggers, no extra voice */
    assert(instrument_engine_voice_count(&engine) == INSTRUMENT_VOICES);
    instrument_engine_note_off(&engine, 64);
    instrument_engine_note_on(&engine, 90, 127); /* released voice stolen first */
    assert(engine.voices[4].note == 90);
    instrument_engine_note_on(&engine, 91, 127); /* oldest held voice next */
    assert(engine.voices[1].note == 91);
    instrument_engine_note_on(&engine, 91, 0);
    assert(!engine.voices[1].held);
    instrument_engine_note_on(&engine, 128, 127);
    assert(instrument_engine_voice_count(&engine) == INSTRUMENT_VOICES);
    instrument_engine_panic(&engine);
    instrument_engine_render(&engine, pcm, 100);
    for (unsigned i = 0; i < 200; ++i) assert(pcm[i] == 0);
}

static void test_all_timbres_and_bounds(void)
{
    for (unsigned timbre = 0; timbre < INSTRUMENT_TIMBRES; ++timbre) {
        instrument_engine_t engine;
        instrument_engine_init(&engine);
        engine.timbre = timbre;
        engine.volume = 100;
        for (unsigned n = 0; n < 128; ++n) {
            instrument_engine_note_on(&engine, n, 127);
            int16_t guarded[516] = {0};
            guarded[0] = 1234;
            guarded[515] = 2345;
            instrument_engine_render(&engine, guarded + 1, 257);
            assert(guarded[0] == 1234 && guarded[515] == 2345);
            for (unsigned k = 0; k < 257; ++k)
                assert(guarded[1 + k * 2] == guarded[2 + k * 2]);
        }
        engine.volume = 0;
        instrument_engine_render(&engine, pcm, 500);
        for (unsigned k = 0; k < 1000; ++k) assert(pcm[k] == 0);
    }
}

static void test_block_independence_and_pluck(void)
{
    instrument_engine_t a, b;
    instrument_engine_init(&a);
    instrument_engine_init(&b);
    a.timbre = b.timbre = 3;
    instrument_engine_note_on(&a, 60, 127);
    instrument_engine_note_on(&b, 60, 127);
    int16_t whole[2048], split[2048];
    instrument_engine_render(&a, whole, 1024);
    instrument_engine_render(&b, split, 257);
    instrument_engine_render(&b, split + 514, 767);
    assert(memcmp(whole, split, sizeof(whole)) == 0);
    for (unsigned i = 0; i < 4; ++i) instrument_engine_render(&a, pcm, 48000);
    assert(instrument_engine_voice_count(&a) == 0); /* held pluck naturally finishes */
}

int main(void)
{
    test_pitch_and_release();
    test_polyphony_and_panic();
    test_all_timbres_and_bounds();
    test_block_independence_and_pluck();
    puts("instrument: pitch, release, polyphony, stealing, mute, bounds, timbres, block determinism passed");
    return 0;
}
