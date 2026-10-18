import { execFileSync } from 'node:child_process';
import { loadAuth } from './auth.mjs';
import { Backend } from './backend.mjs';
import { buildAudio, projectRoot } from './mac-audio.mjs';
import { VoiceController } from './controller.mjs';
import { startControlServer } from './control.mjs';
import { startDeviceServer } from './device-server.mjs';
import { deviceConfig,pairingConfig } from './device-config.mjs';
import { startDiscovery } from './discovery.mjs';

const args = process.argv.slice(2);
function option(name) { const index = args.indexOf(name); return index < 0 ? undefined : args[index + 1]; }
const mode = args[0] && !args[0].startsWith('-') ? args[0] : 'serve';
if (args.includes('--help') || args.includes('-h')) {
  console.log('npm start -- serve --bot 小糖 [--port 8788] [--input DEVICE_ID] [--output DEVICE_ID]\nCommands: doctor, bots, serve\nMac microphone and native Grok Bot voice; no transcripts or credentials are logged.');
} else {
  try {
    if (mode === 'doctor') {
      buildAudio();
      const devices = JSON.parse(execFileSync('.build/mac-audio', ['--devices'], { cwd: projectRoot, encoding: 'utf8' }));
      console.log(JSON.stringify({ platform: process.platform, node: process.version, audioDevices: devices.devices }));
    } else {
      const backend = new Backend(await loadAuth()); const bots = await backend.listBots();
      if (mode === 'bots') console.log(JSON.stringify(bots.map(({ id, name }) => ({ id, name }))));
      else if (mode === 'serve') await serve(backend, bots);
      else if (mode === 'device') await device(backend, bots);
      else throw new Error('UNKNOWN_COMMAND');
    }
  } catch (error) { console.error(`桥接未启动：${error.code ?? error.message}`); process.exitCode = 1; }
}

async function serve(backend, bots) {
  buildAudio();
  let bot = bots.find(item => item.name === (option('--bot') ?? '小糖'));
  if (!bot) throw new Error('BOT_NOT_FOUND');
  const controller = new VoiceController({ backend, bots, bot, audioOptions: { input: option('--input'), output: option('--output') } });
  const server = await startControlServer({ port: Number(option('--port') ?? 8788),
    snapshot: () => controller.snapshot(), command: (action, input) => controller.command(action, input) });
  console.log(`语音面板：http://127.0.0.1:${server.port}\n目标：${bot.name}。在面板中连接后按住说话。`);
  let stopping = false;
  const stop = async () => { if (stopping) return; stopping = true; await controller.hangup(); await server.close(); };
  process.once('SIGINT', () => { void stop(); }); process.once('SIGTERM', () => { void stop(); });
}

async function device(backend, bots) {
  const matches = bots.filter(bot => bot.name === (option('--bot') ?? '小糖'));
  if (matches.length !== 1) throw new Error('BOT_NOT_UNIQUE');
  const bot = matches[0], config = await deviceConfig();
  const bridge = await startDeviceServer({ backend, bot, key: Buffer.from(config.key, 'hex'), host:'0.0.0.0', port: config.port,bridgeId:config.bridgeId });
  const discovery=startDiscovery({port:config.port,bridgeId:config.bridgeId});
  let panel;
  try {
    panel = await startControlServer({ port: Number(option('--port') ?? 8788), setupConfig:()=>pairingConfig(config),
      snapshot: () => { const state = bridge.snapshot(); return { ...state, firstAudioMs: state.firstAudioMs.at(-1), device: true, botId: bot.id, bots: [{ id: bot.id, name: bot.name }] }; },
      command: async action => { if (action === 'hangup') bridge.pause(); else if (action === 'connect') bridge.resume(); } });
  } catch (error) { await discovery.close();await bridge.close(); throw error; }
  console.log(`设备语音桥接已启动，目标：${bot.name}。状态面板：http://127.0.0.1:${panel.port}`);
  let stopping = false;
  const stop = async () => { if (stopping) return; stopping = true; await discovery.close();await bridge.close(); await panel.close(); };
  process.once('SIGINT', () => void stop()); process.once('SIGTERM', () => void stop());
}
