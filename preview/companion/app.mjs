import {createController} from './controller.mjs';

const canvas=document.querySelector('#screen');
const ctx=canvas.getContext('2d');
const boot=document.querySelector('#boot');
const controls=[...document.querySelectorAll('[data-mode]')];
const status=document.querySelector('#status');
const settings=document.querySelector('#settings');
const pet=createController();
const reduced=matchMedia('(prefers-reduced-motion: reduce)');
const names={idle:'待机',happy:'开心',greet:'招呼',listen:'聆听',think:'思考',speak:'回答',sleep:'困倦',curious:'好奇',confused:'困惑'};
const messages={idle:'安静地陪你。',curious:'轻点一下，它会好奇地歪歪头。',happy:'抚摸过后，开心会停留一小会儿。',greet:'不出声，也能和你打个招呼。',listen:'正在模拟聆听。松开 BOOT 后提交。',think:'正在模拟等待回答。此时摸它，不会打断思考。',speak:'正在模拟回答。再按住 BOOT，立即开始新一轮。',sleep:'眯一小会儿。碰一碰就醒来。',confused:'暂时没连上，露个小表情，稍后安静等你。'};
const captions={idle:'',happy:'再摸一下嘛',greet:'嘿，我在这里',listen:'嗯，我在听',think:'让我想想',speak:'今天也有好好陪着你。',sleep:'',curious:'嗯？我在呢',confused:'等一下，再试试吧'};
let data=null,lastMode='',lastOnline=null,gesture=null,bootSource=null,frameId;

function now(){return performance.now();}
function cancelHold(){if(bootSource){pet.send('cancel',now());bootSource=null;}}
function press(source){if(settings.hidden&&bootSource===null){bootSource=source;pet.send('press',now());}}
function release(source){if(bootSource===source){pet.send('release',now());bootSource=null;}}
function showSettings(){cancelHold();settings.hidden=false;document.querySelector('#close-settings').focus();}
function sync(state) {
  if(state.mode!==lastMode){
    lastMode=state.mode;status.textContent=messages[state.mode];
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
function roundRect(x,y,w,h,r){ctx.beginPath();ctx.roundRect(x,y,w,h,r);ctx.fill();}
function draw(state,time) {
  const frames=data.states[state.mode];
  const index=reduced.matches?0:Math.floor(Math.max(0,time-state.since)/data.stepMs)%frames.length;
  const face=frames[index];
  ctx.setTransform(2,0,0,2,0,0);ctx.clearRect(0,0,466,466);
  ctx.fillStyle='#101010';ctx.fillRect(0,0,466,466);
  ctx.save();ctx.translate(face.x+face.width/2,face.y+face.height/2);
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
  ctx.fillStyle='#d7cdd3';ctx.fillText(captions[state.mode],233,334,300);
  ctx.fillStyle='#8b7f87';
  const hint=state.mode==='listen'?'松开 BOOT 后提交':!state.online?'语音连接中':['think','speak'].includes(state.mode)?'按住 BOOT 打断':'按住 BOOT 说话';
  ctx.fillText(hint,233,391,260);ctx.fillStyle='#494146';roundRect(216,435,34,3,2);
  if(face.dim){ctx.fillStyle='#10101070';ctx.fillRect(0,0,466,466);}
}
function tick(time){
  const state=pet.read(time);sync(state);
  if(data)draw(state,time);
  frameId=requestAnimationFrame(tick);
}
for(const button of controls)button.addEventListener('click',()=>{
  cancelHold();pet.select(button.dataset.mode,now());
});
document.querySelector('#pet').addEventListener('click',()=>pet.send('stroke',now()));
document.querySelector('#connection').addEventListener('click',()=>{
  cancelHold();pet.send(pet.read(now()).online?'disconnect':'reconnect',now());
});
document.querySelector('#setting-online').addEventListener('change',event=>pet.send(event.target.checked?'reconnect':'disconnect',now()));
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
window.addEventListener('keyup',event=>{if(event.code==='Space'){event.preventDefault();release('space');}});
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
  else if(Math.abs(dx)>40&&Math.abs(dy)<35)pet.send('stroke',now());
  else if(Math.abs(dx)<30&&Math.abs(dy)<30)pet.send('tap',now());
});
canvas.addEventListener('pointercancel',()=>{gesture=null;});
canvas.addEventListener('keydown',event=>{if(event.key==='Enter'){event.preventDefault();pet.send('tap',now());}});
window.addEventListener('blur',cancelHold);
document.addEventListener('visibilitychange',()=>{if(document.hidden){cancelHold();gesture=null;}});
window.addEventListener('pagehide',()=>{cancelAnimationFrame(frameId);cancelHold();});

for(const button of [...controls,boot,document.querySelector('#pet')])button.disabled=true;
try {
  const response=await fetch('./states.json');
  if(!response.ok)throw new Error('Preview data unavailable');
  data=await response.json();
  if(data.schema!==1||!Object.keys(names).every(mode=>data.states?.[mode]?.length))throw new Error('Invalid preview data');
  for(const button of [...controls,boot,document.querySelector('#pet')])button.disabled=false;
  frameId=requestAnimationFrame(tick);
} catch {
  const error=document.querySelector('#error');error.hidden=false;
  error.textContent='预览没有加载完成，请刷新页面重试。';
}
