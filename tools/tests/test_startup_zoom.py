"""Exercise the shared mission-start camera policy without a GL window."""
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class StartupZoom(unittest.TestCase):
    def test_normal_repeat_load_editor_and_explicit_framing(self):
        source = (ROOT / "game/cnc_eyes.cpp").read_text()
        start = source.index("static void camera_startup_zoom(")
        end = source.index("/* ONE place owns the sense of the zoom control", start)
        # Compile the production helper itself. Only the surrounding renderer state
        # and set_dist's ordinary legal-range clamp are supplied by the fixture.
        helper = source[start:end]
        code = r'''
#include <cassert>
#include <cmath>
#include <cstdio>
static const float DIST_MAX_OURS = 4200.0f;
static bool g_startDistCLI = false, g_startZoomCLI = false;
static float g_startDist = 3000, g_startZoom = 0, g_dist = 1920, g_zoom = 50;
static int g_mapW = 64, g_mapH = 64, basis_updates = 0;
static void set_dist(float d) {
    g_dist = fminf(DIST_MAX_OURS, fmaxf(1920.0f, d));
    ++basis_updates;
}
HELPER
int main() {
    // First start and a later mission/load must give the same far distance,
    // independent of a previous close zoom or restored close-save camera.
    camera_startup_zoom(1280, 720, false);
    assert(g_dist == 4200 && g_zoom > 0 && g_zoom < 20);
    const float overview = g_zoom;
    g_dist = 2400; g_zoom = 40;
    camera_startup_zoom(1280, 720, false);
    assert(g_dist == 4200 && g_zoom == overview && basis_updates == 2);

    // The old camera overview follows the next map, not the previous map's zoom.
    const float large_map = g_zoom;
    g_mapW = 32; g_mapH = 32;
    camera_startup_zoom(1280, 720, false);
    assert(g_zoom == large_map * 2);

    // Deliberate capture framing survives repeated boots and a save restore.
    g_startDistCLI = true; g_startDist = 2733;
    g_startZoomCLI = true; g_startZoom = 37;
    for (int start = 0; start != 2; ++start) {
        g_dist = 1920; g_zoom = 50;
        camera_startup_zoom(1280, 720, false);
        assert(g_dist == 2733 && g_zoom == 37);
    }

    // One explicit camera does not suppress the other camera's default overview.
    g_startZoomCLI = false;
    camera_startup_zoom(1280, 720, false);
    assert(g_dist == 2733 && g_zoom != 37);
    g_startDistCLI = false; g_startZoomCLI = true;
    camera_startup_zoom(1280, 720, false);
    assert(g_dist == 4200 && g_zoom == 37);

    // Invalid explicit distances retain the existing distance setter's bounds.
    g_startDistCLI = true; g_startDist = 9999;
    camera_startup_zoom(1280, 720, false);
    assert(g_dist == 4200);
    g_startDist = 1;
    camera_startup_zoom(1280, 720, false);
    assert(g_dist == 1920);

    // An editor document keeps its working camera, even with diagnostic argv set.
    g_dist = 2817; g_zoom = 34;
    const int updates = basis_updates;
    camera_startup_zoom(1280, 720, true);
    assert(g_dist == 2817 && g_zoom == 34 && basis_updates == updates);
}
'''.replace("HELPER", helper)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.cpp").write_text(code)
            subprocess.run(
                ["clang++", "-std=c++14", str(path / "test.cpp"), "-o", str(path / "test")],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
