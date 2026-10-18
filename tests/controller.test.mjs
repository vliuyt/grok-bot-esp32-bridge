import { test } from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { setTimeout as delay } from 'node:timers/promises';
import { VoiceController } from '../src/controller.mjs';
function fixture() {
  const calls = [], audios = [], bots = [{ id: 'fixture', name: '小糖' }];
  class Audio extends EventEmitter {
    async open() {} beginTurn(turn) { this.turn = turn; }
    endTurn() { return new Promise(resolve => { this.ack = resolve; }); }
    play() {} clear() {} close() { this.closed = true; }
  }
  class Session extends EventEmitter {
    voice = { turn: 0 }; chunks = []; commits = [];
    async open() {} beginTurn() { this.voice.turn++; }
    sendAudio(pcm) { this.chunks.push(pcm); }
    endTurn() { this.commits.push(this.chunks.length); this.emit('state', 'waiting'); }
    async close() { this.closed = true; this.emit('closed'); }
  }
  const controller = new VoiceController({ bots, bot: bots[0], endGraceMs: 10,
    audioFactory: () => { const audio = new Audio(); audios.push(audio); return audio; },
    sessionFactory: () => { const session = new Session(); calls.push(session); return session; } });
  return { controller, calls, audios };
}
test('release includes capture frames preceding the stop acknowledgement', async () => {
  const { controller, calls, audios } = fixture(); await controller.command('connect'); await controller.command('press');
  const releasing = controller.command('release');
  audios[0].emit('capture', { turn: 1, pcm: Buffer.from([1, 0]) });
  assert.equal(calls[0].commits.length, 0); audios[0].ack(); await releasing;
  assert.deepEqual(calls[0].commits, [1]); await controller.command('hangup');
});
test('old callbacks and end-call timer cannot close a replacement call', async () => {
  const { controller, calls } = fixture(); await controller.command('connect');
  calls[0].emit('endRequested'); await controller.command('hangup'); await controller.command('connect');
  calls[0].emit('fault', { code: 'VOICE_DISCONNECTED' }); calls[0].emit('closed'); await delay(25);
  assert.equal(controller.snapshot().state, 'ready'); assert.equal(calls[1].closed, undefined);
  await controller.command('hangup');
});
test('hangup reports a failed native call save', async () => {
  const { controller, calls } = fixture(); await controller.command('connect');
  calls[0].close = async () => ({ saved: false, errors: ['CALL_SAVE_FAILED'] });
  await controller.command('hangup'); assert.equal(controller.snapshot().fault, 'CALL_SAVE_FAILED');
});
