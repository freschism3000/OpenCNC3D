#!/usr/bin/env python3
"""Check Dock entry points, including permissions that a byte hash cannot prove."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib


def check(runtime, repair=False):
    rows=[]
    for name in ('C&C3D.app','OpenCNC3D.app','Editor.app'):
        bundle=runtime/name
        if not bundle.exists():
            continue
        info=plistlib.loads((bundle/'Contents/Info.plist').read_bytes())
        binary=bundle/'Contents/MacOS'/info['CFBundleExecutable']
        assert binary.is_file() and not binary.is_symlink(), str(binary)
        digest=hashlib.sha256(binary.read_bytes()).hexdigest()
        if repair:
            binary.chmod(binary.stat().st_mode | 0o111)
        assert os.access(binary,os.X_OK) and binary.stat().st_mode & 0o111 == 0o111, \
            'Dock executable lost its execute permission: '+str(binary)
        assert digest==hashlib.sha256(binary.read_bytes()).hexdigest()
        rows.append(dict(path=str(binary),mode=oct(binary.stat().st_mode & 0o777),sha256=digest))
    for name in ('opencnc3d-launcher','opencnc3d','cnc_eyes'):
        p=runtime/name
        assert p.is_file() and os.access(p,os.X_OK), 'Missing executable: '+str(p)
    assert rows, 'No Dock bundles in '+str(runtime)
    return dict(passed=True,runtime=str(runtime),launchers=rows)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('runtime',type=Path)
    p.add_argument('--repair',action='store_true',help='Restore execute bits; preserve every byte')
    p.add_argument('--output',type=Path)
    a=p.parse_args(); result=check(a.runtime.resolve(),a.repair)
    if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
