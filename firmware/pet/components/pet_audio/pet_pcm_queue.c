#include "pet_pcm_queue.h"
#include <string.h>
unsigned pet_pcm_reset(pet_pcm_queue_t *q,uint32_t generation,uint32_t turn) {
    unsigned discarded=q->count;
    q->head=q->count=0;q->generation=generation;q->turn=turn;q->done=false;
    return discarded;
}
int pet_pcm_push(pet_pcm_queue_t *q,uint32_t generation,uint32_t turn,const void *pcm,size_t bytes) {
    if(!pcm||!bytes||bytes>sizeof(q->packets[0].pcm)||(bytes&1))return -1;
    if(generation!=q->generation||turn!=q->turn)return 0;
    if(q->count>=PET_PCM_CAPACITY)return -2;
    q->done=false;
    pet_pcm_packet_t *p=&q->packets[(q->head+q->count)%PET_PCM_CAPACITY];
    p->generation=generation;p->turn=turn;p->bytes=(uint16_t)bytes;memcpy(p->pcm,pcm,bytes);
    q->count++;return 1;
}
bool pet_pcm_pop(pet_pcm_queue_t *q,pet_pcm_packet_t *out) {
    if(!q->count)return false;
    *out=q->packets[q->head];q->head=(q->head+1)%PET_PCM_CAPACITY;q->count--;return true;
}
bool pet_pcm_done(pet_pcm_queue_t *q,uint32_t generation,uint32_t turn) {
    if(generation!=q->generation||turn!=q->turn)return false;
    q->done=true;return true;
}
bool pet_pcm_drained(const pet_pcm_queue_t *q) { return q->done&&!q->count; }
