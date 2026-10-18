#include "pet_setup_guard.h"
#include <string.h>
#include <ctype.h>
static void wipe(void *p,size_t n){volatile unsigned char *v=p;while(n--)*v++=0;}
void pet_setup_reset(pet_setup_guard_t *s){wipe(s->bytes,sizeof(s->bytes));s->length=0;s->started=0;}
void pet_setup_open(pet_setup_guard_t *s,uint64_t now,uint64_t duration){pet_setup_reset(s);s->deadline=now+(duration>300000?300000:duration);}
int pet_setup_chunk(pet_setup_guard_t *s,uint64_t now,bool auth,const void *data,size_t n){
 if(!auth||now>=s->deadline){pet_setup_reset(s);return PET_SETUP_DENIED;}
 if(!n||n>=sizeof(s->bytes)-s->length||(s->length&&now-s->started>10000)){pet_setup_reset(s);return PET_SETUP_INVALID;}
 const char *p=data;
 for(size_t i=0;i<n;i++)if(!p[i]||(p[i]=='\n'&&i!=n-1)){pet_setup_reset(s);return PET_SETUP_INVALID;}
 if(!s->length)s->started=now;
 memcpy(s->bytes+s->length,p,n);s->length+=n;s->bytes[s->length]=0;
 if(p[n-1]=='\n'){s->bytes[--s->length]=0;return PET_SETUP_READY;}return PET_SETUP_MORE;
}
bool pet_setup_request(pet_setup_guard_t *s,const char *id){
 size_t n=strlen(id);if(!n||n>32||s->pending||!strcmp(s->request,id))return false;
 for(size_t i=0;i<n;i++)if(!isalnum((unsigned char)id[i])&&id[i]!='-')return false;
 memcpy(s->request,id,n+1);s->pending=true;s->result=0;return true;
}
void pet_setup_result(pet_setup_guard_t *s,const char *id,bool ok){if(!strcmp(id,s->request)&&s->pending){s->pending=false;s->result=ok?1:-1;}}
