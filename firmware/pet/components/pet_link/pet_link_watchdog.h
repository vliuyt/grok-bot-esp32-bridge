#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

/* The link worker owns the deadline; other tasks only observe the atomic flag. */
typedef struct {uint64_t native_wait_at;} pet_link_watchdog_t;
static inline void pet_link_watchdog_begin(pet_link_watchdog_t *watch,atomic_bool *ready,uint64_t now){
 watch->native_wait_at=now;atomic_store(ready,false);
}
static inline void pet_link_watchdog_ready(pet_link_watchdog_t *watch,atomic_bool *ready,bool value,uint64_t now){
 if(atomic_load(ready)&&!value)watch->native_wait_at=now;
 atomic_store(ready,value);
}
static inline bool pet_link_watchdog_expired(const pet_link_watchdog_t *watch,const atomic_bool *ready,uint64_t last_received,uint64_t now){
 return now-last_received>20000||(!atomic_load(ready)&&now-watch->native_wait_at>45000);
}
