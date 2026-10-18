import { test } from 'node:test';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
test('synthetic fixture chunks audio and binds reused acceptance to its firmware', () => {
  execFileSync('python3', [fileURLToPath(new URL('./fixture_audio_test.py', import.meta.url))]);
});
test('public install validates partition/OTA layout and has no private baseline dependency', () => {
  execFileSync('python3', [fileURLToPath(new URL('./public_install_test.py', import.meta.url))]);
});
test('service setup validates first and rolls back failed bootstrap with a custom Bot', () => {
  execFileSync('python3', [fileURLToPath(new URL('./service_install_test.py', import.meta.url))]);
});
