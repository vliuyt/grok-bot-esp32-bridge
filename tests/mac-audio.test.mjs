import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { mkdirSync } from 'node:fs';
import { buildAudio } from '../src/mac-audio.mjs';

test('Mac audio helper validates PCM framing and reports device inventory without recording', { skip: process.platform !== 'darwin' }, () => {
  mkdirSync('.build', { recursive: true });
  buildAudio();
  const run = spawnSync('.build/mac-audio', ['--validate'], {
    input: '{"type":"play","pcm":"AQA="}\n{"type":"play","pcm":"AQ=="}\n', encoding: 'utf8', timeout: 5000
  });
  const events = run.stdout.trim().split('\n').map(line => JSON.parse(line));
  assert.deepEqual(events.map(event => event.type), ['accepted', 'fault']);
  assert.equal(events[0].frames, 1);
  assert.equal(events[1].code, 'INVALID_PCM');
  const devices = JSON.parse(execFileSync('.build/mac-audio', ['--devices'], { encoding: 'utf8', timeout: 5000 }));
  assert.ok(Array.isArray(devices.devices));
  assert.ok(devices.devices.every(device => typeof device.id === 'number' && typeof device.name === 'string'));
});
