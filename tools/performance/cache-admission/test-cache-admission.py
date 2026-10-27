#!/usr/bin/env python3
"""Exercise actual persistent-cache admission helpers without a graphics driver."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def extract_function(source, name):
    match = re.search(r"^static [^\n]+ " + re.escape(name) + r"\(", source, re.M)
    if not match:
        raise AssertionError("Missing runtime function: " + name)
    start = match.start()
    pos = source.index("{", match.end())
    depth = 1
    end = pos + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


HEAD = r"""
#include <unordered_map>
#include <map>
#include <list>
#include <vector>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <random>
using GLuint=unsigned;
using MeshKey=unsigned;
struct MeshBatchEntry{GLuint vbo=0;size_t bytes=0;unsigned used=0;std::list<MeshKey>::iterator lru;};
static std::unordered_map<MeshKey,MeshBatchEntry> g_meshBatchCache;
static std::list<MeshKey> g_meshBatchLRU;
static std::map<GLuint,size_t> g_meshVerified;
static size_t g_meshBatchBytes=0;
static unsigned long g_meshBatchEvictions=0,g_meshBatchMisses=0,g_meshBatchHits=0;
static unsigned long long g_meshBatchAdmissionRefused=0;
static unsigned g_drawFrameSerial=0;
static const unsigned MESH_BATCH_MAX_ENTRIES=8192;
static bool protect=true,allocationOK=true;
static bool mesh_perf_switch(const char* name){
    if(!strcmp(name,"OPENCNC3D_MESH_CACHE_FULL"))return false;
    if(!strcmp(name,"OPENCNC3D_MESH_HOT_ADMISSION"))return protect;
    if(!strcmp(name,"OPENCNC3D_MESH_CACHE_ALLOC_OK"))return allocationOK;
    return true;
}
static std::vector<GLuint> deleted;
static void gr_glDeleteBuffers(int n,const GLuint* names){for(int i=0;i<n;i++)deleted.push_back(names[i]);}
"""

TAIL = r"""
static size_t checks=0;
static void ck(bool q){checks++;assert(q);}
static void reset(){allocationOK=true;deleted.clear();g_meshVerified.clear();g_meshBatchCache.clear();g_meshBatchLRU.clear();g_meshBatchBytes=0;g_meshBatchEvictions=g_meshBatchMisses=g_meshBatchAdmissionRefused=g_meshBatchHits=0;g_drawFrameSerial=0;}
static bool add(unsigned key,size_t bytes){if(!mesh_batch_admit(bytes))return false;MeshBatchEntry e;e.bytes=bytes;mesh_batch_keep(key,std::move(e));return true;}
static void invariant(){size_t bytes=0;ck(g_meshBatchLRU.size()==g_meshBatchCache.size());ck(g_meshBatchCache.size()<=8192);for(auto k:g_meshBatchLRU){auto f=g_meshBatchCache.find(k);ck(f!=g_meshBatchCache.end());ck(*f->second.lru==k);bytes+=f->second.bytes;}ck(bytes==g_meshBatchBytes);ck(bytes<=1048576);}
int main(){
 reset();protect=true;allocationOK=false;{MeshBatchEntry entry;entry.bytes=128;entry.vbo=23;g_meshVerified[23]=128;mesh_batch_keep(4,entry);ck(g_meshBatchCache.empty());ck(g_meshBatchLRU.empty());ck(g_meshBatchBytes==0);ck(g_meshVerified.count(23)==0);ck(deleted.size()==1 && deleted[0]==23);ck(g_meshBatchAdmissionRefused==1);invariant();}
 reset();protect=true;g_meshBatchAdmissionRefused=(1ull<<32);ck(!add(0,1048577));ck(g_meshBatchAdmissionRefused==(1ull<<32)+1);invariant();
 reset();protect=true;{MeshBatchEntry entry;entry.bytes=1048576;entry.vbo=17;mesh_batch_keep(99,entry);g_meshVerified[17]=1048576;const auto refused=g_meshBatchAdmissionRefused;entry.vbo=19;g_meshVerified[19]=1048576;mesh_batch_keep(99,entry);ck(g_meshBatchCache.size()==1);ck(g_meshBatchLRU.size()==1);ck(g_meshBatchBytes==1048576);ck(g_meshBatchCache.at(99).vbo==17);ck(g_meshVerified.count(17)==1);ck(g_meshVerified.count(19)==0);ck(deleted.size()==1 && deleted[0]==19);ck(g_meshBatchAdmissionRefused==refused);entry.vbo=17;mesh_batch_keep(99,entry);ck(deleted.size()==1);ck(g_meshVerified.count(17)==1);invariant();}
 reset();protect=true;ck(add(0,524288));ck(add(1,524288));ck(!add(2,524288));ck(g_meshBatchEvictions==0);invariant();
 for(unsigned frame=1;frame<100;frame++){g_drawFrameSerial=frame;for(unsigned k=0;k<4;k++){if(!mesh_batch_kept_find(k))ck(!add(k,524288));}invariant();}ck(g_meshBatchEvictions==0);
 g_drawFrameSerial+=3;ck(add(2,524288));ck(!mesh_batch_kept_find(0));ck(mesh_batch_kept_find(2));ck(g_meshBatchEvictions==1);invariant();
 reset();protect=false;for(unsigned frame=0;frame<20;frame++){g_drawFrameSerial=frame;for(unsigned k=0;k<4;k++)if(!mesh_batch_kept_find(k))ck(add(k,524288));invariant();}ck(g_meshBatchEvictions==78);
 reset();protect=true;for(unsigned k=0;k<9000;k++)ck(add(k,0)==(k<8192));invariant();ck(mesh_batch_kept_find(0));ck(!mesh_batch_kept_find(8999));g_drawFrameSerial=3;ck(add(8999,0));invariant();ck(mesh_batch_kept_find(8999));
 reset();protect=true;g_drawFrameSerial=UINT32_MAX-1;ck(add(0,1048576));g_drawFrameSerial=0;ck(!add(1,1048576));g_drawFrameSerial=1;ck(add(1,1048576));invariant();
 reset();protect=true;ck(!add(0,1048577));ck(!add(0,(size_t)-1));invariant();
 std::mt19937 rng(20261015);for(unsigned f=0;f<100;f++){g_drawFrameSerial=f;for(unsigned q=0;q<200;q++){unsigned k=rng()%2000;if(!mesh_batch_kept_find(k))add(k,rng()%8192);}invariant();}
 printf("PASS: %zu checks on extracted live cache functions\n",checks);
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[3])
    args = parser.parse_args()
    source = (args.repo / "game" / "mesh_batch.h").read_text()
    names = ["mesh_batch_kept_find", "mesh_batch_evict_one", "mesh_batch_capacity", "mesh_batch_admit", "mesh_batch_keep"]
    runtime = "".join(extract_function(source, name) for name in names)
    with tempfile.TemporaryDirectory(prefix="opencnc3d-cache-admission-") as folder:
        out = Path(folder)
        cpp = out / "admission.cpp"
        cpp.write_text(HEAD + runtime + TAIL)
        binary = out / "admission"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++14", "-O2", "-Wall", "-Wextra", str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
