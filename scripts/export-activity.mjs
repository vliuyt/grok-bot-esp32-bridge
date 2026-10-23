import {execFileSync} from 'node:child_process';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {resolve,join} from 'node:path';
import {tmpdir} from 'node:os';
import {createHash} from 'node:crypto';

const args=process.argv.slice(2);
if(args.length&&!(args.length===2&&args[0]==='--output'))throw new Error('ACTIVITY_ARGUMENTS');
const output=resolve(args[1]??'preview/companion/activity.json');
const core=readFileSync('firmware/pet/components/pet_core/pet_core.c','utf8');
const variants=[
  {id:'current',label:'原先节奏',cycle:6200,gaze:[200,900],lean:[850,1300],nod:[2600,900],blink:[4200,220]},
  {id:'active',label:'更活泼',cycle:4200,gaze:[150,850],lean:[650,1000],nod:[1900,750],blink:[3050,220]},
  {id:'lively',label:'很活泼',cycle:3000,gaze:[100,700],lean:[450,850],nod:[1300,650],blink:[2300,220]},
];
function replaceOnce(source,pattern,value){
  if([...source.matchAll(new RegExp(pattern.source,'g'))].length!==1)throw new Error('ACTIVITY_SOURCE_UNSUPPORTED');
  return source.replace(pattern,value);
}
function variantSource(v){
  let s=replaceOnce(core,/cycle=age%\d+/,`cycle=age%${v.cycle}`);
  s=replaceOnce(s,/s->turn\+age\/\d+/,`s->turn+age/${v.cycle}`);
  for(const [pattern,values]of [
    [/p\.look_x=side\*5\*pose_pulse\(cycle,\d+,\d+\)/,v.gaze],
    [/p\.rotation\+=side\*3\*pose_pulse\(cycle,\d+,\d+\)/,v.lean],
    [/float nod=pose_pulse\(cycle,\d+,\d+\)/,v.nod],
    [/p\.eye_open-=\.82f\*pose_pulse\(cycle,\d+,\d+\)/,v.blink],
  ])s=replaceOnce(s,pattern,match=>match.replace(/cycle,\d+,\d+/,`cycle,${values.join(',')}`));
  return s;
}
const temporary=mkdtempSync(join(tmpdir(),'pet-activity-'));
try{
  const profiles=variants.map(v=>{
    const source=join(temporary,`${v.id}.c`),binary=join(temporary,v.id);writeFileSync(source,variantSource(v));
    execFileSync('/usr/bin/cc',['-std=c11','-Wall','-Wextra','-Werror','-Ifirmware/pet/components/pet_core','tests/pet_activity_export.c',source,
      ...['pet_face','pet_caption','pet_readability'].map(n=>`firmware/pet/components/pet_core/${n}.c`),'-lm','-o',binary],{stdio:'pipe'});
    const frames=JSON.parse(execFileSync(binary,[String(v.cycle)],{encoding:'utf8',maxBuffer:4*1024*1024}));
    return {...v,periodMs:v.cycle*2,frames};
  });
  writeFileSync(output,JSON.stringify({schema:1,stepMs:33,sourceSha256:createHash('sha256').update(core).digest('hex'),profiles})+'\n');
  process.stdout.write('ACTIVITY_PREVIEW_EXPORTED\n');
}finally{rmSync(temporary,{recursive:true,force:true});}
