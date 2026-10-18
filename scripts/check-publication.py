"""Conservative publication guard; supplements, but does not replace, review."""
from pathlib import Path
import re
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
try:
    tracked=subprocess.check_output(['git','ls-files','-z'],cwd=ROOT,stderr=subprocess.DEVNULL).decode().split('\0')
    paths=[ROOT/p for p in tracked if p] if tracked!=[''] else []
except subprocess.CalledProcessError:paths=[]
if not paths:
    ignored={'.git','.local','.build','node_modules','__pycache__','managed_components','build'}
    paths=[p for p in ROOT.rglob('*') if p.is_file() and not any(s in ignored for s in p.relative_to(ROOT).parts)]
patterns={
    'private home path':re.compile('/'+'Users/'+r'(?!runner(?:/|$))[^/\s]+'),
    'private service naming':re.compile('com'+r'\.timliu|local'+r'\.timliu'),
    'GitHub token':re.compile(r'gh[pousr]_[A-Za-z0-9]{30,}'),
    'API secret':re.compile(r'sk-(?:proj-)?[A-Za-z0-9_-]{30,}'),
    'private key':re.compile(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'),
    'AWS access key':re.compile(r'AKIA[A-Z0-9]{16}'),
}
failures=[]
for p in paths:
    rel=p.relative_to(ROOT).as_posix()
    if p.is_symlink() or any(x in {'.local','.build','.superpowers'} for x in p.relative_to(ROOT).parts):failures.append((rel,'private artifact'))
    if p.suffix in {'.bin','.wav','.aiff','.pcm','.pem','.key','.p12'}:failures.append((rel,'binary/credential artifact'))
    if p.parent==ROOT/'docs' and 'backup' in p.name and p.suffix=='.json':failures.append((rel,'device backup record'))
    if p.suffix in {'.png','.jpg'}:continue
    try:text=p.read_text()
    except UnicodeDecodeError:failures.append((rel,'unexpected binary'));continue
    for label,pattern in patterns.items():
        if pattern.search(text):failures.append((rel,label))
for rel,reason in failures:print(f'{rel}: {reason}')
if failures:sys.exit(1)
print(f'PUBLICATION_GUARD_OK ({len(paths)} files; pattern scan plus artifact allowlist, not a secret-proof guarantee)')
