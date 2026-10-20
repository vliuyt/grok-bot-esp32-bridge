#include "pet_caption.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void){
    char out[321];
    assert(pet_caption_text(PET_CAPTION_COMPACT,"你好，小糖。今天也很好！",out,sizeof(out))==18);
    assert(!strcmp(out,"你好，小糖。"));
    pet_caption_text(PET_CAPTION_COMPACT,"Hello! Second.",out,sizeof(out));assert(!strcmp(out,"Hello!"));
    pet_caption_text(PET_CAPTION_COMPACT,"  \nHi 小糖🙂？下一句",out,sizeof(out));assert(!strcmp(out,"Hi 小糖🙂？"));
    pet_caption_text(PET_CAPTION_COMPACT,"一行\n下一行",out,sizeof(out));assert(!strcmp(out,"一行"));
    pet_caption_text(PET_CAPTION_FULL,"第一句。\n第二句🙂",out,sizeof(out));assert(!strcmp(out,"第一句。\n第二句🙂"));
    assert(pet_caption_text(PET_CAPTION_OFF,"Hello",out,sizeof(out))==0&&!out[0]);
    assert(pet_caption_text((pet_caption_mode_t)99,"Hello",out,sizeof(out))==0&&!out[0]);
    assert(pet_caption_text(PET_CAPTION_FULL,NULL,out,sizeof(out))==0);
    assert(pet_caption_text(PET_CAPTION_FULL,"hi",NULL,0)==0);
    assert(pet_caption_text(PET_CAPTION_FULL,"🙂",out,4)==0&&!out[0]);
    assert(pet_caption_text(PET_CAPTION_FULL,"🙂",out,5)==4&&!strcmp(out,"🙂"));
    assert(pet_caption_text(PET_CAPTION_FULL,"中",out,3)==0&&!out[0]);
    assert(pet_caption_text(PET_CAPTION_FULL,"A中",out,4)==1&&!strcmp(out,"A"));
    assert(pet_caption_text(PET_CAPTION_FULL,"A",out,1)==0&&!out[0]);
    pet_caption_text(PET_CAPTION_FULL,"A\xf0\x9f",out,sizeof(out));assert(!strcmp(out,"A"));
    pet_caption_text(PET_CAPTION_FULL,"A\xed\xa0\x80",out,sizeof(out));assert(!strcmp(out,"A"));
    pet_caption_text(PET_CAPTION_FULL,"A\xc0\x80",out,sizeof(out));assert(!strcmp(out,"A"));
    char long_text[325];memset(long_text,'x',319);memcpy(long_text+319,"🙂",5);
    assert(pet_caption_text(PET_CAPTION_FULL,long_text,out,sizeof(out))==319&&out[319]==0);
    pet_caption_text(PET_CAPTION_COMPACT,"没有标点也应显示",out,sizeof(out));assert(!strcmp(out,"没有标点也应显示"));
    puts("PET_CAPTION_OK");
}
