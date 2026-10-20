import {test} from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';

test('caption modes preserve UTF-8 and first-sentence boundaries in a bounded window',()=>{
  const dir=mkdtempSync(join(tmpdir(),'pet-caption-'));
  try{
    execFileSync('cc',['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
      '-Ifirmware/pet/components/pet_core','tests/pet_caption_harness.c',
      'firmware/pet/components/pet_core/pet_caption.c','-o',join(dir,'test')]);
    assert.match(execFileSync(join(dir,'test'),{encoding:'utf8'}),/PET_CAPTION_OK/);
  }finally{rmSync(dir,{recursive:true,force:true});}
});
