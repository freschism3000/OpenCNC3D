#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <set>
#include <map>
#include <string>
#include <algorithm>
#include <new>
#include <utility>
typedef uint64_t Uint64;typedef unsigned GLuint;typedef int GLint;typedef unsigned GLenum;
#define GL_TEXTURE_BINDING_2D 1
#define GL_TEXTURE_2D 2
#define GL_TEXTURE_WIDTH 3
const int EBS_CUT=1,EBS_DESTROYED=2,EBR=6,EBM_N=16,EBM_CUT=1,EBM_OVER0=4,EBM_WRECK=12,EBM_GLOWX=13,K_BUILDING=1,K_TERRAIN=2,BUILDING_TYPE=8,LIVERY_COUNT=8;
const int EBX_WALL=1;
const int MODE_SHADOW=3;
struct PackTri {unsigned char payload[91],mode=0;};struct PackPart{int a[6];};struct PackClip{int a[4];};
struct PackType{int mesh=-1;};
struct LiveryFactionTexel{uint32_t pixel;unsigned char rgba[4],mask;};
struct PackTex{int w=128,h=128;GLuint gl=0,gl_gdi=0,gl_extra[7]={},gl_faction[8]={};std::vector<unsigned char> gdi,nod,mask;std::vector<LiveryFactionTexel> faction;};
struct PackMesh{std::vector<PackTri>tris;std::vector<PackPart>parts;std::vector<int>sections,gpuWaveTris;std::vector<float>nrm,swayPh,animMat;std::vector<unsigned char>tfl,sway,animVis;std::vector<PackClip>clips;};
struct BServiceMesh{std::vector<int>bp,pp;};struct BService{int kind=0;std::map<int,BServiceMesh>meshes;std::map<int,std::vector<int>>tex;};
struct EbldPiece{int ntri=16;};struct EbldSet{std::vector<PackTri>tri;std::vector<EbldPiece>piece;};struct EbldTex{int w=128,h=128,house=0,role=0;long csize=0;bool cover=false;};
struct EbldModel{bool attached=true,resident=false,residentAll=false,cutBuilt=false;int texOwner=-1,tex0=0,nset=3,version=6,cutNext=0,cutBlock=0,wallMask=-1,flags=0,stageOf=-1,stageModel[3]={-1,-1,-1};uint64_t stageMasks[4]={1,3,2,2};size_t cutPiece=0;std::string path="missing-test-file",type="NUKE";std::vector<EbldTex>tex;std::vector<unsigned char>texIntact;EbldSet set[3];BService service;PackMesh cutTemp;std::vector<float>cutAngles;std::vector<int>rng[EBM_N];std::vector<std::pair<int,int>>waveMesh[EBM_N];int mesh[EBM_N]={};};
struct SimObject{int kind=K_BUILDING,str=100,id=-1,authored=-1,cx=0,cy=0,cartMesh=0;float construction=1.0f;bool limbo=false,constructing=false,shown=true;char type[16]="NUKE";};struct SbEntry{int type=BUILDING_TYPE;bool constructing=false,onHold=false,completed=false,busy=false;char name[16]="NUKE";};
struct WallCell {short x=0,y=0;unsigned char kind=0,icon=0,dmg=0;};
std::vector<WallCell> g_walls;
bool g_wallsDraw=true,g_clumpTrees=true;
const int EBLD_NWALL=5;
int ebld_wall_kind(const char* type){const char* names[]={"SBAG","CYCL","BRIK","BARB","WOOD"};for(int index=0;index<EBLD_NWALL;index++)if(!strcmp(type,names[index]))return index;return -1;}
long long ebld_wall_key(int x,int y){return 9000000000LL+(long long)y*512+x;}
enum { EB_INTACT, EB_HELD, EB_DAMAGED, EB_WRECKED };
struct EbldBld { int model=0,stage=EB_INTACT,cx=0,cy=0,treeStage=0;bool orphan=false,shown=true; };
std::map<long long,EbldBld> g_ebldBld;
bool stageResidency=true,requiredPrepare=true,requiredModels=true,g_etreeOn=true;
const EbldBld* ebld_find(const SimObject& object) {
    const auto found=g_ebldBld.find(object.id);
    return found==g_ebldBld.end()?NULL:&found->second;
}
uint64_t ebld_all_texture_mask(const EbldModel& model) {
    return model.tex.size()>=64?~uint64_t(0):(uint64_t(1)<<model.tex.size())-1;
}
uint64_t ebld_texture_mask_for_stage(const EbldModel& model,int stage) {
    return !stageResidency||(model.flags&EBX_WALL)||model.wallMask>=0?ebld_all_texture_mask(model):model.stageMasks[stage];
}
void shroud_corners_sync(){}
bool visible(const SimObject& object){return object.shown&&!object.limbo;}
float construction_frac(const SimObject& object){return object.construction;}
int object_model3d(const SimObject& object){return object.authored;}
bool ebld_anchor_shown(const EbldBld& entry){return entry.shown;}
std::set<std::pair<int,int>> outsideCells,hiddenCells;
bool cell_shown(int x,int y){return !outsideCells.count(std::make_pair(x,y));}
bool shroud_cell_hidden(int x,int y){return hiddenCells.count(std::make_pair(x,y));}
struct ClumpDef {int count=3;};
const ClumpDef* clump_def(const char* type){static ClumpDef clump;return !strcmp(type,"TC02")?&clump:NULL;}
int clump_def_count(const ClumpDef* clump){return clump?clump->count:0;}
SimObject clump_part_n(const SimObject& object,const ClumpDef*,int partIndex){
    static const int offsets[3][2]={{1,0},{0,1},{1,1}};
    static const char* types[4]={"T01","T02","T05","T07"};
    SimObject part=object;part.cx+=offsets[partIndex][0];part.cy+=offsets[partIndex][1];part.id=-1;part.cartMesh=0;
    strcpy(part.type,types[(part.cx*3+part.cy*5)&3]);return part;
}
bool clump_cell_drawn(int x,int y){return cell_shown(x,y)&&!shroud_cell_hidden(x,y);}
int mesh_for(const SimObject& object){return object.cartMesh;}
bool ebld_meshes_ok(const EbldModel& model){return model.attached;}
struct{bool valid=false;std::vector<SbEntry>entry;}g_sbState;
struct{bool enabled=true,dos_classic=false,tree3d=true;}g_fx;
struct{std::vector<PackMesh>mesh;std::vector<PackTex>tex;std::map<std::string,PackType>type;}g_pack;
struct EbldPrepareMetrics{uint64_t demandCutUs=0,demandTextureUs=0;unsigned demandCuts=0,demandTextures=0,demandFailures=0;}g_ebldPrepareMetrics;
std::vector<EbldModel>g_ebld;std::vector<SimObject>g_objects;std::map<std::string,int>g_liveryOf;
struct EbldFile {std::string path;int ti=0,th=0;bool tried=false,lazy=true;};
const char* EBLD_TYPES[]={"T01","T02","T05","T07"};
std::vector<EbldFile> g_ebldFiles;
unsigned g_ebldPackSerial=0;
std::vector<std::string> ensuredTypes;
int ensureFailure=0;
int ebld_pack_theater(){return 0;}
bool ebld_ensure(const char* type,size_t limit=0){
    ensuredTypes.push_back(type);
    if(limit!=4u*1024u*1024u)return false;
    for(EbldFile& file:g_ebldFiles)if(!file.tried&&!strcmp(type,EBLD_TYPES[file.ti])){
        file.tried=true;
        if(ensureFailure==1)throw std::bad_alloc();
        if(ensureFailure==5)return false;
        EbldModel model;model.type=type;model.nset=1;model.tex0=(int)g_pack.tex.size();model.tex.resize(1);model.texIntact={1};
        if(ensureFailure&&!g_ebld.empty())g_ebld[0].attached=false;
        g_ebld.push_back(model);
        if(ensureFailure==2)throw std::bad_alloc();
        g_pack.tex.push_back(PackTex());
        if(ensureFailure==3)throw std::bad_alloc();
        g_pack.mesh.push_back(PackMesh());g_ebldPackSerial++;
        if(ensureFailure==4)throw std::bad_alloc();
        return true;
    }
    return false;
}
unsigned modelLookups=0;
int ebld_model_of(const char* type){modelLookups++;for(size_t index=0;index<g_ebld.size();index++)if(g_ebld[index].attached&&g_ebld[index].type==type)return (int)index;return -1;}
bool g_raModule=false,g_editOn=false,g_liveryLive=false,g_teamColours=false,option=true,quitRequested=false,throwTexture=false,throwMesh=false,refuseTexture=false,failRead=false;
unsigned g_ebldPrepareGeneration=1;int g_engineFrame=0,g_simTicks=0;long g_ebldResidentBytes=0;void*g_au=nullptr;uint64_t clockNow=0;unsigned textureCalls=0,meshCalls=0,finished=0,pumped=0,forgot=0,deleted=0,nextGL=10;GLuint bound=7;std::map<GLuint,int>widths={{7,128}};
bool mesh_perf_switch(const char* name){return !strcmp(name,"OPENCNC3D_EBLD_STAGE_RESIDENCY")?stageResidency:!strcmp(name,"OPENCNC3D_EBLD_REQUIRED_PREPARE")?requiredPrepare:!strcmp(name,"OPENCNC3D_EBLD_REQUIRED_MODELS")?requiredModels:option;}bool ebld_rule_now(){return g_fx.enabled;}const char*ebld_alias(const char*t){return t;}bool building_constructing(const SimObject&o){return o.constructing;}
void livery_wanted(bool*w){for(int i=0;i<8;i++)w[i]=true;}int team_livery_slot(int c){return c;}unsigned livery_colour_factions(int){return 3;}
Uint64 SDL_GetPerformanceCounter(){return clockNow+=100;}Uint64 SDL_GetPerformanceFrequency(){return 1000000;}void SDL_PumpEvents(){pumped++;}bool SDL_QuitRequested(){return quitRequested;}void audio_frame(void*,int){}void glFinish(){finished++;}
void glGetIntegerv(GLenum,GLint*p){*p=(GLint)bound;}void glBindTexture(GLenum,GLuint id){bound=id;}void glGetTexLevelParameteriv(GLenum,int,GLenum,GLint*p){*p=widths[bound];}void glDeleteTextures(int n,const GLuint*p){for(int i=0;i<n;i++){widths.erase(p[i]);deleted++;}}void fx_filter_forget(GLuint){forgot++;}
bool ebld_resident(EbldModel&M,bool,size_t,size_t t){textureCalls++;if(failRead){M.attached=false;return false;}PackTex&pt=g_pack.tex[M.tex0+t];pt.gl=nextGL++;widths[pt.gl]=refuseTexture?0:pt.w;bound=pt.gl;if(throwTexture)throw std::bad_alloc();if(M.tex[t].house&1){pt.gl_gdi=nextGL++;widths[pt.gl_gdi]=pt.w;pt.gdi.resize((size_t)pt.w*pt.h*4);if(M.tex[t].house&2)pt.mask.resize((size_t)pt.w*pt.h);}g_ebldResidentBytes+=(long)pt.w*pt.h*4;M.resident=M.residentAll=true;for(size_t i=0;i<M.tex.size();i++)if(!g_pack.tex[M.tex0+i].gl){M.residentAll=false;if(M.texIntact[i])M.resident=false;}bound=7;return true;}
bool ebld_build_cut_step(EbldModel&M){meshCalls++;M.cutTemp.tris.resize(M.cutTemp.tris.size()+10);if(throwMesh)throw std::bad_alloc();M.cutPiece++;if(M.cutPiece>=2){g_pack.mesh.push_back(std::move(M.cutTemp));M.cutTemp=PackMesh();M.cutPiece=0;M.cutNext++;if(M.cutNext==13)M.cutBuilt=true;}return true;}
#ifdef TEST_WINDOWS_HEADROOM
#define _WIN32 1
struct MEMORYSTATUSEX{unsigned dwLength;uint64_t ullAvailVirtual,ullAvailPhys;};
uint64_t availableVirtual=8ull<<30,availablePhysical=8ull<<30;bool memoryQuery=true;
bool GlobalMemoryStatusEx(MEMORYSTATUSEX*m){m->ullAvailVirtual=availableVirtual;m->ullAvailPhys=availablePhysical;return memoryQuery;}
#endif
#ifdef TEST_NONWINDOWS_HEADROOM
#undef _WIN32
#endif
#include "ebld_prepare.h"
int main(){unsigned checks=0;auto ck=[&](bool ok,const char*name){checks++;if(!ok){printf("FAIL %s (%u)\n",name,checks);return false;}return true;};
 auto reset=[&](){g_pack.mesh.clear();g_pack.tex.clear();g_pack.type.clear();g_ebld.clear();g_objects.clear();g_ebldBld.clear();g_walls.clear();outsideCells.clear();hiddenCells.clear();g_ebldFiles.clear();ensuredTypes.clear();ensureFailure=0;g_ebldPackSerial=7;g_wallsDraw=g_clumpTrees=true;stageResidency=requiredPrepare=requiredModels=g_etreeOn=true;g_sbState.entry.clear();g_sbState.valid=false;g_liveryOf.clear();g_ebldPrepareGeneration++;g_engineFrame++;option=true;g_fx={true,false,true};g_raModule=false;g_editOn=false;g_liveryLive=g_teamColours=false;throwTexture=throwMesh=refuseTexture=failRead=false;quitRequested=false;textureCalls=meshCalls=finished=modelLookups=0;bound=7;widths={{7,128}};ebld_prepare_reset();};
 auto model=[&](const char*name){EbldModel m;m.type=name;m.tex0=(int)g_pack.tex.size();m.tex.resize(2);m.texIntact={1,0};m.set[1].tri.resize(100);m.set[1].piece.resize(10);m.set[2].tri.resize(100);m.set[2].piece.resize(10);g_pack.tex.resize(g_pack.tex.size()+2);g_ebld.push_back(m);};
 auto lazyClump=[&](){
     g_objects.push_back(SimObject());g_objects[0].kind=K_TERRAIN;g_objects[0].cartMesh=-1;strcpy(g_objects[0].type,"TC02");
     for(int index=0;index<4;index++){
         EbldFile file;file.path=std::string(EBLD_TYPES[index])+".ebld";file.ti=index;
         unsigned char bytes[512]={};memcpy(bytes,"EBLD",4);memcpy(bytes+8,EBLD_TYPES[index],3);
         const uint32_t version=6,sets=1;memcpy(bytes+4,&version,4);memcpy(bytes+20,&sets,4);
         FILE* stream=fopen(file.path.c_str(),"wb");if(!stream)return false;
         const bool written=fwrite(bytes,1,sizeof(bytes),stream)==sizeof(bytes);fclose(stream);if(!written)return false;
         g_ebldFiles.push_back(file);
         g_pack.type[EBLD_TYPES[index]]=PackType();
     }
     return true;
 };
 reset();model("NUKE");model("PYLE");g_objects.push_back(SimObject());option=false;ebld_prepare_loading();if(!ck(textureCalls==0&&meshCalls==0&&g_ebldPrepare.models.empty(),"default-off control does no work"))return 1;
 option=true;ebld_prepare_observe();if(!ck(g_ebldPrepare.models.size()==1,"only actual type observed"))return 1;g_sbState.valid=true;SbEntry e;strcpy(e.name,"PYLE");e.busy=true;g_sbState.entry.push_back(e);g_engineFrame++;ebld_prepare_observe();if(!ck(g_ebldPrepare.models.size()==1,"busy column never preloads catalog"))return 1;g_sbState.entry[0].constructing=true;g_engineFrame++;ebld_prepare_observe();if(!ck(g_ebldPrepare.models.size()==2&&g_ebldPrepare.constructing.count(1),"actual queue type added before placement"))return 1;
 if(!ck(ebld_prepare_step(false,true)&&meshCalls==1&&textureCalls==0,"live construction performs geometry only"))return 1;
 ebld_prepare_loading();if(!ck(g_ebld[0].cutBuilt&&g_ebld[1].cutBuilt&&g_ebld[0].residentAll&&g_ebld[1].residentAll,"loading completes all exact jobs"))return 1;if(!ck(g_ebldPrepare.cpu<=EBLD_PREPARE_CPU&&g_ebldPrepare.gpu<=EBLD_PREPARE_GPU&&g_ebldPrepare.transientPeak<=EBLD_PREPARE_TRANSIENT,"all memory caps"))return 1;if(!ck(finished==1,"one loading retirement barrier"))return 1;const unsigned calls=meshCalls+textureCalls;ebld_prepare_loading();if(!ck(calls==meshCalls+textureCalls&&finished==1,"loading once per generation"))return 1;
 g_ebldPrepareGeneration++;ebld_prepare_observe();if(!ck(g_ebldPrepare.cpu==0&&g_ebldPrepare.gpu==0&&!g_ebldPrepare.loadingAttempted,"old generation cancelled"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());g_ebld[0].tex[0].w=8192;g_ebld[0].tex[0].h=8192;g_ebld[0].nset=1;g_ebld[0].texIntact[1]=1;g_ebld[0].tex[1]=g_ebld[0].tex[0];ebld_prepare_observe();if(!ck(!ebld_prepare_step(true)&&textureCalls==0&&g_ebldPrepare.rejected.size()==2,"over-budget work remains demand-only"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());g_fx.enabled=false;ebld_prepare_loading();if(!ck(textureCalls==0&&!g_ebldPrepare.loadingAttempted,"Classic mode inactive"))return 1;g_fx.enabled=true;ebld_prepare_loading();if(!ck(textureCalls==2&&g_ebld[0].residentAll,"mode roundtrip exact readiness"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());ebld_prepare_observe();g_ebld[0].tex[0].house=3;throwTexture=true;const unsigned oldDeleted=deleted;if(!ck(!ebld_prepare_step(true)&&g_ebldPrepare.disabledByFailure&&g_pack.tex[0].gl==0&&deleted>oldDeleted&&bound==7&&g_ebld[0].attached,"allocation refusal rolls back unpublished GL/CPU/state"))return 1;throwTexture=false;if(!ck(ebld_resident(g_ebld[0],true,1,0)&&g_pack.tex[0].gl!=0,"exact demand can retry after preparation failure"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());ebld_prepare_observe();refuseTexture=true;if(!ck(!ebld_prepare_step(true)&&g_pack.tex[0].gl==0&&g_ebldPrepare.disabledByFailure&&bound==7,"driver texture refusal rolls back"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());ebld_prepare_observe();throwMesh=true;if(!ck(!ebld_prepare_step(false)&&g_ebldPrepare.disabledByFailure&&g_pack.mesh.empty()&&g_ebld[0].cutTemp.tris.empty()&&g_ebld[0].cutPiece==0,"geometry allocation refusal restores incomplete kind"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());ebld_prepare_observe();failRead=true;if(!ck(!ebld_prepare_step(true)&&g_ebld[0].attached&&g_pack.tex[0].gl==0&&g_ebldPrepare.disabledByFailure,"optional I/O refusal rolls back and defers exact demand"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());ebld_prepare_observe();g_ebldPrepare.cpu=EBLD_PREPARE_CPU;if(!ck(!ebld_prepare_step(false)&&meshCalls==0,"CPU admission cannot exceed cap"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());ebld_prepare_observe();g_ebld[0].tex[0].w=1;g_ebld[0].tex[0].h=128;const EbldPrepareCost cost=ebld_prepare_texture_cost(g_ebld[0],0);if(!ck(cost.gpu==1020,"non-square exact mip byte accounting"))return 1;
#ifdef TEST_WINDOWS_HEADROOM
 EbldPrepareCost request;request.cpu=32u<<20;request.gpu=64u<<20;request.transient=64u<<20;
 if(!ck(ebld_prepare_headroom(request),"Windows sufficient headroom"))return 1;
 availableVirtual=300u<<20;if(!ck(!ebld_prepare_headroom(request),"Windows virtual floor includes CPU/GPU mirror/transient"))return 1;availableVirtual=8ull<<30;
 availablePhysical=64u<<20;if(!ck(!ebld_prepare_headroom(request),"Windows physical floor"))return 1;availablePhysical=8ull<<30;
 memoryQuery=false;if(!ck(!ebld_prepare_headroom(request),"Windows failed memory query defers optional prep"))return 1;
 memoryQuery=true;
#endif
 if(!ck(ebld_prepare_remaining(100,101)==0&&ebld_prepare_remaining(100,100)==0&&ebld_prepare_remaining(100,17)==83,"saturating remaining never wraps"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());ebld_prepare_observe();g_ebldPrepare.cpu=EBLD_PREPARE_CPU+1;g_ebldPrepare.gpu=EBLD_PREPARE_GPU+1;if(!ck(!ebld_prepare_step(true)&&textureCalls==0&&meshCalls==0,"overcharged counters cannot admit new work"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());g_objects[0].id=42;
 g_ebldBld[42].stage=EB_DAMAGED;ebld_prepare_observe();
 if(!ck(g_ebldPrepare.requiredTextures[0]==2,"initial damaged stage selects actual damage textures"))return 1;
 if(!ck(ebld_prepare_step(true)&&g_pack.tex[1].gl&&!g_pack.tex[0].gl,"current damaged texture precedes unused intact texture"))return 1;
 g_objects.push_back(SimObject());g_engineFrame++;ebld_prepare_observe();
 if(!ck(g_ebldPrepare.requiredTextures[0]==3,"same-model stages union rather than overwrite"))return 1;
 g_objects.pop_back();g_ebldBld[42].stage=EB_INTACT;g_engineFrame++;ebld_prepare_observe();
 if(!ck(g_ebldPrepare.requiredTextures[0]==1,"repair refreshes current requirement"))return 1;
 reset();model("NUKE");g_ebldBld[42].stage=EB_WRECKED;ebld_prepare_observe();
 if(!ck(g_ebldPrepare.models.size()==1&&g_ebldPrepare.requiredTextures[0]==2,"persisted wreck without live engine object is prepared"))return 1;
 reset();model("NUKE");g_ebldBld[42].stage=EB_WRECKED;g_ebldBld[42].orphan=true;ebld_prepare_observe();
 if(!ck(g_ebldPrepare.models.empty(),"orphan tree is not treated as building wreck"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());g_objects[0].id=42;g_ebldBld[42].stage=EB_DAMAGED;
 stageResidency=false;ebld_prepare_observe();
 if(!ck(ebld_prepare_step(true)&&g_pack.tex[0].gl&&!g_pack.tex[1].gl,"disabled control preserves intact-first ordering"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());g_objects[0].id=42;g_ebldBld[42].stage=EB_DAMAGED;ebld_prepare_observe();
 const uint64_t requiredBytes=ebld_prepare_texture_cost(g_ebld[0],1).gpu;
 g_ebldPrepare.gpu=EBLD_PREPARE_GPU-requiredBytes;
 if(!ck(ebld_prepare_step(true)&&g_pack.tex[1].gl&&g_ebldPrepare.gpu==EBLD_PREPARE_GPU,"last budget space belongs to current stage"))return 1;
 g_ebld[0].cutBuilt=true;
 if(!ck(!ebld_prepare_step(true)&&!g_pack.tex[0].gl&&g_ebldPrepare.gpu==EBLD_PREPARE_GPU,"optional intact texture cannot bypass cap"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());
 ebld_prepare_loading(2000.0,true);
 if(!ck(!g_ebldPrepare.loadingAttempted&&textureCalls==0,"script optional loading defers until required boundary"))return 1;
 const int engineBefore=g_engineFrame,ticksBefore=g_simTicks;
 ebld_prepare_required_loading();
 if(!ck(g_ebldPrepare.requiredReady&&g_ebldPrepare.requiredPlanned==1&&g_ebldPrepare.requiredCompleted==1
        &&g_pack.tex[0].gl&&!g_pack.tex[1].gl&&meshCalls==0,"required loading publishes only exact current-stage slots"))return 1;
 if(!ck(g_engineFrame==engineBefore&&g_simTicks==ticksBefore&&finished==1,"required loading preserves clocks and retires uploads once"))return 1;
 const unsigned requiredCalls=textureCalls;
 ebld_prepare_required_loading();
 if(!ck(textureCalls==requiredCalls&&finished==1,"required work executes once per generation"))return 1;
 ebld_prepare_loading(2000.0,true);
 if(!ck(g_ebldPrepare.loadingAttempted&&g_pack.tex[1].gl,"optional preparation resumes after required boundary"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());requiredPrepare=false;
 ebld_prepare_required_loading();ebld_prepare_loading(2000.0,true);
 if(!ck(!g_ebldPrepare.requiredAttempted&&g_ebldPrepare.loadingAttempted&&textureCalls==2,"required OFF preserves original optional loading"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());
 ebld_prepare_loading();
 if(!ck(g_ebldPrepare.loadingAttempted&&textureCalls==2&&!g_ebldPrepare.requiredAttempted,"shot loading preserves original nondeferred policy"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());g_objects[0].id=42;g_ebldBld[42].stage=EB_DAMAGED;
 ebld_prepare_required_loading();
 if(!ck(g_ebldPrepare.requiredReady&&g_pack.tex[1].gl&&!g_pack.tex[0].gl,"required damaged stage never speculates intact atlas"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());g_objects[0].id=42;g_ebldBld[42].stage=EB_DAMAGED;
 g_objects.push_back(SimObject());
 ebld_prepare_required_loading();
 if(!ck(g_ebldPrepare.requiredReady&&textureCalls==2,"current damaged and intact instances union exactly"))return 1;
 for(int exclusion=0;exclusion<5;exclusion++){
     reset();model("NUKE");g_objects.push_back(SimObject());
     if(exclusion==0)g_objects[0].shown=false;
     if(exclusion==1)g_objects[0].limbo=true;
     if(exclusion==2)g_objects[0].constructing=true;
     if(exclusion==3)g_objects[0].construction=0.5f;
     if(exclusion==4)g_objects[0].authored=0;
     ebld_prepare_required_loading();
     if(!ck(g_ebldPrepare.requiredReady&&textureCalls==0,"invisible limbo construction and authored overrides excluded"))return 1;
 }
 reset();model("T01");g_objects.push_back(SimObject());strcpy(g_objects[0].type,"T01");g_objects[0].kind=K_TERRAIN;g_fx.tree3d=false;
 ebld_prepare_required_loading();
 if(!ck(g_ebldPrepare.requiredReady&&textureCalls==0,"disabled remastered trees excluded"))return 1;
 for(int treeCase=0;treeCase<5;treeCase++){
     reset();model("T01");model("T01_SNAG");g_ebld[0].nset=g_ebld[1].nset=1;
     g_ebld[0].stageModel[2]=1;g_ebld[1].stageOf=0;
     g_objects.push_back(SimObject());strcpy(g_objects[0].type,"T01");g_objects[0].kind=K_TERRAIN;g_objects[0].str=0;g_objects[0].id=42;
     if(treeCase==1)g_ebldBld[42].treeStage=0;
     if(treeCase==2)g_ebld[0].stageModel[2]=-1;
     if(treeCase==3)g_ebld[1].attached=false;
     if(treeCase==4){g_objects[0].str=100;g_ebldBld[42].treeStage=1;g_ebldBld[42].model=1;}
     ebld_prepare_required_loading();
     const bool hasSnag=treeCase!=2&&treeCase!=3;
     if(!ck(g_ebldPrepare.requiredReady&&!g_pack.tex[0].gl&&(g_pack.tex[2].gl!=0)==hasSnag,
            "dead no-entry stage-zero missing-invalid snag and current burning selector match draw"))return 1;
 }
 reset();model("NUKE");g_ebldBld[42].stage=EB_WRECKED;
 ebld_prepare_required_loading();
 if(!ck(g_ebldPrepare.requiredReady&&g_pack.tex[1].gl&&!g_pack.tex[0].gl,"persisted visible wreck requires its stage"))return 1;
 reset();model("NUKE");g_ebldBld[42].stage=EB_WRECKED;g_ebldBld[42].shown=false;
 ebld_prepare_required_loading();
 if(!ck(g_ebldPrepare.requiredReady&&textureCalls==0,"shrouded wreck excluded"))return 1;
 reset();model("T01_SNAG");g_ebldBld[42].orphan=true;g_ebldBld[42].stage=EB_INTACT;
 ebld_prepare_required_loading();
 if(!ck(g_ebldPrepare.requiredReady&&g_pack.tex[0].gl,"orphan snag preserves its visible residency requirement"))return 1;
 reset();model("NUKE");model("WALL");g_ebld[1].texOwner=0;g_ebldBld[42].model=1;g_ebldBld[42].stage=EB_WRECKED;
 ebld_prepare_required_loading();
 if(!ck(g_ebldPrepare.requiredReady&&textureCalls==2&&g_pack.tex[0].gl&&g_pack.tex[1].gl&&!g_pack.tex[2].gl,"shared-owner fallback requires original owner textures once"))return 1;
 reset();model("SBAG");g_ebld[0].flags=EBX_WALL;g_walls.push_back(WallCell());g_walls.push_back(WallCell());
 ebld_prepare_required_loading();
 if(!ck(g_ebldPrepare.requiredReady&&textureCalls==2&&g_pack.tex[0].gl&&g_pack.tex[1].gl,"visible intact wall textures prepared once for repeated cells"))return 1;
 for(int wallCase=0;wallCase<8;wallCase++){
     reset();model("SBAG");g_ebld[0].flags=EBX_WALL;g_walls.push_back(WallCell());
     if(wallCase==0)g_wallsDraw=false;
     if(wallCase==1)g_walls[0].kind=5;
     if(wallCase==2)g_walls[0].icon=16;
     if(wallCase==3)outsideCells.insert({0,0});
     if(wallCase==4)hiddenCells.insert({0,0});
     if(wallCase==5)g_ebld[0].attached=false;
     if(wallCase==6)g_ebld[0].flags=0;
     if(wallCase==7)g_ebld[0].wallMask=0;
     ebld_prepare_required_loading();
     if(!ck(g_ebldPrepare.requiredReady&&textureCalls==0,"disabled invalid hidden missing and variant-only walls excluded"))return 1;
 }
 for(int stage=EB_HELD;stage<=EB_WRECKED;stage++){
     reset();model("SBAG");model("SBAG");g_ebld[0].flags=g_ebld[1].flags=EBX_WALL;
     g_ebld[1].wallMask=3;g_ebld[1].texOwner=0;g_walls.push_back(WallCell());
     EbldBld& entry=g_ebldBld[ebld_wall_key(0,0)];entry.model=1;entry.stage=stage;
     ebld_prepare_required_loading();
     if(!ck(g_ebldPrepare.requiredReady&&textureCalls==2&&g_pack.tex[0].gl&&g_pack.tex[1].gl&&!g_pack.tex[2].gl,
            "held damaged wreck wall uses current shared owner without variant allocation"))return 1;
 }
 reset();model("SBAG");model("SBAG");g_ebld[0].flags=g_ebld[1].flags=EBX_WALL;g_walls.push_back(WallCell());
 ebld_prepare_required_loading();
 if(!ck(g_ebldPrepare.requiredReady&&textureCalls==2&&!g_pack.tex[0].gl&&g_pack.tex[2].gl,
        "wall selector matches last usable whole model without changing resolver cache"))return 1;
 for(int clumpCase=0;clumpCase<8;clumpCase++){
     reset();model("T01");model("T02");model("T07");
     for(EbldModel& tree:g_ebld)tree.nset=1;
     g_objects.push_back(SimObject());g_objects[0].kind=K_TERRAIN;g_objects[0].cartMesh=-1;strcpy(g_objects[0].type,"TC02");
     if(clumpCase==1)g_clumpTrees=false;
     if(clumpCase==2)g_fx.tree3d=false;
     if(clumpCase==3)g_objects[0].shown=false;
     if(clumpCase==4){hiddenCells.insert({1,0});outsideCells.insert({0,1});}
     if(clumpCase==5)g_objects[0].cartMesh=0;
     if(clumpCase==6)g_ebld[2].attached=false;
     if(clumpCase==7)strcpy(g_objects[0].type,"TC99");
     ebld_prepare_required_loading();
     const unsigned expected=clumpCase==0?3:clumpCase==4?1:0;
     if(!ck(g_ebldPrepare.requiredReady==(clumpCase!=6)&&textureCalls==expected&&g_objects.size()==1
            &&!strcmp(g_objects[0].type,clumpCase==7?"TC99":"TC02")&&g_objects[0].cx==0&&g_objects[0].cy==0,
            "clump cells use finite existing source identities and visibility"))return 1;
 }
 reset();if(!lazyClump())return 2;
 const int clumpFrame=g_engineFrame,clumpTicks=g_simTicks;
 ebld_prepare_required_loading();
 if(!ck(g_ebldPrepare.requiredReady&&g_ebldPrepare.requiredModelsPlanned==3&&g_ebldPrepare.requiredModelsCompleted==3
        &&textureCalls==3&&ensuredTypes==std::vector<std::string>({"T07","T02","T01"})
        &&!g_ebldFiles[2].tried&&g_engineFrame==clumpFrame&&g_simTicks==clumpTicks,
        "clump-only species attach in actual part order without unrelated species or clock advancement"))return 1;
 if(!ck(g_ebldPrepare.requiredModelCpuReserved>0&&g_ebldPrepare.requiredCpuCharged==g_ebldPrepare.cpu
        &&g_ebldPrepare.cpu<=EBLD_PREPARE_CPU,"bounded model CPU reservation shares the original optional ledger"))return 1;
 ebld_prepare_required_loading();
 if(!ck(ensuredTypes.size()==3&&textureCalls==3,"required clump attachment remains once per generation"))return 1;
 for(int failure=1;failure<=5;failure++){
     reset();model("NUKE");if(!lazyClump())return 2;ensureFailure=failure;
     ebld_prepare_required_loading();
     bool allUntried=true;for(const EbldFile& file:g_ebldFiles)allUntried=allUntried&&!file.tried;
     if(!ck(!g_ebldPrepare.requiredReady&&g_ebldPrepare.requiredModelsCompleted==0&&textureCalls==0
            &&g_ebld.size()==1&&g_ebld[0].attached&&g_pack.tex.size()==2&&g_pack.mesh.empty()
            &&g_ebldPackSerial==7&&allUntried&&!strcmp(g_ebldPrepare.requiredReason,"model-load-refusal"),
            "failed model read and every attachment allocation stage restore publication flags serial and vectors"))return 1;
     ensureFailure=0;ebld_prepare_reset();ebld_prepare_required_loading();
     if(!ck(g_ebldPrepare.requiredReady&&g_ebldPrepare.requiredModelsCompleted==3&&textureCalls==3,
            "rolled back clump family can retry through unchanged authoritative loader"))return 1;
 }
 for(int exclusion=0;exclusion<8;exclusion++){
     reset();if(!lazyClump())return 2;
     if(exclusion==0)g_clumpTrees=false;
     if(exclusion==1)g_fx.tree3d=false;
     if(exclusion==2)g_objects[0].shown=false;
     if(exclusion==3)g_objects[0].cartMesh=0;
     if(exclusion==4)g_raModule=true;
     if(exclusion==5)g_fx.dos_classic=true;
     if(exclusion==6){hiddenCells.insert({1,0});hiddenCells.insert({0,1});hiddenCells.insert({1,1});}
     if(exclusion==7)requiredModels=false;
     ebld_prepare_required_loading();
     if(!ck(ensuredTypes.empty()&&textureCalls==0&&g_ebld.empty(),"off hidden native-mesh Classic RA and model-refusal paths do not attach clump species"))return 1;
     if(exclusion==7&&!ck(!g_ebldPrepare.requiredReady&&!strcmp(g_ebldPrepare.requiredReason,"model-disabled"),
                         "forced model preparation refusal reports incomplete readiness"))return 1;
 }
 for(int sourceCase=0;sourceCase<6;sourceCase++){
     reset();if(!lazyClump())return 2;
     if(sourceCase==0)g_ebldFiles[3].path="missing-clump-source.ebld";
     else if(sourceCase==1){FILE* source=fopen("T07.ebld","wb");fwrite("short",1,5,source);fclose(source);}
     else if(sourceCase==2){FILE* source=fopen("T07.ebld","r+b");fseek(source,4*1024*1024,SEEK_SET);fputc(0,source);fclose(source);}
     else if(sourceCase==3){FILE* source=fopen("T07.ebld","r+b");const uint32_t sets=3;fseek(source,20,SEEK_SET);fwrite(&sets,4,1,source);fclose(source);}
     else {FILE* source=fopen(sourceCase==4?"T07_BURN.ebld":"T07_SNAG.ebld","wb");fwrite("short",1,5,source);fclose(source);}
     ebld_prepare_required_loading();
     if(!ck(!g_ebldPrepare.requiredReady&&ensuredTypes.empty()&&!g_ebldFiles[3].tried
            &&!strcmp(g_ebldPrepare.requiredReason,"model-source-refusal"),"missing truncated oversized non-intact and malformed companion sources refuse before loader mutation"))return 1;
     if(sourceCase>=4)remove(sourceCase==4?"T07_BURN.ebld":"T07_SNAG.ebld");
 }
 reset();if(!lazyClump())return 2;quitRequested=true;ebld_prepare_required_loading();
 if(!ck(!g_ebldPrepare.requiredReady&&ensuredTypes.empty()&&!strcmp(g_ebldPrepare.requiredReason,"quit"),
        "quit refuses clump preparation before loader mutation"))return 1;
 reset();if(!lazyClump())return 2;g_pack.type.erase("T07");ebld_prepare_required_loading();
 if(!ck(!g_ebldPrepare.requiredReady&&ensuredTypes.empty()&&!strcmp(g_ebldPrepare.requiredReason,"model-cartridge-unavailable"),
        "absent cartridge type refuses clump preparation before mutation"))return 1;
 reset();if(!lazyClump())return 2;ebld_prepare_required_loading();const uint64_t withoutShadows=g_ebldPrepare.cpu;
 reset();if(!lazyClump())return 2;g_pack.mesh.resize(1);g_pack.mesh[0].tris.resize(20);
 for(size_t triangle=0;triangle<10;triangle++)g_pack.mesh[0].tris[triangle].mode=MODE_SHADOW;
 for(auto& type:g_pack.type)type.second.mesh=0;
 ebld_prepare_required_loading();
 if(!ck(g_ebldPrepare.requiredReady&&g_ebldPrepare.cpu>=withoutShadows+30*12*(sizeof(PackTri)+9*sizeof(float)+1),
        "source-independent cartridge shadows reserve family copies before mutation"))return 1;
#ifndef _WIN32
 reset();if(!lazyClump())return 2;g_ebldPrepare.cpu=EBLD_PREPARE_CPU;ebld_prepare_required_loading();
 if(!ck(!g_ebldPrepare.requiredReady&&ensuredTypes.empty()&&!strcmp(g_ebldPrepare.requiredReason,"model-platform-budget"),
        "non-Windows clump admission retains remaining original CPU cap"))return 1;
#endif
 reset();model("NUKE");g_objects.push_back(SimObject());g_raModule=true;
 ebld_prepare_required_loading();
 if(!ck(ebld_prepare_on()&&!g_ebldPrepare.requiredAttempted&&textureCalls==0&&modelLookups==0,
        "required preparation stays TD-only without catalog lookups despite enabled optional RA support"))return 1;
 ebld_prepare_observe();
 if(!ck(modelLookups==1&&g_ebldPrepare.models.size()==1,"merged optional RA observation resolves only present building type"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());ebld_prepare_required_loading();ebld_prepare_observe();
 if(!ck(g_ebldPrepare.requiredReady&&modelLookups==0,"TD required and optional census never invokes lazy model resolver"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());g_objects[0].limbo=true;g_objects.push_back(SimObject());g_objects[1].str=0;g_raModule=true;
 ebld_prepare_required_loading();ebld_prepare_observe();
 if(!ck(!g_ebldPrepare.requiredAttempted&&modelLookups==0&&g_ebldPrepare.models.empty(),"RA hidden or dead buildings never invoke optional model resolution"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());
 g_ebld[0].tex.resize(13);g_ebld[0].texIntact.resize(13,1);g_ebld[0].stageMasks[0]=(uint64_t(1)<<13)-1;g_pack.tex.resize(13);
 for(size_t texture=0;texture<13;texture++){g_ebld[0].tex[texture].w=g_ebld[0].tex[texture].h=2048;g_pack.tex[texture].w=g_pack.tex[texture].h=2048;}
 ebld_prepare_required_loading();
#ifndef _WIN32
 if(!ck(!g_ebldPrepare.requiredReady&&textureCalls==0&&!strcmp(g_ebldPrepare.requiredReason,"platform-budget"),
        "without native memory admission required work retains original platform caps"))return 1;
 ebld_prepare_observe();
 if(!ck(ebld_prepare_step(true)&&textureCalls==1&&g_ebldPrepare.gpu<=EBLD_PREPARE_GPU,
        "platform-budget refusal retains bounded optional and demand fallback"))return 1;
#else
 if(!ck(g_ebldPrepare.requiredReady&&textureCalls==13&&g_ebldPrepare.gpu>EBLD_PREPARE_GPU
        &&g_ebldPrepare.gpu==g_ebldPrepare.requiredGpuCharged,"finite mandatory working set can exceed speculative cap without resetting it"))return 1;
 model("PYLE");SimObject queued;strcpy(queued.type,"PYLE");g_objects.push_back(queued);g_engineFrame++;
 ebld_prepare_observe();g_ebld[0].cutBuilt=g_ebld[1].cutBuilt=true;
 if(!ck(!ebld_prepare_step(true)&&textureCalls==13&&ebld_prepare_remaining(EBLD_PREPARE_GPU,g_ebldPrepare.gpu)==0,"required charges leave no fresh speculative GPU allowance"))return 1;
#endif
 reset();model("NUKE");g_objects.push_back(SimObject());g_ebld[0].tex[0].house=3;
 ebld_prepare_required_loading();
 if(!ck(g_ebldPrepare.requiredReady&&g_ebldPrepare.requiredCpuCharged==128u*128u*5u
        &&g_ebldPrepare.cpu==g_ebldPrepare.requiredCpuCharged,"required CPU ledger charges actual retained capacity"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());g_ebld[0].tex[0].w=g_ebld[0].tex[0].h=8192;
 ebld_prepare_required_loading();
 if(!ck(!g_ebldPrepare.requiredReady&&textureCalls==0&&!strcmp(g_ebldPrepare.requiredReason,"transient-limit"),"required preflight rejects oversized transient without upload"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());g_ebld[0].tex[0].w=-1;
 ebld_prepare_required_loading();
 if(!ck(!g_ebldPrepare.requiredReady&&textureCalls==0&&!strcmp(g_ebldPrepare.requiredReason,"invalid-size"),"invalid required dimensions fail closed"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());g_ebld[0].tex.resize(8260);g_ebld[0].texIntact.resize(8260,1);g_pack.tex.resize(8260);
 ebld_prepare_required_loading();
 if(!ck(!g_ebldPrepare.requiredReady&&textureCalls==0&&!strcmp(g_ebldPrepare.requiredReason,"job-limit"),"finite job cap checked before any upload"))return 1;
 reset();model("NUKE");g_objects.push_back(SimObject());quitRequested=true;
 ebld_prepare_required_loading();
 if(!ck(!g_ebldPrepare.requiredReady&&textureCalls==0&&finished==0&&!strcmp(g_ebldPrepare.requiredReason,"quit"),"quit never publishes ready"))return 1;
 for(int refusal=0;refusal<3;refusal++){
     reset();model("NUKE");g_objects.push_back(SimObject());throwTexture=refusal==0;refuseTexture=refusal==1;failRead=refusal==2;
     ebld_prepare_required_loading();
     if(!ck(!g_ebldPrepare.requiredReady&&g_pack.tex[0].gl==0&&g_ebld[0].attached&&bound==7
            &&g_ebldPrepare.requiredCompleted==0,"required allocation driver and I/O failures roll back"))return 1;
     throwTexture=refuseTexture=failRead=false;
     if(!ck(ebld_resident(g_ebld[0],true,1,0),"demand fallback remains available after required refusal"))return 1;
 }
 reset();model("NUKE");g_objects.push_back(SimObject());ebld_prepare_required_loading();
 g_ebldPrepareGeneration++;ebld_prepare_reset();
 if(!ck(!g_ebldPrepare.requiredAttempted&&!g_ebldPrepare.requiredReady&&g_ebldPrepare.requiredGpuCharged==0,"mission reset clears required state and ledger"))return 1;
 uint64_t overflow=~uint64_t(0)-1;
 if(!ck(!ebld_prepare_add_checked(overflow,2)&&overflow==~uint64_t(0)-1,"checked accounting rejects overflow without mutation"))return 1;
#ifdef TEST_WINDOWS_HEADROOM
 for(int refusal=0;refusal<3;refusal++){
     reset();if(!lazyClump())return 2;
     availableVirtual=refusal==0?256u<<20:8ull<<30;
     availablePhysical=refusal==1?256u<<20:8ull<<30;memoryQuery=refusal!=2;
     ebld_prepare_required_loading();
     if(!ck(!g_ebldPrepare.requiredReady&&ensuredTypes.empty()&&g_ebld.empty()
            &&!strcmp(g_ebldPrepare.requiredReason,"model-headroom"),"clump aggregate physical virtual and query failures refuse before mutation"))return 1;
 }
 availableVirtual=availablePhysical=8ull<<30;memoryQuery=true;
 reset();model("NUKE");g_objects.push_back(SimObject());availableVirtual=256u<<20;
 ebld_prepare_required_loading();
 if(!ck(!g_ebldPrepare.requiredReady&&textureCalls==0&&!strcmp(g_ebldPrepare.requiredReason,"headroom"),"required aggregate virtual admission preserves reserve"))return 1;
 availableVirtual=8ull<<30;
 reset();model("NUKE");g_objects.push_back(SimObject());availablePhysical=256u<<20;
 ebld_prepare_required_loading();
 if(!ck(!g_ebldPrepare.requiredReady&&textureCalls==0&&!strcmp(g_ebldPrepare.requiredReason,"headroom"),"required aggregate physical admission preserves reserve"))return 1;
 availablePhysical=8ull<<30;
 reset();model("NUKE");g_objects.push_back(SimObject());memoryQuery=false;
 ebld_prepare_required_loading();
 if(!ck(!g_ebldPrepare.requiredReady&&textureCalls==0,"failed required headroom query defers to demand"))return 1;
 memoryQuery=true;
 EbldPrepareCost impossible;impossible.cpu=~uint64_t(0);
 if(!ck(!ebld_prepare_headroom(impossible),"headroom sum cannot wrap"))return 1;
#endif
 printf("PREPARATION|checks=%u|failures=0|actual_header=1|GL_SDL_are_stubs=1|no_UI=1\n",checks);return 0;
}
