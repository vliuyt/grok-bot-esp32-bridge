#pragma once
typedef void *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void){return (void *)1;}
static inline SemaphoreHandle_t xSemaphoreCreateBinary(void){return (void *)2;}
static inline int xSemaphoreTake(SemaphoreHandle_t s,unsigned timeout){(void)s;(void)timeout;return 1;}
static inline int xSemaphoreGive(SemaphoreHandle_t s){(void)s;return 1;}
