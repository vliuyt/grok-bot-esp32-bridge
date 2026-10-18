import { test } from 'node:test';
import assert from 'node:assert/strict';
import { Backend } from '../src/backend.mjs';

test('uses the authenticated account for native RPC without redirecting credentials', async () => {
  let observed;
  const backend = new Backend({ accessToken: 'fixture-secret', teamId: '42' }, { fetch: async (url, options) => {
    observed = { url, options }; return Response.json({ model: 'fixture-voice' });
  } });
  assert.deepEqual(await backend.rpc('MintSandVoiceCallSecret', {}), { model: 'fixture-voice' });
  assert.equal(observed.url, 'https://api2.cursor.sh/aiserver.v1.GrokBotService/MintSandVoiceCallSecret');
  assert.equal(observed.options.redirect, 'error');
  assert.equal(observed.options.headers.authorization, 'Bearer fixture-secret');
  assert.equal(observed.options.headers['x-cursor-team-id'], '42');
});
test('reports authentication failures without echoing server text or credentials', async () => {
  let calls = 0;
  const backend = new Backend({ accessToken: 'fixture-secret' }, { fetch: async () => {
    calls++; return Response.json({ message: 'fixture-secret should not appear', code: 'unauthenticated' }, { status: 401 });
  } });
  await assert.rejects(backend.rpc('MintSandVoiceCallSecret', {}), error =>
    error.code === 'AUTH_REQUIRED' && !error.message.includes('fixture-secret'));
  assert.equal(calls, 1);
});
test('rejects a gateway outside expected HTTPS origins before sending credentials', async () => {
  const backend = new Backend({ accessToken: 'secret' }, { fetch: async () => Response.json({ gatewayUrl: 'https://attacker.invalid', gatewayToken: 'secret' }) });
  await assert.rejects(backend.connectGateway(), /gateway origin/i);
});
test('does not retry an uncertain task submission', async () => {
  let calls = 0;
  const backend = new Backend({}, { fetch: async () => { calls++; throw new Error('network down'); } });
  backend.gateway = { url: 'https://example.cursorvm.com', token: 'fixture' };
  await assert.rejects(backend.gatewayCall('sendPrompt', { agentId: 'one', prompt: 'hello' }), error => error.code === 'DELIVERY_UNKNOWN');
  assert.equal(calls, 1);
});
test('preserves broker gateway path and network token when fetching the roster', async () => {
  let request;
  const backend = new Backend({ accessToken: 'fixture' }, { fetch: async (url, options) => {
    if (url.includes('/aiserver.')) return Response.json({ gatewayUrl: 'https://box.cursorvm.com/forward/8787/', gatewayToken: 'gateway-fixture', networkToken: 'network-fixture' });
    request = { url, options }; return Response.json([{ id: 'one', name: '小糖' }]);
  } });
  assert.deepEqual(await backend.listBots(), [{ id: 'one', name: '小糖', description: '' }]);
  assert.equal(request.url, 'https://box.cursorvm.com/forward/8787/api/listAgents');
  assert.equal(request.options.headers['x-anyrun-network-token'], 'network-fixture');
});
test('native event stream preserves gateway binding and validates the content type', async () => {
  let request;
  const backend = new Backend({}, { fetch: async (url, options) => { request={url,options}; return new Response('data: {}\n\n',{headers:{'content-type':'text/event-stream'}}); } });
  backend.gateway={url:'https://box.cursorvm.com/prefix',token:'fixture',networkToken:'net-fixture'};
  const body=await backend.openEvents(new AbortController().signal); await body.cancel();
  assert.equal(request.url,'https://box.cursorvm.com/prefix/events');assert.equal(request.options.redirect,'error');
  assert.equal(request.options.headers['x-anyrun-network-token'],'net-fixture');
  const rejected=new Backend({}, {fetch:async()=>Response.json({})});rejected.gateway=backend.gateway;
  await assert.rejects(rejected.openEvents(new AbortController().signal),error=>error.code==='TASK_RETURN_UNAVAILABLE');
});
test('renews expired gateway binding once for events, without retrying tasks', async () => {
  const calls=[];
  const backend=new Backend({}, {fetch:async(url)=>{
    calls.push(url);
    if(url.includes('/aiserver.'))return Response.json({gatewayUrl:'https://fresh.cursorvm.com',gatewayToken:'fresh'});
    if(url.startsWith('https://stale.'))return new Response('',{status:417});
    return new Response('data: {}\n\n',{headers:{'content-type':'text/event-stream'}});
  }});
  backend.gateway={url:'https://stale.cursorvm.com',token:'stale'};
  await (await backend.openEvents(new AbortController().signal)).cancel();
  assert.equal(calls.length,3);assert.equal(backend.gateway.token,'fresh');
  calls.length=0;backend.gateway={url:'https://stale.cursorvm.com',token:'stale'};
  await assert.rejects(backend.gatewayCall('sendPrompt',{}),error=>error.status===417);
  assert.equal(calls.length,1);
});
test('gateway read renewal is bounded and network failures never replay', async()=>{
  let calls=0;
  const backend=new Backend({}, {fetch:async(url)=>{
    calls++;
    if(url.includes('/aiserver.'))return Response.json({gatewayUrl:'https://box.cursorvm.com',gatewayToken:'new'});
    return new Response('',{status:417});
  }});
  backend.gateway={url:'https://box.cursorvm.com',token:'old'};
  await assert.rejects(backend.listBots(),error=>error.status===417);
  assert.equal(calls,3);
  calls=0;
  await assert.rejects(backend.openEvents(new AbortController().signal),error=>error.status===417);
  assert.equal(calls,3);
  let networkCalls=0;
  const offline=new Backend({}, {fetch:async()=>{networkCalls++;throw new Error('offline');}});
  offline.gateway=backend.gateway;
  await assert.rejects(offline.listBots(),error=>error.code==='NETWORK');assert.equal(networkCalls,1);
});
test('hangup aborts the in-flight gateway renewal for native events',async()=>{
  const controller=new AbortController();let renewing;
  const started=new Promise(resolve=>{renewing=resolve;});
  let aborted=false;
  const backend=new Backend({}, {fetch:async(url,options)=>{
    if(!url.includes('/aiserver.'))return new Response('',{status:417});
    renewing();
    return new Promise((resolve,reject)=>options.signal.addEventListener('abort',()=>{aborted=true;reject(new Error('aborted'));},{once:true}));
  }});
  backend.gateway={url:'https://box.cursorvm.com',token:'old'};
  const opening=backend.openEvents(controller.signal);
  await started;controller.abort();
  try { await Promise.race([opening,new Promise((_,reject)=>setTimeout(()=>reject(new Error('NOT_CANCELLED')),100))]); }
  catch(error){assert.notEqual(error.message,'NOT_CANCELLED');}
  assert.equal(aborted,true);
});
