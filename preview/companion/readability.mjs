import {createController} from './controller.mjs';

const byId=id=>document.getElementById(id);
const pages=[...document.querySelectorAll('[data-screen]')];
const sceneButtons=[...document.querySelectorAll('button[data-scene]')];
const layoutButtons=[...document.querySelectorAll('button[data-layout]')];
const captionButtons=[...document.querySelectorAll('button[data-caption]')];
const modeNames=['完整','简洁','关闭'];
const controller=createController();
let samples=[],scene='home',layout='clear',captionMode=1,sampleVisible=true;
let bootSource=null,ownedPointer=null,lastVoice='',frame=null,pageActive=true;
const boot=byId('readability-boot'),face=byId('pet-face');
const error=byId('readability-error');
function state(){return controller.read(performance.now());}
function selectedSample(){return samples.find(item=>item.id===byId('readability-sample').value)||samples[0];}
function cancelHold(){
  if(bootSource!==null){controller.send('cancel',performance.now());bootSource=null;ownedPointer=null;sampleVisible=false;render();}
}
function show(next){
  cancelHold();scene=next;
  for(const page of pages)page.hidden=page.dataset.screen!==scene;
  for(const button of sceneButtons)button.setAttribute('aria-pressed',String(button.dataset.scene===scene));
  const descriptions={home:'轻点字幕展开阅读 · 底部短横打开设置',reading:'上下滑动看当前片段 · 按 BOOT 开始新一轮',settings:'整行都可以点 · 每页只放常用选项',volume:'仅模拟数值，不播放声音',brightness:'仅模拟数值，不改变设备亮度',captions:'关闭正文仍保留 BOOT 指引'};
  byId('screen-description').textContent=layout==='current'?'现有网页字号示意 · 切换新版可操作':descriptions[scene];
  render();
}
function render(){
  if(!samples.length)return;
  const current=state(),sample=selectedSample();
  const prompts={listen:'嗯，我在听',think:'让我想想',happy:'再摸一下嘛',curious:'嗯？我在呢'};
  const text=prompts[current.mode]??(sampleVisible&&captionMode!==2?sample.modes[1]:'');
  byId('home-text').textContent=text;
  byId('read-caption').hidden=!text;
  byId('read-caption').disabled=layout==='current'||Boolean(prompts[current.mode]);
  const full=sampleVisible&&captionMode!==2?sample.modes[0]:'';
  if(byId('reading-text').textContent!==full){byId('reading-text').textContent=full;byId('reading-text').scrollTop=0;}
  byId('pet-status').textContent=current.mode==='speak'?'小糖 · 说给你听':'小糖 · 陪着你';
  face.dataset.mood=current.mode;
  boot.classList.toggle('active',current.held);
  const hint=current.mode==='listen'?'松开 BOOT 后提交':['think','speak'].includes(current.mode)?'按住 BOOT 打断':'按住 BOOT 说话';
  for(const label of document.querySelectorAll('.boot-hint'))label.textContent=hint;
  byId('caption-value').textContent=modeNames[captionMode];
  for(const button of captionButtons)button.setAttribute('aria-pressed',String(Number(button.dataset.caption)===captionMode));
}
function begin(source){
  if(bootSource!==null||!samples.length)return false;
  show('home');bootSource=source;sampleVisible=false;
  controller.send('press',performance.now());render();return true;
}
function release(source){if(bootSource===source){controller.send('release',performance.now());bootSource=null;ownedPointer=null;render();}}
function setLayout(next){
  layout=next;document.body.dataset.layout=layout;
  for(const button of layoutButtons)button.setAttribute('aria-pressed',String(button.dataset.layout===layout));
  for(const button of sceneButtons)button.disabled=layout==='current'&&button.dataset.scene!=='home';
  byId('type-size').disabled=layout==='current';
  document.querySelector('.settings-handle').disabled=layout==='current';
  show('home');
}
for(const button of layoutButtons)button.addEventListener('click',()=>setLayout(button.dataset.layout));
for(const button of sceneButtons)button.addEventListener('click',()=>show(button.dataset.scene));
for(const button of document.querySelectorAll('button[data-open]'))button.addEventListener('click',()=>show(button.dataset.open));
byId('type-size').addEventListener('change',event=>{document.body.dataset.size=event.target.value;});
byId('read-caption').addEventListener('click',()=>show('reading'));
face.addEventListener('click',()=>{controller.send('stroke',performance.now());render();});
for(const button of captionButtons)button.addEventListener('click',()=>{captionMode=Number(button.dataset.caption);render();});
for(const name of ['volume','brightness'])byId(name).addEventListener('input',event=>{
  const value=event.target.value;
  byId(`${name}-value`).textContent=`${value}%`;
  byId(`${name}-number`).replaceChildren(document.createTextNode(value),Object.assign(document.createElement('span'),{textContent:'%'}));
});
byId('readability-sample').addEventListener('change',()=>{
  cancelHold();controller.select('idle',performance.now());sampleVisible=true;render();
});
boot.addEventListener('pointerdown',event=>{
  if(event.button!==0||bootSource!==null)return;
  event.preventDefault();
  if(begin('pointer')){ownedPointer=event.pointerId;boot.setPointerCapture(ownedPointer);}
});
boot.addEventListener('pointerup',event=>{if(ownedPointer===event.pointerId)release('pointer');});
for(const type of ['pointercancel','lostpointercapture'])boot.addEventListener(type,event=>{
  if(bootSource==='pointer'&&ownedPointer===event.pointerId)cancelHold();
});
boot.addEventListener('keydown',event=>{
  if(event.key==='Enter'||event.code==='Space'){event.preventDefault();if(!event.repeat)begin(event.code==='Space'?'space':'enter');}
});
window.addEventListener('keyup',event=>{
  if((event.code==='Space'&&bootSource==='space')||(event.key==='Enter'&&bootSource==='enter')){
    event.preventDefault();release(bootSource);
  }
});
boot.addEventListener('blur',cancelHold);window.addEventListener('blur',cancelHold);
document.addEventListener('visibilitychange',()=>{if(document.hidden)cancelHold();});
window.addEventListener('keydown',event=>{
  if(event.key==='Escape'&&scene!=='home'){show(['volume','brightness','captions'].includes(scene)?'settings':'home');}
});
function tick(){
  frame=null;
  const current=state();
  if(current.mode!==lastVoice){lastVoice=current.mode;if(current.mode==='speak')sampleVisible=true;render();}
  if(pageActive)frame=requestAnimationFrame(tick);
}
window.addEventListener('pagehide',()=>{pageActive=false;cancelHold();cancelAnimationFrame(frame);frame=null;});
window.addEventListener('pageshow',()=>{pageActive=true;if(samples.length&&frame===null)frame=requestAnimationFrame(tick);});
boot.disabled=true;
try{
  const [response,font]=await Promise.all([fetch('./states.json'),document.fonts.load('500 28px "Xiaotang UI Preview"')]);
  if(!response.ok||!font.length)throw new Error('Preview assets unavailable');
  const data=await response.json();
  if(data.schema!==1||!data.captionCases?.length)throw new Error('Invalid preview data');
  samples=data.captionCases;byId('readability-sample').disabled=false;boot.disabled=false;
  render();if(frame===null)frame=requestAnimationFrame(tick);
}catch{
  error.hidden=false;error.textContent='字体或示例没有加载完成，请刷新页面重试。';
}
