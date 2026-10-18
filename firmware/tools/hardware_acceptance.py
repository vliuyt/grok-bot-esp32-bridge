from pathlib import Path
"""Explicit acoustic integration test; no speech, transcript or credentials are logged."""
import sys,time,json,re,subprocess,urllib.request,pathlib,uuid,math,os,atexit
from fixture_audio import FixtureAudio,reuse_rounds
root=pathlib.Path(__file__).resolve().parents[2]
pet_mode='--pet' in sys.argv
long_only='--long-only' in sys.argv
prefix='pet' if pet_mode else 'hardware'
fixture_audio=None
if pet_mode:
    fixture_audio=FixtureAudio(root,os.environ['GROK_BENCH_OUTPUT'])
    atexit.register(fixture_audio.close)
sys.path.insert(0,str(Path(__file__).resolve().parent/'vendor'))
import chat
base='http://127.0.0.1:8788'
http=urllib.request.build_opener(urllib.request.ProxyHandler({}))
def state():
    with http.open(base+'/state',timeout=5) as response: return json.load(response)
def command(action):
    with http.open(base+'/',timeout=5) as response: page=response.read().decode()
    token=re.search(r'const controlToken = "([0-9a-f]+)"',page).group(1)
    request=urllib.request.Request(base+'/command',data=json.dumps({'action':action}).encode(),headers={'content-type':'application/json','origin':base,'x-bridge-control':token})
    with http.open(request,timeout=30) as response: json.load(response)
def wait_ready(timeout=70):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        try: s=state()
        except (OSError,urllib.error.URLError):
            time.sleep(.2);continue
        if s['state']=='ready': return s
        time.sleep(.2)
    raise RuntimeError('READY_TIMEOUT')
def serial_frame(board,cmd,prefix,timeout=3):
    for attempt in range(3):
        board.write(b'\n');board.write_line(cmd);deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            line=board.read_line(deadline)
            if line:
                at=line.find(prefix)
                if at>=0: return line[at+len(prefix):].strip()
    raise RuntimeError('SERIAL_ACK_TIMEOUT')
def ptt(board,edge):
    if serial_frame(board,'grok.ptt '+edge,'@grok.ptt ')!='ok': raise RuntimeError('PTT_FAILED')
    for check in range(10):
        if device(board)['recording']==(edge=='down'): return
        time.sleep(.1)
    raise RuntimeError('PTT_STATE_MISMATCH')
def device(board): return json.loads(serial_frame(board,'grok.status','@grok '))
def speak_fixture(text): subprocess.run(['/usr/bin/say','-v','Tingting']+(['-a',os.environ['GROK_BENCH_OUTPUT']] if os.environ.get('GROK_BENCH_OUTPUT') else [])+['-f','-'],input=text,text=True,check=True,timeout=30)
def fixture_turn(board,text):
    pcm=fixture_audio.prepare(text) if fixture_audio else None
    try:
        ptt(board,'down');time.sleep(.1)
        if fixture_audio:fixture_audio.play(pcm)
        else:speak_fixture(text)
        time.sleep(.25)
    finally:
        ptt(board,'up')
def validate(before,after,played):
    checks={
      'oneTurn':after['turns']==before['turns']+1,
      'noReconnect':after['reconnects']==before['reconnects'],
      'noFault':not after.get('fault'),
      'noUnexpectedCancel':after.get('cancelCount',0)==before.get('cancelCount',0),
      'nativeCompleted':bool(after.get('nativeResponses')) and after['nativeResponses'][-1]['event']=='response.done' and after['nativeResponses'][-1]['owned'] and after['nativeResponses'][-1]['status']=='completed',
      'inputAudio':after.get('lastInputBytes',0)>4800,
      'recognizedInput':after.get('recognizedInputs',0)>before.get('recognizedInputs',0),
      'inputLevel':math.isfinite(after.get('lastInputDb',float('nan'))) and -80<after.get('lastInputDb',-100)<=0,
      'nativeAudio':after.get('nativeEvents',{}).get('response.output_audio.delta',0)>before.get('nativeEvents',{}).get('response.output_audio.delta',0),
      'deviceAudio':played>0,
      'fullyDrained':after['state']=='ready',
    }
    if not all(checks.values()): raise RuntimeError('ROUND_CHECK_FAILED:'+','.join(k for k,v in checks.items() if not v))
    return checks
run_id=str(uuid.uuid4());results=[]
report={'runId':run_id,'complete':False,'status':'running','validation':'acoustic-round-trip-only','rounds':results}
long_report={'runId':run_id,'passed':False,'status':'pending'}
firmware=root/'docs/pet-firmware-manifest.json'
digest=json.loads(firmware.read_text())['sha256'] if pet_mode and firmware.exists() else None
if long_only:
    previous=json.loads((root/('docs/'+prefix+'-ten-turns.json')).read_text())
    report=reuse_rounds(previous,digest);results=report['rounds'];report.update(complete=True,status='passed');report.pop('failure',None)
if digest:
    report['firmwareSha256']=digest;long_report['firmwareSha256']=digest
def save():
    (root/('docs/'+prefix+'-ten-turns.json')).write_text(json.dumps(report,indent=2)+'\n')
    (root/('docs/'+prefix+'-long-reply.json')).write_text(json.dumps(long_report,indent=2)+'\n')
save()
try:
  with chat.Board(chat.pick_port()) as board:
    if not board.status(): raise RuntimeError('DEVICE_STATUS_UNAVAILABLE')
    wait_ready()
    deadline=time.monotonic()+90
    while not device(board)["online"]:
        if time.monotonic()>deadline:raise RuntimeError("DEVICE_NATIVE_READY_TIMEOUT")
        time.sleep(.2)
    for number in ([] if long_only else range(1,11)):
        before=state(); frames=device(board)['playedFrames']
        fixture_turn(board,'这是第'+str(number)+'轮语音测试，请只说测试通过。')
        submitted=time.monotonic();deadline=submitted+70
        while time.monotonic()<deadline:
            after=state()
            if after['replies']>before['replies'] and after['state']=='ready': break
            time.sleep(.2)
        else: raise RuntimeError('ROUND_TIMEOUT')
        played=device(board)['playedFrames']-frames
        checks=validate(before,after,played)
        result={'round':number,'playedFrames':played,'firstAudioMs':after.get('firstAudioMs'),'completeMs':round((time.monotonic()-submitted)*1000),'checks':checks,'passed':True}
        results.append(result);save();print(json.dumps(result),flush=True)
    report['complete']=True;report['status']='passed';save()
    before=state();frames=device(board)['playedFrames']
    fixture_turn(board,'请直接讲一个两百五十字左右的温暖小故事，不要先解释。' if pet_mode else '请用中文讲一个一百五十字左右的温暖小故事，直接讲故事。')
    deadline=time.monotonic()+180
    while time.monotonic()<deadline:
        after=state()
        if after['replies']>before['replies'] and after['state']=='ready': break
        time.sleep(.3)
    else: raise RuntimeError('LONG_REPLY_TIMEOUT')
    played=device(board)['playedFrames']-frames
    checks=validate(before,after,played)
    long_report.update(playedFrames=played,audioSeconds=round(played/16000,2));save()
    if played<16000*(30 if pet_mode else 10): raise RuntimeError('LONG_REPLY_TOO_SHORT')
    long_report.update({'playedFrames':played,'audioSeconds':round(played/16000,2),'firstAudioMs':after.get('firstAudioMs'),'checks':checks,'passed':True,'status':'passed'})
    save();print(json.dumps({'longReplySeconds':round(played/16000,2),'passed':True}),flush=True)
except BaseException as error:
    report['complete']=len(results)==10 and all(r.get('passed') for r in results)
    report['status']='passed' if report['complete'] else 'failed'
    long_report.update({'status':'not-completed','passed':False})
    failure=str(error) if isinstance(error,RuntimeError) else type(error).__name__
    if report['complete']:long_report['failure']=failure
    else:report['failure']=failure
    save();raise
