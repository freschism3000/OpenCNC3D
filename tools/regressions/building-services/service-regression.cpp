#include <vector>
#include <map>
#include <string>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <fstream>
#include <algorithm>
using GLuint=unsigned;bool g_raModule=false;
static bool serviceEnabled=true;static bool mesh_perf_switch(const char*){return serviceEnabled;}
enum {BS_CONSTRUCTION=0,BS_IDLE=1,BS_ACTIVE=2,BS_FULL=3,EBS_INTACT=0};
struct PackVert{float x,y,z,u,v;unsigned char r,g,b,a;};
struct PackTri{int tex;unsigned char mode,wrap;PackVert v[3];};
struct PackMesh{std::vector<PackTri> tris;std::vector<float> nrm;std::vector<unsigned char> tfl;};
struct PackTex{int w,h,uw,uh;GLuint gl=0,gl_gdi=0,gl_extra[7]={};bool mips=false;};
struct Pack{std::vector<PackMesh> mesh;std::vector<PackTex> tex;}g_pack;
struct EbldVert{float x,y,z,u,v,ud,vd;unsigned char r,g,b,a;float nx,ny,nz;};
struct EbldTri{int tex;unsigned char mode,wrap;EbldVert v[3];};
struct EbldSet{std::vector<EbldTri> tri;};
struct EbldTex{int w,h,house;bool bled=true;};
#include "../game/building_service_types.h"
struct EbldModel{std::string type;int tex0=0;std::vector<EbldTex> tex;EbldSet set[1];BService service;};
float g_meshUVScaleU=1,g_meshUVScaleV=1,g_meshUVOffU=0,g_meshUVOffV=0;
int uploads=0;static std::vector<std::vector<unsigned char> > uploaded;static void ebld_mesh_begin(PackMesh&,const char*,const char*){}
static int ebld_mesh_end(PackMesh& p){g_pack.mesh.push_back(p);return (int)g_pack.mesh.size()-1;}
static GLuint ebld_upload_mips(std::vector<unsigned char> p,int w,int h,bool b){assert(p.size()==(size_t)w*h*4);uploads++;uploaded.push_back(p);return uploads;}
static void ebld_uv_clear(){g_meshUVScaleU=g_meshUVScaleV=1;g_meshUVOffU=g_meshUVOffV=0;}
#include "../game/building_service.h"
static uint32_t u32(std::ifstream&f){uint32_t n;f.read((char*)&n,4);return n;}
static uint16_t u16(std::ifstream&f){uint16_t n;f.read((char*)&n,2);return n;}
static unsigned char u8(std::ifstream&f){char x;f.get(x);return (unsigned char)x;}
static EbldModel model(const char*path){
 std::ifstream f(path,std::ios::binary);char tag[8],type[8];f.read(tag,8);assert(!memcmp(tag,"EBLD",4));f.read(type,8);
 unsigned nt=u32(f),ns=u32(f),ne=u32(f),np=u32(f);f.seekg(np*4,std::ios::cur);EbldModel m;m.type=type;
 for(unsigned i=0;i<nt;i++){EbldTex t;t.w=u16(f);t.h=u16(f);f.seekg(4,std::ios::cur);t.house=u8(f);f.seekg(3,std::ios::cur);unsigned n=u32(f);f.seekg(n,std::ios::cur);m.tex.push_back(t);}
 unsigned pieces=u32(f),tris=u32(f);for(unsigned i=0;i<pieces;i++){f.seekg(32+4+12+12+4+8,std::ios::cur);unsigned n=u8(f);f.seekg(3+n*12,std::ios::cur);}
 for(unsigned i=0;i<tris;i++){EbldTri t;t.tex=(short)u16(f);f.seekg(6,std::ios::cur);t.mode=u8(f);u8(f);t.wrap=u8(f);u8(f);f.read((char*)t.v,3*44);m.set[0].tri.push_back(t);}
 assert(f.good());return m;
}
int main(int ac,char**av){assert(ac==4);int states=0,bindings=0,meshchecks=0;
 for(int k=1;k<=3;k++)for(int doing=-1;doing<=5;doing++)for(int stage=-1;stage<=61;stage++)for(int off=0;off<=1;off++)for(int alive=0;alive<=1;alive++){
  int want=0;if(!off&&alive&&doing!=0&&stage>=0){if(k==1&&doing==3&&stage>=6&&stage<12)want=1;else if(doing==2&&stage<7){if(k==2)want=stage%6<5?stage%6:0;else if(k==3){int a[]={0,1,2,3,2,1,0};want=a[stage];}}}
  assert(bservice_phase(k,doing,stage,off,alive)==want);states++;
 }
 for(int j=1;j<=3;j++){
  EbldModel m=model(av[j]);assert(bservice_bind(m,m.service));bindings++;int kind=m.service.kind;
  auto original=m;for(auto&t:m.set[0].tri)if(bservice_role(kind,t.v)){t.v[0].u+=.001f;break;}
  assert(!bservice_bind(m,m.service));bindings++;m=original;g_raModule=true;assert(!bservice_bind(m,m.service));g_raModule=false;bindings++;m=original;serviceEnabled=false;assert(!bservice_bind(m,m.service));bindings++;serviceEnabled=true;m=original;
  g_pack.mesh.clear();g_pack.tex.assign(m.tex.size(),PackTex());PackMesh src;
  for(const auto&t:m.set[0].tri){PackTri q;q.tex=t.tex;q.mode=t.mode;q.wrap=t.wrap;for(int c=0;c<3;c++){const auto&v=t.v[c];q.v[c]={v.x,v.y,v.z,v.u,v.v,v.r,v.g,v.b,v.a};src.nrm.insert(src.nrm.end(),{v.nx,v.ny,v.nz});}src.tfl.push_back(0);src.tris.push_back(q);}
  g_pack.mesh.push_back(src);bservice_mesh(m,0);assert(m.service.meshes.count(0));const auto&s=m.service.meshes[0];
  assert(s.bp.back()+s.pp.back()==(int)src.tris.size());assert(s.pp.back()==(kind==1?3:kind==2?26:6));
  std::vector<int> seen(src.tris.size());for(size_t i=0;i<src.tris.size();i++){
   const bool part=s.pp[i+1]>s.pp[i];const PackTri& q=part?g_pack.mesh[s.part[kind==1?1:0]].tris[s.pp[i]]:g_pack.mesh[s.body].tris[s.bp[i]];
   for(int c=0;c<3;c++){assert(q.v[c].x==src.tris[i].v[c].x&&q.v[c].y==src.tris[i].v[c].y&&q.v[c].z==src.tris[i].v[c].z);assert(q.v[c].a==src.tris[i].v[c].a);}
   if(!part)assert(memcmp(&q,&src.tris[i],sizeof q)==0);meshchecks++;
  }
  for(int f=0;f<(kind==1?2:kind==2?5:4);f++){
   std::vector<int> draws;g_meshUVScaleU=.5f;g_meshUVScaleV=.25f;g_meshUVOffU=.125f;g_meshUVOffV=.75f;
   bservice_draw(m,0,0,(int)src.tris.size(),f,[&](int mi,int a,int b){assert(b>a&&a>=0&&b<=(int)g_pack.mesh[mi].tris.size());draws.push_back(b-a);});
   assert(draws.size()==2&&draws[0]+draws[1]==(int)src.tris.size());
   assert(g_meshUVScaleU==.5f&&g_meshUVScaleV==.25f&&g_meshUVOffU==.125f&&g_meshUVOffV==.75f);meshchecks++;
  }
  if(kind==3){uploads=0;std::vector<unsigned char> p(512*512*4,128);auto alpha=p;long b=bservice_upload(m,1,p);assert(uploads==3&&b==3*(long)(p.size()*4/3));assert(bservice_upload(m,1,p)==0&&uploads==3);assert(p==alpha);
    const int rgb[3][3]={{116,148,156},{168,168,168},{192,192,208}},old[3]={132,132,156};
    for(int f=0;f<3;f++)for(size_t i=0;i<p.size();i+=4){assert(uploaded[f][i+3]==p[i+3]);for(int c=0;c<3;c++)assert(uploaded[f][i+c]==(128*rgb[f][c]+old[c]/2)/old[c]);}}
  BService keep=m.service;assert(bservice_bind(m,m.service));assert(m.service.meshes.empty()&&m.service.tex.empty());bindings++;
 }
 printf("SERVICE-TESTS|states=%d|bindings=%d|mesh=%d|PASS\n",states,bindings,meshchecks);
}
