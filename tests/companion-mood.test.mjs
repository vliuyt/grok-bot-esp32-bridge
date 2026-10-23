import {test} from 'node:test';
import assert from 'node:assert/strict';
import tls from 'node:tls';
import {once} from 'node:events';
import {setTimeout as delay} from 'node:timers/promises';
import {RealtimeSession} from '../src/realtime.mjs';
import {NativeBotSession} from '../src/native-bot.mjs';
import {startDeviceServer} from '../src/device-server.mjs';
import {encodeFrame,FrameDecoder} from '../src/device-wire.mjs';
import {petMessage,encodePetMessage} from '../src/pet-protocol.mjs';

class Socket extends EventTarget {
  readyState=1;sent=[];
  send(text){this.sent.push(JSON.parse(text));}
  receive(frame){this.dispatchEvent(new MessageEvent('message',{data:JSON.stringify(frame)}));}
  close(){if(this.readyState===3)return;this.readyState=3;this.dispatchEvent(new Event('close'));}
}
const credential={websocketUrl:'wss://api.x.ai/v1/realtime',model:'fixture',clientSecret:'fixture',expiresAtUnixSeconds:2100000000};
const plan=Object.freeze({instructions:'Original persona, required calculations and tools.',tools:[{type:'function',name:'fixture',parameters:{type:'object',properties:{}}}],voice:'native'});
function handshake(socket){socket.receive({type:'session.created'});socket.receive({type:'session.updated'});socket.receive({type:'conversation.created',conversation:{id:'fixture-conversation'}});}
async function opened(t,options={}){
  const socket=new Socket(),voice=new RealtimeSession({socketFactory:()=>socket});
  t.after(()=>voice.close());const p=voice.open({credential,plan,...options});handshake(socket);await p;return {socket,voice};
}
const responses=socket=>socket.sent.filter(m=>m.type==='response.create');
function submit(voice,options){voice.beginTurn(options);voice.sendAudio(Buffer.alloc(9600));voice.endTurn();}
function complete(socket,turn){socket.receive({type:'response.created',response:{id:`reply-${turn}`,metadata:{bridge_turn:String(turn)}}});socket.receive({type:'response.done',response:{id:`reply-${turn}`,status:'completed'}});}
async function until(predicate){const end=performance.now()+2500;while(!predicate()){assert(performance.now()<end,'local fixture timed out');await delay(5);}}

test('mood press carries only a finite enum and checked turn identity',()=>{
  const press={op:'press',generation:7,turn:1,mood:'warm'};
  assert.deepEqual(petMessage(encodePetMessage(press)),press);
  assert.deepEqual(petMessage(Buffer.from(JSON.stringify({...press,instructions:'untrusted'}))),press);
  assert.equal(petMessage(encodePetMessage(press),{generation:8}),null);
  assert.equal(petMessage(encodePetMessage(press),{turn:2}),null);
  for(const patch of [{mood:'arbitrary instruction'},{mood:'playful'},{turn:0},{turn:-1},{mood:null}])assert.throws(()=>encodePetMessage({...press,...patch}),/PET_PROTOCOL/);
});
test('warm response preserves the entire persona and changes no session, voice or tools',async t=>{
  const {voice,socket}=await opened(t);submit(voice,{mood:'warm'});
  const response=responses(socket)[0].response;
  assert.equal(response.metadata.bridge_turn,'1');assert.equal(typeof response.instructions,'string');assert.ok(response.instructions.startsWith(plan.instructions+'\n\n'));
  assert.match(response.instructions,/设备.*触摸/);assert.match(response.instructions,/用户.*优先/);
  assert.equal(socket.sent.filter(m=>m.type==='session.update').length,1);
  assert.deepEqual(socket.sent.find(m=>m.type==='session.update').session.tools,plan.tools);
  assert.equal(socket.sent.filter(m=>m.type==='conversation.item.create').length,0);
  assert.equal(plan.instructions,'Original persona, required calculations and tools.');
  complete(socket,1);submit(voice);assert.equal(responses(socket)[1].response.instructions,undefined);
});
test('short input, cancellation and a new BOOT never leak an old mood',async t=>{
  const {voice,socket}=await opened(t);
  voice.beginTurn({mood:'warm'});voice.endTurn();assert.equal(responses(socket).length,0);
  submit(voice);assert.equal(responses(socket)[0].response.instructions,undefined);complete(socket,2);
  voice.beginTurn({mood:'warm'});voice.stopPlayback();
  submit(voice,{mood:'neutral'});assert.equal(responses(socket).at(-1).response.instructions,undefined);
  const before=voice.turn;assert.throws(()=>voice.beginTurn({mood:'untrusted'}),/mood/i);assert.equal(voice.turn,before);
});
test('a tool continuation retains only the current real turn mood',async t=>{
  const {voice,socket}=await opened(t,{onTool:async()=>({value:5535})});submit(voice,{mood:'warm'});
  socket.receive({type:'response.created',response:{id:'tool-reply',metadata:{bridge_turn:'1'}}});
  socket.receive({type:'response.function_call_arguments.done',response_id:'tool-reply',call_id:'one',name:'fixture',arguments:'{}'});
  await until(()=>socket.sent.some(m=>m.item?.type==='function_call_output'));
  socket.receive({type:'response.done',response:{id:'tool-reply',status:'completed'}});
  assert.equal(responses(socket).length,2);assert.equal(responses(socket)[1].response.instructions,responses(socket)[0].response.instructions);
});

async function deviceFixture(t,capabilities=['companion_mood'],style='companion'){
  const key=Buffer.alloc(32,42),bot={id:'fixture',name:'fixture'},socket=new Socket(),frames=[],calls=[];
  const backend={rpc:async name=>{calls.push(name);if(name==='VoiceCallHarnessSession')return plan;if(name==='MintSandVoiceCallSecret')return credential;throw new Error('Unexpected RPC');},gatewayCall:async name=>{calls.push(name);return {entries:[]};}};
  const voice=new RealtimeSession({socketFactory:()=>socket});let session;
  const server=await startDeviceServer({backend,bot,key,host:'127.0.0.1',port:0,sessionFactory:options=>(session=new NativeBotSession({backend,bot,voice,replyStyle:style,pollMs:60000,...options}))});
  const device=tls.connect({port:server.port,host:'127.0.0.1',minVersion:'TLSv1.2',maxVersion:'TLSv1.2',ciphers:'PSK-AES128-GCM-SHA256',rejectUnauthorized:false,pskCallback:()=>({identity:'grok-esp32',psk:key})});
  device.on('error',()=>{});device.on('data',chunk=>decoder.push(chunk));
  const decoder=new FrameDecoder(f=>frames.push(f));
  t.after(async()=>{device.destroy();await server.close();assert.equal(session.closed,true);assert.equal(voice.state,'closed');});
  await once(device,'secureConnect');await until(()=>socket.sent.length===0&&voice.state==='connecting');handshake(socket);await until(()=>server.snapshot().state==='ready');
  const send=(turn,payload,type=2)=>device.write(encodeFrame(type,turn,Buffer.isBuffer(payload)?payload:Buffer.from(typeof payload==='string'?payload:JSON.stringify(payload))));
  const barrier=async()=>{const before=frames.filter(f=>f.payload.toString()==='pong').length;send(0,'ping');await until(()=>frames.filter(f=>f.payload.toString()==='pong').length===before+1);};
  send(0,{op:'hello',v:2,generation:7,capabilities});await barrier();
  return {socket,voice,server,send,barrier,frames,calls};
}
test('paired TLS accepts negotiated atomic mood press and legacy press clears it',async t=>{
  const f=await deviceFixture(t);const hello=f.frames.filter(m=>m.payload[0]===123).map(m=>JSON.parse(m.payload)).find(m=>m.op==='hello');assert.deepEqual(hello.capabilities,['companion_mood']);
  assert.equal(f.server.snapshot().companionMoodCapable,true);
  const baseline=responses(f.socket).length;await f.barrier();assert.equal(responses(f.socket).length,baseline);
  for(const press of [{op:'press',generation:8,turn:1,mood:'warm'},{op:'press',generation:7,turn:2,mood:'warm'}]){f.send(1,press);await f.barrier();assert.equal(f.server.snapshot().turns,0);}
  f.send(1,{op:'press',generation:7,turn:1,mood:'warm'});for(let i=0;i<5;i++)f.send(1,Buffer.alloc(1280),3);f.send(1,'release');await until(()=>responses(f.socket).length===1);
  assert.equal(f.server.snapshot().lastCompanionMood,'warm');
  assert.ok(responses(f.socket)[0].response.instructions.startsWith('Original persona'));complete(f.socket,1);await f.barrier();
  f.send(1,{op:'press',generation:7,turn:1,mood:'warm'});await f.barrier();assert.equal(f.server.snapshot().turns,1);
  f.send(2,'press');for(let i=0;i<5;i++)f.send(2,Buffer.alloc(1280),3);f.send(2,'release');await until(()=>responses(f.socket).length===2);assert.equal(responses(f.socket)[1].response.instructions,undefined);
  assert.equal(f.server.snapshot().lastCompanionMood,'neutral');
});
test('JSON mood press without capability is rejected without starting a turn',async t=>{
  const f=await deviceFixture(t,[]);f.send(1,{op:'press',generation:7,turn:1,mood:'warm'});await until(()=>f.server.snapshot().fault==='PET_PROTOCOL');assert.equal(f.server.snapshot().turns,0);assert.equal(responses(f.socket).length,0);
});
test('non-companion native sessions ignore device style even if an enum is supplied',async t=>{
  const f=await deviceFixture(t,['companion_mood'],null);f.send(1,{op:'press',generation:7,turn:1,mood:'warm'});for(let i=0;i<5;i++)f.send(1,Buffer.alloc(1280),3);f.send(1,'release');await until(()=>responses(f.socket).length===1);assert.equal(responses(f.socket)[0].response.instructions,undefined);
});
