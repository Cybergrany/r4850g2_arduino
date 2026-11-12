"""Decoder handles mixed console output, truncation, reconnects, reboot and wrap."""
import importlib.util
from pathlib import Path
import sys
import unittest
sys.dont_write_bytecode = True
spec=importlib.util.spec_from_file_location('bus',Path(__file__).resolve().parents[1]/'tools/analyze_bus_profile.py')
bus=importlib.util.module_from_spec(spec); sys.modules[spec.name]=bus; spec.loader.exec_module(bus)
def summary(ms, rx=0, tx=0):
    values=dict.fromkeys(bus.SUMMARY_FIELDS,0); values.update(ms=ms,rx=rx,tx_info=tx)
    return '> BUS S,'+','.join(str(values[f]) for f in bus.SUMMARY_FIELDS)+'\r\n'
class Decoder(unittest.TestCase):
    def test_wrap_and_boot(self):
        text=summary(0xffffff00,0xfffffffe,65535)+summary(500,2,1)+'hello\n'+summary(1500,3,2)
        captures=bus.parse(text)
        self.assertEqual(len(captures),1)
        self.assertEqual(bus.delta(captures[0].summaries,'rx'),5)
        self.assertEqual(bus.delta(captures[0].summaries,'tx_info'),3)
        self.assertEqual(len(bus.parse(text+summary(100,0,0))),2)
    def test_event_order_and_truncation(self):
        text=summary(1000)+'> BUS E t=980 rx-ack addr=9 value=1 detail=308\n'
        text+='BUS S,2000,123\nBUS E garbage\n'+summary(3000)
        capture=bus.parse(text)[0]
        self.assertEqual(capture.malformed,2)
        self.assertIn('[fan=MAX] [accepted]',bus.describe_event(capture.events[0]))
        self.assertEqual(len(capture.summaries),2)
    def test_unit(self):
        fields=['1000','9','2','6C6884641855','0','8','1','1','6','0','2','3','4','17.0','20.0','54.0','0.0','230.0','1000','1000','0','5400','26']
        c=bus.parse('BUS U,'+','.join(fields))[0]
        self.assertEqual(c.malformed,0)
        self.assertEqual(c.units[0]['input_c'],17)
        self.assertEqual(c.units[0]['identity'],'6C6884641855')
if __name__=='__main__': unittest.main()
