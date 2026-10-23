import {test} from 'node:test';import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';import {mkdtempSync,rmSync} from 'node:fs';import {tmpdir} from 'node:os';import {join} from 'node:path';
test('reader gates snapshot identity requests rate limits timeout close and new turns',()=>{
 const dir=mkdtempSync(join(tmpdir(),'pet-reader-'));
 try{execFileSync('cc',['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-Ifirmware/pet/components/pet_core','tests/pet_reader_harness.c','firmware/pet/components/pet_core/pet_reader.c','-o',join(dir,'test')]);assert.match(execFileSync(join(dir,'test'),{encoding:'utf8'}),/PET_READER_OK/);}
 finally{rmSync(dir,{recursive:true,force:true});}
});

import {existsSync} from 'node:fs';import {jsonSource} from './support/paths.mjs';
test('production reader metadata validates UTF-8 page fields and bounded request encoding',{skip:!existsSync(join(jsonSource,'cJSON.c'))&&'Build target to resolve cJSON first'},()=>{
 const dir=mkdtempSync(join(tmpdir(),'pet-reader-wire-'));
 try{execFileSync('cc',['-std=c11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined','-Ifirmware/pet/components/pet_core','-Ifirmware/pet/components/pet_link','-Ifirmware/pet/components/pet_store','-I'+jsonSource,'tests/pet_reader_wire_harness.c','firmware/pet/components/pet_link/pet_reader_wire.c','firmware/pet/components/pet_store/pet_json.c',join(jsonSource,'cJSON.c'),'-o',join(dir,'test')]);assert.match(execFileSync(join(dir,'test'),{encoding:'utf8'}),/PET_READER_WIRE_OK/);}
 finally{rmSync(dir,{recursive:true,force:true});}
});
