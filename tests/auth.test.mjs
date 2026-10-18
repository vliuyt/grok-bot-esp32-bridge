import { test } from 'node:test';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import { decryptSafeStorage, readActiveAccount } from '../src/auth.mjs';

const password = 'fixture-password';
function seal(text) {
  const key = crypto.pbkdf2Sync(password, 'saltysalt', 1003, 16, 'sha1');
  const cipher = crypto.createCipheriv('aes-128-cbc', key, Buffer.alloc(16, 32));
  return Buffer.concat([Buffer.from('v10'), cipher.update(text), cipher.final()]).toString('base64');
}
function store(token, extra = {}) {
  return { version: 1, 'cursor-machine-id': seal('fixture-machine'),
    'cursor-accounts': JSON.stringify({ active: 'account-a', accounts: {
      'account-a': { 'cursor-access-token': seal(token), ...extra },
      'account-b': { 'cursor-access-token': seal('never-select-this') }
    } }) };
}
const token = `header.${Buffer.from(JSON.stringify({ sub: 'fixture-user', exp: 2100000000 })).toString('base64url')}.signature`;

test('decrypts Chromium macOS v10 ciphertext without plaintext fallback', () => {
  assert.equal(decryptSafeStorage(seal('hello'), password), 'hello');
  assert.throws(() => decryptSafeStorage(Buffer.from('secret').toString('base64'), password), /format/i);
});
test('selects only the active account and keeps secrets out of JSON diagnostics', () => {
  const auth = readActiveAccount(store(token, { 'cursor-selected-team-id': seal('42') }), password, { now: 1800000000000 });
  assert.equal(auth.accessToken, token);
  assert.equal(auth.teamId, '42');
  assert.equal(auth.machineId, 'fixture-machine');
  assert.doesNotMatch(JSON.stringify(auth), /fixture-user|signature|fixture-machine/);
});
test('refuses expired authentication instead of refreshing the App store', () => {
  assert.throws(() => readActiveAccount(store(token), password, { now: 2200000000000 }), /expired/i);
});
test('refuses ambiguous or missing active account rather than choosing another account', () => {
  const data = store(token); const accounts = JSON.parse(data['cursor-accounts']); accounts.active = null;
  data['cursor-accounts'] = JSON.stringify(accounts);
  assert.throws(() => readActiveAccount(data, password), /active account/i);
});
