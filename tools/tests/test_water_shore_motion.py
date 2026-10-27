#!/usr/bin/env python3
"""Negative controls for the source-extracted shore-motion contract."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT=Path(__file__).resolve().parents[2]
GATE=ROOT/'tools/gate-water-shore-motion.py'
SOURCE=ROOT/'game/water_mod.h'


class ShoreMotionControls(unittest.TestCase):
    def run_gate(self,source):
        with tempfile.TemporaryDirectory(prefix='cnc3d-shore-motion-') as directory:
            directory=Path(directory);header=directory/'water_mod.h';header.write_text(source)
            result=subprocess.run([sys.executable,str(GATE),'--source',str(header),'--output',str(directory/'result')],capture_output=True,text=True)
            report=directory/'result/RESULTS.json'
            return result.returncode,json.loads(report.read_text()) if report.exists() else None,result.stderr

    def test_current_source(self):
        code,result,error=self.run_gate(SOURCE.read_text())
        self.assertEqual(code,0,error)
        self.assertTrue(result['passed'])

    def test_nearly_synchronized_clock_is_rejected(self):
        raw=SOURCE.read_text().replace('return n*0.90;', 'return n*0.20;')
        self.assertNotEqual(raw,SOURCE.read_text())
        code,result,_=self.run_gate(raw)
        self.assertNotEqual(code,0)
        self.assertFalse(result['passed'])

    def test_unfaded_section_sides_are_rejected(self):
        raw=SOURCE.read_text().replace('return sqrt(max(s.x,1.0-s.x)*max(s.y,1.0-s.y));', 'return 1.0;')
        code,result,_=self.run_gate(raw)
        self.assertNotEqual(code,0)
        self.assertFalse(result['passed'])

    def test_old_pulsed_travel_is_rejected(self):
        raw=SOURCE.read_text().replace('float speed=max(baseSpeed,0.0);','float speed=max(baseSpeed,0.0)*30.0*u*u*(1.0-u)*(1.0-u);').replace('float travel=time*speed;','float travel=max(baseSpeed,0.0)*(floor(tau)+advance);')
        self.assertNotEqual(raw,SOURCE.read_text())
        code,result,_=self.run_gate(raw)
        self.assertNotEqual(code,0)
        self.assertFalse(result['passed'])

    def test_constant_chart_presence_is_rejected(self):
        raw=SOURCE.read_text().replace('return spatial*mix(coastPresence(wave),1.0,river);', 'return spatial;')
        code,result,_=self.run_gate(raw)
        self.assertNotEqual(code,0)
        self.assertFalse(result['passed'])

    def test_unfaded_beach_chart_is_rejected(self):
        raw=SOURCE.read_text().replace('coast.z=coastInfluence(coast.z,river,wave);','')
        code,result,error=self.run_gate(raw)
        self.assertNotEqual(code,0)
        self.assertIsNone(result)
        self.assertIn('shared sampled-colour/normal',error)

    def test_constant_speed_is_rejected(self):
        raw=SOURCE.read_text().replace('speed*=0.20+gain*surge;', 'speed*=1.0;')
        code,result,_=self.run_gate(raw)
        self.assertNotEqual(code,0)
        self.assertFalse(result['passed'])

    def test_time_times_changing_speed_is_rejected(self):
        raw=SOURCE.read_text().replace('travel=speed*(time+gain*correction/rate);', 'travel=time*speed*(0.20+gain*surge);')
        code,result,_=self.run_gate(raw)
        self.assertNotEqual(code,0)
        self.assertFalse(result['passed'])

    def test_ignored_hold_is_rejected(self):
        raw=SOURCE.read_text().replace('clamp(uSwashHold,0.0,10.0)*rate', '0.0*rate')
        self.assertNotEqual(raw,SOURCE.read_text())
        code,result,_=self.run_gate(raw)
        self.assertNotEqual(code,0)
        self.assertFalse(result['passed'])

    def test_lingering_zero_hold_is_rejected(self):
        raw=SOURCE.read_text().replace('advance:(1.0-back)*(1.0-back)', 'advance:1.0-coastS5(back)')
        self.assertNotEqual(raw,SOURCE.read_text())
        code,result,_=self.run_gate(raw)
        self.assertNotEqual(code,0)
        self.assertFalse(result['passed'])

    def test_old_fourteen_cell_sea_band_is_rejected(self):
        raw=SOURCE.read_text().replace('float inner=mix(uAlignLength,0.10,river),outer=mix(uAlignLength+uAlignBlend,0.65,river);','float inner=mix(2.0,0.10,river),outer=mix(14.0,0.65,river);')
        self.assertNotEqual(raw,SOURCE.read_text())
        code,result,_=self.run_gate(raw)
        self.assertNotEqual(code,0)
        self.assertFalse(result['passed'])

    def test_periodic_uv_reset_is_rejected(self):
        raw=SOURCE.read_text().replace('float travel=time*speed;','float travel=fract(time)*speed;')
        self.assertNotEqual(raw,SOURCE.read_text())
        code,result,_=self.run_gate(raw)
        self.assertNotEqual(code,0)
        self.assertFalse(result['passed'])

    def test_mismatched_normal_motion_is_rejected(self):
        raw=SOURCE.read_text().replace('waterCoastNormal(coastX*1.35+0.5,coastZ*1.35+0.5,coastWeight,coast.xy)','waterCoastNormal(coastX*1.35+uTime,coastZ*1.35+0.5,coastWeight,coast.xy)')
        self.assertNotEqual(raw,SOURCE.read_text())
        code,result,error=self.run_gate(raw)
        self.assertNotEqual(code,0)
        self.assertIsNone(result)
        self.assertIn('shared sampled-colour/normal',error)


if __name__=='__main__':unittest.main()
