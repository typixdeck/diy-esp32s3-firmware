#include "builtin_apps.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void calc_value(const char *text, double want) {
    double actual = 999;
    assert(calculator_eval(text, &actual) == CALC_OK);
    assert(fabs(actual - want) < 1e-10);
}
static void calculator_tests(void) {
    calc_value("2+3*4", 14); calc_value("(2+3)*4", 20);
    calc_value("12 / (2 * 3)", 2); calc_value("-(-2.5)+.5", 3);
    calc_value("1--2", 3); calc_value(".1+.2", .3); calc_value("1.5e2/10", 15);
    calc_value("8/2/2", 2); calc_value("1-2-3", -4);
    const char *invalid[] = {"", " ", ".", "1..2", "1+", "()", "1(2)", "(1+2", "1+2)",
                             "a", "nan", "inf", "0x10", "1e", "1e+", "1 2", "2**3"};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
        double result = 99;
        assert(calculator_eval(invalid[i], &result) == CALC_SYNTAX && result == 99);
    }
    calc_value("((((((((((((((((1))))))))))))))))", 1);
    double result;
    assert(calculator_eval("1/(2-2)", &result) == CALC_DIV_ZERO);
    assert(calculator_eval("1e300*1e300", &result) == CALC_RANGE);
    assert(calculator_eval("(((((((((((((((((1)))))))))))))))))", &result) == CALC_RANGE);
    char huge[65]; memset(huge, '1', 64); huge[64] = 0;
    assert(calculator_eval(huge, &result) == CALC_TOO_LONG);
    calculator_t calc = {0};
    const char *entry = "(2+.5)*4";
    for (const char *c = entry; *c; c++) calculator_input(&calc, *c);
    calculator_equals(&calc);
    assert(!strcmp(calc.result, "10"));
    calculator_input(&calc, '/'); calculator_input(&calc, '2'); calculator_equals(&calc);
    assert(!strcmp(calc.result, "5"));
    calculator_input(&calc, '3'); assert(!strcmp(calc.input, "3"));
    calculator_input(&calc, '/'); calculator_input(&calc, '0'); calculator_equals(&calc);
    assert(calc.error == CALC_DIV_ZERO);
    calculator_backspace(&calc); calculator_input(&calc, '2'); calculator_equals(&calc);
    assert(!strcmp(calc.result, "1.5"));
    calculator_input(&calc, '.'); assert(!strcmp(calc.input, "."));
    calculator_clear(&calc);
    for (int i = 0; i < CALC_INPUT_MAX + 10; i++) calculator_input(&calc, '8');
    assert(strlen(calc.input) == CALC_INPUT_MAX && calc.error == CALC_TOO_LONG);
    calculator_backspace(&calc); assert(calc.error == CALC_OK);
    calculator_clear(&calc); assert(!calc.input[0] && !calc.result[0] && !calc.evaluated);
    /* Bounded parser rejects malformed edits without overread/recursive blow-up. */
    uint32_t random = 55;
    for (int sample = 0; sample < 3000; sample++) {
        char input[64];
        for (int i = 0; i < 63; i++) {
            random = random * 1664525U + 1013904223U;
            input[i] = "012.()+-*/e "[random % 12];
        }
        input[63] = 0;
        calculator_eval(input, &result);
    }
}
static void calendar_tests(void) {
    assert(calendar_days(2000, 2) == 29 && calendar_days(2024, 2) == 29);
    assert(calendar_days(1900, 2) == 28 && calendar_days(2100, 2) == 28);
    assert(!calendar_days(0, 1) && !calendar_days(10000, 1) && !calendar_days(2024, 13));
    assert(calendar_weekday(2000, 1, 1) == 6);
    assert(calendar_weekday(2024, 2, 29) == 4);
    assert(calendar_weekday(2026, 10, 1) == 4);
    assert(calendar_weekday(1900, 2, 29) == -1);
    /* Whole Gregorian 400-year cycle, including all century boundaries. */
    int weekday = 6;
    for (int year = 2000; year < 2400; year++)
        for (int month = 1; month <= 12; month++)
            for (int day = 1; day <= calendar_days(year, month); day++) {
                assert(calendar_weekday(year, month, day) == weekday);
                weekday = (weekday + 1) % 7;
            }
    assert(weekday == 6);
    calendar_date_t date = {2024, 1, 31};
    assert(calendar_shift(&date, 1) && date.month == 2 && date.day == 29);
    assert(calendar_shift(&date, 12) && date.year == 2025 && date.day == 28);
    assert(calendar_shift(&date, -14) && date.year == 2023 && date.month == 12);
    date = (calendar_date_t){1,1,1};
    assert(!calendar_shift(&date, -1) && date.year == 1 && date.month == 1);
    date = (calendar_date_t){9999,12,31};
    assert(!calendar_shift(&date, 1) && !calendar_shift(&date, INT_MAX));
    assert(!calendar_shift(&date, INT_MIN));
    assert(calendar_today(1735688700, 30, true, &date));
    assert(date.year == 2025 && date.month == 1 && date.day == 1);
    assert(calendar_today(1735689900, -30, true, &date));
    assert(date.year == 2024 && date.month == 12 && date.day == 31);
    calendar_date_t before = date;
    assert(!calendar_today(0, 480, false, &date) && !memcmp(&date, &before, sizeof(date)));
    assert(!calendar_today(0, 841, true, &date));
}
static unsigned occupied(const game2048_t *game) {
    unsigned count = 0;
    for (int i = 0; i < 16; i++) count += game->cells[i] != 0;
    return count;
}
static uint64_t sum(const game2048_t *game) {
    uint64_t total = 0;
    for (int i = 0; i < 16; i++) if (game->cells[i]) total += UINT64_C(1) << game->cells[i];
    return total;
}
static void game_tests(void) {
    game2048_t game;
    game2048_start(&game, 0);
    assert(occupied(&game) == 2 && !game.won && !game.over && !game.score && game.random);
    game = (game2048_t){.cells = {1,1,1,1}, .random = 11};
    assert(game2048_move(&game, GAME_LEFT));
    assert(game.cells[0] == 2 && game.cells[1] == 2 && game.score == 8 && occupied(&game) == 3);
    game = (game2048_t){.cells = {1,1,2,0}, .random = 12};
    assert(game2048_move(&game, GAME_LEFT));
    assert(game.cells[0] == 2 && game.cells[1] == 2 && game.score == 4);
    game = (game2048_t){.cells = {2,0,2,2}, .random = 13};
    assert(game2048_move(&game, GAME_RIGHT));
    assert(game.cells[3] == 3 && game.cells[2] == 2 && game.score == 8);
    game = (game2048_t){.cells = {1,0,0,0,1,0,0,0,2}, .random = 14};
    assert(game2048_move(&game, GAME_UP));
    assert(game.cells[0] == 2 && game.cells[4] == 2 && game.score == 4);
    game = (game2048_t){.cells = {2,0,0,0,2,0,0,0,2}, .random = 15};
    assert(game2048_move(&game, GAME_DOWN));
    assert(game.cells[12] == 3 && game.cells[8] == 2 && game.score == 8);
    game = (game2048_t){.cells = {1,2,3,4}, .random = 16};
    game2048_t before = game;
    assert(!game2048_move(&game, GAME_LEFT) && !memcmp(&game, &before, sizeof(game)));
    game = (game2048_t){.cells = {1,2,1,2,2,1,2,1,1,2,1,2,2,1,2,1}, .random = 17};
    assert(!game2048_can_move(&game));
    assert(!game2048_move(&game, GAME_LEFT) && game.over && game.random == 17);
    game.cells[1] = 1; game.over = false;
    assert(game2048_can_move(&game) && game2048_move(&game, GAME_LEFT));
    game = (game2048_t){.cells = {10,10}, .random = 18};
    assert(game2048_move(&game, GAME_LEFT) && game.won && !game.continued && game.score == 2048);
    before = game;
    assert(!game2048_move(&game, GAME_RIGHT) && !memcmp(&game, &before, sizeof(game)));
    game2048_continue(&game);
    assert(game.continued && game2048_move(&game, GAME_RIGHT));
    game2048_start(&game, 19);
    assert(!game.won && !game.continued && !game.over && !game.score && occupied(&game) == 2);
    game = (game2048_t){.cells = {30,30}, .score = UINT32_MAX - 1, .random = 20};
    assert(game2048_move(&game, GAME_LEFT) && game.cells[0] == 31 && game.score == UINT32_MAX);
    game2048_start(&game, 21);
    for (int i = 0; i < 5000; i++) {
        if (game.over) game2048_start(&game, i + 1);
        if (game.won) game2048_continue(&game);
        uint64_t old_sum = sum(&game);
        uint32_t random = game.random;
        bool changed = game2048_move(&game, (game_direction_t)(i % 4));
        uint64_t delta = sum(&game) - old_sum;
        assert(changed ? (delta == 2 || delta == 4) : (delta == 0 && random == game.random));
        assert(game.over == !game2048_can_move(&game));
    }
    assert(sizeof(game2048_t) < 128 && sizeof(calculator_t) < 128);
}
int main(void) {
    calculator_tests(); calendar_tests(); game_tests();
    puts("Built-in app logic passed: bounded calculator parser/edit/errors, full Gregorian cycle/timezones, 2048 merges/spawn/no-op/win/continue/gameover/restart and score bounds.");
    return 0;
}
