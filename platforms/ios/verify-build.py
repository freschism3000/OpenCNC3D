#!/usr/bin/env python3
"""Verify app platform, static dependency boundary and staged resource integrity."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('sdk', choices=['iphoneos','iphonesimulator'])
parser.add_argument('--app', type=Path)
args = parser.parse_args()
app = (args.app or root/'build'/args.sdk/'OpenCNC3DiOS.app').resolve()
binary = app/'OpenCNC3DiOS'
env = dict(os.environ, DEVELOPER_DIR=os.environ.get('DEVELOPER_DIR','/Applications/Xcode.app/Contents/Developer'))
def run(*command):
    return subprocess.check_output(command, env=env, text=True, stderr=subprocess.STDOUT)
platform = run('xcrun','vtool','-show-build',str(binary))
expected = 'IOSSIMULATOR' if args.sdk == 'iphonesimulator' else 'IOS'
assert f'platform {expected}\n' in platform, platform
arch = run('xcrun','lipo','-archs',str(binary)).strip()
assert arch == 'arm64', arch
links = run('otool','-L',str(binary))
for line in links.splitlines()[1:]:
    dependency = line.strip().split(' (')[0]
    assert dependency.startswith(('/System/Library/','/usr/lib/')), dependency
stage = json.loads(run('python3',str(root/'resources/stage.py'),'--output',str(app/'GameData'),'--verify'))
report = {'sdk':args.sdk,'architecture':arch,'platform':expected,
          'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
          'bundle':str(app),'resources':stage,'dependencies':links.splitlines()[1:],
          'physical_device_validated':False}
print(json.dumps(report,indent=2))
