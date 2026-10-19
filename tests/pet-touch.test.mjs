import {test} from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
test('touch hold/release/cancel and BOOT priority share one safe voice turn',()=>{
 const dir=mkdtempSync(join(tmpdir(),'pet-touch-'));
 try{
  execFileSync('cc',['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-Ifirmware/pet/components/pet_core','tests/pet_touch_harness.c','firmware/pet/components/pet_core/pet_core.c','-lm','-o',join(dir,'test')]);
  assert.match(execFileSync(join(dir,'test'),{encoding:'utf8'}),/PET_TOUCH_OK/);
 }finally{rmSync(dir,{recursive:true,force:true});}
});
