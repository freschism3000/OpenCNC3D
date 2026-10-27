"""Reject mixing native GLES and emulated desktop GL through the state cache."""
import os
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SOURCE = Path(os.environ.get('OPENCNC3D_SOURCE_ROOT', str(Path.home() / 'CNC3D')))

class PlatformBoundary(unittest.TestCase):
    def test_all_renderer_cache_entrypoints_still_reach_gl4es(self):
        cache = (SOURCE / 'game/gl_state_cache.h').read_text()
        overrides = set(re.findall(r'^#define (gl\w+)\(', cache, re.MULTILINE))
        bridge = (ROOT / 'compat/OpenGL/gl_cache_bridge.h').read_text()
        forwarded = set(re.findall(r'static inline \w+ (gl\w+)\(', bridge))
        self.assertGreater(len(overrides), 30)
        self.assertEqual(overrides - forwarded, set(), 'New cache entrypoints need an iOS GL4ES forwarding function')
        for function in overrides:
            self.assertRegex(bridge, rf'{function}\([^\n]+gl4es_{function}\(', function)

if __name__ == '__main__':
    unittest.main()
