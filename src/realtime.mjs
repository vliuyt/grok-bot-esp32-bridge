import { EventEmitter } from 'node:events';
import { validatePcm } from './audio.mjs';

export class RealtimeSession extends EventEmitter {
  constructor({ socketFactory = (url, protocols) => new WebSocket(url, protocols), now = Date.now } = {}) {
    super(); this.socketFactory = socketFactory; this.now = now; this.state = 'idle';
    this.turn = 0; this.cancelled = new Set(); this.toolCalls = new Set(); this.pendingTools = new Set(); this.updates = [];
  }
  async open({ credential, plan, onTool = async () => ({ error: 'Tool unavailable' }), voice = null, language = 'zh' }) {
    if (this.state !== 'idle') throw new Error('Session already opened');
    if (Number(credential.expiresAtUnixSeconds) * 1000 <= this.now() + 5000 || !Number.isFinite(Number(credential.expiresAtUnixSeconds))) throw new Error('Voice credential expired');
    const url = new URL(credential.websocketUrl);
    if (url.origin !== 'wss://api.x.ai' || url.username || url.password || url.pathname !== '/v1/realtime') throw new Error('Unexpected realtime origin');
    if (!credential.clientSecret || typeof plan.instructions !== 'string' || !Array.isArray(plan.tools)) throw new Error('Invalid native voice contract');
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
  }
  receive(message, { voice, language }) {
    if (this.state === 'closed') return;
    switch (message.type) {
      case 'session.created':
        this.send({ type: 'session.update', session: {
          instructions: this.plan.instructions, tools: this.plan.tools, turn_detection: null,
          resumption: { enabled: true }, ...(voice ? { voice } : {}),
          audio: { input: { format: { type: 'audio/pcm', rate: 24000 }, transcription: { model: 'grok-transcribe', language_hint: language } },
            output: { format: { type: 'audio/pcm', rate: 24000 } } }
        } }); break;
      case 'session.updated':
        if (this.state === 'connecting') { clearTimeout(this.openTimer); this.state = 'ready'; this.openResolve(); this.emit('ready'); } break;
      case 'conversation.created': this.conversationId = message.conversation?.id; break;
      case 'response.created': {
        const id = message.response?.id, tag = message.response?.metadata?.bridge_turn;
        if (!id || this.state === 'recording' || (tag != null && Number(tag) !== this.turn)) {
          if (id) this.cancelled.add(id); break;
        }
        this.responseRequested = false; this.responseId = id; this.responseActive = true; break;
      }
      case 'response.audio.delta': case 'response.output_audio.delta': {
        if (this.state === 'recording' || this.cancelled.has(message.response_id) || message.response_id !== this.responseId) break;
        const pcm = validatePcm(Buffer.from(message.delta, 'base64'));
        if (this.firstAudioAt == null) { this.firstAudioAt = this.now(); this.emit('latency', { firstAudioMs: this.firstAudioAt - this.submittedAt }); }
        this.emit('audio', pcm); break;
      }
      case 'response.function_call_arguments.done':
        if (this.ownsResponse(message.response_id)) void this.handleTool(message).catch(() => this.fail('TOOL_DELIVERY_FAILED')); break;
      case 'response.done':
        if (message.response?.id === this.responseId) {
          this.responseActive = false;
          if (message.response?.status === 'failed') this.emit('fault', { code: 'VOICE_RESPONSE_FAILED' });
          this.emit('responseDone');
          if (this.pendingResponse) { this.pendingResponse = false; this.requestResponse(); }
        } break;
      case 'conversation.item.input_audio_transcription.completed':
        this.emit('transcript', { role: 'user', text: message.transcript ?? '', itemId: message.item_id }); break;
      case 'response.output_audio_transcript.done': case 'response.audio_transcript.done':
        if (this.ownsResponse(message.response_id)) this.emit('transcript', { role: 'assistant', text: message.transcript ?? '', itemId: message.item_id }); break;
      case 'error': this.fail('VOICE_SERVICE_ERROR'); break;
    }
  }
  beginTurn() {
    if (!['ready', 'recording'].includes(this.state)) throw new Error('Voice session not ready');
    this.stopPlayback(); this.turn++; this.inputBytes = 0; this.state = 'recording';
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
    if (this.inputBytes < 4800) { this.send({ type: 'input_audio_buffer.clear' }); this.emit('state', 'ready'); }
    else {
      this.submittedAt = this.now(); this.firstAudioAt = null;
      this.send({ type: 'input_audio_buffer.commit' }); this.requestResponse(); this.emit('state', 'waiting');
    }
    for (const update of this.updates.splice(0)) this.injectUpdate(update);
  }
  injectUpdate(update) {
    if (this.state === 'closed') return;
    if (this.state === 'recording') { this.updates.push(update); return; }
    this.send({ type: 'async_update.add', event_id: `task-${update.id}`, async_update: {
      topic: 'work_landed', payload: [{ message: { type: 'text', content: update.text }, timestampMs: this.now() }]
    } });
  }
  requestResponse() {
    if (this.responseActive || this.responseRequested || this.pendingTools.size) { this.pendingResponse = true; return; }
    this.responseRequested = true;
    this.send({ type: 'response.create', response: { metadata: { bridge_turn: String(this.turn) } } });
  }
  ownsResponse(id) { return Boolean(id) && id === this.responseId && !this.cancelled.has(id) && !['recording', 'closed'].includes(this.state); }
  stopPlayback() {
    this.pendingTools.clear(); this.pendingResponse = false; this.responseRequested = false;
    if (this.responseId) this.cancelled.add(this.responseId);
    if (this.responseActive) { this.send({ type: 'response.cancel' }); this.responseActive = false; }
    this.responseId = undefined; this.emit('clearAudio');
  }
  async handleTool(message) {
    const id = message.call_id, turn = this.turn;
    if (!this.ownsResponse(message.response_id) || typeof id !== 'string' || this.toolCalls.has(id)) return;
    this.toolCalls.add(id); this.pendingTools.add(id);
    let output;
    try {
      if (!this.plan.tools.some(tool => tool.name === message.name)) throw new Error();
      const input = JSON.parse(message.arguments);
      output = await this.onTool({ name: message.name, input, callId: id });
    } catch { output = { error: 'Tool failed; do not automatically retry an uncertain task.' }; this.emit('fault', { code: 'TOOL_FAILED' }); }
    this.pendingTools.delete(id);
    if (this.state === 'closed' || this.state === 'recording' || this.cancelled.has(message.response_id) || turn !== this.turn) return;
    this.send({ type: 'conversation.item.create', item: { type: 'function_call_output', call_id: id, output: JSON.stringify(output) } });
    if (!this.pendingTools.size) { this.pendingResponse = false; this.requestResponse(); }
  }
  fail(code) {
    if (this.state === 'closed') return;
    this.openReject?.(new Error(code)); this.emit('fault', { code }); this.close();
  }
  close() {
    if (this.state === 'closed') return;
    this.state = 'closed'; clearTimeout(this.openTimer); this.emit('clearAudio');
    this.openReject?.(new Error('Voice session closed')); this.socket?.close(); this.emit('closed');
  }
}
