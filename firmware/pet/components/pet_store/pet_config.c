#include "pet_config.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
void pet_config_wipe(void *data,unsigned length) {
    volatile unsigned char *p=data;
    while(length--) *p++=0;
}
void pet_config_defaults(pet_config_t *out) {
    memset(out,0,sizeof(*out));out->version=PET_CONFIG_VERSION;
    out->volume=65;out->mic_gain=30;out->brightness=80;
    out->speaker=1;out->quiet=1;out->captions=1;
}
static bool terminated(const char *s,unsigned size) { return memchr(s,0,size)!=NULL; }
static bool local_host(const char *s) {
    unsigned a,b,c,d;char end;
    if(sscanf(s,"%u.%u.%u.%u%c",&a,&b,&c,&d,&end)==4)
        return a<=255&&b<=255&&c<=255&&d<=255&&
            (a==10 || (a==192&&b==168) || (a==172&&b>=16&&b<=31));
    size_t n=strlen(s);
    if(n<=6||strcmp(s+n-6,".local")) return false;
    for(size_t i=0;i<n;i++) if(!isalnum((unsigned char)s[i])&&s[i]!='-'&&s[i]!='.')return false;
    return true;
}
static bool network_valid(const pet_network_config_t *n) {
    if(!terminated(n->ssid,sizeof(n->ssid))||!n->ssid[0]||
       !terminated(n->password,sizeof(n->password))||n->hidden>1) return false;
    size_t length=strlen(n->password);
    if(length==64) {
        for(unsigned i=0;i<64;i++) if(!isxdigit((unsigned char)n->password[i]))return false;
    }else if(length && (length<8||length>63))return false;
    return true;
}
bool pet_config_valid(const pet_config_t *c) {
    if(!c||c->version!=PET_CONFIG_VERSION||c->volume>100||c->mic_gain>36||
       c->brightness<10||c->brightness>100||c->speaker>1||c->quiet>1||c->captions>1||
       c->network_count>PET_NETWORK_MAX||c->bridge_paired>1||c->quota_count>3||
       !terminated(c->host,sizeof(c->host))||!terminated(c->bridge_id,sizeof(c->bridge_id))) return false;
    for(unsigned i=0;i<c->network_count;i++) {
        if(!network_valid(&c->networks[i]))return false;
        for(unsigned j=0;j<i;j++)if(!strcmp(c->networks[i].ssid,c->networks[j].ssid))return false;
    }
    if(c->bridge_paired) {
        uint8_t nonzero=0;for(unsigned i=0;i<sizeof(c->key);i++)nonzero|=c->key[i];
        if(!nonzero||c->port<1024||!local_host(c->host))return false;
    }
    return true;
}
bool pet_config_migrate(pet_config_t *out,const pet_legacy_t *legacy) {
    if(!out||!legacy||legacy->network_count>PET_NETWORK_MAX)return false;
    pet_config_t next;pet_config_defaults(&next);
    if(legacy->audio_present) {
        next.volume=legacy->volume;next.mic_gain=legacy->mic_gain;
        next.brightness=legacy->brightness;next.speaker=legacy->speaker;
    }
    next.network_count=legacy->network_count;
    memcpy(next.networks,legacy->networks,sizeof(next.networks));
    if(legacy->bridge_present) {
        next.bridge_paired=1;next.port=legacy->port;
        memcpy(next.host,legacy->host,sizeof(next.host));memcpy(next.key,legacy->key,sizeof(next.key));
    }
    bool ok=pet_config_valid(&next);
    if(ok)*out=next;
    pet_config_wipe(&next,sizeof(next));
    return ok;
}
