#!/usr/bin/env python3
"""Red Alert's Update_Buildables, compiled against isolated sidebar/house counters; no UI.

The same check as test-sale-options.py, for brain/vanilla/redalert/building.cpp: a factory
being sold must not publish its production choices again, and everything else the function
decides (vessels, the kennel's dogs, captures, discovery, limbo, the multiplayer context)
must be as before. --baseline calibrates against an unguarded copy of the function.
"""
import argparse, hashlib, json, shutil, subprocess, tempfile
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--source', required=True, type=Path)
p.add_argument('--out', required=True, type=Path)
p.add_argument('--baseline', action='store_true')
a = p.parse_args()

src = a.source.read_bytes().decode('latin1')
# the live definition is the first one; the second sits in an #else kept for reference
start = src.index('void BuildingClass::Update_Buildables(void)')
opening = src.index('{', start)
depth, end = 1, opening + 1
while depth:
    if src[end] == '{': depth += 1
    elif src[end] == '}': depth -= 1
    end += 1
function = src[start:end]
guarded = 'Mission != MISSION_DECONSTRUCTION' in function
assert guarded != a.baseline, 'Expected source guard; use explicit --baseline only to calibrate the old behaviour'

out = a.out.resolve()
assert Path('/private/tmp') in out.parents or Path(tempfile.gettempdir()).resolve() in out.parents
assert not out.exists()
out.mkdir(parents=True)

body = r'''
#include <cstdio>
#include <set>
#include <utility>
#define assert(x) ((void)0)
enum RTTIType{RTTI_NONE=0,RTTI_AIRCRAFTTYPE=2,RTTI_BUILDINGTYPE=6,RTTI_INFANTRYTYPE=9,RTTI_UNITTYPE=21,RTTI_VESSELTYPE=23};
enum StructType{STRUCT_FIRST,STRUCT_KENNEL=5,STRUCT_COUNT=8};enum UnitType{UNIT_FIRST,UNIT_COUNT=4};
enum InfantryType{INFANTRY_FIRST,INFANTRY_DOG=0,INFANTRY_COUNT=6};enum AircraftType{AIRCRAFT_FIRST,AIRCRAFT_COUNT=3};
enum VesselType{VESSEL_FIRST,VESSEL_COUNT=5};
enum MissionType{MISSION_GUARD,MISSION_CONSTRUCTION,MISSION_DECONSTRUCTION};
enum GameType{GAME_NORMAL,GAME_GLYPHX_MULTIPLAYER};
struct SessionMock{GameType Type=GAME_NORMAL;};static SessionMock Session;
struct HouseClass{bool IsHuman=true;int ActLike=0;bool can=true;template<class T>bool Can_Build(T,int){return can;}};
static HouseClass*PlayerPtr;
static unsigned additions,voices,capturedAdds,dogs;
struct SidebarMock{std::set<std::pair<int,int>> available;void Add(RTTIType t,int id,bool capture){if(available.insert({int(t),id}).second){additions++;voices++;if(capture)capturedAdds++;if(t==RTTI_INFANTRYTYPE&&id==INFANTRY_DOG)dogs++;}}};
static SidebarMock Map,Glyph;
static void Sidebar_Glyphx_Add(RTTIType t,int id,HouseClass*,bool captured){Glyph.Add(t,id,captured);}
struct TypeMock{bool IsDog;};
struct VesselTypeClass{static TypeMock const&As_Reference(VesselType){static TypeMock t{false};return t;}};
struct BuildingTypeClass{static TypeMock const&As_Reference(StructType){static TypeMock t{false};return t;}};
struct UnitTypeClass{static TypeMock const&As_Reference(UnitType){static TypeMock t{false};return t;}};
struct AircraftTypeClass{static TypeMock const&As_Reference(AircraftType){static TypeMock t{false};return t;}};
struct InfantryTypeClass{static TypeMock const&As_Reference(InfantryType f){static TypeMock dog{true},man{false};return f==INFANTRY_DOG?dog:man;}};
struct BuildingsMock{int ID(const void*){return 0;}};static BuildingsMock Buildings;
struct FactoryType{RTTIType ToBuild;};
struct BuildingClass{HouseClass*House;FactoryType*Class;StructType Kind=STRUCT_FIRST;int ID=0;bool IsActive=true;
 bool IsCaptured=false,IsInLimbo=false,discovered=true;int ActLike=0;MissionType Mission=MISSION_GUARD;
 bool Is_Discovered_By_Player(){return discovered;}bool operator==(StructType t)const{return Kind==t;}
 bool operator!=(StructType t)const{return Kind!=t;}void Update_Buildables();};
'''
body += function + r'''
static unsigned checks,failures;
static void check(bool v,const char*why){checks++;if(!v){failures++;std::fprintf(stderr,"SALEOPTIONS|FAIL|%s\n",why);}}
static void reset(){Map.available.clear();Glyph.available.clear();additions=voices=capturedAdds=dogs=0;}
int main(int argc,char**argv){const bool guarded=argc>1&&argv[1][0]=='1';HouseClass mine,other;PlayerPtr=&mine;
 const RTTIType kinds[]={RTTI_BUILDINGTYPE,RTTI_UNITTYPE,RTTI_INFANTRYTYPE,RTTI_AIRCRAFTTYPE,RTTI_VESSELTYPE};
 const unsigned counts[]={8,4,5,3,5};   /* a barracks trains every soldier but the dog */
 for(int mode=0;mode<2;mode++)for(int k=0;k<5;k++){
  Session.Type=mode?GAME_GLYPHX_MULTIPLAYER:GAME_NORMAL;FactoryType type{kinds[k]};BuildingClass b{&mine,&type};
  reset();b.Update_Buildables();check(additions==counts[k],"genuine available choices still added");check(voices==counts[k],"genuine gain still announced");b.Update_Buildables();check(voices==counts[k],"same choices do not announce twice");
  /* Recalc has removed this selling factory's options while another building
     changes the house. It must not publish the just-removed options again. */
  reset();b.Mission=MISSION_DECONSTRUCTION;b.Update_Buildables();check(additions==(guarded?0:counts[k]),"selling factory cannot re-add removed choices");check(voices==(guarded?0:counts[k]),"lost choices do not announce new construction");
  b.Mission=MISSION_GUARD;reset();b.Update_Buildables();check(voices==counts[k],"return to a genuine producing factory still unlocks choices");
  b.Mission=MISSION_CONSTRUCTION;reset();b.Update_Buildables();check(voices==counts[k],"existing construction eligibility untouched");b.Mission=MISSION_GUARD;
  if(kinds[k]==RTTI_INFANTRYTYPE){b.Kind=STRUCT_KENNEL;reset();b.Update_Buildables();check(additions==1&&dogs==1,"the kennel offers the dog alone");
   b.Mission=MISSION_DECONSTRUCTION;reset();b.Update_Buildables();check(dogs==(guarded?0u:1u),"a kennel being sold offers no dog");b.Mission=MISSION_GUARD;b.Kind=STRUCT_FIRST;}
  b.IsInLimbo=true;reset();b.Update_Buildables();check(additions==0,"limbo remains excluded");b.IsInLimbo=false;
  b.discovered=false;reset();b.Update_Buildables();check(additions==0,"undiscovered remains excluded");b.discovered=true;
  mine.can=false;reset();b.Update_Buildables();check(additions==0,"house technology refusal remains excluded");mine.can=true;
  b.IsCaptured=true;b.ActLike=1;reset();b.Update_Buildables();check(capturedAdds==counts[k],"captured-factory genuine options remain marked");
  b.House=&other;other.IsHuman=false;reset();b.Update_Buildables();check(additions==0,"other AI house remains excluded");
  other.IsHuman=true;reset();b.Update_Buildables();check(additions==(mode?counts[k]:0),"other human contexts retain original multiplayer gate");
 }
 std::printf("SALEOPTIONS|ra|checks=%u|failures=%u|guarded=%d|no_engine_or_UI=1\n",checks,failures,guarded);return failures?1:0;}
'''
cpp = out / 'actual-update-buildables-ra.cpp'
cpp.write_text(body)
exe = out / 'sale-options-ra'
cc = shutil.which('clang++') or shutil.which('g++')
assert cc
with (out / 'compile.txt').open('w') as log:
    subprocess.run([cc, '-std=c++14', '-DREMASTER_BUILD=1', '-O2', str(cpp), '-o', str(exe)],
                   check=True, stdout=log, stderr=subprocess.STDOUT)
q = subprocess.run([str(exe), '1' if guarded else '0'], capture_output=True, text=True, timeout=10)
assert q.returncode == 0, q.stdout + q.stderr
result = {'passed': True, 'guarded': guarded,
          'source_sha256': hashlib.sha256(a.source.read_bytes()).hexdigest(),
          'function_sha256': hashlib.sha256(function.encode('latin1')).hexdigest(),
          'actual_Update_Buildables_function': True,
          'house_sidebar_callbacks_are_isolated_counters': True,
          'no_engine_or_UI_launched': True, 'result': q.stdout.strip()}
(out / 'verification.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
