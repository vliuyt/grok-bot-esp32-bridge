import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {tmpdir} from 'node:os';
import {join} from 'node:path';

test('leaving settings closes the screen-owned caption popup',()=>{
  // Execute the production navigation function with the three LVGL objects it
  // owns. The dropdown list lives on the screen, outside its hidden drawer.
  const source=readFileSync('firmware/pet/components/pet_ui/pet_ui.c','utf8');
  const navigation=source.match(/static void show_page\(int next\)\{[\s\S]*?\n\}/)?.[0];
  assert.ok(navigation);
  const dir=mkdtempSync(join(tmpdir(),'pet-ui-navigation-'));
  try{
    const file=join(dir,'navigation.c'),binary=join(dir,'test');
    writeFileSync(file,`
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#define LV_OBJ_FLAG_HIDDEN 1
typedef struct { bool hidden,open; } lv_obj_t;
static lv_obj_t settings,connections,dropdown;
static lv_obj_t *drawer=&settings,*connection_page=&connections,*caption_dropdown=&dropdown;
static int page;
static void lv_obj_set_flag(lv_obj_t *obj,int flag,bool value){assert(flag==LV_OBJ_FLAG_HIDDEN);obj->hidden=value;}
void lv_dropdown_close(lv_obj_t *obj){obj->open=false;}
${navigation}
int main(void){
  assert(caption_dropdown==&dropdown);
  show_page(1);assert(!settings.hidden && connections.hidden);
  dropdown.open=true;show_page(0);
  assert(page==0 && settings.hidden && connections.hidden);
  assert(!dropdown.open && "caption popup must close when BOOT returns home");
  show_page(1);dropdown.open=true;show_page(2);
  assert(page==2 && settings.hidden && !connections.hidden);
  assert(!dropdown.open && "caption popup must close when changing pages");
  puts("PET_UI_NAVIGATION_OK");
}
`);
    execFileSync('cc',['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',file,'-o',binary]);
    assert.match(execFileSync(binary,{encoding:'utf8',stdio:['ignore','pipe','pipe']}),/PET_UI_NAVIGATION_OK/);
  }finally{rmSync(dir,{recursive:true,force:true});}
});
