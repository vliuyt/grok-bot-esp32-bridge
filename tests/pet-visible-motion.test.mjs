import {test} from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync,spawnSync} from 'node:child_process';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';

test('33ms production motion remains visible after real round-screen pixel quantization',()=>{
  const dir=mkdtempSync(join(tmpdir(),'pet-visible-motion-'));
  try{
    execFileSync('/usr/bin/cc',['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',
      '-Ifirmware/pet/components/pet_core','tests/pet_visible_motion_harness.c',
      ...['pet_core','pet_face','pet_caption','pet_readability'].map(n=>`firmware/pet/components/pet_core/${n}.c`),
      '-lm','-o',join(dir,'test')],{stdio:'pipe'});
    const results=['listen','warmth','wait','speech','finish','bounds'].map(mode=>({mode,...spawnSync(join(dir,'test'),[mode],{encoding:'utf8'})}));
    assert.deepEqual(results.filter(r=>r.status!==0).map(r=>`${r.mode}: ${r.stdout}${r.stderr}`),[]);
    for(const r of results)assert.match(r.stdout,/PET_VISIBLE_MOTION_OK/);
  }finally{rmSync(dir,{recursive:true,force:true});}
});
