/* Script-only fidelity evidence. Capture decoded target values after a normal
   frame; no alternate render and no access from interactive gameplay. */
static bool render_capture_target(const char* prefix,const char* name,const FxRT& rt,bool depth) {
    const GLuint texture=depth?rt.depth:rt.tex;
    if(!texture||rt.w<=0||rt.h<=0)return true;
    /* Float readback preserves every RGBA16F value and every depth24 value.
       An 8-bit readback would hide HDR/normal differences below one byte step. */
    const size_t bytes=(size_t)rt.w*(size_t)rt.h*(depth?1:4)*sizeof(float);
    if(bytes>256u*1024u*1024u)return false;
    std::vector<unsigned char> pixels(bytes);
    glBindTexture(GL_TEXTURE_2D,texture);
    glGetTexImage(GL_TEXTURE_2D,0,depth?GL_DEPTH_COMPONENT:GL_RGBA,
                  GL_FLOAT,pixels.data());
    char path[2048];snprintf(path,sizeof path,"%s-%s.bin.gz",prefix,name);
    gzFile f=gzopen(path,"wb1");if(!f)return false;
    const int header[3]={rt.w,rt.h,depth?1:2};
    const bool ok=gzwrite(f,header,sizeof header)==sizeof header && gzwrite(f,pixels.data(),(unsigned)bytes)==(int)bytes;
    return gzclose(f)==Z_OK && ok;
}
static bool render_capture(const char* prefix) {
    GLint active=GL_TEXTURE0,bound=0,align=4;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
    glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);
    glGetIntegerv(GL_PACK_ALIGNMENT,&align);glPixelStorei(GL_PACK_ALIGNMENT,1);
    bool ok=true;
    ok=render_capture_target(prefix,"scene",g_fxScene,false)&&ok;
    ok=render_capture_target(prefix,"depth",g_fxScene,true)&&ok;
    ok=render_capture_target(prefix,"normals",g_fxGNorm,false)&&ok;
    ok=render_capture_target(prefix,"bake",g_fxBake,false)&&ok;
    ok=render_capture_target(prefix,"sun",g_fxShadow,true)&&ok;
    ok=render_capture_target(prefix,"terrain-sun",g_fxTerrainShadow,true)&&ok;
    ok=render_capture_target(prefix,"infantry-sun",g_fxInfShadow,true)&&ok;
    ok=render_capture_target(prefix,"reflection",g_waterReflRT,false)&&ok;
    char metaPath[2048];snprintf(metaPath,sizeof metaPath,"%s-matrices.json",prefix);
    FILE* meta=fopen(metaPath,"w");
    if(meta) {
        fprintf(meta,"{\"terrain_strength\":%.9g,\"sun_span\":%.9g,\"sun_radius\":%.9g",fx_terrain_shadow_strength(),g_fxSunSpan,g_fxSunR);
        const char* keys[]={"inverse_vp","sun_vp","sun_direction"};
        const float* values[]={g_fxInvVP,g_fxSunVP,g_fxSunDir};
        for(int k=0;k<3;k++) {
            fprintf(meta,",\"%s\":[",keys[k]);
            for(int j=0;j<(k==2?3:16);j++)fprintf(meta,"%s%.9g",j?",":"",values[k][j]);
            fprintf(meta,"]");
        }
        /* the soldiers' map and the sun's receiver windows (fx_post.h), so a texel that
           differs between two captures can be checked against every pixel's reach */
        fprintf(meta,",\"inf_sun_vp\":[");
        for(int j=0;j<16;j++)fprintf(meta,"%s%.9g",j?",":"",g_fxInfSunVP[j]);
        fprintf(meta,"],\"inf_radius\":%.9g,\"inf_live\":%d,\"shadow_soft\":%.9g,\"shadow_pen\":%.9g,"
                     "\"shadow_noff\":%.9g,\"recv_floor\":%.9g,\"recv_floor_ok\":%d",
                g_fxInfR,g_fxInfLive,g_fx.shadow_soft,g_fx.shadow_pen,g_fx.shadow_noff,
                g_fxRecvFloor,g_fxRecvFloorOk);
        const char* wkeys[]={"sun_window","inf_window"};
        const FxRecvWin* wins[]={&g_fxRecvSun,&g_fxRecvInf};
        for(int k=0;k<2;k++) {
            const FxRecvWin& w=*wins[k];
            fprintf(meta,",\"%s\":{\"on\":%d,\"pad\":%.12g,\"need\":%.12g,\"hull\":[",wkeys[k],w.on,w.m,w.need);
            for(int j=0;j<w.nh;j++)fprintf(meta,"%s[%.12g,%.12g]",j?",":"",w.hx[j],w.hy[j]);
            fprintf(meta,"]}");
        }
        fprintf(meta,"}\n");fclose(meta);
    } else ok=false;
    glPixelStorei(GL_PACK_ALIGNMENT,align);glBindTexture(GL_TEXTURE_2D,(GLuint)bound);
    glActiveTexture((GLenum)active);printf("RENDERCAPTURE|%s|%s\n",prefix,ok?"ok":"failed");return ok;
}
static void physics_capture() {
    uint64_t hash=1469598103934665603ull;
    auto add=[&](const void* p,size_t n){const unsigned char* b=(const unsigned char*)p;for(size_t i=0;i<n;i++){hash^=b[i];hash*=1099511628211ull;}};
    for(const auto& item:g_irBody) {
        const InfRag& b=item.second;add(&item.first,sizeof item.first);
        add(b.p,sizeof b.p);add(b.pp,sizeof b.pp);add(&b.cut,sizeof b.cut);
        add(&b.state,sizeof b.state);add(&b.still,sizeof b.still);add(&b.overBy,sizeof b.overBy);
        add(&b.bloodBorn,sizeof b.bloodBorn);add(&b.bx,sizeof b.bx);add(&b.bz,sizeof b.bz);
    }
    printf("PHYSICSCAPTURE|frame=%d|bodies=%zu|hash=%016llx\n",g_engineFrame,g_irBody.size(),(unsigned long long)hash);
}
