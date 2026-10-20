#include "lvgl.h"
#include "pet_fonts.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

extern const lv_font_t muse_font_cjk_16;
static uint16_t pixels[466*466];
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *map){
    (void)area;(void)map;lv_display_flush_ready(display);
}
static void coverage(uint32_t start,uint32_t end){
    for(uint32_t cp=start;cp<=end;cp++){
        lv_font_glyph_dsc_t original={0},current={0};
        if(!muse_font_cjk_16.get_glyph_dsc(&muse_font_cjk_16,&original,cp,0))continue;
        assert(lv_font_get_glyph_dsc(pet_font(PET_FONT_BODY),&current,cp,0));
        assert(!current.is_placeholder);
    }
}
int main(void){
    lv_init();
    const lv_font_t *body=pet_font(PET_FONT_BODY),*reader=pet_font(PET_FONT_READER);
    lv_font_glyph_dsc_t glyph={0};
    assert(lv_font_get_glyph_dsc(body,&glyph,0x4e2d,0)&&!glyph.is_placeholder);
    assert(glyph.box_h>=23&&body->line_height<=40);
    assert(lv_font_get_glyph_dsc(reader,&glyph,0x4e2d,0)&&!glyph.is_placeholder);
    assert(glyph.box_h>=25&&reader->line_height<=42);
    coverage(0x3000,0x30ff);coverage(0x4e00,0x9fff);coverage(0xff00,0xffef);
    for(uint32_t cp=32;cp<127;cp++){
        assert(lv_font_get_glyph_dsc(body,&glyph,cp,0)&&!glyph.is_placeholder);
    }
    lv_display_t *display=lv_display_create(466,466);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display,flush);
    lv_obj_t *root=lv_screen_active();lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root,lv_color_hex(0x101010),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
    lv_obj_t *label=lv_label_create(root);lv_obj_set_style_text_font(label,body,0);
    lv_obj_set_style_text_color(label,lv_color_white(),0);lv_label_set_text(label,"中文苹果小糖");lv_obj_set_pos(label,80,100);
    lv_refr_now(display);
    unsigned gray=0,bright=0;
    for(unsigned i=0;i<466*466;i++){
        if(pixels[i]==0xffff)bright++;
        else if(pixels[i]!=0x1082&&pixels[i]!=0)gray++;
    }
    assert(bright>100&&gray>100);
    puts("PET_FONT_RENDER_OK");
    return 0;
}
