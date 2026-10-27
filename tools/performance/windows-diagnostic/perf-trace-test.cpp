#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <map>
#include <vector>
#include <cassert>
#include <cmath>
#include <unistd.h>
#include <sys/stat.h>
#define APIENTRY
using Uint64=uint64_t;using Uint32=uint32_t;using GLuint=unsigned;using GLenum=unsigned;using GLint=int;using GLsizei=int;
static const GLenum GL_EXTENSIONS=1,GL_VERSION=2,GL_RENDERER=3,GL_VENDOR=4;
struct SDL_Window{};struct SDL_DisplayMode{int refresh_rate=60;};struct SDL_version{unsigned char major=2,minor=32,patch=8;};
static Uint64 mockClock=1000000;static bool mockGpu=false,mockReady=false;static GLuint mockNextQuery=1;
static int mockGpuDeletes=0;
struct MockQuery{unsigned long long stamp=0;};static std::map<GLuint,MockQuery> mockQueries;
static Uint64 SDL_GetPerformanceCounter(){return mockClock;}
static Uint64 SDL_GetPerformanceFrequency(){return 1000000;}
static int SDL_GetWindowDisplayIndex(SDL_Window*){return 0;}
static int SDL_GetCurrentDisplayMode(int,SDL_DisplayMode* m){m->refresh_rate=60;return 0;}
static Uint32 SDL_GetWindowFlags(SDL_Window*){return 4;}
static void SDL_GetVersion(SDL_version* v){*v=SDL_version();}
static const char* SDL_GetCurrentVideoDriver(){return "mock";}
static const char* SDL_GetPlatform(){return "Stub";}
static const unsigned char* glGetString(GLenum e){return (const unsigned char*)(e==GL_EXTENSIONS?(mockGpu?"GL_ARB_timer_query":""):(e==GL_VERSION?"2.1 mock":"mock"));}
static void mock_gen(GLsizei n,GLuint* q){for(int i=0;i<n;i++){q[i]=mockNextQuery++;mockQueries[q[i]]=MockQuery();}}
static void mock_delete(GLsizei n,const GLuint* q){for(int i=0;i<n;i++){mockQueries.erase(q[i]);mockGpuDeletes++;}}
static void mock_stamp(GLuint q,GLenum){mockQueries.at(q).stamp=mockClock*1000;}
static void mock_available(GLuint,GLenum,GLint* v){*v=mockReady?1:0;}
static void mock_info(GLenum,GLenum,GLint* v){*v=64;}
static void mock_result(GLuint q,GLenum,unsigned long long* v){*v=mockQueries.at(q).stamp;}
static void* SDL_GL_GetProcAddress(const char* name){
 if(!strcmp(name,"glGenQueries"))return reinterpret_cast<void*>(&mock_gen);
 if(!strcmp(name,"glDeleteQueries"))return reinterpret_cast<void*>(&mock_delete);
 if(!strcmp(name,"glQueryCounter"))return reinterpret_cast<void*>(&mock_stamp);
 if(!strcmp(name,"glGetQueryObjectiv"))return reinterpret_cast<void*>(&mock_available);
 if(!strcmp(name,"glGetQueryiv"))return reinterpret_cast<void*>(&mock_info);
 if(!strcmp(name,"glGetQueryObjectui64v"))return reinterpret_cast<void*>(&mock_result);
 return NULL;
}
struct MockFx {int tree3d=1,tib3d=1,water_fx=1,bilinear=1;};static MockFx g_fx;
static int fx_mode_of(MockFx*){return 2;}static int terrain_texset_drawn(){return 3;}static int fx_infset_get(){return 1;}
static int fx_save(MockFx*,const char* p){FILE* f=fopen(p,"wb");if(!f)return 0;fputs("# stub configuration\n",f);fclose(f);return 1;}
#define OPENCNC3D_BUILD_MARK "test-reviewed-header"
static const char* g_scenLabel="SCG08EA";struct MockPack{char scen[16]="SCG08EA",theater[16]="DESERT";};static MockPack g_pack;
static int g_mapW=64,g_mapH=64,g_engineFrame=0,g_simTicks=0,g_shroudOn=1;
static float g_camX=45,g_camZ=45,g_dist=3000,g_zoom=1,g_tickAlpha=0;
#ifndef PERF_TRACE_TEST_HEADER
#define PERF_TRACE_TEST_HEADER "../../../game/perf_trace.h"
#endif
#include PERF_TRACE_TEST_HEADER
static SDL_Window window;
static void advance(Uint64 ticks){mockClock+=ticks;}
static void frame(int ticks,bool held=false){
 perf_trace_begin_frame(ticks);
 assert(perf_trace_frame_active()==perf_trace_enabled());
 advance(1000);perf_trace_events_end();
 advance(500);perf_trace_tick_begin();
 Uint64 a=perf_trace_now();advance(200);perf_trace_add_sim(a);
 Uint64 b=perf_trace_now();advance(300);perf_trace_add_refresh(b);
 advance(400);perf_trace_tick_end();
 advance(600);perf_trace_section("events + tick + refresh");perf_trace_draw_begin();
 advance(1000);perf_trace_section("first draw pass");
 Uint64 extra[2]={7,~(Uint64)0};perf_trace_extra("available,missing",extra,2);
 advance(1000);perf_trace_draw_end();
 advance(200);perf_trace_section("editor + options");perf_trace_swap_begin();
 advance(3000);perf_trace_swap_end();perf_trace_section("swap");
 advance(100);perf_trace_end_frame(&window,1280,720,ticks+1,false,held,held,12);
}
static bool exists(const std::string& p){struct stat s;return stat(p.c_str(),&s)==0;}
static std::string read(const std::string& p){FILE* f=fopen(p.c_str(),"rb");assert(f);std::string s;char b[4096];size_t n;while((n=fread(b,1,sizeof b,f)))s.append(b,n);fclose(f);return s;}
int main(int argc,char** argv){
 assert(argc==3);std::string mode=argv[1],out=argv[2];
 assert(PERFTRACE_CAPACITY*sizeof(PerfTraceRow)<=16u*1024u*1024u);
 if(mode=="off") {
  unsetenv("OPENCNC3D_PERFTRACE");perf_trace_begin_session(&window);assert(!perf_trace_enabled());
  frame(0);perf_trace_end_session();assert(g_perfTrace.rows.empty()&&g_perfTrace.rows.capacity()==0);assert(!exists(out+"/frames-mission-001.csv"));
 } else if(mode=="invalid") {
  setenv("OPENCNC3D_PERFTRACE",(out+"/absent/frames.csv").c_str(),1);perf_trace_begin_session(&window);
  assert(!perf_trace_enabled()&&g_perfTrace.rows.empty());frame(0);assert(!perf_trace_enabled());
 } else if(mode=="sessions") {
  const std::string keep=out+"/frames-mission-001.csv";FILE* f=fopen(keep.c_str(),"wb");assert(f);fputs("keep me",f);fclose(f);
  setenv("OPENCNC3D_PERFTRACE",(out+"/frames.csv").c_str(),1);perf_trace_swap_state(-1,1);perf_trace_begin_session(&window);assert(perf_trace_enabled()&&!perf_trace_frame_active());
  frame(0);frame(1,true);assert(g_perfTrace.rows[0].id==1&&g_perfTrace.rows[1].id==2);perf_trace_end_session();
  assert(read(keep)=="keep me");assert(exists(out+"/frames-mission-002.csv"));assert(g_perfTrace.rows.capacity()==0);
  advance(100000);perf_trace_begin_session(&window);frame(2);frame(3);perf_trace_end_session();
  assert(exists(out+"/frames-mission-003.csv"));assert(read(keep)=="keep me");
 } else if(mode=="ring") {
  setenv("OPENCNC3D_PERFTRACE",(out+"/frames.csv").c_str(),1);perf_trace_begin_session(&window);
  const size_t count=PERFTRACE_CAPACITY+9;
  for(size_t i=0;i<count;i++)frame((int)i);
  assert(g_perfTrace.count==PERFTRACE_CAPACITY&&g_perfTrace.dropped==9);
  assert(g_perfTrace.rows[g_perfTrace.next].id==10);perf_trace_end_session();
 } else if(mode=="cancel") {
  setenv("OPENCNC3D_PERFTRACE",(out+"/frames.csv").c_str(),1);perf_trace_begin_session(&window);
  perf_trace_begin_frame(0);assert(perf_trace_frame_active());advance(700);perf_trace_cancel_frame();assert(!perf_trace_frame_active());frame(1);perf_trace_end_session();
 } else if(mode=="gpu") {
  mockGpu=true;setenv("OPENCNC3D_PERFTRACE",(out+"/frames.csv").c_str(),1);perf_trace_begin_session(&window);assert(g_perfTrace.gpuState==1);
  for(int i=0;i<33;i++)frame(i);
  assert(g_perfTrace.rows[0].gpuMs<0&&g_perfTrace.rows[32].gpuMs<0);
  mockReady=true;frame(33);assert(std::llround(g_perfTrace.rows[0].gpuMs*1.0e6)==2200000);assert(std::llround(g_perfTrace.rows[31].gpuMs*1.0e6)==2200000);assert(g_perfTrace.rows[32].gpuMs<0);
  perf_trace_end_session();assert(mockQueries.empty()&&mockGpuDeletes==64);
  mockReady=false;perf_trace_begin_session(&window);const Uint64 first=g_perfTrace.serial+1;
  for(size_t i=0;i<PERFTRACE_CAPACITY+34;i++)frame((int)i);
  mockReady=true;frame((int)PERFTRACE_CAPACITY+34);
  for(const auto& r:g_perfTrace.rows)assert(r.id>=first+35&&r.gpuMs<0);
  perf_trace_end_session();assert(mockQueries.empty()&&mockGpuDeletes==128);
 } else assert(false);
 fprintf(stdout,"PASS %s capacity=%zu row_bytes=%zu buffer_bytes=%zu\n",mode.c_str(),PERFTRACE_CAPACITY,sizeof(PerfTraceRow),PERFTRACE_CAPACITY*sizeof(PerfTraceRow));
}
