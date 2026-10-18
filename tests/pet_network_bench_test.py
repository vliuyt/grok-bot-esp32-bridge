import unittest,pathlib,sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'firmware/tools'))
import pet_network_bench as m
class GateTests(unittest.TestCase):
 def test_ble_and_cleanup_are_required(self):
  report={key:True for key in m.REQUIRED_CHECKS};self.assertTrue(m.acceptance_passed(report))
  report['bleAdvertisementSeen']=False;self.assertFalse(m.acceptance_passed(report))
  report['bleAdvertisementSeen']=True;report['setupClosedFinally']=False;self.assertFalse(m.acceptance_passed(report))
 def test_closed_window_probe_is_valid_configuration(self):
  config={'host':'192.168.1.2','port':8790,'key':'f'*64}
  probe=m.pairing_request(config,'closed');self.assertEqual(probe['op'],'configure');self.assertEqual(probe['bridge']['key'],config['key'])
if __name__=='__main__':unittest.main()
