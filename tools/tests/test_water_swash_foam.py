"""Negative controls for foam lifecycle and its independent transport."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
GATE=ROOT/'tools/gate-water-swash-foam.py'
spec=importlib.util.spec_from_file_location('foam_gate',GATE);gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)

class SwashFoam(unittest.TestCase):
    def source(self):return (ROOT/'game/water_mod.h').read_text()
    def run_gate(self,raw):
        with tempfile.TemporaryDirectory(prefix='cnc3d-foam-life-')as t:
            p=Path(t);(p/'water.h').write_text(raw)
            return subprocess.run([sys.executable,str(GATE),'--source',str(p/'water.h'),'--output',str(p/'results')],capture_output=True,text=True)
    def test_actual_lifecycle(self):
        r=self.run_gate(self.source());self.assertEqual(r.returncode,0,r.stdout+r.stderr)
    def test_forward_scrolling_lip_rejected(self):
        raw=self.source().replace('-sd+front','-sd-wave.z')
        with self.assertRaises(ValueError):gate.check_source(raw)
    def test_sliding_residue_rejected(self):
        raw=self.source().replace('-sd+peak','-sd+front')
        with self.assertRaises(ValueError):gate.check_source(raw)
    def test_constant_width_rejected(self):
        raw=self.source().replace('mix(0.018,0.052,build)+0.085*spread','0.052')
        self.assertNotEqual(self.run_gate(raw).returncode,0)
    def test_premature_deposit_rejected(self):
        raw=self.source().replace('smoothstep(0.0,0.05,back)', 'smoothstep(0.65,1.0,up)')
        self.assertNotEqual(self.run_gate(raw).returncode,0)
    def test_unfading_foam_rejected(self):
        raw=self.source().replace('*(1.0-smoothstep(0.08,0.85,back))','')
        self.assertNotEqual(self.run_gate(raw).returncode,0)
if __name__=='__main__':unittest.main()
