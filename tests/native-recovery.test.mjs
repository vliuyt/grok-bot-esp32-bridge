import {test} from 'node:test';
import assert from 'node:assert/strict';
import {EventEmitter} from 'node:events';
import {setTimeout as delay} from 'node:timers/promises';
import {NativeBotSession,voiceUpdates} from '../src/native-bot.mjs';

class Voice extends EventEmitter {
  updates=[]; failNext=false;
  async open(){} close(){}
  injectUpdate(update){if(this.failNext){this.failNext=false;throw new Error('SEND_FAILED');}this.updates.push(update);}
}
async function fixture(pollMs=60000){
 const voice=new Voice();let entries=[],stream;
 const backend={rpc:async method=>method==='VoiceCallHarnessSession'?{instructions:'',tools:[]}:{model:'fixture'},
   gatewayCall:async method=>method==='getAgentTranscriptTail'?{entries}:{},
   openEvents:async signal=>new ReadableStream({start(c){stream=c;signal.addEventListener('abort',()=>c.close(),{once:true});}})};
 const session=new NativeBotSession({backend,voice,bot:{id:'bot',name:'fixture'},pollMs});
 await session.open();await delay(5);
 const message=(id,text='5535')=>({id,kind:'send-message',message:{type:'text',channel:`voice:${session.callId}`,content:text}});
 const nudge=(id='nudge')=>stream.enqueue(Buffer.from('data: '+JSON.stringify({eventId:id,channel:'voice-call-nudge',payload:{agentId:'bot',callId:session.callId,kind:'outcome',update:'notification only'}})+'\n\n'));
 return {session,voice,backend,message,nudge,setEntries:value=>{entries=value;}};
}
test('history is the canonical result source and SSE cannot duplicate its entry',async()=>{
 const f=await fixture();
 try{f.setEntries([f.message('one')]);await f.session.poll();assert.equal(f.voice.updates.length,1);
 f.nudge();await delay(30);assert.equal(f.voice.updates.length,1);assert.equal(f.voice.updates[0].text,'5535');}
 finally{await f.session.close();}
});
test('history requires real bounded IDs and preserves identical results from different entries',()=>{
 const m=id=>({id,kind:'send-message',message:{type:'text',channel:'voice:mine',content:'5535'}});
 const results=voiceUpdates([m(undefined),m(''),m('x'.repeat(257)),m('one'),m('one'),m('two')],'mine',new Set());
 assert.deepEqual(results.map(r=>r.id),['one','two']);
});
test('failed injection does not consume the history entry',async()=>{
 const f=await fixture();
 try{f.setEntries([f.message('retry')]);f.voice.failNext=true;await f.session.poll();assert.equal(f.voice.updates.length,0);
 await f.session.poll();assert.equal(f.voice.updates.length,1);}
 finally{await f.session.close();}
});
test('nudge before history commit only injects the eventual canonical message',async()=>{
 const f=await fixture(10);
 try{f.nudge();await delay(20);assert.equal(f.voice.updates.length,0);f.setEntries([f.message('late')]);
 await delay(100);assert.deepEqual(f.voice.updates.map(u=>u.text),['5535']);}
 finally{await f.session.close();}
});
test('history reads are single flight and dirty triggers cause a catch-up',async()=>{
 const f=await fixture();let release,entered,reads=0,active=0,maximum=0;
 const started=new Promise(resolve=>{entered=resolve;});
 f.backend.gatewayCall=async method=>{
   if(method!=='getAgentTranscriptTail')return{};
   reads++;active++;maximum=Math.max(maximum,active);
   if(reads===1){entered();await new Promise(resolve=>{release=resolve;});}
   active--;return{entries:reads===1?[]:[f.message('caught-up')]};
 };
 try{const first=f.session.poll();await started;const second=f.session.poll();release();await Promise.all([first,second]);await delay(30);
 assert.equal(maximum,1);assert.equal(f.voice.updates.length,1);}
 finally{await f.session.close();}
});
test('hangup cancels the history request and never injects its late result',async()=>{
 const f=await fixture();let release,entered,aborted=false;
 const started=new Promise(resolve=>{entered=resolve;});
 f.backend.gatewayCall=async(method,body,signal)=>{
  if(method!=='getAgentTranscriptTail')return{};
  entered();await new Promise(resolve=>{release=resolve;signal?.addEventListener('abort',()=>{aborted=true;resolve();},{once:true});});
  return{entries:[f.message('too-late')]};
 };
 const reading=f.session.poll();await started;const closing=f.session.close();await delay(5);release();await Promise.all([reading,closing]);
 assert.equal(aborted,true);assert.equal(f.voice.updates.length,0);
});
test('SSE bursts cannot bypass a failed history read cooldown',async()=>{
 const f=await fixture();let reads=0;
 f.backend.gatewayCall=async method=>{if(method==='getAgentTranscriptTail'){reads++;throw Object.assign(new Error('unavailable'),{status:503});}return{};};
 try{await f.session.poll();
 for(let i=0;i<10;i++){f.nudge('burst-'+i);await delay(2);}await delay(30);
 assert.equal(reads,1);}
 finally{await f.session.close();}
});
