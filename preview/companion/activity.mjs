const container=document.querySelector('#profiles'),status=document.querySelector('#status');
const play=document.querySelector('#play'),restart=document.querySelector('#restart');
let profiles=[],running=!matchMedia('(prefers-reduced-motion: reduce)').matches,started=performance.now(),elapsed=0,lastDraw=-Infinity,raf=0;
function draw(canvas,f){
  const c=canvas.getContext('2d');c.clearRect(0,0,466,466);c.fillStyle='#101010';c.fillRect(0,0,466,466);
  c.save();c.translate(f.x+f.width/2,f.y+f.height/2);c.rotate(f.rotation*Math.PI/1800);c.translate(-f.width/2,-f.height/2);
  c.fillStyle='#fa2c99';c.beginPath();c.roundRect(0,0,f.width,f.height,Math.min(f.width,f.height)/2);c.fill();
  c.fillStyle='#121212';for(const eye of f.eyes){c.save();c.translate(eye.x+12,eye.y+30);c.rotate(eye.rotation*Math.PI/1800);c.beginPath();c.roundRect(-12,-30,24,eye.height,12);c.fill();c.restore();}c.restore();
  c.textAlign='center';c.textBaseline='top';c.fillStyle='#f5f3f5';c.font='22px Xiaotang, sans-serif';c.fillText('小糖',233,45);
  c.font='28px Xiaotang, sans-serif';c.fillText('我在听',233,312);c.fillStyle='#b5a8b1';c.font='22px Xiaotang, sans-serif';c.fillText('松开 BOOT 后提交',233,403);
}
function tick(){
  const now=performance.now();
  if(now-lastDraw>=32){const time=Math.max(0,running?elapsed+now-started:elapsed);for(const p of profiles){const index=Math.min(p.frames.length-1,Math.floor((time%p.periodMs)/33));draw(p.canvas,p.frames[index]);}lastDraw=now;}
  if(running)raf=requestAnimationFrame(tick);
}
function renderNow(){cancelAnimationFrame(raf);lastDraw=-Infinity;raf=requestAnimationFrame(tick);}
play.addEventListener('click',()=>{if(running)elapsed+=performance.now()-started;else started=performance.now();running=!running;play.textContent=running?'暂停':'播放';renderNow();});
restart.addEventListener('click',()=>{elapsed=0;started=performance.now();renderNow();});
document.querySelector('#enlarge').addEventListener('change',event=>container.classList.toggle('enlarged',event.target.checked));
document.addEventListener('visibilitychange',()=>{if(document.hidden){if(running)elapsed+=performance.now()-started;cancelAnimationFrame(raf);}else{started=performance.now();renderNow();}});
try{
  const response=await fetch('./activity.json',{cache:'no-store'});if(!response.ok)throw new Error();const data=await response.json();
  if(data.schema!==1||data.profiles.length!==3||data.profiles.some(p=>!p.frames?.length||!(p.periodMs>0)))throw new Error();
  profiles=data.profiles.map(p=>{
    const card=document.createElement('section');card.className='profile';const title=document.createElement('h2');title.textContent=p.label;
    const canvas=document.createElement('canvas');canvas.width=canvas.height=466;canvas.setAttribute('aria-label',`${p.label}的聆听动作`);
    const detail=document.createElement('p');detail.textContent={current:'动作之间停得较久',active:'衔接更紧，保留停顿',lively:'小动作更连续'}[p.id];
    const choose=document.createElement('button');choose.textContent='我更喜欢这档';choose.setAttribute('aria-pressed','false');choose.addEventListener('click',()=>{for(const entry of profiles){entry.card.classList.remove('selected');entry.choose.setAttribute('aria-pressed','false');}card.classList.add('selected');choose.setAttribute('aria-pressed','true');status.textContent=`你选了「${p.label}」，可以继续切换比较。`;});
    card.append(title,canvas,detail,choose);container.append(card);return {...p,canvas,card,choose};
  });
  await document.fonts.load('22px Xiaotang');play.disabled=restart.disabled=false;play.textContent=running?'暂停':'播放';status.textContent='先看一会儿，再选择你喜欢的节奏。';started=performance.now();renderNow();
}catch{status.textContent='预览暂时没加载好，请刷新再试。';}
