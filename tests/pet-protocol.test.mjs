import { test } from 'node:test';
import assert from 'node:assert/strict';
import { petMessage, encodePetMessage, shortCaption, bridgeIdentity, privateLanPeer } from '../src/pet-protocol.mjs';
import {FrameDecoder,encodeFrame} from '../src/device-wire.mjs';
test('pet protocol validates negotiated identity and UTF-8 without breaking legacy control',()=>{
  assert.equal(petMessage(Buffer.from('press')),null);
  assert.deepEqual(petMessage(Buffer.from('{"op":"hello","v":2,"generation":7}')),{op:'hello',v:2,generation:7});
  assert.throws(()=>petMessage(Buffer.from('{"op":"hello","v":3,"generation":7}')),/PET_PROTOCOL/);
  assert.throws(()=>petMessage(Buffer.alloc(1025,32)),/PET_SIZE/);
  assert.throws(()=>petMessage(Buffer.from([123,34,120,34,58,34,255,34,125])),/PET_UTF8/);
  assert.equal(shortCaption('苹果'.repeat(60)).length,80);
  assert.equal([...shortCaption('🙂'.repeat(100))].length,80);
  const msg={op:'caption',generation:7,turn:2,id:'one',text:'你好🙂'};
  assert.deepEqual(petMessage(encodePetMessage(msg),{generation:7,turn:2}),msg);
  const messages=[],decoder=new FrameDecoder(frame=>messages.push(petMessage(frame.payload)));
  const wire=encodeFrame(2,2,encodePetMessage(msg));
  for(let i=0;i<wire.length;i+=2)decoder.push(wire.subarray(i,i+2));
  assert.deepEqual(messages,[msg]);
  assert.equal(petMessage(encodePetMessage(msg),{generation:8,turn:2}),null);
  assert.equal(petMessage(encodePetMessage(msg),{generation:7,turn:3}),null);
  assert.equal(bridgeIdentity('aa'.repeat(32)),bridgeIdentity('aa'.repeat(32)));
  assert.notEqual(bridgeIdentity('aa'.repeat(32)),bridgeIdentity('bb'.repeat(32)));
  assert(privateLanPeer('192.168.1.9'));assert(privateLanPeer('::ffff:10.0.0.8'));assert(privateLanPeer('127.0.0.1'));
  assert(!privateLanPeer('8.8.8.8'));assert(!privateLanPeer('192.168.1.9/evil'));assert(!privateLanPeer('198.18.0.1'));
});

test('optional voice status capability keeps legacy hello valid and status identities bounded',()=>{
  const hello={op:'hello',v:2,generation:7,capabilities:['turn_status']};
  assert.deepEqual(petMessage(encodePetMessage(hello)),hello);
  const status={op:'turn_status',generation:7,turn:3,code:'short_input'};
  assert.deepEqual(petMessage(encodePetMessage(status),{generation:7,turn:3}),status);
  assert.equal(petMessage(encodePetMessage(status),{generation:7,turn:4}),null);
  assert.throws(()=>encodePetMessage({...status,code:'arbitrary server text'}),/PET_PROTOCOL/);
  assert.throws(()=>encodePetMessage({...hello,capabilities:[{}]}),/PET_PROTOCOL/);
});

test('caption reading identities, byte limits and controlled errors are validated',()=>{
 const request={op:'caption_read',generation:7,turn:3,request:1,version:0,page:0};
 assert.deepEqual(petMessage(encodePetMessage(request)),request);
 for(const patch of [{request:0},{version:-1},{page:65536},{page:1,version:0}])assert.throws(()=>encodePetMessage({...request,...patch}),/PET_PROTOCOL/);
 const page={op:'caption_page',generation:7,turn:3,request:1,version:1,page:0,pages:2,text:'中文🙂\n\t',truncated:false,responding:true};
 assert.deepEqual(petMessage(encodePetMessage(page)),page);
 assert.equal(petMessage(encodePetMessage(page),{generation:7,turn:4}),null);
 for(const patch of [{text:'中'.repeat(101)},{text:'\ud800'},{text:'bad\x00'},{pages:0},{page:2}])assert.throws(()=>encodePetMessage({...page,...patch}),/PET_PROTOCOL/);
 const error={op:'caption_page',generation:7,turn:3,request:1,version:1,page:0,error:'stale'};
 assert.deepEqual(petMessage(encodePetMessage(error)),error);
 assert.throws(()=>encodePetMessage({...error,error:'private raw reason'}),/PET_PROTOCOL/);
 const catalog={op:'caption_catalog',generation:7,turn:3,available:true,truncated:false,responding:false};
 assert.deepEqual(petMessage(encodePetMessage(catalog)),catalog);
});
