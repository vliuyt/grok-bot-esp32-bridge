import {test} from 'node:test';
import assert from 'node:assert/strict';
import {CaptionBook} from '../src/caption-book.mjs';
import {encodePetMessage,petMessage} from '../src/pet-protocol.mjs';
const read=book=>{const opened=book.open();return {...opened,text:Array.from({length:opened.pages},(_,page)=>{
 const result=book.page({version:opened.version,page});
 assert.ok(Buffer.byteLength(result.text)<=300);
 const message={op:'caption_page',generation:0xffffffff,turn:0xffffffff,request:0xffffffff,...result};
 const bytes=encodePetMessage(message);assert.ok(bytes.length<=1024);assert.deepEqual(petMessage(bytes),message);
 return result.text;
}).join('')};};
test('long native items replace in place and a reader freezes exactly one version',()=>{
 const book=new CaptionBook();book.start(7,3);
 const story='苹果故事：“你好”\\🙂\n'.repeat(100);
 book.receive({turn:3,itemId:'a',text:'partial'});book.receive({turn:3,itemId:'b',text:'结尾'});
 book.receive({turn:3,itemId:'a',text:story});assert.equal(read(book).text,story+'\n结尾');
 const frozen=book.open(),first=book.page({version:frozen.version,page:0}).text;
 book.receive({turn:3,itemId:'a',text:'新内容'});assert.equal(book.page({version:frozen.version,page:0}).text,first);
 const next=read(book);assert.equal(next.text,'新内容\n结尾');assert.notEqual(next.version,frozen.version);
 assert.throws(()=>book.page({version:frozen.version,page:0}),/STALE/);
 book.clear();assert.equal(book.receive({turn:3,itemId:'a',text:'old'}),false);assert.throws(()=>book.open(),/UNAVAILABLE/);
 book.start(8,4);assert.equal(book.receive({turn:3,itemId:'a',text:'old'}),false);
 book.receive({turn:4,itemId:'a',text:'new'});assert.ok(book.open().version>next.version);
});
test('UTF-8 byte cap and JSON escapes stay bounded and truncation is explicit',()=>{
 const book=new CaptionBook();book.start(1,1);
 book.receive({turn:1,itemId:'a',text:'🙂'.repeat(10000)});
 const result=read(book);assert.equal(Buffer.byteLength(result.text),24576);assert.equal(result.truncated,true);
 assert.equal(result.text,'🙂'.repeat(6144));
 book.start(1,2);const escapes='"\\\n\t中🙂'.repeat(600);
 book.receive({turn:2,itemId:'a',text:escapes});assert.equal(read(book).text,escapes);
 for(let i=0;i<20;i++)book.open();assert.ok(book.open().version>20);
});
test('normalization preserves line breaks and tabs and rejects malformed Unicode',()=>{
 const book=new CaptionBook();book.start(1,1);
 book.receive({turn:1,itemId:'a',text:'甲\r\n乙\r丙\x00\x01\x1f\t丁'});assert.equal(read(book).text,'甲\n乙\n丙\t丁');
 assert.throws(()=>book.receive({turn:1,itemId:'b',text:'\ud800'}),/UTF8/);
 assert.throws(()=>book.receive({turn:1,itemId:'b',text:Buffer.from([0xff])}),/UTF8/);
 book.clear();book.start(1,2);book.receive({turn:2,itemId:'a',text:''});assert.throws(()=>book.open(),/UNAVAILABLE/);
});
