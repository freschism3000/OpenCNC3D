/* Enhanced remaster submission. Keep the existing CPU vertex equations exactly;
   cache their finished world-space output instead of changing shader coordinates.
   Runs retain texture/wrap/order, including partial construction and loose pieces.
   Tier 1 uses its separate renderer. Classic and unavailable VBOs retain the
   original path. The environment switches are diagnostic A/B controls only. */
#include <unordered_set>
#include <array>
#include <list>
#include <deque>
#include <new>
/* Exact input bytes, held inline: no per-draw string allocation. Hash collisions
   are resolved by the full byte comparison, including float representations. */
struct MeshKey {
    std::array<unsigned char, 512> data;
    size_t length = 0;
    mutable size_t hashValue = 0;
    mutable bool hashed = false;
    void reserve(size_t) {}
    bool empty() const { return length == 0; }
    void append(const char* p, size_t n) {
        if (length + n > data.size()) abort(); /* key schema must stay bounded */
        memcpy(data.data() + length, p, n); length += n; hashed = false;
    }
    bool operator==(const MeshKey& b) const {
        return length == b.length && !memcmp(data.data(), b.data.data(), length);
    }
    size_t hash() const {
        if (!hashed) {
            uint64_t h = 0x9e3779b97f4a7c15ull ^ length;
            for (size_t i = 0; i < length; i += 8) {
                uint64_t v = 0; memcpy(&v, data.data() + i, std::min((size_t)8, length - i));
                v ^= v >> 30; v *= 0xbf58476d1ce4e5b9ull;
                v ^= v >> 27; v *= 0x94d049bb133111ebull;
                h ^= (v ^ (v >> 31)) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            }
            hashValue = (size_t)(h ^ (h >> 32)); hashed = true;
        }
        return hashValue;
    }
};
struct MeshKeyHash { size_t operator()(const MeshKey& k) const { return k.hash(); } };
static bool mesh_perf_switch(const char* name);
struct MeshBatchVertex {
    float p[3], uv[2], n[3];
    unsigned char c[4];
};
struct MeshBatchAttributes { float uv[2], n[3]; unsigned char c[4]; };
struct MeshBatchRun { int tex, wrap; bool x2; size_t first, count; };
struct MeshBatchEntry {
    GLuint vbo = 0;
    size_t bytes = 0;
    unsigned used = 0;
    float lo[3], hi[3];
    std::vector<MeshBatchRun> runs;
    MeshBatchVertex last;
    bool uv = false;
    GLuint positionVbo = 0;
    bool compact = false;
    std::list<MeshKey>::iterator lru;
};
static std::unordered_map<MeshKey, MeshBatchEntry, MeshKeyHash> g_meshBatchCache;
static std::list<MeshKey> g_meshBatchLRU;
static unsigned long g_meshBatchEvictions = 0, g_meshBatchMisses = 0;
static unsigned long long g_meshBatchAdmissionRefused = 0;
enum { MESH_BATCH_MAX_ENTRIES = 8192 };
static size_t g_meshBatchBytes = 0;
static GLuint g_meshBatchStream = 0;
/* Animated output survives only for the current draw_frame. Recycle separate
   orphaned stores on the next frame instead of overwriting one in-flight store
   between every object. Both this pool and the static cache have hard bounds. */
struct MeshFrameStore { GLuint vbo=0; size_t bytes=0; unsigned lastUsed=0; };
static std::vector<MeshFrameStore> g_meshFrameStores;
static std::unordered_map<MeshKey,MeshBatchEntry,MeshKeyHash> g_meshFrameCache;
static unsigned g_meshFrameSerial=~0u;
static size_t g_meshFrameNext=0, g_meshFrameBytes=0;
static unsigned long g_meshFrameHits=0;
static unsigned long g_meshFrameFallbacks=0, g_meshFrameTrimmed=0;
static void mesh_frame_store_release(MeshFrameStore& s);
static void mesh_batch_frame_begin() {
    if (g_meshFrameSerial == g_drawFrameSerial) return;
    g_meshFrameCache.clear(); g_meshFrameNext=0; g_meshFrameSerial=g_drawFrameSerial;
    if (mesh_perf_switch("OPENCNC3D_MESH_POOL_TRIM")) {
        for (auto& s : g_meshFrameStores)
            if (s.vbo && g_drawFrameSerial - s.lastUsed > 60u) mesh_frame_store_release(s);
        while (!g_meshFrameStores.empty() && !g_meshFrameStores.back().vbo)
            g_meshFrameStores.pop_back();
    }
}
static const MeshBatchEntry* mesh_batch_frame_find(const MeshKey& key) {
    mesh_batch_frame_begin();
    auto it=g_meshFrameCache.find(key);
    return it==g_meshFrameCache.end()?NULL:&it->second;
}
static void mesh_batch_frame_keep(const MeshKey& key,const MeshBatchEntry& entry) {
    /* A failed copy must not leave a default empty entry for this key. The
       frame store still owns the submitted buffer, so only reuse is lost. */
    try {g_meshFrameCache.emplace(key,entry);}
    catch(const std::bad_alloc&) {g_meshFrameFallbacks++;}
}
/* PLACED PARTS SEEN THIS FRAME AND THE LAST (draw_mesh, placedStill). A key that comes
   back on the next frame belongs to a part that did not move, and that part goes into the
   persistent cache; a key seen once is rebuilt per frame as before. Two sets of keys,
   swapped when a new frame starts. */
static std::unordered_set<MeshKey, MeshKeyHash> g_meshPlacedPrev, g_meshPlacedNow;
static unsigned g_meshPlacedSerial = ~0u;
static unsigned long g_meshPlacedPromoted = 0;
static bool mesh_batch_placed_seen(const MeshKey& key) {
    if (g_meshPlacedSerial != g_drawFrameSerial) {
        g_meshPlacedPrev.swap(g_meshPlacedNow);
        g_meshPlacedNow.clear();
        g_meshPlacedSerial = g_drawFrameSerial;
    }
    g_meshPlacedNow.insert(key);
    const bool again = g_meshPlacedPrev.count(key) != 0;
    if (again) g_meshPlacedPromoted++;
    return again;
}
static GLuint mesh_batch_frame_store(size_t bytes) {
    mesh_batch_frame_begin();
    const size_t cap=64u*1024u*1024u;
    if(g_meshFrameNext>=2048 || bytes>cap) { g_meshFrameFallbacks++; return 0; }
    if(g_meshFrameNext==g_meshFrameStores.size()) {
        try {g_meshFrameStores.push_back({});}
        catch(const std::bad_alloc&) {g_meshFrameFallbacks++;return 0;}
    }
    auto& s=g_meshFrameStores[g_meshFrameNext];
    if(g_meshFrameBytes-s.bytes+bytes>cap && mesh_perf_switch("OPENCNC3D_MESH_POOL_TRIM"))
        /* Tail stores have not been handed to any current-frame command. Deleting
           them is safe: GL retains storage needed by earlier queued draws. */
        for (size_t i=g_meshFrameStores.size(); i>g_meshFrameNext+1 && g_meshFrameBytes-s.bytes+bytes>cap;)
            mesh_frame_store_release(g_meshFrameStores[--i]);
    if(g_meshFrameBytes-s.bytes+bytes>cap) { g_meshFrameFallbacks++; return 0; }
    if(!s.vbo) gr_glGenBuffers(1,&s.vbo);
    g_meshFrameBytes=g_meshFrameBytes-s.bytes+bytes; s.bytes=bytes;
    s.lastUsed=g_drawFrameSerial;
    g_meshFrameNext++;
    return s.vbo;
}
static bool mesh_perf_switch(const char* name);
static bool g_meshBatchEditor = false;
/* WHETHER A REFILLED STORE TOOK ITS DATA. glBufferData can fail for want of memory and
   leave the store empty, so the size the driver kept is checked after each fill. Asking
   is a round trip to the driver, and the per-frame stores are refilled with nearly the
   same size every frame, so each store remembers the largest fill it has been seen to
   hold and is asked again only for a larger one (remember=false: always ask). The store
   must be the one bound to GL_ARRAY_BUFFER. */
static std::map<GLuint, size_t> g_meshVerified;
static void mesh_frame_store_release(MeshFrameStore& s) {
    if (!s.vbo) return;
    g_meshVerified.erase(s.vbo); gr_glDeleteBuffers(1,&s.vbo);
    g_meshFrameBytes -= s.bytes; s.vbo=0; s.bytes=0; g_meshFrameTrimmed++;
}
static bool mesh_store_holds(GLuint vbo, size_t bytes, bool remember = true) {
    if (remember) {
        auto it = g_meshVerified.find(vbo);
        if (it != g_meshVerified.end() && it->second >= bytes) return true;
    }
    GLint allocated = 0; gr_glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &allocated);
    const bool ok = allocated == (GLint)bytes;
    if (ok && remember) { size_t& seen = g_meshVerified[vbo]; if (bytes > seen) seen = bytes; }
    return ok;
}
/* A wreck has hundreds of tiny ranges. Collect one body's loop in its original
   order, upload its unchanged world-space vertices once, and bind the arrays once.
   Only culling/lining offset vary inside these scopes; shader, blend, depth and
   colour masks remain fixed. The caller must end a scope before changing them. */
struct MeshFragmentCommand {
    MeshBatchEntry entry;
    const Pack* src;
    int house, pass;
    bool depth, normals, cull, offset;
};
static bool g_meshFragmentCollect = false;
/* Collection emits no geometry. Keep its two varying polygon enables logical
   until replay, rather than submitting every piece's toggles twice. The driver
   cache continues to describe actual GL. Every flush applies the final logical
   state, including empty prefixes and ordinary/client fallback boundaries. */
static bool mesh_fragment_polygon_defer_enabled() {
    static const bool on=[](){
        const char* p=getenv("OPENCNC3D_FRAGMENT_POLYGON_DEFER");
        if(p)return strcmp(p,"0")!=0;
#ifdef _WIN32
        return true;
#else
        /* Driver-call reduction is for Windows. Preserve the existing Mac path
           unless a controlled diagnostic explicitly asks for the same replay. */
        return false;
#endif
    }();
    return on;
}
static bool g_meshFragmentStateDeferred=false;
static bool g_meshFragmentCull=false,g_meshFragmentOffset=false;
static bool mesh_fragment_polygon(GLenum cap) {
    if(g_meshFragmentStateDeferred&&g_meshFragmentCollect)
        return cap==GL_CULL_FACE?g_meshFragmentCull:g_meshFragmentOffset;
    return glIsEnabled(cap)==GL_TRUE;
}
static bool mesh_fragment_polygon_set(GLenum cap,bool on) {
    if(!g_meshFragmentStateDeferred||!g_meshFragmentCollect)return false;
    if(cap==GL_CULL_FACE)g_meshFragmentCull=on;else g_meshFragmentOffset=on;
    return true;
}
static void mesh_fragment_polygon_apply() {
    if(!g_meshFragmentStateDeferred)return;
    if(g_meshFragmentCull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
    if(g_meshFragmentOffset)glEnable(GL_POLYGON_OFFSET_FILL);else glDisable(GL_POLYGON_OFFSET_FILL);
}
static std::vector<MeshBatchVertex> g_meshFragmentVertices;
/* Only bytes consumed by this pass cross the CPU/GL boundary: sun depth uses
   positions (12 B), colour uses positions/UV/colour (24 B), normal output keeps
   the full 36 B record. The exact CPU stream and client fallback stay unchanged. */
static std::vector<unsigned char> g_meshFragmentPacked;
static bool g_meshFragmentPackedCollect=false;
static size_t g_meshFragmentPackedStride=0;
static unsigned char* mesh_fragment_pack_space(size_t count,bool depth) {
    if(!mesh_perf_switch("OPENCNC3D_MESH_FRAGMENT_PACK_ALLOC_OK"))return NULL;
    const size_t stride=depth?3*sizeof(float):5*sizeof(float)+4;
    const size_t old=g_meshFragmentPacked.size(),cap=16u*1024u*1024u;
    if(count>cap/stride||old>cap-count*stride)return NULL;
    const size_t bytes=old+count*stride;
    try {
        if(g_meshFragmentPacked.capacity()<bytes) {
            const size_t grow=std::min(cap,std::max(bytes,std::max((size_t)4096,g_meshFragmentPacked.capacity()*2)));
            std::vector<unsigned char> next(grow);
            if(old)memcpy(next.data(),g_meshFragmentPacked.data(),old);
            g_meshFragmentPacked.swap(next);
        }
        g_meshFragmentPacked.resize(bytes);
    } catch(const std::bad_alloc&){return NULL;}
    g_meshFragmentPackedStride=stride;return g_meshFragmentPacked.data()+old;
}
static bool mesh_fragment_pack_append(const std::vector<MeshBatchVertex>& vertices,bool depth) {
    unsigned char* data=mesh_fragment_pack_space(vertices.size(),depth);if(!data)return false;
    const size_t stride=g_meshFragmentPackedStride;
    for(size_t i=0;i<vertices.size();++i) {
        unsigned char* d=data+i*stride;const MeshBatchVertex& v=vertices[i];
        memcpy(d,v.p,sizeof v.p);
        if(!depth){memcpy(d+3*sizeof(float),v.uv,sizeof v.uv);memcpy(d+5*sizeof(float),v.c,sizeof v.c);}
    }
    return true;
}
static unsigned long long g_meshFragmentPackFallbacks=0;
static unsigned long long g_meshFragmentPackedBytes=0,g_meshFragmentUploadBytes=0,g_meshFragmentFullBytes=0;
static std::vector<MeshFragmentCommand> g_meshFragmentCommands;
static unsigned long g_meshFragmentFlushes = 0, g_meshFragmentRanges = 0;
static unsigned long long g_meshFragmentDrawsSaved=0;
/* Keep only the latest exact stream of each fragment scope. No time/opacity keys
   accumulate: a changed stream replaces that instance/pass's previous stream. */
struct MeshFragmentInput {
    MeshKey key;
    size_t first=0, count=0, command=~(size_t)0;
};
struct MeshFragmentStore {
    std::vector<MeshBatchVertex> vertices;
    std::vector<MeshFragmentCommand> commands;
    std::vector<MeshFragmentInput> inputs;
    GLuint vbo=0;
    size_t bytes=0, gpuBytes=0, gpuStride=sizeof(MeshBatchVertex);
    unsigned used=0;
};
static std::unordered_map<MeshKey,MeshFragmentStore,MeshKeyHash> g_meshFragmentStores;
static size_t g_meshFragmentStoreBytes=0;
static MeshFragmentStore* g_meshFragmentStore=NULL;
static std::vector<MeshFragmentInput> g_meshFragmentInputs;
static size_t g_meshFragmentCount=0;
static bool g_meshFragmentChanged=true;
static unsigned long g_meshFragmentInputHits=0, g_meshFragmentStreamHits=0;
static const size_t MESH_FRAGMENT_STORE_CAP=64u*1024u*1024u;
static unsigned g_meshFragmentClockFrame=~0u;
static int g_meshFragmentClockEngine=-1;
static float g_meshFragmentClockAlpha=-1.0f;
static bool g_meshFragmentClockAgain=false;
static bool mesh_fragment_clock_admit() {
    if(g_meshFragmentClockFrame!=g_drawFrameSerial) {
        g_meshFragmentClockAgain=g_meshFragmentClockFrame+1u==g_drawFrameSerial
            && g_meshFragmentClockEngine==g_engineFrame && g_meshFragmentClockAlpha==g_tickAlpha;
        g_meshFragmentClockFrame=g_drawFrameSerial;g_meshFragmentClockEngine=g_engineFrame;
        g_meshFragmentClockAlpha=g_tickAlpha;
    }
    return g_meshFragmentClockAgain;
}
static void mesh_fragment_store_release(MeshFragmentStore& s) {
    if(s.vbo) { g_meshVerified.erase(s.vbo); gr_glDeleteBuffers(1,&s.vbo); }
    g_meshFragmentStoreBytes-=s.bytes; s=MeshFragmentStore();
}
/* Retained vertex CPU allocation + VBO allocation + inline keys/input allocation +
   commands and their run allocations. The bounded map/node overhead is charged too.
   Existing current-scope assembly scratch remains under its separate64MiB bound. */
static size_t mesh_fragment_store_footprint(size_t gpuBytes,size_t vertices,
        const std::vector<MeshFragmentInput>& inputs,const std::vector<MeshFragmentCommand>& commands) {
    size_t bytes=gpuBytes+vertices*sizeof(MeshBatchVertex)
        +inputs.capacity()*sizeof(MeshFragmentInput)+commands.capacity()*sizeof(MeshFragmentCommand)+96u;
    for(const auto& command:commands)bytes+=command.entry.runs.capacity()*sizeof(MeshBatchRun)
        +(command.entry.runs.capacity()?32u:0u);
    return bytes;
}
static bool mesh_fragment_store_budget(size_t bytes) {
    const size_t cap=mesh_perf_switch("OPENCNC3D_MESH_FRAGMENT_BUDGET_FULL") ? MESH_FRAGMENT_STORE_CAP : 256u;
    const size_t nodes=64u*(sizeof(MeshKey)+sizeof(MeshFragmentStore)+64u);
    if(bytes+nodes>cap)return false;
    while(g_meshFragmentStoreBytes-g_meshFragmentStore->bytes+bytes+nodes>cap) {
        auto oldest=g_meshFragmentStores.end();
        for(auto i=g_meshFragmentStores.begin();i!=g_meshFragmentStores.end();++i)
            if(&i->second!=g_meshFragmentStore && (oldest==g_meshFragmentStores.end()
               || (unsigned)(g_drawFrameSerial-i->second.used)>(unsigned)(g_drawFrameSerial-oldest->second.used))) oldest=i;
        if(oldest==g_meshFragmentStores.end())return false;
        mesh_fragment_store_release(oldest->second);g_meshFragmentStores.erase(oldest);
    }
    return true;
}
static void mesh_fragment_store_begin(const void* identity,unsigned variant) {
    g_meshFragmentStore=NULL;g_meshFragmentInputs.clear();g_meshFragmentCount=0;
    g_meshFragmentChanged=true;
    if(!identity || !mesh_perf_switch("OPENCNC3D_MESH_FRAGMENT_REUSE")
        || !mesh_fragment_clock_admit())return;
    for(auto i=g_meshFragmentStores.begin();i!=g_meshFragmentStores.end();) {
        if(g_drawFrameSerial-i->second.used>60u){mesh_fragment_store_release(i->second);i=g_meshFragmentStores.erase(i);}
        else ++i;
    }
    MeshKey slot;slot.append((const char*)&identity,sizeof identity);
    slot.append((const char*)&variant,sizeof variant);
    const bool layout[]={g_meshSunDepth,g_meshNrmOut,g_meshBakePass,g_meshBakeCutout,g_meshShadowPass};
    slot.append((const char*)layout,sizeof layout);
    auto f=g_meshFragmentStores.find(slot);
    if(f==g_meshFragmentStores.end()) {
        if(g_meshFragmentStores.size()>=64) {
            auto oldest=g_meshFragmentStores.begin();
            for(auto i=g_meshFragmentStores.begin();i!=g_meshFragmentStores.end();++i)
                if((unsigned)(g_drawFrameSerial-i->second.used)>(unsigned)(g_drawFrameSerial-oldest->second.used))oldest=i;
            mesh_fragment_store_release(oldest->second);g_meshFragmentStores.erase(oldest);
        }
        f=g_meshFragmentStores.emplace(std::move(slot),MeshFragmentStore()).first;
    }
    g_meshFragmentStore=&f->second;g_meshFragmentStore->used=g_drawFrameSerial;
    g_meshFragmentChanged=g_meshFragmentStore->inputs.empty();
}
/* Prefix hits need no copies. Materialize only when a later input changes. */
static void mesh_fragment_materialize() {
    if(g_meshFragmentStore && !g_meshFragmentChanged) {
        g_meshFragmentVertices.assign(g_meshFragmentStore->vertices.begin(),
             g_meshFragmentStore->vertices.begin()+g_meshFragmentCount);
        g_meshFragmentChanged=true;
    }
}
static void mesh_fragment_store_disable() {
    mesh_fragment_materialize();g_meshFragmentStore=NULL;g_meshFragmentInputs.clear();
}
static bool mesh_fragment_input_reuse(const MeshKey& key,const Pack& src,int house,int pass,
                                      bool depth,bool normals) {
    if(!g_meshFragmentStore)return false;
    const size_t index=g_meshFragmentInputs.size();
    const size_t limit=mesh_perf_switch("OPENCNC3D_MESH_FRAGMENT_INPUT_FULL") ? 2048u : 16u;
    if(index>=limit){mesh_fragment_store_disable();return false;}
    if(key.empty() || index>=g_meshFragmentStore->inputs.size())return false;
    const MeshFragmentInput& old=g_meshFragmentStore->inputs[index];
    if(!(old.key==key))return false;
    if(old.count) {
        const MeshFragmentCommand& previous=g_meshFragmentStore->commands[old.command];
        if(previous.src!=&src || previous.house!=house || previous.pass!=pass
            || previous.depth!=depth || previous.normals!=normals)return false;
        MeshFragmentCommand cmd=previous;
        cmd.cull=mesh_fragment_polygon(GL_CULL_FACE);cmd.offset=mesh_fragment_polygon(GL_POLYGON_OFFSET_FILL);
        for(auto& run:cmd.entry.runs)run.first=run.first-old.first+g_meshFragmentCount;
        if(g_meshFragmentChanged)g_meshFragmentVertices.insert(g_meshFragmentVertices.end(),
            g_meshFragmentStore->vertices.begin()+old.first,
            g_meshFragmentStore->vertices.begin()+old.first+old.count);
        MeshFragmentInput input;input.key=key;input.first=g_meshFragmentCount;input.count=old.count;
        input.command=g_meshFragmentCommands.size();g_meshFragmentInputs.push_back(std::move(input));
        g_meshFragmentCommands.push_back(std::move(cmd));g_meshFragmentCount+=old.count;
    } else {
        MeshFragmentInput input;input.key=key;input.first=g_meshFragmentCount;
        g_meshFragmentInputs.push_back(std::move(input));
    }
    g_meshFragmentInputHits++;return true;
}
static void mesh_fragment_input_record(const MeshKey& key,size_t count,size_t command) {
    if(!g_meshFragmentStore){g_meshFragmentCount+=count;return;}
    const size_t limit=mesh_perf_switch("OPENCNC3D_MESH_FRAGMENT_INPUT_FULL") ? 2048u : 16u;
    if(g_meshFragmentInputs.size()>=limit){mesh_fragment_store_disable();g_meshFragmentCount+=count;return;}
    mesh_fragment_materialize();
    MeshFragmentInput input;input.key=key;input.first=g_meshFragmentCount;
    input.count=count;input.command=command;g_meshFragmentInputs.push_back(std::move(input));
    g_meshFragmentCount+=count;
}

/* Optional bounded Windows diagnostic. Counts are reset at each recorded frame,
   and only work inside that frame is recorded. Startup/script work is excluded. */
static Uint64 g_destructTrace[22] = {};
static int destruction_trace_pass_kind();
static void mesh_fragment_flush(void);
struct MeshFragmentScope {
    bool active;
    MeshFragmentScope(const void* identity=NULL,unsigned variant=0) : active(g_fx.enabled && !g_meshBatchEditor
        && mesh_perf_switch("OPENCNC3D_MESH_FRAGMENT_BATCH")
        && mesh_perf_switch("OPENCNC3D_MESH_GPU") && grass_buf_load()
        && !g_meshFragmentCollect) {
        if (active) {
                      g_meshFragmentStateDeferred=mesh_fragment_polygon_defer_enabled();
                      if(g_meshFragmentStateDeferred) {
                          g_meshFragmentCull=glIsEnabled(GL_CULL_FACE)==GL_TRUE;
                          g_meshFragmentOffset=glIsEnabled(GL_POLYGON_OFFSET_FILL)==GL_TRUE;
                      }
                      g_meshFragmentVertices.clear(); g_meshFragmentCommands.clear();
                      g_meshFragmentCollect = true; mesh_fragment_store_begin(identity,variant);
                      g_meshFragmentPacked.clear();g_meshFragmentPackedStride=0;
                      g_meshFragmentPackedCollect=!g_meshFragmentStore&&!g_meshNrmOut
                          &&mesh_perf_switch("OPENCNC3D_MESH_FRAGMENT_PACK")
                          &&mesh_perf_switch("OPENCNC3D_MESH_FRAGMENT_PACK_OK"); }
    }
    ~MeshFragmentScope() {
        if (active) { g_meshFragmentCollect = false; mesh_fragment_flush();
            g_meshFragmentStore=NULL;g_meshFragmentInputs.clear();g_meshFragmentPackedCollect=false;
            g_meshFragmentStateDeferred=false; }
    }
};
static bool g_meshClassSkip = false;
static bool g_meshBatchRestPose = false;
static unsigned long g_meshBatchHits = 0, g_meshBatchUploads = 0,
                     g_meshBatchCulled = 0, g_meshBatchDraws = 0,
                     g_meshClassSkipped = 0;
/* Vertices draw_mesh still sent one call at a time (glVertex3f), outside the batch. */
static unsigned long g_meshImmVerts = 0;
/* Bytes handed to the card by every vertex upload counted in uploads above. */
static unsigned long long g_meshBatchUploadBytes = 0;
/* READ ONCE PER NAME. These are launch-time A/B switches, and draw_mesh and the cull ask
   for them on every model drawn: about 1,800 lookups a frame under Enhanced. On Windows
   each getenv takes the C runtime's environment lock and compares every variable in the
   block, case-insensitively. Nothing in the game changes these variables after launch. */
static bool mesh_perf_switch_named(const char* name) {
    struct Known { const char* name; bool on; };
    static Known known[256];
    static int nknown = 0;
    for (int i = 0; i < nknown; i++)
        if (known[i].name == name || !strcmp(known[i].name, name)) return known[i].on;
    const char* v = getenv(name);
    const bool on = !v || strcmp(v, "0");
    if (nknown < 256) { known[nknown].name = name; known[nknown].on = on; nknown++; }
    return on;
}
/* AND FOUND BY ADDRESS, NOT BY NAME. The table above was searched in order with a
   string compare against every entry before the one asked for, and dozens of these
   names share their first fifteen characters, so a lookup near the end of the table
   cost dozens of compares; draw_mesh asks for four to six of them per model, in every
   pass. Every caller passes a string literal, whose address never changes, so the
   address is the key: one hash, usually one probe. The name table is still what reads
   each variable, once, so both paths give the same answer for every name.
   OPENCNC3D_MESH_SWITCH_ADDRESS=0 puts the search by name back. */
static bool mesh_perf_switch(const char* name) {
    static const bool byAddress = mesh_perf_switch_named("OPENCNC3D_MESH_SWITCH_ADDRESS");
    if (!byAddress) return mesh_perf_switch_named(name);
    struct Slot { const char* name; bool on; };
    enum { SLOT_BITS = 10, SLOTS = 1 << SLOT_BITS };
    static Slot slots[SLOTS];
    static int used = 0;
    size_t h = (size_t)(((uint64_t)(uintptr_t)name * 0x9E3779B97F4A7C15ull) >> (64 - SLOT_BITS));
    for (int probe = 0; probe < SLOTS; probe++, h = (h + 1) & (SLOTS - 1)) {
        if (slots[h].name == name) return slots[h].on;
        if (!slots[h].name) {
            const bool on = mesh_perf_switch_named(name);
            if (used < SLOTS / 2) { slots[h].on = on; slots[h].name = name; used++; }
            return on;
        }
    }
    return mesh_perf_switch_named(name);
}
static void mesh_geometry_clear();
static void mesh_prepared_clear();
static void mesh_scratch_clear();
static void mesh_batch_clear(void) {
    mesh_geometry_clear(); mesh_scratch_clear(); mesh_prepared_clear();
    if (gr_glDeleteBuffers) {
        for (auto& p : g_meshBatchCache)
            if (p.second.vbo) gr_glDeleteBuffers(1, &p.second.vbo);
        for(auto& s:g_meshFrameStores) if(s.vbo) gr_glDeleteBuffers(1,&s.vbo);
        if (g_meshBatchStream) gr_glDeleteBuffers(1, &g_meshBatchStream);
    }
    g_meshBatchCache.clear(); g_meshBatchLRU.clear(); g_meshBatchBytes = 0; g_meshBatchStream = 0;
    g_meshVerified.clear();
    g_meshFrameCache.clear(); g_meshFrameStores.clear(); g_meshFrameBytes=0;
    g_meshFragmentVertices.clear(); g_meshFragmentVertices.shrink_to_fit();
    g_meshFragmentPacked.clear();g_meshFragmentPacked.shrink_to_fit();
    g_meshFragmentCommands.clear(); g_meshFragmentCommands.shrink_to_fit();
    g_meshFragmentCollect = false;g_meshFragmentStateDeferred=false;g_meshFragmentPackedCollect=false;g_meshFragmentPackedStride=0;
    g_meshFragmentStore=NULL;
    for(auto& s:g_meshFragmentStores)mesh_fragment_store_release(s.second);
    g_meshFragmentStores.clear();g_meshFragmentInputs.clear();g_meshFragmentInputs.shrink_to_fit();
    g_meshFragmentCount=0;g_meshFragmentChanged=true;
    g_meshFragmentClockFrame=~0u;g_meshFragmentClockEngine=-1;g_meshFragmentClockAlpha=-1.0f;
    g_meshFragmentClockAgain=false;
    g_meshFrameSerial=~0u; g_meshFrameNext=0;
    g_meshPlacedPrev.clear(); g_meshPlacedNow.clear(); g_meshPlacedSerial=~0u;
}
static const MeshBatchEntry* mesh_batch_kept_find(const MeshKey& key) {
    auto it = g_meshBatchCache.find(key);
    if (it == g_meshBatchCache.end()) { g_meshBatchMisses++; return NULL; }
    it->second.used = g_drawFrameSerial;
    g_meshBatchLRU.splice(g_meshBatchLRU.end(), g_meshBatchLRU, it->second.lru);
    g_meshBatchHits++;
    return &it->second;
}
static void mesh_batch_evict_one() {
    if (g_meshBatchLRU.empty()) return;
    auto it = g_meshBatchCache.find(g_meshBatchLRU.front());
    if (it != g_meshBatchCache.end()) {
        g_meshBatchBytes -= it->second.bytes;
        if (it->second.vbo) {
            g_meshVerified.erase(it->second.vbo);
            gr_glDeleteBuffers(1, &it->second.vbo);
        }
        g_meshBatchCache.erase(it); g_meshBatchEvictions++;
    }
    g_meshBatchLRU.pop_front();
}
static size_t mesh_batch_capacity() {
    return mesh_perf_switch("OPENCNC3D_MESH_CACHE_FULL") ? 128u*1024u*1024u : 1024u*1024u;
}
/* A scene larger than the persistent cache must not evict its own earlier draws
   on every frame. Protect the current and previous two frames' hot set, then
   submit refused entries through bounded transient stores. Cold entries still
   retire in LRU order as the view changes. Admission precedes GPU allocation. */
static bool mesh_batch_admit(size_t bytes) {
    const size_t cap=mesh_batch_capacity();
    if(bytes>cap) {g_meshBatchAdmissionRefused++;return false;}
    const bool protect=mesh_perf_switch("OPENCNC3D_MESH_HOT_ADMISSION");
    while (!g_meshBatchLRU.empty() &&
           (bytes>cap-g_meshBatchBytes || g_meshBatchCache.size() >= MESH_BATCH_MAX_ENTRIES)) {
        auto oldest=g_meshBatchCache.find(g_meshBatchLRU.front());
        if(protect && oldest!=g_meshBatchCache.end()
            && g_drawFrameSerial-oldest->second.used<=2u) {
            g_meshBatchAdmissionRefused++;return false;
        }
        mesh_batch_evict_one();
    }
    return bytes<=cap-g_meshBatchBytes && g_meshBatchCache.size()<MESH_BATCH_MAX_ENTRIES;
}
static void mesh_batch_keep(const MeshKey& key, MeshBatchEntry entry) {
    /* A repeated offer must not grow the LRU or byte charge behind an existing
       key. Its resident store remains owned by that entry. */
    auto existing=g_meshBatchCache.find(key);
    if(existing!=g_meshBatchCache.end()) {
        if(entry.vbo && entry.vbo!=existing->second.vbo) {
            g_meshVerified.erase(entry.vbo);gr_glDeleteBuffers(1,&entry.vbo);
        }
        return;
    }
    if(!mesh_batch_admit(entry.bytes)) {
        if(entry.vbo){g_meshVerified.erase(entry.vbo);gr_glDeleteBuffers(1,&entry.vbo);}
        return;
    }
    const GLuint offered=entry.vbo;
    bool staged=false;
    try {
        g_meshBatchLRU.push_back(key);staged=true;
        entry.lru=--g_meshBatchLRU.end();entry.used=g_drawFrameSerial;
        if(!mesh_perf_switch("OPENCNC3D_MESH_CACHE_ALLOC_OK"))throw std::bad_alloc();
        auto inserted=g_meshBatchCache.emplace(key,std::move(entry));
        g_meshBatchBytes+=inserted.first->second.bytes;
    } catch(const std::bad_alloc&) {
        if(staged)g_meshBatchLRU.pop_back();
        if(offered){g_meshVerified.erase(offered);gr_glDeleteBuffers(1,&offered);}
        g_meshBatchAdmissionRefused++;
    }
}
/* Geometry precedes pass-specific colour/depth packing. Keep the original CPU
   results, including its operation order, once per required pose in this frame.
   Slots retain capacity between frames, with a separate bounded CPU budget. */
struct MeshGeometryCorner { float p[3], n[3]; };
struct MeshGeometryPose {
    std::vector<MeshGeometryCorner> corners;
    std::vector<unsigned char> valid;
    GLuint gpu = 0;
    size_t gpuCount = 0;
    unsigned lastUsed = 0;
    float lo[3],hi[3];
    bool boundsValid = false;
};
static std::deque<MeshGeometryPose> g_meshGeometryPool;
static std::unordered_map<MeshKey, size_t, MeshKeyHash> g_meshGeometryKeys;
static unsigned g_meshGeometrySerial=~0u;
static size_t g_meshGeometryNext=0, g_meshGeometryBytes=0;
static unsigned long g_meshGeometryHits=0, g_meshGeometryBuilds=0, g_meshGeometryUploads=0;
static std::vector<float> g_meshGeometryUpload;
static unsigned long long g_meshWindDirectDraws=0,g_meshWindDirectVertices=0,g_meshWindDirectFallbacks=0;
static unsigned long long g_meshWindDirectVerified=0,g_meshWindDirectMismatches=0;
static unsigned long long g_meshWindDepthDraws=0,g_meshWindDepthVertices=0,g_meshWindDepthFallbacks=0;
static unsigned long long g_meshWindDepthVerified=0,g_meshWindDepthMismatches=0;
static unsigned long long g_meshBakeShareDraws=0,g_meshBakeShareVertices=0,g_meshBakeShareStores=0;
static unsigned long long g_meshBakeShareVerified=0,g_meshBakeShareMismatches=0;
/* Reads a buffer back for the verifiers, which compare what the card holds with what
   the full loop makes. Never on a normal frame: a readback waits for the card. */
typedef void (APIENTRY *MeshGetBufferSubData)(GLenum, FxGLsizeiptr, FxGLsizeiptr, void*);
static bool mesh_buffer_read(GLuint vbo, size_t bytes, void* out) {
    static MeshGetBufferSubData get = NULL;
    static bool asked = false;
    if (!asked) {
        asked = true;
        get = (MeshGetBufferSubData)SDL_GL_GetProcAddress("glGetBufferSubData");
        if (!get) get = (MeshGetBufferSubData)SDL_GL_GetProcAddress("glGetBufferSubDataARB");
        if (!get) fprintf(stderr, "MESHVERIFY|unavailable|no glGetBufferSubData\n");
    }
    if (!get || !vbo) return false;
    if (!bytes) return true;
    const GLuint bound = gls_array_binding();
    gr_glBindBuffer(GL_ARRAY_BUFFER, vbo);
    get(GL_ARRAY_BUFFER, 0, (FxGLsizeiptr)bytes, out);
    gr_glBindBuffer(GL_ARRAY_BUFFER, bound);
    return true;
}
static bool mesh_wind_direct_verify_enabled() {
    static const bool enabled=[](){
        const char* value=getenv("OPENCNC3D_MESH_WIND_DIRECT_VERIFY");
        return value && !strcmp(value,"1");
    }();
    return enabled;
}
static size_t mesh_geometry_bytes(const MeshGeometryPose& p) {
    return p.corners.capacity()*sizeof(MeshGeometryCorner)+p.valid.capacity();
}
static void mesh_geometry_frame_begin() {
    if (g_meshGeometrySerial == g_drawFrameSerial) return;
    g_meshGeometryKeys.clear(); g_meshGeometryNext=0; g_meshGeometrySerial=g_drawFrameSerial;
    for (auto& p : g_meshGeometryPool) if (g_drawFrameSerial-p.lastUsed>60u) {
        g_meshGeometryBytes-=mesh_geometry_bytes(p);
        std::vector<MeshGeometryCorner>().swap(p.corners);
        std::vector<unsigned char>().swap(p.valid); p.gpu=0;
    }
}
struct MeshSettledPose { MeshGeometryPose pose; std::list<MeshKey>::iterator lru; };
static std::unordered_map<MeshKey,MeshSettledPose,MeshKeyHash> g_meshSettled;
static std::list<MeshKey> g_meshSettledLRU;
static size_t g_meshSettledBytes=0;
static unsigned long g_meshSettledHits=0;
static MeshGeometryPose* mesh_geometry_settled(const MeshKey& key,size_t n) {
    auto f=g_meshSettled.find(key);
    if(f!=g_meshSettled.end()) {
        auto& p=f->second.pose;
        if(p.lastUsed!=g_drawFrameSerial){p.gpu=0;p.gpuCount=0;p.lastUsed=g_drawFrameSerial;}
        g_meshSettledLRU.splice(g_meshSettledLRU.end(),g_meshSettledLRU,f->second.lru);
        g_meshSettledHits++;return &p;
    }
    const size_t bytes=n*(sizeof(MeshGeometryCorner)+1),cap=32u*1024u*1024u;
    if(bytes>cap)return NULL;
    while(!g_meshSettledLRU.empty()&&(g_meshSettledBytes+bytes>cap||g_meshSettled.size()>=2048)) {
        auto old=g_meshSettled.find(g_meshSettledLRU.front());
        if(old!=g_meshSettled.end()){g_meshSettledBytes-=mesh_geometry_bytes(old->second.pose);g_meshSettled.erase(old);}
        g_meshSettledLRU.pop_front();
    }
    MeshSettledPose entry;entry.pose.corners.resize(n);entry.pose.valid.resize(n);
    entry.pose.lastUsed=g_drawFrameSerial;g_meshSettledBytes+=mesh_geometry_bytes(entry.pose);
    g_meshSettledLRU.push_back(key);entry.lru=--g_meshSettledLRU.end();
    return &g_meshSettled.emplace(key,std::move(entry)).first->second.pose;
}
static MeshGeometryPose* mesh_geometry_find(const MeshKey& key, size_t n) {
    mesh_geometry_frame_begin();
    auto found=g_meshGeometryKeys.find(key);
    if (found!=g_meshGeometryKeys.end()) { g_meshGeometryHits++; return &g_meshGeometryPool[found->second]; }
    const size_t cap=64u*1024u*1024u, bytes=n*(sizeof(MeshGeometryCorner)+1);
    if (bytes>cap || g_meshGeometryNext>=2048) return NULL;
    if (g_meshGeometryNext==g_meshGeometryPool.size()) g_meshGeometryPool.emplace_back();
    MeshGeometryPose& p=g_meshGeometryPool[g_meshGeometryNext];
    size_t old=mesh_geometry_bytes(p);
    if (g_meshGeometryBytes-old+bytes>cap) {
        for (size_t i=g_meshGeometryPool.size(); i>g_meshGeometryNext+1 && g_meshGeometryBytes-old+bytes>cap;) {
            auto& tail=g_meshGeometryPool[--i]; g_meshGeometryBytes-=mesh_geometry_bytes(tail);
            std::vector<MeshGeometryCorner>().swap(tail.corners);
            std::vector<unsigned char>().swap(tail.valid); tail.gpu=0;
        }
    }
    if (g_meshGeometryBytes-old+bytes>cap) return NULL;
    /* Exact capacity on growth avoids an unaccounted vector doubling near the cap. */
    if (p.corners.capacity()<n) std::vector<MeshGeometryCorner>(n).swap(p.corners);
    else p.corners.resize(n);
    if (p.valid.capacity()<n) std::vector<unsigned char>(n).swap(p.valid);
    else { p.valid.resize(n); std::fill(p.valid.begin(),p.valid.end(),0); }
    g_meshGeometryBytes=g_meshGeometryBytes-old+mesh_geometry_bytes(p);
    p.gpu=0; p.gpuCount=0; p.boundsValid=false; p.lastUsed=g_drawFrameSerial;
    g_meshGeometryKeys.emplace(key,g_meshGeometryNext++);
    return &p;
}
static GLuint mesh_geometry_upload(MeshGeometryPose& pose,const float* positions,size_t count,GLuint vbo=0) {
    if(pose.gpu)return pose.gpuCount==count?pose.gpu:0;
    const size_t bytes=count*3*sizeof(float);
    if (bytes>64u*1024u*1024u) return 0;
    if(!vbo)vbo=mesh_batch_frame_store(bytes);
    if (!vbo) return 0;
    const GLuint was=gls_array_binding(); gr_glBindBuffer(GL_ARRAY_BUFFER,vbo);
    gr_glBufferData(GL_ARRAY_BUFFER,(FxGLsizeiptr)bytes,positions,GL_STREAM_DRAW);
    const bool ok=mesh_store_holds(vbo,bytes);
    if (!ok) mesh_frame_store_release(g_meshFrameStores[g_meshFrameNext-1]);
    gr_glBindBuffer(GL_ARRAY_BUFFER,was);
    if (ok) { pose.gpu=vbo; pose.gpuCount=count; g_meshGeometryUploads++; g_meshBatchUploadBytes+=bytes; }
    return ok?vbo:0;
}
static GLuint mesh_geometry_gpu(MeshGeometryPose& p, const std::vector<MeshBatchVertex>& vertices) {
    if (p.gpu) return p.gpuCount==vertices.size()?p.gpu:0;
    if (vertices.size()>64u*1024u*1024u/(3*sizeof(float))) return 0;
    const GLuint vbo=mesh_batch_frame_store(vertices.size()*3*sizeof(float));
    if(!vbo)return 0;
    g_meshGeometryUpload.resize(vertices.size()*3);
    for (size_t i=0;i<vertices.size();i++) {
        memcpy(&g_meshGeometryUpload[i*3],vertices[i].p,3*sizeof(float));
        for(int k=0;k<3;k++) {
            if(!i)p.lo[k]=p.hi[k]=vertices[i].p[k];
            else{p.lo[k]=std::min(p.lo[k],vertices[i].p[k]);p.hi[k]=std::max(p.hi[k],vertices[i].p[k]);}
        }
    }
    return mesh_geometry_upload(p,g_meshGeometryUpload.data(),vertices.size(),vbo);
}
static void mesh_geometry_clear() {
    g_meshSettled.clear();g_meshSettledLRU.clear();g_meshSettledBytes=0;
    g_meshGeometryKeys.clear(); g_meshGeometryPool.clear(); g_meshGeometryBytes=0;
    std::vector<float>().swap(g_meshGeometryUpload); g_meshGeometrySerial=~0u; g_meshGeometryNext=0;
}
/* A remastered object's pieces share client-array ownership. Legacy helpers can
   still save/restore or change arrays inside the scope; each mesh sets its own
   pointers, and the outer scope restores the caller's state once. */
static unsigned g_meshArrayDepth=0;
static GLuint g_meshArrayWas=0;
struct MeshArrayScope {
    bool active;
    explicit MeshArrayScope(bool wanted = true)
        : active(wanted && g_fx.enabled && mesh_perf_switch("OPENCNC3D_MESH_ARRAY_SCOPE")
                 && mesh_perf_switch("OPENCNC3D_MESH_GPU") && grass_buf_load()) {
        if (active && g_meshArrayDepth++==0) {
            g_meshArrayWas=gls_array_binding(); glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
        }
    }
    ~MeshArrayScope() {
        if (active && --g_meshArrayDepth==0) {
            glPopClientAttrib(); gr_glBindBuffer(GL_ARRAY_BUFFER,g_meshArrayWas);
        }
    }
};
/* Reject only when every corner lies outside one homogeneous clip plane.
   These are the CURRENT matrices: camera, mirrored water camera or sun camera.
   No assumption about ground height, anchor visibility or terrain cull bounds. */
static bool mesh_box_outside(const float lo[3], const float hi[3], const float* clip = NULL) {
    if (!mesh_perf_switch("OPENCNC3D_MESH_CULL")) return false;
    float c[16];
    memcpy(c, clip ? clip : gls_clip(), sizeof c);   /* gls_clip asks only after a matrix changed */
    for (int axis = 0; axis < 3; axis++) for (int sign = -1; sign <= 1; sign += 2) {
        float a[4];
        for (int k = 0; k < 4; k++) a[k] = c[k*4+3] + sign*c[k*4+axis];
        /* Positive support point of the world-space box, with a small conservative
           margin for matrix roundoff. Non-finite bounds never reject geometry. */
        float d = a[3], margin = 0.001f;
        for (int k = 0; k < 3; k++) {
            d += a[k] * (a[k] >= 0 ? hi[k] : lo[k]);
            margin += fabsf(a[k]) * 0.001f;
        }
        if (std::isfinite(d) && d < -margin) return true;
    }
    return false;
}
static bool mesh_batch_outside(const MeshBatchEntry& e, const float* clip = NULL) {
    if (mesh_box_outside(e.lo, e.hi, clip)) return true;
    /* while the sun's receiver window is armed (fx_post.h): a piece none of whose texels
       is read is dropped the same way, its draw only, every state call kept */
    if (!g_fxRecvArmed || !g_fxRecvArmed->on) return false;
    if (!fx_recv_box_outside(e.lo, e.hi, clip ? clip : gls_clip())) return false;
    g_fxRecvPieceCulled++;
    return true;
}
static void mesh_batch_x2(bool on, GLint env) {
    if (!on) {
        glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, env);
        return;
    }
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
    glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
    glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
    glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_PRIMARY_COLOR);
    glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
    glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 2.0f);
}
static void mesh_batch_draw(const MeshBatchEntry& e, const Pack& src, int house,
                            int pass, bool depth, bool normals, const MeshBatchVertex* client = NULL) {
    if (e.runs.empty()) return;
    const bool outside = mesh_batch_outside(e);
    if (outside) g_meshBatchCulled++;
    /* The env mode is only needed to put back after a doubled (x2) run, so it is asked
       for just before the first one rather than for every model. */
    GLint env = -1;
    GLuint bound = 0;
    if (!outside) {
    bound = gls_array_binding();
    if (!g_meshArrayDepth) glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    gr_glBindBuffer(GL_ARRAY_BUFFER, e.positionVbo ? e.positionVbo : e.vbo);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, e.positionVbo ? 3*sizeof(float) : sizeof(MeshBatchVertex),
                    e.positionVbo ? NULL : client ? (const void*)client->p : (const void*)offsetof(MeshBatchVertex,p));
    if (!depth) {
        if (e.positionVbo) gr_glBindBuffer(GL_ARRAY_BUFFER,e.vbo);
        const bool compact=e.compact && !client;
        const GLsizei stride=compact ? sizeof(MeshBatchAttributes) : sizeof(MeshBatchVertex);
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(4, GL_UNSIGNED_BYTE, stride, client ? (const void*)client->c : compact ? (const void*)offsetof(MeshBatchAttributes,c) : (const void*)offsetof(MeshBatchVertex,c));
        if (normals) {
            glEnableClientState(GL_NORMAL_ARRAY);
            glNormalPointer(GL_FLOAT, stride, client ? (const void*)client->n : compact ? (const void*)offsetof(MeshBatchAttributes,n) : (const void*)offsetof(MeshBatchVertex,n));
        } else glDisableClientState(GL_NORMAL_ARRAY);
        if (!g_meshBakePass || g_meshBakeCutout) {
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glTexCoordPointer(2, GL_FLOAT, stride, client ? (const void*)client->uv : compact ? (const void*)offsetof(MeshBatchAttributes,uv) : (const void*)offsetof(MeshBatchVertex,uv));
        } else glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    } else {
        glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    }
    bool x2 = false;
    for (const auto& r : e.runs) {
        if (r.tex == -3) glDisable(GL_TEXTURE_2D);
        else if (!depth) {
            const bool flash = g_meshFlash && pass != MODE_SHADOW && pass != MODE_XLU;
            if (r.tex >= 0 && !flash) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, livery_texture(src.tex[r.tex], house));
                set_wrap((unsigned char)r.wrap);
            } else glDisable(GL_TEXTURE_2D);
        }
        if (r.x2 != x2) {
            if (env < 0) glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &env);
            mesh_batch_x2(r.x2, env); x2 = r.x2;
        }
        if (!outside) { glDrawArrays(GL_TRIANGLES, (GLint)r.first, (GLsizei)r.count); g_meshBatchDraws++; }
    }
    if (x2) mesh_batch_x2(false, env);
    if (!outside && !g_meshArrayDepth) {
        glPopClientAttrib(); gr_glBindBuffer(GL_ARRAY_BUFFER,bound);
    }
    /* Array draws leave current attributes unspecified. Match the immediate path's
       last emitted attributes, because later legacy draws can inherit them. */
    if (!depth) glColor4ubv(e.last.c);
    if (normals) glNormal3fv(e.last.n);
    if (e.uv) glTexCoord2fv(e.last.uv);
}
/* Replay exactly the same ordered runs, culling each fragment in this pass's
   matrices. Bounds and vertices are the original CPU results, including wind,
   shroud, player paint, clipping, fill planes and the fading depth pre-pass. */
static void mesh_fragment_flush(void) {
    if (g_meshFragmentCommands.empty()) {mesh_fragment_polygon_apply();return;}
    bool streamHit=g_meshFragmentStore && !g_meshFragmentChanged
        && g_meshFragmentInputs.size()==g_meshFragmentStore->inputs.size()
        && g_meshFragmentCount==g_meshFragmentStore->vertices.size()
        && g_meshFragmentStore->vbo;
    if(g_meshFragmentStore && !mesh_perf_switch("OPENCNC3D_MESH_FRAGMENT_GPU_OK"))streamHit=false;
    if(!streamHit)mesh_fragment_materialize();
    const bool directPacked=g_meshFragmentPackedCollect;
    const size_t vertexCount=streamHit||directPacked ? g_meshFragmentCount : g_meshFragmentVertices.size();
    const auto& first=g_meshFragmentCommands.front();
    size_t stride=sizeof(MeshBatchVertex),colourOffset=offsetof(MeshBatchVertex,c);
    const bool wantPack=mesh_perf_switch("OPENCNC3D_MESH_FRAGMENT_PACK") && !first.normals;
    bool pack=wantPack && mesh_perf_switch("OPENCNC3D_MESH_FRAGMENT_PACK_OK");
    const size_t packedStride=first.depth ? 3*sizeof(float) : 5*sizeof(float)+4;
    if(vertexCount>(16u*1024u*1024u)/packedStride)pack=false;
    if(wantPack&&!pack&&!streamHit)g_meshFragmentPackFallbacks++;
    if(directPacked){stride=g_meshFragmentPackedStride;colourOffset=5*sizeof(float);}
    else if(pack) {
        try {
            const size_t packedBytes=vertexCount*packedStride;
            /* Exact growth and a16MiB scratch ceiling. A failed allocation keeps
               the original full-record upload/client path. No geometry is lost. */
            if(g_meshFragmentPacked.capacity()<packedBytes)std::vector<unsigned char>(packedBytes).swap(g_meshFragmentPacked);
            else g_meshFragmentPacked.resize(packedBytes);
            stride=packedStride;colourOffset=5*sizeof(float);
        } catch(const std::bad_alloc&) {pack=false;g_meshFragmentPackFallbacks++;}
    }
    if(streamHit && g_meshFragmentStore->gpuStride!=stride) {
        streamHit=false;mesh_fragment_materialize();
    }
    const size_t bytes=vertexCount*stride;
    const GLuint bound = gls_array_binding();
    GLint env = -1;                       /* asked for before the first doubled run */
    const bool cullWas=glIsEnabled(GL_CULL_FACE), offsetWas=glIsEnabled(GL_POLYGON_OFFSET_FILL);
    /* Transfer ownership of changed streams instead of copying their full payload.
       Compact only excessive scratch slack, so tiny streams cannot retain a large
       preceding allocation. Charge the actual retained capacity, not size. */
    if(g_meshFragmentStore && !streamHit && g_meshFragmentVertices.capacity()>std::max((size_t)4096,vertexCount*2))
        std::vector<MeshBatchVertex>(g_meshFragmentVertices).swap(g_meshFragmentVertices);
    const size_t retained=g_meshFragmentStore ? mesh_fragment_store_footprint(bytes,
        streamHit ? g_meshFragmentStore->vertices.capacity() : g_meshFragmentVertices.capacity(),
        g_meshFragmentInputs,g_meshFragmentCommands) : 0;
    const bool keep=g_meshFragmentStore && mesh_fragment_store_budget(retained);
    GLuint vbo=keep ? g_meshFragmentStore->vbo : 0;
    if(keep && !vbo)gr_glGenBuffers(1,&vbo);
    if(!keep) { streamHit=false;mesh_fragment_materialize();vbo=mesh_batch_frame_store(bytes); }
    if (!vbo) { if (!g_meshBatchStream) gr_glGenBuffers(1,&g_meshBatchStream); vbo=g_meshBatchStream; }
    const void* data=directPacked ? (const void*)g_meshFragmentPacked.data() : (const void*)g_meshFragmentVertices.data();
    if(directPacked)g_meshFragmentPackedBytes+=bytes;
    if(pack && !directPacked && !streamHit) {
        for(size_t i=0;i<vertexCount;++i) {
            unsigned char* d=&g_meshFragmentPacked[i*stride];const MeshBatchVertex& v=g_meshFragmentVertices[i];
            memcpy(d,v.p,sizeof v.p);
            if(!first.depth){memcpy(d+3*sizeof(float),v.uv,sizeof v.uv);memcpy(d+colourOffset,v.c,sizeof v.c);}
        }
        data=g_meshFragmentPacked.data();g_meshFragmentPackedBytes+=bytes;
    }
    gr_glBindBuffer(GL_ARRAY_BUFFER,vbo);
    if(!streamHit) {
        gr_glBufferData(GL_ARRAY_BUFFER,(FxGLsizeiptr)bytes,data,GL_STREAM_DRAW);
        g_meshFragmentUploadBytes+=bytes;g_meshFragmentFullBytes+=vertexCount*sizeof(MeshBatchVertex);
    }
    const bool gpu=(streamHit || mesh_store_holds(vbo,bytes,!keep))
        && (!keep || mesh_perf_switch("OPENCNC3D_MESH_FRAGMENT_GPU_OK"))
        && mesh_perf_switch("OPENCNC3D_MESH_FRAGMENT_BUFFER_OK");
    if(streamHit)g_meshFragmentStreamHits++;
    if(keep && !gpu) {
        /* A failed replacement must not leave a deleted prior handle reusable. */
        if(vbo!=g_meshFragmentStore->vbo){g_meshVerified.erase(vbo);gr_glDeleteBuffers(1,&vbo);}
        mesh_fragment_store_release(*g_meshFragmentStore);g_meshFragmentStore=NULL;vbo=0;
    }
    if(keep && gpu && !streamHit) {
        g_meshFragmentStore->gpuBytes=bytes;g_meshFragmentStore->vbo=vbo;g_meshFragmentStore->gpuStride=stride;

    }
    if (!gpu) g_meshVerified.erase(vbo);
    if (!gpu) gr_glBindBuffer(GL_ARRAY_BUFFER,0); /* preserve the bounded client fallback */
    const unsigned char* client=gpu ? NULL : directPacked ? g_meshFragmentPacked.data()
        : (const unsigned char*)g_meshFragmentVertices.data();
    if(client&&!directPacked){stride=sizeof(MeshBatchVertex);colourOffset=offsetof(MeshBatchVertex,c);}
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3,GL_FLOAT,(GLsizei)stride,client ? (const void*)client : (const void*)offsetof(MeshBatchVertex,p));
    const bool traceFragments = perf_trace_frame_active();
    const int traceBucket = traceFragments ? 4 * destruction_trace_pass_kind() : 0;
    if (traceFragments) {
        g_destructTrace[traceBucket] += g_meshFragmentCommands.size();
        g_destructTrace[traceBucket+2] += vertexCount;
        if(!streamHit)g_destructTrace[traceBucket+3] += bytes;
    }
    if (!first.depth) {
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(4,GL_UNSIGNED_BYTE,(GLsizei)stride,client ? (const void*)(client+colourOffset) : (const void*)colourOffset);
        if(first.normals) {
            glEnableClientState(GL_NORMAL_ARRAY);
            glNormalPointer(GL_FLOAT,(GLsizei)stride,client ? (const void*)(client+offsetof(MeshBatchVertex,n)) : (const void*)offsetof(MeshBatchVertex,n));
        } else glDisableClientState(GL_NORMAL_ARRAY);
        if(!g_meshBakePass || g_meshBakeCutout) {
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glTexCoordPointer(2,GL_FLOAT,(GLsizei)stride,client ? (const void*)(client+offsetof(MeshBatchVertex,uv)) : (const void*)offsetof(MeshBatchVertex,uv));
        } else glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    } else {
        glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    float clip[16];
    memcpy(clip, gls_clip(), sizeof clip);
    bool cull=cullWas, offset=offsetWas, x2=false;
    const bool coalesce=mesh_perf_switch("OPENCNC3D_MESH_FRAGMENT_COALESCE");
    size_t pendingFirst=0,pendingCount=0;
    const Pack* pendingSrc=NULL;int pendingHouse=0,pendingPass=0,pendingTex=0,pendingWrap=0;
    bool pendingDepth=false,pendingX2=false;
    const auto drawPending=[&]() {
        if(!pendingCount)return;
        glDrawArrays(GL_TRIANGLES,(GLint)pendingFirst,(GLsizei)pendingCount);g_meshBatchDraws++;
        if(traceFragments)g_destructTrace[traceBucket+1]++;
        pendingCount=0;
    };
    for(const auto& cmd:g_meshFragmentCommands) {
        /* A draw captures state when submitted. Drain before every state change,
           culled gap, or material boundary; only contiguous complete triangles
           under exactly the same state can join the pending ordered range. */
        if(cmd.cull!=cull || cmd.offset!=offset)drawPending();
        if(cmd.cull!=cull) { cull=cmd.cull; if(cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE); }
        if(cmd.offset!=offset) { offset=cmd.offset; if(offset) glEnable(GL_POLYGON_OFFSET_FILL); else glDisable(GL_POLYGON_OFFSET_FILL); }
        const bool outside=mesh_batch_outside(cmd.entry,clip);
        if(outside) g_meshBatchCulled++;
        for(const auto& r:cmd.entry.runs) {
            const bool join=coalesce && pendingCount && !outside
                && pendingFirst+pendingCount==r.first && pendingCount%3==0 && r.count%3==0
                && pendingSrc==cmd.src && pendingHouse==cmd.house && pendingPass==cmd.pass
                && pendingDepth==cmd.depth && pendingTex==r.tex && pendingWrap==r.wrap && pendingX2==r.x2;
            if(!join)drawPending();
            if(r.tex==-3) glDisable(GL_TEXTURE_2D);
            else if(!cmd.depth) {
                const bool flash=g_meshFlash && cmd.pass!=MODE_SHADOW && cmd.pass!=MODE_XLU;
                if(r.tex>=0 && !flash) { glEnable(GL_TEXTURE_2D);
                    glBindTexture(GL_TEXTURE_2D,livery_texture(cmd.src->tex[r.tex],cmd.house));
                    set_wrap((unsigned char)r.wrap);
                } else glDisable(GL_TEXTURE_2D);
            }
            if(r.x2!=x2) {
                if(env<0) glGetTexEnviv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,&env);
                mesh_batch_x2(r.x2,env); x2=r.x2;
            }
            if(!outside) {
                if(join) {pendingCount+=r.count;g_meshFragmentDrawsSaved++;}
                else {pendingFirst=r.first;pendingCount=r.count;pendingSrc=cmd.src;pendingHouse=cmd.house;
                    pendingPass=cmd.pass;pendingDepth=cmd.depth;pendingTex=r.tex;pendingWrap=r.wrap;pendingX2=r.x2;}
            }
        }
    }
    drawPending();
    if(x2) mesh_batch_x2(false,env);
    if(cull!=cullWas) { if(cullWas) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE); }
    if(offset!=offsetWas) { if(offsetWas) glEnable(GL_POLYGON_OFFSET_FILL); else glDisable(GL_POLYGON_OFFSET_FILL); }
    mesh_fragment_polygon_apply();
    glPopClientAttrib(); gr_glBindBuffer(GL_ARRAY_BUFFER,bound);
    const auto& last=g_meshFragmentCommands.back();
    if(!last.depth) glColor4ubv(last.entry.last.c);
    if(last.normals) glNormal3fv(last.entry.last.n);
    /* An untextured last range inherits the last textured range's current UV. */
    for(auto it=g_meshFragmentCommands.rbegin();it!=g_meshFragmentCommands.rend();++it)
        if(it->entry.uv) { glTexCoord2fv(it->entry.last.uv); break; }
    if(!streamHit){g_meshBatchUploads++;g_meshBatchUploadBytes+=bytes;} g_meshFragmentFlushes++;
    g_meshFragmentRanges+=(unsigned long)g_meshFragmentCommands.size();
    if(keep && gpu) {
        g_meshFragmentStoreBytes-=g_meshFragmentStore->bytes;
        if(!streamHit)g_meshFragmentStore->vertices.swap(g_meshFragmentVertices);
        g_meshFragmentStore->commands.swap(g_meshFragmentCommands);
        g_meshFragmentStore->inputs.swap(g_meshFragmentInputs);
        g_meshFragmentStore->bytes=mesh_fragment_store_footprint(g_meshFragmentStore->gpuBytes,
            g_meshFragmentStore->vertices.capacity(),g_meshFragmentStore->inputs,g_meshFragmentStore->commands);
        g_meshFragmentStoreBytes+=g_meshFragmentStore->bytes;
    }
    g_meshFragmentVertices.clear(); g_meshFragmentCommands.clear();
    if(directPacked)g_meshFragmentPacked.clear();
    g_meshFragmentInputs.clear();g_meshFragmentCount=0;g_meshFragmentChanged=true;
}
static std::vector<MeshBatchVertex> g_meshVertexScratch;
static std::vector<MeshBatchAttributes> g_meshAttributeScratch;
static void mesh_scratch_clear() {
    std::vector<MeshBatchVertex>().swap(g_meshVertexScratch);
    std::vector<MeshBatchAttributes>().swap(g_meshAttributeScratch);
}
struct MeshBatch {
    std::vector<MeshBatchVertex> vertices;
    MeshBatchEntry entry;
    MeshBatchVertex current = {};
    size_t first = 0;
    int tex = -2, wrap = -1; bool x2 = false;
    MeshGeometryPose* geometry = NULL;
    MeshKey attributesKey;
    bool fragmentQueued=false;
    void prepare(size_t count) {
        if (!vertices.capacity()) vertices.swap(g_meshVertexScratch);
        vertices.clear(); vertices.reserve(count);
    }
    ~MeshBatch() {
        if (vertices.capacity()>g_meshVertexScratch.capacity()
            && vertices.capacity()*sizeof(MeshBatchVertex)<=16u*1024u*1024u)
            vertices.swap(g_meshVertexScratch);
    }
    void begin(int t, int w, bool x) { first = vertices.size(); tex=t; wrap=w; x2=x; }
    void end() {
        if (vertices.size() > first) entry.runs.push_back({tex,wrap,x2,first,vertices.size()-first});
    }
    void uv(float u, float v) { current.uv[0]=u; current.uv[1]=v; entry.uv=true; }
    void normal(float x,float y,float z) { current.n[0]=x;current.n[1]=y;current.n[2]=z; }
    void color(unsigned char r,unsigned char g,unsigned char b,unsigned char a) {
        current.c[0]=r;current.c[1]=g;current.c[2]=b;current.c[3]=a;
    }
    void vertex(float x,float y,float z) {
        current.p[0]=x;current.p[1]=y;current.p[2]=z;
        for(int k=0;k<3;k++) {
            if(vertices.empty()) entry.lo[k]=entry.hi[k]=current.p[k];
            else {entry.lo[k]=std::min(entry.lo[k],current.p[k]);entry.hi[k]=std::max(entry.hi[k],current.p[k]);}
        }
        vertices.push_back(current);
    }
    void finish(const MeshKey& key, const Pack& src, int house,int pass,bool depth,bool normals,bool frame=false) {
        if(vertices.empty()) {
            if(g_meshFragmentCollect)mesh_fragment_input_record(key,0,~(size_t)0);
            /* NOTHING TO DRAW IS AN ANSWER TOO. A part offered to a pass none of its
               triangles belong to (the opaque pass for an all-cutout piece, and the
               reverse) emits nothing, and without an entry the whole triangle walk ran
               again on every draw only to find that out. An entry with no runs draws
               nothing (mesh_batch_draw returns at once). Admit through the same LRU,
               so cap pressure cannot make empty pieces repeatedly walk triangles. */
            if (!key.empty() && !g_meshFragmentCollect) {
                if (frame) mesh_batch_frame_keep(key,MeshBatchEntry());
                else mesh_batch_keep(key, MeshBatchEntry());
            }
            return;
        }
        entry.last=vertices.back(); entry.bytes=vertices.size()*sizeof(MeshBatchVertex);
        if(geometry) {
            memcpy(geometry->lo,entry.lo,sizeof entry.lo);
            memcpy(geometry->hi,entry.hi,sizeof entry.hi);geometry->boundsValid=true;
        }
        if (g_meshFragmentCollect) {
            /* Bound scratch storage. An unusually large asset drains in ordered chunks. */
            if (g_meshFragmentCount*sizeof(MeshBatchVertex)+entry.bytes > 64u*1024u*1024u) {
                mesh_fragment_store_disable();mesh_fragment_flush();
            }
            mesh_fragment_materialize();
            if(g_meshFragmentPackedCollect) {
                const size_t stride=depth?3*sizeof(float):5*sizeof(float)+4;
                if(vertices.size()>(16u*1024u*1024u)/stride
                    ||g_meshFragmentPacked.size()>16u*1024u*1024u-vertices.size()*stride
                    ||(g_meshFragmentCount&&g_meshFragmentPackedStride!=stride))mesh_fragment_flush();
                if(normals||!mesh_fragment_pack_append(vertices,depth)) {
                    /* A compact scratch allocation can fail after earlier pieces.
                       Submit that ordered prefix, then this piece through the existing
                       ordinary batch/client path. Continue with full-record collection. */
                    mesh_fragment_flush();g_meshFragmentPackedCollect=false;g_meshFragmentPackFallbacks++;
                    g_meshFragmentCollect=false;finish(key,src,house,pass,depth,normals,frame);
                    g_meshFragmentCollect=true;return;
                }
            }
            const size_t firstVertex=g_meshFragmentCount;
            mesh_fragment_input_record(key,vertices.size(),g_meshFragmentCommands.size());
            MeshFragmentCommand cmd;
            cmd.entry=std::move(entry); cmd.src=&src; cmd.house=house; cmd.pass=pass;
            cmd.depth=depth; cmd.normals=normals;
            cmd.cull=mesh_fragment_polygon(GL_CULL_FACE); cmd.offset=mesh_fragment_polygon(GL_POLYGON_OFFSET_FILL);
            for(auto& r:cmd.entry.runs)r.first+=firstVertex;
            if(!g_meshFragmentPackedCollect)
                g_meshFragmentVertices.insert(g_meshFragmentVertices.end(),vertices.begin(),vertices.end());
            g_meshFragmentCommands.push_back(std::move(cmd));
            return;
        }
        const GLuint bound = gls_array_binding();
        bool keep=!key.empty() && !frame;
        bool keepAttributes=false;
        bool frameStore=false;
        /* THE BAKE CONSUMES THE FRAME'S POSE AND NEVER MAKES ONE. It drops untextured
           triangles, so a pose it posted first would carry a shorter count and every
           later pass of the frame would fail the count test below and lose sharing.
           When an earlier pass already posted the pose and the counts agree, every
           triangle is textured, so the bake's corners are that pose's corners in the
           same order; its own colour and UV then go into a kept store like the lit
           pass's. */
        const bool bakeConsumes = g_meshBakePass && geometry && geometry->gpu
            && mesh_perf_switch("OPENCNC3D_BAKE_SHARED_POSE");
        if (frame && geometry && (!g_meshBakePass || bakeConsumes) && mesh_perf_switch("OPENCNC3D_MESH_SHARED_POSITIONS")) {
            entry.positionVbo=mesh_geometry_gpu(*geometry,vertices);
            if (entry.positionVbo && depth) {
                entry.vbo=entry.positionVbo; entry.bytes=vertices.size()*3*sizeof(float);
                mesh_batch_draw(entry,src,house,pass,depth,normals);
                mesh_batch_frame_keep(key,entry); return;
            }
            entry.compact=entry.positionVbo!=0;
            keepAttributes=entry.compact && !attributesKey.empty();
            if(keepAttributes) {
                const MeshBatchEntry* cached=mesh_batch_kept_find(attributesKey);
                if(cached && cached->compact) {
                    MeshBatchEntry draw=*cached;draw.positionVbo=entry.positionVbo;
                    memcpy(draw.lo,entry.lo,sizeof draw.lo);memcpy(draw.hi,entry.hi,sizeof draw.hi);
                    mesh_batch_draw(draw,src,house,pass,depth,normals);return;
                }
                keep=true;
            }
        }
        const void* data=vertices.data();
        if (entry.compact) {
            auto& attributes=g_meshAttributeScratch;
            attributes.resize(vertices.size());
            for (size_t i=0;i<vertices.size();i++) {
                memcpy(attributes[i].uv,vertices[i].uv,sizeof attributes[i].uv);
                memcpy(attributes[i].n,vertices[i].n,sizeof attributes[i].n);
                memcpy(attributes[i].c,vertices[i].c,sizeof attributes[i].c);
            }
            data=attributes.data(); entry.bytes=attributes.size()*sizeof(MeshBatchAttributes);
        }
        if(keep && !mesh_batch_admit(entry.bytes)) {keep=false;frame=true;}
        if(keep) gr_glGenBuffers(1,&entry.vbo);
        else {
            if(frame) {entry.vbo=mesh_batch_frame_store(entry.bytes);frameStore=entry.vbo!=0;}
            if(!entry.vbo) {if(!g_meshBatchStream) gr_glGenBuffers(1,&g_meshBatchStream);entry.vbo=g_meshBatchStream;}
        }
        gr_glBindBuffer(GL_ARRAY_BUFFER,entry.vbo);
        gr_glBufferData(GL_ARRAY_BUFFER,(FxGLsizeiptr)entry.bytes,data,keep?GL_STATIC_DRAW:GL_STREAM_DRAW);
        g_meshBatchUploads++; g_meshBatchUploadBytes+=entry.bytes;
        /* A kept store is new every time and always asked; the per-frame and stream
           stores are refilled every frame at much the same size and are asked only
           when they grow past what they have already been seen to hold. */
        if (!mesh_store_holds(entry.vbo, entry.bytes, !keep)) {
            g_meshVerified.erase(entry.vbo);
            gr_glDeleteBuffers(1,&entry.vbo); entry.vbo=0;
            if(frameStore) {
                auto& s=g_meshFrameStores[g_meshFrameNext-1];
                g_meshFrameBytes-=s.bytes; s.bytes=0; s.vbo=0;
            } else if(!keep) g_meshBatchStream=0;
        }
        mesh_batch_draw(entry,src,house,pass,depth,normals,entry.vbo?NULL:vertices.data());
        gr_glBindBuffer(GL_ARRAY_BUFFER,bound);
        if(frameStore && entry.vbo) mesh_batch_frame_keep(key,entry);
        if(keep && entry.vbo) {
            if(keepAttributes && g_meshBakePass)g_meshBakeShareStores++;
            if(keepAttributes){entry.positionVbo=0;mesh_batch_keep(attributesKey,std::move(entry));}
            else mesh_batch_keep(key,std::move(entry));
        }
    }
};
template<class T> static void mesh_batch_key(MeshKey& key,const T& v) {
    key.append((const char*)&v,sizeof v);
}
/* Active pieces keep their exact CPU positions, but used to re-walk source
   texture/colour/run preparation in every pass after those positions were ready.
   These immutable source templates remove that repeated walk. They never contain
   a pose, opacity or camera, and the caller opts in only where all vertex-varying
   source effects are absent. Other cases keep the original vertex loop. */
struct MeshPreparedCorner {
    float uv[2];
    unsigned char c[4];
    uint32_t source;
    unsigned char selflit;
    bool textured;
};
struct MeshPreparedEntry {
    std::vector<MeshPreparedCorner> corners;
    std::vector<MeshBatchRun> runs;
    bool uv=false;
    size_t bytes=0;
    std::list<MeshKey>::iterator lru;
};
static std::unordered_map<MeshKey,MeshPreparedEntry,MeshKeyHash> g_meshPrepared;
static std::list<MeshKey> g_meshPreparedLRU;
static size_t g_meshPreparedBytes=0;
static unsigned long long g_meshPreparedHits=0,g_meshPreparedBuilds=0,g_meshPreparedVertices=0,g_meshPreparedFallbacks=0;
static void mesh_prepared_clear() {
    g_meshPrepared.clear();g_meshPreparedLRU.clear();g_meshPreparedBytes=0;
}
static const MeshPreparedEntry* mesh_prepared_source(const Pack& src,const PackMesh& mesh,
        size_t first,size_t last,int pass,bool depth,bool normals,bool hasX2) {
    if(last-first>0xffffffffu/3u)return NULL;
    MeshKey key;const PackTri* triangles=mesh.tris.data();
    mesh_batch_key(key,triangles);mesh_batch_key(key,mesh.tris.size());
    mesh_batch_key(key,first);mesh_batch_key(key,last);mesh_batch_key(key,pass);
    mesh_batch_key(key,depth);mesh_batch_key(key,hasX2);
    mesh_batch_key(key,g_meshBakePass);mesh_batch_key(key,g_meshBakeCutout);mesh_batch_key(key,g_meshFlash);
    /* The caller excludes deformed source ranges. A wave elsewhere in this same
       mesh must not invalidate the immutable walls' source attributes each frame. */
    mesh_batch_key(key,&src);
    auto found=g_meshPrepared.find(key);
    if(found!=g_meshPrepared.end()) {
        g_meshPreparedLRU.splice(g_meshPreparedLRU.end(),g_meshPreparedLRU,found->second.lru);
        return &found->second;
    }
    const size_t cap=mesh_perf_switch("OPENCNC3D_MESH_PREPARED_BUDGET_FULL") ? 32u*1024u*1024u : 256u;
    size_t n=0;
    for(size_t i=first;i<last;++i)
        if((int)mesh.tris[i].mode==pass && (!g_meshBakePass||mesh.tris[i].tex>=0))n+=3;
    /* Charge both copies of each inline key (map + LRU) and conservative nodes. */
    const size_t overhead=sizeof(MeshPreparedEntry)+2u*sizeof(MeshKey)+192u;
    const size_t cornerBudget=sizeof(MeshPreparedCorner)+sizeof(MeshBatchRun);
    if(overhead>cap||n>(cap-overhead)/cornerBudget)return NULL;
    if(!mesh_perf_switch("OPENCNC3D_MESH_PREPARED_ALLOC_OK")){g_meshPreparedFallbacks++;return NULL;}
    try {
    MeshPreparedEntry entry;entry.corners.reserve(n);
    MeshBatchVertex current={};int tex=-2,wrap=-1;bool x2=false,started=false;size_t runFirst=0;
    const auto end=[&]() {if(entry.corners.size()>runFirst)entry.runs.push_back({tex,wrap,x2,runFirst,entry.corners.size()-runFirst});};
    for(size_t i=first;i<last;++i) {
        const PackTri& t=mesh.tris[i];if((int)t.mode!=pass)continue;
        int nextTex=tex,nextWrap=wrap;
        if(g_meshBakePass && !g_meshBakeCutout) {if(t.tex<0)continue;nextTex=-3;}
        else if(!depth && (t.tex!=tex || (t.tex>=0&&(int)t.wrap!=wrap))) {
            nextTex=t.tex;
            const bool flash=g_meshFlash && t.mode!=MODE_SHADOW && t.mode!=MODE_XLU;
            nextWrap=nextTex>=0&&!flash ? (int)t.wrap : -1;
        }
        const bool nextX2=hasX2 && nextTex>=0 && (mesh.tfl[i]&2)!=0;
        if(started && (nextTex!=tex || nextWrap!=wrap || nextX2!=x2)) {end();started=false;}
        tex=nextTex;wrap=nextWrap;x2=nextX2;
        if(!started){runFirst=entry.corners.size();started=true;}
        for(int k=0;k<3;++k) {
            const PackVert& v=t.v[k];
            if(tex>=0) {
                const PackTex& tx=src.tex[tex];
                current.uv[0]=v.u*(float)tx.uw/(float)tx.w;
                current.uv[1]=v.v*(float)tx.uh/(float)tx.h;entry.uv=true;
            }
            if(!depth) {current.c[0]=v.r;current.c[1]=v.g;current.c[2]=v.b;current.c[3]=v.a;}
            MeshPreparedCorner c;memcpy(c.uv,current.uv,sizeof c.uv);memcpy(c.c,current.c,sizeof c.c);
            c.source=(uint32_t)((i-first)*3+(size_t)k);
            c.selflit=(i<mesh.tfl.size()&&(mesh.tfl[i]&1))?255:0;c.textured=t.tex>=0;
            entry.corners.push_back(c);
        }
    }
    if(started)end();
    entry.bytes=entry.corners.capacity()*sizeof(MeshPreparedCorner)+entry.runs.capacity()*sizeof(MeshBatchRun)
        +overhead;
    if(entry.bytes>cap)return NULL;
    while(!g_meshPreparedLRU.empty()&&(g_meshPreparedBytes+entry.bytes>cap||g_meshPrepared.size()>=4096)) {
        auto old=g_meshPrepared.find(g_meshPreparedLRU.front());
        g_meshPreparedBytes-=old->second.bytes;g_meshPrepared.erase(old);g_meshPreparedLRU.pop_front();
    }
    auto inserted=g_meshPrepared.emplace(key,std::move(entry));
    try {g_meshPreparedLRU.push_back(key);}
    catch(const std::bad_alloc&){g_meshPrepared.erase(inserted.first);g_meshPreparedFallbacks++;return NULL;}
    inserted.first->second.lru=--g_meshPreparedLRU.end();g_meshPreparedBytes+=inserted.first->second.bytes;
    g_meshPreparedBuilds++;return &inserted.first->second;
    } catch(const std::bad_alloc&){g_meshPreparedFallbacks++;return NULL;}
}
static bool mesh_prepare_fragment(MeshBatch& output,const Pack& src,const PackMesh& mesh,
        MeshGeometryPose& geometry,size_t first,size_t last,int pass,bool depth,bool normals,
        bool hasX2,const float* extra,float rc,float rs,int house) {
    if(!geometry.boundsValid)return false;
    const MeshPreparedEntry* prepared=mesh_prepared_source(src,mesh,first,last,pass,depth,normals,hasX2);
    if(!prepared)return false;
    /* A template stores only this pass's emitted corners. Other modes in the same
       range may have no resolved position and must neither read nor generate one. */
    for(const auto& c:prepared->corners)if(!(geometry.valid[c.source]&1))return false;
    std::vector<MeshBatchRun> runs;
    try {runs=prepared->runs;}catch(const std::bad_alloc&){g_meshPreparedFallbacks++;return false;}
    unsigned char* direct=NULL;
    if(g_meshFragmentPackedCollect&&!normals) {
        const size_t stride=depth?3*sizeof(float):5*sizeof(float)+4;
        if(prepared->corners.size()>(16u*1024u*1024u)/stride
            ||g_meshFragmentPacked.size()>16u*1024u*1024u-prepared->corners.size()*stride
            ||(g_meshFragmentCount&&g_meshFragmentPackedStride!=stride))mesh_fragment_flush();
        direct=mesh_fragment_pack_space(prepared->corners.size(),depth);
        if(!direct)return false;
    } else output.prepare(prepared->corners.size());
    output.entry.runs.swap(runs);output.entry.uv=prepared->uv;
    memcpy(output.entry.lo,geometry.lo,sizeof geometry.lo);memcpy(output.entry.hi,geometry.hi,sizeof geometry.hi);
    size_t index=0;
    for(const auto& c:prepared->corners) {
        MeshBatchVertex v={};memcpy(v.uv,c.uv,sizeof v.uv);memcpy(v.c,c.c,sizeof v.c);
        memcpy(v.p,geometry.corners[c.source].p,sizeof v.p);
        if(normals) {
            if(!(geometry.valid[c.source]&2)) {
                const size_t i=first+c.source/3,k=c.source%3;
                const float* nn=&mesh.nrm[i*9+k*3];float nx=nn[0],ny=nn[1],nz=nn[2];
                if(extra) {
                    const float ax=extra[0]*nx+extra[1]*ny+extra[2]*nz;
                    const float ay=extra[4]*nx+extra[5]*ny+extra[6]*nz;
                    const float az=extra[8]*nx+extra[9]*ny+extra[10]*nz;
                    nx=ax;ny=ay;nz=az;
                }
                const float n[3]={nx*rc+nz*rs,ny,-nx*rs+nz*rc};
                memcpy(geometry.corners[c.source].n,n,sizeof n);geometry.valid[c.source]|=2;
            }
            memcpy(v.n,geometry.corners[c.source].n,sizeof v.n);
            v.c[0]=v.c[1]=v.c[2]=255;v.c[3]=c.selflit;
        } else if(!depth) {
            if(g_meshBakePass)v.c[3]=255;
            else if(g_meshShadowPass){v.c[0]=v.c[1]=v.c[2]=0;v.c[3]=g_meshShadowAlpha;}
            else if(g_meshFlash&&c.textured&&pass!=MODE_SHADOW&&pass!=MODE_XLU)v.c[3]=g_meshAlphaScale;
            else v.c[3]=(unsigned char)(((int)c.c[3]*(int)g_meshAlphaScale)/255);
        }
        if(direct) {
            unsigned char* d=direct+index*g_meshFragmentPackedStride;memcpy(d,v.p,sizeof v.p);
            if(!depth){memcpy(d+3*sizeof(float),v.uv,sizeof v.uv);memcpy(d+5*sizeof(float),v.c,sizeof v.c);}
            output.entry.last=v;
        } else output.vertices.push_back(v);
        ++index;
    }
    if(direct) {
        output.entry.bytes=index*sizeof(MeshBatchVertex);
        MeshFragmentCommand cmd;cmd.entry=std::move(output.entry);cmd.src=&src;cmd.house=house;cmd.pass=pass;
        cmd.depth=depth;cmd.normals=normals;cmd.cull=mesh_fragment_polygon(GL_CULL_FACE);cmd.offset=mesh_fragment_polygon(GL_POLYGON_OFFSET_FILL);
        for(auto& run:cmd.entry.runs)run.first+=g_meshFragmentCount;
        MeshKey empty;mesh_fragment_input_record(empty,index,g_meshFragmentCommands.size());
        g_meshFragmentCommands.push_back(std::move(cmd));output.fragmentQueued=true;
    }
    g_meshPreparedHits++;g_meshPreparedVertices+=index;return true;
}
static void mesh_fragment_polygon_drain() {
    if(!g_meshFragmentStateDeferred)return;
    mesh_fragment_flush();mesh_fragment_polygon_apply();g_meshFragmentStateDeferred=false;
}
static void mesh_batch_dump(void) {
    printf("MESHPERF|hits=%lu|uploads=%lu|culled=%lu|draws=%lu|class_skip=%lu|entries=%zu|bytes=%zu|pose_hits=%lu|pose_entries=%zu|pose_bytes=%zu|fragment_flushes=%lu|fragment_ranges=%lu|placed_promoted=%lu|imm_verts=%lu|upload_bytes=%llu\n",
      g_meshBatchHits,g_meshBatchUploads,g_meshBatchCulled,g_meshBatchDraws,g_meshClassSkipped,
      g_meshBatchCache.size(),g_meshBatchBytes,g_meshFrameHits,g_meshFrameCache.size(),g_meshFrameBytes,g_meshFragmentFlushes,g_meshFragmentRanges,g_meshPlacedPromoted,g_meshImmVerts,g_meshBatchUploadBytes);
    printf("HUDUPLOAD|uploads=%lu|skips=%lu|bytes=%lu\n",g_h6Uploads,g_h6UploadSkips,g_h6UploadBytes);
    printf("MESHCACHE|misses=%lu|evictions=%lu|lru=%zu|admission_refused=%llu\n", g_meshBatchMisses, g_meshBatchEvictions, g_meshBatchLRU.size(),g_meshBatchAdmissionRefused);
    printf("MESHPOOL|active_stores=%zu|reserved_stores=%zu|reserved_bytes=%zu|fallbacks=%lu|trimmed=%lu\n",
      g_meshFrameNext,g_meshFrameStores.size(),g_meshFrameBytes,g_meshFrameFallbacks,g_meshFrameTrimmed);
    printf("MESHGEOMETRY|hits=%lu|vertices=%lu|uploads=%lu|poses=%zu|cpu_bytes=%zu\n",
      g_meshGeometryHits,g_meshGeometryBuilds,g_meshGeometryUploads,g_meshGeometryKeys.size(),g_meshGeometryBytes);
    printf("MESHWINDDIRECT|draws=%llu|vertices=%llu|fallbacks=%llu|verified=%llu|mismatches=%llu|scratch_bytes=%zu\n",
      g_meshWindDirectDraws,g_meshWindDirectVertices,g_meshWindDirectFallbacks,
      g_meshWindDirectVerified,g_meshWindDirectMismatches,g_meshGeometryUpload.capacity()*sizeof(float));
    printf("MESHWINDDEPTH|draws=%llu|vertices=%llu|fallbacks=%llu|verified=%llu|mismatches=%llu\n",
      g_meshWindDepthDraws,g_meshWindDepthVertices,g_meshWindDepthFallbacks,
      g_meshWindDepthVerified,g_meshWindDepthMismatches);
    printf("MESHBAKESHARE|draws=%llu|vertices=%llu|stores=%llu|verified=%llu|mismatches=%llu\n",
      g_meshBakeShareDraws,g_meshBakeShareVertices,g_meshBakeShareStores,
      g_meshBakeShareVerified,g_meshBakeShareMismatches);
    printf("GLSHADOW|program|binds_skipped=%lu|queries_skipped=%lu|mismatches=%lu\n",
      g_glsProgramBindsSkipped,g_glsProgramQueriesSkipped,g_glsProgramMismatches);
    printf("MESHSETTLED|hits=%lu|entries=%zu|bytes=%zu\n",g_meshSettledHits,g_meshSettled.size(),g_meshSettledBytes);
    printf("FRAGMENTPREP|hits=%llu|builds=%llu|vertices=%llu|entries=%zu|bytes=%zu|draws_saved=%llu|packed_bytes=%llu|pack_scratch=%zu|pack_fallbacks=%llu|source_fallbacks=%llu|upload_bytes=%llu|full_bytes=%llu\n",g_meshPreparedHits,g_meshPreparedBuilds,g_meshPreparedVertices,g_meshPrepared.size(),g_meshPreparedBytes,g_meshFragmentDrawsSaved,g_meshFragmentPackedBytes,g_meshFragmentPacked.capacity(),g_meshFragmentPackFallbacks,g_meshPreparedFallbacks,g_meshFragmentUploadBytes,g_meshFragmentFullBytes);
    printf("FRAGMENTREUSE|inputs=%lu|streams=%lu|entries=%zu|bytes=%zu\n",g_meshFragmentInputHits,g_meshFragmentStreamHits,g_meshFragmentStores.size(),g_meshFragmentStoreBytes);
}

/* Script-only cap stress: empty ranges obey the entry bound, hot admission must
   not churn the current frame, and cold entries remain replaceable. */
static bool mesh_batch_selftest() {
    mesh_batch_clear();
    for(unsigned i=0;i<9000;i++) {MeshKey k;mesh_batch_key(k,i);mesh_batch_keep(k,MeshBatchEntry());}
    MeshKey first,last;const unsigned a=0,b=8999;mesh_batch_key(first,a);mesh_batch_key(last,b);
    const bool protect=mesh_perf_switch("OPENCNC3D_MESH_HOT_ADMISSION");
    bool ok=g_meshBatchCache.size()==MESH_BATCH_MAX_ENTRIES && g_meshBatchLRU.size()==MESH_BATCH_MAX_ENTRIES
        && !!mesh_batch_kept_find(first)==protect && !!mesh_batch_kept_find(last)!=protect && g_meshBatchBytes==0;
    const unsigned serial=g_drawFrameSerial;g_drawFrameSerial+=3;
    if(protect) {
        mesh_batch_keep(last,MeshBatchEntry());
        ok=ok && mesh_batch_kept_find(last) && g_meshBatchCache.size()==MESH_BATCH_MAX_ENTRIES;
    }
    g_drawFrameSerial=serial;
    printf("MESHCAPTEST|%s|entries=%zu|bytes=%zu|evictions=%lu\n",ok?"ok":"failed",g_meshBatchCache.size(),g_meshBatchBytes,g_meshBatchEvictions);
    mesh_batch_clear();return ok;
}
