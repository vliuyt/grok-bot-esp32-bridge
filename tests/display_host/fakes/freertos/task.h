#pragma once
static inline int xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,unsigned stack,void *arg,int priority,void *handle,int core){
    (void)name;(void)stack;(void)priority;(void)handle;(void)core;fn(arg);return 1;
}
static inline void vTaskDelete(void *task){(void)task;}
