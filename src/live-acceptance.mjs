// Explicit live integration test. Test speech is generated locally; it is not a product STT/TTS dependency.
import { mkdir, readFile, unlink } from 'node:fs/promises';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { setTimeout as delay } from 'node:timers/promises';
import { loadAuth } from './auth.mjs';
import { Backend } from './backend.mjs';
import { NativeBotSession } from './native-bot.mjs';
import { MacAudio, projectRoot } from './mac-audio.mjs';

const run = promisify(execFile);
const backend = new Backend(await loadAuth());
const bots = await backend.listBots(); const matches = bots.filter(bot => bot.name === (process.env.GROK_BOT_NAME ?? '小糖'));
if (matches.length !== 1) throw new Error('BOT_NOT_UNIQUE');
const session = new NativeBotSession({ backend, bot: matches[0] });
const audio = new MacAudio(); let audioBytes = 0, taskReturns = 0, toolCalls = 0, faults = [], transcript = [], stats = [];
session.on('audio', pcm => { audioBytes += pcm.length; audio.play(pcm); });
session.on('clearAudio', () => audio.clear());
session.on('transcript', event => transcript.push(event));
session.on('tool', event => { if (event.phase === 'started') toolCalls++; });
session.on('taskReturn', () => { taskReturns++; });
session.on('fault', event => { faults.push(event.code); });
session.on('latency', event => stats.push(Math.round(event.firstAudioMs)));
audio.on('fault', event => { faults.push(event.code); });
await mkdir(new URL('../.local/', import.meta.url), { recursive: true });
async function speech(text, number) {
  const prefix = `${projectRoot}.local/acceptance-${number}`;
  try {
    await run('/usr/bin/say', ['-v', 'Tingting', '-o', `${prefix}.aiff`, text], { timeout: 30000 });
    await run('ffmpeg', ['-v', 'error', '-y', '-i', `${prefix}.aiff`, '-f', 's16le', '-ac', '1', '-ar', '24000', `${prefix}.pcm`], { timeout: 30000 });
    return await readFile(`${prefix}.pcm`);
  } finally { await Promise.all([unlink(`${prefix}.aiff`).catch(() => {}), unlink(`${prefix}.pcm`).catch(() => {})]); }
}
async function speakTurn(text, number) {
  const samples = await speech(text, number); const before = audioBytes;
  let done = false; const ended = () => { done = true; }; session.once('responseDone', ended);
  session.beginTurn();
  for (let i = 0; i < samples.length; i += 4800) { session.sendAudio(samples.subarray(i, i + 4800)); await delay(100); }
  session.endTurn();
  const deadline = Date.now() + 60000;
  while ((!done || audioBytes === before) && Date.now() < deadline && !session.closed) await delay(100);
  session.off('responseDone', ended);
  const bytes = audioBytes - before;
  console.log(JSON.stringify({ turn: number, audioBytes: bytes, completed: done, faults: [...new Set(faults)] }));
  if (!done || !bytes) throw new Error('VOICE_TURN_FAILED');
  await delay(1500);
}
try {
  await audio.open({ playbackOnly: true, output: 93 }); await session.open();
  const eventCounts = {};
  session.voice.socket.addEventListener('message', event => {
    try { const { type } = JSON.parse(event.data); if (typeof type === 'string') eventCounts[type] = (eventCounts[type] ?? 0) + 1; } catch {}
  });
  console.log(JSON.stringify({ phase: 'native-ready', bot: '小糖' }));
  await speakTurn('你好小糖，这是一次中文语音连通测试。请记住，本次测试的水果是苹果。请只回复记住了。', 1);
  const previous = transcript.length;
  await speakTurn('我刚才告诉你的测试水果是什么？请只说出水果的名字。', 2);
  const contextPassed = transcript.slice(previous).some(event => event.role === 'assistant' && /苹果/.test(event.text));
  await speakTurn('我们继续测试，请用中文说一句简短的鼓励。', 3);
  // Invoke the same native task tool used by the App; do not rely on the voice model doing arithmetic itself.
  const result = await session.tool({ name: 'send_task', input: { request: '这是经用户授权的语音桥接验证。仅计算123乘以45，不使用外部应用，不发送任何消息。请通过本次语音通话渠道返回准确结果，简短说出数字。' }, callId: 'acceptance-task-123x45' });
  console.log(JSON.stringify({ taskAccepted: result?.relay === 'accepted' }));
  const beforeReturn = audioBytes, taskDeadline = Date.now() + 90000;
  while ((taskReturns === 0 || audioBytes === beforeReturn) && Date.now() < taskDeadline && !session.closed) await delay(250);
  await delay(2500);
  const taskSpoken = transcript.some(event => event.role === 'assistant' && /5535|五千五百三十五|五五三五/.test(event.text));
  console.log(JSON.stringify({ acceptance: { threeAudioTurns: true, contextPassed, taskReturned: taskReturns > 0,
    taskSpoken, audioAfterTask: audioBytes > beforeReturn, toolCalls, firstAudioMs: stats, faults: [...new Set(faults)] }, eventCounts }));
  if (!contextPassed || taskReturns === 0 || !taskSpoken || audioBytes <= beforeReturn || faults.length) throw new Error('ACCEPTANCE_FAILED');
} catch (error) { console.log(JSON.stringify({ failure: error.code ?? error.message, faults: [...new Set(faults)] })); process.exitCode = 1; }
finally { audio.close(); await session.close(); }
