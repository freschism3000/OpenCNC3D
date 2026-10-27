"""Where the mockup generator reads and writes. Everything is relative to the checkout."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.dirname(HERE)                                  # art/interface/ra-enhanced-menu-v01
ROOT = os.path.abspath(os.path.join(ART, '..', '..', '..'))
WORK = os.environ.get('RAMENU_WORK', os.path.join(ROOT, 'build-native', 'ra-enhanced-menu-v01'))
OUT = os.path.join(ART, 'mockups')
EMBLEMS = os.path.join(ART, 'emblems')
RADATA = os.path.join(ROOT, 'data', 'radata')
# inputs that are build or lab products, each rebuilt by its own tool
PLAY = os.environ.get('OPENCNC3D_PLAY', os.path.join(ROOT, 'playable'))            # ecur/ecur.bin
LAB_RA = os.path.join(ROOT, 'build-native', 'remaster-ra')                          # TSLA render, map textures
BATTLE = os.environ.get('RAMENU_BATTLE', os.path.join(ROOT, 'tools', 'art', 'remaster', 'viewer', 'interface',
                                                      'spatial', 'reference', 'ra-enhanced-capture.png'))
for p in (os.path.join(ROOT, 'tools', 'ra3d'), os.path.join(ROOT, 'menu', 'tools'), os.path.join(ROOT, 'menu'),
          os.path.join(ROOT, 'game'), HERE):
    if p not in sys.path: sys.path.insert(0, p)
