import { validatePcm } from './audio.mjs';
export function encodeFrame(type, turn, payload = Buffer.alloc(0)) {
  if (!Buffer.isBuffer(payload) || payload.length > 32763 || ![2, 3].includes(type) || !Number.isInteger(turn) || turn < 0 || turn > 0xffffffff) throw new Error('FRAME_SIZE');
  const frame = Buffer.alloc(9 + payload.length); frame.writeUInt32BE(5 + payload.length); frame[4] = type; frame.writeUInt32BE(turn, 5); payload.copy(frame, 9); return frame;
}
export class FrameDecoder {
  constructor(onFrame) { this.onFrame = onFrame; this.buffer = Buffer.alloc(0); }
  push(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    while (this.buffer.length >= 4) {
      const length = this.buffer.readUInt32BE(); if (length < 5 || length > 32768) throw new Error('FRAME_SIZE');
      if (this.buffer.length < length + 4) return;
      const frame = this.buffer.subarray(4, length + 4); this.buffer = this.buffer.subarray(length + 4);
      if (![2, 3].includes(frame[0])) throw new Error('FRAME_TYPE');
      this.onFrame({ type: frame[0], turn: frame.readUInt32BE(1), payload: frame.subarray(5) });
    }
  }
}
// At most 400 ms is handed to the network. Device credits arrive only after playback.
export class PlaybackQueue {
  constructor(send) { this.send = send; this.reset(0); }
  reset(turn) {
    if (turn !== this.turn) this.available = 20;
    this.turn = turn; this.queue = []; this.bytes = 0; this.ending = false;
  }
  push(pcm) {
    validatePcm(pcm);
    if (this.bytes + pcm.length > 16000 * 2 * 180) throw new Error('PLAYBACK_OVERFLOW');
    for (let i = 0; i < pcm.length; i += 640) this.queue.push(Buffer.from(pcm.subarray(i, i + 640)));
    this.bytes += pcm.length; this.flush();
  }
  credit(turn, count) {
    if (turn !== this.turn) return;
    if (!Number.isInteger(count) || count < 1 || count > 20 || this.available + count > 20) throw new Error('INVALID_CREDIT');
    this.available += count; this.flush();
  }
  done() { this.ending = true; this.flush(); }
  flush() {
    while (this.available > 0 && this.queue.length) {
      const pcm = this.queue.shift(); this.bytes -= pcm.length; this.available--; this.send(3, this.turn, pcm);
    }
    if (!this.queue.length && this.ending) { this.ending = false; this.send(2, this.turn, Buffer.from('done')); }
  }
}
