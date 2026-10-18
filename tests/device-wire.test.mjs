import { test } from 'node:test';
import assert from 'node:assert/strict';
import { encodeFrame, FrameDecoder, PlaybackQueue } from '../src/device-wire.mjs';
test('device framing reassembles split packets and rejects oversized payloads', () => {
  const frames = [], decode = new FrameDecoder(frame => frames.push(frame));
  const wire = Buffer.concat([encodeFrame(2, 7, Buffer.from('press')), encodeFrame(3, 7, Buffer.from([1, 0]))]);
  for (let i = 0; i < wire.length; i += 3) decode.push(wire.subarray(i, i + 3));
  assert.deepEqual(frames.map(x => [x.type, x.turn, x.payload.toString('hex')]), [[2, 7, '7072657373'], [3, 7, '0100']]);
  assert.throws(() => decode.push(Buffer.from([0, 1, 0, 0])), /FRAME_SIZE/);
});
test('device playback credits bound queued transport and interruption drops old audio', () => {
  const frames = []; const queue = new PlaybackQueue((type, turn, payload) => frames.push({ type, turn, payload }));
  queue.reset(1); queue.push(Buffer.alloc(640 * 25));
  assert.equal(frames.filter(x => x.type === 3).length, 20);
  queue.reset(2); queue.credit(1, 10); queue.push(Buffer.from([2, 0])); queue.done();
  const next = frames.filter(x => x.turn === 2); assert.equal(next.filter(x => x.type === 3).length, 1);
  assert.equal(next.at(-1).payload.toString(), 'done');
  assert.equal(next.find(x => x.type === 3).payload.readInt16LE(), 2);
});
test('clearing the same turn retains in-flight credit accounting', () => {
  const queue = new PlaybackQueue(() => {}); queue.reset(1); queue.push(Buffer.alloc(640));
  queue.reset(1); assert.doesNotThrow(() => queue.credit(1, 1)); assert.equal(queue.available, 20);
});
