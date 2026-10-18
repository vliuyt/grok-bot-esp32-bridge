import {test} from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
test('device PCM queue isolates generations and drains only after actual consumption',()=>{
  const dir=mkdtempSync(join(tmpdir(),'pet-audio-'));
  try{
    execFileSync('/usr/bin/cc',['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
      '-Ifirmware/pet/components/pet_audio','-Ifirmware/pet/components/pet_core','tests/pet_audio_harness.c',
      'firmware/pet/components/pet_audio/pet_pcm_queue.c','firmware/pet/components/pet_core/pet_core.c','-lm','-o',join(dir,'test')]);
    assert.match(execFileSync(join(dir,'test'),{encoding:'utf8'}),/PET_AUDIO_OK/);
  }finally{rmSync(dir,{recursive:true,force:true});}
});

test('same-turn clear invalidates a packet already popped by the real audio worker',()=>{
 const dir=mkdtempSync(join(tmpdir(),'pet-audio-runtime-'));try{execFileSync('cc',['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-Itests/pet_audio_fakes','-Ifirmware/pet/components/pet_audio','-Ifirmware/pet/components/pet_core','-Ifirmware/pet/components/pet_store','tests/pet_audio_runtime_harness.c','firmware/pet/components/pet_core/pet_core.c','firmware/pet/components/pet_audio/pet_pcm_queue.c','firmware/pet/components/pet_store/pet_config.c','-lm','-o',join(dir,'test')]);assert.match(execFileSync(join(dir,'test'),{encoding:'utf8'}),/SAME_TURN_CLEAR_OK/);}finally{rmSync(dir,{recursive:true,force:true});}
});
