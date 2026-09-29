#include "pi_link.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PI_LINK_HOST_TEST
extern uint64_t pi_link_test_now_ms(void);
extern uint32_t pi_link_test_random(void);
#define now_ms pi_link_test_now_ms
#define random_word pi_link_test_random
static void lock(void) {}
static void unlock(void) {}
#else
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static SemaphoreHandle_t s_mutex;
static uint64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static uint32_t random_word(void) { return esp_random(); }
static void lock(void) { xSemaphoreTake(s_mutex, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mutex); }
#endif

#define LINK_FRESH_MS 8000
#define COMMAND_TIMEOUT_MS 15000
static bool s_ready, s_seen, s_used;
static char s_host[17], s_device[17], s_tx[160];
static uint64_t s_last, s_requested, s_sys_last;
static bool s_sys_seen;
static uint32_t s_sequence;
static pi_link_snapshot_t s_status;

static bool hex_nonce(const char *value)
{
    if (strlen(value) != 16) return false;
    for (unsigned i = 0; i < 16; ++i)
        if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) return false;
    return true;
}

static bool number(const char *value, int64_t minimum, int64_t maximum, int64_t *out)
{
    if (!*value) return false;
    const char *p = value;
    if (*p == '-') ++p;
    if (!*p) return false;
    for (; *p; ++p) if (*p < '0' || *p > '9') return false;
    if (strlen(value) > 11) return false;
    int64_t parsed = strtoll(value, NULL, 10);
    if (parsed < minimum || parsed > maximum) return false;
    *out = parsed;
    return true;
}

static bool ipv4(const char *s) {
    if (!strcmp(s, "-")) return true;
    unsigned a,b,c,d; char end;
    return strlen(s) < 16 && sscanf(s, "%u.%u.%u.%u%c", &a,&b,&c,&d,&end) == 4 &&
           a < 256 && b < 256 && c < 256 && d < 256;
}
static void refresh(void)
{
    uint64_t now = now_ms();
    uint64_t age = s_seen ? now - s_last : UINT32_MAX;
    s_status.age_ms = age > UINT32_MAX ? UINT32_MAX : (uint32_t)age;
    s_status.online = s_seen && age < LINK_FRESH_MS;
    s_status.transport = s_status.online ? 1 : 0;
    if (!s_sys_seen || now-s_sys_last >= LINK_FRESH_MS) {
        s_status.ip[0]=0;
        s_status.mem_mib=s_status.disk_mib=s_status.load100=-1;
    }
    if (s_status.shutdown_pending && now - s_requested >= COMMAND_TIMEOUT_MS) {
        s_status.shutdown_pending = false;
        s_status.shutdown_result = PI_LINK_SHUTDOWN_TIMEOUT;
        s_tx[0] = '\0'; /* An unsent request must never escape after timeout. */
    }
}

void pi_link_init(void)
{
    if (s_ready) return;
#ifndef PI_LINK_HOST_TEST
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return;
#endif
    s_status.cpu_millicelsius = s_status.cpu_khz = -1;
    s_status.mem_mib = s_status.disk_mib = s_status.load100 = -1;
    s_status.ip[0] = 0;
    s_ready = true;
}

void pi_link_receive(const char *line)
{
    if (!s_ready || !line || strncmp(line, "TD1 ", 4) || strlen(line) >= 159) return;
    char copy[160];
    strcpy(copy, line);
    char *tokens[11], *save = NULL;
    unsigned count = 0;
    for (char *p = strtok_r(copy, " ", &save); p; p = strtok_r(NULL, " ", &save)) {
        if (count >= 11) return;
        tokens[count++] = p;
    }
    if (count < 3) return;
    lock();
    refresh();
    if (count == 3 && !strcmp(tokens[1], "HELLO") && hex_nonce(tokens[2])) {
        if (strcmp(s_host, tokens[2])) {
            s_status.shutdown_result = s_status.shutdown_pending
                ? PI_LINK_SHUTDOWN_DISCONNECTED : PI_LINK_SHUTDOWN_NONE;
            s_status.shutdown_pending = false;
            s_tx[0] = '\0';
            strcpy(s_host, tokens[2]);
            snprintf(s_device, sizeof(s_device), "%08" PRIx32 "%08" PRIx32, random_word(), random_word());
            s_sequence = 0;
            s_seen = s_used = false;
            s_sys_seen=false;
            s_status.can_shutdown = false;
            s_status.uptime_s = 0;
            s_status.cpu_millicelsius = s_status.cpu_khz = -1;
    s_status.mem_mib = s_status.disk_mib = s_status.load100 = -1;
    s_status.ip[0] = 0;
        }
        /* Never replace an already queued command on a repeated HELLO. */
        if (!s_tx[0]) snprintf(s_tx, sizeof(s_tx), "TD1 WELCOME %s %s\n", s_host, s_device);
    } else if (count >= 4 && !strcmp(tokens[2], s_host) && !strcmp(tokens[3], s_device) && s_host[0]) {
        if (count == 9 && !strcmp(tokens[1], "HB")) {
            int64_t seq, caps, uptime, temperature, frequency;
            if (number(tokens[4], 1, UINT32_MAX, &seq) && (uint32_t)seq > s_sequence &&
                number(tokens[5], 0, 1, &caps) && number(tokens[6], 0, UINT32_MAX, &uptime) &&
                number(tokens[7], -1, 200000, &temperature) && number(tokens[8], -1, 10000000, &frequency)) {
                s_sequence = seq;
                s_seen = true;
                s_last = now_ms();
                s_status.can_shutdown = caps == 1;
                s_status.uptime_s = uptime;
                s_status.cpu_millicelsius = temperature;
                s_status.cpu_khz = frequency;
            }
        } else if (count == 9 && !strcmp(tokens[1], "SYS") && s_status.online) {
            int64_t seq, mem, disk, load;
            if (number(tokens[4], 1, UINT32_MAX, &seq) && (uint32_t)seq == s_sequence &&
                ipv4(tokens[5]) && number(tokens[6], -1, 1048576, &mem) &&
                number(tokens[7], -1, INT32_MAX, &disk) && number(tokens[8], -1, 1000000, &load)) {
                strcpy(s_status.ip, !strcmp(tokens[5], "-") ? "" : tokens[5]);
                s_sys_seen=true; s_sys_last=now_ms();
                s_status.mem_mib = mem; s_status.disk_mib = disk; s_status.load100 = load;
            }
        } else if (count == 6 && !strcmp(tokens[1], "ACK") && !strcmp(tokens[4], "1") && s_status.shutdown_pending) {
            pi_link_shutdown_result_t result = PI_LINK_SHUTDOWN_NONE;
            if (!strcmp(tokens[5], "ACCEPTED")) result = PI_LINK_SHUTDOWN_ACCEPTED;
            if (!strcmp(tokens[5], "DENIED")) result = PI_LINK_SHUTDOWN_DENIED;
            if (!strcmp(tokens[5], "FAILED")) result = PI_LINK_SHUTDOWN_FAILED;
            if (result != PI_LINK_SHUTDOWN_NONE) {
                s_status.shutdown_result = result;
                s_status.shutdown_pending = false;
                s_tx[0] = '\0';
            }
        }
    }
    refresh();
    unlock();
}

bool pi_link_take_tx(char *buffer, size_t capacity)
{
    if (!s_ready || !buffer) return false;
    lock();
    refresh();
    bool available = s_tx[0] && strlen(s_tx) + 1 <= capacity;
    if (available) { strcpy(buffer, s_tx); s_tx[0] = '\0'; }
    unlock();
    return available;
}

bool pi_link_request_shutdown(void)
{
    if (!s_ready) return false;
    lock();
    refresh();
    bool allowed = s_status.online && s_status.can_shutdown && !s_used && !s_tx[0];
    if (allowed) {
        snprintf(s_tx, sizeof(s_tx), "TD1 CMD %s %s 1 SHUTDOWN\n", s_host, s_device);
        s_used = true;
        s_requested = now_ms();
        s_status.shutdown_pending = true;
        s_status.shutdown_result = PI_LINK_SHUTDOWN_PENDING;
    }
    unlock();
    return allowed;
}

void pi_link_get_snapshot(pi_link_snapshot_t *out)
{
    if (!out) return;
    if (!s_ready) { memset(out, 0, sizeof(*out)); out->cpu_millicelsius = out->cpu_khz = -1; return; }
    lock();
    refresh();
    *out = s_status;
    out->can_shutdown = out->can_shutdown && out->online && !s_used;
    unlock();
}
