const LIMIT=24*1024;
const decoder=new TextDecoder('utf-8',{fatal:true});
function normalize(input){
 let text;
 try{text=Buffer.isBuffer(input)?decoder.decode(input):input;if(typeof text!=='string'||!text.isWellFormed())throw new Error();}
 catch{throw new Error('CAPTION_UTF8');}
 return text.replace(/\r\n?/g,'\n').replace(/[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]/g,'');
}
function prefix(text,budget){
 let bytes=0,result='';
 for(const point of text){const length=Buffer.byteLength(point);if(bytes+length>budget)break;result+=point;bytes+=length;}
 return result;
}
export class CaptionBook{
 #items=new Map();#turn=null;#version=0;#snapshot=null;#overflow=false;#responding=false;
 start(generation,turn){this.clear();this.generation=generation;this.#turn=turn;this.#responding=true;}
 clear(){this.#items.clear();this.#snapshot=null;this.#turn=null;this.#overflow=false;this.#responding=false;}
 receive({turn,itemId='current',text}){
  if(this.#turn===null||turn!==this.#turn)return false;
  if(typeof itemId!=='string'||!itemId.length||Buffer.byteLength(itemId)>256)return false;
  const normalized=normalize(text);
  if(!this.#items.has(itemId)&&this.#items.size>=256){this.#overflow=true;return false;}
  const other=[...this.#items].filter(([id])=>id!==itemId).map(([,item])=>item.text).filter(Boolean);
  const budget=LIMIT-Buffer.byteLength(other.join('\n'))-(other.length&&normalized.length?1:0);
  const value=prefix(normalized,Math.max(0,budget));
  this.#items.set(itemId,{text:value,truncated:value!==normalized});this.#responding=true;
  return true;
 }
 finish(){this.#responding=false;}
 #body(){return [...this.#items.values()].map(item=>item.text).filter(Boolean).join('\n');}
 catalog(){return {available:[...this.#items.values()].some(item=>item.text.length>0),truncated:this.#overflow||[...this.#items.values()].some(item=>item.truncated),responding:this.#responding};}
 open(){
  const text=this.#body();if(!text)throw new Error('CAPTION_UNAVAILABLE');
  // Exhaustion must never make an old snapshot's identity valid again.
  if(this.#version===0xffffffff)throw new Error('CAPTION_UNAVAILABLE');
  const parts=[];let part='',bytes=0;
  for(const point of text){const size=Buffer.byteLength(point);if(bytes+size>300){parts.push(part);part='';bytes=0;}part+=point;bytes+=size;}
  if(part)parts.push(part);
  const {truncated,responding}=this.catalog();
  this.#snapshot={version:++this.#version,parts,truncated,responding};
  return {version:this.#version,pages:parts.length,truncated,responding};
 }
 page({version,page}){
  const snapshot=this.#snapshot;
  if(!snapshot||version!==snapshot.version)throw new Error('CAPTION_STALE');
  if(!Number.isInteger(page)||page<0||page>=snapshot.parts.length)throw new Error('CAPTION_UNAVAILABLE');
  return {version,page,pages:snapshot.parts.length,text:snapshot.parts[page],truncated:snapshot.truncated,responding:snapshot.responding};
 }
}
