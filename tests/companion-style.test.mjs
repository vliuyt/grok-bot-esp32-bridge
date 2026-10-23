import {test} from 'node:test';
import assert from 'node:assert/strict';
import {EventEmitter} from 'node:events';
import {companionPlan} from '../src/companion-style.mjs';
import {NativeBotSession} from '../src/native-bot.mjs';

test('device advice copies the plan while preserving identity tools and voice',()=>{
 const plan=Object.freeze({instructions:'原有角色和身份',tools:[{name:'send_task'}],voice:'native',other:{a:1}});
 const next=companionPlan(plan);
 assert.notEqual(next,plan);assert.equal(plan.instructions,'原有角色和身份');
 assert.ok(next.instructions.startsWith(plan.instructions));assert.equal(next.tools,plan.tools);
 assert.equal(next.voice,plan.voice);assert.equal(next.other,plan.other);
 assert.match(next.instructions,/一到两句/);assert.match(next.instructions,/明确要求.*优先/);assert.match(next.instructions,/故事.*计算.*任务/);
});
test('only opted-in native sessions use device advice',async()=>{
 for(const style of [null,'companion']){
  const plan={instructions:'原角色',tools:[],voice:'native'};
  class Voice extends EventEmitter {async open(args){this.plan=args.plan;}close(){}}
  const voice=new Voice(),backend={rpc:async name=>name==='VoiceCallHarnessSession'?plan:{model:'native'},gatewayCall:async()=>({entries:[]})};
  const session=new NativeBotSession({backend,bot:{id:'fixture',name:'fixture'},voice,replyStyle:style});
  try{await session.open();if(style)assert.match(voice.plan.instructions,/一到两句/);else assert.equal(voice.plan,plan);
   assert.equal(voice.plan.tools,plan.tools);assert.equal(voice.plan.voice,'native');assert.equal(plan.instructions,'原角色');
  }finally{await session.close();}
 }
});
