import { test } from 'node:test';
import assert from 'node:assert/strict';
import { RealtimeSession } from '../src/realtime.mjs';

class Socket extends EventTarget {
  readyState = 1; sent = [];
  send(text) { this.sent.push(JSON.parse(text)); }
  close() { this.readyState = 3; this.dispatchEvent(new Event('close')); }
  receive(data) { this.dispatchEvent(new MessageEvent('message', { data: JSON.stringify(data) })); }
}
const credential = { websocketUrl: 'wss://api.x.ai/v1/realtime', model: 'fixture-model', clientSecret: 'fixture-secret', expiresAtUnixSeconds: 2100000000 };
const plan = { instructions: 'real bot instructions', tools: [{ type: 'function', name: 'recall_text_messages', description: 'Recall history', parameters: { type: 'object', properties: {} } }] };
function fixture(options={}) {
  const socket = new Socket();
  const voice = new RealtimeSession({ socketFactory: () => socket, now: () => 1800000000000,...options });
  return { socket, voice };
}
test('configures native instructions and tools and waits for session acknowledgement', async () => {
  const { socket, voice } = fixture(); let ready = false;
  const opening = voice.open({ credential, plan }).then(() => { ready = true; });
  socket.receive({ type: 'session.created' });
  assert.equal(ready, false);
  assert.equal(socket.sent[0].session.instructions, 'real bot instructions');
  assert.deepEqual(socket.sent[0].session.tools, plan.tools);
  assert.equal(socket.sent[0].session.turn_detection, null);
  socket.receive({ type: 'session.updated' }); await opening;
  assert.equal(ready, true); voice.close();
});
test('refuses expired credentials and unexpected WebSocket origins', async () => {
  const { voice } = fixture();
  await assert.rejects(voice.open({ credential: { ...credential, expiresAtUnixSeconds: 1 }, plan }), /expired/i);
  await assert.rejects(voice.open({ credential: { ...credential, websocketUrl: 'wss://attacker.invalid/realtime' }, plan }), /origin/i);
});
test('push-to-talk commits captured PCM exactly once and does not send before press', async () => {
  const { socket, voice } = fixture(); const opening = voice.open({ credential, plan });
  socket.receive({ type: 'session.created' }); socket.receive({ type: 'session.updated' }); await opening;
  voice.sendAudio(Buffer.from([1, 0])); assert.equal(socket.sent.length, 1);
  voice.beginTurn(); voice.sendAudio(Buffer.alloc(9600)); voice.endTurn(); voice.endTurn();
  assert.equal(socket.sent.filter(x => x.type === 'input_audio_buffer.commit').length, 1);
  assert.equal(socket.sent.filter(x => x.type === 'response.create').length, 1);
  voice.close();
});
test('interrupt discards late audio from the cancelled response', async () => {
  const { socket, voice } = fixture(); const pcm = [];
  voice.on('audio', frame => pcm.push(frame));
  const opening = voice.open({ credential, plan }); socket.receive({ type: 'session.created' }); socket.receive({ type: 'session.updated' }); await opening;
  voice.beginTurn(); voice.sendAudio(Buffer.alloc(9600)); voice.endTurn();
  socket.receive({ type: 'response.created', response: { id: 'old' } });
  socket.receive({ type: 'response.output_audio.delta', response_id: 'old', delta: 'AQA=' });
  voice.beginTurn();
  socket.receive({ type: 'response.output_audio.delta', response_id: 'old', delta: 'AgA=' });
  voice.sendAudio(Buffer.alloc(9600)); voice.endTurn();
  socket.receive({ type: 'response.created', response: { id: 'new' } });
  socket.receive({ type: 'response.output_audio.delta', response_id: 'new', delta: 'AwA=' });
  assert.deepEqual(pcm.map(frame => frame.readInt16LE()), [1, 3]);
  assert.equal(socket.sent.filter(x => x.type === 'response.cancel').length, 1);
  voice.close();
});
test('deduplicates function calls and ignores late completion after closing', async () => {
  const { socket, voice } = fixture(); let finish; let calls = 0;
  const opening = voice.open({ credential, plan, onTool: async () => { calls++; return await new Promise(resolve => { finish = resolve; }); } });
  socket.receive({ type: 'session.created' }); socket.receive({ type: 'session.updated' }); await opening;
  socket.receive({ type: 'response.created', response: { id: 'owned' } });
  const event = { type: 'response.function_call_arguments.done', response_id: 'owned', call_id: 'one', name: 'recall_text_messages', arguments: '{}' };
  socket.receive(event); socket.receive(event); await new Promise(resolve => setImmediate(resolve));
  assert.equal(calls, 1); voice.close(); finish({ reply: 'fixture' }); await new Promise(resolve => setImmediate(resolve));
  assert.equal(socket.sent.filter(x => x.item?.type === 'function_call_output').length, 0);
});
test('queues a real task update during recording and forwards it after release', async () => {
  const { socket, voice } = fixture(); const opening = voice.open({ credential, plan });
  socket.receive({ type: 'session.created' }); socket.receive({ type: 'session.updated' }); await opening;
  voice.beginTurn(); voice.injectUpdate({ id: 'result', text: '5535' });
  assert.equal(socket.sent.filter(x => x.type === 'async_update.add').length, 0);
  voice.sendAudio(Buffer.alloc(9600)); voice.endTurn();
  const update = socket.sent.find(x => x.type === 'async_update.add');
  assert.equal(update.async_update.topic, 'work_landed');
  assert.equal(update.async_update.payload[0].message.content, '5535'); voice.close();
});
test('ignores tools from an unowned response during recording', async () => {
  const { socket, voice } = fixture(); let calls = 0;
  const opening = voice.open({ credential, plan, onTool: async () => { calls++; return {}; } });
  socket.receive({ type: 'session.updated' }); await opening; voice.beginTurn();
  socket.receive({ type: 'response.function_call_arguments.done', response_id: 'unknown', call_id: 'unsafe', name: 'recall_text_messages', arguments: '{}' });
  await new Promise(resolve => setImmediate(resolve)); assert.equal(calls, 0); voice.close();
});
test('short press releases queued task updates', async () => {
  const { socket, voice } = fixture(); const opening = voice.open({ credential, plan });
  socket.receive({ type: 'session.updated' }); await opening;
  voice.beginTurn(); voice.injectUpdate({ id: 'short', text: '5535' }); voice.endTurn();
  assert.equal(socket.sent.filter(x => x.type === 'async_update.add').length, 1); voice.close();
});
test('cancelled response cannot publish an awaited tool output', async () => {
  const { socket, voice } = fixture(); let finish;
  const opening = voice.open({ credential, plan, onTool: () => new Promise(resolve => { finish = resolve; }) });
  socket.receive({ type: 'session.updated' }); await opening;
  socket.receive({ type: 'response.created', response: { id: 'active' } });
  socket.receive({ type: 'response.function_call_arguments.done', response_id: 'active', call_id: 'pending', name: 'recall_text_messages', arguments: '{}' });
  voice.stopPlayback(); finish({ ok: true }); await new Promise(resolve => setImmediate(resolve));
  assert.equal(socket.sent.filter(x => x.item?.type === 'function_call_output').length, 0); voice.close();
});
test('tool completion during socket closing becomes a fault without an unhandled rejection', async () => {
  const { socket, voice } = fixture(); let finish; const faults = [];
  voice.on('fault', fault => faults.push(fault.code));
  const opening = voice.open({ credential, plan, onTool: () => new Promise(resolve => { finish = resolve; }) });
  socket.receive({ type: 'session.updated' }); await opening;
  socket.receive({ type: 'response.created', response: { id: 'closing' } });
  socket.receive({ type: 'response.function_call_arguments.done', response_id: 'closing', call_id: 'closing-tool', name: 'recall_text_messages', arguments: '{}' });
  socket.readyState = 2; finish({ ok: true }); await new Promise(resolve => setImmediate(resolve));
  assert.deepEqual(faults, ['TOOL_DELIVERY_FAILED']); assert.equal(voice.state, 'closed');
});
test('publishes every parallel tool result before requesting continuation', async () => {
  const { socket, voice } = fixture(); const finish = {};
  const opening = voice.open({ credential, plan, onTool: ({ callId }) => new Promise(resolve => { finish[callId] = resolve; }) });
  socket.receive({ type: 'session.updated' }); await opening;
  socket.receive({ type: 'response.created', response: { id: 'parallel' } });
  for (const id of ['one', 'two']) socket.receive({ type: 'response.function_call_arguments.done', response_id: 'parallel', call_id: id, name: 'recall_text_messages', arguments: '{}' });
  socket.receive({ type: 'response.done', response: { id: 'parallel' } });
  finish.one({ ok: 1 }); await new Promise(resolve => setImmediate(resolve));
  assert.equal(socket.sent.filter(x => x.type === 'response.create').length, 0);
  finish.two({ ok: 2 }); await new Promise(resolve => setImmediate(resolve));
  assert.deepEqual(socket.sent.filter(x => x.item?.type === 'function_call_output').map(x => x.item.call_id), ['one', 'two']);
  assert.equal(socket.sent.filter(x => x.type === 'response.create').length, 1); voice.close();
});

test('short input is explained without committing an empty recording',async()=>{
  const {socket,voice}=fixture(),notices=[];voice.on('turnStatus',e=>notices.push(e));
  const opening=voice.open({credential,plan});socket.receive({type:'session.updated'});await opening;
  voice.beginTurn();voice.sendAudio(Buffer.alloc(4798));voice.endTurn();
  assert.deepEqual(notices,[{turn:1,code:'short_input'}]);
  assert.equal(socket.sent.filter(x=>x.type==='input_audio_buffer.commit').length,0);voice.close();
});
test('an empty or failed response is a recoverable per-turn outcome',async()=>{
  for(const [status,code] of [['completed','empty_response'],['failed','response_failed']]){
    const {socket,voice}=fixture(),notices=[];voice.on('turnStatus',e=>notices.push(e));
    const opening=voice.open({credential,plan});socket.receive({type:'session.updated'});await opening;
    voice.beginTurn();voice.sendAudio(Buffer.alloc(9600));voice.endTurn();
    socket.receive({type:'response.created',response:{id:'reply',metadata:{bridge_turn:'1'}}});
    socket.receive({type:'response.done',response:{id:'reply',status}});
    assert.deepEqual(notices.filter(e=>e.code!=='progress'),[{turn:1,code}]);assert.equal(voice.state,'ready');voice.close();
  }
});
test('late recognition retains the committed input turn instead of becoming the next turn',async()=>{
  const {socket,voice}=fixture(),heard=[];voice.on('transcript',e=>heard.push(e));
  const opening=voice.open({credential,plan});socket.receive({type:'session.updated'});await opening;
  voice.beginTurn();voice.sendAudio(Buffer.alloc(9600));voice.endTurn();
  socket.receive({type:'input_audio_buffer.committed',item_id:'first-input'});
  socket.receive({type:'response.created',response:{id:'reply',metadata:{bridge_turn:'1'}}});
  voice.beginTurn();
  socket.receive({type:'conversation.item.input_audio_transcription.completed',item_id:'first-input',transcript:'old fixture'});
  assert.equal(heard[0].turn,1);assert.equal(voice.turn,2);voice.close();
});
test('interrupt before a response identity arrives closes the ambiguous native session',async()=>{
  const {socket,voice}=fixture(),audio=[];voice.on('audio',a=>audio.push(a));
  const opening=voice.open({credential,plan});socket.receive({type:'session.updated'});await opening;
  voice.beginTurn();voice.sendAudio(Buffer.alloc(9600));voice.endTurn();
  assert.throws(()=>voice.beginTurn(),/reconnect|closed|unavailable/i);
  socket.receive({type:'response.created',response:{id:'late-untagged'}});
  socket.receive({type:'response.output_audio.delta',response_id:'late-untagged',delta:'AQA='});
  assert.equal(audio.length,0);assert.equal(voice.state,'closed');
});

test('a tool finishing before response.done leaves its continuation monitored',async()=>{
  let now=0,id=0;const timers=new Map(),notices=[];
  const {socket,voice}=fixture({setTimer:(fn,ms)=>{timers.set(++id,{fn,at:now+ms});return id;},clearTimer:id=>timers.delete(id)});
  voice.on('turnStatus',e=>notices.push(e));
  const opening=voice.open({credential,plan,onTool:async()=>({ok:true})});socket.receive({type:'session.updated'});await opening;
  voice.beginTurn();voice.sendAudio(Buffer.alloc(9600));voice.endTurn();
  socket.receive({type:'response.created',response:{id:'tool-reply'}});
  socket.receive({type:'response.function_call_arguments.done',response_id:'tool-reply',call_id:'one',name:'recall_text_messages',arguments:'{}'});
  await new Promise(r=>setImmediate(r));
  socket.receive({type:'response.done',response:{id:'tool-reply',status:'completed'}});
  socket.receive({type:'response.created',response:{id:'continuation'}});
  for(;;){const next=[...timers].sort((a,b)=>a[1].at-b[1].at)[0];if(!next||next[1].at>30000)break;now=next[1].at;timers.delete(next[0]);next[1].fn();}
  assert(notices.some(e=>e.code==='no_reply'));voice.close();
});

test('interrupting a tool continuation before its identity arrives cannot adopt old audio',async()=>{
  const {socket,voice}=fixture();
  const opening=voice.open({credential,plan,onTool:async()=>({ok:true})});socket.receive({type:'session.updated'});await opening;
  try{
    voice.beginTurn();voice.sendAudio(Buffer.alloc(9600));voice.endTurn();
    socket.receive({type:'response.created',response:{id:'first'}});
    socket.receive({type:'response.function_call_arguments.done',response_id:'first',call_id:'one',name:'recall_text_messages',arguments:'{}'});
    await new Promise(r=>setImmediate(r));socket.receive({type:'response.done',response:{id:'first',status:'completed'}});
    assert.equal(socket.sent.filter(e=>e.type==='response.create').length,2);
    assert.throws(()=>voice.beginTurn(),/reconnect|closed/i);
    assert.equal(voice.state,'closed');
  }finally{voice.close();}
});

test('an old tool rejection cannot fault a new recording',async()=>{
  const {socket,voice}=fixture(),faults=[];let rejectTool;
  voice.on('fault',e=>faults.push(e.code));
  const opening=voice.open({credential,plan,onTool:()=>new Promise((resolve,reject)=>{rejectTool=reject;})});socket.receive({type:'session.updated'});await opening;
  voice.beginTurn();voice.sendAudio(Buffer.alloc(9600));voice.endTurn();
  socket.receive({type:'response.created',response:{id:'first'}});
  socket.receive({type:'response.function_call_arguments.done',response_id:'first',call_id:'one',name:'recall_text_messages',arguments:'{}'});
  voice.beginTurn();rejectTool(new Error('fixture failed'));await new Promise(r=>setImmediate(r));
  assert.equal(voice.state,'recording');assert.deepEqual(faults,[]);voice.close();
});

test('a failed response is terminal even when its tool later succeeds',async()=>{
  const {socket,voice}=fixture(),audio=[];let finish;
  voice.on('audio',a=>audio.push(a));
  const opening=voice.open({credential,plan,onTool:()=>new Promise(resolve=>{finish=resolve;})});socket.receive({type:'session.updated'});await opening;
  voice.beginTurn();voice.sendAudio(Buffer.alloc(9600));voice.endTurn();
  socket.receive({type:'response.created',response:{id:'first'}});
  socket.receive({type:'response.function_call_arguments.done',response_id:'first',call_id:'one',name:'recall_text_messages',arguments:'{}'});
  socket.receive({type:'response.done',response:{id:'first',status:'failed'}});
  finish({ok:true});await new Promise(r=>setImmediate(r));
  socket.receive({type:'response.output_audio.delta',response_id:'first',delta:'AQA='});
  assert.equal(socket.sent.filter(e=>e.type==='response.create').length,1);assert.equal(audio.length,0);voice.close();
});
