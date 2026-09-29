#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <assert.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NVS_NOT_FOUND 3
#define ESP_LOG_WARN 2
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0xffffffff
#define pdMS_TO_TICKS(n) (n)
#define IPSTR "%d.%d.%d.%d"
#define IP2STR(ip) 192,168,1,10
#define WIFI_EVENT ((void *)1)
#define IP_EVENT ((void *)2)
#define WIFI_EVENT_STA_DISCONNECTED 1
#define WIFI_EVENT_SCAN_DONE 2
#define IP_EVENT_STA_GOT_IP 3
#define ESP_EVENT_ANY_ID -1
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define WIFI_IF_STA 0
#define WIFI_AUTH_OPEN 0
#define WIFI_STORAGE_RAM 0
#define WIFI_MODE_STA 0
#define WIFI_PS_MIN_MODEM 0
#define WIFI_REASON_AUTH_FAIL 202
#define WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT 15
#define WIFI_REASON_HANDSHAKE_TIMEOUT 204
#define SNTP_OPMODE_POLL 0
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})

typedef struct {uint32_t addr;} esp_ip4_addr_t;
typedef void *esp_event_base_t;
typedef int esp_event_handler_instance_t;
typedef struct {uint16_t reason;} wifi_event_sta_disconnected_t;
typedef struct {uint16_t status;} wifi_event_sta_scan_done_t;
typedef struct {struct {esp_ip4_addr_t ip;} ip_info;} ip_event_got_ip_t;
typedef struct {uint8_t ssid[33]; int8_t rssi; int authmode;} wifi_ap_record_t;
typedef struct { struct {uint8_t ssid[32],password[64]; struct {bool capable,required;} pmf_cfg;} sta;} wifi_config_t;
typedef struct {int value;} wifi_init_config_t;
typedef int nvs_handle_t;
typedef void *SemaphoreHandle_t;
typedef struct {size_t capacity,size,count; unsigned char slots[16][256];} fake_queue_t;
typedef fake_queue_t *QueueHandle_t;
static int64_t mock_now;
static bool mock_associated, mock_nvs_fail;
static int mock_connect_calls, mock_ntp_starts, mock_ntp_stops, mock_nvs_saves;
static wifi_config_t mock_config;
static unsigned char mock_saved[256];
static size_t mock_saved_size;
static uint8_t mock_enabled;
static wifi_ap_record_t mock_aps[8];
static uint16_t mock_ap_count;
static int mock_wifi_init_calls;
static bool mock_wifi_init_fail;

#define strlcpy fake_strlcpy
static size_t fake_strlcpy(char *dst,const char *src,size_t n) {size_t len=strlen(src); if(n){size_t count=len<n-1?len:n-1;memcpy(dst,src,count);dst[count]=0;}return len;}
static QueueHandle_t xQueueCreate(size_t cap,size_t size){assert(size<=256&&cap<=16);QueueHandle_t q=calloc(1,sizeof(*q));q->capacity=cap;q->size=size;return q;}
static int xQueueSend(QueueHandle_t q,const void *data,unsigned wait){(void)wait;if(q->count>=q->capacity)return 0;memcpy(q->slots[q->count++],data,q->size);return 1;}
static int xQueueReceive(QueueHandle_t q,void *data,unsigned wait){(void)wait;if(!q->count)return 0;memcpy(data,q->slots[0],q->size);memmove(q->slots[0],q->slots[1],(--q->count)*sizeof(q->slots[0]));return 1;}
static void vQueueDelete(QueueHandle_t q){free(q);}
static SemaphoreHandle_t xSemaphoreCreateMutex(void){return (void *)1;}
static int xSemaphoreTake(SemaphoreHandle_t s,unsigned timeout){(void)s;(void)timeout;return 1;}
static int xSemaphoreGive(SemaphoreHandle_t s){(void)s;return 1;}
static void vSemaphoreDelete(SemaphoreHandle_t s){(void)s;}
static int xTaskCreate(void (*fn)(void *),const char *name,int stack,void *arg,int priority,void *handle){(void)fn;(void)name;(void)stack;(void)arg;(void)priority;(void)handle;return pdPASS;}
static int64_t esp_timer_get_time(void){return mock_now;}
static void esp_log_level_set(const char *name,int level){(void)name;(void)level;}
static esp_err_t esp_netif_init(void){return ESP_OK;}
static esp_err_t esp_event_loop_create_default(void){return ESP_OK;}
static void *esp_netif_create_default_wifi_sta(void){return (void *)1;}
static esp_err_t esp_wifi_init(const wifi_init_config_t *config){(void)config;mock_wifi_init_calls++;return mock_wifi_init_fail?ESP_ERR_NO_MEM:ESP_OK;}
static esp_err_t esp_event_handler_instance_register(esp_event_base_t base,int id,void (*fn)(void *,esp_event_base_t,int32_t,void *),void *arg,esp_event_handler_instance_t *instance){(void)base;(void)id;(void)fn;(void)arg;(void)instance;return ESP_OK;}
static esp_err_t esp_wifi_set_storage(int value){(void)value;return ESP_OK;}
static esp_err_t esp_wifi_set_mode(int value){(void)value;return ESP_OK;}
static esp_err_t esp_wifi_set_ps(int value){(void)value;return ESP_OK;}
static esp_err_t esp_wifi_start(void){return ESP_OK;}
static esp_err_t esp_wifi_stop(void){mock_associated=false;return ESP_OK;}
static esp_err_t esp_wifi_disconnect(void){mock_associated=false;return ESP_OK;}
static esp_err_t esp_wifi_connect(void){mock_connect_calls++;return ESP_OK;}
static esp_err_t esp_wifi_set_config(int iface,const wifi_config_t *config){(void)iface;mock_config=*config;return ESP_OK;}
static esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap){if(!mock_associated)return ESP_ERR_INVALID_STATE;memset(ap,0,sizeof(*ap));memcpy(ap->ssid,mock_config.sta.ssid,32);return ESP_OK;}
static esp_err_t esp_wifi_scan_start(void *config,bool block){(void)config;(void)block;return ESP_OK;}
static esp_err_t esp_wifi_scan_stop(void){return ESP_OK;}
static esp_err_t esp_wifi_clear_ap_list(void){return ESP_OK;}
static esp_err_t esp_wifi_scan_get_ap_records(uint16_t *count,wifi_ap_record_t *records){if(*count>mock_ap_count)*count=mock_ap_count;memcpy(records,mock_aps,*count*sizeof(*records));return ESP_OK;}
static void esp_sntp_setoperatingmode(int mode){(void)mode;}
static void esp_sntp_setservername(int idx,const char *server){(void)idx;(void)server;}
static void esp_sntp_set_time_sync_notification_cb(void (*fn)(struct timeval *)){(void)fn;}
static void esp_sntp_init(void){mock_ntp_starts++;}
static void esp_sntp_stop(void){mock_ntp_stops++;}
static esp_err_t nvs_open(const char *name,int flags,nvs_handle_t *handle){(void)name;(void)flags;*handle=1;return mock_nvs_fail?ESP_ERR_NO_MEM:ESP_OK;}
static void nvs_close(nvs_handle_t handle){(void)handle;}
static esp_err_t nvs_set_blob(nvs_handle_t handle,const char *key,const void *data,size_t size){(void)handle;assert(!strcmp(key,"network"));memcpy(mock_saved,data,size);mock_saved_size=size;mock_nvs_saves++;return ESP_OK;}
static esp_err_t nvs_get_blob(nvs_handle_t handle,const char *key,void *data,size_t *size){(void)handle;(void)key;if(!mock_saved_size)return ESP_ERR_NVS_NOT_FOUND;if(data){if(*size<mock_saved_size)return ESP_ERR_NO_MEM;memcpy(data,mock_saved,mock_saved_size);}*size=mock_saved_size;return ESP_OK;}
static esp_err_t nvs_set_u8(nvs_handle_t handle,const char *key,uint8_t value){(void)handle;(void)key;mock_enabled=value;return ESP_OK;}
static esp_err_t nvs_get_u8(nvs_handle_t handle,const char *key,uint8_t *value){(void)handle;(void)key;*value=mock_enabled;return ESP_OK;}
static esp_err_t nvs_set_i32(nvs_handle_t handle,const char *key,int32_t value){(void)handle;(void)key;(void)value;return ESP_OK;}
static esp_err_t nvs_get_i32(nvs_handle_t handle,const char *key,int32_t *value){(void)handle;(void)key;(void)value;return ESP_ERR_NVS_NOT_FOUND;}
static esp_err_t nvs_set_str(nvs_handle_t handle,const char *key,const char *value){(void)handle;(void)key;(void)value;return ESP_OK;}
static esp_err_t nvs_get_str(nvs_handle_t handle,const char *key,char *value,size_t *size){(void)handle;(void)key;(void)value;(void)size;return ESP_ERR_NVS_NOT_FOUND;}
static esp_err_t nvs_commit(nvs_handle_t handle){(void)handle;return mock_nvs_fail?ESP_ERR_NO_MEM:ESP_OK;}
static esp_err_t nvs_erase_key(nvs_handle_t handle,const char *key){(void)handle;(void)key;mock_saved_size=0;return ESP_OK;}
