#include "pet_readability.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void safe_face(pet_pose_t pose) {
    pet_face_t f=pet_readability_face(pose);
    double theta=f.rotation*3.141592653589793/1800;
    double radius=fmin(f.width,f.height)/2;
    double extent=radius+fabs((double)f.width-f.height)/2*(f.width>=f.height?fabs(sin(theta)):fabs(cos(theta)));
    assert(f.y+f.height/2+extent<307);
    assert(hypot(f.x+f.width/2-233,f.y+f.height/2-233)+fmax(f.width,f.height)/2<233);
}
int main(void) {
    pet_state_t notice_state;pet_init(&notice_state,0);notice_state.connection=PET_CONN_READY;notice_state.generation=7;
    pet_step(&notice_state,(pet_event_t){.kind=PET_EV_PRESS,.now_ms=1});
    pet_step(&notice_state,(pet_event_t){.kind=PET_EV_RELEASE,.now_ms=201});
    uint32_t notice_fx=pet_step(&notice_state,(pet_event_t){.kind=PET_EV_TURN_STATUS,.generation=7,.turn=1,.value=PET_NOTICE_NO_REPLY,.now_ms=301});
    assert(notice_state.notice==PET_NOTICE_NO_REPLY&&notice_state.dialogue==PET_DIALOGUE_IDLE&&(notice_fx&PET_FX_CANCEL));
    pet_step(&notice_state,(pet_event_t){.kind=PET_EV_PRESS,.now_ms=401});
    pet_step(&notice_state,(pet_event_t){.kind=PET_EV_TURN_STATUS,.generation=7,.turn=1,.value=PET_NOTICE_NO_REPLY,.now_ms=501});
    assert(notice_state.notice==PET_NOTICE_NONE&&notice_state.dialogue==PET_DIALOGUE_LISTEN);
    assert(pet_notice_from_code("short_input")==PET_NOTICE_SHORT_INPUT&&pet_notice_from_code("not-a-code")==PET_NOTICE_NONE);
    pet_readability_input_t failed={.connection=PET_CONN_READY,.caption_mode=PET_CAPTION_OFF,.notice=PET_NOTICE_SHORT_INPUT};
    pet_readability_text_t failure=pet_readability_text(&failed);
    assert(failure.local&&!failure.can_read&&strstr(failure.summary,"太短"));
    failed.notice=PET_NOTICE_NO_REPLY;assert(strstr(pet_readability_text(&failed).summary,"收到回复"));
    failed.notice=PET_NOTICE_UNRECOGNIZED;assert(strstr(pet_readability_text(&failed).summary,"听清"));
    pet_readability_input_t in={.pose={.expression=PET_EXPR_IDLE,.scale_x=1,.scale_y=1,.eye_open=1},.connection=PET_CONN_READY,.caption_mode=PET_CAPTION_FULL,.caption="你好，苹果。第二句。"};
    pet_readability_text_t text=pet_readability_text(&in);
    assert(!strcmp(text.summary,"你好，苹果。")&&text.can_read&&!text.local);
    in.caption_mode=PET_CAPTION_COMPACT;
    assert(!strcmp(pet_readability_text(&in).summary,text.summary));
    in.caption="  Hello! Next sentence.";
    assert(!strcmp(pet_readability_text(&in).summary,"Hello!"));
    char long_text[700];for(unsigned i=0;i<210;i++)memcpy(long_text+3*i,"糖",3);long_text[630]=0;
    in.caption=long_text;text=pet_readability_text(&in);
    assert(strlen(text.summary)==318&&text.can_read);
    char valid[321];assert(pet_caption_text(PET_CAPTION_FULL,text.summary,valid,sizeof(valid))==318);
    in.caption_mode=PET_CAPTION_OFF;
    assert(!pet_readability_text(&in).summary[0]&&!pet_readability_text(&in).can_read);
    in.dialogue=PET_DIALOGUE_LISTEN;text=pet_readability_text(&in);
    assert(text.local&&!strcmp(text.summary,"我在听")&&!strcmp(text.hint,"松开 BOOT 后提交"));
    assert(pet_readability_page(PET_PAGE_READING,&in)==PET_PAGE_HOME);
    in.dialogue=PET_DIALOGUE_WAIT;in.waiting_long=true;
    assert(!strcmp(pet_readability_text(&in).summary,"还在想，稍等我一下"));
    in.waiting_long=false;assert(!strcmp(pet_readability_text(&in).summary,"让我想想"));
    in.dialogue=PET_DIALOGUE_IDLE;in.caption_mode=PET_CAPTION_FULL;in.caption="当前回答。还有内容。";
    for(int c=PET_CONN_OFFLINE;c<=PET_CONN_UNAVAILABLE;c++) {
        in.connection=(pet_connection_t)c;
        assert(pet_readability_text(&in).can_read==(c==PET_CONN_READY));
        assert(pet_readability_page(PET_PAGE_READING,&in)==(c==PET_CONN_READY?PET_PAGE_READING:PET_PAGE_HOME));
    }
    in.connection=PET_CONN_READY;
    in.pose.expression=PET_EXPR_HAPPY;
    assert(pet_readability_text(&in).local);
    /* An autonomous expression cannot eject someone already reading. */
    assert(pet_readability_page(PET_PAGE_READING,&in)==PET_PAGE_READING);
    in.caption="";assert(pet_readability_page(PET_PAGE_READING,&in)==PET_PAGE_HOME);
    in.caption="旧回答";in.caption_mode=PET_CAPTION_OFF;
    assert(pet_readability_page(PET_PAGE_READING,&in)==PET_PAGE_HOME);
    assert(pet_readability_page(PET_PAGE_MORE,&in)==PET_PAGE_MORE);
    assert(pet_readability_page((pet_page_t)-1,&in)==PET_PAGE_HOME);
    assert(pet_readability_page(PET_PAGE_COUNT,&in)==PET_PAGE_HOME);
    pet_state_t s;pet_init(&s,0);
    for(unsigned event=0;event<4;event++) {
        pet_init(&s,0);
        if(event==0)pet_step(&s,(pet_event_t){.kind=PET_EV_TOUCH_TAP,.now_ms=100});
        if(event==1)pet_step(&s,(pet_event_t){.kind=PET_EV_PET,.now_ms=100});
        if(event==2)pet_step(&s,(pet_event_t){.kind=PET_EV_TICK,.now_ms=91000});
        if(event==3){s.connection=PET_CONN_READY;pet_step(&s,(pet_event_t){.kind=PET_EV_PRESS,.now_ms=100});}
        for(uint64_t now=s.now_ms;now<s.now_ms+4000;now+=17)safe_face(pet_pose(&s,now));
    }
    for(unsigned expression=0;expression<=PET_EXPR_CONFUSED;expression++) {
        in.pose.expression=(pet_expression_t)expression;safe_face(in.pose);
    }
    puts("PET_READABILITY_OK");
}
