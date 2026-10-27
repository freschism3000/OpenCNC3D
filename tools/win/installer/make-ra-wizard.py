#!/usr/bin/env python3
"""Fit the accepted RA menu plate/logo to NSIS's fixed wizard bitmap slot.

This is a format/layout adaptation of existing approved native artwork, with no
generated painting, new emblem or texture. Source identities ride in the receipt.
"""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageOps

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[3]
plate = root / 'art/interface/ra-enhanced-menu-v01/kit/title-backdrop-hi.png'
logo = root / 'art/interface/launcher-module-browser-v01/kit/ra.png'
background = ImageOps.fit(Image.open(plate).convert('RGB'), (164, 314),
                          method=Image.Resampling.NEAREST).convert('RGBA')
emblem = Image.open(logo).convert('RGBA')
if emblem.size != (160, 160):
    raise ValueError('accepted RA module emblem dimensions changed')
background.alpha_composite(emblem, (2, 77))
a.output.parent.mkdir(parents=True, exist_ok=True)
background.convert('RGB').save(a.output, format='BMP')
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
a.output.with_suffix('.json').write_text(json.dumps(dict(
    source_roles='accepted RA menu plate and Module Browser RA emblem; format/layout adaptation only',
    sources=[dict(path=str(x.relative_to(root)), sha256=sha(x)) for x in (plate, logo)],
    dimensions=[164, 314], output_sha256=sha(a.output)), indent=2) + '\n')
