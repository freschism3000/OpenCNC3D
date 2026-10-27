/* Automatic diagnostics for the dedicated Windows frontend only. The producer
   never waits for filesystem work, and the worker never calls OpenGL. */
#ifndef OPENCNC3D_DIAGNOSTIC_SESSION_H
#define OPENCNC3D_DIAGNOSTIC_SESSION_H
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iomanip>
#include <locale>
#include <map>
#include <new>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace game_diag {
static FILE* native_fopen(const char*,const char*);
static int native_mkdir(const char*);
static int native_replace(const char*,const char*);
static const size_t QUEUE_CAPACITY=1024, CONTEXT_CAPACITY=1024, PASS_COUNT=40, WORK_COUNT=8, GAUGE_COUNT=21;
static const char* const WORK_NAMES[WORK_COUNT]={"mesh_draw_calls","immediate_vertices","mesh_vbo_uploads","mesh_cache_hits","mesh_cache_misses","mesh_evictions","hud_upload_bytes","fragment_upload_bytes"};
static const char* const GAUGE_NAMES[GAUGE_COUNT]={"mesh_entries","mesh_bytes","frame_pose_entries","frame_pose_bytes","geometry_entries","geometry_bytes","settled_entries","settled_bytes","frame_pool_stores","frame_pool_fallbacks","frame_pool_trimmed","game_speed_setting","object_infantry","object_vehicles","object_buildings","object_terrain","object_aircraft","object_other","enhanced_model_entries","enhanced_burning_trees","mesh_admission_refused"};
static const char* const PHASE_NAMES[]={"startup","initialization","menu","movie","mission_load","mission","settings","display","shutdown","error"};
static const Uint64 MIB=1024u*1024u;
struct Limits { Uint64 summary=192*MIB,detail=48*MIB,config=8*MIB,event=8*MIB; unsigned joinMs=500; };
static const size_t SLICE_COUNT=10;
static const char* const SLICE_NAMES[SLICE_COUNT]={"events","camera","tick","advance","refresh","prep","draw_submit","overlay","swap","post_swap"};
struct Frame { double interval=0; bool haveInterval=false; PerfTraceRow row; Uint64 work[PASS_COUNT][WORK_COUNT]={},gauge[GAUGE_COUNT]={}; unsigned mission=0; };
enum Kind { FRAME,GPU,PHASE,CONFIG,SCHEMA,GLINFO,MODEL,DISPLAY,SCENE };
struct Packet { Kind kind=PHASE; Uint64 clock=0; Frame frame; FxState fx={}; int a=0,b=0; char text[384]={}; };
struct Aggregate {
 Uint64 second=0,frames=0,intervalSamples=0,gpuSamples=0; unsigned mission=0; int phase=0;
 double sum=0,worst=0,swap=0,interval=0,intervalWorst=0,gpu=0,gpuWorst=0,section[PASS_COUNT]={},slice[SLICE_COUNT]={};
 Uint64 work[PASS_COUNT][WORK_COUNT]={},extra[48]={},gauge[GAUGE_COUNT]={};
 bool extraSeen[48]={}; Frame worstFrame; bool haveWorst=false;
};
struct State {
 std::atomic<bool> active{false},stop{false},done{false};
 std::atomic<Uint64> dropped{0},ioErrors{0},modelMarkersDropped{0};
 std::mutex queueMutex,doneMutex; std::condition_variable wake,finished;
 std::vector<Packet> queue; size_t head=0,count=0;
 std::thread worker; Limits limits; std::string dir;
 Uint64 origin=0,freq=0,frames=0,recordGaps=0,eventDropped=0,gpuSamples=0,contextTruncated=0,hardwareDropped=0,unmatchedGpu=0,lateGpu=0,detailDropped=0,summaryDropped=0,configDropped=0;
 unsigned mission=0,phase=0,producerPhase=0,configSerial=0; Uint64 lastSwap=0,lastFrame=0,detailThrough=0,detailUntil=0,lastFlush=0;
 unsigned baselineSamples=0; double baseline=0; bool detailActive=false;
 FxState lastFx={}; bool haveFx=false;
 std::deque<Frame> context; std::map<std::pair<Uint64,unsigned>,Aggregate> aggregates;
 std::vector<std::string> passNames,extraNames;
 FILE *summary=NULL,*detail=NULL,*gpu=NULL,*events=NULL,*hardware=NULL,*schema=NULL;
 Uint64 summaryBytes=0,detailBytes=0,eventBytes=0,configBytes=0;
};
static State* state=NULL;  /* Heap-owned; safe to abandon if exit-time disk IO is stalled. */
static bool active(){return state&&state->active.load(std::memory_order_relaxed);}
static Uint64 now(){return SDL_GetPerformanceCounter();}
static std::string safe_text(const char* input) {
 if(!input)return "unavailable";
 std::string x(input);if(x.size()>240)x.resize(240);
 if(x.find(":\\")!=std::string::npos||x.find(":/")!=std::string::npos||x.find("/Users/")!=std::string::npos||x.find("/home/")!=std::string::npos||x.find("\\\\")!=std::string::npos||x.find('@')!=std::string::npos)return "redacted";
 for(char& c:x)if((unsigned char)c<32||c=='"'||c=='\\')c=' ';
 return x;
}
static std::string scene_id(const char* x) {
 if(!x||strlen(x)!=7||x[0]!='S'||x[1]!='C'||(x[2]!='G'&&x[2]!='B'&&x[2]!='M')||x[3]<'0'||x[3]>'9'||x[4]<'0'||x[4]>'9'||x[5]<'A'||x[5]>'Z'||x[6]<'A'||x[6]>'Z')return "custom";
 return x;
}
static std::ostringstream stream(){std::ostringstream s;s.imbue(std::locale::classic());s<<std::fixed<<std::setprecision(6);return s;}
static bool put(State& s,FILE* f,const std::string& text,Uint64& used,Uint64 cap) {
 if(!f||used>cap||text.size()>cap-used)return false;
 const size_t wrote=fwrite(text.data(),1,text.size(),f);used+=wrote;
 if(wrote!=text.size()){s.ioErrors++;return false;}return true;
}
static void event_write(State& s,FILE* f,const std::string& text){if(!put(s,f,text,s.eventBytes,s.limits.event))s.eventDropped++;}
static double ms(State& s,Uint64 a,Uint64 b){return b>=a?double(b-a)*1000.0/double(s.freq):0;}
static bool enqueue(Packet& p) {
 State* s=state;if(!s||!s->active.load(std::memory_order_relaxed))return false;
 std::unique_lock<std::mutex> lock(s->queueMutex,std::try_to_lock);
 if(!lock.owns_lock()||s->count==s->queue.size()){s->dropped++;return false;}
 s->queue[(s->head+s->count)%s->queue.size()]=p;s->count++;return true;
}
static std::string fx_text(const FxState& fx) {
 auto o=stream();o<<"# Numeric diagnostic snapshot; no file paths or user identity.\n";
 for(const FxParam* p=FX_PARAMS;p->kind!=FXP_END;p++) {
  if(p->kind==FXP_GROUP)continue;o<<p->key<<' ';
  if(p->kind==FXP_BOOL)o<<*(const int*)((const char*)&fx+p->off);
  else o<<*(const float*)((const char*)&fx+p->off);o<<'\n';
 }
 for(int m=0;m<FX_MODE_COUNT;m++)for(int r=0;r<FXR_COUNT;r++)o<<"mode."<<FX_MODE_NAME[m]<<'.'<<FX_MODE_ROWS[r].key<<' '<<fx.mode_row[m][r]<<'\n';
 return o.str();
}
static void configuration() {
 State* s=state;if(!active())return;
 if(s->haveFx&&memcmp(&s->lastFx,&g_fx,sizeof g_fx)==0)return;
 Packet p;p.kind=CONFIG;p.clock=now();p.fx=g_fx;
 if(enqueue(p)){s->lastFx=g_fx;s->haveFx=true;}
}
static void phase(int value,int code=0) {
 if(!active())return;Packet p;p.kind=PHASE;p.clock=now();p.a=value>=0&&value<10?value:9;p.b=code;state->producerPhase=(unsigned)p.a;enqueue(p);
}
static void write_status(State& s,bool clean) {
 auto o=stream();o<<"{\n\"format\":1,\"clean_exit\":"<<(clean?"true":"false")<<",\"recording_active\":"<<(s.active?"true":"false")
 <<",\"frame_rows_seen\":"<<s.frames<<",\"unobserved_or_cancelled_frames\":"<<s.recordGaps<<",\"event_rows_dropped\":"<<s.eventDropped<<",\"gpu_samples_seen\":"<<s.gpuSamples<<",\"queue_dropped\":"<<s.dropped.load()
 <<",\"io_errors\":"<<s.ioErrors.load()<<",\"model_markers_dropped\":"<<s.modelMarkersDropped.load()<<",\"hardware_rows_dropped\":"<<s.hardwareDropped<<",\"context_truncated\":"<<s.contextTruncated<<",\"unmatched_gpu_samples\":"<<s.unmatchedGpu<<",\"late_gpu_samples\":"<<s.lateGpu
 <<",\"detail_rows_dropped\":"<<s.detailDropped<<",\"aggregate_seconds_dropped\":"<<s.summaryDropped<<",\"config_snapshots_dropped\":"<<s.configDropped
 <<",\"summary_bytes\":"<<s.summaryBytes<<",\"detail_bytes\":"<<s.detailBytes<<",\"event_bytes\":"<<s.eventBytes<<",\"config_bytes\":"<<s.configBytes
 <<",\"queue_capacity\":"<<QUEUE_CAPACITY<<",\"context_capacity\":"<<CONTEXT_CAPACITY<<",\"queue_bytes\":"<<QUEUE_CAPACITY*sizeof(Packet)
 <<",\"disk_cap_bytes\":"<<s.limits.summary+s.limits.detail+s.limits.config+s.limits.event<<",\"frame_interval_is_displayed_fps\":false}\n";
 const std::string next=s.dir+"/status-next.json",target=s.dir+"/status.json";
 FILE* f=native_fopen(next.c_str(),"wb");if(f){const std::string text=o.str();const bool wrote=fwrite(text.data(),1,text.size(),f)==text.size();const bool closed=fclose(f)==0;if(!wrote||!closed||native_replace(next.c_str(),target.c_str())!=0)s.ioErrors++;}else s.ioErrors++;
}
static void slices(State& s,const PerfTraceRow& r,double* out) {
 out[0]=ms(s,r.begin,r.events);out[1]=ms(s,r.events,r.tick0);out[2]=ms(s,r.tick0,r.tick1);out[3]=ms(s,0,r.sim);out[4]=ms(s,0,r.refresh);out[5]=ms(s,r.tick1,r.draw0);out[6]=ms(s,r.draw0,r.draw1);out[7]=ms(s,r.draw1,r.overlay1);out[8]=ms(s,r.overlay1,r.swap1);out[9]=ms(s,r.swap1,r.end);
}
static std::string frame_text(State& s,const Frame& f,const char* cause) {
 const auto& r=f.row;auto o=stream();o<<r.id<<','<<f.mission<<','<<ms(s,s.origin,r.begin)<<','<<ms(s,r.begin,r.end)<<','<<ms(s,r.overlay1,r.swap1)<<','<<cause;
 if(r.gpuMs>=0)o<<','<<r.gpuMs;else o<<',';
 o<<','<<r.engineFrame<<','<<r.simTicks<<','<<r.paused<<','<<r.options<<','<<r.held<<','<<r.objects<<','<<r.w<<','<<r.h<<','<<r.requested<<','<<r.returned<<','<<r.display<<','<<r.hz<<','<<r.windowFlags<<','<<r.mode<<','<<r.terrain<<','<<r.infantry<<','<<r.trees<<','<<r.tiberium<<','<<r.water<<','<<r.bilinear;
 o<<',';if(f.haveInterval)o<<f.interval;
 o<<','<<r.tickBefore<<','<<r.tickAfter<<','<<r.shroud<<','<<r.camX<<','<<r.camZ<<','<<r.distance<<','<<r.zoom<<','<<r.alpha;
 double split[SLICE_COUNT];slices(s,r,split);for(double v:split)o<<','<<v;
 for(size_t i=0;i<PASS_COUNT;i++)o<<','<<ms(s,0,r.section[i]);
 for(size_t i=0;i<48;i++){o<<',';if(r.extra[i]!=~Uint64(0))o<<r.extra[i];}
 for(size_t i=0;i<PASS_COUNT;i++)for(size_t k=0;k<WORK_COUNT;k++)o<<','<<f.work[i][k];
 for(size_t i=0;i<GAUGE_COUNT;i++)o<<','<<f.gauge[i];o<<'\n';return o.str();
}
static void detail_row(State& s,const Frame& f,const char* cause,bool uniqueContext=true) {
 if(uniqueContext){if(f.row.id<=s.detailThrough)return;s.detailThrough=f.row.id;}
 if(!put(s,s.detail,frame_text(s,f,cause),s.detailBytes,s.limits.detail))s.detailDropped++;
}
static void aggregate_write(State& s,Aggregate& a) {
 auto o=stream();o<<a.second<<','<<a.mission<<','<<a.phase<<','<<a.frames<<',';
 if(a.frames)o<<a.sum/a.frames;o<<','<<a.worst<<',';if(a.frames)o<<a.swap/a.frames;o<<',';
 if(a.intervalSamples)o<<a.interval/a.intervalSamples;o<<','<<a.intervalWorst<<','<<a.gpuSamples<<',';
 if(a.gpuSamples)o<<a.gpu/a.gpuSamples;o<<',';if(a.gpuSamples)o<<a.gpuWorst;
 for(size_t i=0;i<SLICE_COUNT;i++){o<<',';if(a.frames)o<<a.slice[i]/a.frames;}
 for(size_t i=0;i<PASS_COUNT;i++){o<<',';if(a.frames)o<<a.section[i]/a.frames;}
 for(size_t i=0;i<48;i++){o<<',';if(a.extraSeen[i])o<<a.extra[i];}
 for(size_t i=0;i<PASS_COUNT;i++)for(size_t k=0;k<WORK_COUNT;k++)o<<','<<a.work[i][k];
 for(size_t i=0;i<GAUGE_COUNT;i++)o<<','<<a.gauge[i];o<<'\n';
 if(!put(s,s.summary,o.str(),s.summaryBytes,s.limits.summary))s.summaryDropped++;
 if(a.haveWorst)detail_row(s,a.worstFrame,"second_worst",false);
}
static void aggregates_flush(State& s,Uint64 through,bool all) {
 while(!s.aggregates.empty()) {auto i=s.aggregates.begin();if(!all&&i->first.first+2>=through)break;aggregate_write(s,i->second);s.aggregates.erase(i);}
}
static void process_frame(State& s,const Frame& incoming) {
 Frame frame=incoming;const auto& r=frame.row;
 if(s.mission!=frame.mission){s.lastSwap=0;s.baseline=0;s.baselineSamples=0;s.context.clear();s.detailActive=false;s.phase=5;}
 const Uint64 sec=(r.begin-s.origin)/s.freq;
 auto& a=s.aggregates[{sec,frame.mission}];a.second=sec;a.mission=frame.mission;a.phase=(int)s.phase;
 const bool contiguous=s.lastFrame&&r.id==s.lastFrame+1;
 if(s.lastFrame&&r.id>s.lastFrame+1){s.recordGaps+=r.id-s.lastFrame-1;auto gap=stream();gap<<ms(s,s.origin,r.begin)<<",frame_record_gap,"<<r.id-s.lastFrame-1<<','<<frame.mission<<",\"observed_record_span_ms="<<ms(s,s.lastSwap,r.swap1)<<"\"\n";event_write(s,s.events,gap.str());}
 const double total=ms(s,r.begin,r.end),interval=s.lastSwap&&s.mission==frame.mission&&contiguous?ms(s,s.lastSwap,r.swap1):0;
 frame.haveInterval=s.lastSwap&&s.mission==frame.mission&&contiguous;frame.interval=interval;s.lastFrame=r.id;
 s.lastSwap=r.swap1;s.mission=frame.mission;s.frames++;a.frames++;a.sum+=total;a.swap+=ms(s,r.overlay1,r.swap1);if(frame.haveInterval){a.interval+=interval;a.intervalSamples++;}a.intervalWorst=std::max(a.intervalWorst,interval);
 if(total>=a.worst){a.worst=total;a.worstFrame=frame;a.haveWorst=true;}
 for(size_t i=0;i<PASS_COUNT;i++){a.section[i]+=ms(s,0,r.section[i]);for(size_t k=0;k<WORK_COUNT;k++)a.work[i][k]+=frame.work[i][k];}
 double split[SLICE_COUNT];slices(s,r,split);for(size_t i=0;i<SLICE_COUNT;i++)a.slice[i]+=split[i];
 for(size_t i=0;i<48;i++)if(r.extra[i]!=~Uint64(0)){a.extraSeen[i]=true;a.extra[i]=std::max(a.extra[i],r.extra[i]);}
 for(size_t i=0;i<GAUGE_COUNT;i++)a.gauge[i]=std::max(a.gauge[i],frame.gauge[i]);
 const double observed=std::max(total,interval);bool spike=observed>33.3||(s.baselineSamples>=5&&s.baseline>0&&observed>s.baseline*1.5&&observed>s.baseline+8.0);
 if(frame.haveInterval){s.baseline=s.baselineSamples?0.98*s.baseline+0.02*observed:observed;s.baselineSamples++;}
 while(!s.context.empty()&&s.context.front().row.end+2*s.freq<r.begin)s.context.pop_front();
 if(s.context.size()==CONTEXT_CAPACITY){s.context.pop_front();s.contextTruncated++;}s.context.push_back(frame);
 if(spike){s.detailUntil=r.end+2*s.freq;s.detailActive=true;for(const auto& c:s.context)if(c.mission==frame.mission&&c.row.end+2*s.freq>=r.begin)detail_row(s,c,"spike_context");}
 if(s.detailActive){detail_row(s,frame,"spike_after");if(r.begin>s.detailUntil)s.detailActive=false;}
 aggregates_flush(s,sec,false);
}
static void process_packet(State& s,const Packet& p) {
 if(p.kind==FRAME)process_frame(s,p.frame);
 else if(p.kind==GPU) {
  s.gpuSamples++;const Uint64 sec=(p.frame.row.begin-s.origin)/s.freq;
  bool retained=false;for(auto i=s.context.rbegin();i!=s.context.rend();++i)if(i->row.id==p.frame.row.id&&i->mission==p.frame.mission){i->row.gpuMs=p.frame.row.gpuMs;retained=true;break;}
  auto i=s.aggregates.find({sec,p.frame.mission});if(retained&&i!=s.aggregates.end()){auto& a=i->second;a.gpuSamples++;a.gpu+=p.frame.row.gpuMs;a.gpuWorst=std::max(a.gpuWorst,p.frame.row.gpuMs);if(a.haveWorst&&a.worstFrame.row.id==p.frame.row.id)a.worstFrame.row.gpuMs=p.frame.row.gpuMs;}else if(retained)s.lateGpu++;else s.unmatchedGpu++;
  auto o=stream();o<<p.frame.row.id<<','<<p.frame.mission<<','<<p.frame.row.gpuMs<<','<<(retained?1:0)<<'\n';if(!put(s,s.gpu,o.str(),s.detailBytes,s.limits.detail))s.detailDropped++;
 } else if(p.kind==CONFIG) {
  const std::string text=fx_text(p.fx);if(text.size()>s.limits.config-s.configBytes){s.configDropped++;return;}
  char name[64];snprintf(name,sizeof name,"/settings-%04u.cfg",++s.configSerial);FILE* f=native_fopen((s.dir+name).c_str(),"wb");
  if(f){if(fwrite(text.data(),1,text.size(),f)!=text.size())s.ioErrors++;fclose(f);s.configBytes+=text.size();}else{s.ioErrors++;s.configDropped++;}
  auto o=stream();o<<ms(s,s.origin,p.clock)<<",settings,"<<s.configSerial<<",0\n";event_write(s,s.events,o.str());
 } else if(p.kind==PHASE) {
  s.phase=(unsigned)p.a;if(p.a==5&&s.mission!=(unsigned)std::max(0,p.b)){s.mission=(unsigned)std::max(0,p.b);s.lastSwap=0;s.baseline=0;s.baselineSamples=0;s.context.clear();s.detailActive=false;}
  auto o=stream();o<<ms(s,s.origin,p.clock)<<','<<PHASE_NAMES[p.a]<<','<<p.b<<','<<s.mission<<'\n';event_write(s,s.events,o.str());
 } else if(p.kind==SCHEMA) {
  auto o=stream();o<<p.a<<','<<p.b<<",\""<<safe_text(p.text)<<"\"\n";event_write(s,s.schema,o.str());
 } else if(p.kind==SCENE) {
  auto o=stream();o<<ms(s,s.origin,p.clock)<<",mission_scene,"<<p.a<<','<<p.b<<",\""<<scene_id(p.text)<<";map="<<p.frame.row.w<<'x'<<p.frame.row.h<<"\"\n";event_write(s,s.events,o.str());
 } else if(p.kind==DISPLAY) {
  const auto& r=p.frame.row;auto o=stream();o<<ms(s,s.origin,p.clock)<<",display_change,0,"<<p.frame.mission<<",\"drawable="<<r.w<<'x'<<r.h<<";flags="<<r.windowFlags<<";display="<<r.display<<";hz="<<r.hz<<";swap_requested="<<r.requested<<";swap_returned="<<r.returned<<"\"\n";event_write(s,s.events,o.str());
 } else if(p.kind==MODEL) {
  auto o=stream();o<<ms(s,s.origin,p.clock)<<",model_stage,"<<p.a<<','<<p.b<<",\""<<safe_text(p.text)<<";engine_frame="<<p.frame.row.engineFrame<<";kind="<<p.frame.row.tickBefore<<";model="<<p.frame.row.tickAfter<<"\"\n";event_write(s,s.events,o.str());
 } else if(p.kind==GLINFO) {
  auto o=stream();o<<"# GL "<<safe_text(p.text)<<'\n';event_write(s,s.hardware,o.str());
 }
}
/* Native hardware and process sampling are isolated in a separate adapter. */
#include "diagnostic_wininfo.h"
static void close_files(State& s) {for(FILE** p:{&s.summary,&s.detail,&s.gpu,&s.events,&s.hardware,&s.schema}){FILE* f=*p;*p=NULL;if(f&&fclose(f)!=0)s.ioErrors++;}}
static void worker_main(State* s) {
 try {
  s->summary=native_fopen((s->dir+"/seconds.csv").c_str(),"wb");s->detail=native_fopen((s->dir+"/frames-context.csv").c_str(),"wb");s->gpu=native_fopen((s->dir+"/gpu-completions.csv").c_str(),"wb");s->events=native_fopen((s->dir+"/timeline.csv").c_str(),"wb");s->hardware=native_fopen((s->dir+"/hardware-and-process.csv").c_str(),"wb");s->schema=native_fopen((s->dir+"/schema.csv").c_str(),"wb");
  for(FILE* f:{s->summary,s->detail,s->gpu,s->events,s->hardware,s->schema})if(f)setvbuf(f,NULL,_IOFBF,65536);else s->ioErrors++;
  auto header=stream();header<<"second,mission,phase,frames,frame_total_mean_ms,frame_total_worst_ms,swap_mean_ms,inter_present_mean_ms,inter_present_worst_ms,gpu_samples,gpu_mean_ms,gpu_worst_ms";
  for(size_t i=0;i<SLICE_COUNT;i++)header<<','<<SLICE_NAMES[i]<<"_mean_ms";
  for(size_t i=0;i<PASS_COUNT;i++)header<<",cpu_pass_"<<i<<"_mean_ms";for(size_t i=0;i<48;i++)header<<",extra_"<<i<<"_max";
  for(size_t i=0;i<PASS_COUNT;i++)for(size_t k=0;k<WORK_COUNT;k++)header<<",pass_"<<i<<'_'<<WORK_NAMES[k];for(size_t i=0;i<GAUGE_COUNT;i++)header<<','<<GAUGE_NAMES[i]<<"_max";header<<'\n';if(!put(*s,s->summary,header.str(),s->summaryBytes,s->limits.summary)){if(s->summary)fclose(s->summary);s->summary=NULL;}
  auto detailHeader=stream();detailHeader<<"frame,mission,start_ms,frame_total_ms,swap_ms,cause,gpu_span_ms,engine_frame,sim_ticks,paused,options,held,objects,width,height,swap_requested,swap_returned,display,display_hz,window_flags,mode,terrain,infantry,trees,tiberium,water,bilinear";
  detailHeader<<",inter_present_ms,tick_before,tick_after,shroud,cam_x,cam_z,distance,zoom,tick_alpha";for(size_t i=0;i<SLICE_COUNT;i++)detailHeader<<','<<SLICE_NAMES[i]<<"_ms";
  for(size_t i=0;i<PASS_COUNT;i++)detailHeader<<",cpu_pass_"<<i<<"_ms";for(size_t i=0;i<48;i++)detailHeader<<",extra_"<<i;for(size_t i=0;i<PASS_COUNT;i++)for(size_t k=0;k<WORK_COUNT;k++)detailHeader<<",pass_"<<i<<'_'<<WORK_NAMES[k];for(size_t i=0;i<GAUGE_COUNT;i++)detailHeader<<','<<GAUGE_NAMES[i];detailHeader<<'\n';if(!put(*s,s->detail,detailHeader.str(),s->detailBytes,s->limits.detail)){if(s->detail)fclose(s->detail);s->detail=NULL;}
  put(*s,s->gpu,"frame,mission,gpu_span_ms,retained_recent_frame\n",s->detailBytes,s->limits.detail);put(*s,s->events,"at_ms,phase_or_event,code,mission,detail\n",s->eventBytes,s->limits.event);put(*s,s->schema,"kind,index,name\n",s->eventBytes,s->limits.event);
  native_hardware(*s);Uint64 sampled=0;
  while(true) {
   std::array<Packet,16> batch;size_t n=0;
   {std::lock_guard<std::mutex> lock(s->queueMutex);n=std::min(batch.size(),s->count);for(size_t i=0;i<n;i++)batch[i]=s->queue[(s->head+i)%s->queue.size()];s->head=(s->head+n)%s->queue.size();s->count-=n;}
   for(size_t i=0;i<n;i++)process_packet(*s,batch[i]);
   const Uint64 clock=now();if(clock>=sampled+s->freq){native_process_sample(*s,clock);auto heartbeat=stream();heartbeat<<ms(*s,s->origin,clock)<<",heartbeat,"<<s->phase<<','<<s->mission<<"\n";event_write(*s,s->events,heartbeat.str());sampled=clock;}
   if(clock>=s->lastFlush+s->freq){for(FILE* f:{s->summary,s->detail,s->gpu,s->events,s->hardware,s->schema})if(f&&fflush(f)!=0)s->ioErrors++;write_status(*s,false);s->lastFlush=clock;}
   if(s->stop&&n==0)break;
   if(!n){std::unique_lock<std::mutex> wait(s->doneMutex);s->wake.wait_for(wait,std::chrono::milliseconds(50));}
  }
  aggregates_flush(*s,0,true);
  close_files(*s);
  native_close();s->active=false;write_status(*s,s->ioErrors==0);
 } catch(...) {close_files(*s);native_close();s->ioErrors++;s->active=false;try{write_status(*s,false);}catch(...){}}
 s->done=true;s->finished.notify_all();
}
static bool start(const char* logsRoot,Limits limits=Limits()) {
 if(active())return false;
 State* s=new(std::nothrow)State;if(!s)return false;s->limits=limits;s->freq=SDL_GetPerformanceFrequency();s->origin=now();if(!s->freq){delete s;return false;}
 if(!logsRoot||!*logsRoot){delete s;return false;}
if(native_mkdir(logsRoot)!=0&&errno!=EEXIST){delete s;return false;}
 const time_t stamp=time(NULL);struct tm tm={};
#ifdef _WIN32
 gmtime_s(&tm,&stamp);
#else
 gmtime_r(&stamp,&tm);
#endif
 char prefix[80];strftime(prefix,sizeof prefix,"session-%Y%m%d-%H%M%S-UTC",&tm);bool created=false;
 for(unsigned attempt=0;attempt<1000;attempt++) {
  char suffix[16];snprintf(suffix,sizeof suffix,"-%03u",attempt);s->dir=std::string(logsRoot)+"/"+prefix+suffix;
  if(native_mkdir(s->dir.c_str())==0){created=true;break;}
  if(errno!=EEXIST)break;
 }
 if(!created){delete s;return false;}
 FILE* probe=native_fopen((s->dir+"/status.json").c_str(),"wb");if(!probe){delete s;return false;}fputs("{\"clean_exit\":false,\"initializing\":true}\n",probe);fclose(probe);
 try{s->queue.resize(QUEUE_CAPACITY);s->active=true;state=s;s->worker=std::thread(worker_main,s);}catch(...){s->active=false;state=NULL;delete s;return false;}
 phase(0);return true;
}
static bool finish() {
 State* s=state;if(!s)return true;state=NULL; /* stop producer access before waiting; idempotent atexit */
 s->active=false;s->stop=true;s->wake.notify_all();
 std::unique_lock<std::mutex> lock(s->doneMutex);const bool complete=s->finished.wait_for(lock,std::chrono::milliseconds(s->limits.joinMs),[&]{return s->done.load();});lock.unlock();
 if(s->worker.joinable()){if(complete)s->worker.join();else s->worker.detach();}
 if(complete)delete s; /* A stalled detached writer owns its surviving heap state. */
 return complete;
}
}
#endif
