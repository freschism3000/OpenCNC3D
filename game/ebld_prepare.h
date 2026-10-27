/* Exact first-use work, scheduled before tactical play. No model catalog scan, worker
   GL, reduced mip chain or partially published texture. The ordinary demand paths
   remain authoritative when loading limits are reached or a hit arrives early. */
#ifndef EBLD_PREPARE_H
#define EBLD_PREPARE_H
#include <new>
struct EbldPrepareCost {
    uint64_t cpu = 0, gpu = 0, transient = 0;
};
struct EbldPrepareState {
    unsigned generation = 0;
    uint64_t cpu = 0, gpu = 0, transientPeak = 0;
    unsigned textures = 0, meshes = 0, steps = 0, failures = 0;
    uint64_t loadingUs = 0, loadingWaitUs = 0, idleUs = 0, constructionUs = 0, maxConstructionUs = 0;
    std::set<uint64_t> rejected;
    std::map<uint64_t, std::pair<uint64_t, uint64_t> > meshCharges;
    bool disabledByFailure = false;
    bool loadingAttempted = false;
    bool requiredAttempted = false, requiredReady = false;
    bool raLoaded = false;                    /* Red Alert's first-tick preparation done */
    unsigned requiredPlanned = 0, requiredCompleted = 0;
    uint64_t requiredCpuBound = 0, requiredGpuBound = 0, requiredUs = 0, requiredWaitUs = 0;
    uint64_t requiredCpuCharged = 0, requiredGpuCharged = 0;
    uint64_t requiredModelCpuReserved = 0;
    unsigned requiredModelsPlanned = 0, requiredModelsCompleted = 0;
    const char* requiredReason = "not-attempted";
    int observedFrame = -2;
    size_t observedObjects = (size_t)-1;
    bool observedTrees = false;
    std::vector<int> models;
    std::set<int> known;
    std::set<int> constructing;
    std::map<int, uint64_t> requiredTextures;
};
static EbldPrepareState g_ebldPrepare;
/* (Red Alert's structures carry several full-size damaged, glow and overlay atlases each, and
   its whole base is prepared on the first tick: eight times the room, still under the headroom
   test where the system reports its memory) */
static uint64_t ebld_prepare_cap(uint64_t tiberianDawn) { return g_raModule ? tiberianDawn * 8u : tiberianDawn; }
#define EBLD_PREPARE_CPU ebld_prepare_cap(128u * 1024u * 1024u)
#define EBLD_PREPARE_GPU ebld_prepare_cap(256u * 1024u * 1024u)
static const uint64_t EBLD_PREPARE_TRANSIENT = 96u * 1024u * 1024u;
static uint64_t ebld_prepare_remaining(uint64_t cap, uint64_t charged)
{
    return charged < cap ? cap - charged : 0;
}
static void ebld_prepare_reset(void)
{
    g_ebldPrepare = EbldPrepareState();
    g_ebldPrepare.generation = g_ebldPrepareGeneration;
}
static bool ebld_prepare_on(void)
{
    return ebld_rule_now() && !g_fx.dos_classic
        && mesh_perf_switch("OPENCNC3D_EBLD_PREPARE");
}
static bool ebld_prepare_required_on(void)
{
    return !g_raModule && ebld_prepare_on() && mesh_perf_switch("OPENCNC3D_EBLD_REQUIRED_PREPARE");
}
static bool ebld_prepare_add_checked(uint64_t& total, uint64_t value)
{
    if (value > ~uint64_t(0) - total) return false;
    total += value;
    return true;
}
/* Decoded pixels never remain queued. Admission includes retained Gold/mask/faction
   arrays, texture variants/mips, conservative vector-capacity slack and the maximum
   simultaneously live decode/upload/bleed buffers of this one texture. */
static EbldPrepareCost ebld_prepare_texture_cost(const EbldModel& M, size_t t)
{
    EbldPrepareCost c;
    const EbldTex& T = M.tex[t];
    const uint64_t pixels = (uint64_t)T.w * T.h, sz = pixels * 4;
    const bool painted = (T.house & 1) != 0;
    const uint64_t raw = sz * (painted ? 2 : 1) + ((T.house & 2) ? pixels : 0);
    uint64_t variants = painted ? 2 : 1;
    bool faction = false;
    if (painted && (T.house & 2)) {
        const std::string sidecar = M.path + ".fclv";
        FILE* f = fopen(sidecar.c_str(), "rb");
        if (f) { faction = true; fclose(f); }
    }
    if (g_liveryLive && g_teamColours && !g_liveryOf.empty() && painted) {
        bool want[LIVERY_COUNT]; livery_wanted(want);
        for (int colour = 0; colour < LIVERY_COUNT; colour++) {
            if (!want[colour]) continue;
            if (team_livery_slot(colour) >= 2) variants++;
            if (faction && (livery_colour_factions(colour) & 2u)) variants++;
        }
    }
    const bool mips = M.version >= 5 && T.role != 6;
    uint64_t chain = sz;
    if (mips) {
        chain = 0;
        for (int w = T.w, h = T.h;;) {
            chain += (uint64_t)w * h * 4;
            if (w == 1 && h == 1) break;
            w = w > 1 ? w / 2 : 1; h = h > 1 ? h / 2 : 1;
        }
    }
    c.gpu = chain * variants;
    if (M.service.tex.count((int)t) && !T.house) c.gpu += chain * 3;
    /* Pixel arrays are resized once from empty; check actual retained capacities
       transactionally before commitment. Sparse faction records conservatively reserve
       twice their maximum data size and cannot outnumber the atlas' pixels. */
    c.cpu = painted ? sz + ((T.house & 2) ? pixels : 0) : 0;
    if (faction) c.cpu += pixels * sizeof(LiveryFactionTexel) * 2;
    c.transient = raw + 65536;
    uint64_t upload = raw + sz + pixels * 2;
    if (T.cover) upload = raw + 2*sz + sz/2;  /* px+cur, first nx and float alpha plane */
    if (M.service.tex.count((int)t) && !T.house)
        upload = std::max(upload, raw + 2*sz + pixels*2);
    if (c.transient < upload) c.transient = upload;
    /* Transaction backups are normally empty placeholders. Charge existing vector
       capacities too, rather than relying on that property for future callers. */
    const auto clone = [](const PackTex& pt) -> uint64_t {
        return 2*(pt.gdi.capacity()+pt.nod.capacity()+pt.mask.capacity()
                 +pt.faction.capacity()*sizeof(LiveryFactionTexel));
    };
    const size_t slot=(size_t)M.tex0+t;
    if(slot>=g_pack.tex.size()){c.transient=EBLD_PREPARE_TRANSIENT+1;return c;}
    c.transient += clone(g_pack.tex[slot]);
    const auto service=M.service.tex.find((int)t);
    if(service!=M.service.tex.end())for(int index:service->second) {
        if(index<0 || (size_t)index>=g_pack.tex.size()){c.transient=EBLD_PREPARE_TRANSIENT+1;return c;}
        c.transient += clone(g_pack.tex[index]);
    }
    return c;
}
/* Actual retained vector capacities, excluding authored model data that was already
   present before preparation. The before/after delta includes generated service meshes,
   ranges, masks, faction records and pack vector growth, not just triangle payloads. */
static uint64_t ebld_prepare_packmesh_bytes(const PackMesh& M)
{
    return M.tris.capacity()*sizeof(PackTri) + M.parts.capacity()*sizeof(PackPart)
        + M.sections.capacity()*sizeof(int) + M.nrm.capacity()*sizeof(float)
        + M.tfl.capacity() + M.sway.capacity() + M.swayPh.capacity()*sizeof(float)
        + M.clips.capacity()*sizeof(PackClip) + M.animMat.capacity()*sizeof(float)
        + M.animVis.capacity() + M.gpuWaveTris.capacity()*sizeof(int);
}
static uint64_t ebld_prepare_footprint(void)
{
    uint64_t n = g_pack.mesh.capacity()*sizeof(PackMesh) + g_pack.tex.capacity()*sizeof(PackTex);
    for (const PackMesh& M : g_pack.mesh) n += ebld_prepare_packmesh_bytes(M);
    for (const PackTex& T : g_pack.tex)
        n += T.gdi.capacity()+T.nod.capacity()+T.mask.capacity()
           + T.faction.capacity()*sizeof(LiveryFactionTexel);
    for (const EbldModel& M : g_ebld) {
        n += ebld_prepare_packmesh_bytes(M.cutTemp) + M.cutAngles.capacity()*sizeof(float);
        for (int k=0;k<EBM_N;k++)
            n += M.rng[k].capacity()*sizeof(int)
               + M.waveMesh[k].capacity()*sizeof(std::pair<int,int>);
        n += M.service.meshes.size()*(sizeof(std::pair<const int,BServiceMesh>)+4*sizeof(void*));
        for (const auto& item : M.service.meshes)
            n += item.second.bp.capacity()*sizeof(int)+item.second.pp.capacity()*sizeof(int);
        n += M.service.tex.size()*(sizeof(std::pair<const int,std::vector<int> >)+4*sizeof(void*));
        for (const auto& item : M.service.tex) n += item.second.capacity()*sizeof(int);
    }
    return n;
}
static bool ebld_prepare_headroom(const EbldPrepareCost& cost)
{
#ifdef _WIN32
    MEMORYSTATUSEX mem = {}; mem.dwLength = sizeof(mem);
    if (!GlobalMemoryStatusEx(&mem)) return false;
    uint64_t reserve = 256u * 1024u * 1024u;
    if (!ebld_prepare_add_checked(reserve, cost.cpu)
        || !ebld_prepare_add_checked(reserve, cost.gpu)
        || !ebld_prepare_add_checked(reserve, cost.transient)) return false;
    if (mem.ullAvailVirtual < reserve || mem.ullAvailPhys < reserve) return false;
#else
    (void)cost;
#endif
    return true;
}
static uint64_t ebld_prepare_mesh_cost(const EbldModel& M)
{
    const int kind = M.cutNext;
    const EbldSet& S = M.set[kind < 11 ? EBS_CUT : EBS_DESTROYED];
    /* Every source triangle can appear at most once in this original mesh. Geometry,
       normals, flags, pieces/ranges, waving records and vector growth are included.
       Service-split meshes (rare FIX/HPAD) are charged as four copies conservatively. */
    const uint64_t tri = sizeof(PackTri) + 9 * sizeof(float) + 1
                       + 6 * sizeof(float) + 2 * sizeof(int);
    const uint64_t piece = sizeof(PackPart) + EBR * sizeof(int);
    return 2 * (S.tri.size() * tri + S.piece.size() * piece)
             * (M.service.kind ? 8 : 1)
             + (kind == 0 ? S.tri.size() * 3 * sizeof(float) : 0);
}
/* Observe only actual world/construction objects. The list is bounded by models
   already attached/read for this mission, not every type in the installation. */
static void ebld_prepare_observe_impl(void)
{
    if (g_ebldPrepare.generation != g_ebldPrepareGeneration) ebld_prepare_reset();
    if (!ebld_prepare_on()) return;
    if (g_ebldPrepare.observedFrame == g_engineFrame
        && g_ebldPrepare.observedObjects == g_objects.size()
        && g_ebldPrepare.observedTrees == (g_fx.tree3d != 0)) return;
    g_ebldPrepare.observedFrame = g_engineFrame;
    g_ebldPrepare.observedObjects = g_objects.size();
    g_ebldPrepare.observedTrees = g_fx.tree3d != 0;
    g_ebldPrepare.constructing.clear();
    g_ebldPrepare.requiredTextures.clear();
    const bool stageAware = mesh_perf_switch("OPENCNC3D_EBLD_STAGE_RESIDENCY");
    const auto noteModel = [stageAware](int index, int stage, bool constructing) {
        if (index < 0 || (size_t)index >= g_ebld.size()) return;
        const EbldModel& model = g_ebld[index];
        if (!model.attached || model.texOwner >= 0) return;
        if (g_ebldPrepare.known.insert(index).second) g_ebldPrepare.models.push_back(index);
        if (constructing) g_ebldPrepare.constructing.insert(index);
        if (stageAware) g_ebldPrepare.requiredTextures[index] |= ebld_texture_mask_for_stage(model, stage);
    };
    const auto note = [&](const char* rawType, bool constructing) {
        const char* type = ebld_alias(rawType);
        for (size_t i = 0; i < g_ebld.size(); i++) {
            const EbldModel& M = g_ebld[i];
            if (!M.attached || M.texOwner >= 0 || M.type != type) continue;
            noteModel((int)i, EB_INTACT, constructing);
            break;
        }
    };
    for (const SimObject& o : g_objects) {
        if ((o.kind != K_BUILDING && o.kind != K_TERRAIN) || o.limbo || o.str <= 0) continue;
        const bool ordinaryTree=o.type[0]=='T' && o.type[1]>='0' && o.type[1]<='9';
        if (o.kind == K_TERRAIN && ordinaryTree && !g_fx.tree3d) continue;
        if (g_raModule && o.kind == K_BUILDING) ebld_model_of(o.type);
        const bool constructing = o.kind == K_BUILDING && building_constructing(o);
        const EbldBld* entry = ebld_find(o);
        if (stageAware && entry && entry->model >= 0 && (size_t)entry->model < g_ebld.size())
            noteModel(entry->model, entry->stage, constructing);
        else note(o.type, constructing);
    }
    if (stageAware) for (const auto& item : g_ebldBld)
        if (item.second.stage == EB_WRECKED && !item.second.orphan)
            noteModel(item.second.model, EB_WRECKED, false);
    /* The authoritative mirrored queue names the type while it is still being made,
       often seconds before placement and its first damage opportunity. Busy alone is
       per-column and must never prepare every available cameo. */
    if (g_sbState.valid)
        for (const SbEntry& entry : g_sbState.entry)
            if (entry.type == BUILDING_TYPE && (entry.constructing || entry.onHold || entry.completed))
                note(entry.name, true);

}
static void ebld_prepare_observe(void)
{
    try { ebld_prepare_observe_impl(); }
    catch (const std::bad_alloc&) {
        g_ebldPrepare.disabledByFailure=true;g_ebldPrepare.failures++;
    }
}
/* Optional preparation must not turn an allocation/driver refusal into a game
   failure. Roll back only the currently unpublished texture/service slots; exact
   demand can retry later. No prepared texture is ever exposed half-uploaded. */
static void ebld_prepare_restore_texture(PackTex& current, PackTex& previous)
{
    GLuint now[18] = {current.gl,current.gl_gdi}, old[18] = {previous.gl,previous.gl_gdi};
    for (int i=0;i<7;i++) { now[i+2]=current.gl_extra[i]; old[i+2]=previous.gl_extra[i]; }
    for (int i=0;i<8;i++) { now[i+9]=current.gl_faction[i]; old[i+9]=previous.gl_faction[i]; }
    now[17]=current.gl_spec; old[17]=previous.gl_spec;        /* (its specular map) */
    for (int i=0;i<18;i++) if (now[i]) {
        bool retained=false, duplicate=false;
        for(int j=0;j<18;j++) if(now[i]==old[j])retained=true;
        for(int j=0;j<i;j++) if(now[i]==now[j])duplicate=true;
        if (retained || duplicate) continue;
        /* a name shared with another tree's file (ebld_share_key) goes only with its last
           holder, and takes its table entry with it */
        const auto ref=g_ebldShareRefs.find(now[i]);
        if (ref!=g_ebldShareRefs.end()) {
            if (--ref->second>0) continue;
            g_ebldShareRefs.erase(ref);
            for (auto e=g_ebldShare.begin(); e!=g_ebldShare.end();)
                if (e->second.gl==now[i]) e=g_ebldShare.erase(e); else ++e;
        }
        fx_filter_forget(now[i]); glDeleteTextures(1,&now[i]);
    }
    current = std::move(previous);
}
static bool ebld_prepare_texture_safe(EbldModel& M, size_t texture, bool all,
                                        uint64_t cpuRemaining)
{
    GLint bound=0; glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);
    size_t indices[4] = {(size_t)M.tex0+texture,0,0,0};
    PackTex saved[4]; int count=0, wanted=1;
    const auto service=M.service.tex.find((int)texture);
    if (service!=M.service.tex.end())
        for(int index:service->second) if(wanted<4)indices[wanted++]=(size_t)index;
    const bool resident=M.resident, residentAll=M.residentAll, attached=M.attached;
    const long bytes=g_ebldResidentBytes;
    const uint64_t footprintBefore=ebld_prepare_footprint();
    try {
        bool valid=true;
        for(int i=0;i<wanted;i++) {
            if(indices[i]>=g_pack.tex.size()){valid=false;break;}
            saved[i]=g_pack.tex[indices[i]]; count++;
        }
        const bool decoded=valid && ebld_resident(M,all,1,texture);
        bool complete=decoded && g_pack.tex[indices[0]].gl!=0;
        if (decoded && (M.tex[texture].house&1) && !g_pack.tex[indices[0]].gl_gdi) complete=false;
        for(int i=0;i<count && complete;i++) {
            const PackTex& pt=g_pack.tex[indices[i]];
            GLuint ids[17]={pt.gl,pt.gl_gdi};
            for(int k=0;k<7;k++)ids[k+2]=pt.gl_extra[k];
            for(int k=0;k<8;k++)ids[k+9]=pt.gl_faction[k];
            for(GLuint id:ids)if(id) {
                glBindTexture(GL_TEXTURE_2D,id);GLint width=0;
                glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&width);
                if(width!=pt.w) { complete=false;break; }
            }
        }
        glBindTexture(GL_TEXTURE_2D,(GLuint)bound);
        const uint64_t footprintAfter=ebld_prepare_footprint();
        if(footprintAfter>footprintBefore && footprintAfter-footprintBefore>cpuRemaining) complete=false;
        if (complete) return true;
    } catch (const std::bad_alloc&) {
        /* The cleanup below owns only newly created names, never the caller's. */
    }
    for(int i=0;i<count;i++) ebld_prepare_restore_texture(g_pack.tex[indices[i]],saved[i]);
    M.resident=resident;M.residentAll=residentAll;M.attached=attached;g_ebldResolveGen++;
    g_ebldResidentBytes=bytes;
    glBindTexture(GL_TEXTURE_2D,(GLuint)bound);
    g_ebldPrepare.disabledByFailure=true;
    fprintf(stderr,"EBLDPREPARE|deferred|reason=allocation-or-texture-refusal\n");
    return false;
}
static bool ebld_prepare_mesh_safe(EbldModel& M)
{
    const size_t meshes=g_pack.mesh.size(),textures=g_pack.tex.size();
    const int kind=M.cutNext;
    const int mk=kind<11?(kind<3?EBM_CUT+kind:EBM_OVER0+kind-3):(kind==11?EBM_WRECK:EBM_GLOWX);
    const int oldMesh=M.mesh[mk];
    BService service;
    bool copied=false;
    try {
        if (M.service.kind) { service=M.service; copied=true; }
        return ebld_build_cut_step(M);
    } catch (const std::bad_alloc&) {
        g_pack.mesh.resize(meshes);g_pack.tex.resize(textures);
        if(copied)M.service=std::move(service);
        M.mesh[mk]=oldMesh;M.cutTemp=PackMesh();M.cutPiece=0;M.cutBlock=0;
        std::vector<float>().swap(M.cutAngles);
        std::vector<int>().swap(M.rng[mk]);
        std::vector<std::pair<int,int> >().swap(M.waveMesh[mk]);
        g_ebldPrepare.disabledByFailure=true;
        g_ebldPrepare.failures++;
        fprintf(stderr,"EBLDPREPARE|deferred|reason=allocation-refusal\n");
        return false;
    }
}
/* One indivisible original job. Texture jobs can take tens of milliseconds and are
   therefore permitted ONLY while loading or explicitly paused, never under a nominal
   per-frame budget. Mesh kinds may run during construction, one per frame at most. */
static bool ebld_prepare_step_impl(bool texturesAllowed, bool constructionOnly)
{
    if (!ebld_prepare_on()) return false;
    if (g_ebldPrepare.generation != g_ebldPrepareGeneration) ebld_prepare_reset();
    if (g_ebldPrepare.disabledByFailure) return false;
    for (int phase = 0; phase < 3; phase++)
    for (int index : g_ebldPrepare.models) {
        if (index < 0 || index >= (int)g_ebld.size()) continue;
        EbldModel& M = g_ebld[index];
        if (!M.attached || M.texOwner >= 0) continue;
        if (phase != 1) {
            if (!texturesAllowed) continue;
            for (size_t t = 0; t < M.tex.size(); t++) {
                bool required = M.texIntact[t] != 0;
                if (mesh_perf_switch("OPENCNC3D_EBLD_STAGE_RESIDENCY")) {
                    const auto wanted = g_ebldPrepare.requiredTextures.find(index);
                    required = wanted != g_ebldPrepare.requiredTextures.end()
                        && (t >= 64 || (wanted->second & (uint64_t(1) << t)) != 0);
                }
                if ((phase == 0) != required) continue;
                if ((size_t)M.tex0 + t >= g_pack.tex.size() || g_pack.tex[M.tex0 + t].gl) continue;
                const uint64_t job = ((uint64_t)(unsigned)index << 32) | (uint64_t)t;
                if (g_ebldPrepare.rejected.count(job)) continue;
                const EbldPrepareCost cost = ebld_prepare_texture_cost(M, t);
                if (cost.cpu > ebld_prepare_remaining(EBLD_PREPARE_CPU,g_ebldPrepare.cpu)
                    || cost.gpu > ebld_prepare_remaining(EBLD_PREPARE_GPU,g_ebldPrepare.gpu)
                    || cost.transient > EBLD_PREPARE_TRANSIENT || !ebld_prepare_headroom(cost)) {
                    g_ebldPrepare.rejected.insert(job); continue;
                }
                const long before = g_ebldResidentBytes;
                const uint64_t cpuBefore = ebld_prepare_footprint();
                if (!ebld_prepare_texture_safe(M, t, !M.texIntact[t], ebld_prepare_remaining(EBLD_PREPARE_CPU,g_ebldPrepare.cpu))) {
                    g_ebldPrepare.failures++;
                    if (g_ebldPrepare.disabledByFailure) return false;
                    if (!M.attached) break;
                    continue;
                }
                const uint64_t cpuAfter = ebld_prepare_footprint();
                g_ebldPrepare.cpu += cpuAfter > cpuBefore ? cpuAfter - cpuBefore : 0;
                g_ebldPrepare.gpu += cost.gpu;
                if (cost.transient > g_ebldPrepare.transientPeak) g_ebldPrepare.transientPeak = cost.transient;
                if (g_ebldResidentBytes != before) g_ebldPrepare.textures++;
                g_ebldPrepare.steps++; return true;
            }
        } else if (M.nset >= 2 && !M.cutBuilt) {
            if (constructionOnly) {
                if (!g_ebldPrepare.constructing.count(index)) continue;
                /* A pathological single-piece source remains on the demand/loading
                   path: a piece is the smallest atomic unit preserving its ranges. */
                bool bounded = M.service.kind == 0;
                for (int set = EBS_CUT; bounded && set <= EBS_DESTROYED; set++)
                    for (const EbldPiece& piece : M.set[set].piece)
                        if (piece.ntri > 2048) { bounded = false; break; }
                if (!bounded) continue;
            }
            const uint64_t job = ((uint64_t)(unsigned)index << 32) | 0x80000000u | (unsigned)M.cutNext;
            if (g_ebldPrepare.rejected.count(job)) continue;
            const uint64_t bytes = M.cutBlock == 0 && M.cutPiece == 0 ? ebld_prepare_mesh_cost(M) : 0;
            EbldPrepareCost cost; cost.cpu = bytes;
            if (bytes > ebld_prepare_remaining(EBLD_PREPARE_CPU,g_ebldPrepare.cpu) || !ebld_prepare_headroom(cost)) {
                g_ebldPrepare.rejected.insert(job); continue;
            }
            if (bytes) {
                g_ebldPrepare.meshCharges[job] = std::make_pair(bytes, ebld_prepare_footprint());
                g_ebldPrepare.cpu += bytes;  /* reserve whole next mesh before its first slice */
            }
            if (!ebld_prepare_mesh_safe(M)) {
                if (g_ebldPrepare.disabledByFailure) return false;
                continue;
            }
            if (M.cutBlock == 0 && M.cutPiece == 0) {
                g_ebldPrepare.meshes++;
                const auto charge = g_ebldPrepare.meshCharges.find(job);
                if (charge != g_ebldPrepare.meshCharges.end()) {
                    const uint64_t current = ebld_prepare_footprint();
                    const uint64_t retained = current > charge->second.second ? current - charge->second.second : 0;
                    g_ebldPrepare.cpu -= charge->second.first;
                    g_ebldPrepare.cpu += retained;
                    g_ebldPrepare.meshCharges.erase(charge);
                    /* The delta conservatively includes shared-container/ordinary
                       demand growth since this mesh began. Stop optional work if
                       that total exhausts the admission budget; never wrap remaining. */
                    if (g_ebldPrepare.cpu > EBLD_PREPARE_CPU)
                        g_ebldPrepare.disabledByFailure=true;
                }
            }
            g_ebldPrepare.steps++; return true;
        }
    }
    return false;
}
static bool ebld_prepare_step(bool texturesAllowed, bool constructionOnly = false)
{
    try { return ebld_prepare_step_impl(texturesAllowed, constructionOnly); }
    catch (const std::bad_alloc&) {
        g_ebldPrepare.disabledByFailure=true;g_ebldPrepare.failures++;
        return false;
    }
}
struct EbldRequiredTexture {
    int model;
    size_t texture;
    EbldPrepareCost cost;
};
static bool ebld_prepare_attached_type(const std::string& type)
{
    for (const EbldModel& model : g_ebld)
        if (model.type == type && model.wallMask < 0 && !(model.flags & EBX_WALL)
            && ebld_meshes_ok(model)) return true;
    return false;
}
static bool ebld_prepare_source_cost(const std::string& path, const std::string& type,
                                     bool optional, uint64_t& bytes)
{
    FILE* source = fopen(path.c_str(), "rb");
    if (!source) return optional;
    unsigned char header[32] = {};
    const bool sized = fseek(source, 0, SEEK_END) == 0;
    const long length = sized ? ftell(source) : -1;
    const bool read = length >= 32 && fseek(source, 0, SEEK_SET) == 0
                   && fread(header, 1, sizeof(header), source) == sizeof(header);
    fclose(source);
    if (!read || length > 4 * 1024 * 1024 || memcmp(header, "EBLD", 4)
        || type.size() > 8 || memcmp(header + 8, type.c_str(), type.size())) return false;
    uint32_t version = 0, sets = 0;
    memcpy(&version, header + 4, 4); memcpy(&sets, header + 20, 4);
    if (version < 3 || version > 6 || sets != 1) return false;
    return ebld_prepare_add_checked(bytes, (uint64_t)length);
}
static bool ebld_prepare_required_models(void)
{
    EbldPrepareState& state = g_ebldPrepare;
    std::vector<std::string> types;
    shroud_corners_sync();
    if (g_clumpTrees && g_fx.tree3d) for (const SimObject& object : g_objects) {
        if (object.kind != K_TERRAIN || !visible(object) || mesh_for(object) >= 0) continue;
        const ClumpDef* clump = clump_def(object.type);
        /* a clump with a file of its own is drawn whole from it (ebld_terr.h), not composed
           of these single trees; its file is read at first sight, as every lazy type is
           (it has no pack type and is larger than this plan's bounded read takes) */
        if (clump && ebld_type_has_file(object.type)) continue;
        for (int partIndex = 0; partIndex < clump_def_count(clump); partIndex++) {
            const SimObject part = clump_part_n(object, clump, partIndex);
            if (!clump_cell_drawn(part.cx, part.cy)) continue;
            const std::string type = part.type;
            if (type != "T01" && type != "T02" && type != "T05" && type != "T07") continue;
            if (!ebld_prepare_attached_type(type)
                && std::find(types.begin(), types.end(), type) == types.end()) types.push_back(type);
        }
    }
    state.requiredModelsPlanned = (unsigned)types.size();
    if (!types.empty() && !mesh_perf_switch("OPENCNC3D_EBLD_REQUIRED_MODELS")) {
        state.requiredReason = "model-disabled"; return false;
    }
    std::vector<EbldPrepareCost> costs;
    EbldPrepareCost aggregate;
    const int theater = ebld_pack_theater();
    for (const std::string& type : types) {
        uint64_t sourceBytes = 0;
        bool found = false;
        for (const EbldFile& file : g_ebldFiles) {
            if (file.tried || !file.lazy || type != EBLD_TYPES[file.ti]
                || (file.th >= 0 && file.th != theater)) continue;
            found = true;
            if (file.path.size() < 5 || !ebld_prepare_source_cost(file.path, type, false, sourceBytes)) {
                state.requiredReason = "model-source-refusal"; return false;
            }
            const std::string base = file.path.substr(0, file.path.size() - 5);
            if (!ebld_prepare_source_cost(base + "_BURN.ebld", type, true, sourceBytes)
                || !ebld_prepare_source_cost(base + "_SNAG.ebld", type, true, sourceBytes)) {
                state.requiredReason = "model-source-refusal"; return false;
            }
        }
        if (!found) { state.requiredReason = "model-unavailable"; return false; }
        EbldPrepareCost cost;
        cost.cpu = sourceBytes * 16;
        cost.transient = EBLD_PREPARE_TRANSIENT;
        const auto cartridge = g_pack.type.find(type);
        if (cartridge == g_pack.type.end()) { state.requiredReason = "model-cartridge-unavailable"; return false; }
        uint64_t shadowTriangles = 0;
        if (cartridge->second.mesh >= 0 && (size_t)cartridge->second.mesh < g_pack.mesh.size())
            for (const PackTri& triangle : g_pack.mesh[cartridge->second.mesh].tris)
                if (triangle.mode == MODE_SHADOW) shadowTriangles++;
        const uint64_t attachmentBytes = (shadowTriangles + 128) * 12
                                       * (sizeof(PackTri) + 9 * sizeof(float) + 1);
        const uint64_t containers = g_ebld.capacity() * sizeof(EbldModel)
                                  + g_pack.mesh.capacity() * sizeof(PackMesh)
                                  + g_pack.tex.capacity() * sizeof(PackTex);
        if (sourceBytes > EBLD_PREPARE_CPU / 16 || !ebld_prepare_add_checked(cost.cpu, attachmentBytes)
            || !ebld_prepare_add_checked(cost.cpu, containers)
            || cost.cpu > EBLD_PREPARE_CPU) {
            state.requiredReason = "model-budget"; return false;
        }
        if (!ebld_prepare_add_checked(aggregate.cpu, cost.cpu)) {
            state.requiredReason = "model-cost-overflow"; return false;
        }
        aggregate.transient = EBLD_PREPARE_TRANSIENT;
        costs.push_back(cost);
    }
#ifndef _WIN32
    if (aggregate.cpu > ebld_prepare_remaining(EBLD_PREPARE_CPU, state.cpu)) {
        state.requiredReason = "model-platform-budget"; return false;
    }
#endif
    if (!types.empty() && !ebld_prepare_headroom(aggregate)) {
        state.requiredReason = "model-headroom"; return false;
    }
    for (size_t index = 0; index < types.size(); index++) {
        if (SDL_QuitRequested()) { state.requiredReason = "quit"; return false; }
        if (!ebld_prepare_headroom(aggregate)) { state.requiredReason = "model-headroom"; return false; }
        const size_t modelCount = g_ebld.size(), meshCount = g_pack.mesh.size(), textureCount = g_pack.tex.size();
        const unsigned serial = g_ebldPackSerial;
        const uint64_t before = ebld_prepare_footprint() + g_ebld.capacity() * sizeof(EbldModel);
        std::vector<bool> tried, attached;
        for (const EbldFile& file : g_ebldFiles) tried.push_back(file.tried);
        for (const EbldModel& model : g_ebld) attached.push_back(model.attached);
        bool loaded = false;
        try {
            loaded = ebld_ensure(types[index].c_str(), 4u * 1024u * 1024u)
                  && ebld_prepare_attached_type(types[index]);
        } catch (const std::bad_alloc&) {
            loaded = false;
        }
        const uint64_t grown = ebld_prepare_footprint() + g_ebld.capacity() * sizeof(EbldModel);
        if (grown > before && grown - before > costs[index].cpu) loaded = false;
        if (!loaded) {
            g_pack.mesh.resize(meshCount); g_pack.tex.resize(textureCount); g_ebld.resize(modelCount);
            for (size_t item = 0; item < tried.size(); item++) g_ebldFiles[item].tried = tried[item];
            for (size_t item = 0; item < attached.size(); item++) g_ebld[item].attached = attached[item];
            g_ebldPackSerial = serial; g_ebldResolveGen++;
        }
        const uint64_t after = ebld_prepare_footprint() + g_ebld.capacity() * sizeof(EbldModel);
        const uint64_t retained = after > before ? after - before : 0;
        const uint64_t charge = loaded ? costs[index].cpu : retained;
        if (!ebld_prepare_add_checked(state.cpu, charge)
            || !ebld_prepare_add_checked(state.requiredCpuCharged, charge)) {
            state.disabledByFailure = true; state.requiredReason = "model-charge-overflow"; return false;
        }
        if (!loaded) { state.failures++; state.requiredReason = "model-load-refusal"; return false; }
        state.requiredModelCpuReserved += costs[index].cpu - retained;
        state.requiredModelsCompleted++;
        state.transientPeak = std::max(state.transientPeak, costs[index].transient);
        aggregate.cpu -= costs[index].cpu;
        SDL_PumpEvents();
        if (g_au) audio_frame(g_au, 0);
    }
    return true;
}
static std::map<int, uint64_t> ebld_prepare_required_census(void)
{
    std::map<int, uint64_t> masks;
    const auto note = [&](int index, int stage) {
        if (index < 0 || (size_t)index >= g_ebld.size()) return;
        bool shared = false;
        for (size_t depth = 0; g_ebld[index].texOwner >= 0; depth++) {
            if (depth >= g_ebld.size()) return;
            index = g_ebld[index].texOwner;
            if (index < 0 || (size_t)index >= g_ebld.size()) return;
            shared = true;
        }
        const EbldModel& model = g_ebld[index];
        if (!model.attached) return;
        masks[index] |= shared ? ebld_all_texture_mask(model) : ebld_texture_mask_for_stage(model, stage);
    };
    const auto noteObject = [&](const SimObject& object) {
        if (object_model3d(object) >= 0 || building_constructing(object)
            || construction_frac(object) < 1.0f) return;
        if (object.kind == K_BUILDING && object.str <= 0) return;
        const bool ordinaryTree = object.type[0] == 'T' && object.type[1] >= '0' && object.type[1] <= '9';
        if (object.kind == K_TERRAIN && ordinaryTree && !g_fx.tree3d) return;
        const EbldBld* entry = ebld_find(object);
        if (object.kind == K_BUILDING && entry && entry->model >= 0 && (size_t)entry->model < g_ebld.size()) {
            note(entry->model, entry->stage);
            return;
        }
        const char* type = ebld_alias(object.type);
        for (size_t index = 0; index < g_ebld.size(); index++) {
            const EbldModel& model = g_ebld[index];
            if (!model.attached || model.type != type || model.wallMask >= 0 || (model.flags & EBX_WALL)) continue;
            if (!ebld_meshes_ok(model)) break;
            int selected = (int)index;
            if (object.kind == K_TERRAIN) {
                if (model.nset != 1) break;
                if (entry && entry->treeStage > 0 && entry->model >= 0 && (size_t)entry->model < g_ebld.size()
                    && g_ebld[entry->model].stageOf == (int)index) {
                    if (ebld_meshes_ok(g_ebld[entry->model])) selected = entry->model;
                } else if (object.str <= 0) {
                    selected = model.stageModel[2];
                    if (!g_etreeOn || !ordinaryTree || !g_fx.tree3d || selected < 0
                        || (size_t)selected >= g_ebld.size() || !ebld_meshes_ok(g_ebld[selected])) break;
                }
            }
            note(selected, EB_INTACT);
            break;
        }
    };
    shroud_corners_sync();
    for (const SimObject& object : g_objects) {
        if ((object.kind != K_BUILDING && object.kind != K_TERRAIN) || !visible(object)) continue;
        /* (a clump with a file of its own is noted whole: ebld_terr.h draws it so) */
        if (object.kind == K_TERRAIN && mesh_for(object) < 0
            && !(ebld_packless_type(object.type) && ebld_type_has_file(object.type))) {
            const ClumpDef* clump = g_clumpTrees && g_fx.tree3d ? clump_def(object.type) : NULL;
            for (int partIndex = 0; partIndex < clump_def_count(clump); partIndex++) {
                const SimObject part = clump_part_n(object, clump, partIndex);
                if (clump_cell_drawn(part.cx, part.cy)) noteObject(part);
            }
        } else noteObject(object);
    }
    for (const WallCell& wall : g_walls) {
        if (!g_wallsDraw || wall.kind >= EBLD_NWALL || wall.icon > 15
            || !cell_shown(wall.x, wall.y) || shroud_cell_hidden(wall.x, wall.y)) continue;
        const auto entry = g_ebldBld.find(ebld_wall_key(wall.x, wall.y));
        if (entry != g_ebldBld.end() && entry->second.stage != EB_INTACT) continue;
        int selected = -1;
        for (size_t index = 0; index < g_ebld.size(); index++) {
            const EbldModel& model = g_ebld[index];
            if (ebld_wall_kind(model.type.c_str()) == wall.kind && (model.flags & EBX_WALL)
                && model.wallMask < 0 && ebld_meshes_ok(model)) selected = (int)index;
        }
        note(selected, EB_INTACT);
    }
    for (const auto& item : g_ebldBld) {
        const EbldBld& entry = item.second;
        if (!ebld_anchor_shown(entry)) continue;
        if (entry.orphan) {
            if (!g_etreeOn || !g_fx.tree3d || !cell_shown(entry.cx, entry.cy)
                || shroud_cell_hidden(entry.cx, entry.cy)) continue;
        } else if (entry.stage != EB_WRECKED) {
            if (entry.stage == EB_INTACT || entry.model < 0 || (size_t)entry.model >= g_ebld.size()) continue;
            const EbldModel& model = g_ebld[entry.model];
            if (!(model.flags & EBX_WALL) && model.wallMask < 0) continue;
        }
        note(entry.model, entry.stage);
    }
    return masks;
}
static void ebld_prepare_required_loading_impl(void)
{
    EbldPrepareState& state = g_ebldPrepare;
    state.requiredReason = "planning";
    if (!ebld_prepare_required_models()) return;
    const std::map<int, uint64_t> masks = ebld_prepare_required_census();
    std::vector<EbldRequiredTexture> jobs;
    EbldPrepareCost remaining;
    for (const auto& item : masks) {
        const EbldModel& model = g_ebld[item.first];
        for (size_t texture = 0; texture < model.tex.size(); texture++) {
            if (texture < 64 && !(item.second & (uint64_t(1) << texture))) continue;
            if ((size_t)model.tex0 + texture >= g_pack.tex.size()) {
                state.requiredReason = "invalid-slot"; return;
            }
            if (g_pack.tex[model.tex0 + texture].gl) continue;
            if (jobs.size() >= 8192) { state.requiredReason = "job-limit"; return; }
            if (model.tex[texture].w <= 0 || model.tex[texture].h <= 0
                || model.tex[texture].w > 8192 || model.tex[texture].h > 8192) {
                state.requiredReason = "invalid-size"; return;
            }
            const EbldPrepareCost cost = ebld_prepare_texture_cost(model, texture);
            if (cost.transient > EBLD_PREPARE_TRANSIENT) {
                state.requiredReason = "transient-limit"; return;
            }
            if (!ebld_prepare_add_checked(remaining.cpu, cost.cpu)
                || !ebld_prepare_add_checked(remaining.gpu, cost.gpu)) {
                state.requiredReason = "cost-overflow"; return;
            }
            remaining.transient = std::max(remaining.transient, cost.transient);
            jobs.push_back({item.first, texture, cost});
        }
    }
    state.requiredPlanned = (unsigned)jobs.size();
    state.requiredCpuBound = remaining.cpu;
    state.requiredGpuBound = remaining.gpu;
#ifndef _WIN32
    if (remaining.cpu > ebld_prepare_remaining(EBLD_PREPARE_CPU, state.cpu)
        || remaining.gpu > ebld_prepare_remaining(EBLD_PREPARE_GPU, state.gpu)) {
        state.requiredReason = "platform-budget"; return;
    }
#endif
    if (!jobs.empty() && !ebld_prepare_headroom(remaining)) {
        state.requiredReason = "headroom"; return;
    }
    for (const EbldRequiredTexture& job : jobs) {
        if (SDL_QuitRequested()) { state.requiredReason = "quit"; return; }
        if (!ebld_prepare_headroom(remaining)) { state.requiredReason = "headroom"; return; }
        EbldModel& model = g_ebld[job.model];
        const uint64_t before = ebld_prepare_footprint();
        if (!ebld_prepare_texture_safe(model, job.texture, !model.texIntact[job.texture], job.cost.cpu)) {
            state.failures++; state.requiredReason = "resource-refusal"; return;
        }
        const uint64_t after = ebld_prepare_footprint();
        const uint64_t retained = after > before ? after - before : 0;
        if (!ebld_prepare_add_checked(state.cpu, retained)
            || !ebld_prepare_add_checked(state.gpu, job.cost.gpu)) {
            state.disabledByFailure = true; state.requiredReason = "charge-overflow"; return;
        }
        state.requiredCpuCharged += retained;
        state.requiredGpuCharged += job.cost.gpu;
        state.transientPeak = std::max(state.transientPeak, job.cost.transient);
        state.textures++; state.steps++; state.requiredCompleted++;
        remaining.cpu -= job.cost.cpu; remaining.gpu -= job.cost.gpu;
        SDL_PumpEvents();
        if (g_au) audio_frame(g_au, 0);
    }
    if (SDL_QuitRequested()) { state.requiredReason = "quit"; return; }
    state.requiredReady = true;
    state.requiredReason = "complete";
}
static void ebld_prepare_required_loading(void)
{
    if (!ebld_prepare_required_on()) return;
    if (g_ebldPrepare.generation != g_ebldPrepareGeneration) ebld_prepare_reset();
    if (g_ebldPrepare.requiredAttempted) return;
    const int engineFrame = g_engineFrame, simTicks = g_simTicks;
    const Uint64 started = SDL_GetPerformanceCounter();
    g_ebldPrepare.requiredAttempted = true;
    try { ebld_prepare_required_loading_impl(); }
    catch (const std::bad_alloc&) {
        g_ebldPrepare.requiredReady = false;
        g_ebldPrepare.requiredReason = "allocation-refusal";
        g_ebldPrepare.disabledByFailure = true;
        g_ebldPrepare.failures++;
    }
    if (g_ebldPrepare.requiredCompleted && !SDL_QuitRequested()) {
        const Uint64 retire = SDL_GetPerformanceCounter();
        glFinish();
        g_ebldPrepare.requiredWaitUs = (SDL_GetPerformanceCounter() - retire)
                                      * 1000000u / SDL_GetPerformanceFrequency();
    }
    if (engineFrame != g_engineFrame || simTicks != g_simTicks) {
        g_ebldPrepare.requiredReady = false;
        g_ebldPrepare.requiredReason = "clock-changed";
    }
    g_ebldPrepare.requiredUs = (SDL_GetPerformanceCounter() - started)
                              * 1000000u / SDL_GetPerformanceFrequency();
    fprintf(stderr, "EBLDPREPARE|required-models|planned=%u|completed=%u|cpu_reserved=%llu|reason=%s\n",
            g_ebldPrepare.requiredModelsPlanned, g_ebldPrepare.requiredModelsCompleted,
            (unsigned long long)g_ebldPrepare.requiredModelCpuReserved, g_ebldPrepare.requiredReason);
    fprintf(stderr, "EBLDPREPARE|required|ready=%d|planned=%u|completed=%u|pending=%u"
                    "|cpu_bound=%llu|gpu_bound=%llu|transient_cap=%llu|ms=%.3f|retire_us=%llu"
                    "|engine_before=%d|engine_after=%d|ticks_before=%d|ticks_after=%d|reason=%s\n",
            g_ebldPrepare.requiredReady ? 1 : 0, g_ebldPrepare.requiredPlanned,
            g_ebldPrepare.requiredCompleted, g_ebldPrepare.requiredPlanned - g_ebldPrepare.requiredCompleted,
            (unsigned long long)g_ebldPrepare.requiredCpuBound,
            (unsigned long long)g_ebldPrepare.requiredGpuBound,
            (unsigned long long)EBLD_PREPARE_TRANSIENT, (double)g_ebldPrepare.requiredUs / 1000.0,
            (unsigned long long)g_ebldPrepare.requiredWaitUs, engineFrame, g_engineFrame,
            simTicks, g_simTicks, g_ebldPrepare.requiredReason);
}
static void ebld_prepare_loading(double budgetMs = 2000.0, bool deferRequired = false)
{
    if (!ebld_prepare_on()) return;
    ebld_prepare_observe();
    if (deferRequired && ebld_prepare_required_on() && !g_ebldPrepare.requiredAttempted) return;
    if (g_ebldPrepare.loadingAttempted) return;
    g_ebldPrepare.loadingAttempted = true;
    const Uint64 start = SDL_GetPerformanceCounter();
    unsigned jobs = 0;
    while (jobs < 8192 && (double)(SDL_GetPerformanceCounter() - start) * 1000.0
                             / SDL_GetPerformanceFrequency() < budgetMs) {
        if (!ebld_prepare_step(true)) break;
        jobs++;
        SDL_PumpEvents();                  /* loading remains responsive, never advances sim */
        if (g_au) audio_frame(g_au, 0);     /* refill music without advancing world/audio clocks */
        if (SDL_QuitRequested()) break;
    }
    /* Loading owns this one retirement point. Without it the driver may defer an
       atlas transfer/mipmap build until the first interactive draw. Never in idle. */
    if (jobs && !SDL_QuitRequested()) {
        const Uint64 retire = SDL_GetPerformanceCounter();
        glFinish();
        g_ebldPrepare.loadingWaitUs += (SDL_GetPerformanceCounter() - retire)
                                      * 1000000u / SDL_GetPerformanceFrequency();
        if (g_au) audio_frame(g_au, 0);
    }
    g_ebldPrepare.loadingUs += (SDL_GetPerformanceCounter() - start) * 1000000u / SDL_GetPerformanceFrequency();
    fprintf(stderr, "EBLDPREPARE|loading|jobs=%u|textures=%u|meshes=%u|cpu_bound=%llu"
                    "|gpu_bound=%llu|transient_bound=%llu|retire_us=%llu|ms=%.3f\n",
            jobs, g_ebldPrepare.textures, g_ebldPrepare.meshes,
            (unsigned long long)g_ebldPrepare.cpu, (unsigned long long)g_ebldPrepare.gpu,
            (unsigned long long)g_ebldPrepare.transientPeak,
            (unsigned long long)g_ebldPrepare.loadingWaitUs,
            (double)(SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency());
}
/* RED ALERT'S BUILDINGS ARE READ AT FIRST SIGHT (ebld_type_lazy): when the mission's loading
   preparation runs, none of its structures' files is read yet and there is nothing to prepare,
   so the first hit on each one read its damaged, glow, broken and scorch atlases and built its
   cut on the spot (a frame of up to a second and a half with a base hit at once). On the first
   tick the structures are known and their files read; the loading preparation runs then, once a
   mission, under the same budgets, before the first frame is drawn. */
static void ebld_prepare_ra(void)
{
    if (!g_raModule || !ebld_prepare_on()) return;
    if (g_ebldPrepare.generation != g_ebldPrepareGeneration) ebld_prepare_reset();
    if (g_ebldPrepare.raLoaded) return;
    g_ebldPrepare.raLoaded = true;
    g_ebldPrepare.loadingAttempted = false;
    g_ebldPrepare.observedFrame = -2;          /* (look again: the files are read now) */
    ebld_prepare_observe();
    /* the structures with their stages first and alone (their damaged atlases are what a first
       hit waits for; everything else draws whole from the first frame and loads as it is seen) */
    const std::vector<int> all = g_ebldPrepare.models;
    std::vector<int> staged;
    for (int index : all)
        if (index >= 0 && (size_t)index < g_ebld.size() && g_ebld[(size_t)index].nset >= 2
            && !(g_ebld[(size_t)index].flags & EBX_WALL) && g_ebld[(size_t)index].stageOf < 0)
            staged.push_back(index);
    g_ebldPrepare.models = staged;
    ebld_prepare_loading(8000.0);
    g_ebldPrepare.models = all;
}

/* One live geometry slice, or one indivisible legacy job while explicitly paused.
   This measured split is not presented as a hard millisecond texture budget. */
static void ebld_prepare_idle(bool paused)
{
    ebld_prepare_observe();
    if (!ebld_prepare_on() || g_editOn) return;
    const Uint64 start = SDL_GetPerformanceCounter();
    if (!ebld_prepare_step(paused, !paused)) return;
    const uint64_t elapsed = (SDL_GetPerformanceCounter() - start) * 1000000u / SDL_GetPerformanceFrequency();
    if (paused) g_ebldPrepare.idleUs += elapsed;
    else {
        g_ebldPrepare.constructionUs += elapsed;
        if (elapsed > g_ebldPrepare.maxConstructionUs) g_ebldPrepare.maxConstructionUs = elapsed;
    }
}
static void ebld_prepare_dump(void)
{
    if (g_ebldPrepare.generation != g_ebldPrepareGeneration) ebld_prepare_reset();
    unsigned complete = 0;
    for (int index : g_ebldPrepare.models)
        if (index >= 0 && index < (int)g_ebld.size()) {
            const EbldModel& M = g_ebld[index];
            if (M.attached && M.residentAll && (M.nset < 2 || M.cutBuilt)) complete++;
        }
    fprintf(stdout, "EBLDPREPARE|status|enabled=%d|queued_models=%zu|completed_models=%u"
                    "|textures=%u|meshes=%u|steps=%u|rejected_jobs=%zu|failures=%u"
                    "|cpu_charged=%llu|cpu_cap=%llu|gpu_logical_charged=%llu|gpu_cap=%llu"
                    "|transient_peak_bound=%llu|transient_cap=%llu|loading_us=%llu|loading_retire_us=%llu|paused_us=%llu"
                    "|construction_us=%llu|max_construction_slice_us=%llu"
                    "|demand_cut_jobs=%u|demand_texture_jobs=%u|demand_failures=%u"
                    "|demand_cut_us=%llu|demand_texture_us=%llu\n",
            ebld_prepare_on()?1:0, g_ebldPrepare.models.size(), complete,
            g_ebldPrepare.textures,g_ebldPrepare.meshes,g_ebldPrepare.steps,
            g_ebldPrepare.rejected.size(),g_ebldPrepare.failures,
            (unsigned long long)g_ebldPrepare.cpu,(unsigned long long)EBLD_PREPARE_CPU,
            (unsigned long long)g_ebldPrepare.gpu,(unsigned long long)EBLD_PREPARE_GPU,
            (unsigned long long)g_ebldPrepare.transientPeak,(unsigned long long)EBLD_PREPARE_TRANSIENT,
            (unsigned long long)g_ebldPrepare.loadingUs,(unsigned long long)g_ebldPrepare.loadingWaitUs,(unsigned long long)g_ebldPrepare.idleUs,
            (unsigned long long)g_ebldPrepare.constructionUs,(unsigned long long)g_ebldPrepare.maxConstructionUs,
            g_ebldPrepareMetrics.demandCuts,g_ebldPrepareMetrics.demandTextures,g_ebldPrepareMetrics.demandFailures,
            (unsigned long long)g_ebldPrepareMetrics.demandCutUs,(unsigned long long)g_ebldPrepareMetrics.demandTextureUs);
    fprintf(stdout, "EBLDPREPARE|required-status|enabled=%d|attempted=%d|ready=%d|planned=%u|completed=%u"
                    "|cpu_bound=%llu|gpu_bound=%llu|ms=%.3f|reason=%s"
                    "|required_cpu_charged=%llu|required_gpu_charged=%llu|model_cpu_reserved=%llu"
                    "|optional_cpu_charged=%llu|optional_gpu_charged=%llu"
                    "|optional_cpu_remaining=%llu|optional_gpu_remaining=%llu\n",
            ebld_prepare_required_on() ? 1 : 0, g_ebldPrepare.requiredAttempted ? 1 : 0,
            g_ebldPrepare.requiredReady ? 1 : 0, g_ebldPrepare.requiredPlanned, g_ebldPrepare.requiredCompleted,
            (unsigned long long)g_ebldPrepare.requiredCpuBound,
            (unsigned long long)g_ebldPrepare.requiredGpuBound,
            (double)g_ebldPrepare.requiredUs / 1000.0, g_ebldPrepare.requiredReason,
            (unsigned long long)g_ebldPrepare.requiredCpuCharged,
            (unsigned long long)g_ebldPrepare.requiredGpuCharged,
            (unsigned long long)g_ebldPrepare.requiredModelCpuReserved,
            (unsigned long long)(g_ebldPrepare.cpu - g_ebldPrepare.requiredCpuCharged),
            (unsigned long long)(g_ebldPrepare.gpu - g_ebldPrepare.requiredGpuCharged),
            (unsigned long long)ebld_prepare_remaining(EBLD_PREPARE_CPU, g_ebldPrepare.cpu),
            (unsigned long long)ebld_prepare_remaining(EBLD_PREPARE_GPU, g_ebldPrepare.gpu));
}
#endif
