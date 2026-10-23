import {execFileSync} from 'node:child_process';
import {mkdtempSync,rmSync,writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,dirname,resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const dir=mkdtempSync(join(tmpdir(),'pet-preview-'));
try {
  execFileSync('cc',['-std=c11','-Wall','-Wextra','-Werror','-Ifirmware/pet/components/pet_core',
    'tests/pet_pose_export.c','firmware/pet/components/pet_core/pet_core.c',
    'firmware/pet/components/pet_core/pet_face.c','firmware/pet/components/pet_core/pet_caption.c','-lm','-o',join(dir,'export')],{cwd:root});
  const frames=execFileSync(join(dir,'export'),{encoding:'utf8',maxBuffer:4*1024*1024});
  JSON.parse(frames);
  writeFileSync(join(root,'preview/companion/states.json'),frames);
  process.stdout.write('PET_PREVIEW_EXPORTED\n');
} finally {rmSync(dir,{recursive:true,force:true});}
