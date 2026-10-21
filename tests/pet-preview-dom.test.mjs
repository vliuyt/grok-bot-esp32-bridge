import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createController} from '../preview/companion/controller.mjs';
import {sampleFace} from '../preview/companion/face.mjs';

// Only the DOM, animation clock and static fetch are fake. Run the actual app
// listeners and controller; these regressions are at the input/lifecycle boundary.
class Target {
  constructor(){this.listeners={};this.hidden=true;this.classList={toggle(){}};this.attributes={};this.textContent='';this.style={};this.value='';this.dataset={};}
  addEventListener(type,fn){(this.listeners[type]??=[]).push(fn);}
  emit(type,args={}) {
    let prevented=false;
    const event={...args,preventDefault(){prevented=true;}};
    for(const fn of this.listeners[type]??[])fn(event);
    return prevented;
  }
  setAttribute(key,value){this.attributes[key]=value;}
  setPointerCapture(){} focus(){} getBoundingClientRect(){return {width:466,top:0};}
}
async function harness(storage=new Map(),failSave=false) {
  const data=JSON.parse(readFileSync('preview/companion/states.json','utf8'));
  const source=readFileSync('preview/companion/app.mjs','utf8').replace(/^import .*;$/gm,'');
  const nodes=new Map();const get=id=>nodes.get(id)??(nodes.set(id,new Target()),nodes.get(id));
  const buttons=Object.keys(data.states).map(mode=>{const b=new Target();b.dataset={mode};return b;});
  get('#screen').getContext=()=>new Proxy({},{get:()=>()=>{},set:()=>true});
  const document=new Target();document.body=new Target();document.querySelector=get;
  document.querySelectorAll=selector=>selector==='[data-mode]'?[...buttons,get('#caption-window')]:buttons;
  const window=new Target();let time=1000,id=0,controller;const raf=new Map();
  const requestAnimationFrame=fn=>{raf.set(++id,fn);return id;};
  const cancelAnimationFrame=key=>raf.delete(key);
  const AsyncFunction=Object.getPrototypeOf(async function(){}).constructor;
  const run=new AsyncFunction('createController','sampleFace','document','window','matchMedia','performance','requestAnimationFrame','cancelAnimationFrame','fetch','localStorage',source);
  await run(()=>controller=createController(),sampleFace,document,window,()=>({matches:false}),{now:()=>time},
    requestAnimationFrame,cancelAnimationFrame,async()=>({ok:true,json:async()=>data}),{getItem:key=>storage.get(key)??null,setItem:(key,value)=>{if(failSave)throw new Error('storage');storage.set(key,value);}});
  const tick=()=>{const jobs=[...raf.values()];raf.clear();for(const fn of jobs)fn(time);};
  return {get,buttons,document,window,tick,raf,controller,storage,setTime(t){time=t;}};
}
test('unrelated Space keyup keeps native control activation available',async()=>{
  const page=await harness();
  assert.equal(page.window.emit('keyup',{code:'Space',target:page.buttons[2]}),false);
  const screen=page.get('#screen');
  assert.equal(page.window.emit('keydown',{code:'Space',target:screen,repeat:false}),true);
  assert.equal(page.controller.read(1000).mode,'listen');
  page.setTime(1300);
  assert.equal(page.window.emit('keyup',{code:'Space',target:screen}),true);
  assert.equal(page.controller.read(1300).mode,'think');
});
test('BOOT Enter and canvas Space focus loss cancel without submission',async()=>{
  const page=await harness();
  page.get('#boot').emit('keydown',{key:'Enter',repeat:false});
  assert.equal(page.controller.read(1000).held,true);
  page.get('#boot').emit('blur');page.setTime(2000);
  page.window.emit('keyup',{key:'Enter',target:page.buttons[0]});
  assert.equal(page.controller.read(2000).mode,'idle');
  page.window.emit('keydown',{code:'Space',target:page.get('#screen'),repeat:false});
  page.get('#screen').emit('blur');
  assert.equal(page.controller.read(2000).held,false);
});
test('returning from page cache restarts exactly one render loop',async()=>{
  const page=await harness();page.tick();assert.equal(page.raf.size,1);
  page.window.emit('pagehide',{persisted:true});assert.equal(page.raf.size,0);
  page.window.emit('pageshow',{persisted:true});assert.equal(page.raf.size,1);
  page.window.emit('pageshow',{persisted:true});assert.equal(page.raf.size,1);
  page.buttons.find(b=>b.dataset.mode==='curious').emit('click');page.tick();
  assert.match(page.get('#screen').attributes['aria-label'],/好奇/);
});
test('focused canvas keyboard can open settings and return',async()=>{
  const page=await harness();const settings=page.get('#settings');assert.equal(settings.hidden,true);
  page.get('#screen').emit('keydown',{key:'ArrowUp'});assert.equal(settings.hidden,false);
  page.get('#close-settings').emit('click');assert.equal(settings.hidden,true);
});

test('caption controls apply full compact and off without hiding BOOT instructions',async()=>{
  const page=await harness();const mode=page.get('#caption-mode'),fixture=page.get('#caption-case');
  fixture.value='chinese';fixture.emit('change');mode.value='0';mode.emit('change');page.tick();
  assert.equal(page.get('#caption-window').attributes['data-mode'],'full');
  assert.match(page.get('#caption-text').textContent,/路上/);
  mode.value='1';mode.emit('change');page.tick();
  assert.equal(page.get('#caption-text').textContent,'今天一起出门散步吧。');
  mode.value='2';mode.emit('change');page.tick();assert.equal(page.get('#caption-window').hidden,true);
  assert.equal(page.controller.read(1000).mode,'speak');
});
test('scrollable caption clicks never select a pet expression',async()=>{
  const page=await harness();const mode=page.get('#caption-mode');mode.value='0';mode.emit('change');
  page.get('#caption-case').emit('change');page.tick();
  const box=page.get('#caption-window');box.dataset.mode='full';box.emit('click');page.tick();
  assert.equal(page.controller.read(1000).mode,'speak');
  assert.match(page.get('#caption-text').textContent,/路上/);
});
test('caption preview persists valid mode and reports failed saves without changing it',async()=>{
  const saved=new Map([['xiaotang.preview.caption-mode','0']]);
  const page=await harness(saved);assert.equal(page.get('#caption-mode').value,'0');
  page.get('#caption-mode').value='2';page.get('#caption-mode').emit('change');
  const reloaded=await harness(saved);assert.equal(reloaded.get('#caption-mode').value,'2');
  const failed=await harness(saved,true);failed.get('#caption-mode').value='0';failed.get('#caption-mode').emit('change');
  assert.equal(failed.get('#caption-mode').value,'2');assert.equal(failed.get('#caption-error').hidden,false);
});
test('new turn and disconnect clear preview captions rather than replaying them',async()=>{
  const page=await harness();page.get('#caption-case').value='mixed';page.get('#caption-case').emit('change');page.tick();
  assert.match(page.get('#caption-text').textContent,/🙂/);
  page.window.emit('keydown',{code:'Space',target:page.get('#screen'),repeat:false});page.tick();
  assert.equal(page.get('#caption-text').textContent,'嗯，我在听');
  page.get('#screen').emit('blur');page.tick();assert.equal(page.get('#caption-window').hidden,true);
  page.get('#caption-case').emit('change');page.tick();page.get('#connection').emit('click');page.tick();
  assert.equal(page.get('#caption-text').textContent,'等一下，再试试吧');
  page.get('#connection').emit('click');page.tick();assert.equal(page.get('#caption-window').hidden,true);
  page.get('#caption-case').value='empty';page.get('#caption-case').emit('change');page.tick();
  assert.equal(page.get('#caption-window').hidden,true);
});
