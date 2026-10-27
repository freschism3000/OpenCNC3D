#!/usr/bin/env python3
"""Check the actual Enhanced rotor visibility rule against authored fan/disc roles.

The Orca's translucent discs overlay solid fan hubs and blades. Larger helicopter
rotors keep the existing landed-blade / airborne-disc replacement. No GL required.
"""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[3])
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
s = (a.repo / 'game/evhc_mod.h').read_text()
start = s.index('static bool evhc_piece_shows(')
begin = s.index('{', start)
level = 1
end = begin + 1
while level:
    if s[end] == '{': level += 1
    if s[end] == '}': level -= 1
    end += 1
fn = s[start:end]
code = r'''
#include <string>
#include <vector>
#include <cstdio>
enum { EVP_SPIN, EVP_ROTOR }; enum { EVF_DISC=1 };
struct EvhcPart { int kind, flags; };
struct EvhcModel { struct {std::string type;} m; std::vector<EvhcPart> part; bool discs; };
struct EvhcPose { float disc; };
''' + fn + r'''
int main() {
 int cases=0, fail=0;
 for (const char* type : {"ORCA", "HELI", "TRAN"}) {
  EvhcModel v;v.m.type=type;v.discs=true;
  v.part={{EVP_ROTOR,0},{EVP_ROTOR,EVF_DISC},{EVP_SPIN,0}};
  for (float d : {0.0f,0.01f,0.1f,0.49f,0.5f,0.8f,1.0f}) {
   EvhcPose q={d};
   bool expected[4]={std::string(type)=="ORCA" || d<0.5f,d>0.01f,true,true};
   for(int k=0;k<4;k++) {cases++;if(evhc_piece_shows(v,k==3?-1:k,q)!=expected[k])fail++;}
  }
  v.discs=false; EvhcPose q={1.0f}; cases++; if(!evhc_piece_shows(v,0,q)) fail++;
 }
 printf("ROTOR_RULE|cases=%d|failures=%d\n",cases,fail); return fail?1:0;
}
'''
a.out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='rotor-visibility-') as tmp:
    source=Path(tmp)/'gate.cpp'; binary=Path(tmp)/'gate';source.write_text(code)
    subprocess.run(['c++','-std=c++14','-O2',str(source),'-o',str(binary)],check=True)
    result=subprocess.run([str(binary)],text=True,capture_output=True)
    print(result.stdout.strip())
    (a.out/'result.json').write_text(json.dumps(dict(check='authored_rotor_visibility',
        cases=87,passed=result.returncode==0,output=result.stdout.strip()),indent=2)+'\n')
    raise SystemExit(result.returncode)
