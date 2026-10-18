import { test } from 'node:test';
import assert from 'node:assert/strict';
import { NativeEventDecoder, taskUpdate } from '../src/native-events.mjs';
test('native SSE preserves split Chinese text and filters exact call identity', () => {
  const events = [], parser = new NativeEventDecoder(event => events.push(event));
  const raw = Buffer.from('data: '+JSON.stringify({eventId:'one',channel:'voice-call-nudge',payload:{agentId:'bot',callId:'call',kind:'outcome',update:'结果5535'}})+'\n\n');
  for(let i=0;i<raw.length;i+=3) parser.push(raw.subarray(i,i+3));
  const seen = new Set();
  assert.equal(taskUpdate(events[0], { agentId:'bot',callId:'other',seen }), null);
  assert.deepEqual(taskUpdate(events[0], { agentId:'bot',callId:'call',seen }), {id:'nudge-one',text:'结果5535'});
  assert.equal(taskUpdate(events[0], { agentId:'bot',callId:'call',seen }), null);
  const second={...events[0],eventId:'two'};
  assert.equal(taskUpdate(second, { agentId:'bot',callId:'call',seen }).text,'结果5535');
});
test('ignores other native channels and bounds event memory', () => {
  const parser = new NativeEventDecoder(()=>{});
  assert.throws(()=>parser.push(Buffer.alloc(2*1024*1024+1,65)),/EVENT_SIZE/);
  assert.equal(taskUpdate({channel:'transcript',payload:{agentId:'bot',callId:'call',kind:'outcome',update:'ignore'}},{agentId:'bot',callId:'call',seen:new Set()}),null);
});
