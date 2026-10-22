#include "pet_link_watchdog.h"
#include <assert.h>
#include <stdio.h>

int main(void){
 pet_link_watchdog_t watch={0};atomic_bool ready=false;
 pet_link_watchdog_begin(&watch,&ready,100);
 assert(!atomic_load(&ready));
 assert(!pet_link_watchdog_expired(&watch,&ready,45100,45100));
 assert(pet_link_watchdog_expired(&watch,&ready,45101,45101));

 /* A native redial on an old TLS connection gets its own bounded window. */
 pet_link_watchdog_ready(&watch,&ready,true,200);
 assert(!pet_link_watchdog_expired(&watch,&ready,900000,900000));
 pet_link_watchdog_ready(&watch,&ready,false,900000);
 assert(!atomic_load(&ready));
 assert(!pet_link_watchdog_expired(&watch,&ready,900000,900000));
 pet_link_watchdog_ready(&watch,&ready,false,930000);
 assert(!pet_link_watchdog_expired(&watch,&ready,945000,945000));
 assert(pet_link_watchdog_expired(&watch,&ready,945001,945001));

 /* Ready followed by a later redial starts a new deadline. */
 pet_link_watchdog_ready(&watch,&ready,true,950000);
 pet_link_watchdog_ready(&watch,&ready,false,1800000);
 assert(!pet_link_watchdog_expired(&watch,&ready,1845000,1845000));
 assert(pet_link_watchdog_expired(&watch,&ready,1845001,1845001));

 /* Incoming frames remain mandatory during both ready and connecting. */
 assert(!pet_link_watchdog_expired(&watch,&ready,1800000,1820000));
 assert(pet_link_watchdog_expired(&watch,&ready,1800000,1820001));
 pet_link_watchdog_ready(&watch,&ready,true,1850000);
 assert(!pet_link_watchdog_expired(&watch,&ready,1850000,1870000));
 assert(pet_link_watchdog_expired(&watch,&ready,1850000,1870001));

 /* A fresh TLS connection cannot inherit a previous native deadline. */
 pet_link_watchdog_begin(&watch,&ready,0);
 assert(!atomic_load(&ready)&&watch.native_wait_at==0);
 pet_link_watchdog_begin(&watch,&ready,2000000);
 assert(!atomic_load(&ready));
 assert(!pet_link_watchdog_expired(&watch,&ready,2045000,2045000));
 assert(pet_link_watchdog_expired(&watch,&ready,2045001,2045001));
 puts("PET_LINK_WATCHDOG_OK");
}
