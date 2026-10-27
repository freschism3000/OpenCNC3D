/* ---- THE FIXED CARTRIDGE BUILDINGS (Enhanced only) ---------------------------------
 *
 * WHAT THEY ARE. The cartridge's building models were made for a camera that looks from
 * the south and never turns, so their north sides are missing, round parts are half
 * built, and the textures are 8 to 64 texels a side. tools/art/n64fix/ repairs one model
 * at a time: it completes the hidden sides, roughly doubles the triangles where that
 * reads as smoother, and raises the textures 4x. A repaired model is reviewed beside the
 * cartridge's and, once approved, baked by tools/art/n64fix/bake_enhanced.py into
 * n64fix/<TYPE>.n64x beside the missions. The file layout is documented at the top of
 * that script.
 *
 * WHAT THIS FILE DOES WITH THEM. Under Enhanced, a finished and healthy building of an
 * approved type draws the fixed mesh INSTEAD of the cartridge's, and through the SAME
 * pack-mesh path: draw_mesh with the same facing, ground, shroud, house colour, parts,
 * node animation, tint and flash. That is possible because the repair keeps the
 * cartridge's rendering model on purpose: texture times baked vertex colour, the same
 * parts with the same pivots, positions already posed the way the pack's are. So the
 * fixed model is appended to the mission pack as one more PackMesh, and its textures as
 * more PackTex entries, and nothing downstream can tell it from a cartridge model.
 *
 * The file carries the cartridge's own per-face wrap (mirror, clamp) where the baker
 * could read one off the cartridge model, so a texture the cartridge mirrors is
 * mirrored here too; set_wrap applies it per triangle exactly as for a pack model.
 *
 * WHAT IS BORROWED FROM THE CARTRIDGE AT LOAD, rather than stored in the file, so that
 * there is exactly one copy of each: the parts' roles and pivots, the animation clips,
 * frames and visibility, and the ground-shadow faces (MODE_SHADOW, the plate under the
 * footprint, which the repair never touches). The shadow faces go at the end of their
 * own part's run so the part cursor in draw_mesh still meets the parts in order.
 *
 * THE TEXTURE BOOKS. Six structures animate a surface by swapping its texture per time
 * slot (the Construction Yard's fans, the Refinery's light and storage strip, the
 * Helipad's lights, the Repair Bay's plates, the Silo's fill readout). The pack's book
 * meshes are cartridge triangles drawn over the base model's own copy of them, and the
 * repair moved most of those triangles, so over a fixed base they would land on nothing
 * or paint the cartridge's shape back. Each of the six is a pure texture swap, so the
 * baker records it as one: in slot k the fixed triangles textured `from` take texture
 * slotTex[k]. n64fix_attach turns that into one mesh per image, and draw_object_mesh
 * draws it where it would have drawn the cartridge's slot, which texbook_mesh_for still
 * chooses. The images were raised 4x the same way the fixed textures were.
 *
 * WHAT STAYS THE CARTRIDGE'S, and why. Construction and selling reveal the model
 * section by section in the cartridge's display-list order, the damage overlay is
 * drawn on top of the cartridge shell, and the collapse sheds and shatters the
 * cartridge's own section list. None of those orders exists for the fixed mesh, so a
 * building that is going up, being sold, below half health (the rule that draws the
 * damage overlay), dying or shattering keeps the cartridge model for as long as that
 * lasts, and the model visibly changes when it enters or leaves one of those states. Classic always keeps it. So does any type
 * with no approved file, and any type whose cartridge model in this pack does not have
 * the part count the file was made for.
 *
 * PRECEDENCE. The authored art set under F5 Experimental Art (models3d/<TYPE>.c3m, which
 * has a Construction Yard) is asked first by draw_object_mesh and answers the whole
 * object when its switch is on; this set is only asked when that one declined. So the
 * order is authored model, then fixed cartridge model, then cartridge model.
 *
 * TIER 1. Enhanced does not exist on the Win98 build, so neither does this. The meshes
 * themselves would draw there unchanged (fixed function, textures at most 256 a side);
 * what they would cost is texture memory, 4.7 MB of RGBA for the 22 types approved.
 *
 * THE DATABASE TAB turns a type's model on its own, with no building behind it and so
 * no build or damage state, and under Enhanced it turns the fixed one
 * (n64fix_codex_mesh). The turntable is where a missing north side shows most.
 * ---------------------------------------------------------------------------------- */

struct N64FixGroup {
    int part, tex;                   /* tex indexes N64FixModel::tex, -1 untextured */
    unsigned char mode, wrap;
    std::vector<PackVert> v;         /* three per triangle                          */
};
struct N64FixTex {
    int w, h;
    std::vector<unsigned char> base, gdi;   /* gdi empty when the decodes agree */
};
/* A texture book carried over to the fixed mesh: in slot k, every triangle drawn with
   texture `from` draws slotTex[k] instead. Indices are into N64FixModel::tex. */
struct N64FixBook {
    char letter;                     /* T U V W, as in the pack's <TYPE><letter><slot> */
    int from;
    std::vector<int> slotTex;
};
struct N64FixModel {
    std::string type;
    int nparts;
    std::vector<N64FixTex> tex;
    std::vector<N64FixGroup> group;
    std::vector<N64FixBook> book;
};

static std::vector<N64FixModel> g_n64fix;
static bool g_n64fixLoaded = false;
/* --non64fix clears it: the A/B for every judgement about this art. */
static bool g_n64fixOn = true;
/* Cartridge mesh index in the CURRENT pack -> the fixed mesh appended to it, or -1.
   Rebuilt by n64fix_attach on every pack load, because pack_free renumbers every
   index; the entries are checked against the pack before use for the same reason.
   g_n64fixBookOf is the same map for the texture books: a cartridge book slot's mesh
   -> the fixed book mesh that stands in for it over a fixed base, or -1 where that
   slot shows the base's own image and there is nothing to draw over it. */
static std::vector<int> g_n64fixOf;
static std::vector<int> g_n64fixBookOf;
/* THE OBSERVER. draw_object_mesh sets g_n64fixLast to whether the base model it just
   drew was a fixed one; the tactical view's solid pass counts it into g_n64fixDrawn,
   which is zeroed with the frame and read by the n64fixdump verb. So what is asserted
   is what was submitted on the pass the player sees, not a decision recomputed beside
   the draw, and a picture that happens to look right cannot stand in for it. */
static bool g_n64fixLast = false;
static int  g_n64fixDrawn = 0;

/* One file, read whole and checked before anything of it is kept. */
static bool n64fix_read_one(const char* path, N64FixModel& m)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    std::vector<unsigned char> b;
    {
        unsigned char chunk[65536];
        size_t n;
        while ((n = fread(chunk, 1, sizeof chunk, f)) > 0)
            b.insert(b.end(), chunk, chunk + n);
    }
    fclose(f);
    size_t o = 0;
    bool ok = true;
    #define N64X_NEED(n) do { if (o + (size_t)(n) > b.size()) { ok = false; goto done; } } while (0)
    {
        uint32_t ver = 0, np = 0, nt = 0, ng = 0, ntri = 0;
        char type[9];
        N64X_NEED(32);
        if (memcmp(&b[0], "N64X", 4) != 0) { ok = false; goto done; }
        memcpy(&ver, &b[4], 4);
        if (ver != 1 && ver != 2) {
            fprintf(stderr, "n64fix: %s is format version %u, this build reads 1 and 2 "
                            "-- skipped\n", path, ver);
            return false;
        }
        memcpy(type, &b[8], 8);
        type[8] = 0;
        memcpy(&np, &b[16], 4);
        memcpy(&nt, &b[20], 4);
        memcpy(&ng, &b[24], 4);
        memcpy(&ntri, &b[28], 4);
        o = 32;
        if (np == 0 || np > 256 || nt > 256 || ng > 4096 || ntri > 65536) { ok = false; goto done; }
        m.type = type;
        m.nparts = (int)np;
        m.tex.resize(nt);
        for (uint32_t t = 0; t < nt; t++) {
            uint16_t w = 0, h = 0;
            N64X_NEED(8);
            memcpy(&w, &b[o], 2);
            memcpy(&h, &b[o + 2], 2);
            const unsigned char flags = b[o + 4];
            o += 8;
            if (w == 0 || h == 0 || w > 256 || h > 256 || (w & (w - 1)) || (h & (h - 1))) {
                ok = false; goto done;
            }
            const size_t sz = (size_t)w * h * 4;
            N64X_NEED(sz);
            m.tex[t].w = w;
            m.tex[t].h = h;
            m.tex[t].base.assign(b.begin() + o, b.begin() + o + sz);
            o += sz;
            if (flags & 1) {
                N64X_NEED(sz);
                m.tex[t].gdi.assign(b.begin() + o, b.begin() + o + sz);
                o += sz;
            }
        }
        uint32_t seen = 0;
        int lastPart = -1;
        m.group.resize(ng);
        for (uint32_t g = 0; g < ng; g++) {
            N64FixGroup& G = m.group[g];
            int16_t part = 0, tex = 0;
            uint32_t n = 0;
            N64X_NEED(12);
            memcpy(&part, &b[o], 2);
            memcpy(&tex, &b[o + 2], 2);
            G.mode = b[o + 4];
            G.wrap = b[o + 5];
            memcpy(&n, &b[o + 8], 4);
            o += 12;
            G.part = part;
            G.tex = tex;
            /* ascending part order is what lets the attach lay the parts out as the
               contiguous runs draw_mesh walks */
            /* the wrap byte's top nibble is the running-track flipbook, which no
               building has */
            if (part < lastPart || part < 0 || part >= (int)np || tex < -1 || tex >= (int)nt
                || (G.mode != MODE_OPAQUE && G.mode != MODE_CUTOUT && G.mode != MODE_XLU)
                || (G.wrap & 0xF0)) {
                ok = false; goto done;
            }
            lastPart = part;
            N64X_NEED((size_t)n * 3 * 24);
            G.v.resize((size_t)n * 3);
            for (size_t k = 0; k < G.v.size(); k++) {
                PackVert& v = G.v[k];
                memcpy(&v.x, &b[o], 4);
                memcpy(&v.y, &b[o + 4], 4);
                memcpy(&v.z, &b[o + 8], 4);
                memcpy(&v.u, &b[o + 12], 4);
                memcpy(&v.v, &b[o + 16], 4);
                v.r = b[o + 20]; v.g = b[o + 21]; v.b = b[o + 22]; v.a = b[o + 23];
                o += 24;
            }
            seen += n;
        }
        /* Version 2: the texture books, as swaps of one texture for another per slot. */
        if (ver >= 2) {
            uint32_t nb = 0;
            N64X_NEED(4);
            memcpy(&nb, &b[o], 4);
            o += 4;
            if (nb > 4) { ok = false; goto done; }
            m.book.resize(nb);
            for (uint32_t k = 0; k < nb; k++) {
                N64FixBook& B = m.book[k];
                int16_t from = 0;
                uint32_t ns = 0;
                N64X_NEED(8);
                B.letter = (char)b[o];
                memcpy(&from, &b[o + 2], 2);
                memcpy(&ns, &b[o + 4], 4);
                o += 8;
                if (B.letter < 'T' || B.letter > 'W' || from < 0 || from >= (int)nt
                    || ns == 0 || ns > 400) {
                    ok = false; goto done;
                }
                B.from = from;
                N64X_NEED((size_t)ns * 2);
                B.slotTex.resize(ns);
                for (uint32_t q = 0; q < ns; q++) {
                    int16_t t = 0;
                    memcpy(&t, &b[o], 2);
                    o += 2;
                    if (t < 0 || t >= (int)nt) { ok = false; goto done; }
                    B.slotTex[q] = t;
                }
            }
        }
        if (seen != ntri || o != b.size()) ok = false;
    }
done:
    #undef N64X_NEED
    if (!ok)
        fprintf(stderr, "n64fix: %s is damaged or truncated -- skipped\n", path);
    return ok;
}

/* Once per process, like the authored models: the files do not depend on the mission.
   SCANNED BY NAME rather than by listing the directory, over every structure the engine
   knows, so a newly approved type is installed by dropping its file in and nothing has
   to be registered. */
static void n64fix_load(const char* dir)
{
    if (g_n64fixLoaded) return;
    g_n64fixLoaded = true;
    std::vector<std::string> types;
    static const char* const STRUCT_TYPES[] = {
        "AFLD", "ATWR", "BIO", "EYE", "FACT", "FIX", "GTWR", "GUN", "HAND", "HOSP",
        "HPAD", "HQ", "MISS", "NUK2", "NUKE", "OBLI", "PROC", "PYLE", "SAM", "SILO",
        "TMPL", "WEAP", "ARCO"
    };
    for (size_t i = 0; i < sizeof STRUCT_TYPES / sizeof STRUCT_TYPES[0]; i++)
        types.push_back(STRUCT_TYPES[i]);
    for (int k = 1; k <= 37; k++) {                     /* the civilian buildings */
        char v[8];
        snprintf(v, sizeof v, "V%02d", k);
        types.push_back(v);
    }
    std::string names;
    for (size_t i = 0; i < types.size(); i++) {
        char path[512];
        snprintf(path, sizeof path, "%sn64fix/%s.n64x", dir && *dir ? dir : "",
                 types[i].c_str());
        N64FixModel m;
        if (!n64fix_read_one(path, m))
            continue;
        if (m.type != types[i]) {
            fprintf(stderr, "n64fix: %s says it is %s -- skipped\n", path, m.type.c_str());
            continue;
        }
        names += (names.empty() ? "" : " ") + m.type;
        g_n64fix.push_back(m);
    }
    fprintf(stderr, "n64fix: %d fixed cartridge building%s for Enhanced%s%s\n",
            (int)g_n64fix.size(), g_n64fix.size() == 1 ? "" : "s",
            names.empty() ? "" : ": ", names.c_str());
}

static GLuint n64fix_upload(const std::vector<unsigned char>& px, int w, int h, bool bled = false)
{
    /* The same bleed and the same texture state load_pack gives every pack texture, so
       the bilinear switch and the alpha test treat these exactly as they treat those. */
    std::vector<unsigned char> prepared;
    const unsigned char* pixels = px.data();
    if (!bled) {
        prepared = px;
        fx_bleed_rgba(prepared.data(), w, h, 4);
        pixels = prepared.data();
    }
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, fx_filter_mode(GL_NEAREST));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, fx_filter_mode(GL_NEAREST));
    fx_filter_note(id, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    return id;
}

/* After every load of the mission pack: append each fixed model the pack has a
   cartridge model for, and remember which cartridge mesh it stands in for. Before the
   liveries are built, so a skirmish seat's colour reaches these textures too: they
   keep both decodes exactly as a pack texture with a house variant does. pack_free
   deletes the textures with the rest of the pack, because they ARE pack textures. */
static void n64fix_attach(void)
{
    const size_t ncart = g_pack.mesh.size();
    g_n64fixOf.assign(ncart, -1);
    g_n64fixBookOf.assign(ncart, -1);
    if (g_n64fix.empty()) return;
    int attached = 0;
    std::string refused;
    for (size_t k = 0; k < g_n64fix.size(); k++) {
        const N64FixModel& fm = g_n64fix[k];
        std::map<std::string, PackType>::const_iterator it = g_pack.type.find(fm.type);
        if (it == g_pack.type.end() || it->second.conf < 1) continue;
        const int cmi = it->second.mesh;
        if (cmi < 0 || cmi >= (int)ncart) continue;
        const PackMesh& cart = g_pack.mesh[cmi];
        if ((int)cart.parts.size() != fm.nparts) {
            char why[96];
            snprintf(why, sizeof why, " %s(parts %d, file %d)", fm.type.c_str(),
                     (int)cart.parts.size(), fm.nparts);
            refused += why;
            continue;
        }
        const int tex0 = (int)g_pack.tex.size();
        for (size_t t = 0; t < fm.tex.size(); t++) {
            const N64FixTex& ft = fm.tex[t];
            PackTex pt;
            pt.w = pt.uw = ft.w;
            pt.h = pt.uh = ft.h;
            pt.gl_gdi = 0;
            for (int e = 0; e < 7; e++) pt.gl_extra[e] = 0;
            pt.gl = n64fix_upload(ft.base, ft.w, ft.h);
            if (!ft.gdi.empty()) {
                pt.gl_gdi = n64fix_upload(ft.gdi, ft.w, ft.h);
                pt.nod = ft.base;          /* unbled, as load_pack keeps them */
                pt.gdi = ft.gdi;
            }
            g_pack.tex.push_back(pt);
        }
        PackMesh m;
        memset(m.name, 0, sizeof m.name);
        snprintf(m.name, sizeof m.name, "n64x:%s", fm.type.c_str());
        m.parts = cart.parts;
        m.animFrames = cart.animFrames;
        m.animTicksPerFrame = cart.animTicksPerFrame;
        m.clips = cart.clips;
        m.animMat = cart.animMat;
        m.animVis = cart.animVis;
        size_t gi = 0;
        for (int p = 0; p < fm.nparts; p++) {
            m.parts[p].tri0 = (int)m.tris.size();
            for (; gi < fm.group.size() && fm.group[gi].part == p; gi++) {
                const N64FixGroup& G = fm.group[gi];
                for (size_t v = 0; v + 2 < G.v.size(); v += 3) {
                    PackTri t;
                    t.tex = G.tex < 0 ? -1 : tex0 + G.tex;
                    t.mode = G.mode;
                    t.wrap = G.wrap;
                    t.v[0] = G.v[v]; t.v[1] = G.v[v + 1]; t.v[2] = G.v[v + 2];
                    m.tris.push_back(t);
                }
            }
            const PackPart& cp = cart.parts[p];
            for (int i = cp.tri0; i < cp.tri0 + cp.ntris && i < (int)cart.tris.size(); i++)
                if (cart.tris[i].mode == MODE_SHADOW)
                    m.tris.push_back(cart.tris[i]);
            m.parts[p].ntris = (int)m.tris.size() - m.parts[p].tri0;
        }
        const int fi = (int)g_pack.mesh.size();
        g_n64fixOf[cmi] = fi;
        g_pack.mesh.push_back(m);
        attached++;
        /* THE TEXTURE BOOKS, as meshes of the fixed model's own triangles. For each
           image a book shows, a copy of the fixed mesh's parts holding only the triangles
           textured `from`, retextured with that image. Drawn over the fixed base, they
           are the SAME triangles with the same pose, so they win the depth test exactly
           the way the cartridge's own copied books do, with no bias. One mesh per
           distinct image, not per slot: the Refinery's 100 slots show two images. */
        for (size_t bk = 0; bk < fm.book.size(); bk++) {
            const N64FixBook& B = fm.book[bk];
            std::map<int, int> meshOfTex;
            for (size_t k = 0; k < B.slotTex.size(); k++) {
                char code[24];
                snprintf(code, sizeof code, "%s%c%d", fm.type.c_str(), B.letter, (int)k);
                std::map<std::string, PackType>::const_iterator vt = g_pack.type.find(code);
                if (vt == g_pack.type.end() || vt->second.mesh < 0
                    || vt->second.mesh >= (int)ncart)
                    continue;
                const int st = B.slotTex[k];
                if (st == B.from) continue;            /* the base already shows it */
                std::map<int, int>::iterator mt = meshOfTex.find(st);
                if (mt == meshOfTex.end()) {
                    const PackMesh& base = g_pack.mesh[fi];
                    PackMesh bm;
                    memset(bm.name, 0, sizeof bm.name);
                    snprintf(bm.name, sizeof bm.name, "n64x:%s:%c", fm.type.c_str(),
                             B.letter);
                    bm.parts = base.parts;
                    bm.animFrames = base.animFrames;
                    bm.animTicksPerFrame = base.animTicksPerFrame;
                    bm.clips = base.clips;
                    bm.animMat = base.animMat;
                    bm.animVis = base.animVis;
                    for (size_t p = 0; p < base.parts.size(); p++) {
                        const PackPart& bp = base.parts[p];
                        bm.parts[p].tri0 = (int)bm.tris.size();
                        for (int i = bp.tri0; i < bp.tri0 + bp.ntris; i++)
                            if (base.tris[i].tex == tex0 + B.from) {
                                bm.tris.push_back(base.tris[i]);
                                bm.tris.back().tex = tex0 + st;
                            }
                        bm.parts[p].ntris = (int)bm.tris.size() - bm.parts[p].tri0;
                    }
                    meshOfTex[st] = (int)g_pack.mesh.size();
                    g_pack.mesh.push_back(bm);
                    mt = meshOfTex.find(st);
                }
                g_n64fixBookOf[vt->second.mesh] = mt->second;
            }
        }
    }
    fprintf(stderr, "n64fix: %d of %d fixed buildings attached to %s%s%s\n", attached,
            (int)g_n64fix.size(), g_pack.scen,
            refused.empty() ? "" : "; REFUSED, the cartridge model's parts differ:",
            refused.c_str());
}

/* THE ONE DECISION, asked by draw_object_mesh for the base model only (the texture
   books, doors, rigs and damage overlay are separate commands and keep their own
   meshes). Scene assembly: which mesh, not how it is drawn. */
static int n64fix_mesh_for(int mi, const SimObject& o)
{
    if (!g_n64fixOn || !g_fx.enabled || o.kind != K_BUILDING)
        return mi;
    if (mi < 0 || mi >= (int)g_n64fixOf.size())
        return mi;
    const int fi = g_n64fixOf[mi];
    if (fi < 0 || fi >= (int)g_pack.mesh.size())
        return mi;
    /* going up, being sold, damaged, dying or in pieces: the cartridge's own orders
       (sections, overlay, shed, shatter) only exist for the cartridge's own mesh */
    if (building_constructing(o) || o.str <= 0 || cart_is_damaged(o)
        || construction_frac(o) < 1.0f || shatter_owns(o))
        return mi;
    /* the debug pack is a side by side of cartridge art; it wins where it applies */
    if (debug_meshes_on() && g_debugMeshOf.count(mi))
        return mi;
    return fi;
}

/* The Database tab's turntable (codex_mod.h): a type's model with no object behind it,
   so no build or damage state to ask. The fixed model under Enhanced, else `mi`. */
static int n64fix_codex_mesh(int mi)
{
    if (!g_n64fixOn || !g_fx.enabled)
        return mi;
    if (mi < 0 || mi >= (int)g_n64fixOf.size())
        return mi;
    if (debug_meshes_on() && g_debugMeshOf.count(mi))
        return mi;
    const int fi = g_n64fixOf[mi];
    return (fi >= 0 && fi < (int)g_pack.mesh.size()) ? fi : mi;
}

/* The book slot to draw over a FIXED base, given the cartridge's slot mesh for this
   moment (texbook_mesh_for chose it, so the clock and the silo, helipad, repair bay and
   refinery readouts are unchanged). -1: nothing to draw, the base shows this image. */
static int n64fix_book_for(int tb)
{
    if (tb < 0 || tb >= (int)g_n64fixBookOf.size())
        return -1;
    const int fb = g_n64fixBookOf[tb];
    return (fb >= 0 && fb < (int)g_pack.mesh.size()) ? fb : -1;
}
