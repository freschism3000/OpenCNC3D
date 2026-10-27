#!/usr/bin/env python3
"""Real TD missions/matches + native production sidebar adapters; no visible window."""
from pathlib import Path
import argparse,subprocess,json,re,shutil,hashlib,sys
p=argparse.ArgumentParser();p.add_argument('--repo',type=Path,required=True);p.add_argument('--exe',type=Path);p.add_argument('--brain',type=Path);p.add_argument('--playable',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.repo=a.repo.resolve();a.playable=a.playable.resolve();a.output=a.output.resolve();a.exe=a.exe.resolve() if a.exe else None;a.brain=a.brain.resolve() if a.brain else None;o=a.output;o.mkdir(parents=True,exist_ok=True)
if a.exe is None:
 if a.brain is None:raise SystemExit('Supply --brain for the current native TD library, or --exe for an already compiled harness.')
 work=o/'native';work.mkdir(exist_ok=True)
 subprocess.run([sys.executable,str(Path(__file__).with_name('extract-adapters.py')),str(a.repo),str(work/'production-adapters.inc')],check=True)
 # The harness links the actual engine classes plus the public sidebar adapter,
 # matching the brain's compiler definitions and allocating only private outputs.
 cmd=['clang++','-std=c++14','-O2','-g','-DNDEBUG','-fms-extensions','-fdeclspec','-fcheck-new','-fsigned-char','-DEIGHTPLAYERS','-DENGLISH','-DMEGAMAPS','-DREMASTER_BUILD','-DTRUE_FALSE_DEFINED','-D_USRDLL']
 if sys.platform=='darwin':cmd+=['-mmacosx-version-min=14.0']
 cmd+=['-I'+str(a.repo/'brain/vanilla'),'-I'+str(a.repo/'brain/vanilla/tiberiandawn'),'-I'+str(a.repo/'brain/vanilla/common'),'-I'+str(a.repo/'game'),'-I'+str(work),str(Path(__file__).with_name('sidebar-potential.cpp')),str(a.brain),'-Wl,-rpath,'+str(a.brain.resolve().parent),'-o',str(work/'sidebar-potential')]
 with (o/'compile.log').open('w') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
 a.exe=work/'sidebar-potential'
if not (o/'CONQUER.MIX').exists():(o/'CONQUER.MIX').symlink_to(a.playable/'CONQUER.MIX')
fixture=o/'fixture';fixture.mkdir(exist_ok=True)
base='''[Basic]
Player=GoodGuy
BuildLevel=15
Action=x
Lose=x
Win=x
Brief=x
Intro=x
Theme=No theme
[MAP]
Height=58
Width=57
Y=3
X=3
Theater=DESERT
[GoodGuy]
Allies=GoodGuy,Neutral
MaxBuilding=150
MaxUnit=150
Credits=5000
Edge=South
[BadGuy]
Allies=BadGuy
MaxBuilding=150
MaxUnit=150
Credits=0
Edge=North
[Neutral]
Allies=Neutral,GoodGuy
MaxBuilding=150
MaxUnit=150
Credits=0
Edge=North
[STRUCTURES]
000=GoodGuy,FACT,256,780,0,None
001=GoodGuy,NUKE,256,786,0,None
002=GoodGuy,PYLE,256,1044,0,None
003=GoodGuy,WEAP,256,1050,0,None
004=BadGuy,HAND,256,1300,0,None
005=BadGuy,FACT,256,796,0,None
[UNITS]
[INFANTRY]
[OVERLAY]
[TERRAIN]
[SMUDGE]
[Triggers]
[CellTriggers]
[Teams]
[Waypoints]
[Base]
Count=0
'''
(fixture/'SCG97EA.INI').write_text(base);shutil.copy2(Path(__file__).with_name('fixtures')/'SCG97EA.BIN',fixture/'SCG97EA.BIN')
cases=[('gdi-01','SCG01EA',1,0),('gdi-02','SCG02EA',2,0),('gdi-05','SCG05EA',5,0),('gdi-07','SCG07EA',7,0),('gdi-15','SCG15EA',15,0),('nod-05','SCB05EA',5,1),('nod-11','SCB11EA',11,1),('nod-13','SCB13EA',13,1),('mp-gdi-1','SCM01EA',1,0),('mp-gdi-5','SCM01EA',5,0),('mp-gdi-7','SCM01EA',7,0),('mp-nod-7','SCM01EA',7,1),('capture','SCG97EA',15,0),('restore','SCG97EA',15,0),('fallback','SCG97EA',15,0)]
records={};expectations=0;fail=[]
for tag,scen,level,side in cases:
 d=fixture if scen=='SCG97EA' else a.playable/'missions';args=[str(a.exe),str(a.playable/'content')+'/',str(d)+'/',scen,str(level),str(side),tag]
 with (o/(tag+'.log')).open('w') as log:pr=subprocess.run(args,cwd=o,stdout=log,stderr=subprocess.STDOUT,timeout=90)
 lines=(o/(tag+'.log')).read_text().splitlines();states={}
 for line in lines:
  m=re.fullmatch(r'DISPLAY\|([^|]+)\|category=(\d+)\|([^|]+)\|state=(\w+)\|engine=(-?\d+)',line)
  if m:states.setdefault(m[1],{})[m[3]]={'category':int(m[2]),'state':m[4],'engine':int(m[5])}
 result=re.search(r'RESULT\|checks=(\d+)\|failures=(\d+)', '\n'.join(lines))
 rec={'args':args,'exit':pr.returncode,'checks':int(result[1]) if result else 0,'failures':int(result[2]) if result else None,'display_states':states,'case_lines':[x for x in lines if x.startswith('CASE|')],'timing_lines':[x for x in lines if x.startswith('TIMING|')]};records[tag]=rec
 if pr.returncode or not result or int(result[2]):fail.append(tag+': native harness failed')
 print(tag,'exit',pr.returncode,'checks',rec['checks'],'items', {k:len(v) for k,v in states.items()},flush=True)
def expect(tag,name,state):
 global expectations;expectations+=1
 actual=next((value['display_states'][tag].get(name,{}).get('state','hidden') for value in records.values() if tag in value['display_states']),'missing-case')
 if actual!=state:fail.append(f'{tag}: {name}={actual}, wanted {state}')
def exact_category(tag,cat,want):
 global expectations;expectations+=1
 got=next(({n for n,v in value['display_states'][tag].items() if v['category']==cat} for value in records.values() if tag in value['display_states']),set())
 if got!=set(want):fail.append(f'{tag}: category{cat}={sorted(got)}, wanted {sorted(want)}')
exact_category('gdi-01',0,['NUKE','PYLE']);exact_category('gdi-02',0,['NUKE','PYLE'])
for tag in ['gdi-01','gdi-02','gdi-05']:
 for name in ['E3','HTNK','MSAM','MCV','ORCA','EYE']:expect(tag,name,'hidden')
expect('gdi-07','E3','grey');expect('gdi-07','HTNK','hidden');expect('gdi-15','HTNK','grey');expect('gdi-15','MSAM','grey')
for tag in ['nod-05','nod-11','nod-13']:
 for name in ['HPAD','APC','MSAM','EYE']:expect(tag,name,'hidden')
expect('nod-11','STNK','grey')
for name in ['E3','HTNK','RMBO','ORCA','EYE']:expect('mp-gdi-1',name,'hidden')
expect('mp-gdi-5','HTNK','grey');expect('mp-gdi-5','RMBO','hidden');expect('mp-gdi-7','HTNK','grey');expect('mp-gdi-7','MSAM','grey');expect('mp-nod-7','HPAD','grey')
expect('capture','E4','hidden');expect('captured-hand','E4','live');expect('captured-hand','E5','hidden')
for name in ['OBLI','TMPL']:expect('captured-yard',name,'hidden')
expect('restore','E1','live');expect('restore-destroyed','E1','grey');expect('restore-rebuilt','E1','live')
expectations+=1
fallback=records['fallback']['display_states'].get('fallback',{})
if not fallback or any(v['state']!='live' for v in fallback.values()):fail.append('older-brain fallback must contain only actual live entries')
summary={'passed':not fail,'expectations':expectations,'native_checks':sum(v['checks'] for v in records.values()),'failures':fail,'records':records,'source_hashes':{str(x.relative_to(a.repo)):hashlib.sha256(x.read_bytes()).hexdigest() for x in [a.repo/'game/cnc_sidebar.h',a.repo/'game/sidebar_build_potential.h',a.repo/'brain/vanilla/tiberiandawn/house.cpp',a.repo/'brain/vanilla/tiberiandawn/dllinterface.cpp']},'window_opened':False,'installed_writes':False,'brain_sha256':hashlib.sha256(a.brain.read_bytes()).hexdigest() if a.brain else None}
(o/'verification.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps({'passed':summary['passed'],'expectations':expectations,'native_checks':summary['native_checks'],'failures':fail},indent=2));raise SystemExit(1 if fail else 0)
