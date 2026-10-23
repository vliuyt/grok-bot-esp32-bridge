import { EventEmitter } from 'node:events';
import { validatePcm } from './audio.mjs';
import {createTurnMonitor} from './voice-turn.mjs';
import {VoiceTrace} from './voice-trace.mjs';
import {companionTurnInstructions} from './companion-style.mjs';

function idleSessionError(error) {
  if(error?.type!=='server_error')return false;
  const fields=[typeof error.code==='string'&&error.code.length<=128?error.code:'',typeof error.message==='string'&&error.message.length<=512?error.message:''];
  const text=fields.join(' ').replaceAll('_',' ');
  return /\b(?:session|conversation)\b/i.test(text)&&/\b(?:idle|inactive|inactivity|silence)\b/i.test(text)&&/\btimeout\b|\btimed out\b|\bdeadline\b/i.test(text)&&
    !/\b(?:auth|unauthorized|forbidden|credential|token|secret)\b|\brate.?limit/i.test(text);
}

export class RealtimeSession extends EventEmitter {
  constructor({ socketFactory = (url, protocols) => new WebSocket(url, protocols), now = Date.now,idleNow=()=>performance.now(),setTimer=setTimeout,clearTimer=clearTimeout } = {}) {
    super(); this.socketFactory = socketFactory; this.now = now; this.state = 'idle';
    this.idleNow=idleNow;this.lastInputAt=idleNow();
    this.trace = new VoiceTrace({now});
    this.turn = 0; this.cancelled = new Set(); this.toolCalls = new Set(); this.pendingTools = new Set(); this.updates = [];
    this.responseIds=new Set();
    this.inputTurns=[];this.inputItems=new Map();this.inputPending=new Set();this.inputStateUncertain=false;
    this.monitor=createTurnMonitor({now,setTimer,clearTimer,onStatus:event=>this.emit('turnStatus',event),onExpire:turn=>{
      if(turn!==this.turn||this.state==='closed')return;
      this.stopPlayback();if(this.state!=='closed'){this.state='ready';this.emit('state','ready');}
    }});
  }
  async open({ credential, plan, onTool = async () => ({ error: 'Tool unavailable' }), voice = null, language = 'zh',conversationId=null }) {
    if (this.state !== 'idle') throw new Error('Session already opened');
    if (Number(credential.expiresAtUnixSeconds) * 1000 <= this.now() + 5000 || !Number.isFinite(Number(credential.expiresAtUnixSeconds))) throw new Error('Voice credential expired');
    const url = new URL(credential.websocketUrl);
    if (url.origin !== 'wss://api.x.ai' || url.username || url.password || url.pathname !== '/v1/realtime') throw new Error('Unexpected realtime origin');
    if (!credential.clientSecret || typeof plan.instructions !== 'string' || !Array.isArray(plan.tools)) throw new Error('Invalid native voice contract');
    if(conversationId!==null){
      if(typeof conversationId!=='string'||!/^[\x21-\x7e]{1,256}$/.test(conversationId))throw new Error('Invalid conversation ID');
      url.searchParams.set('conversation_id',conversationId);
    }
    this.expectedConversationId=conversationId;this.sessionConfigured=false;
    url.searchParams.set('model', credential.model);
    this.state = 'connecting'; this.plan = plan; this.onTool = onTool;
    this.socket = this.socketFactory(url.toString(), [`xai-client-secret.${credential.clientSecret}`]);
    const opening = new Promise((resolve, reject) => { this.openResolve = resolve; this.openReject = reject; });
    this.openTimer = setTimeout(() => this.fail('VOICE_TIMEOUT'), 20000);
    this.socket.addEventListener('message', event => {
      try {
        if (typeof event.data !== 'string' || event.data.length > 2 * 1024 * 1024) throw new Error();
        const message = JSON.parse(event.data);
        this.receive(message, { voice, language });
      } catch { this.fail('VOICE_PROTOCOL'); }
    });
    this.socket.addEventListener('error', () => this.fail('VOICE_CONNECTION'));
    this.socket.addEventListener('close', () => {
      if (this.state !== 'closed') this.fail('VOICE_DISCONNECTED');
    });
    return opening;
  }
  send(frame) {
    if (this.state === 'closed' || this.socket?.readyState !== 1) throw new Error('Voice connection unavailable');
    this.socket.send(JSON.stringify(frame));
    if(frame.type==='input_audio_buffer.append')this.lastInputAt=this.idleNow();
    this.trace.sent(frame, this.turn);
  }
  receive(message, { voice, language }) {
    if (this.state === 'closed') return;
    this.trace.received(message, this.turn);
    switch (message.type) {
      case 'session.created':
        this.send({ type: 'session.update', session: {
          instructions: this.plan.instructions, tools: this.plan.tools, turn_detection: null,
          resumption: { enabled: true }, ...(voice ? { voice } : {}),
          audio: { input: { format: { type: 'audio/pcm', rate: 24000 }, transcription: { model: 'grok-transcribe', language_hint: language } },
            output: { format: { type: 'audio/pcm', rate: 24000 } } }
        } }); break;
      case 'session.updated':
        this.sessionConfigured=true;this.finishOpening();break;
      case 'conversation.created': {
        const id=message.conversation?.id;
        if(typeof id==='string'&&/^[\x21-\x7e]{1,256}$/.test(id))this.conversationId=id;
        if(this.expectedConversationId!==null&&this.conversationId!==this.expectedConversationId)this.fail('VOICE_RESUMPTION_FAILED');
        else this.finishOpening();break;
      }
      case 'response.created': {
        const id = message.response?.id, tag = message.response?.metadata?.bridge_turn;
        if(id&&(typeof id!=='string'||id.length>256)){this.fail('VOICE_PROTOCOL');break;}
        if (!id || this.cancelled.has(id) || this.state === 'recording' || (tag != null && Number(tag) !== this.turn)) {
          if (id) this.rememberResponse(id,true); break;
        }
        if(!this.rememberResponse(id))break;
        this.responseRequested = false; this.responseId = id; this.responseActive = true;
        this.responseHasAudio=false;this.responseHadTool=false;this.monitor.note(this.turn,'response');break;
      }
      case 'response.audio.delta': case 'response.output_audio.delta': {
        if (this.state === 'recording' || this.cancelled.has(message.response_id) || message.response_id !== this.responseId) break;
        const pcm = validatePcm(Buffer.from(message.delta, 'base64'));
        this.responseHasAudio ||= pcm.length>0;this.monitor.note(this.turn,'audio');
        if (this.firstAudioAt == null) { this.firstAudioAt = this.now(); this.emit('latency', { firstAudioMs: this.firstAudioAt - this.submittedAt }); }
        this.emit('audio', pcm); break;
      }
      case 'response.function_call_arguments.done':
        if (this.ownsResponse(message.response_id)) void this.handleTool(message).catch(() => this.fail('TOOL_DELIVERY_FAILED')); break;
      case 'response.done':
        if (message.response?.id === this.responseId) {
          this.responseActive = false;
          this.monitor.note(this.turn,'done');
          if(message.response?.status==='failed'){
            this.stopPlayback();
            this.emit('turnStatus',{turn:this.turn,code:'response_failed'});
          }else if(!this.responseHasAudio&&!this.responseHadTool&&!this.pendingTools.size&&!this.pendingResponse){
            this.emit('turnStatus',{turn:this.turn,code:'empty_response'});
          }
          this.emit('responseDone');
          if (this.pendingResponse) { this.pendingResponse = false; this.requestResponse(); }
        } break;
      case 'input_audio_buffer.committed':{
        const turn=this.inputTurns.shift();
        if(turn!=null&&typeof message.item_id==='string'&&message.item_id.length>0&&message.item_id.length<=256){
          if(this.inputItems.has(message.item_id))this.inputStateUncertain=true;
          this.inputItems.set(message.item_id,turn);
          this.inputPending.add(message.item_id);
          if(this.inputItems.size>128){
            const oldest=this.inputItems.keys().next().value;
            if(this.inputPending.delete(oldest))this.inputStateUncertain=true;
            this.inputItems.delete(oldest);
          }
        }else this.inputStateUncertain=true;
        break;
      }
      case 'conversation.item.input_audio_transcription.completed':{
        const turn=this.inputItems.get(message.item_id);if(turn==null)break;
        if(message.status===undefined||message.status==='completed')this.inputPending.delete(message.item_id);
        if(turn===this.turn&&this.state!=='recording')this.monitor.note(turn,'recognized');
        this.emit('transcript', { role: 'user', text: message.transcript ?? '', itemId: message.item_id,turn });break;
      }
      case 'conversation.item.input_audio_transcription.failed':{
        this.inputPending.delete(message.item_id);
        if(this.inputItems.get(message.item_id)!==this.turn||this.state==='recording')break;
        this.emit('turnStatus',{turn:this.turn,code:'unrecognized'});this.stopPlayback();
        if(this.state!=='closed')this.emit('state','ready');break;
      }
      case 'response.output_audio_transcript.done': case 'response.audio_transcript.done':
        if (this.ownsResponse(message.response_id)) this.emit('transcript', { role: 'assistant', text: message.transcript ?? '', itemId: message.item_id,turn:this.turn }); break;
      case 'error':
        this.idleExpiryCheck={signature:idleSessionError(message.error),settled:this.canRecoverIdle(),socketOpen:this.socket?.readyState===1};
        if(this.idleExpiryCheck.signature&&this.idleExpiryCheck.settled){
          // A listener must synchronously retire this socket before resuming.
          // An unclaimed expiry keeps the ordinary failure/cleanup behavior.
          this.emit('idleExpired');if(this.state==='closed')break;
        }
        this.fail('VOICE_SERVICE_ERROR');break;
    }
  }
  finishOpening() {
    if(this.state!=='connecting'||!this.sessionConfigured||
      (this.expectedConversationId!==null&&this.conversationId!==this.expectedConversationId))return;
    clearTimeout(this.openTimer);this.state='ready';this.openResolve();this.emit('ready');
  }
  rememberResponse(id,cancelled=false) {
    const history=cancelled?this.cancelled:this.responseIds;
    if(!history.has(id)&&history.size>=1024){this.fail('VOICE_HISTORY_LIMIT');return false;}
    history.add(id);return true;
  }
  canRecoverIdle() {
    const idle=this.idleNow()-this.lastInputAt;
    return this.state==='ready'&&this.socket?.readyState===1&&Number.isFinite(idle)&&idle>=840000&&
      !this.responseActive&&!this.responseRequested&&!this.pendingResponse&&!this.pendingTools.size&&
      !this.updates.length&&!this.unscopedAsync&&!this.inputTurns.length&&!this.inputPending.size&&!this.inputStateUncertain;
  }
  beginTurn({mood='neutral'}={}) {
    const instructions=companionTurnInstructions(this.plan?.instructions,mood);
    if (!['ready', 'recording'].includes(this.state)) throw new Error('Voice session not ready');
    this.stopPlayback();
    if(this.state==='closed')throw new Error('Voice session closed; reconnect before recording');
    this.turn++; this.inputBytes = 0; this.state = 'recording';this.responseInstructions=instructions;
    this.send({ type: 'input_audio_buffer.clear' }); this.emit('state', 'recording');
  }
  sendAudio(bytes) {
    if (this.state !== 'recording') return;
    validatePcm(bytes); this.inputBytes += bytes.length;
    if (this.inputBytes > 24000 * 2 * 60) { this.fail('RECORDING_LIMIT'); return; }
    this.send({ type: 'input_audio_buffer.append', audio: bytes.toString('base64') });
  }
  endTurn() {
    if (this.state !== 'recording') return;
    this.state = 'ready';
    if (this.inputBytes < 4800) {
      this.responseInstructions=undefined;
      this.send({ type: 'input_audio_buffer.clear' });this.emit('turnStatus',{turn:this.turn,code:'short_input'});this.emit('state', 'ready');
    }
    else {
      this.submittedAt = this.now(); this.firstAudioAt = null;
      this.inputTurns.push(this.turn);
      this.send({ type: 'input_audio_buffer.commit' }); this.requestResponse(); this.emit('state', 'waiting');
    }
    for (const update of this.updates.splice(0)) this.injectUpdate(update);
  }
  injectUpdate(update) {
    if (this.state === 'closed') return;
    if (this.state === 'recording') { this.updates.push(update); return; }
    // Native async updates have no response ID/turn correlation contract. Once
    // sent, a later BOOT must rebuild this session before accepting a new turn.
    this.unscopedAsync=true;
    this.send({ type: 'async_update.add', event_id: `task-${update.id}`, async_update: {
      topic: 'work_landed', payload: [{ message: { type: 'text', content: update.text }, timestampMs: this.now() }]
    } });
  }
  requestResponse() {
    if (this.responseActive || this.responseRequested || this.pendingTools.size) { this.pendingResponse = true; return; }
    this.monitor.start(this.turn);
    this.responseId=undefined;
    this.responseRequested = true;
    this.send({ type: 'response.create', response: { metadata: { bridge_turn: String(this.turn) },...(this.responseInstructions?{instructions:this.responseInstructions}:{}) } });
  }
  ownsResponse(id) { return Boolean(id) && id === this.responseId && !this.cancelled.has(id) && !['recording', 'closed'].includes(this.state); }
  stopPlayback() {
    this.responseInstructions=undefined;
    const ambiguous=(!!this.responseRequested&&!this.responseId)||!!this.unscopedAsync;
    this.monitor.clear(this.turn);
    this.pendingTools.clear(); this.pendingResponse = false; this.responseRequested = false;
    if (this.responseId&&!this.rememberResponse(this.responseId,true))return;
    if (this.responseActive) { this.send({ type: 'response.cancel' }); this.responseActive = false; }
    this.responseId = undefined; this.emit('clearAudio');
    if(ambiguous)this.fail('VOICE_TURN_UNCERTAIN');
  }
  async handleTool(message) {
    const id = message.call_id, turn = this.turn;
    if (!this.ownsResponse(message.response_id) || typeof id !== 'string' || this.toolCalls.has(id)) return;
    if(!id.length||id.length>256){this.fail('VOICE_PROTOCOL');return;}
    if(this.toolCalls.size>=1000){this.fail('VOICE_HISTORY_LIMIT');return;}
    this.toolCalls.add(id); this.pendingTools.add(id);this.responseHadTool=true;this.monitor.note(turn,'tool_started');
    let output;
    try {
      if (!this.plan.tools.some(tool => tool.name === message.name)) throw new Error();
      const input = JSON.parse(message.arguments);
      output = await this.onTool({ name: message.name, input, callId: id });
    } catch {
      if(!this.ownsResponse(message.response_id)||turn!==this.turn){this.pendingTools.delete(id);return;}
      output = { error: 'Tool failed; do not automatically retry an uncertain task.' }; this.emit('fault', { code: 'TOOL_FAILED' });
    }
    this.pendingTools.delete(id);
    if (this.state === 'closed' || this.state === 'recording' || this.cancelled.has(message.response_id) || turn !== this.turn) return;
    this.monitor.note(turn,'tool_completed');
    this.send({ type: 'conversation.item.create', item: { type: 'function_call_output', call_id: id, output: JSON.stringify(output) } });
    if (!this.pendingTools.size) { this.pendingResponse = false; this.requestResponse(); }
  }
  fail(code) {
    if (this.state === 'closed') return;
    this.openReject?.(new Error(code)); this.emit('fault', { code }); this.close();
  }
  close() {
    if (this.state === 'closed') return;
    this.state = 'closed';this.responseInstructions=undefined; clearTimeout(this.openTimer);this.monitor.close();this.inputTurns=[];this.inputItems.clear();this.inputPending.clear(); this.emit('clearAudio');
    this.openReject?.(new Error('Voice session closed')); this.socket?.close(); this.emit('closed');
  }
}
