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
  constructor({ backend, bot, voice = new RealtimeSession(), pollMs = 1000, replyStyle = null }) {
    super(); this.backend = backend; this.bot = bot; this.voice = voice; this.pollMs = pollMs; this.replyStyle = replyStyle;
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
    this.voice.on('transcript', ({ role, text, itemId,turn }) => {
      if (this.closed) return;
      const id = itemId ? `${role}:${itemId}` : randomUUID();
      const previous = this.turns.find(turn => turn.id === id);
      if (previous) previous.text = text;
      else if (this.turns.length < 1000) this.turns.push({ id, speaker: role, text, atMs: Date.now() });
      this.emit('transcript', { role, text, itemId, turn: turn??this.voice.turn });
    });
    for (const name of ['audio', 'clearAudio', 'state', 'latency', 'responseDone','turnStatus']) this.voice.on(name, event => this.emit(name, event));
    this.voice.on('fault', fault => this.emit('fault', fault));
    this.voice.on('closed', () => { void this.close('disconnected'); });
    await this.voice.open({ credential, plan: this.replyStyle==='companion'?companionPlan(plan):plan, onTool: args => this.tool(args) });
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
  assertOpen() { if (this.closed) throw new Error('Voice session closed'); }
  beginTurn() { this.voice.beginTurn(); }
  sendAudio(bytes) { this.voice.sendAudio(bytes); }
  endTurn() { this.voice.endTurn(); }
  stopPlayback() { this.voice.stopPlayback(); }
  async tool({ name, input, callId }) {
    if (this.closed) throw new Error('Voice session closed');
    const call = { id: callId, name, argumentsJson: JSON.stringify(input), atMs: Date.now(), result: null };
    this.toolCalls.push(call);
    if(name==='send_task'){this.taskWatchUntil=Date.now()+120000;this.schedulePoll(0);}
    this.emit('tool', { name, phase: 'started' });
    if (name === 'end_the_call') { this.emit('endRequested'); return {}; }
    const reply = await this.backend.rpc('VoiceCallHarnessTool', {
      agentId: this.bot.id, callId: this.callId, name, input,
      line: { theyJustTalked: true, newsOwed: false, receiptOwed: false, alreadySpokeThisTurn: false }
    });
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
    if (this.closed) return;
    try {
      const tail = await this.backend.gatewayCall('getAgentTranscriptTail', { id: this.bot.id, limit: 200 },this.historyAbort.signal);
      if (this.closed) return;
      this.pollFailures = 0;this.historyNotBefore=0;this.historyAvailable=true;this.taskChannel();
      for (const update of voiceUpdates(tail.entries ?? [], this.callId, this.seen,{markSeen:false})) {
        if(this.closed)return;
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
        callId: this.callId, agentId: this.bot.id, model: '', realtimeConversationId: this.voice.conversationId ?? null,
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
