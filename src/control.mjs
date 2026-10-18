import http from 'node:http';
import { setupPage } from './setup-page.mjs';
import { randomBytes, timingSafeEqual } from 'node:crypto';

export async function startControlServer({ command, snapshot, setupConfig, port = 0 }) {
  const token = randomBytes(32).toString('hex'); let queue = Promise.resolve();
  const server = http.createServer(async (request, response) => {
    const origin = `http://127.0.0.1:${server.address().port}`;
    const headers = { 'cache-control': 'no-store', 'x-frame-options': 'DENY', 'referrer-policy': 'no-referrer',
      'content-security-policy': "default-src 'none'; connect-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; frame-ancestors 'none'" };
    const reply = (status, data, type = 'application/json') => { response.writeHead(status, { ...headers, 'content-type': `${type}; charset=utf-8` }); response.end(type === 'application/json' ? JSON.stringify(data) : data); };
    if (request.headers.host !== `127.0.0.1:${server.address().port}`) { reply(403, { error: 'HOST_REFUSED' }); return; }
    if (request.method === 'GET' && request.url === '/') { reply(200, page(token,!!snapshot().device), 'text/html'); return; }
    if (request.method === 'GET' && request.url === '/setup') { reply(200, setupPage(token), 'text/html'); return; }
    if (request.method === 'GET' && request.url === '/state') { reply(200, snapshot()); return; }
    if (request.method !== 'POST' || !['/command','/setup-config'].includes(request.url)) { reply(404, {}); return; }
    const supplied = Buffer.from(request.headers['x-bridge-control'] ?? '');
    if (request.headers.origin !== origin || supplied.length !== token.length || !timingSafeEqual(supplied, Buffer.from(token))) { reply(403, { error: 'CONTROL_REFUSED' }); return; }
    try {
      if (request.url === '/setup-config') { if (!setupConfig) {reply(404, {});return;} reply(200,await setupConfig());return; }
      let body = '';
      for await (const chunk of request) { body += chunk; if (body.length > 4096) throw new Error(); }
      const input = JSON.parse(body);
      if (!['connect', 'press', 'release', 'hangup', 'select'].includes(input.action)) throw new Error();
      const result = queue.then(() => command(input.action, input)); queue = result.catch(() => {});
      await result; reply(200, { ok: true });
    } catch { reply(400, { error: 'COMMAND_FAILED' }); }
  });
  server.requestTimeout = 65000; server.headersTimeout = 10000;
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen(port, '127.0.0.1', resolve); });
  return { port: server.address().port, close: () => new Promise(resolve => server.close(resolve)) };
}

function page(token, device = false) {
  return `<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>小糖 · 语音桥接</title>
<style>*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;background:#10131b;color:#f1f1f7;font:16px -apple-system,BlinkMacSystemFont,sans-serif}main{width:min(520px,92vw);padding:38px;border:1px solid #333846;border-radius:30px;background:#191e2a;text-align:center}small{color:#a4adc1;letter-spacing:2px}h1{font-size:32px;margin-bottom:10px}p{color:#b6bfd4;line-height:1.7}select,button{font:inherit;border-radius:14px;border:1px solid #424a60;background:#262d3e;color:inherit;padding:12px 20px}select{width:100%;margin:20px 0}#talk{display:block;width:180px;height:180px;border-radius:50%;margin:28px auto;background:#6757cf;border:2px solid #a497f2;font-size:22px;touch-action:none;user-select:none}#talk:active{background:#a3527c;transform:scale(.98)}button:disabled{opacity:.4}#status{min-height:52px}#metric{font-size:13px;color:#8794b3}footer{margin-top:24px;font-size:13px;color:#8794b3}</style>
<main><small>GROK BOT · 原生语音</small><h1 id="name">小糖</h1><p>按住说话，松开后听她回答。</p><select id="bots" aria-label="选择 Bot"></select><button id="connect">连接</button> <button id="hangup" disabled>挂断</button><button id="talk" disabled>按住说话</button><p id="status" role="status">正在读取状态…</p><div id="metric"></div><footer>松开即结束录音 · 播放时按住可打断<br>关闭通话后会将记录保存到所选 Bot 的聊天中<br><a href="/setup" style="color:#f6a0ca">设备配网与连接</a></footer></main>
<script>const controlToken = "${token}";
const $=id=>document.getElementById(id);let chain=Promise.resolve(),held=false,listed='';
function command(action,extra={}){chain=chain.catch(()=>{}).then(async()=>{const r=await fetch('/command',{method:'POST',headers:{'content-type':'application/json','x-bridge-control':controlToken},body:JSON.stringify({action,...extra}),keepalive:true});if(!r.ok)throw Error('操作未完成');});return chain;}
$('connect').onclick=()=>command('connect');$('hangup').onclick=()=>command('hangup');$('bots').onchange=()=>command('select',{id:$('bots').value});
function release(){if(!held)return;held=false;command('release');}
$('talk').onpointerdown=e=>{if(e.button!==0||held)return;e.preventDefault();held=true;$('talk').setPointerCapture(e.pointerId);command('press');};
$('talk').onpointerup=release;$('talk').onpointercancel=release;window.onblur=release;window.addEventListener('pagehide',()=>{release();${device?'':"command('hangup');"}});
async function refresh(){try{const s=await(await fetch('/state')).json();$('name').textContent=s.bot;if(s.device){document.querySelector('main>p').textContent='按住设备的 BOOT 说话，松开后听回答。';$('talk').textContent='设备按键控制';}const names={disconnected:'尚未连接',connecting:'正在连接原生语音…',ready:'已连接，可以按住说话',recording:'正在听，松开后回答',finishing:'正在提交语音…',waiting:'正在等她回答…',speaking:'正在播放回答',failed:'连接未完成'};$('status').textContent=(names[s.state]||s.state)+(s.fault?' · '+s.fault:'')+(s.taskReturnAvailable===false?' · 任务结果通道正在恢复':'');$('connect').disabled=!['disconnected','failed'].includes(s.state);$('hangup').disabled=s.state==='disconnected';$('talk').disabled=s.device||!['ready','recording','waiting','speaking'].includes(s.state);$('bots').disabled=!['disconnected','failed'].includes(s.state);const key=JSON.stringify(s.bots);if(key!==listed){listed=key;$('bots').replaceChildren(...s.bots.map(b=>{const o=document.createElement('option');o.value=b.id;o.textContent=b.name;return o;}));}$('bots').value=s.botId;$('metric').textContent=s.firstAudioMs==null?'':'首段语音 '+(s.firstAudioMs/1000).toFixed(2)+' 秒';}catch{$('status').textContent='桥接程序已停止';$('talk').disabled=true;}setTimeout(refresh,500);}refresh();</script></html>`;
}
