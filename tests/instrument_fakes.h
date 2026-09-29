#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_NO_MEM 3
#define ESP_CODEC_DEV_OK 0
#define ESP_CODEC_DEV_INVALID_ARG 2
#define pdPASS 1
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(n) (n)
typedef int esp_err_t;
typedef void *esp_codec_dev_handle_t;
typedef void *SemaphoreHandle_t;
static int codec_volume=-1,codec_writes;
static bool codec_mute;
static void *xSemaphoreCreateMutex(void){return (void *)1;}
static void xSemaphoreTake(void *m,int t){(void)m;(void)t;}
static void xSemaphoreGive(void *m){(void)m;}
static void vSemaphoreDelete(void *m){(void)m;}
static int xTaskCreate(void(*f)(void*),const char*n,int s,void*a,int p,void*h){(void)f;(void)n;(void)s;(void)a;(void)p;(void)h;return pdPASS;}
static void vTaskDelay(int t){(void)t;}
static int esp_codec_dev_set_out_vol(void*c,int v){assert(c);codec_volume=v;return 0;}
static int esp_codec_dev_set_out_mute(void*c,bool v){assert(c);codec_mute=v;return 0;}
static int esp_codec_dev_write(void*c,void*b,int n){assert(c&&b&&n>0);codec_writes++;return 0;}
