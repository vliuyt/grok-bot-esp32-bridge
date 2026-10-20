import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {resolve,join} from 'node:path';
import {pathToFileURL} from 'node:url';

// Run actual UI listeners and controller with a fake DOM and animation clock.
// No microphone, Bot session, real timers, or hardware is involved.
const repo=resolve(process.argv[2]??process.cwd());
const {createController}=await import(pathToFileURL(join(repo,'preview/companion/controller.mjs')));

class Target {
  constructor(dataset={}) {
    this.listeners={};this.hidden=false;this.attributes={};this.textContent='';
    this.value='';this.dataset=dataset;this.scrollTop=0;this.classes=new Set();
    this.classList={toggle:(key,on)=>on?this.classes.add(key):this.classes.delete(key)};
  }
  addEventListener(type,fn){(this.listeners[type]??=[]).push(fn);}
  emit(type,args={}) {
    let prevented=false;
    const event={...args,preventDefault(){prevented=true;}};
    for(const fn of this.listeners[type]??[])fn(event);
    return prevented;
  }
  setAttribute(key,value){this.attributes[key]=value;}
  setPointerCapture(){}
}

async function harness() {
  const nodes=new Map();
  const get=id=>nodes.get(id)??(nodes.set(id,new Target()),nodes.get(id));
  const pages=['home','reading','settings','volume','brightness','captions'].map(screen=>new Target({screen}));
  const scenes=['home','reading','settings'].map(scene=>new Target({scene}));
  const layouts=['current','clear'].map(layout=>new Target({layout}));
  const captions=['0','1','2'].map(caption=>new Target({caption}));
  const hints=[new Target(),new Target()];
  const opens=['settings','home','volume','brightness','captions','settings'].map(open=>new Target({open}));
  const document=new Target();document.body=new Target();document.getElementById=get;
  document.querySelector=()=>opens[0];
  document.querySelectorAll=selector=>({
    '[data-screen]':pages,'button[data-scene]':scenes,'button[data-layout]':layouts,
    'button[data-caption]':captions,'button[data-open]':opens,'.boot-hint':hints,
  }[selector]??[]);
  document.fonts={load:async()=>[{}]};
  document.createElement=()=>new Target();document.createTextNode=text=>({textContent:text});
  const window=new Target();let time=1000,id=0,controller;const raf=new Map();
  const source=readFileSync(join(repo,'preview/companion/readability.mjs'),'utf8')
    .replace("import {createController} from './controller.mjs';",'');
  const data=JSON.parse(readFileSync(join(repo,'preview/companion/states.json'),'utf8'));
  get('readability-sample').value='chinese';
  const AsyncFunction=Object.getPrototypeOf(async function(){}).constructor;
  const run=new AsyncFunction('createController','document','window','performance','requestAnimationFrame','cancelAnimationFrame','fetch',source);
  await run(()=>controller=createController(),document,window,{now:()=>time},
    fn=>{raf.set(++id,fn);return id;},key=>raf.delete(key),
    async()=>({ok:true,json:async()=>data}));
  const tick=()=>{const jobs=[...raf.values()];raf.clear();for(const fn of jobs)fn(time);};
  return {get,window,controller,raf,tick,hints,pages,scenes,layouts,captions,document,setTime:t=>time=t};
}

test('same-frame BOOT cancellation immediately restores the visible idle state',async()=>{
  const page=await harness();page.tick();const boot=page.get('readability-boot');
  boot.emit('pointerdown',{button:0,pointerId:1});
  page.setTime(1005);boot.emit('pointercancel',{pointerId:1});page.tick();
  assert.equal(page.controller.read(1005).mode,'idle');
  assert.equal(page.controller.read(1005).held,false);
  assert.equal(page.get('pet-face').dataset.mood,'idle');
  assert.equal(page.get('home-text').textContent,'');
  assert.equal(boot.classes.has('active'),false);
  assert.equal(page.hints[0].textContent,'按住 BOOT 说话');
});

test('a second pointer release cannot submit the first pointer hold',async()=>{
  const page=await harness();page.tick();const boot=page.get('readability-boot');
  boot.emit('pointerdown',{button:0,pointerId:1});page.tick();page.setTime(1300);
  boot.emit('pointerdown',{button:0,pointerId:2});
  boot.emit('pointerup',{pointerId:2});
  assert.equal(page.controller.read(1300).mode,'listen');
  assert.equal(page.controller.read(1300).held,true);
  boot.emit('pointerup',{pointerId:1});
  assert.equal(page.controller.read(1300).mode,'think');
});

test('unowned pointer cancellation does not end a held BOOT gesture',async()=>{
  const page=await harness();const boot=page.get('readability-boot');
  boot.emit('pointerdown',{button:0,pointerId:1});page.setTime(1300);
  boot.emit('lostpointercapture',{pointerId:2});
  boot.emit('pointercancel',{pointerId:2});
  assert.equal(page.controller.read(1300).held,true);
  boot.emit('pointercancel',{pointerId:1});
  assert.equal(page.controller.read(1300).mode,'idle');
  assert.equal(boot.classes.has('active'),false);
});

test('Enter repeat is ignored and release submits exactly one turn',async()=>{
  const page=await harness();page.tick();const boot=page.get('readability-boot');
  boot.emit('keydown',{key:'Enter',code:'Enter',repeat:false});
  boot.emit('keydown',{key:'Enter',code:'Enter',repeat:true});
  assert.equal(page.controller.read(1000).mode,'listen');
  page.setTime(1300);page.window.emit('keyup',{key:'Enter',code:'Enter'});page.tick();
  assert.equal(page.controller.read(1300).mode,'think');
  page.window.emit('keyup',{key:'Enter',code:'Enter'});
  assert.equal(page.controller.read(1300).since,1300);
});

test('pagehide stops the loop and repeated pageshow starts only one loop',async()=>{
  const page=await harness();page.tick();assert.equal(page.raf.size,1);
  page.window.emit('pagehide');assert.equal(page.raf.size,0);
  page.window.emit('pageshow');page.window.emit('pageshow');assert.equal(page.raf.size,1);
});

test('caption off hides dialogue but retains local BOOT prompts',async()=>{
  const page=await harness();page.tick();
  page.captions.find(button=>button.dataset.caption==='2').emit('click');
  assert.equal(page.get('read-caption').hidden,true);
  assert.equal(page.get('reading-text').textContent,'');
  page.get('readability-boot').emit('keydown',{key:'Enter',code:'Enter',repeat:false});
  assert.equal(page.get('home-text').textContent,'嗯，我在听');
  assert.equal(page.hints[0].textContent,'松开 BOOT 后提交');
});

test('mode updates preserve an independently scrolled current reading fragment',async()=>{
  const page=await harness();page.tick();
  page.get('pet-face').emit('click');page.tick();
  page.scenes.find(button=>button.dataset.scene==='reading').emit('click');
  page.get('reading-text').scrollTop=84;
  page.setTime(2800);page.tick();
  assert.equal(page.get('reading-text').scrollTop,84);
  assert.match(page.get('reading-text').textContent,/路上/);
});
