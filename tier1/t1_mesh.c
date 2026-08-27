/* t1_mesh.c -- see t1_mesh.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "t1_mesh.h"

static unsigned int rd32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}
static float rdf32(const unsigned char *p)
{
    union { unsigned int u; float f; } c;
    c.u = rd32(p);
    return c.f;
}

int t1_mesh_load(T1_MeshBank *b, const char *path, char *err, int errlen)
{
    FILE *f;
    long n;
    unsigned char *p;
    int i;

    memset(b, 0, sizeof *b);
    f = fopen(path, "rb");
    if (!f) { _snprintf(err, errlen, "cannot open %s", path); return 0; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b->blob = (unsigned char *)malloc((size_t)n);
    if (!b->blob) { fclose(f); _snprintf(err, errlen, "out of memory (%ld bytes)", n); return 0; }
    if (fread(b->blob, 1, (size_t)n, f) != (size_t)n)
    { fclose(f); _snprintf(err, errlen, "short read on %s", path); return 0; }
    fclose(f);
    b->blobsize = n;

    if (n < 800 || memcmp(b->blob, "T1MESH05", 8) != 0)
    { _snprintf(err, errlen, "%s is not a T1MESH05 file (re-run tools/win98/mkmesh.py)", path); return 0; }
    p = b->blob + 8;
    if (rd32(p) != 5) { _snprintf(err, errlen, "unknown t1mesh version %u", rd32(p)); return 0; }
    p += 4;

    b->pal = p; p += 768;
    b->ntex = (int)rd32(p); p += 4;
    b->texrec = p; p += (long)b->ntex * 20;
    { unsigned int tb = rd32(p); p += 4; b->texdata = p; p += tb; }
    /* The house map, immediately after the textures it indexes. */
    b->ngdi = (int)rd32(p); p += 4;
    b->gdirec = p; p += (long)b->ngdi * 8;
    b->nmesh = (int)rd32(p); p += 4;
    b->meshrec = p; p += (long)b->nmesh * 64;
    /* THE POOLS COME IN THE WRITER'S ORDER: sections, parts, triangles, animation, types.
     * Reading them in any other order takes one count for another and every offset after
     * it is wrong, silently. That happened once already, when parts and triangles were
     * swapped and the startup line reported "triangles 260", which is the PART count. */
    b->nsec = (int)rd32(p); p += 4;
    b->secrec = p; p += (long)b->nsec * 4;
    b->npart = (int)rd32(p); p += 4;
    b->partrec = p; p += (long)b->npart * 24;
    b->ntri = (int)rd32(p); p += 4;
    b->tridata = p; p += (long)b->ntri * 80;
    b->animsize = (long)rd32(p); p += 4;
    b->animblob = p; p += b->animsize;
    b->nshtex = (int)rd32(p); p += 4;
    b->shrec = p; p += (long)b->nshtex * 20;
    { unsigned int sb = rd32(p); p += 4; b->shdata = p; p += sb; }
    b->ntype = (int)rd32(p); p += 4;
    b->typerec = p; p += (long)b->ntype * 12;

    if (p > b->blob + n)
    { _snprintf(err, errlen, "%s is truncated", path); return 0; }

    b->gdi = (int *)malloc(sizeof(int) * (size_t)(b->ntex > 0 ? b->ntex : 1));
    if (b->gdi)
    {
        int k;
        for (k = 0; k < b->ntex; ++k) b->gdi[k] = -1;
        for (k = 0; k < b->ngdi; ++k)
        {
            int base = (int)rd32(b->gdirec + (long)k * 8);
            int var  = (int)rd32(b->gdirec + (long)k * 8 + 4);
            if (base >= 0 && base < b->ntex && var >= 0 && var < b->ntex)
                b->gdi[base] = var;
        }
    }
    b->house = 0;

    b->tex = (SR_Texture *)malloc(sizeof(SR_Texture) * (size_t)b->ntex);
    if (!b->tex) { _snprintf(err, errlen, "out of memory for %d textures", b->ntex); return 0; }
    b->pad = 0;
    for (i = 0; i < b->ntex; ++i)
    {
        const unsigned char *r = b->texrec + (long)i * 20;
        int w = (int)rd32(r), h = (int)rd32(r + 4);
        unsigned int off = rd32(r + 16);
        const unsigned char *px = b->texdata + off;

        /* THE VOODOO HAS TWO TEXTURE LIMITS AND THE PACK ONLY RESPECTS ONE.
         *
         * Everything here is a power of two and at most 256, which is the limit the baker
         * knew about. The card ALSO caps the aspect ratio at 8:1, and one texture in the
         * bank is 32x1. t1_glide_upload refuses it, and a triangle whose texture never
         * uploaded is silently DROPPED -- glide_tri returns before it even reaches the
         * reject counter, so the only trace is one line at startup.
         *
         * That one texture is not incidental. It is used by exactly seven meshes and all
         * seven of them are CURSORS (CUR02, 03, 04, 05, 07, 08 and 0D, 378 triangles
         * between them), so drawing the 3D cursor without this fix renders half the set
         * as fragments while the report says every texture uploaded.
         *
         * Padded here rather than in the pack, exactly as t1_dosinf.c pads the over-wide
         * DOS infantry strips, and for the same reason: the pack is shared with the
         * desktop build, which has no such limit. The pad REPLICATES the last row rather
         * than zero-filling, because v reaches 1.03 on those triangles under the card's
         * global WRAP and a transparent pad row would show as a seam. */
        {
            int big = w > h ? w : h, small = w > h ? h : w;
            if (small > 0 && big / small > 8)
            {
                int want = big / 8, y, x;
                unsigned char *np = (unsigned char *)malloc((size_t)(w > h ? w : want)
                                                          * (size_t)(w > h ? want : h));
                if (np)
                {
                    if (w > h)
                    {
                        for (y = 0; y < want; ++y)
                            memcpy(np + (long)y * w, px + (long)(y < h ? y : h - 1) * w,
                                   (size_t)w);
                        h = want;
                    }
                    else
                    {
                        for (y = 0; y < h; ++y)
                            for (x = 0; x < want; ++x)
                                np[(long)y * want + x] = px[(long)y * w + (x < w ? x : w - 1)];
                        w = want;
                    }
                    px = np;
                    b->padded[b->npad++ % 8] = np;   /* freed with the bank */
                    if (b->npad <= 8) b->pad = 1;
                }
            }
        }
        sr_texture(&b->tex[i], px, w, h);
        /* Every object texture is cutout-capable: index 0 is the transparent one the
         * converter reserved, and no opaque texel was ever assigned to it. Setting the
         * flag on all of them costs one predictable branch and means MODE_CUTOUT needs
         * no separate texture list. */
        sr_texture_ckey(&b->tex[i], 1);
    }
    /* The shadow planes get SR_Textures of their own. No chroma key: coverage comes from
     * the alpha channel, which is the whole point of the format. */
    if (b->nshtex > 0)
    {
        b->shtex = (SR_Texture *)malloc(sizeof(SR_Texture) * (size_t)b->nshtex);
        if (!b->shtex)
        { _snprintf(err, errlen, "out of memory for %d shadow planes", b->nshtex); return 0; }
        for (i = 0; i < b->nshtex; ++i)
        {
            const unsigned char *r = b->shrec + (long)i * 20;
            sr_texture_alpha(&b->shtex[i], b->shdata + rd32(r + 16),
                             (int)rd32(r), (int)rd32(r + 4));
        }
    }
    return 1;
}

void t1_mesh_free(T1_MeshBank *b)
{
    int i;
    for (i = 0; i < b->npad && i < 8; ++i) free(b->padded[i]);
    if (b->shtex) free(b->shtex);
    if (b->gdi) free(b->gdi);
    if (b->tex) free(b->tex);
    if (b->blob) free(b->blob);
    memset(b, 0, sizeof *b);
}

int t1_mesh_for_type(const T1_MeshBank *b, const char *code)
{
    int i;
    for (i = 0; i < b->ntype; ++i)
    {
        const unsigned char *r = b->typerec + (long)i * 12;
        if (strncmp((const char *)r, code, 8) == 0)
            return (int)(int)rd32(r + 8);
    }
    return -1;
}

/* The mesh record's fields, by name rather than by offset arithmetic at each use. */
#define MR_FIRSTTRI  16
#define MR_NTRI      20
#define MR_FIRSTPART 24
#define MR_NPARTS    28
#define MR_FIRSTSEC  32
#define MR_NSEC      36
#define MR_NFRAMES   40
#define MR_TPF       44
#define MR_ANIMOFF   48
#define MR_CLIPT0    52
#define MR_CLIPT1    56
#define MR_CLIPLOOP  60

int t1_mesh_clip(const T1_MeshBank *b, int mi,
                 int *frames, int *tpf, int *t0, int *t1, int *loop)
{
    const unsigned char *mr;
    if (mi < 0 || mi >= b->nmesh) return 0;
    mr = b->meshrec + (long)mi * 64;
    if (rd32(mr + MR_NFRAMES) == 0) return 0;
    if (frames) *frames = (int)rd32(mr + MR_NFRAMES);
    if (tpf)    *tpf    = (int)rd32(mr + MR_TPF);
    if (t0)     *t0     = (int)rd32(mr + MR_CLIPT0);
    if (t1)     *t1     = (int)rd32(mr + MR_CLIPT1);
    if (loop)   *loop   = (int)rd32(mr + MR_CLIPLOOP);
    return 1;
}

/* Counted, not inferred. See t1_mesh_spins in the header for why. */
static long g_spin_turret, g_spin_rotor;
void t1_mesh_spins(long *turret, long *rotor)
{ if (turret) *turret = g_spin_turret; if (rotor) *rotor = g_spin_rotor; }
void t1_mesh_spin_reset(void) { g_spin_turret = g_spin_rotor = 0; }

int t1_mesh_sections(const T1_MeshBank *b, int mi)
{
    if (mi < 0 || mi >= b->nmesh) return 0;
    return (int)rd32(b->meshrec + (long)mi * 64 + MR_NSEC);
}

int t1_mesh_has_role(const T1_MeshBank *b, int mi, int role)
{
    unsigned int pfirst, npart, pi;
    if (mi < 0 || mi >= b->nmesh || b->npart <= 0) return 0;
    pfirst = rd32(b->meshrec + (long)mi * 64 + MR_FIRSTPART);
    npart  = rd32(b->meshrec + (long)mi * 64 + MR_NPARTS);
    for (pi = 0; pi < npart; ++pi)
    {
        if ((int)(pfirst + pi) >= b->npart) break;
        if ((int)rd32(b->partrec + (long)(pfirst + pi) * 24 + 8) == role) return 1;
    }
    return 0;
}

long t1_mesh_draw(T1_MeshBank *b, SR_Target *t, const T1_Cam *cam, const T1_Screen *scr,
                  int mi, float wx, float wy, float wz, int facing, int tdelta)
{
    T1_MeshParams p;
    memset(&p, 0, sizeof p);
    p.mesh = mi; p.wx = wx; p.wy = wy; p.wz = wz;
    p.facing = facing; p.tdelta = tdelta;
    p.animT = -1.0f; p.build_frac = 1.0f;
    return t1_mesh_draw_p(b, t, cam, scr, &p);
}

long t1_mesh_draw_p(T1_MeshBank *b, SR_Target *t, const T1_Cam *cam, const T1_Screen *scr,
                    const T1_MeshParams *pm)
{
    const unsigned char *mr;
    unsigned int first, ntri, pfirst, npart, nframes, pi;
    unsigned int trilimit;
    long drawn = 0;
    float ca, sa, ta, tca, tsa, rca, rsa;
    float wsx = 0.0f, wcx = 1.0f, wsz = 0.0f, wcz = 1.0f;
    int   wob = 0;
    int mask = pm->modemask ? pm->modemask : T1_MASK_SOLID;
    const float *amat = 0;
    const unsigned char *avis = 0;
    int af0 = 0, af1 = 0;
    float afmix = 0.0f;
    int mi = pm->mesh;

    if (mi < 0 || mi >= b->nmesh) return 0;
    mr = b->meshrec + (long)mi * 64;
    first   = rd32(mr + MR_FIRSTTRI);
    ntri    = rd32(mr + MR_NTRI);
    pfirst  = rd32(mr + MR_FIRSTPART);
    npart   = rd32(mr + MR_NPARTS);
    nframes = rd32(mr + MR_NFRAMES);
    if (!ntri) return 0;
    trilimit = first + ntri;

    /* CONSTRUCTION: only the first K sections, which are the display list's own G_VTX
     * batches in the order the cartridge submits them. Never fewer than one, so a
     * building that has just been placed shows a piece immediately rather than nothing. */
    if (pm->build_frac > 0.0f && pm->build_frac < 1.0f)
    {
        int nsec = (int)rd32(mr + MR_NSEC);
        unsigned int sfirst = rd32(mr + MR_FIRSTSEC);
        if (nsec > 1)
        {
            int k = (int)(pm->build_frac * (float)nsec + 0.999f);
            if (k < 1) k = 1;
            if (k < nsec) trilimit = rd32(b->secrec + (long)(sfirst + k) * 4);
        }
        else
        {
            /* no section table: a raw triangle prefix, still in display list order */
            unsigned int k = (unsigned int)(pm->build_frac * (float)ntri + 0.999f);
            if (k < 1) k = 1;
            if (k < ntri) trilimit = first + k;
        }
    }
    else if (pm->build_frac <= 0.0f) return 0;

    /* Which two baked frames this draw sits between, resolved once for the whole mesh. */
    if (pm->animT >= 0.0f && nframes > 0 && npart > 0)
    {
        float ft = pm->animT;
        unsigned int aoff = rd32(mr + MR_ANIMOFF);
        amat = (const float *)(b->animblob + aoff);
        avis = b->animblob + aoff + (long)nframes * npart * 12 * 4;
        af0 = (int)ft;
        afmix = ft - (float)af0;
        if (af0 < 0) { af0 = 0; afmix = 0.0f; }
        if (af0 >= (int)nframes) { af0 = (int)nframes - 1; afmix = 0.0f; }
        af1 = af0 + 1;
        if (af1 >= (int)nframes) af1 = (int)nframes - 1;  /* the CLIP decides looping */
    }

    /* Body yaw. Facing is 0..255 clockwise from north, and north is world -Z:
     *      wx = mx*cos - mz*sin
     *      wz = mx*sin + mz*cos
     * checkable at one value rather than by inspection, since at facing 64 (east) a point
     * at model north (0,-1) must land at world east (+1,0), and it does. */
    ta = (float)pm->facing * 6.28318531f / 256.0f;
    ca = (float)cos(ta);
    sa = (float)sin(ta);

    /* Turret yaw, in the SAME sense, so the two compose to tface in world space. */
    ta  = (float)(pm->tdelta & 255) * 6.28318531f / 256.0f;
    tca = (float)cos(ta);
    tsa = (float)sin(ta);

    /* The washboard lean, resolved once per object: a pure function of where it stands. */
    if (pm->wobble == T1_WOBBLE_VEHICLE)
    {
        float ax = 0.1f * (float)sin(6.0 * pm->wz);
        float az = 0.1f * (float)sin(6.0 * pm->wx);
        wsx = (float)sin(ax); wcx = (float)cos(ax);
        wsz = (float)sin(az); wcz = (float)cos(az);
        wob = 1;
    }
    else if (pm->wobble == T1_WOBBLE_SHIP)
    {
        float az = 0.07f * (float)sin(1.6 * pm->wx);
        wsz = (float)sin(az); wcz = (float)cos(az);
        wob = 1;
    }

    /* Rotor yaw, same units and same sense, about the rotor part's own mount pivot. */
    ta  = (float)(b->rotor & 255) * 6.28318531f / 256.0f;
    rca = (float)cos(ta);
    rsa = (float)sin(ta);

    if (npart == 0) { pfirst = 0; npart = 1; }

    for (pi = 0; pi < npart; ++pi)
    {
        unsigned int tri0, count, role, i;
        float px = 0.0f, pz = 0.0f;
        float sca = 1.0f, ssa = 0.0f;
        int spin = 0;
        float am[12];
        int amon = 0;

        if (b->npart > 0 && (int)(pfirst + pi) < b->npart)
        {
            const unsigned char *pr = b->partrec + (long)(pfirst + pi) * 24;
            tri0  = rd32(pr);
            count = rd32(pr + 4);
            role  = rd32(pr + 8);
            px    = rdf32(pr + 12);
            pz    = rdf32(pr + 20);       /* pivot y is not used: the yaw is about Y */
            if (role == T1_ROLE_TURRET && pm->tdelta != 0)
            { spin = 1; sca = tca; ssa = tsa; ++g_spin_turret; }
            else if (role == T1_ROLE_ROTOR)
            { spin = 1; sca = rca; ssa = rsa; ++g_spin_rotor; }
        }
        else { tri0 = first; count = ntri; }

        if (amat)
        {
            int q;
            const float *m0, *m1;
            /* A node before its first keyframe is ABSENT, not sitting at its rest pose:
             * the Construction Yard's pad is empty while the MCV is still unfolding. */
            if (avis && !avis[(long)af0 * npart + pi]) continue;
            m0 = amat + ((long)af0 * npart + pi) * 12;
            m1 = amat + ((long)af1 * npart + pi) * 12;
            for (q = 0; q < 12; ++q) am[q] = m0[q] + (m1[q] - m0[q]) * afmix;
            amon = 1;
        }

        for (i = 0; i < count; ++i)
        {
            const unsigned char *tr;
            int ti, mode, k;
            SR_Vertex v[3];

            if (tri0 + i >= trilimit) break;
            tr = b->tridata + (long)(tri0 + i) * 80;
            ti = (int)rd32(tr);
            mode = tr[4];
            if (!((mask >> mode) & 1)) continue;
            if (ti & T1_SHADOW_TEX)
            {
                int si = ti & 0x3FFFFFFF;
                if (!b->shtex || si < 0 || si >= b->nshtex) continue;
            }
            else if (ti < 0 || ti >= b->ntex) continue;
            for (k = 0; k < 3; ++k)
            {
                const unsigned char *vp = tr + 8 + k * 24;
                float mx = rdf32(vp);
                float my = rdf32(vp + 4);
                float mz = rdf32(vp + 8);
                float ox, oz;
                if (pm->extra)
                {
                    const float *e = pm->extra;
                    float bx = e[0]*mx + e[1]*my + e[2] *mz + e[3];
                    float by = e[4]*mx + e[5]*my + e[6] *mz + e[7];
                    float bz = e[8]*mx + e[9]*my + e[10]*mz + e[11];
                    mx = bx; my = by; mz = bz;
                }
                if (amon)
                {
                    /* The node's own delta from its rest pose. Our triangles are stored
                     * ALREADY POSED by that rest transform, so this multiplies rather
                     * than replaces, and identity is exactly the still picture. */
                    float axx = am[0]*mx + am[1]*my + am[2] *mz + am[3];
                    float ayy = am[4]*mx + am[5]*my + am[6] *mz + am[7];
                    float azz = am[8]*mx + am[9]*my + am[10]*mz + am[11];
                    mx = axx; my = ayy; mz = azz;
                }
                if (spin)
                {
                    /* About the part's own mount pivot, in MODEL units, because that is
                     * the space the pivot is given in. */
                    float dx = mx - px, dz = mz - pz;
                    mx = px + dx * sca - dz * ssa;
                    mz = pz + dx * ssa + dz * sca;
                }
                mx *= T1_MODEL_SCALE;
                my *= T1_MODEL_SCALE;
                mz *= T1_MODEL_SCALE;
                ox = mx * ca - mz * sa;
                oz = mx * sa + mz * ca;
                if (wob)
                {
                    /* World X then world Z, the console's own order, in CELL space so the
                     * lean pivots about the object's own anchor on the ground rather than
                     * about the mesh origin. */
                    float y1 = my * wcx - oz * wsx;
                    float z1 = my * wsx + oz * wcx;
                    float x2 = ox * wcz - y1 * wsz;
                    float y2 = ox * wsz + y1 * wcz;
                    t1_world_to_eye(cam, pm->wx + x2, pm->wy + y2, pm->wz + z1, &v[k]);
                }
                else
                t1_world_to_eye(cam, pm->wx + ox, pm->wy + my, pm->wz + oz, &v[k]);
                v[k].u     = rdf32(vp + 12);
                v[k].v     = rdf32(vp + 16);
                v[k].r     = (float)vp[20] * (1.0f / 255.0f);
                v[k].g     = (float)vp[21] * (1.0f / 255.0f);
                v[k].b     = (float)vp[22] * (1.0f / 255.0f);
                v[k].light = (float)vp[23] * (1.0f / 255.0f);
            }
            if (ti & T1_SHADOW_TEX)
            {
                /* A shadow face names an alpha plane. No house variant: no shadow texture
                 * carries one, because a shadow has no colour to vary. */
                drawn += t1_tri(t, &v[0], &v[1], &v[2],
                                &b->shtex[ti & 0x3FFFFFFF], scr);
            }
            else
            {
                /* The house set, chosen per triangle because a mesh mixes textures that
                 * have a GDI variant with textures that do not. */
                int tx = ti;
                if (b->house && b->gdi && b->gdi[ti] >= 0) tx = b->gdi[ti];
                drawn += t1_tri(t, &v[0], &v[1], &v[2], &b->tex[tx], scr);
            }
        }
    }
    return drawn;
}
