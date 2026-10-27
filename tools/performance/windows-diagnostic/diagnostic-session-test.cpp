#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <map>
#include <vector>
#include <cassert>
#include <atomic>
#include <thread>
#include <chrono>
#include <cmath>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#define APIENTRY
using Uint64=uint64_t;using Uint32=uint32_t;using GLuint=unsigned;using GLenum=unsigned;using GLint=int;using GLsizei=int;
static const GLenum GL_EXTENSIONS=1,GL_VERSION=2,GL_RENDERER=3,GL_VENDOR=4;
struct SDL_Window{};struct SDL_DisplayMode{int refresh_rate=60;};struct SDL_version{unsigned char major=2,minor=32,patch=8;};
static std::atomic<Uint64> mockClock{1000000};static bool mockGpu=false,mockReady=false;static GLuint mockNextQuery=1;
static int mockGpuDeletes=0;
struct MockQuery{unsigned long long stamp=0;};static std::map<GLuint,MockQuery> mockQueries;
static Uint64 SDL_GetPerformanceCounter(){return mockClock.load();}
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
static void mock_stamp(GLuint q,GLenum){mockQueries.at(q).stamp=mockClock.load()*1000;}
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
#include "../../../game/fx_state.h"
static int terrain_texset_drawn(){return 3;}
#define OPENCNC3D_BUILD_MARK "test-reviewed-header"
static const char* g_scenLabel="SCG08EA";struct MockPack{char scen[16]="SCG08EA",theater[16]="DESERT";};static MockPack g_pack;
static int g_mapW=64,g_mapH=64,g_engineFrame=0,g_simTicks=0,g_shroudOn=1;
static float g_camX=45,g_camZ=45,g_dist=3000,g_zoom=1,g_tickAlpha=0;
#ifndef PERF_TRACE_TEST_HEADER
#define PERF_TRACE_TEST_HEADER "../../../game/perf_trace.h"
#endif
#include PERF_TRACE_TEST_HEADER
#include "../../../game/diagnostic_session.h"
static SDL_Window window;
static unsigned mission=0;static bool skipFrame=false;
static void checked_send(game_diag::Packet& p) {while(!game_diag::enqueue(p))std::this_thread::yield();}
static void on_start(SDL_Window*,unsigned id){mission=id;game_diag::phase(5,(int)id);}
static void on_frame(const PerfTraceRow& r,unsigned id){if(skipFrame)return;game_diag::Packet p;p.kind=game_diag::FRAME;p.frame.row=r;p.frame.mission=id;p.frame.work[0][0]=20;p.frame.gauge[0]=10;p.frame.gauge[20]=Uint64(0x100000000ULL)+r.engineFrame;checked_send(p);}
static void on_gpu(const PerfTraceRow& r,unsigned id){game_diag::Packet p;p.kind=game_diag::GPU;p.frame.row=r;p.frame.mission=id;checked_send(p);}
static void on_end(unsigned){game_diag::phase(2);}
static void advance(Uint64 ticks){mockClock.fetch_add(ticks);}
static void frame(int ticks,Uint64 draw=2000){
 perf_trace_begin_frame(ticks);advance(1000);perf_trace_events_end();advance(500);perf_trace_tick_begin();
 Uint64 a=perf_trace_now();advance(200);perf_trace_add_sim(a);Uint64 b=perf_trace_now();advance(300);perf_trace_add_refresh(b);advance(400);perf_trace_tick_end();
 advance(600);perf_trace_draw_begin();advance(draw);perf_trace_section("opaque");perf_trace_draw_end();advance(200);perf_trace_swap_begin();advance(3000);perf_trace_swap_end();advance(100);
 Uint64 extra[2]={7,~(Uint64)0};perf_trace_extra("available,missing",extra,2);perf_trace_end_frame(&window,1920,1080,ticks+1,false,false,false,12);
}
int main(int argc,char** argv) {
 assert(argc==3);const std::string mode=argv[1],root=argv[2];fx_defaults(&g_fx);
 if(mode=="off"){unsetenv("OPENCNC3D_PERFTRACE");perf_trace_begin_session(&window);assert(!perf_trace_enabled()&&!game_diag::active());return 0;}
 if(mode=="permission"){assert(!game_diag::start((root+"/absent/Logs").c_str()));assert(!game_diag::active());return 0;}
 if(mode=="queue"){
  game_diag::State s;s.queue.resize(game_diag::QUEUE_CAPACITY);s.active=true;game_diag::state=&s;game_diag::Packet p;
  for(size_t i=0;i<game_diag::QUEUE_CAPACITY;i++)assert(game_diag::enqueue(p));const auto at=std::chrono::steady_clock::now();for(int i=0;i<10000;i++)assert(!game_diag::enqueue(p));
  assert(s.dropped==10000&&std::chrono::steady_clock::now()-at<std::chrono::milliseconds(100));s.count=0;
  std::thread lock([&]{std::lock_guard<std::mutex> held(s.queueMutex);s.stop=true;std::this_thread::sleep_for(std::chrono::milliseconds(80));});while(!s.stop)std::this_thread::yield();assert(!game_diag::enqueue(p));lock.join();s.active=false;game_diag::state=NULL;return 0;
 }
 if(mode=="writeerror"){
  game_diag::State s;s.freq=1000000;int fds[2];assert(pipe(fds)==0);close(fds[0]);signal(SIGPIPE,SIG_IGN);FILE* file=fdopen(fds[1],"wb");assert(file);setvbuf(file,NULL,_IONBF,0);Uint64 bytes=0;
  const auto at=std::chrono::steady_clock::now();assert(!game_diag::put(s,file,"row\n",bytes,100));assert(s.ioErrors==1&&bytes==0);fclose(file);assert(std::chrono::steady_clock::now()-at<std::chrono::milliseconds(100));return 0;
 }
 game_diag::Limits limits;if(mode=="caps"){limits.summary=1500;limits.detail=2500;limits.config=10;limits.event=300;}
 assert(game_diag::start(root.c_str(),limits));const std::string directory=game_diag::state->dir;assert(!game_diag::start(root.c_str()));
 game_diag::configuration();g_perfTraceHooks.enabled=game_diag::active;g_perfTraceHooks.beginSession=on_start;g_perfTraceHooks.frame=on_frame;g_perfTraceHooks.gpu=on_gpu;g_perfTraceHooks.endSession=on_end;
 mockGpu=true;mockReady=true;perf_trace_begin_session(&window);assert(perf_trace_enabled()&&g_perfTrace.permanent);
 if(mode=="long"){
  for(int i=0;i<20000;i++){g_engineFrame=i%10000;g_simTicks=i%10000;game_diag::configuration();frame(i%10000,i==1500?100000:2000);advance(11700);
   if(i==1000){g_fx.water_fx=!g_fx.water_fx;game_diag::configuration();}
   if(i==9999){perf_trace_end_session();game_diag::phase(4);advance(2000000);perf_trace_begin_session(&window);}
  }
 } else if(mode=="gaps"){
  for(int i=0;i<13;i++){skipFrame=i>=3&&i<8;frame(i);advance(11700);}
 } else if(mode=="missions"){frame(0);perf_trace_end_session();perf_trace_begin_session(&window);frame(0);}
 else if(mode=="privacy"){
  game_diag::Packet p;p.kind=game_diag::GLINFO;strcpy(p.text,(std::string("C:\\")+"Users\\privacy-fixture\\file user@example.invalid").c_str());checked_send(p);p.kind=game_diag::SCHEMA;p.a=0;p.b=0;strcpy(p.text,(std::string("/Users/")+"privacy-fixture/file").c_str());checked_send(p);frame(0);
 } else if(mode=="partial"){for(int i=0;i<200;i++){frame(i);advance(11700);}std::this_thread::sleep_for(std::chrono::milliseconds(200));advance(2000000);std::this_thread::sleep_for(std::chrono::milliseconds(100));_Exit(0);}
 else if(mode=="blocked"){
  while(true){{std::lock_guard<std::mutex> l(game_diag::state->queueMutex);if(!game_diag::state->count)break;}std::this_thread::yield();}
  std::unique_lock<std::mutex> held(game_diag::state->queueMutex);game_diag::state->limits.joinMs=20;
  game_diag::State* old=game_diag::state;const auto at=std::chrono::steady_clock::now();assert(!game_diag::finish());assert(game_diag::state==NULL&&game_diag::finish());assert(std::chrono::steady_clock::now()-at<std::chrono::milliseconds(100));held.unlock();
  for(int i=0;i<200&&!old->done;i++)std::this_thread::sleep_for(std::chrono::milliseconds(5));assert(old->done);return 0;
 } else if(mode=="collision"){frame(0);perf_trace_end_session();assert(game_diag::finish());assert(game_diag::start(root.c_str()));assert(game_diag::state->dir!=directory);assert(game_diag::finish());return 0;}
 else if(mode=="caps"){for(int i=0;i<200;i++){frame(i,50000);advance(11700);}}else assert(false);
 perf_trace_end_session();game_diag::phase(8);assert(game_diag::finish());assert(game_diag::state==NULL&&game_diag::finish());
 assert(mockQueries.empty());assert(game_diag::safe_text("user@example.invalid")=="redacted");assert(game_diag::scene_id((std::string("/Users/")+"privacy-fixture/save.sav").c_str())=="custom");
 printf("PASS %s\n",mode.c_str());
}
