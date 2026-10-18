import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { networkInterfaces } from 'node:os';
import { randomBytes } from 'node:crypto';
import { bridgeIdentity } from './pet-protocol.mjs';
export function chooseLanAddress(interfaces) {
  const addresses = Object.entries(interfaces).filter(([name]) => /^en\d+$/.test(name)).flatMap(([, entries]) => entries)
    .filter(item => item.family === 'IPv4' && !item.internal && /^(10\.|192\.168\.|172\.(1[6-9]|2\d|3[01])\.)/.test(item.address)).map(item => item.address);
  if (addresses.length !== 1) throw new Error(addresses.length ? 'LAN_ADDRESS_AMBIGUOUS' : 'LAN_ADDRESS_REQUIRED');
  return addresses[0];
}
export async function deviceConfig() {
  const path = new URL('../.local/device.json', import.meta.url);
  try {
    const config = JSON.parse(await readFile(path, 'utf8'));
    if (!/^[0-9a-f]{64}$/.test(config.key) || !Number.isInteger(config.port) || config.port < 1024 || config.port > 65535 || !/^\d{1,3}(\.\d{1,3}){3}$/.test(config.host)) throw new Error('PAIRING_CONFIG');
    return {...config,bridgeId:bridgeIdentity(config.key)};
  } catch (error) {
    if (error.code !== 'ENOENT') throw error;
    const address = chooseLanAddress(networkInterfaces());
    const config = { host: address, port: 8790, key: randomBytes(32).toString('hex') };
    await mkdir(new URL('../.local/', import.meta.url), { recursive: true });
    await writeFile(path, JSON.stringify(config), { mode: 0o600, flag: 'wx' }); return {...config,bridgeId:bridgeIdentity(config.key)};
  }
}

export function pairingConfig(config,interfaces=networkInterfaces()){return {host:chooseLanAddress(interfaces),port:config.port,key:config.key};}
