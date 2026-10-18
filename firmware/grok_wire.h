#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define GROK_FRAME_MAX 32768u
#define GROK_AUDIO_BYTES 640u
#define GROK_PLAY_SLOTS 64u
static inline uint32_t grok_u32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static inline void grok_put32(uint8_t *p,uint32_t n) { p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n; }
static inline bool grok_valid_frame(uint32_t length,uint8_t type) { return length>=5 && length<=GROK_FRAME_MAX && (type==2 || type==3); }
static inline bool grok_valid_audio(size_t n) { return n>0 && n<=GROK_AUDIO_BYTES && n%2==0; }
static inline bool grok_key(const char *text,uint8_t key[32]) {
    for(size_t i=0;i<32;i++) {
        unsigned byte=0;
        for(size_t j=0;j<2;j++) { char c=text[2*i+j]; unsigned v;
            if(c>='0' && c<='9') v=c-'0'; else if(c>='a' && c<='f') v=c-'a'+10; else if(c>='A' && c<='F') v=c-'A'+10; else return false;
            byte=(byte<<4)|v;
        } key[i]=(uint8_t)byte;
    } return text[64]=='\0';
}
