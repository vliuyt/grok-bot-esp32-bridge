import {test} from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync,spawnSync} from 'node:child_process';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';

test('local contact reacts before lift, follows movement, distinguishes taps and preserves voice priority',()=>{
  const dir=mkdtempSync(join(tmpdir(),'pet-polish-'));
  try{
    execFileSync('cc',['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
      '-Ifirmware/pet/components/pet_core','tests/pet_polish_harness.c',
      'firmware/pet/components/pet_core/pet_core.c','firmware/pet/components/pet_core/pet_face.c',
      '-lm','-o',join(dir,'test')],{stdio:'pipe'});
    const results=['all','tap-window','quick-stroke'].map(mode=>({mode,...spawnSync(join(dir,'test'),[mode],{encoding:'utf8'})}));
    assert.deepEqual(results.filter(r=>r.status!==0).map(r=>`${r.mode}: ${r.stderr}`),[]);
    for(const r of results)assert.match(r.stdout,/PET_POLISH_OK/);
  }finally{rmSync(dir,{recursive:true,force:true});}
});
