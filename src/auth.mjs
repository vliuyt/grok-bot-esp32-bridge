import crypto from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { homedir } from 'node:os';
import { join } from 'node:path';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';

const run = promisify(execFile);
export function decryptSafeStorage(encoded, password) {
  const bytes = Buffer.from(encoded, 'base64');
  if (bytes.length < 19 || bytes.subarray(0, 3).toString() !== 'v10') throw new Error('Unsupported secure storage format');
  const key = crypto.pbkdf2Sync(password, 'saltysalt', 1003, 16, 'sha1');
  try {
    const decipher = crypto.createDecipheriv('aes-128-cbc', key, Buffer.alloc(16, 32));
    return Buffer.concat([decipher.update(bytes.subarray(3)), decipher.final()]).toString('utf8');
  } finally { key.fill(0); }
}

export function readActiveAccount(data, password, { now = Date.now() } = {}) {
  if (data.version !== 1) throw new Error('Unsupported App credential store version');
  const accounts = JSON.parse(data['cursor-accounts'] ?? '{}');
  const selected = typeof accounts.active === 'string' ? accounts.accounts?.[accounts.active] : undefined;
  if (!selected?.['cursor-access-token']) throw new Error('No active account; sign in in Grok Bot first');
  const accessToken = decryptSafeStorage(selected['cursor-access-token'], password);
  let claims;
  try { claims = JSON.parse(Buffer.from(accessToken.split('.')[1], 'base64url').toString()); }
  catch { throw new Error('Invalid App access token'); }
  if (typeof claims.exp !== 'number' || claims.exp * 1000 <= now + 30000) throw new Error('App login expired; open Grok Bot and sign in normally');
  const teamId = selected['cursor-selected-team-id'] ? decryptSafeStorage(selected['cursor-selected-team-id'], password) : undefined;
  const machineId = data['cursor-machine-id'] ? decryptSafeStorage(data['cursor-machine-id'], password) : undefined;
  const result = {};
  Object.defineProperties(result, {
    accessToken: { value: accessToken }, teamId: { value: teamId }, machineId: { value: machineId },
    accountScope: { value: accounts.active }, toJSON: { value: () => ({ authenticated: true }) }
  });
  return result;
}

export async function loadAuth() {
  if (process.platform !== 'darwin') throw new Error('This prototype supports macOS only');
  const path = join(homedir(), 'Library/Application Support/Grok Bot/sand-secrets.json');
  let data;
  try { data = JSON.parse(await readFile(path, 'utf8')); }
  catch { throw new Error('Grok Bot login store unavailable; open the App and sign in'); }
  let password;
  try {
    const result = await run('/usr/bin/security', ['find-generic-password', '-w', '-s', 'Grok Bot Safe Storage'],
      { encoding: 'utf8', timeout: 60000, maxBuffer: 4096 });
    password = result.stdout.trimEnd();
  } catch { throw new Error('Keychain access unavailable; allow normal access to Grok Bot Safe Storage'); }
  try { return readActiveAccount(data, password); }
  catch (error) {
    if (/expired|active account|version/.test(error.message)) throw error;
    throw new Error('Cannot read active Grok Bot login; keep the App signed in');
  } finally { password = undefined; }
}
