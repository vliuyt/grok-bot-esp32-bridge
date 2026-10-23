#include "pet_reader.h"
#include <string.h>

void pet_reader_close(pet_reader_t *r){
 r->active=r->pending=r->failed=r->responding=r->truncated=false;
 r->version=r->request=r->page=r->pages=r->target=0;memset(r->text,0,sizeof(r->text));
}
void pet_reader_sync(pet_reader_t *r,const pet_state_t *s,pet_caption_mode_t mode,uint64_t now){
 bool enabled=s->connection==PET_CONN_READY&&!s->response_cancelled&&s->dialogue!=PET_DIALOGUE_LISTEN&&mode!=PET_CAPTION_OFF;
 if(s->generation!=r->generation||s->turn!=r->turn||!enabled){
  pet_reader_close(r);r->available=false;
  if(s->generation!=r->generation||!enabled)r->capable=false;
 }
 r->generation=s->generation;r->turn=s->turn;r->enabled=enabled;
 if(r->pending&&now>=r->sent_ms&&now-r->sent_ms>=3000){r->pending=false;r->failed=true;}
}
void pet_reader_catalog(pet_reader_t *r,uint32_t g,uint32_t t,bool available,bool responding,bool truncated){
 if(!r->enabled||g!=r->generation||t!=r->turn)return;
 r->capable=true;r->available=available;
 if(!r->active){r->responding=responding;r->truncated=truncated;}
 if(!available)pet_reader_close(r);
}
bool pet_reader_begin(pet_reader_t *r,pet_reader_intent_t intent,uint64_t now,pet_reader_request_t *out){
 if(!r->enabled||!r->capable||!r->available||r->pending||r->sequence==UINT32_MAX)return false;
 uint16_t target=r->target;
 if(intent==PET_READER_OPEN){pet_reader_close(r);r->active=true;target=0;}
 else if(!r->active)return false;
 else if(intent==PET_READER_PREV){if(!r->version||r->page==0)return false;target=r->page-1;}
 else if(intent==PET_READER_NEXT){if(!r->version||r->page+1>=r->pages)return false;target=r->page+1;}
 else if(intent!=PET_READER_RETRY||!r->failed)return false;
 r->target=target;
 if(r->requested&&(now<r->last_request_ms||now-r->last_request_ms<1000)){r->failed=true;return false;}
 r->request=++r->sequence;r->sent_ms=r->last_request_ms=now;r->requested=true;r->pending=true;r->failed=false;
 *out=(pet_reader_request_t){r->generation,r->turn,r->version,r->request,target};return true;
}
bool pet_reader_receive(pet_reader_t *r,const pet_reader_page_t *p){
 if(!r->enabled||!r->active||!r->pending||p->generation!=r->generation||p->turn!=r->turn||p->request!=r->request||p->page!=r->target)return false;
 if(p->error){r->pending=false;r->failed=true;return true;}
 if(!p->version||(r->version&&p->version!=r->version)||!p->pages||p->page>=p->pages||(r->version&&p->pages!=r->pages))return false;
 r->version=p->version;r->page=p->page;r->pages=p->pages;r->responding=p->responding;r->truncated=p->truncated;
 memcpy(r->text,p->text,sizeof(r->text));r->text[sizeof(r->text)-1]=0;r->pending=r->failed=false;return true;
}
