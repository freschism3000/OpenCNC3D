/* Read-only frontend adapter: no worker reads mutable world or OpenGL state. */
#ifndef OPENCNC3D_DIAGNOSTIC_RENDERER_H
#define OPENCNC3D_DIAGNOSTIC_RENDERER_H
#include "diagnostic_session.h"
static game_diag::Frame g_diagFrame;
static Uint64 g_diagPrevious[game_diag::WORK_COUNT]={};
static bool g_diagSchemaPasses[game_diag::PASS_COUNT]={};
static size_t g_diagSchemaExtra=0;
static unsigned g_diagMission=0;
static Uint64 g_diagLastWorld=0,g_diagCountAt=0;
static Uint64 g_diagWorldCounts[8]={};
static PerfTraceRow g_diagDisplay;
static int g_diagLastSpeed=-1;
struct DiagModelStage { int id=0,kind=0,stage=-999,model=-1; bool used=false; };
static DiagModelStage g_diagModels[2048];
static void diag_counter_values(Uint64* v) {
 v[0]=g_meshBatchDraws;v[1]=g_meshImmVerts;v[2]=Uint64(g_meshBatchUploads)+g_meshGeometryUploads;
 v[3]=Uint64(g_meshBatchHits)+g_meshFrameHits+g_meshGeometryHits+g_meshSettledHits;
 v[4]=g_meshBatchMisses+g_meshGeometryBuilds;v[5]=g_meshBatchEvictions;v[6]=g_h6UploadBytes;
 v[7]=g_destructTrace[3]+g_destructTrace[7]+g_destructTrace[11]+g_destructTrace[15];
}
static void diag_begin_frame() {g_diagFrame=game_diag::Frame();diag_counter_values(g_diagPrevious);}
static void diag_section(const char* name,size_t k) {
 if(k>=game_diag::PASS_COUNT)return;
 Uint64 current[game_diag::WORK_COUNT];diag_counter_values(current);
 for(size_t i=0;i<game_diag::WORK_COUNT;i++){g_diagFrame.work[k][i]+=current[i]>=g_diagPrevious[i]?current[i]-g_diagPrevious[i]:current[i];g_diagPrevious[i]=current[i];}
 if(!g_diagSchemaPasses[k]){game_diag::Packet p;p.kind=game_diag::SCHEMA;p.a=0;p.b=(int)k;snprintf(p.text,sizeof p.text,"%s",name);if(game_diag::enqueue(p))g_diagSchemaPasses[k]=true;}
}
static void diag_graphics() {
 game_diag::Packet p;p.kind=game_diag::GLINFO;
 snprintf(p.text,sizeof p.text,"renderer=%s; vendor=%s; version=%s; GPU timestamps=%s; build=%s; pointer_bits=%zu",glGetString(GL_RENDERER)?(const char*)glGetString(GL_RENDERER):"unavailable",glGetString(GL_VENDOR)?(const char*)glGetString(GL_VENDOR):"unavailable",glGetString(GL_VERSION)?(const char*)glGetString(GL_VERSION):"unavailable",g_perfTrace.gpuState==1?"asynchronous":"unavailable",OPENCNC3D_BUILD_MARK,8*sizeof(void*));game_diag::enqueue(p);
 SDL_version version={};SDL_GetVersion(&version);snprintf(p.text,sizeof p.text,"SDL=%u.%u.%u; video=%s; platform=%s",version.major,version.minor,version.patch,SDL_GetCurrentVideoDriver()?SDL_GetCurrentVideoDriver():"unavailable",SDL_GetPlatform());game_diag::enqueue(p);
}
static void diag_begin_session(SDL_Window*,unsigned mission) {
 g_diagMission=mission;g_diagLastWorld=0;g_diagCountAt=0;g_diagDisplay=PerfTraceRow();g_diagDisplay.w=-1;g_diagLastSpeed=-1;memset(g_diagModels,0,sizeof g_diagModels);
 game_diag::phase(5,(int)mission);game_diag::configuration();diag_graphics();
 game_diag::Packet p;p.kind=game_diag::SCENE;p.clock=game_diag::now();p.a=fx_mode_of(&g_fx);p.b=(int)mission;p.frame.row.w=g_mapW;p.frame.row.h=g_mapH;snprintf(p.text,sizeof p.text,"%s",game_diag::scene_id(g_scenLabel).c_str());game_diag::enqueue(p);
}
static const char* diag_model_type(const char* type) {
 static const char* const names[]={"FACT","POWR","APWR","PROC","PYLE","HAND","WEAP","AFLD","HPAD","GTWR","ATWR","GUN","SAM","OBLI","NUKE","NUK2","HQ","EYE","TMPL","SILO","FIX","HOSP","BIO","ARCO","MISS"};
 for(const char* n:names)if(!strcmp(n,type))return n;return "other_model";
}
static void diag_world_changes(const PerfTraceRow& r) {
 /* At most ten polls/second. Fixed table; no model/world mutation or heap map. */
 if(g_diagLastWorld&&r.begin<g_diagLastWorld+g_perfTrace.freq/10)return;g_diagLastWorld=r.begin;
 for(const auto& pair:g_ebldBld) {
  const auto& e=pair.second;size_t slot=((unsigned)e.id*2654435761u+(unsigned)e.kind)%2048u;DiagModelStage* entry=NULL;
  for(size_t i=0;i<2048;i++){auto& n=g_diagModels[(slot+i)%2048];if(!n.used||(n.id==e.id&&n.kind==e.kind)){entry=&n;break;}}
  if(!entry){game_diag::state->modelMarkersDropped++;continue;}
  if(entry->used&&entry->stage==e.stage&&entry->model==e.model)continue;
  game_diag::Packet p;p.kind=game_diag::MODEL;p.clock=r.begin;p.a=e.stage;p.b=(int)g_diagMission;p.frame.row.engineFrame=r.engineFrame;p.frame.row.tickBefore=e.kind;p.frame.row.tickAfter=e.model;
  snprintf(p.text,sizeof p.text,"%s",diag_model_type(e.type));
  if(game_diag::enqueue(p)){entry->used=true;entry->id=e.id;entry->kind=e.kind;entry->stage=e.stage;entry->model=e.model;}
 }
}
static void diag_frame(const PerfTraceRow& row,unsigned mission) {
 g_diagFrame.row=row;g_diagFrame.mission=mission;
 Uint64* g=g_diagFrame.gauge;g[0]=g_meshBatchCache.size();g[1]=g_meshBatchBytes;g[2]=g_meshFrameCache.size();g[3]=g_meshFrameBytes;g[4]=g_meshGeometryKeys.size();g[5]=g_meshGeometryBytes;g[6]=g_meshSettled.size();g[7]=g_meshSettledBytes;g[8]=g_meshFrameStores.size();g[9]=g_meshFrameFallbacks;g[10]=g_meshFrameTrimmed;g[11]=game_speed_setting();
 if(!g_diagCountAt||row.begin>=g_diagCountAt+g_perfTrace.freq){
  memset(g_diagWorldCounts,0,sizeof g_diagWorldCounts);for(const auto& o:g_objects)if(o.kind>=0&&o.kind<6)g_diagWorldCounts[o.kind]++;
  g_diagWorldCounts[6]=g_ebldBld.size();for(const auto& item:g_ebldBld)if(item.second.kind==K_TERRAIN&&item.second.treeStage==1)g_diagWorldCounts[7]++;
  g_diagCountAt=row.begin;
 }
 for(size_t i=0;i<8;i++)g[12+i]=g_diagWorldCounts[i];
 g[20]=g_meshBatchAdmissionRefused;
 if(row.w!=g_diagDisplay.w||row.h!=g_diagDisplay.h||row.windowFlags!=g_diagDisplay.windowFlags||row.display!=g_diagDisplay.display||row.hz!=g_diagDisplay.hz||row.requested!=g_diagDisplay.requested||row.returned!=g_diagDisplay.returned){
  game_diag::Packet p;p.kind=game_diag::DISPLAY;p.clock=row.begin;p.frame.row=row;p.frame.mission=mission;if(game_diag::enqueue(p))g_diagDisplay=row;
 }
 game_diag::configuration();diag_world_changes(row);
 if(g_diagLastSpeed!=(int)g[11]){game_diag::phase(6,(int)g[11]);g_diagLastSpeed=(int)g[11];game_diag::phase(5,(int)mission);}
 if(g_diagSchemaExtra<g_perfTrace.extraCount){std::string names(g_perfTrace.columns);size_t start=0,index=0;while(start<=names.size()){size_t end=names.find(',',start);if(end==std::string::npos)end=names.size();game_diag::Packet p;p.kind=game_diag::SCHEMA;p.a=1;p.b=(int)index;snprintf(p.text,sizeof p.text,"%s",names.substr(start,end-start).c_str());if(!game_diag::enqueue(p))break;g_diagSchemaExtra=++index;if(end==names.size())break;start=end+1;}}
 game_diag::Packet p;p.kind=game_diag::FRAME;p.clock=row.end;p.frame=g_diagFrame;game_diag::enqueue(p);
}
static void diag_gpu(const PerfTraceRow& row,unsigned mission){game_diag::Packet p;p.kind=game_diag::GPU;p.clock=game_diag::now();p.frame.row=row;p.frame.mission=mission;game_diag::enqueue(p);}
static void diag_end_session(unsigned){game_diag::configuration();game_diag::phase(2);}
extern "C" int game_diagnostics_begin() {
 int location=1;
 if(!game_diag::start("Logs")){const std::string fallback=game_diag::native_fallback_logs();if(fallback.empty()||!game_diag::start(fallback.c_str()))return 0;location=2;}
 g_perfTraceHooks.enabled=game_diag::active;g_perfTraceHooks.beginSession=diag_begin_session;g_perfTraceHooks.beginFrame=diag_begin_frame;g_perfTraceHooks.section=diag_section;g_perfTraceHooks.frame=diag_frame;g_perfTraceHooks.gpu=diag_gpu;g_perfTraceHooks.endSession=diag_end_session;
 game_diag::Packet p;p.kind=game_diag::GLINFO;snprintf(p.text,sizeof p.text,"diagnostic_build=%s; log_location=%s",OPENCNC3D_BUILD_MARK,location==1?"installation_Logs":"LocalAppData_OpenCNC3D_Logs");game_diag::enqueue(p);
 return location;
}
extern "C" void game_diagnostics_configuration(){game_diag::configuration();}
extern "C" void game_diagnostics_graphics(){if(game_diag::active()){diag_graphics();game_diag::configuration();}}
extern "C" void game_diagnostics_end(){if(game_diag::active()){game_diag::phase(8);game_diag::configuration();}game_diag::finish();}
extern "C" void game_diagnostics_phase(int phase,int code){game_diag::phase(phase,code);}
extern "C" int game_diagnostics_current_phase(){return game_diag::active()?(int)game_diag::state->producerPhase:0;}
#endif
