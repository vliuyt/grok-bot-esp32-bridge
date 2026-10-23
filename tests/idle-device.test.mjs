import { test } from 'node:test';
import assert from 'node:assert/strict';
import tls from 'node:tls';
import { once } from 'node:events';
import { setTimeout as delay } from 'node:timers/promises';
import { startDeviceServer } from '../src/device-server.mjs';
import { encodeFrame, FrameDecoder } from '../src/device-wire.mjs';
import { NativeBotSession } from '../src/native-bot.mjs';
import { RealtimeSession } from '../src/realtime.mjs';

// The live diagnostic retains categories only; this synthetic message has the verified keywords.
const idleError = { type: 'error', error: { type: 'server_error', message: 'Session idle timeout' } };
const conversationId = 'conversation-fixture';
function deferred() {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}
class NativeSocket extends EventTarget {
  readyState = 1; sent = []; closes = 0;
  constructor(url, protocols) { super(); this.url = url; this.protocols = protocols; }
  send(text) { this.sent.push(JSON.parse(text)); }
  receive(frame) { this.dispatchEvent(new MessageEvent('message', { data: JSON.stringify(frame) })); }
  close() {
    if (this.readyState === 3) return;
    this.readyState = 3; this.closes++; this.dispatchEvent(new Event('close'));
  }
}
async function until(predicate, message = 'fixture did not reach its expected state') {
  const deadline = performance.now() + 2000;
  while (!predicate()) { assert(performance.now() < deadline, message); await delay(5); }
}
async function bounded(promise) {
  let timer;
  try { return await Promise.race([promise, new Promise((_, reject) => { timer = setTimeout(() => reject(new Error('fixture cleanup timed out')), 2000); })]); }
  finally { clearTimeout(timer); }
}
function pcm(samples, value) {
  const bytes = Buffer.alloc(samples * 2);
  for (let at = 0; at < bytes.length; at += 2) bytes.writeInt16LE(value, at);
  return bytes;
}
function credential(number) {
  return { websocketUrl: 'wss://api.x.ai/v1/realtime', model: 'fixture', clientSecret: `fixture-${number}`, expiresAtUnixSeconds: 2100000000 };
}
async function fixture(t, { capabilities = ['native_resume'], mint } = {}) {
  const key = Buffer.alloc(32, 42), bot = { id: 'idle-fixture', name: 'fixture' };
  const calls = [], sessions = [], voices = [], sockets = [], frames = [], deviceErrors = [], recoveries = [];
  let now = 1800000000000, idle = 0, device;
  const backend = {
    async rpc(method, body, signal) {
      calls.push({ method, body });
      if (method === 'VoiceCallHarnessSession') return { instructions: 'fixture persona', tools: [{ type: 'function', name: 'send_task', parameters: { type: 'object', properties: {} } }] };
      if (method === 'MintSandVoiceCallSecret') {
        const number = calls.filter(call => call.method === method).length;
        return number > 1 && mint ? mint(signal) : credential(number);
      }
      throw new Error(`Unexpected fixture RPC: ${method}`);
    },
    async gatewayCall(method, body) {
      calls.push({ method, body });
      if (method === 'getAgentTranscriptTail') return { entries: [] };
      if (method === 'setVoiceCallPresence' || method === 'recordVoiceCall') return {};
      throw new Error(`Unexpected fixture gateway call: ${method}`);
    }
  };
  const voiceFactory = () => {
    const voice = new RealtimeSession({ now: () => now, idleNow: () => idle, socketFactory: (url, protocols) => {
      const socket = new NativeSocket(url, protocols); sockets.push(socket); return socket;
    } });
    voices.push(voice); return voice;
  };
  const server = await startDeviceServer({ backend, bot, key, host: '127.0.0.1', port: 0,
    sessionFactory: options => {
      const session = new NativeBotSession({ backend, bot, voiceFactory, replyStyle: 'companion', pollMs: 60000, ...options });
      session.on('voiceRecovery', event => recoveries.push(event.phase)); sessions.push(session); return session;
    }
  });
  let closing;
  const close = () => closing ??= server.close();
  t.after(async () => {
    device?.destroy(); await bounded(close());
    assert(sessions.every(session => session.closed && session.timer?._destroyed === true));
    assert(voices.every(voice => voice.state === 'closed'));
    assert(sockets.every(socket => socket.readyState === 3 && socket.closes === 1));
    assert.deepEqual(deviceErrors, []);
  });
  device = tls.connect({ port: server.port, host: '127.0.0.1', minVersion: 'TLSv1.2', maxVersion: 'TLSv1.2',
    ciphers: 'PSK-AES128-GCM-SHA256', rejectUnauthorized: false, pskCallback: () => ({ identity: 'grok-esp32', psk: key }) });
  const decoder = new FrameDecoder(frame => frames.push(frame));
  device.on('data', chunk => decoder.push(chunk)); device.on('error', error => deviceErrors.push(error)); device.setNoDelay(true);
  await bounded(once(device, 'secureConnect'));
  await until(() => sockets.length === 1);
  const send = (type, turn, payload) => device.write(encodeFrame(type, turn, Buffer.isBuffer(payload) ? payload : Buffer.from(payload)));
  const controls = text => frames.filter(frame => frame.type === 2 && frame.payload.toString() === text);
  const metadata = () => frames.filter(frame => frame.type === 2 && frame.payload[0] === 123).map(frame => JSON.parse(frame.payload));
  const links = () => metadata().filter(message => message.op === 'link').map(message => message.state);
  const nativeFrames = (socket, type) => socket.sent.filter(frame => frame.type === type);
  const audioFrames = turn => frames.filter(frame => frame.type === 3 && frame.turn === turn);
  const barrier = async () => { const count = controls('pong').length; send(2, 0, 'ping'); await until(() => controls('pong').length === count + 1); };
  const handshake = socket => {
    socket.receive({ type: 'session.created' }); socket.receive({ type: 'session.updated' });
    socket.receive({ type: 'conversation.created', conversation: { id: conversationId } });
  };
  handshake(sockets[0]);
  await until(() => server.snapshot().state === 'ready' && controls('ready').length === 1);
  send(2, 0, JSON.stringify({ op: 'hello', v: 2, generation: 7, capabilities }));
  await until(() => metadata().some(message => message.op === 'hello'));
  await until(() => calls.some(call => call.method === 'getAgentTranscriptTail'));
  const capture = (turn, value, mood) => {
    send(2, turn, mood?JSON.stringify({op:'press',generation:7,turn,mood}):'press'); for (let i = 0; i < 4; i++) send(3, turn, pcm(640, value)); send(2, turn, 'release'); send(2, turn, 'release');
  };
  const reply = async (turn, { samples = 2400, value = 4000, mood } = {}) => {
    const socket = sockets.at(-1), voice = voices.at(-1), before = nativeFrames(socket, 'input_audio_buffer.commit').length;
    capture(turn, turn === 1 ? 2000 : -2000, mood);
    await until(() => nativeFrames(socket, 'input_audio_buffer.commit').length === before + 1);
    const response = `reply-${turn}`;
    socket.receive({ type: 'input_audio_buffer.committed', item_id: `input-${turn}` });
    socket.receive({ type: 'conversation.item.input_audio_transcription.completed', item_id: `input-${turn}`, status: 'completed', transcript: `fixture ${turn}` });
    socket.receive({ type: 'response.created', response: { id: response, metadata: { bridge_turn: String(voice.turn) } } });
    socket.receive({ type: 'response.output_audio.delta', response_id: response, delta: pcm(samples, value).toString('base64') });
    socket.receive({ type: 'response.done', response: { id: response, status: 'completed' } });
    await until(() => audioFrames(turn).length === Math.min(20, Math.ceil(samples * 2 / 1.5 / 640)));
    assert.equal(server.snapshot().state, 'speaking'); assert.equal(voice.inputPending.size, 0);
  };
  const drain = async (turn, total) => {
    let credited = 0;
    while (credited < total) {
      const delivered = audioFrames(turn).length, count = delivered - credited;
      assert(count > 0 && count <= 20); assert.equal(server.snapshot().state, 'speaking');
      for (let i = 0; i < count; i++) send(2, turn, 'credit');
      credited += count;
      await until(() => server.snapshot().playback?.credits === 20 || audioFrames(turn).length > delivered);
    }
    await until(() => server.snapshot().state === 'ready');
    assert.equal(server.snapshot().playback.credits, 20); assert.equal(server.snapshot().playback.packets, 0);
  };
  return { server, close, device, calls, sessions, voices, sockets, frames, recoveries, send, controls, links, nativeFrames, audioFrames, barrier, reply, drain,
    expire() { now += 900000; idle += 900000; sockets.at(-1).receive(idleError); } };
}

test('idle resumption and a BOOT during recovery cannot carry stale petting into the next turn',{timeout:10000},async t=>{
  const mint=deferred(),f=await fixture(t,{capabilities:['native_resume','companion_mood'],mint:()=>mint.promise});
  t.after(()=>mint.resolve(credential(2)));
  await f.reply(1,{mood:'warm'});await f.drain(1,5);
  assert.ok(f.nativeFrames(f.sockets[0],'response.create')[0].response.instructions.startsWith('fixture persona'));
  f.expire();await until(()=>f.links().at(-1)==='connecting');
  f.send(2,2,JSON.stringify({op:'press',generation:7,turn:2,mood:'warm'}));f.send(2,2,'release');await f.barrier();
  assert.equal(f.server.snapshot().turns,1);
  mint.resolve(credential(2));await until(()=>f.sockets.length===2);
  const next=f.sockets[1];next.receive({type:'session.created'});next.receive({type:'session.updated'});next.receive({type:'conversation.created',conversation:{id:conversationId}});
  await until(()=>f.links().at(-1)==='ready');assert.equal(f.nativeFrames(next,'response.create').length,0);
  await f.reply(2);await f.drain(2,5);
  assert.equal(f.nativeFrames(next,'response.create')[0].response.instructions,undefined);
  assert.equal(f.server.snapshot().reconnects,0);
});

test('idle recovery resumes one native call over the same TLS connection and accepts only the next complete BOOT', { timeout: 10000 }, async t => {
  const mint = deferred(), f = await fixture(t, { mint: () => mint.promise });
  t.after(() => mint.resolve(credential(2)));
  const session = f.sessions[0], callId = session.callId, oldVoice = f.voices[0], oldSocket = f.sockets[0];
  await f.reply(1); await f.drain(1, 5);
  const oldAudio = Buffer.concat(f.audioFrames(1).map(frame => frame.payload)), clearCount = f.controls('clear').length;
  f.expire();
  assert.equal(oldVoice.state, 'closed', 'expiry must synchronously retire the old voice');
  await until(() => f.links().at(-1) === 'connecting');
  assert.equal(f.server.snapshot().state, 'connecting'); assert.equal(f.device.destroyed, false);
  assert.equal(f.controls('ready').length, 1); assert.equal(f.controls('clear').length, clearCount);
  const diagnosticBefore=JSON.stringify(f.server.snapshot().nativeEvents);
  oldSocket.receive({type:'retired_fixture',padding:'x'.repeat(2*1024*1024)});
  assert.equal(JSON.stringify(f.server.snapshot().nativeEvents),diagnosticBefore,'retired sockets must not be parsed again by diagnostics');
  f.send(2, 2, 'press'); f.send(3, 2, pcm(640, 12000)); f.send(2, 2, 'release'); await f.barrier();
  assert.equal(f.nativeFrames(oldSocket, 'input_audio_buffer.commit').length, 1);
  assert.equal(f.server.snapshot().turns, 1, 'BOOT during recovery must not begin a capture');

  mint.resolve(credential(2)); await until(() => f.sockets.length === 2);
  const newSocket = f.sockets[1], newVoice = f.voices[1];
  assert.notEqual(newVoice, oldVoice); assert.notEqual(newSocket, oldSocket);
  assert.equal(new URL(newSocket.url).searchParams.get('conversation_id'), conversationId);
  assert.deepEqual(newSocket.protocols, ['xai-client-secret.fixture-2']);
  assert.equal(new URL(oldSocket.url).searchParams.get('conversation_id'), null);
  newSocket.receive({ type: 'session.created' });
  assert.equal(f.nativeFrames(newSocket, 'session.update').length, 1);
  assert.equal(f.nativeFrames(newSocket, 'session.update')[0].session.resumption.enabled, true);
  assert.equal(f.server.snapshot().state, 'connecting', 'the replacement session must be acknowledged before ready');
  assert.equal(f.controls('ready').length, 1);
  newSocket.receive({ type: 'session.updated' }); newSocket.receive({ type: 'conversation.created', conversation: { id: conversationId } });
  await until(() => f.server.snapshot().state === 'ready' && f.controls('ready').length === 2 && f.links().at(-1) === 'ready');
  f.send(3, 2, pcm(640, 12000)); f.send(2, 2, 'release'); await f.barrier();
  assert.equal(newSocket.sent.filter(frame => frame.type.startsWith('input_audio_buffer.')).length, 0, 'a release after recovery cannot submit the ignored press');
  assert.equal(session, f.sessions[0]); assert.equal(session.callId, callId); assert.equal(session.closed, false);
  assert.deepEqual(f.recoveries, ['started', 'ready']); assert.equal(f.server.snapshot().reconnects, 0);
  assert.equal(f.server.snapshot().nativeRecoveries, 1);

  const beforeLate = f.frames.length;
  oldSocket.receive({ type: 'response.output_audio.delta', response_id: 'reply-1', delta: pcm(2400, 12000).toString('base64') });
  oldVoice.emit('audio', pcm(2400, 12000)); oldVoice.emit('responseDone'); oldVoice.emit('state', 'ready');
  await f.barrier(); assert.equal(f.frames.length, beforeLate + 1, 'retired socket and voice epoch must not leak device frames');
  await f.reply(3, { samples: 24000, value: -7000 });
  assert.equal(f.server.snapshot().playback.credits, 0); assert.equal(f.server.snapshot().playback.packets, 30);
  await f.drain(3, 50);
  assert.deepEqual(Buffer.concat(f.audioFrames(3).map(frame => frame.payload)), pcm(16000, -7000));
  assert.deepEqual(Buffer.concat(f.audioFrames(1).map(frame => frame.payload)), oldAudio);
  assert.equal(f.nativeFrames(newSocket, 'input_audio_buffer.commit').length, 1);
  assert.equal(f.nativeFrames(newSocket, 'response.create').length, 1);
  const input = Buffer.concat(f.nativeFrames(newSocket, 'input_audio_buffer.append').map(frame => Buffer.from(frame.audio, 'base64')));
  assert(Math.abs(input.length - 7680) <= 2, 'the fresh capture may retain at most one interpolation edge sample');
  for (let at = 0; at < input.length; at += 2) assert(input.readInt16LE(at) < 0, 'old or ignored positive capture must not enter the new commit');
  assert.equal(f.calls.filter(call => call.method === 'MintSandVoiceCallSecret').length, 2);
  assert.equal(f.calls.filter(call => call.method === 'VoiceCallHarnessSession').length, 1);
  assert.equal(f.calls.filter(call => call.method === 'VoiceCallHarnessTool').length, 0);
  assert.equal(f.sessions.length, 1); assert.equal(f.voices.length, 2); assert.equal(f.device.destroyed, false);
  f.device.destroy(); await bounded(f.close());
  const records = f.calls.filter(call => call.method === 'recordVoiceCall');
  assert.equal(records.length, 1); assert.equal(records[0].body.record.callId, callId);
  assert.equal(records[0].body.record.realtimeConversationId, conversationId); assert.equal(records[0].body.record.turns.length, 2);
  assert.deepEqual(f.calls.filter(call => call.method === 'setVoiceCallPresence').map(call => call.body.isOnTheLine), [true, false]);
});

test('firmware without native_resume keeps the ordinary fatal idle-error path', { timeout: 5000 }, async t => {
  const f = await fixture(t, { capabilities: [] }); await f.reply(1); await f.drain(1, 5);
  f.expire(); await until(() => f.device.destroyed && f.sessions[0].closed);
  assert.equal(f.server.snapshot().state, 'disconnected'); assert.equal(f.server.snapshot().fault, 'VOICE_SERVICE_ERROR');
  assert.equal(f.calls.filter(call => call.method === 'MintSandVoiceCallSecret').length, 1);
  assert.deepEqual(f.recoveries, []); assert.equal(f.controls('ready').length, 1);
  assert.deepEqual(f.links(), ['ready']); assert.equal(f.audioFrames(1).length, 5);
});

test('queued audio and outstanding device playback credits each prevent native recovery', { timeout: 10000 }, async t => {
  for (const samples of [2400, 24000]) await t.test(samples === 2400 ? 'device credit outstanding' : 'audio still buffered', async subtest => {
    const f = await fixture(subtest); await f.reply(1, { samples });
    const packets = f.audioFrames(1).length;
    assert.equal(f.server.snapshot().playback.credits, samples === 2400 ? 15 : 0);
    assert.equal(f.server.snapshot().playback.packets, samples === 2400 ? 0 : 30);
    f.expire(); await until(() => f.device.destroyed && f.sessions[0].closed);
    assert.equal(f.server.snapshot().fault, 'VOICE_SERVICE_ERROR'); assert.deepEqual(f.recoveries, []);
    assert.equal(f.calls.filter(call => call.method === 'MintSandVoiceCallSecret').length, 1);
    assert.equal(f.controls('ready').length, 1); assert.deepEqual(f.links(), ['ready']);
    assert.equal(f.audioFrames(1).length, packets, 'failure must not replay held audio');
  });
});

test('credential renewal failure closes the paired connection without replay or another attempt', { timeout: 5000 }, async t => {
  const mint = deferred(), f = await fixture(t, { mint: () => mint.promise });
  t.after(() => mint.resolve(credential(2)));
  await f.reply(1); await f.drain(1, 5); f.expire();
  await until(() => f.links().at(-1) === 'connecting'); mint.reject(new Error('fixture renewal refused'));
  await until(() => f.device.destroyed && f.sessions[0].closed);
  assert.equal(f.server.snapshot().fault, 'VOICE_RECOVERY_FAILED'); assert.deepEqual(f.recoveries, ['started']);
  assert.equal(f.calls.filter(call => call.method === 'MintSandVoiceCallSecret').length, 2);
  assert.equal(f.calls.filter(call => call.method === 'VoiceCallHarnessTool').length, 0);
  assert.equal(f.sockets.length, 1); assert.equal(f.controls('ready').length, 1); assert.equal(f.audioFrames(1).length, 5);
  assert.equal(f.nativeFrames(f.sockets[0], 'input_audio_buffer.commit').length, 1);
});
