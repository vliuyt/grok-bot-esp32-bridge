import {test} from 'node:test';
import assert from 'node:assert/strict';
import {RealtimeSession} from '../src/realtime.mjs';

class Socket extends EventTarget {
  readyState=1;sent=[];
  send(text){this.sent.push(JSON.parse(text));}
  close(){this.readyState=3;this.dispatchEvent(new Event('close'));}
  receive(frame){this.dispatchEvent(new MessageEvent('message',{data:JSON.stringify(frame)}));}
}
const credential={websocketUrl:'wss://api.x.ai/v1/realtime',model:'fixture',clientSecret:'fixture',expiresAtUnixSeconds:2100000000};
const plan={instructions:'fixture',tools:[]};
// Synthetic signature fixture; the real idle error is retained only as fixed categories.
const idleError={type:'error',error:{type:'server_error',message:'Session idle timeout'}};
async function fixture(options={}){
  let now=1800000000000,idle=0,url;const socket=new Socket();
  const voice=new RealtimeSession({socketFactory:value=>{url=value;return socket;},now:()=>now,idleNow:()=>idle});
  const opening=voice.open({credential,plan,...options});socket.receive({type:'session.created'});socket.receive({type:'session.updated'});
  if(options.conversationId)socket.receive({type:'conversation.created',conversation:{id:options.conversationId}});
  await opening;
  const faults=[];let recoveries=0;
  voice.on('fault',e=>faults.push(e.code));
  voice.on('idleExpired',()=>{recoveries++;voice.close();});
  return {voice,socket,faults,url:()=>url,recoveries:()=>recoveries,advance:ms=>{now+=ms;idle+=ms;},wallJump:ms=>{now+=ms;}};
}
function completedReply(voice,socket,{item='input',response='reply',asr='completed',commit=true}={}){
  voice.beginTurn();voice.sendAudio(Buffer.alloc(9600));voice.endTurn();
  if(commit)socket.receive({type:'input_audio_buffer.committed',item_id:item});
  if(commit&&asr!==null)socket.receive({type:'conversation.item.input_audio_transcription.completed',item_id:item,...(asr==='legacy'?{}:{status:asr}),transcript:'fixture'});
  socket.receive({type:'response.created',response:{id:response,metadata:{bridge_turn:String(voice.turn)}}});
  socket.receive({type:'response.output_audio.delta',response_id:response,delta:'AQA='});
  socket.receive({type:'response.done',response:{id:response,status:'completed'}});
}

test('a settled native idle expiry can be claimed without fault or replay',async()=>{
  const f=await fixture();
  try{
    f.advance(900000);f.socket.receive(idleError);
    assert.equal(f.recoveries(),1);assert.deepEqual(f.faults,[]);assert.equal(f.voice.state,'closed');
    assert.equal(f.socket.sent.filter(e=>e.type==='response.create').length,0);
    assert.equal(f.socket.sent.filter(e=>e.type==='input_audio_buffer.commit').length,0);
  }finally{f.voice.close();}
});
test('an unclaimed idle expiry still follows the ordinary failure path',async()=>{
  const f=await fixture();
  try{f.voice.removeAllListeners('idleExpired');f.advance(900000);f.socket.receive(idleError);assert.deepEqual(f.faults,['VOICE_SERVICE_ERROR']);}
  finally{f.voice.close();}
});
test('resumption uses only a validated conversation ID in the native URL',async()=>{
  const f=await fixture({conversationId:'conversation-fixture'});
  try{assert.equal(new URL(f.url()).searchParams.get('conversation_id'),'conversation-fixture');}
  finally{f.voice.close();}
  for(const conversationId of ['',{},'x'.repeat(257),'private\nvalue']){
    let opened=0;const voice=new RealtimeSession({socketFactory:()=>{opened++;throw new Error('Invalid ID must not dial');}});
    try{await assert.rejects(voice.open({credential,plan,conversationId}));assert.equal(opened,0);}finally{voice.close();}
  }
});
test('a wall-clock jump does not turn a fresh connection into an idle recovery',async()=>{
  const f=await fixture();
  try{f.wallJump(900000);f.socket.receive(idleError);assert.equal(f.recoveries(),0);assert.deepEqual(f.faults,['VOICE_SERVICE_ERROR']);}
  finally{f.voice.close();}
});

test('resumption requires both session configuration and the matching conversation, in either order',async()=>{
  for(const idFirst of [true,false]){
    const socket=new Socket(),voice=new RealtimeSession({socketFactory:()=>socket}),states=[];
    const opening=voice.open({credential,plan,conversationId:'expected-conversation'});voice.on('ready',()=>states.push('ready'));
    try{
      socket.receive({type:'session.created'});
      const configured={type:'session.updated'},confirmed={type:'conversation.created',conversation:{id:'expected-conversation'}};
      socket.receive(idFirst?confirmed:configured);await Promise.resolve();
      assert.equal(voice.state,'connecting');assert.deepEqual(states,[]);
      socket.receive(idFirst?configured:confirmed);await opening;assert.equal(voice.state,'ready');assert.deepEqual(states,['ready']);
    }finally{voice.close();}
  }
});
test('a different or missing resumed conversation never reports ready',async()=>{
  for(const id of ['different-conversation',null]){
    const socket=new Socket(),voice=new RealtimeSession({socketFactory:()=>socket}),ready=[];
    const opening=voice.open({credential,plan,conversationId:'expected-conversation'});voice.on('ready',()=>ready.push(true));
    const rejected=assert.rejects(opening);
    try{
      socket.receive({type:'session.created'});socket.receive({type:'session.updated'});
      if(id)socket.receive({type:'conversation.created',conversation:{id}});
      else{assert.equal(voice.state,'connecting');voice.close();}
      await rejected;assert.deepEqual(ready,[]);assert.equal(voice.state,'closed');
    }finally{voice.close();}
  }
});
test('unconfirmed commits and unfinished ASR never qualify as settled idle',async()=>{
  for(const options of [{commit:false},{asr:'in_progress'},{asr:null}]){
    const f=await fixture();
    try{completedReply(f.voice,f.socket,options);f.advance(900000);f.socket.receive(idleError);assert.equal(f.recoveries(),0);assert.deepEqual(f.faults,['VOICE_SERVICE_ERROR']);}
    finally{f.voice.close();}
  }
});
test('final and legacy ASR history permits recovery, including a settled old ASR failure',async()=>{
  for(const asr of ['completed','legacy','failed-old']){
    const f=await fixture();
    try{
      if(asr==='failed-old'){
        completedReply(f.voice,f.socket,{item:'old',response:'old-response',asr:'in_progress'});
        completedReply(f.voice,f.socket,{item:'new',response:'new-response'});
        const statuses=[];f.voice.on('turnStatus',e=>statuses.push(e));
        f.socket.receive({type:'conversation.item.input_audio_transcription.failed',item_id:'old'});assert.deepEqual(statuses,[]);
      }else completedReply(f.voice,f.socket,{asr});
      f.advance(900000);f.socket.receive(idleError);assert.equal(f.recoveries(),1);assert.deepEqual(f.faults,[]);
    }finally{f.voice.close();}
  }
});
test('malformed commit ownership or eviction of unresolved ASR makes recovery conservative',async()=>{
  for(const malformed of [true,false]){
    const f=await fixture();
    try{
      if(malformed){completedReply(f.voice,f.socket,{commit:false});f.socket.receive({type:'input_audio_buffer.committed'});}
      else for(let i=0;i<129;i++)completedReply(f.voice,f.socket,{item:`input-${i}`,response:`reply-${i}`,asr:i===0?'in_progress':'completed'});
      f.advance(900000);f.socket.receive(idleError);assert.equal(f.recoveries(),0);assert.deepEqual(f.faults,['VOICE_SERVICE_ERROR']);
    }finally{f.voice.close();}
  }
});
test('in-flight tools cannot be resumed or deliver late results after an idle error',async()=>{
  let finish;const f=await fixture({plan:{...plan,tools:[{type:'function',name:'fixture_tool'}]},onTool:()=>new Promise(resolve=>{finish=resolve;})});
  try{
    completedReply(f.voice,f.socket);
    f.socket.receive({type:'response.function_call_arguments.done',response_id:'reply',call_id:'tool',name:'fixture_tool',arguments:'{}'});
    f.advance(900000);f.socket.receive(idleError);assert.equal(f.recoveries(),0);finish({ok:true});
    await new Promise(resolve=>setImmediate(resolve));assert.equal(f.socket.sent.filter(e=>e.item?.type==='function_call_output').length,0);
  }finally{f.voice.close();finish?.({ok:true});}
});
test('recording, waiting, response playback and unscoped task work stay terminal',async()=>{
  for(const phase of ['recording','waiting','playback','unscoped-task']){
    const f=await fixture();
    try{
      if(phase==='unscoped-task'){completedReply(f.voice,f.socket);f.voice.injectUpdate({id:'fixture',text:'fixture'});}
      else{
        f.voice.beginTurn();
        if(phase!=='recording'){f.voice.sendAudio(Buffer.alloc(9600));f.voice.endTurn();}
        if(phase==='playback')f.socket.receive({type:'response.created',response:{id:'old'}});
      }
      f.advance(900000);f.socket.receive(idleError);assert.equal(f.recoveries(),0);assert.deepEqual(f.faults,['VOICE_SERVICE_ERROR']);
    }finally{f.voice.close();}
  }
});
test('other errors and closed transports are never reclassified as idle recovery',async()=>{
  for(const kind of ['ordinary','auth','asr','closed-socket','recent']){
    const f=await fixture();
    try{
      if(kind!=='recent')f.advance(900000);if(kind==='closed-socket')f.socket.readyState=3;
      const error=kind==='ordinary'?{type:'error',error:{type:'server_error',message:'private fixture'}}:
        kind==='auth'?{type:'error',error:{type:'authentication_error',message:'session idle timeout'}}:
        kind==='asr'?{type:'error',error:{type:'server_error',message:'ASR stream timed out'}}:idleError;
      f.socket.receive(error);assert.equal(f.recoveries(),0,kind);assert.deepEqual(f.faults,['VOICE_SERVICE_ERROR']);
    }finally{f.voice.close();}
  }
});
