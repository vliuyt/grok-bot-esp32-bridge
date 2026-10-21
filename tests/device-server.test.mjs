import { test } from 'node:test';
import assert from 'node:assert/strict';
import tls from 'node:tls';
import { EventEmitter, once } from 'node:events';
import { setTimeout as delay } from 'node:timers/promises';
import { startDeviceServer } from '../src/device-server.mjs';
import { encodeFrame, FrameDecoder } from '../src/device-wire.mjs';
const key = Buffer.alloc(32, 42);
test('pet hello negotiates metadata and captions only for the current spoken turn',async()=>{
  let session;
  class Session extends EventEmitter {
    voice={turn:0};async open(){}beginTurn(){this.voice.turn++;this.emit('clearAudio');}
    sendAudio(){}endTurn(){}async close(){return {saved:true};}
  }
  const server=await startDeviceServer({bot:{name:'fixture'},key,host:'127.0.0.1',port:0,bridgeId:'a'.repeat(32),
    sessionFactory:()=>(session=new Session())});
  const socket=await client(server.port),frames=[];
  const decoder=new FrameDecoder(f=>frames.push(f));socket.on('data',c=>decoder.push(c));
  try{
    assert.equal(server.snapshot().recognizedInputs,0);
    session.emit('transcript',{role:'user',text:'测试'});
    assert.equal(server.snapshot().recognizedInputs,1);
    await delay(20);assert(!frames.some(f=>f.payload[0]===123));
    const hello=encodeFrame(2,0,Buffer.from('{"op":"hello","v":2,"generation":7}'));
    socket.write(hello.subarray(0,11));socket.write(hello.subarray(11));await delay(20);
    const metadata=()=>frames.filter(f=>f.type===2&&f.payload[0]===123).map(f=>JSON.parse(f.payload));
    assert(metadata().some(m=>m.op==='hello'&&m.generation===7&&m.bridgeId==='a'.repeat(32)));
    socket.write(encodeFrame(2,4,Buffer.from('press')));socket.write(encodeFrame(2,4,Buffer.from('release')));await delay(20);
    session.emit('transcript',{role:'assistant',turn:0,text:'stale'});
    session.emit('transcript',{role:'assistant',turn:1,itemId:'fixture-one',text:'苹果'.repeat(60)});await delay(20);
    const captions=metadata().filter(m=>m.op==='caption');assert.equal(captions.length,1);
    assert.equal(captions[0].turn,4);assert.equal([...captions[0].text].length,80);
    assert.equal(captions[0].text.includes('stale'),false);
  }finally{socket.destroy();await server.close();}
});
async function client(port, secret = key) {
  const socket = tls.connect({ port, host: '127.0.0.1', minVersion: 'TLSv1.2', maxVersion: 'TLSv1.2',
    ciphers: 'PSK-AES128-GCM-SHA256', rejectUnauthorized: false, pskCallback: () => ({ identity: 'grok-esp32', psk: secret }) });
  await once(socket, 'secureConnect'); return socket;
}
test('TLS pairing rejects a wrong key and reconnect never replays a submitted turn', async () => {
  const calls = [];
  class Session extends EventEmitter {
    async open() {} beginTurn() {} sendAudio() {} endTurn() { this.commits = (this.commits ?? 0) + 1; }
    stopPlayback() { this.emit('clearAudio'); } async close() { return { saved: true }; }
  }
  const server = await startDeviceServer({ bot: { name: 'fixture' }, key, host: '127.0.0.1', port: 0,
    sessionFactory: () => { const session = new Session(); calls.push(session); return session; } });
  try {
    await assert.rejects(client(server.port, Buffer.alloc(32, 9)));
    const socket = await client(server.port); const frames = []; socket.on('data', chunk => decoder.push(chunk));
    const decoder = new FrameDecoder(frame => frames.push(frame));
    await delay(20); socket.write(encodeFrame(2, 1, Buffer.from('press')));
    socket.write(encodeFrame(3, 1, Buffer.alloc(640))); socket.write(encodeFrame(2, 1, Buffer.from('release')));
    socket.write(encodeFrame(2, 1, Buffer.from('release'))); await delay(30);
    assert.equal(calls[0].commits, 1); socket.destroy(); await delay(20);
    const replacement = await client(server.port); await delay(20);
    assert.equal(calls.length, 2); assert.equal(calls[1].commits, undefined); replacement.destroy();
  } finally { await server.close(); }
});
test('server shutdown waits for saving a disconnected call', async () => {
  let finish, entered; const saving = new Promise(resolve => { entered = resolve; });
  class Session extends EventEmitter {
    async open() {} async close() { entered(); await new Promise(resolve => { finish = resolve; }); return { saved: true }; }
  }
  const server = await startDeviceServer({ bot: { name: 'fixture' }, key, host: '127.0.0.1', port: 0, sessionFactory: () => new Session() });
  const socket = await client(server.port); socket.destroy(); await saving;
  let stopped = false; const stopping = server.close().then(() => { stopped = true; });
  await delay(10); assert.equal(stopped, false); finish(); await stopping; assert.equal(stopped, true);
});
test('ready state follows drain and response completion in either order', async () => {
  const until=async predicate=>{
    const deadline=performance.now()+2000;
    while(!predicate()){assert(performance.now()<deadline,'server did not reach the expected state');await delay(5);}
  };
  for (const creditFirst of [true, false]) {
    let session;
    class Session extends EventEmitter {
      voice = { responseActive: true };
      async open() {} beginTurn() { this.emit('clearAudio'); } sendAudio() {} endTurn() {} async close() { return { saved: true }; }
    }
    const server = await startDeviceServer({ bot: { name: 'fixture' }, key, host: '127.0.0.1', port: 0, sessionFactory: () => (session = new Session()) });
    try {
      const socket = await client(server.port); await until(()=>server.snapshot().state==='ready');
      socket.write(encodeFrame(2, 1, Buffer.from('press'))); socket.write(encodeFrame(2, 1, Buffer.from('release'))); await until(()=>server.snapshot().state==='waiting');
      session.emit('audio', Buffer.alloc(6));
      const credit = async () => { socket.write(encodeFrame(2, 1, Buffer.from('credit'))); await until(()=>server.snapshot().playback?.credits===20); };
      const done = () => { session.voice.responseActive = false; session.emit('responseDone'); };
      if (creditFirst) { await credit(); done(); } else { done(); await credit(); }
      assert.equal(server.snapshot().state, 'ready'); socket.destroy();
    } finally { await server.close(); }
  }
});
test('native task test callers can negotiate pet hello with a derived default bridge identity',async()=>{
 class Session extends EventEmitter{async open(){}async close(){return {saved:true};}}
 const server=await startDeviceServer({bot:{name:'fixture'},key,host:'127.0.0.1',port:0,sessionFactory:()=>new Session()});
 const socket=await client(server.port),frames=[];const decoder=new FrameDecoder(f=>frames.push(f));socket.on('data',c=>decoder.push(c));
 try{socket.write(encodeFrame(2,0,Buffer.from('{"op":"hello","v":2,"generation":7}')));await delay(30);assert.equal(server.snapshot().fault,null);assert(frames.some(f=>f.payload.toString().includes('"op":"hello"')));}finally{socket.destroy();await server.close();}
});
test('a newer press cancels unfinished capture without submitting the previous turn',async()=>{
 let session;class Session extends EventEmitter{begun=0;ended=0;async open(){}beginTurn(){this.begun++;}sendAudio(){}endTurn(){this.ended++;}async close(){return {saved:true};}}
 const server=await startDeviceServer({bot:{name:'fixture'},key,host:'127.0.0.1',port:0,sessionFactory:()=>(session=new Session())});
 const socket=await client(server.port);try{await delay(20);socket.write(encodeFrame(2,1,Buffer.from('press')));socket.write(encodeFrame(2,2,Buffer.from('press')));socket.write(encodeFrame(2,2,Buffer.from('release')));await delay(30);assert.equal(session.begun,2);assert.equal(session.ended,1);assert.equal(server.snapshot().fault,null);}finally{socket.destroy();await server.close();}
});

test('turn feedback is negotiated and late notices cannot affect newer recording',async()=>{
  for(const negotiated of [false,true]){
    let session;
    class Session extends EventEmitter{
      voice={turn:0,responseActive:false};async open(){}beginTurn(){this.voice.turn++;}sendAudio(){}endTurn(){}async close(){return{saved:true};}
    }
    const server=await startDeviceServer({bot:{name:'fixture'},key,host:'127.0.0.1',port:0,sessionFactory:()=>(session=new Session())});
    const socket=await client(server.port),frames=[];const decoder=new FrameDecoder(f=>frames.push(f));socket.on('data',c=>decoder.push(c));
    try{
      socket.write(encodeFrame(2,0,Buffer.from(JSON.stringify({op:'hello',v:2,generation:7,...(negotiated?{capabilities:['turn_status']}:{})}))));
      await delay(20);socket.write(encodeFrame(2,1,Buffer.from('press')));socket.write(encodeFrame(2,1,Buffer.from('release')));await delay(20);
      session.emit('turnStatus',{turn:1,code:'short_input'});await delay(20);
      const notices=()=>frames.map(f=>{try{return JSON.parse(f.payload);}catch{return null;}}).filter(m=>m?.op==='turn_status');
      assert.equal(notices().length,negotiated?1:0);
      if(negotiated)assert.deepEqual(notices()[0],{op:'turn_status',turn:1,code:'short_input',generation:7});
      socket.write(encodeFrame(2,2,Buffer.from('press')));await delay(20);
      session.emit('turnStatus',{turn:1,code:'no_reply'});await delay(20);
      assert.equal(notices().length,negotiated?1:0);assert.equal(server.snapshot().state,'recording');
    }finally{socket.destroy();await server.close();}
  }
});
