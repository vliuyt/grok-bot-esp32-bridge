import { test } from 'node:test';
import assert from 'node:assert/strict';
import { startControlServer } from '../src/control.mjs';

test('local control rejects cross-origin calls and serializes press then release', async () => {
  const commands = [];
  const server = await startControlServer({ command: async name => { commands.push(name); }, snapshot: () => ({ state: 'ready', bot: '小糖' }) });
  try {
    const origin = `http://127.0.0.1:${server.port}`;
    const page = await (await fetch(origin)).text();
    const token = page.match(/const controlToken = "([a-f0-9]+)"/)[1];
    assert.doesNotMatch(page, /accessToken|clientSecret/);
    const refused = await fetch(`${origin}/command`, { method: 'POST', headers: { origin: 'https://attacker.invalid', 'x-bridge-control': token }, body: '{"action":"press"}' });
    assert.equal(refused.status, 403); assert.equal(commands.length, 0);
    for (const action of ['press', 'release']) {
      const response = await fetch(`${origin}/command`, { method: 'POST', headers: { origin, 'x-bridge-control': token }, body: JSON.stringify({ action }) });
      assert.equal(response.status, 200);
    }
    assert.deepEqual(commands, ['press', 'release']);
  } finally { await server.close(); }
});
test('navigating a device panel to setup does not implicitly pause its bridge',async()=>{
 const server=await startControlServer({command:async()=>{},snapshot:()=>({device:true})});
 try{const page=await(await fetch(`http://127.0.0.1:${server.port}`)).text();const handler=page.match(/window.addEventListener\('pagehide',\(\)=>\{([^}]+)\}/)[1];assert.doesNotMatch(handler,/hangup/);}finally{await server.close();}
});
