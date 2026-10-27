#!/usr/bin/env python3
"""Compile actual Update_Buildables against isolated sidebar/house counters; no UI."""
import argparse,json,hashlib,subprocess,shutil,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--source',required=True,type=Path);p.add_argument('--out',required=True,type=Path);p.add_argument('--baseline',action='store_true');a=p.parse_args()
src=a.source.read_bytes().decode('latin1');start=src.index('void BuildingClass::Update_Buildables(void)');opening=src.index('{',start);depth=1;end=opening+1
while depth:
 if src[end]=='{':depth+=1
 elif src[end]=='}':depth-=1
 end+=1
function=src[start:end];guarded='Mission != MISSION_DECONSTRUCTION' in function;assert guarded!=a.baseline,'Expected source guard; use explicit --baseline only to calibrate the old behavior'
out=a.out.resolve();assert Path('/private/tmp') in out.parents or Path(tempfile.gettempdir()).resolve() in out.parents;assert not out.exists();out.mkdir(parents=True)
body=r'''
#include <cstdio>
#include <set>
#include <utility>
enum RTTIType{RTTI_NONE=0,RTTI_INFANTRYTYPE=2,RTTI_UNITTYPE=4,RTTI_AIRCRAFTTYPE=6,RTTI_BUILDINGTYPE=8};
enum StructType{STRUCT_FIRST,STRUCT_COUNT=8};enum UnitType{UNIT_FIRST,UNIT_COUNT=4};enum InfantryType{INFANTRY_FIRST,INFANTRY_COUNT=6};enum AircraftType{AIRCRAFT_FIRST,AIRCRAFT_COUNT=3};
#define INC(T) T&operator++(T&x){x=(T)(int(x)+1);return x;} T operator++(T&x,int){T old=x;++x;return old;}
INC(StructType) INC(UnitType) INC(InfantryType) INC(AircraftType)
enum MissionType{MISSION_GUARD,MISSION_CONSTRUCTION,MISSION_DECONSTRUCTION};
enum GameType{GAME_NORMAL,GAME_GLYPHX_MULTIPLAYER};static GameType GameToPlay;
struct HouseClass{bool IsHuman=true;int ActLike=0;bool can=true;template<class T>bool Can_Build(T,int){return can;}};
static HouseClass*PlayerPtr;
static unsigned additions,voices,capturedAdds;
struct SidebarMock{std::set<std::pair<int,int>> available;void Add(RTTIType t,int id,bool capture){if(available.insert({int(t),id}).second){additions++;voices++;if(capture)capturedAdds++;}}};
static SidebarMock Map,Glyph;
static void Sidebar_Glyphx_Add(RTTIType t,int id,HouseClass*,bool captured){Glyph.Add(t,id,captured);}
struct FactoryType{RTTIType ToBuild;};
struct BuildingClass{HouseClass*House;FactoryType*Class;bool IsCaptured=false,IsInLimbo=false,discovered=true;int ActLike=0;MissionType Mission=MISSION_GUARD;void Validate(){}bool Is_Discovered_By_Player(){return discovered;}void Update_Buildables();};
'''
body+=function+r'''
static unsigned checks,failures;
static void check(bool v,const char*why){checks++;if(!v){failures++;std::fprintf(stderr,"SALEOPTIONS|FAIL|%s\n",why);}}
static void reset(){Map.available.clear();Glyph.available.clear();additions=voices=capturedAdds=0;}
int main(int argc,char**argv){const bool guarded=argc>1&&argv[1][0]=='1';HouseClass mine,other;PlayerPtr=&mine;
 const RTTIType kinds[]={RTTI_BUILDINGTYPE,RTTI_UNITTYPE,RTTI_INFANTRYTYPE,RTTI_AIRCRAFTTYPE};const unsigned counts[]={8,4,6,3};
 for(int mode=0;mode<2;mode++)for(int k=0;k<4;k++){
  GameToPlay=mode?GAME_GLYPHX_MULTIPLAYER:GAME_NORMAL;FactoryType type{kinds[k]};BuildingClass b{&mine,&type};
  reset();b.Update_Buildables();check(additions==counts[k],"genuine available choices still added");check(voices==counts[k],"genuine gain still announced");b.Update_Buildables();check(voices==counts[k],"same choices do not announce twice");
  /* Recalc has removed this selling factory's options while another building
     changes the house. It must not publish the just-removed options again. */
  reset();b.Mission=MISSION_DECONSTRUCTION;b.Update_Buildables();check(additions==(guarded?0:counts[k]),"selling factory cannot re-add removed choices");check(voices==(guarded?0:counts[k]),"lost choices do not announce new construction");
  b.Mission=MISSION_GUARD;reset();b.Update_Buildables();check(voices==counts[k],"return to a genuine producing factory still unlocks choices");
  b.Mission=MISSION_CONSTRUCTION;reset();b.Update_Buildables();check(voices==counts[k],"existing construction eligibility untouched");
  b.IsInLimbo=true;reset();b.Update_Buildables();check(additions==0,"limbo remains excluded");b.IsInLimbo=false;
  b.discovered=false;reset();b.Update_Buildables();check(additions==0,"undiscovered remains excluded");b.discovered=true;
  mine.can=false;reset();b.Update_Buildables();check(additions==0,"house technology refusal remains excluded");mine.can=true;
  b.IsCaptured=true;b.ActLike=1;reset();b.Update_Buildables();check(capturedAdds==counts[k],"captured-factory genuine options remain marked");
  b.House=&other;other.IsHuman=false;reset();b.Update_Buildables();check(additions==0,"other AI house remains excluded");
  other.IsHuman=true;reset();b.Update_Buildables();check(additions==(mode?counts[k]:0),"other human contexts retain original multiplayer gate");
 }
 std::printf("SALEOPTIONS|checks=%u|failures=%u|guarded=%d|no_engine_or_UI=1\n",checks,failures,guarded);return failures?1:0;}
'''
cpp=out/'actual-update-buildables.cpp';cpp.write_text(body);exe=out/'sale-options';cc=shutil.which('clang++') or shutil.which('g++');assert cc
with (out/'compile.txt').open('w') as log:subprocess.run([cc,'-std=c++14','-DREMASTER_BUILD=1','-O2',str(cpp),'-o',str(exe)],check=True,stdout=log,stderr=subprocess.STDOUT)
q=subprocess.run([str(exe),'1' if guarded else '0'],capture_output=True,text=True,timeout=10);assert q.returncode==0,q.stdout+q.stderr
result={'passed':True,'guarded':guarded,'source_sha256':hashlib.sha256(a.source.read_bytes()).hexdigest(),'function_sha256':hashlib.sha256(function.encode('latin1')).hexdigest(),'actual_Update_Buildables_function':True,'house_sidebar_callbacks_are_isolated_counters':True,'no_engine_or_UI_launched':True,'result':q.stdout.strip()};(out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
