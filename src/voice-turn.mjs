// A single turn owns these timers. No transcript or recording is retained here.
export function createTurnMonitor({now=Date.now,setTimer=setTimeout,clearTimer=clearTimeout,onStatus=()=>{},onExpire=()=>{}}={}){
  let active,closed=false;
  const clear=turn=>{
    if(!active||active.turn!==turn)return;
    clearTimer(active.wait);clearTimer(active.expiry);active=undefined;
  };
  const arm=state=>{
    clearTimer(state.expiry);
    state.expiry=setTimer(()=>{
      if(active!==state||closed)return;
      clear(state.turn);onStatus({turn:state.turn,code:'no_reply'});onExpire(state.turn);
    },state.tools?60000:30000);
    state.expiry?.unref?.();
  };
  return{
    start(turn){
      if(closed)return;
      if(active)clear(active.turn);
      const state=active={turn,tools:0,at:now()};
      state.wait=setTimer(()=>{if(active===state&&!closed)onStatus({turn,code:state.tools?'working':'waiting'});},12000);
      state.wait?.unref?.();arm(state);
    },
    note(turn,kind){
      const state=active;if(!state||state.turn!==turn||closed)return;
      if(!['recognized','response','audio','tool_started','tool_completed','done'].includes(kind))return;
      if(kind==='tool_started'){state.tools++;onStatus({turn,code:'working'});}
      if(kind==='tool_completed'&&state.tools)state.tools--;
      if(kind==='done'&&!state.tools){clear(turn);return;}
      if(kind==='audio'){clearTimer(state.wait);state.wait=undefined;}
      state.at=Math.max(state.at,now());arm(state);
    },
    clear,
    close(){if(active)clear(active.turn);closed=true;}
  };
}
