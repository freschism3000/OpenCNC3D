#!/usr/bin/env python3
"""Calibrate rejection of incomplete and frozen-frame comparison reports."""
import importlib.util
from pathlib import Path
import tempfile

ROOT=Path(__file__).resolve().parent
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/file)
    value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value
analyzer=module('analyzer','analyze-comparison.py');maker=module('maker','make-comparison.py')


def log(case,timing,n=1):
    phases={'active':[('intact',1),('first-damage',1),('moving',32),('settling',32),('sinking',32),('late',32)],'siege':[('active-siege',80)]}[case]
    out=[]
    for phase,count in phases:
        out.append('ECHO|BENCH|phase='+phase)
        out.extend(f'FRAMEBENCH|n={n}|median={timing:.2f}|p90={timing:.2f}' for _ in range(count))
    return '\n'.join(out)+'\n'


with tempfile.TemporaryDirectory(prefix='comparison-regression-') as t:
    folder=Path(t)
    for variant,value in [('baseline-1',20),('baseline-2',20),('optimized-1',15),('optimized-2',15)]:
        for case in ['active','siege']:(folder/(variant+'-'+case+'.log')).write_text(log(case,value))
    x=analyzer.analyze(folder)
    assert x['statistics']['active']['moving']['median_saved_ms']==5
    assert x['statistics']['active']['moving']['median_reduction_percent']==25
    assert x['statistics']['active']['moving']['baseline']['n']==64
    assert x['statistics']['siege']['active-siege']['optimized']['n']==160
    assert x['warnings']==[]
    path=folder/'baseline-1-active.log';good=path.read_text()
    path.write_text(log('active',20,n=60))
    try:analyzer.analyze(folder);raise AssertionError('Frozen multi-frame medians were accepted as active samples')
    except ValueError:pass
    path.write_text(good);path.unlink()
    try:analyzer.analyze(folder);raise AssertionError('A missing baseline was accepted')
    except ValueError:pass
    path.write_text(good)
    (folder/'baseline-2-active.log').write_text(log('active',32))
    assert any('drift' in w for w in analyzer.analyze(folder)['warnings'])
    for case,text in maker.scripts().items():
        lines=text.splitlines()
        # Every active measured sample follows a changed engine tick and alpha.
        for i,line in enumerate(lines):
            if line=='framebench 1' and i>=2 and lines[i-1].startswith('tickalpha '):assert lines[i-2]=='tick 1'
        assert 'renderdump ' not in text and 'shot ' not in text and 'save ' not in text
    assert 'powershell' not in maker.CMD.lower() and 'python' not in maker.CMD.lower()
    assert 'setlocal DisableDelayedExpansion' in maker.CMD
    assert 'OPENCNC3D_GENERATED_MODE_MASK' in maker.CONTROLS
    assert 'input.cfg" >nul || goto failed' in maker.CMD
    assert all('||' in row for row in maker.CMD.splitlines() if row.startswith('echo ') and 'status.txt' in row)
    assert all('|| exit /b 1' in row for row in maker.CMD.splitlines() if row.startswith('findstr /b ') and 'report' in row)
print('comparison tests: complete-pair math, sample counts, frozen/missing report rejection, drift warning, active script stepping, private output and batch constraints PASS')
