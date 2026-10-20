#include "pet_ui.h"
#include "pet_readability.h"
#include "pet_fonts.h"
#include "board_port.h"
#include "lvgl.h"
#include "driver/usb_serial_jtag.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static SemaphoreHandle_t view_lock,ready_signal;
static pet_ui_view_t view,presented;
static pet_ui_callback_t callback;
static void *callback_context;
static lv_obj_t *root,*pages[PET_PAGE_COUNT],*body,*eyes[2],*smiles[2];
static lv_obj_t *caption,*caption_box,*reader_box,*reader_text,*voice_hint,*handle;
static lv_obj_t *volume_slider,*brightness_slider,*volume_value,*brightness_value;
static lv_obj_t *setting_values[3],*more_values[2],*connection_values[3],*caption_buttons[3];
static lv_obj_t *caption_status,*setup_label,*passkey_label,*setup_button_text,*detail_text,*power_label;
static pet_page_t page;
static int requested_page=-1,detail_kind;
static bool initialized,screen_on=true,syncing,snapshot_requested;
static int touch_x,touch_y;
static bool touching;
static char last_summary[321];

static void action(pet_ui_action_kind_t kind,int value){if(callback)callback((pet_ui_action_t){kind,value},callback_context);}
static void text(lv_obj_t *o,const char *value){if(strcmp(lv_label_get_text(o),value))lv_label_set_text(o,value);}
static lv_obj_t *label(lv_obj_t *parent,const char *value,int x,int y,int width,pet_font_role_t role){
    lv_obj_t *o=lv_label_create(parent);
    lv_obj_set_style_text_font(o,pet_font(role),0);
    lv_obj_set_style_text_color(o,lv_color_hex(0xf5f3f5),0);
    lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_line_space(o,5,0);
    lv_obj_set_width(o,width);lv_obj_set_pos(o,x,y);lv_label_set_text(o,value);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
static lv_obj_t *container(lv_obj_t *parent,int x,int y,int width,int height){
    lv_obj_t *o=lv_obj_create(parent);lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y);lv_obj_set_size(o,width,height);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);return o;
}
static pet_readability_input_t input(void){
    return (pet_readability_input_t){presented.pose,presented.dialogue,presented.connection,
        presented.caption_mode,presented.waiting_long,presented.caption};
}
static void show_page(pet_page_t next){
    pet_readability_input_t in=input();pet_page_t target=pet_readability_page(next,&in);
    if(target!=page)touching=false;
    page=target;
    for(int i=0;i<PET_PAGE_COUNT;i++)lv_obj_set_flag(pages[i],LV_OBJ_FLAG_HIDDEN,i!=(int)page);
    lv_obj_set_flag(voice_hint,LV_OBJ_FLAG_HIDDEN,page!=PET_PAGE_HOME&&page!=PET_PAGE_READING);
    if(power_label)lv_obj_set_flag(power_label,LV_OBJ_FLAG_HIDDEN,page==PET_PAGE_HOME||page==PET_PAGE_READING);
}
static void navigate(lv_event_t *e){show_page((pet_page_t)(intptr_t)lv_event_get_user_data(e));action(PET_UI_WAKE,0);}
static void open_reader(lv_event_t *e){
    (void)e;pet_readability_input_t in=input();
    if(pet_readability_text(&in).can_read)show_page(PET_PAGE_READING);
}
static void open_detail(lv_event_t *e){detail_kind=(int)(intptr_t)lv_event_get_user_data(e);show_page(PET_PAGE_DETAIL);action(PET_UI_WAKE,0);}
static void touch(lv_event_t *event){
    if(page!=PET_PAGE_HOME)return;
    lv_event_code_t code=lv_event_get_code(event);lv_point_t point;
    lv_indev_t *indev=lv_indev_active();if(!indev)return;lv_indev_get_point(indev,&point);
    if(code==LV_EVENT_PRESSED){touch_x=point.x;touch_y=point.y;touching=true;action(PET_UI_WAKE,0);}
    else if(code==LV_EVENT_RELEASED&&touching){
        touching=false;pet_gesture_t g=pet_gesture(touch_x,touch_y,point.x,point.y);
        if(g==PET_GESTURE_SETTINGS)show_page(PET_PAGE_SETTINGS);
        else if(g==PET_GESTURE_TAP)action(PET_UI_TOUCH_TAP,0);
        else if(g==PET_GESTURE_STROKE)action(PET_UI_PET,0);
    }else if(code==LV_EVENT_PRESS_LOST)touching=false;
}
static lv_obj_t *button(lv_obj_t *parent,const char *value,int x,int y,int width,int height,lv_event_cb_t cb,int argument){
    lv_obj_t *o=lv_button_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,width,height);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x282228),0);lv_obj_set_style_radius(o,22,0);
    lv_obj_set_style_shadow_width(o,0,0);lv_obj_set_style_pad_all(o,0,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *txt=label(o,value,0,0,width,PET_FONT_BODY);lv_obj_center(txt);
    lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,(void *)(intptr_t)argument);return o;
}
static void title(pet_page_t target,const char *value){label(pages[target],value,93,49,280,PET_FONT_BODY);}
static void back(pet_page_t target,pet_page_t destination){button(pages[target],"返回",173,396,120,44,navigate,destination);}
static lv_obj_t *row(pet_page_t target,const char *name,int index,lv_event_cb_t cb,int argument,lv_obj_t **value){
    lv_obj_t *o=button(pages[target],name,87,108+index*88,292,74,cb,argument);
    if(value){
        lv_obj_t *name_label=lv_obj_get_child(o,0);lv_obj_set_align(name_label,LV_ALIGN_TOP_LEFT);
        lv_obj_set_width(name_label,150);lv_obj_set_pos(name_label,16,19);
        lv_obj_set_style_text_align(name_label,LV_TEXT_ALIGN_LEFT,0);
        *value=label(o,"",174,24,100,PET_FONT_HINT);
        lv_obj_set_style_text_color(*value,lv_color_hex(0xf29ec9),0);
    }
    return o;
}
static void slider_changed(lv_event_t *event){
    if(syncing)return;
    lv_obj_t *o=lv_event_get_target(event);
    action(o==volume_slider?PET_UI_VOLUME:PET_UI_BRIGHTNESS,lv_slider_get_value(o));
}
static lv_obj_t *slider(pet_page_t target,int minimum){
    lv_obj_t *o=lv_slider_create(pages[target]);lv_obj_set_pos(o,103,239);lv_obj_set_size(o,260,24);
    lv_slider_set_range(o,minimum,100);lv_obj_set_ext_click_area(o,22);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x443340),LV_PART_MAIN);
    lv_obj_set_style_bg_color(o,lv_color_hex(0xfa2c99),LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(o,lv_color_hex(0xf5f3f5),LV_PART_KNOB);
    lv_obj_add_event_cb(o,slider_changed,LV_EVENT_VALUE_CHANGED,NULL);return o;
}
static void toggle_changed(lv_event_t *e){
    pet_ui_action_kind_t kind=(pet_ui_action_kind_t)(intptr_t)lv_event_get_user_data(e);
    action(kind,kind==PET_UI_QUIET?!presented.quiet:!presented.speaker);
}
static void caption_changed(lv_event_t *e){action(PET_UI_CAPTIONS,(int)(intptr_t)lv_event_get_user_data(e));}
static void provision(lv_event_t *e){(void)e;action(presented.setup_open?PET_UI_SETUP_CLOSE:PET_UI_SETUP_OPEN,0);}
static void snapshot(void){
#if LV_USE_SNAPSHOT
    /* UI-task atomic redaction: private body on either page and pairing code. */
    lv_obj_t *private_objects[]={caption_box,reader_box,passkey_label};bool hidden[3];
    for(unsigned i=0;i<3;i++){hidden[i]=lv_obj_has_flag(private_objects[i],LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(private_objects[i],LV_OBJ_FLAG_HIDDEN);}
    lv_draw_buf_t *buf=lv_snapshot_take(root,LV_COLOR_FORMAT_RGB565);
    for(unsigned i=0;i<3;i++)lv_obj_set_flag(private_objects[i],LV_OBJ_FLAG_HIDDEN,hidden[i]);
    if(!buf)return;
    char line[48];int count=snprintf(line,sizeof(line),"\nSNAP BEGIN %u %u 144\n",(unsigned)buf->header.w,(unsigned)buf->header.h);
    usb_serial_jtag_write_bytes(line,count,portMAX_DELAY);unsigned char encoded[196];
    for(uint32_t y=0;y<buf->header.h;y++){
        const uint8_t *p=buf->data+y*buf->header.stride;size_t left=buf->header.w*2;
        while(left){size_t n=left>144?144:left,out=0;mbedtls_base64_encode(encoded,sizeof(encoded)-1,&out,p,n);encoded[out++]='\n';usb_serial_jtag_write_bytes(encoded,out,portMAX_DELAY);left-=n;p+=n;}
    }
    usb_serial_jtag_write_bytes("SNAP END\n",9,portMAX_DELAY);lv_draw_buf_destroy(buf);
#endif
}
static void sync_settings(void){
    char value[32];syncing=true;
    if(!lv_obj_has_state(volume_slider,LV_STATE_PRESSED))lv_slider_set_value(volume_slider,presented.volume,LV_ANIM_OFF);
    if(!lv_obj_has_state(brightness_slider,LV_STATE_PRESSED))lv_slider_set_value(brightness_slider,presented.brightness,LV_ANIM_OFF);
    syncing=false;
    snprintf(value,sizeof(value),"%d%%",presented.volume);text(setting_values[0],value);text(volume_value,value);
    snprintf(value,sizeof(value),"%d%%",presented.brightness);text(setting_values[1],value);text(brightness_value,value);
    const char *modes[]={"完整","简洁","关闭"};
    text(setting_values[2],pet_caption_mode_valid(presented.caption_mode)?modes[presented.caption_mode]:"简洁");
    for(unsigned i=0;i<3;i++)lv_obj_set_style_bg_color(caption_buttons[i],lv_color_hex(i==(unsigned)presented.caption_mode?0x593247:0x282228),0);
    text(caption_status,presented.caption_save_failed?"未保存，请重试":"");
    text(more_values[0],presented.quiet?"开启":"关闭");text(more_values[1],presented.speaker?"开启":"关闭");
    text(connection_values[0],presented.wifi_online?"已连接":"未连接");
    text(connection_values[1],presented.mac_online?"已连接":"等待中");
    text(connection_values[2],presented.connection==PET_CONN_READY?(presented.task_channel?"已就绪":"恢复中"):"未就绪");
    text(detail_text,detail_kind==0?presented.wifi:detail_kind==1?presented.mac:presented.voice);
    text(setup_label,presented.setup);text(setup_button_text,presented.setup_open?"关闭蓝牙配网":"开启蓝牙配网");
    char code[32];if(presented.passkey)snprintf(code,sizeof(code),"配对码 %06d",presented.passkey);else code[0]=0;
    text(passkey_label,code);
    char power[32];if(presented.battery>=0)snprintf(power,sizeof(power),"电量 %d%%",presented.battery);else snprintf(power,sizeof(power),"小糖");
    text(power_label,power);
}
static void draw(lv_timer_t *timer){
    (void)timer;xSemaphoreTake(view_lock,portMAX_DELAY);presented=view;
    int next=requested_page;requested_page=-1;bool snap=snapshot_requested;snapshot_requested=false;xSemaphoreGive(view_lock);
    pet_readability_input_t in=input();show_page(next>=0?(pet_page_t)next:page);
    pet_pose_t p=presented.pose;
    if(p.screen_on!=screen_on){screen_on=p.screen_on;pet_board()->panel_sleep(!screen_on);if(screen_on)lv_obj_invalidate(root);}
    if(!screen_on){if(snap)snapshot();return;}
    int brightness=p.dim?presented.brightness/4:presented.brightness;pet_board()->set_brightness(brightness<5?5:brightness);
    pet_face_t face=pet_readability_face(p);
    lv_obj_set_size(body,face.width,face.height);lv_obj_set_pos(body,face.x,face.y);
    lv_obj_set_style_transform_pivot_x(body,face.width/2,0);lv_obj_set_style_transform_pivot_y(body,face.height/2,0);
    lv_obj_set_style_transform_rotation(body,face.rotation,0);
    for(unsigned i=0;i<2;i++){
        pet_eye_t eye=face.eyes[i];lv_obj_set_pos(eyes[i],eye.x,eye.y);lv_obj_set_height(eyes[i],eye.height);
        lv_obj_set_style_transform_rotation(eyes[i],eye.rotation,0);lv_obj_set_flag(eyes[i],LV_OBJ_FLAG_HIDDEN,eye.arc);
        lv_obj_set_pos(smiles[i],eye.x-6,eye.y);lv_arc_set_bg_angles(smiles[i],eye.arc_start,eye.arc_end);
        lv_obj_set_flag(smiles[i],LV_OBJ_FLAG_HIDDEN,!eye.arc);
    }
    pet_readability_text_t copy=pet_readability_text(&in);
    if(strcmp(last_summary,copy.summary)){snprintf(last_summary,sizeof(last_summary),"%s",copy.summary);lv_label_set_text(caption,last_summary);}
    text(voice_hint,copy.hint);
    lv_obj_set_flag(caption_box,LV_OBJ_FLAG_HIDDEN,!copy.summary[0]);lv_obj_set_flag(caption_box,LV_OBJ_FLAG_CLICKABLE,copy.can_read);
    const char *full=pet_readability_page(PET_PAGE_READING,&in)==PET_PAGE_READING?presented.caption:"";
    if(strcmp(lv_label_get_text(reader_text),full)){text(reader_text,full);lv_obj_scroll_to_y(reader_box,0,LV_ANIM_OFF);}
    sync_settings();if(snap)snapshot();
}
static void build(void){
    root=lv_screen_active();lv_obj_remove_style_all(root);lv_obj_set_style_bg_color(root,lv_color_hex(0x101010),0);
    lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    for(unsigned i=0;i<PET_PAGE_COUNT;i++){
        pages[i]=container(root,0,0,466,466);lv_obj_set_style_bg_color(pages[i],lv_color_hex(0x101010),0);
        lv_obj_set_style_bg_opa(pages[i],LV_OPA_COVER,0);lv_obj_add_flag(pages[i],LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(pages[PET_PAGE_HOME],LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pages[PET_PAGE_HOME],touch,LV_EVENT_ALL,NULL);
    label(pages[PET_PAGE_HOME],"小糖",113,45,240,PET_FONT_HINT);
    body=container(pages[PET_PAGE_HOME],133,89,200,200);
    lv_obj_set_style_radius(body,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(body,lv_color_hex(0xfa2c99),0);
    lv_obj_set_style_bg_opa(body,LV_OPA_COVER,0);lv_obj_add_flag(body,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_EVENT_BUBBLE);
    for(unsigned i=0;i<2;i++){
        eyes[i]=container(body,0,0,24,60);lv_obj_set_style_radius(eyes[i],12,0);
        lv_obj_set_style_bg_color(eyes[i],lv_color_hex(0x121212),0);lv_obj_set_style_bg_opa(eyes[i],LV_OPA_COVER,0);
        lv_obj_set_style_transform_pivot_x(eyes[i],12,0);lv_obj_set_style_transform_pivot_y(eyes[i],30,0);
        smiles[i]=lv_arc_create(body);lv_obj_set_size(smiles[i],36,36);
        lv_obj_set_style_arc_color(smiles[i],lv_color_hex(0x121212),LV_PART_MAIN);lv_obj_set_style_arc_width(smiles[i],10,LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(smiles[i],true,LV_PART_MAIN);lv_obj_set_style_arc_opa(smiles[i],LV_OPA_TRANSP,LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(smiles[i],LV_OPA_TRANSP,LV_PART_KNOB);lv_obj_remove_flag(smiles[i],LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    }
    caption_box=container(pages[PET_PAGE_HOME],83,307,300,80);
    caption=label(caption_box,"",0,0,300,PET_FONT_BODY);lv_obj_set_height(caption,75);lv_label_set_long_mode(caption,LV_LABEL_LONG_DOT);
    lv_obj_add_event_cb(caption_box,open_reader,LV_EVENT_CLICKED,NULL);
    handle=button(pages[PET_PAGE_HOME],"",183,425,100,36,navigate,PET_PAGE_SETTINGS);
    lv_obj_remove_style_all(handle);lv_obj_set_pos(handle,183,425);lv_obj_set_size(handle,100,36);
    lv_obj_t *bar=container(handle,26,15,48,4);
    lv_obj_set_style_bg_color(bar,lv_color_hex(0x93858e),0);lv_obj_set_style_bg_opa(bar,LV_OPA_COVER,0);lv_obj_set_style_radius(bar,2,0);
    button(pages[PET_PAGE_READING],"返回",143,42,180,50,navigate,PET_PAGE_HOME);
    label(pages[PET_PAGE_READING],"小糖说",113,108,240,PET_FONT_HINT);
    reader_box=container(pages[PET_PAGE_READING],73,160,320,205);
    lv_obj_add_flag(reader_box,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);lv_obj_set_scroll_dir(reader_box,LV_DIR_VER);
    lv_obj_set_scrollbar_mode(reader_box,LV_SCROLLBAR_MODE_AUTO);
    reader_text=label(reader_box,"",0,0,320,PET_FONT_READER);lv_obj_set_style_text_align(reader_text,LV_TEXT_ALIGN_LEFT,0);
    voice_hint=label(root,"语音连接中",103,400,260,PET_FONT_HINT);lv_obj_set_style_text_color(voice_hint,lv_color_hex(0xc9c3ca),0);
    title(PET_PAGE_SETTINGS,"舒服一点");
    row(PET_PAGE_SETTINGS,"音量",0,navigate,PET_PAGE_VOLUME,&setting_values[0]);
    row(PET_PAGE_SETTINGS,"亮度",1,navigate,PET_PAGE_BRIGHTNESS,&setting_values[1]);
    row(PET_PAGE_SETTINGS,"通话字幕",2,navigate,PET_PAGE_CAPTIONS,&setting_values[2]);
    button(pages[PET_PAGE_SETTINGS],"返回",133,388,90,44,navigate,PET_PAGE_HOME);
    button(pages[PET_PAGE_SETTINGS],"更多",243,388,90,44,navigate,PET_PAGE_MORE);
    title(PET_PAGE_VOLUME,"音量");title(PET_PAGE_BRIGHTNESS,"亮度");
    volume_value=label(pages[PET_PAGE_VOLUME],"",113,140,240,PET_FONT_READER);
    brightness_value=label(pages[PET_PAGE_BRIGHTNESS],"",113,140,240,PET_FONT_READER);
    lv_obj_set_style_text_font(volume_value,&lv_font_montserrat_48,0);lv_obj_set_style_text_font(brightness_value,&lv_font_montserrat_48,0);
    volume_slider=slider(PET_PAGE_VOLUME,0);brightness_slider=slider(PET_PAGE_BRIGHTNESS,10);
    label(pages[PET_PAGE_VOLUME],"拖动调整音量",93,309,280,PET_FONT_HINT);
    label(pages[PET_PAGE_BRIGHTNESS],"拖动调整亮度",93,309,280,PET_FONT_HINT);
    back(PET_PAGE_VOLUME,PET_PAGE_SETTINGS);back(PET_PAGE_BRIGHTNESS,PET_PAGE_SETTINGS);
    title(PET_PAGE_CAPTIONS,"通话字幕");const char *modes[]={"完整","简洁","关闭"};
    for(int i=0;i<3;i++)caption_buttons[i]=row(PET_PAGE_CAPTIONS,modes[i],i,caption_changed,i,NULL);
    caption_status=label(pages[PET_PAGE_CAPTIONS],"",93,363,280,PET_FONT_HINT);back(PET_PAGE_CAPTIONS,PET_PAGE_SETTINGS);
    title(PET_PAGE_MORE,"更多设置");
    row(PET_PAGE_MORE,"安静陪伴",0,toggle_changed,PET_UI_QUIET,&more_values[0]);
    row(PET_PAGE_MORE,"扬声器",1,toggle_changed,PET_UI_SPEAKER,&more_values[1]);
    row(PET_PAGE_MORE,"连接状态",2,navigate,PET_PAGE_CONNECTION,NULL);back(PET_PAGE_MORE,PET_PAGE_SETTINGS);
    title(PET_PAGE_CONNECTION,"连接状态");const char *names[]={"Wi-Fi","Mac 桥接","原生语音"};
    for(int i=0;i<3;i++)row(PET_PAGE_CONNECTION,names[i],i,open_detail,i,&connection_values[i]);
    button(pages[PET_PAGE_CONNECTION],"返回",133,388,90,44,navigate,PET_PAGE_MORE);
    button(pages[PET_PAGE_CONNECTION],"配网",243,388,90,44,navigate,PET_PAGE_PROVISION);
    title(PET_PAGE_DETAIL,"连接详情");lv_obj_t *detail_box=container(pages[PET_PAGE_DETAIL],83,130,300,220);
    lv_obj_add_flag(detail_box,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);lv_obj_set_scroll_dir(detail_box,LV_DIR_VER);
    detail_text=label(detail_box,"",0,0,300,PET_FONT_BODY);back(PET_PAGE_DETAIL,PET_PAGE_CONNECTION);
    title(PET_PAGE_PROVISION,"设备配网");setup_label=label(pages[PET_PAGE_PROVISION],"",93,108,280,PET_FONT_HINT);
    lv_obj_set_height(setup_label,80);lv_label_set_long_mode(setup_label,LV_LABEL_LONG_DOT);
    passkey_label=label(pages[PET_PAGE_PROVISION],"",83,207,300,PET_FONT_READER);
    lv_obj_t *setup=button(pages[PET_PAGE_PROVISION],"开启蓝牙配网",87,280,292,74,provision,0);
    setup_button_text=lv_obj_get_child(setup,0);back(PET_PAGE_PROVISION,PET_PAGE_CONNECTION);
    power_label=label(root,"",153,18,160,PET_FONT_HINT);
    show_page(PET_PAGE_HOME);lv_timer_create(draw,33,NULL);
}
static void ui_task(void *unused){
    (void)unused;lv_indev_t *touch_device=NULL;
    if(pet_board_display_start(&touch_device)&&pet_board()->display_lock(-1)){
        build();pet_board()->display_unlock();initialized=true;
    }
    xSemaphoreGive(ready_signal);vTaskDelete(NULL);
}
esp_err_t pet_ui_start(pet_ui_callback_t cb,void *context){
    if(view_lock)return ESP_ERR_INVALID_STATE;
    callback=cb;callback_context=context;view_lock=xSemaphoreCreateMutex();ready_signal=xSemaphoreCreateBinary();
    if(!view_lock||!ready_signal)return ESP_ERR_NO_MEM;
    view.pose=(pet_pose_t){.scale_x=1,.scale_y=1,.eye_open=1,.screen_on=true};view.brightness=80;view.battery=-1;view.caption_mode=PET_CAPTION_COMPACT;
    if(xTaskCreatePinnedToCore(ui_task,"pet_ui_init",8192,NULL,5,NULL,1)!=pdPASS)return ESP_ERR_NO_MEM;
    if(xSemaphoreTake(ready_signal,pdMS_TO_TICKS(10000))!=pdTRUE)return ESP_ERR_TIMEOUT;
    return initialized?ESP_OK:ESP_FAIL;
}
void pet_ui_update(const pet_ui_view_t *next){if(view_lock){xSemaphoreTake(view_lock,portMAX_DELAY);view=*next;xSemaphoreGive(view_lock);}}
void pet_ui_home(void){pet_ui_bench_page(0);}
void pet_ui_bench_page(int next){if(view_lock&&next>=0&&next<=2){xSemaphoreTake(view_lock,portMAX_DELAY);requested_page=next;xSemaphoreGive(view_lock);}}
void pet_ui_request_snapshot(void){if(view_lock){xSemaphoreTake(view_lock,portMAX_DELAY);snapshot_requested=true;xSemaphoreGive(view_lock);}}
