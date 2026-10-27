from pathlib import Path
import struct,zlib
import argparse,hashlib,json,shutil,subprocess,tempfile,shlex,os
p=argparse.ArgumentParser(description='Exercise actual current-source livery selectors, palette builders, faction loader and fringe bleed.');p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[1]);p.add_argument('--work',type=Path);args=p.parse_args();r=args.repo.resolve();s=(r/'game/cnc_eyes.cpp').read_text();out=args.work or Path(tempfile.mkdtemp(prefix='opencnc3d-livery-regression-'));out.mkdir(parents=True,exist_ok=True)
input_paths=[r/'game/cnc_eyes.cpp',r/'game/faction_livery.h',r/'game/fx_filter.h',r/'game/ebld/FACT.ebld',r/'game/ebld/FACT.ebld.fclv'];before={str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for p in input_paths}

def func(name):
 import re
 matches=list(re.finditer(r'^static[^\n]*\b'+re.escape(name)+r'\(',s,re.M))
 for m in matches:
  a=m.start();brace=s.index('{',a);semi=s.find(';',a)
  if semi>=0 and semi<brace:continue
  b=brace;level=1;i=b+1
  # Strip comments before counting braces to avoid prose punctuation.
  while level:
   if s[i]=='{':level+=1
   elif s[i]=='}':level-=1
   i+=1
  return s[a:i]+'\n'
 raise ValueError(name)
text='''#include <vector>
#include <map>
#include <string>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <cassert>
#include <fstream>
#include <zlib.h>
typedef unsigned GLuint;
static bool g_raModule=false, g_teamColours=true;
enum { GL_TEXTURE_2D=1, GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T,GL_REPEAT,GL_CLAMP,GL_TEXTURE_MIN_FILTER,GL_TEXTURE_MAG_FILTER,GL_LINEAR_MIPMAP_LINEAR,GL_LINEAR,GL_NEAREST,GL_RGBA,GL_UNSIGNED_BYTE,GL_GENERATE_MIPMAP,GL_TRUE };
static GLuint bound=0,next=1;static std::map<GLuint,std::vector<unsigned char>> uploaded;
static void glGenTextures(int,GLuint* id){*id=next++;}static void glBindTexture(int,GLuint id){bound=id;}
static void glDeleteTextures(int,const GLuint*){}static void glTexParameteri(int,int,int){}static int fx_filter_mode(int x){return x;}static void fx_filter_note(GLuint,int){}
BLEED_IMPLEMENTATION
static void glTexImage2D(int,int,int,int w,int h,int,int,int,const void* p){const auto* b=(const unsigned char*)p;uploaded[bound]=std::vector<unsigned char>(b,b+(size_t)w*h*4);}
static void gl_upload_mips(const unsigned char* p,int w,int h){glTexImage2D(0,0,0,w,h,0,0,0,p);}
struct SimObject {int act,remap;char house[16];};
static std::vector<SimObject> g_objects;
'''
filter_source=(r/'game/fx_filter.h').read_text();a=filter_source.index('static long fx_bleed_rgba');b=filter_source.index('\n/* Flip it',a);bleed='static int fx_bilfix_on=1;\n'+filter_source[a:b];text=text.replace('BLEED_IMPLEMENTATION',bleed)
a=s.index('struct LiveryFactionTexel');b=s.index('bool mips = false; };',a)+len('bool mips = false; };');text+=s[a:b]+'\n'
a=s.index('enum { LIVERY_COUNT');b=s.index('/* Which PlayerColorType a house wears',a);text+=s[a:b]+'\n'
text+='static std::map<std::string,int> g_liveryOf;static std::map<std::string,unsigned> g_liveryFactionOf;static bool g_liveryLive=false;\n'
for name in ('livery_of_house','livery_slot','team_livery_slot','livery_colour_from_slot','dos_livery_index','livery_texture','livery_ramp_k','livery_rd','livery_lum','livery_recolour','obj_side','obj_is_gdi','sprite_house','sprite_livery','mesh_house','livery_reset','livery_colour_factions'):
 if name=='livery_rd':text+='static const double LIVERY_GRADE_SAT=0.72;\n'
 if name=='sprite_house':text+='enum {SPRH_GDI=0,SPRH_NOD=1};\n'
 text+=func(name)
text+='#include "'+str(r/'game/faction_livery.h')+'"\n'+func('livery_build_one')
text+='''
enum {DA_COUNT=1};struct DosInfType {int strip[2][DA_COUNT];};
struct DosStrip {GLuint gl_extra[7];int texw,texh;};
static bool g_dosinfOn=true;static std::vector<DosStrip> g_dosStrips;static std::vector<std::vector<unsigned char>> g_dosIdx;static std::map<std::string,DosInfType> g_dosInfTypes;static unsigned char g_dosPal8[768]={};
'''+func('dosinf_livery_build')
text+='''enum {N64A_COUNT=1};struct N64InfStrip {GLuint gl_livery[7]={};int texw,texh,frames,facings,fw,fh;std::vector<unsigned char> rgba;};struct N64InfType{int strip[2][N64A_COUNT];};static std::vector<N64InfStrip> g_n64Strips;static std::map<std::string,N64InfType> g_n64InfTypes;\n'''+func('livery_wanted')+func('n64inf_livery_build')+'struct Pack {std::vector<PackTex> tex;};static Pack g_pack;\n'+func('livery_build_range')
text+='''static std::vector<unsigned char> read(const std::string& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
int main(int argc,char**argv){assert(argc==2);const std::string root=argv[1];
 SimObject o={0,-1,"GoodGuy"};int row=-1;
 assert(mesh_house(o)==1 && sprite_livery(o,&row)==-1 && row==SPRH_GDI);o.act=1;strcpy(o.house,"BadGuy");assert(mesh_house(o)==0 && sprite_livery(o,&row)==-1 && row==SPRH_NOD);
 strcpy(o.house,"Multi1");for(int faction=0;faction<2;faction++)for(int c=0;c<8;c++){o.act=faction;g_liveryOf["Multi1"]=c;assert(mesh_house(o)==(team_livery_slot(c)|(faction?16:0)));const int liv=sprite_livery(o,&row);assert(c?liv==c:liv==-1);if(c<=1)assert(row==SPRH_GDI);}
 g_liveryOf["Multi1"]=0;o.act=0;assert(mesh_house(o)==1);o.act=1;assert(mesh_house(o)==17);g_liveryOf["Multi1"]=2;assert(mesh_house(o)==18); // same object after ownership/faction and paint changes
 g_teamColours=false;assert(mesh_house(o)==0);g_teamColours=true;
 g_liveryFactionOf["Multi1"]=2;g_objects.clear();assert(livery_colour_factions(2)==2);g_liveryLive=true;livery_reset();assert(g_liveryOf.empty() && g_liveryFactionOf.empty() && !g_liveryLive);strcpy(o.house,"BadGuy");assert(mesh_house(o)==0);

 for(int ti:{0,2,4}){auto b=read(root+"/texture-"+std::to_string(ti)+".raw");assert(b.size()==2048u*2048u*9u);const size_t n=2048u*2048u;
  PackTex t; t.w=t.h=2048;t.gl=100;t.gl_gdi=101;memset(t.gl_extra,0,sizeof t.gl_extra);t.gdi.assign(b.begin(),b.begin()+4*n);std::vector<unsigned char> nod(b.begin()+4*n,b.begin()+8*n);t.mask.assign(b.begin()+8*n,b.end());
  livery_faction_load(t,(root+"/FACT.ebld").c_str(),ti,t.gdi,nod,t.mask);assert(!t.faction.empty());auto golden=read(root+"/alternate-"+std::to_string(ti)+".rgba");
  std::vector<unsigned char> out;long npx=0;assert(livery_build_one(t,0,out,&npx,true)==1);assert(out==golden);assert(livery_texture(t,17)==t.gl_faction[0]);assert(livery_texture(t,1)==101 && livery_texture(t,0)==100);
  for(int c=1;c<8;c++){assert(livery_build_one(t,c,out,&npx)==1);assert(livery_texture(t,team_livery_slot(c))==t.gl_extra[team_livery_slot(c)-2]);assert(livery_build_one(t,c,out,&npx,true)==1);assert(livery_texture(t,team_livery_slot(c)|16)==t.gl_faction[c]);}
  PackTex bad=t;bad.faction.clear();auto corrupt=t.gdi;corrupt[0]^=1;livery_faction_load(bad,(root+"/FACT.ebld").c_str(),ti,corrupt,nod,t.mask);assert(bad.faction.empty());
  for(GLuint id:t.gl_extra)if(id)uploaded.erase(id);for(GLuint id:t.gl_faction)if(id)uploaded.erase(id);
  if(ti==0)for(const char* test:{"short","magic","empty","huge","wrongdim","crc","zlib","duplicate","outofrange","alpha"}){bad.faction.clear();livery_faction_load(bad,(root+"/bad-"+test).c_str(),ti,t.gdi,nod,t.mask);assert(bad.faction.empty());}

 }
 // The actual range builder allocates one requested faction family, never an unused second family.
 auto fraw=read(root+"/texture-0.raw");const size_t fn=2048u*2048u;
 for(unsigned factions:{1u,2u,3u}){g_pack.tex.clear();PackTex tx;tx.w=tx.h=2048;tx.gl=100;tx.gl_gdi=101;memset(tx.gl_extra,0,sizeof tx.gl_extra);tx.gdi.assign(fraw.begin(),fraw.begin()+4*fn);tx.mask.assign(fraw.begin()+8*fn,fraw.end());std::vector<unsigned char> nod(fraw.begin()+4*fn,fraw.begin()+8*fn);livery_faction_load(tx,(root+"/FACT.ebld").c_str(),0,tx.gdi,nod,tx.mask);g_pack.tex.push_back(tx);g_liveryOf.clear();g_liveryFactionOf.clear();g_liveryOf["Multi1"]=1;g_liveryFactionOf["Multi1"]=factions;g_liveryLive=true;GLuint before=next;livery_build_range(0,1);assert(next-before==(factions==3?2u:1u));assert((g_pack.tex[0].gl_extra[6]!=0)==((factions&1)!=0));assert((g_pack.tex[0].gl_faction[1]!=0)==((factions&2)!=0));for(GLuint id:g_pack.tex[0].gl_extra)if(id)uploaded.erase(id);for(GLuint id:g_pack.tex[0].gl_faction)if(id)uploaded.erase(id);}g_pack.tex.clear();
 // The actual DOS builder substitutes only 176..191 from its stored index data.
 g_liveryOf.clear();g_liveryOf["Multi1"]=1;g_liveryOf["Multi2"]=2;g_dosStrips.resize(2);g_dosStrips[0]={};g_dosStrips[0].texw=16;g_dosStrips[0].texh=1;g_dosIdx.resize(2);for(int k=0;k<16;k++)g_dosIdx[0].push_back((unsigned char)(176+k));DosInfType ty={{{0},{1}}};g_dosInfTypes["E1"]=ty;dosinf_livery_build();
 for(int c:{1,2}){auto tex=g_dosStrips[0].gl_extra[dos_livery_index(c)];assert(tex);const auto& pixels=uploaded[tex];for(int k=0;k<16;k++){assert(!memcmp(&pixels[k*4],LIVERY_BAND[c][k],3));assert(pixels[k*4+3]==255);}}
 g_n64Strips.resize(2);for(auto& n:g_n64Strips){n.texw=16;n.texh=1;n.frames=n.facings=n.fh=1;n.fw=16;n.rgba.resize(64,255);}
 for(int k=0;k<16;k++){memcpy(&g_n64Strips[0].rgba[k*4],KEY_BAND[k],3);g_n64Strips[1].rgba[k*4]=0;g_n64Strips[1].rgba[k*4+1]=0;g_n64Strips[1].rgba[k*4+2]=0;}N64InfType nt={{{0},{1}}};g_n64InfTypes["E1"]=nt;n64inf_livery_build();
 for(int c:{1,2}){auto tex=g_n64Strips[0].gl_livery[dos_livery_index(c)];assert(tex);for(int k=0;k<16;k++)assert(!memcmp(&uploaded[tex][k*4],LIVERY_BAND[c][k],3));}for(const auto& n:g_n64Strips)assert(n.rgba.empty());
 PackTex edge;edge.w=edge.h=5;memset(edge.gl_extra,0,sizeof edge.gl_extra);edge.gdi.resize(100,0);edge.nod.resize(100,0);memcpy(&edge.gdi[48],KEY_BAND[2],3);edge.gdi[51]=edge.nod[51]=255;std::vector<unsigned char> goodEdge,badEdge;long edgePx=0;assert(livery_build_one(edge,1,goodEdge,&edgePx)==1);
 for(int p=0;p<25;p++){assert(!memcmp(&goodEdge[p*4],LIVERY_BAND[1][2],3));assert(goodEdge[p*4+3]==(p==12?255:0));}edge.gl_extra[6]=0;fx_bilfix_on=0;assert(livery_build_one(edge,1,badEdge,&edgePx)==1);assert(goodEdge!=badEdge && badEdge[0]==0);fx_bilfix_on=1;
 puts("LIVERY-REGRESSION|PASS|campaign=2|faction-paint=16|capture-selector=3|wreck-owner-join=1|mission-reset=1|atlas-phases=3|palette-builds=45|invalid-sidecars=13|family-allocation-controls=3|DOS-band-entries=32|N64-band-entries=32|fringe-alpha=25|bleed-negative=1");
}
'''
(out/'livery-unit.cpp').write_text(text)
b=(r/'game/ebld/FACT.ebld').read_bytes();off=32+struct.unpack_from('<I',b,28)[0]*4
from PIL import Image
for i in range(struct.unpack_from('<I',b,16)[0]):
 sz=struct.unpack_from('<I',b,off+12)[0];off+=16;raw=zlib.decompress(b[off:off+sz]);off+=sz
 if i in (0,2,4):
  (out/f'texture-{i}.raw').write_bytes(raw);phase={0:'intact',2:'damaged',4:'overlay'}[i];(out/f'alternate-{i}.rgba').write_bytes(Image.open(r/f'art/remaster/FACT/faction-livery/v01/FACT-{phase}-Nod-Gold.png').convert('RGBA').tobytes())
(out/'FACT.ebld.fclv').write_bytes((r/'game/ebld/FACT.ebld.fclv').read_bytes())

# Malformed payload and metadata controls must leave the authored texture untouched.
blob=(r/'game/ebld/FACT.ebld.fclv').read_bytes();hdr=list(struct.unpack_from('<8I',blob,12));start=44;raw=zlib.decompress(blob[start:start+hdr[7]])
variants={'short':blob[:31],'magic':b'NOTFCLV!'+blob[8:],'empty':blob[:8]+struct.pack('<I',0)}
for key,field,value in [('huge',6,0xffffffff),('wrongdim',1,1024),('crc',3,hdr[3]^1)]:
 h=hdr.copy();h[field]=value;variants[key]=blob[:12]+struct.pack('<8I',*h)+blob[44:]
variants['zlib']=blob[:44]+b'BAD'+blob[47:]
for key in ('duplicate','outofrange','alpha'):
 t=bytearray(raw)
 if key=='duplicate':t[9:13]=t[:4]
 if key=='outofrange':t[:4]=struct.pack('<I',2048*2048)
 if key=='alpha':t[7]^=1
 z=zlib.compress(bytes(t));h=hdr.copy();h[7]=len(z);variants[key]=blob[:12]+struct.pack('<8I',*h)+z+blob[start+hdr[7]:]
for key,data in variants.items():(out/f'bad-{key}.fclv').write_bytes(data)
cxx=shlex.split(os.environ.get('CXX','clang++'));assert shutil.which(cxx[0]),'C++ compiler is required'
with (out/'build.log').open('w') as f:subprocess.run(cxx+['-std=c++14','-O2','-Wall','-Wextra',str(out/'livery-unit.cpp'),'-lz','-o',str(out/'livery-unit')],check=True,stdout=f,stderr=subprocess.STDOUT)
with (out/'result.log').open('w') as f:subprocess.run([str(out/'livery-unit'),str(out)],check=True,stdout=f,stderr=subprocess.STDOUT)
last=(out/'result.log').read_text().splitlines()[-1];assert last.startswith('LIVERY-REGRESSION|PASS|'),last
after={str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for p in input_paths};assert before==after,'regression input changed'
result={'status':'PASS','test_summary':last,'source_unchanged':True,'input_hashes':before,'harness_sha256':hashlib.sha256((out/'livery-unit.cpp').read_bytes()).hexdigest(),'native_platform':'macOS test host; this is not a Windows game FPS measurement','work':str(out)};(out/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
