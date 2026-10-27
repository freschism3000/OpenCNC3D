/* Shared TREE3D4 mesh/atlas reader for scattered bushes and rocks.
   The imported tree instance and tree3d.pack were retired.
   Keep the shared format, GPU attributes and baker for foliage3d/rock3d only. */
#ifndef TREE3D_MOD_H
#define TREE3D_MOD_H

#include <zlib.h>

/* THE FOUR BAKED TERMS RIDE IN THE COLOUR ARRAY, one byte each. The vertex program
   needs sway, ao, thickness and the material flag, and a generic attribute would mean
   glBindAttribLocation, a hand-loaded glVertexAttribPointer and the location-zero
   aliasing hazard, for four numbers that are all a fraction between nothing and one.
   gl_Color is a standard client array, costs no extension, and eight bits is finer than
   anything the eye can find in an occlusion term. */
struct Tree3dModel {
    std::vector<float>         pos;   /* 3 per vertex */
    std::vector<float>         nrm;   /* 3 per vertex */
    std::vector<float>         uv;    /* 2 per vertex */
    std::vector<unsigned char> col;   /* 4 per vertex: sway, ao, thick, mat */
    std::vector<unsigned int>  idx;
    /* THE SAME MODEL IN GPU MEMORY, uploaded once on first draw (tree3d_gpu_ensure in
       cnc_eyes.cpp); zero until then and on any driver without buffer objects, where the
       arrays above are drawn from as before. Mutable because every draw path holds the
       model const. */
    mutable GLuint vbo, ibo;
    /* THE RIG, WHEN THE ARTIST SHIPPED ONE. Two bone indices and the first one's weight
       per vertex, as SHORTS, because glTexCoordPointer rejects GL_UNSIGNED_BYTE outright
       with GL_INVALID_ENUM and does it silently: a byte index would have arrived as a
       tree that never moved and no error to say why. 97 vertices in a hundred have a
       single influence, so the second slot is nearly always the first one again.
       frames holds nframes palettes of nbones matrices, each three vec4 rows ready for
       glUniform4fv with no transpose at draw time. */
    std::vector<short>         skin;  /* 3 per vertex: bone, bone, weight * 32767 */
    std::vector<float>         frames;
    int   nbones, nframes;
    float height, radius;
    int   set;                        /* which texture set this species uses */
};

/* The palette the shader carries. Fifty-four bones is the widest rig in the pack and
   sixty-four is the ceiling the bake refuses past, which costs 768 of the 4096 vertex
   uniform components this card reports. */
#define TREE3D_MAXBONES 64

/* HOW LONG ONE TURN OF THE AUTHORED SWAY IS. Every rig in the pack is 100 keys at the
   file's own frame rate, which comes out at 4.1667 seconds, and the bake decimates that
   to twenty frames. It is a constant rather than a per-model field because all sixteen
   agree; the day one does not, this becomes a float in the model record. */
#define TREE3D_LOOP_SECONDS 4.1667f

/* ONE TEXTURE SET PER SPECIES: foliage colour and normal, bark colour and normal.
   Foliage could have been atlased across every species, because a foliage atlas's UVs
   sit inside nought to one. BARK COULD NOT: a trunk's UVs on these models run from about
   minus nine to ten, because bark tiles up a trunk, and a tiling texture inside an atlas
   cell reads its neighbours. A birch's white bark against an oak's is also the first
   difference the eye finds. So the pack carries a set per species and a model says which
   one it uses. */
struct Tree3dSet { GLuint tex[4]; };

/* WHICH MODEL A CARTRIDGE NAME DRAWS. Picking by a hash of the cell is right when every
   tree is one species and wrong the moment T18 has to be an acacia because all 241 of
   its placements are on desert maps, and T05 a fir because all of its are temperate.
   The pack carries the decision; an unmapped name still falls back to the hash. */
struct Tree3dCode { char code[8]; int model; };


/* One sheet: a mip chain, each level deflated. Uploaded level by level rather than left
   to the driver, because the levels are not box filters of one another: the foliage
   colour's alpha is rescaled per level to hold its coverage, and glGenerateMipmap would
   throw that away. */
static bool tree3d_read_sheet(FILE* f, GLuint tex, bool repeat, const char* what)
{
    unsigned int nlev = 0;
    if (fread(&nlev, 4, 1, f) != 1 || !nlev || nlev > 16) {
        fprintf(stderr, "trees: %s has an impossible mip count\n", what);
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    std::vector<unsigned char> comp, raw;
    for (unsigned int l = 0; l < nlev; l++) {
        unsigned int hdr[4];
        if (fread(hdr, 4, 4, f) != 4)
            return false;
        const unsigned int w = hdr[0], h = hdr[1], rawn = hdr[2], compn = hdr[3];
        if (!w || !h || w > 4096 || h > 4096 || rawn != w * h * 4u || !compn ||
            compn > 64u * 1024u * 1024u) {
            fprintf(stderr, "trees: %s level %u is malformed\n", what, l);
            return false;
        }
        comp.resize(compn);
        raw.resize(rawn);
        if (fread(&comp[0], 1, compn, f) != compn)
            return false;
        uLongf got = rawn;
        if (uncompress(&raw[0], &got, &comp[0], (uLong)compn) != Z_OK || got != rawn) {
            fprintf(stderr, "trees: %s level %u would not inflate\n", what, l);
            return false;
        }
        glTexImage2D(GL_TEXTURE_2D, (GLint)l, GL_RGBA, (GLsizei)w, (GLsizei)h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, &raw[0]);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)(nlev - 1));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    /* THE FOLIAGE ATLAS MUST CLAMP AND THE BARK MUST REPEAT. Sixteen sprays share the
       atlas, so a wrapped sample there reads a neighbouring spray and draws a seam
       through a leaf; the bark is one tile swept round a cylindrical unwrap and has to
       tile or the trunk shows a band. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

/* The reader refuses rather than guesses: a short file, a bad magic or a count that does
   not fit leaves the feature off and the cartridge model drawing, which is the safe
   direction to fail in. */
static bool tree3d_load_into(const char* path, std::vector<Tree3dModel>& models,
                             std::vector<Tree3dSet>& sets, std::vector<Tree3dCode>& codes,
                             const char* who)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return false;
    char magic[8];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "TREE3D4", 7) != 0) {
        fclose(f);
        fprintf(stderr, "trees: %s is not a TREE3D4 pack (rebake with bake_tree3d.py)\n",
                path);
        return false;
    }
    unsigned int hdr[3];
    if (fread(hdr, 4, 3, f) != 3) { fclose(f); return false; }
    const unsigned int nmodel = hdr[1], nset = hdr[2];
    if (hdr[0] != 4 || !nmodel || nmodel > 256 || !nset || nset > 64) {
        fclose(f);
        fprintf(stderr, "trees: %s has an impossible header\n", path);
        return false;
    }

    models.clear();
    for (unsigned int m = 0; m < nmodel; m++) {
        unsigned int nv = 0, nt = 0, set = 0;
        float hr[2] = { 1.0f, 0.5f };
        if (fread(&nv, 4, 1, f) != 1 || fread(&nt, 4, 1, f) != 1 ||
            fread(&set, 4, 1, f) != 1 || fread(hr, 4, 2, f) != 2 ||
            !nv || !nt || nv > 400000 || nt > 400000 || set >= nset) {
            fclose(f); models.clear();
            fprintf(stderr, "trees: %s model %u is malformed\n", path, m);
            return false;
        }
        Tree3dModel t;
        t.vbo = t.ibo = 0;
        t.height = hr[0];
        t.radius = hr[1];
        t.set = (int)set;
        std::vector<float> rec((size_t)nv * 12);
        if (fread(&rec[0], 4, rec.size(), f) != rec.size()) {
            fclose(f); models.clear(); return false;
        }
        t.pos.resize((size_t)nv * 3); t.nrm.resize((size_t)nv * 3);
        t.uv.resize((size_t)nv * 2);  t.col.resize((size_t)nv * 4);
        for (unsigned int i = 0; i < nv; i++) {
            const float* r = &rec[(size_t)i * 12];
            t.pos[i*3+0] = r[0]; t.pos[i*3+1] = r[1]; t.pos[i*3+2] = r[2];
            t.nrm[i*3+0] = r[3]; t.nrm[i*3+1] = r[4]; t.nrm[i*3+2] = r[5];
            t.uv [i*2+0] = r[6]; t.uv [i*2+1] = r[7];
            for (int k = 0; k < 4; k++) {
                float v = r[8 + k];
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                t.col[i*4+k] = (unsigned char)(v * 255.0f + 0.5f);
            }
        }
        t.idx.resize((size_t)nt * 3);
        if (fread(&t.idx[0], 4, (size_t)nt * 3, f) != (size_t)nt * 3) {
            fclose(f); models.clear(); return false;
        }
        for (size_t i = 0; i < t.idx.size(); i++)
            if (t.idx[i] >= nv) {
                fclose(f); models.clear();
                fprintf(stderr, "trees: %s model %u indexes past its own vertices\n",
                        path, m);
                return false;
            }
        unsigned int rig[2] = { 0, 0 };
        if (fread(rig, 4, 2, f) != 2) {
            fclose(f); models.clear(); return false;
        }
        t.nbones  = (int)rig[0];
        t.nframes = (int)rig[1];
        if (t.nbones > 0 && t.nframes > 0) {
            if (t.nbones > TREE3D_MAXBONES || t.nframes > 256) {
                fclose(f); models.clear();
                fprintf(stderr, "trees: %s model %u has a rig this shader cannot hold "
                                "(%d bones, %d frames)\n", path, m, t.nbones, t.nframes);
                return false;
            }
            t.skin.resize((size_t)nv * 3);
            t.frames.resize((size_t)t.nframes * (size_t)t.nbones * 12);
            if (fread(&t.skin[0], 2, t.skin.size(), f) != t.skin.size() ||
                fread(&t.frames[0], 4, t.frames.size(), f) != t.frames.size()) {
                fclose(f); models.clear(); return false;
            }
            for (size_t i = 0; i < t.skin.size(); i += 3)
                if (t.skin[i] < 0 || t.skin[i] >= t.nbones ||
                    t.skin[i+1] < 0 || t.skin[i+1] >= t.nbones) {
                    fclose(f); models.clear();
                    fprintf(stderr, "trees: %s model %u binds a vertex to a bone it does "
                                    "not have\n", path, m);
                    return false;
                }
        } else {
            t.nbones = t.nframes = 0;
        }
        models.push_back(t);
    }

    unsigned int nmap = 0;
    if (fread(&nmap, 4, 1, f) != 1 || nmap > 256) { fclose(f); models.clear(); return false; }
    codes.clear();
    for (unsigned int i = 0; i < nmap; i++) {
        Tree3dCode c;
        unsigned int mi = 0;
        if (fread(c.code, 1, 8, f) != 8 || fread(&mi, 4, 1, f) != 1 || mi >= nmodel) {
            fclose(f); models.clear(); codes.clear();
            fprintf(stderr, "trees: %s has a bad name map\n", path);
            return false;
        }
        c.code[7] = 0;
        c.model = (int)mi;
        codes.push_back(c);
    }

    for (size_t i = 0; i < sets.size(); i++)
        glDeleteTextures(4, sets[i].tex);
    sets.clear();
    static const char* WHAT[4] = { "the foliage atlas", "the foliage normals",
                                   "the bark", "the bark normals" };
    static const bool REPEAT[4] = { false, false, true, true };
    for (unsigned int si = 0; si < nset; si++) {
        unsigned int dims[4];
        if (fread(dims, 4, 4, f) != 4) { fclose(f); models.clear(); return false; }
        Tree3dSet st;
        glGenTextures(4, st.tex);
        for (int i = 0; i < 4; i++) {
            if (!tree3d_read_sheet(f, st.tex[i], REPEAT[i], WHAT[i])) {
                glDeleteTextures(4, st.tex);
                fclose(f);
                models.clear();
                return false;
            }
        }
        sets.push_back(st);
    }
    fclose(f);

    size_t tris = 0;
    for (size_t i = 0; i < models.size(); i++)
        tris += models[i].idx.size() / 3;
    fprintf(stderr, "%s v4 -- %u model(s), %zu triangles, %u texture "
                    "set(s), %u cartridge name(s) mapped\n",
            who, nmodel, tris, nset, nmap);
    return true;
}

/* A tree's own settled character: which model, how big, which way round, how it leans
   and where in the wind's cycle it is. A hash of the CELL, never a random number and
   never the clock, so a wood looks the same on every run and two --shot runs of one
   script stay byte-identical. */
static unsigned tree3d_hash(int x, int y)
{
    unsigned h = (unsigned)(x * 73856093) ^ (unsigned)(y * 19349663);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}

#endif /* TREE3D_MOD_H */
