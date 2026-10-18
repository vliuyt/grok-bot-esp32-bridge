// Explicit harmless native task acceptance. Audio is counted in memory and never played.
import {writeFile} from 'node:fs/promises';
import {setTimeout as delay} from 'node:timers/promises';
import {loadAuth} from './auth.mjs';
import {Backend} from './backend.mjs';
import {NativeBotSession} from './native-bot.mjs';

const historyOnly=process.argv.includes('--history-only');
const report={passed:false,status:'running',phase:'login',playback:'disabled',deviceSpeakerVerified:false,eventTransport:historyOnly?'blocked-in-test':'normal',taskSubmissions:0};
const output=new URL('../docs/'+(historyOnly?'pet-history-task':'pet-silent-task')+'.json',import.meta.url);
let session,available=false,returned=0,result=false,audioBytes=0;
const save=()=>writeFile(output,JSON.stringify(report,null,2)+'\n');
async function until(check){
  const deadline=Date.now()+90000;
  while(Date.now()<deadline){if(check())return;await delay(100);}
  throw new Error('SILENT_TASK_TIMEOUT');
}
await save();
try{
  const backend=new Backend(await loadAuth());
  if(historyOnly)backend.openEvents=async()=>{throw new Error('TEST_EVENT_STREAM_UNAVAILABLE');};
  const matches=(await backend.listBots()).filter(bot=>bot.name===(process.env.GROK_BOT_NAME??'小糖'));
  if(matches.length!==1)throw new Error('BOT_NOT_UNIQUE');
  session=new NativeBotSession({backend,bot:matches[0]});
  session.on('taskChannel',event=>{available=event.available;});
  session.on('taskReturn',()=>{returned++;});
  session.on('transcript',event=>{if(event.role==='assistant'&&/5535|五千五百三十五|五五三五/.test(event.text))result=true;});
  session.on('audio',pcm=>{audioBytes+=pcm.length;});
  report.phase='opening';await session.open();report.phase='task-channel';await save();await until(()=>available&&!session.closed);
  report.nativeEvents={};
  session.voice.socket?.addEventListener('message',event=>{
    try{const message=JSON.parse(event.data);if(typeof message.type==='string'&&/^[a-z_.]{1,100}$/.test(message.type))report.nativeEvents[message.type]=(report.nativeEvents[message.type]??0)+1;}catch{}
  });
  report.phase='submitting';await save();
  report.taskSubmissions++;
  const reply=await session.tool({name:'send_task',input:{request:'这是经用户授权的静音验收。仅计算123乘以45，不使用外部应用，不发送消息。通过本次语音通话返回结果，简短说出数字。'},callId:'pet-silent-task-123x45'});
  if(reply?.relay!=='accepted')throw new Error('TASK_NOT_ACCEPTED');
  report.phase='waiting-result';report.taskAccepted=true;await save();
  await until(()=>returned>0&&result&&audioBytes>0&&!session.closed);
  report.taskReturned=true;report.nativeResultProduced=true;report.nativeAudioBytes=audioBytes;
  report.passed=true;report.status='passed';
}catch(error){
  report.status='failed';report.failure=/^[A-Z0-9_]{1,80}$/.test(error.code??error.message)?(error.code??error.message):'SILENT_TASK_FAILED';
}finally{
  report.taskChannelAvailable=available;report.taskReturnCount=returned;report.nativeResultProduced=result;report.nativeAudioBytes=audioBytes;
  if(session?.toolCalls.length){
    try{
      const tail=await session.backend.gatewayCall('getAgentTranscriptTail',{id:session.bot.id,limit:200});
      const addressed=(tail.entries??[]).filter(entry=>entry.kind==='send-message'&&entry.message?.channel===`voice:${session.callId}`);
      report.addressedHistoryResults=addressed.length;
      report.addressedHistoryHasResult=addressed.some(entry=>/5535|五千五百三十五/.test(entry.message.content??''));
    }catch{report.historyDiagnosticUnavailable=true;}
  }
  if(session){
    try{const closed=await session.close();report.callSaved=closed?.saved===true;}
    catch{report.callSaved=false;}
    if(!report.callSaved){report.passed=false;report.status='failed';report.failure='CALL_SAVE_FAILED';}
  }
  await save();
}
if(!report.passed)process.exitCode=1;
console.log(JSON.stringify(report));
