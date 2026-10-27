#!/usr/bin/env python3
"""Compile the actual generated-mesh finalizer against bounded data fixtures."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--repo',required=True,type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
source=a.repo/'game/ebld_mod.h';s=source.read_text();start=s.index('static int ebld_mesh_end(PackMesh& pm)');cur=s.index('{',start);depth=0
for i in range(cur,len(s)):
 if s[i]=='{':depth+=1
 elif s[i]=='}':
  depth-=1
  if depth==0:end=i+1;break
actual=s[start:end]
cpp=r'''#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <utility>
struct PackTri {unsigned char mode;int payload;};
struct PackPart {int tri0,ntris,role;float px,py,pz;};
struct PackMesh {std::vector<PackTri> tris;std::vector<PackPart> parts;unsigned modeMask=~0u;int payload=1234;};
struct Pack {std::vector<PackMesh> mesh;} g_pack;
static const int ROLE_STATIC=0;
static bool census=true,moving=true;
static bool mesh_perf_switch(const char* n){return !strcmp(n,"OPENCNC3D_GENERATED_MODE_MASK")?census:moving;}
'''+actual+r'''
int main(){unsigned checks=0;uint32_t rng=0x37192485;
for(int move=0;move<2;move++)for(int enabled=0;enabled<2;enabled++)for(int scene=0;scene<1200;scene++){
 moving=move;census=enabled;PackMesh pm;const int n=scene<260?scene%65:scene%301;
 for(int k=0;k<n;k++){rng=rng*1664525u+1013904223u;pm.tris.push_back({(unsigned char)(scene<260?scene%256:rng>>24),k*7+scene});}
 const auto original=pm.tris;const int index=ebld_mesh_end(pm);const auto& built=g_pack.mesh[(size_t)index];
 assert(built.tris.size()==original.size()&&built.payload==1234);checks++;
 assert(built.parts.size()==1&&built.parts[0].tri0==0&&built.parts[0].ntris==n&&built.parts[0].role==ROLE_STATIC);checks++;
 for(size_t k=0;k<original.size();k++){assert(built.tris[k].mode==original[k].mode&&built.tris[k].payload==original[k].payload);checks++;}
 for(int pass=0;pass<32;pass++){
  const bool contains=std::any_of(original.begin(),original.end(),[&](const PackTri& t){return t.mode==pass;});
  if(enabled)assert(bool(built.modeMask&(1u<<pass))==contains);
  else assert(built.modeMask==~0u);
  checks++;
 }
 // Geometry and pass membership survive later pose/UV/colour payload changes.
 for(auto& t:g_pack.mesh[(size_t)index].tris)t.payload^=0x733;
 for(int pass=0;pass<32;pass++){
  const bool contains=std::any_of(original.begin(),original.end(),[&](const PackTri& t){return t.mode==pass;});
  if(enabled)assert(bool(g_pack.mesh[(size_t)index].modeMask&(1u<<pass))==contains);checks++;
 }
 if(!moving){assert(pm.tris.size()==original.size());checks++;}
 g_pack.mesh.clear();
}
printf("GENERATED_MODE_MASK|checks=%u|pass\n",checks);}
'''
c=a.out/'test.cpp';c.write_text(cpp);binary=a.out/'test';subprocess.run(['clang++','-std=c++14','-O2',str(c),'-o',str(binary)],check=True)
r=subprocess.run([str(binary)],check=True,capture_output=True,text=True);print(r.stdout,end='')
result={'status':'PASS','actual_source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'actual_function_sha256':hashlib.sha256(actual.encode()).hexdigest(),'result':r.stdout.strip(),'scope':'Actual ebld_mesh_end function, 4800 bounded data cases across enabled/disabled census and move/copy finalization; empty/pass0..31/reserved-byte membership, source order, payloads, ROLE_STATIC part and later nonmode mutations. No GL/game execution.'}
(a.out/'verification.json').write_text(json.dumps(result,indent=2)+'\n')
