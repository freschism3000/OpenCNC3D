#!/usr/bin/env python3
"""Hidden source-based lobby + campaign score regression.
Exports current sources; uses isolated assets/config and only an SDL hidden window.
Test instrumentation is inserted in the export, never in the source repository.
"""
import argparse, hashlib, json, os, pathlib, re, shlex, shutil, subprocess, sys, tempfile

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
p=argparse.ArgumentParser()
p.add_argument("--repo",type=pathlib.Path,required=True)
p.add_argument("--out",type=pathlib.Path)
p.add_argument("--sdl-config",type=pathlib.Path)
p.add_argument("--assets",type=pathlib.Path)
p.add_argument("--arch")
p.add_argument("--patch",type=pathlib.Path,help="Apply a reviewed private patch only in the exported source.")
p.add_argument("--expect-known-bugs",action="store_true",help="Negative control: verify the old code fails both regressions.")
a=p.parse_args(); repo=a.repo.resolve();assets=(a.assets or repo/"playable").resolve()
out=(a.out or pathlib.Path(tempfile.mkdtemp(prefix="opencnc3d-menu-regression-"))).resolve()
assert repo.is_dir() and assets.is_dir() and repo!=out and repo not in out.parents and out not in repo.parents
out.mkdir(parents=True,exist_ok=True);src=out/"source";assert not src.exists(),"Use a fresh output directory";src.mkdir()
for name in subprocess.check_output(["git","ls-files","--cached","--others","--exclude-standard"],cwd=repo,text=True).splitlines():
 f=repo/name
 if f.is_file() and f.suffix in [".c",".cpp",".h"]:
  t=src/name;t.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(f,t)
shutil.copy2(repo/"game/opencnc3d_build.h",src/"game/opencnc3d_build.h")
if a.patch:subprocess.run(["patch","-p1","-i",str(a.patch.resolve())],cwd=src,check=True)
files=["menu/doslobby.c","game/cnc_eyes.cpp","app/campaign.c"]
inputs={name:sha(src/name) for name in files}
# Snapshot the actual CampCtx at the completed tally; leave the menu sequence early,
# before the Hall of Fame, without changing the production drawing or font routines.
f=src/"app/campaign.c";s=f.read_text();needle="    /* ------------------------------------------------------------- hall of fame\n"
assert s.count(needle)==1;s=s.replace(needle,"    if (menu_regression_tally(c, &k, side)) goto closed;\n\n"+needle,1)
needle="    camp_fill(k->c, x, y, x + (int)strlen(buf) * 7, y + 7, DB_BLACK);"
assert s.count(needle)==1;s=s.replace(needle,"    menu_regression_number(x, y, buf, pal);\n"+needle,1);f.write_text(s)
shutil.copy2(pathlib.Path(__file__).with_name("menu-regression.cpp"),src/"game/menu-regression.cpp")
run=out/"assets";run.mkdir()
for item in assets.iterdir():
 t=run/item.name
 if item.name.lower() in ["saves","performance-trace"]:t.mkdir()
 elif item.is_file() and item.suffix.lower() in [".cfg",".ini",".json",".txt",".log"]:shutil.copy2(item,t)
 else:t.symlink_to(item,target_is_directory=item.is_dir())
sdk=a.sdl_config
if not sdk:
 bundled=pathlib.Path(os.environ.get("OPENCNC3D_MACSDK",str(pathlib.Path.home()/".opencnc3d-macsdk"))).expanduser()/"bin/sdl2-config"
 sdk=bundled if bundled.is_file() else pathlib.Path(shutil.which("sdl2-config") or "")
assert sdk.is_file(),"Provide --sdl-config"
flags=shlex.split(subprocess.check_output([str(sdk),"--cflags"],text=True));libs=shlex.split(subprocess.check_output([str(sdk),"--libs"],text=True))
mac=sys.platform=="darwin";arch=["-arch",a.arch] if a.arch else []
plat=["-mmacosx-version-min=14.0","-DGL_SILENCE_DEPRECATION"] if mac else []
platlibs=["-framework","OpenGL","-Wl,-headerpad_max_install_names"] if mac else ["-lGL","-ldl"]
inc=["-I.","-I../brain/vanilla/tiberiandawn","-I../menu","-I../audio","-I../video",*flags]
units=["game/dosbar.c","game/hud640.c","game/dosopt.c","game/dossave.c","menu/dosmenu.c","menu/dosops.c","menu/doslobby.c","menu/dosmp.c","menu/dosmenu_shell.c","menu/enhanced_menu.cpp","video/vqaplay.c","video/movieplay.c","video/moviesnd.c","video/pngwrite.c","app/logo3d.c","net/lockstep.c","net/net_udp.c","net/netmatch.c","net/netbeacon.c","net/roomcode.c","menu/mpbrowse.c","launcher/lnet.c","launcher/ljson.c"]
units += ["audio/"+n+".c" for n in "sosadpcm wsadpcm wsaud mixfile sndbank mixer sfxtable sfxname cncaudio wavio audiotap audioboot audio_sdl".split()]
objects=[]
with (out/"dependencies.log").open("wb") as log:
 for name in units:
  obj=out/(name.replace("/","_").rsplit(".",1)[0]+".o");cpp=name.endswith(".cpp")
  subprocess.run(["clang++" if cpp else "clang",*arch,*plat,"-std=c++14" if cpp else "-std=gnu89","-O2","-g",*inc,"-c",str(src/name),"-o",str(obj)],cwd=src/"game",stdout=log,stderr=subprocess.STDOUT,check=True);objects.append(obj)
cmd=["clang++",*arch,*plat,"-std=c++14","-O2","-g","-fms-extensions","-fdeclspec","-D__int64=long long","-DOPENCNC3D_COOKED=1","-include","./opencnc3d_build.h",*inc,"menu-regression.cpp",*map(str,objects),*libs,*platlibs,"-lz","-lcurl","-o",str(out/"menu-regression")]
manifest={"source_head":subprocess.check_output(["git","rev-parse","HEAD"],cwd=repo,text=True).strip(),"production_sha256":inputs,"harness_sha256":sha(src/"game/menu-regression.cpp"),"patch_sha256":sha(a.patch) if a.patch else None,"compile_argv":cmd,"hidden":True,"silent":True,"Windows_native_claim":False}
(out/"inputs.json").write_text(json.dumps(manifest,indent=2))
with (out/"compile.log").open("wb") as log:subprocess.run(cmd,cwd=src/"game",stdout=log,stderr=subprocess.STDOUT,check=True)
if mac:subprocess.run(["sh",str(repo/"tools/bundle-sdl.sh"),str(out),"menu-regression"],check=True)
captures=out/"captures";captures.mkdir();env=os.environ.copy();env.update(OPENCNC3D_BACKGROUND="1",SDL_MAC_BACKGROUND_APP="1")
for name in ["OPENCNC3D_PERFTRACE","OPENCNC3D_FRAMEPROF","OPENCNC3D_GPUPROF","OPENCNC3D_PERFDETAIL","OPENCNC3D_MENU_1995"]:env.pop(name,None)
with (out/"run.log").open("wb") as log:r=subprocess.run([str(out/"menu-regression"),str(captures)],cwd=run,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=90)
log=(out/"run.log").read_text();m=re.search(r"MENU_RESULT\|checks=(\d+)\|failures=(\d+)\|scores=(\d+)",log);assert m,"Native harness did not finish; inspect run.log"
checks,failures,scores=map(int,m.groups());rows=[json.loads(f.read_text()) for f in captures.glob("score-*.json")]
known_empty_fail="MENU_CHECK|FAIL|Enhanced Empty removes exactly one opponent" in log
known_engine_fail="MENU_CHECK|FAIL|Empty and Blocked do not become ghost players" in log
known_score_fail=all(r["overlap_pixels"]>0 for r in rows) and len(rows)==6
passed=(r.returncode==1 and known_empty_fail and known_engine_fail and known_score_fail) if a.expect_known_bugs else (r.returncode==0 and failures==0 and scores==6)
verification={"passed":passed,"negative_control":a.expect_known_bugs,"checks":checks,"failures":failures,"score_cases":scores,"known_empty_failure":known_empty_fail,"known_engine_failure":known_engine_fail,"known_score_failure":known_score_fail,"captures":{f.name:sha(f) for f in captures.glob("*.png")},"hidden":True,"silent":True,"live_installation_changed":False,"platform":sys.platform}
(out/"verification.json").write_text(json.dumps(verification,indent=2));print(json.dumps({"passed":passed,"checks":checks,"failures":failures,"scores":scores,"evidence":str(out)}));assert passed,"Regression result differs from expectation"
