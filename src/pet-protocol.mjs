import { createHash } from 'node:crypto';
import { isIP } from 'node:net';
const utf8=new TextDecoder('utf-8',{fatal:true});
const uint=value=>Number.isInteger(value)&&value>=0&&value<=0xffffffff;
export const TURN_STATUS_CODES=Object.freeze(['waiting','working','short_input','no_reply','empty_response','unrecognized','response_failed','auth_required','progress']);
export function shortCaption(text){return [...String(text).replace(/[\x00-\x1f\x7f]/g,' ').trim()].slice(0,80).join('');}
export function bridgeIdentity(key){
  if(typeof key!=='string'||!/^[a-f0-9]{64}$/i.test(key))throw new Error('PAIRING_CONFIG');
  return createHash('sha256').update('grokpet-bridge-id-v2').update(Buffer.from(key,'hex')).digest('hex').slice(0,32);
}
export function privateLanPeer(input){
  const host=String(input??'').replace(/^::ffff:/,'');
  if(host==='::1')return true;
  if(isIP(host)!==4)return false;
  const [a,b]=host.split('.').map(Number);
  return a===127||a===10||(a===192&&b===168)||(a===172&&b>=16&&b<=31);
}
export function petMessage(payload,{generation,turn}={}){
  if(!Buffer.isBuffer(payload)||payload.length>1024)throw new Error('PET_SIZE');
  let text;try{text=utf8.decode(payload);}catch{throw new Error('PET_UTF8');}
  if(!text.startsWith('{'))return null;
  let m;try{m=JSON.parse(text);}catch{throw new Error('PET_PROTOCOL');}
  if(!m||!uint(m.generation)||!m.generation)throw new Error('PET_PROTOCOL');
  if(generation!=null&&m.generation!==generation)return null;
  const common={op:m.op,generation:m.generation};
  switch(m.op){
    case 'hello':
      if(m.v!==2||(m.bridgeId!=null&&!/^[a-f0-9]{32}$/.test(m.bridgeId)))throw new Error('PET_PROTOCOL');
      if(m.capabilities!=null&&(!Array.isArray(m.capabilities)||m.capabilities.length>8||m.capabilities.some(c=>typeof c!=='string'||!/^[a-z_]{1,32}$/.test(c))||new Set(m.capabilities).size!==m.capabilities.length))throw new Error('PET_PROTOCOL');
      return {op:'hello',v:2,generation:m.generation,...(m.bridgeId?{bridgeId:m.bridgeId}:{}),...(m.capabilities?{capabilities:m.capabilities}:{})};
    case 'turn_status':
      if(!uint(m.turn)||!TURN_STATUS_CODES.includes(m.code))throw new Error('PET_PROTOCOL');
      if(turn!=null&&m.turn!==turn)return null;
      return {...common,turn:m.turn,code:m.code};
    case 'caption_catalog':
      if(!uint(m.turn)||!['available','responding','truncated'].every(key=>typeof m[key]==='boolean'))throw new Error('PET_PROTOCOL');
      if(turn!=null&&m.turn!==turn)return null;
      return {...common,turn:m.turn,available:m.available,responding:m.responding,truncated:m.truncated};
    case 'caption_read':case 'caption_page': {
      if(!uint(m.turn)||!uint(m.request)||!m.request||!uint(m.version)||!Number.isInteger(m.page)||m.page<0||m.page>65535)throw new Error('PET_PROTOCOL');
      if(turn!=null&&m.turn!==turn)return null;
      const identity={...common,turn:m.turn,request:m.request,version:m.version,page:m.page};
      if(m.op==='caption_read'){
        if(!m.version&&m.page!==0)throw new Error('PET_PROTOCOL');
        return identity;
      }
      if(m.error!=null){
        if(!['unavailable','stale','busy'].includes(m.error))throw new Error('PET_PROTOCOL');
        return {...identity,error:m.error};
      }
      if(!m.version||!Number.isInteger(m.pages)||m.pages<1||m.pages>65535||m.page>=m.pages||
        typeof m.text!=='string'||!m.text.isWellFormed()||Buffer.byteLength(m.text)>300||/[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]/.test(m.text)||
        typeof m.truncated!=='boolean'||typeof m.responding!=='boolean')throw new Error('PET_PROTOCOL');
      return {...identity,pages:m.pages,text:m.text,truncated:m.truncated,responding:m.responding};
    }
    case 'caption':
      if(!uint(m.turn)||typeof m.id!=='string'||!m.id.length||m.id.length>64||
         typeof m.text!=='string'||[...m.text].length>80||/[\x00-\x1f\x7f]/.test(m.text))throw new Error('PET_PROTOCOL');
      if(turn!=null&&m.turn!==turn)return null;
      return {...common,turn:m.turn,id:m.id,text:m.text};
    case 'clock':
      if(!Number.isSafeInteger(m.epochMs)||m.epochMs<1700000000000||m.epochMs>4102444800000)throw new Error('PET_PROTOCOL');
      return {...common,epochMs:m.epochMs};
    case 'link':
      if(!['connecting','ready','unavailable'].includes(m.state))throw new Error('PET_PROTOCOL');
      return {...common,state:m.state};
    case 'task_channel':
      if(typeof m.available!=='boolean')throw new Error('PET_PROTOCOL');
      return {...common,available:m.available};
    default:throw new Error('PET_PROTOCOL');
  }
}
export function encodePetMessage(message){
  const payload=Buffer.from(JSON.stringify(message));
  if(!petMessage(payload))throw new Error('PET_PROTOCOL');
  return payload;
}
