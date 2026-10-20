#include "pet_readability.h"
#include <string.h>

static bool readable(const pet_readability_input_t *in) {
    char first[5];
    return in->connection==PET_CONN_READY&&
        (in->dialogue==PET_DIALOGUE_IDLE||in->dialogue==PET_DIALOGUE_SPEAK)&&
        pet_caption_mode_valid(in->caption_mode)&&in->caption_mode!=PET_CAPTION_OFF&&
        pet_caption_text(PET_CAPTION_COMPACT,in->caption,first,sizeof(first))>0;
}
pet_readability_text_t pet_readability_text(const pet_readability_input_t *in) {
    pet_readability_text_t out={.hint=in->connection==PET_CONN_READY?"按住 BOOT 说话":"语音连接中"};
    const char *local=NULL;
    if(in->dialogue==PET_DIALOGUE_LISTEN){local="我在听";out.hint="松开 BOOT 后提交";}
    else if(in->dialogue==PET_DIALOGUE_WAIT)local=in->waiting_long?"还在想，稍等我一下":"让我想想";
    else if(in->pose.expression==PET_EXPR_HAPPY)local="好舒服呀";
    else if(in->pose.expression==PET_EXPR_CURIOUS)local="嗯？我在呢";
    else if(in->pose.expression==PET_EXPR_CONFUSED)local="刚才没连上";
    else if(in->pose.invitation)local="想和你聊聊天";
    if(in->connection==PET_CONN_READY&&(in->dialogue==PET_DIALOGUE_WAIT||in->dialogue==PET_DIALOGUE_SPEAK))out.hint="按住 BOOT 打断";
    if(local){pet_caption_text(PET_CAPTION_FULL,local,out.summary,sizeof(out.summary));out.local=true;}
    else if(readable(in))pet_caption_text(PET_CAPTION_COMPACT,in->caption,out.summary,sizeof(out.summary));
    out.can_read=readable(in)&&!out.local;
    return out;
}
pet_page_t pet_readability_page(pet_page_t page,const pet_readability_input_t *in) {
    if(page<PET_PAGE_HOME||page>=PET_PAGE_COUNT)return PET_PAGE_HOME;
    if(page==PET_PAGE_READING&&!readable(in))return PET_PAGE_HOME;
    return page;
}
pet_face_t pet_readability_face(pet_pose_t pose) {
    pose.scale_x*=200.0f/224;pose.scale_y*=200.0f/224;
    pose.offset_y-=21;
    return pet_face_geometry(pose);
}
