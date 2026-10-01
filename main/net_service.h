// Asynchronous, optional ESP-only Wi-Fi and clock service. No Pi dependency.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#define NET_MAX_APS 8
#define NET_SSID_SIZE 33
#define NET_NTP_SERVER_SIZE 64

typedef enum {
    NET_OFF, NET_STARTING, NET_IDLE, NET_SCANNING,
    NET_CONNECTING, NET_CONNECTED, NET_ERROR
} net_state_t;

typedef enum {
    NET_ERROR_NONE, NET_ERROR_INIT, NET_ERROR_WIFI, NET_ERROR_TIMEOUT,
    NET_ERROR_AUTH, NET_ERROR_OFFLINE, NET_ERROR_STORAGE
} net_error_t;

typedef struct {
    char ssid[NET_SSID_SIZE];
    int8_t rssi;
    bool secured;
} net_ap_t;

typedef struct {
    net_state_t state;
    net_error_t last_error;
    uint8_t init_stage; // 0 not attempted, 1..10 SDK initialization steps, 11 ready
    esp_err_t init_error; // exact SDK error; no network identifiers
    bool enabled;
    bool connected;
    bool saved_network;
    char ssid[NET_SSID_SIZE];
    char ip[16];
    uint8_t ap_count;
    net_ap_t aps[NET_MAX_APS];
    bool ntp_busy;
    bool time_valid;
    int64_t last_sync;
    int timezone_offset_minutes; // UTC offset, -720..840; no implicit DST.
    char ntp_server[NET_NTP_SERVER_SIZE];
} net_snapshot_t;

// Call after application NVS initialization. No NVS erase/recovery is done here.
// Return value reports task creation; snapshot reports asynchronous setup errors.
esp_err_t net_service_init(void);
void net_service_get_snapshot(net_snapshot_t *out);
// Bounded fixed numeric fields only: no SSID, password, IP or MAC.
size_t net_service_format_diagnostics(char *out, size_t capacity);

// Commands are queued, never block the UI, and return false for invalid input,
// uninitialized service, or a full queue. Completion is visible in the snapshot.
// First boot defaults to Wi-Fi available (no embedded credentials), UTC+08:00.
// Explicit off and all saved preferences survive reboot. Transient saved-network
// failures retry with 5..60s backoff; auth failure/off/cancel stop automatic retry.
bool net_service_set_enabled(bool enabled);
bool net_service_scan(void);
bool net_service_connect(const char *ssid, const char *password);
// Cancel current scan/connection/NTP attempt. Keeps an established connection.
bool net_service_cancel(void);
// Disconnects and erases the saved network; leaves Wi-Fi available for scanning.
bool net_service_forget(void);
bool net_service_request_ntp(void);
bool net_service_set_time_config(const char *server, int timezone_offset_minutes);
