// Idle uses the core's absolute 60-second clock, rather than looping a short
// feedback clip. Cold and warm frames share that phase and are never mutated.
export function sampleFace(data,state,time,reduced=false){
  const frames=data.states[state.mode];
  const phase=state.mode==='idle'?Math.max(0,time)%60000:Math.max(0,time-state.since);
  const index=reduced?0:Math.floor(phase/data.stepMs)%frames.length;
  const face=frames[index];
  if(state.mode!=='idle'||!state.warmth||!data.states.warm_idle)return face;
  const warm=data.states.warm_idle[index],amount=Math.max(0,Math.min(1,state.warmth/100));
  const blend=(base,target)=>Object.fromEntries(Object.entries(base).map(([key,value])=>[key,typeof value==='number'?value+(target[key]-value)*amount:value]));
  return {...blend(face,warm),eyes:face.eyes.map((eye,i)=>blend(eye,warm.eyes[i]))};
}
