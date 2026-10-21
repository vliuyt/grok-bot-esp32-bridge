const modes=new Set(['idle','happy','greet','listen','think','speak','sleep','curious','confused']);
const durations={happy:1800,greet:3000,curious:2000,confused:2000,think:1200,speak:3200};

// A silent interaction simulator. It never opens a microphone or a Bot session.
export function createController() {
  let state={mode:'idle',since:0,held:false,online:true};
  let warmthLevel=0,warmthAt=0;
  const warmth=now=>warmthLevel*Math.max(0,1-Math.max(0,now-warmthAt)/60000);
  function enter(mode,now){state={...state,mode,since:now};}
  function read(now) {
    while(durations[state.mode]!==undefined&&now-state.since>=durations[state.mode]) {
      const end=state.since+durations[state.mode];
      enter(state.mode==='think'?'speak':'idle',end);
    }
    return {...state,warmth:warmth(now)};
  }
  function send(action,now) {
    read(now);
    switch(action) {
      case 'press':
        if(state.online&&!state.held){state.held=true;enter('listen',now);}break;
      case 'release':
        if(state.held){state.held=false;enter(now-state.since>=100?'think':'idle',now);}break;
      case 'cancel':
        if(state.held){state.held=false;enter('idle',now);}break;
      case 'tap':case 'stroke':
        now=Math.max(now,warmthAt);warmthLevel=Math.min(100,warmth(now)+(action==='stroke'?35:10));warmthAt=now;
        if(!['listen','think','speak','confused'].includes(state.mode))enter(action==='tap'?'curious':'happy',now);break;
      case 'disconnect':
        if(state.online){state.online=false;state.held=false;enter('confused',now);}break;
      case 'reconnect':
        state.online=true;
        if(state.mode==='confused')enter('idle',now);break;
      default:throw new Error('Unknown action');
    }
    return read(now);
  }
  function select(mode,now) {
    if(!modes.has(mode))throw new Error('Unknown mode');
    state.held=false;enter(mode,now);return read(now);
  }
  return {send,select,read};
}
