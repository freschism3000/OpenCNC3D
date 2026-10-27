#ifndef CNC3D_STONE_MATERIALS_H
#define CNC3D_STONE_MATERIALS_H
/* Mineral data is linear, UV-paired and independent of albedo/lighting. The
   runtime payload is normal.xyz + perceptual roughness.a. */
struct StoneMapRecord { unsigned w,h,crc,off,size; };
static bool g_stoneMapsTried=false;
static std::vector<unsigned char> g_stoneMapsBytes;
static std::map<unsigned long long,StoneMapRecord> g_stoneMaps;
static GLuint g_stoneMeshNormalProgram=0;
static unsigned stone_u32(const unsigned char* p)
{ return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24); }
static bool stone_maps_enabled(void)
{ const char* p=getenv("OPENCNC3D_STONE_MAPS");return !p || strcmp(p,"0"); }
static unsigned long long stone_map_key(unsigned crc,unsigned w,unsigned h)
{return ((unsigned long long)crc<<32)|((unsigned long long)w<<16)|h;}
static GLuint stone_data_upload(const unsigned char* pixels,int w,int h)
{
    if(!pixels||w<1||h<1)return 0;
    GLint was=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&was);
    GLuint tex=0;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    std::vector<unsigned char> data(pixels,pixels+(size_t)w*h*4);int level=0;
    for(;;) {
        glTexImage2D(GL_TEXTURE_2D,level++,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,data.data());
        if(w==1&&h==1)break;
        const int nw=std::max(1,w/2),nh=std::max(1,h/2);std::vector<unsigned char> next((size_t)nw*nh*4);
        for(int y=0;y<nh;y++)for(int x=0;x<nw;x++)for(int c=0;c<4;c++) {
            unsigned sum=0,count=0;
            for(int dy=0;dy<2;dy++)for(int dx=0;dx<2;dx++) {int sx=std::min(w-1,x*2+dx),sy=std::min(h-1,y*2+dy);sum+=data[((size_t)sy*w+sx)*4+c];count++;}
            next[((size_t)y*nw+x)*4+c]=(unsigned char)((sum+count/2)/count);
        }
        data.swap(next);w=nw;h=nh;
    }
    glBindTexture(GL_TEXTURE_2D,(GLuint)was);return tex;
}
static void stone_maps_load(void)
{
    if(g_stoneMapsTried)return;g_stoneMapsTried=true;if(!stone_maps_enabled())return;
    const char* roots[]={"","game/","../game/","../"};
    for(auto root:roots) {
        std::string p=std::string(root)+"stonematerials.pack";FILE* f=fopen(p.c_str(),"rb");if(!f)continue;
        fseek(f,0,SEEK_END);long n=ftell(f);if(n>=16&&n<=256L*1024*1024&&fseek(f,0,SEEK_SET)==0){g_stoneMapsBytes.resize(n);if(fread(g_stoneMapsBytes.data(),1,n,f)!=(size_t)n)g_stoneMapsBytes.clear();}fclose(f);break;
    }
    const auto& b=g_stoneMapsBytes;if(b.size()<16||memcmp(b.data(),"C3DSTNM\0",8)||stone_u32(b.data()+8)!=1)return;
    unsigned count=stone_u32(b.data()+12);if(count>1024||16+(size_t)count*20>b.size())return;
    for(unsigned i=0;i<count;i++) {
        const unsigned char* p=b.data()+16+i*20;StoneMapRecord r={stone_u32(p),stone_u32(p+4),stone_u32(p+8),stone_u32(p+12),stone_u32(p+16)};
        if(r.w<1||r.h<1||r.w>4096||r.h>4096||r.off<16+count*20||(size_t)r.off+r.size>b.size()){g_stoneMaps.clear();return;}
        g_stoneMaps[stone_map_key(r.crc,r.w,r.h)]=r;
    }
    fprintf(stderr,"STONEMATERIAL|loaded|uv-paired=%lu\n",(unsigned long)g_stoneMaps.size());
}
static GLuint stone_map_for_rgba(const std::vector<unsigned char>& rgba,int w,int h)
{
    if(!stone_maps_enabled()||rgba.size()!=(size_t)w*h*4)return 0;stone_maps_load();
    unsigned crc=(unsigned)crc32(0,rgba.data(),(uInt)rgba.size());auto i=g_stoneMaps.find(stone_map_key(crc,w,h));if(i==g_stoneMaps.end())return 0;
    const auto& r=i->second;std::vector<unsigned char> pixels((size_t)w*h*4);uLongf n=pixels.size();
    if(uncompress(pixels.data(),&n,g_stoneMapsBytes.data()+r.off,r.size)!=Z_OK||n!=pixels.size())return 0;
    return stone_data_upload(pixels.data(),w,h);
}
/* Called for every textured run of every normal-target draw. The bound program comes
   from the state cache's shadow, and the two uniforms' locations are kept until a
   program is linked or deleted or a frame begins (g_glsProgramEpoch). This is the only
   place that sets either uniform, so their values are kept too and set only when they
   change; a link resets them, and the epoch forgets them with it. */
static void stone_mesh_normal_bind(GLuint tex)
{
    if(!g_stoneMeshNormalProgram)return;
    GLint p=0;glGetIntegerv(GL_CURRENT_PROGRAM,&p);if((GLuint)p!=g_stoneMeshNormalProgram)return;
    if(gls_program_shadow()) {
        static GLuint prog=0;static unsigned epoch=0;static GLint locMap=-1,locOn=-1;static int valMap=-1,valOn=-1;
        if(prog!=g_stoneMeshNormalProgram||epoch!=g_glsProgramEpoch) {
            prog=g_stoneMeshNormalProgram;epoch=g_glsProgramEpoch;valMap=valOn=-1;
            locMap=fx_glGetUniformLocation(prog,"uStoneMap");locOn=fx_glGetUniformLocation(prog,"uStoneMapOn");
        }
        const int on=tex?1:0;
        if(locMap>=0&&valMap!=1){fx_glUniform1i(locMap,1);valMap=1;}
        if(locOn>=0&&valOn!=on){fx_glUniform1i(locOn,on);valOn=on;}
    } else {
        fx_set1i(g_stoneMeshNormalProgram,"uStoneMap",1);fx_set1i(g_stoneMeshNormalProgram,"uStoneMapOn",tex?1:0);
    }
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,tex);glActiveTexture(GL_TEXTURE0);
}
/* Derivative TBN follows the existing UVs, including mirrored islands. Keep the
   geometric normal when a triangle has a degenerate UV Jacobian. */
static const char* STONE_TBN_GLSL =
    "vec3 stoneNormal(vec3 N, vec3 P, vec2 uv, vec3 m) {\n"
    "    vec3 dp1=dFdx(P), dp2=dFdy(P);vec2 a=dFdx(uv), b=dFdy(uv);\n"
    "    vec3 p2=cross(dp2,N), p1=cross(N,dp1);\n"
    "    vec3 T=p2*a.x+p1*b.x, B=p2*a.y+p1*b.y;\n"
    "    float d=max(dot(T,T),dot(B,B));if(d<0.000000000001)return N;\n"
    "    return normalize(mat3(T*inversesqrt(d),B*inversesqrt(d),N)*m);\n"
    "}\n";
#endif
