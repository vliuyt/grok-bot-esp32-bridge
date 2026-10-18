// The Grok image owns voice; original Muse chat networking stays dormant.
#include <stdlib.h>
#include <string.h>
#include "muse_chat_priv.h"
void muse_hatch_start(void) {}
void muse_hatch_chat_connect(void) {}
void muse_hatch_chat_forget(void) {}
bool muse_hatch_ready(void) { return false; }
void muse_hatch_turn_begin(void) {}
void muse_hatch_turn_audio(const int16_t *pcm,size_t frames) { (void)pcm;(void)frames; }
size_t muse_hatch_turn_audio_wait(const int16_t *pcm,size_t frames,int wait_ms) { (void)pcm;(void)wait_ms;return frames; }
void muse_hatch_turn_end(void) {}
void muse_hatch_turn_cancel(void) {}
void muse_hatch_set_resting(bool resting) { (void)resting; }
void muse_hatch_text_turn(char *text) { free(text);muse_hatch_console("error","Use Grok voice on the Mac",NULL); }
void muse_hatch_text_cancel(void) {}
muse_hatch_ev_t muse_hatch_turn_event(char *text,size_t cap) { if(cap) text[0]=0;return MUSE_HATCH_EV_NONE; }
bool muse_hatch_turn_caption(size_t played,char *out,size_t cap) { (void)played;if(cap) out[0]=0;return false; }
size_t muse_hatch_turn_read(int16_t *pcm,size_t frames,int wait_ms) { (void)pcm;(void)frames;(void)wait_ms;return 0; }
size_t muse_hatch_mp3_selftest(int16_t **pcm) { *pcm=NULL;return 0; }
