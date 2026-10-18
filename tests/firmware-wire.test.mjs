import { test } from 'node:test';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
test('firmware wire guards reject malformed frames and pairing keys', () => {
  const dir = mkdtempSync(join(tmpdir(), 'grok-wire-'));
  try {
    writeFileSync(join(dir, 'test.cpp'), `#include "grok_wire.h"\n#include <cassert>\n#include <cstring>\nint main(){uint8_t b[4];grok_put32(b,0x12345678);assert(grok_u32(b)==0x12345678);assert(!grok_valid_frame(4,3));assert(!grok_valid_frame(32769,3));assert(!grok_valid_frame(8,9));assert(!grok_valid_audio(3));assert(!grok_valid_audio(642));assert(grok_valid_audio(640));uint8_t key[32];char text[66];memset(text,'a',64);text[64]=0;assert(grok_key(text,key));assert(key[0]==170);text[5]='z';assert(!grok_key(text,key));return 0;}`);
    execFileSync('/usr/bin/c++', ['-std=c++17', '-I', resolve('firmware'), join(dir, 'test.cpp'), '-o', join(dir, 'test')]);
    execFileSync(join(dir, 'test'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
