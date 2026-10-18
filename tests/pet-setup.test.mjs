import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync,rmSync } from 'node:fs';
import {tmpdir} from 'node:os';import {join} from 'node:path';
import {existsSync} from 'node:fs';
import {stage,jsonSource} from './support/paths.mjs';
test('target configuration stacks cover actual compiled parser frames with headroom',
 {skip:(!stage||!existsSync(join(stage,'build/compile_commands.json')))&&'Build the pet target first'},()=>{
 assert.match(execFileSync('python3',['tests/pet_stack_budget.py'],{encoding:'utf8'}),/PET_STACK_BUDGET_OK/);
});
test('setup refuses unauthenticated/expired/overflow chunks and deduplicates requests',()=>{
 const dir=mkdtempSync(join(tmpdir(),'pet-setup-'));
 try {execFileSync('cc',['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-Ifirmware/pet/components/pet_provision','tests/pet_setup_harness.c','firmware/pet/components/pet_provision/pet_setup_guard.c','-o',join(dir,'test')]);assert.match(execFileSync(join(dir,'test'),{encoding:'utf8'}),/PET_SETUP_OK/);}finally{rmSync(dir,{recursive:true,force:true});}
});
import {startControlServer} from '../src/control.mjs';
test('local pairing key requires same-origin POST and a page token',async()=>{
 const key='f'.repeat(64);const server=await startControlServer({snapshot:()=>({}),command:async()=>{},setupConfig:()=>({host:'192.168.1.2',port:8790,key})});
 try{const origin=`http://127.0.0.1:${server.port}`;const page=await(await fetch(origin+'/setup')).text();assert.match(page,/navigator.bluetooth/);assert.doesNotMatch(page,new RegExp(key));const token=page.match(/const token="([a-f0-9]+)"/)[1];
 assert.equal((await fetch(origin+'/setup-config')).status,404);
 assert.equal((await fetch(origin+'/setup-config',{method:'POST',headers:{origin:'https://bad.test','x-bridge-control':token}})).status,403);
 const r=await fetch(origin+'/setup-config',{method:'POST',headers:{origin,'x-bridge-control':token}});assert.equal(r.status,200);assert.equal((await r.json()).key,key);assert.equal(r.headers.get('cache-control'),'no-store');
 }finally{await server.close();}
});
test('actual network transaction restores old configuration on wrong password or storage failure',()=>{
 const dir=mkdtempSync(join(tmpdir(),'pet-transaction-'));try{execFileSync('cc',['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-Ifirmware/pet/components/pet_network','-Ifirmware/pet/components/pet_store','tests/pet_transaction_harness.c','firmware/pet/components/pet_network/pet_transaction.c','firmware/pet/components/pet_store/pet_config.c','-o',join(dir,'test')]);assert.match(execFileSync(join(dir,'test'),{encoding:'utf8'}),/TRANSACTION_OK/);}finally{rmSync(dir,{recursive:true,force:true});}
});
test('credential/metadata parser rejects deep nesting, invalid UTF-8, trailing data and embedded NUL',{skip:!existsSync(join(jsonSource,'cJSON.c'))&&'Build the pet target or set CJSON_SOURCE_DIR first'},()=>{
 const dir=mkdtempSync(join(tmpdir(),'pet-json-'));const json=jsonSource;try{execFileSync('cc',['-std=c11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined','-Ifirmware/pet/components/pet_store','-I'+json,'tests/pet_json_harness.c','firmware/pet/components/pet_store/pet_json.c',join(json,'cJSON.c'),'-o',join(dir,'test')]);assert.match(execFileSync(join(dir,'test'),{encoding:'utf8'}),/PET_JSON_OK/);}finally{rmSync(dir,{recursive:true,force:true});}
});
