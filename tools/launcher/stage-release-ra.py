#!/usr/bin/env python3
"""Stage the independently packaged RA runtime for hidden release gates.

The source folder is read only. Output must be a new private directory; it may
be linked temporarily into the release checkout's otherwise RA-free playable/.
"""
import argparse
import importlib.util
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
for name in ('source', 'out', 'app', 'brain'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
source = a.source.resolve()
out = a.out.resolve()
if source == out or source in out.parents or out in source.parents:
    p.error('release output must be separate from preserved input')
spec = importlib.util.spec_from_file_location('release_modules', Path(__file__).with_name('package-modules.py'))
modules = importlib.util.module_from_spec(spec)
spec.loader.exec_module(modules)
modules.stage_ra(source, a.out, a.app, a.brain)
