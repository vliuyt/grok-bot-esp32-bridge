
#pragma once
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define ESP_ERR_INVALID_STATE -3
#define ESP_CODEC_DEV_OK 0
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define portMAX_DELAY 0xffffffffu
#define pdPASS 1
#define pdMS_TO_TICKS(n) (n)
typedef void* SemaphoreHandle_t;
typedef void* esp_codec_dev_handle_t;
typedef struct {int sample_rate,channel,bits_per_sample;} esp_codec_dev_sample_info_t;
static int writes,amp_on,credits,mutex_takes;
static inline int esp_codec_dev_open(void *a,void*b){(void)a;(void)b;return 0;}
static inline int esp_codec_dev_close(void *a){(void)a;return 0;}
static inline int esp_codec_dev_set_out_vol(void*a,int b){(void)a;(void)b;return 0;}
static inline int esp_codec_dev_set_in_gain(void*a,float b){(void)a;(void)b;return 0;}
static inline int esp_codec_dev_read(void*a,void*b,int n){(void)a;(void)b;(void)n;return 0;}
static inline int esp_codec_dev_write(void*a,void*b,int n){(void)a;(void)b;(void)n;writes++;return 0;}
static inline int64_t esp_timer_get_time(void){return 1000000;}
static inline void *heap_caps_calloc(size_t a,size_t b,int c){(void)c;return calloc(a,b);}
static inline void *xSemaphoreCreateMutex(void){return (void*)1;}
static inline int xSemaphoreTake(void*a,unsigned b){(void)a;(void)b;mutex_takes++;return 1;}
static inline int xSemaphoreGive(void*a){(void)a;return 1;}
static inline void vTaskDelay(unsigned a){(void)a;}
static inline int xTaskCreatePinnedToCore(void(*f)(void*),const char*n,int s,void*a,int p,void*b,int c){(void)f;(void)n;(void)s;(void)a;(void)p;(void)b;(void)c;return 1;}
static inline void pa(bool on){amp_on=on;}
typedef struct{void(*audio_power)(bool);void(*codec_power)(bool);} fake_board;
static inline fake_board *pet_board(void){static fake_board b={pa,pa};return &b;}
static inline int pet_board_audio_init(void **a,void **b){*a=*b=(void*)1;return 0;}

#define configTICK_RATE_HZ 100
