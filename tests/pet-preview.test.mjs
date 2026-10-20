import {test} from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mkdtempSync,rmSync,readFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';

test('round face geometry distinguishes feedback without colliding with captions',()=>{
  const dir=mkdtempSync(join(tmpdir(),'pet-face-'));
  try {
    execFileSync('cc',['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
      '-Ifirmware/pet/components/pet_core','tests/pet_face_harness.c',
      'firmware/pet/components/pet_core/pet_core.c','firmware/pet/components/pet_core/pet_face.c','firmware/pet/components/pet_core/pet_caption.c',
      '-lm','-o',join(dir,'test')]);
    assert.match(execFileSync(join(dir,'test'),{encoding:'utf8'}),/PET_FACE_OK/);
  } finally {rmSync(dir,{recursive:true,force:true});}
});

test('preview touch never starts a call and interrupted simulations do not resume',async()=>{
  const {createController}=await import('../preview/companion/controller.mjs');
  const pet=createController();
  pet.send('tap',0);assert.equal(pet.read(1999).mode,'curious');
  assert.equal(pet.read(2000).mode,'idle');
  pet.send('stroke',2100);assert.equal(pet.read(3899).mode,'happy');
  assert.equal(pet.read(3900).mode,'idle');
  pet.send('press',4000);pet.send('tap',4010);
  assert.equal(pet.read(4010).mode,'listen');
  pet.send('release',4200);assert.equal(pet.read(4200).mode,'think');
  pet.send('tap',4250);assert.equal(pet.read(4250).mode,'think');
  assert.equal(pet.read(5400).mode,'speak');
  pet.send('press',5500);assert.equal(pet.read(5500).mode,'listen');
  pet.send('cancel',5600);assert.equal(pet.read(15000).mode,'idle');
  pet.send('release',15100);assert.equal(pet.read(15100).mode,'idle');
  pet.send('press',15200);pet.send('release',15250);
  assert.equal(pet.read(15250).mode,'idle');
});

test('preview offline feedback is bounded and petting remains available',async()=>{
  const {createController}=await import('../preview/companion/controller.mjs');
  const pet=createController();
  pet.send('disconnect',0);assert.equal(pet.read(0).mode,'confused');
  pet.send('disconnect',1000);assert.equal(pet.read(2000).mode,'idle');
  pet.send('press',2100);assert.equal(pet.read(2100).held,false);
  pet.send('tap',2200);assert.equal(pet.read(2200).mode,'curious');
  pet.send('reconnect',2300);assert.equal(pet.read(2300).online,true);
  assert.equal(pet.read(2300).mode,'curious');
  pet.select('sleep',3000);pet.send('tap',3100);
  assert.equal(pet.read(3100).mode,'curious');
  pet.select('think',4000);pet.send('press',4100);pet.send('release',4400);
  assert.equal(pet.read(5600).mode,'speak');
  assert.equal(pet.read(8800).mode,'idle');
  assert.throws(()=>pet.select('unknown',9000),/Unknown/);
});

test('committed preview frames match the real core and renderer geometry',()=>{
  const dir=mkdtempSync(join(tmpdir(),'pet-frames-'));
  try {
    execFileSync('cc',['-std=c11','-Wall','-Wextra','-Werror',
      '-Ifirmware/pet/components/pet_core','tests/pet_pose_export.c',
      'firmware/pet/components/pet_core/pet_core.c','firmware/pet/components/pet_core/pet_face.c','firmware/pet/components/pet_core/pet_caption.c',
      '-lm','-o',join(dir,'export')]);
    const frames=JSON.parse(execFileSync(join(dir,'export'),{encoding:'utf8',maxBuffer:4*1024*1024}));
    const published=JSON.parse(readFileSync('preview/companion/states.json','utf8'));
    assert.deepEqual(published,frames,'Regenerate preview frames after firmware pose changes');
    assert.equal(frames.states.curious[0].expression,7);
    assert.equal(frames.states.confused[0].expression,8);
    assert.equal(frames.states.curious[61].expression,0);
    assert.equal(frames.states.happy[55].expression,0);
    assert.ok(Object.values(frames.states).every(samples=>samples.length===121));
  } finally {rmSync(dir,{recursive:true,force:true});}
});
