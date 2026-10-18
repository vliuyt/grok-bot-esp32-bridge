import { MacAudio } from './mac-audio.mjs';
import { NativeBotSession } from './native-bot.mjs';

// Every callback belongs to one call object; late callbacks cannot affect a new call.
export class VoiceController {
  constructor({ backend, bots, bot, audioOptions = {}, endGraceMs = 5000,
    audioFactory = () => new MacAudio(), sessionFactory = bot => new NativeBotSession({ backend, bot }) }) {
    Object.assign(this, { bots, bot, audioOptions, endGraceMs, audioFactory, sessionFactory });
    this.state = 'disconnected'; this.fault = null;
  }
  snapshot() {
    return { state: this.state, bot: this.bot.name, botId: this.bot.id,
      bots: this.bots.map(({ id, name }) => ({ id, name })), fault: this.fault, firstAudioMs: this.firstAudioMs, taskReturnAvailable: this.taskReturnAvailable };
  }
  current(call) { return this.active === call; }
  async hangup(call = this.active) {
    if (!call || !this.current(call)) return;
    this.active = null; clearTimeout(call.recordingTimer); clearTimeout(call.endTimer);
    this.state = 'disconnected'; call.audio.close();
    const result = await call.session?.close();
    if (!this.active && result?.saved === false) this.fault = 'CALL_SAVE_FAILED';
  }
  async fail(call, code) {
    if (!this.current(call)) return;
    this.fault = /^[A-Z0-9_]+$/.test(code) ? code : 'CONNECTION_FAILED';
    await this.hangup(call);
    if (!this.active) this.state = 'failed';
  }
  async release(call = this.active) {
    if (!call || !this.current(call) || this.state !== 'recording') return;
    clearTimeout(call.recordingTimer); this.state = 'finishing';
    try {
      await call.audio.endTurn();
      if (!this.current(call)) return;
      this.state = 'waiting'; call.session.endTurn();
    } catch (error) { await this.fail(call, error.code ?? error.message); }
  }
  async connect() {
    if (!['disconnected', 'failed'].includes(this.state)) return;
    const call = { audio: this.audioFactory(), session: this.sessionFactory(this.bot) };
    this.active = call; this.state = 'connecting'; this.fault = null; this.firstAudioMs = undefined;
    const guard = callback => (...args) => { if (this.current(call)) callback(...args); };
    call.audio.on('fault', guard(({ code }) => { void this.fail(call, code); }));
    call.audio.on('capture', guard(event => {
      if (['recording', 'finishing'].includes(this.state) && event.turn === call.session.voice.turn) call.session.sendAudio(event.pcm);
    }));
    call.audio.on('drained', guard(() => { if (this.state === 'speaking') this.state = 'ready'; }));
    call.session.on('audio', guard(pcm => { call.audio.play(pcm); if (!['recording', 'finishing'].includes(this.state)) this.state = 'speaking'; }));
    call.session.on('clearAudio', guard(() => call.audio.clear()));
    call.session.on('state', guard(state => { if (this.state !== 'connecting') this.state = state; }));
    call.session.on('latency', guard(event => { this.firstAudioMs = Math.round(event.firstAudioMs); }));
    call.session.on('taskChannel', guard(event => { this.taskReturnAvailable = event.available; }));
    call.session.on('fault', guard(({ code }) => { if (code !== 'TASK_RETURN_UNAVAILABLE') void this.fail(call, code); }));
    call.session.on('closed', guard(() => { void this.hangup(call); }));
    call.session.on('endRequested', guard(() => {
      clearTimeout(call.endTimer); call.endTimer = setTimeout(() => void this.hangup(call), this.endGraceMs); call.endTimer.unref();
    }));
    try {
      await call.audio.open(this.audioOptions);
      if (!this.current(call)) return;
      await call.session.open();
      if (this.current(call)) this.state = 'ready';
    } catch (error) { await this.fail(call, error.code ?? error.message); throw new Error('CONNECT_FAILED'); }
  }
  async command(action, input = {}) {
    if (action === 'connect') await this.connect();
    else if (action === 'hangup') await this.hangup();
    else if (action === 'release') await this.release();
    else if (action === 'select') {
      if (!['disconnected', 'failed'].includes(this.state)) throw new Error('CALL_ACTIVE');
      const next = this.bots.find(item => item.id === input.id); if (!next) throw new Error('BOT_NOT_FOUND'); this.bot = next;
    } else if (action === 'press' && ['ready', 'waiting', 'speaking'].includes(this.state)) {
      const call = this.active; call.session.beginTurn(); call.audio.beginTurn(call.session.voice.turn); this.state = 'recording';
      call.recordingTimer = setTimeout(() => void this.release(call), 55000); call.recordingTimer.unref();
    }
  }
}
