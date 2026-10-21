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
