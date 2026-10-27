#ifndef OPENCNC3D_TD_ROAD_JOIN_H
#define OPENCNC3D_TD_ROAD_JOIN_H
/* Both sides evaluate the same atlas colour field at a continuing road edge.
   Explicit saved ground mattes determine road coverage; diffuse hue is never
   classified. This pass changes colour only, before objects and decals. */
static GLuint td_road_join_program()
{
    static GLuint program=0;static bool tried=false;
    if(tried)return program;tried=true;
    const char* vs="#version 120\nvarying vec2 world; varying vec4 shade;\n"
        "void main(){world=gl_Vertex.xz;shade=gl_Color;gl_Position=ftransform();"
        "gl_ClipVertex=gl_ModelViewMatrix*gl_Vertex;}\n";
    const char* fs=R"RJGLSL(#version 120
uniform sampler2D atlas;
uniform sampler2D groundMasks;
uniform vec2 origin;
uniform vec4 tile[9],crop[9],maskTile[9];
uniform vec4 joins;
varying vec2 world;
varying vec4 shade;
void main(){
    vec2 p=world-origin;
    float zone=max(max(joins.x*(1.0-smoothstep(0.0,0.22,p.x)),
                       joins.y*smoothstep(0.78,1.0,p.x)),
                   max(joins.z*(1.0-smoothstep(0.0,0.22,p.y)),
                       joins.w*smoothstep(0.78,1.0,p.y)));
    // Colour may cross a placement seam, foreground coverage may not.
    if(maskTile[4].z>maskTile[4].x)zone*=1.0-texture2D(groundMasks,mix(maskTile[4].xy,maskTile[4].zw,clamp(p,vec2(0),vec2(1)))).r;
    if(zone<0.00001)discard;
    vec3 colour=vec3(0);float total=0.0;
    for(int k=0;k<9;k++){
        bool hasMask=maskTile[k].z>maskTile[k].x;
        // All world neighbours share one continuous colour field. Saved
        // foreground mattes give diagonal snow zero influence on the road.
        if(tile[k].z<=tile[k].x)continue;
        vec2 q=p-vec2(float(k-3*(k/3)-1),float(k/3-1));
        vec2 w=smoothstep(vec2(-0.16),vec2(0.16),q)
               *(1.0-smoothstep(vec2(0.84),vec2(1.16),q));
        float weight=w.x*w.y;
        if(weight<=0.000001)continue;
        vec2 st=1.0-abs(mod(q,2.0)-1.0);
        st+=crop[k].xz*(1.0-smoothstep(vec2(0),vec2(0.25),st))
                         -crop[k].yw*smoothstep(vec2(0.75),vec2(1),st);
        if(hasMask)weight*=1.0-texture2D(groundMasks,mix(maskTile[k].xy,maskTile[k].zw,st)).r;
        colour+=texture2D(atlas,mix(tile[k].xy,tile[k].zw,st)).rgb*weight;
        total+=weight;
    }
    if(total<=0.000001)discard;
    gl_FragColor=vec4(max(colour/total-shade.rgb,vec3(0))*shade.a,zone);
}
)RJGLSL";
    GLuint v=fx_compile(GL_VERTEX_SHADER,vs,"td-road-join-vs");
    GLuint f=fx_compile(GL_FRAGMENT_SHADER,fs,"td-road-join-fs");
    if(v && f){GLuint p=fx_glCreateProgram();fx_glAttachShader(p,v);fx_glAttachShader(p,f);fx_glLinkProgram(p);
        GLint ok=0;fx_glGetProgramiv(p,GL_LINK_STATUS,&ok);if(ok)program=p;else fx_glDeleteProgram(p);}
    if(v)fx_glDeleteShader(v);if(f)fx_glDeleteShader(f);
    fprintf(stderr,"TD|roadjoins|%s\n",program?"ready":"unavailable");return program;
}
struct TdRoadCell { int index=-1,slot=-1,mask=-1,variant=0; unsigned edge[4]={0,0,0,0}; };
static bool td_road_contact(const TdRoadCell& a,const TdRoadCell& b,int side)
{
    unsigned shared=a.edge[side]&b.edge[side^1];int count=0;
    for(;shared;shared&=shared-1)++count;
    return a.index>=0 && b.index>=0 && count>=3;
}
static void draw_td_road_joins(bool tinted)
{
    if(g_raModule || !g_fxActive || !g_fx.enabled || g_fx.dos_classic || terrain_texset_drawn()!=FX_TEX_ENH || !fx_gl_ready)return;
    // Native Snow/Winter road art retains its existing approved composition.
    if(strcmp(g_pack.theater,"TEMPERAT") && strcmp(g_pack.theater,"DESERT") && strcmp(g_pack.theater,"SAND"))return;
    const char* off=getenv("OPENCNC3D_ROADJOINS");if(off && (!strcmp(off,"off") || !strcmp(off,"none")))return;
    terrain_grass_masks_ensure();const GLuint program=td_road_join_program();if(!program)return;
    const PackTex& atlas=g_pack.tex[terrain_atlas_index()];
    const float us=(float)atlas.uw/atlas.w,vs=(float)atlas.uh/atlas.h;
    static unsigned generation=~0u;static GLuint maskTexture=0,atlasTexture=0;
    static std::vector<TdRoadCell> cells;static std::vector<int> roads;
    if(generation!=g_groundGen || maskTexture!=g_terrainGrassMasks.texture || atlasTexture!=atlas.gl || cells.size()!=(size_t)g_gridW*g_gridH){
        cells.assign((size_t)g_gridW*g_gridH,TdRoadCell());roads.clear();
        int count=0;const TtSlot* table=tt_table(g_pack.theater,&count);std::vector<int> tid(count,-1),icon(count,0);
        for(int i=0;i<count;i++)if(table[i].slot<count){tid[table[i].slot]=table[i].tmpl;icon[table[i].slot]=table[i].icon;}
        for(size_t i=0;i<g_pack.cell.size();i++){
            const PackCell& c=g_pack.cell[i];const int slot=ts_slot_of_cell(c,count);
            if(slot<0 || tid[slot]<93 || tid[slot]>135 || c.x<0 || c.y<0 || c.x>=g_gridW || c.y>=g_gridH)continue;
            const int variant=enh_cell_variant(c,slot),mask=terrain_grass_mask_td(tid[slot],icon[slot],variant);
            if(mask<=0 || ((size_t)mask+1)*24*24>g_terrainGrassMasks.coverage.size())continue;
            TdRoadCell r;r.index=(int)i;r.slot=slot;r.mask=mask;r.variant=variant;
            const unsigned char* coverage=&g_terrainGrassMasks.coverage[(size_t)mask*24*24];
            for(int j=0;j<24;j++){
                if(coverage[j*24]<128)r.edge[0]|=1u<<j;
                if(coverage[j*24+23]<128)r.edge[1]|=1u<<j;
                if(coverage[j]<128)r.edge[2]|=1u<<j;
                if(coverage[23*24+j]<128)r.edge[3]|=1u<<j;
            }
            const int at=c.y*g_gridW+c.x;cells[at]=r;roads.push_back(at);
        }
        generation=g_groundGen;maskTexture=g_terrainGrassMasks.texture;atlasTexture=atlas.gl;
        fprintf(stderr,"TD|roadjoins|scene=%s|source-matte-cells=%zu\n",g_bootScen,roads.size());
    }
    const TdRoadCell empty;
    auto at=[&](int x,int z)->const TdRoadCell&{return x>=0 && z>=0 && x<g_gridW && z<g_gridH?cells[z*g_gridW+x]:empty;};
    static const int dx[4]={-1,1,0,0},dz[4]={0,0,-1,1};
    auto exits=[&](int x,int z,float* values){for(int e=0;e<4;e++)values[e]=td_road_contact(at(x,z),at(x+dx[e],z+dz[e]),e)?1.0f:0.0f;};
    glPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_POLYGON_BIT|GL_TEXTURE_BIT|GL_CURRENT_BIT);
    GLint previous=0;glGetIntegerv(GL_CURRENT_PROGRAM,&previous);
    glDisable(GL_ALPHA_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(-1,-1);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,maskTexture);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,atlas.gl);
    fx_glUseProgram(program);fx_set1i(program,"atlas",0);fx_set1i(program,"groundMasks",1);
    for(int ci:roads){
        const int x=ci%g_gridW,z=ci/g_gridW;if(!cell_shown(x,z) || !cell_in_view(x,z))continue;
        float own[4];exits(x,z,own);if(own[0]+own[1]+own[2]+own[3]==0)continue;
        float tiles[36]={0},crops[36]={0},masks[36]={0};
        for(int k=0;k<9;k++){
            const int ox=k%3-1,oz=k/3-1,nx=x+ox,nz=z+oz;const TdRoadCell& r=at(nx,nz);
            if(r.index<0 || !cell_shown(nx,nz))continue;
            if(ox && !oz && !td_road_contact(at(x,z),r,ox<0?0:1))continue;
            if(oz && !ox && !td_road_contact(at(x,z),r,oz<0?2:3))continue;
            if(ox && oz && !(td_road_contact(at(x,z),at(nx,z),ox<0?0:1) && td_road_contact(at(nx,z),r,oz<0?2:3))
                         && !(td_road_contact(at(x,z),at(x,nz),oz<0?2:3) && td_road_contact(at(x,nz),r,ox<0?0:1)))continue;
            const PackCell& c=g_pack.cell[r.index];float du=0,dv=0;
            if(r.variant){du=g_pack.enhVar[r.slot].du[r.variant];dv=g_pack.enhVar[r.slot].dv[r.variant];}
            tiles[k*4]=(c.u0+du)*us;tiles[k*4+1]=(c.v0+dv)*vs;tiles[k*4+2]=(c.u1+du)*us;tiles[k*4+3]=(c.v1+dv)*vs;
            terrain_grass_mask_uv(r.mask,0,0,&masks[k*4]);terrain_grass_mask_uv(r.mask,1,1,&masks[k*4+2]);
        }
        fx_set2f(program,"origin",(float)x,(float)z);fx_set4fv(program,"tile[0]",9,tiles);fx_set4fv(program,"crop[0]",9,crops);fx_set4fv(program,"maskTile[0]",9,masks);fx_set4fv(program,"joins",1,own);
        float light[4];unsigned char tint[4][3]={{0}};terrain_cell_light(g_pack.cell[at(x,z).index],tinted,light,tint);
        auto vertex=[&](float px,float py,float pz){
            float rgba[4];ts_corner_rgba(light,tint,true,px-x,pz-z,rgba);
            glColor4fv(rgba);glVertex3f(px,py,pz);
        };
        glBegin(GL_TRIANGLES);
        if(terrain_surface_has_cell(x,z)){
            const auto& S=g_terrainSurface;const int index=S.recordAt[(size_t)z*g_gridW+x];const auto& r=S.records[index];
            if(r.active)for(int j=r.first;j<r.first+r.count;j++){const float* p=S.vertices[j].p;vertex(p[0],p[1],p[2]);}
        }else{
            const int vx[6]={0,0,1,1,0,1},vz[6]={0,1,0,0,1,1};
            for(int j=0;j<6;j++)vertex((float)(x+vx[j]),terrain_corner_y(x+vx[j],z+vz[j]),(float)(z+vz[j]));
        }
        glEnd();
    }
    fx_glUseProgram((GLuint)previous);glPopAttrib();
}
#endif
