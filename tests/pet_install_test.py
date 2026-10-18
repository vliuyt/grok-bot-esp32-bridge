import importlib.util,pathlib,tempfile,unittest,hashlib,json
source=pathlib.Path(__file__).resolve().parents[1]/'firmware/tools/pet_install.py'
spec=importlib.util.spec_from_file_location('pet_install',source);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class GuardTests(unittest.TestCase):
 def test_backup_guards(self):
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d);(root/'.local').mkdir();blob=b'x'*0x800000;p=root/'.local/test.bin';p.write_bytes(blob)
   manifest={'path':'.local/test.bin','bytes':len(blob),'sha256':hashlib.sha256(blob).hexdigest(),'verified':True,'deviceIdHash':'device'}
   self.assertEqual(m.verified_backup(root,manifest,'device'),p.resolve())
   with self.assertRaises(ValueError):m.verified_backup(root,manifest,'wrong')
   manifest['sha256']='wrong'
   with self.assertRaises(ValueError):m.verified_backup(root,manifest,'device')
   manifest['sha256']=hashlib.sha256(blob).hexdigest();p.write_bytes(b'x')
   with self.assertRaises(ValueError):m.verified_backup(root,manifest,'device')
   manifest['path']='../outside'
   with self.assertRaises(ValueError):m.verified_backup(root,manifest,'device')
 def test_refuses_existing_and_wrong_board(self):
  with tempfile.TemporaryDirectory() as d:
   p=pathlib.Path(d)/'backup';p.write_text('keep')
   with self.assertRaises(ValueError):m.ensure_new(p)
   with self.assertRaises(ValueError):m.require_board({'board':'other'})
   m.require_board({'board':m.BOARD})
 def test_restore_never_queries_a_broken_application(self):
  import types,sys
  from unittest.mock import patch
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d);(root/'.local').mkdir();(root/'docs').mkdir();serial='test-device';device=hashlib.sha256(serial.encode()).hexdigest()
   from public_install_test import flash_image
   blob=bytes(flash_image());backup=root/'.local/backup.bin';backup.write_bytes(blob)
   manifest={'board':m.BOARD,'path':'.local/backup.bin','bytes':len(blob),'sha256':hashlib.sha256(blob).hexdigest(),'verified':True,'deviceIdHash':device}
   (root/'.local/device-backup.json').write_text(json.dumps(manifest))
   calls=[]
   class ESP:
    CHIP_NAME='ESP32-S3'
    _port=types.SimpleNamespace(close=lambda:None)
    def run_stub(self):return self
    def flash_id(self):return 0x170000
    def hard_reset(self):calls.append('reset')
   cmds=types.ModuleType('esptool.cmds');cmds.detect_chip=lambda *a,**kw:ESP();cmds.write_flash=lambda esp,blocks:calls.append(('write',blocks[0][0],pathlib.Path(blocks[0][1]).stat().st_size));cmds.verify_flash=lambda *a:calls.append('verify')
   cmds.read_flash=lambda esp,offset,size,**kw:blob[offset:offset+size]
   esptool=types.ModuleType('esptool');esptool.cmds=cmds
   chat=types.ModuleType('chat');chat.pick_port=lambda:'fake';chat.Board=lambda port:(_ for _ in ()).throw(AssertionError('must not read broken app'))
   serial_module=types.ModuleType('serial');tools=types.ModuleType('serial.tools');ports=types.ModuleType('serial.tools.list_ports');ports.comports=lambda:[types.SimpleNamespace(device='fake',serial_number=serial)];tools.list_ports=ports;serial_module.tools=tools
   with patch.object(m,'ROOT',root),patch.object(sys,'argv',['pet_install','restore']),patch.dict(sys.modules,{'esptool':esptool,'esptool.cmds':cmds,'chat':chat,'serial':serial_module,'serial.tools':tools,'serial.tools.list_ports':ports}):m.main()
   self.assertEqual(calls,[('write',0x20000,0x3e0000),'verify','reset'])
class ArtifactTests(unittest.TestCase):
 def test_stale_source_refused(self):
  import sys
  sys.path.insert(0,str(source.parent))
  from pet_artifacts import publish, verify_application
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d);(root/'firmware/pet').mkdir(parents=True);(root/'firmware/pet/main.c').write_text('first');(root/'firmware/pet/dependencies.lock').write_text('locked');(root/'docs').mkdir()
   image=root/'image';image.write_bytes(b'\xe9'+b'x'*2047)
   manifest=publish(root,image,'sdkhash')
   self.assertEqual(verify_application(root,manifest).read_bytes(),image.read_bytes())
   (root/'firmware/pet/main.c').write_text('new')
   with self.assertRaises(ValueError):verify_application(root,manifest)
 def test_recover_interrupted_backup_commit(self):
  from pet_artifacts import commit_backup,recover_backup,atomic_json
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d);(root/'.local').mkdir();(root/'docs').mkdir();partial=root/'.local/flash.partial';path=root/'.local/flash.bin';partial.write_bytes(b'x'*m.FLASH_BYTES)
   data=partial.read_bytes();manifest={'path':'.local/flash.bin','bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'verified':True,'deviceIdHash':'device'};record=root/'docs/backup.json'
   atomic_json(record.with_suffix('.pending.json'),manifest)
   self.assertTrue(recover_backup(root,record,'device'));self.assertFalse(partial.exists());self.assertTrue(path.exists());self.assertEqual(json.loads(record.read_text()),manifest)
   self.assertEqual(m.verified_backup(root,manifest,'device'),path.resolve())
   with self.assertRaises(ValueError):commit_backup(root,partial,path,manifest,record)
if __name__=='__main__':unittest.main()
