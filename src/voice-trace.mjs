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
const ERROR_TYPES = new Set(['invalid_request_error', 'server_error', 'rate_limit_error']);
const ERROR_CODES = new Set([
  'input_audio_buffer_commit_empty', 'conversation_already_has_active_response',
  'response_cancel_not_active', 'session_expired', 'rate_limit_exceeded',
  'invalid_value', 'invalid_request_error', 'server_error'
]);

export function voiceTraceAliasKey(id, key) {
  if (typeof id !== 'string' || !id.length || id.length > 256) return null;
  return createHmac('sha256', key).update(id, 'utf16le').digest('hex');
}

// Only session-keyed digests are retained in the alias table; snapshots expose numbers.
export class VoiceTrace {
  #events = []; #aliases = new Map(); #dropped = 0; #aliasLimit = false; #aliasKey = randomBytes(32);
  constructor({now = Date.now} = {}) { this.now = now; this.startedAt = now(); }
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
    }
    this.#add(message.type, turn, details);
  }
  snapshot() {
    return {events: this.#events.map(event => ({...event})), droppedEvents: this.#dropped, aliasLimitReached: this.#aliasLimit};
  }
  toJSON() { return this.snapshot(); }
}
