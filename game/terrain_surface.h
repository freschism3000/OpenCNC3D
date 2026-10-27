#ifndef CNC3D_TERRAIN_SURFACE_H
#define CNC3D_TERRAIN_SURFACE_H

/* Enhanced terrain detail is real bounded displacement over the native heightfield.
   A saved physical-rock matte supplies height. Every pass uses this one cached mesh;
   there is no shadow-only cliff proxy. Missing packs leave the native surface intact.
   C3DSURF\0, LE u32 version, cell, count; keys name[16],tid/u16,icon/u16,
   variant/u8,game/u8,reserved/u16; then one L8 height tile per key. */
struct TerrainSurfaceCell {
    int x=0,z=0,tile=-1;
    float uv[4]={0,0,0,0};
};
struct TerrainSurfaceVert { float p[3],uv[2],n[4],mu[2]; };
struct TerrainSurfaceRecord { int x,z,first,count; float light[4]; bool known,active; };
/* THE HALF OF A BUILD THAT NO BUILDING PAD CAN CHANGE. A placement, sale or loss
   anywhere on the map moves g_padEpoch, and with it the surface key; what follows
   from the pads is the lift's pad test and everything after it. The coast cut (one
   readback of the whole terrain atlas), the distance transform and its taper, and
   each node's raw template height are all functions of the cells, the atlas texels
   and the relief height alone, so they are kept here and reused while those match.
   The atlas texels are covered by the GL state cache's write watch: any upload,
   copy or delete that reaches the atlas name moves g_glsWatchGen. */
struct TerrainSurfaceBase {
    bool valid=false;
    std::string key;                       // the surface key without the pad epoch
    GLuint atlas=0;unsigned coastGen=0;
    int steps=0,gridW=0,gridH=0;
    std::vector<TerrainSurfaceCell> cells; // compared whole
    std::vector<float> height;             // land ? template height*relief : 0, per node of each templated cell, build order
    std::vector<unsigned char> water;      // the coast cut per lattice node; empty without an atlas
    std::vector<float> taper;              // the land-side taper per lattice node, beside water
    float width=0;
};
struct TerrainSurfaceState {
    std::string theater,key;
    int cell=0,steps=8,fw=0,fh=0;
    unsigned epoch=0,gen=0,pad=0;
    bool tried=false,ready=false,failed=false;
    std::vector<unsigned char> tiles;
    std::map<unsigned,int> td;
    std::map<std::string,int> ra;
    std::vector<float> lift;
    std::vector<TerrainSurfaceCell> source;
    std::vector<TerrainSurfaceVert> vertices;
    std::vector<float> colour;
    std::vector<TerrainSurfaceRecord> records;
    GLuint geo=0,col=0,cb=0,material=0;
    int materialCell=0,materialCols=0,materialRows=0;
    std::vector<float> cbCoordinates;
    std::vector<int> recordAt,cbMasks;
    std::vector<float> cbEdges;
    long triangles=0;
    TerrainSurfaceBase base;
    /* SLOTS (OPENCNC3D_SURFACE_SLOTS). A full build gives every cell that is raised
       with the pads left out a record of its own, in cell order, whether or not it is
       raised now; an inactive record is never drawn, so the triangles drawn and their
       order are the full build's. A pad can only take lift away, so no pad can raise a
       cell without a record, and a pad change re-lays only the records whose inputs
       moved. slotAt is each cell's record or -1; emitCorners the padded corners the
       records were laid from. */
    bool slotted=false;
    std::vector<int> slotAt;
    std::vector<unsigned char> emitCorners;
    /* For the local lift (terrain_surface_tail_local): each cell's place in the build's
       cell order and its first node in base.height (-1 without a template), and which
       cells had a padded corner when the lift was last computed. */
    std::vector<int> cellOrder,cellHeight;
    std::vector<unsigned char> padCells;
    bool orderOk=false;
    /* The lift nodes the last patch changed, and the epochs it moved the surface from
       and to, so ground_rev_sync need not compare the whole lattice again. */
    std::vector<int> liftMoved;
    unsigned liftFrom=0,liftTo=0;
};
static TerrainSurfaceState g_terrainSurface;
static bool terrain_surface_on(void)
{ return fx_unified_lighting() && g_fx.terrain_relief && fx_texset_get()==FX_TEX_ENH && g_fx.terrain_relief_height>0.0f; }
static bool terrain_surface_bed_bias(void)
{ return terrain_surface_on() && g_terrainSurface.ready; }
// A successfully uploaded cell owns the visible surface in every pass. The
// native two-triangle slab is a fallback, never a second layer underneath it.
static bool terrain_surface_has_cell(int x,int z)
{
    const auto& S=g_terrainSurface;
    return terrain_surface_on() && S.ready && S.geo && S.col && x>=0 && z>=0
        && x<g_gridW && z<g_gridH && S.recordAt.size()==(size_t)g_gridW*g_gridH
        && S.recordAt[(size_t)z*g_gridW+x]>=0;
}
static std::string terrain_surface_theater(void)
{
    if(g_raModule) return !strcmp(g_pack.theater,"RASNOW")?"RA_SNOW":!strcmp(g_pack.theater,"RATEMP")?"RA_TEMPERAT":"RA_INTERIOR";
    return g_pack.theater;
}
static void terrain_surface_release_geometry(void)
{
    TerrainSurfaceState& S=g_terrainSurface;
    if(grass_buf_load()) { if(S.geo)gr_glDeleteBuffers(1,&S.geo);if(S.col)gr_glDeleteBuffers(1,&S.col);if(S.cb)gr_glDeleteBuffers(1,&S.cb); }
    S.geo=S.col=S.cb=0;S.cbCoordinates.clear();S.recordAt.clear();S.cbMasks.clear();S.cbEdges.clear();S.ready=false;S.key.clear();S.lift.clear();S.vertices.clear();S.colour.clear();S.records.clear();S.source.clear();
    S.slotted=false;S.slotAt.clear();S.emitCorners.clear();S.cellOrder.clear();S.cellHeight.clear();S.padCells.clear();S.orderOk=false;
}
static void terrain_surface_free(void)
{ terrain_surface_release_geometry();if(g_terrainSurface.material)glDeleteTextures(1,&g_terrainSurface.material);g_terrainSurface=TerrainSurfaceState(); }
/* C3DMATL uses the exact C3DSURF key order and separately zipped linear material
   data: tangent normal XY, roughness, explicit mineral coverage. Two-texel edge
   gutters prevent a neighbouring template from bleeding into the normal target. */
static void terrain_surface_material_load(const std::vector<unsigned char>& surface)
{
    auto& S=g_terrainSurface;if(!stone_maps_enabled())return;
    std::vector<unsigned char> b;const char* roots[]={"","game/","../game/","ra3d/","../"};bool found=false;
    /* Prefer an explicit SAND file across every root. Its identical Desert donor
       is considered only when no explicit file exists, never after corruption. */
    for(int donor=0;donor<(S.theater=="SAND"?2:1)&&!found;donor++)for(auto root:roots){const char* dir=getenv("OPENCNC3D_TERRAIN_SURFACE_DIR");
        std::string path=(dir&&*dir?std::string(dir)+"/":std::string(root))+"terrainmaterial_"+(donor?"DESERT":S.theater)+".pack";
        FILE* f=fopen(path.c_str(),"rb");if(!f)continue;found=true;fseek(f,0,SEEK_END);long n=ftell(f);
        if(n>=24&&n<=256L*1024*1024&&fseek(f,0,SEEK_SET)==0){b.resize(n);if(fread(b.data(),1,n,f)!=(size_t)n)b.clear();}fclose(f);break;
    }
    if(b.size()<24||memcmp(b.data(),"C3DMATL\0",8)||stone_u32(b.data()+8)!=1)return;
    unsigned cell=stone_u32(b.data()+12),count=stone_u32(b.data()+16);size_t head=20+(size_t)count*24;
    if(cell<8||cell>144||!count||count!=stone_u32(surface.data()+16)||head+4>b.size()
       ||memcmp(b.data()+20,surface.data()+20,count*24))return;
    const unsigned packed=stone_u32(b.data()+head);if(head+4+packed!=b.size())return;
    const size_t size=(size_t)cell*cell*count*4;if(size>256u*1024u*1024u)return;
    std::vector<unsigned char> pixels(size);uLongf decoded=size;
    if(uncompress(pixels.data(),&decoded,b.data()+head+4,packed)!=Z_OK||decoded!=size)return;
    const int pitch=cell+4,cols=(int)ceil(sqrt((double)count+1)),rows=(count+1+cols-1)/cols;
    GLint limit=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);if(cols*pitch>limit||rows*pitch>limit)return;
    const int width=cols*pitch,height=rows*pitch;std::vector<unsigned char> atlas((size_t)width*height*4,0);
    for(unsigned i=0;i<count;i++)for(int z=-2;z<(int)cell+2;z++)for(int x=-2;x<(int)cell+2;x++){
        const int sx=std::max(0,std::min((int)cell-1,x)),sz=std::max(0,std::min((int)cell-1,z));
        const size_t dst=((size_t)((i/cols)*pitch+z+2)*width+(i%cols)*pitch+x+2)*4;
        memcpy(&atlas[dst],&pixels[((size_t)i*cell*cell+sz*cell+sx)*4],4);
    }
    GLint was=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&was);glGenTextures(1,&S.material);glBindTexture(GL_TEXTURE_2D,S.material);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,atlas.data());glBindTexture(GL_TEXTURE_2D,was);
    S.materialCell=cell;S.materialCols=cols;S.materialRows=rows;
    fprintf(stderr,"STONEMATERIAL|terrain|%s|tiles=%u|cell=%u|atlas=%dx%d\n",S.theater.c_str(),count,cell,width,height);
}
static GLuint terrain_surface_normal_program(void)
{
    static GLuint program=0;static bool tried=false;if(program||tried)return program;tried=true;
    const char* vs="#version 120\nvarying vec3 W;varying vec4 C;varying vec2 U;varying vec2 A;\nvoid main(){W=gl_Vertex.xyz;C=gl_Color;U=gl_MultiTexCoord0.xy;A=gl_MultiTexCoord1.xy;gl_ClipVertex=gl_ModelViewMatrix*gl_Vertex;gl_Position=ftransform();}\n";
    std::string fs=std::string("#version 120\nuniform sampler2D uMaterial;uniform sampler2D uCoast;uniform int uMaterialOn;uniform int uCutOn;varying vec3 W;varying vec4 C;varying vec2 U;varying vec2 A;\n")+STONE_TBN_GLSL+
        "void main(){if(uCutOn==1 && texture2D(uCoast,A).a<0.5)discard;vec4 m=uMaterialOn==1?texture2D(uMaterial,U):vec4(0.5,0.5,0.82,0.0);vec3 N=normalize(C.rgb*2.0-1.0);\n"
        "vec2 xy=m.rg*2.0-1.0;vec3 mn=normalize(vec3(xy,sqrt(max(0.0001,1.0-dot(xy,xy)))));\n"
        "N=normalize(mix(N,stoneNormal(N,W,U,mn),m.a));\n"
        "float cls=m.a>0.5?0.861+0.028*clamp((m.b-0.48)/0.42,0.0,1.0):C.a;\n"
        "gl_FragColor=vec4(N*0.5+0.5,cls);}\n";
    GLuint v=fx_compile(GL_VERTEX_SHADER,vs,"stone-terrain-normal"),f=fx_compile(GL_FRAGMENT_SHADER,fs.c_str(),"stone-terrain-normal");
    if(!v||!f){if(v)fx_glDeleteShader(v);if(f)fx_glDeleteShader(f);return 0;}
    GLuint p=fx_glCreateProgram();fx_glAttachShader(p,v);fx_glAttachShader(p,f);fx_glLinkProgram(p);
    GLint ok=0;fx_glGetProgramiv(p,GL_LINK_STATUS,&ok);fx_glDeleteShader(v);fx_glDeleteShader(f);
    if(!ok){fx_glDeleteProgram(p);fprintf(stderr,"STONEMATERIAL|terrain shader failed\n");return 0;}program=p;return p;
}
static bool terrain_surface_load(void)
{
    TerrainSurfaceState& S=g_terrainSurface;const std::string theater=terrain_surface_theater();
    if(S.theater!=theater) { terrain_surface_free();S.theater=theater; }
    if(S.tried)return S.cell>0;
    S.tried=true;
    const char* roots[]={"","game/","../game/","ra3d/","../"};std::vector<unsigned char> data;
    for(size_t i=0;i<sizeof roots/sizeof roots[0];i++) {
        const char* overrideDir=getenv("OPENCNC3D_TERRAIN_SURFACE_DIR");
        const std::string p=(overrideDir&&*overrideDir?std::string(overrideDir)+"/":std::string(roots[i]))+"terrainsurface_"+theater+".pack";
        FILE* f=fopen(p.c_str(),"rb");if(!f)continue;
        if(fseek(f,0,SEEK_END)==0) { long n=ftell(f);if(n>=20 && n<=128L*1024*1024 && fseek(f,0,SEEK_SET)==0) { data.resize((size_t)n);if(fread(data.data(),1,data.size(),f)!=data.size())data.clear(); } }
        fclose(f);break;
    }
    if(data.size()<20 || memcmp(data.data(),"C3DSURF\0",8) || cb_u32(data.data()+8)!=1)return false;
    const unsigned cell=cb_u32(data.data()+12),count=cb_u32(data.data()+16);
    if(cell<8 || cell>144 || count<1 || count>8192 || data.size()!=20+(size_t)count*24+(size_t)cell*cell*count)return false;
    S.cell=(int)cell;S.tiles.assign(data.begin()+20+count*24,data.end());
    for(unsigned i=0;i<count;i++) {
        const unsigned char* k=data.data()+20+i*24;char name[17];memcpy(name,k,16);name[16]=0;
        const unsigned tid=k[16]|(k[17]<<8),icon=k[18]|(k[19]<<8),v=k[20];
        if(v>3 || k[21]>1){S.cell=0;S.tiles.clear();S.td.clear();S.ra.clear();return false;}
        if(k[21])S.ra[terrain_grass_ra_key(name,(int)icon,(int)v)]=(int)i;
        else S.td[(tid<<16)|(icon<<2)|v]=(int)i;
    }
    terrain_surface_material_load(data);
    fprintf(stderr,"TERRAINSURFACE|loaded|%s|tiles=%u|cell=%u\n",theater.c_str(),count,cell);return true;
}
static int terrain_surface_td_tile(unsigned tid,unsigned icon,int var)
{
    const auto& m=g_terrainSurface.td;auto i=m.find((tid<<16)|(icon<<2)|(unsigned)var);return i==m.end()?-1:i->second;
}
static int terrain_surface_ra_tile(const char* name,int icon,int var)
{
    const auto& m=g_terrainSurface.ra;auto i=m.find(terrain_grass_ra_key(name,icon,var));return i==m.end()?-1:i->second;
}
static float terrain_surface_tile_height(int tile,float x,float z)
{
    const TerrainSurfaceState& S=g_terrainSurface;if(tile<0 || !S.cell)return 0;
    const float u=std::max(0.0f,std::min((float)S.cell-1,x*S.cell-0.5f)),v=std::max(0.0f,std::min((float)S.cell-1,z*S.cell-0.5f));
    const int ix=(int)u,iz=(int)v,jx=std::min(ix+1,S.cell-1),jz=std::min(iz+1,S.cell-1);const float a=u-ix,b=v-iz;
    const unsigned char* p=&S.tiles[(size_t)tile*S.cell*S.cell];
    return ((p[iz*S.cell+ix]*(1-a)+p[iz*S.cell+jx]*a)*(1-b)+(p[jz*S.cell+ix]*(1-a)+p[jz*S.cell+jx]*a)*b)*(1.0f/255.0f);
}
/* This is also the height returned to attachments and picking. Match the mesh's
   SW-NE triangle diagonal exactly; bilinear interpolation is a different surface. */
static float terrain_surface_height(float wx,float wz)
{
    const TerrainSurfaceState& S=g_terrainSurface;
    if(!terrain_surface_on() || !S.ready || S.gen!=g_groundGen || S.fw<2 || S.fh<2)return 0;
    const float px=std::max(0.0f,std::min((float)S.fw-1,wx*S.steps)),pz=std::max(0.0f,std::min((float)S.fh-1,wz*S.steps));
    const int x=std::min((int)px,S.fw-2),z=std::min((int)pz,S.fh-2);const float a=px-x,b=pz-z;
    const float h00=S.lift[(size_t)z*S.fw+x],h10=S.lift[(size_t)z*S.fw+x+1],h01=S.lift[(size_t)(z+1)*S.fw+x],h11=S.lift[(size_t)(z+1)*S.fw+x+1];
    return a+b<=1 ? h00+(h10-h00)*a+(h01-h00)*b : h11+(h01-h11)*(1-a)+(h10-h11)*(1-b);
}
/* ---- the ground revision grid's producer (g_groundRev, declared with g_padEpoch) ----
   The lift the rest of the frame sees is terrain_surface_height's: the surface's lattice
   while it is on, ready and of this ground generation, and zero otherwise. */
static bool ground_rev_lift_now(unsigned* surface)
{
    const TerrainSurfaceState& S=g_terrainSurface;
    const bool live=terrain_surface_on() && S.ready && S.gen==g_groundGen && S.fw>=2 && S.fh>=2;
    *surface=live?S.epoch:0;return live;
}
static bool ground_patch_on(void){ return mesh_perf_switch("OPENCNC3D_GROUND_PATCH"); }
static bool ground_patch_verify(void)
{ static const bool on=[](){const char* v=getenv("OPENCNC3D_GROUND_PATCH_VERIFY");return v && *v && strcmp(v,"0");}();return on; }
struct GroundRevSnapshot {
    int gridW=-1,gridH=-1;unsigned gen=0,pad=0;
    std::vector<unsigned char> corners;   // corner_raw at the last sync
    bool live=false;unsigned surface=0;int fw=0,fh=0,steps=0;
    std::vector<float> lift;              // the live lift at the last sync, empty when it was not live
    bool owed=false;                      // the ground moved between pad changes: the next one is whole
};
static GroundRevSnapshot g_groundRevSnap;
/* Once a frame, after terrain_surface_prepare_scene. Every cache that reads these stamps
   used to refresh on a pad change and on nothing else, keeping whatever the ground did in
   between until the next one, when it took all of it at once. So stamps are issued only
   on a frame whose pad epoch moved, and they cover everything since the last such frame:
   a corner or a lift node that moved between pad changes (a relief setting, the coast
   atlas, a raw corner written in place) makes that next pad change a whole one, exactly
   as every one of them used to be. The water plane is not a ground height here: a cache
   whose drawing reads it holds the plane's epoch itself and refreshes whole when it moved.
   A corner is read by the cells on both sides of it and, with zero weight, by the cell
   beyond (terrain_y_base at a cell's far edge), so a moved corner marks the two cells
   before it and the one after; a node of the lift is read by the cells whose lattice
   reaches it, plus the emitter's +-1 support, so it marks one cell more each way. Both
   margins are generous on purpose. */
static void ground_rev_sync(void)
{
    if(!ground_patch_on())return;
    GroundRevSnapshot& R=g_groundRevSnap;const TerrainSurfaceState& S=g_terrainSurface;
    unsigned surface=0;const bool live=ground_rev_lift_now(&surface);
    const size_t cells=(size_t)std::max(0,g_gridW)*std::max(0,g_gridH);
    const size_t ncorn=g_pack.corner.empty()?0:(size_t)(g_gridW+1)*(g_gridH+1);
    static std::vector<unsigned char> corners,marked;corners.resize(ncorn);
    for(int z=0;ncorn && z<=g_gridH;z++)for(int x=0;x<=g_gridW;x++)corners[(size_t)z*(g_gridW+1)+x]=corner_raw(x,z);
    const bool padMoved=R.pad!=g_padEpoch;
    bool whole=R.gridW!=g_gridW || R.gridH!=g_gridH || R.gen!=g_groundGen || R.corners.size()!=ncorn || g_groundRev.size()!=cells;
    bool moved=false;
    marked.assign(whole?0:cells,0);
    const auto mark=[&](int x0,int z0,int x1,int z1) {
        moved=true;x0=std::max(0,x0);z0=std::max(0,z0);x1=std::min(g_gridW-1,x1);z1=std::min(g_gridH-1,z1);
        for(int z=z0;z<=z1;z++)for(int x=x0;x<=x1;x++)marked[(size_t)z*g_gridW+x]=1;
    };
    if(!whole) {
        if(memcmp(corners.data(),R.corners.data(),ncorn))
            for(int z=0;z<=g_gridH;z++)for(int x=0;x<=g_gridW;x++)
                if(corners[(size_t)z*(g_gridW+1)+x]!=R.corners[(size_t)z*(g_gridW+1)+x])mark(x-2,z-2,x+1,z+1);
        if(!whole && (live!=R.live || (live && surface!=R.surface))) {
            if(live && R.live && (S.fw!=R.fw || S.fh!=R.fh || S.steps!=R.steps))whole=true;
            else {
                // One side may be off, which reads as a lift of zero everywhere.
                const int fw=live?S.fw:R.fw,fh=live?S.fh:R.fh,steps=live?S.steps:R.steps;
                const float zero=0;
                // A patch since the last sync says which nodes it changed; nothing else moved.
                const bool listed=live && R.live && R.surface==S.liftFrom && surface==S.liftTo && S.liftTo;
                for(size_t j=0;listed && j<S.liftMoved.size();j++) {
                    const int i=S.liftMoved[j],x=i%fw,z=i/fw;
                    if(memcmp(&S.lift[i],&R.lift[i],sizeof(float)))mark(x/steps-2,z/steps-2,x/steps+1,z/steps+1);
                }
                const bool same=listed || (live && R.live && !memcmp(S.lift.data(),R.lift.data(),S.lift.size()*sizeof(float)));
                for(int z=0;!same && z<fh;z++)for(int x=0;x<fw;x++) {
                    const size_t i=(size_t)z*fw+x;
                    const float* a=live?&S.lift[i]:&zero;const float* b=R.live?&R.lift[i]:&zero;
                    if(memcmp(a,b,sizeof(float)))mark(x/steps-2,z/steps-2,x/steps+1,z/steps+1);
                }
            }
        }
    }
    if(R.gridW!=g_gridW || R.gridH!=g_gridH || R.gen!=g_groundGen || g_groundRev.size()!=cells) {
        // A new pack, grid or ground generation: every key holding the generation moves too.
        g_groundWholeSeq=++g_groundSeq;g_groundRev.assign(cells,g_groundWholeSeq);R.owed=false;
    } else if(!padMoved) {
        if(whole || moved)R.owed=true;
    } else if(whole || R.owed) {
        g_groundWholeSeq=++g_groundSeq;std::fill(g_groundRev.begin(),g_groundRev.end(),g_groundWholeSeq);R.owed=false;
    } else if(moved) {
        const unsigned seq=++g_groundSeq;
        for(size_t i=0;i<cells;i++)if(marked[i])g_groundRev[i]=seq;
    }
    R.gridW=g_gridW;R.gridH=g_gridH;R.gen=g_groundGen;R.pad=g_padEpoch;R.corners=corners;
    if(live!=R.live || (live && surface!=R.surface)) {
        const bool listed=live && R.live && R.surface==S.liftFrom && surface==S.liftTo && S.liftTo && R.lift.size()==S.lift.size();
        R.live=live;R.surface=surface;
        if(listed)for(int i:S.liftMoved)R.lift[i]=S.lift[i];
        else if(live){R.lift=S.lift;R.fw=S.fw;R.fh=S.fh;R.steps=S.steps;}else{R.lift.clear();R.fw=R.fh=R.steps=0;}
    }
    g_groundRevPad=g_padEpoch;g_groundRevGen=g_groundGen;g_groundRevLive=live;g_groundRevSurface=surface;
}
static float terrain_surface_node(int x,int z)
{
    const TerrainSurfaceState& S=g_terrainSurface;x=std::max(0,std::min(S.fw-1,x));z=std::max(0,std::min(S.fh-1,z));
    return S.lift[(size_t)z*S.fw+x];
}
/* Milliseconds since a performance-counter stamp, for the build's own log line. */
static double terrain_surface_ms(Uint64 since)
{ return (double)(SDL_GetPerformanceCounter()-since)*1000.0/(double)SDL_GetPerformanceFrequency(); }
/* The surface key without its pad epoch (terrain_surface_key's layout: ground
   generation, pad epoch, atlas, extra, relief height, four bytes each). */
static std::string terrain_surface_key_without_pad(const std::string& key)
{ return key.size()<8?key:key.substr(0,4)+key.substr(8); }
/* Reuse the pad-free base when nothing it was computed from has moved, else compute
   it. The arithmetic is the full build's, statement for statement, so a reused value
   is the value a fresh build would produce. True when the kept base was reused. */
static bool terrain_surface_base_ensure(const std::vector<TerrainSurfaceCell>& cells,const std::string& key,double* msRead)
{
    TerrainSurfaceState& S=g_terrainSurface;TerrainSurfaceBase& B=S.base;
    const std::string baseKey=terrain_surface_key_without_pad(key);
    if(B.valid && B.key==baseKey && B.atlas==g_terrainSurfaceAtlas && g_glsWatchTex==B.atlas && g_glsWatchGen==B.coastGen
       && B.steps==S.steps && B.gridW==g_gridW && B.gridH==g_gridH && B.cells.size()==cells.size()
       && (cells.empty() || !memcmp(B.cells.data(),cells.data(),cells.size()*sizeof(TerrainSurfaceCell))))
        return true;
    const Uint64 tStart=SDL_GetPerformanceCounter();
    B=TerrainSurfaceBase();B.key=baseKey;B.atlas=g_terrainSurfaceAtlas;B.steps=S.steps;B.gridW=g_gridW;B.gridH=g_gridH;B.cells=cells;
    std::vector<unsigned char> coast;int coastW=0,coastH=0;
    if(g_terrainSurfaceAtlas) {
        GLint active=GL_TEXTURE0,bound=0,pack=4;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);glGetIntegerv(GL_PACK_ALIGNMENT,&pack);
        glBindTexture(GL_TEXTURE_2D,g_terrainSurfaceAtlas);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&coastW);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&coastH);
        if(coastW>0 && coastH>0 && coastW<=8192 && coastH<=8192){coast.resize((size_t)coastW*coastH);glPixelStorei(GL_PACK_ALIGNMENT,1);glGetTexImage(GL_TEXTURE_2D,0,GL_ALPHA,GL_UNSIGNED_BYTE,coast.data());}
        glPixelStorei(GL_PACK_ALIGNMENT,pack);glBindTexture(GL_TEXTURE_2D,bound);glActiveTexture((GLenum)active);
    }
    g_glsWatchTex=B.atlas;B.coastGen=g_glsWatchGen;
    *msRead=terrain_surface_ms(tStart);
    B.water.assign(coast.empty()?0:(size_t)S.fw*S.fh,0);
    const auto landAt=[&](const TerrainSurfaceCell& c,float x,float z)->bool {
        if(coast.empty())return true;
        const float u=c.uv[0]+(c.uv[2]-c.uv[0])*x,v=c.uv[1]+(c.uv[3]-c.uv[1])*z;
        const int ix=std::max(0,std::min(coastW-1,(int)(u*coastW))),iz=std::max(0,std::min(coastH-1,(int)(v*coastH)));
        return coast[(size_t)iz*coastW+ix]>=250;
    };
    for(const auto& c:cells) {
        if(c.x<0 || c.z<0 || c.x>=g_gridW || c.z>=g_gridH)continue;
        for(int z=0;z<=S.steps;z++)for(int x=0;x<=S.steps;x++) {
            const int gx=c.x*S.steps+x,gz=c.z*S.steps+z;
            const bool land=landAt(c,(float)x/S.steps,(float)z/S.steps);
            if(!land && !B.water.empty())B.water[(size_t)gz*S.fw+gx]=1;
            if(c.tile<0)continue;
            float h=terrain_surface_tile_height(c.tile,(float)x/S.steps,(float)z/S.steps)*g_fx.terrain_relief_height;
            if(!land)h=0;
            B.height.push_back(h);
        }
    }
    if(!B.water.empty()) {
        std::vector<float> distance(B.water.size());
        water_edt_2d(B.water.data(),distance.data(),NULL,S.fw,S.fh);
        const float width=std::max(0.50f,2.0f/(float)S.steps);B.width=width;B.taper.resize(B.water.size());
        for(size_t i=0;i<B.water.size();i++) {
            const float t=std::max(0.0f,std::min(1.0f,(distance[i]-0.5f)/((float)S.steps*width)));
            const float k=t*t*t*(t*(t*6.0f-15.0f)+10.0f);
            B.taper[i]=k;
        }
    }
    B.valid=true;return false;
}
/* The pad-dependent half: the lift, from the base, in the full build's order. A
   padded cell adds +0 to its nodes exactly as the full build does. */
static void terrain_surface_tail(const std::vector<TerrainSurfaceCell>& cells,bool pads=true)
{
    TerrainSurfaceState& S=g_terrainSurface;const TerrainSurfaceBase& B=S.base;size_t next=0;
    for(const auto& c:cells) {
        if(c.x<0 || c.z<0 || c.x>=g_gridW || c.z>=g_gridH || c.tile<0)continue;
        // Building foundations own their native graded footprint and its corners.
        const bool pad=pads && (g_padOn[c.z*(g_gridW+1)+c.x] || g_padOn[c.z*(g_gridW+1)+c.x+1] || g_padOn[(c.z+1)*(g_gridW+1)+c.x] || g_padOn[(c.z+1)*(g_gridW+1)+c.x+1]);
        for(int z=0;z<=S.steps;z++)for(int x=0;x<=S.steps;x++) {
            const int gx=c.x*S.steps+x,gz=c.z*S.steps+z;
            float h=B.height[next++];
            if(pad)h=0;
            S.lift[(size_t)gz*S.fw+gx]+=h;
        }
    }
    for(int z=0;z<S.fh;z++)for(int x=0;x<S.fw;x++) {
        int count=(x%S.steps==0 && x>0 && x<S.fw-1?2:1)*(z%S.steps==0 && z>0 && z<S.fh-1?2:1);
        S.lift[(size_t)z*S.fw+x]/=(float)count;
        if(!B.water.empty() && B.water[(size_t)z*S.fw+x])S.lift[(size_t)z*S.fw+x]=0;
    }
    if(S.theater=="RA_SNOW") {
        std::vector<float> tmp(S.lift.size());const int kernel[5]={1,4,6,4,1};const int spacing=std::max(1,S.steps/4);
        for(int z=0;z<S.fh;z++)for(int x=0;x<S.fw;x++) {
            float h=0;for(int k=-2;k<=2;k++)h+=S.lift[(size_t)z*S.fw+std::max(0,std::min(S.fw-1,x+k*spacing))]*kernel[k+2];
            tmp[(size_t)z*S.fw+x]=h*(1.0f/16.0f);
        }
        for(int z=0;z<S.fh;z++)for(int x=0;x<S.fw;x++) {
            float h=0;for(int k=-2;k<=2;k++)h+=tmp[(size_t)std::max(0,std::min(S.fh-1,z+k*spacing))*S.fw+x]*kernel[k+2];
            const int cx=std::min(g_gridW-1,x/S.steps),cz=std::min(g_gridH-1,z/S.steps),cw=g_gridW+1;
            const bool pad=pads && (g_padOn[cz*cw+cx]||g_padOn[cz*cw+cx+1]||g_padOn[(cz+1)*cw+cx]||g_padOn[(cz+1)*cw+cx+1]);
            S.lift[(size_t)z*S.fw+x]=pad?0:h*(1.0f/16.0f);
        }
    }
    if(!B.water.empty()) {
        long wet=0;float maxWet=0;
        for(size_t i=0;i<S.lift.size();i++) {
            S.lift[i]*=B.taper[i];
            if(B.water[i]){wet++;maxWet=std::max(maxWet,S.lift[i]);}
        }
        if(pads)fprintf(stderr,"TERRAINSURFACE|coast-cut|water-nodes=%ld|wet-lift-max=%.6f|land-taper=%.3f|normal-cut=atlas-alpha\n",wet,maxWet,B.width);
    }
}
/* The build's raised test on any lift lattice: a node of the cell's lattice, or one
   normal-sampling support beyond it, above 0.0001 (terrain_surface_node's clamping). */
static bool terrain_surface_raised(const std::vector<float>& lift,const TerrainSurfaceCell& c)
{
    const TerrainSurfaceState& S=g_terrainSurface;
    for(int z=-1;z<=S.steps+1;z++)for(int x=-1;x<=S.steps+1;x++) {
        const int gx=std::max(0,std::min(S.fw-1,c.x*S.steps+x)),gz=std::max(0,std::min(S.fh-1,c.z*S.steps+z));
        if(lift[(size_t)gz*S.fw+gx]>.0001f)return true;
    }
    return false;
}
/* One cell's (steps+1)^2 lattice nodes as finished vertices. Every triangle corner
   on a node is the same vertex, so each node is evaluated once and copied into the
   six corners that use it. The four corner normals and their byte encodings depend
   only on the cell, so they are taken once per cell; the float blend is per node,
   written exactly as the per-corner loop writes it, so every vertex keeps its bytes. */
static void terrain_surface_cell_nodes(const TerrainSurfaceCell& c,std::vector<TerrainSurfaceVert>& nodes)
{
    const TerrainSurfaceState& S=g_terrainSurface;const int side=S.steps+1;nodes.resize((size_t)side*side);
    const int dx[4]={0,1,1,0},dz[4]={0,0,1,1};int encodedAt[4][3];
    for(int q=0;q<4;q++) {
        float n[3];terrain_corner_normal(c.x+dx[q],c.z+dz[q],n);
        if(n[0]==0 && n[1]==0 && n[2]==0)n[1]=127;
        for(int j=0;j<3;j++)encodedAt[q][j]=std::max(0,std::min(255,(int)((n[j]/127.0f*.5f+.5f)*255.0f+.5f)));
    }
    for(int z=0;z<=S.steps;z++)for(int x=0;x<=S.steps;x++) {
        const int gx=c.x*S.steps+x,gz=c.z*S.steps+z;
        const float fx=(float)(x)/S.steps,fz=(float)(z)/S.steps,wx=c.x+fx,wz=c.z+fz;
        TerrainSurfaceVert& v=nodes[(size_t)z*side+x];v.p[0]=wx;v.p[1]=terrain_y_base(wx,wz)+terrain_surface_node(gx,gz);v.p[2]=wz;
        v.uv[0]=c.uv[0]+(c.uv[2]-c.uv[0])*fx;v.uv[1]=c.uv[1]+(c.uv[3]-c.uv[1])*fz;
        v.mu[0]=v.mu[1]=0;
        if(S.material){const int tile=c.tile<0?(int)(S.tiles.size()/(S.cell*S.cell)):c.tile,pitch=S.materialCell+4;
            v.mu[0]=((tile%S.materialCols)*pitch+2+fx*S.materialCell)/(float)(S.materialCols*pitch);
            v.mu[1]=((tile/S.materialCols)*pitch+2+fz*S.materialCell)/(float)(S.materialRows*pitch);}
        float base[3]={0,0,0};
        const float wt[4]={std::max(0.0f,1-fx-fz),std::min(fx,1-fz),std::max(0.0f,fx+fz-1),std::min(fz,1-fx)};
        for(int q=0;q<4;q++) {
            for(int j=0;j<3;j++) {
                const int encoded=encodedAt[q][j];
                base[j]+=(encoded*(2.0f/255.0f)-1.0f)*wt[q];
            }
        }
        const float hx=(terrain_surface_node(gx+1,gz)-terrain_surface_node(gx-1,gz))*S.steps*.5f,hz=(terrain_surface_node(gx,gz+1)-terrain_surface_node(gx,gz-1))*S.steps*.5f;
        const float n[3]={base[0]-hx*base[1],base[1],base[2]-hz*base[1]};
        const float length=std::max(1.0f,sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]));
        for(int j=0;j<3;j++)v.n[j]=n[j]/length*.5f+.5f;
        v.n[3]=1.0f-.08f*std::min(1.0f,terrain_surface_node(gx,gz)/std::max(g_fx.terrain_relief_height*.25f,.001f));
    }
}
static bool terrain_surface_slots_on(void)
{ static const bool on=mesh_perf_switch("OPENCNC3D_SURFACE_BASE") && mesh_perf_switch("OPENCNC3D_SURFACE_NODES") && mesh_perf_switch("OPENCNC3D_SURFACE_SLOTS");return on; }
static void terrain_surface_corners_now(std::vector<unsigned char>& out)
{
    out.resize(g_pack.corner.empty()?0:(size_t)(g_gridW+1)*(g_gridH+1));
    for(int z=0;!out.empty() && z<=g_gridH;z++)for(int x=0;x<=g_gridW;x++)out[(size_t)z*(g_gridW+1)+x]=corner_raw(x,z);
}
static void terrain_surface_pad_cells(std::vector<unsigned char>& out)
{
    const int cw=g_gridW+1;out.assign((size_t)g_gridW*g_gridH,0);
    for(int z=0;z<g_gridH;z++)for(int x=0;x<g_gridW;x++)
        out[(size_t)z*g_gridW+x]=(g_padOn[z*cw+x]||g_padOn[z*cw+x+1]||g_padOn[(z+1)*cw+x]||g_padOn[(z+1)*cw+x+1])?1:0;
}
/* Each cell's place in the cell order and its first node in base.height, as
   terrain_surface_tail walks them. Not usable when a cell appears twice. */
static void terrain_surface_cell_order(const std::vector<TerrainSurfaceCell>& cells)
{
    TerrainSurfaceState& S=g_terrainSurface;const size_t n=(size_t)g_gridW*g_gridH;
    S.cellOrder.assign(n,-1);S.cellHeight.assign(n,-1);S.orderOk=true;
    int next=0;const int per=(S.steps+1)*(S.steps+1);
    for(size_t i=0;i<cells.size();i++) {
        const TerrainSurfaceCell& c=cells[i];
        if(c.x<0 || c.z<0 || c.x>=g_gridW || c.z>=g_gridH)continue;
        const size_t k=(size_t)c.z*g_gridW+c.x;
        if(S.cellOrder[k]>=0){S.orderOk=false;return;}
        S.cellOrder[k]=(int)i;
        if(c.tile>=0){S.cellHeight[k]=next;next+=per;}
    }
}
/* One node of the lift before any blur, from the base: the contributions of the cells
   whose lattice holds it, added in the cell order, then divided and cut at the coast,
   exactly as terrain_surface_tail does it. */
static float terrain_surface_pre_node(int gx,int gz)
{
    const TerrainSurfaceState& S=g_terrainSurface;const TerrainSurfaceBase& B=S.base;const int st=S.steps,cw=g_gridW+1;
    int order[4],cell[4],n=0;
    for(int cz=gz/st-1;cz<=gz/st;cz++)for(int cx=gx/st-1;cx<=gx/st;cx++) {
        if(cx<0 || cz<0 || cx>=g_gridW || cz>=g_gridH || gx<cx*st || gx>cx*st+st || gz<cz*st || gz>cz*st+st)continue;
        const int k=cz*g_gridW+cx;if(S.cellOrder[k]<0 || S.cellHeight[k]<0)continue;
        int j=n++;while(j>0 && order[j-1]>S.cellOrder[k]){order[j]=order[j-1];cell[j]=cell[j-1];j--;}
        order[j]=S.cellOrder[k];cell[j]=k;
    }
    float lift=0;
    for(int i=0;i<n;i++) {
        const int cx=cell[i]%g_gridW,cz=cell[i]/g_gridW;
        float h=B.height[(size_t)S.cellHeight[cell[i]]+(size_t)(gz-cz*st)*(st+1)+(gx-cx*st)];
        if(g_padOn[cz*cw+cx] || g_padOn[cz*cw+cx+1] || g_padOn[(cz+1)*cw+cx] || g_padOn[(cz+1)*cw+cx+1])h=0;
        lift+=h;
    }
    int count=(gx%st==0 && gx>0 && gx<S.fw-1?2:1)*(gz%st==0 && gz>0 && gz<S.fh-1?2:1);
    lift/=(float)count;
    if(!B.water.empty() && B.water[(size_t)gz*S.fw+gx])lift=0;
    return lift;
}
/* The lift after a pad change, recomputed only where it can have moved: the lattice of
   each cell whose padded state flipped, widened by the snow blur's reach. Every node
   outside that has the same inputs it had, so it keeps its value. Each region is
   computed from the base alone, never from the lift, so overlapping regions agree.
   `moved` is told every node whose bytes changed. */
template<class F> static void terrain_surface_tail_local(const std::vector<int>& flipped,F moved)
{
    TerrainSurfaceState& S=g_terrainSurface;const TerrainSurfaceBase& B=S.base;
    const int st=S.steps,fw=S.fw,fh=S.fh,cw=g_gridW+1;const bool snow=S.theater=="RA_SNOW";
    const int spacing=std::max(1,S.steps/4),reach=snow?2*spacing:0;const int kernel[5]={1,4,6,4,1};
    static std::vector<float> pre,tmp;
    for(int k:flipped) {
        const int cx=k%g_gridW,cz=k/g_gridW;
        const int x0=std::max(0,cx*st-reach),x1=std::min(fw-1,cx*st+st+reach),z0=std::max(0,cz*st-reach),z1=std::min(fh-1,cz*st+st+reach);
        const int px0=std::max(0,x0-reach),px1=std::min(fw-1,x1+reach),pz0=std::max(0,z0-reach),pz1=std::min(fh-1,z1+reach);
        const int pw=px1-px0+1;
        pre.assign((size_t)pw*(pz1-pz0+1),0);
        for(int z=pz0;z<=pz1;z++)for(int x=px0;x<=px1;x++)pre[(size_t)(z-pz0)*pw+(x-px0)]=terrain_surface_pre_node(x,z);
        if(snow) {
            const int tw=x1-x0+1;tmp.assign((size_t)tw*(pz1-pz0+1),0);
            for(int z=pz0;z<=pz1;z++)for(int x=x0;x<=x1;x++) {
                float h=0;for(int q=-2;q<=2;q++)h+=pre[(size_t)(z-pz0)*pw+(std::max(0,std::min(fw-1,x+q*spacing))-px0)]*kernel[q+2];
                tmp[(size_t)(z-pz0)*tw+(x-x0)]=h*(1.0f/16.0f);
            }
            for(int z=z0;z<=z1;z++)for(int x=x0;x<=x1;x++) {
                float h=0;for(int q=-2;q<=2;q++)h+=tmp[(size_t)(std::max(0,std::min(fh-1,z+q*spacing))-pz0)*tw+(x-x0)]*kernel[q+2];
                const int ccx=std::min(g_gridW-1,x/st),ccz=std::min(g_gridH-1,z/st);
                const bool pad=g_padOn[ccz*cw+ccx]||g_padOn[ccz*cw+ccx+1]||g_padOn[(ccz+1)*cw+ccx]||g_padOn[(ccz+1)*cw+ccx+1];
                float v=pad?0:h*(1.0f/16.0f);
                const size_t i=(size_t)z*fw+x;
                if(!B.water.empty())v*=B.taper[i];
                if(memcmp(&v,&S.lift[i],sizeof v)){S.lift[i]=v;moved(x,z);}
            }
        } else {
            for(int z=z0;z<=z1;z++)for(int x=x0;x<=x1;x++) {
                float v=pre[(size_t)(z-pz0)*pw+(x-px0)];
                const size_t i=(size_t)z*fw+x;
                if(!B.water.empty())v*=B.taper[i];
                if(memcmp(&v,&S.lift[i],sizeof v)){S.lift[i]=v;moved(x,z);}
            }
        }
    }
}
/* Lay the records as slots (see TerrainSurfaceState). False, with nothing laid, if a
   cell is raised now but not without its pads, which the arithmetic rules out; the
   caller then lays the records the old way. */
static bool terrain_surface_lay_slots(const std::vector<TerrainSurfaceCell>& cells)
{
    TerrainSurfaceState& S=g_terrainSurface;
    static std::vector<float> withPads,padFree;
    withPads.swap(S.lift);S.lift.assign(withPads.size(),0);terrain_surface_tail(cells,false);padFree.swap(S.lift);S.lift.swap(withPads);
    S.slotAt.assign((size_t)g_gridW*g_gridH,-1);
    const int ox[6]={0,0,1,1,0,1},oz[6]={0,1,0,0,1,1},side=S.steps+1;
    static std::vector<TerrainSurfaceVert> nodes;
    for(const auto& c:cells) {
        if(c.x<0 || c.z<0 || c.x>=g_gridW || c.z>=g_gridH)continue;
        const bool raised=terrain_surface_raised(S.lift,c);
        const size_t cell=(size_t)c.z*g_gridW+c.x;
        if(!terrain_surface_raised(padFree,c) || S.slotAt[cell]>=0) {
            if(raised || S.slotAt[cell]>=0) {
                S.vertices.clear();S.records.clear();S.slotAt.clear();S.triangles=0;
                fprintf(stderr,"TERRAINSURFACE|slots|refused|cell=%d,%d\n",c.x,c.z);return false;
            }
            continue;
        }
        TerrainSurfaceRecord rec={c.x,c.z,(int)S.vertices.size(),0,{0,0,0,0},false,raised};
        terrain_surface_cell_nodes(c,nodes);
        for(int z=0;z<S.steps;z++)for(int x=0;x<S.steps;x++)for(int k=0;k<6;k++)S.vertices.push_back(nodes[(size_t)(z+oz[k])*side+x+ox[k]]);
        rec.count=(int)S.vertices.size()-rec.first;if(raised)S.triangles+=rec.count/3;
        S.slotAt[cell]=(int)S.records.size();S.records.push_back(rec);
    }
    terrain_surface_corners_now(S.emitCorners);
    terrain_surface_cell_order(cells);terrain_surface_pad_cells(S.padCells);
    S.slotted=true;return true;
}
static void terrain_surface_lift_dump(const std::vector<unsigned char>& water);
static bool terrain_surface_verify_on(void);
static void terrain_surface_lift_legacy(const std::vector<TerrainSurfaceCell>& cells,std::vector<unsigned char>& waterNodes,double* msRead,double* msLift);
static void terrain_surface_cell_corners(const TerrainSurfaceCell& c,std::vector<TerrainSurfaceVert>& out);
/* OPENCNC3D_SURFACE_VERIFY on a slotted surface: the lift a whole build computes, its
   raised set against the active records, each active record's bytes against the
   corner-by-corner emitter, and the GPU store against the arrays. */
static void terrain_surface_check_slots(const std::vector<TerrainSurfaceCell>& cells,const char* what)
{
    TerrainSurfaceState& S=g_terrainSurface;long mismatches=0;
    const std::vector<float> fast=S.lift;std::vector<unsigned char> water;double r0=0,l0=0;
    S.lift.assign(fast.size(),0);terrain_surface_lift_legacy(cells,water,&r0,&l0);
    if(memcmp(fast.data(),S.lift.data(),fast.size()*sizeof(float)) || water!=S.base.water)mismatches++;
    S.lift=fast;
    std::vector<TerrainSurfaceVert> check,gpu(S.vertices.size());
    for(const auto& c:cells) {
        if(c.x<0 || c.z<0 || c.x>=g_gridW || c.z>=g_gridH)continue;
        const size_t cell=(size_t)c.z*g_gridW+c.x;const int at=S.slotAt[cell];
        const bool raised=terrain_surface_raised(S.lift,c);
        if(raised!=(at>=0 && S.records[at].active) || S.recordAt[cell]!=(raised?at:-1)){mismatches++;continue;}
        if(!raised)continue;
        check.clear();terrain_surface_cell_corners(c,check);
        if(check.size()!=(size_t)S.records[at].count || memcmp(check.data(),&S.vertices[S.records[at].first],check.size()*sizeof(TerrainSurfaceVert)))mismatches++;
    }
    const bool read=S.geo && ground_read_buffer(GL_ARRAY_BUFFER,S.geo,gpu.size()*sizeof(TerrainSurfaceVert),gpu.data());
    if(read && memcmp(gpu.data(),S.vertices.data(),gpu.size()*sizeof(TerrainSurfaceVert)))mismatches++;
    fprintf(stderr,"TERRAINSURFACE|verify|%s|%s|mismatches=%ld|gpu=%s\n",what,mismatches?"FAILED":"ok",mismatches,read?"read":"unread");
}
/* A pad change on a slotted surface: the lift again from the kept base, then only the
   records whose lattice window or corners moved are laid again and sent over their old
   bytes, and their active flags follow the build's raised test. A record that becomes
   active starts uncoloured, as every record of a fresh build does; one that stays
   active keeps its colours, which are a function of its corners' x and z and its
   light alone. The epoch still moves, so everything keyed on the surface refreshes
   exactly when it did. False when a whole build is needed instead. */
static bool terrain_surface_patch(const std::vector<TerrainSurfaceCell>& cells,const std::string& key)
{
    TerrainSurfaceState& S=g_terrainSurface;
    if(!S.ready || !S.slotted || S.failed || !S.geo || S.gen!=g_groundGen
       || terrain_surface_key_without_pad(S.key)!=terrain_surface_key_without_pad(key) || S.source.size()!=cells.size()
       || (!cells.empty() && memcmp(S.source.data(),cells.data(),cells.size()*sizeof(TerrainSurfaceCell))))return false;
    const Uint64 t0=SDL_GetPerformanceCounter();
    double msRead=0;if(!terrain_surface_base_ensure(cells,key,&msRead))return false;
    static std::vector<unsigned char> dirty,corners,pads;dirty.assign((size_t)g_gridW*g_gridH,0);
    const auto mark=[&](int x0,int z0,int x1,int z1) {
        x0=std::max(0,x0);z0=std::max(0,z0);x1=std::min(g_gridW-1,x1);z1=std::min(g_gridH-1,z1);
        for(int z=z0;z<=z1;z++)for(int x=x0;x<=x1;x++)dirty[(size_t)z*g_gridW+x]=1;
    };
    // A node is read by the cells whose lattice or +-1 normal support reaches it.
    S.liftMoved.clear();S.liftFrom=S.liftTo=0;
    // At one step per cell, a cell's +1 normal support reads a node two cells away, so a
    // moved node is read from two cells back.
    const int reach=S.steps<2?2:1;
    const auto movedNode=[&](int x,int z){mark(x/S.steps-reach,z/S.steps-reach,x/S.steps+1,z/S.steps+1);S.liftMoved.push_back(z*S.fw+x);};
    terrain_surface_pad_cells(pads);
    static const bool localOn=mesh_perf_switch("OPENCNC3D_SURFACE_LOCAL_LIFT");
    if(localOn && S.orderOk && pads.size()==S.padCells.size()) {
        static std::vector<int> flipped;flipped.clear();
        for(size_t k=0;k<pads.size();k++)if(pads[k]!=S.padCells[k])flipped.push_back((int)k);
        terrain_surface_tail_local(flipped,movedNode);
        if(!S.base.water.empty()) {
            // The coast-cut line the whole lift prints, from the same nodes.
            long wet=0;float maxWet=0;
            for(size_t i=0;i<S.lift.size();i++)if(S.base.water[i]){wet++;maxWet=std::max(maxWet,S.lift[i]);}
            fprintf(stderr,"TERRAINSURFACE|coast-cut|water-nodes=%ld|wet-lift-max=%.6f|land-taper=%.3f|normal-cut=atlas-alpha\n",wet,maxWet,S.base.width);
        }
    } else {
        static std::vector<float> old;old.swap(S.lift);S.lift.assign(old.size(),0);terrain_surface_tail(cells);
        if(memcmp(old.data(),S.lift.data(),old.size()*sizeof(float)))
            for(int z=0;z<S.fh;z++)for(int x=0;x<S.fw;x++) {
                const size_t i=(size_t)z*S.fw+x;
                if(memcmp(&old[i],&S.lift[i],sizeof(float)))movedNode(x,z);
            }
    }
    S.padCells=pads;
    const double msLift=terrain_surface_ms(t0);
    // A corner reaches the cells around it through the height, and one further through
    // the raw corner normals it takes part in.
    terrain_surface_corners_now(corners);
    if(corners.size()!=S.emitCorners.size())return false;
    if(memcmp(corners.data(),S.emitCorners.data(),corners.size()))
        for(int z=0;z<=g_gridH;z++)for(int x=0;x<=g_gridW;x++)
            if(corners[(size_t)z*(g_gridW+1)+x]!=S.emitCorners[(size_t)z*(g_gridW+1)+x])mark(x-2,z-2,x+1,z+1);
    const int ox[6]={0,0,1,1,0,1},oz[6]={0,1,0,0,1,1},side=S.steps+1;
    static std::vector<TerrainSurfaceVert> nodes,fresh;
    long relaid=0,flipped=0;size_t lo=0,hi=0;const size_t gap=4096;
    const auto send=[&]() {
        if(lo>=hi)return;
        gr_glBindBuffer(GL_ARRAY_BUFFER,S.geo);
        gr_glBufferSubData(GL_ARRAY_BUFFER,(FxGLsizeiptr)(lo*sizeof(TerrainSurfaceVert)),(FxGLsizeiptr)((hi-lo)*sizeof(TerrainSurfaceVert)),&S.vertices[lo]);
        gr_glBindBuffer(GL_ARRAY_BUFFER,0);
    };
    for(const auto& c:cells) {
        if(c.x<0 || c.z<0 || c.x>=g_gridW || c.z>=g_gridH)continue;
        const size_t cell=(size_t)c.z*g_gridW+c.x;
        if(!dirty[cell])continue;
        const bool raised=terrain_surface_raised(S.lift,c);
        const int at=S.slotAt[cell];
        if(at<0) { if(raised){fprintf(stderr,"TERRAINSURFACE|patch|refused|cell=%d,%d\n",c.x,c.z);return false;} continue; }
        TerrainSurfaceRecord& r=S.records[at];
        terrain_surface_cell_nodes(c,nodes);fresh.clear();
        for(int z=0;z<S.steps;z++)for(int x=0;x<S.steps;x++)for(int k=0;k<6;k++)fresh.push_back(nodes[(size_t)(z+oz[k])*side+x+ox[k]]);
        if(memcmp(fresh.data(),&S.vertices[r.first],fresh.size()*sizeof(TerrainSurfaceVert))) {
            memcpy(&S.vertices[r.first],fresh.data(),fresh.size()*sizeof(TerrainSurfaceVert));relaid++;
            const size_t a=(size_t)r.first,b=a+fresh.size();
            if(lo==hi){lo=a;hi=b;}else if(a<=hi+gap)hi=std::max(hi,b);else{send();lo=a;hi=b;}
        }
        if(raised!=r.active) {
            flipped++;
            if(raised){r.known=false;S.cbMasks[at]=-2;}
            r.active=raised;S.recordAt[cell]=raised?at:-1;
        }
    }
    send();
    S.triangles=0;for(const auto& r:S.records)if(r.active)S.triangles+=r.count/3;
    S.emitCorners=corners;S.key=key;S.pad=g_padEpoch;
    terrain_surface_lift_dump(S.base.water);
    if(terrain_surface_verify_on())terrain_surface_check_slots(cells,"patch");
    S.liftFrom=S.epoch;S.epoch++;S.liftTo=S.epoch;
    fprintf(stderr,"TERRAINSURFACE|patched|%s|cells=%ld|relaid=%ld|flipped=%ld|ms=%.2f|lift=%.2f\n",
        S.theater.c_str(),(long)(S.triangles/(S.steps*S.steps*2)),relaid,flipped,terrain_surface_ms(t0),msLift);
    return true;
}
/* The full build's lift, read straight from the atlas: the path the base and tail
   above reproduce, kept whole for OPENCNC3D_SURFACE_BASE=0 and for the verify mode. */
static void terrain_surface_lift_legacy(const std::vector<TerrainSurfaceCell>& cells,std::vector<unsigned char>& waterNodes,double* msRead,double* msLift)
{
    TerrainSurfaceState& S=g_terrainSurface;const Uint64 tStart=SDL_GetPerformanceCounter();
    // The rendered coast atlas, not the mineral matte, owns physical water.
    // Read once per geometry rebuild; all colour/normal/caster passes share it.
    std::vector<unsigned char> coast;int coastW=0,coastH=0;
    if(g_terrainSurfaceAtlas) {
        GLint active=GL_TEXTURE0,bound=0,pack=4;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);glGetIntegerv(GL_PACK_ALIGNMENT,&pack);
        glBindTexture(GL_TEXTURE_2D,g_terrainSurfaceAtlas);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&coastW);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&coastH);
        if(coastW>0 && coastH>0 && coastW<=8192 && coastH<=8192){coast.resize((size_t)coastW*coastH);glPixelStorei(GL_PACK_ALIGNMENT,1);glGetTexImage(GL_TEXTURE_2D,0,GL_ALPHA,GL_UNSIGNED_BYTE,coast.data());}
        glPixelStorei(GL_PACK_ALIGNMENT,pack);glBindTexture(GL_TEXTURE_2D,bound);glActiveTexture((GLenum)active);
    }
    *msRead=terrain_surface_ms(tStart);Uint64 tStage=SDL_GetPerformanceCounter();
    waterNodes.assign(coast.empty()?0:S.lift.size(),0);
    const auto landAt=[&](const TerrainSurfaceCell& c,float x,float z)->bool {
        if(coast.empty())return true;
        const float u=c.uv[0]+(c.uv[2]-c.uv[0])*x,v=c.uv[1]+(c.uv[3]-c.uv[1])*z;
        const int ix=std::max(0,std::min(coastW-1,(int)(u*coastW))),iz=std::max(0,std::min(coastH-1,(int)(v*coastH)));
        return coast[(size_t)iz*coastW+ix]>=250;
    };
    for(const auto& c:cells) {
        if(c.x<0 || c.z<0 || c.x>=g_gridW || c.z>=g_gridH)continue;
        for(int z=0;z<=S.steps;z++)for(int x=0;x<=S.steps;x++) {
            const int gx=c.x*S.steps+x,gz=c.z*S.steps+z;
            const bool land=landAt(c,(float)x/S.steps,(float)z/S.steps);
            if(!land && !waterNodes.empty())waterNodes[(size_t)gz*S.fw+gx]=1;
            if(c.tile<0)continue;
            float h=terrain_surface_tile_height(c.tile,(float)x/S.steps,(float)z/S.steps)*g_fx.terrain_relief_height;
            if(!land)h=0;
            // Building foundations own their native graded footprint and its corners.
            const int cx=c.x+(x==S.steps),cz=c.z+(z==S.steps);
            if(g_padOn[c.z*(g_gridW+1)+c.x] || g_padOn[c.z*(g_gridW+1)+c.x+1] || g_padOn[(c.z+1)*(g_gridW+1)+c.x] || g_padOn[(c.z+1)*(g_gridW+1)+c.x+1])h=0;
            S.lift[(size_t)gz*S.fw+gx]+=h;
        }
    }
    for(int z=0;z<S.fh;z++)for(int x=0;x<S.fw;x++) {
        int count=(x%S.steps==0 && x>0 && x<S.fw-1?2:1)*(z%S.steps==0 && z>0 && z<S.fh-1?2:1);
        S.lift[(size_t)z*S.fw+x]/=(float)count;
        if(!waterNodes.empty() && waterNodes[(size_t)z*S.fw+x])S.lift[(size_t)z*S.fw+x]=0;
    }
    if(S.theater=="RA_SNOW") {
        // Feather the shared snow mound, not each template rectangle. A compact
        // binomial filter carries the foot and its tangent into surrounding snow.
        // Apply the authoritative coast taper afterward, keeping wet nodes flat.
        std::vector<float> tmp(S.lift.size());const int kernel[5]={1,4,6,4,1};const int spacing=std::max(1,S.steps/4);
        for(int z=0;z<S.fh;z++)for(int x=0;x<S.fw;x++) {
            float h=0;for(int k=-2;k<=2;k++)h+=S.lift[(size_t)z*S.fw+std::max(0,std::min(S.fw-1,x+k*spacing))]*kernel[k+2];
            tmp[(size_t)z*S.fw+x]=h*(1.0f/16.0f);
        }
        for(int z=0;z<S.fh;z++)for(int x=0;x<S.fw;x++) {
            float h=0;for(int k=-2;k<=2;k++)h+=tmp[(size_t)std::max(0,std::min(S.fh-1,z+k*spacing))*S.fw+x]*kernel[k+2];
            const int cx=std::min(g_gridW-1,x/S.steps),cz=std::min(g_gridH-1,z/S.steps),cw=g_gridW+1;
            const bool pad=g_padOn[cz*cw+cx]||g_padOn[cz*cw+cx+1]||g_padOn[(cz+1)*cw+cx]||g_padOn[(cz+1)*cw+cx+1];
            S.lift[(size_t)z*S.fw+x]=pad?0:h*(1.0f/16.0f);
        }
    }
    *msLift=terrain_surface_ms(tStage);
    if(!waterNodes.empty()) {
        // Partial-alpha foam/water belongs to the flat live surface too. Bring
        // detail relief down on the LAND side over several shared mesh supports,
        // rather than stretching a painted blue fringe down one vertical quad.
        std::vector<float> distance(S.lift.size());
        water_edt_2d(waterNodes.data(),distance.data(),NULL,S.fw,S.fh);
        const float width=std::max(0.50f,2.0f/(float)S.steps);long wet=0;float maxWet=0;
        for(size_t i=0;i<S.lift.size();i++) {
            const float t=std::max(0.0f,std::min(1.0f,(distance[i]-0.5f)/((float)S.steps*width)));
            const float k=t*t*t*(t*(t*6.0f-15.0f)+10.0f);
            S.lift[i]*=k;
            if(waterNodes[i]){wet++;maxWet=std::max(maxWet,S.lift[i]);}
        }
        fprintf(stderr,"TERRAINSURFACE|coast-cut|water-nodes=%ld|wet-lift-max=%.6f|land-taper=%.3f|normal-cut=atlas-alpha\n",wet,maxWet,width);
    }
}
/* One raised cell's triangles, one triangle corner at a time: the path the node
   evaluation above reproduces, kept whole for OPENCNC3D_SURFACE_NODES=0 and verify. */
static void terrain_surface_cell_corners(const TerrainSurfaceCell& c,std::vector<TerrainSurfaceVert>& out)
{
    const TerrainSurfaceState& S=g_terrainSurface;const int ox[6]={0,0,1,1,0,1},oz[6]={0,1,0,0,1,1};
    for(int z=0;z<S.steps;z++)for(int x=0;x<S.steps;x++)for(int k=0;k<6;k++) {
        const int gx=c.x*S.steps+x+ox[k],gz=c.z*S.steps+z+oz[k];
        const float fx=(float)(x+ox[k])/S.steps,fz=(float)(z+oz[k])/S.steps,wx=c.x+fx,wz=c.z+fz;
        TerrainSurfaceVert v;v.p[0]=wx;v.p[1]=terrain_y_base(wx,wz)+terrain_surface_node(gx,gz);v.p[2]=wz;
        v.uv[0]=c.uv[0]+(c.uv[2]-c.uv[0])*fx;v.uv[1]=c.uv[1]+(c.uv[3]-c.uv[1])*fz;
        v.mu[0]=v.mu[1]=0;
        if(S.material){const int tile=c.tile<0?(int)(S.tiles.size()/(S.cell*S.cell)):c.tile,pitch=S.materialCell+4;
            v.mu[0]=((tile%S.materialCols)*pitch+2+fx*S.materialCell)/(float)(S.materialCols*pitch);
            v.mu[1]=((tile/S.materialCols)*pitch+2+fz*S.materialCell)/(float)(S.materialRows*pitch);}

        // Preserve the native SW-NE interpolation and its encoded lengths.
        // Renormalising bilinear corner normals at every fine vertex changed
        // even zero-relief snow into a visibly different whole-cell patch.
        float base[3]={0,0,0};const int dx[4]={0,1,1,0},dz[4]={0,0,1,1};
        const float wt[4]={std::max(0.0f,1-fx-fz),std::min(fx,1-fz),std::max(0.0f,fx+fz-1),std::min(fz,1-fx)};
        for(int q=0;q<4;q++) {
            float n[3];terrain_corner_normal(c.x+dx[q],c.z+dz[q],n);
            if(n[0]==0 && n[1]==0 && n[2]==0)n[1]=127;
            for(int j=0;j<3;j++) {
                const int encoded=std::max(0,std::min(255,(int)((n[j]/127.0f*.5f+.5f)*255.0f+.5f)));
                base[j]+=(encoded*(2.0f/255.0f)-1.0f)*wt[q];
            }
        }
        const float hx=(terrain_surface_node(gx+1,gz)-terrain_surface_node(gx-1,gz))*S.steps*.5f,hz=(terrain_surface_node(gx,gz+1)-terrain_surface_node(gx,gz-1))*S.steps*.5f;
        const float n[3]={base[0]-hx*base[1],base[1],base[2]-hz*base[1]};
        const float length=std::max(1.0f,sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]));
        for(int j=0;j<3;j++)v.n[j]=n[j]/length*.5f+.5f;
        v.n[3]=1.0f-.08f*std::min(1.0f,terrain_surface_node(gx,gz)/std::max(g_fx.terrain_relief_height*.25f,.001f));
        out.push_back(v);
    }
}
// Optional CPU-geometry QA readback; no rendering or geometry state changes.
// C3DLIFT1: magic[8], ten native uint32 fields (version, fw, fh, steps,
// gridW, gridH, hasWaterNodes, groundGen, padEpoch, coastAtlas), relief float,
// then fw*fh lift floats and, when present, fw*fh water-node bytes.
static void terrain_surface_lift_dump(const std::vector<unsigned char>& water)
{
    const TerrainSurfaceState& S=g_terrainSurface;
    if(const char* path=getenv("OPENCNC3D_TERRAIN_LIFT_DUMP"))if(*path) {
        if(FILE* f=fopen(path,"wb")) {
            const unsigned header[10]={1,(unsigned)S.fw,(unsigned)S.fh,(unsigned)S.steps,
                (unsigned)g_gridW,(unsigned)g_gridH,water.empty()?0u:1u,
                g_groundGen,g_padEpoch,(unsigned)g_terrainSurfaceAtlas};
            const float relief=g_fx.terrain_relief_height;
            bool ok=fwrite("C3DLIFT1",1,8,f)==8 && fwrite(header,4,10,f)==10
                && fwrite(&relief,4,1,f)==1
                && fwrite(S.lift.data(),4,S.lift.size(),f)==S.lift.size();
            if(!water.empty())ok=fwrite(water.data(),1,water.size(),f)==water.size()&&ok;
            if(fclose(f))ok=false;
            fprintf(stderr,"TERRAINSURFACE|lift-dump|%s|nodes=%zu|steps=%d|ok=%d\n",path,S.lift.size(),S.steps,ok?1:0);
        }
    }
}
/* OPENCNC3D_SURFACE_VERIFY=1 builds every surface twice, the fast way and the full
   way, and compares the lift, the coast cut and every vertex byte for byte. */
static bool terrain_surface_verify_on(void)
{ static const bool on=[](){const char* v=getenv("OPENCNC3D_SURFACE_VERIFY");return v && *v && strcmp(v,"0");}();return on; }
static void terrain_surface_build(const std::vector<TerrainSurfaceCell>& cells,const std::string& key)
{
    TerrainSurfaceState& S=g_terrainSurface;if((S.ready || S.failed) && S.key==key)return;
    if(terrain_surface_slots_on() && terrain_surface_patch(cells,key))return;
    const Uint64 tStart=SDL_GetPerformanceCounter();double msRead=0,msLift=0,msBase=0,msVert=0,msUpload=0;
    terrain_surface_release_geometry();S.failed=false;S.key=key;S.source=cells;S.steps=8;
    size_t physicalCells=0;for(const auto& c:cells)if(c.tile>=0)physicalCells++;
    const size_t conservativeCells=std::min(cells.size(),physicalCells*9);
    const size_t bytesPerVertex=sizeof(TerrainSurfaceVert)+10*sizeof(float);
    while(S.steps>1 && ((size_t)(g_gridW*S.steps+1)*(g_gridH*S.steps+1)>8000000
        || conservativeCells*S.steps*S.steps*6*bytesPerVertex>192u*1024u*1024u))S.steps/=2;
    S.fw=g_gridW*S.steps+1;S.fh=g_gridH*S.steps+1;S.lift.assign((size_t)S.fw*S.fh,0);
    S.gen=g_groundGen;S.pad=g_padEpoch;S.triangles=0;
    static const bool baseOn=mesh_perf_switch("OPENCNC3D_SURFACE_BASE"),nodesOn=mesh_perf_switch("OPENCNC3D_SURFACE_NODES");
    const bool verify=terrain_surface_verify_on() && (baseOn || nodesOn);
    long mismatches=0;
    std::vector<unsigned char> waterNodes;bool reused=false;
    if(baseOn) {
        reused=terrain_surface_base_ensure(cells,key,&msRead);
        msBase=terrain_surface_ms(tStart)-msRead;const Uint64 tStage=SDL_GetPerformanceCounter();
        terrain_surface_tail(cells);msLift=terrain_surface_ms(tStage);
        if(verify) {
            const std::vector<float> fast=S.lift;S.lift.assign(fast.size(),0);double r=0,l=0;
            terrain_surface_lift_legacy(cells,waterNodes,&r,&l);
            if(memcmp(fast.data(),S.lift.data(),fast.size()*sizeof(float)) || waterNodes!=S.base.water) {
                size_t i=0;while(i<fast.size() && !memcmp(&fast[i],&S.lift[i],sizeof(float)))i++;
                fprintf(stderr,"TERRAINSURFACE|verify|MISMATCH|lift|node=%zu|water=%d\n",i,waterNodes!=S.base.water?1:0);mismatches++;
            }
            S.lift=fast;waterNodes.clear();
        }
    } else terrain_surface_lift_legacy(cells,waterNodes,&msRead,&msLift);
    const std::vector<unsigned char>& water=baseOn?S.base.water:waterNodes;
    terrain_surface_lift_dump(water);
    Uint64 tStage=SDL_GetPerformanceCounter();
    S.ready=true; // The shared sampler can now supply normals and positions.
    const int ox[6]={0,0,1,1,0,1},oz[6]={0,1,0,0,1,1};
    static std::vector<TerrainSurfaceVert> nodes,check;
    const bool slotted=terrain_surface_slots_on() && terrain_surface_lay_slots(cells);
    for(const auto& c:cells) {
        if(slotted)break;
        if(c.x<0 || c.z<0 || c.x>=g_gridW || c.z>=g_gridH)continue;
        // A zero-height neighbour still shares the relief slope at its edge.
        // Include one normal-sampling support beyond each cell, so its flat
        // geometry carries the same normals as the raised cell beside it.
        bool raised=false;for(int z=-1;z<=S.steps+1&&!raised;z++)for(int x=-1;x<=S.steps+1;x++)if(terrain_surface_node(c.x*S.steps+x,c.z*S.steps+z)>.0001f){raised=true;break;}
        if(!raised)continue;
        TerrainSurfaceRecord rec={c.x,c.z,(int)S.vertices.size(),0,{0,0,0,0},false,true};
        if(nodesOn) {
            terrain_surface_cell_nodes(c,nodes);const int side=S.steps+1;
            for(int z=0;z<S.steps;z++)for(int x=0;x<S.steps;x++)for(int k=0;k<6;k++)S.vertices.push_back(nodes[(size_t)(z+oz[k])*side+x+ox[k]]);
            if(verify) {
                check.clear();terrain_surface_cell_corners(c,check);
                if(check.size()!=S.vertices.size()-rec.first || memcmp(check.data(),&S.vertices[rec.first],check.size()*sizeof(TerrainSurfaceVert))) {
                    if(mismatches<8)fprintf(stderr,"TERRAINSURFACE|verify|MISMATCH|vertices|cell=%d,%d\n",c.x,c.z);mismatches++;
                }
            }
        } else terrain_surface_cell_corners(c,S.vertices);
        rec.count=(int)S.vertices.size()-rec.first;S.triangles+=rec.count/3;S.records.push_back(rec);
    }
    msVert=terrain_surface_ms(tStage);tStage=SDL_GetPerformanceCounter();
    if(verify)fprintf(stderr,"TERRAINSURFACE|verify|%s|mismatches=%ld|vertices=%zu\n",mismatches?"FAILED":"ok",mismatches,S.vertices.size());
    S.colour.assign(S.vertices.size()*4,1);
    S.cbCoordinates.assign(S.vertices.size()*6,0);S.cbMasks.assign(S.records.size(),-2);S.cbEdges.assign(S.records.size()*4,0);S.recordAt.assign((size_t)g_gridW*g_gridH,-1);
    for(size_t i=0;i<S.records.size();i++)if(S.records[i].active)S.recordAt[(size_t)S.records[i].z*g_gridW+S.records[i].x]=(int)i;
    if(grass_buf_load() && !S.vertices.empty()) {
        GLuint* buffers[3]={&S.geo,&S.col,&S.cb};
        const void* streams[3]={S.vertices.data(),S.colour.data(),S.cbCoordinates.data()};
        const size_t sizes[3]={S.vertices.size()*sizeof(TerrainSurfaceVert),S.colour.size()*sizeof(float),S.cbCoordinates.size()*sizeof(float)};
        for(int i=0;i<3;i++) {
            gr_glGenBuffers(1,buffers[i]);gr_glBindBuffer(GL_ARRAY_BUFFER,*buffers[i]);
            gr_glBufferData(GL_ARRAY_BUFFER,(FxGLsizeiptr)sizes[i],streams[i],i==0?GL_STATIC_DRAW:GL_DYNAMIC_DRAW);
            GLint actual=-1;gr_glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&actual);
            if(!*buffers[i] || actual<0 || (size_t)actual!=sizes[i]) {
                gr_glBindBuffer(GL_ARRAY_BUFFER,0);terrain_surface_release_geometry();S.failed=true;S.key=key;
                fprintf(stderr,"TERRAINSURFACE|refused|buffer allocation; native terrain retained\n");return;
            }
        }
        gr_glBindBuffer(GL_ARRAY_BUFFER,0);
    }
    msUpload=terrain_surface_ms(tStage);
    if(verify && slotted)terrain_surface_check_slots(cells,"slots");
    S.liftFrom=S.liftTo=0;S.epoch++;fprintf(stderr,"TERRAINSURFACE|built|%s|cells=%ld|triangles=%ld|steps=%d|height=%.3f|ms=%.1f|base=%s|readback=%.1f|coast=%.1f|lift=%.1f|vertices=%.1f|upload=%.1f\n",
        S.theater.c_str(),(long)(S.triangles/(S.steps*S.steps*2)),S.triangles,S.steps,g_fx.terrain_relief_height,terrain_surface_ms(tStart),
        !baseOn?"off":reused?"reused":"built",msRead,msBase,msLift,msVert,msUpload);
}
/* pass 0 colour, 1 normals, 2 sun depth. The fixed pipeline's shade is flat under
   unified daylight; the common post pass supplies the actual sun and sky response. */
static void terrain_surface_draw(int pass,GLuint atlas)
{
    TerrainSurfaceState& S=g_terrainSurface;if(!terrain_surface_on() || !S.ready || S.vertices.empty() || !S.geo || !S.col)return;
    // Every slot inactive (all relief under buildings) is the old empty surface: no state touched.
    if(S.slotted && S.triangles==0)return;
    glPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_TEXTURE_BIT|GL_POLYGON_BIT|GL_CURRENT_BIT);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);fx_glUseProgram(0);glDisable(GL_CULL_FACE);if(pass!=0)glDisable(GL_BLEND);glDepthFunc(GL_LEQUAL);
    glActiveTexture(GL_TEXTURE0);glClientActiveTexture(GL_TEXTURE0);
    // Colour inherits the parent terrain's feathered coast blend and alpha cut.
    // Data/caster passes stay opaque, with the normal cutout threshold.
    if(pass==1){glDisable(GL_TEXTURE_2D);glDisable(GL_ALPHA_TEST);}else{glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,atlas);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);if(pass==2){glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.5f);}}
    glEnableClientState(GL_VERTEX_ARRAY);glDisableClientState(GL_NORMAL_ARRAY);
    const GLsizei stride=sizeof(TerrainSurfaceVert);
    gr_glBindBuffer(GL_ARRAY_BUFFER,S.geo);glVertexPointer(3,GL_FLOAT,stride,(const void*)offsetof(TerrainSurfaceVert,p));
    if(pass==1) {
        glEnableClientState(GL_COLOR_ARRAY);glColorPointer(4,GL_FLOAT,stride,(const void*)offsetof(TerrainSurfaceVert,n));glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        if(terrain_surface_normal_program()){
            fx_glUseProgram(terrain_surface_normal_program());fx_set1i(terrain_surface_normal_program(),"uMaterial",0);
            fx_set1i(terrain_surface_normal_program(),"uMaterialOn",S.material?1:0);fx_set1i(terrain_surface_normal_program(),"uCutOn",water_fx_wanted()?1:0);fx_set1i(terrain_surface_normal_program(),"uCoast",1);
            glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,g_terrainSurfaceAtlas);
            glClientActiveTexture(GL_TEXTURE1);glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glTexCoordPointer(2,GL_FLOAT,stride,(const void*)offsetof(TerrainSurfaceVert,uv));
            glClientActiveTexture(GL_TEXTURE0);glActiveTexture(GL_TEXTURE0);
            glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,S.material);glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glTexCoordPointer(2,GL_FLOAT,stride,(const void*)offsetof(TerrainSurfaceVert,mu));
        }
    }else{
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);glTexCoordPointer(2,GL_FLOAT,stride,(const void*)offsetof(TerrainSurfaceVert,uv));
        if(pass==0){gr_glBindBuffer(GL_ARRAY_BUFFER,S.col);glEnableClientState(GL_COLOR_ARRAY);glColorPointer(4,GL_FLOAT,0,(const void*)0);}else{glDisableClientState(GL_COLOR_ARRAY);glColor4f(1,1,1,1);}
    }
    std::vector<GLint> first;std::vector<GLsizei> count;
    for(auto& r:S.records) {
        if(!r.active || !cell_shown(r.x,r.z) || (pass!=2 && !cell_in_view(r.x,r.z)))continue;
        if(pass==0) {
            const float L[4]={shroud_corner_vis(r.x,r.z)*(160.0f/255.0f),shroud_corner_vis(r.x+1,r.z)*(160.0f/255.0f),shroud_corner_vis(r.x+1,r.z+1)*(160.0f/255.0f),shroud_corner_vis(r.x,r.z+1)*(160.0f/255.0f)};
            if(!r.known || memcmp(L,r.light,sizeof L)) {
                for(int j=r.first;j<r.first+r.count;j++) {
                    const float x=S.vertices[j].p[0]-r.x,z=S.vertices[j].p[2]-r.z;
                    const float l=x+z<=1 ? L[0]+(L[1]-L[0])*x+(L[3]-L[0])*z : L[2]+(L[3]-L[2])*(1-x)+(L[1]-L[2])*(1-z);
                    for(int k=0;k<3;k++)S.colour[j*4+k]=l;S.colour[j*4+3]=1;
                }
                gr_glBindBuffer(GL_ARRAY_BUFFER,S.col);gr_glBufferSubData(GL_ARRAY_BUFFER,(size_t)r.first*4*sizeof(float),(size_t)r.count*4*sizeof(float),&S.colour[r.first*4]);memcpy(r.light,L,sizeof L);r.known=true;
            }
        }
        if(!first.empty() && first.back()+count.back()==r.first)count.back()+=r.count;else{first.push_back(r.first);count.push_back(r.count);}
    }
    for(size_t i=0;i<first.size();i++)glDrawArrays(GL_TRIANGLES,first[i],count[i]);
    fx_glUseProgram(0);gr_glBindBuffer(GL_ARRAY_BUFFER,0);glPopClientAttrib();glPopAttrib();glActiveTexture(GL_TEXTURE0);glClientActiveTexture(GL_TEXTURE0);
}
static std::string terrain_surface_key(GLuint atlas,unsigned extra)
{
    std::string s((const char*)&g_groundGen,sizeof g_groundGen);s.append((const char*)&g_padEpoch,sizeof g_padEpoch);s.append((const char*)&atlas,sizeof atlas);s.append((const char*)&extra,sizeof extra);s.append((const char*)&g_fx.terrain_relief_height,sizeof g_fx.terrain_relief_height);return s;
}
static void terrain_surface_prepare_td(void)
{
    if(!terrain_surface_on() || g_raModule || !grass_buf_load() || !terrain_surface_load())return;
    terrain_enh_ensure();const PackTex& a=g_pack.tex[terrain_atlas_index()];
    // The atlas must be current before build() reads its authoritative water cut.
    g_terrainSurfaceAtlas=water_terrain_gl(a.gl);const std::string key=terrain_surface_key(g_terrainSurfaceAtlas,desert_detail_state());
    if((g_terrainSurface.ready || g_terrainSurface.failed) && key==g_terrainSurface.key)return;
    int n=0;const TtSlot* table=tt_table(g_pack.theater,&n);std::vector<unsigned> tid(n,65535),icon(n,0);
    for(int i=0;i<n;i++)if(table[i].slot<n){tid[table[i].slot]=table[i].tmpl;icon[table[i].slot]=table[i].icon;}
    std::vector<TerrainSurfaceCell> cells;
    for(const PackCell& c:g_pack.cell) {
        const int slot=ts_slot_of_cell(c,n);if(slot<0)continue;
        TerrainSurfaceCell cell;cell.x=c.x;cell.z=c.y;const int v=enh_cell_variant(c,slot);cell.tile=terrain_surface_td_tile(tid[slot],icon[slot],v);
        const float du=v?g_pack.enhVar[slot].du[v]:0,dv=v?g_pack.enhVar[slot].dv[v]:0;
        cell.uv[0]=(c.u0+du)*a.uw/a.w;cell.uv[1]=(c.v0+dv)*a.uh/a.h;cell.uv[2]=(c.u1+du)*a.uw/a.w;cell.uv[3]=(c.v1+dv)*a.uh/a.h;cells.push_back(cell);
    }
    terrain_surface_build(cells,key);
}
/* The continuous ground material must follow the raised surface too. Reusing the
   old two flat triangles exposes a bright static sand rim where they fail depth.
   Mask/edge streams are cached by the same geometry epoch and per-cell mask key. */
static void terrain_surface_clear_draw(const std::vector<ClearBlendCell>& cells)
{
    TerrainSurfaceState& S=g_terrainSurface;
    if(!terrain_surface_on() || !S.ready || !S.geo || !S.col || !S.cb)return;
    std::vector<GLint> first;std::vector<GLsizei> count;
    for(const auto& c:cells) {
        if(c.x<0 || c.z<0 || c.x>=g_gridW || c.z>=g_gridH)continue;
        const int at=S.recordAt[(size_t)c.z*g_gridW+c.x];if(at<0)continue;
        const TerrainSurfaceRecord& r=S.records[at];
        if(S.cbMasks[at]!=c.grassMask || memcmp(&S.cbEdges[at*4],c.groundEdges,sizeof c.groundEdges)) {
            for(int j=r.first;j<r.first+r.count;j++) {
                const float x=S.vertices[j].p[0]-r.x,z=S.vertices[j].p[2]-r.z;
                terrain_grass_mask_uv(c.grassMask,x,z,&S.cbCoordinates[j*6]);memcpy(&S.cbCoordinates[j*6+2],c.groundEdges,sizeof c.groundEdges);
            }
            gr_glBindBuffer(GL_ARRAY_BUFFER,S.cb);gr_glBufferSubData(GL_ARRAY_BUFFER,(size_t)r.first*6*sizeof(float),(size_t)r.count*6*sizeof(float),&S.cbCoordinates[r.first*6]);S.cbMasks[at]=c.grassMask;memcpy(&S.cbEdges[at*4],c.groundEdges,sizeof c.groundEdges);
        }
        first.push_back(r.first);count.push_back(r.count);
    }
    if(first.empty())return;
    fx_set1i(g_clearBlend.program,"uSurface",1);
    fx_set1i(g_clearBlend.program,"uCoast",5);
    glActiveTexture(GL_TEXTURE5);glBindTexture(GL_TEXTURE_2D,g_terrainSurfaceAtlas);glActiveTexture(GL_TEXTURE0);
    gr_glBindBuffer(GL_ARRAY_BUFFER,S.geo);
    glClientActiveTexture(GL_TEXTURE3);glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2,GL_FLOAT,sizeof(TerrainSurfaceVert),(const void*)offsetof(TerrainSurfaceVert,uv));
    glClientActiveTexture(GL_TEXTURE0);
    gr_glBindBuffer(GL_ARRAY_BUFFER,S.geo);glVertexPointer(3,GL_FLOAT,sizeof(TerrainSurfaceVert),(const void*)offsetof(TerrainSurfaceVert,p));
    // n.w already records the shared mesh's normalized rock rise (1 ground,
    // .92 raised rock). Use that same geometry to keep sand at the cliff foot.
    glClientActiveTexture(GL_TEXTURE2);glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(1,GL_FLOAT,sizeof(TerrainSurfaceVert),(const void*)(offsetof(TerrainSurfaceVert,n)+3*sizeof(float)));
    glClientActiveTexture(GL_TEXTURE0);
    gr_glBindBuffer(GL_ARRAY_BUFFER,S.col);glColorPointer(4,GL_FLOAT,0,(const void*)0);
    gr_glBindBuffer(GL_ARRAY_BUFFER,S.cb);glClientActiveTexture(GL_TEXTURE0);glTexCoordPointer(2,GL_FLOAT,6*sizeof(float),(const void*)0);
    glClientActiveTexture(GL_TEXTURE1);glTexCoordPointer(4,GL_FLOAT,6*sizeof(float),(const void*)(2*sizeof(float)));glClientActiveTexture(GL_TEXTURE0);
    for(size_t i=0;i<first.size();i++)glDrawArrays(GL_TRIANGLES,first[i],count[i]);
    fx_set1i(g_clearBlend.program,"uSurface",0);gr_glBindBuffer(GL_ARRAY_BUFFER,0);
}

#endif
