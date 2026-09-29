#include "net_service.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#define CONNECT_TIMEOUT_US (25LL * 1000000)
#define SCAN_TIMEOUT_US (12LL * 1000000)
#define NTP_TIMEOUT_US (20LL * 1000000)
#define MAX_CONNECT_RETRIES 2
#define CLOCK_EPOCH_MIN 1704067200LL // 2024-01-01; rejects unset Unix epoch.

typedef enum {
    CMD_ENABLE, CMD_SCAN, CMD_CONNECT, CMD_CANCEL, CMD_FORGET,
    CMD_NTP, CMD_TIME_CONFIG
} command_kind_t;

typedef struct {
    command_kind_t kind;
    bool enabled;
    char ssid[NET_SSID_SIZE];
    char password[65];
    char server[NET_NTP_SERVER_SIZE];
    int timezone;
} command_t;

typedef enum { EVENT_DISCONNECT, EVENT_IP, EVENT_SCAN, EVENT_SYNC } event_kind_t;
typedef struct {
    event_kind_t kind;
    uint16_t reason;
    esp_ip4_addr_t ip;
    int64_t epoch;
} event_t;

static QueueHandle_t s_commands;
static QueueHandle_t s_events;
static SemaphoreHandle_t s_lock;
static net_snapshot_t s_snapshot;
// Worker owns these fields and all Wi-Fi/SNTP control operations.
static net_snapshot_t s_work;
static bool s_started;
static bool s_ready;
static bool s_setup_attempted;
static bool s_connecting;
static bool s_scan_active;
static bool s_sntp_active;
static int s_retries;
static int64_t s_connect_deadline;
static int64_t s_scan_deadline;
static int64_t s_ntp_deadline;
static char s_candidate_ssid[NET_SSID_SIZE];
static char s_candidate_password[65];
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;

typedef struct {
    char ssid[NET_SSID_SIZE];
    char password[65];
} saved_network_t;

static void erase_secret(void *memory, size_t size)
{
    volatile unsigned char *p = memory;
    while (size--) *p++ = 0;
}

static void publish(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_snapshot = s_work;
    xSemaphoreGive(s_lock);
}

static void clear_candidate(void)
{
    erase_secret(s_candidate_password, sizeof(s_candidate_password));
    memset(s_candidate_ssid, 0, sizeof(s_candidate_ssid));
}

static void update_state(void)
{
    if (s_work.last_error == NET_ERROR_INIT) { s_work.state = NET_ERROR; return; }
    s_work.state = !s_work.enabled ? NET_OFF : s_scan_active ? NET_SCANNING :
        s_connecting ? NET_CONNECTING : s_work.connected ? NET_CONNECTED : NET_IDLE;
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    event_t event = {0};
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        event.kind = EVENT_DISCONNECT;
        event.reason = ((wifi_event_sta_disconnected_t *)data)->reason;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        event.kind = EVENT_SCAN;
        event.reason = ((wifi_event_sta_scan_done_t *)data)->status;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        event.kind = EVENT_IP;
        event.ip = ((ip_event_got_ip_t *)data)->ip_info.ip;
    } else {
        return;
    }
    // A missed event is recovered by a finite deadline; event task never blocks.
    xQueueSend(s_events, &event, 0);
}

static void time_sync(struct timeval *tv)
{
    event_t event = {.kind = EVENT_SYNC, .epoch = tv->tv_sec};
    xQueueSend(s_events, &event, 0);
}

static bool store_enabled(bool enabled)
{
    nvs_handle_t handle;
    if (nvs_open("deck_net", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_u8(handle, "enabled", enabled);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

static bool save_network(void)
{
    nvs_handle_t handle;
    if (nvs_open("deck_net", NVS_READWRITE, &handle) != ESP_OK) return false;
    saved_network_t network = {0};
    strlcpy(network.ssid, s_candidate_ssid, sizeof(network.ssid));
    strlcpy(network.password, s_candidate_password, sizeof(network.password));
    // One atomic NVS item prevents mixing a new SSID with an old password.
    esp_err_t err = nvs_set_blob(handle, "network", &network, sizeof(network));
    erase_secret(&network, sizeof(network));
    if (err == ESP_OK) err = nvs_set_u8(handle, "enabled", 1);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

static bool load_network(char *ssid, char *password)
{
    nvs_handle_t handle;
    if (nvs_open("deck_net", NVS_READONLY, &handle) != ESP_OK) return false;
    saved_network_t network = {0};
    size_t size = sizeof(network);
    esp_err_t err = nvs_get_blob(handle, "network", &network, &size);
    nvs_close(handle);
    bool valid = err == ESP_OK && size == sizeof(network) && network.ssid[0] &&
        strnlen(network.ssid, sizeof(network.ssid)) < sizeof(network.ssid) &&
        strnlen(network.password, sizeof(network.password)) < sizeof(network.password);
    if (valid) {
        memcpy(ssid, network.ssid, sizeof(network.ssid));
        memcpy(password, network.password, sizeof(network.password));
    }
    erase_secret(&network, sizeof(network));
    if (!valid) {
        erase_secret(password, 65);
        ssid[0] = '\0';
        return false;
    }
    return true;
}

static bool valid_server(const char *server)
{
    if (!server) return false;
    size_t len = strnlen(server, NET_NTP_SERVER_SIZE);
    if (!len || len >= NET_NTP_SERVER_SIZE) return false;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = server[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
    }
    return true;
}

static void stop_ntp(void)
{
    if (s_sntp_active) esp_sntp_stop();
    s_sntp_active = false;
    s_work.ntp_busy = false;
}

static void start_ntp(void)
{
    if (!s_work.connected) {
        s_work.last_error = NET_ERROR_OFFLINE;
        return;
    }
    stop_ntp();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, s_work.ntp_server);
    esp_sntp_set_time_sync_notification_cb(time_sync);
    s_work.ntp_busy = true;
    s_sntp_active = true;
    s_work.last_error = NET_ERROR_NONE;
    s_ntp_deadline = esp_timer_get_time() + NTP_TIMEOUT_US;
    esp_sntp_init();
}

static void stop_scan(void)
{
    if (s_scan_active) {
        esp_wifi_scan_stop();
        esp_wifi_clear_ap_list();
    }
    s_scan_active = false;
}

static void disconnect(void)
{
    s_connecting = false;
    s_work.connected = false;
    s_work.ip[0] = '\0';
    s_work.ssid[0] = '\0';
    stop_scan();
    stop_ntp();
    if (s_started) esp_wifi_disconnect();
    if (s_ready) {
        wifi_config_t blank = {0};
        esp_wifi_set_config(WIFI_IF_STA, &blank);
    }
    clear_candidate();
}

static esp_err_t setup_wifi(void);

static bool enable_wifi(void)
{
    if (s_started) return true;
    // A disabled radio must not compete with LCD/audio/USB for boot-time RAM.
    // A failed partial setup is not retried until restart (no duplicate netifs).
    if (!s_ready && !s_setup_attempted) {
        s_setup_attempted = true;
        s_work.init_error = setup_wifi();
        s_ready = s_work.init_error == ESP_OK;
    }
    if (!s_ready) {
        s_work.last_error = NET_ERROR_INIT;
        return false;
    }
    s_work.init_stage = 10;
    s_work.init_error = esp_wifi_start();
    if (s_work.init_error != ESP_OK) {
        s_work.last_error = NET_ERROR_WIFI;
        return false;
    }
    s_started = true;
    s_work.init_stage = 11;
    s_work.enabled = true;
    return true;
}

static void connect_network(const char *ssid, const char *password)
{
    disconnect();
    if (!enable_wifi()) return;
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid, strlen(ssid));
    memcpy(config.sta.password, password, strlen(password));
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    // WPA credentials stay in RAM until a matching AP has supplied an IP.
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    erase_secret(&config, sizeof(config));
    if (err != ESP_OK) {
        s_work.last_error = NET_ERROR_WIFI;
        return;
    }
    strlcpy(s_candidate_ssid, ssid, sizeof(s_candidate_ssid));
    strlcpy(s_candidate_password, password, sizeof(s_candidate_password));
    strlcpy(s_work.ssid, ssid, sizeof(s_work.ssid));
    s_connecting = true;
    s_retries = 0;
    s_connect_deadline = esp_timer_get_time() + CONNECT_TIMEOUT_US;
    s_work.last_error = NET_ERROR_NONE;
    if (esp_wifi_connect() != ESP_OK) {
        disconnect();
        s_work.last_error = NET_ERROR_WIFI;
    }
}

static void reconnect_saved(void)
{
    char ssid[NET_SSID_SIZE] = {0};
    char password[65] = {0};
    if (load_network(ssid, password)) connect_network(ssid, password);
    erase_secret(password, sizeof(password));
}

static void handle_command(const command_t *cmd)
{
    s_work.last_error = NET_ERROR_NONE;
    switch (cmd->kind) {
    case CMD_ENABLE:
        if (cmd->enabled) {
            if (enable_wifi() && !s_work.connected && !s_connecting) reconnect_saved();
        } else {
            disconnect();
            esp_err_t err = s_started ? esp_wifi_stop() : ESP_OK;
            if (err == ESP_OK) { s_started = false; s_work.enabled = false; }
            else s_work.last_error = NET_ERROR_WIFI;
        }
        if (!store_enabled(s_work.enabled)) s_work.last_error = NET_ERROR_STORAGE;
        break;
    case CMD_SCAN:
        if (!s_work.enabled) { s_work.last_error = NET_ERROR_OFFLINE; break; }
        if (s_connecting) { s_work.last_error = NET_ERROR_WIFI; break; }
        stop_scan();
        s_work.ap_count = 0;
        if (esp_wifi_scan_start(NULL, false) != ESP_OK) {
            s_work.last_error = NET_ERROR_WIFI;
        } else {
            s_scan_active = true;
            s_scan_deadline = esp_timer_get_time() + SCAN_TIMEOUT_US;
        }
        break;
    case CMD_CONNECT:
        connect_network(cmd->ssid, cmd->password);
        break;
    case CMD_CANCEL:
        stop_scan();
        stop_ntp();
        if (s_connecting) disconnect();
        break;
    case CMD_FORGET: {
        disconnect();
        wifi_config_t blank = {0};
        if (s_ready) esp_wifi_set_config(WIFI_IF_STA, &blank);
        nvs_handle_t handle;
        esp_err_t err = nvs_open("deck_net", NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            // Only this service's network keys; never erase system NVS.
            err = nvs_erase_key(handle, "network");
            if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
            if (err == ESP_OK) err = nvs_commit(handle);
            nvs_close(handle);
        }
        if (err == ESP_OK) s_work.saved_network = false;
        else s_work.last_error = NET_ERROR_STORAGE;
        break;
    }
    case CMD_NTP:
        start_ntp();
        break;
    case CMD_TIME_CONFIG: {
        stop_ntp();
        strlcpy(s_work.ntp_server, cmd->server, sizeof(s_work.ntp_server));
        s_work.timezone_offset_minutes = cmd->timezone;
        nvs_handle_t handle;
        esp_err_t err = nvs_open("deck_net", NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            err = nvs_set_str(handle, "ntp", cmd->server);
            if (err == ESP_OK) err = nvs_set_i32(handle, "tz_min", cmd->timezone);
            if (err == ESP_OK) err = nvs_commit(handle);
            nvs_close(handle);
        }
        if (err != ESP_OK) s_work.last_error = NET_ERROR_STORAGE;
        break;
    }
    }
    update_state();
}

static bool auth_failure(uint16_t reason)
{
    return reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
        reason == WIFI_REASON_HANDSHAKE_TIMEOUT;
}

static void handle_event(const event_t *event)
{
    switch (event->kind) {
    case EVENT_DISCONNECT: {
        // A disconnect from the previous attempt can be delivered after a new
        // association. Do not tear down a currently associated connection.
        wifi_ap_record_t current_ap = {0};
        if (esp_wifi_sta_get_ap_info(&current_ap) == ESP_OK) break;
        bool was_connected = s_work.connected;
        s_work.connected = false;
        s_work.ip[0] = '\0';
        stop_ntp();
        if (s_connecting) {
            if (!auth_failure(event->reason) && s_retries++ < MAX_CONNECT_RETRIES &&
                esp_timer_get_time() < s_connect_deadline && esp_wifi_connect() == ESP_OK) break;
            disconnect();
            s_work.last_error = auth_failure(event->reason) ? NET_ERROR_AUTH : NET_ERROR_WIFI;
        }
        if (was_connected) s_work.last_error = NET_ERROR_WIFI;
        break;
    }
    case EVENT_IP: {
        if (!s_started || !s_work.enabled || (!s_connecting && !s_work.connected)) break;
        wifi_ap_record_t ap = {0};
        const char *expected_ssid = s_connecting ? s_candidate_ssid : s_work.ssid;
        if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK ||
            strncmp((const char *)ap.ssid, expected_ssid, 32) != 0) break;
        snprintf(s_work.ip, sizeof(s_work.ip), IPSTR, IP2STR(&event->ip));
        if (!s_connecting) break; // DHCP renewal: update IP, do not resave credentials.
        s_connecting = false;
        s_work.connected = true;
        if (save_network()) s_work.saved_network = true;
        else s_work.last_error = NET_ERROR_STORAGE;
        clear_candidate();
        // First connection performs one bounded clock sync; no forever retry loop.
        net_error_t connection_error = s_work.last_error;
        start_ntp();
        if (connection_error == NET_ERROR_STORAGE) s_work.last_error = connection_error;
        break;
    }
    case EVENT_SCAN: {
        if (!s_scan_active) { esp_wifi_clear_ap_list(); break; }
        s_scan_active = false;
        wifi_ap_record_t records[NET_MAX_APS] = {0};
        uint16_t count = NET_MAX_APS;
        if (event->reason || esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) {
            s_work.last_error = NET_ERROR_WIFI;
            esp_wifi_clear_ap_list();
            break;
        }
        s_work.ap_count = 0;
        for (uint16_t i = 0; i < count; ++i) {
            if (!records[i].ssid[0]) continue;
            bool duplicate = false;
            for (uint8_t j = 0; j < s_work.ap_count; ++j)
                if (strncmp(s_work.aps[j].ssid, (const char *)records[i].ssid, 32) == 0)
                    duplicate = true;
            if (duplicate) continue;
            net_ap_t *ap = &s_work.aps[s_work.ap_count++];
            memcpy(ap->ssid, records[i].ssid, 32);
            ap->ssid[32] = '\0';
            ap->rssi = records[i].rssi;
            ap->secured = records[i].authmode != WIFI_AUTH_OPEN;
        }
        break;
    }
    case EVENT_SYNC:
        if (!s_work.ntp_busy || event->epoch < CLOCK_EPOCH_MIN) break;
        s_work.last_sync = event->epoch;
        s_work.time_valid = true;
        stop_ntp();
        break;
    }
    update_state();
}

static esp_err_t setup_wifi(void)
{
    // Avoid the driver's informational connection records (SSID/MAC).
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);
    s_work.init_stage = 1;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    s_work.init_stage = 2;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    s_work.init_stage = 3;
    if (!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    s_work.init_stage = 4;
    err = esp_wifi_init(&config);
    if (err != ESP_OK) return err;
    s_work.init_stage = 5;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
        wifi_event, NULL, &s_wifi_handler);
    if (err != ESP_OK) return err;
    s_work.init_stage = 6;
    err = esp_event_handler_instance_register(IP_EVENT,
        IP_EVENT_STA_GOT_IP, wifi_event, NULL, &s_ip_handler);
    if (err != ESP_OK) return err;
    s_work.init_stage = 7;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;
    s_work.init_stage = 8;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    s_work.init_stage = 9;
    err = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    return err;
}

static void check_deadlines(int64_t now)
{
    if (s_connecting && now >= s_connect_deadline) {
        disconnect();
        s_work.last_error = NET_ERROR_TIMEOUT;
    }
    if (s_scan_active && now >= s_scan_deadline) {
        stop_scan();
        s_work.last_error = NET_ERROR_TIMEOUT;
    }
    if (s_work.ntp_busy && now >= s_ntp_deadline) {
        stop_ntp();
        s_work.last_error = NET_ERROR_TIMEOUT;
    }
}

static void load_preferences(void)
{
    s_work = s_snapshot;
    nvs_handle_t handle;
    uint8_t enabled = 0;
    if (nvs_open("deck_net", NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u8(handle, "enabled", &enabled);
        int32_t tz = 0;
        if (nvs_get_i32(handle, "tz_min", &tz) == ESP_OK && tz >= -720 && tz <= 840)
            s_work.timezone_offset_minutes = tz;
        char server[NET_NTP_SERVER_SIZE] = {0};
        size_t size = sizeof(server);
        if (nvs_get_str(handle, "ntp", server, &size) == ESP_OK && valid_server(server))
            strlcpy(s_work.ntp_server, server, sizeof(s_work.ntp_server));
        size = 0;
        s_work.saved_network = nvs_get_blob(handle, "network", NULL, &size) == ESP_OK &&
            size == sizeof(saved_network_t);
        nvs_close(handle);
    }
    if (enabled && enable_wifi()) reconnect_saved();
    update_state();
    publish();
}

static void network_task(void *arg)
{
    (void)arg;
    load_preferences();
    for (;;) {
        command_t *command = NULL;
        if (xQueueReceive(s_commands, &command, pdMS_TO_TICKS(50))) {
            handle_command(command);
            erase_secret(command, sizeof(*command));
            free(command);
        }
        event_t event;
        while (xQueueReceive(s_events, &event, 0)) handle_event(&event);
        check_deadlines(esp_timer_get_time());
        s_work.time_valid = (int64_t)time(NULL) >= CLOCK_EPOCH_MIN;
        update_state();
        publish();
    }
}

esp_err_t net_service_init(void)
{
    if (s_commands) return ESP_ERR_INVALID_STATE;
    s_lock = xSemaphoreCreateMutex();
    // Queue pointers, not command bodies: FreeRTOS leaves consumed slots intact.
    // The separate command allocation is explicitly wiped immediately after use.
    s_commands = xQueueCreate(4, sizeof(command_t *));
    s_events = xQueueCreate(12, sizeof(event_t));
    if (!s_lock || !s_commands || !s_events) goto failed;
    s_snapshot = (net_snapshot_t){.state = NET_STARTING};
    strlcpy(s_snapshot.ntp_server, "pool.ntp.org", sizeof(s_snapshot.ntp_server));
    if (xTaskCreate(network_task, "deck_net", 6144, NULL, 3, NULL) != pdPASS) goto failed;
    return ESP_OK;
failed:
    if (s_events) vQueueDelete(s_events);
    if (s_commands) vQueueDelete(s_commands);
    if (s_lock) vSemaphoreDelete(s_lock);
    s_events = NULL;
    s_commands = NULL;
    s_lock = NULL;
    return ESP_ERR_NO_MEM;
}

void net_service_get_snapshot(net_snapshot_t *out)
{
    if (!out) return;
    if (!s_lock) { *out = (net_snapshot_t){.state = NET_OFF}; return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_snapshot;
    xSemaphoreGive(s_lock);
}

size_t net_service_format_diagnostics(char *out, size_t capacity)
{
    if (!out || !capacity) return 0;
    net_snapshot_t n;
    net_service_get_snapshot(&n);
    int count = snprintf(out, capacity,
        "TD_NET v=1 state=%u enabled=%u connected=%u aps=%u error=%u stage=%u sdk_err=%d\r\n",
        (unsigned)n.state, n.enabled, n.connected, n.ap_count,
        (unsigned)n.last_error, n.init_stage, (int)n.init_error);
    return count < 0 ? 0 : (size_t)count < capacity ? (size_t)count : capacity - 1;
}

static bool submit(command_t *command)
{
    command_t *queued = s_commands ? malloc(sizeof(*queued)) : NULL;
    if (queued) *queued = *command;
    erase_secret(command, sizeof(*command));
    bool accepted = queued && xQueueSend(s_commands, &queued, 0) == pdTRUE;
    if (!accepted && queued) {
        erase_secret(queued, sizeof(*queued));
        free(queued);
    }
    return accepted;
}

bool net_service_set_enabled(bool enabled)
{
    command_t command = {.kind = CMD_ENABLE, .enabled = enabled};
    return submit(&command);
}

bool net_service_scan(void)
{
    command_t command = {.kind = CMD_SCAN};
    return submit(&command);
}

bool net_service_connect(const char *ssid, const char *password)
{
    if (!ssid || !password) return false;
    size_t ssid_len = strnlen(ssid, NET_SSID_SIZE);
    size_t password_len = strnlen(password, 65);
    if (!ssid_len || ssid_len > 32 || password_len > 64 ||
        (password_len > 0 && password_len < 8)) return false;
    if (password_len == 64) {
        for (size_t i = 0; i < password_len; ++i) {
            char c = password[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                  (c >= 'A' && c <= 'F'))) return false;
        }
    }
    command_t command = {.kind = CMD_CONNECT};
    memcpy(command.ssid, ssid, ssid_len);
    memcpy(command.password, password, password_len);
    return submit(&command);
}

bool net_service_cancel(void)
{
    command_t command = {.kind = CMD_CANCEL};
    return submit(&command);
}

bool net_service_forget(void)
{
    command_t command = {.kind = CMD_FORGET};
    return submit(&command);
}

bool net_service_request_ntp(void)
{
    command_t command = {.kind = CMD_NTP};
    return submit(&command);
}

bool net_service_set_time_config(const char *server, int timezone_offset_minutes)
{
    if (!valid_server(server) || timezone_offset_minutes < -720 ||
        timezone_offset_minutes > 840) return false;
    command_t command = {.kind = CMD_TIME_CONFIG, .timezone = timezone_offset_minutes};
    strlcpy(command.server, server, sizeof(command.server));
    return submit(&command);
}
