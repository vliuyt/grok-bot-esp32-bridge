import {createController} from './controller.mjs';

const canvas=document.querySelector('#screen');
const ctx=canvas.getContext('2d');
const boot=document.querySelector('#boot');
const controls=[...document.querySelectorAll('button[data-mode]')];
const status=document.querySelector('#status');
const settings=document.querySelector('#settings');
const captionWindow=document.querySelector('#caption-window'),captionText=document.querySelector('#caption-text');
const captionModeControl=document.querySelector('#caption-mode'),captionCase=document.querySelector('#caption-case');
const captionError=document.querySelector('#caption-error');
const storageKey='xiaotang.preview.caption-mode';
let captionMode=1,sampleActive=false,lastCaptionMode=-1;
try{const saved=localStorage.getItem(storageKey);if(['0','1','2'].includes(saved))captionMode=Number(saved);}catch{}
captionModeControl.value=String(captionMode);
const pet=createController();
const reduced=matchMedia('(prefers-reduced-motion: reduce)');
const names={idle:'待机',happy:'开心',greet:'招呼',listen:'聆听',think:'思考',speak:'回答',sleep:'困倦',curious:'好奇',confused:'困惑'};
const messages={idle:'安静地陪你。',curious:'轻点一下，它会好奇地歪歪头。',happy:'抚摸过后，开心会停留一小会儿。',greet:'不出声，也能和你打个招呼。',listen:'正在模拟聆听。松开 BOOT 后提交。',think:'正在模拟等待回答。此时摸它，不会打断思考。',speak:'正在模拟回答。再按住 BOOT，立即开始新一轮。',sleep:'眯一小会儿。碰一碰就醒来。',confused:'暂时没连上，露个小表情，稍后安静等你。'};
const captions={idle:'',happy:'再摸一下嘛',greet:'嘿，我在这里',listen:'嗯，我在听',think:'让我想想',speak:'',sleep:'',curious:'嗯？我在呢',confused:'等一下，再试试吧'};
let data=null,lastMode='',lastOnline=null,gesture=null,bootSource=null,frameId=null,pageActive=true;

function now(){return performance.now();}
function act(action){
  const before=pet.read(now()),next=pet.send(action,now());
  if((action==='press'&&next.held)||action==='disconnect'||(action==='cancel'&&before.held))sampleActive=false;
  return next;
}
function cancelHold(){if(bootSource){act('cancel');bootSource=null;}}
function press(source){if(settings.hidden&&bootSource===null){bootSource=source;act('press');}}
function release(source){if(bootSource===source){act('release');bootSource=null;}}
function showSettings(){cancelHold();settings.hidden=false;document.querySelector('#close-settings').focus();}
function sync(state) {
  if(state.mode!==lastMode){
    lastMode=state.mode;status.textContent=messages[state.mode];
    if(state.mode==='speak'&&state.online)sampleActive=true;
    canvas.setAttribute('aria-label',`小糖${names[state.mode]}圆屏，轻点好奇，横向拖动抚摸`);
    for(const button of controls)button.setAttribute('aria-pressed',String(button.dataset.mode===state.mode));
  }
  boot.classList.toggle('active',state.held);
  if(lastOnline!==state.online){
    lastOnline=state.online;
    document.querySelector('#connection-dot').classList.toggle('offline',!state.online);
    document.querySelector('#connection-text').textContent=state.online?'语音就绪 · 模拟':'语音离线 · 模拟';
    document.querySelector('#connection').textContent=state.online?'模拟断网':'恢复连接';
    document.querySelector('#setting-online').checked=state.online;
  }
}
captionModeControl.addEventListener('change',()=>{
  const next=Number(captionModeControl.value);
  try{
    if(!Number.isInteger(next)||next<0||next>2)throw new Error('mode');
    localStorage.setItem(storageKey,String(next));captionMode=next;captionError.hidden=true;
  }catch{captionModeControl.value=String(captionMode);captionError.hidden=false;captionError.textContent='字幕模式未保存，请重试。';}
});
captionCase.addEventListener('change',()=>{cancelHold();sampleActive=true;pet.select('speak',now());});
function syncCaption(state){
  const sample=data.captionCases.find(item=>item.id===(captionCase.value||'chinese'))||data.captionCases[0];
  const text=sampleActive&&state.online&&['idle','speak'].includes(state.mode)?sample.modes[captionMode]:captions[state.mode];
  if(captionText.textContent!==text||lastCaptionMode!==captionMode){captionText.textContent=text;captionWindow.scrollTop=0;}
  lastCaptionMode=captionMode;
  captionWindow.setAttribute('data-mode',captionMode===0?'full':'compact');captionWindow.hidden=!text;
}
function roundRect(x,y,w,h,r){ctx.beginPath();ctx.roundRect(x,y,w,h,r);ctx.fill();}
function draw(state,time) {
  const frames=data.states[state.mode];
  const index=reduced.matches?0:Math.floor(Math.max(0,time-state.since)/data.stepMs)%frames.length;
  let face=frames[index];
  if(state.mode==='idle'&&state.warmth>0&&data.states.warm_idle){
    const warm=data.states.warm_idle[index%data.states.warm_idle.length],amount=state.warmth/100;
    const blend=(base,target)=>Object.fromEntries(Object.entries(base).map(([key,value])=>[key,typeof value==='number'?value+(target[key]-value)*amount:value]));
    face={...blend(face,warm),eyes:face.eyes.map((eye,i)=>blend(eye,warm.eyes[i]))};
  }
  ctx.setTransform(2,0,0,2,0,0);ctx.clearRect(0,0,466,466);
  ctx.fillStyle='#101010';ctx.fillRect(0,0,466,466);
  ctx.save();ctx.translate(face.x+face.width/2,face.y+face.height/2-(captionMode===0?22:0));
  ctx.rotate(face.rotation*Math.PI/1800);ctx.translate(-face.width/2,-face.height/2);
  ctx.fillStyle='#fa2c99';roundRect(0,0,face.width,face.height,Math.min(face.width,face.height)/2);
  ctx.fillStyle='#121212';
  for(const eye of face.eyes){
    if(eye.arc){
      ctx.beginPath();ctx.strokeStyle='#121212';ctx.lineWidth=10;ctx.lineCap='round';
      ctx.arc(eye.x+12,eye.y+18,13,eye.arcStart*Math.PI/180,eye.arcEnd*Math.PI/180);ctx.stroke();
    } else {
      ctx.save();ctx.translate(eye.x+12,eye.y+30);ctx.rotate(eye.rotation*Math.PI/1800);
      roundRect(-12,-30,24,eye.height,12);ctx.restore();
    }
  }
  ctx.restore();
  ctx.textAlign='center';ctx.textBaseline='top';ctx.font='16px "PingFang SC", sans-serif';
  ctx.fillStyle='#8b7f87';
  const hint=state.mode==='listen'?'松开 BOOT 后提交':!state.online?'语音连接中':['think','speak'].includes(state.mode)?'按住 BOOT 打断':'按住 BOOT 说话';
  ctx.fillText(hint,233,391,260);ctx.fillStyle='#494146';roundRect(216,435,34,3,2);
  if(face.dim){ctx.fillStyle='#10101070';ctx.fillRect(0,0,466,466);}
}
function tick(time){
  const state=pet.read(time);sync(state);
  if(data){syncCaption(state);draw(state,time);}
  frameId=requestAnimationFrame(tick);
}
for(const button of controls)button.addEventListener('click',()=>{
  cancelHold();pet.select(button.dataset.mode,now());
});
document.querySelector('#pet').addEventListener('click',()=>act('stroke'));
document.querySelector('#connection').addEventListener('click',()=>{
  cancelHold();act(pet.read(now()).online?'disconnect':'reconnect');
});
document.querySelector('#setting-online').addEventListener('change',event=>act(event.target.checked?'reconnect':'disconnect'));
document.querySelector('#close-settings').addEventListener('click',()=>{settings.hidden=true;canvas.focus();});
boot.addEventListener('pointerdown',event=>{
  if(event.button!==0)return;
  event.preventDefault();boot.setPointerCapture(event.pointerId);press('pointer');
});
boot.addEventListener('pointerup',()=>release('pointer'));
boot.addEventListener('pointercancel',cancelHold);
boot.addEventListener('lostpointercapture',cancelHold);
boot.addEventListener('keydown',event=>{
  if(event.key==='Enter'){event.preventDefault();if(!event.repeat)press('enter');}
});
boot.addEventListener('keyup',event=>{if(event.key==='Enter'){event.preventDefault();release('enter');}});
window.addEventListener('keydown',event=>{
  if(event.code==='Space'&&(event.target===document.body||event.target===canvas||event.target===boot)){
    event.preventDefault();if(!event.repeat)press('space');
  }
  if(event.key==='Escape'&&!settings.hidden){settings.hidden=true;canvas.focus();}
});
window.addEventListener('keyup',event=>{if(event.code==='Space'&&bootSource==='space'){event.preventDefault();release('space');}});
canvas.addEventListener('pointerdown',event=>{
  if(event.button!==0||!settings.hidden)return;
  const rect=canvas.getBoundingClientRect();
  gesture={x:event.clientX,y:event.clientY,scale:466/rect.width,id:event.pointerId};
  canvas.setPointerCapture(event.pointerId);
});
canvas.addEventListener('pointerup',event=>{
  if(!gesture||gesture.id!==event.pointerId)return;
  const dx=(event.clientX-gesture.x)*gesture.scale,dy=(event.clientY-gesture.y)*gesture.scale;
  const rect=canvas.getBoundingClientRect();const y=(gesture.y-rect.top)*gesture.scale;
  gesture=null;
  if(dy< -40&&Math.abs(dy)>Math.abs(dx))showSettings();
  else if(y>415&&Math.abs(dx)<30&&Math.abs(dy)<30)showSettings();
  else if(Math.abs(dx)>40&&Math.abs(dy)<35)act('stroke');
  else if(Math.abs(dx)<30&&Math.abs(dy)<30)act('tap');
});
canvas.addEventListener('pointercancel',()=>{gesture=null;});
canvas.addEventListener('keydown',event=>{
  if(event.key==='Enter'){event.preventDefault();act('tap');}
  else if(event.key==='ArrowUp'){event.preventDefault();showSettings();}
});
boot.addEventListener('blur',cancelHold);
canvas.addEventListener('blur',cancelHold);
window.addEventListener('blur',cancelHold);
document.addEventListener('visibilitychange',()=>{if(document.hidden){cancelHold();gesture=null;}});
function startLoop(){if(data&&pageActive&&frameId===null)frameId=requestAnimationFrame(tick);}
window.addEventListener('pagehide',()=>{pageActive=false;cancelAnimationFrame(frameId);frameId=null;cancelHold();});
window.addEventListener('pageshow',()=>{pageActive=true;startLoop();});

for(const button of [...controls,boot,document.querySelector('#pet'),captionModeControl,captionCase])button.disabled=true;
try {
  const response=await fetch('./states.json');
  if(!response.ok)throw new Error('Preview data unavailable');
  data=await response.json();
  if(data.schema!==1||!Object.keys(names).every(mode=>data.states?.[mode]?.length)||!data.captionCases?.length)throw new Error('Invalid preview data');
  for(const button of [...controls,boot,document.querySelector('#pet'),captionModeControl,captionCase])button.disabled=false;
  startLoop();
} catch {
  const error=document.querySelector('#error');error.hidden=false;
  error.textContent='预览没有加载完成，请刷新页面重试。';
}
