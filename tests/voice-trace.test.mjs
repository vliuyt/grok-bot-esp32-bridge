import {test} from 'node:test';
import assert from 'node:assert/strict';
import {VoiceTrace,voiceTraceAliasKey} from '../src/voice-trace.mjs';

test('retained alias keys conceal original identifiers and cannot be linked between sessions',()=>{
  const key=Buffer.alloc(32,1),otherKey=Buffer.alloc(32,2),id='private-item';
  const digest=voiceTraceAliasKey(id,key);
  assert.match(digest,/^[a-f0-9]{64}$/);assert(!digest.includes(id));
  assert.equal(digest,voiceTraceAliasKey(id,key));
  assert.notEqual(digest,voiceTraceAliasKey(id,otherKey));
  assert.notEqual(voiceTraceAliasKey('\ud800',key),voiceTraceAliasKey('\ud801',key));
  assert.equal(voiceTraceAliasKey('x'.repeat(257),key),null);
});

test('trace distinguishes commit, partial recognition, final recognition and response without retaining content',()=>{
  let now=100;const trace=new VoiceTrace({now:()=>now});
  trace.sent({type:'input_audio_buffer.commit'},1);
  now=120;trace.received({type:'input_audio_buffer.committed',item_id:'private-item'},1);
  trace.received({type:'conversation.item.input_audio_transcription.completed',item_id:'private-item',status:'in_progress',transcript:'private speech'},1);
  now=150;trace.received({type:'conversation.item.input_audio_transcription.completed',item_id:'private-item',status:'completed',transcript:'private speech'},1);
  trace.received({type:'response.created',response:{id:'private-response',status:'in_progress',metadata:{private:'secret'}}},1);
  const result=trace.snapshot();
  assert.deepEqual(result.events.map(e=>e.event),['client.input_audio_buffer.commit','input_audio_buffer.committed','conversation.item.input_audio_transcription.completed','conversation.item.input_audio_transcription.completed','response.created']);
  assert.equal(result.events[1].item,1);assert.equal(result.events[2].item,1);
  assert.equal(result.events[2].status,'in_progress');assert.equal(result.events[3].status,'completed');
  assert.equal(result.events[4].response,2);assert.equal(result.events[3].ms,50);
  assert(!JSON.stringify(result).includes('private'));assert(!JSON.stringify(result).includes('secret'));
});

test('trace saves only predefined error categories and omits unknown messages, parameters and codes',()=>{
  const trace=new VoiceTrace();
  trace.received({type:'error',error:{type:'invalid_request_error',code:'input_audio_buffer_commit_empty',message:'secret token',param:'secret parameter'}},1);
  trace.received({type:'error',error:{type:'private type',code:'secret-token',message:'secret'}},1);
  const result=trace.snapshot();
  assert.equal(result.events[0].errorType,'invalid_request_error');assert.equal(result.events[0].errorCode,'input_audio_buffer_commit_empty');
  assert.equal(result.events[1].errorType,'other');assert.equal(result.events[1].errorCode,'other');
  assert(!JSON.stringify(result).includes('secret'));assert(!JSON.stringify(result).includes('private'));
});

test('audio payloads and unknown events are ignored, retained events and identifier aliases are bounded',()=>{
  const trace=new VoiceTrace();
  for(let i=0;i<1000;i++){
    trace.sent({type:'input_audio_buffer.append',audio:'private-audio'},1);
    trace.received({type:'response.output_audio.delta',delta:'private-audio'},1);
    trace.received({type:'private-event',text:'private-text'},1);
  }
  assert.equal(trace.snapshot().events.length,0);
  for(let i=0;i<200;i++)trace.received({type:'input_audio_buffer.committed',item_id:'private-'+i},1);
  const result=trace.snapshot();
  assert.equal(result.events.length,96);assert.equal(result.droppedEvents,104);
  assert.equal(result.events.at(-1).item,null);assert.equal(result.aliasLimitReached,true);
  assert(!JSON.stringify(result).includes('private'));
  result.events[0].event='mutated';assert.notEqual(trace.snapshot().events[0].event,'mutated');
});
