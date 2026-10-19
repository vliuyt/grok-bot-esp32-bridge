import importlib.util
import binascii
import json
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'firmware/tools'))
import pet_install as m


def flash_image():
    data=bytearray(b'\xff'*m.FLASH_BYTES)
    entries=[(1,2,0x11000,0xc000,'nvs'),(1,0,0x1d000,0x2000,'otadata'),(1,1,0x1f000,0x1000,'phy_init'),(0,0x10,0x20000,0x3e0000,'ota_0'),(0,0x11,0x400000,0x3e0000,'ota_1'),(1,0x40,0x7e0000,0x1000,'prod_data'),(1,0x41,0x7e1000,0x1000,'prod_bak')]
    for i,(kind,sub,offset,size,name) in enumerate(entries):
        data[m.TABLE_OFFSET+i*32:m.TABLE_OFFSET+(i+1)*32]=struct.pack('<HBBII16sI',0x50aa,kind,sub,offset,size,name.encode(),0)
    struct.pack_into('<I',data,0x1d000,1)
    struct.pack_into('<I',data,0x1d000+24,2)
    struct.pack_into('<I',data,0x1d000+28,1195612314)
    return data


class PublicInstallTests(unittest.TestCase):
    def test_only_known_partition_layout_and_default_boot_allowed(self):
        data=flash_image();m.require_safe_layout(data)
        data[m.TABLE_OFFSET+3*32+4:m.TABLE_OFFSET+3*32+8]=struct.pack('<I',0x10000)
        with self.assertRaisesRegex(ValueError,'PARTITION'):m.require_safe_layout(data)
        data=flash_image();data[0x1d000]=2
        with self.assertRaisesRegex(ValueError,'OTA'):m.require_safe_layout(data)
        with self.assertRaises(ValueError):m.require_safe_layout(b'x')
    def test_partition_offset_matches_target_config(self):
        import re
        config=(Path(m.__file__).resolve().parents[1]/'pet/sdkconfig.defaults').read_text()
        expected=int(re.search(r'CONFIG_PARTITION_TABLE_OFFSET=(0x[0-9a-fA-F]+)',config)[1],16)
        self.assertEqual(m.TABLE_OFFSET,expected)
        data=flash_image();table=bytes(data[m.TABLE_OFFSET:m.TABLE_OFFSET+0x1000])
        data[m.TABLE_OFFSET:m.TABLE_OFFSET+0x1000]=b'\xff'*0x1000
        data[0x8000:0x9000]=table
        with self.assertRaisesRegex(ValueError,'PARTITION'):m.require_safe_layout(data)

    def test_confirmed_ota_zero_is_supported_but_other_selections_are_refused(self):
        def record(data,offset,seq,state=2,broken=False):
            struct.pack_into('<I',data,offset,seq)
            struct.pack_into('<I',data,offset+24,state)
            crc=binascii.crc32(struct.pack('<I',seq),0xffffffff)&0xffffffff
            struct.pack_into('<I',data,offset+28,crc^(1 if broken else 0))
        data=flash_image();record(data,0x1d000,1)
        self.assertEqual(struct.unpack_from('<I',data,0x1d000+28)[0],1195612314)
        m.require_safe_layout(data)
        for seq,state,broken in [(2,2,False),(1,1,False),(1,3,False),(1,2,True),(0,2,False)]:
            data=flash_image();record(data,0x1d000,seq,state,broken)
            with self.assertRaisesRegex(ValueError,'OTA'):m.require_safe_layout(data)
        data=flash_image();record(data,0x1d000,2);record(data,0x1e000,3)
        m.require_safe_layout(data)
        record(data,0x1d000,4)
        with self.assertRaisesRegex(ValueError,'OTA'):m.require_safe_layout(data)

    def test_erased_ota_is_not_stable_across_a_first_boot(self):
        data=flash_image();data[0x1d000:0x1f000]=b'\xff'*0x2000
        with self.assertRaisesRegex(ValueError,'OTA'):m.require_safe_layout(data)

    def test_new_backup_has_no_original_author_device_dependency(self):
        text=Path(m.__file__).read_text()
        self.assertNotIn('docs/device-backup.json',text)
        self.assertNotIn('grok-voice.bin',text)
        self.assertNotIn('muse-gadget-sdk',text)

if __name__=='__main__':unittest.main()
