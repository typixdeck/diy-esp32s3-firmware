#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PI_LINK_SHUTDOWN_NONE, PI_LINK_SHUTDOWN_PENDING,
    PI_LINK_SHUTDOWN_ACCEPTED, PI_LINK_SHUTDOWN_DENIED,
    PI_LINK_SHUTDOWN_FAILED, PI_LINK_SHUTDOWN_TIMEOUT,
    PI_LINK_SHUTDOWN_DISCONNECTED,
} pi_link_shutdown_result_t;

typedef struct {
    bool online, can_shutdown, shutdown_pending;
    uint8_t transport; /* 0 none, 1 CDC, 2 paired HTTPS */
    char ip[16];
    int32_t mem_mib, disk_mib, load100;
    pi_link_shutdown_result_t shutdown_result;
    uint32_t uptime_s, age_ms;
    int32_t cpu_millicelsius, cpu_khz; /* -1 = unknown; no HDMI FPS claim */
} pi_link_snapshot_t;

void pi_link_init(void);
void pi_link_receive(const char *line);
/* Includes newline. Only the existing CDC writer should drain this queue. */
bool pi_link_take_tx(char *buffer, size_t capacity);
bool pi_link_request_shutdown(void);
void pi_link_get_snapshot(pi_link_snapshot_t *out);
