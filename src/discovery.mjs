import { spawn } from 'node:child_process';
export function startDiscovery({port,bridgeId,spawnFactory=spawn}){
  if(!Number.isInteger(port)||port<1024||port>65535||!/^[a-f0-9]{32}$/.test(bridgeId))throw new Error('DISCOVERY_CONFIG');
  let child,retry,closing,stopped=false,running=false,failures=0,fault=null;
  function launch(){
    if(stopped)return;
    let current;
    const failed=()=>{
      if(child!==current)return;
      child=null;running=false;fault='DISCOVERY_UNAVAILABLE';
      if(!stopped){retry=setTimeout(launch,Math.min(30000,1000*2**Math.min(++failures,5)));retry.unref();}
    };
    try {
      current=spawnFactory('/usr/bin/dns-sd',['-R','GrokPet-'+bridgeId.slice(0,8),'_grokpet._tcp','local.',String(port),'v=2','id='+bridgeId],{stdio:'ignore'});
      child=current;
      current.once('spawn',()=>{if(child===current){running=true;fault=null;}});
      current.once('error',failed);current.once('exit',failed);
    }catch{child=current;failed();}
  }
  launch();
  return {status:()=>({running,fault}),close(){
    if(closing)return closing;
    stopped=true;running=false;clearTimeout(retry);
    closing=new Promise(resolve=>{
      const current=child;child=null;
      if(!current){resolve();return;}
      let killTimer;
      const done=()=>{clearTimeout(killTimer);resolve();};
      current.once('exit',done);current.once('error',done);
      killTimer=setTimeout(()=>{current.kill('SIGKILL');done();},2500);killTimer.unref();
      if(!current.kill('SIGTERM'))done();
    });
    return closing;
  }};
}
