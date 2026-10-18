import { test } from 'node:test';
import assert from 'node:assert/strict';
import { Writable } from 'node:stream';
import { setTimeout as delay } from 'node:timers/promises';
import { MacAudio } from '../src/mac-audio.mjs';

test('buffers temporary audio pipe pressure without losing or replaying commands', async () => {
  const lines = [], faults = [];
  const stdin = new Writable({ highWaterMark: 1, write(chunk, encoding, done) { lines.push(JSON.parse(chunk)); setTimeout(done, 2); } });
  const audio = new MacAudio(); audio.child = { stdin }; audio.on('fault', fault => faults.push(fault));
  audio.play(Buffer.from([1, 0])); audio.play(Buffer.from([2, 0])); audio.clear();
  await delay(50);
  assert.equal(faults.length, 0);
  assert.equal(lines[0].type, 'play');
  assert.equal(lines.filter(x => x.type === 'clear').length, 1);
  assert.equal(lines.filter(x => x.type === 'play' && x.pcm === 'AQA=').length, 1);
  stdin.end();
});
test('audio stop waits for a matching acknowledgement', async () => {
  const commands = [], stdin = new Writable({ write(chunk, encoding, done) { commands.push(JSON.parse(chunk)); done(); } });
  const audio = new MacAudio(); audio.child = { stdin };
  audio.beginTurn(7); let stopped = false; const stopping = audio.endTurn().then(() => { stopped = true; });
  await delay(5); assert.equal(stopped, false);
  audio.emit('recordingStopped', { turn: 6 }); await delay(5); assert.equal(stopped, false);
  audio.emit('recordingStopped', { turn: 7 }); await stopping; assert.equal(stopped, true);
  assert.equal(commands.at(-1).turn, 7); stdin.end();
});
test('broken helper stdin emits one fault and drops queued audio', async () => {
  const stdin = new Writable({ write(chunk, encoding, done) { done(Object.assign(new Error('fixture'), { code: 'EPIPE' })); } });
  const audio = new MacAudio(); audio.child = { stdin }; const faults = []; audio.on('fault', event => faults.push(event));
  audio.play(Buffer.from([1, 0])); audio.play(Buffer.from([2, 0])); await delay(20);
  assert.deepEqual(faults, [{ code: 'AUDIO_HELPER_UNAVAILABLE' }]); assert.equal(audio.queue.length, 0);
});
