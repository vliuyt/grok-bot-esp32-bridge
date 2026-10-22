import {test} from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
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
    assert.match(execFileSync(join(dir,'test'),{encoding:'utf8'}),/PET_POLISH_OK/);
  }finally{rmSync(dir,{recursive:true,force:true});}
});
