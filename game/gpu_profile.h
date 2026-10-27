/* Optional GPU timestamps, independent of CPU submission timing. Eight frames of
   query storage; unavailable results are skipped, never waited for. No glFinish.
   ARB_timer_query is optional and missing support leaves gameplay untouched. */
#ifndef OPENCNC3D_GPU_PROFILE_H
#define OPENCNC3D_GPU_PROFILE_H
#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP 0x8E28
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif
struct GpuProfileFrame { GLuint query[64]={}; const char* names[64]={}; int n=0; bool pending=false; unsigned frame=0; };
static GpuProfileFrame g_gpFrames[8];
static void (APIENTRY *gp_gen)(GLsizei,GLuint*)=NULL;
static void (APIENTRY *gp_delete)(GLsizei,const GLuint*)=NULL;
static void (APIENTRY *gp_counter)(GLuint,GLenum)=NULL;
static void (APIENTRY *gp_available)(GLuint,GLenum,GLint*)=NULL;
static void (APIENTRY *gp_result)(GLuint,GLenum,unsigned long long*)=NULL;
static int g_gpOn=-1,g_gpCurrent=-1;
static bool g_gpDropFrame=false;
static unsigned g_gpFrame=0,g_gpSamples=0,g_gpSkipped=0;
static std::map<std::string,double> g_gpSum;
static void gpu_prof_report() {
    if(!g_gpSamples)return;
    printf("GPUFRAMEPROF|frames=%u|skipped=%u",g_gpSamples,g_gpSkipped);
    for(const auto& p:g_gpSum)printf("|%s=%.3f",p.first.c_str(),p.second/g_gpSamples);
    printf("\n");g_gpSum.clear();g_gpSamples=0;g_gpSkipped=0;
}
static void gpu_prof_collect(bool flush=false) {
    if(g_gpOn!=1)return;
    for(auto& f:g_gpFrames) {
        if(!f.pending||(!flush&&g_gpFrame-f.frame<3))continue;
        bool ready=true;
        for(int i=0;i<f.n;i++){GLint value=0;gp_available(f.query[i],GL_QUERY_RESULT_AVAILABLE,&value);if(!value){ready=false;break;}}
        if(!ready)continue;
        unsigned long long prev=0;
        for(int i=0;i<f.n;i++) {
            unsigned long long value=0;gp_result(f.query[i],GL_QUERY_RESULT,&value);
            if(i&&value>=prev)g_gpSum[f.names[i]]+=(double)(value-prev)*1.0e-6;
            prev=value;
        }
        f.pending=false;g_gpSamples++;
    }
    if(flush||g_gpSamples>=120)gpu_prof_report();
}
static void gpu_prof_mark(const char* name) {
    if(g_gpOn<0) {
        g_gpOn=0;const char* opt=getenv("OPENCNC3D_GPUPROF");if(!opt||!strcmp(opt,"0"))return;
        const char* ext=(const char*)glGetString(GL_EXTENSIONS);
        const char* version=(const char*)glGetString(GL_VERSION);int major=0,minor=0;
        if(version)sscanf(version,"%d.%d",&major,&minor);
        if((!ext||!strstr(ext,"GL_ARB_timer_query"))&&(major<3||(major==3&&minor<3))) {
            printf("GPUPROF|unavailable|ARB_timer_query not supported\n");return;
        }
        gp_gen=(decltype(gp_gen))SDL_GL_GetProcAddress("glGenQueries");
        gp_delete=(decltype(gp_delete))SDL_GL_GetProcAddress("glDeleteQueries");
        gp_counter=(decltype(gp_counter))SDL_GL_GetProcAddress("glQueryCounter");
        gp_available=(decltype(gp_available))SDL_GL_GetProcAddress("glGetQueryObjectiv");
        gp_result=(decltype(gp_result))SDL_GL_GetProcAddress("glGetQueryObjectui64v");
        if(!gp_gen||!gp_delete||!gp_counter||!gp_available||!gp_result){printf("GPUPROF|unavailable|entry point\n");return;}
        g_gpOn=1;printf("GPUPROF|timestamp|ring=8|blocking=0\n");
    }
    if(!g_gpOn||g_gpDropFrame)return;
    if(g_gpCurrent<0) {
        gpu_prof_collect();
        for(int i=0;i<8;i++)if(!g_gpFrames[i].pending){g_gpCurrent=i;break;}
        if(g_gpCurrent<0){g_gpDropFrame=true;return;}
        auto& f=g_gpFrames[g_gpCurrent];f.n=0;f.frame=g_gpFrame;
        if(!f.query[0])gp_gen(64,f.query);
    }
    auto& f=g_gpFrames[g_gpCurrent];
    if(f.n>=64)return;
    f.names[f.n]=name;gp_counter(f.query[f.n++],GL_TIMESTAMP);
}
static void gpu_prof_end() {
    if(g_gpOn!=1)return;
    if(g_gpCurrent>=0) {auto& f=g_gpFrames[g_gpCurrent];f.pending=f.n>1;g_gpCurrent=-1;}
    else g_gpSkipped++;
    g_gpDropFrame=false;g_gpFrame++;
}
static void gpu_prof_reset() {
    if(g_gpOn==1){gpu_prof_collect(true);for(auto& f:g_gpFrames)if(f.query[0])gp_delete(64,f.query);}
    for(auto& f:g_gpFrames)f=GpuProfileFrame();g_gpCurrent=-1;g_gpFrame=0;g_gpDropFrame=false;
    g_gpSum.clear();g_gpSamples=g_gpSkipped=0;g_gpOn=-1;
}
#endif
