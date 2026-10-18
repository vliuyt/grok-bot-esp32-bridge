"""Explicit bench-only synthetic speech through one persistent physical output engine."""
import base64
import json
from pathlib import Path
import queue
import subprocess
import tempfile
import shutil
import threading

def reuse_rounds(report,digest):
    if len(report.get('rounds',[]))!=10 or not all(r.get('passed') for r in report['rounds']):
        raise RuntimeError('TEN_TURN_BASELINE_REQUIRED')
    if digest and report.get('firmwareSha256')!=digest:
        raise RuntimeError('TEN_TURN_FIRMWARE_MISMATCH')
    return report

class FixtureAudio:
    def __init__(self,root,output):
        self.root=Path(root);self.events=queue.Queue();self.closed=False
        self.child=subprocess.Popen([str(self.root/'.build/mac-audio'),'--playback-only','--output',str(output)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True)
        threading.Thread(target=self._read,daemon=True).start()
        try:self._wait('ready',15)
        except BaseException:
            self.close();raise

    def _read(self):
        try:
            for line in self.child.stdout:
                try:self.events.put(json.loads(line))
                except ValueError:self.events.put({'type':'fault'})
        finally:self.events.put({'type':'closed'})

    def _wait(self,kind,timeout):
        import time
        deadline=time.monotonic()+timeout
        while True:
            try:event=self.events.get(timeout=max(.001,deadline-time.monotonic()))
            except queue.Empty:raise RuntimeError('FIXTURE_OUTPUT_TIMEOUT') from None
            if event.get('type') in ('fault','closed'):raise RuntimeError('FIXTURE_OUTPUT_FAILED')
            if event.get('type')==kind:return
            if time.monotonic()>=deadline:raise RuntimeError('FIXTURE_OUTPUT_TIMEOUT')

    def prepare(self,text):
        with tempfile.TemporaryDirectory(prefix='fixture-',dir=self.root/'.local') as temporary:
            path=Path(temporary)/'speech.aiff'
            subprocess.run(['/usr/bin/say','-v','Tingting','-o',str(path),'-f','-'],input=text,text=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True,timeout=30)
            pcm=subprocess.run([shutil.which('ffmpeg') or 'ffmpeg','-v','error','-i',str(path),'-f','s16le','-ac','1','-ar','24000','pipe:1'],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,check=True,timeout=30).stdout
        if not 4800<=len(pcm)<=24000*2*30 or len(pcm)%2:raise RuntimeError('FIXTURE_PCM_INVALID')
        return pcm

    def play(self,pcm):
        for start in range(0,len(pcm),96000):
            chunk=pcm[start:start+96000]
            self.child.stdin.write(json.dumps({'type':'play','pcm':base64.b64encode(chunk).decode()})+'\n');self.child.stdin.flush()
            self._wait('drained',len(chunk)/48000+10)

    def close(self):
        if self.closed:return
        self.closed=True
        try:
            if self.child.poll() is None:
                self.child.stdin.write('{"type":"quit"}\n');self.child.stdin.flush();self.child.stdin.close();self.child.wait(timeout=4)
        except (OSError,subprocess.TimeoutExpired):
            self.child.kill();self.child.wait(timeout=4)
        finally:
            self.child.stdout.close()

if __name__=='__main__':
    import argparse
    import sys
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',required=True)
    args=parser.parse_args()
    fixture=FixtureAudio(Path(__file__).resolve().parents[2],args.output)
    try:fixture.play(fixture.prepare(sys.stdin.read(4096)))
    finally:fixture.close()
