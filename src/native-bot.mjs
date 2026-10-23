import { EventEmitter } from 'node:events';
import { randomUUID } from 'node:crypto';
import { RealtimeSession } from './realtime.mjs';
import { companionPlan } from './companion-style.mjs';
import { NativeEventDecoder, taskUpdate } from './native-events.mjs';

export function voiceUpdates(entries, callId, seen, {markSeen=true}={}) {
  const updates = [], batch=new Set(seen);
  for (const entry of entries) {
    if (typeof entry?.id!=='string'||!entry.id||entry.id.length>256||batch.has(entry.id)||entry.kind !== 'send-message' || entry.message?.type !== 'text' ||
        entry.message.channel !== `voice:${callId}` || typeof entry.message.content !== 'string'||entry.message.content.length>64000) continue;
    if(!entry.message.content.trim())continue;
    batch.add(entry.id);if(markSeen)seen.add(entry.id);
    updates.push({ id: entry.id, text: entry.message.content.trim() });
  }
  return updates;
}

export class NativeBotSession extends EventEmitter {
  constructor({ backend, bot, voiceFactory = () => new RealtimeSession(), voice = voiceFactory(), canRecoverIdle = () => false, pollMs = 1000, replyStyle = null }) {
    super(); this.backend = backend; this.bot = bot; this.voice = voice; this.pollMs = pollMs; this.replyStyle = replyStyle;
    this.voiceFactory=voiceFactory;this.canRecoverIdle=canRecoverIdle;this.voiceEpoch=0;this.recovering=false;
    this.callId = randomUUID(); this.closed = false; this.seen = new Set(); this.turns = [];
    this.nudgeSeen=new Set();this.historyAbort=new AbortController();
    this.toolCalls = []; this.nudges = []; this.startedAt = Date.now();
  }
  toJSON() { return { bot: this.bot.name, closed: this.closed }; }
  async open() {
    const [plan, credential] = await Promise.all([
      this.backend.rpc('VoiceCallHarnessSession', { agentId: this.bot.id, agentName: this.bot.name, spokenLanguage: 'Chinese' }),
      this.backend.rpc('MintSandVoiceCallSecret', {})
    ]);
    this.assertOpen();
    this.model = credential.model;
    this.plan=this.replyStyle==='companion'?companionPlan(plan):plan;
    const {active}=this.bindVoice(this.voice);
    await this.voice.open({ credential, plan:this.plan, onTool:args=>this.tool(args,active) });
    this.assertOpen();
    await this.backend.gatewayCall('setVoiceCallPresence', { id: this.bot.id, callId: this.callId, isOnTheLine: true, acceptsOverheard: false });
    if (this.closed) {
      await this.backend.gatewayCall('setVoiceCallPresence', { id: this.bot.id, callId: this.callId, isOnTheLine: false, acceptsOverheard: false });
      this.assertOpen();
    }
    if (this.backend.openEvents) {
      this.eventsAbort = new AbortController(); this.eventsLoop = this.listenEvents();
    }
    this.schedulePoll(0);
    this.emit('state', 'ready');
  }
  bindVoice(voice){
    const epoch=++this.voiceEpoch;
    const current=()=>!this.closed&&this.voice===voice&&this.voiceEpoch===epoch;
    const active=()=>current()&&!this.recovering;
    voice.on('transcript', ({ role, text, itemId,turn }) => {
      if (!active()) return;
      const id = itemId ? `${role}:${itemId}` : randomUUID();
      const previous = this.turns.find(turn => turn.id === id);
      if (previous) previous.text = text;
      else if (this.turns.length < 1000) this.turns.push({ id, speaker: role, text, atMs: Date.now() });
      this.emit('transcript', { role, text, itemId, turn: turn??voice.turn });
    });
    for (const name of ['audio', 'clearAudio', 'state', 'latency', 'responseDone','turnStatus']) voice.on(name,event=>{if(active())this.emit(name,event);});
    voice.on('fault',fault=>{if(active())this.emit('fault',fault);});
    voice.on('closed',()=>{if(active())void this.close('disconnected');});
    voice.on('idleExpired',()=>{if(active())this.startVoiceRecovery(voice);});
    return {current,active};
  }
  startVoiceRecovery(previous){
    const conversationId=previous.conversationId;
    if(this.closed||this.recovering||typeof conversationId!=='string'||!conversationId.length||conversationId.length>256)return;
    try{if(!this.canRecoverIdle())return;}catch{return;}
    // Retire old callbacks before notifying the device or closing its socket.
    this.recovering=true;this.voiceEpoch++;
    const controller=this.voiceRecoveryAbort=new AbortController(),started=performance.now();
    this.resumeConversationId=conversationId;
    this.emit('voiceRecovery',{phase:'started',trace:previous.trace?.snapshot()??null});
    previous.close();
    if(this.closed)return;
    this.recoveryPromise=this.recoverVoice(previous,conversationId,controller,started).catch(error=>{
      if(this.closed||this.voiceRecoveryAbort!==controller)return;
      this.recovering=false;this.emit('fault',{code:error?.code==='AUTH_REQUIRED'?'AUTH_REQUIRED':'VOICE_RECOVERY_FAILED'});void this.close('disconnected');
    });
  }
  async recoverVoice(previous,conversationId,controller,started){
    const credential=await this.backend.rpc('MintSandVoiceCallSecret',{},controller.signal);
    if(this.closed||controller.signal.aborted||this.voiceRecoveryAbort!==controller)return;
    const cancelled=new Set([...(previous.cancelled??[]),...(previous.responseIds??[])]);
    if(cancelled.size>1024)throw new Error('Voice response history limit');
    const voice=this.voiceFactory();
    if(voice===previous||voice.state==='closed')throw new Error('Fresh voice session required');
    this.voice=voice;
    // Correlation belongs to this native call, including events replayed on a new socket.
    voice.turn=previous.turn;
    if(voice.cancelled instanceof Set)voice.cancelled=cancelled;
    if(voice.responseIds instanceof Set)voice.responseIds=new Set(previous.responseIds??[]);
    if(voice.toolCalls instanceof Set)voice.toolCalls=new Set(previous.toolCalls??[]);
    const {current,active}=this.bindVoice(voice);
    await voice.open({credential,plan:this.plan,conversationId,onTool:args=>this.tool(args,active)});
    if(!current()||controller.signal.aborted)return;
    if(voice.state==='closed')throw new Error('Replacement voice closed');
    this.model=credential.model;this.recovering=false;this.voiceRecoveryAbort=undefined;
    this.emit('voiceRecovery',{phase:'ready',elapsedMs:Math.max(0,Math.round(performance.now()-started))});
    this.schedulePoll(0);
  }
  assertOpen() { if (this.closed) throw new Error('Voice session closed'); }
  beginTurn({mood='neutral'}={}) { if(this.recovering)throw new Error('Voice session reconnecting');this.voice.beginTurn({mood:this.replyStyle==='companion'?mood:'neutral'}); }
  sendAudio(bytes) { if(!this.recovering)this.voice.sendAudio(bytes); }
  endTurn() { if(!this.recovering)this.voice.endTurn(); }
  stopPlayback() { if(!this.recovering)this.voice.stopPlayback(); }
  async tool({ name, input, callId },isCurrent=()=>!this.closed&&!this.recovering) {
    if (!isCurrent()) throw new Error('Voice session closed or stale');
    if(this.toolCalls.length>=1000||this.toolCalls.some(call=>call.id===callId))throw new Error('Duplicate or excessive native tool call');
    const call = { id: callId, name, argumentsJson: JSON.stringify(input), atMs: Date.now(), result: null };
    this.toolCalls.push(call);
    if(name==='send_task'){this.taskWatchUntil=Date.now()+120000;this.schedulePoll(0);}
    this.emit('tool', { name, phase: 'started' });
    if (name === 'end_the_call') { this.emit('endRequested'); return {}; }
    const reply = await this.backend.rpc('VoiceCallHarnessTool', {
      agentId: this.bot.id, callId: this.callId, name, input,
      line: { theyJustTalked: true, newsOwed: false, receiptOwed: false, alreadySpokeThisTurn: false }
    });
    if(!isCurrent())throw new Error('Voice session closed or stale');
    if (![1, 'SERVED', 'VOICE_CALL_HARNESS_TOOL_OUTCOME_KIND_SERVED'].includes(reply.kind)) throw new Error('Native tool refused');
    call.result = { json: JSON.stringify(reply.output), atMs: Date.now() };
    if(name==='send_task')this.schedulePoll(0);
    this.emit('tool', { name, phase: 'completed' });
    return reply.output;
  }
  schedulePoll(ms){
    if(this.closed)return;
    if(this.pollPromise){this.pollAgain=true;return;}
    const wait=Math.max(ms,(this.historyNotBefore??0)-performance.now(),0);
    clearTimeout(this.timer);this.timer=setTimeout(()=>void this.poll(),wait);this.timer.unref();
  }
  taskChannel(status){
    const available=!!this.historyAvailable;
    this.emit('taskChannel',{available,status:available?null:status??null});
  }
  async poll(){
    if(this.closed)return;
    if(this.pollPromise){this.pollAgain=true;return this.pollPromise;}
    clearTimeout(this.timer);
    this.pollPromise=this.readHistory();
    try{await this.pollPromise;}
    finally{
      this.pollPromise=null;
      const active=Date.now()<(this.taskWatchUntil??0);
      const base=this.backend.openEvents&&!active?Math.max(30000,this.pollMs):this.pollMs;
      const again=this.pollAgain;this.pollAgain=false;
      this.schedulePoll(again?0:Math.min(30000,base*2**(this.pollFailures??0)));
    }
  }
  async readHistory() {
    if (this.closed||this.recovering) return;
    try {
      const tail = await this.backend.gatewayCall('getAgentTranscriptTail', { id: this.bot.id, limit: 200 },this.historyAbort.signal);
      if (this.closed||this.recovering) return;
      this.pollFailures = 0;this.historyNotBefore=0;this.historyAvailable=true;this.taskChannel();
      for (const update of voiceUpdates(tail.entries ?? [], this.callId, this.seen,{markSeen:false})) {
        if(this.closed||this.recovering)return;
        if(this.seen.size>=4096)throw new Error('EVENT_HISTORY_LIMIT');
        this.voice.injectUpdate?.(update);
        this.seen.add(update.id);this.emit('taskReturn', { received: true });
      }
    } catch (error) {
      if (!this.closed) {
        this.pollFailures = Math.min((this.pollFailures ?? 0) + 1, 4);
        this.historyNotBefore=performance.now()+Math.min(30000,this.pollMs*2**this.pollFailures);
        this.historyAvailable=false;this.taskChannel(Number.isInteger(error.status) ? error.status : null);
        this.emit('fault', { code: 'TASK_RETURN_UNAVAILABLE' });
      }
    }
  }
  async listenEvents() {
    let failures = 0;
    while (!this.closed) {
      const attempt = new AbortController(); let idle, openedAt;
      try {
        const body = await this.backend.openEvents(AbortSignal.any([this.eventsAbort.signal, attempt.signal]));
        if (this.closed) { await body.cancel?.(); return; }
        this.schedulePoll(0);openedAt = Date.now();
        const decoder = new NativeEventDecoder(event => {
          const update = taskUpdate(event, { agentId: this.bot.id, callId: this.callId, seen: this.nudgeSeen });
          // Persisted voice-channel entries provide a stable ID across both transports.
          if (update && !this.closed) {this.taskWatchUntil=Date.now()+120000;this.schedulePoll(0);}
        });
        idle = setTimeout(() => attempt.abort(), 65000);
        for await (const bytes of body) {
          clearTimeout(idle); idle = setTimeout(() => attempt.abort(), 65000);
          if (this.closed) break; decoder.push(bytes);
        }
        if (!this.closed) throw new Error('EVENT_STREAM_ENDED');
      } catch (error) {
        if (!this.closed) {
          this.schedulePoll(0);
          if(!this.historyAvailable){this.taskChannel(Number.isInteger(error.status) ? error.status : null);this.emit('fault', { code: 'TASK_RETURN_UNAVAILABLE' });}
        }
      } finally { clearTimeout(idle); attempt.abort(); if (openedAt && Date.now() - openedAt > 5000) failures = 0; }
      if (!this.closed) {
        failures = Math.min(failures + 1, 4);
        await new Promise(resolve => { this.retryResolve = resolve; this.eventRetry = setTimeout(resolve, Math.min(10000, 1000 * 2 ** failures)); });
      }
    }
  }
  async close(ending = 'hung-up') {
    if (this.closed) return this.closing;
    this.voiceRecoveryAbort?.abort();this.recovering=false;
    this.closed = true; clearTimeout(this.timer); clearTimeout(this.eventRetry);this.historyAbort.abort(); this.eventsAbort?.abort(); this.retryResolve?.();
    this.closing = this.finish(ending); return this.closing;
  }
  async finish(ending) {
    this.voice.close(); const endedAtMs = Date.now();
    await this.eventsLoop;
    await this.pollPromise;
    const errors = [];
    for (const [method, body] of [
      ['setVoiceCallPresence', { id: this.bot.id, callId: this.callId, isOnTheLine: false, acceptsOverheard: false }],
      ...(this.turns.length || this.toolCalls.length ? [['recordVoiceCall', { record: {
        callId: this.callId, agentId: this.bot.id, model: '', realtimeConversationId: this.voice.conversationId ?? this.resumeConversationId ?? null,
        startedAtMs: this.startedAt, endedAtMs, durationMs: endedAtMs - this.startedAt,
        ending, turns: this.turns, nudges: this.nudges, toolCalls: this.toolCalls, events: [], overheard: []
      } }]] : [])
    ]) {
      try { await this.backend.gatewayCall(method, body); } catch (error) { errors.push(error.code ?? 'CALL_SAVE_FAILED'); }
    }
    this.emit('closed', { saved: errors.length === 0 });
    for (const code of errors) this.emit('fault', { code });
    return { saved: errors.length === 0, errors };
  }
}
