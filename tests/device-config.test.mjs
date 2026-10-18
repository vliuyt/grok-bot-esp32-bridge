import { test } from 'node:test';
import assert from 'node:assert/strict';
import { chooseLanAddress } from '../src/device-config.mjs';
test('selects the available physical LAN interface and excludes tunnels', () => {
  assert.equal(chooseLanAddress({ en1: [{ family: 'IPv4', internal: false, address: '192.168.31.21' }], utun5: [{ family: 'IPv4', internal: false, address: '198.18.0.1' }] }), '192.168.31.21');
  assert.throws(() => chooseLanAddress({}), /LAN_ADDRESS_REQUIRED/);
  assert.throws(() => chooseLanAddress({ en0: [{ family: 'IPv4', internal: false, address: '10.0.0.2' }], en1: [{ family: 'IPv4', internal: false, address: '10.0.1.2' }] }), /LAN_ADDRESS_AMBIGUOUS/);
});
test('USB recovery refreshes the Mac LAN address while preserving pairing identity',async()=>{
 const {pairingConfig}=await import('../src/device-config.mjs');const config={host:'192.168.1.20',key:'a'.repeat(64),port:8790};
 const next=pairingConfig(config,{en0:[{family:'IPv4',internal:false,address:'192.168.1.99'}]});assert.equal(next.host,'192.168.1.99');assert.equal(next.key,config.key);assert.equal(config.host,'192.168.1.20');
});
