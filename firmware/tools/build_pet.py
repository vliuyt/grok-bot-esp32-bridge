"""Build the standalone pet application without paths containing spaces or private config."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import hashlib
import tempfile
import json
from pet_artifacts import publish

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--idf', type=Path, default=Path(os.environ['IDF_PATH']) if os.environ.get('IDF_PATH') else None)
parser.add_argument('--stage', type=Path, default=Path(tempfile.gettempdir())/('grok-pet-'+hashlib.sha256(str(root).encode()).hexdigest()[:12]))
args = parser.parse_args()
stage = args.stage.resolve()
marker = stage / '.grok-pet-stage'
if stage.exists() and not marker.is_file():
    raise SystemExit('UNOWNED_BUILD_DIRECTORY')
if args.idf is None or not (args.idf / 'export.sh').is_file():
    raise SystemExit('ESP_IDF_601_REQUIRED_USE_IDF_OPTION')
stage.mkdir(parents=True, exist_ok=True)
marker.touch()
subprocess.run(['rsync', '-a', '--delete', '--exclude=managed_components', '--exclude=build',
                '--exclude=sdkconfig', '--exclude=sdkconfig.old', '--exclude=.grok-pet-*',
                str(root / 'firmware/pet') + '/', str(stage) + '/'], check=True)
defaults_hash = hashlib.sha256((stage / 'sdkconfig.defaults').read_bytes()).hexdigest()
defaults_stamp = stage / '.grok-pet-defaults'
if not defaults_stamp.exists() or defaults_stamp.read_text() != defaults_hash:
    (stage / 'sdkconfig').unlink(missing_ok=True)
    defaults_stamp.write_text(defaults_hash)
env = os.environ.copy()
env['IDF_PATH'] = str(args.idf.resolve())
subprocess.run(['bash', '-c', 'source "$IDF_PATH/export.sh" >/dev/null && exec python "$IDF_PATH/tools/idf.py" -B build -DIDF_TARGET=esp32s3 build'],
               cwd=stage, env=env, check=True)
image = stage / 'build/grok-pet.bin'
if not 1024 <= image.stat().st_size <= 0x3e0000:
    raise SystemExit('APPLICATION_SLOT_EXCEEDED')
if (stage / 'dependencies.lock').is_file():
    shutil.copy2(stage / 'dependencies.lock', root / 'firmware/pet/dependencies.lock')
publish(root,image,hashlib.sha256((stage/'sdkconfig').read_bytes()).hexdigest())
(root/'.local').mkdir(mode=0o700,exist_ok=True)
(root/'.local/build-info.json').write_text(json.dumps({'stage':str(stage)}))
print('PET_APPLICATION_BUILT', image.stat().st_size)

print('BUILD_ONLY: use firmware/tools/pet_install.py; do not run the generic idf.py flash command.')
