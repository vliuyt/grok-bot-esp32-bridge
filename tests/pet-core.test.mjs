import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
test('portable pet: offline touch, voice generations, sleep and bounded greetings', () => {
  const temp=mkdtempSync(join(tmpdir(),'grok-pet-core-'));
  try {
    execFileSync('/usr/bin/cc',['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g',
      '-Ifirmware/pet/components/pet_core','tests/pet_core_harness.c','firmware/pet/components/pet_core/pet_core.c','-lm','-o',join(temp,'pet')],{stdio:'pipe'});
    const result=execFileSync(join(temp,'pet'),{encoding:'utf8'});
    assert.match(result,/PET_CORE_OK/);
  } finally { rmSync(temp,{recursive:true,force:true}); }
});
