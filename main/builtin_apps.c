#include "builtin_apps.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* At most 16 nested parentheses/unary operators plus the terminal number. */
typedef struct { const char *p; calc_error_t error; int depth; } parser_t;
static void spaces(parser_t *p) { while (*p->p == ' ') p->p++; }
static double expression(parser_t *p);
static double factor(parser_t *p) {
    spaces(p);
    if (++p->depth > 17) { p->error = CALC_RANGE; p->depth--; return 0; }
    double value = 0;
    if (*p->p == '+' || *p->p == '-') {
        bool negative = *p->p++ == '-';
        value = factor(p);
        if (negative) value = -value;
    } else if (*p->p == '(') {
        p->p++;
        value = expression(p);
        spaces(p);
        if (*p->p == ')') p->p++;
        else if (!p->error) p->error = CALC_SYNTAX;
    } else {
        const char *start = p->p;
        int digits = 0;
        while (isdigit((unsigned char)*p->p)) { p->p++; digits++; }
        if (*p->p == '.') {
            p->p++;
            while (isdigit((unsigned char)*p->p)) { p->p++; digits++; }
        }
        if (!digits) { p->error = CALC_SYNTAX; }
        else {
            /* Scientific notation also permits chaining large/small results. */
            if (*p->p == 'e' || *p->p == 'E') {
                p->p++;
                if (*p->p == '+' || *p->p == '-') p->p++;
                if (!isdigit((unsigned char)*p->p)) p->error = CALC_SYNTAX;
                while (isdigit((unsigned char)*p->p)) p->p++;
            }
            value = strtod(start, NULL);
        }
    }
    if (!isfinite(value) && !p->error) p->error = CALC_RANGE;
    p->depth--;
    return value;
}
static double product(parser_t *p) {
    double value = factor(p);
    spaces(p);
    while (!p->error && (*p->p == '*' || *p->p == '/')) {
        char op = *p->p++;
        double rhs = factor(p);
        if (op == '/' && rhs == 0 && !p->error) p->error = CALC_DIV_ZERO;
        if (p->error) break;
        value = op == '*' ? value * rhs : value / rhs;
        if (!isfinite(value)) p->error = CALC_RANGE;
        spaces(p);
    }
    return value;
}
static double expression(parser_t *p) {
    double value = product(p);
    spaces(p);
    while (!p->error && (*p->p == '+' || *p->p == '-')) {
        char op = *p->p++;
        double rhs = product(p);
        value = op == '+' ? value + rhs : value - rhs;
        if (!isfinite(value) && !p->error) p->error = CALC_RANGE;
        spaces(p);
    }
    return value;
}
calc_error_t calculator_eval(const char *input, double *result) {
    if (!input || !result) return CALC_SYNTAX;
    size_t length = 0;
    while (length <= CALC_INPUT_MAX && input[length]) length++;
    if (length > CALC_INPUT_MAX) return CALC_TOO_LONG;
    parser_t parser = {.p = input};
    double value = expression(&parser);
    spaces(&parser);
    if (*parser.p && !parser.error) parser.error = CALC_SYNTAX;
    if (!parser.error) *result = value == 0 ? 0 : value;
    return parser.error;
}
void calculator_clear(calculator_t *calc) { memset(calc, 0, sizeof(*calc)); }
void calculator_input(calculator_t *calc, char ch) {
    if (!ch || !strchr("0123456789.+-*/()", ch)) return;
    if (calc->evaluated) {
        bool chain = !calc->error && strchr("+-*/", ch);
        if (chain) snprintf(calc->input, sizeof(calc->input), "%s", calc->result);
        else calc->input[0] = 0;
        calc->evaluated = false;
    }
    calc->error = CALC_OK;
    calc->result[0] = 0;
    size_t n = strlen(calc->input);
    if (n == CALC_INPUT_MAX) calc->error = CALC_TOO_LONG;
    else { calc->input[n] = ch; calc->input[n + 1] = 0; }
}
void calculator_backspace(calculator_t *calc) {
    size_t n = strlen(calc->input);
    if (n) calc->input[n - 1] = 0;
    calc->result[0] = 0;
    calc->evaluated = false;
    calc->error = CALC_OK;
}
void calculator_equals(calculator_t *calc) {
    double value;
    calc->error = calculator_eval(calc->input, &value);
    calc->evaluated = true;
    calc->result[0] = 0;
    if (!calc->error) snprintf(calc->result, sizeof(calc->result), "%.12g", value);
}

int calendar_days(int year, int month) {
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (year < 1 || year > 9999 || month < 1 || month > 12) return 0;
    return days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 || year % 400 == 0));
}
int calendar_weekday(int year, int month, int day) {
    static const int offsets[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (day < 1 || day > calendar_days(year, month)) return -1;
    year -= month < 3;
    return (year + year / 4 - year / 100 + year / 400 + offsets[month - 1] + day) % 7;
}
bool calendar_shift(calendar_date_t *date, int months) {
    if (!date || !calendar_days(date->year, date->month)) return false;
    int64_t total = (int64_t)(date->year - 1) * 12 + date->month - 1 + (int64_t)months;
    if (total < 0 || total >= 9999 * 12) return false;
    date->year = (int)(total / 12) + 1;
    date->month = (int)(total % 12) + 1;
    int days = calendar_days(date->year, date->month);
    if (date->day > days) date->day = days;
    if (date->day < 1) date->day = 1;
    return true;
}
bool calendar_today(time_t utc, int offset_minutes, bool valid, calendar_date_t *date) {
    if (!valid || !date || offset_minutes < -720 || offset_minutes > 840) return false;
    /* Supported calendar years fit safely in signed 64-bit time_t on ESP-IDF. */
    int64_t seconds = (int64_t)utc;
    if (seconds < -62135647200LL || seconds > 253402351200LL) return false;
    time_t shifted = (time_t)(seconds + (int64_t)offset_minutes * 60);
    struct tm tm;
    if (!gmtime_r(&shifted, &tm) || !calendar_days(tm.tm_year + 1900, tm.tm_mon + 1)) return false;
    *date = (calendar_date_t){tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday};
    return true;
}

static uint32_t next_random(game2048_t *game) {
    uint32_t x = game->random;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    game->random = x;
    return x;
}
static void spawn(game2048_t *game) {
    uint8_t empty[16];
    int count = 0;
    for (int i = 0; i < 16; i++) if (!game->cells[i]) empty[count++] = i;
    if (!count) return;
    int index = empty[next_random(game) % (unsigned)count];
    game->cells[index] = next_random(game) % 10 == 0 ? 2 : 1;
}
bool game2048_can_move(const game2048_t *game) {
    for (int i = 0; i < 16; i++) {
        uint8_t value = game->cells[i];
        if (!value) return true;
        if (value < 31 && ((i % 4 < 3 && value == game->cells[i + 1]) ||
                          (i < 12 && value == game->cells[i + 4]))) return true;
    }
    return false;
}
void game2048_start(game2048_t *game, uint32_t seed) {
    memset(game, 0, sizeof(*game));
    game->random = seed ? seed : 0x6d2b79f5;
    spawn(game); spawn(game);
}
static int cell_index(game_direction_t direction, int line, int index) {
    if (direction == GAME_LEFT) return line * 4 + index;
    if (direction == GAME_RIGHT) return line * 4 + 3 - index;
    if (direction == GAME_UP) return index * 4 + line;
    return (3 - index) * 4 + line;
}
bool game2048_move(game2048_t *game, game_direction_t direction) {
    if (direction < GAME_LEFT || direction > GAME_DOWN || game->over ||
        (game->won && !game->continued)) return false;
    bool changed = false;
    for (int line = 0; line < 4; line++) {
        uint8_t compact[4] = {0}, output[4] = {0};
        int count = 0, out = 0;
        for (int i = 0; i < 4; i++) {
            uint8_t value = game->cells[cell_index(direction, line, i)];
            if (value) compact[count++] = value;
        }
        for (int i = 0; i < count; i++) {
            uint8_t value = compact[i];
            if (i + 1 < count && value < 31 && value == compact[i + 1]) {
                value++; i++;
                uint32_t points = UINT32_C(1) << value;
                game->score = game->score > UINT32_MAX - points ? UINT32_MAX : game->score + points;
            }
            if (value >= 11) game->won = true;
            output[out++] = value;
        }
        for (int i = 0; i < 4; i++) {
            int index = cell_index(direction, line, i);
            if (game->cells[index] != output[i]) changed = true;
            game->cells[index] = output[i];
        }
    }
    if (changed) spawn(game);
    game->over = !game2048_can_move(game);
    return changed;
}
void game2048_continue(game2048_t *game) { if (game->won) game->continued = true; }
