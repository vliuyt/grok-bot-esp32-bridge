// Explicit acoustic interruption/reconnect test. Only derived levels and timings persist.
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { writeFile } from 'node:fs/promises';
import { setTimeout as delay } from 'node:timers/promises';
import { MacAudio, projectRoot } from './mac-audio.mjs';

const base = 'http://127.0.0.1:8788';
const snapshot = async () => (await fetch(base + '/state', { signal: AbortSignal.timeout(5000) })).json();
async function control(action) {
  const page = await (await fetch(base + '/')).text(), token = page.match(/const controlToken = "([0-9a-f]+)"/)[1];
  const response = await fetch(base + '/command', { method: 'POST', headers: { origin: base, 'content-type': 'application/json', 'x-bridge-control': token }, body: JSON.stringify({ action }) });
  if (!response.ok) throw new Error('CONTROL_FAILED');
}
async function until(check, ms = 90000) {
  const deadline = Date.now() + ms;
  while (Date.now() < deadline) { const result = await check(); if (result) return result; await delay(100); }
  throw new Error('TEST_TIMEOUT');
}
function say(text) {
  return new Promise((resolve, reject) => {
    const pet=process.argv.includes('--pet');
    const child = pet
      ? spawn(`${projectRoot}.local/hardware-venv/bin/python`,['firmware/tools/fixture_audio.py','--output',process.env.GROK_BENCH_OUTPUT??(()=>{throw new Error('GROK_BENCH_OUTPUT_REQUIRED')})()],{cwd:projectRoot,stdio:['pipe','ignore','ignore']})
      : spawn('/usr/bin/say', ['-v','Tingting',...(process.env.GROK_BENCH_OUTPUT?['-a',process.env.GROK_BENCH_OUTPUT]:[]),'-f','-'], { stdio: ['pipe', 'ignore', 'ignore'] });
    const timeout = setTimeout(() => { child.kill(); reject(new Error('FIXTURE_TIMEOUT')); }, 30000);
    child.once('error', reject); child.once('exit', code => { clearTimeout(timeout); code === 0 ? resolve() : reject(new Error('FIXTURE_FAILED')); });
    child.stdin.on('error', () => {}); child.stdin.end(text);
  });
}
const usb = spawn(`${projectRoot}.local/hardware-venv/bin/python`, ['firmware/tools/serial_control.py'], { cwd: projectRoot, stdio: ['pipe', 'pipe', 'ignore'] });
let pending, usbReady, requestId = 0;
const ready = new Promise((resolve, reject) => { usbReady = resolve; usb.once('error', reject); });
const lines = createInterface({ input: usb.stdout });
lines.on('line', line => { try { const event = JSON.parse(line); if (event.ready) usbReady(); else { if (!pending || event.id !== pending.id) return; const waiter = pending; pending = null; if (event.ok === true) waiter?.resolve(event.result); else waiter?.reject(new Error('SERIAL_COMMAND_FAILED')); } } catch { pending?.reject(new Error('SERIAL_PROTOCOL')); pending = null; } });
usb.on('exit', () => { pending?.reject(new Error('SERIAL_CLOSED')); pending = null; });
usb.stdin.on('error', () => { pending?.reject(new Error('SERIAL_CLOSED')); pending = null; });
function serial(action) {
  if (pending) throw new Error('SERIAL_BUSY');
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => { pending = null; reject(new Error('SERIAL_TIMEOUT')); }, 12000);
    pending = { id: ++requestId, resolve: value => { clearTimeout(timer); if (action !== 'status' && value !== true) reject(new Error('PTT_FAILED')); else resolve(value); }, reject: error => { clearTimeout(timer); reject(error); } };
    usb.stdin.write(JSON.stringify({ id: requestId, action }) + '\n');
  });
}
async function fixture(text) { try { await serial('down'); await delay(100); await say(text); await delay(250); } finally { await serial('up'); } }
const audio = new MacAudio(), levels = [];
audio.on('capture', ({ pcm }) => {
  let energy = 0; for (let i = 0; i < pcm.length; i += 2) energy += pcm.readInt16LE(i) ** 2;
  levels.push({ at: Date.now(), db: 10 * Math.log10(energy / Math.max(1, pcm.length / 2) / 1073741824 + 1e-10) });
  if (levels.length > 1000) levels.shift();
});
let audioFault;
audio.on('fault', ({ code }) => { audioFault = code; });
const median = values => [...values].sort((a, b) => a - b)[Math.floor(values.length / 2)];
const reportUrl=new URL('../docs/'+(process.argv.includes('--pet')?'pet':'hardware')+'-interrupt.json',import.meta.url);
const report = { passed: false, status: 'running' };
await writeFile(reportUrl, JSON.stringify(report));
try {
  await Promise.race([ready, delay(10000).then(() => { throw new Error('SERIAL_START_TIMEOUT'); })]);
  await until(async () => (await snapshot()).state === 'ready');
  await audio.open({input:process.env.GROK_BENCH_INPUT}); audio.beginTurn(100); await delay(800);
  const quiet = median(levels.slice(-10).map(x => x.db));
  const before = await snapshot();
  await fixture('请连续讲一个两百字左右的小故事，直接开始讲，不要先解释。');
  if(process.argv.includes('--pet'))await until(async()=>(await snapshot()).recognizedInputs>before.recognizedInputs,10000);
  await until(async () => (await snapshot()).state === 'speaking'); await delay(1800);
  const speaking = median(levels.filter(x => x.at > Date.now() - 1000).map(x => x.db));
  report.quietDb=quiet;report.speakingDb=speaking;
  if (!Number.isFinite(quiet) || !Number.isFinite(speaking) || speaking - quiet < 8) throw new Error('ACOUSTIC_SIGNAL_TOO_WEAK');
  const threshold = Math.max(quiet + 6, speaking - 15);
  report.thresholdDb=threshold;
  await until(async () => (await snapshot()).state === 'speaking' && median(levels.filter(x => x.at > Date.now()-200).map(x=>x.db)) > threshold, 3000);
  const pressedAt = Date.now(); await serial('down');
  const pressFrames=(await serial('status')).playedFrames;
  const stopped = await until(() => {
    const after = levels.filter(x => x.at >= pressedAt);
    for (let i = 0; i < after.length; i++) {
      const window = after.filter(x => x.at >= after[i].at && x.at <= after[i].at + 250);
      if (window.length >= 3 && window.at(-1).at - window[0].at >= 200 && window.every(x => x.db < threshold)) return window[0].at;
    } return false;
  }, 5000);
  await delay(1600);
  report.afterPressLevels=levels.filter(x=>x.at>=pressedAt&&x.at<=Date.now()).map(x=>[x.at-pressedAt,Math.round(x.db*10)/10]);
  report.oldPlaybackFramesAdded=(await serial('status')).playedFrames-pressFrames;
  if (levels.some(x => x.at > stopped + 250 && x.db >= threshold)) throw new Error('OLD_AUDIO_RESUMED');
  const stableFrames = (await serial('status')).playedFrames; await delay(300);
  if ((await serial('status')).playedFrames !== stableFrames) throw new Error('OLD_AUDIO_WRITES_RESUMED');
  report.silenceObservedMs = Date.now() - stopped;
  report.acousticStopUpperMs = Math.max(0, stopped - pressedAt);
  report.measurement = 'external microphone level drop; includes microphone and command latency';
  await say('请只说一句你好。'); await delay(250); await serial('up');
  const after = await until(async () => { const s = await snapshot(); return s.state === 'ready' && s.turns >= before.turns + 2 && s.replies > before.replies ? s : false; });
  report.followupPassed = after.reconnects === before.reconnects && !after.fault;
  const turns = after.turns, reconnects = after.reconnects;
  if(process.argv.includes('--pet')&&after.recognizedInputs<before.recognizedInputs+2)throw new Error('FOLLOWUP_NOT_RECOGNIZED');
  await control('hangup'); await until(async () => (await snapshot()).state === 'disconnected');
  await control('connect'); const restored = await until(async () => { const s = await snapshot(); return s.state === 'ready' ? s : false; });
  report.reconnectPassed = restored.reconnects > reconnects && restored.turns === turns;
  const frames = (await serial('status')).playedFrames;
  await fixture('重连测试，请只说恢复正常。');
  const final=await until(async () => { const s = await snapshot(); return s.state === 'ready' && s.turns === turns + 1 && s.replies > restored.replies ? s : false; });
  if(process.argv.includes('--pet')&&final.recognizedInputs<=restored.recognizedInputs)throw new Error('RECONNECT_FIXTURE_NOT_RECOGNIZED');
  report.audioAfterReconnect = (await serial('status')).playedFrames > frames;
  report.passed = report.followupPassed && report.reconnectPassed && report.audioAfterReconnect && !audioFault;
  if (!report.passed) throw new Error('ACCEPTANCE_FAILED');
  report.status = 'passed';
} catch (error) { report.status = 'failed'; report.failure = error.code ?? error.message; process.exitCode = 1; }
finally {
  try { await serial('up'); } catch { report.passed = false; report.status = 'failed'; report.failure = 'PTT_RELEASE_FAILED'; await control('hangup').catch(() => {}); }
  audio.close(); usb.stdin.end(); const timer = setTimeout(() => usb.kill(), 5000); timer.unref(); usb.once('exit', () => clearTimeout(timer));
  await writeFile(reportUrl, JSON.stringify(report, null, 2) + '\n');
}

if (!report.passed) process.exitCode = 1;
console.log(JSON.stringify(report));
