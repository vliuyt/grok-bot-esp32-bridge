#include "pet_caption.h"
#include <stdint.h>
#include <string.h>

bool pet_caption_mode_valid(pet_caption_mode_t mode){return mode>=PET_CAPTION_FULL&&mode<=PET_CAPTION_OFF;}
static size_t codepoint(const unsigned char *p,uint32_t *value){
    unsigned n;uint32_t cp;
    if(p[0]<0x80){*value=p[0];return p[0]?1:0;}
    if(p[0]>=0xc2&&p[0]<=0xdf){n=2;cp=p[0]&31;}
    else if(p[0]>=0xe0&&p[0]<=0xef){n=3;cp=p[0]&15;}
    else if(p[0]>=0xf0&&p[0]<=0xf4){n=4;cp=p[0]&7;}
    else return 0;
    for(unsigned i=1;i<n;i++){
        if(p[i]<0x80||p[i]>0xbf)return 0;
        cp=(cp<<6)|(p[i]&63);
    }
    if((n==3&&cp<0x800)||(n==4&&cp<0x10000)||
       (cp>=0xd800&&cp<=0xdfff)||cp>0x10ffff)return 0;
    *value=cp;return n;
}
size_t pet_caption_text(pet_caption_mode_t mode,const char *input,char *out,size_t capacity){
    if(!out||!capacity)return 0;
    out[0]=0;
    if(!input||!pet_caption_mode_valid(mode)||mode==PET_CAPTION_OFF)return 0;
    const unsigned char *p=(const unsigned char *)input;
    if(mode==PET_CAPTION_COMPACT)while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')p++;
    size_t used=0;
    while(*p){
        uint32_t cp;size_t n=codepoint(p,&cp);
        if(!n||n>=capacity-used)break;
        if(mode==PET_CAPTION_COMPACT&&(cp=='\n'||cp=='\r'))break;
        memcpy(out+used,p,n);used+=n;p+=n;
        if(mode==PET_CAPTION_COMPACT&&(cp=='.'||cp=='!'||cp=='?'||cp==0x3002||cp==0xff01||cp==0xff1f))break;
    }
    out[used]=0;return used;
}

void pet_caption_reset(pet_caption_buffer_t *b){memset(b,0,sizeof(*b));}
bool pet_caption_receive(pet_caption_buffer_t *b,const pet_state_t *s,uint32_t generation,uint32_t turn,const char *text){
    if(!pet_response_audio_allowed(s,generation,turn))return false;
    pet_caption_text(PET_CAPTION_FULL,text,b->text,sizeof(b->text));
    b->generation=generation;b->turn=turn;b->valid=true;return true;
}
size_t pet_caption_current(pet_caption_buffer_t *b,const pet_state_t *s,pet_caption_mode_t mode,char *out,size_t capacity){
    if(b->valid&&!pet_response_audio_allowed(s,b->generation,b->turn))pet_caption_reset(b);
    return pet_caption_text(mode,b->valid?b->text:"",out,capacity);
}
