#!/usr/bin/env python3
"""Compile the actual native wrapper’s folder predicate; no launcher/game/UI starts."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='mac-wrapper-cpu-',dir=ROOT/'build')as name:
 p=Path(name);source=p/'gate.c';binary=p/'gate'
 source.write_text('''#define main app_wrapper_main
#include "'''+str(ROOT/'launcher/mac_launch.c')+'''"
#undef main
#include <assert.h>
static void put(const char*d,const char*n,int executable){char p[4096];FILE*f;snprintf(p,sizeof p,"%s/%s",d,n);f=fopen(p,"wb");assert(f);fputs("fixture",f);fclose(f);if(executable)assert(!chmod(p,0755));}
int main(int argc,char**argv){char p[4096];const char*d=argv[1];assert(argc==2);assert(!is_game_dir(d));
put(d,"opencnc3d-launcher",1);assert(!is_game_dir(d));put(d,"dosmenu.pack",0);assert(is_game_dir(d));
/* Empty and RA-only platforms have the same valid bootstrap. */
snprintf(p,sizeof p,"%s/ra3d",d);assert(!mkdir(p,0755));put(p,"RedAlert.dylib",0);assert(is_game_dir(d));
snprintf(p,sizeof p,"%s/opencnc3d-launcher",d);assert(!remove(p));assert(!is_game_dir(d));
put(d,"opencnc3d-launcher.old2",1);put(d,"opencnc3d-install.txt",0);put(d,"opencnc3d-update.journal",0);assert(is_game_dir(d));
snprintf(p,sizeof p,"%s/opencnc3d-update.journal",d);assert(!remove(p));assert(!is_game_dir(d));
put(d,"opencnc3d",1);put(d,"TiberianDawn.dylib",0);assert(is_game_dir(d));
puts("MAC-WRAPPER|PASS|empty/RA-only native bootstrap; missing art refusal; interrupted updater old2 recovery; legacy full TD fallback; no GUI");return 0;}
''')
 subprocess.run(['cc','-std=gnu11','-mmacosx-version-min=14.0',str(source),'-o',str(binary)],check=True)
 case=p/'case';case.mkdir();subprocess.run([str(binary),str(case)],check=True)
