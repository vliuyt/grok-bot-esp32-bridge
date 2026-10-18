import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync, existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
test('pet configuration migration is bounded and does not mutate legacy settings',()=>{
  const dir=mkdtempSync(join(tmpdir(),'pet-store-'));
  try {
    execFileSync('/usr/bin/cc',['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
      '-Ifirmware/pet/components/pet_store','tests/pet_store_harness.c',
      'firmware/pet/components/pet_store/pet_config.c','-o',join(dir,'test')]);
    assert.match(execFileSync(join(dir,'test'),{encoding:'utf8'}),/PET_STORE_OK/);
  }finally{rmSync(dir,{recursive:true,force:true});}
});
import {jsonSource} from './support/paths.mjs';
test('real store rejects malformed legacy JSON and failed commits without modifying legacy NVS',
  {skip:!existsSync(join(jsonSource,'cJSON.c'))&&'Run firmware/tools/build_pet.py to resolve the cJSON component first'},()=>{
  const dir=mkdtempSync(join(tmpdir(),'pet-nvs-'));
  try {
    execFileSync('/usr/bin/cc',['-std=c11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined',
      '-Itests/pet_store_fakes','-Ifirmware/pet/components/pet_store','-I'+jsonSource,
      'tests/pet_nvs_harness.c','firmware/pet/components/pet_store/pet_config.c',
      'firmware/pet/components/pet_store/pet_store.c',join(jsonSource,'cJSON.c'),'-o',join(dir,'test')]);
    for(const mode of ['0','1','2','3'])assert.match(execFileSync(join(dir,'test'),[mode],{encoding:'utf8'}),/PET_NVS_OK/);
  }finally{rmSync(dir,{recursive:true,force:true});}
});
