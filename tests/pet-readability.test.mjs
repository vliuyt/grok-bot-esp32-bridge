import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { spawnSync } from 'node:child_process';

test('small-screen presentation keeps text current and animated face inside its safe area', () => {
  const dir = mkdtempSync(join(tmpdir(), 'pet-readable-'));
  try {
    const sources = ['tests/pet_readability_harness.c', ...['pet_readability', 'pet_caption', 'pet_face', 'pet_core'].map(n => `firmware/pet/components/pet_core/${n}.c`)];
    const build = spawnSync('cc', ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Ifirmware/pet/components/pet_core', ...sources, '-lm', '-o', join(dir, 'test')], { encoding: 'utf8' });
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(join(dir, 'test'), [], { encoding: 'utf8' });
    assert.equal(run.status, 0, run.stderr);
    assert.match(run.stdout, /PET_READABILITY_OK/);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
