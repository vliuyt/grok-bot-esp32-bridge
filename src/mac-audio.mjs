import { EventEmitter } from 'node:events';
import { spawn, execFileSync } from 'node:child_process';
import { createInterface } from 'node:readline';
import { mkdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { validatePcm } from './audio.mjs';

export const projectRoot = fileURLToPath(new URL('../', import.meta.url));
export function buildAudio() {
  mkdirSync(new URL('../.build/', import.meta.url), { recursive: true });
  execFileSync('swiftc', ['-module-cache-path', '.build/module-cache', 'native/MacAudio.swift',
    '-Xlinker', '-sectcreate', '-Xlinker', '__TEXT', '-Xlinker', '__info_plist', '-Xlinker', 'native/Info.plist', '-o', '.build/mac-audio'],
  { cwd: projectRoot, timeout: 120000, stdio: 'pipe' });
  execFileSync('/usr/bin/codesign', ['--force', '--sign', '-', '--identifier', 'io.github.grokpet.audio', '.build/mac-audio'],
    { cwd: projectRoot, timeout: 10000, stdio: 'pipe' });
}
export class MacAudio extends EventEmitter {
  constructor() { super(); this.queue = []; this.queuedBytes = 0; this.blocked = false; }
  async open({ input, output, playbackOnly = false } = {}) {
    const args = [...(input ? ['--input', String(input)] : []), ...(output ? ['--output', String(output)] : []), ...(playbackOnly ? ['--playback-only'] : [])];
    this.child = spawn(fileURLToPath(new URL('../.build/mac-audio', import.meta.url)), args, { stdio: ['pipe', 'pipe', 'pipe'] });
    this.watchPipe();
    this.child.stderr.resume();
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => { this.close(); reject(new Error('AUDIO_PERMISSION_TIMEOUT')); }, 60000);
      const fail = () => { clearTimeout(timeout); reject(new Error('AUDIO_HELPER_UNAVAILABLE')); this.pipeFailure(); };
      this.once('pipeFailed', fail);
      this.child.once('error', fail);
      this.child.once('exit', () => { if (!this.closing) fail(); });
      const lines = createInterface({ input: this.child.stdout });
      lines.on('line', line => {
        try {
          if (line.length > 300000) throw new Error();
          const event = JSON.parse(line);
          if (event.type === 'ready') { clearTimeout(timeout); resolve(); }
          else if (event.type === 'capture') this.emit('capture', { pcm: validatePcm(Buffer.from(event.pcm, 'base64')), turn: event.turn });
          else if (event.type === 'fault') { clearTimeout(timeout); reject(new Error(event.code)); this.emit('fault', { code: event.code }); }
          else this.emit(event.type, event);
        } catch { this.emit('fault', { code: 'AUDIO_PROTOCOL' }); }
      });
    });
  }
  command(data) {
    this.watchPipe();
    if (this.failed || !this.child?.stdin.writable || (this.closing && data.type !== 'quit')) return;
    const line = `${JSON.stringify(data)}\n`;
    if (this.queuedBytes + line.length > 2 * 1024 * 1024) { this.emit('fault', { code: 'AUDIO_PIPE_OVERFLOW' }); return; }
    this.queue.push({ line, type: data.type }); this.queuedBytes += line.length; this.flush();
  }
  flush() {
    if (this.failed || this.blocked || !this.child?.stdin.writable) return;
    while (this.queue.length) {
      const next = this.queue.shift(); this.queuedBytes -= next.line.length;
      if (!this.child.stdin.write(next.line)) {
        this.blocked = true;
        this.child.stdin.once('drain', () => { this.blocked = false; this.flush(); }); return;
      }
    }
    if (this.closing && !this.child.stdin.writableEnded) this.child.stdin.end();
  }
  watchPipe() {
    const pipe = this.child?.stdin;
    if (!pipe || pipe === this.watchedPipe) return;
    this.watchedPipe = pipe; pipe.on('error', () => this.pipeFailure());
  }
  pipeFailure() {
    if (this.failed) return;
    this.failed = true; this.queue = []; this.queuedBytes = 0; this.blocked = false;
    this.emit('pipeFailed');
    if (!this.closing) this.emit('fault', { code: 'AUDIO_HELPER_UNAVAILABLE' });
  }
  beginTurn(turn) { this.turn = turn; this.command({ type: 'record.start', turn }); }
  endTurn() {
    return new Promise((resolve, reject) => {
      const turn = this.turn;
      const cleanup = () => { clearTimeout(timer); this.off('recordingStopped', stopped); this.off('pipeFailed', failed); this.off('closing', failed); };
      const stopped = event => { if (event.turn === turn) { cleanup(); resolve(); } };
      const failed = () => { cleanup(); reject(new Error('AUDIO_STOP_FAILED')); };
      const timer = setTimeout(failed, 2000);
      this.on('recordingStopped', stopped); this.once('pipeFailed', failed); this.once('closing', failed);
      this.command({ type: 'record.stop', turn });
    });
  }
  play(pcm) { validatePcm(pcm); if (pcm.length) this.command({ type: 'play', pcm: pcm.toString('base64') }); }
  clear() {
    this.queue = this.queue.filter(item => item.type !== 'play');
    this.queuedBytes = this.queue.reduce((total, item) => total + item.line.length, 0);
    this.command({ type: 'clear' });
  }
  close() {
    if (this.closing) return;
    this.closing = true; this.emit('closing');
    this.queue = []; this.queuedBytes = 0; this.command({ type: 'quit' });
    const timer = setTimeout(() => this.child?.kill(), 2000); timer.unref();
    this.child?.once('exit', () => clearTimeout(timer));
  }
}
