/* Wheel disturbance in a Tiberium field. Host-only Enhanced VFX; no gameplay RNG,
   harvested quantity, field geometry, or model assets are changed. Include after
   the scene helpers and before draw_frame. The crystal facets and UVs are copied
   verbatim from disconnected shards of the CURRENT loaded tib3d pack. */
#ifndef CNC3D_HARVESTER_FX_MOD_H
#define CNC3D_HARVESTER_FX_MOD_H

enum { HFX_CRYSTAL, HFX_DUST, HFX_GRASS, HFX_SNOW, HFX_DIRT, HFX_KINDS, HFX_LIMIT = 768 };
/* Lighter rear-wheel spray: half the original birth rate, smaller fragments. */
static const float HFX_TRAVEL_STEP = 0.13f;
static const float HFX_REAR_CONTACT_Z = -0.34f;
struct HarvestFxShard { std::vector<Tib3dVert> v; float extent; };
struct HarvestFxParticle {
    float x,y,z,vx,vy,vz,age,life,size,angle,spin;
    unsigned seed;
    int kind, shard;
};
struct HarvestFxTrack { float x,z,travel; int frame, axle; };
static std::vector<HarvestFxShard> g_hfxShards;
static std::vector<HarvestFxParticle> g_hfxParticles;
static std::map<int, HarvestFxTrack> g_hfxTracks;
static int g_hfxFrame = -1, g_hfxEmitters = 0;
static unsigned g_hfxBirths[HFX_KINDS] = {0,0,0,0,0};
static bool g_hfxOn = true;
/* Draw-only diagnostic: stable bits crystal/dust/grass/snow/dirt = 1/2/4/8/16. */
static unsigned g_hfxDrawMask = (1u<<HFX_KINDS)-1u;
static GLuint g_hfxDustTex = 0, g_hfxSourceTex = 0;
static const Tib3dClump* g_hfxSourceClumps = NULL;

static bool harvestfx_enhanced(void)
{
    return g_fx.enabled && !g_fx.dos_classic && g_fx.tib3d && g_tib3dHave
        && !(g_pack.theater[0]=='R' && g_pack.theater[1]=='A');
}
static void harvestfx_reset(void)
{
    g_hfxParticles.clear(); g_hfxTracks.clear(); g_hfxShards.clear(); g_hfxFrame=-1;
    g_hfxSourceTex=0; g_hfxSourceClumps=NULL;
    g_hfxEmitters=0;
    g_hfxDrawMask=(1u<<HFX_KINDS)-1u;
    for (int i=0;i<HFX_KINDS;i++) g_hfxBirths[i]=0;
}
static float hfx_random(unsigned h) { return (h & 65535u) / 65535.0f; }

/* Position-weld only to discover connected pieces: the art has split normals/UVs
   at its facets, so index connectivity alone would yield individual triangles.
   Keep the original per-corner data, including the current atlas UVs. */
struct HarvestFxPoint {
    int x,y,z;
    bool operator<(const HarvestFxPoint& b) const {
        if (x!=b.x) return x<b.x; if (y!=b.y) return y<b.y; return z<b.z;
    }
};
static int hfx_root(std::vector<int>& p, int n)
{
    while (p[n]!=n) { p[n]=p[p[n]]; n=p[n]; } return n;
}
static void harvestfx_shards(void)
{
    const Tib3dClump* source=g_tib3dClump.empty()?NULL:&g_tib3dClump[0];
    if (!g_hfxShards.empty() && g_hfxSourceTex==g_tib3dTex && g_hfxSourceClumps==source) return;
    g_hfxShards.clear(); g_hfxParticles.clear();
    g_hfxSourceTex=g_tib3dTex; g_hfxSourceClumps=source;
    for (size_t ci=0;ci<g_tib3dClump.size();ci++) {
        const Tib3dClump& c=g_tib3dClump[ci];
        if (c.stage<=0 || c.vert.empty()) continue;  /* never throw the soil pod */
        std::vector<int> parent(c.vert.size());
        std::map<HarvestFxPoint,int> weld;
        for (size_t j=0;j<c.vert.size();j++) {
            parent[j]=(int)j;
            const Tib3dVert& v=c.vert[j];
            HarvestFxPoint k={(int)floorf(v.x*100000.0f+0.5f),
                              (int)floorf(v.y*100000.0f+0.5f),
                              (int)floorf(v.z*100000.0f+0.5f)};
            std::map<HarvestFxPoint,int>::iterator it=weld.find(k);
            if (it==weld.end()) weld[k]=(int)j;
            else parent[j]=hfx_root(parent,it->second);
        }
        for (size_t j=0;j+2<c.idx.size();j+=3) {
            int a=hfx_root(parent,(int)c.idx[j]);
            for (int q=1;q<3;q++) parent[hfx_root(parent,(int)c.idx[j+q])]=a;
        }
        std::map<int,std::vector<Tib3dVert> > pieces;
        for (size_t j=0;j<c.idx.size();j++)
            pieces[hfx_root(parent,(int)c.idx[j])].push_back(c.vert[c.idx[j]]);
        for (std::map<int,std::vector<Tib3dVert> >::iterator it=pieces.begin();it!=pieces.end();++it) {
            if (it->second.size()<12) continue;
            HarvestFxShard s; s.v.swap(it->second);
            float lo[3]={1e9f,1e9f,1e9f}, hi[3]={-1e9f,-1e9f,-1e9f};
            for (size_t j=0;j<s.v.size();j++) {
                const float p[3]={s.v[j].x,s.v[j].y,s.v[j].z};
                for (int q=0;q<3;q++) { if(p[q]<lo[q])lo[q]=p[q]; if(p[q]>hi[q])hi[q]=p[q]; }
            }
            s.extent=0;
            for (int q=0;q<3;q++) if(hi[q]-lo[q]>s.extent)s.extent=hi[q]-lo[q];
            if (s.extent<0.00001f) continue;
            for (size_t j=0;j<s.v.size();j++) {
                s.v[j].x-=(lo[0]+hi[0])*0.5f;
                s.v[j].y-=(lo[1]+hi[1])*0.5f;
                s.v[j].z-=(lo[2]+hi[2])*0.5f;
            }
            g_hfxShards.push_back(s);
        }
    }
    fprintf(stderr,"HARVESTFXART|shards=%zu|source=current-tib3d-pack|texture=current-tib3d-atlas\n",g_hfxShards.size());
}

static void hfx_spawn(int kind,float x,float z,float dx,float dz,float side,unsigned seed)
{
    if (g_hfxParticles.size()>=HFX_LIMIT || (kind==HFX_CRYSTAL && g_hfxShards.empty())) return;
    const float a=hfx_random(efx_hash(seed,11u,37u,1u));
    const float b=hfx_random(efx_hash(seed,19u,53u,2u));
    const float c=hfx_random(efx_hash(seed,31u,71u,3u));
    HarvestFxParticle p={};
    p.kind=kind; p.seed=seed; p.x=x; p.z=z; p.y=terrain_y(x,z)+0.045f;
    p.vx=dz*side*(0.24f+0.28f*a)-dx*(0.22f+0.33f*b);
    p.vz=-dx*side*(0.24f+0.28f*a)-dz*(0.22f+0.33f*b);
    p.vy=0.55f+0.65f*c;
    p.angle=a*6.2831853f; p.spin=(b-0.5f)*18.0f;
    p.life=0.55f+0.30f*b; p.size=0.04875f+0.05625f*a;
    if (kind==HFX_CRYSTAL) p.shard=(int)(seed%g_hfxShards.size());
    if (kind==HFX_DUST || kind==HFX_SNOW) {
        p.size=(kind==HFX_DUST?0.085f:0.060f)+0.040f*a;
        p.y=terrain_y(x,z)+0.075f; p.vy=0.14f+0.16f*c;
        p.vx*=0.45f; p.vz*=0.45f; p.life=0.65f+0.35f*b;
    }
    if (kind==HFX_GRASS) { p.size=0.032f+0.028f*a; p.life=0.45f+0.35f*b; }
    if (kind==HFX_DIRT) {
        const bool dry=!strcmp(g_pack.theater,"DESERT") || !strcmp(g_pack.theater,"SAND");
        p.size=(dry?0.018f:0.026f)+(dry?0.018f:0.022f)*a;
        p.vy=0.35f+0.45f*c; p.life=0.45f+0.25f*b;
    }
    g_hfxParticles.push_back(p); g_hfxBirths[kind]++;
}

/* Once per ENGINE frame, never per rendered frame. Actual distance rather than
   Harvest mission/IsHarvesting: collecting while stationary must not kick wheels.
   Each wheel contact must be on TIB| engine truth, including along diagonal moves. */
static void harvestfx_step(int frame)
{
    if (frame==g_hfxFrame) return;
    if (frame<g_hfxFrame || (g_hfxFrame>=0 && frame-g_hfxFrame>5)) harvestfx_reset();
    const float dt=(g_hfxFrame>=0 ? frame-g_hfxFrame : 1)*(1.0f/15.0f);
    g_hfxFrame=frame; g_hfxEmitters=0;
    const bool on=g_hfxOn && harvestfx_enhanced();
    if (!on) g_hfxParticles.clear();
    size_t w=0;
    for (size_t i=0;i<g_hfxParticles.size();i++) {
        HarvestFxParticle p=g_hfxParticles[i]; p.age+=dt;
        if (p.age>=p.life) continue;
        p.x+=p.vx*dt; p.y+=p.vy*dt; p.z+=p.vz*dt; p.angle+=p.spin*dt;
        if (p.kind==HFX_DUST || p.kind==HFX_SNOW) {
            p.vx*=0.94f; p.vz*=0.94f; p.vy*=0.97f;
        } else {
            p.vy-=3.8f*dt;
            const float ground=terrain_y(p.x,p.z)+0.015f;
            if (p.y<ground) {
                p.y=ground; p.vy=fabsf(p.vy)*0.26f;
                p.vx*=0.48f; p.vz*=0.48f; p.spin*=0.5f;
            }
        }
        g_hfxParticles[w++]=p;
    }
    g_hfxParticles.resize(w);
    std::map<int,HarvestFxTrack> now;
    std::vector<unsigned char> field;
    if (on) {
        harvestfx_shards();
        field.assign((size_t)g_gridW*g_gridH,0);
        for (size_t i=0;i<g_tib.size();i++) {
            const TibCell& t=g_tib[i];
            if(t.x>=0 && t.y>=0 && t.x<g_gridW && t.y<g_gridH) field[(size_t)t.y*g_gridW+t.x]=1;
        }
    }
    const bool snow=!strcmp(g_pack.theater,"SNOW");
    const bool desert=!strcmp(g_pack.theater,"DESERT") || !strcmp(g_pack.theater,"SAND");
    for (size_t i=0;i<g_objects.size();i++) {
        const SimObject& o=g_objects[i];
        if(o.kind!=K_UNIT || strcmp(o.type,"HARV") || o.id<0 || o.limbo) continue;
        HarvestFxTrack n={o.ex,o.ez,0,frame,0};
        std::map<int,HarvestFxTrack>::iterator it=g_hfxTracks.find(o.id);
        if(it!=g_hfxTracks.end() && frame-it->second.frame<=2) {
            const HarvestFxTrack& old=it->second;
            const float dx=o.ex-old.x, dz=o.ez-old.z, dist=sqrtf(dx*dx+dz*dz);
            n.axle=old.axle;
            if(on && dist>0.0005f && dist<0.5f && visible(o)) {
                const float fx=dx/dist,fz=dz/dist;
                n.travel=old.travel+dist;
                const size_t before=g_hfxParticles.size();
                int samples=0;
                while(n.travel>=HFX_TRAVEL_STEP && samples++<8) {
                    n.travel-=HFX_TRAVEL_STEP;
                    float back=n.travel; if(back>dist)back=dist;
                    /* Measured rear axle Z -.295: seat the spray .045 cells
                       behind it, at the outer belts. No front-wheel emitter. */
                    const float axle=HFX_REAR_CONTACT_Z;
                    n.axle++;
                    float sn,cs;facing_rot(evhc_face(o),&sn,&cs);
                    for(int s=-1;s<=1;s+=2) {
                        const float x=o.ex-fx*back+s*0.29f*cs+axle*sn;
                        const float z=o.ez-fz*back-s*0.29f*sn+axle*cs;
                        const int cx=(int)floorf(x),cy=(int)floorf(z);
                        if(cx<0 || cy<0 || cx>=g_gridW || cy>=g_gridH || !field[(size_t)cy*g_gridW+cx])continue;
                        const unsigned seed=efx_hash((unsigned)o.id,(unsigned)frame,(unsigned)n.axle,(unsigned)(s+2));
                        hfx_spawn(HFX_CRYSTAL,x,z,fx,fz,(float)s,seed);
                        hfx_spawn(snow?HFX_SNOW:HFX_DUST,x,z,fx,fz,(float)s,seed^0x5123u);
                        if(!desert && !snow) {
                            hfx_spawn(HFX_GRASS,x,z,fx,fz,(float)s,seed^0x8241u);
                        }
                        /* Swap the second grass speck for earthy clods. Dry
                           theaters add only an occasional loose sand grain. */
                        if(!snow && (!desert || (n.axle&1)))
                            hfx_spawn(HFX_DIRT,x,z,fx,fz,(float)s,seed^0x1937u);
                    }
                }
                if(g_hfxParticles.size()!=before) g_hfxEmitters++;
            }
        }
        now[o.id]=n;
    }
    g_hfxTracks.swap(now);
}

static float hfx_visibility(float x,float z)
{
    const int cx=(int)floorf(x),cy=(int)floorf(z);
    if(!cell_shown(cx,cy) || shroud_cell_hidden(cx,cy)) return 0;
    return shroud_vis_at(x,z);
}
static void hfx_dust_texture(void)
{
    if(g_hfxDustTex)return;
    unsigned char px[32*32*4];
    for(int y=0;y<32;y++)for(int x=0;x<32;x++) {
        const float dx=(x-15.5f)/15.5f,dy=(y-15.5f)/15.5f;
        float a=1.0f-dx*dx-dy*dy;if(a<0)a=0;
        const int j=(y*32+x)*4;px[j]=px[j+1]=px[j+2]=255;
        px[j+3]=(unsigned char)(a*a*210);
    }
    glGenTextures(1,&g_hfxDustTex);glBindTexture(GL_TEXTURE_2D,g_hfxDustTex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,32,32,0,GL_RGBA,GL_UNSIGNED_BYTE,px);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
}
static void hfx_rotation(float a,float b,float* m)
{
    const float ca=cosf(a),sa=sinf(a),cb=cosf(b),sb=sinf(b);
    m[0]=cb; m[1]=sa*sb; m[2]=ca*sb;
    m[3]=0; m[4]=ca; m[5]=-sa;
    m[6]=-sb; m[7]=sa*cb; m[8]=ca*cb;
}
static void hfx_transform(float x,float y,float z,const float* m,float& ox,float& oy,float& oz)
{
    ox=m[0]*x+m[1]*y+m[2]*z;
    oy=m[3]*x+m[4]*y+m[5]*z;
    oz=m[6]*x+m[7]*y+m[8]*z;
}
static void harvestfx_draw(const float* R,const float* U)
{
    if(!g_hfxOn || !harvestfx_enhanced() || g_hfxParticles.empty())return;
    if(g_hfxSourceTex!=g_tib3dTex) { g_hfxParticles.clear(); return; }
    /* World pass, before rain/shroud/post fog and before the HUD. Restore the
       caller's complete fixed-function state; use no private lighting shader. */
    glPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_TEXTURE_BIT|GL_CURRENT_BIT);
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_ALPHA_TEST);glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D,g_tib3dTex);
    /* Atlas alpha is emissive, NOT coverage. Primary alpha carries the life fade. */
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_COMBINE);
    glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_RGB,GL_MODULATE);
    glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_RGB,GL_TEXTURE);
    glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_RGB,GL_PRIMARY_COLOR);
    glTexEnvi(GL_TEXTURE_ENV,GL_OPERAND0_RGB,GL_SRC_COLOR);
    glTexEnvi(GL_TEXTURE_ENV,GL_OPERAND1_RGB,GL_SRC_COLOR);
    glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_ALPHA,GL_REPLACE);
    glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_ALPHA,GL_PRIMARY_COLOR);
    glTexEnvi(GL_TEXTURE_ENV,GL_OPERAND0_ALPHA,GL_SRC_ALPHA);
    GLint gradeWas=0;
    if(fx_gl_ready)glGetIntegerv(GL_CURRENT_PROGRAM,&gradeWas);
    const bool graded=efx_grade_begin(true);
    /* This atlas stores emissive in alpha; life fade owns coverage for shards. */
    if(graded)efx_grade_alpha(false);
    fx_sun_dir();
    const float ahead=g_tickAlpha*(1.0f/15.0f);
    glBegin(GL_TRIANGLES);
    for(size_t i=0;i<g_hfxParticles.size();i++) {
        const HarvestFxParticle& p=g_hfxParticles[i];
        if(p.kind!=HFX_CRYSTAL || !(g_hfxDrawMask&(1u<<p.kind)))continue;
        const float x=p.x+p.vx*ahead,z=p.z+p.vz*ahead;
        const float vis=hfx_visibility(x,z);if(vis<=0)continue;
        const float fade=fminf(1.0f,(p.life-p.age-ahead)*6.0f);if(fade<=0)continue;
        const HarvestFxShard& s=g_hfxShards[p.shard];
        const float scale=p.size/s.extent;
        const float a=p.angle+p.spin*ahead,b=(p.seed&255)*0.0246f+a*0.63f;
        float m[9];hfx_rotation(a,b,m);
        for(size_t j=0;j<s.v.size();j++) {
            const Tib3dVert& v=s.v[j];float vx,vy,vz,nx,ny,nz;
            hfx_transform(v.x,v.y,v.z,m,vx,vy,vz);
            hfx_transform(v.nx,v.ny,v.nz,m,nx,ny,nz);
            const float nd=fmaxf(0.0f,-nx*g_fxSunDir[0]-ny*g_fxSunDir[1]-nz*g_fxSunDir[2]);
            const float light=(0.60f+0.40f*nd)*vis;
            glColor4f(light,light,light,fade);
            glTexCoord2f(v.u,v.v);
            glVertex3f(x+vx*scale,p.y+p.vy*ahead+vy*scale,z+vz*scale);
        }
    }
    glEnd();
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    hfx_dust_texture();glBindTexture(GL_TEXTURE_2D,g_hfxDustTex);
    if(graded)efx_grade_alpha(true);
    glBegin(GL_QUADS);
    for(size_t i=0;i<g_hfxParticles.size();i++) {
        const HarvestFxParticle& p=g_hfxParticles[i];
        if((p.kind!=HFX_DUST && p.kind!=HFX_SNOW) || !(g_hfxDrawMask&(1u<<p.kind)))continue;
        const float x=p.x+p.vx*ahead,z=p.z+p.vz*ahead;
        const float vis=hfx_visibility(x,z);if(vis<=0)continue;
        const float u=fminf(1.0f,(p.age+ahead)/p.life);
        const bool dry=!strcmp(g_pack.theater,"DESERT") || !strcmp(g_pack.theater,"SAND");
        const float alpha=sinf(u*3.14159265f)*(p.kind==HFX_SNOW?0.22f:(dry?0.28f:0.24f))*vis;
        const float r=p.size*(1.0f+1.35f*u),y=p.y+p.vy*ahead;
        if(p.kind==HFX_SNOW)glColor4f(0.76f*vis,0.83f*vis,0.88f*vis,alpha);
        else {
            glColor4f((dry?0.68f:0.34f)*vis,(dry?0.53f:0.28f)*vis,(dry?0.34f:0.18f)*vis,alpha);
        }
        const float q[4][2]={{-1,-1},{1,-1},{1,1},{-1,1}};
        for(int j=0;j<4;j++) {
            glTexCoord2f((q[j][0]+1)*0.5f,(q[j][1]+1)*0.5f);
            glVertex3f(x+r*(R[0]*q[j][0]+U[0]*q[j][1]),y+r*(R[1]*q[j][0]+U[1]*q[j][1]),z+r*(R[2]*q[j][0]+U[2]*q[j][1]));
        }
    }
    glEnd();glDisable(GL_TEXTURE_2D);
    if(graded)efx_grade_textured(false);
    glBegin(GL_TRIANGLES);
    for(size_t i=0;i<g_hfxParticles.size();i++) {
        const HarvestFxParticle& p=g_hfxParticles[i];
        if((p.kind!=HFX_GRASS && p.kind!=HFX_DIRT) || !(g_hfxDrawMask&(1u<<p.kind)))continue;
        const float x=p.x+p.vx*ahead,z=p.z+p.vz*ahead,vis=hfx_visibility(x,z);if(vis<=0)continue;
        const float fade=fminf(1.0f,(p.life-p.age-ahead)*5.0f);if(fade<=0)continue;
        const bool winter=!strcmp(g_pack.theater,"WINTER");
        const bool dry=!strcmp(g_pack.theater,"DESERT") || !strcmp(g_pack.theater,"SAND");
        const float tone=0.75f+0.25f*hfx_random(p.seed);
        float m[9];hfx_rotation(p.angle+p.spin*ahead,p.angle*0.7f,m);
        if(p.kind==HFX_GRASS) {
            glColor4f((winter?0.34f:0.22f)*tone*vis,(winter?0.31f:0.34f)*tone*vis,0.10f*tone*vis,fade);
            const float q[3][2]={{-0.36f,-0.5f},{0.36f,-0.5f},{0,1.0f}};
            /* Crossed blades keep a small tuft legible as it tumbles edge-on. */
            for(int blade=0;blade<2;blade++)for(int j=0;j<3;j++) {
                const float side=q[j][0]*p.size;
                float vx,vy,vz;hfx_transform(blade?0:side,q[j][1]*p.size,blade?side:0,m,vx,vy,vz);
                glVertex3f(x+vx,p.y+p.vy*ahead+vy,z+vz);
            }
        } else {
            glColor4f((dry?0.55f:0.25f)*tone*vis,(dry?0.37f:0.17f)*tone*vis,(dry?0.16f:0.08f)*tone*vis,fade);
            const float q[4][2]={{-0.48f,-0.25f},{0.22f,-0.46f},{0.54f,0.18f},{-0.18f,0.48f}};
            const int idx[6]={0,1,2,0,2,3};
            for(int j=0;j<6;j++) {
                float vx,vy,vz;hfx_transform(q[idx[j]][0]*p.size,q[idx[j]][1]*p.size,0,m,vx,vy,vz);
                glVertex3f(x+vx,p.y+p.vy*ahead+vy,z+vz);
            }
        }
    }
    glEnd();
    if(graded)fx_glUseProgram((GLuint)gradeWas);
    glPopAttrib();
}
static void harvestfx_dump(void)
{
    unsigned hash=2166136261u;
    unsigned live[HFX_KINDS]={0,0,0,0,0},births=0;
    for(int i=0;i<HFX_KINDS;i++)births+=g_hfxBirths[i];
    for(size_t i=0;i<g_hfxParticles.size();i++) {
        const HarvestFxParticle& p=g_hfxParticles[i];
        live[p.kind]++;
        const float v[5]={p.x,p.y,p.z,p.age,p.angle};
        for(int j=0;j<5;j++) { unsigned b=0;memcpy(&b,&v[j],4);hash=(hash^b)*16777619u; }
        hash=(hash^p.seed)*16777619u;
    }
    printf("HARVESTFX|enabled=%d|enhanced=%d|active=%zu|emitters=%d|births=%u|crystal=%u|dust=%u|grass=%u|snow=%u|dirt=%u|statehash=%08x|drawmask=%u|live_crystal=%u|live_dust=%u|live_grass=%u|live_snow=%u|live_dirt=%u\n",
        g_hfxOn?1:0,harvestfx_enhanced()?1:0,g_hfxParticles.size(),g_hfxEmitters,
        births,g_hfxBirths[0],g_hfxBirths[1],g_hfxBirths[2],g_hfxBirths[3],g_hfxBirths[4],hash,
        g_hfxDrawMask,live[0],live[1],live[2],live[3],live[4]);
}
#endif
