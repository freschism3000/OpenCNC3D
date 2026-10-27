#ifndef OPENCNC3D_RA3D_ROAD_JOIN_H
#define OPENCNC3D_RA3D_ROAD_JOIN_H
#include "ra3d_road_exits.h"
/* Register road exits at adjoining placements and evaluate a single continuous
   blend on either side of the shared edge. The original atlas supplies every
   pixel; map topology, heights, depth, shroud and gameplay remain unchanged. */
static GLuint g_raRoadProgram=0;
static bool g_raRoadTried=false;
static bool ra3d_road_slot(int slot)
{
    if(slot<0 || slot>=(int)g_ra3d.slot.size())return false;
    const std::string& n=g_ra3d.names[g_ra3d.slot[slot].name];
    return n.size()>1 && n[0]=='D' && n[1]>='0' && n[1]<='9';
}
static bool ra3d_roads_ready()
{
    if(!g_fxActive || !g_fx.enabled || g_fx.dos_classic || g_ra3d.set!=FX_TEX_ENH || g_ra3d.theater=='I' || !fx_gl_ready)return false;
    const char* off=getenv("OPENCNC3D_ROADJOINS");
    if(off && (!strcmp(off,"off") || !strcmp(off,"none")))return false;
    if(g_raRoadTried)return g_raRoadProgram!=0;
    g_raRoadTried=true;
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
    gl_FragColor=vec4(colour/total*shade.rgb,zone);
}
)RJGLSL";
    GLuint v=fx_compile(GL_VERTEX_SHADER,vs,"ra-road-join-vs");
    GLuint f=fx_compile(GL_FRAGMENT_SHADER,fs,"ra-road-join-fs");
    if(v && f){
        GLuint p=fx_glCreateProgram();fx_glAttachShader(p,v);fx_glAttachShader(p,f);fx_glLinkProgram(p);
        GLint ok=0;fx_glGetProgramiv(p,GL_LINK_STATUS,&ok);
        if(ok)g_raRoadProgram=p;else{char log[2048]={0};GLsizei n=0;fx_glGetProgramInfoLog(p,sizeof log-1,&n,log);fprintf(stderr,"RA3D|roadjoins|refused|%s\n",log);fx_glDeleteProgram(p);}
    }
    if(v)fx_glDeleteShader(v);if(f)fx_glDeleteShader(f);
    fprintf(stderr,"RA3D|roadjoins|%s\n",g_raRoadProgram?"ready":"unavailable");
    return g_raRoadProgram!=0;
}
static void ra3d_draw_road_joins()
{
    if(!ra3d_roads_ready())return;
    const Ra3dGround& G=g_ra3d;
    terrain_grass_masks_ensure();
    const int W=g_raStaticMap.MapCellWidth,H=g_raStaticMap.MapCellHeight;
    const int X=g_raStaticMap.MapCellX,Z=g_raStaticMap.MapCellY;
    auto at=[&](int x,int z){return x>=0 && z>=0 && x<W && z<H ? G.cellBase[z*W+x] : -1;};
    auto exits=[&](int x,int z,float* crop){
        const int s=at(x,z);static const int dx[4]={-1,1,0,0},dz[4]={0,0,-1,1};
        for(int e=0;e<4;e++){
            crop[e]=0;const int n=at(x+dx[e],z+dz[e]);if(!ra3d_road_slot(n))continue;
            const Ra3dSlot &a=G.slot[s],&b=G.slot[n];
            if(!ra_road_exits_connect(atoi(G.names[a.name].c_str()+1),a.icon,e,
                                     atoi(G.names[b.name].c_str()+1),b.icon))continue;
            const bool continuous=a.name==b.name && b.icon==a.icon+dx[e]+dz[e]*a.w
                && (dx[e]==0 || a.icon/a.w==b.icon/b.w);
            if(!continuous)crop[e]=0.10f;
        }
    };
    glPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_POLYGON_BIT|GL_TEXTURE_BIT);
    GLint previous=0;glGetIntegerv(GL_CURRENT_PROGRAM,&previous);
    glDisable(GL_ALPHA_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(-1,-1);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,g_terrainGrassMasks.texture);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,G.tex);
    fx_glUseProgram(g_raRoadProgram);fx_set1i(g_raRoadProgram,"atlas",0);fx_set1i(g_raRoadProgram,"groundMasks",1);
    int drawn=0;
    for(int z=0;z<H;z++)for(int x=0;x<W;x++){
        if(!ra3d_road_slot(at(x,z)) || !cell_shown(X+x,Z+z) || !cell_in_view(X+x,Z+z))continue;
        float own[4];exits(x,z,own);if(own[0]+own[1]+own[2]+own[3]==0)continue;
        float tiles[36]={0},crops[36]={0},masks[36]={0};
        for(int k=0;k<9;k++){
            const int nx=x+k%3-1,nz=z+k/3-1,s=at(nx,nz);
            if(!ra3d_road_slot(s) || !cell_shown(X+nx,Z+nz))continue;
            const int drawnSlot=ra3d_cell_slot(nz*W+nx,X+nx,Z+nz);
            ra3d_slot_uv(drawnSlot,&tiles[k*4],&tiles[k*4+1],&tiles[k*4+2],&tiles[k*4+3]);
            exits(nx,nz,&crops[k*4]);
            const Ra3dSlot& material=G.slot[drawnSlot];
            const int mask=terrain_grass_mask_ra(G.names[material.name].c_str(),material.icon,material.var);
            if(mask>0){terrain_grass_mask_uv(mask,0,0,&masks[k*4]);terrain_grass_mask_uv(mask,1,1,&masks[k*4+2]);}
        }
        fx_set2f(g_raRoadProgram,"origin",(float)(X+x),(float)(Z+z));
        fx_set4fv(g_raRoadProgram,"tile[0]",9,tiles);fx_set4fv(g_raRoadProgram,"crop[0]",9,crops);fx_set4fv(g_raRoadProgram,"maskTile[0]",9,masks);
        const float joins[4]={own[0]>0?1.f:0.f,own[1]>0?1.f:0.f,own[2]>0?1.f:0.f,own[3]>0?1.f:0.f};
        fx_set4fv(g_raRoadProgram,"joins",1,joins);
        float light[4];ra3d_cell_light(X+x,Z+z,light);
        const int dx[6]={0,0,1,1,0,1},dz[6]={0,1,0,0,1,1},ci[6]={0,3,1,1,3,2};
        glBegin(GL_TRIANGLES);
        for(int j=0;j<6;j++){const float l=light[ci[j]];glColor4f(l,l,l,1);glVertex3f((float)(X+x+dx[j]),terrain_corner_y(X+x+dx[j],Z+z+dz[j]),(float)(Z+z+dz[j]));}
        glEnd();drawn++;
    }
    fx_glUseProgram((GLuint)previous);glPopAttrib();glColor4f(1,1,1,1);
    static std::string said;if(said!=G.key){said=G.key;fprintf(stderr,"RA3D|roadjoins|scene=%s|cells=%d\n",g_raStaticScene.c_str(),drawn);}
}
#endif
