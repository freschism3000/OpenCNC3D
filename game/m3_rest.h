/* Exact reuse of a completed skin across frames. This does not infer that a body
   is asleep: only the complete skin input matching bit for bit can reuse it.
   The pose arithmetic, animation clock, draw ordering and per-draw material stay
   in the original callers. A moving or newly cut body immediately misses.

   The persistent CPU data and VBO data each stay below 16 MiB/128 records. Two
   fixed observation banks use 1 MiB of key storage in total, with 128 observations
   and at most 1024 floats per key. Larger keys keep the ordinary frame cache.
   Record addresses remain stable throughout a frame: admission and eviction
   happen only after all previous frame references have been released. */
#ifndef OPENCNC3D_M3_REST_H
#define OPENCNC3D_M3_REST_H
#include <array>
#include <list>
#include <new>
#include <unordered_map>

static const size_t M3_REST_BYTES = 16u * 1024u * 1024u;
static const size_t M3_REST_RECORDS = 128;
static const size_t M3_REST_KEY_FLOATS = 1024;
static const size_t M3_REST_OBSERVATIONS = 128;
static const size_t M3_REST_SEEN_SLOTS = 256;

struct M3RestPose {
    const M3Model* m;
    M3PoseModelStamp stamp;
    unsigned nv, hash;
    std::vector<float> key, out;
    GLuint vbo;
    bool gpuTried;
    unsigned long long lastFrame;
    size_t bytes;
    M3RestPose() : m(0), nv(0), hash(0), vbo(0), gpuTried(false), lastFrame(0), bytes(0) {}
};
struct M3RestObserved {
    const M3Model* m;
    M3PoseModelStamp stamp;
    unsigned nv, nkey, hash;
    float key[M3_REST_KEY_FLOATS];
};
typedef std::list<M3RestPose> M3RestList;
typedef std::unordered_multimap<unsigned, M3RestList::iterator> M3RestMap;
static M3RestList g_m3Rest;
static M3RestMap g_m3RestMap;
static std::array<M3RestObserved, M3_REST_OBSERVATIONS> g_m3RestSeen[2];
static std::array<int, M3_REST_SEEN_SLOTS> g_m3RestSeenTab;
static size_t g_m3RestSeenCount = 0;
static unsigned g_m3RestSeenBank = 0;
static size_t g_m3RestBytes = 0, g_m3RestGpuBytes = 0;
static unsigned long long g_m3RestFrame = 0;
static unsigned g_m3RestHits = 0, g_m3RestUploads = 0, g_m3RestGpuDraws = 0;
static unsigned g_m3RestAdmit = 0, g_m3RestEvict = 0;

static bool m3_rest_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("OPENCNC3D_REST_POSES");
        on = !(e && *e && strcmp(e, "0") == 0);
    }
    return on != 0;
}

static bool m3_rest_model_live(const M3Model* m, const M3PoseModelStamp& stamp)
{
    /* Do not dereference a model pointer until it is known to remain loaded. */
    for (size_t i = 0; i < g_m3.size(); i++)
        if (&g_m3[i] == m) return m3_pose_model_stamp(g_m3[i]) == stamp;
    return false;
}

static M3RestMap::iterator m3_rest_lookup(const M3Model* m, const M3PoseModelStamp& stamp,
                                        size_t nv, const float* key, size_t nkey, unsigned hash)
{
    const std::pair<M3RestMap::iterator, M3RestMap::iterator> range = g_m3RestMap.equal_range(hash);
    for (M3RestMap::iterator f = range.first; f != range.second; ++f) {
        const M3RestPose& p = *f->second;
        if (p.m == m && p.stamp == stamp && p.nv == nv && p.key.size() == nkey
            && memcmp(&p.key[0], key, nkey * sizeof(float)) == 0)
            return f;
    }
    return g_m3RestMap.end();
}

static M3RestPose* m3_rest_find(const M3Model* m, const M3PoseModelStamp& stamp,
                               size_t nv, const float* key, size_t nkey, unsigned hash)
{
    if (!m3_rest_on() || !nv || !nkey || nkey > M3_REST_KEY_FLOATS) return 0;
    const M3RestMap::iterator f = m3_rest_lookup(m, stamp, nv, key, nkey, hash);
    if (f == g_m3RestMap.end()) return 0;
    M3RestList::iterator p = f->second;
    p->lastFrame = g_m3RestFrame;
    g_m3Rest.splice(g_m3Rest.end(), g_m3Rest, p);
    g_m3RestHits++;
    return &*p;
}

static void m3_rest_drop(M3RestList::iterator p)
{
    const std::pair<M3RestMap::iterator, M3RestMap::iterator> range = g_m3RestMap.equal_range(p->hash);
    for (M3RestMap::iterator f = range.first; f != range.second; ++f)
        if (f->second == p) { g_m3RestMap.erase(f); break; }
    if (p->vbo) {
        gr_glDeleteBuffers(1, &p->vbo);
        g_m3RestGpuBytes -= p->out.size() * sizeof(float);
    }
    g_m3RestBytes -= p->bytes;
    g_m3Rest.erase(p);
    g_m3RestEvict++;
}

static void m3_rest_free(void)
{
    while (!g_m3Rest.empty()) m3_rest_drop(g_m3Rest.begin());
    M3RestMap().swap(g_m3RestMap);
    g_m3RestSeenCount = 0; g_m3RestSeenBank = 0;
    g_m3RestSeenTab.fill(-1);
    g_m3RestBytes = g_m3RestGpuBytes = 0; g_m3RestFrame = 0;
    g_m3RestHits = g_m3RestUploads = g_m3RestGpuDraws = 0;
    g_m3RestAdmit = g_m3RestEvict = 0;
}

static bool m3_rest_seen_before(const M3PoseEnt& e)
{
    if (!g_m3RestSeenCount) return false;
    const size_t mask = M3_REST_SEEN_SLOTS - 1;
    for (size_t slot = e.hash & mask; g_m3RestSeenTab[slot] >= 0; slot = (slot + 1) & mask) {
        const M3RestObserved& p = g_m3RestSeen[g_m3RestSeenBank][g_m3RestSeenTab[slot]];
        if (p.hash == e.hash && p.m == e.m && p.stamp == e.stamp && p.nv == e.nv
            && p.nkey == e.nkey && memcmp(p.key, e.key, e.nkey * sizeof(float)) == 0)
            return true;
    }
    return false;
}

static void m3_rest_admit(const M3PoseEnt& e)
{
    if (m3_rest_lookup(e.m, e.stamp, e.nv, e.key, e.nkey, e.hash) != g_m3RestMap.end()) return;
    const size_t want = ((size_t)e.nv * 6 + e.nkey) * sizeof(float);
    while (!g_m3Rest.empty() && (g_m3Rest.size() >= M3_REST_RECORDS
                               || g_m3RestBytes + want > M3_REST_BYTES)) {
        /* At capacity keep last frame's actively reused records. Replacing them
           with another active pose every few frames would add copies and uploads
           without increasing coverage. Excess poses retain the frame fallback. */
        if (g_m3Rest.begin()->lastFrame + 1 >= g_m3RestFrame) return;
        m3_rest_drop(g_m3Rest.begin());
    }
    M3RestList::iterator p = g_m3Rest.end();
    try {
        g_m3Rest.emplace_back();
        p = --g_m3Rest.end();
        p->m = e.m; p->stamp = e.stamp; p->nv = e.nv; p->hash = e.hash;
        p->key.resize(e.nkey);
        p->out.resize((size_t)e.nv * 6);
        memcpy(&p->key[0], e.key, (size_t)e.nkey * sizeof(float));
        memcpy(&p->out[0], e.out, (size_t)e.nv * 6 * sizeof(float));
        p->bytes = (p->key.capacity() + p->out.capacity()) * sizeof(float);
        /* Account actual capacities, rather than assuming a vector allocates its size. */
        if (p->bytes > M3_REST_BYTES) { g_m3Rest.erase(p); return; }
        while (g_m3RestBytes + p->bytes > M3_REST_BYTES) {
            if (g_m3Rest.begin()->lastFrame + 1 >= g_m3RestFrame) {
                g_m3Rest.erase(p); return;
            }
            m3_rest_drop(g_m3Rest.begin());
        }
        p->lastFrame = g_m3RestFrame;
        g_m3RestMap.emplace(p->hash, p);
    } catch (const std::bad_alloc&) {
        if (p != g_m3Rest.end()) g_m3Rest.erase(p);
        return;                 /* the unchanged frame cache remains available */
    }
    g_m3RestBytes += p->bytes;
    g_m3RestAdmit++;
}

static void m3_rest_frame(void)
{
    ++g_m3RestFrame;
    g_m3RestHits = g_m3RestUploads = g_m3RestGpuDraws = 0;
    g_m3RestAdmit = g_m3RestEvict = 0;
    std::array<M3PoseEnt, M3_REST_RECORDS> add;
    size_t nadd = 0, nseen = 0;
    const unsigned nextBank = g_m3RestSeenBank ^ 1;
    if (m3_rest_on()) {
        for (size_t i = 0; i < g_m3PoseEnt.size() && nseen < M3_REST_OBSERVATIONS; i++) {
            const M3PoseEnt& e = g_m3PoseEnt[i];
            if (e.rest || !e.nv || !e.nkey || e.nkey > M3_REST_KEY_FLOATS
                || e.nv > M3_REST_BYTES / (6 * sizeof(float))
                || ((size_t)e.nv * 6 + e.nkey) * sizeof(float) > M3_REST_BYTES
                || !m3_rest_model_live(e.m, e.stamp)) continue;
            M3RestObserved& s = g_m3RestSeen[nextBank][nseen++];
            s.m = e.m; s.stamp = e.stamp; s.nv = e.nv; s.nkey = e.nkey; s.hash = e.hash;
            memcpy(s.key, e.key, e.nkey * sizeof(float));
            if (m3_rest_seen_before(e) && nadd < add.size()) add[nadd++] = e;
        }
    }
    /* All admission sources are non-persistent frame-arena entries. Their floats
       remain untouched until this function returns; releasing frame references
       first makes evicting any persistent CPU/VBO owner safe. */
    g_m3PoseEnt.clear();
    for (M3RestList::iterator p = g_m3Rest.begin(); p != g_m3Rest.end();) {
        M3RestList::iterator old = p++;
        if (!m3_rest_model_live(old->m, old->stamp) || g_m3RestFrame - old->lastFrame > 600)
            m3_rest_drop(old);
    }
    for (size_t i = 0; i < nadd; i++) m3_rest_admit(add[i]);
    g_m3RestSeenBank = nextBank; g_m3RestSeenCount = nseen;
    g_m3RestSeenTab.fill(-1);
    const size_t mask = M3_REST_SEEN_SLOTS - 1;
    for (size_t i = 0; i < nseen; i++) {
        size_t slot = g_m3RestSeen[nextBank][i].hash & mask;
        while (g_m3RestSeenTab[slot] >= 0) slot = (slot + 1) & mask;
        g_m3RestSeenTab[slot] = (int)i;
    }
}

static GLuint m3_rest_gpu(M3RestPose& p)
{
    if (!p.gpuTried) {
        p.gpuTried = true;
        gr_glGenBuffers(1, &p.vbo);
        if (p.vbo) {
            const size_t bytes = p.out.size() * sizeof(float);
            gr_glBindBuffer(GL_ARRAY_BUFFER, p.vbo);
            gr_glBufferData(GL_ARRAY_BUFFER, (FxGLsizeiptr)bytes, &p.out[0], GL_STATIC_DRAW);
            GLint got = 0;
            /* One allocation check for a retained store, never on a cached draw. */
            gr_glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &got);
            gr_glBindBuffer(GL_ARRAY_BUFFER, 0);
            if (got < 0 || (size_t)got != bytes) {
                gr_glDeleteBuffers(1, &p.vbo); p.vbo = 0;
            } else {
                g_m3RestGpuBytes += bytes;
                g_m3RestUploads++;
            }
        }
    }
    if (p.vbo) g_m3RestGpuDraws++;
    return p.vbo;
}

static void m3_rest_dump(void)
{
    printf("M3REST|enabled=%d|entries=%lu|bytes=%lu|gpubytes=%lu|observed=%lu|hits=%u|uploads=%u|gpudraws=%u|admit=%u|evict=%u\n",
           m3_rest_on() ? 1 : 0, (unsigned long)g_m3Rest.size(), (unsigned long)g_m3RestBytes,
           (unsigned long)g_m3RestGpuBytes, (unsigned long)g_m3RestSeenCount, g_m3RestHits,
           g_m3RestUploads, g_m3RestGpuDraws, g_m3RestAdmit, g_m3RestEvict);
}
#endif
