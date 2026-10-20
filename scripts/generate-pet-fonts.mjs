import {readFileSync,writeFileSync,mkdtempSync,mkdirSync,renameSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {tmpdir} from 'node:os';
import {dirname,join,resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const fontDirectory=join(root,'firmware/pet/components/pet_ui/fonts');
const hashes={noto:'7633f5a016d4dd95e685a69633d818aabc4644c4b08e26bd35b1b30c45ed5dda',unifont:'0e3981ab552231b5a2a870f2b61741903a4bf25c23ef5aeb05fdced1b3c7af4d'};
const sha=data=>createHash('sha256').update(data).digest('hex');
const options={};
for(let i=2;i<process.argv.length;i+=2)options[process.argv[i]]=process.argv[i+1];
let temporary;
try{
  for(const name of ['noto','unifont']){
    const path=options['--'+name];
    if(!path||sha(readFileSync(path))!==hashes[name])throw new Error('FONT_SOURCE_UNVERIFIED');
  }
  const converter=options['--converter'];
  if(!converter||execFileSync(process.execPath,[converter,'--version'],{encoding:'utf8'}).trim()!=='1.5.3')throw new Error('FONT_CONVERTER_VERSION_UNSUPPORTED');
  const chat=readFileSync(join(fontDirectory,'chat-characters.txt'),'utf8').trim();
  const ui=readFileSync(join(fontDirectory,'ui-characters.txt'),'utf8').trim();
  if([...new Set(chat)].length!==6763)throw new Error('FONT_CHAT_CHARSET_INVALID');
  const primary=new Set(chat);
  for(const [a,b]of[[32,126],[0x3000,0x301f],[0xff01,0xff65],[0x2010,0x203f]])for(let cp=a;cp<=b;cp++)primary.add(String.fromCodePoint(cp));
  primary.add('□');
  const rare=[];
  for(const [a,b]of[[0x3000,0x30ff],[0x4e00,0x9fff],[0xff00,0xffef]])for(let cp=a;cp<=b;cp++)if(!primary.has(String.fromCodePoint(cp)))rare.push(String.fromCodePoint(cp));
  const definitions=[
    {name:'pet_font_rare16',size:16,bpp:1,source:'unifont',symbols:rare.join(''),fallback:null},
    {name:'pet_font_28',size:28,bpp:2,source:'noto',symbols:[...primary].sort().join(''),fallback:'pet_font_rare16'},
    {name:'pet_font_30',size:30,bpp:2,source:'noto',symbols:[...primary].sort().join(''),fallback:'pet_font_28'},
    {name:'pet_font_22',size:22,bpp:2,source:'noto',symbols:[...new Set([...ui,...Array.from({length:95},(_,i)=>String.fromCharCode(i+32))])].sort().join(''),fallback:'pet_font_28'},
  ];
  temporary=mkdtempSync(join(tmpdir(),'pet-font-generation-'));
  const manifest={converter:'lv_font_conv@1.5.3',sources:hashes,chatCharacters:6763,fonts:[]};
  for(const d of definitions){
    const output=join(temporary,d.name+'.c');
    const args=[converter,'--font',options['--'+d.source],'--symbols',d.symbols,'--size',String(d.size),'--bpp',String(d.bpp),'--format','lvgl','--no-kerning','--lv-font-name',d.name,'-o',output];
    if(d.bpp===1)args.push('--no-compress');
    if(d.fallback)args.push('--lv-fallback',d.fallback);
    execFileSync(process.execPath,args,{stdio:'pipe',maxBuffer:2*1024*1024});
    let source=readFileSync(output,'utf8');
    const bitmap=source.match(/glyph_bitmap\[\] = \{([\s\S]*?)\n\};/)?.[1];
    if(!bitmap)throw new Error('FONT_OUTPUT_INVALID');
    const bytes=(bitmap.replace(/\/\*[\s\S]*?\*\//g,'').match(/0x[0-9a-fA-F]+/g)||[]).length;
    const lineHeight=Number(source.match(/\.line_height = (\d+)/)?.[1]);
    if(bytes>=0x100000||!lineHeight||lineHeight>(d.size===30?42:d.size===28?40:32))throw new Error('FONT_LAYOUT_OR_INDEX_LIMIT');
    const copyright=d.source==='noto'?'Copyright 2014-2021 Adobe.':'GNU Unifont 16.0.04; original copyright and OFL in README.md.';
    const header=`/* Xiaotang ${d.size}px / ${d.bpp}bpp font; lv_font_conv 1.5.3.\n * ${copyright}\n * SIL Open Font License 1.1; see README.md and preserved full license.\n * Source SHA-256: ${hashes[d.source]}\n */\n#include "pet_fonts.h"\n`;
    source=source.replace(/^\/\*\*+[\s\S]*?\*\/\s*/,header);
    writeFileSync(output,source);
    manifest.fonts.push({file:d.name+'.c',size:d.size,bpp:d.bpp,bitmapBytes:bytes,lineHeight,glyphs:(source.match(/\.bitmap_index =/g)||[]).length,characterSetSha256:sha(d.symbols),sha256:sha(source),fallback:d.fallback});
  }
  const output=resolve(options['--output']||fontDirectory);mkdirSync(output,{recursive:true});
  for(const d of definitions){const name=d.name+'.c';writeFileSync(join(output,name+'.tmp'),readFileSync(join(temporary,name)));renameSync(join(output,name+'.tmp'),join(output,name));}
  writeFileSync(join(output,'provenance.json'),JSON.stringify(manifest,null,2)+'\n');
  console.log(JSON.stringify(manifest));
}catch(error){console.error(error.message);process.exitCode=1;}
finally{if(temporary)rmSync(temporary,{recursive:true,force:true});}
