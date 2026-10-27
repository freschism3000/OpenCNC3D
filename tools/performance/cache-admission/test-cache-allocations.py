#!/usr/bin/env python3
from pathlib import Path
import importlib.util,hashlib,subprocess,json,argparse
parser=argparse.ArgumentParser();parser.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[3]);parser.add_argument('--out',type=Path,required=True);args=parser.parse_args();root=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('extract',root/'test-cache-admission.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
source=(args.repo/'game/mesh_batch.h').read_text()
names=['mesh_batch_kept_find','mesh_batch_evict_one','mesh_batch_capacity','mesh_batch_admit','mesh_batch_keep','mesh_batch_frame_keep','mesh_batch_frame_store']
runtime=''.join(m.extract_function(source,n) for n in names)
head=r'''
#include <unordered_map>
#include <map>
#include <list>
#include <vector>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <new>
#include <cstdlib>
using GLuint=unsigned;
using MeshKey=unsigned;
struct MeshBatchEntry{GLuint vbo=0;size_t bytes=0;unsigned used=0;GLuint positionVbo=0;std::vector<unsigned> runs;std::list<MeshKey>::iterator lru;};
struct MeshFrameStore{GLuint vbo=0;size_t bytes=0;unsigned lastUsed=0;};
static std::unordered_map<MeshKey,MeshBatchEntry> g_meshBatchCache,g_meshFrameCache;
static std::list<MeshKey> g_meshBatchLRU;
static std::map<GLuint,size_t> g_meshVerified;
static size_t g_meshBatchBytes=0,g_meshFrameNext=0,g_meshFrameBytes=0;
static std::vector<MeshFrameStore> g_meshFrameStores;
static unsigned long g_meshBatchEvictions=0,g_meshBatchMisses=0,g_meshBatchHits=0,g_meshFrameFallbacks=0;
static unsigned long long g_meshBatchAdmissionRefused=0;
static unsigned g_drawFrameSerial=0;
static const unsigned MESH_BATCH_MAX_ENTRIES=8192;
static bool protect=true,allocationOk=true;
static bool mesh_perf_switch(const char* name){if(!strcmp(name,"OPENCNC3D_MESH_CACHE_FULL"))return false;if(!strcmp(name,"OPENCNC3D_MESH_CACHE_ALLOC_OK"))return allocationOk;return protect;}
static std::vector<GLuint> deleted;
static unsigned generated=0;
static void gr_glDeleteBuffers(int n,const GLuint* names){for(int i=0;i<n;i++)deleted.push_back(names[i]);}
static void gr_glGenBuffers(int n,GLuint* names){for(int i=0;i<n;i++)names[i]=++generated+100;}
static void mesh_batch_frame_begin(){}
static void mesh_frame_store_release(MeshFrameStore& s){if(s.vbo){gr_glDeleteBuffers(1,&s.vbo);g_meshFrameBytes-=s.bytes;s={};}}
static long failAfter=-1,allocations=0;
void* operator new(std::size_t n){if(failAfter==0){failAfter=-1;throw std::bad_alloc();}if(failAfter>0)--failAfter;++allocations;void* p=std::malloc(n);if(!p)throw std::bad_alloc();return p;}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
'''
tail=r'''
static unsigned checks=0,persistentFaults=0,frameFaults=0,poolFaults=0;
static void ck(bool x){++checks;assert(x);}
static void reset(){failAfter=-1;decltype(g_meshBatchCache)().swap(g_meshBatchCache);decltype(g_meshFrameCache)().swap(g_meshFrameCache);g_meshBatchLRU.clear();g_meshVerified.clear();g_meshFrameStores.clear();g_meshFrameStores.shrink_to_fit();g_meshFrameNext=g_meshFrameBytes=g_meshBatchBytes=0;g_meshBatchAdmissionRefused=g_meshFrameFallbacks=0;deleted.clear();generated=0;allocationOk=true;protect=true;}
static MeshBatchEntry offered(){MeshBatchEntry e;e.vbo=17;e.positionVbo=88;e.bytes=100;e.runs.assign(4,7);g_meshVerified[17]=100;return e;}
static void persistentFault(long at){reset();auto e=offered();failAfter=at;bool escaped=false;try{mesh_batch_keep(1,std::move(e));}catch(...){escaped=true;}failAfter=-1;ck(!escaped);if(g_meshBatchCache.empty()){++persistentFaults;ck(g_meshBatchLRU.empty());ck(g_meshBatchBytes==0);ck(deleted.size()==1&&deleted[0]==17);ck(!g_meshVerified.count(17));ck(g_meshBatchAdmissionRefused==1);}else{ck(g_meshBatchCache.size()==1&&g_meshBatchLRU.size()==1);ck(g_meshBatchBytes==100);ck(deleted.empty());ck(g_meshBatchCache.at(1).vbo==17);ck(g_meshBatchCache.at(1).positionVbo==88);}}
int main(){deleted.reserve(100);
 for(long at=0;at<6;++at)persistentFault(at);
 reset();auto e=offered();allocationOk=false;mesh_batch_keep(1,std::move(e));ck(g_meshBatchCache.empty()&&g_meshBatchLRU.empty()&&g_meshBatchBytes==0);ck(deleted.size()==1&&deleted[0]==17);ck(g_meshBatchAdmissionRefused==1);
 for(long at=0;at<6;++at){reset();auto e=offered();failAfter=at;bool escaped=false;try{mesh_batch_frame_keep(1,e);}catch(...){escaped=true;}failAfter=-1;ck(!escaped);if(g_meshFrameCache.empty()){++frameFaults;ck(g_meshFrameFallbacks==1);}else{ck(g_meshFrameCache.size()==1);ck(g_meshFrameCache.at(1).runs==e.runs);ck(g_meshFrameCache.at(1).vbo==17&&g_meshFrameCache.at(1).positionVbo==88);}ck(deleted.empty());ck(g_meshVerified.count(17));}
 reset();auto old=offered();mesh_batch_frame_keep(1,old);auto newer=old;newer.vbo=19;newer.runs.push_back(99);for(long at=0;at<5;++at){failAfter=at;mesh_batch_frame_keep(1,newer);failAfter=-1;ck(g_meshFrameCache.size()==1);ck(g_meshFrameCache.at(1).vbo==17&&g_meshFrameCache.at(1).runs==old.runs);ck(deleted.empty());}
 for(long at=0;at<3;++at){reset();failAfter=at;GLuint vbo=0;bool escaped=false;try{vbo=mesh_batch_frame_store(100);}catch(...){escaped=true;}failAfter=-1;ck(!escaped);if(!vbo){++poolFaults;ck(g_meshFrameFallbacks==1);ck(g_meshFrameStores.empty());ck(g_meshFrameNext==0&&g_meshFrameBytes==0);ck(generated==0);}else{ck(g_meshFrameStores.size()==1);ck(g_meshFrameNext==1&&g_meshFrameBytes==100);ck(g_meshFrameStores[0].vbo==vbo);}ck(deleted.empty());}
 reset();ck(mesh_batch_frame_store(100));failAfter=0;ck(!mesh_batch_frame_store(100));failAfter=-1;ck(g_meshFrameNext==1&&g_meshFrameBytes==100&&g_meshFrameStores.size()==1);ck(deleted.empty());
 reset();g_meshBatchAdmissionRefused=0xffffffffull;for(int i=0;i<3;i++)ck(!mesh_batch_admit((size_t)-1));ck(g_meshBatchAdmissionRefused==0x100000002ull);
 printf("PASS checks=%u persistent_faults=%u frame_faults=%u pool_faults=%u counter=%llu\n",checks,persistentFaults,frameFaults,poolFaults,g_meshBatchAdmissionRefused);
}
'''
out=args.out.resolve();out.mkdir(parents=True,exist_ok=True); cpp=out/'review-cache-allocations.cpp';cpp.write_text(head+runtime+tail);binary=out/'review-cache-allocations'
subprocess.run(['c++','-std=c++14','-O2','-Wall','-Wextra',str(cpp),'-o',str(binary)],check=True)
r=subprocess.run([str(binary)],check=True,text=True,capture_output=True);print(r.stdout)
(out/'review-cache-allocations.json').write_text(json.dumps({'source_sha256':hashlib.sha256(source.encode()).hexdigest(),'extracted_helpers_sha256':hashlib.sha256(runtime.encode()).hexdigest(),'helpers':names,'result':r.stdout.strip(),'scope':'Actual live helper function bodies; stand-in MeshKey/GL handles/entry vectors; injected global operator new refusal. No native GL or game execution.'},indent=2)+'\n')
