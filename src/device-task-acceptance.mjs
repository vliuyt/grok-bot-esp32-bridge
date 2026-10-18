// Explicit, harmless native task test through the physical device speaker.
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { writeFile } from 'node:fs/promises';
import { setTimeout as delay } from 'node:timers/promises';
import { loadAuth } from './auth.mjs';
import { Backend } from './backend.mjs';
import { NativeBotSession } from './native-bot.mjs';
import { startDeviceServer } from './device-server.mjs';
import { deviceConfig } from './device-config.mjs';
import { projectRoot } from './mac-audio.mjs';
const reportUrl=new URL('../docs/'+(process.argv.includes('--pet')?'pet':'hardware')+'-task.json',import.meta.url);
const report={passed:false,status:'running'};
await writeFile(reportUrl,JSON.stringify(report));
const backend = new Backend(await loadAuth()), bots = await backend.listBots(), matches=bots.filter(bot=>bot.name===(process.env.GROK_BOT_NAME??'小糖'));
if(matches.length!==1) throw new Error('BOT_NOT_UNIQUE');
const config=await deviceConfig();
let session, available=false, returned=0, spoken=false, audioBytes=0, saved, serialWaiter, usbReady;
const bridge=await startDeviceServer({backend,bot:matches[0],host:config.host,port:config.port,key:Buffer.from(config.key,'hex'),sessionFactory:()=>{
  session=new NativeBotSession({backend,bot:matches[0]});available=false;
  session.on('taskChannel',event=>{available=event.available;});session.on('taskReturn',()=>{returned++;});
  session.on('transcript',event=>{if(event.role==='assistant' && /5535|五千五百三十五|五五三五/.test(event.text)) spoken=true;});
  session.on('audio',pcm=>{audioBytes+=pcm.length;});session.on('closed',event=>{saved=event.saved;});
  return session;
}});
const usb=spawn(`${projectRoot}.local/hardware-venv/bin/python`,['firmware/tools/serial_control.py'],{cwd:projectRoot,stdio:['pipe','pipe','ignore']});
const ready=new Promise((resolve,reject)=>{usbReady=resolve;usb.once('error',reject);});
createInterface({input:usb.stdout}).on('line',line=>{try{const e=JSON.parse(line);if(e.ready)usbReady();else if(e.id===1){serialWaiter?.resolve(e.result);serialWaiter=null;}}catch{serialWaiter?.reject(new Error('SERIAL_PROTOCOL'));}});
usb.on('exit',()=>serialWaiter?.reject(new Error('SERIAL_CLOSED')));usb.stdin.on('error',()=>serialWaiter?.reject(new Error('SERIAL_CLOSED')));
async function status(){return await new Promise((resolve,reject)=>{const timeout=setTimeout(()=>reject(new Error('SERIAL_TIMEOUT')),12000);serialWaiter={resolve:value=>{clearTimeout(timeout);resolve(value);},reject:error=>{clearTimeout(timeout);reject(error);}};usb.stdin.write(JSON.stringify({id:1,action:'status'})+'\n');});}
async function until(check,ms=90000){const deadline=Date.now()+ms;while(Date.now()<deadline){if(await check())return;await delay(100);}throw new Error('ACCEPTANCE_TIMEOUT');}
try {
  await writeFile(reportUrl,JSON.stringify(report));
  await Promise.race([ready,delay(10000).then(()=>{throw new Error('SERIAL_START_TIMEOUT');})]);
  await until(()=>session && !session.closed && session.voice.state==='ready' && available);
  const active=session, frames=(await status()).playedFrames;
  const outcome=await active.tool({name:'send_task',input:{request:'这是经用户授权的硬件语音验收。仅计算123乘以45，不使用任何外部应用，不发送消息。通过本次语音通话返回结果，简短说出数字。'},callId:'hardware-task-123x45'});
  if(outcome?.relay!=='accepted')throw new Error('TASK_NOT_ACCEPTED');
  await until(()=>returned>0 && spoken && audioBytes>0 && bridge.snapshot().state==='ready' && !active.closed);
  report.taskReturned=returned>0;report.nativeResultSpoken=spoken;report.devicePlayedFrames=(await status()).playedFrames-frames;
  report.audioBytes=audioBytes;report.passed=report.devicePlayedFrames>0;report.status=report.passed?'passed':'failed';
} catch(error){report.failure=error.code??error.message;report.status='failed';}
finally {
  usb.stdin.end();const timer=setTimeout(()=>usb.kill(),5000);timer.unref();usb.once('exit',()=>clearTimeout(timer));
  await bridge.close();report.callSaved=saved===true;if(saved!==true){report.passed=false;report.status='failed';report.failure='CALL_SAVE_FAILED';}
  await writeFile(reportUrl,JSON.stringify(report,null,2)+'\n');
}
if(!report.passed)process.exitCode=1;console.log(JSON.stringify(report));
