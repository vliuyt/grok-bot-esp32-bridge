#include "pet_core.h"
#include "pet_face.h"
#include "pet_caption.h"
#include <stdio.h>
#include <string.h>

static void event(pet_state_t *s,pet_event_kind_t kind,uint64_t now,int value) {
    pet_step(s,(pet_event_t){.kind=kind,.now_ms=now,.value=value,
                           .generation=s->generation,.turn=s->turn});
}
static uint64_t prepare(pet_state_t *s,const char *name) {
    pet_init(s,0);
    if(!strcmp(name,"sleep")){event(s,PET_EV_TICK,91000,0);return 91000;}
    event(s,PET_EV_USB,0,1);event(s,PET_EV_LINK,1,PET_CONN_READY);
    if(!strcmp(name,"curious"))event(s,PET_EV_TOUCH_TAP,1000,0);
    else if(!strcmp(name,"happy"))event(s,PET_EV_PET,1000,0);
    else if(!strcmp(name,"confused"))event(s,PET_EV_LINK,1000,PET_CONN_UNAVAILABLE);
    else if(!strcmp(name,"listen"))event(s,PET_EV_PRESS,1000,0);
    else if(!strcmp(name,"think")||!strcmp(name,"speak")) {
        event(s,PET_EV_PRESS,500,0);event(s,PET_EV_RELEASE,800,0);
        if(!strcmp(name,"speak")){event(s,PET_EV_BUFFERED,900,0);event(s,PET_EV_PLAYING,1000,600);}
    } else if(!strcmp(name,"greet")) {
        event(s,PET_EV_CLOCK,1,600);s->day=20261020;s->clock_valid=true;
        s->greet_until_ms=4000;
    }
    return 1000;
}
static void json_string(const char *text){
    putchar('"');
    for(const unsigned char *p=(const unsigned char *)text;*p;p++){
        if(*p=='"'||*p=='\\'){putchar('\\');putchar(*p);}
        else if(*p<32)printf("\\u%04x",*p);
        else putchar(*p);
    }
    putchar('"');
}
int main(void) {
    const char *names[]={"idle","happy","greet","listen","think","speak","sleep","curious","confused","warm_idle"};
    printf("{\"schema\":1,\"stepMs\":33,\"width\":466,\"height\":466,\"states\":{");
    for(unsigned n=0;n<10;n++) {
        pet_state_t s;uint64_t start=prepare(&s,names[n]);
        printf("%s\"%s\":[",n?",":"",names[n]);
        for(unsigned i=0;i<=120;i++) {
            uint64_t now=start+i*33;
            if(!strcmp(names[n],"speak"))event(&s,PET_EV_PLAYING,now,180+(i*97)%750);
            event(&s,PET_EV_TICK,now,0);
            if(!strcmp(names[n],"warm_idle")){s.warmth=100;s.warmth_at_ms=now;}
            pet_pose_t p=pet_pose(&s,now);pet_face_t f=pet_face_geometry(p);
            printf("%s{\"expression\":%d,\"screenOn\":%s,\"dim\":%s,\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,\"rotation\":%d,\"eyes\":[",
                   i?",":"",p.expression,p.screen_on?"true":"false",p.dim?"true":"false",f.x,f.y,f.width,f.height,f.rotation);
            for(unsigned e=0;e<2;e++) {
                pet_eye_t eye=f.eyes[e];
                printf("%s{\"x\":%d,\"y\":%d,\"height\":%d,\"rotation\":%d,\"arc\":%s,\"arcStart\":%d,\"arcEnd\":%d}",
                       e?",":"",eye.x,eye.y,eye.height,eye.rotation,eye.arc?"true":"false",eye.arc_start,eye.arc_end);
            }
            printf("]}");
        }
        printf("]");
    }
    printf("},\"captionCases\":[");
    const char *ids[]={"chinese","english","mixed","empty","long"};
    const char *labels[]={"中文长句","English","中英与 emoji","空文本","长文本边界"};
    const char *texts[]={
        "今天一起出门散步吧。路上可以看看小花，再聊聊你今天的心情。用一个慢慢的下午，把忙碌放在一边。",
        "Let's take a little walk. We can talk about your day and watch the clouds drift by.",
        "今天状态很好🙂！Let's make a tiny plan: 喝杯水，再看看窗外。",
        "",
        "这是一段用于检查圆屏字幕边界的固定示例。这里不会出现私人聊天内容。长一些的文字可以在完整模式中上下滚动，底部仍然保留实体按键提示和设置入口，切换为简洁时只显示第一句，关闭后隐藏对话正文。"
    };
    for(unsigned c=0;c<5;c++){
        printf("%s{\"id\":",c?",":"");json_string(ids[c]);printf(",\"label\":");json_string(labels[c]);
        printf(",\"modes\":[");
        for(unsigned m=0;m<3;m++){
            char text[321];pet_caption_text((pet_caption_mode_t)m,texts[c],text,sizeof(text));
            if(m)putchar(',');json_string(text);
        }
        printf("]}");
    }
    printf("],\"moodCases\":[");
    pet_state_t mood;pet_init(&mood,0);event(&mood,PET_EV_PET,100,0);
    printf("{\"events\":[{\"action\":\"stroke\",\"at\":100}],\"at\":30100,\"warmth\":%.3f},",(double)pet_warmth(&mood,30100));
    event(&mood,PET_EV_TOUCH_TAP,30100,0);
    printf("{\"events\":[{\"action\":\"stroke\",\"at\":100},{\"action\":\"tap\",\"at\":30100}],\"at\":30100,\"warmth\":%.3f}",(double)pet_warmth(&mood,30100));
    puts("]}");
}
