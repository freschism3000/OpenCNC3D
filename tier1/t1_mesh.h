/*
 * t1_mesh.h -- the cartridge's own 3D models, drawn by the CPU.
 *
 * Reads a .t1mesh file (made by tools/win98/mkmesh.py from a baked scenario pack) and
 * draws a named model at a world position and facing. The mesh bank is byte-identical
 * across every shipped pack of the same side, so there is one file per side rather than
 * one per mission.
 *
 * Model units are 1024 to a world cell, read off the cartridge rather than fitted: the
 * flat ground-overlay display lists decode to quads spanning -512..+511, which is one
 * cell edge to edge. Meshes are authored CENTRED ON THEIR OWN ORIGIN, so an object is
 * drawn at the centre of its FOOTPRINT and not at the centre of its top-left cell. The
 * brain reports that centre directly as clx/cly in leptons, so nothing here keeps a
 * building-size table of its own.
 */

#ifndef T1_MESH_H
#define T1_MESH_H

#include "softras.h"
#include "t1_cam.h"

#define T1_MODEL_SCALE (1.0f / 1024.0f)

/* The four triangle modes the cartridge's display lists distinguish, and the masks the
 * draw takes. They used to be a bake-time decision -- shadow and translucent triangles
 * were thrown away by the converter -- which put the choice in the wrong place and made
 * the shadow sets unreachable without a re-bake. The pack carries all four now and the
 * renderer picks per pass. */
#define T1_MODE_OPAQUE 0
#define T1_MODE_CUTOUT 1
#define T1_MODE_SHADOW 2
#define T1_MODE_XLU    3

#define T1_MASK_SOLID  0x03      /* opaque + cutout: the ordinary object pass */
#define T1_MASK_SHADOW 0x04
#define T1_MASK_XLU    0x08

/* A triangle's texture index with this bit set names a SHADOW ALPHA PLANE rather than an
 * entry in the colour bank. One field, two banks, and an older renderer cannot mistake
 * one for the other. */
#define T1_SHADOW_TEX  0x40000000

typedef struct
{
    unsigned char *blob;
    long blobsize;

    const unsigned char *pal;      /* 768, the OBJECT palette, not the terrain's */
    int ntex;
    const unsigned char *texrec;   /* ntex x 20 bytes: w,h,uw,uh,dataoffset */
    const unsigned char *texdata;

    int nmesh;
    /* nmesh x 64: name[16], firsttri, ntri, firstpart, nparts, firstsec, nsec,
     *             animframes, ticksperframe, animoffset, clip0 t0, t1, loop */
    const unsigned char *meshrec;
    int npart;
    const unsigned char *partrec;  /* npart x 24: first, count, role, pivot[3] */
    int nsec;
    const unsigned char *secrec;   /* nsec x 4: a section's first triangle */
    int ntri;
    const unsigned char *tridata;  /* ntri x 80 bytes */
    long animsize;
    const unsigned char *animblob; /* per mesh at its animoffset: nf*nparts*12 floats,
                                    * then nf*nparts visibility bytes */

    int ntype;
    const unsigned char *typerec;  /* ntype x 12 bytes: code[8], i32 mesh index */

    SR_Texture *tex;
    /* Textures the loader had to RESHAPE to fit the Voodoo's 8:1 aspect cap, owned here
     * so they are freed with the bank. Eight is more than generous: exactly one texture
     * in the shipped bank needs it, and it is the one every 3D cursor draws with. */
    unsigned char *padded[8];
    int         npad, pad;
    /* THE SHADOW ALPHA PLANES. A MODE 2 triangle names one of these rather than a colour
     * texture, through a texture index with bit 30 set. They are a separate bank because
     * they are a separate FORMAT: one byte of coverage per texel and no palette at all.
     * See SR_Texture.alpha8 for why the palettised bank cannot hold them. */
    int          nshtex;
    const unsigned char *shrec;    /* nshtex x 20: w,h,uw,uh,dataoffset */
    const unsigned char *shdata;
    SR_Texture  *shtex;
    /* HOUSE COLOURS. The cartridge keeps TWO texture sets for 64 of these: the sand
     * table at ROM 0x98F30 for GoodGuy and the blue-grey one at 0x99130 for everyone
     * else, chosen by its own selector at RAM 0x80055d08. gdi[i] is the GDI variant of
     * texture i, or -1 where the pack carries only one set.
     *
     * `house` is which set the NEXT draw uses: 1 GDI, 0 Nod and neutral. It is set on the
     * bank rather than passed to t1_mesh_draw so that every call site does not have to
     * grow an argument it mostly does not care about; drawing is one object at a time on
     * one thread, so there is nothing for it to race with. */
    int        *gdi;
    int         house;
    /* ROTOR yaw, in the same DirType units as everything else: 0..255 for a full turn.
     * A helicopter's blades on the cartridge spin as a function of the ENGINE FRAME and
     * never of the wall clock, which is what keeps a scripted screenshot reproducible.
     * Set on the bank for the same reason `house` is. */
    int         rotor;
    int         ngdi;
    const unsigned char *gdirec;     /* ngdi x 8 bytes: base, variant */
} T1_MeshBank;

int  t1_mesh_load(T1_MeshBank *b, const char *path, char *err, int errlen);
void t1_mesh_free(T1_MeshBank *b);

/* -1 if the code has no model. Codes are the engine's INI names: MTNK, NUKE, E1. */
int  t1_mesh_for_type(const T1_MeshBank *b, const char *code);

/* Part roles, from the baker (bake5.py:26). */
#define T1_ROLE_STATIC 0
#define T1_ROLE_TURRET 1
#define T1_ROLE_ROTOR  2

/* Draws mesh `mi` at world (wx, wy, wz) in cells, turned to `facing` (0..255, 0 is
 * north, increasing clockwise, which is how the engine stores it).
 *
 * `tdelta` is the TURRET's offset from the body, i.e. tface - face, and it is applied to
 * parts with role TURRET about their own mount pivot BEFORE the body rotation. The two
 * compose so the turret ends up pointing at tface in world space, which is what the
 * engine means by it. Pass 0 for anything with no turret. */
long t1_mesh_draw(T1_MeshBank *b, SR_Target *t, const T1_Cam *cam, const T1_Screen *scr,
                  int mi, float wx, float wy, float wz, int facing, int tdelta);

/* ---- the full draw ------------------------------------------------------------------
 *
 * t1_mesh_draw above is this with everything optional left at its default, and it stays
 * because most callers -- walls, bullets, the model viewer -- want exactly that.
 *
 * `animT` is a FRACTIONAL baked frame. The pack stores a per-node delta from the rest
 * pose for each frame of a fixed grid, so the draw lerps between floor(animT) and the
 * next one and multiplies the already-posed vertex by the result. Negative means the
 * model does not animate, which is 171 of the 193 meshes and costs them one compare.
 *
 * `build_frac` under 1 draws only the first ceil(frac * nsections) SECTIONS of the mesh,
 * which is the cartridge's own construction order: one section is one G_VTX batch of the
 * display list, so a building assembles in the pieces its artist drew it in.
 *
 * `extra` is an optional 3x4 applied in MODEL units before everything else. The MCV
 * deploy rig uses it and nothing else does yet. */
typedef struct
{
    int          mesh;
    float        wx, wy, wz;      /* world position, in cells */
    int          facing, tdelta;  /* body facing 0..255, turret delta */
    int          modemask;        /* 0 means T1_MASK_SOLID */
    float        animT;           /* < 0: not animating */
    float        build_frac;      /* <= 0 or >= 1: the whole model */
    const float *extra;           /* 3x4 row major, or NULL */
    /* THE CARTRIDGE'S WASHBOARD LEAN. A vehicle on the console is not level: it pitches
     * and rolls by a fixed sinusoidal field of its own world position, so driving across
     * the map rocks it and two vehicles parked a cell apart sit at different angles. It
     * is what gives them weight, and it is a pure function of position -- no state, no
     * clock, and a scripted screenshot is unaffected.
     *   0 none (every building, wall, tree, cursor and bullet)
     *   1 vehicle: ax = 0.1*sin(6*wz), az = 0.1*sin(6*wx)
     *   2 ship:    az = 0.07*sin(1.6*wx) */
    int          wobble;
} T1_MeshParams;

#define T1_WOBBLE_NONE    0
#define T1_WOBBLE_VEHICLE 1
#define T1_WOBBLE_SHIP    2

long t1_mesh_draw_p(T1_MeshBank *b, SR_Target *t, const T1_Cam *cam, const T1_Screen *scr,
                    const T1_MeshParams *p);

/* The mesh's baked clip, for a caller that has to decide WHICH frame to ask for.
 * Returns 0 and touches nothing when the mesh does not animate. */
int t1_mesh_clip(const T1_MeshBank *b, int mi,
                 int *frames, int *tpf, int *t0, int *t1, int *loop);

/* Does this mesh carry a part with the given role. The facing law needs it: a turreted
 * unit whose mesh has NO turret part turns its whole hull to the turret facing when it is
 * standing, and one that HAS a turret part never does. */
int t1_mesh_has_role(const T1_MeshBank *b, int mi, int role);

/* How many construction sections the mesh has; 0 or 1 means it cannot be assembled. */
int t1_mesh_sections(const T1_MeshBank *b, int mi);

/* HOW MANY PARTS THE DRAW ACTUALLY TURNED, cumulative, by role.
 *
 * Rotor spin and turret facing have both been "implemented and unproven" for days, and a
 * screenshot cannot separate "the part did not turn" from "the camera was not looking at
 * one". These two counters can: a rotor that is drawn but never spun reports parts but no
 * turns. Reset with t1_mesh_spin_reset. */
void t1_mesh_spins(long *turret, long *rotor);
void t1_mesh_spin_reset(void);

#endif /* T1_MESH_H */
