import sys,pathlib,tempfile,json,unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'firmware/tools'))
import pet_bench as m
class SoakTests(unittest.TestCase):
 def samples(self,leak=0):return [{'seconds':i*30,'uptimeMs':i*30000+1000,'generation':1,'heap':300000-i*leak,'psram':7000000-i*leak,'audioFault':False} for i in range(240)]
 def test_reject_leak_psram_and_reset(self):
  self.assertTrue(m.summarize(self.samples(),7200,7200)['passed'])
  self.assertFalse(m.summarize(self.samples(1000),7200,7200)['passed'])
  bad=self.samples();bad[-1]['psram']=0;self.assertFalse(m.summarize(bad,7200,7200)['passed'])
  bad=self.samples();bad[-1]['uptimeMs']=1;self.assertFalse(m.summarize(bad,7200,7200)['passed'])
  bad=self.samples();bad[-1]['eventDrops']=1;self.assertFalse(m.summarize(bad,7200,7200)['passed'])
 def test_first_failure_replaces_previous_success(self):
  with tempfile.TemporaryDirectory() as d:
   path=pathlib.Path(d)/'soak.json';path.write_text('{"passed":true}')
   def connect():raise RuntimeError('DISCONNECTED')
   report=m.soak(connect,7200,path)
   self.assertFalse(report['passed']);saved=json.loads(path.read_text());self.assertFalse(saved['complete']);self.assertEqual(saved['status'],'failed');self.assertIn('runId',saved)
if __name__=='__main__':unittest.main()
