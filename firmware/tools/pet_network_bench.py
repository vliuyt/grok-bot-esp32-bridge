"""Explicit USB rollback/pairing and BLE advertisement bench; no secrets in reports."""
import asyncio
import json
from pathlib import Path
import sys
import time
import uuid
from pet_bench import request
from pet_artifacts import atomic_json
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(Path(__file__).resolve().parent/'vendor'))


def setup_status(board):
    return json.loads(request(board,'pet.setup status','@pet.setup '))


def until(check,timeout=60):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        result=check()
        if result:return result
        time.sleep(.2)
    raise RuntimeError('BENCH_TIMEOUT')


async def scan():
    from bleak import BleakScanner
    devices=await BleakScanner.discover(timeout=6,return_adv=True)
    expected='67727000-51d4-418c-9042-e6869a8a5810'
    return any(expected in [u.lower() for u in adv.service_uuids] for _,adv in devices.values())


REQUIRED_CHECKS=('closedWindowRejected','badCandidateAcceptedForValidation','wrongNetworkFailed','oldWifiRestored','duplicateDidNotRetry','usbPairingSaved','voiceRecovered','bleAdvertisementSeen','setupClosedFinally','noReset','stackHeadroom')
def acceptance_passed(report):return all(report.get(key) is True for key in REQUIRED_CHECKS)
def pairing_request(config,request_id):return {'id':request_id,'op':'configure','bridge':{key:config[key] for key in ('host','port','key')}}

def main():
    import chat
    report={'runId':str(uuid.uuid4()),'passed':False,'status':'running',
            'bleAuthenticatedPairing':'requires physical passkey entry; not claimed'}
    path=ROOT/'docs/pet-network-acceptance.json';atomic_json(path,report)
    report['firmwareSha256']=json.loads((ROOT/'docs/pet-firmware-manifest.json').read_text())['sha256']
    try:
        with chat.Board(chat.pick_port()) as board:
            try:
                until(lambda:board.status()['device']['link']['state']=='Online',90)
                started=board.status()['pet']['uptimeMs']
                config=json.loads((ROOT/'.local/device.json').read_text())
                request(board,'pet.setup close','@pet.setup ')
                probe=pairing_request(config,uuid.uuid4().hex)
                rejected=request(board,'pet.configure '+json.dumps(probe),'@pet.configure ')
                report['closedWindowRejected']=rejected=='rejected'
                request(board,'pet.setup open','@pet.setup ')
                try:report['bleAdvertisementSeen']=asyncio.run(scan())
                except Exception as error:report['bleScanFailure']=type(error).__name__;report['bleAdvertisementSeen']=False
                bad_id=uuid.uuid4().hex
                bad={'id':bad_id,'op':'configure','wifi':{'ssid':'GrokPet-Invalid-Test-Network','password':'not-a-real-password'}}
                accepted=request(board,'pet.configure '+json.dumps(bad),'@pet.configure ')
                report['badCandidateAcceptedForValidation']=accepted=='accepted'
                result=until(lambda: (s if (s:=setup_status(board))['id']==bad_id and s['state']=='failed' else False),90)
                report['wrongNetworkFailed']=result['state']=='failed'
                until(lambda:board.status()['device']['wifi']['state']=='connected',60)
                report['oldWifiRestored']=True
                request(board,'pet.configure '+json.dumps(bad),'@pet.configure ')
                duplicate=setup_status(board)
                report['duplicateDidNotRetry']=duplicate['id']==bad_id and duplicate['state']=='failed'
                good_id=uuid.uuid4().hex;good=pairing_request(config,good_id)
                if request(board,'pet.configure '+json.dumps(good),'@pet.configure ')!='accepted':raise RuntimeError('USB_PAIRING_REJECTED')
                good['bridge']['key']='';probe['bridge']['key']='';config.clear()
                until(lambda: (s if (s:=setup_status(board))['id']==good_id and s['state']=='saved' else False),60)
                report['usbPairingSaved']=True
                request(board,'pet.setup close','@pet.setup ')
                until(lambda:board.status()['device']['link']['state']=='Online',90)
                report['voiceRecovered']=True
                final=board.status()['pet']
                report['noReset']=final['uptimeMs']>started
                report['stackConsoleMinimum']=final['stackConsole'];report['stackNetworkMinimum']=final['stackNetwork']
                report['stackHeadroom']=min(final['stackConsole'],final['stackNetwork'])>=2048
            finally:
                try:
                    request(board,'pet.setup close','@pet.setup ')
                    report['setupClosedFinally']=not setup_status(board)['open']
                except Exception:report['setupClosedFinally']=False
            report['passed']=acceptance_passed(report)
            report['status']='passed' if report['passed'] else 'failed'
    except BaseException as error:report['failure']=type(error).__name__;report['status']='failed'
    finally:
        atomic_json(path,report)
    print(json.dumps(report))
    raise SystemExit(0 if report['passed'] else 1)


if __name__=='__main__':main()
