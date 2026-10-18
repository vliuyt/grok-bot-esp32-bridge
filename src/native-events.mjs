import { randomUUID } from 'node:crypto';
export class NativeEventDecoder {
  constructor(onEvent) { this.onEvent = onEvent; this.decoder = new TextDecoder(); this.pending = ''; this.data = []; this.size = 0; }
  push(bytes) {
    this.pending += this.decoder.decode(bytes, { stream: true });
    if (this.pending.length + this.size > 2 * 1024 * 1024) throw new Error('EVENT_SIZE');
    let end;
    while ((end = this.pending.indexOf('\n')) >= 0) {
      const line = this.pending.slice(0, end).replace(/\r$/, ''); this.pending = this.pending.slice(end + 1);
      if (!line) {
        if (this.data.length) this.onEvent(JSON.parse(this.data.join('\n')));
        this.data = []; this.size = 0;
      } else if (line.startsWith('data:')) {
        const value = line.slice(5).trimStart(); this.size += value.length;
        if (this.size > 2 * 1024 * 1024) throw new Error('EVENT_SIZE');
        this.data.push(value);
      }
    }
  }
}
export function taskUpdate(event, { agentId, callId, seen }) {
  const payload = event?.payload;
  if (event?.channel !== 'voice-call-nudge' || payload?.agentId !== agentId || payload.callId !== callId ||
      payload.kind !== 'outcome' || typeof payload.update !== 'string' || !payload.update.trim() || payload.update.length > 64000) return null;
  const id = typeof event.eventId === 'string' && event.eventId.length > 0 && event.eventId.length <= 256 ? `nudge-${event.eventId}` : `nudge-${randomUUID()}`;
  if (seen.has(id)) return null;
  if (seen.size > 4096) throw new Error('EVENT_HISTORY_LIMIT');
  seen.add(id); return { id, text: payload.update.trim() };
}
