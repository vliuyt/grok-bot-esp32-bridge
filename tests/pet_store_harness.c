#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pet_config.h"
int main(void){
    pet_config_t c;pet_config_defaults(&c);assert(pet_config_valid(&c));
    c.volume=101;assert(!pet_config_valid(&c));pet_config_defaults(&c);
    c.network_count=9;assert(!pet_config_valid(&c));pet_config_defaults(&c);
    c.network_count=1;strcpy(c.networks[0].ssid,"fixture-network");strcpy(c.networks[0].password,"short");
    assert(!pet_config_valid(&c));strcpy(c.networks[0].password,"fixture-password");assert(pet_config_valid(&c));
    memset(c.networks[0].ssid,'s',sizeof(c.networks[0].ssid));assert(!pet_config_valid(&c));
    pet_config_defaults(&c);c.bridge_paired=1;c.port=8790;strcpy(c.host,"192.168.1.9");
    assert(!pet_config_valid(&c));memset(c.key,0x55,sizeof(c.key));assert(pet_config_valid(&c));
    strcpy(c.host,"8.8.8.8");assert(!pet_config_valid(&c));strcpy(c.host,"192.168.1.9/path");assert(!pet_config_valid(&c));
    strcpy(c.host,"grok-fixture.local");assert(pet_config_valid(&c));
    pet_legacy_t old={0};old.audio_present=true;old.volume=89;old.mic_gain=30;old.brightness=100;old.speaker=1;
    old.network_count=8;for(unsigned i=0;i<8;i++){snprintf(old.networks[i].ssid,sizeof(old.networks[i].ssid),"network-%u",i);strcpy(old.networks[i].password,"fixture-pass");}
    old.bridge_present=true;strcpy(old.host,"192.168.1.9");old.port=8790;memset(old.key,0x55,sizeof(old.key));
    pet_legacy_t copy=old;
    assert(pet_config_migrate(&c,&old));assert(c.volume==89&&c.mic_gain==30&&c.speaker==1&&c.network_count==8&&c.bridge_paired);
    assert(!memcmp(&old,&copy,sizeof(old)));
    old.network_count=9;pet_config_t before=c;assert(!pet_config_migrate(&c,&old));assert(!memcmp(&c,&before,sizeof(c)));
    old=copy;old.key[0]=0;old.port=0;assert(!pet_config_migrate(&c,&old));assert(!memcmp(&c,&before,sizeof(c)));
    puts("PET_STORE_OK");
}
