import { test } from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { voiceUpdates, NativeBotSession } from '../src/native-bot.mjs';

test('task return feed accepts only unseen messages addressed to this voice call', () => {
  const entries = [
    { id: 'old', kind: 'send-message', message: { type: 'text', channel: 'voice:mine', content: 'old' } },
    { id: 'other', kind: 'send-message', message: { type: 'text', channel: 'voice:other', content: 'private other call' } },
    { id: 'chat', kind: 'send-message', message: { type: 'text', content: 'ordinary chat' } },
    { id: 'new', kind: 'send-message', message: { type: 'text', channel: 'voice:mine', content: '5535' } }
  ];
  const seen = new Set(['old']);
  assert.deepEqual(voiceUpdates(entries, 'mine', seen), [{ id: 'new', text: '5535' }]);
  assert.deepEqual(voiceUpdates(entries, 'mine', seen), []);
});
test('native controller binds tools, presence and call record to the chosen bot', async () => {
  class Voice extends EventEmitter {
    async open(args) { this.args = args; }
    close() { this.emit('closed'); }
  }
  const calls = [], voice = new Voice();
  const backend = {
    rpc: async (method, body) => {
      calls.push({ method, body });
      if (method === 'VoiceCallHarnessSession') return { instructions: 'fixture', tools: [] };
      if (method === 'MintSandVoiceCallSecret') return { clientSecret: 'fixture', model: 'native' };
      return { kind: 'VOICE_CALL_HARNESS_TOOL_OUTCOME_KIND_SERVED', output: { relay: 'accepted' } };
    },
    gatewayCall: async (method, body) => { calls.push({ method, body }); return { entries: [] }; }
  };
  const session = new NativeBotSession({ backend, bot: { id: 'selected', name: '小糖' }, voice, pollMs: 60000 });
  await session.open();
  await voice.args.onTool({ name: 'send_task', input: { request: '123×45' }, callId: 'wire-call' });
  voice.emit('transcript', { role: 'user', text: 'fixture conversation, never log me' });
  await session.close();
  assert.equal(calls.filter(call => call.method === 'recordVoiceCall').length, 1);
  assert.equal(calls.filter(call => call.method === 'setVoiceCallPresence' && !call.body.isOnTheLine).length, 1);
  const tool = calls.find(call => call.method === 'VoiceCallHarnessTool');
  assert.equal(tool.body.agentId, 'selected');
  assert.equal(tool.body.callId, session.callId);
  const record = calls.find(call => call.method === 'recordVoiceCall').body.record;
  assert.equal(record.agentId, 'selected'); assert.equal(record.turns[0].speaker, 'user');
  assert.equal(record.turns[0].text, 'fixture conversation, never log me');
  assert.equal(calls.filter(call => call.method === 'setVoiceCallPresence').at(-1).body.isOnTheLine, false);
  assert.doesNotMatch(JSON.stringify(session), /fixture conversation|clientSecret/);
});
test('closing during pending presence cannot resurrect the call', async () => {
  class Voice extends EventEmitter { async open() {} close() { this.emit('closed'); } }
  let resolvePresence, entered; const pending = new Promise(resolve => { entered = resolve; }); const presence = [];
  const backend = {
    rpc: async method => method === 'VoiceCallHarnessSession' ? { instructions: '', tools: [] } : { model: 'fixture' },
    gatewayCall: async (method, body) => {
      if (method === 'setVoiceCallPresence') { presence.push(body.isOnTheLine); if (body.isOnTheLine) { entered(); await new Promise(resolve => { resolvePresence = resolve; }); } }
      return {};
    }
  };
  const session = new NativeBotSession({ backend, bot: { id: 'fixture', name: 'fixture' }, voice: new Voice() });
  const states = []; session.on('state', state => states.push(state));
  const opening = session.open(); const rejected = assert.rejects(opening, /closed/i);
  await pending; await session.close(); resolvePresence(); await rejected;
  assert.deepEqual(states, []); assert.equal(presence.at(-1), false); assert.equal(session.timer, undefined);
});
test('native history updates repeated transcription of the same item', async () => {
  class Voice extends EventEmitter { async open() {} close() {} }
  const voice = new Voice(), records = [];
  const backend = { rpc: async method => method === 'VoiceCallHarnessSession' ? { instructions: '', tools: [] } : { model: 'fixture' }, gatewayCall: async (method, body) => { if (method === 'recordVoiceCall') records.push(body.record); return {}; } };
  const session = new NativeBotSession({ backend, bot: { id: 'fixture', name: 'fixture' }, voice }); await session.open();
  voice.emit('transcript', { role: 'user', itemId: 'one', text: 'partial' });
  voice.emit('transcript', { role: 'user', itemId: 'one', text: 'complete' });
  await session.close(); assert.equal(records[0].turns.length, 1); assert.equal(records[0].turns[0].text, 'complete');
});
test('task feed failure does not tear down the independent voice call', async () => {
  class Voice extends EventEmitter { async open() {} close() { this.closed = true; } }
  const voice = new Voice(), faults = [];
  const backend = { rpc: async method => method === 'VoiceCallHarnessSession' ? { instructions: '', tools: [] } : { model: 'fixture' },
    gatewayCall: async method => { if (method === 'getAgentTranscriptTail') throw Object.assign(new Error('redacted'), { code: 'SERVICE_REFUSED', status: 503 }); return {}; } };
  const session = new NativeBotSession({ backend, bot: { id: 'fixture', name: 'fixture' }, voice, pollMs: 60000 });
  session.on('fault', fault => faults.push(fault)); await session.open(); clearTimeout(session.timer); await session.poll();
  assert.equal(faults[0].code, 'TASK_RETURN_UNAVAILABLE'); assert.equal(voice.closed, undefined); assert.equal(session.closed, false);
  await session.close();
});
test('native event channel filters the call and aborts on hangup', async () => {
  class Voice extends EventEmitter { updates=[];async open() {} close() {} injectUpdate(update) { this.updates.push(update); } }
  const voice=new Voice();let stream,opened,resultReady=false;
  const listening=new Promise(resolve=>{opened=resolve;});
  const backend={rpc:async method=>method==='VoiceCallHarnessSession'?{instructions:'',tools:[]}:{model:'fixture'},gatewayCall:async method=>method==='getAgentTranscriptTail'?{entries:resultReady?[{id:'result',kind:'send-message',message:{type:'text',channel:`voice:${session.callId}`,content:'5535'}}]:[]}: {},
    openEvents:async signal=>new ReadableStream({start(controller){stream=controller;signal.addEventListener('abort',()=>controller.close(),{once:true});opened();}})};
  const session=new NativeBotSession({backend,bot:{id:'bot',name:'fixture'},voice});await session.open();await listening;
  const event={eventId:'id',channel:'voice-call-nudge',payload:{agentId:'bot',callId:session.callId,kind:'outcome',update:'5535'}};
  resultReady=true;stream.enqueue(Buffer.from('data: '+JSON.stringify(event)+'\n\n'));await new Promise(resolve=>setTimeout(resolve,30));
  assert.equal(voice.updates.length,1);await session.close();assert.equal(session.closed,true);
});
