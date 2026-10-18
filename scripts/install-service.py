"""Install a per-user macOS bridge after a successful foreground setup."""
from pathlib import Path
import argparse
import ipaddress
import json
import os
import plistlib
import re
import shutil
import subprocess
import tempfile

LABEL='io.github.grokpet.bridge'


def install_service(root,home,bot,node,start=False,runner=subprocess.run):
    root,home=Path(root),Path(home)
    node=shutil.which(node)
    if not node:raise ValueError('NODE_NOT_FOUND')
    node=str(Path(node).resolve())
    version=runner([node,'--version'],capture_output=True,text=True,check=True).stdout.strip()
    match=re.fullmatch(r'v(\d+)\.(\d+)\.(\d+)',version)
    if not match or tuple(map(int,match.groups()))<(22,19,0):raise ValueError('NODE_22_19_REQUIRED')
    if not bot.strip():raise ValueError('BOT_NAME_REQUIRED')
    config_path=root/'.local/device.json'
    if not config_path.is_file():raise ValueError('RUN_FOREGROUND_DEVICE_SETUP_FIRST')
    config=json.loads(config_path.read_text())
    address=ipaddress.ip_address(config.get('host',''))
    if address.version!=4 or not address.is_private or address.is_loopback:raise ValueError('PRIVATE_LAN_ADDRESS_REQUIRED')
    if not re.fullmatch('[0-9a-f]{64}',config.get('key','')) or not isinstance(config.get('port'),int) or not 1024<=config['port']<=65535:raise ValueError('INVALID_PAIRING_CONFIG')
    if not (root/'src/cli.mjs').is_file():raise ValueError('SOURCE_MISSING')
    runtime=home/'Library/Application Support/GrokPetBridge'
    agent=home/f'Library/LaunchAgents/{LABEL}.plist'
    if runtime.exists() or agent.exists():raise ValueError('SERVICE_ALREADY_EXISTS')
    runtime.parent.mkdir(parents=True,exist_ok=True)
    temporary=Path(tempfile.mkdtemp(prefix='.grokpet-install-',dir=runtime.parent))
    agent_created=False;runtime_created=False
    try:
        os.chmod(temporary,0o700);shutil.copytree(root/'src',temporary/'src')
        private=temporary/'.local';private.mkdir(mode=0o700)
        (private/'device.json').write_text(json.dumps(config));os.chmod(private/'device.json',0o600)
        temporary.rename(runtime);runtime_created=True
        plist={'Label':LABEL,'ProgramArguments':[node,str(runtime/'src/cli.mjs'),'device','--bot',bot],
               'WorkingDirectory':str(runtime),'RunAtLoad':True,'KeepAlive':True,'ThrottleInterval':30,
               'ExitTimeOut':60,'ProcessType':'Background','StandardOutPath':str(runtime/'.local/stdout.log'),
               'StandardErrorPath':str(runtime/'.local/stderr.log')}
        agent.parent.mkdir(parents=True,exist_ok=True)
        with agent.open('xb') as output:
            agent_created=True;os.chmod(agent,0o600);plistlib.dump(plist,output)
        if start:
            domain=f'gui/{os.getuid()}'
            runner(['launchctl','enable',f'{domain}/{LABEL}'],check=True)
            runner(['launchctl','bootstrap',domain,str(agent)],check=True)
    except BaseException:
        if agent_created:
            if start:runner(['launchctl','bootout',f'gui/{os.getuid()}',str(agent)],check=False,capture_output=True)
            agent.unlink(missing_ok=True)
        if runtime_created:shutil.rmtree(runtime)
        raise
    finally:
        if temporary.exists():shutil.rmtree(temporary)
    return agent


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bot',required=True,help='Exact name of your existing Bot')
    parser.add_argument('--node',default='node',help='Node executable; resolved to an absolute path')
    parser.add_argument('--start',action='store_true',help='Also start now (can produce voice)')
    args=parser.parse_args()
    install_service(Path(__file__).resolve().parents[1],Path.home(),args.bot,args.node,args.start)
    print('GROK_PET_SERVICE_INSTALLED')


if __name__=='__main__':
    try:main()
    except Exception as error:
        print('SERVICE_INSTALL_FAILED: '+(str(error) if isinstance(error,ValueError) and str(error).isupper() else type(error).__name__))
        raise SystemExit(1)
