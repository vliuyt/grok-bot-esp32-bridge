import { test } from 'node:test';
import assert from 'node:assert/strict';
import { PcmResampler, validatePcm } from '../src/audio.mjs';

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
