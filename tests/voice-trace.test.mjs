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
  trace.received({type:'error',error:{type:'authentication_error',code:'internal_error',message:'secret'}},1);
  const result=trace.snapshot();
  assert.equal(result.events[0].errorType,'invalid_request_error');assert.equal(result.events[0].errorCode,'input_audio_buffer_commit_empty');
  assert.equal(result.events[1].errorType,'other');assert.equal(result.events[1].errorCode,'other');
  assert.equal(result.events[2].errorType,'authentication_error');assert.equal(result.events[2].errorCode,'internal_error');
  assert(!JSON.stringify(result).includes('secret'));assert(!JSON.stringify(result).includes('private'));
});

test('idle ASR errors retain only a bounded category and time since the last input',()=>{
  let now=100;const trace=new VoiceTrace({now:()=>now});
  now=900100;
  trace.received({type:'error',error:{type:'server_error',code:'upstream_error',message:'ASR stream timed out'}},0);
  assert.equal(trace.snapshot().events[0].errorClass,'asr_timeout');
  assert.equal(trace.snapshot().events[0].inputIdleMs,900000);
  now=900200;trace.sent({type:'input_audio_buffer.append',audio:'private-pcm'},1);
  now=1800200;
  trace.received({type:'error',error:{type:'server_error',message:'ASR stream timed out: private upstream detail'}},1);
  assert.equal(trace.snapshot().events[1].inputIdleMs,900000);
  for(const message of ['tool request timed out','ASR stream timed out'+'.'.repeat(600),{private:'text'}]){
    trace.received({type:'error',error:{type:'server_error',message}},1);
    assert.equal(trace.snapshot().events.at(-1).errorClass,'other');
  }
  const serialized=JSON.stringify(trace.snapshot());
  for(const secret of ['private','ASR stream','upstream_error'])assert(!serialized.includes(secret));
});

test('input-idle telemetry clamps invalid clock differences without retaining audio',()=>{
  let now=100;const trace=new VoiceTrace({now:()=>now});
  for(const [at,want] of [[99,0],[NaN,0],[Infinity,0],[100+0x100000000,0xffffffff]]){
    now=at;trace.received({type:'error',error:{}},0);
    assert.equal(trace.snapshot().events.at(-1).inputIdleMs,want);
  }
});

test('unknown failures expose fixed diagnostic signals even when their message is long',()=>{
  const trace=new VoiceTrace();
  trace.received({type:'error',error:{type:'server_error',code:'private-code',message:'private-detail '.repeat(60)+'ASR upstream connection deadline exceeded'}},0);
  const event=trace.snapshot().events[0];
  assert.equal(event.messageSize,'long');
  assert.deepEqual(event.errorSignals,['timeout','asr','transport','limit']);
  trace.received({type:'error',error:{code:'session_expired',message:'private credential expired'}},0);
  assert.deepEqual(trace.snapshot().events[1].errorSignals,['auth','session']);
  trace.received({type:'error',error:{message:'ASR '.repeat(5000)}},0);
  assert.equal(trace.snapshot().events[2].messageSize,'oversize');
  assert.deepEqual(trace.snapshot().events[2].errorSignals,[]);
  const serialized=JSON.stringify(trace.snapshot());
  assert(!serialized.includes('private'));assert(!serialized.includes('ASR upstream'));
});

test('mutating returned error signals cannot inject content into future trace snapshots',()=>{
  const trace=new VoiceTrace();trace.received({type:'error',error:{message:'ASR stream timed out'}},0);
  const first=trace.snapshot();first.events[0].errorSignals.push('private content');first.events[0].errorSignals[0]='private replacement';
  assert.deepEqual(trace.snapshot().events[0].errorSignals,['timeout','asr','transport']);
  assert(!JSON.stringify(trace.snapshot()).includes('private'));
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
