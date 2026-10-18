import { test } from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { startDiscovery } from '../src/discovery.mjs';
test('Bonjour child uses only public metadata and is stopped exactly once',async()=>{
  const calls=[];
  class Child extends EventEmitter { kill(signal){calls.push(signal);queueMicrotask(()=>this.emit('exit',0));return true;} }
  const child=new Child();let command,args;
  const service=startDiscovery({port:8790,bridgeId:'a'.repeat(32),spawnFactory:(bin,argv)=>{command=bin;args=argv;return child;}});
  child.emit('spawn');assert.equal(service.status().running,true);
  assert.equal(command,'/usr/bin/dns-sd');assert(args.includes('_grokpet._tcp'));assert(args.includes('id='+'a'.repeat(32)));
  assert(args.every(x=>!x.includes('key=')&&!x.includes('小糖')));
  await service.close();await service.close();assert.deepEqual(calls,['SIGTERM']);assert.equal(service.status().running,false);
});
test('discovery failure is visible and cleanup prevents retries',async()=>{
  const child=new EventEmitter();child.kill=()=>true;let attempts=0;
  const service=startDiscovery({port:8790,bridgeId:'b'.repeat(32),spawnFactory:()=>{attempts++;return child;}});
  child.emit('error',new Error('fixture'));assert.equal(service.status().fault,'DISCOVERY_UNAVAILABLE');
  await service.close();assert.equal(attempts,1);
});
