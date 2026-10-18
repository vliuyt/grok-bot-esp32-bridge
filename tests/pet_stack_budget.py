"""Measure real Xtensa frames and reserve headroom above configuration call paths."""
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
stage=Path(os.environ['GROK_PET_STAGE']) if os.environ.get('GROK_PET_STAGE') else Path(json.loads((root/'.local/build-info.json').read_text())['stage'])
build=stage/'build'
commands=json.loads((build/'compile_commands.json').read_text())
frames={}
with tempfile.TemporaryDirectory(prefix='pet-stack-') as temporary:
    for name in ('pet_console.c','pet_provision.c','pet_json.c'):
        row=next(r for r in commands if Path(r['file']).name==name)
        args=shlex.split(row['command'])
        # Measure current project headers too; staged generated/dependency headers stay fixed.
        for index,arg in enumerate(args):
            if arg.startswith(('-I'+str(stage/'components')+'/', '-I'+str(stage/'main'))):
                args[index]=arg.replace(str(stage),str(root/'firmware/pet'),1)
        args[args.index('-o')+1]=str(Path(temporary)/(name+'.o'))
        args[args.index(row['file'])]=str(root/'firmware/pet'/Path(row['file']).relative_to(stage))
        args.append('-fstack-usage')
        subprocess.run(args,cwd=row['directory'],check=True,stdout=subprocess.DEVNULL)
        for line in (Path(temporary)/(name+'.su')).read_text().splitlines():
            location,size,*_=line.split('\t')
            frames[location.rsplit(':',1)[-1]]=int(size)
console_source=(root/'firmware/pet/main/pet_console.c').read_text()
console=int(re.search(r'xTaskCreate\(serial_task,"pet_console",(\d+)',console_source)[1])
host=int(re.search(r'CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=(\d+)',(root/'firmware/pet/sdkconfig.defaults').read_text())[1])
usb_chain=sum(frames[key] for key in ('serial_task','pet_provision_usb','submit_locked','pet_json_parse'))
ble_chain=frames['submit_locked']+frames['pet_json_parse']+max(value for key,value in frames.items() if key.startswith('access_cb'))
assert console>=usb_chain+2048, f'USB stack {console} below measured parser chain {usb_chain} plus 2048 headroom'
assert host>=ble_chain+4096, f'BLE stack {host} below measured parser chain {ble_chain} plus 4096 host headroom'
print('PET_STACK_BUDGET_OK')
