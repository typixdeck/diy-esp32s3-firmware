#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define CALC_INPUT_MAX 63
/* Fixed-size state; these applications perform no allocation or I/O. */
typedef enum { CALC_OK, CALC_SYNTAX, CALC_DIV_ZERO, CALC_RANGE, CALC_TOO_LONG } calc_error_t;
typedef struct {
    char input[CALC_INPUT_MAX + 1];
    char result[32];
    calc_error_t error;
    bool evaluated;
} calculator_t;
calc_error_t calculator_eval(const char *expression, double *result);
void calculator_clear(calculator_t *calc);
void calculator_input(calculator_t *calc, char ch);
void calculator_backspace(calculator_t *calc);
void calculator_equals(calculator_t *calc);

typedef struct { int year, month, day; } calendar_date_t;
int calendar_days(int year, int month);
int calendar_weekday(int year, int month, int day); /* Sunday = 0; invalid = -1 */
bool calendar_shift(calendar_date_t *date, int months);
bool calendar_today(time_t utc, int offset_minutes, bool valid, calendar_date_t *date);

typedef enum { GAME_LEFT, GAME_RIGHT, GAME_UP, GAME_DOWN } game_direction_t;
typedef struct {
    uint8_t cells[16]; /* powers of two; zero is empty; maximum exponent 31 */
    uint32_t score, random;
    bool won, continued, over;
} game2048_t;
void game2048_start(game2048_t *game, uint32_t seed);
bool game2048_move(game2048_t *game, game_direction_t direction);
bool game2048_can_move(const game2048_t *game);
void game2048_continue(game2048_t *game);
