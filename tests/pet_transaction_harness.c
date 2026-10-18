#include "pet_transaction.h"
#include "pet_discovery_filter.h"
#include <assert.h>
#include <stdio.h>
static bool wifi,save;static int attempts,commits,restores;
static bool connect(const pet_network_config_t *n,void *ctx){(void)n;(void)ctx;attempts++;return wifi;}
static bool commit(const pet_config_t *n,void *ctx){(void)n;(void)ctx;commits++;return save;}
static void restore(void *ctx){(void)ctx;restores++;}
int main(void){assert(!pet_discovery_txt("id",NULL,"id","abc"));assert(!pet_discovery_txt(NULL,"2","v","2"));assert(pet_discovery_txt("v","2","v","2"));pet_config_t c;pet_config_defaults(&c);c.network_count=1;c.networks[0]=(pet_network_config_t){.ssid="test",.password="12345678"};pet_transaction_ops_t ops={connect,commit,restore,0};
 assert(!pet_network_transaction(&c,ops));assert(attempts==1&&commits==0&&restores==1);
 wifi=true;assert(!pet_network_transaction(&c,ops));assert(commits==1&&restores==2);
 save=true;assert(pet_network_transaction(&c,ops));assert(commits==2&&restores==2);
 c.network_count=0;assert(!pet_network_transaction(&c,ops));assert(attempts==3);puts("TRANSACTION_OK");}
