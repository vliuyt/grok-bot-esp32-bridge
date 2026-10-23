#include "lvgl.h"
#include "pet_ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Compile the production UI itself; only hardware, scheduling and USB are faked. */
#include "../../firmware/pet/components/pet_ui/pet_ui.c"
static uint16_t framebuffer[466*466];
static unsigned char exported[466*466*2],previous[466*466*2];
static size_t exported_size;
static lv_display_t *display;
static lv_indev_t *pointer;
static lv_point_t pointer_point;
static lv_indev_state_t pointer_state;
static pet_ui_action_t last_action;
static unsigned action_count;
static pet_ui_action_kind_t last_reader_action;
static pet_state_t activity_model;
static uint64_t activity_now;
static unsigned wake_count;
static unsigned contact_counts[5];
static int contact_x,contact_y;
static const char *artifact_dir;
static void noop_sleep(bool on){(void)on;}
static void noop_brightness(int level){(void)level;}
static bool lock(int timeout){(void)timeout;return true;}
static void unlock(void){}
static const muse_board_t board={noop_sleep,noop_brightness,lock,unlock};
const muse_board_t *pet_board(void){return &board;}
lv_display_t *pet_board_display_start(lv_indev_t **input){*input=pointer;return display;}
int mbedtls_base64_encode(unsigned char *dst,size_t capacity,size_t *used,const unsigned char *src,size_t n){
    (void)dst;(void)capacity;assert(exported_size+n<=sizeof(exported));
    memcpy(exported+exported_size,src,n);exported_size+=n;*used=0;return 0;
}
static void flush(lv_display_t *d,const lv_area_t *area,uint8_t *map){(void)area;(void)map;lv_display_flush_ready(d);}
static void read_pointer(lv_indev_t *d,lv_indev_data_t *data){(void)d;data->point=pointer_point;data->state=pointer_state;}
static void point(int x,int y,bool pressed){pointer_point=(lv_point_t){x,y};pointer_state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;lv_tick_inc(20);lv_indev_read(pointer);}
static void record(pet_ui_action_t event,void *context){
    (void)context;last_action=event;action_count++;
    if(event.kind>=PET_UI_READER_OPEN&&event.kind<=PET_UI_READER_CLOSE)last_reader_action=event.kind;
    if(event.kind==PET_UI_READER_CLOSE+1){
        unsigned phase=(unsigned)event.value>>18;assert(phase>=1&&phase<=4);contact_counts[phase]++;
        contact_x=(event.value>>9)&511;contact_y=event.value&511;
    }
    if(event.kind==PET_UI_WAKE){wake_count++;pet_note_activity(&activity_model,activity_now);}
}
static void frame(pet_ui_view_t *v){pet_ui_update(v);draw(NULL);lv_refr_now(display);}
static lv_obj_t *find(lv_obj_t *parent,const char *text){
    if(lv_obj_check_type(parent,&lv_label_class)&&!strcmp(lv_label_get_text(parent),text))return parent;
    for(uint32_t i=0;i<lv_obj_get_child_count(parent);i++){
        lv_obj_t *found=find(lv_obj_get_child(parent,(int)i),text);if(found)return found;
    }
    return NULL;
}
static void click(const char *text){
    lv_obj_t *o=find(pages[page],text);assert(o);
    while(o&&!lv_obj_check_type(o,&lv_button_class))o=lv_obj_get_parent(o);
    assert(o&&!lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN));
    lv_obj_send_event(o,LV_EVENT_CLICKED,NULL);
}
static void only_page(pet_page_t expected){
    if(page!=expected)fprintf(stderr,"page=%d expected=%d\n",page,expected);
    assert(page==expected);
    for(int i=0;i<PET_PAGE_COUNT;i++)assert(lv_obj_has_flag(pages[i],LV_OBJ_FLAG_HIDDEN)==(i!=(int)expected));
}
static void export_now(void){exported_size=0;pet_ui_request_snapshot();draw(NULL);assert(exported_size==sizeof(exported));}
static void artifact(const char *name){
    if(!artifact_dir)return;
    char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",artifact_dir,name);
    lv_draw_buf_t *buf=lv_snapshot_take(root,LV_COLOR_FORMAT_RGB565);assert(buf);
    FILE *file=fopen(path,"wb");assert(file);fprintf(file,"P6\n466 466\n255\n");
    for(int y=0;y<466;y++)for(int x=0;x<466;x++){
        uint16_t p;memcpy(&p,buf->data+y*buf->header.stride+x*2,2);
        unsigned char rgb[]={(unsigned char)((p>>11)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};
        assert(fwrite(rgb,1,3,file)==3);
    }
    fclose(file);lv_draw_buf_destroy(buf);
}
static uint64_t raw_hash(void){
    lv_draw_buf_t *buf=lv_snapshot_take(root,LV_COLOR_FORMAT_RGB565);assert(buf);uint64_t hash=1469598103934665603ULL;
    for(int y=0;y<466;y++)for(int x=0;x<932;x++){hash^=buf->data[y*buf->header.stride+x];hash*=1099511628211ULL;}
    lv_draw_buf_destroy(buf);return hash;
}
static uint64_t title_hash(void){
    lv_draw_buf_t *buf=lv_snapshot_take(root,LV_COLOR_FORMAT_RGB565);assert(buf);uint64_t hash=1469598103934665603ULL;
    for(int y=45;y<=71;y++)for(int x=113*2;x<353*2;x++){hash^=buf->data[y*buf->header.stride+x];hash*=1099511628211ULL;}
    lv_draw_buf_destroy(buf);return hash;
}
static void tap_at(int x,int y){point(x,y,true);point(x+4,y+3,false);}
static void dialogue_event(pet_state_t *s,pet_event_kind_t kind,uint64_t now,int value){
    pet_step(s,(pet_event_t){.kind=kind,.now_ms=now,.value=value,.generation=s->generation,.turn=s->turn});
}
static void dialogue_frame(pet_ui_view_t *v,const pet_state_t *s,uint64_t now){
    v->dialogue=s->dialogue;v->pose=pet_pose(s,now);lv_tick_inc(33);frame(v);
}
static void ergonomics(pet_ui_view_t *v,const char *mode){
    if(!strcmp(mode,"expressions")){
        pet_state_t s;pet_init(&s,0);dialogue_event(&s,PET_EV_LINK,1,PET_CONN_READY);
        for(uint64_t now=1;now<600;now+=33)dialogue_frame(v,&s,now);
        artifact("dialogue-idle");uint64_t title=title_hash();
        dialogue_event(&s,PET_EV_PRESS,600,0);
        for(uint64_t now=600;now<1200;now+=33)dialogue_frame(v,&s,now);
        assert(lv_obj_get_height(eyes[0])>=58&&lv_obj_get_height(eyes[0])<=65&&title_hash()==title);artifact("dialogue-listen");
        dialogue_event(&s,PET_EV_RELEASE,1200,0);
        for(uint64_t now=1200;now<1900;now+=33)dialogue_frame(v,&s,now);
        assert(abs(lv_obj_get_height(eyes[0])-lv_obj_get_height(eyes[1]))>=9&&abs(lv_obj_get_height(eyes[0])-lv_obj_get_height(eyes[1]))<=20);
        assert(title_hash()==title);artifact("dialogue-think");
        for(uint64_t now=1900;now<2500;now+=33){
            dialogue_event(&s,PET_EV_PLAYING,now,90);dialogue_frame(v,&s,now);
        }
        /* Independent axes can settle one integer pixel apart after WAIT. */
        assert(abs(lv_obj_get_height(body)-lv_obj_get_width(body))<=1);
        assert(title_hash()==title);artifact("dialogue-speak-soft");
        for(uint64_t now=2500;now<2900;now+=33){
            dialogue_event(&s,PET_EV_PLAYING,now,500);dialogue_frame(v,&s,now);
        }
        assert(abs(lv_obj_get_height(body)-lv_obj_get_width(body))<=1);
        assert(title_hash()==title);artifact("dialogue-speak-loud");
        dialogue_event(&s,PET_EV_RESPONSE_DONE,2900,0);dialogue_event(&s,PET_EV_DRAIN,2901,0);
        for(uint64_t now=2901;now<3260;now+=33)dialogue_frame(v,&s,now);
        assert(lv_obj_get_height(eyes[0])>=15&&lv_obj_get_height(eyes[0])<=42);
        assert(lv_obj_has_flag(caption_box,LV_OBJ_FLAG_CLICKABLE)&&title_hash()==title);artifact("dialogue-finish-blink");
        for(uint64_t now=3260;now<4100;now+=33)dialogue_frame(v,&s,now);
        assert(lv_obj_get_height(eyes[0])>=55&&title_hash()==title);artifact("dialogue-return");
        s.turn_warmth=100;s.dialogue=PET_DIALOGUE_SPEAK;
        for(uint64_t now=4100;now<4700;now+=33){
            dialogue_event(&s,PET_EV_PLAYING,now,1000);dialogue_frame(v,&s,now);
        }
        assert(lv_obj_get_width(body)>=180&&lv_obj_get_height(body)<=222);
        assert(abs(lv_obj_get_height(body)-lv_obj_get_width(body))<=1);
        assert(title_hash()==title);artifact("dialogue-speak-maximum-warm");
        pet_init(&s,0);dialogue_event(&s,PET_EV_LINK,1,PET_CONN_READY);dialogue_event(&s,PET_EV_PRESS,100,0);
        unsigned actions_before=action_count;
        int min_x=1000,max_x=-1000,min_r=1000,max_r=-1000,min_y=1000,max_y=-1000,min_eye=1000,blinks=0;bool closed=false;
        for(uint64_t age=0;age<=12600;age+=33){
            dialogue_frame(v,&s,100+age);assert(title_hash()==title);
            assert(lv_obj_get_height(eyes[0])<=65);
            if(age>=500&&!closed&&lv_obj_get_height(eyes[0])<45){blinks++;closed=true;} /* Exclude the previous warm speech pose settling into LISTEN. */
            if(lv_obj_get_height(eyes[0])>55)closed=false;
            int x=lv_obj_get_x(eyes[0]),r=lv_obj_get_style_transform_rotation(body,0),y=lv_obj_get_y(body)+lv_obj_get_height(body)/2;
            if(age>=150&&age<1150){if(x<min_x)min_x=x;if(x>max_x)max_x=x;}
            if(age>=650&&age<1900){if(r<min_r)min_r=r;if(r>max_r)max_r=r;}
            if(age>=1850&&age<2900){if(y<min_y)min_y=y;if(y>max_y)max_y=y;}
            if(age>=3000&&age<3500&&lv_obj_get_height(eyes[0])<min_eye)min_eye=lv_obj_get_height(eyes[0]);
            if(age>=600&&age<633)artifact("dialogue-listen-glance");
            if(age>=1150&&age<1183)artifact("dialogue-listen-lean");
            if(age>=2350&&age<2383)artifact("dialogue-listen-nod");
        }
        assert(max_x-min_x>=3&&max_x-min_x<=6&&max_r-min_r>=15&&max_r-min_r<=35);
        assert(max_y-min_y>=2&&max_y-min_y<=4&&min_eye>=15&&min_eye<=45);
        assert(blinks==3&&action_count==actions_before&&s.dialogue==PET_DIALOGUE_LISTEN);
    }else if(!strcmp(mode,"volume")){
        v->volume=65;show_page(PET_PAGE_VOLUME);unsigned before=action_count;frame(v);
        assert(action_count==before&&v->volume==65); /* Rendering never rewrites the saved codec setting. */
        assert(!strcmp(lv_label_get_text(volume_value),"13%"));
        const int percent[]={0,1,10,20,50,100},codec[]={0,20,60,72,88,100};
        for(unsigned i=0;i<sizeof(percent)/sizeof(percent[0]);i++){
            lv_slider_set_value(volume_slider,percent[i],LV_ANIM_OFF);
            lv_obj_send_event(volume_slider,LV_EVENT_VALUE_CHANGED,NULL);
            assert(last_action.kind==PET_UI_VOLUME&&last_action.value==codec[i]);
        }
        v->volume=100;frame(v);assert(!strcmp(lv_label_get_text(volume_value),"100%"));
        v->volume=0;frame(v);assert(!strcmp(lv_label_get_text(volume_value),"0%"));
        v->volume=1;frame(v);assert(!strcmp(lv_label_get_text(volume_value),"1%"));
    }else if(!strcmp(mode,"buttons")){
        const pet_page_t single[]={PET_PAGE_VOLUME,PET_PAGE_BRIGHTNESS,PET_PAGE_CAPTIONS,PET_PAGE_MORE,PET_PAGE_PROVISION,PET_PAGE_DETAIL};
        const pet_page_t parent[]={PET_PAGE_SETTINGS,PET_PAGE_SETTINGS,PET_PAGE_SETTINGS,PET_PAGE_SETTINGS,PET_PAGE_CONNECTION,PET_PAGE_CONNECTION};
        const lv_point_t spots[]={{128,378},{233,368},{342,416},{233,438}};
        for(unsigned i=0;i<sizeof(single)/sizeof(single[0]);i++)for(unsigned j=0;j<sizeof(spots)/sizeof(spots[0]);j++){
            show_page(single[i]);frame(v);tap_at(spots[j].x,spots[j].y);only_page(parent[i]);
        }
        show_page(PET_PAGE_VOLUME);frame(v);
        lv_obj_t *b=lv_obj_get_parent(find(pages[page],"返回"));
        point(233,400,true);frame(v);assert(lv_obj_has_state(b,LV_STATE_PRESSED));
        assert(lv_color_eq(lv_obj_get_style_bg_color(b,LV_PART_MAIN),lv_color_hex(0x593247)));
        point(249,410,false);only_page(PET_PAGE_SETTINGS);
        show_page(PET_PAGE_SETTINGS);frame(v);tap_at(115,393);only_page(PET_PAGE_HOME);
        show_page(PET_PAGE_SETTINGS);frame(v);tap_at(348,393);only_page(PET_PAGE_MORE);
        show_page(PET_PAGE_CONNECTION);frame(v);tap_at(115,393);only_page(PET_PAGE_MORE);
        show_page(PET_PAGE_CONNECTION);frame(v);tap_at(348,393);only_page(PET_PAGE_PROVISION);
        show_page(PET_PAGE_HOME);frame(v);tap_at(160,435);only_page(PET_PAGE_SETTINGS);
        v->reader=(pet_reader_t){.enabled=true,.capable=true,.available=true,.active=true,.version=4,.page=1,.pages=3};
        strcpy(v->reader.text,"这是一段可以阅读的字幕。");
        show_page(PET_PAGE_READING);frame(v);tap_at(130,104);only_page(PET_PAGE_HOME);
        show_page(PET_PAGE_READING);frame(v);tap_at(150,386);assert(last_reader_action==PET_UI_READER_PREV);
        tap_at(310,386);assert(last_reader_action==PET_UI_READER_NEXT);
        v->reader.failed=true;frame(v);
        lv_area_t retry_text,meta_bounds;lv_obj_get_coords(lv_obj_get_child(reader_retry,0),&retry_text);lv_obj_get_coords(reader_meta,&meta_bounds);
        assert(retry_text.y1>=meta_bounds.y1&&retry_text.y2<=meta_bounds.y2); /* Retry label must not be cropped. */
        v->reader.failed=false;frame(v);
        artifact("firmware-easy-reading");
        show_page(PET_PAGE_SETTINGS);frame(v);artifact("firmware-easy-settings");
        show_page(PET_PAGE_VOLUME);frame(v);artifact("firmware-easy-volume");
    }else if(!strcmp(mode,"swipe")){
        const pet_page_t nested[]={PET_PAGE_SETTINGS,PET_PAGE_CONNECTION,PET_PAGE_READING,PET_PAGE_VOLUME,PET_PAGE_BRIGHTNESS,PET_PAGE_CAPTIONS,PET_PAGE_MORE,PET_PAGE_PROVISION,PET_PAGE_DETAIL};
        const pet_page_t parent[]={PET_PAGE_HOME,PET_PAGE_MORE,PET_PAGE_HOME,PET_PAGE_SETTINGS,PET_PAGE_SETTINGS,PET_PAGE_SETTINGS,PET_PAGE_SETTINGS,PET_PAGE_CONNECTION,PET_PAGE_CONNECTION};
        /* Real release reads can repeat the last pressed point. A short,
         * slightly diagonal stroke should already navigate before the lift. */
        for(unsigned i=0;i<sizeof(nested)/sizeof(nested[0]);i++){
            show_page(nested[i]);frame(v);point(120,180,true);point(145,188,true);point(184,204,true);
            only_page(parent[i]);
            point(300,207,true);only_page(parent[i]); /* One finger cannot pop two pages. */
            point(300,207,false);only_page(parent[i]);
        }
        show_page(PET_PAGE_SETTINGS);frame(v);
        point(120,180,true);point(180,216,true);only_page(PET_PAGE_HOME);point(180,216,false);
        show_page(PET_PAGE_SETTINGS);frame(v);
        point(120,90,true);point(125,115,true);point(230,116,true);point(230,116,false);
        only_page(PET_PAGE_SETTINGS); /* A stroke initially locked vertical cannot become back. */
        for(unsigned i=0;i<sizeof(nested)/sizeof(nested[0]);i++){
            show_page(nested[i]);frame(v);point(120,180,true);point(170,182,true);point(240,184,true);point(260,185,false);
            if(page!=parent[i])fprintf(stderr,"swipe from page=%d\n",nested[i]);
            only_page(parent[i]);
        }
        show_page(PET_PAGE_VOLUME);frame(v);unsigned before=action_count;
        point(130,250,true);point(210,252,true);point(300,251,false);only_page(PET_PAGE_VOLUME);assert(action_count>before);
        show_page(PET_PAGE_BRIGHTNESS);frame(v);
        point(130,250,true);point(210,252,true);point(300,251,false);only_page(PET_PAGE_BRIGHTNESS);
        show_page(PET_PAGE_READING);frame(v);
        point(230,290,true);point(231,240,true);point(232,200,false);only_page(PET_PAGE_READING);assert(lv_obj_get_scroll_y(reader_box)>0);
        lv_anim_delete(pointer,NULL);lv_obj_stop_scroll_anim(reader_box);
        show_page(PET_PAGE_SETTINGS);frame(v);
        point(130,90,true);point(165,92,false);only_page(PET_PAGE_SETTINGS); /* Short drift in blank space is not a back gesture. */
        point(330,90,true);point(220,92,true);point(180,93,false);only_page(PET_PAGE_SETTINGS);
        point(130,180,true);show_page(PET_PAGE_MORE);frame(v);point(280,183,false);only_page(PET_PAGE_MORE);
        show_page(PET_PAGE_HOME);frame(v);point(180,190,true);point(300,192,true);point(320,193,false);
        only_page(PET_PAGE_HOME);assert(last_action.kind==PET_UI_PET);
    }else if(!strcmp(mode,"dialogue")){
        pet_state_t s;pet_init(&s,0);
        pet_step(&s,(pet_event_t){.kind=PET_EV_LINK,.now_ms=1,.generation=1,.value=PET_CONN_READY});
        pet_step(&s,(pet_event_t){.kind=PET_EV_PET,.now_ms=100});
        pet_step(&s,(pet_event_t){.kind=PET_EV_PRESS,.now_ms=200});
        assert(s.dialogue==PET_DIALOGUE_LISTEN&&s.turn_warmth>0);
        v->dialogue=s.dialogue;v->pose=pet_pose(&s,200);lv_tick_inc(600);frame(v);
        assert(find(pages[page],"我在听"));
        pet_step(&s,(pet_event_t){.kind=PET_EV_RELEASE,.now_ms=500});
        v->dialogue=s.dialogue;v->pose=pet_pose(&s,1000);lv_tick_inc(33);frame(v);
        int focused=lv_obj_get_style_transform_rotation(body,0);
        v->pose=pet_pose(&s,9500);for(unsigned i=0;i<20;i++){lv_tick_inc(33);frame(v);}
        assert(lv_obj_get_style_transform_rotation(body,0)!=focused);
        pet_step(&s,(pet_event_t){.kind=PET_EV_BUFFERED,.now_ms=9600,.generation=s.generation,.turn=s.turn});
        pet_step(&s,(pet_event_t){.kind=PET_EV_PLAYING,.now_ms=9700,.generation=s.generation,.turn=s.turn,.value=600});
        pet_step(&s,(pet_event_t){.kind=PET_EV_RESPONSE_DONE,.now_ms=9800,.generation=s.generation,.turn=s.turn});
        pet_step(&s,(pet_event_t){.kind=PET_EV_DRAIN,.now_ms=9900,.generation=s.generation,.turn=s.turn});
        assert(s.dialogue==PET_DIALOGUE_IDLE);
        v->dialogue=s.dialogue;v->pose=pet_pose(&s,9950);frame(v);
        assert(v->pose.expression==PET_EXPR_IDLE&&lv_obj_has_flag(caption_box,LV_OBJ_FLAG_CLICKABLE));
        assert(strncmp(lv_label_get_text(caption),"今天也想和你一起",strlen("今天也想和你一起"))==0);
        lv_obj_send_event(caption_box,LV_EVENT_CLICKED,NULL);frame(v);only_page(PET_PAGE_READING);
        assert(strcmp(lv_label_get_text(reader_text),v->caption)==0);
        pet_step(&s,(pet_event_t){.kind=PET_EV_TICK,.now_ms=10800});
        v->pose=pet_pose(&s,10800);frame(v);only_page(PET_PAGE_READING);
        pet_ui_home();frame(v);
    }else if(!strcmp(mode,"motion")){
        lv_tick_inc(600);v->pose=(pet_pose_t){.scale_x=1,.scale_y=1,.eye_open=1,.screen_on=true};frame(v);
        assert(lv_obj_get_style_transform_rotation(body,0)==0);
        v->pose.rotation=20;lv_tick_inc(33);frame(v);
        int rotation=lv_obj_get_style_transform_rotation(body,0);assert(rotation>0&&rotation<200);
        for(unsigned i=0;i<20;i++){lv_tick_inc(33);frame(v);}
        assert(lv_obj_get_style_transform_rotation(body,0)>=198);
        v->pose.screen_on=false;frame(v);assert(!screen_on);
    }else if(!strcmp(mode,"touch")){
        show_page(PET_PAGE_HOME);frame(v);point(160,190,true);
        assert(contact_counts[1]==1&&contact_x==160&&contact_y==190);
        point(200,190,true);point(240,190,true);point(300,190,true);
        assert(contact_counts[2]==1&&contact_x==300); /*50ms movement throttle.*/
        point(320,190,false);assert(contact_counts[3]==1&&last_action.kind==PET_UI_PET);
        point(180,190,true);show_page(PET_PAGE_SETTINGS);assert(contact_counts[4]==1);
        point(180,190,false);assert(contact_counts[3]==1);
        show_page(PET_PAGE_HOME);frame(v);point(180,190,true);
        pet_ui_home();frame(v);assert(contact_counts[4]==2);point(180,190,false);
        assert(contact_counts[3]==1); /*BOOT cannot leave a held touch behind.*/
    }else assert(!"unknown ergonomic test");
    puts("PET_UI_ERGONOMICS_OK");
}
int main(int argc,char **argv){
    artifact_dir=argc>1?argv[1]:NULL;lv_init();display=lv_display_create(466,466);
    pet_init(&activity_model,0);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,framebuffer,NULL,sizeof(framebuffer),LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display,flush);
    pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(pointer,display);lv_indev_set_read_cb(pointer,read_pointer);
    assert(pet_ui_start(record,NULL)==ESP_OK);
    pet_ui_view_t v={.pose={.expression=PET_EXPR_IDLE,.scale_x=1,.scale_y=1,.eye_open=1,.screen_on=true},.connection=PET_CONN_READY,.caption_mode=PET_CAPTION_FULL,.brightness=80,.volume=72,.speaker=true,.battery=76,.wifi_online=true,.mac_online=true};
    snprintf(v.caption,sizeof(v.caption),"今天也想和你一起，慢慢发现生活里的小快乐。我们可以先喝杯水，再聊聊今天的心情。你有想分享的事，我就在这里听着。");
    frame(&v);only_page(PET_PAGE_HOME);
    if(argc>2){ergonomics(&v,argv[2]);return 0;}
    point(230,190,true);frame(&v);frame(&v);point(300,190,false);
    assert(last_action.kind==PET_UI_PET); /* A frame cannot cancel an in-flight stroke. */
    /* LVGL DOT mode replaces the overflowing tail in its own text buffer. */
    assert(!strncmp(lv_label_get_text(caption),"今天也想和你一起",strlen("今天也想和你一起")));
    assert(lv_label_get_long_mode(caption)==LV_LABEL_LONG_DOT);
    assert(lv_obj_get_style_text_font(caption,0)==pet_font(PET_FONT_BODY));
    assert(lv_obj_get_height(caption)==75&&lv_obj_get_y(caption_box)==307);
    assert(lv_obj_has_flag(caption_box,LV_OBJ_FLAG_CLICKABLE));artifact("firmware-readable-home");
    activity_now=115000;pet_step(&activity_model,(pet_event_t){.kind=PET_EV_TICK,.now_ms=activity_now});
    assert(activity_model.life==PET_LIFE_DROWSY);
    unsigned reader_wakes=wake_count;
    lv_obj_send_event(caption_box,LV_EVENT_CLICKED,NULL);frame(&v);only_page(PET_PAGE_READING);
    assert(wake_count>reader_wakes&&activity_model.last_input_ms==activity_now);
    pet_step(&activity_model,(pet_event_t){.kind=PET_EV_TICK,.now_ms=120001});
    assert(activity_model.life==PET_LIFE_AWAKE);
    assert(!strcmp(lv_label_get_text(reader_text),v.caption));
    assert(lv_obj_get_style_text_font(reader_text,0)==pet_font(PET_FONT_READER));
    assert(lv_obj_get_scroll_bottom(reader_box)>0);artifact("firmware-readable-reading");
    activity_now=200001;reader_wakes=wake_count;
    point(230,290,true);point(230,235,true);frame(&v);point(230,235,false);
    assert(wake_count>reader_wakes&&activity_model.last_input_ms==activity_now);
    pet_step(&activity_model,(pet_event_t){.kind=PET_EV_TICK,.now_ms=250000});
    assert(activity_model.life==PET_LIFE_AWAKE&&pet_pose(&activity_model,250000).screen_on);
    reader_wakes=wake_count;
    for(unsigned i=0;i<200;i++)lv_obj_send_event(reader_box,LV_EVENT_SCROLL,NULL);
    assert(wake_count==reader_wakes); /* Movement is rate-limited, not queued per pixel. */
    lv_tick_inc(1001);activity_now=251001;lv_obj_send_event(reader_box,LV_EVENT_SCROLL,NULL);
    assert(wake_count==reader_wakes+1&&activity_model.last_input_ms==activity_now);
    lv_anim_delete(pointer,NULL);lv_obj_stop_scroll_anim(reader_box);
    lv_obj_scroll_to_y(reader_box,30,LV_ANIM_OFF);frame(&v);assert(lv_obj_get_scroll_y(reader_box)==30);
    pet_ui_home();frame(&v);only_page(PET_PAGE_HOME);
    lv_obj_send_event(handle,LV_EVENT_CLICKED,NULL);frame(&v);only_page(PET_PAGE_SETTINGS);
    artifact("firmware-readable-settings");
    assert(find(pages[page],"音量"));
    lv_obj_t *volume_row=lv_obj_get_parent(find(pages[page],"音量"));
    lv_area_t bounds;lv_obj_get_coords(find(pages[page],"音量"),&bounds);
    assert(bounds.x1==lv_obj_get_x(volume_row)+16&&bounds.y1==lv_obj_get_y(volume_row)+19);
    click("音量");frame(&v);only_page(PET_PAGE_VOLUME);
    lv_slider_set_value(volume_slider,37,LV_ANIM_OFF);lv_obj_send_event(volume_slider,LV_EVENT_VALUE_CHANGED,NULL);
    assert(last_action.kind==PET_UI_VOLUME&&last_action.value==83);click("返回");
    click("亮度");frame(&v);only_page(PET_PAGE_BRIGHTNESS);
    lv_slider_set_value(brightness_slider,63,LV_ANIM_OFF);lv_obj_send_event(brightness_slider,LV_EVENT_VALUE_CHANGED,NULL);
    assert(last_action.kind==PET_UI_BRIGHTNESS&&last_action.value==63);click("返回");
    click("通话字幕");frame(&v);only_page(PET_PAGE_CAPTIONS);click("关闭");
    assert(last_action.kind==PET_UI_CAPTIONS&&last_action.value==PET_CAPTION_OFF);
    v.caption_save_failed=true;frame(&v);assert(find(pages[page],"未保存，请重试"));
    assert(presented.caption_mode==PET_CAPTION_FULL);v.caption_save_failed=false;click("返回");
    click("更多");frame(&v);only_page(PET_PAGE_MORE);click("安静陪伴");assert(last_action.kind==PET_UI_QUIET&&last_action.value==1);
    click("扬声器");assert(last_action.kind==PET_UI_SPEAKER&&last_action.value==0);
    click("连接状态");frame(&v);only_page(PET_PAGE_CONNECTION);
    click("Wi-Fi");frame(&v);only_page(PET_PAGE_DETAIL);click("返回");click("配网");frame(&v);only_page(PET_PAGE_PROVISION);
    click("开启蓝牙配网");assert(last_action.kind==PET_UI_SETUP_OPEN);
    v.setup_open=true;frame(&v);click("关闭蓝牙配网");assert(last_action.kind==PET_UI_SETUP_CLOSE);
    /* Every nested page is closed by the same externally queued BOOT route. */
    for(int i=0;i<PET_PAGE_COUNT;i++){show_page((pet_page_t)i);pet_ui_home();frame(&v);only_page(PET_PAGE_HOME);}
    for(int reason=0;reason<5;reason++){
        v.caption_mode=PET_CAPTION_FULL;v.dialogue=PET_DIALOGUE_IDLE;v.connection=PET_CONN_READY;
        snprintf(v.caption,sizeof(v.caption),"现在的回答");frame(&v);
        lv_obj_send_event(caption_box,LV_EVENT_CLICKED,NULL);frame(&v);only_page(PET_PAGE_READING);
        if(reason==0)v.caption_mode=PET_CAPTION_OFF;
        if(reason==1)v.caption[0]=0;
        if(reason==2)v.dialogue=PET_DIALOGUE_LISTEN;
        if(reason==3)v.dialogue=PET_DIALOGUE_WAIT;
        if(reason==4)v.connection=PET_CONN_OFFLINE;
        frame(&v);only_page(PET_PAGE_HOME);assert(!lv_label_get_text(reader_text)[0]);
    }
    v.connection=PET_CONN_READY;
    unsigned before=action_count;
    v.caption_mode=PET_CAPTION_OFF;v.dialogue=PET_DIALOGUE_LISTEN;frame(&v);
    assert(!strcmp(lv_label_get_text(caption),"我在听"));assert(!lv_obj_has_flag(caption_box,LV_OBJ_FLAG_CLICKABLE));
    lv_obj_send_event(caption_box,LV_EVENT_CLICKED,NULL);only_page(PET_PAGE_HOME);assert(action_count==before);
    v.caption_mode=PET_CAPTION_FULL;v.dialogue=PET_DIALOGUE_IDLE;frame(&v);
    lv_obj_send_event(caption_box,LV_EVENT_CLICKED,NULL);frame(&v);only_page(PET_PAGE_READING);
    v.connection=PET_CONN_OFFLINE;frame(&v);only_page(PET_PAGE_HOME);
    assert(lv_obj_has_flag(caption_box,LV_OBJ_FLAG_HIDDEN));assert(!lv_label_get_text(reader_text)[0]);
    v.connection=PET_CONN_READY;
    /* Private strings alter the real rendering, but never ordinary exports. */
    for(int p=0;p<3;p++){
        pet_ui_home();snprintf(v.caption,sizeof(v.caption),"私密苹果记录");v.passkey=123456;frame(&v);
        if(p==1){lv_obj_send_event(caption_box,LV_EVENT_CLICKED,NULL);frame(&v);}
        if(p==2){show_page(PET_PAGE_PROVISION);frame(&v);}
        uint64_t a=raw_hash();export_now();memcpy(previous,exported,sizeof(previous));
        snprintf(v.caption,sizeof(v.caption),"私密橙子记录");v.passkey=987654;frame(&v);
        assert(raw_hash()!=a);export_now();assert(!memcmp(previous,exported,sizeof(previous)));
    }
    pet_ui_home();v.caption_mode=PET_CAPTION_FULL;v.dialogue=PET_DIALOGUE_IDLE;v.connection=PET_CONN_READY;
    v.reader=(pet_reader_t){.enabled=true,.capable=true,.available=true,.active=true,.version=6,.page=0,.pages=3};
    snprintf(v.reader.text,sizeof(v.reader.text),"这是第一段。这里是完整的中文故事正文，可以上下滚动阅读。翻段由使用者按下大按钮，不会自己跳到下一段。");
    frame(&v);lv_obj_send_event(caption_box,LV_EVENT_CLICKED,NULL);frame(&v);only_page(PET_PAGE_READING);
    assert(last_reader_action==PET_UI_READER_OPEN);assert(!strcmp(lv_label_get_text(reader_text),v.reader.text));
    lv_obj_t *prev=lv_obj_get_parent(find(pages[page],"上一段")),*next_button=lv_obj_get_parent(find(pages[page],"下一段"));
    assert(lv_obj_get_width(prev)>=132&&lv_obj_get_height(prev)>=48);
    assert(lv_obj_get_width(next_button)>=132&&lv_obj_get_height(next_button)>=48);
    lv_obj_scroll_to_y(reader_box,30,LV_ANIM_OFF);frame(&v);
    for(unsigned phase=0;phase<3;phase++){
        v.notice=phase==0?PET_NOTICE_WORKING:PET_NOTICE_NONE;
        v.dialogue=phase==2?PET_DIALOGUE_SPEAK:PET_DIALOGUE_WAIT;
        frame(&v);only_page(PET_PAGE_READING);
        assert(!strcmp(lv_label_get_text(reader_text),v.reader.text));assert(v.reader.version==6);
        assert(lv_obj_get_scroll_y(reader_box)==30);
    }
    v.notice=PET_NOTICE_NONE;v.dialogue=PET_DIALOGUE_IDLE;lv_obj_scroll_to_y(reader_box,0,LV_ANIM_OFF);frame(&v);
    artifact("firmware-paged-reading");
    assert(lv_obj_has_state(prev,LV_STATE_DISABLED));assert(lv_obj_get_style_opa(prev,0)<LV_OPA_COVER);
    click("下一段");assert(last_reader_action==PET_UI_READER_NEXT);
    v.reader.pending=true;frame(&v);assert(!strcmp(lv_label_get_text(reader_text),v.reader.text));assert(find(pages[page],"正在加载"));
    v.reader.pending=false;v.reader.failed=true;frame(&v);click("点此重试");assert(last_reader_action==PET_UI_READER_RETRY);
    v.reader.failed=false;v.reader.page=1;strcpy(v.reader.text,"第二段的私密文字");frame(&v);
    assert(!lv_obj_has_state(prev,LV_STATE_DISABLED));click("上一段");assert(last_reader_action==PET_UI_READER_PREV);
    uint64_t first_raw=raw_hash();export_now();memcpy(previous,exported,sizeof(previous));
    v.reader.page=2;strcpy(v.reader.text,"第三段的不同私密文字");frame(&v);assert(raw_hash()!=first_raw);export_now();assert(!memcmp(previous,exported,sizeof(previous)));
    click("返回");assert(last_reader_action==PET_UI_READER_CLOSE);
    v.reader.active=false;frame(&v);only_page(PET_PAGE_HOME);
    puts("PET_UI_RENDER_OK");return 0;
}
