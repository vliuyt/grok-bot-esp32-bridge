import importlib.util
import json
import os
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest
spec=importlib.util.spec_from_file_location('service',Path(__file__).resolve().parents[1]/'scripts/install-service.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class ServiceTests(unittest.TestCase):
    def test_missing_setup_does_not_leave_runtime(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)/'source';home=Path(d)/'home';root.mkdir()
            runner=lambda *a,**kw:subprocess.CompletedProcess(a,0,'v22.19.0\n')
            with self.assertRaisesRegex(ValueError,'FOREGROUND'):m.install_service(root,home,'Example Bot','python3',runner=runner)
            self.assertFalse((home/'Library/Application Support/GrokPetBridge').exists())
    def test_relative_node_is_resolved_before_runtime_directory_changes(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)/'source';home=Path(d)/'home';(root/'src').mkdir(parents=True)
            (root/'src/cli.mjs').write_text('//fixture');(root/'.local').mkdir()
            (root/'.local/device.json').write_text(json.dumps({'host':'192.168.1.20','port':8790,'key':'f'*64}))
            node=root/'fake-node';node.write_text('#!/bin/sh\necho v22.19.0\n');node.chmod(0o755)
            runner=lambda *args,**kw:subprocess.CompletedProcess(args,0,'v22.19.0\n')
            agent=m.install_service(root,home,'Example Bot',os.path.relpath(node,Path.cwd()),runner=runner)
            executable=plistlib.loads(agent.read_bytes())['ProgramArguments'][0]
            self.assertTrue(Path(executable).is_absolute())
            self.assertEqual(Path(executable),node.resolve())

    def test_custom_bot_absolute_executable_and_bootstrap_rollback(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)/'source';home=Path(d)/'home';(root/'src').mkdir(parents=True);(root/'src/cli.mjs').write_text('//fixture');(root/'.local').mkdir()
            (root/'.local/device.json').write_text(json.dumps({'host':'192.168.1.20','port':8790,'key':'f'*64}))
            def runner(args,**kw):
                if args[-1]=='--version':return subprocess.CompletedProcess(args,0,'v22.19.0\n')
                if 'bootstrap' in args:raise subprocess.CalledProcessError(1,args)
                return subprocess.CompletedProcess(args,0)
            with self.assertRaises(subprocess.CalledProcessError):m.install_service(root,home,'My Bot','python3',True,runner)
            self.assertFalse((home/'Library/Application Support/GrokPetBridge').exists())
            self.assertFalse((home/f'Library/LaunchAgents/{m.LABEL}.plist').exists())
            agent=m.install_service(root,home,'My Bot','python3',False,runner)
            data=plistlib.loads(agent.read_bytes());self.assertEqual(data['ProgramArguments'][-1],'My Bot');self.assertTrue(Path(data['ProgramArguments'][0]).is_absolute())
            self.assertEqual((home/'Library/Application Support/GrokPetBridge/.local/device.json').stat().st_mode&0o777,0o600)
if __name__=='__main__':unittest.main()
