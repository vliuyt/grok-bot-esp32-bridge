from pathlib import Path
"""Sanitized pet status/snapshot/soak harness, no credentials or raw audio saved."""
import argparse
import base64
import json
import pathlib
import sys
import time
import uuid
import statistics
from pet_artifacts import atomic_json
from datetime import datetime, timezone
ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent/'vendor'))


def request(board, command, prefix, timeout=5):
    board.write(b'\n')
    board.write_line(command)
    deadline = time.monotonic()+timeout
    while time.monotonic()<deadline:
        line=board.read_line(deadline)
        if line and prefix in line:
            return line.split(prefix,1)[1].strip()
    raise RuntimeError('DEVICE_RESPONSE_TIMEOUT')


def snapshot(board, output):
    from PIL import Image
    board.write(b'\n')
    board.write_line('pet.snapshot')
    deadline=time.monotonic()+60
    pixels=bytearray()
    width=height=None
    while time.monotonic()<deadline:
        line=board.read_line(deadline)
        if not line:
            continue
        if line.startswith('SNAP BEGIN '):
            _,_,w,h,*_=line.split();width,height=int(w),int(h)
        elif width and line and not line.startswith('SNAP END'):
            try:
                pixels.extend(base64.b64decode(line.strip(),validate=True))
            except ValueError:
                pass
        elif line.startswith('SNAP END'):
            break
    if width!=466 or height!=466 or len(pixels)!=width*height*2:
        raise RuntimeError('SNAPSHOT_INCOMPLETE')
    rgb=bytearray(width*height*3)
    for i in range(width*height):
        value=pixels[i*2]|pixels[i*2+1]<<8
        rgb[i*3:i*3+3]=bytes(((value>>11)*255//31,((value>>5)&63)*255//63,(value&31)*255//31))
    Image.frombytes('RGB',(width,height),bytes(rgb)).save(output)
    return {'width':width,'height':height,'path':str(output.relative_to(ROOT))}


def summarize(samples,elapsed,requested):
    stable=[s for s in samples if s['seconds']>=min(300,requested/4)]
    width=min(10,max(1,len(stable)//4))
    first,last=stable[:width],stable[-width:]
    def median(group,key):return statistics.median(s[key] for s in group) if group else 0
    internal_loss=median(first,'heap')-median(last,'heap')
    external_loss=median(first,'psram')-median(last,'psram')
    resets=sum(b['uptimeMs']<=a['uptimeMs'] for a,b in zip(samples,samples[1:]))
    faults=sum(bool(s['audioFault']) for s in samples)
    low_internal=min((s['heap'] for s in samples),default=0)
    low_external=min((s['psram'] for s in samples),default=0)
    drops=max((s.get('eventDrops',0) for s in samples),default=0)-(samples[0].get('eventDrops',0) if samples else 0)
    enough=len(samples)>=max(2,int(requested/30)-1) and elapsed>=requested
    passed=enough and bool(stable) and not resets and not faults and not drops and low_internal>10000 and low_external>1000000 and internal_loss<=8192 and external_loss<=32768
    return {'passed':passed,'elapsedSeconds':round(elapsed,1),'audioFaults':faults,'resets':resets,'newEventDrops':drops,
            'minimumInternalHeap':low_internal,'minimumPsram':low_external,
            'stableInternalLoss':internal_loss,'stablePsramLoss':external_loss,
            'limits':{'internalFloor':10000,'psramFloor':1000000,'internalLoss':8192,'psramLoss':32768}}


def soak(connect,seconds,path):
    report={'runId':str(uuid.uuid4()),'startedAt':datetime.now(timezone.utc).isoformat(),
            'requestedSeconds':seconds,'complete':False,'passed':False,'status':'running','samples':[]}
    manifest=ROOT/'docs/pet-firmware-manifest.json'
    if manifest.exists():report['firmwareSha256']=json.loads(manifest.read_text())['sha256']
    atomic_json(path,report)
    start=time.monotonic()
    try:
        with connect() as board:
            while time.monotonic()-start<seconds:
                state=json.loads(request(board,'status','@status '))
                report['samples'].append({'seconds':round(time.monotonic()-start,1),**state['pet']})
                atomic_json(path,report)
                time.sleep(min(30,max(0,seconds-(time.monotonic()-start))))
        report.update(summarize(report['samples'],time.monotonic()-start,seconds))
        report['complete']=True;report['status']='passed' if report['passed'] else 'failed'
    except Exception as error:
        report['status']='failed';report['failure']=type(error).__name__
    atomic_json(path,report)
    return report


def main():
    import chat
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['status','snapshot','soak','pet','home','settings','connection'])
    parser.add_argument('--seconds',type=int,default=7200)
    args=parser.parse_args()
    if args.action=='soak':
        if not 60<=args.seconds<=86400:raise SystemExit('SOAK_DURATION_REFUSED')
        report=soak(lambda:chat.Board(chat.pick_port()),args.seconds,ROOT/'docs/pet-soak.json')
        print(json.dumps({k:v for k,v in report.items() if k!='samples'}))
        raise SystemExit(0 if report['passed'] else 1)
    with chat.Board(chat.pick_port()) as board:
        if args.action=='status':
            state=json.loads(request(board,'status','@status '));device=state.get('device',{});print(json.dumps({'board':state.get('board'),'app':state.get('app'),'volume':device.get('volume'),'speaker':device.get('speaker'),'pet':state.get('pet')}))
        elif args.action=='snapshot':
            out=ROOT/'docs/pet-device-screen.png'
            print(json.dumps(snapshot(board,out)))
        elif args.action in ('pet','home','settings','connection'):
            command='pet.pet' if args.action=='pet' else 'pet.page '+args.action
            board.write_line(command)
            print('PET_BENCH_REQUESTED')


if __name__=='__main__':
    main()
