/* Opt-in bounded live-loop telemetry. CPU wall times include driver waits;
   asynchronous GPU spans are completion intervals, not GPU utilization.
   No per-frame output, glFinish, present-policy change, or visible UI. */
#ifndef OPENCNC3D_PERF_TRACE_H
#define OPENCNC3D_PERF_TRACE_H
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP 0x8E28
#endif
#ifndef GL_QUERY_COUNTER_BITS
#define GL_QUERY_COUNTER_BITS 0x8864
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif
struct PerfTraceRow {
    Uint64 id=0, begin=0, events=0, tick0=0, tick1=0, draw0=0, draw1=0, overlay1=0, swap1=0, end=0;
    Uint64 sim=0, refresh=0, extra[48]={}, section[40]={};
    double gpuMs=-1.0;
    int tickBefore=0,tickAfter=0,paused=0,options=0,held=0,objects=0,w=0,h=0;
    int requested=0,returned=0,display=0,hz=0,mode=0,terrain=0,infantry=0,trees=0,tiberium=0,water=0,bilinear=0;
    Uint32 windowFlags=0;
    int engineFrame=0,simTicks=0,shroud=0;float camX=0,camZ=0,distance=0,zoom=0,alpha=0;
};
/* Null hooks preserve the opt-in legacy capture and normal runtime behavior.
   Only the dedicated frontend activates the permanent worker. */
struct PerfTraceHooks {
 bool (*enabled)()=NULL; void (*beginSession)(SDL_Window*,unsigned)=NULL;
 void (*beginFrame)()=NULL; void (*section)(const char*,size_t)=NULL;
 void (*frame)(const PerfTraceRow&,unsigned)=NULL; void (*gpu)(const PerfTraceRow&,unsigned)=NULL;
 void (*endSession)(unsigned)=NULL;
};
static PerfTraceHooks g_perfTraceHooks;
struct PerfTraceQuery { GLuint q[2]={}; Uint64 id=0; size_t slot=0; bool pending=false; };
struct PerfTraceState {
    bool enabled=false,active=false,checked=false,permanent=false; unsigned session=0;
    char rootPath[1024]={},path[1100]={},columns[2048]={};
    const char* sections[40]={}; size_t sectionCount=0; Uint64 sectionLast=0;
    size_t extraCount=0,next=0,count=0;
    Uint64 freq=0,origin=0,serial=0,dropped=0,cancelled=0;
    int requested=0,returned=0,display=-99,hz=0,gpuState=0,currentQuery=-1,lastW=0,lastH=0; Uint32 lastFlags=0;
    std::vector<PerfTraceRow> rows;
    PerfTraceQuery queries[32];
    PerfTraceRow current;
    void (APIENTRY *gen)(GLsizei,GLuint*)=NULL;
    void (APIENTRY *del)(GLsizei,const GLuint*)=NULL;
    void (APIENTRY *stamp)(GLuint,GLenum)=NULL;
    void (APIENTRY *available)(GLuint,GLenum,GLint*)=NULL;
    void (APIENTRY *queryInfo)(GLenum,GLenum,GLint*)=NULL;
    void (APIENTRY *result)(GLuint,GLenum,unsigned long long*)=NULL;
};
static PerfTraceState g_perfTrace;
static const size_t PERFTRACE_CAPACITY=(16u*1024u*1024u/sizeof(PerfTraceRow)<16384u)
    ?16u*1024u*1024u/sizeof(PerfTraceRow):16384u;
static bool perf_trace_enabled() { return g_perfTrace.enabled; }
static bool perf_trace_frame_active() { return g_perfTrace.active; }
static void perf_trace_cancel_frame() {
    if(!g_perfTrace.active)return;
    g_perfTrace.active=false;g_perfTrace.cancelled++;
}
static Uint64 perf_trace_now() { return g_perfTrace.enabled?SDL_GetPerformanceCounter():0; }
static void perf_trace_add_sim(Uint64 t) {
    if(g_perfTrace.active&&t)g_perfTrace.current.sim+=SDL_GetPerformanceCounter()-t;
}
static void perf_trace_add_refresh(Uint64 t) {
    if(g_perfTrace.active&&t)g_perfTrace.current.refresh+=SDL_GetPerformanceCounter()-t;
}
/* Extra counters use a fixed schema supplied once by the renderer. A sentinel
   UINT64_MAX is emitted blank, distinguishing unavailable from a measured zero. */
static void perf_trace_extra(const char* columns,const Uint64* values,size_t n) {
    if(!g_perfTrace.active||!columns||!values||n>48)return;
    if(!g_perfTrace.extraCount) {
        if(strlen(columns)>=sizeof(g_perfTrace.columns))return;
        memcpy(g_perfTrace.columns,columns,strlen(columns)+1);g_perfTrace.extraCount=n;
    }
    if(n!=g_perfTrace.extraCount||strcmp(columns,g_perfTrace.columns))return;
    memcpy(g_perfTrace.current.extra,values,n*sizeof(Uint64));
}
static void perf_trace_gpu_collect() {
    if(g_perfTrace.gpuState!=1)return;
    for(auto& q:g_perfTrace.queries) {
        if(!q.pending)continue;
        GLint ready=0;g_perfTrace.available(q.q[1],GL_QUERY_RESULT_AVAILABLE,&ready);
        if(!ready)continue;
        unsigned long long a=0,b=0;
        g_perfTrace.result(q.q[0],GL_QUERY_RESULT,&a);
        g_perfTrace.result(q.q[1],GL_QUERY_RESULT,&b);
        const size_t slot=q.slot;
        if(slot<g_perfTrace.rows.size()&&g_perfTrace.rows[slot].id==q.id&&b>=a) {
            auto& r=g_perfTrace.rows[slot];r.gpuMs=(double)(b-a)*1.0e-6;
            if(g_perfTrace.permanent&&g_perfTraceHooks.gpu)g_perfTraceHooks.gpu(r,g_perfTrace.session);
        }
        q.pending=false;
    }
}
static void perf_trace_gpu_init() {
    if(g_perfTrace.gpuState)return;
    g_perfTrace.gpuState=-1;
    const char* ext=(const char*)glGetString(GL_EXTENSIONS);
    const char* ver=(const char*)glGetString(GL_VERSION);int major=0,minor=0;
    if(ver)sscanf(ver,"%d.%d",&major,&minor);
    if((!ext||!strstr(ext,"GL_ARB_timer_query"))&&(major<3||(major==3&&minor<3)))return;
    g_perfTrace.gen=(decltype(g_perfTrace.gen))SDL_GL_GetProcAddress("glGenQueries");
    g_perfTrace.del=(decltype(g_perfTrace.del))SDL_GL_GetProcAddress("glDeleteQueries");
    g_perfTrace.stamp=(decltype(g_perfTrace.stamp))SDL_GL_GetProcAddress("glQueryCounter");
    g_perfTrace.available=(decltype(g_perfTrace.available))SDL_GL_GetProcAddress("glGetQueryObjectiv");
    g_perfTrace.queryInfo=(decltype(g_perfTrace.queryInfo))SDL_GL_GetProcAddress("glGetQueryiv");
    g_perfTrace.result=(decltype(g_perfTrace.result))SDL_GL_GetProcAddress("glGetQueryObjectui64v");
    if(!g_perfTrace.gen||!g_perfTrace.del||!g_perfTrace.stamp||!g_perfTrace.available||!g_perfTrace.queryInfo||!g_perfTrace.result)return;
    GLint bits=0;g_perfTrace.queryInfo(GL_TIMESTAMP,GL_QUERY_COUNTER_BITS,&bits);if(bits<=0)return;
    g_perfTrace.gpuState=1;
}
static void perf_trace_swap_state(int requested,int returned) {
    g_perfTrace.requested=requested;g_perfTrace.returned=returned;
}
static void perf_trace_begin_session(SDL_Window* win) {
    g_perfTrace.permanent=g_perfTraceHooks.enabled&&g_perfTraceHooks.enabled();
    if(g_perfTrace.permanent) {
        try {g_perfTrace.rows.resize(PERFTRACE_CAPACITY);}catch(...){return;}
        g_perfTrace.session++;g_perfTrace.enabled=true;g_perfTrace.active=false;
        g_perfTrace.count=g_perfTrace.next=0;g_perfTrace.dropped=g_perfTrace.cancelled=0;
        g_perfTrace.freq=SDL_GetPerformanceFrequency();if(!g_perfTrace.origin)g_perfTrace.origin=SDL_GetPerformanceCounter();
        g_perfTrace.display=SDL_GetWindowDisplayIndex(win);g_perfTrace.hz=0;
        SDL_DisplayMode dm={};if(g_perfTrace.display>=0&&SDL_GetCurrentDisplayMode(g_perfTrace.display,&dm)==0)g_perfTrace.hz=dm.refresh_rate;
        perf_trace_gpu_init();
        if(g_perfTraceHooks.beginSession)g_perfTraceHooks.beginSession(win,g_perfTrace.session);
        return;
    }
    if(!g_perfTrace.checked) {
        g_perfTrace.checked=true;
        const char* p=getenv("OPENCNC3D_PERFTRACE");
        if(!p||!*p||strlen(p)>=sizeof(g_perfTrace.rootPath))return;
        memcpy(g_perfTrace.rootPath,p,strlen(p)+1);
    }
    if(!g_perfTrace.rootPath[0])return;
    const size_t rootLen=strlen(g_perfTrace.rootPath);
    const int baseLen=(int)(rootLen>=4&&!strcmp(g_perfTrace.rootPath+rootLen-4,".csv")?rootLen-4:rootLen);
    int outputFd=-1;
    for(unsigned attempt=0;attempt<1000;attempt++) {
        g_perfTrace.session++;
        snprintf(g_perfTrace.path,sizeof(g_perfTrace.path),"%.*s-mission-%03u.csv",baseLen,g_perfTrace.rootPath,g_perfTrace.session);
        outputFd=open(g_perfTrace.path,O_WRONLY|O_CREAT|O_EXCL|O_BINARY,0600);
        if(outputFd>=0)break;
        if(errno!=EEXIST)break;
    }
    if(outputFd<0){fprintf(stderr,"PERFTRACE|unavailable|cannot create unique output\n");return;}
    FILE* output=fdopen(outputFd,"wb");
    if(!output){close(outputFd);fprintf(stderr,"PERFTRACE|unavailable|cannot open output stream\n");return;}
    try { g_perfTrace.rows.resize(PERFTRACE_CAPACITY); } catch(...) {
        fprintf(stderr,"PERFTRACE|unavailable|row allocation failed\n");fclose(output);return;
    }
    g_perfTrace.enabled=true;g_perfTrace.active=false;
    g_perfTrace.count=g_perfTrace.next=0;g_perfTrace.dropped=g_perfTrace.cancelled=0;
    g_perfTrace.freq=SDL_GetPerformanceFrequency();if(!g_perfTrace.origin)g_perfTrace.origin=SDL_GetPerformanceCounter();
    g_perfTrace.display=SDL_GetWindowDisplayIndex(win);g_perfTrace.hz=0;
    SDL_DisplayMode dm={};if(g_perfTrace.display>=0&&SDL_GetCurrentDisplayMode(g_perfTrace.display,&dm)==0)g_perfTrace.hz=dm.refresh_rate;
    perf_trace_gpu_init();
    char cfg[1200];snprintf(cfg,sizeof(cfg),"%s.settings-start.cfg",g_perfTrace.path);fx_save(&g_fx,cfg);
    FILE* f=output;
    SDL_version v={};SDL_GetVersion(&v);
    fprintf(f,"# PERFTRACE schema=1 clock=SDL_GetPerformanceCounter frequency=%llu origin=%llu session=%u capacity=%zu row_bytes=%zu buffer_bytes=%zu gpu=%s\n",
        (unsigned long long)g_perfTrace.freq,(unsigned long long)g_perfTrace.origin,g_perfTrace.session,PERFTRACE_CAPACITY,sizeof(PerfTraceRow),PERFTRACE_CAPACITY*sizeof(PerfTraceRow),g_perfTrace.gpuState==1?"async_timestamp":"unavailable");
    fprintf(f,"# SDL=%u.%u.%u video=%s platform=%s pointer_bits=%zu renderer=%s vendor=%s version=%s\n",
        v.major,v.minor,v.patch,SDL_GetCurrentVideoDriver()?SDL_GetCurrentVideoDriver():"unknown",SDL_GetPlatform(),8*sizeof(void*),
        glGetString(GL_RENDERER)?(const char*)glGetString(GL_RENDERER):"unknown",glGetString(GL_VENDOR)?(const char*)glGetString(GL_VENDOR):"unknown",glGetString(GL_VERSION)?(const char*)glGetString(GL_VERSION):"unknown");
    fprintf(f,"# build=%s swap_requested=%d swap_returned=%d display=%d display_hz=%d\n",OPENCNC3D_BUILD_MARK,g_perfTrace.requested,g_perfTrace.returned,g_perfTrace.display,g_perfTrace.hz);
    fprintf(f,"# scene=%s pack=%s theater=%s map=%dx%d\n",g_scenLabel?g_scenLabel:"unknown",g_pack.scen,g_pack.theater,g_mapW,g_mapH);
    fclose(f);
    fprintf(stderr,"PERFTRACE|on|bounded=%zu|gpu=%s|buffered=1\n",PERFTRACE_CAPACITY,g_perfTrace.gpuState==1?"timestamp":"unavailable");
}
static void perf_trace_begin_frame(int ticks) {
    if(!g_perfTrace.enabled)return;
    perf_trace_cancel_frame();  /* a non-presented editor reboot iteration */
    const Uint64 begin=SDL_GetPerformanceCounter();
    perf_trace_gpu_collect();
    g_perfTrace.current=PerfTraceRow();g_perfTrace.current.id=++g_perfTrace.serial;
    g_perfTrace.current.begin=begin;g_perfTrace.current.tickBefore=ticks;
    g_perfTrace.sectionLast=g_perfTrace.current.begin;
    for(auto& n:g_perfTrace.current.extra)n=~(Uint64)0;
    g_perfTrace.active=true;g_perfTrace.currentQuery=-1;
    if(g_perfTrace.permanent&&g_perfTraceHooks.beginFrame)g_perfTraceHooks.beginFrame();
}
static void perf_trace_section(const char* name) {
    if(!g_perfTrace.active)return;
    const Uint64 now=SDL_GetPerformanceCounter();
    size_t k=0;while(k<g_perfTrace.sectionCount&&strcmp(g_perfTrace.sections[k],name))k++;
    if(k==g_perfTrace.sectionCount&&k<40){g_perfTrace.sections[k]=name;g_perfTrace.sectionCount++;}
    if(k<40){g_perfTrace.current.section[k]+=now-g_perfTrace.sectionLast;
        if(g_perfTrace.permanent&&g_perfTraceHooks.section)g_perfTraceHooks.section(name,k);}
    g_perfTrace.sectionLast=now;
}
static void perf_trace_events_end() {if(g_perfTrace.active)g_perfTrace.current.events=SDL_GetPerformanceCounter();}
static void perf_trace_tick_begin() {if(g_perfTrace.active)g_perfTrace.current.tick0=SDL_GetPerformanceCounter();}
static void perf_trace_tick_end() {if(g_perfTrace.active)g_perfTrace.current.tick1=SDL_GetPerformanceCounter();}
static void perf_trace_draw_begin() {
    if(!g_perfTrace.active)return;
    g_perfTrace.current.draw0=SDL_GetPerformanceCounter();
    g_perfTrace.sectionLast=g_perfTrace.current.draw0;
    if(g_perfTrace.gpuState!=1)return;
    for(int i=0;i<32;i++)if(!g_perfTrace.queries[i].pending){g_perfTrace.currentQuery=i;break;}
    if(g_perfTrace.currentQuery<0)return;
    auto& q=g_perfTrace.queries[g_perfTrace.currentQuery];
    if(!q.q[0])g_perfTrace.gen(2,q.q);
    if(!q.q[0]||!q.q[1]){g_perfTrace.currentQuery=-1;return;}
    q.id=g_perfTrace.current.id;q.slot=g_perfTrace.next;g_perfTrace.stamp(q.q[0],GL_TIMESTAMP);
}
static void perf_trace_draw_end() {if(g_perfTrace.active)g_perfTrace.current.draw1=SDL_GetPerformanceCounter();}
static void perf_trace_swap_begin() {
    if(!g_perfTrace.active)return;
    if(g_perfTrace.currentQuery>=0) {
        auto& q=g_perfTrace.queries[g_perfTrace.currentQuery];g_perfTrace.stamp(q.q[1],GL_TIMESTAMP);q.pending=true;
    }
    g_perfTrace.current.overlay1=SDL_GetPerformanceCounter();
}
static void perf_trace_swap_end() {if(g_perfTrace.active)g_perfTrace.current.swap1=SDL_GetPerformanceCounter();}
static void perf_trace_end_frame(SDL_Window* win,int w,int h,int ticks,bool paused,bool options,bool held,size_t objects) {
    if(!g_perfTrace.active)return;
    auto& r=g_perfTrace.current;
    r.w=w;r.h=h;r.tickAfter=ticks;r.paused=paused;r.options=options;r.held=held;r.objects=(int)objects;
    r.windowFlags=SDL_GetWindowFlags(win);r.display=SDL_GetWindowDisplayIndex(win);
    if(r.display!=g_perfTrace.display||w!=g_perfTrace.lastW||h!=g_perfTrace.lastH||r.windowFlags!=g_perfTrace.lastFlags) {
        g_perfTrace.lastW=w;g_perfTrace.lastH=h;g_perfTrace.lastFlags=r.windowFlags;
        g_perfTrace.display=r.display;g_perfTrace.hz=0;SDL_DisplayMode dm={};
        if(r.display>=0&&SDL_GetCurrentDisplayMode(r.display,&dm)==0)g_perfTrace.hz=dm.refresh_rate;
    }
    r.hz=g_perfTrace.hz;r.requested=g_perfTrace.requested;r.returned=g_perfTrace.returned;
    r.mode=fx_mode_of(&g_fx);r.terrain=terrain_texset_drawn();r.infantry=fx_infset_get();r.trees=g_fx.tree3d;r.tiberium=g_fx.tib3d;r.water=g_fx.water_fx;r.bilinear=g_fx.bilinear;
    r.engineFrame=g_engineFrame;r.simTicks=g_simTicks;r.shroud=g_shroudOn;r.camX=g_camX;r.camZ=g_camZ;r.distance=g_dist;r.zoom=g_zoom;r.alpha=g_tickAlpha;
    r.end=SDL_GetPerformanceCounter();
    g_perfTrace.rows[g_perfTrace.next]=r;g_perfTrace.next=(g_perfTrace.next+1)%PERFTRACE_CAPACITY;
    if(g_perfTrace.count<PERFTRACE_CAPACITY)g_perfTrace.count++;else g_perfTrace.dropped++;
    g_perfTrace.active=false;
    if(g_perfTrace.permanent&&g_perfTraceHooks.frame)g_perfTraceHooks.frame(r,g_perfTrace.session);
}
static double perf_trace_ms(Uint64 a,Uint64 b) {
    return b>=a?(double)(b-a)*1000.0/(double)g_perfTrace.freq:0.0;
}
static void perf_trace_end_session() {
    if(!g_perfTrace.enabled)return;
    perf_trace_cancel_frame();
    perf_trace_gpu_collect();
    FILE* f=g_perfTrace.permanent?NULL:fopen(g_perfTrace.path,"ab");
    if(f) {
        setvbuf(f,NULL,_IOFBF,65536);
        fprintf(f,"# rows=%zu total=%llu dropped_oldest=%llu non_presented_iterations=%llu pending_gpu_not_waited=1\n",g_perfTrace.count,(unsigned long long)(g_perfTrace.count+g_perfTrace.dropped),(unsigned long long)g_perfTrace.dropped,(unsigned long long)g_perfTrace.cancelled);
        fprintf(f,"frame,begin_ticks,end_ticks,start_ms,total_ms,events_ms,camera_ms,tick_ms,advance_ms,refresh_ms,prep_ms,draw_submit_ms,overlay_ms,swap_ms,post_swap_ms,gpu_span_ms,tick_before,tick_after,paused,options,held,objects,width,height,swap_requested,swap_returned,display,display_hz,window_flags,mode,terrain,infantry,trees,tiberium,water,bilinear,engine_frame,sim_ticks,shroud,cam_x,cam_z,distance,zoom,tick_alpha");
        if(g_perfTrace.extraCount)fprintf(f,",%s",g_perfTrace.columns);
        for(size_t i=0;i<g_perfTrace.sectionCount;i++)fprintf(f,",\"cpu:%s\"",g_perfTrace.sections[i]);
        fprintf(f,"\n");
        const size_t first=g_perfTrace.count==PERFTRACE_CAPACITY?g_perfTrace.next:0;
        for(size_t k=0;k<g_perfTrace.count;k++) {
            const auto& r=g_perfTrace.rows[(first+k)%PERFTRACE_CAPACITY];
            fprintf(f,"%llu,%llu,%llu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,",
                (unsigned long long)r.id,(unsigned long long)r.begin,(unsigned long long)r.end,perf_trace_ms(g_perfTrace.origin,r.begin),perf_trace_ms(r.begin,r.end),
                perf_trace_ms(r.begin,r.events),perf_trace_ms(r.events,r.tick0),perf_trace_ms(r.tick0,r.tick1),perf_trace_ms(0,r.sim),perf_trace_ms(0,r.refresh),
                perf_trace_ms(r.tick1,r.draw0),perf_trace_ms(r.draw0,r.draw1),perf_trace_ms(r.draw1,r.overlay1),perf_trace_ms(r.overlay1,r.swap1),perf_trace_ms(r.swap1,r.end));
            if(r.gpuMs>=0.0)fprintf(f,"%.6f",r.gpuMs);
            fprintf(f,",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u,%d,%d,%d,%d,%d,%d,%d",
                r.tickBefore,r.tickAfter,r.paused,r.options,r.held,r.objects,r.w,r.h,r.requested,r.returned,r.display,r.hz,(unsigned)r.windowFlags,
                r.mode,r.terrain,r.infantry,r.trees,r.tiberium,r.water,r.bilinear);
            fprintf(f,",%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f",r.engineFrame,r.simTicks,r.shroud,(double)r.camX,(double)r.camZ,(double)r.distance,(double)r.zoom,(double)r.alpha);
            for(size_t i=0;i<g_perfTrace.extraCount;i++){fprintf(f,",");if(r.extra[i]!=~(Uint64)0)fprintf(f,"%llu",(unsigned long long)r.extra[i]);}
            for(size_t i=0;i<g_perfTrace.sectionCount;i++)fprintf(f,",%.6f",perf_trace_ms(0,r.section[i]));
            fprintf(f,"\n");
        }
        fclose(f);
    } else if(!g_perfTrace.permanent)fprintf(stderr,"PERFTRACE|failed|cannot append frame rows\n");
    if(!g_perfTrace.permanent){char cfg[1200];snprintf(cfg,sizeof(cfg),"%s.settings-end.cfg",g_perfTrace.path);fx_save(&g_fx,cfg);}
    else if(g_perfTraceHooks.endSession)g_perfTraceHooks.endSession(g_perfTrace.session);
    if(g_perfTrace.gpuState==1)for(auto& q:g_perfTrace.queries){if(q.q[0])g_perfTrace.del(2,q.q);q=PerfTraceQuery();}
    g_perfTrace.currentQuery=-1;g_perfTrace.active=false;g_perfTrace.enabled=false;g_perfTrace.gpuState=0;
    std::vector<PerfTraceRow>().swap(g_perfTrace.rows);
    if(!g_perfTrace.permanent)fprintf(stderr,"PERFTRACE|end|rows=%zu|dropped=%llu|blocking_gpu_waits=0\n",g_perfTrace.count,(unsigned long long)g_perfTrace.dropped);
}
#endif
