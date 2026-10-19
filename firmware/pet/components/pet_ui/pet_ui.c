#include "pet_ui.h"
#include "board_port.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "driver/usb_serial_jtag.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

extern const lv_font_t muse_font_cjk_16;
static lv_font_t font;
static SemaphoreHandle_t view_lock,ready_signal;
static pet_ui_view_t view;
static pet_ui_callback_t callback;
static void *callback_context;
static lv_obj_t *root,*body,*eyes[2],*smiles[2],*caption,*handle,*drawer,*connection_page;
static lv_obj_t *talk_button,*talk_label,*talk_hint;
static lv_obj_t *volume_slider,*brightness_slider,*quiet_switch,*captions_switch,*speaker_switch;
static lv_obj_t *network_label,*bridge_label,*voice_label,*setup_label,*passkey_label,*power_label;
static bool initialized,screen_on=true,syncing;
static int page,requested_page=-1;
static bool snapshot_requested;
static bool talk_touching;
static int requested_talk=-1;
static int touch_x,touch_y;
static bool touching;
static char last_caption[321];
static char last_entry[64],last_hint[64];
static void action(pet_ui_action_kind_t kind,int value){if(callback)callback((pet_ui_action_t){kind,value},callback_context);}
static lv_obj_t *label(lv_obj_t *parent,const char *text,int width){
    lv_obj_t *o=lv_label_create(parent);lv_obj_set_style_text_font(o,&font,0);
    lv_obj_set_style_text_color(o,lv_color_hex(0xd7cdd3),0);lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);
    if(width){lv_obj_set_width(o,width);}
    lv_label_set_text(o,text);return o;
}
static void show_page(int next){
    if(next&&talk_touching){talk_touching=false;action(PET_UI_TALK_CANCEL,0);}
    page=next;lv_obj_set_flag(drawer,LV_OBJ_FLAG_HIDDEN,next!=1);lv_obj_set_flag(connection_page,LV_OBJ_FLAG_HIDDEN,next!=2);
}
static void talk(lv_event_t *event){
    lv_event_code_t code=lv_event_get_code(event);
    if(code==LV_EVENT_PRESSED&&!talk_touching){talk_touching=true;action(PET_UI_TALK_PRESS,0);}
    else if((code==LV_EVENT_RELEASED||code==LV_EVENT_PRESS_LOST)&&talk_touching){
        talk_touching=false;
        action(code==LV_EVENT_RELEASED?PET_UI_TALK_RELEASE:PET_UI_TALK_CANCEL,0);
    }
}
static void touch(lv_event_t *event){
    lv_event_code_t code=lv_event_get_code(event);lv_point_t point;
    lv_indev_t *indev=lv_indev_active();if(!indev)return;lv_indev_get_point(indev,&point);
    if(code==LV_EVENT_PRESSED){touch_x=point.x;touch_y=point.y;touching=true;action(PET_UI_WAKE,0);}
    else if(code==LV_EVENT_RELEASED&&touching){
        touching=false;pet_gesture_t g=pet_gesture(touch_x,touch_y,point.x,point.y);
        if(g==PET_GESTURE_SETTINGS)show_page(1);
        else if(g==PET_GESTURE_TAP||g==PET_GESTURE_STROKE)action(PET_UI_PET,0);
    }else if(code==LV_EVENT_PRESS_LOST)touching=false;
}
static void open_drawer(lv_event_t *event){(void)event;show_page(1);action(PET_UI_WAKE,0);}
static void home(lv_event_t *event){(void)event;show_page(0);action(PET_UI_WAKE,0);}
static void connections(lv_event_t *event){(void)event;show_page(2);action(PET_UI_WAKE,0);}
static void provision(lv_event_t *event){(void)event;action(PET_UI_SETUP_OPEN,0);}
static void close_provision(lv_event_t *event){(void)event;action(PET_UI_SETUP_CLOSE,0);}
static lv_obj_t *button(lv_obj_t *parent,const char *text,int width,lv_event_cb_t cb){
    lv_obj_t *o=lv_button_create(parent);lv_obj_set_size(o,width,42);lv_obj_set_style_bg_color(o,lv_color_hex(0x32272e),0);
    lv_obj_set_style_radius(o,21,0);lv_obj_set_style_shadow_width(o,0,0);lv_obj_t *txt=label(o,text,0);lv_obj_center(txt);
    lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,NULL);return o;
}
static void slider_changed(lv_event_t *event){
    if(syncing)return;
    lv_obj_t *o=lv_event_get_target(event);
    if(lv_event_get_code(event)==LV_EVENT_VALUE_CHANGED)action(o==volume_slider?PET_UI_VOLUME:PET_UI_BRIGHTNESS,lv_slider_get_value(o));
}
static lv_obj_t *slider(lv_obj_t *parent,const char *text,int y,int min,int max){
    lv_obj_t *txt=label(parent,text,100);lv_obj_set_pos(txt,110,y);
    lv_obj_t *s=lv_slider_create(parent);lv_obj_set_size(s,220,8);lv_obj_set_pos(s,123,y+33);
    lv_slider_set_range(s,min,max);lv_obj_set_style_bg_color(s,lv_color_hex(0x4b3341),LV_PART_MAIN);
    lv_obj_set_style_bg_color(s,lv_color_hex(0xfa2c99),LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s,lv_color_hex(0xefc9dd),LV_PART_KNOB);
    lv_obj_add_event_cb(s,slider_changed,LV_EVENT_VALUE_CHANGED,NULL);return s;
}
static void switch_changed(lv_event_t *event){
    if(syncing)return;
    lv_obj_t *o=lv_event_get_target(event);
    pet_ui_action_kind_t kind=o==quiet_switch?PET_UI_QUIET:o==captions_switch?PET_UI_CAPTIONS:PET_UI_SPEAKER;
    action(kind,lv_obj_has_state(o,LV_STATE_CHECKED));
}
static lv_obj_t *toggle(lv_obj_t *parent,const char *text,int y){
    lv_obj_t *txt=label(parent,text,120);lv_obj_set_pos(txt,115,y+4);
    lv_obj_t *o=lv_switch_create(parent);lv_obj_set_size(o,48,25);lv_obj_set_pos(o,280,y);
    lv_obj_set_style_bg_color(o,lv_color_hex(0xfa2c99),LV_PART_INDICATOR|LV_STATE_CHECKED);
    lv_obj_add_event_cb(o,switch_changed,LV_EVENT_VALUE_CHANGED,NULL);return o;
}
static lv_obj_t *overlay(void){
    lv_obj_t *o=lv_obj_create(root);lv_obj_remove_style_all(o);lv_obj_set_size(o,466,466);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x151215),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);return o;
}
static void set_checked(lv_obj_t *o,bool on){lv_obj_set_state(o,LV_STATE_CHECKED,on);}
static void snapshot(void){
#if LV_USE_SNAPSHOT
    /* A single UI-task operation: no caption or pairing code may enter exported pixels. */
    bool caption_hidden=lv_obj_has_flag(caption,LV_OBJ_FLAG_HIDDEN);
    bool code_hidden=lv_obj_has_flag(passkey_label,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(caption,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(passkey_label,LV_OBJ_FLAG_HIDDEN);
    lv_draw_buf_t *buf=lv_snapshot_take(root,LV_COLOR_FORMAT_RGB565);
    lv_obj_set_flag(caption,LV_OBJ_FLAG_HIDDEN,caption_hidden);lv_obj_set_flag(passkey_label,LV_OBJ_FLAG_HIDDEN,code_hidden);
    if(!buf)return;
    char line[48];int count=snprintf(line,sizeof(line),"\nSNAP BEGIN %u %u 144\n",(unsigned)buf->header.w,(unsigned)buf->header.h);
    usb_serial_jtag_write_bytes(line,count,portMAX_DELAY);
    unsigned char encoded[196];
    for(uint32_t y=0;y<buf->header.h;y++){
        const uint8_t *p=buf->data+y*buf->header.stride;size_t left=buf->header.w*2;
        while(left){size_t n=left>144?144:left,out=0;mbedtls_base64_encode(encoded,sizeof(encoded)-1,&out,p,n);encoded[out++]='\n';usb_serial_jtag_write_bytes(encoded,out,portMAX_DELAY);left-=n;p+=n;}
    }
    usb_serial_jtag_write_bytes("SNAP END\n",9,portMAX_DELAY);lv_draw_buf_destroy(buf);
#endif
}
static void draw(lv_timer_t *timer){
    (void)timer;pet_ui_view_t current;
    xSemaphoreTake(view_lock,portMAX_DELAY);current=view;
    int next=requested_page;requested_page=-1;bool snap=snapshot_requested;snapshot_requested=false;
    int talk_edge=requested_talk;requested_talk=-1;
    xSemaphoreGive(view_lock);
    if(next>=0)show_page(next);
    if(talk_edge>=0&&page==0){
        lv_event_code_t code=talk_edge==0?LV_EVENT_PRESSED:talk_edge==1?LV_EVENT_RELEASED:LV_EVENT_PRESS_LOST;
        lv_obj_send_event(talk_button,code,NULL);
    }
    pet_pose_t p=current.pose;
    if(p.screen_on!=screen_on){
        screen_on=p.screen_on;pet_board()->panel_sleep(!screen_on);
        if(screen_on)lv_obj_invalidate(root);
    }
    if(!screen_on){if(snap)snapshot();return;}
    int brightness=p.dim?current.brightness/4:current.brightness;
    pet_board()->set_brightness(brightness<5?5:brightness);
    int w=(int)(224*p.scale_x),h=(int)(224*p.scale_y);
    lv_obj_set_size(body,w,h);lv_obj_set_pos(body,(466-w)/2,200-h/2+(int)p.offset_y);
    lv_obj_set_style_transform_pivot_x(body,w/2,0);lv_obj_set_style_transform_pivot_y(body,h/2,0);
    lv_obj_set_style_transform_rotation(body,(int)(p.rotation*10),0);
    bool happy=p.expression==PET_EXPR_HAPPY||p.expression==PET_EXPR_SLEEP;
    for(unsigned i=0;i<2;i++){
        int x=(int)(w*(i?.5223f:.2411f))+(int)p.look_x;
        lv_obj_set_pos(eyes[i],x,(int)(h*.29f));lv_obj_set_height(eyes[i],(int)fmaxf(5,60*p.eye_open));
        lv_obj_set_flag(eyes[i],LV_OBJ_FLAG_HIDDEN,happy);
        lv_obj_set_pos(smiles[i],x-6,(int)(h*.29f));lv_obj_set_flag(smiles[i],LV_OBJ_FLAG_HIDDEN,!happy);
    }
    const char *text=current.captions?current.caption:"";
    if(current.dialogue==PET_DIALOGUE_LISTEN)text="嗯，我在听";
    else if(current.dialogue==PET_DIALOGUE_WAIT)text=current.waiting_long?"稍等一下，还在准备回答":"让我想想";
    else if(p.expression==PET_EXPR_HAPPY)text="再摸一下嘛";
    else if(p.expression==PET_EXPR_GREET&&p.invitation)text="嘿，我在这里";
    else if(current.dialogue==PET_DIALOGUE_IDLE)text="";
    if(strcmp(last_caption,text)){snprintf(last_caption,sizeof(last_caption),"%s",text);lv_label_set_text(caption,last_caption);}
    lv_obj_set_flag(caption,LV_OBJ_FLAG_HIDDEN,!text[0]);
    bool available=current.connection==PET_CONN_READY;
    bool listening=current.dialogue==PET_DIALOGUE_LISTEN;
    const char *entry=available?"按住说话":"连接后按住说话";
    if(listening)entry="松开提交";
    else if(current.held&&!available)entry="连接中，松开取消";
    else if(available&&current.dialogue!=PET_DIALOGUE_IDLE)entry="按住打断";
    if(strcmp(last_entry,entry)){
        snprintf(last_entry,sizeof(last_entry),"%s",entry);lv_label_set_text(talk_label,last_entry);
        uint32_t background=listening?0xfa2c99:available?0x32272e:0x242124;
        lv_obj_set_style_bg_color(talk_button,lv_color_hex(background),0);
        lv_obj_set_style_bg_color(talk_button,lv_color_hex(background),LV_STATE_PRESSED);
        lv_obj_set_style_border_color(talk_button,lv_color_hex(available?0xfa2c99:0x494146),0);
        lv_obj_set_style_text_color(talk_label,lv_color_hex(listening?0x101010:0xeee4eb),0);
    }
    const char *hint=listening?(current.touch_recording?"滑出按钮可取消":"松开 BOOT 后提交"):"也可按住 BOOT";
    if(strcmp(last_hint,hint)){snprintf(last_hint,sizeof(last_hint),"%s",hint);lv_label_set_text(talk_hint,last_hint);}
    if(page==1){
        syncing=true;lv_slider_set_value(volume_slider,current.volume,LV_ANIM_OFF);
        lv_slider_set_value(brightness_slider,current.brightness,LV_ANIM_OFF);
        set_checked(quiet_switch,current.quiet);set_checked(captions_switch,current.captions);set_checked(speaker_switch,current.speaker);syncing=false;
    }
    if(page==2){
        lv_label_set_text(network_label,current.wifi);lv_label_set_text(bridge_label,current.mac);lv_label_set_text(voice_label,current.voice);
        lv_label_set_text(setup_label,current.setup);
        char code[32];if(current.passkey)snprintf(code,sizeof(code),"配对码 %06d",current.passkey);else code[0]=0;
        lv_label_set_text(passkey_label,code);
    }
    char battery[32];if(current.battery>=0)snprintf(battery,sizeof(battery),"电量 %d%%",current.battery);else snprintf(battery,sizeof(battery),"小糖");
    if(page)lv_label_set_text(power_label,battery);
    if(snap)snapshot();
}
static void build(void){
    root=lv_screen_active();lv_obj_remove_style_all(root);lv_obj_set_style_bg_color(root,lv_color_hex(0x101010),0);
    lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(root,touch,LV_EVENT_ALL,NULL);
    body=lv_obj_create(root);lv_obj_remove_style_all(body);lv_obj_set_size(body,224,224);lv_obj_set_pos(body,121,88);
    lv_obj_set_style_radius(body,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(body,lv_color_hex(0xfa2c99),0);
    lv_obj_set_style_bg_opa(body,LV_OPA_COVER,0);lv_obj_add_flag(body,LV_OBJ_FLAG_EVENT_BUBBLE);lv_obj_remove_flag(body,LV_OBJ_FLAG_SCROLLABLE);
    for(unsigned i=0;i<2;i++){
        eyes[i]=lv_obj_create(body);lv_obj_remove_style_all(eyes[i]);lv_obj_set_size(eyes[i],24,60);
        lv_obj_set_style_radius(eyes[i],12,0);lv_obj_set_style_bg_color(eyes[i],lv_color_hex(0x121212),0);
        lv_obj_set_style_bg_opa(eyes[i],LV_OPA_COVER,0);lv_obj_remove_flag(eyes[i],LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_transform_pivot_x(eyes[i],12,0);lv_obj_set_style_transform_pivot_y(eyes[i],30,0);lv_obj_set_style_transform_rotation(eyes[i],50,0);
        smiles[i]=lv_arc_create(body);lv_obj_set_size(smiles[i],36,36);lv_arc_set_bg_angles(smiles[i],200,340);
        lv_obj_set_style_arc_color(smiles[i],lv_color_hex(0x121212),LV_PART_MAIN);lv_obj_set_style_arc_width(smiles[i],10,LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(smiles[i],true,LV_PART_MAIN);lv_obj_set_style_arc_opa(smiles[i],LV_OPA_TRANSP,LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(smiles[i],LV_OPA_TRANSP,LV_PART_KNOB);lv_obj_remove_flag(smiles[i],LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(smiles[i],LV_OBJ_FLAG_HIDDEN);
    }
    caption=label(root,"",300);lv_obj_set_pos(caption,83,324);lv_obj_set_height(caption,38);lv_label_set_long_mode(caption,LV_LABEL_LONG_WRAP);
    talk_button=lv_button_create(root);lv_obj_set_size(talk_button,220,48);lv_obj_set_pos(talk_button,123,370);
    lv_obj_set_style_radius(talk_button,24,0);lv_obj_set_style_shadow_width(talk_button,0,0);
    lv_obj_set_style_bg_color(talk_button,lv_color_hex(0x242124),0);lv_obj_set_style_border_width(talk_button,1,0);
    lv_obj_remove_flag(talk_button,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_EVENT_BUBBLE|LV_OBJ_FLAG_PRESS_LOCK);
    talk_label=label(talk_button,"连接后按住说话",0);lv_obj_center(talk_label);
    lv_obj_add_event_cb(talk_button,talk,LV_EVENT_ALL,NULL);
    talk_hint=label(root,"也可按住 BOOT",220);lv_obj_set_pos(talk_hint,123,423);
    lv_obj_set_style_text_color(talk_hint,lv_color_hex(0x8b7f87),0);
    handle=lv_button_create(root);lv_obj_remove_style_all(handle);lv_obj_set_size(handle,60,20);lv_obj_align(handle,LV_ALIGN_BOTTOM_MID,0,-3);
    lv_obj_t *bar=lv_obj_create(handle);lv_obj_remove_style_all(bar);lv_obj_set_size(bar,34,3);lv_obj_center(bar);
    lv_obj_set_style_bg_color(bar,lv_color_hex(0x494146),0);lv_obj_set_style_bg_opa(bar,LV_OPA_COVER,0);lv_obj_set_style_radius(bar,2,0);lv_obj_remove_flag(bar,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(handle,open_drawer,LV_EVENT_CLICKED,NULL);
    drawer=overlay();lv_obj_t *title=label(drawer,"舒服一点",200);lv_obj_set_pos(title,133,63);
    volume_slider=slider(drawer,"音量",102,0,100);brightness_slider=slider(drawer,"亮度",164,10,100);
    quiet_switch=toggle(drawer,"安静陪伴",225);captions_switch=toggle(drawer,"通话字幕",265);speaker_switch=toggle(drawer,"扬声器",305);
    lv_obj_t *connection=button(drawer,"连接与配网",150,connections);lv_obj_set_pos(connection,158,350);
    lv_obj_t *back=button(drawer,"返回",70,home);lv_obj_set_pos(back,198,402);
    connection_page=overlay();title=label(connection_page,"连接与配网",240);lv_obj_set_pos(title,113,58);
    network_label=label(connection_page,"",290);lv_obj_set_pos(network_label,88,106);
    bridge_label=label(connection_page,"",290);lv_obj_set_pos(bridge_label,88,143);
    voice_label=label(connection_page,"",290);lv_obj_set_pos(voice_label,88,180);
    setup_label=label(connection_page,"",290);lv_obj_set_pos(setup_label,88,220);
    passkey_label=label(connection_page,"",250);lv_obj_set_pos(passkey_label,108,258);
    lv_obj_t *setup=button(connection_page,"开启蓝牙配网",168,provision);lv_obj_set_pos(setup,149,294);
    lv_obj_t *stop=button(connection_page,"关闭配网",130,close_provision);lv_obj_set_pos(stop,168,345);
    back=button(connection_page,"返回小糖",120,home);lv_obj_set_pos(back,173,396);
    power_label=label(drawer,"",130);lv_obj_set_pos(power_label,168,28);
    lv_timer_create(draw,33,NULL);
}
static void ui_task(void *unused){
    (void)unused;font=lv_font_montserrat_16;font.fallback=&muse_font_cjk_16;
    lv_indev_t *touch_device=NULL;
    if(pet_board_display_start(&touch_device)&&pet_board()->display_lock(-1)){
        build();pet_board()->display_unlock();initialized=true;
    }
    xSemaphoreGive(ready_signal);vTaskDelete(NULL);
}
esp_err_t pet_ui_start(pet_ui_callback_t cb,void *context){
    if(view_lock)return ESP_ERR_INVALID_STATE;
    callback=cb;callback_context=context;view_lock=xSemaphoreCreateMutex();ready_signal=xSemaphoreCreateBinary();
    if(!view_lock||!ready_signal)return ESP_ERR_NO_MEM;
    view.pose=(pet_pose_t){.scale_x=1,.scale_y=1,.eye_open=1,.screen_on=true};view.brightness=80;view.battery=-1;
    if(xTaskCreatePinnedToCore(ui_task,"pet_ui_init",8192,NULL,5,NULL,1)!=pdPASS)return ESP_ERR_NO_MEM;
    if(xSemaphoreTake(ready_signal,pdMS_TO_TICKS(10000))!=pdTRUE)return ESP_ERR_TIMEOUT;
    return initialized?ESP_OK:ESP_FAIL;
}
void pet_ui_update(const pet_ui_view_t *next){if(view_lock){xSemaphoreTake(view_lock,portMAX_DELAY);view=*next;xSemaphoreGive(view_lock);}}
void pet_ui_home(void){pet_ui_bench_page(0);}
void pet_ui_bench_page(int next){if(view_lock&&next>=0&&next<=2){xSemaphoreTake(view_lock,portMAX_DELAY);requested_page=next;xSemaphoreGive(view_lock);}}
void pet_ui_request_snapshot(void){if(view_lock){xSemaphoreTake(view_lock,portMAX_DELAY);snapshot_requested=true;xSemaphoreGive(view_lock);}}
bool pet_ui_bench_talk(int edge){
    if(!view_lock||edge<0||edge>2)return false;
    xSemaphoreTake(view_lock,portMAX_DELAY);bool accepted=requested_talk<0;
    if(accepted)requested_talk=edge;
    xSemaphoreGive(view_lock);return accepted;
}
