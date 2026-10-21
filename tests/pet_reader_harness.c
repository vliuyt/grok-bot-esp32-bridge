#include "pet_reader.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
 pet_reader_t r={0};pet_state_t s={.connection=PET_CONN_READY,.generation=7,.turn=3};pet_reader_request_t request;
 pet_reader_sync(&r,&s,PET_CAPTION_FULL,0);
 assert(!pet_reader_begin(&r,PET_READER_OPEN,0,&request)); /* old bridge */
 pet_reader_catalog(&r,7,3,true,true,false);
 assert(pet_reader_begin(&r,PET_READER_OPEN,0,&request));assert(request.version==0&&request.page==0&&request.request==1);
 assert(!pet_reader_begin(&r,PET_READER_NEXT,1100,&request));
 pet_reader_page_t p={.generation=7,.turn=3,.request=1,.version=4,.page=0,.pages=3,.responding=true};strcpy(p.text,"第一段");
 assert(pet_reader_receive(&r,&p));assert(!strcmp(r.text,"第一段"));
 pet_reader_catalog(&r,7,3,true,false,false);assert(r.responding); /* frozen status */
 assert(!pet_reader_begin(&r,PET_READER_NEXT,500,&request));
 assert(pet_reader_begin(&r,PET_READER_NEXT,1100,&request));assert(request.version==4&&request.page==1);
 p.request=1;p.page=0;assert(!pet_reader_receive(&r,&p));assert(!strcmp(r.text,"第一段"));
 pet_reader_sync(&r,&s,PET_CAPTION_FULL,4100);assert(r.failed&&!r.pending&&!strcmp(r.text,"第一段"));
 p.request=2;p.page=1;assert(!pet_reader_receive(&r,&p)); /* expired */
 assert(pet_reader_begin(&r,PET_READER_RETRY,4200,&request));assert(request.page==1&&request.request==3);
 p.request=3;p.version=5;assert(!pet_reader_receive(&r,&p));p.version=4;p.generation=8;assert(!pet_reader_receive(&r,&p));
 p.generation=7;assert(pet_reader_receive(&r,&p));assert(r.page==1);
 assert(pet_reader_begin(&r,PET_READER_NEXT,5300,&request));pet_reader_close(&r);p.request=request.request;p.page=2;
 assert(!pet_reader_receive(&r,&p));assert(!r.active&&!r.text[0]);
 assert(pet_reader_begin(&r,PET_READER_OPEN,6400,&request));assert(!request.version&&!request.page);
 s.turn++;pet_reader_sync(&r,&s,PET_CAPTION_FULL,6401);assert(!r.active&&!r.available&&!r.text[0]);assert(!pet_reader_receive(&r,&p));
 pet_reader_catalog(&r,7,4,true,false,false);assert(pet_reader_begin(&r,PET_READER_OPEN,7500,&request));
 pet_reader_sync(&r,&s,PET_CAPTION_OFF,7501);assert(!r.active&&!r.available&&!r.pending&&!r.text[0]);
 pet_reader_catalog(&r,7,4,true,false,false);s.connection=PET_CONN_OFFLINE;pet_reader_sync(&r,&s,PET_CAPTION_FULL,7502);assert(!r.capable);
 puts("PET_READER_OK");
}
