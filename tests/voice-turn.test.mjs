import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createTurnMonitor} from '../src/voice-turn.mjs';

function fixture(){
  let now=0,id=0;const timers=new Map(),status=[],expired=[];
  const monitor=createTurnMonitor({now:()=>now,setTimer:(fn,ms)=>{timers.set(++id,{fn,at:now+ms});return id;},clearTimer:id=>timers.delete(id),onStatus:e=>status.push(e),onExpire:turn=>expired.push(turn)});
  const tick=to=>{for(;;){const next=[...timers].sort((a,b)=>a[1].at-b[1].at)[0];if(!next||next[1].at>to)break;now=next[1].at;timers.delete(next[0]);next[1].fn();}now=to;};
  return{monitor,status,expired,tick};
}
test('an unanswered turn explains the wait and expires without submitting another request',()=>{
  const f=fixture();f.monitor.start(1);f.tick(11999);assert.deepEqual(f.status,[]);
  f.tick(12000);assert.deepEqual(f.status,[{turn:1,code:'waiting'}]);
  f.tick(29999);assert.deepEqual(f.expired,[]);f.tick(30000);
  assert.deepEqual(f.expired,[1]);assert.equal(f.status.at(-1).code,'no_reply');
  f.tick(100000);assert.deepEqual(f.expired,[1]);
});
test('real response progress renews the deadline and completion cancels it',()=>{
  const f=fixture();f.monitor.start(2);f.tick(20000);f.monitor.note(2,'response');
  f.tick(49999);assert.deepEqual(f.expired,[]);f.monitor.note(2,'audio');f.monitor.note(2,'done');
  f.tick(200000);assert.deepEqual(f.expired,[]);
});
test('tools have a bounded longer wait and parallel completion cannot finish an outstanding tool',()=>{
  const f=fixture();f.monitor.start(3);f.tick(1000);f.monitor.note(3,'tool_started');f.monitor.note(3,'tool_started');
  f.tick(2000);f.monitor.note(3,'tool_completed');f.monitor.note(3,'done');
  f.tick(61999);assert.deepEqual(f.expired,[]);f.tick(62000);assert.deepEqual(f.expired,[3]);
  assert(f.status.some(e=>e.code==='working'));
});
test('new turns, cancellation and close invalidate old timers and events',()=>{
  const f=fixture();f.monitor.start(1);f.tick(1000);f.monitor.start(2);
  f.monitor.note(1,'audio');f.monitor.clear(1);f.tick(13000);
  assert.deepEqual(f.status,[{turn:2,code:'waiting'}]);f.monitor.clear(2);f.tick(100000);
  assert.deepEqual(f.expired,[]);f.monitor.start(3);f.monitor.close();f.tick(200000);
  assert.deepEqual(f.expired,[]);assert.equal(f.status.length,1);
});
test('real progress can renew a device watchdog without pretending an audio reply arrived',()=>{
  const f=fixture();f.monitor.start(1);f.tick(20000);f.monitor.note(1,'recognized');
  assert.deepEqual(f.status.at(-1),{turn:1,code:'progress'});assert.deepEqual(f.expired,[]);
});
