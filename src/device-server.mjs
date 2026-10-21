import tls from 'node:tls';
import { CaptionBook } from './caption-book.mjs';
import { NativeBotSession } from './native-bot.mjs';
import { PcmResampler, validatePcm } from './audio.mjs';
import { encodeFrame, FrameDecoder, PlaybackQueue } from './device-wire.mjs';
import { petMessage,encodePetMessage,shortCaption,privateLanPeer,bridgeIdentity } from './pet-protocol.mjs';

export async function startDeviceServer({ backend, bot, key, host = '0.0.0.0', port = 8790,bridgeId,
  sessionFactory = () => new NativeBotSession({ backend, bot, replyStyle:'companion' }) }) {
  if (!Buffer.isBuffer(key) || key.length !== 32) throw new Error('INVALID_PAIRING_KEY');
  bridgeId ??= bridgeIdentity(key.toString('hex'));
  let active, connections = 0, paused = false, stopping = false;
  const stats = { state: 'disconnected', bot: bot.name, turns: 0, replies: 0, reconnects: 0, fault: null, firstAudioMs: [], diagnostics: [], nativeEvents: {}, uplinkBytes: 0, recognizedInputs: 0 };
  const diagnostic = code => { stats.diagnostics.push(code); if (stats.diagnostics.length > 20) stats.diagnostics.shift(); };
  const sockets = new Set(), calls = new Set();
  const server = tls.createServer({ minVersion: 'TLSv1.2', maxVersion: 'TLSv1.2', ciphers: 'PSK-AES128-GCM-SHA256',
    handshakeTimeout: 5000, pskIdentityHint: 'grok-esp32',
    pskCallback: (_socket, identity) => identity === 'grok-esp32' ? key : null }, socket => {
    if (active || paused || stopping || !privateLanPeer(socket.remoteAddress)) { socket.destroy(); return; }
    sockets.add(socket); connections++; stats.reconnects = Math.max(0, connections - 1);
    const call = { socket, session: sessionFactory(), turn: 0, ready: false, recording: false,
      book:new CaptionBook(),
      up: new PcmResampler(16000, 24000), down: new PcmResampler(24000, 16000) };
    active = call; stats.state = 'connecting'; stats.fault = null;
    calls.add(call);
    const current = () => active === call && !socket.destroyed;
    const send = (type, turn, payload) => {
      if (!current()) return;
      if (socket.writableLength > 128 * 1024) throw new Error('DEVICE_PIPE_OVERFLOW');
      socket.write(encodeFrame(type, turn, payload));
    };
    const control = text => send(2, call.turn, Buffer.from(text));
    const metadata=message=>{if(call.petGeneration&&current())send(2,call.turn,encodePetMessage({...message,generation:call.petGeneration}));};
    const catalog=()=>{if(call.capabilities?.has('caption_pages'))metadata({op:'caption_catalog',turn:call.turn,...call.book.catalog()});};
    call.playback = new PlaybackQueue(send);
    const updateDrained = () => {
      if (!call.recording && call.playback.available === 20 && !call.playback.queue.length && !call.session.voice?.responseActive) stats.state = 'ready';
    };
    const cleanup = () => {
      if (call.closing) return call.closing;
      clearTimeout(call.timer);clearInterval(call.clockTimer);call.book.clear(); sockets.delete(socket);
      if (active === call) { active = null; stats.state = 'disconnected'; }
      diagnostic(call.ready ? 'DEVICE_CLOSED_READY' : 'DEVICE_CLOSED_OPENING');
      socket.destroy();
      call.closing = Promise.resolve().then(async () => {
        const result = await call.session.close();
        await call.opening;
        if (!active && result?.saved === false) stats.fault = 'CALL_SAVE_FAILED';
      }).catch(() => { if (!active) stats.fault = 'CALL_SAVE_FAILED'; }).finally(() => calls.delete(call));
      return call.closing;
    };
    call.cleanup = cleanup;
    const fail = code => {
      if (!current()) return;
      stats.fault = /^[A-Z0-9_]+$/.test(code) ? code : 'DEVICE_CONNECTION_FAILED';
      diagnostic(stats.fault);
      if(code==='AUTH_REQUIRED'&&call.capabilities?.has('turn_status')){
        try{metadata({op:'turn_status',turn:call.turn,code:'auth_required'});}catch{}
      }
      try { control('fault'); } catch {}
      void cleanup();
    };
    call.session.on('audio', pcm => {
      if (!current() || call.recording) return;
      try { call.playback.push(call.down.push(pcm)); stats.state = 'speaking'; } catch (error) { fail(error.message); }
    });
    call.session.on('clearAudio', () => {
      if (!current()) return;
      call.book.clear();catalog();
      call.playback.reset(call.turn); call.down = new PcmResampler(24000, 16000);
      try { control('clear'); } catch (error) { fail(error.message); }
    });
    call.session.on('responseDone', () => {
      if (!current() || call.recording) return;
      try { call.book.finish();catalog();call.playback.push(call.down.finish()); call.playback.done(); stats.replies++; updateDrained(); } catch (error) { fail(error.message); }
    });
    call.session.on('state', state => {
      if (current() && call.ready && !call.recording && state === 'ready') {
        try { call.playback.done(); stats.state = 'ready'; } catch (error) { fail(error.message); }
      }
    });
    call.session.on('latency', ({ firstAudioMs }) => { if (current()) { stats.firstAudioMs.push(Math.round(firstAudioMs)); if (stats.firstAudioMs.length > 100) stats.firstAudioMs.shift(); } });
    call.session.on('turnStatus',event=>{
      if(!current()||!call.ready||call.recording||event.turn!==call.voiceTurn)return;
      if(event.code==='progress'&&!call.capabilities?.has('turn_progress'))return;
      stats.lastTurnStatus=event.code;
      if(call.capabilities?.has('turn_status')){
        try{metadata({op:'turn_status',turn:call.turn,code:event.code});}catch(error){fail(error.message);}
      }
    });
    call.session.on('taskChannel', event => { if (current()) { stats.taskReturnAvailable = event.available; stats.taskChannelStatus = event.status ?? null;
      try{metadata({op:'task_channel',available:event.available});}catch(error){fail(error.message);} } });
    call.captionItems=new Map();call.captionSequence=0;
    call.session.on('transcript',event=>{if(current()&&event.role==='user'&&event.text?.trim())stats.recognizedInputs=(stats.recognizedInputs??0)+1;});
    call.session.on('transcript',event=>{
      if(!current()||!call.petGeneration||call.recording||event.role!=='assistant'||
         (event.turn!=null&&event.turn!==call.voiceTurn))return;
      try{call.book.receive({turn:call.turn,itemId:event.itemId??'current',text:event.text});catalog();}catch{diagnostic('CAPTION_TEXT_INVALID');}
      const text=shortCaption(event.text);if(!text)return;
      const item=event.itemId??'current';
      if(!call.captionItems.has(item))call.captionItems.set(item,'c-'+(++call.captionSequence));
      const id=call.captionItems.get(item);
      if(call.captionItems.size>64)call.captionItems.delete(call.captionItems.keys().next().value);
      try{metadata({op:'caption',turn:call.turn,id,text});}catch(error){fail(error.message);}
    });
    call.session.on('fault', ({ code }) => { if (code !== 'TASK_RETURN_UNAVAILABLE') fail(code); });
    call.session.on('closed', () => { void cleanup(); });
    call.session.on('endRequested', () => { call.timer = setTimeout(() => void cleanup(), 5000); call.timer.unref(); });
    const decoder = new FrameDecoder(({ type, turn, payload }) => {
      if (!current()) return;
      if (type === 3) {
        if (!call.ready || !call.recording || turn !== call.turn) return;
        if (payload.length > 1280) throw new Error('DEVICE_AUDIO_SIZE');
        validatePcm(payload); stats.uplinkBytes += payload.length;
        for (let i = 0; i < payload.length; i += 2) { const sample = payload.readInt16LE(i); call.energy = (call.energy ?? 0) + sample * sample; call.samples = (call.samples ?? 0) + 1; }
        call.session.sendAudio(call.up.push(payload)); return;
      }
      const action = payload.toString('utf8');
      if(action.startsWith('{')){
        const message=petMessage(payload);
        if(message?.op==='caption_read'){
          if(!call.capabilities?.has('caption_pages')||message.generation!==call.petGeneration||message.turn!==call.turn||turn!==call.turn||call.recording)return;
          const identity={op:'caption_page',turn:call.turn,request:message.request,version:message.version,page:message.page};
          const now=performance.now();
          if(message.request<=(call.readerSequence??0)){metadata({...identity,error:'stale'});return;}
          call.readerSequence=message.request;
          if(call.readerAt!=null&&now-call.readerAt<1000){metadata({...identity,error:'busy'});return;}
          call.readerAt=now;
          try{
            const version=message.version||call.book.open().version;
            metadata({...identity,...call.book.page({version,page:message.page})});
          }catch(error){metadata({...identity,error:error.message==='CAPTION_STALE'?'stale':'unavailable'});}
          return;
        }
        if(message?.op!=='hello'||!bridgeId||call.petGeneration)throw new Error('PET_PROTOCOL');
        call.petGeneration=message.generation;call.capabilities=new Set((message.capabilities??[]).filter(c=>['turn_status','turn_progress','caption_pages'].includes(c)));stats.petProtocol=2;diagnostic("PET_HELLO");
        metadata({op:'hello',v:2,bridgeId,...(message.capabilities?{capabilities:[...call.capabilities]}:{})});metadata({op:'link',state:call.ready?'ready':'connecting'});
        metadata({op:'clock',epochMs:Date.now()});
        call.clockTimer=setInterval(()=>{try{metadata({op:'clock',epochMs:Date.now()});}catch(error){fail(error.message);}},30000);call.clockTimer.unref();
        catalog();
        if(typeof stats.taskReturnAvailable==='boolean')metadata({op:'task_channel',available:stats.taskReturnAvailable});
        return;
      }
      if (action === 'ping') { control('pong'); return; }
      stats.lastControl={action:['press','release','cancel','credit'].includes(action)?action:'other',turn};
      if(action==='cancel')stats.cancelCount=(stats.cancelCount??0)+1;
      if (!call.ready) return;
      if (action === 'press') {
        if (turn <= call.turn) return;
        // beginTurn clears an unfinished old capture without committing it.
        call.turn = turn; call.recording = true; call.up = new PcmResampler(16000, 24000);
        call.energy = 0; call.samples = 0;
        call.session.beginTurn();call.voiceTurn=call.session.voice?.turn;call.book.start(call.petGeneration,turn);call.captionItems.clear();catalog(); stats.state = 'recording'; stats.turns++;
        clearTimeout(call.timer); call.timer = setTimeout(() => fail('RECORDING_LIMIT'), 60000); call.timer.unref();
      } else if (action === 'release' && turn === call.turn && call.recording) {
        clearTimeout(call.timer); call.session.sendAudio(call.up.finish()); call.recording = false; stats.state = 'waiting';
        stats.lastInputBytes = call.session.voice?.inputBytes;
        stats.lastInputDb = Math.round(10 * Math.log10((call.energy ?? 0) / Math.max(1, call.samples ?? 0) / 1073741824 + 1e-10));
        control('sent'); call.session.endTurn();
      } else if (action === 'cancel' && turn === call.turn) {
        clearTimeout(call.timer); call.recording = false; call.session.stopPlayback();call.book.clear();catalog(); stats.state = 'ready';
        call.playback.done();
      } else if (action === 'credit') {
        call.playback.credit(turn, 1);
        updateDrained();
      }
      else if (action !== 'release') throw new Error('DEVICE_COMMAND');
    });
    socket.setNoDelay(true); socket.setTimeout(30000, () => fail('DEVICE_TIMEOUT'));
    socket.on('data', chunk => { try { decoder.push(chunk); } catch (error) { fail(error.message); } });
    socket.on('error', () => { void cleanup(); }); socket.on('close', () => { void cleanup(); });
    call.opening = (async () => {
      try {
        await call.session.open(); if (!current()) return;
        call.session.voice?.socket?.addEventListener('message', event => {
          if (!current()) return;
          try { const message=JSON.parse(event.data),{type}=message;
            if (typeof type === 'string' && /^[a-z_.]{1,100}$/.test(type)) stats.nativeEvents[type] = (stats.nativeEvents[type] ?? 0) + 1;
            if(type==='response.output_audio.delta')stats.nativeAudioMatch={state:call.session.voice.state,hasResponseId:typeof message.response_id==='string',owned:message.response_id===call.session.voice.responseId,cancelled:call.session.voice.cancelled.has(message.response_id),keys:Object.keys(message).filter(key=>key!=='delta')};
            if(['response.created','response.done'].includes(type)){
              const tag=message.response?.metadata?.bridge_turn;
              const status=message.response?.status;
              const entry={event:type,tag:/^\d{1,6}$/.test(String(tag))?Number(tag):null,turn:call.session.voice.turn,
                status:['in_progress','completed','cancelled','failed'].includes(status)?status:'unknown',owned:message.response?.id===call.session.voice.responseId};
              stats.nativeResponses??=[];stats.nativeResponses.push(entry);if(stats.nativeResponses.length>12)stats.nativeResponses.shift();
            }
          } catch {}
        });
        call.ready = true; stats.state = 'ready'; diagnostic('NATIVE_READY'); control('ready');metadata({op:'link',state:'ready'});
      }
      catch (error) { fail(error.code ?? error.message); }
    })();
  });
  server.on('tlsClientError', () => {});
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen(port, host, resolve); });
  return { port: server.address().port, snapshot: () => ({ ...stats, firstAudioMs: [...stats.firstAudioMs], playback:active?{credits:active.playback.available,packets:active.playback.queue.length,bytes:active.playback.bytes,responseActive:!!active.session.voice?.responseActive,recording:active.recording}:null }),
    disconnect: () => active?.socket.destroy(),
    pause: () => { paused = true; active?.socket.destroy(); }, resume: () => { paused = false; },
    close: async () => {
      stopping = true; const closed = new Promise(resolve => server.close(resolve));
      await Promise.all([...calls].map(call => call.cleanup())); await closed;
    } };
}
