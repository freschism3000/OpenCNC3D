#!/usr/bin/env python3
"""Hidden cursor regression. Test hooks exist solely in an isolated source copy.
Requires C/C++ compilers, SDL2 development files, and playable assets in --repo.
Never changes repo sources, live settings, cursor position, or the Dock runtime.
"""
import argparse, hashlib, json, os, pathlib, shlex, shutil, subprocess, sys, tempfile
from PIL import Image

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

ap = argparse.ArgumentParser()
ap.add_argument("--repo", type=pathlib.Path, required=True)
ap.add_argument("--out", type=pathlib.Path)
ap.add_argument("--sdl-config", type=pathlib.Path)
ap.add_argument("--assets", type=pathlib.Path)
ap.add_argument("--scen", default="SCG01EA")
ap.add_argument("--pack", default="SCG01EA.pack")
ap.add_argument("--dir", type=pathlib.Path)
ap.add_argument("--arch")
ap.add_argument("--negative-control", action="store_true", help="Remove only the dialog cache reset in the private copy and assert the known failure.")
a = ap.parse_args()
repo = a.repo.resolve()
out = (a.out or pathlib.Path(tempfile.mkdtemp(prefix="opencnc3d-cursor-regression-"))).resolve()
assets = (a.assets or repo / "playable").resolve()
assert repo.exists() and (assets / "dossidebar.pack").is_file()
assert repo != out and repo not in out.parents and out not in repo.parents
out.mkdir(parents=True, exist_ok=True)
assert not (out / "source").exists(), "Use a fresh output directory."
# Asset files are read through symlinks; writable config/save paths belong to this run.
run_assets = out / "assets"
run_assets.mkdir()
for item in assets.iterdir():
    target = run_assets / item.name
    if item.name.lower() in ["saves", "performance-trace"]:
        target.mkdir()
    elif item.is_file() and item.suffix.lower() in [".cfg", ".ini", ".json", ".txt", ".log"]:
        shutil.copy2(item, target)
    else:
        target.symlink_to(item, target_is_directory=item.is_dir())
src = out / "source"
src.mkdir()
for name in subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard"], cwd=repo, text=True).splitlines():
    f = repo / name
    if f.is_file() and f.suffix in [".h", ".c", ".cpp"]:
        t = src / name
        t.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(f, t)
(src / "brain").mkdir(exist_ok=True)
if not (src / "brain/vanilla").exists():
    (src / "brain/vanilla").symlink_to(repo / "brain/vanilla", target_is_directory=True)
shutil.copy2(repo / "game/opencnc3d_build.h", src / "game/opencnc3d_build.h")
source_head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
input_files = {n: digest(src / n) for n in ["game/cnc_eyes.cpp", "game/dosopt_gl.h", "game/dosopt.h", "game/fullscreen.h", "menu/enhanced_menu.cpp"]}
assert all(digest(repo / n) == sha for n, sha in input_files.items()), "Source changed during export; run again."

s = (src / "game/cnc_eyes.cpp").read_text()
needle = "g_mouseScrC = (float)mx; g_mouseScrR = (float)my;"
assert s.count(needle) == 3
s = s.replace(needle, "mx = 600; my = 500; " + needle)
start = s.index("int game_visuals_open(")
needle = "    dopt_bind_visuals(&g_optState, opt_apply_visuals);"
s = s[:start] + s[start:].replace(needle, '    if (getenv("CURSOR_TEST_CONTROLS") && *getenv("CURSOR_TEST_CONTROLS")) g_optState.page = DOPT_PAGE_CONTROLS;\n' + needle, 1)
(src / "game/cnc_eyes.cpp").write_text(s)
shutil.copy2(pathlib.Path(__file__).with_name("cursor-lifecycle-harness.cpp"), src / "game/cursor_probe.cpp")
# Compare the same caret blink phase in both controls; the real game keeps its clock.
menu_source = src / "menu/enhanced_menu.cpp"
menu_text = menu_source.read_text()
assert menu_text.count("SDL_GetTicks()%800<400") == 1
menu_source.write_text(menu_text.replace("SDL_GetTicks()%800<400", "true /* private fixed caret phase */"))
if a.negative_control:
    f = src / "game/dosopt_gl.h"
    text = f.read_text()
    at = text.index("static void opt_draw(")
    assert "    gls_frame_begin();" in text[at:]
    text = text[:at] + text[at:].replace("    gls_frame_begin();", "    /* Negative-control test: omit the dialog boundary reset. */", 1)
    f.write_text(text)
# Compile dependencies from the same source snapshot to avoid stale structure ABI.
sdk = a.sdl_config
if not sdk:
    mac_sdk = pathlib.Path(os.environ.get("OPENCNC3D_MACSDK", str(pathlib.Path.home() / ".opencnc3d-macsdk"))).expanduser()
    bundled = mac_sdk / "bin/sdl2-config"
    sdk = bundled if bundled.is_file() else pathlib.Path(shutil.which("sdl2-config") or "")
assert sdk.is_file(), "Provide --sdl-config."
flags = shlex.split(subprocess.check_output([str(sdk), "--cflags"], text=True))
libs = shlex.split(subprocess.check_output([str(sdk), "--libs"], text=True))
arch = ["-arch", a.arch] if a.arch else []
mac = sys.platform == "darwin"
platform_flags = (["-mmacosx-version-min=14.0", "-DGL_SILENCE_DEPRECATION"] if mac else [])
platform_libs = (["-framework", "OpenGL", "-Wl,-headerpad_max_install_names"] if mac else ["-lGL", "-ldl"])
inc = ["-I.", "-I../brain/vanilla/tiberiandawn", "-I../menu", "-I../audio", "-I../video", *flags]
units = ["game/dosbar.c", "game/hud640.c", "game/dosopt.c", "game/dossave.c", "menu/dosmenu.c", "menu/dosops.c", "menu/doslobby.c", "menu/dosmp.c", "menu/dosmenu_shell.c", "menu/enhanced_menu.cpp", "video/vqaplay.c", "video/movieplay.c", "video/moviesnd.c", "video/pngwrite.c", "app/campaign.c", "app/logo3d.c", "net/lockstep.c", "net/net_udp.c", "net/netmatch.c", "net/netbeacon.c", "net/roomcode.c", "menu/mpbrowse.c", "launcher/lnet.c", "launcher/ljson.c"]
units += ["audio/" + n + ".c" for n in "sosadpcm wsadpcm wsaud mixfile sndbank mixer sfxtable sfxname cncaudio wavio audiotap audioboot audio_sdl".split()]
objects = []
with (out / "dependency-compile.log").open("wb") as log:
    for name in units:
        obj = out / (name.replace("/", "_").rsplit(".", 1)[0] + ".o")
        cpp = name.endswith(".cpp")
        args = ["clang++" if cpp else "clang", *arch, *platform_flags, "-std=c++14" if cpp else "-std=gnu89", "-O2", "-g", *inc, "-c", str(src / name), "-o", str(obj)]
        subprocess.run(args, cwd=src / "game", stdout=log, stderr=subprocess.STDOUT, check=True)
        objects.append(obj)
cmd = ["clang++", *arch, *platform_flags, "-std=c++14", "-O2", "-g", "-fms-extensions", "-fdeclspec", "-D__int64=long long", "-DOPENCNC3D_COOKED=1", "-include", "./opencnc3d_build.h", *inc, "cursor_probe.cpp", *map(str, objects), *libs, *platform_libs, "-lz", "-lcurl", "-o", str(out / "cursor-regression")]
manifest = {"source_head": source_head, "renderer_input_sha256": input_files["game/cnc_eyes.cpp"], "dialog_input_sha256": input_files["game/dosopt_gl.h"], "production_input_files": input_files, "linked_objects": {str(f): digest(f) for f in objects}, "compiler_argv": cmd, "private_source_mouse_hook": "fixed framebuffer coordinates only in source copy", "private_controls_hook": "test-only selected page", "private_caret_hook": "fixed blink phase in the private menu source", "windows_gpu_claim": False, "negative_control": a.negative_control}
(out / "inputs.json").write_text(json.dumps(manifest, indent=2))
with (out / "compile.log").open("wb") as log:
    subprocess.run(cmd, cwd=src / "game", stdout=log, stderr=subprocess.STDOUT, check=True)
if mac:
    subprocess.run(["sh", str(repo / "tools/bundle-sdl.sh"), str(out), "cursor-regression"], check=True)
for variant, cache in [("cached", "1"), ("uncached-control", "0")]:
    target = out / variant
    target.mkdir()
    env = os.environ.copy()
    env.update(OPENCNC3D_BACKGROUND="1", OPENCNC3D_STATE_CACHE=cache, SDL_MAC_BACKGROUND_APP="1")
    for k in ["OPENCNC3D_PERFTRACE", "OPENCNC3D_FRAMEPROF", "OPENCNC3D_GPUPROF", "OPENCNC3D_PERFDETAIL", "OPENCNC3D_MENU_1995", "CURSOR_TEST_CONTROLS"]:
        env.pop(k, None)
    with (target / "run.log").open("wb") as log:
        subprocess.run([str(out / "cursor-regression"), str(target), "--scen", a.scen, "--pack", a.pack, *(["--dir", str(a.dir.resolve())] if a.dir else [])], cwd=run_assets, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=90)
rows = []
for f in sorted((out / "cached").glob("*.png")):
    ref = out / "uncached-control" / f.name
    im, ctrl = Image.open(f), Image.open(ref)
    assert im.size == ctrl.size
    diff = sum(x != y for x, y in zip(im.get_flattened_data(), ctrl.get_flattened_data()))
    rows.append({"file": f.name, "pixel_differences": diff, "png_bytes_equal": f.read_bytes() == ref.read_bytes()})
assert len(rows) == 21
passed = all(r["png_bytes_equal"] for r in rows)
if a.negative_control:
    known = [r for r in rows if r["file"] in ["settings-reopen.png", "controls-reopen.png"]]
    passed = len(known) == 2 and all(r["pixel_differences"] >= 200 for r in known)
(out / "verification.json").write_text(json.dumps({"shots": rows, "passed": passed, "negative_control": a.negative_control, "hidden": True, "silent": True, "live_runtime_changed": False}, indent=2))
assert passed, "Unexpected cursor/state behavior; see the captured controls."
print(json.dumps({"passed": True, "shots": len(rows), "negative_control": a.negative_control, "evidence": str(out)}))
