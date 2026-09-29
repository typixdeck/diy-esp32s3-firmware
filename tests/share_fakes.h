#pragma once
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <setjmp.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_HTTP_EAGAIN 0x7007
#define ESP_ERR_NVS_NOT_FOUND 1
#define ESP_ERR_NO_MEM 2
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(n) (n)
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
typedef void *SemaphoreHandle_t;
typedef struct { int count,size; char slots[4][160]; } queue_t;
typedef queue_t *QueueHandle_t;
typedef int nvs_handle_t;
typedef int mbedtls_x509_crt;
static jmp_buf pump_exit;
static int64_t mock_now=1000000;
static bool mock_memory_fail,mock_open_fail,mock_short,mock_chunked,mock_nvs_fail;
static int mock_code=200,mock_pos,mock_closed,mock_cleaned,mock_created,mock_open_calls,mock_once_fail,mock_eagain;
static const char *mock_body="";
static int64_t mock_size=-2;
static uint8_t saved[2048]; static size_t saved_size;
static void *heap_caps_malloc(size_t n,int caps) { (void)caps;return mock_memory_fail?NULL:malloc(n); }
static SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static void xSemaphoreTake(void *p,unsigned t) { (void)p;(void)t; }
static void xSemaphoreGive(void *p) { (void)p; }
static QueueHandle_t xQueueCreate(int n,int size) { assert(n==4&&size<=160);queue_t *q=calloc(1,sizeof(*q));q->size=size;return q; }
static int xQueueSend(queue_t *q,const void *p,int wait) { (void)wait;if(q->count==4)return 0;memcpy(q->slots[q->count++],p,q->size);return 1; }
static int xQueueReceive(queue_t *q,void *p,int wait) { (void)wait;if(!q->count)longjmp(pump_exit,1);memcpy(p,q->slots[0],q->size);memmove(q->slots[0],q->slots[1],(--q->count)*160);return 1; }
static void xQueueReset(queue_t *q) { q->count=0; }
static void vQueueDelete(queue_t *q) { free(q); }
static int xTaskCreate(void(*fn)(void*),const char*n,int s,void*a,int pri,void*h) { (void)fn;(void)n;(void)s;(void)a;(void)pri;(void)h;return pdPASS; }
static int64_t esp_timer_get_time(void) { return mock_now; }
static int nvs_open(const char*n,int mode,int*h) { (void)n;(void)mode;*h=1;return mock_nvs_fail?ESP_FAIL:ESP_OK; }
static void nvs_close(int h) { (void)h; }
static int nvs_get_blob(int h,const char*k,void*p,size_t*n) { (void)h;(void)k;if(!saved_size)return ESP_ERR_NVS_NOT_FOUND;assert(*n>=saved_size);memcpy(p,saved,saved_size);*n=saved_size;return ESP_OK; }
static int nvs_set_blob(int h,const char*k,const void*p,size_t n) { (void)h;(void)k;assert(n<=sizeof(saved));memcpy(saved,p,n);saved_size=n;return ESP_OK; }
static int nvs_commit(int h) { (void)h;return ESP_OK; }
static int nvs_erase_key(int h,const char*k) { (void)h;(void)k;saved_size=0;return ESP_OK; }
static void mbedtls_x509_crt_init(int*c) { *c=0; }
static void mbedtls_x509_crt_free(int*c) { (void)c; }
static int mbedtls_x509_crt_parse(int*c,const unsigned char*p,size_t n) { (void)c;return n==5&&!memcmp(p,"CERT",4)?0:-1; }
static int mbedtls_base64_decode(unsigned char*d,size_t cap,size_t*n,const unsigned char*s,size_t len) {
    (void)cap;if(len!=8||memcmp(s,"Q0VSVA==",8))return -1;memcpy(d,"CERT",4);*n=4;return 0;
}
typedef struct {const char *url,*cert_pem;int timeout_ms,buffer_size,buffer_size_tx;bool disable_auto_redirect,keep_alive_enable;} esp_http_client_config_t;
typedef void *esp_http_client_handle_t;
static void *esp_http_client_init(const esp_http_client_config_t*c) { assert(c->disable_auto_redirect&&c->cert_pem&&c->timeout_ms==6000&&c->keep_alive_enable);mock_pos=0;mock_created++;return (void *)1; }
static void esp_http_client_set_header(void*h,const char*k,const char*v) { (void)h;assert(!strcmp(k,"Authorization")&&!strncmp(v,"Bearer ",7)); }
static int esp_http_client_set_url(void*h,const char*u) { (void)h;(void)u;return ESP_OK; }
static int esp_http_client_set_timeout_ms(void*h,int ms) { (void)h;assert(ms==6000||ms==1000);return ESP_OK; }
static int esp_http_client_open(void*h,int n) { (void)h;(void)n;mock_pos=0;mock_open_calls++;if(mock_once_fail){mock_once_fail--;return ESP_FAIL;}return mock_open_fail?ESP_FAIL:ESP_OK; }
static int64_t esp_http_client_fetch_headers(void*h) { (void)h;return mock_size==-2?(int64_t)strlen(mock_body):mock_size; }
static int esp_http_client_get_status_code(void*h) { (void)h;return mock_code; }
static bool esp_http_client_is_chunked_response(void*h) { (void)h;return mock_chunked; }
static int esp_http_client_read(void*h,char*p,int n) { (void)h;if(mock_eagain){mock_eagain--;mock_now+=1000000;return -ESP_ERR_HTTP_EAGAIN;}if(mock_short)return 0;int left=strlen(mock_body)-mock_pos;if(n>left)n=left;if(n>9)n=9;memcpy(p,mock_body+mock_pos,n);mock_pos+=n;return n; }
static void esp_http_client_close(void*h) { (void)h;mock_closed++; }
static void esp_http_client_cleanup(void*h) { (void)h;mock_cleaned++; }
