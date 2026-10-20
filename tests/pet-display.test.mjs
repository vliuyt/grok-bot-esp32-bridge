import {test} from 'node:test';
import assert from 'node:assert/strict';
import {existsSync,readFileSync,mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {execFileSync,spawnSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {stage} from './support/paths.mjs';

test('font generation refuses an unverified source before changing output',()=>{
  const temporary=mkdtempSync(join(tmpdir(),'pet-font-input-'));
  try{
    const wrong=join(temporary,'wrong.otf'),marker=join(temporary,'sentinel.txt');
    writeFileSync(wrong,'not a verified font');writeFileSync(marker,'preserve');
    const result=spawnSync(process.execPath,['scripts/generate-pet-fonts.mjs','--noto',wrong,'--unifont',wrong,'--converter',wrong,'--output',temporary],{encoding:'utf8'});
    assert.notEqual(result.status,0);assert.match(result.stderr,/FONT_SOURCE_UNVERIFIED/);
    assert.equal(readFileSync(marker,'utf8'),'preserve');
  }finally{rmSync(temporary,{recursive:true,force:true});}
});

const lvgl=stage&&join(stage,'managed_components/lvgl__lvgl');
test('real LVGL renders readable gray glyphs and preserves the original character coverage',
  {skip:!lvgl||!existsSync(join(lvgl,'lvgl.h'))?'Build firmware once to resolve LVGL first':false},()=>{
    let cmake=process.env.GROK_PET_CMAKE;
    if(!cmake){
      const cache=join(stage,'build/CMakeCache.txt');
      if(existsSync(cache))cmake=readFileSync(cache,'utf8').match(/^CMAKE_COMMAND:INTERNAL=(.+)$/m)?.[1];
    }
    cmake??='cmake';
    const compressed=/^CONFIG_LV_USE_FONT_COMPRESSED=y$/m.test(readFileSync('firmware/pet/sdkconfig.defaults','utf8'));
    const build=resolve('.local/display-host');
    execFileSync(cmake,['-S','tests/display_host','-B',build,'-G','Unix Makefiles',
      '-DLVGL_SOURCE='+lvgl,'-DPET_ROOT='+process.cwd(),'-DPET_FONT_COMPRESSED='+(compressed?1:0),'-DCMAKE_BUILD_TYPE=Release',
      '-DCMAKE_C_COMPILER=/usr/bin/cc','-DCMAKE_CXX_COMPILER=/usr/bin/c++'],{stdio:'pipe'});
    execFileSync(cmake,['--build',build,'--parallel','4'],{stdio:'pipe',maxBuffer:8*1024*1024});
    assert.match(execFileSync(join(build,'pet-font-test'),{encoding:'utf8'}),/PET_FONT_RENDER_OK/);
    assert.match(execFileSync(join(build,'pet-ui-test'),[build],{encoding:'utf8',stdio:['ignore','pipe','pipe']}),/PET_UI_RENDER_OK/);
  });
