#pragma once
#include "FreeRTOS.h"
static inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *s){return s;}
static inline int xSemaphoreTake(SemaphoreHandle_t s,unsigned timeout){(void)s;(void)timeout;return 1;}
static inline int xSemaphoreGive(SemaphoreHandle_t s){(void)s;return 1;}
