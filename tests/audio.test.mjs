import { test } from 'node:test';
import assert from 'node:assert/strict';
import { PcmResampler, PcmMeter, validatePcm } from '../src/audio.mjs';

test('PCM metering distinguishes no samples, silence and full-scale audio without retaining or changing samples',()=>{
  const meter=new PcmMeter();
  assert.deepEqual(meter.snapshot(),{samples:0,rmsDb:null,peakDb:null});
  meter.push(Buffer.alloc(4));assert.deepEqual(meter.snapshot(),{samples:2,rmsDb:-100,peakDb:-100});
  const loud=Buffer.from([0,128,0,128]),copy=Buffer.from(loud);meter.push(loud);
  assert.deepEqual(loud,copy);assert.equal(meter.snapshot().samples,4);
  assert(Math.abs(meter.snapshot().rmsDb+3.01)<.01);assert.equal(meter.snapshot().peakDb,0);
  assert(!JSON.stringify(meter.snapshot()).includes('pcm'));
});
test('PCM level aggregation is independent of transport chunk boundaries and rejects malformed input before mutation',()=>{
  const values=Buffer.from([0,0,255,127,0,128,0,16]);const whole=new PcmMeter(),split=new PcmMeter();
  whole.push(values);split.push(values.subarray(0,2));split.push(values.subarray(2));split.push(Buffer.alloc(0));
  assert.deepEqual(split.snapshot(),whole.snapshot());const before=split.snapshot();
  assert.throws(()=>split.push(Buffer.from([1])),/PCM/);assert.deepEqual(split.snapshot(),before);
});

test('converts 16 kHz samples to 24 kHz without losing a boundary sample', () => {
  const pcm = values => { const b = Buffer.alloc(values.length * 2); values.forEach((v, i) => b.writeInt16LE(v, i * 2)); return b; };
  const split = new PcmResampler(16000, 24000);
  const whole = new PcmResampler(16000, 24000);
  const a = Buffer.concat([split.push(pcm([0, 3000])), split.push(pcm([6000, 9000])), split.finish()]);
  const b = Buffer.concat([whole.push(pcm([0, 3000, 6000, 9000])), whole.finish()]);
  assert.deepEqual(a, b);
  assert.equal(a.readInt16LE(2), 2000);
  assert.equal(a.readInt16LE(4), 4000);
});
test('rejects malformed PCM frames rather than shifting sample alignment', () => {
  assert.throws(() => validatePcm(Buffer.from([1])), /PCM/);
  assert.throws(() => validatePcm(Buffer.alloc(200001)), /PCM/);
});
