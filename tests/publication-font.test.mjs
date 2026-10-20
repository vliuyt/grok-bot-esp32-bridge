import {test} from 'node:test';
import assert from 'node:assert/strict';
import {copyFileSync,mkdirSync,mkdtempSync,readFileSync,rmSync,unlinkSync,writeFileSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';

test('publication accepts the reviewed font with license and refuses tampering or other binaries',()=>{
  const root=mkdtempSync(join(tmpdir(),'publication-font-'));
  try{
    mkdirSync(join(root,'scripts'));
    const source='preview/companion/fonts',target=join(root,source);
    mkdirSync(target,{recursive:true});
    copyFileSync('scripts/check-publication.py',join(root,'scripts/check-publication.py'));
    const font=join(target,'xiaotang-ui-preview.woff'),license=join(target,'OFL.txt');
    copyFileSync(`${source}/xiaotang-ui-preview.woff`,font);copyFileSync(`${source}/OFL.txt`,license);
    const check=()=>execFileSync('python3',[join(root,'scripts/check-publication.py')],{cwd:root,encoding:'utf8',stdio:['ignore','pipe','pipe']});
    assert.match(check(),/PUBLICATION_GUARD_OK/);
    const bytes=readFileSync(font);bytes[bytes.length-1]^=1;writeFileSync(font,bytes);
    assert.throws(check,error=>error.status===1&&error.stdout.includes('unreviewed font asset'));
    copyFileSync(`${source}/xiaotang-ui-preview.woff`,font);unlinkSync(license);
    assert.throws(check,error=>error.status===1&&error.stdout.includes('font license missing or changed'));
    copyFileSync(`${source}/OFL.txt`,license);copyFileSync(font,join(target,'other.woff'));
    assert.throws(check,error=>error.status===1&&error.stdout.includes('unexpected binary'));
  }finally{rmSync(root,{recursive:true,force:true});}
});

test('Git publication requires the font license to be included in the tracked file set',()=>{
  const root=mkdtempSync(join(tmpdir(),'publication-font-git-'));
  try{
    mkdirSync(join(root,'scripts'));mkdirSync(join(root,'preview/companion/fonts'),{recursive:true});
    for(const path of ['scripts/check-publication.py','preview/companion/fonts/xiaotang-ui-preview.woff','preview/companion/fonts/OFL.txt'])copyFileSync(path,join(root,path));
    const git=args=>execFileSync('git',args,{cwd:root,stdio:'pipe'});
    git(['init','-q']);git(['add','scripts/check-publication.py','preview/companion/fonts/xiaotang-ui-preview.woff']);
    const check=()=>execFileSync('python3',[join(root,'scripts/check-publication.py')],{cwd:root,encoding:'utf8',stdio:['ignore','pipe','pipe']});
    assert.throws(check,error=>error.status===1&&error.stdout.includes('font license missing or changed'));
    git(['add','preview/companion/fonts/OFL.txt']);assert.match(check(),/PUBLICATION_GUARD_OK/);
  }finally{rmSync(root,{recursive:true,force:true});}
});
