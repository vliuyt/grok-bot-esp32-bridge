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
    if(event.kind>=PET_UI_READER_OPEN)last_reader_action=event.kind;
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
int main(int argc,char **argv){
    artifact_dir=argc>1?argv[1]:NULL;lv_init();display=lv_display_create(466,466);
    pet_init(&activity_model,0);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,framebuffer,NULL,sizeof(framebuffer),LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display,flush);
    pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(pointer,display);lv_indev_set_read_cb(pointer,read_pointer);
    assert(pet_ui_start(record,NULL)==ESP_OK);
    pet_ui_view_t v={.pose={.expression=PET_EXPR_IDLE,.scale_x=1,.scale_y=1,.eye_open=1,.screen_on=true},.connection=PET_CONN_READY,.caption_mode=PET_CAPTION_FULL,.brightness=80,.volume=20,.speaker=true,.battery=76,.wifi_online=true,.mac_online=true};
    snprintf(v.caption,sizeof(v.caption),"今天也想和你一起，慢慢发现生活里的小快乐。我们可以先喝杯水，再聊聊今天的心情。你有想分享的事，我就在这里听着。");
    frame(&v);only_page(PET_PAGE_HOME);
    point(230,190,true);frame(&v);frame(&v);point(300,190,false);
    assert(last_action.kind==PET_UI_PET); /* A frame cannot cancel an in-flight stroke. */
    /* LVGL DOT mode replaces the overflowing tail in its own text buffer. */
    assert(!strncmp(lv_label_get_text(caption),"今天也想和你一起",strlen("今天也想和你一起")));
    assert(lv_label_get_long_mode(caption)==LV_LABEL_LONG_DOT);
    assert(lv_obj_get_style_text_font(caption,0)==pet_font(PET_FONT_BODY));
    assert(lv_obj_get_height(caption)==75&&lv_obj_get_y(caption_box)==307);
    assert(lv_obj_get_y(handle)==425&&lv_obj_get_width(handle)==100);
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
    assert(last_action.kind==PET_UI_VOLUME&&last_action.value==37);click("返回");
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
    assert(lv_obj_has_state(prev,LV_STATE_DISABLED));click("下一段");assert(last_reader_action==PET_UI_READER_NEXT);
    v.reader.pending=true;frame(&v);assert(!strcmp(lv_label_get_text(reader_text),v.reader.text));assert(find(pages[page],"正在加载"));
    v.reader.pending=false;v.reader.failed=true;frame(&v);click("点此重试");assert(last_reader_action==PET_UI_READER_RETRY);
    v.reader.failed=false;v.reader.page=1;strcpy(v.reader.text,"第二段的私密文字");frame(&v);
    assert(!lv_obj_has_state(prev,LV_STATE_DISABLED));click("上一段");assert(last_reader_action==PET_UI_READER_PREV);
    uint64_t first_raw=raw_hash();export_now();memcpy(previous,exported,sizeof(previous));
    v.reader.page=2;strcpy(v.reader.text,"第三段的不同私密文字");frame(&v);assert(raw_hash()!=first_raw);export_now();assert(!memcmp(previous,exported,sizeof(previous)));
    artifact("firmware-paged-reading");click("返回");assert(last_reader_action==PET_UI_READER_CLOSE);
    v.reader.active=false;frame(&v);only_page(PET_PAGE_HOME);
    puts("PET_UI_RENDER_OK");return 0;
}
