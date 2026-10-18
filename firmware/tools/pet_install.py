"""Verified private backup and application-only installation for the supported board."""
from pathlib import Path
import argparse
import contextlib
import hashlib
import json
import os
import struct
import sys
from datetime import datetime, timezone
sys.path.insert(0,str(Path(__file__).resolve().parent))
from pet_artifacts import verify_application, commit_backup, recover_backup

ROOT = Path(__file__).resolve().parents[2]
BOARD = 'Waveshare ESP32-S3-Touch-AMOLED-1.43C'
FLASH_BYTES = 0x800000
TABLE_OFFSET = 0x10000
APP_OFFSET, APP_END = 0x20000, 0x400000
PARTITIONS = [('nvs',1,2,0x11000,0xc000),('otadata',1,0,0x1d000,0x2000),
              ('phy_init',1,1,0x1f000,0x1000),('ota_0',0,0x10,APP_OFFSET,0x3e0000),
              ('ota_1',0,0x11,0x400000,0x3e0000),('prod_data',1,0x40,0x7e0000,0x1000),
              ('prod_bak',1,0x41,0x7e1000,0x1000)]


def ensure_new(path):
    if path.exists():raise ValueError('BACKUP_EXISTS_DO_NOT_OVERWRITE')


def require_board(status):
    if not status or status.get('board') != BOARD:raise ValueError('WRONG_BOARD')


def require_safe_layout(data):
    if len(data)!=FLASH_BYTES:raise ValueError('PARTITION_FLASH_SIZE_MISMATCH')
    found=[]
    for index in range(TABLE_OFFSET,TABLE_OFFSET+0x1000,32):
        magic,kind,sub,offset,size,label,flags=struct.unpack('<HBBII16sI',data[index:index+32])
        if magic in (0xffff,0xebeb):break
        if magic!=0x50aa or flags:raise ValueError('PARTITION_TABLE_UNSUPPORTED')
        found.append((label.split(b'\0',1)[0].decode('ascii'),kind,sub,offset,size))
    if found!=PARTITIONS:raise ValueError('PARTITION_LAYOUT_UNSUPPORTED')
    # A non-default OTA selection might boot ota_1 after writing ota_0. Never rewrite it.
    if data[0x1d000:0x1f000]!=b'\xff'*0x2000:raise ValueError('OTA_SELECTION_UNSUPPORTED')


def verified_backup(root,manifest,device):
    path=(root/manifest['path']).resolve()
    if not path.is_relative_to((root/'.local').resolve()):raise ValueError('BACKUP_PATH_REFUSED')
    if (manifest.get('verified') is not True or manifest.get('bytes')!=FLASH_BYTES
            or manifest.get('deviceIdHash')!=device or path.stat().st_size!=FLASH_BYTES
            or hashlib.sha256(path.read_bytes()).hexdigest()!=manifest.get('sha256')):
        raise ValueError('BACKUP_NOT_VERIFIED')
    return path


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['backup','install','restore'])
    parser.add_argument('--port',help='USB serial port; auto-selection requires exactly one board')
    parser.add_argument('--board',choices=[BOARD],help='Required physical board confirmation for the first backup')
    args=parser.parse_args()
    if args.action=='backup' and args.board!=BOARD:raise ValueError('BOARD_CONFIRMATION_REQUIRED')
    import esptool.cmds as cmds
    from serial.tools import list_ports
    sys.path.insert(0,str(ROOT/'firmware/tools/vendor'))
    import chat
    port=args.port or chat.pick_port()
    usb=next((p for p in list_ports.comports() if p.device==port),None)
    if not usb or not usb.serial_number:raise ValueError('DEVICE_ID_REQUIRED')
    device=hashlib.sha256(usb.serial_number.encode()).hexdigest()
    private=ROOT/'.local'
    if private.is_symlink():raise ValueError('PRIVATE_DIRECTORY_SYMLINK_REFUSED')
    private.mkdir(mode=0o700,exist_ok=True);os.chmod(private,0o700)
    record=private/'device-backup.json'
    if args.action=='backup':
        if recover_backup(ROOT,record,device):print('PET_BACKUP_COMMIT_RECOVERED');return
        ensure_new(record)
        path=private/'before-pet-full.bin';partial=path.with_suffix('.partial')
        ensure_new(path);ensure_new(partial)
    else:
        manifest=json.loads(record.read_text())
        require_board(manifest)
        path=verified_backup(ROOT,manifest,device)
        backup=path.read_bytes();require_safe_layout(backup)
        if args.action=='restore':
            image=private/'before-pet-application-slot.bin'
            image.write_bytes(backup[APP_OFFSET:APP_END]);os.chmod(image,0o600)
        else:image=verify_application(ROOT,json.loads((ROOT/'docs/pet-firmware-manifest.json').read_text()))
    with (private/('pet-'+args.action+'.log')).open('w') as log,contextlib.redirect_stdout(log),contextlib.redirect_stderr(log):
        esp=cmds.detect_chip(port,baud=460800,connect_mode='default-reset')
        try:
            if esp.CHIP_NAME!='ESP32-S3':raise ValueError('WRONG_CHIP')
            esp=esp.run_stub()
            if 1<<((esp.flash_id()>>16)&0xff)!=FLASH_BYTES:raise ValueError('WRONG_FLASH_SIZE')
            if args.action=='backup':
                cmds.read_flash(esp,0,FLASH_BYTES,str(partial),no_progress=True)
                if partial.stat().st_size!=FLASH_BYTES:raise ValueError('BACKUP_INCOMPLETE')
                cmds.verify_flash(esp,[(0,str(partial))])
                os.chmod(partial,0o600);data=partial.read_bytes()
                result={'board':BOARD,'path':'.local/'+path.name,'bytes':len(data),'verified':True,
                        'sha256':hashlib.sha256(data).hexdigest(),'deviceIdHash':device,
                        'verifiedAt':datetime.now(timezone.utc).isoformat()}
                commit_backup(ROOT,partial,path,result,record);os.chmod(record,0o600)
            else:
                # Refuse changed boot metadata. Only the validated app slot is ever written.
                for offset,size in [(TABLE_OFFSET,0x1000),(0x1d000,0x2000)]:
                    if cmds.read_flash(esp,offset,size,no_progress=True)!=backup[offset:offset+size]:
                        raise ValueError('BOOT_METADATA_CHANGED')
                cmds.write_flash(esp,[(APP_OFFSET,str(image))]);cmds.verify_flash(esp,[(APP_OFFSET,str(image))])
        finally:
            try:esp.hard_reset()
            finally:esp._port.close()
    print('PET_'+args.action.upper()+'_VERIFIED')


if __name__=='__main__':
    try:main()
    except Exception as error:
        # Print only known validation codes; third-party errors may contain private data.
        code=str(error) if isinstance(error,ValueError) and str(error).isupper() else type(error).__name__
        print('PET_OPERATION_FAILED: '+code+'; see .local/pet-<action>.log',file=sys.stderr)
        raise SystemExit(1)
