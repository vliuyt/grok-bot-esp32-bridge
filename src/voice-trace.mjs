import {createHmac, randomBytes} from 'node:crypto';

const RECEIVED = new Set([
  'input_audio_buffer.cleared', 'input_audio_buffer.speech_started',
  'input_audio_buffer.speech_stopped', 'input_audio_buffer.committed',
  'conversation.item.input_audio_transcription.updated',
  'conversation.item.input_audio_transcription.completed',
  'conversation.item.input_audio_transcription.failed',
  'response.created', 'response.done', 'error'
]);
const SENT = new Set(['input_audio_buffer.clear', 'input_audio_buffer.commit', 'response.create', 'response.cancel']);
const STATUSES = new Set(['in_progress', 'completed', 'cancelled', 'failed']);
const ERROR_TYPES = new Set(['invalid_request_error', 'server_error', 'rate_limit_error','authentication_error']);
const ERROR_CODES = new Set([
  'input_audio_buffer_commit_empty', 'conversation_already_has_active_response',
  'response_cancel_not_active', 'session_expired', 'rate_limit_exceeded',
  'invalid_value', 'invalid_request_error', 'server_error',
  'internal_error','async_not_enabled','missing_tool_call_results'
]);
const ERROR_SIGNALS=[
  ['idle',/\b(?:idle|inactive|inactivity|silence)\b/i],
  ['timeout',/\btimeout\b|\btimed out\b|\bdeadline\b/i],
  ['auth',/\b(?:auth|unauthorized|forbidden|credential|token|secret|expired|expiration)\b/i],
  ['asr',/\b(?:asr|transcribe|transcription|recognition)\b/i],
  ['transport',/\b(?:connection|websocket|socket|grpc|upstream|keepalive|ping|pong|stream|transport)\b/i],
  ['service',/\b(?:internal|unavailable|overloaded|backend|service|server)\b/i],
  ['rate_limit',/\brate.?limit(?:ed)?\b|\bresource.?exhausted\b/i],
  ['session',/\b(?:session|conversation)\b/i],
  ['cancelled',/\b(?:cancelled|canceled|aborted|closed|closing|reset|disconnected)\b/i],
  ['model',/\b(?:model|deployment|inference)\b/i],
  ['limit',/\b(?:limit|maximum|capacity|duration|lifetime|exceeded)\b/i]
];

export function voiceTraceAliasKey(id, key) {
  if (typeof id !== 'string' || !id.length || id.length > 256) return null;
  return createHmac('sha256', key).update(id, 'utf16le').digest('hex');
}

// Only session-keyed digests are retained in the alias table; snapshots expose numbers.
export class VoiceTrace {
  #events = []; #aliases = new Map(); #dropped = 0; #aliasLimit = false; #aliasKey = randomBytes(32);
  #lastInputAt;
  constructor({now = Date.now} = {}) { this.now = now; this.startedAt = now(); this.#lastInputAt=this.startedAt; }
  #alias(id) {
    const key = voiceTraceAliasKey(id, this.#aliasKey);
    if (key === null) return null;
    if (this.#aliases.has(key)) return this.#aliases.get(key);
    if (this.#aliases.size >= 64) { this.#aliasLimit = true; return null; }
    const alias = this.#aliases.size + 1; this.#aliases.set(key, alias); return alias;
  }
  #add(event, turn, details = {}) {
    const elapsed = this.now() - this.startedAt;
    this.#events.push({ms: Number.isFinite(elapsed) ? Math.max(0, Math.round(elapsed)) : 0,
      turn: Number.isSafeInteger(turn) && turn >= 0 && turn <= 0xffffffff ? turn : null,
      event, ...details});
    if (this.#events.length > 96) { this.#events.shift(); this.#dropped++; }
  }
  sent(message, turn) {
    if(message?.type==='input_audio_buffer.append')this.#lastInputAt=this.now();
    if (SENT.has(message?.type)) this.#add(`client.${message.type}`, turn);
  }
  received(message, turn) {
    if (!RECEIVED.has(message?.type)) return;
    const details = {item: this.#alias(message.item_id), response: this.#alias(message.response_id ?? message.response?.id)};
    const status = message.status ?? message.response?.status;
    if (STATUSES.has(status)) details.status = status;
    if (message.type === 'error') {
      details.errorType = ERROR_TYPES.has(message.error?.type) ? message.error.type : 'other';
      details.errorCode = ERROR_CODES.has(message.error?.code) ? message.error.code : 'other';
      const text=message.error?.message,idle=this.now()-this.#lastInputAt;
      details.errorClass=typeof text==='string'&&text.length<=512&&/\bASR\b[\s\S]*\b(?:timed out|timeout)\b/i.test(text)?'asr_timeout':'other';
      details.inputIdleMs=Number.isFinite(idle)?Math.min(0xffffffff,Math.max(0,Math.round(idle))):0;
      details.messageSize=typeof text!=='string'?'missing':text.length<=512?'short':text.length<=16384?'long':'oversize';
      const code=message.error?.code;
      const diagnosticText=[typeof code==='string'&&code.length<=128?code:'',typeof text==='string'&&text.length<=16384?text:''].join(' ').replaceAll('_',' ');
      details.errorSignals=ERROR_SIGNALS.filter(([,pattern])=>pattern.test(diagnosticText)).map(([name])=>name);
    }
    this.#add(message.type, turn, details);
  }
  snapshot() {
    return {events: this.#events.map(event => ({...event,...(event.errorSignals?{errorSignals:[...event.errorSignals]}:{})})), droppedEvents: this.#dropped, aliasLimitReached: this.#aliasLimit};
  }
  toJSON() { return this.snapshot(); }
}
