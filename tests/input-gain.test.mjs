import {test} from 'node:test';
import assert from 'node:assert/strict';
import {DeviceInputGain} from '../src/input-gain.mjs';

const pcm=values=>{const b=Buffer.alloc(values.length*2);values.forEach((v,i)=>b.writeInt16LE(v,i*2));return b;};
const values=bytes=>Array.from({length:bytes.length/2},(_,i)=>bytes.readInt16LE(i*2));

test('device input gives quiet speech 12 dB of headroom without changing silence or the source buffer',()=>{
  const source=pcm([0,100,-100,1000,-1000]),copy=Buffer.from(source);
  const gain=new DeviceInputGain();
  assert.deepEqual(values(gain.push(source)),[0,400,-400,4000,-4000]);
  assert.deepEqual(source,copy);assert.notEqual(gain.push(source),source);
  assert.deepEqual(gain.push(Buffer.alloc(0)),Buffer.alloc(0));
  assert.deepEqual(JSON.parse(JSON.stringify(gain)),{});
});

test('input peak limiting avoids integer clipping, preserves full-scale input, and recovers slowly',()=>{
  const gain=new DeviceInputGain();
  assert.deepEqual(values(gain.push(pcm([10000,-32768,32767]))),[30000,-32768,32767]);
  const immediately=values(gain.push(pcm([100])))[0];assert(immediately>=100&&immediately<102);
  gain.push(Buffer.alloc(24000));
  // A 500 ms time constant recovers 63.2% of the unity-to-4 gap in 0.5 s.
  const later=values(gain.push(pcm([100])))[0];assert.equal(later,290);
  assert.equal(values(new DeviceInputGain().push(pcm([100])))[0],400);
  const source=pcm(Array.from({length:65536},(_,i)=>i-32768));
  const result=values(new DeviceInputGain().push(source));
  result.forEach((v,i)=>{assert(Math.abs(v)<=Math.max(30000,Math.abs(i-32768)));assert(Math.abs(v)>=Math.abs(i-32768));});
});

test('input gain is independent of transport chunks and invalid frames do not advance its envelope',()=>{
  const source=pcm(Array.from({length:20000},(_,i)=>Math.round(Math.sin(i/7)*(i%31?800:32767))));
  const whole=new DeviceInputGain(),split=new DeviceInputGain();
  const parts=[];for(let at=0;at<source.length;at+=638)parts.push(split.push(source.subarray(at,at+638)));
  assert.deepEqual(Buffer.concat(parts),whole.push(source));
  const first=new DeviceInputGain(),second=new DeviceInputGain();first.push(pcm([32767]));second.push(pcm([32767]));
  assert.throws(()=>first.push(Buffer.from([1])),/PCM/);
  assert.throws(()=>first.push(Buffer.alloc(192002)),/PCM/);
  assert.deepEqual(first.push(source),second.push(source));
});
