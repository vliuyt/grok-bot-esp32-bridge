import {test} from 'node:test';
import assert from 'node:assert/strict';
import {EventEmitter} from 'node:events';
import {setTimeout as delay} from 'node:timers/promises';
import {NativeBotSession} from '../src/native-bot.mjs';
import {RealtimeSession} from '../src/realtime.mjs';

function deferred(){let resolve,reject;const promise=new Promise((yes,no)=>{resolve=yes;reject=no;});return {promise,resolve,reject};}
async function until(predicate){const end=performance.now()+1000;while(!predicate()){assert(performance.now()<end,'fixture did not settle');await delay(1);}}
class Voice extends EventEmitter {
  state='idle';conversationId='conversation-fixture';turn=0;closes=0;updates=[];
  trace={snapshot:()=>({events:[{event:'error'}]})};
  constructor({opening}={}){super();this.opening=opening;}
  async open(args){this.args=args;this.state='connecting';if(this.opening)await this.opening;if(this.state==='closed')throw new Error('Fixture voice closed');this.state='ready';}
  close(){if(this.state==='closed')return;this.state='closed';this.closes++;this.emit('clearAudio');this.emit('closed');}
  injectUpdate(update){this.updates.push(update);}
}
async function fixture(options={}){
  const voices=[],calls=[];let entries=[];
  const backend={
    async rpc(method,body,signal){
      calls.push({method,body,signal});
      if(method==='VoiceCallHarnessSession')return {instructions:'original persona',tools:[{type:'function',name:'send_task'}]};
      if(method==='MintSandVoiceCallSecret'){
        const number=calls.filter(call=>call.method===method).length;
        if(number>1&&options.mint)return options.mint(signal);
        return {model:'fixture',clientSecret:`credential-${number}`};
      }
      if(method==='VoiceCallHarnessTool')return {kind:'SERVED',output:{ok:true}};
      throw new Error('Unexpected fixture RPC');
    },
    async gatewayCall(method,body,signal){calls.push({method,body,signal});return method==='getAgentTranscriptTail'?{entries}:{};}
  };
  const voiceFactory=()=>{const voice=new Voice(voices.length?options.nextVoice:undefined);voices.push(voice);return voice;};
  const session=new NativeBotSession({backend,bot:{id:'selected-bot',name:'fixture'},voice:voiceFactory(),voiceFactory,canRecoverIdle:options.canRecoverIdle??(()=>true),pollMs:60000,replyStyle:'companion'});
  await session.open();clearTimeout(session.timer);
  return {session,voices,calls,backend,setEntries:value=>{entries=value;}};
}

test('idle recovery renews once under the same call and resumes the original conversation without replay',async()=>{
  const f=await fixture(),events=[],audio=[];const {session,voices,calls}=f,original=voices[0],callId=session.callId;
  session.on('voiceRecovery',event=>events.push(event));session.on('audio',pcm=>audio.push(pcm));
  try{
    original.emit('transcript',{role:'user',text:'old turn',itemId:'old-user'});
    original.emit('idleExpired');
    assert.equal(original.state,'closed','the listener must synchronously claim the expiry');
    await until(()=>voices.length===2&&!session.recovering);
    const resumed=voices[1];
    assert.equal(session.callId,callId);assert.equal(session.closed,false);
    assert.equal(resumed.args.conversationId,'conversation-fixture');
    assert.equal(resumed.args.credential.clientSecret,'credential-2');
    assert.equal(resumed.args.plan,original.args.plan);assert.match(resumed.args.plan.instructions,/original persona/);
    assert.equal(calls.filter(call=>call.method==='VoiceCallHarnessSession').length,1);
    assert.equal(calls.filter(call=>call.method==='MintSandVoiceCallSecret').length,2);
    assert.equal(calls.filter(call=>call.method==='VoiceCallHarnessTool').length,0);
    assert.deepEqual(events.map(event=>event.phase),['started','ready']);
    assert(events[1].elapsedMs>=0);assert.deepEqual(audio,[]);
    resumed.emit('transcript',{role:'assistant',text:'new turn',itemId:'new-assistant'});
    await session.close();
    const record=calls.find(call=>call.method==='recordVoiceCall').body.record;
    assert.equal(record.callId,callId);assert.equal(record.agentId,'selected-bot');assert.equal(record.turns.length,2);
    assert.deepEqual(calls.filter(call=>call.method==='setVoiceCallPresence').map(call=>call.body.isOnTheLine),[true,false]);
  }finally{await session.close();}
});

test('recovery is opt-in and requires a bounded conversation ID',async()=>{
  for(const id of [undefined,'','x'.repeat(257)]){
    const f=await fixture();
    try{f.voices[0].conversationId=id;f.voices[0].emit('idleExpired');await delay(1);assert.equal(f.voices.length,1);assert.equal(f.voices[0].state,'ready');}
    finally{await f.session.close();}
  }
  const voice=new Voice(),calls=[];
  const backend={rpc:async method=>{calls.push(method);return method==='VoiceCallHarnessSession'?{instructions:'',tools:[]}:{model:'fixture'};},gatewayCall:async()=>({})};
  const session=new NativeBotSession({backend,bot:{id:'fixture',name:'fixture'},voice});
  try{await session.open();voice.emit('idleExpired');await delay(1);assert.equal(voice.state,'ready');assert.equal(calls.filter(method=>method==='MintSandVoiceCallSecret').length,1);}
  finally{await session.close();}
  for(const canRecoverIdle of [()=>false,()=>{throw new Error('fixture gate failed');}]){
    const f=await fixture({canRecoverIdle});
    try{f.voices[0].emit('idleExpired');assert.equal(f.voices[0].state,'ready');assert.equal(f.voices.length,1);}
    finally{await f.session.close();}
  }
});

test('one recovery is in flight and old voice callbacks cannot affect the replacement',async()=>{
  const gate=deferred(),f=await fixture({mint:()=>gate.promise}),events=[],faults=[];
  const {session,voices}=f,old=voices[0];
  for(const name of ['audio','clearAudio','state','transcript','responseDone','turnStatus'])session.on(name,event=>events.push({name,event}));
  session.on('fault',event=>faults.push(event));
  try{
    old.emit('idleExpired');old.emit('idleExpired');
    assert.equal(session.recovering,true);
    old.emit('audio',Buffer.from([1,0]));old.emit('clearAudio');old.emit('state','ready');
    old.emit('transcript',{role:'user',text:'late old text'});old.emit('responseDone');old.emit('turnStatus',{code:'waiting'});old.emit('fault',{code:'OLD_FAULT'});old.emit('closed');
    await assert.rejects(old.args.onTool({name:'send_task',input:{},callId:'old-tool'}),/closed|stale/i);
    assert.deepEqual(events,[]);assert.deepEqual(faults,[]);assert.equal(session.closed,false);
    gate.resolve({model:'fixture',clientSecret:'renewed'});await until(()=>voices.length===2&&!session.recovering);
    assert.equal(f.calls.filter(call=>call.method==='MintSandVoiceCallSecret').length,2);
    assert.equal(f.calls.filter(call=>call.method==='VoiceCallHarnessTool').length,0);
    voices[1].emit('audio',Buffer.from([2,0]));assert.equal(events.length,1);assert.equal(events[0].name,'audio');
  }finally{gate.resolve({model:'fixture'});await session.close();}
});

test('credential or replacement-open failure ends recovery without another attempt',async()=>{
  for(const phase of ['mint','open']){
    const options=phase==='mint'?{mint:async()=>{throw new Error('private mint failure');}}:{nextVoice:{opening:Promise.reject(new Error('private socket failure'))}};
    options.nextVoice?.opening.catch(()=>{});
    const f=await fixture(options),faults=[];f.session.on('fault',event=>faults.push(event.code));
    try{
      f.voices[0].emit('idleExpired');await until(()=>f.session.closed);
      assert.deepEqual(faults,['VOICE_RECOVERY_FAILED']);assert.equal(f.calls.filter(call=>call.method==='MintSandVoiceCallSecret').length,2);
      assert(f.voices.every(voice=>voice.state==='closed'));
    }finally{await f.session.close();}
  }
});

test('renewal preserves the fixed authentication-required notice without exposing unknown errors',async()=>{
  for(const code of ['AUTH_REQUIRED','private-upstream-code']){
    const f=await fixture({mint:async()=>{throw Object.assign(new Error('private credential detail'),{code});}}),faults=[];
    f.session.on('fault',event=>faults.push(event.code));
    try{
      f.voices[0].emit('idleExpired');await until(()=>f.session.closed);
      assert.deepEqual(faults,[code==='AUTH_REQUIRED'?'AUTH_REQUIRED':'VOICE_RECOVERY_FAILED']);
    }finally{await f.session.close();}
  }
});

test('hangup aborts renewal and a late credential cannot create a new voice',async()=>{
  const gate=deferred();let signal;const f=await fixture({mint:value=>{signal=value;return gate.promise;}}),events=[];
  f.session.on('voiceRecovery',event=>events.push(event.phase));
  try{
    f.voices[0].emit('idleExpired');await until(()=>!!signal);
    await f.session.close();assert.equal(signal.aborted,true);
    gate.resolve({model:'fixture',clientSecret:'too-late'});await f.session.recoveryPromise;
    assert.equal(f.voices.length,1);assert.deepEqual(events,['started']);assert.equal(f.session.closed,true);
  }finally{gate.resolve({model:'fixture'});await f.session.close();}
});

test('history arriving during renewal is delivered once after the replacement is ready',async()=>{
  const mint=deferred(),history=deferred(),f=await fixture({mint:()=>mint.promise});const {session,voices}=f;
  const entry={id:'result-one',kind:'send-message',message:{type:'text',channel:`voice:${session.callId}`,content:'fixture result'}};
  let entered=false;
  const gateway=f.backend.gatewayCall.bind(f.backend);
  f.backend.gatewayCall=async(method,...args)=>{if(method==='getAgentTranscriptTail'&&!entered){entered=true;return history.promise;}return gateway(method,...args);};
  try{
    const pending=session.poll();await until(()=>entered);
    voices[0].emit('idleExpired');history.resolve({entries:[entry]});await pending;
    assert.equal(session.seen.has(entry.id),false);assert.deepEqual(voices[0].updates,[]);
    f.setEntries([entry]);mint.resolve({model:'fixture',clientSecret:'renewed'});await until(()=>voices.length===2&&!session.recovering);
    await session.poll();assert.equal(voices[1].updates.length,1);assert.equal(session.seen.has(entry.id),true);
    await session.poll();assert.equal(voices[1].updates.length,1);
  }finally{history.resolve({entries:[]});mint.resolve({model:'fixture'});await session.close();}
});

test('closing from the recovery notification cannot resurrect the call',async()=>{
  const f=await fixture();f.session.on('voiceRecovery',event=>{if(event.phase==='started')void f.session.close();});
  try{f.voices[0].emit('idleExpired');await until(()=>f.session.closed);await f.session.close();assert.equal(f.voices.length,1);assert.equal(f.calls.filter(call=>call.method==='MintSandVoiceCallSecret').length,1);}
  finally{await f.session.close();}
});

test('replacement callbacks remain gated until open completes and hangup cancels a pending open',async()=>{
  const gate=deferred(),f=await fixture({nextVoice:{opening:gate.promise}}),events=[],recoveries=[];
  f.session.on('voiceRecovery',event=>recoveries.push(event.phase));
  for(const name of ['audio','state','transcript','fault'])f.session.on(name,event=>events.push({name,event}));
  try{
    f.voices[0].emit('idleExpired');await until(()=>f.voices.length===2);
    const replacement=f.voices[1];
    replacement.emit('state','ready');replacement.emit('audio',Buffer.from([1,0]));replacement.emit('transcript',{role:'user',text:'premature'});
    assert.deepEqual(events,[]);assert.equal(f.session.recovering,true);
    await f.session.close();gate.resolve();await f.session.recoveryPromise;
    assert.deepEqual(events,[]);assert.deepEqual(recoveries,['started']);assert.equal(replacement.state,'closed');
  }finally{gate.resolve();await f.session.close();}
});

test('an old tool completion cannot update the new voice epoch or be retried',async()=>{
  const f=await fixture(),gate=deferred(),toolEvents=[];
  const rpc=f.backend.rpc.bind(f.backend);
  f.backend.rpc=async(method,body,signal)=>{
    if(method!=='VoiceCallHarnessTool')return rpc(method,body,signal);
    f.calls.push({method,body,signal});return gate.promise;
  };
  f.session.on('tool',event=>toolEvents.push(event.phase));
  const pending=f.voices[0].args.onTool({name:'send_task',input:{request:'fixture'},callId:'old-call'});
  const rejected=assert.rejects(pending,/closed|stale/i);
  try{
    f.voices[0].emit('idleExpired');await until(()=>f.voices.length===2&&!f.session.recovering);
    gate.resolve({kind:'SERVED',output:{value:'late'}});await rejected;
    assert.deepEqual(toolEvents,['started']);assert.equal(f.session.toolCalls[0].result,null);
    assert.equal(f.calls.filter(call=>call.method==='VoiceCallHarnessTool').length,1);
  }finally{gate.resolve({kind:'SERVED',output:{}});await f.session.close();}
});

test('resumption preserves turn and tool fences against replays on the new socket',async()=>{
  class Socket extends EventTarget{
    readyState=1;sent=[];
    send(value){this.sent.push(JSON.parse(value));}
    receive(frame){this.dispatchEvent(new MessageEvent('message',{data:JSON.stringify(frame)}));}
    close(){this.readyState=3;this.dispatchEvent(new Event('close'));}
  }
  const sockets=[];let idle=0,tools=0;
  const backend={
    async rpc(method){
      if(method==='VoiceCallHarnessSession')return {instructions:'fixture',tools:[{type:'function',name:'fixture_tool'}]};
      if(method==='VoiceCallHarnessTool'){tools++;return {kind:'SERVED',output:{ok:true}};}
      return {websocketUrl:'wss://api.x.ai/v1/realtime',model:'fixture',clientSecret:'fixture',expiresAtUnixSeconds:2100000000};
    },async gatewayCall(){return {entries:[]};}
  };
  const session=new NativeBotSession({backend,bot:{id:'fixture',name:'fixture'},canRecoverIdle:()=>true,pollMs:60000,
    voiceFactory:()=>new RealtimeSession({idleNow:()=>idle,socketFactory:()=>{const s=new Socket();sockets.push(s);return s;}})});
  const handshake=s=>{s.receive({type:'session.created'});s.receive({type:'session.updated'});s.receive({type:'conversation.created',conversation:{id:'conversation-fixture'}});};
  const response=(s,id,turn)=>s.receive({type:'response.created',response:{id,metadata:{bridge_turn:String(turn)}}});
  const tool=(s,responseId,callId)=>s.receive({type:'response.function_call_arguments.done',response_id:responseId,call_id:callId,name:'fixture_tool',arguments:'{}'});
  try{
    const opening=session.open();await until(()=>sockets.length===1);handshake(sockets[0]);await opening;
    session.beginTurn();session.sendAudio(Buffer.alloc(9600));session.endTurn();
    sockets[0].receive({type:'input_audio_buffer.committed',item_id:'input'});
    sockets[0].receive({type:'conversation.item.input_audio_transcription.completed',item_id:'input',status:'completed',transcript:'fixture'});
    response(sockets[0],'tool-response',1);tool(sockets[0],'tool-response','same-tool');
    sockets[0].receive({type:'response.done',response:{id:'tool-response',status:'completed'}});
    await new Promise(resolve=>setImmediate(resolve));
    response(sockets[0],'continuation',1);sockets[0].receive({type:'response.output_audio.delta',response_id:'continuation',delta:'AQA='});
    sockets[0].receive({type:'response.done',response:{id:'continuation',status:'completed'}});assert.equal(tools,1);
    idle=900000;sockets[0].receive({type:'error',error:{type:'server_error',message:'Session idle timeout'}});
    await until(()=>sockets.length===2);handshake(sockets[1]);await session.recoveryPromise;
    const audio=[];session.on('audio',e=>audio.push(e));
    response(sockets[1],'continuation',1);sockets[1].receive({type:'response.output_audio.delta',response_id:'continuation',delta:'AQA='});
    response(sockets[1],'tool-response',1);sockets[1].receive({type:'response.output_audio.delta',response_id:'tool-response',delta:'AQA='});
    assert.deepEqual(audio,[],'retired response IDs must stay fenced before the next BOOT');
    session.beginTurn();session.sendAudio(Buffer.alloc(9600));session.endTurn();assert.equal(session.voice.turn,2);
    response(sockets[1],'replayed-old-response',1);tool(sockets[1],'replayed-old-response','same-tool');
    response(sockets[1],'new-response',2);tool(sockets[1],'new-response','same-tool');
    await new Promise(resolve=>setImmediate(resolve));assert.equal(tools,1,'a new socket cannot repeat an existing native tool call');
    tool(sockets[1],'new-response','fresh-tool');await new Promise(resolve=>setImmediate(resolve));assert.equal(tools,2,'fresh tools must remain available');
  }finally{await session.close();}
});
