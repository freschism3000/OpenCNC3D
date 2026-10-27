/* TD service effects on the existing authored surfaces. No geometry/paint files are
   replaced. Binding happens once per pack attachment; all phase meshes and textures
   are persistent and every draw is a normal cached draw_mesh range. */
#include "building_service_bindings.h"
static GLuint ebld_upload_mips(const std::vector<unsigned char>&,int,int,bool);
static void ebld_uv_clear(void);
static const BServiceFace* bservice_faces(int kind, int* n)
{
    if(kind==1){*n=3;return BS_PROC;}
    if(kind==2){*n=13;return BS_HPAD;}
    if(kind==3){*n=1;return BS_FIX;}
    *n=0;return NULL;
}
/* All these authored surfaces are horizontal convex polygons. Test the exact plane
   and every edge, rather than a guessed building-wide region or a colour mask. */
template<class V> static int bservice_role(int kind, const V* v)
{
    int n=0;const BServiceFace* fs=bservice_faces(kind,&n);
    for(int f=0;f<n;f++){
        const BServiceFace& p=fs[f];bool yes=true;
        for(int k=0;k<3 && yes;k++){
            if(fabsf(v[k].y-p.p[0][1])>0.05f){yes=false;break;}
            float sign=0.0f;
            for(int i=0;i<p.n;i++){
                const int j=(i+1)%p.n;
                const float cross=(p.p[j][0]-p.p[i][0])*(v[k].z-p.p[i][2])
                                 -(p.p[j][2]-p.p[i][2])*(v[k].x-p.p[i][0]);
                if(fabsf(cross)<=0.1f*(fabsf(p.p[j][0]-p.p[i][0])+fabsf(p.p[j][2]-p.p[i][2])))continue;
                if(sign==0.0f)sign=cross;else if(sign*cross<0.0f){yes=false;break;}
            }
        }
        if(yes)return f+1;
    }
    return 0;
}
static uint64_t bservice_hash_u32(uint64_t h,uint32_t x)
{
    for(int i=0;i<4;i++){h^=(unsigned char)(x>>(i*8));h*=1099511628211ull;}return h;
}
static uint64_t bservice_hash_float(uint64_t h,float f)
{uint32_t x;memcpy(&x,&f,4);return bservice_hash_u32(h,x);}
static bool bservice_bind(const EbldModel& M,BService& B)
{
    B.kind=0;B.meshes.clear();B.tex.clear();
    if(g_raModule || !mesh_perf_switch("OPENCNC3D_SERVICE"))return false;
    const int kind=M.type=="PROC"?1:M.type=="HPAD"?2:M.type=="FIX"?3:0;
    if(!kind)return false;
    uint64_t h=1469598103934665603ull;int count=0;
    const std::vector<EbldTri>& ts=M.set[EBS_INTACT].tri;
    for(size_t i=0;i<ts.size();i++)if(bservice_role(kind,ts[i].v)){
        const EbldTri& t=ts[i];count++;
        h=bservice_hash_u32(h,(uint32_t)t.tex);h=bservice_hash_u32(h,t.mode);
        h=bservice_hash_u32(h,t.wrap);
        for(int k=0;k<3;k++){
            const EbldVert& v=t.v[k];h=bservice_hash_float(h,v.x);h=bservice_hash_float(h,v.y);
            h=bservice_hash_float(h,v.z);h=bservice_hash_float(h,v.u);h=bservice_hash_float(h,v.v);
        }
    }
    const int want=kind==1?3:kind==2?26:6;
    if(count!=want || h!=BS_HASH[kind-1]){
        fprintf(stderr,"BSERVICE|%s|binding declined|tris=%d|hash=%016llx\n",M.type.c_str(),count,(unsigned long long)h);
        return false;
    }
    B.kind=kind;return true;
}
static int bservice_phase(int kind,int doing,int stage,bool poweroff,bool alive)
{
    if(poweroff || !alive || doing==BS_CONSTRUCTION || stage<0)return 0;
    if(kind==1)return doing==BS_FULL && stage>=6 && stage<12?1:0;
    if(doing!=BS_ACTIVE)return 0;
    if(kind==2)return stage>=0&&stage<7?(stage%6<5?stage%6:0):0;
    if(kind==3){static const int phase[7]={0,1,2,3,2,1,0};return stage<7?phase[stage]:0;}
    return 0;
}
static void bservice_append(PackMesh& out,const PackMesh& src,size_t i)
{
    out.tris.push_back(src.tris[i]);
    if(src.nrm.size()==src.tris.size()*9)
        out.nrm.insert(out.nrm.end(),src.nrm.begin()+i*9,src.nrm.begin()+(i+1)*9);
    if(src.tfl.size()==src.tris.size())out.tfl.push_back(src.tfl[i]);
}
/* Reserve only the three distinct FIX well colours. They inherit the original
   neutral atlas, including damage, bleed and alpha; house paint is never touched. */
static const std::vector<int>& bservice_tex_slots(EbldModel& M,int tx)
{
    std::vector<int>& slots=M.service.tex[tx];
    if(slots.empty())for(int f=0;f<3;f++){
        PackTex p;p.w=p.uw=M.tex[tx].w;p.h=p.uh=M.tex[tx].h;
        p.gl=p.gl_gdi=0;for(int c=0;c<7;c++)p.gl_extra[c]=0;
        p.mips=true;slots.push_back((int)g_pack.tex.size());g_pack.tex.push_back(p);
    }
    return slots;
}
static void bservice_mesh(EbldModel& M,int mi)
{
    if(!M.service.kind || mi<0 || mi>=(int)g_pack.mesh.size() || M.service.meshes.count(mi))return;
    /* Do not hold a pack vector reference while appending derived meshes. */
    const PackMesh src=g_pack.mesh[mi];BServiceMesh s;s.source=mi;
    s.bp.resize(src.tris.size()+1);s.pp.resize(src.tris.size()+1);
    PackMesh body,parts[6];ebld_mesh_begin(body,M.type.c_str(),"servicebody");
    const int nf=M.service.kind==1?2:M.service.kind==2?5:4;
    for(int f=0;f<nf;f++)ebld_mesh_begin(parts[f],M.type.c_str(),"service");
    int selected=0;
    for(size_t i=0;i<src.tris.size();i++){
        const PackTri& t=src.tris[i];const int role=bservice_role(M.service.kind,t.v);
        /* A broken rim in this plane has a different material/UV and must not glow.
           Only the original signal texture, HPAD atlas, or neutral FIX deck atlas. */
        const int tx=t.tex-M.tex0;
        const bool named=role && (M.service.kind==1?tx==4:
                         M.service.kind==2?(tx==0||tx==2):(tx==1||tx==7));
        if(!named)bservice_append(body,src,i);
        else{
            selected++;
            for(int f=0;f<nf;f++){
                bservice_append(parts[f],src,i);PackTri& q=parts[f].tris.back();
                if(M.service.kind==1 && f==0){
                    /* Each lens reads its own authored OFF column from another
                       chase frame. The unused fourth tile is black, not off glass. */
                    const int frame=role%3;
                    for(int c=0;c<3;c++){q.v[c].u=q.v[c].u*.5f+(frame%2)*.5f;
                                             q.v[c].v=q.v[c].v*.5f+(frame/2)*.5f;}
                }else if(M.service.kind==2){
                    const int ring=(role-1==0)?4:(role-1<=4)?3:(role-1<=8)?2:1;
                    if(f==0 || f!=ring)for(int c=0;c<3;c++){
                        /* Original HPAD.SHP dark lamp index189 / lit176, VGA PAL*4. */
                        q.v[c].r=(unsigned char)((unsigned)q.v[c].r*88u/244u);
                        q.v[c].g=(unsigned char)((unsigned)q.v[c].g*84u/212u);
                        q.v[c].b=(unsigned char)((unsigned)q.v[c].b*44u/120u);
                    }
                }else if(M.service.kind==3 && f>0){
                    if(tx<(int)M.tex.size() && M.tex[tx].house==0)
                        q.tex=bservice_tex_slots(M,tx)[f-1];
                }
            }
        }
        s.bp[i+1]=(int)body.tris.size();s.pp[i+1]=(int)parts[0].tris.size();
    }
    if(!selected)return;
    s.body=ebld_mesh_end(body);
    for(int f=0;f<nf;f++)s.part[f]=ebld_mesh_end(parts[f]);
    M.service.meshes[mi]=s;
    fprintf(stderr,"BSERVICE|%s|mesh=%d|selected=%d|phases=%d\n",M.type.c_str(),mi,selected,nf);
}
/* Called only while the source atlas is already decoded for ordinary residency.
   Original FIX.SHP dominant floor palette203 maps to119,14,201 across active stages.
   Apply those exact RGB ratios to the approved neutral well paint, retaining detail. */
static long bservice_upload(EbldModel& M,int tx,const std::vector<unsigned char>& rgba)
{
    std::map<int,std::vector<int> >::const_iterator it=M.service.tex.find(tx);
    if(it==M.service.tex.end())return 0;
    const EbldTex& T=M.tex[tx];if(T.house || rgba.size()!=(size_t)T.w*T.h*4)return 0;
    static const int rgb[3][3]={{116,148,156},{168,168,168},{192,192,208}};
    static const int idle[3]={132,132,156};long bytes=0;
    for(int f=0;f<3;f++){
        PackTex& p=g_pack.tex[it->second[f]];if(p.gl)continue;
        std::vector<unsigned char> px(rgba);
        for(size_t i=0;i<px.size();i+=4)for(int c=0;c<3;c++){
            const unsigned q=((unsigned)px[i+c]*rgb[f][c]+idle[c]/2)/idle[c];
            px[i+c]=(unsigned char)(q>255?255:q);
        }
        p.gl=ebld_upload_mips(px,T.w,T.h,T.bled);bytes+=(long)px.size()*4/3;
    }
    return bytes;
}
/* Range mapping keeps body/lamps separate without walking vertices or allocating on
   a draw. Damage runs and their lifted fragments keep the same original geometry. */
template<class Draw> static void bservice_draw(const EbldModel& M,int mi,int a,int b,
                                              int phase,const Draw& draw)
{
    if(!M.service.kind){draw(mi,a,b);return;}
    std::map<int,BServiceMesh>::const_iterator it=M.service.meshes.find(mi);
    if(it==M.service.meshes.end() || a<0 || b>(int)it->second.bp.size()-1){draw(mi,a,b);return;}
    const BServiceMesh& s=it->second;
    if(s.bp[b]>s.bp[a])draw(s.body,s.bp[a],s.bp[b]);
    if(s.pp[b]>s.pp[a]){
        if(phase<0||phase>5||s.part[phase]<0)phase=0;
        const float su=g_meshUVScaleU,sv=g_meshUVScaleV,ou=g_meshUVOffU,ov=g_meshUVOffV;
        if(M.service.kind==1 && phase==0)ebld_uv_clear();
        draw(s.part[phase],s.pp[a],s.pp[b]);
        g_meshUVScaleU=su;g_meshUVScaleV=sv;g_meshUVOffU=ou;g_meshUVOffV=ov;
    }
}
