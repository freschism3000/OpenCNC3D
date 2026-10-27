/* ================================================================================
 * ENHANCED BUILDINGS (ebld) -- a remastered building that wears down hit by hit, breaks
 * at half health and blows apart when it dies, drawn and simulated by the renderer alone.
 *
 * WHAT IT IS. Under Enhanced, a building type with a baked ebld/<TYPE>.ebld file is not
 * drawn from the cartridge's model at all. A FULL file (the Power Plant) carries three
 * cuts of one remastered model and everything needed to animate them:
 *
 *   INTACT     the finished building, painted light and white vertex colour, its water
 *              surface a 4x4 flipbook.
 *   CUT        the same building cut into pieces that line up exactly: from outside it
 *              is indistinguishable from INTACT, but chips can come off it one at a time
 *              and its loose parts can fall away at half health. It carries a second
 *              texture set (the damaged look), a per-zone damage overlay and an ember
 *              glow.
 *   DESTROYED  about two hundred shards of mixed size, each one knowing which CUT piece
 *              it broke from, so a shard of a fallen piece starts where that piece lies.
 *
 * THE STAGES of one building, and what moves it between them:
 *
 *   INTACT     drawn from draw_object_mesh like any other model.
 *   HELD       the first hit. The CUT model takes over in place, identical outside, and
 *              every 5% of strength lost knocks one chip off the struck side, thrown back
 *              toward where the hit came from, while the damaged texture creeps into that
 *              side in patches.
 *   DAMAGED    the cartridge's own half-health rule (cart_is_damaged). The loose pieces
 *              break off under physics, the water drains and a waterfall pours, the whole
 *              building wears the damaged texture and a flickering ember glow, and the
 *              camera jolts at half the strength of a building's death.
 *   WRECKED    strength reaches zero. Everything still standing is swapped for the
 *              DESTROYED shards, a blast throws them, and after a few seconds the wreck
 *              sinks into the ground as it fades, leaving a scorch mark that lasts longer.
 *
 * NO DESTROYED SET (format version 4, nset 2). Every other approved structure carries
 * INTACT and CUT and wears down, breaks and plays its effects exactly as above, with what
 * the lab's later stages added: pieces of a turning part that keep turning, standing parts
 * that loop (a sagging crane arm trying to lift, a jammed door trying to close) or bend (a
 * flag pole), shoves of their own for loose pieces, a rolling ball, several draining pools
 * and pouring spills, the Silo's fill draining from the level it stood at, glass, and
 * sparks, glows and ion sparks with bursts, gravity, ends, strain bursts, fill gates and
 * lights. Its pieces also land on the standing building (the REST flag). At its death
 * there is no blast and no shard set: what stands is held where it is, what lies keeps
 * lying, and all of it sinks and fades on the wreck's timeline over the scorch mark.
 *
 * INTACT ONLY. A model may carry the INTACT set alone (format
 * version 3, nset 1): it is drawn whole at every strength, the cartridge's own damage
 * smoke and fire still say how hurt it is, and when it dies the old shatter does not
 * spawn: the model stands in the engine's death explosions until the engine lets the
 * object go, and then it is gone, over the Power Plant's scorch mark sized to its
 * footprint. Going up and being sold, it plays its placement (ebld_place.h), as the full
 * one does.
 *
 * MOVING PARTS (version 3). A piece of the intact model can turn about its own axis at
 * its own rate (a fan, a radar dish; the lab's painter.spin), aim with the building's
 * facing (the Gun Turret's turret, the SAM Site's launcher head: the engine's own facing,
 * as the cartridge's turret part follows it), or be a fill drawn flattened to a level
 * (the Silo's Tiberium, from the owning house's store). A flipbook texture on any piece
 * plays (water, a pool, a chase of signal lights). All of it on the engine clock.
 *
 * WALLS (version 3, the WALL flag). The sandbag, chain-link and concrete walls carry
 * sixteen connection pieces, and a wall cell draws the one its connection nibble names,
 * so a run of cells joins as the lab joined it (ebld_wall_draw).
 *
 * THE CIVILIAN BUILDINGS AND THE FENCES (version 6). Every civilian building (V01 to V37)
 * is a full model like a structure, owned by Neutral, read the first time the game asks
 * for its type; one standing on part of its footprint blows up and scorches on the cells
 * it blocks. The barbed wire and the wooden fence are walls like the other three. Parts
 * that move frame by frame (CLIPS: the oil pump's linkage on the cartridge's own clip, a
 * terrain object's idle loop with bursts sync'd to it) and a theater's own look of a type
 * are in the format too.
 *
 * WHO OWNS WHAT. Everything here is gated on one rule: the Enhanced picture is on, this
 * switch is on (--noebld is the A/B), the object is a building and a model is loaded and
 * attached for its type. Classic never reaches any of it, so a Classic frame is the frame
 * it always was. Under the rule, for such a building: its cartridge model, the damage
 * overlay, the fixed cartridge model and the cartridge's per-building emitter rows stand
 * down, and the old shatter does not spawn for it when it dies. Going up and being sold,
 * it is drawn by its placement (ebld_place.h): the lab's part-by-part placement played on
 * the engine clock, and backwards for a sale; where a type has none, or its placement does
 * not match the model, the cartridge's own section-by-section reveal draws it.
 *
 * THE VIEWER. Every behaviour and number here was converted from the remaster viewer,
 * tools/art/remaster/viewer/index.html, which plays the same model's damage and
 * destruction in a browser; "the viewer" below means that page. Its seconds are 15
 * engine ticks here, its cells are map cells, and its y-up, v-up axes are turned into
 * the pack's (x east, y up, z south, v down) by the baker.
 *
 * THE CLOCK. Every state change and every physics step happens on the engine tick edge
 * (ebld_step, called from refresh_objects inside its once-per-frame guard), never per
 * drawn frame: under --script a plain `tick` advances the sim without drawing, and a
 * pool advanced per frame would make two runs of one script disagree. Draws only lerp
 * between the last two ticks with g_tickAlpha, which is 0 under --shot and --script.
 * Randomness is efx_hash keyed on the building's kind-qualified key, the tick and the
 * piece, never rand() and never a wallclock.
 *
 * THE PHYSICS is a small rigid-body solver of its own, deterministic and plain C++: each
 * moving piece is one body with a sphere inertia, contact is tested at up to sixteen hull
 * points against the terrain height (and against the top of the building's slab while
 * that is standing), and pieces never collide with each other or with what is still
 * standing. They are thrown clear instead.
 *
 * THE DRAW is fixed function throughout: every piece is a draw_mesh call with a 3x4
 * extra matrix and a triangle range, exactly the way the old shatter draws its pieces
 * (two ranges in fact, its surfaces and then its broken faces, see EbldGL), and the
 * damage overlay is the same triangles again, blended in by zone (ebld_overlay_begin).
 * The glow is an additive redraw. Nothing here needs more than GL 1.3.
 *
 * TIER 1. Enhanced does not exist on the Win98 build, so neither does this; see
 * docs/tier1-gap.md. The Classic path, including the old shatter, is what that build
 * keeps.
 * ================================================================================ */
#ifndef EBLD_MOD_H
#define EBLD_MOD_H

/* ---- the file's own vocabulary ---------------------------------------------------- */
enum { EBS_INTACT = 0, EBS_CUT = 1, EBS_DESTROYED = 2, EBS_N = 3 };
enum { EBF_FIXED = 1, EBF_CHIP = 2, EBF_WATER = 4, EBF_WATERFALL = 8, EBF_DOUBLE = 16,
       EBF_SLAB = 32 };
/* The PARAM table, read by index. A file may carry more; they are ignored. */
enum { EBP_WATER_LEVEL = 0, EBP_WATER_DROP, EBP_WATER_FLOOR, EBP_WATER_SECONDS,
       EBP_FALL_SPEED, EBP_SCORCH_SIZE, EBP_SCORCH_SECONDS, EBP_SCORCH_FADE,
       EBP_SCORCH_TEX, EBP_SINK_AFTER, EBP_SINK_TIME, EBP_SINK_DEPTH, EBP_SHAKE_PER_CELL,
       EBP_CHIP_STEP, EBP_ZONE_ADD, EBP_FOOT_W, EBP_FOOT_H, EBP_N,
       /* VERSION 6, read when the file has them (0 otherwise): the centre of the death, its
          blast and its scorch, pack units from the footprint centre. A civilian building
          stands on only some of its footprint's cells (bdata.cpp's occupy lists), and its
          death is centred on those. */
       EBP_BLAST_X = EBP_N, EBP_BLAST_Z, EBP_N6,
       /* THE SEA, a tail a building standing in the water carries (bake_ebld.py's format):
          the surface, how a piece moves in the water, and three splash bursts of
          EBP_SEA_BN values each where a piece goes in */
       EBP_SEA_Z = EBP_N6, EBP_SEA_DRAG, EBP_SEA_SPIN, EBP_SEA_SINK, EBP_SEA_DEPTH,
       EBP_SEA_BURST, EBP_SEA_BN = 18, EBP_SEA_N = EBP_SEA_BURST + 3 * EBP_SEA_BN };
/* The pack meshes one model becomes on every pack load. */
enum { EBM_INTACT = 0, EBM_CUT, EBM_CUTDMG, EBM_GLOWC, EBM_OVER0,
       EBM_WRECK = EBM_OVER0 + 8, EBM_GLOWX, EBM_SCORCH, EBM_N };
/* A building's stage. */
enum { EB_INTACT = 0, EB_HELD, EB_DAMAGED, EB_WRECKED };

struct EbldTex {
    int w, h, grid, frames, fps, role;
    float fpsf;                               /* frames a second: fps over the file's divisor */
    /* THE PIXELS STAY IN THE FILE. `off` is where they start: the RGBA rows top to
       bottom (the GDI look, as a pack texture), then for house paint (house bit0) the
       Nod decode of the same texels and (bit1) the player-colour mask, one byte a texel,
       255 = house paint. ebld_attach reads them on every pack load, uploads them and lets
       them go: held here for the whole process they were 137 MB for 25 models. */
    long off;
    /* VERSION 5: the pixels are one zlib stream of `csize` bytes at `off` (the GDI look,
       then the Nod decode and the mask); 0 for a raw file */
    long csize;
    int house;
    bool bled;                                /* version 5: its alpha bleed is baked in */
    /* version 6: a cut-out sheet whose mip chain holds the alpha-tested coverage at every
       level (the trees' spray cards; ebld_upload_cover) */
    bool cover;
};
/* A corner: position, the two UV pairs, colour, and (version 5) its smooth normal in the
   pack frame; older files carry none and get their face's. */
struct EbldVert { float x, y, z, u, v, ud, vd; unsigned char r, g, b, a; float nx, ny, nz; };
/* A triangle's own flags (version 4, the byte that was padding): drawn from both sides;
   version 5: SELF-LIT (the light pass leaves it unlit), and moved by a wave. */
enum { EBT_TWO = 1, EBT_EMIT = 2, EBT_WAVE = 4 };
struct EbldTri {
    int tex, texDmg, texGlow, texOver;        /* model texture indices, -1 for none */
    unsigned char mode, kind, wrap, tflags;
    EbldVert v[3];
};
struct EbldPiece {
    char  name[33];                           /* version 5: up to 31; 15 before */
    int   parent;                             /* DESTROYED: the CUT piece it broke from */
    int   zone;                               /* chip zone 0..7, 255 for a non-chip */
    int   flags;
    float c[3];                               /* rest centre, pack units */
    float push[3];                            /* unit launch direction for the pulse */
    float size;                               /* largest extent, pack units */
    int   tri0, ntri;
    int   nhull;
    float hull[16][3];                        /* contact points relative to c, pack units */
};
struct EbldSet {
    std::vector<EbldPiece> piece;
    std::vector<EbldTri>   tri;
    bool  hasSlab;
    float slabTop;                            /* pack units y of the slab's top */
};
/* A MOVING PART of the intact model (format version 3): one INTACT piece that turns about
   its own axis (a fan, a radar dish), aims with the building's facing (a turret), or is a
   fill drawn flattened to a level (the Silo's Tiberium). a[] as the baker documents it. */
enum { EBV_SPIN = 0, EBV_TURRET = 1, EBV_FILL = 2, EBV_CLIP = 3 };
struct EbldMove { int piece, kind; float a[9]; };
/* A CLIP (format version 6): per-frame part animation, a 3x4 matrix per frame and part in
   the pack frame. An INTACT clip moves the INTACT pieces whose move is CLIP (a[0] the clip,
   a[1] its part): on the cartridge's own frame for the object (the Oil Pump nods as the
   cartridge's pump does), or flagged IDLE, as a loop on the engine clock (the Blossom
   Trees). A CUT clip is the damaged stage's own loop: its pieces (map) ride its parts from
   the switch on, starting at the tick whose phase is nearest the intact frame shown then,
   and show their INTACT part while held. The lab's anim and CLIPS, as the baker documents
   them. */
struct EbldClip {
    int set, nparts, frames, turn;
    bool idle;
    float fps;
    std::vector<int> seq;                     /* the frame order, empty: 0..frames-1 */
    std::vector<float> mat;                   /* frames x nparts x 12 */
    std::vector<float> phase;                 /* CUT: per tick, the intact frame it matches */
};
/* The file's flags: a wall's sixteen connection pieces (version 3); the lab's physics for
   the stages made after the Power Plant's, pieces resting on the standing building and a
   chip leaving along its own facing (REST); a scorch sized per footprint cell (version 4). */
enum { EBX_WALL = 1, EBX_REST = 2, EBX_SCORCHCELL = 4 };
/* THE EMITTER'S GATES AND SPARK FIELDS (version 4, the viewer's burst, gravity, until,
   burst_on, fill and light). */
enum { EBE_BURST = 1, EBE_GRAVITY = 2, EBE_UNTIL = 4, EBE_BURSTON = 8, EBE_FILL = 16,
       EBE_LIGHT = 32, EBE_SYNC = 64 };
struct EbldEmit {
    int state, src, piece, npoint;
    std::vector<float> pt;                    /* npoint x (x, y, z, weight) */
    float wsum;
    float rate, life[2], size[2], vel[3], dir[3], speed, jitter, drag, rise;
    unsigned char rgba[4];
    float fade, delay, ramp, disc;
    int eflags;
    float burstEvery[2], burstCount[2], gravity, until, untilFade;
    int onPiece;
    float onAbove, onCount[2], fill, light[5];
    /* VERSION 6: a burst of syncCount particles at syncAt seconds into every syncPeriod of
       the IDLE clock, so it lands on the same moment of the model's own loop every time */
    float syncAt, syncPeriod, syncCount[2];
};
/* THE EXPLOSION (the file's BOOM trailer, any version): one slot of the recipe the
   remaster lab plays when the building is destroyed, slot for slot in the lab's order. The
   source record is the cartridge's own kind (EfxSourceRec); its name pointer is set from
   `name` where it is emitted, so the record can move with the vector. A slot marked REP is
   the cartridge's own FRAG3 source, which Enhanced replaces: kept for its index (which
   keys the particles' hashes, as the lab's slot index does) and never played. */
enum { EBB_FLAT = 1, EBB_REP = 2 };
struct EbldBoomSlot {
    char name[24], sheet[12];
    EfxSourceRec src;
    int stage, flags, hold, every;
    short dx, dy, dz;
};
/* ONE LOOPING MOTION of a standing piece (version 4): a hinge (degrees about an axis
   through a pivot) or a slide (pack units along an axis), settling at `rest` once damaged
   and then trying, again and again, along one of its shapes. The viewer's looping motions
   (loopValue), number for number. */
struct EbldMotion {
    int type;                                 /* 0 hinge, 1 slide */
    float pivot[3], axis[3], rest, settle, stroke[2], period[2], shudder[2];
    std::vector<std::vector<float> > tries;   /* per shape, keys of (seconds, fraction, weight) */
};
/* WHAT A CUT PIECE DOES BESIDES FALLING (version 4, the cut spec's PUSH and the spin groups). */
enum { EBQ_PUSH = 1, EBQ_BALL = 2, EBQ_SOLID = 4, EBQ_BEND = 8, EBQ_LOOP = 16,
       EBQ_LOOPHELD = 32, EBQ_FACING = 64 };
struct EbldPX {
    int spin;                                 /* the moving part it turns with, -1 none */
    int parent;                               /* the piece whose loops carry this one's */
    int flags;
    float v[3], w[3], ghost, ball[3], bend[8], seed;
    std::vector<EbldMotion> mo;
};
/* A LIQUID of the CUT set (version 4; a version 2 or 3 file's water and waterfall are
   read into the same records from its PARAM table). */
enum { EBL_DRAIN = 0, EBL_FALL = 1, EBL_FILL = 2 };
enum { EBL_GATE = 1, EBL_SHIFT = 2 };    /* SHIFT (version 6): a drain with no floor moves
                                            down by the level's drop instead of shrinking */
struct EbldLiquid {
    int piece, kind, flags;
    float a, b, c, seconds, speed, gate, rest;
};
/* THE STANDING BUILDING AS SOMETHING TO LAND ON (the REST flag): the upward faces of the
   pieces that never move, in a grid over the footprint, so a falling piece can ask what
   surface lies under it. Pack units. */
struct EbldSupTri { float x[3], z[3], a, b, c, n[3]; };
struct EbldSupport {
    bool on;
    float x0, z0, cell;
    int nx, nz;
    std::vector<EbldSupTri> tri;
    std::vector<int> start, list;             /* per grid cell: [start[i], start[i+1]) in list */
};
/* A WAVE (version 5): a sheet moving in the wind, the Barracks' flag (the lab's WAVES,
   wave.json). a[] in the model frame (cells, Z up) as the baker documents it: origin 0-1,
   along 2-3, across 4-5, hoist 6, length 7, z 8-9, box s 10-11, box q 12, box z 13-14,
   rest amp, period, power 15-17, droop amp, low 18-19, gust depth 20, gust hz 21-22,
   pull 23, light dir 24-26, base 27, gain 28, wrap 29, contrast 30, has light 31, swell 32;
   then seven floats per mode: amp, wavelength, hz, power, lag, skew, phase. */
struct EbldWave { float a[33]; std::vector<float> mode; };
/* One waved triangle at rest: which wave, and per corner its position and normal (pack
   frame), its place along the sheet (s) and height (z) in model cells, its colour, and
   which face of the cloth it is (+1, -1, or 0 for a lip joining the two). */
struct EbldWaveV { float p[3], n[3], s, z; unsigned char c[3]; signed char side; };
struct EbldWaveT { int w; EbldWaveV v[3]; };
struct EbldWavePose {
    float time = 0.0f;
    float position[9], normal[9], light[3];
    bool valid = false;
};
struct EbldWaveCache {
    EbldWavePose pose[2];
    unsigned next = 0;
};
#include "building_service_types.h"
/* One texture's specular map in a .spec file: its own pixels at `off` (a zlib stream of red
   and green, `csize` bytes), or another texture's (`from`) taken at `scale` / 255 of its
   strength (the damaged stage's atlas takes the standing one's, dulled). */
/* (sheen: an own map's upright sheen, 0..255 from the record's flags bits 8..15, carried to
   the light pass in the map's blue: a broad, stronger sun highlight for a model whose shine
   is on upright round sides, which the tight lobe never reaches at the game camera) */
struct EbldSpecRec { int from = -1; int scale = 255; long off = 0; long csize = 0; int sheen = 0; };
struct EbldModel {
    BService service;
    std::string type;
    /* The prefix of this model's log lines: "ebld", or "evhc" for a remastered vehicle
       (evhc_mod.h keeps its models in this struct, to share the records it reads). */
    std::string tag;
    std::string path;                         /* the file, for the textures (EbldTex) */
    long fileSize;
    int version;
    /* 3: INTACT, CUT and DESTROYED, the building wears down, breaks and is wrecked. 2:
       INTACT and CUT, it wears down and breaks, and at its death what stands and what lies
       sinks where it is (see "NO DESTROYED SET" at the top). 1: the INTACT set alone, drawn
       whole at every strength and simply gone at its death (see "INTACT ONLY"). */
    int nset;
    int flags;                                /* EBX_* */
    std::vector<float> param;
    std::vector<EbldTex> tex;
    EbldSet set[EBS_N];
    std::vector<EbldEmit> emit;
    /* the explosion (the BOOM trailer); empty: the engine's own FRAG3 table plays it. dur
       is the anim's length in ticks (a continuous slot with no hold of its own emits while
       the stage is below it), until the stage after which, plus 16, nothing emits. */
    std::vector<EbldBoomSlot> boom;
    int boomDur = 0, boomUntil = 0;
    /* and the footprint cells the lab sizes the death's jolt by (its damaged stage's is
       half): 0 without the trailer, when the building's own footprint does */
    float boomShake = 0.0f;
    /* A TREE'S WIND (the SWAY trailer): per INTACT triangle corner two bytes, the sway
       weight and the needles' flutter weight; empty for everything else */
    std::vector<unsigned char> sway;
    long windDrawn = 0;                       /* lit draws with the wind on, for ebldump */
    std::vector<EbldMove> move;
    std::vector<int> moveOf;                  /* per INTACT piece, its move or -1 */
    std::vector<EbldPX> px;                   /* per CUT piece (version 4; defaults before) */
    std::vector<EbldLiquid> liquid;
    std::vector<int> liqOf;                   /* per CUT piece, its liquid or -1 */
    int nmotion;                              /* looping motions over all CUT pieces */
    std::vector<int> motion0;                 /* per CUT piece, its first motion's slot */
    bool hasTwo;                              /* a CUT triangle is drawn from both sides */
    EbldSupport sup;
    bool emitState[3] = { false, false, false };   /* it carries intact / damaged /
                                                      destroyed emitters */
    std::vector<EbldWave> wave;               /* version 5 */
    /* THE WAVED TRIANGLES (version 5, EBT_WAVE): per INTACT and CUT triangle its record
       or -1, the records (each corner at rest), and per pack mesh the triangles of it that
       are waved, (mesh triangle, record), with the time they were last posed at. */
    std::vector<int> waveOf[2];
    std::vector<struct EbldWaveT> waveT[2];
    std::vector<EbldWaveCache> waveCache[2];
    std::vector<std::pair<int, int> > waveMesh[EBM_N];
    std::vector<int> waveMeshSet;             /* per mesh slot: 0 INTACT, 1 CUT, -1 none */
    float wavePosed[EBM_N];
    /* A STAGED WALL (version 5): the sixteen connection masks' cut, wreck and emitters,
       [first, count) each in the CUT set, the DESTROYED set and the emitters. At load each
       mask becomes a model of its own (wallSub), which draws with this model's textures
       (its texOwner) and is what a damaged or destroyed wall cell is played by. */
    bool wallStaged = false;
    int  wallRange[16][6] = {};
    int  wallSub[16] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    int  wallMask = -1;                       /* a mask's model: its mask, else -1 */
    int  texOwner = -1;                       /* the model whose textures it draws with */
    /* A TREE'S OTHER TWO STAGES (<TYPE>_<TH>_BURN.ebld and _SNAG.ebld, intact-only files of
       their own): on the type's model, the models of its burning stage [1] and its snag [2]
       (-1: none); on a stage's model, the model it is a stage of and which stage it is.
       A stage's model is read and attached with its tree, its type is the tree's with a
       suffix no pack type has (so nothing that looks a type up ever finds it), and the
       tree's entry switches to it while the tree burns and after it has burnt (etree_*). */
    int  stageModel[3] = { -1, -1, -1 };
    int  stageOf = -1, stageKind = 0;
    int nchip;
    /* VERSION 6: the clips, and per CUT piece the CUT clip's part it rides and the INTACT
       clip's part it shows while held (-1, -1: none) */
    std::vector<EbldClip> clip;
    std::vector<int> clipPart, clipIPart;
    int clipIntact = -1, clipCut = -1;        /* the clips' indices, -1 none */
    /* RED ALERT'S DRIVEN CLIP (ebld_ra_drive_*): the row of RA_DRIVE whose engine state
       plays the intact clip, -1 none; and where its motion lies in the clip: the last rest
       frame before it moves, the first frame fully out, the last frame fully out, and the
       first frame back at rest (-1: a one-way clip, closed by playing the opening back) */
    int raDrive = -1;
    bool sea = false;                         /* its PARAM table has THE SEA (EBP_SEA_N) */
    int drvSeg[4] = { 0, 0, 0, -1 };
    bool drvSprite = false;                   /* its clip is the 1996 pictures, frame for frame */
    /* A THEATER'S OWN LOOK (version 6, <TYPE>_<TH>.ebld): 0 temperate, 1 desert, 2 winter;
       -1 for a file that serves every theater */
    int theater = -1;
    /* THE ATTACHMENT TO THE CURRENT PACK, rebuilt by ebld_attach on every pack load
       because pack_free renumbers every mesh and texture index. rng[m] holds EBR ints
       per piece of the set that mesh is built from (see EBR_* below): the piece's
       surface run [tri0, tri1) in THAT mesh, the model texture its first triangle
       wears (-1 for an empty run), its run of broken faces, and where the flipbook
       triangles at the END of its surface run begin (EBR_FA; B when it has none). The
       glow and overlay meshes drop triangles, so their ranges are not the set's.
       EVERY MESH IS TWO BLOCKS: all the pieces' surfaces first, in piece order, then
       all their broken faces (the rims along the cuts and the linings behind them), in
       piece order. The broken faces are drawn pushed back in depth, so a surface they
       touch always wins, and keeping them in a block of their own is what lets the
       standing pieces still go as a few long runs in each. */
    bool attached;
    int  tex0;
    int  mesh[EBM_N];
    std::vector<int> rng[EBM_N];
    int  shadow0, shadow1;                    /* the borrowed shadow plate in `intact` */
    bool resident;                            /* its whole model's textures are on the card */
    bool residentAll;                         /* and the rest of them (ebld_resident) */
    bool cutBuilt;                            /* its cut's meshes exist (ebld_build_cut) */
    int cutNext = 0;                           /* prepared original mesh kinds, 0..13 */
    double cutPrepareMs = 0.0;
    std::vector<float> cutAngles;              /* only while an incremental cut is building */
    PackMesh cutTemp;
    int cutBlock = 0;
    size_t cutPiece = 0;
    std::vector<unsigned char> texIntact;     /* per texture: the whole model draws with it */
    uint64_t stageTextures[4] = {};
    /* THE OBSERVER: whole-model draws in the colour picture since the last ebldump, counted
       where they are submitted (ebld_draw_object, ebld_wall_draw), so what a gate asserts
       is what was drawn and not a decision recomputed beside it. */
    long drawn;
    /* THE SPECULAR MAPS beside the file (<file stem>.spec, ebld_spec_read): per texture,
       where its map lies in that file, or none. Only the index is kept; the pixels are read
       when the texture goes to the card (ebld_spec_pixels). */
    std::string specPath;
    std::vector<EbldSpecRec> spec;
    bool hasSpec = false;
};
#include "ebld_texture_mask.h"

enum { EBR_A = 0, EBR_B, EBR_TEX, EBR_RA, EBR_RB, EBR_FA, EBR };

static std::vector<EbldModel> g_ebld;
static bool g_ebldLoaded = false;
/* --noebld clears it: the A/B for every judgement about this system. */
static bool g_ebldOn = true;
/* --noebldnormals clears it: the remasters' normals and self-lit class stay out of the
   light pass's normal target (ebld_draw_normals), the A/B for that alone. */
static bool g_ebldNrmOn = true;
/* The wall models, resolved per pack load by ebld_wall_draw: which model draws each wall
   kind (-1 none), for which attach (ebld_attach bumps the serial). */
/* Bytes of texture uploaded for the pack now loaded (ebld_resident). */
static long g_ebldResidentBytes = 0;
static void ebld_share_reset(void);   /* the textures trees share on the card, per pack */
static int g_ebldWallModel[6] = { -1, -1, -1, -1, -1, -1 };
static int g_ebldWallPack = -1;
static int g_ebldPackSerial = 0;
/* Bumped wherever a model is attached or detached outside a pack-wide attach, which
   bumps g_ebldPackSerial instead; ebld_model_of's memo is keyed on both. */
static unsigned g_ebldResolveGen = 0;
/* Object draws answered before their walk because the pass has no triangle of the model. */
static unsigned long g_ebldPassEarlyOuts = 0;
static unsigned g_ebldPrepareGeneration = 0;
struct EbldPrepareMetrics {
    uint64_t demandCutUs = 0, demandTextureUs = 0;
    unsigned demandCuts = 0, demandTextures = 0, demandFailures = 0;
};
static EbldPrepareMetrics g_ebldPrepareMetrics;

/* THE POSES DRAWN, the moving parts' observer: per building and moving piece, the angle
   (degrees) or fill level the colour picture last submitted it at. Written by the draw,
   printed and cleared by ebldump. */
struct EbldPoseSeen { char type[16]; int id, kind; float value; };
static std::map<std::pair<long long, int>, EbldPoseSeen> g_ebldPoseSeen;

/* ---- one moving piece -------------------------------------------------------------- */
struct EbldBody {
    float p[3], pp[3];                        /* world cells, this tick and the last */
    float q[4], pq[4];                        /* rotation (w, x, y, z), likewise */
    float v[3], w[3];                         /* cells/s and rad/s, world axes */
    float y0;                                 /* its height when the wreck began to sink */
    int   still;                              /* consecutive ticks under the rest limits */
    int   ghost;                              /* ticks it still passes through the building */
    bool  live;                               /* false: at rest in place, no body */
    bool  asleep;
    bool  pinned;                             /* held where it is: the wreck's slab, and a
                                                 piece standing when a building with no
                                                 DESTROYED set dies */
    int   tore;                               /* the tick it came off (a flag on it keeps
                                                 the pose the wave had then) */
    int   wet;                                /* THE SEA: 0 dry, else the tick it went into
                                                 the water + 1; it then only drifts and sinks */
    bool  sunk;                               /* gone under: no longer drawn */
};
/* A looping motion's place in its tries, so the next frame need not count them again. */
struct EbldLoopCache { int k; double t; };
/* One emitter's own clock on one building: its next burst, how many it has fired, the
   tick its last burst flashed its light, and the try of each motion it last burst on. */
struct EbldEmitState { float next; int nburst, flash; long spawned; std::vector<int> tries;
                       double syncNext; };   /* (version 6: the next sync'd burst, idle clock) */
/* (nburst and spawned run on across stages: the burst hash's index, and the dump's totals) */

/* ---- one building ------------------------------------------------------------------ */
struct EbldBld {
    int   model, stage, id, kind;
    char  type[16];
    int   cx, cy;                             /* origin cell: a recycled slot changes it */
    float wx, wz, ground;                     /* the anchor, and the terrain under it */
    int   fw, fh, house;
    int   str, maxstr;
    int   applied;                            /* chip steps already applied */
    float want[8], cur[8], curPrev[8];        /* zone weights: asked for, shown, last tick */
    int   lastZone;
    int   chips;                              /* chips launched */
    std::vector<unsigned char> launched;      /* per CUT piece */
    std::vector<EbldBody> cut, wreck;         /* per CUT / DESTROYED piece */
    int   stageTick, drainTick, damagedTick, blastTick;
    int   heldTick;                           /* when it was first hit (a held loop's clock) */
    int   face;                               /* the building's facing, for a turret piece */
    int serviceDoing=BS_IDLE, serviceStage=0;
    bool servicePowerOff=false;
    float fillF;                              /* the owner's store, 0..1, or -1 unknown */
    float fillH0;                             /* the fill's level when it broke, pack y */
    float fillWreck;                          /* and when it died, pack y */
    bool  fresh, deathClaimed, sinking, dropped, seen, standDown, blush;
    bool  rose;                               /* first seen going up or being sold */
    bool  hurt;                               /* below half strength, the cartridge's rule */
    /* VERSION 6, the clips: the frame the cartridge's own model of the object shows this
       tick (-1 none), the tick of the damaged loop the switch started on, and the seconds
       this object's idle loop runs ahead of the engine clock */
    float animF;
    int   clipI0;
    float idlePhase;
    /* THE WEAPONS FACTORY'S DOOR: the frame its door clips show this tick, from the engine's
       door stage (weap_door_frame), and that stage; -1 for every other model */
    float doorF;
    int   doorStage;
    /* RED ALERT'S DRIVEN CLIP (ebld_ra_drive_step): how far out its motion is, 0 at rest to
       1 fully out, this tick and the last; whether it is on its way back; and a looping
       clip's frame this tick and the last */
    float drvCur, drvPrev;
    bool  drvClosing;
    float cycCur, cycPrev;
    /* A REMASTERED TREE'S STAGE (etree_*): 0 standing, 1 burning, 2 the charred snag; the
       tick it began; and whether the engine has deleted the tree, so that the entry, under
       a key of its own, is all that is left of it while the snag smokes out */
    int   treeStage, treeTick, fireTick;  /* (fireTick: the last tick its fire was seen) */
    bool  orphan;
    std::vector<float> acc;                   /* per emitter, fractional particles */
    std::vector<EbldEmitState> es;            /* per emitter */
    /* THE DEATH'S EXPLOSION, the lab's (ebld_boom_step): the tick it was born (-1 none, -2
       armed at the death and born on the next particle tick), the last stage fired, where
       it goes off (the death's centre, world cells, and the ground there), the chunks'
       house (1 GDI), and what it has emitted; and the tick the death was first seen, which
       the engine's own death anims are matched against (ebld_anims_filter). */
    int   boomBorn, boomLast, boomHouse, deathTick;
    float boomX, boomY, boomZ;
    long  boomParts, boomChunks;
    mutable std::vector<EbldLoopCache> lc;    /* per looping motion */
};
/* KEYED ON smooth_key(o) -- kind * 1000000 + id -- AND NEVER ON THE RAW ID. Heap ids
   are per kind, so a unit and a building can share one, and they are recycled. A wreck
   whose heap slot is taken by a new object before its pieces are gone moves to a
   negative key of its own (g_ebldOrphan), so it finishes rather than being cut short. */
static std::map<long long, EbldBld> g_ebldBld;
static long long g_ebldOrphan = 0;
static int g_ebldLastTick = -1;
/* Last tick's bullets, for the vanished-bullet half of the hit direction. */
static std::vector<EfxBullet> g_ebldBulletsPrev;

static const float EBLD_PI = 3.14159265f;

static bool ebld_finite(float f) { return f == f && f < 3.0e38f && f > -3.0e38f; }

/* ================================================================================
 * THE FILE. Read whole, checked completely, and refused whole on any inconsistency,
 * printing why: a half-read model would draw garbage or index outside its own arrays.
 * The layout is documented in tools/art/remaster/bake_ebld.py, which writes it.
 * ============================================================================== */
struct EbldIn {
    const std::vector<unsigned char>* b;
    size_t o;
    char why[200];
    bool bad;
    bool bounded = false;
    bool available(size_t count, size_t width)
    {
        if (!bounded) return true;
        if (bad) return false;
        if (o > b->size() || (width && count > (b->size() - o) / width)) {
            fail("bounded preparation count exceeds remaining source bytes");
            return false;
        }
        return true;
    }
    bool take(void* dst, size_t n)
    {
        if (bad) return false;
        if (o + n > b->size()) {
            snprintf(why, sizeof why, "it ends early at byte %lu", (unsigned long)o);
            bad = true;
            return false;
        }
        memcpy(dst, &(*b)[o], n);
        o += n;
        return true;
    }
    uint32_t u32() { uint32_t v = 0; take(&v, 4); return v; }
    int16_t  i16() { int16_t v = 0; take(&v, 2); return v; }
    uint16_t u16() { uint16_t v = 0; take(&v, 2); return v; }
    unsigned char u8() { unsigned char v = 0; take(&v, 1); return v; }
    float f32()
    {
        float v = 0.0f;
        take(&v, 4);
        if (!bad && !ebld_finite(v)) {
            snprintf(why, sizeof why, "a number at byte %lu is not finite",
                     (unsigned long)(o - 4));
            bad = true;
        }
        return v;
    }
    void fail(const char* what)
    {
        if (bad) return;
        snprintf(why, sizeof why, "%s", what);
        bad = true;
    }
};

/* ================================================================================
 * THE STANDING BUILDING AS GROUND (the REST flag). The lab's physics lets a falling piece
 * land on what is still standing: a roof plate settles on the floor under it, a dish on
 * the roof, a chip on a ledge. Here the upward faces of the pieces that never move once
 * damaged (fixed, not a chip, not turning, looping or bending, not a liquid) go into a
 * grid over their plan, and a point asks for the highest of them at or just below it: the
 * question the viewer's own fallback solver asks with a ray straight down. A face steeper
 * than about 72 degrees is a wall and holds nothing up.
 * ============================================================================== */
static void ebld_support_build(EbldModel& m)
{
    EbldSupport& G = m.sup;
    G.on = false;
    G.tri.clear(); G.start.clear(); G.list.clear();
    if (!(m.flags & EBX_REST) || m.nset < 2) return;
    const EbldSet& S = m.set[EBS_CUT];
    float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
    for (size_t k = 0; k < S.piece.size(); k++) {
        const EbldPiece& P = S.piece[k];
        const EbldPX& X = m.px[k];
        if (!(P.flags & EBF_FIXED) || (P.flags & EBF_CHIP) || X.spin >= 0
            || (X.flags & (EBQ_LOOP | EBQ_BEND)) || m.liqOf[k] >= 0)
            continue;
        for (int t = P.tri0; t < P.tri0 + P.ntri; t++) {
            const EbldTri& T = S.tri[t];
            if (T.mode == MODE_XLU) continue;
            const float e1[3] = { T.v[1].x - T.v[0].x, T.v[1].y - T.v[0].y, T.v[1].z - T.v[0].z };
            const float e2[3] = { T.v[2].x - T.v[0].x, T.v[2].y - T.v[0].y, T.v[2].z - T.v[0].z };
            float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                           e1[0] * e2[1] - e1[1] * e2[0] };
            const float L = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (L < 1e-6f) continue;
            for (int c = 0; c < 3; c++) n[c] /= L;
            if (n[1] < 0.3f) continue;
            EbldSupTri st;
            for (int q = 0; q < 3; q++) { st.x[q] = T.v[q].x; st.z[q] = T.v[q].z; }
            /* its plane as y = a x + b z + c */
            st.a = -n[0] / n[1];
            st.b = -n[2] / n[1];
            st.c = T.v[0].y - st.a * T.v[0].x - st.b * T.v[0].z;
            for (int c = 0; c < 3; c++) st.n[c] = n[c];
            G.tri.push_back(st);
            for (int q = 0; q < 3; q++) {
                if (st.x[q] < lo[0]) lo[0] = st.x[q];
                if (st.x[q] > hi[0]) hi[0] = st.x[q];
                if (st.z[q] < lo[1]) lo[1] = st.z[q];
                if (st.z[q] > hi[1]) hi[1] = st.z[q];
            }
        }
    }
    if (G.tri.empty()) return;
    G.cell = 128.0f;                          /* an eighth of a map cell */
    G.x0 = lo[0] - 1.0f; G.z0 = lo[1] - 1.0f;
    G.nx = (int)((hi[0] + 1.0f - G.x0) / G.cell) + 1;
    G.nz = (int)((hi[1] + 1.0f - G.z0) / G.cell) + 1;
    if (G.nx > 256) G.nx = 256;
    if (G.nz > 256) G.nz = 256;
    std::vector<std::vector<int> > cells((size_t)G.nx * G.nz);
    for (size_t t = 0; t < G.tri.size(); t++) {
        const EbldSupTri& st = G.tri[t];
        float mnx = st.x[0], mxx = st.x[0], mnz = st.z[0], mxz = st.z[0];
        for (int q = 1; q < 3; q++) {
            if (st.x[q] < mnx) mnx = st.x[q];
            if (st.x[q] > mxx) mxx = st.x[q];
            if (st.z[q] < mnz) mnz = st.z[q];
            if (st.z[q] > mxz) mxz = st.z[q];
        }
        int i0 = (int)((mnx - G.x0) / G.cell), i1 = (int)((mxx - G.x0) / G.cell);
        int j0 = (int)((mnz - G.z0) / G.cell), j1 = (int)((mxz - G.z0) / G.cell);
        if (i0 < 0) i0 = 0;
        if (j0 < 0) j0 = 0;
        if (i1 >= G.nx) i1 = G.nx - 1;
        if (j1 >= G.nz) j1 = G.nz - 1;
        for (int j = j0; j <= j1; j++)
            for (int i = i0; i <= i1; i++) cells[(size_t)j * G.nx + i].push_back((int)t);
    }
    G.start.assign(cells.size() + 1, 0);
    for (size_t i = 0; i < cells.size(); i++) {
        G.start[i + 1] = G.start[i] + (int)cells[i].size();
        G.list.insert(G.list.end(), cells[i].begin(), cells[i].end());
    }
    G.on = true;
}
/* The highest standing face at or below yMax over the point (pack units), and its normal;
   false when there is none. */
static bool ebld_support_at(const EbldSupport& G, float x, float z, float yMax, float* y,
                            float* n)
{
    if (!G.on) return false;
    const int i = (int)floorf((x - G.x0) / G.cell), j = (int)floorf((z - G.z0) / G.cell);
    if (i < 0 || j < 0 || i >= G.nx || j >= G.nz) return false;
    const size_t c = (size_t)j * G.nx + i;
    bool hit = false;
    float best = -1e30f;
    for (int k = G.start[c]; k < G.start[c + 1]; k++) {
        const EbldSupTri& st = G.tri[G.list[k]];
        /* inside the triangle as seen from above, either winding, a hair of slack */
        float s[3];
        for (int q = 0; q < 3; q++) {
            const int r = (q + 1) % 3;
            s[q] = (st.x[r] - st.x[q]) * (z - st.z[q]) - (st.z[r] - st.z[q]) * (x - st.x[q]);
        }
        const float eps = 1e-3f * (fabsf(s[0]) + fabsf(s[1]) + fabsf(s[2]));
        const bool in = (s[0] >= -eps && s[1] >= -eps && s[2] >= -eps)
                     || (s[0] <= eps && s[1] <= eps && s[2] <= eps);
        if (!in) continue;
        const float yy = st.a * x + st.b * z + st.c;
        if (yy > yMax || yy <= best) continue;
        best = yy;
        n[0] = st.n[0]; n[1] = st.n[1]; n[2] = st.n[2];
        hit = true;
    }
    if (hit) *y = best;
    return hit;
}

/* THE TEXTURES AND THE SETS, the part of the file the remastered vehicles' file
   (evhc_mod.h) shares byte for byte. `setOf` maps the file's set order onto EBS_* (NULL:
   the file's own order); `maxRole` is the highest texture role the caller's format knows.
   `recVer` is the version of the piece and triangle records when it is not `ver` (0: the
   same): the vehicles' version 2 keeps version 3's textures and takes version 5's pieces
   and triangles, the smooth normals with them. */
static void ebld_read_body(EbldIn& in, const std::vector<unsigned char>& b, EbldModel& m,
                           uint32_t ver, uint32_t ntex, uint32_t nset, int maxRole,
                           const int* setOf, uint32_t recVer = 0)
{
    const uint32_t rv = recVer ? recVer : ver;
    char msg[200];
    m.tex.resize(in.bad ? 0 : ntex);
    for (uint32_t t = 0; t < ntex && !in.bad; t++) {
        EbldTex& T = m.tex[t];
        T.w = in.u16(); T.h = in.u16();
        T.grid = in.u8(); T.frames = in.u8(); T.fps = in.u8(); T.role = in.u8();
        int house = 0, fpsdiv = 1;
        if (ver >= 2) {
            unsigned char pad[3];
            house = in.u8();
            in.take(pad, 3);
            /* version 3's divisor; version 2 wrote 0 there, read as 1 */
            if (ver >= 3 && pad[0] > 1) fpsdiv = pad[0];
            /* version 5's texture flags: bit0 the alpha bleed is done; version 6's bit1,
               the coverage-held mip chain */
            T.bled = (ver >= 5 && (pad[1] & 1));
            T.cover = (ver >= 6 && (pad[1] & 2));
            if (ver >= 5 && (pad[1] & ~(ver >= 6 ? 3 : 1))) { in.fail("a texture has flags this build does not know"); break; }
        } else {
            T.bled = false;
            T.cover = false;
        }
        T.fpsf = (float)T.fps / (float)fpsdiv;
        if (in.bad) break;
        /* bit0 a Nod decode follows, bit1 a mask follows; a mask alone would give the
           other colours with no Nod look to stand beside them, so it is refused. Only the
           base (0), damaged (2) and overlay (6) textures carry house paint. */
        if (house > 3 || house == 2
            || (house && T.role != 0 && T.role != 2 && T.role != 6)) {
            snprintf(msg, sizeof msg, "texture %u has an impossible house byte %d", t, house);
            in.fail(msg);
            break;
        }
        /* VERSION 5 takes the hand-painted method's 2048 atlases; 1024 before */
        const int maxw = ver >= 5 ? 2048 : 1024;
        if (T.w == 0 || T.h == 0 || T.w > maxw || T.h > maxw
            || (T.w & (T.w - 1)) || (T.h & (T.h - 1))) {
            snprintf(msg, sizeof msg, "texture %u is %dx%d, not a power of two up to %d",
                     t, T.w, T.h, maxw);
            in.fail(msg);
            break;
        }
        if (T.grid < 1 || T.frames < 1 || T.frames > T.grid * T.grid
            || T.role > maxRole) {
            snprintf(msg, sizeof msg, "texture %u has an impossible flipbook or role", t);
            in.fail(msg);
            break;
        }
        const size_t sz = (size_t)T.w * T.h * 4;
        T.house = house;
        T.csize = 0;
        if (ver >= 5) {
            /* one zlib stream: the pixels, the Nod decode, the mask */
            const uint32_t cs = in.u32();
            if (in.bad) break;
            if (cs == 0 || in.o + cs > b.size()) {
                in.fail("a texture's stream runs past the end of the file");
                break;
            }
            T.off = (long)in.o;
            T.csize = (long)cs;
            in.o += cs;
            continue;
        }
        if (in.o + sz > b.size()) { in.fail("a texture runs past the end of the file"); break; }
        T.off = (long)in.o;
        in.o += sz;
        if (house & 1) {
            if (in.o + sz > b.size()) { in.fail("a Nod decode runs past the end of the file"); break; }
            in.o += sz;
        }
        if (house & 2) {
            const size_t msz = (size_t)T.w * T.h;
            if (in.o + msz > b.size()) { in.fail("a house mask runs past the end of the file"); break; }
            in.o += msz;
        }
    }

    for (int s = 0; s < EBS_N; s++) {
        m.set[s].piece.clear();
        m.set[s].tri.clear();
    }
    for (int fs = 0; fs < (int)nset && !in.bad; fs++) {
        const int s = setOf ? setOf[fs] : fs;
        EbldSet& S = m.set[s];
        const uint32_t np = in.u32(), nt = in.u32();
        if (in.bad) break;
        if (np == 0 || np > 4096 || nt == 0 || nt > 200000) {
            snprintf(msg, sizeof msg, "set %d has %u pieces and %u triangles", s, np, nt);
            in.fail(msg);
            break;
        }
        if (!in.available(np, ver >= 5 ? 76 : 60)) break;
        S.piece.resize(np);
        uint32_t run = 0;
        for (uint32_t k = 0; k < np && !in.bad; k++) {
            EbldPiece& P = S.piece[k];
            memset(P.name, 0, sizeof P.name);
            in.take(P.name, rv >= 5 ? 32 : 16);
            P.parent = in.i16();
            P.zone = in.u8();
            P.flags = in.u8();
            for (int c = 0; c < 3; c++) P.c[c] = in.f32();
            for (int c = 0; c < 3; c++) P.push[c] = in.f32();
            P.size = in.f32();
            const uint32_t t0 = in.u32(), tn = in.u32();
            P.nhull = in.u8();
            unsigned char pad[3];
            in.take(pad, 3);
            if (in.bad) break;
            /* THE RUNS TILE THE SET: each piece's triangles start where the last piece's
               ended. That is what lets a piece be drawn as one draw_mesh range. */
            if (t0 != run || tn > nt || t0 + tn > nt) {
                snprintf(msg, sizeof msg, "set %d piece %u (%s) does not continue the "
                         "triangle list", s, k, P.name);
                in.fail(msg);
                break;
            }
            run = t0 + tn;
            P.tri0 = (int)t0; P.ntri = (int)tn;
            if (P.nhull > 16) { in.fail("a piece has more than sixteen hull points"); break; }
            for (int hI = 0; hI < P.nhull; hI++)
                for (int c = 0; c < 3; c++) P.hull[hI][c] = in.f32();
            if (in.bad) break;
            const bool chip = (P.flags & EBF_CHIP) != 0;
            if ((P.flags & ~63) || (chip && (s != EBS_CUT || P.zone > 7))
                || (!chip && P.zone != 255 && P.zone > 7)) {
                snprintf(msg, sizeof msg, "set %d piece %u (%s) has bad flags or zone",
                         s, k, P.name);
                in.fail(msg);
                break;
            }
            if (s == EBS_DESTROYED ? (P.parent < -1 || P.parent >= 4096) : (P.parent != -1)) {
                snprintf(msg, sizeof msg, "set %d piece %u (%s) has a bad parent", s, k, P.name);
                in.fail(msg);
                break;
            }
            if (P.size < 0.0f) { in.fail("a piece has a negative size"); break; }
        }
        if (in.bad) break;
        if (run != nt) {
            snprintf(msg, sizeof msg, "set %d's pieces cover %u of its %u triangles", s, run, nt);
            in.fail(msg);
            break;
        }
        if (!in.available(nt, ver >= 5 ? 144 : 108)) break;
        S.tri.resize(nt);
        for (uint32_t t = 0; t < nt && !in.bad; t++) {
            EbldTri& T = S.tri[t];
            T.tex = in.i16(); T.texDmg = in.i16(); T.texGlow = in.i16(); T.texOver = in.i16();
            T.mode = in.u8(); T.kind = in.u8(); T.wrap = in.u8();
            T.tflags = in.u8();
            if (rv < 4) T.tflags = 0;         /* padding before version 4 */
            for (int k = 0; k < 3 && !in.bad; k++) {
                EbldVert& V = T.v[k];
                V.x = in.f32(); V.y = in.f32(); V.z = in.f32();
                V.u = in.f32(); V.v = in.f32(); V.ud = in.f32(); V.vd = in.f32();
                V.r = in.u8(); V.g = in.u8(); V.b = in.u8(); V.a = in.u8();
                if (rv >= 5) { V.nx = in.f32(); V.ny = in.f32(); V.nz = in.f32(); }
            }
            if (in.bad) break;
            if (rv < 5) {
                /* no normals before version 5: each corner takes its face's */
                const float e1[3] = { T.v[1].x - T.v[0].x, T.v[1].y - T.v[0].y, T.v[1].z - T.v[0].z };
                const float e2[3] = { T.v[2].x - T.v[0].x, T.v[2].y - T.v[0].y, T.v[2].z - T.v[0].z };
                float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                               e1[0] * e2[1] - e1[1] * e2[0] };
                const float L = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (L > 1e-12f) { n[0] /= L; n[1] /= L; n[2] /= L; }
                else { n[0] = 0.0f; n[1] = 1.0f; n[2] = 0.0f; }
                for (int k = 0; k < 3; k++) { T.v[k].nx = n[0]; T.v[k].ny = n[1]; T.v[k].nz = n[2]; }
            }
            const int nt16 = (int)ntex;
            if (T.tex < -1 || T.tex >= nt16 || T.texDmg < -1 || T.texDmg >= nt16
                || T.texGlow < -1 || T.texGlow >= nt16 || T.texOver < -1 || T.texOver >= nt16
                || (T.mode != MODE_OPAQUE && T.mode != MODE_CUTOUT && T.mode != MODE_XLU)
                || T.kind > 2 || (T.wrap & 0xF0)
                || (T.tflags & ~(rv >= 5 ? (EBT_TWO | EBT_EMIT | EBT_WAVE) : EBT_TWO))) {
                snprintf(msg, sizeof msg, "set %d triangle %u has a bad texture, mode, kind "
                         "or wrap", s, t);
                in.fail(msg);
                break;
            }
        }
    }
    /* The DESTROYED pieces' parents index the CUT set, which is only complete now. */
    if (!in.bad && nset == EBS_N)
        for (size_t k = 0; k < m.set[EBS_DESTROYED].piece.size(); k++)
            if (m.set[EBS_DESTROYED].piece[k].parent >= (int)m.set[EBS_CUT].piece.size()) {
                in.fail("a destroyed piece names a cut piece that does not exist");
                break;
            }

}

/* What the file implies and the engine asks of it often, and the attachment reset: worked
   out once a model's records are read (or a wall's mask is split off), never from a draw. */
static void ebld_derive(EbldModel& m)
{
    ebld_derive_texture_masks(m);
    const int ncut = (int)m.set[EBS_CUT].piece.size();
    m.nmotion = 0;
    m.motion0.assign((size_t)ncut, 0);
    for (int k = 0; k < ncut; k++) {
        m.motion0[k] = m.nmotion;
        m.nmotion += (int)m.px[k].mo.size();
    }
    m.hasTwo = false;
    for (size_t t = 0; t < m.set[EBS_CUT].tri.size(); t++)
        if (m.set[EBS_CUT].tri[t].tflags & EBT_TWO) m.hasTwo = true;
    /* the slab's top, which the falling pieces rest on, and how many chips there are */
    m.nchip = 0;
    for (int s = 0; s < EBS_N; s++) {
        EbldSet& S = m.set[s];
        S.hasSlab = false;
        S.slabTop = 0.0f;
        for (size_t k = 0; k < S.piece.size(); k++) {
            const EbldPiece& P = S.piece[k];
            if (s == EBS_CUT && (P.flags & EBF_CHIP)) m.nchip++;
            if (!(P.flags & EBF_SLAB)) continue;
            float top = P.c[1];
            for (int hI = 0; hI < P.nhull; hI++)
                if (P.c[1] + P.hull[hI][1] > top) top = P.c[1] + P.hull[hI][1];
            if (!S.hasSlab || top > S.slabTop) S.slabTop = top;
            S.hasSlab = true;
        }
    }
    ebld_support_build(m);
    m.moveOf.assign(m.set[EBS_INTACT].piece.size(), -1);
    for (size_t k = 0; k < m.move.size(); k++) m.moveOf[m.move[k].piece] = (int)k;
    m.emitState[0] = m.emitState[1] = m.emitState[2] = false;
    for (size_t e = 0; e < m.emit.size(); e++) m.emitState[m.emit[e].state % 3] = true;
    m.attached = false;
    m.tex0 = -1;
    for (int i = 0; i < EBM_N; i++) m.mesh[i] = -1;
    m.shadow0 = m.shadow1 = 0;
    m.drawn = 0;
    m.resident = m.residentAll = m.cutBuilt = false;
    /* which textures the whole model draws with: the rest wait for the first hit */
    m.texIntact.assign(m.tex.size(), m.nset == 1 ? 1 : 0);
    for (size_t t = 0; t < m.set[EBS_INTACT].tri.size(); t++) {
        const int x = m.set[EBS_INTACT].tri[t].tex;
        if (x >= 0 && x < (int)m.tex.size()) m.texIntact[x] = 1;
    }
    /* THE WAVED TRIANGLES at rest, in the model frame the wave is written in */
    for (int q = 0; q < 2; q++) {
        const EbldSet& S = m.set[q ? EBS_CUT : EBS_INTACT];
        m.waveOf[q].assign(S.tri.size(), -1);
        m.waveT[q].clear();
        m.waveCache[q].clear();
        if (m.wave.empty()) continue;
        for (size_t t = 0; t < S.tri.size(); t++) {
            const EbldTri& T = S.tri[t];
            if (!(T.tflags & EBT_WAVE)) continue;
            /* which wave's box holds it (the first; the baker marked it for one) */
            int wi = -1;
            for (size_t w = 0; w < m.wave.size() && wi < 0; w++) {
                const float* a = m.wave[w].a;
                bool in = true;
                for (int k = 0; k < 3 && in; k++) {
                    const float X = T.v[k].x / 1024.0f, Y = -T.v[k].z / 1024.0f, Z = T.v[k].y / 1024.0f;
                    const float sa = (X - a[0]) * a[2] + (Y - a[1]) * a[3] - a[6];
                    const float qa = (X - a[0]) * a[4] + (Y - a[1]) * a[5];
                    in = sa >= a[10] - 1e-4f && sa <= a[11] + 1e-4f && fabsf(qa) <= a[12] + 1e-4f
                      && Z >= a[13] - 1e-4f && Z <= a[14] + 1e-4f;
                }
                if (in) wi = (int)w;
            }
            if (wi < 0) wi = 0;
            const float* a = m.wave[wi].a;
            EbldWaveT W;
            W.w = wi;
            for (int k = 0; k < 3; k++) {
                const EbldVert& V = T.v[k];
                EbldWaveV& o = W.v[k];
                o.p[0] = V.x; o.p[1] = V.y; o.p[2] = V.z;
                o.n[0] = V.nx; o.n[1] = V.ny; o.n[2] = V.nz;
                const float X = V.x / 1024.0f, Y = -V.z / 1024.0f;
                o.s = (X - a[0]) * a[2] + (Y - a[1]) * a[3] - a[6];
                o.z = V.y / 1024.0f;
                o.c[0] = V.r; o.c[1] = V.g; o.c[2] = V.b;
                /* across the cloth in the pack frame is (across x, 0, -across y) */
                const float d = V.nx * a[4] - V.nz * a[5];
                o.side = d > 0.5f ? 1 : d < -0.5f ? -1 : 0;
            }
            m.waveOf[q][t] = (int)m.waveT[q].size();
            m.waveT[q].push_back(W);
        }
        if (mesh_perf_switch("OPENCNC3D_EBLD_WAVE_CACHE")) {
            try {
                if (!mesh_perf_switch("OPENCNC3D_EBLD_WAVE_CACHE_ALLOC_OK")) throw std::bad_alloc();
                m.waveCache[q].resize(std::min(m.waveT[q].size(), (size_t)2048));
            } catch (const std::bad_alloc&) {
                m.waveCache[q].clear();
            }
        }
    }
    for (int i = 0; i < EBM_N; i++) { m.waveMesh[i].clear(); m.wavePosed[i] = -1.0e30f; }
}

/* (Red Alert's driven clips, ebld_ra_drive_*, far below) */
static void ebld_ra_drive_prepare(EbldModel& m);
static bool ebld_read_one(const char* path, EbldModel& m, size_t maxSourceBytes = 0)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    std::vector<unsigned char> b;
    try {
        unsigned char chunk[65536];
        size_t n;
        while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) {
            if (maxSourceBytes && (b.size() > maxSourceBytes || n > maxSourceBytes - b.size())) {
                fclose(f);
                return false;
            }
            b.insert(b.end(), chunk, chunk + n);
        }
    } catch (...) {
        fclose(f);
        throw;
    }
    fclose(f);
    EbldIn in;
    in.b = &b; in.o = 0; in.bad = false; in.why[0] = 0;
    in.bounded = maxSourceBytes != 0;
    char msg[200];
    char magic[4] = { 0, 0, 0, 0 }, type[9];
    in.take(magic, 4);
    if (!in.bad && memcmp(magic, "EBLD", 4) != 0) in.fail("it is not an EBLD file");
    /* VERSION 2 adds house paint to a texture (a house byte in its header, then a Nod
       decode and a mask after its pixels). A version 1 file is still read: its textures
       simply carry none, and the building wears the GDI look for every house.
       VERSION 3 adds a building with the INTACT set alone (nset 1), a flipbook rate that
       is not a whole number (a divisor byte in the texture header), the animated-texture
       role, and a tail after the emitters: the moving parts and the wall flag. A version
       2 file reads exactly as it did.
       VERSION 4 adds a building with INTACT and CUT and no DESTROYED set (nset 2), a flag
       byte per triangle, the emitters' spark fields and gates, and after the moving parts
       a record per CUT piece (its turn, shove, bend and looping motions) and the liquids.
       VERSION 5 adds textures up to 2048 a side stored as zlib streams, piece names up to
       31 characters, a smooth normal per corner, a triangle's SELF-LIT and WAVE flags in
       every set, the destroyed state's emitters, the waves (the Barracks' flag), and a
       wall's damaged and destroyed stages per connection mask.
       VERSION 6 adds an emitter's sync to the idle clock, a moving part of kind CLIP and
       the clips after the wall ranges (per-frame part animation: the Oil Pump's nodding,
       the Blossom Trees' idle loop), a drain with no floor (SHIFT), and the death's centre
       as two more parameters. */
    const uint32_t ver = in.u32();
    if (!in.bad && (ver < 1 || ver > 6)) {
        snprintf(msg, sizeof msg, "it is format version %u and this build reads 1 to 6",
                 ver);
        in.fail(msg);
    }
    m.version = (int)ver;
    memset(type, 0, sizeof type);
    in.take(type, 8);
    type[8] = 0;
    const uint32_t ntex = in.u32(), nset = in.u32(), nemit = in.u32(), nparam = in.u32();
    if (!in.bad && (ntex == 0 || ntex > 64)) in.fail("its texture count is out of range");
    if (!in.bad && nset != EBS_N && !(ver >= 3 && nset == 1) && !(ver >= 4 && nset == 2))
        in.fail("it carries neither three sets, nor the intact and cut sets, nor the intact "
                "set alone");
    m.nset = (int)nset;
    if (!in.bad && maxSourceBytes && nset != 1) in.fail("bounded preparation requires an intact-only source");
    /* (a staged wall carries its sixteen masks' emitters: version 5 allows more) */
    if (!in.bad && nemit > (ver >= 5 ? 1024u : 64u)) in.fail("its emitter count is out of range");
    if (!in.bad && (nparam < EBP_N || nparam > 256))
        in.fail("its parameter table is too short or too long");
    m.type = type;
    m.param.clear();
    for (uint32_t i = 0; i < nparam && !in.bad; i++) m.param.push_back(in.f32());
    /* (a file older than version 6 has no blast centre: the footprint's centre) */
    if (m.param.size() < (size_t)EBP_N6) m.param.resize((size_t)EBP_N6, 0.0f);
    m.sea = m.param.size() >= (size_t)EBP_SEA_N;

    ebld_read_body(in, b, m, ver, ntex, nset, ver >= 3 ? 9 : 8, NULL);

    m.emit.resize(in.bad ? 0 : nemit);
    for (uint32_t e = 0; e < nemit && !in.bad; e++) {
        EbldEmit& E = m.emit[e];
        E.state = in.u8(); E.src = in.u8(); E.piece = in.i16();
        const uint32_t npt = in.u32();
        if (in.bad) break;
        if (E.state > (ver >= 5 ? 2 : 1) || E.src > (ver >= 4 ? 6u : 3u) || E.piece < -1
            || (nset == 1 && E.piece >= 0) || (E.state == 2 && E.piece >= 0)
            || E.piece >= (int)m.set[EBS_CUT].piece.size() || npt == 0 || npt > 65536) {
            snprintf(msg, sizeof msg, "emitter %u has a bad state, source, piece or point "
                     "count", e);
            in.fail(msg);
            break;
        }
        E.npoint = (int)npt;
        if (!in.available(npt, 16)) break;
        E.pt.resize((size_t)npt * 4);
        E.wsum = 0.0f;
        for (uint32_t p = 0; p < npt * 4 && !in.bad; p++) E.pt[p] = in.f32();
        for (uint32_t p = 0; p < npt; p++) {
            if (E.pt[p * 4 + 3] < 0.0f) E.pt[p * 4 + 3] = 0.0f;
            E.wsum += E.pt[p * 4 + 3];
        }
        E.rate = in.f32();
        E.life[0] = in.f32(); E.life[1] = in.f32();
        E.size[0] = in.f32(); E.size[1] = in.f32();
        for (int c = 0; c < 3; c++) E.vel[c] = in.f32();
        for (int c = 0; c < 3; c++) E.dir[c] = in.f32();
        E.speed = in.f32(); E.jitter = in.f32(); E.drag = in.f32(); E.rise = in.f32();
        in.take(E.rgba, 4);
        E.fade = in.f32(); E.delay = in.f32(); E.ramp = in.f32(); E.disc = in.f32();
        if (!in.bad && (E.rate < 0.0f || E.life[0] <= 0.0f || E.life[1] < E.life[0])) {
            snprintf(msg, sizeof msg, "emitter %u has a bad rate or life", e);
            in.fail(msg);
        }
        /* VERSION 4: the spark fields and the gates, 72 bytes after the disc */
        E.eflags = 0;
        E.burstEvery[0] = E.burstEvery[1] = E.burstCount[0] = E.burstCount[1] = 0.0f;
        E.gravity = E.until = E.untilFade = 0.0f;
        E.onPiece = -1;
        E.onAbove = E.onCount[0] = E.onCount[1] = E.fill = 0.0f;
        for (int c = 0; c < 5; c++) E.light[c] = 0.0f;
        if (ver >= 4 && !in.bad) {
            E.eflags = (int)in.u32();
            E.burstEvery[0] = in.f32(); E.burstEvery[1] = in.f32();
            E.burstCount[0] = in.f32(); E.burstCount[1] = in.f32();
            E.gravity = in.f32();
            E.until = in.f32(); E.untilFade = in.f32();
            E.onPiece = in.i16();
            (void)in.u16();
            E.onAbove = in.f32(); E.onCount[0] = in.f32(); E.onCount[1] = in.f32();
            E.fill = in.f32();
            for (int c = 0; c < 5; c++) E.light[c] = in.f32();
            if (!in.bad && ((E.eflags & ~(ver >= 6 ? 127 : 63))
                            || ((E.eflags & EBE_BURST) && (E.burstEvery[0] < 0.0f
                                || E.burstEvery[1] < E.burstEvery[0]
                                || E.burstCount[1] < E.burstCount[0]))
                            || ((E.eflags & EBE_BURSTON) && (E.onPiece < 0
                                || E.onPiece >= (int)m.set[EBS_CUT].piece.size()))
                            || (!(E.eflags & EBE_BURSTON) && E.onPiece != -1))) {
                snprintf(msg, sizeof msg, "emitter %u has impossible spark fields or gates", e);
                in.fail(msg);
            }
        }
        /* VERSION 6: the sync, 16 bytes after the gates */
        E.syncAt = E.syncPeriod = E.syncCount[0] = E.syncCount[1] = 0.0f;
        if (ver >= 6 && !in.bad) {
            E.syncAt = in.f32(); E.syncPeriod = in.f32();
            E.syncCount[0] = in.f32(); E.syncCount[1] = in.f32();
            if (!in.bad && (E.eflags & EBE_SYNC)
                && (!(E.syncPeriod > 0.0f) || E.syncAt < 0.0f || E.syncCount[1] < E.syncCount[0])) {
                snprintf(msg, sizeof msg, "emitter %u has an impossible sync", e);
                in.fail(msg);
            }
        }
    }
    /* THE VERSION 3 TAIL: the flags, and the moving parts in INTACT piece order */
    m.flags = 0;
    m.move.clear();
    if (!in.bad && ver >= 3) {
        m.flags = (int)in.u32();
        const uint32_t nmove = in.u32();
        const int nip = (int)m.set[EBS_INTACT].piece.size();
        const int known = (ver >= 4) ? (EBX_WALL | EBX_REST | EBX_SCORCHCELL) : EBX_WALL;
        /* (version 5 gives a staged wall the REST flag: its pieces land on its base) */
        if (!in.bad && (m.flags & ~known)) in.fail("it has flags this build does not know");
        if (!in.bad && nmove > (uint32_t)nip) in.fail("it has more moving parts than pieces");
        int last = -1;
        for (uint32_t k = 0; k < nmove && !in.bad; k++) {
            EbldMove mv;
            mv.piece = in.u16();
            mv.kind = in.u8();
            (void)in.u8();
            for (int c = 0; c < 9; c++) mv.a[c] = in.f32();
            if (in.bad) break;
            if (mv.piece <= last || mv.piece >= nip || mv.kind > (ver >= 6 ? EBV_CLIP : EBV_FILL)
                || (mv.kind == EBV_SPIN && mv.a[6] <= 0.0f)
                || (mv.kind == EBV_FILL && !(mv.a[0] < mv.a[1]))) {
                snprintf(msg, sizeof msg, "moving part %u is out of order or impossible", k);
                in.fail(msg);
                break;
            }
            last = mv.piece;
            m.move.push_back(mv);
        }
        /* A WALL is exactly its sixteen connection pieces, nothing moving: the draw indexes
           the pieces by the cell's connection nibble. Version 5 may give it its stages. */
        if (!in.bad && (m.flags & EBX_WALL)
            && (nip != 16 || !m.move.empty()
                || !(nset == 1 ? nemit == 0 : (ver >= 5 && nset == EBS_N))))
            in.fail("a wall that is not sixteen still pieces");
    }
    /* THE VERSION 4 TAIL: a record per CUT piece, then the liquids. */
    const int ncut = (int)m.set[EBS_CUT].piece.size();
    m.px.clear();
    m.liquid.clear();
    if (!in.bad && ver >= 4) {
        const uint32_t npx = in.u32();
        if (!in.bad && (int)npx != ncut) in.fail("its piece table does not match its cut set");
        for (uint32_t k = 0; k < npx && !in.bad; k++) {
            EbldPX X;
            X.spin = in.i16();
            X.parent = in.i16();
            X.flags = in.u16();
            const int nmo = in.u16();
            for (int c = 0; c < 3; c++) X.v[c] = in.f32();
            for (int c = 0; c < 3; c++) X.w[c] = in.f32();
            X.ghost = in.f32();
            for (int c = 0; c < 3; c++) X.ball[c] = in.f32();
            for (int c = 0; c < 8; c++) X.bend[c] = in.f32();
            X.seed = in.f32();
            if (in.bad) break;
            if (X.spin < -1 || X.spin >= (int)m.move.size()
                || (X.spin >= 0 && m.move[X.spin].kind >= EBV_FILL)
                || X.parent < -1 || X.parent >= ncut || X.parent == (int)k
                || (X.flags & ~127) || nmo > 16 || X.ghost < 0.0f
                || ((X.flags & EBQ_BALL) && X.ball[0] <= 0.0f)
                || ((X.flags & EBQ_BEND) && X.bend[7] <= 0.0f)
                || (nmo > 0) != ((X.flags & EBQ_LOOP) != 0)) {
                snprintf(msg, sizeof msg, "cut piece %u's record is impossible", k);
                in.fail(msg);
                break;
            }
            for (int q = 0; q < nmo && !in.bad; q++) {
                EbldMotion mo;
                mo.type = in.u8();
                const int ntry = in.u8();
                (void)in.u16();
                for (int c = 0; c < 3; c++) mo.pivot[c] = in.f32();
                for (int c = 0; c < 3; c++) mo.axis[c] = in.f32();
                mo.rest = in.f32(); mo.settle = in.f32();
                mo.stroke[0] = in.f32(); mo.stroke[1] = in.f32();
                mo.period[0] = in.f32(); mo.period[1] = in.f32();
                mo.shudder[0] = in.f32(); mo.shudder[1] = in.f32();
                if (in.bad) break;
                if (mo.type > 1 || mo.settle <= 0.0f) {
                    snprintf(msg, sizeof msg, "cut piece %u has an impossible motion", k);
                    in.fail(msg);
                    break;
                }
                for (int tI = 0; tI < ntry && !in.bad; tI++) {
                    const int nkey = in.u16();
                    (void)in.u16();
                    if (in.bad || nkey < 1 || nkey > 64) {
                        in.fail("a motion's try has no keys or too many");
                        break;
                    }
                    std::vector<float> keys((size_t)nkey * 3);
                    for (int c = 0; c < nkey * 3; c++) keys[c] = in.f32();
                    mo.tries.push_back(keys);
                }
                X.mo.push_back(mo);
            }
            m.px.push_back(X);
        }
        const uint32_t nliq = in.u32();
        if (!in.bad && nliq > (uint32_t)ncut) in.fail("it has more liquids than pieces");
        for (uint32_t k = 0; k < nliq && !in.bad; k++) {
            EbldLiquid L;
            L.piece = in.u16();
            L.kind = in.u8();
            L.flags = in.u8();
            L.a = in.f32(); L.b = in.f32(); L.c = in.f32();
            L.seconds = in.f32(); L.speed = in.f32(); L.gate = in.f32(); L.rest = in.f32();
            if (in.bad) break;
            if (L.piece >= ncut || L.kind > EBL_FILL
                || (L.flags & ~(EBL_GATE | (ver >= 6 ? EBL_SHIFT : 0)))
                || (L.kind == EBL_DRAIN && !(m.set[EBS_CUT].piece[L.piece].flags & EBF_WATER))
                || (L.kind == EBL_FALL
                    && !(m.set[EBS_CUT].piece[L.piece].flags & EBF_WATERFALL))
                || (L.kind == EBL_FILL && !(L.a < L.b))) {
                snprintf(msg, sizeof msg, "liquid %u is impossible", k);
                in.fail(msg);
                break;
            }
            m.liquid.push_back(L);
        }
    }
    /* THE VERSION 5 TAIL: the waves, then a staged wall's ranges per connection mask */
    m.wave.clear();
    m.wallStaged = false;
    m.wallMask = m.texOwner = -1;
    for (int k = 0; k < 16; k++) {
        m.wallSub[k] = -1;
        for (int c = 0; c < 6; c++) m.wallRange[k][c] = 0;
    }
    if (!in.bad && ver >= 5) {
        const uint32_t nw = in.u32();
        if (!in.bad && nw > 4) in.fail("it has more than four waves");
        for (uint32_t w = 0; w < nw && !in.bad; w++) {
            EbldWave W;
            for (int c = 0; c < 33; c++) W.a[c] = in.f32();
            const uint32_t nm = in.u32();
            if (in.bad) break;
            if (nm < 1 || nm > 16 || !(W.a[7] > 0.0f) || !(W.a[16] > 0.0f)) {
                in.fail("a wave is impossible");
                break;
            }
            if (!in.available(nm, 28)) break;
            W.mode.resize((size_t)nm * 7);
            for (uint32_t c = 0; c < nm * 7; c++) W.mode[c] = in.f32();
            for (uint32_t q = 0; q < nm; q++)
                if (!(W.mode[q * 7 + 1] > 0.0f)) in.fail("a wave's mode has no wavelength");
            m.wave.push_back(W);
        }
        if (!in.bad && (m.flags & EBX_WALL) && nset == EBS_N) {
            const int nc = (int)m.set[EBS_CUT].piece.size();
            const int nd = (int)m.set[EBS_DESTROYED].piece.size();
            int cc = 0, dc = 0, ec = 0;
            for (int k = 0; k < 16 && !in.bad; k++) {
                for (int c = 0; c < 6; c++) m.wallRange[k][c] = in.u16();
                const int* r = m.wallRange[k];
                if (in.bad) break;
                if (r[0] != cc || r[2] != dc || r[4] != ec || r[1] < 1) {
                    in.fail("a wall's mask ranges do not tile its sets");
                    break;
                }
                cc += r[1]; dc += r[3]; ec += r[5];
                for (int d = r[2]; d < r[2] + r[3]; d++) {
                    const int par = m.set[EBS_DESTROYED].piece[d].parent;
                    if (par >= 0 && (par < r[0] || par >= r[0] + r[1]))
                        in.fail("a wall's shard breaks from another mask's piece");
                }
                for (int e = r[4]; e < r[4] + r[5]; e++) {
                    const int pc = m.emit[e].piece;
                    if (pc >= 0 && (pc < r[0] || pc >= r[0] + r[1]))
                        in.fail("a wall's emitter rides another mask's piece");
                }
            }
            if (!in.bad && (cc != nc || dc != nd || ec != (int)nemit))
                in.fail("a wall's mask ranges do not tile its sets");
            m.wallStaged = !in.bad;
        }
    }
    /* THE VERSION 6 TAIL: the clips. Every index is checked here so the draw can trust
       them: a CLIP move names an INTACT clip and one of its parts, a CUT clip's map names a
       CUT piece, one of its own parts and one of the INTACT clip's. */
    m.clip.clear();
    m.clipIntact = m.clipCut = -1;
    m.clipPart.assign(m.set[EBS_CUT].piece.size(), -1);
    m.clipIPart.assign(m.set[EBS_CUT].piece.size(), -1);
    if (!in.bad && ver >= 6) {
        const uint32_t nc = in.u32();
        if (!in.bad && nc > 8) in.fail("it has more than eight clips");
        for (uint32_t c = 0; c < nc && !in.bad; c++) {
            EbldClip C;
            C.set = in.u8();
            const int fl = in.u8();
            C.nparts = in.u16();
            C.frames = (int)in.u32();
            C.fps = in.f32();
            const uint32_t nseq = in.u32();
            if (in.bad) break;
            C.idle = (fl & 1) != 0;
            if (C.set > 1 || (fl & ~1) || (C.idle && C.set != 0) || C.nparts < 1
                || C.nparts > 256 || C.frames < 1 || C.frames > 20000 || !(C.fps > 0.0f)
                || nseq > 100000 || (size_t)C.frames * C.nparts * 12 > 8000000) {
                in.fail("a clip is impossible");
                break;
            }
            if (!in.available(nseq, 4)) break;
            C.seq.resize(nseq);
            for (uint32_t q = 0; q < nseq && !in.bad; q++) {
                C.seq[q] = (int)in.u32();
                if (C.seq[q] >= C.frames) in.fail("a clip's order names a frame it has not");
            }
            const size_t nm = (size_t)C.frames * C.nparts * 12;
            if (!in.available(nm, 4)) break;
            C.mat.resize(nm);
            for (size_t q = 0; q < nm && !in.bad; q++) C.mat[q] = in.f32();
            C.turn = 0;
            if (C.set == 1 && !in.bad) {
                if (m.clipIntact < 0 || m.clipCut >= 0 || m.nset < 2) {
                    in.fail("a cut clip without the intact clip, or a second one");
                    break;
                }
                C.turn = (int)in.u32();
                if (!in.available((size_t)C.frames, 4)) break;
                C.phase.resize((size_t)C.frames);
                for (int q = 0; q < C.frames && !in.bad; q++) C.phase[q] = in.f32();
                const uint32_t nmap = in.u32();
                if (!in.bad && (C.turn < 1 || nmap > (uint32_t)m.clipPart.size()))
                    in.fail("a cut clip's turn or map is impossible");
                for (uint32_t q = 0; q < nmap && !in.bad; q++) {
                    const int pc = in.u16(), part = in.u16(), ip = in.u16();
                    (void)in.u16();
                    if (in.bad) break;
                    if (pc >= (int)m.clipPart.size() || part >= C.nparts
                        || ip >= m.clip[m.clipIntact].nparts || m.clipPart[pc] >= 0) {
                        in.fail("a cut clip's map names a piece or part it has not");
                        break;
                    }
                    m.clipPart[pc] = part;
                    m.clipIPart[pc] = ip;
                }
                m.clipCut = (int)m.clip.size();
            } else if (!in.bad) {
                if (m.clipIntact >= 0) { in.fail("a second intact clip"); break; }
                m.clipIntact = (int)m.clip.size();
            }
            m.clip.push_back(C);
        }
        for (size_t k = 0; k < m.move.size() && !in.bad; k++) {
            const EbldMove& mv = m.move[k];
            if (mv.kind != EBV_CLIP) continue;
            if ((int)mv.a[0] != m.clipIntact || m.clipIntact < 0 || mv.a[1] < 0.0f
                || (int)mv.a[1] >= m.clip[m.clipIntact].nparts)
                in.fail("a moving part names a clip or part the file has not");
        }
    }
    /* (under Red Alert, a structure whose clip its engine's state plays: RA_DRIVE) */
    m.raDrive = -1;
    if (!in.bad && g_raModule) ebld_ra_drive_prepare(m);
    if (!in.bad) {
        const float st = m.param[EBP_SCORCH_TEX];
        const bool none = (st == -1.0f && ((m.flags & EBX_WALL) || m.nset == 1));
        if (!none && (st < 0.0f || (int)st >= (int)ntex || (float)(int)st != st))
            in.fail("the scorch texture index is out of range");
        else if (m.param[EBP_CHIP_STEP] <= 0.0f || m.param[EBP_CHIP_STEP] > 1.0f)
            in.fail("the chip step is not a fraction of strength");
    }
    /* THE EXPLOSION, an optional trailer in a file of any version (bake_ebld.py's layout):
       the lab's recipe, slot by slot. A file without it plays the engine's own FRAG3 table
       at the death's centre, which is the same recipe as the lab's today. */
    m.boom.clear();
    m.boomDur = m.boomUntil = 0;
    m.boomShake = 0.0f;
    if (!in.bad && in.o + 4 <= b.size() && !memcmp(&b[in.o], "BOOM", 4)) {
        in.o += 4;
        const uint32_t ns = in.u32(), dur = in.u32(), until = in.u32();
        in.f32();                             /* the lab's trail scale: the engine's own dial */
        const float cells = in.f32();         /* the lab's jolt, in footprint cells */
        if (!in.bad && !(cells > 0.0f && cells <= 8.0f)) in.fail("its explosion's jolt is impossible");
        m.boomShake = cells;
        if (!in.bad && (ns < 1 || ns > 64 || dur < 1 || until < dur || until > 4096))
            in.fail("its explosion's header is impossible");
        if (!in.bad && (m.flags & EBX_WALL)) in.fail("a wall carries an explosion");
        for (uint32_t i = 0; i < ns && !in.bad; i++) {
            EbldBoomSlot S;
            memset(&S, 0, sizeof S);
            in.take(S.name, 24);
            in.take(S.sheet, 12);
            S.name[23] = S.sheet[11] = 0;
            S.src.sys = in.u8();
            S.src.nvar = in.u8();
            S.stage = in.u8();
            S.flags = in.u8();
            S.src.burst = in.i16();
            S.hold = in.i16();
            S.every = in.i16();
            S.src.life = in.i16();
            S.src.lifeR = in.i16();
            S.dx = in.i16(); S.dy = in.i16(); S.dz = in.i16();
            S.src.ppt = in.f32(); S.src.mult = in.f32();
            S.src.spd = in.f32(); S.src.spdR = in.f32();
            S.src.size = in.f32(); S.src.sizeR = in.f32();
            S.src.aMin = in.f32(); S.src.aMax = in.f32();
            S.src.bMin = in.f32(); S.src.bMax = in.f32();
            S.src.yacc = in.f32(); S.src.grow = in.f32();
            S.src.a = in.u8(); S.src.r = in.u8(); S.src.g = in.u8(); S.src.b = in.u8();
            S.src.arate = in.u8();
            unsigned char pad[3];
            in.take(pad, 3);
            S.src.name = NULL;
            if (!in.bad && (!S.name[0] || S.src.sys >= 10 || S.src.nvar < 1 || S.src.nvar > 2
                            || (S.flags & ~(EBB_FLAT | EBB_REP)) || S.src.burst < 0
                            || S.hold < 0 || S.every < 1 || S.src.life < 0))
                in.fail("an explosion slot is impossible");
            m.boom.push_back(S);
        }
        m.boomDur = (int)dur;
        m.boomUntil = (int)until;
    }
    /* THE WIND, a tree's optional trailer after that (bake_ebld.py's layout) */
    m.sway.clear();
    if (!in.bad && in.o + 4 <= b.size() && !memcmp(&b[in.o], "SWAY", 4)) {
        in.o += 4;
        const uint32_t n = in.u32();
        if (!in.bad && n != m.set[EBS_INTACT].tri.size())
            in.fail("its wind weights do not cover its intact triangles");
        if (!in.bad) {
            if (!in.available(n, 6)) return false;
            m.sway.resize((size_t)n * 6);
            in.take(&m.sway[0], m.sway.size());
        }
    }
    if (!in.bad && in.o != b.size()) {
        snprintf(msg, sizeof msg, "%lu bytes follow the last emitter",
                 (unsigned long)(b.size() - in.o));
        in.fail(msg);
    }
    /* A FILE OLDER THAN VERSION 4 speaks of one water and one waterfall through its PARAM
       table, and its pieces do nothing but fall: the same records are made from that, so
       one code path plays every version. */
    if (!in.bad && ver < 4) {
        EbldPX X;
        X.spin = X.parent = -1;
        X.flags = 0;
        for (int c = 0; c < 3; c++) X.v[c] = X.w[c] = X.ball[c] = 0.0f;
        for (int c = 0; c < 8; c++) X.bend[c] = 0.0f;
        X.ghost = X.seed = 0.0f;
        m.px.assign((size_t)ncut, X);
        for (int k = 0; k < ncut; k++) {
            const int fl = m.set[EBS_CUT].piece[k].flags;
            if (!(fl & (EBF_WATER | EBF_WATERFALL))) continue;
            EbldLiquid L;
            L.piece = k;
            L.kind = (fl & EBF_WATER) ? EBL_DRAIN : EBL_FALL;
            L.flags = 0;
            L.a = m.param[EBP_WATER_LEVEL]; L.b = m.param[EBP_WATER_DROP];
            L.c = m.param[EBP_WATER_FLOOR];
            L.seconds = m.param[EBP_WATER_SECONDS];
            L.speed = m.param[EBP_FALL_SPEED];
            L.gate = L.rest = 0.0f;
            m.liquid.push_back(L);
        }
    }
    /* every draining or pouring piece has its record, and no piece has two */
    m.liqOf.assign((size_t)ncut, -1);
    for (size_t k = 0; k < m.liquid.size() && !in.bad; k++) {
        if (m.liqOf[m.liquid[k].piece] >= 0) in.fail("a piece has two liquids");
        m.liqOf[m.liquid[k].piece] = (int)k;
    }
    for (int k = 0; k < ncut && !in.bad; k++)
        if ((m.set[EBS_CUT].piece[k].flags & (EBF_WATER | EBF_WATERFALL)) && m.liqOf[k] < 0)
            in.fail("a draining or pouring piece has no liquid record");
    if (in.bad) {
        fprintf(stderr, "ebld: %s refused: %s\n", path, in.why);
        return false;
    }
    m.path = path;
    m.fileSize = (long)b.size();
    ebld_derive(m);
    return true;
}

/* A STAGED WALL'S MASK AS A MODEL OF ITS OWN: the piece the intact wall draws for that
   connection mask, and the mask's own cut, wreck, emitters and piece records, every index
   re-based to the mask's ranges. It shares the wall's file and textures (texOwner); a
   damaged or destroyed wall cell is a small building played by it. */
static void ebld_wall_split(const EbldModel& W, int owner, int k, EbldModel& S)
{
    const int* r = W.wallRange[k];
    const int c0 = r[0], cn = r[1], d0 = r[2], dn = r[3], e0 = r[4], en = r[5];
    S.type = W.type;
    S.tag = W.tag;
    S.path = W.path;
    S.fileSize = W.fileSize;
    S.version = W.version;
    S.nset = EBS_N;
    S.flags = W.flags & ~EBX_WALL;
    S.param = W.param;
    S.tex = W.tex;
    /* the sets: the wall's piece k alone, then the mask's cut and wreck */
    {
        const EbldSet& I = W.set[EBS_INTACT];
        EbldSet& O = S.set[EBS_INTACT];
        O.piece.assign(1, I.piece[k]);
        O.piece[0].tri0 = 0;
        O.tri.assign(I.tri.begin() + I.piece[k].tri0,
                     I.tri.begin() + I.piece[k].tri0 + I.piece[k].ntri);
    }
    const int lo[2] = { c0, d0 }, n[2] = { cn, dn };
    for (int q = 0; q < 2; q++) {
        const int s = q ? EBS_DESTROYED : EBS_CUT;
        const EbldSet& I = W.set[s];
        EbldSet& O = S.set[s];
        O.piece.assign(I.piece.begin() + lo[q], I.piece.begin() + lo[q] + n[q]);
        O.tri.clear();
        for (size_t i = 0; i < O.piece.size(); i++) {
            EbldPiece& P = O.piece[i];
            const int t0 = P.tri0;
            P.tri0 = (int)O.tri.size();
            O.tri.insert(O.tri.end(), I.tri.begin() + t0, I.tri.begin() + t0 + P.ntri);
            if (s == EBS_DESTROYED && P.parent >= 0) P.parent -= c0;
        }
    }
    S.emit.assign(W.emit.begin() + e0, W.emit.begin() + e0 + en);
    for (size_t e = 0; e < S.emit.size(); e++) {
        if (S.emit[e].piece >= 0) S.emit[e].piece -= c0;
        if (S.emit[e].onPiece >= 0) S.emit[e].onPiece -= c0;
    }
    S.move.clear();
    S.px.assign(W.px.begin() + c0, W.px.begin() + c0 + cn);
    for (size_t i = 0; i < S.px.size(); i++) {
        if (S.px[i].parent >= 0) S.px[i].parent -= c0;
        S.px[i].spin = -1;
    }
    S.liquid.clear();
    for (size_t i = 0; i < W.liquid.size(); i++)
        if (W.liquid[i].piece >= c0 && W.liquid[i].piece < c0 + cn) {
            EbldLiquid L = W.liquid[i];
            L.piece -= c0;
            S.liquid.push_back(L);
        }
    S.liqOf.assign((size_t)cn, -1);
    for (size_t i = 0; i < S.liquid.size(); i++) S.liqOf[S.liquid[i].piece] = (int)i;
    S.wave.clear();
    S.clip.clear();
    S.clipIntact = S.clipCut = -1;
    S.clipPart.assign((size_t)cn, -1);
    S.clipIPart.assign((size_t)cn, -1);
    S.theater = W.theater;
    S.wallStaged = false;
    S.wallMask = k;
    S.texOwner = owner;
    for (int q = 0; q < 16; q++) S.wallSub[q] = -1;
    ebld_derive(S);
}

/* THE WALLS WITH A REMASTER, in the order of the wall overlay kinds the dump reports
   (WallCell::kind: 0 SBAG, 1 CYCL, 2 BRIK, 3 BARB, 4 WOOD: the barbed wire and the wooden
   fence are walls like the other three, never built, standing in a mission's map; 5 FENC,
   Red Alert's own barbed-wire fence, which only its files carry and only its map reports). */
enum { EBLD_NWALL = 6 };
static const char* const EBLD_WALLS[EBLD_NWALL] = { "SBAG", "CYCL", "BRIK", "BARB", "WOOD", "FENC" };
static int ebld_wall_kind(const char* type)
{
    for (int k = 0; k < EBLD_NWALL; k++)
        if (!strcmp(type, EBLD_WALLS[k])) return k;
    return -1;
}
/* A wall cell's entry key: far above every object's kind * 1000000 + id. */
static const long long EBLD_WALL_KEY0 = 9000000000LL;
static long long ebld_wall_key(int x, int y) { return EBLD_WALL_KEY0 + (long long)y * 512 + x; }
static bool ebld_meshes_ok(const EbldModel& M);
/* Which model draws each wall kind in the pack loaded now (-1 none), resolved once per
   attach. */
static bool ebld_ensure(const char* type, size_t maxSourceBytes = 0);
static void ebld_wall_models(void)
{
    /* the barbed wire and the wooden fence are read at first sight: the first time a cell
       of theirs is asked for (ebld_wall_step, ebld_wall_draw) */
    if (g_ebldWallPack == g_ebldPackSerial) return;
    for (int k = 0; k < EBLD_NWALL; k++) g_ebldWallModel[k] = -1;
    for (size_t i = 0; i < g_ebld.size(); i++) {
        const EbldModel& M = g_ebld[i];
        const int wk = ebld_wall_kind(M.type.c_str());
        if (wk >= 0 && (M.flags & EBX_WALL) && M.wallMask < 0 && ebld_meshes_ok(M))
            g_ebldWallModel[wk] = (int)i;
    }
    g_ebldWallPack = g_ebldPackSerial;
}

/* THE TYPES WITH A REMASTER that the loader looks for: every structure the engine knows,
   the civilian buildings (V01 to V37: houses, farms, churches, the fields and haystacks in
   their structure form, the oil pump, the desert town), and the five walls. Each name is
   also tried with a theater's suffix (EBLD_THEATERS): a type whose look differs per
   theater has a file per theater, and the mission's own is drawn. */
static const char* const EBLD_TYPES[] = {
    "AFLD", "ATWR", "BIO", "EYE", "FACT", "FIX", "GTWR", "GUN", "HAND", "HOSP",
    "HPAD", "HQ", "MISS", "NUK2", "NUKE", "OBLI", "PROC", "PYLE", "SAM", "SILO",
    "TMPL", "WEAP", "ARCO",
    /* Red Alert files live in ra3d/ebld, independent of the C&C names above. */
    "POWR", "APWR", "BARR", "TENT", "DOME", "ATEK", "STEK", "PBOX", "HBOX",
    "KENN", "GAP", "IRON", "PDOX", "MSLO", "FCOM", "SYRD", "SPEN", "TSLA", "FTUR", "AGUN",
    "V01", "V02", "V03", "V04", "V05", "V06", "V07", "V08", "V09", "V10", "V11", "V12",
    "V13", "V14", "V15", "V16", "V17", "V18", "V19", "V20", "V21", "V22", "V23", "V24",
    "V25", "V26", "V27", "V28", "V29", "V30", "V31", "V32", "V33", "V34", "V35", "V36",
    "V37",
    "SBAG", "CYCL", "BRIK", "BARB", "WOOD", "FENC",
    /* RED ALERT'S PROPS (ra3d_props_mod.h), in ra3d/ebld only: the explosive barrels and the
       mines (one-cell buildings), the interior's crate stacks and the ice floes (terrain
       objects), the water crate and the flag location (cell decorations) */
    "BARL", "BRL3", "MINV", "MINP",
    "BOXES01", "BOXES02", "BOXES03", "BOXES04", "BOXES05", "BOXES06", "BOXES07", "BOXES08",
    "BOXES09", "ICE01", "ICE02", "ICE03", "ICE04", "ICE05", "WWCRATE", "FPLS",
    /* the terrain objects (rocks, blossom trees) and the cell decorations ebld_terr.h
       draws (the crashed Orca on rock patch P04, the bonus crates; the field overlays draw
       V13 to V18 above) */
    "ROCK1", "ROCK2", "ROCK3", "ROCK4", "ROCK5", "ROCK6", "ROCK7", "SPLIT2", "SPLIT3",
    "P04", "SCRATE", "WCRATE",
    /* the remastered trees (tools/art/remaster/foliage), each file its theater's look: the
       temperate conifers T01, T02, T05, T06, T07 and T16 (its own spruce; drawn from T01's
       file where the play folder has no T16 file, EBLD_ALIAS); the desert cacti T04 and
       T09; the temperate broadleaf trees and saplings T03, T10 to T15, T17; T08 (its
       temperate look, a young conifer and a seedling; its desert look, an acacia) and the
       desert acacia T18; and the tree clumps TC01 to TC05, each one model composed of the
       approved trees (ebld_terr.h draws it whole at the clump's centre coordinate) */
    "T01", "T02", "T05", "T06", "T07", "T04", "T09",
    "T03", "T10", "T11", "T12", "T13", "T14", "T15", "T17", "T08", "T18", "T16",
    "TC01", "TC02", "TC03", "TC04", "TC05",
    /* Red Alert's ore mine, a terrain object, its theater's look (ra3d_ore_mod.h) */
    "MINE"
};
/* A TYPE DRAWN FROM ANOTHER TYPE'S FILE where it has none of its own, as the cartridge
   draws one mesh for both: the spruce T16 is T01's model in a play folder without T16's
   own file. */
static const char* const EBLD_ALIAS[][2] = { { "T16", "T01" } };
static bool ebld_type_has_file(const char* t);
static const char* ebld_alias(const char* t)
{
    if (g_raModule && t) {
        static const char* const decoy[][2] = {
            {"FACF", "FACT"}, {"WEAF", "WEAP"}, {"DOMF", "DOME"},
            {"SYRF", "SYRD"}, {"SPEF", "SPEN"}
        };
        for (size_t i = 0; i < sizeof decoy / sizeof decoy[0]; i++)
            if (!strcmp(t, decoy[i][0])) return decoy[i][1];
        return t;
    }
    for (size_t i = 0; t && i < sizeof EBLD_ALIAS / sizeof EBLD_ALIAS[0]; i++)
        if (!strcmp(t, EBLD_ALIAS[i][0]) && !ebld_type_has_file(t)) return EBLD_ALIAS[i][1];
    return t;
}
/* A theater's file suffix, in the order of EbldModel::theater (0 temperate, 1 desert,
   2 winter). */
static const char* const EBLD_THEATERS[3] = { "TEM", "DES", "WIN" };
/* The pack's theater as EbldModel::theater counts them, -1 for one it does not know. The
   cartridge's SNOW and SAND are the winter and the desert looks. */
static bool ebld_ra_snow_temperate(void);
static int ebld_pack_theater(void)
{
    const char* t = g_pack.theater;
    /* RED ALERT'S SCENES (RATEMP, RASNOW, RAINT): its temperate is Tiberian Dawn's, so the
       trees reused from OpenCNC3D (their files are the temperate look, <TYPE>_TEM) stand on
       it. Its snow is the winter look, which no reused tree has: a snow map keeps its 1996
       trees, frosted as the engine paints them, unless OPENCNC3D_RA3DSNOWTREES=temperate
       stands the green temperate models on the snow (the comparison it was decided on).
       The interior has no theater look. Every file without a suffix, the civilian
       buildings and the Red Alert structures among them, is drawn in every theater. */
    if (g_raModule) {
        if (!strcmp(t, "RATEMP")) return 0;
        if (!strcmp(t, "RASNOW")) return ebld_ra_snow_temperate() ? 0 : 2;
        return -1;
    }
    if (!strcmp(t, "TEMPERAT") || !strcmp(t, "TEMPERATE")) return 0;
    if (!strcmp(t, "DESERT") || !strcmp(t, "SAND")) return 1;
    if (!strcmp(t, "WINTER") || !strcmp(t, "SNOW")) return 2;
    return -1;
}
static bool ebld_ra_snow_temperate(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("OPENCNC3D_RA3DSNOWTREES");
        on = (e && !strcmp(e, "temperate")) ? 1 : 0;
    }
    return on != 0;
}

/* THE FILES THERE ARE, found once per process (ebld_load) and READ THE FIRST TIME A
   MISSION'S PACK CARRIES THEIR TYPE (ebld_load_needed, from ebld_attach). With the civilian
   buildings there are over sixty files and most missions show a handful of their types,
   and a model's triangles stay in memory once read (every pack load rebuilds its meshes
   from them): reading them all at the start cost the 32-bit Windows build a few hundred
   megabytes of address space for buildings no mission of that theater has. A model read
   is kept for the rest of the process, and a model's index never moves (staged walls'
   masks are appended after their wall as it is read). */
struct EbldFile { std::string path; int ti, th; bool tried, lazy; };
static std::vector<EbldFile> g_ebldFiles;
/* Whether the play folder has a file of this type for the pack's theater (its own look or
   a plain file): EBLD_ALIAS draws another type's file only where it has none. */
static bool ebld_type_has_file(const char* t)
{
    const int th = ebld_pack_theater();
    for (size_t i = 0; t && i < g_ebldFiles.size(); i++)
        if (!strcmp(EBLD_TYPES[g_ebldFiles[i].ti], t)
            && (g_ebldFiles[i].th < 0 || g_ebldFiles[i].th == th))
            return true;
    return false;
}
/* THE TREE CLUMPS, TC01 TO TC05: no cartridge mesh and no type in a Tiberian Dawn pack
   (the proof at ClumpDef in cnc_eyes.cpp), so their files are read at first sight and
   attached without a pack type, and borrow no cartridge shadow plate. */
static bool ebld_packless_type(const char* t)
{
    return t && t[0] == 'T' && t[1] == 'C' && t[2] == '0' && t[3] >= '1' && t[3] <= '5'
        && !t[4];
}
/* WHICH ARE READ AT FIRST SIGHT rather than with the pack: every mission's pack carries
   every type of the game, civilian buildings of all three theaters included, so reading
   with the pack would read them all. The structures and the three buildable walls are
   read with the pack (a structure going up needs its model before it is first seen: its
   placement is matched against it at the pack load, ebld_place.h); the civilian buildings,
   the barbed wire and the wooden fence, which are never built, are read the first time
   the game asks for their type (ebld_ensure), which is the mission's first tick for every
   one the map places. */
static bool ebld_type_lazy(const char* t)
{
    if (g_raModule) return true;
    if (t[0] == 'V' && t[1] >= '0' && t[1] <= '9' && t[2] >= '0' && t[2] <= '9' && !t[3])
        return true;
    /* and the terrain objects and cell decorations (ebld_terr.h): a mission shows a few */
    if (!strncmp(t, "ROCK", 4) || !strncmp(t, "SPLIT", 5)) return true;
    /* and the trees, T01 to T18, and the tree clumps, TC01 to TC05 */
    if (t[0] == 'T' && t[1] >= '0' && t[1] <= '9' && t[2] >= '0' && t[2] <= '9' && !t[3])
        return true;
    if (ebld_packless_type(t)) return true;
    return !strcmp(t, "BARB") || !strcmp(t, "WOOD") || !strcmp(t, "P04")
        || !strcmp(t, "SCRATE") || !strcmp(t, "WCRATE");
}

/* One file into the models, with its wall masks after it. False: refused (stderr says
   why). */
static void ebld_spec_read(const char* modelPath, EbldModel& M);   /* (below, by the textures) */
static bool ebld_read_file(const EbldFile& F, size_t maxSourceBytes = 0)
{
    EbldModel m;
    const char* path = F.path.c_str();
    if (!ebld_read_one(path, m, maxSourceBytes))
        return false;
    const char* want = EBLD_TYPES[F.ti];
    if (m.type != want) {
        fprintf(stderr, "ebld: %s says it is %s -- refused\n", path, m.type.c_str());
        return false;
    }
    m.theater = F.th;
    /* a wall file for a building's name, or the reverse, would be drawn by the wrong
       path: the wall flag and the name have to agree */
    if (((m.flags & EBX_WALL) != 0) != (ebld_wall_kind(want) >= 0)) {
        fprintf(stderr, "ebld: %s is %s a wall -- refused\n", path,
                (m.flags & EBX_WALL) ? "" : "not");
        return false;
    }
    /* WHETHER IT WEARS ITS OWNER'S COLOUR: a file without house paint (version 1, or
       baked from an export without masks, every civilian building's) draws the GDI look
       for every house. */
    {
        int painted = 0;
        for (size_t t = 0; t < m.tex.size(); t++)
            if (m.tex[t].house & 1) painted++;
        if (painted)
            fprintf(stderr, "ebld: %s: house paint on %d of %d textures\n",
                    m.type.c_str(), painted, (int)m.tex.size());
        else
            fprintf(stderr, "ebld: %s: no house paint, every house wears the GDI look\n",
                    m.type.c_str());
    }
    /* its specular maps, beside it (walls carry none) */
    if (!(m.flags & EBX_WALL)) {
        ebld_spec_read(path, m);
        if (m.hasSpec) {
            int k = 0;
            for (size_t t = 0; t < m.spec.size(); t++) if (m.spec[t].from >= 0) k++;
            fprintf(stderr, "ebld: %s carries %d specular map%s\n", m.type.c_str(), k, k == 1 ? "" : "s");
        }
    }
    g_ebld.push_back(m);
    const int w = (int)g_ebld.size() - 1;
    /* A STAGED WALL'S SIXTEEN MASKS become models of their own, right after it */
    if (g_ebld[w].wallStaged) {
        for (int k = 0; k < 16; k++) {
            EbldModel S;
            ebld_wall_split(g_ebld[w], w, k, S);
            g_ebld.push_back(S);
            g_ebld[w].wallSub[k] = (int)g_ebld.size() - 1;
        }
        fprintf(stderr, "ebld: %s: its damaged and destroyed stages for all sixteen "
                        "connection masks\n", g_ebld[w].type.c_str());
    }
    /* A TREE'S BURNING STAGE AND ITS SNAG, files of their own beside its file
       (<file>_BURN.ebld, <file>_SNAG.ebld), read with it and kept right after it. Each
       names the tree's own type in its header and must be intact-only; in memory its type
       takes a suffix (~BURN, ~SNAG) so that no lookup by type ever answers it. */
    if (want[0] == 'T' && want[1] >= '0' && want[1] <= '9' && F.path.size() > 5) {
        static const char* const SUF[3] = { "", "BURN", "SNAG" };
        for (int k = 1; k <= 2; k++) {
            const std::string sp = F.path.substr(0, F.path.size() - 5) + "_" + SUF[k] + ".ebld";
            FILE* f = fopen(sp.c_str(), "rb");
            if (!f) continue;
            fclose(f);
            EbldModel S;
            if (!ebld_read_one(sp.c_str(), S, maxSourceBytes)) {
                if (maxSourceBytes) return false;
                continue;
            }
            if (S.type != want || S.nset != 1 || (S.flags & EBX_WALL)) {
                fprintf(stderr, "ebld: %s is not an intact-only stage of %s -- refused\n",
                        sp.c_str(), want);
                if (maxSourceBytes) return false;
                continue;
            }
            S.type = std::string(want) + "~" + SUF[k];
            S.theater = F.th;
            S.stageOf = w;
            S.stageKind = k;
            g_ebld.push_back(S);
            g_ebld[w].stageModel[k] = (int)g_ebld.size() - 1;
            fprintf(stderr, "ebld: %s: its %s stage read (%d triangles, %d emitters)\n",
                    want, k == 1 ? "burning" : "snag",
                    (int)g_ebld.back().set[EBS_INTACT].tri.size(),
                    (int)g_ebld.back().emit.size());
        }
    }
    return true;
}

/* Once per process: which files there are, scanned by name over EBLD_TYPES, each plain
   and with every theater's suffix, so installing a new type is dropping its file in (and
   naming it there). Nothing is read yet. */
static void ebld_load(const char* dir)
{
    if (g_ebldLoaded) return;
    g_ebldLoaded = true;
    for (size_t i = 0; i < sizeof EBLD_TYPES / sizeof EBLD_TYPES[0]; i++)
        for (int th = -1; th < 3; th++) {
            char path[512];
            if (th < 0)
                snprintf(path, sizeof path, "%s%s/%s.ebld", dir && *dir ? dir : "",
                         g_raModule ? "ra3d/ebld" : "ebld", EBLD_TYPES[i]);
            else
                snprintf(path, sizeof path, "%s%s/%s_%s.ebld", dir && *dir ? dir : "",
                         g_raModule ? "ra3d/ebld" : "ebld", EBLD_TYPES[i], EBLD_THEATERS[th]);
            FILE* f = fopen(path, "rb");
            if (!f) continue;
            fclose(f);
            EbldFile F;
            F.path = path; F.ti = (int)i; F.th = th; F.tried = false;
            F.lazy = ebld_type_lazy(EBLD_TYPES[i]);
            g_ebldFiles.push_back(F);
        }
    fprintf(stderr, "ebld: %d remastered building and wall files found; each is read the "
                    "first time a mission's pack carries its type\n", (int)g_ebldFiles.size());
}

/* Before an attach: read every file whose type the pack just loaded carries (in its own
   theater, for a theater's look) and that has not been read. */
static void ebld_load_needed(void)
{
    const int th = ebld_pack_theater();
    std::string names;
    int n = 0;
    for (size_t i = 0; i < g_ebldFiles.size(); i++) {
        EbldFile& F = g_ebldFiles[i];
        if (F.tried || F.lazy) continue;
        if (F.th >= 0 && F.th != th) continue;
        if (!g_raModule && g_pack.type.find(EBLD_TYPES[F.ti]) == g_pack.type.end()) continue;
        F.tried = true;
        if (!ebld_read_file(F)) continue;
        names += (names.empty() ? "" : " ") + std::string(EBLD_TYPES[F.ti])
               + (F.th >= 0 ? std::string("_") + EBLD_THEATERS[F.th] : std::string());
        n++;
    }
    if (n)
        fprintf(stderr, "ebld: %d remastered building%s read for %s: %s\n", n,
                n == 1 ? "" : "s", g_pack.scen, names.c_str());
}

/* ---- attaching a model to the pack just loaded ------------------------------------ */
static void ebld_mesh_begin(PackMesh& pm, const char* type, const char* what)
{
    memset(pm.name, 0, sizeof pm.name);
    snprintf(pm.name, sizeof pm.name, "ebld:%s:%s", type, what);
}
static int ebld_mesh_end(PackMesh& pm)
{
    /* Generated meshes need the same exact pass census as pack_load. Their
       default all-modes mask otherwise defeats draw_mesh's empty-pass exit. */
    if(mesh_perf_switch("OPENCNC3D_GENERATED_MODE_MASK")) {
        pm.modeMask=0;
        for(const auto& tri:pm.tris)if(tri.mode<32)pm.modeMask|=1u<<tri.mode;
    }

    PackPart part;
    part.tri0 = 0;
    part.ntris = (int)pm.tris.size();
    part.role = ROLE_STATIC;
    part.px = part.py = part.pz = 0.0f;
    pm.parts.assign(1, part);
    if(mesh_perf_switch("OPENCNC3D_EBLD_MESH_MOVE"))g_pack.mesh.push_back(std::move(pm));
    else g_pack.mesh.push_back(pm);
    return (int)g_pack.mesh.size() - 1;
}
#include "building_service.h"

/* One triangle of a set into a pack mesh: `which` picks the texture and UV pair. */
enum { EBT_TEX = 0, EBT_DMG, EBT_GLOW, EBT_OVER };
static void ebld_push_tri(PackMesh& pm, const EbldModel& M, const EbldTri& t, int which,
                          const unsigned char* alpha)
{
    int tex = t.tex;
    if (which == EBT_DMG)  tex = t.texDmg;
    if (which == EBT_GLOW) tex = t.texGlow;
    if (which == EBT_OVER) tex = t.texOver;
    PackTri pt;
    pt.tex = tex < 0 ? -1 : M.tex0 + tex;
    pt.mode = (which == EBT_GLOW) ? (unsigned char)MODE_OPAQUE
            : (which == EBT_OVER) ? (unsigned char)MODE_CUTOUT : t.mode;
    pt.wrap = t.wrap;
    for (int k = 0; k < 3; k++) {
        const EbldVert& s = t.v[k];
        PackVert& d = pt.v[k];
        d.x = s.x; d.y = s.y; d.z = s.z;
        const bool dmgUV = (which != EBT_TEX);
        d.u = dmgUV ? s.ud : s.u;
        d.v = dmgUV ? s.vd : s.v;
        if (which == EBT_GLOW) {
            d.r = d.g = d.b = d.a = 255;      /* white: the baked shade would darken it */
        } else {
            d.r = s.r; d.g = s.g; d.b = s.b;
            d.a = alpha ? alpha[k] : s.a;
        }
        /* its smooth normal, for the normal target (draw_mesh's g_meshNrmOut) */
        pm.nrm.push_back(s.nx); pm.nrm.push_back(s.ny); pm.nrm.push_back(s.nz);
    }
    pm.tfl.push_back((t.tflags & EBT_EMIT) ? 1 : 0);
    pm.tris.push_back(pt);
}

/* THE ZONE A VERTEX BELONGS TO, as a weight: 1 on the zone's own bearing, falling to 0
   a whole zone away, so neighbouring zones blend. Bearings are 0 east, counting
   counter-clockwise seen from above (2 north, 4 west, 6 south), about the footprint
   centre, which is the pack origin. */
static unsigned char ebld_zone_alpha_angle(float ang, int zone)
{
    float d = ang - 45.0f * (float)zone;
    while (d > 180.0f)  d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    float w = 1.0f - fabsf(d) / 45.0f;
    if (w < 0.0f) w = 0.0f;
    return (unsigned char)(255.0f * w + 0.5f);
}

static unsigned char ebld_zone_alpha(float x, float z, int zone)
{
    const float ang = atan2f(-z, x) * (180.0f / EBLD_PI);
    return ebld_zone_alpha_angle(ang, zone);
}
/* The eight overlays use the same source vertex bearing. Keep its original
   float calculation once per eligible corner for this build only. */
static std::vector<float> ebld_overlay_angles(const EbldSet& S)
{
    std::vector<float> angles(S.tri.size()*3,0.0f);
    for(size_t t=0;t<S.tri.size();t++) {
        const EbldTri& T=S.tri[t];
        if(T.kind!=0 || T.texOver<0 || T.mode==MODE_XLU)continue;
        for(int q=0;q<3;q++)
            angles[t*3+q]=atan2f(-T.v[q].z,T.v[q].x)*(180.0f/EBLD_PI);
    }
    return angles;
}

/* Whether a model texture is a flipbook (a grid of frames the draw steps through). */
static bool ebld_flipbook(const EbldModel& M, int texIdx)
{
    return texIdx >= 0 && texIdx < (int)M.tex.size() && M.tex[texIdx].grid > 1;
}

/* ONE SET INTO ONE PACK MESH, as `which` draws it (the plain look, the damaged look, the
   glow, or zone `zone`'s overlay), in the two blocks the draw expects: every piece's
   surfaces in piece order, then every piece's broken faces in piece order, each run
   recorded in rng (EBR ints per piece). A triangle the look does not use is left out.
   WITHIN A PIECE'S SURFACES the flipbook triangles go last (a glowing pool or a signal
   lamp on a piece that is otherwise plain), so the plain part is one run and the part
   that needs its frame chosen is another: [A, FA) and [FA, B). A piece that is all one
   or the other keeps its triangles in the file's order. */
/* One original piece/block, with identical triangle order, arithmetic and ranges.
   The wrapper retains the existing all-at-once API; preparation can yield at a piece. */
static void ebld_fill_mesh_piece(PackMesh& pm, const EbldModel& M, const EbldSet& S,
                                 int which, int zone, std::vector<int>& rng,
                                 std::vector<std::pair<int, int> >* waved,
                                 const std::vector<float>* overlayAngles, int block, size_t k)
{
    const int wq = (&S == &M.set[EBS_INTACT]) ? 0 : (&S == &M.set[EBS_CUT]) ? 1 : -1;
    const bool wv = waved && wq >= 0 && !M.waveT[wq].empty();
    const EbldPiece& P = S.piece[k];
    const int a = (int)pm.tris.size();
    int first = -1, fa = -1;
    for (int sub = 0; sub < (block ? 1 : 2); sub++) {
        if (sub == 1) fa = (int)pm.tris.size();
        for (int t = P.tri0; t < P.tri0 + P.ntri; t++) {
            const EbldTri& T = S.tri[t];
            if ((T.kind != 0) != (block != 0)) continue;
            const int look = (which == EBT_TEX) ? T.tex : (which == EBT_DMG) ? T.texDmg
                           : (which == EBT_GLOW) ? T.texGlow : T.texOver;
            if (block == 0 && ebld_flipbook(M, look) != (sub == 1)) continue;
            unsigned char al[3] = { 255, 255, 255 };
            if (which == EBT_GLOW && T.texGlow < 0) continue;
            if (which == EBT_OVER) {
                /* glass keeps its transparency: the overlay is a redraw of the surface
                   in the cutout pass, and a pane is drawn translucent in its own */
                if (T.kind != 0 || T.texOver < 0 || T.mode == MODE_XLU) continue;
                for (int q = 0; q < 3; q++)
                    al[q] = overlayAngles && overlayAngles->size()==S.tri.size()*3
                          ? ebld_zone_alpha_angle((*overlayAngles)[(size_t)t*3+q],zone)
                          : ebld_zone_alpha(T.v[q].x, T.v[q].z, zone);
                if (!al[0] && !al[1] && !al[2]) continue;
            }
            if (first < 0) first = look;
            if (wv && M.waveOf[wq][t] >= 0)
                waved->push_back(std::make_pair((int)pm.tris.size(), M.waveOf[wq][t]));
            ebld_push_tri(pm, M, T, which, which == EBT_OVER ? al : NULL);
            /* a tree's wind weights ride along, triangle for triangle */
            if (!M.sway.empty() && &S == &M.set[EBS_INTACT] && which == EBT_TEX)
            {
                pm.sway.insert(pm.sway.end(), &M.sway[(size_t)t * 6],
                               &M.sway[(size_t)t * 6] + 6);
                for (int q = 0; q < 3; q++) {
                    const float ph = (9.0f * T.v[q].x + 4.0f * T.v[q].y
                                      + 6.0f * T.v[q].z) * (1.0f / 1024.0f);
                    pm.swayPh.push_back(sinf(ph));
                    pm.swayPh.push_back(cosf(ph));
                    pm.swayPh.push_back(sinf(1.7f * ph));
                    pm.swayPh.push_back(cosf(1.7f * ph));
                }
            }
        }
    }
    int* r = &rng[k * EBR];
    if (block == 0) {
        r[EBR_A] = a; r[EBR_B] = (int)pm.tris.size(); r[EBR_TEX] = first;
        r[EBR_FA] = fa;
    } else {
        r[EBR_RA] = a; r[EBR_RB] = (int)pm.tris.size();
        if (r[EBR_TEX] < 0) r[EBR_TEX] = first;
    }
}

static void ebld_fill_mesh(PackMesh& pm, const EbldModel& M, const EbldSet& S, int which,
                           int zone, std::vector<int>& rng,
                           std::vector<std::pair<int, int> >* waved = NULL,
                           const std::vector<float>* overlayAngles = NULL)
{
    rng.assign(S.piece.size() * EBR, -1);
    if (waved) waved->clear();
    for (int block = 0; block < 2; block++)
        for (size_t k = 0; k < S.piece.size(); k++)
            ebld_fill_mesh_piece(pm, M, S, which, zone, rng, waved, overlayAngles, block, k);
}

/* THE CUT AND THE WRECK AS PACK MESHES, made the first time a building of the type is
   hit (from the tick edge, ebld_step), not at the pack load: they are ten meshes of up to
   sixteen thousand triangles, a hundred megabytes over the whole set, and in a mission
   most buildings are never touched. cut, cutdmg, glowc, over0..7: the CUT set five ways;
   wreck and glowx: the DESTROYED set, where there is one. An intact-only model has none
   of these, and a model without a DESTROYED set none of the wreck's: their mesh slots
   stay -1. */
/* One original cut/wreck mesh at a time. Demand uses the same function until ready;
   loading/construction preparation can stop between kinds without exposing half-built
   geometry to a draw. Pack attachment resets every index and the progress together. */
static bool ebld_build_cut_step(EbldModel& M)
{
    if (M.cutBuilt || !M.attached || M.nset < 2) return false;
    const int total = 11 + (M.nset == EBS_N ? 2 : 0);
    if (M.cutNext < 0 || M.cutNext >= total) M.cutNext = 0;
    const Uint64 t0 = SDL_GetPerformanceCounter();
    if (M.cutNext == 0 && M.cutBlock == 0 && M.cutPiece == 0) {
        M.cutPrepareMs = 0.0;
        if (mesh_perf_switch("OPENCNC3D_EBLD_CUT_ANGLES"))
            M.cutAngles = ebld_overlay_angles(M.set[EBS_CUT]);
    }
    const int kind = M.cutNext;
    const char* ty = M.type.c_str();
    const int mk = kind < 11 ? ((kind < 3) ? EBM_CUT + kind : EBM_OVER0 + kind - 3)
                            : (kind == 11 ? EBM_WRECK : EBM_GLOWX);
    const int which = kind == 0 || kind == 11 ? EBT_TEX : kind == 1 ? EBT_DMG
                    : kind == 2 || kind == 12 ? EBT_GLOW : EBT_OVER;
    const int zone = kind < 11 ? kind - 3 : -1;
    const EbldSet& S = M.set[kind < 11 ? EBS_CUT : EBS_DESTROYED];
    if (M.cutBlock == 0 && M.cutPiece == 0) {
        char what[12];
        if (kind == 0) snprintf(what, sizeof what, "cut");
        else if (kind == 1) snprintf(what, sizeof what, "cutdmg");
        else if (kind == 2) snprintf(what, sizeof what, "glowc");
        else if (kind == 11) snprintf(what, sizeof what, "wreck");
        else if (kind == 12) snprintf(what, sizeof what, "glowx");
        else snprintf(what, sizeof what, "over%d", zone);
        ebld_mesh_begin(M.cutTemp, ty, what);
        M.rng[mk].assign(S.piece.size() * EBR, -1);
        M.waveMesh[mk].clear();
    }
    size_t triangles = 0;
    do {
        if (M.cutPiece >= S.piece.size()) {
            M.cutPiece = 0; M.cutBlock++;
            if (M.cutBlock == 2) break;
        }
        if (S.piece.empty()) { M.cutBlock = 2; break; }
        const size_t piece = M.cutPiece++;
        ebld_fill_mesh_piece(M.cutTemp, M, S, which, zone, M.rng[mk],
                             kind < 11 ? &M.waveMesh[mk] : NULL,
                             M.cutAngles.empty() ? NULL : &M.cutAngles, M.cutBlock, piece);
        triangles += (size_t)S.piece[piece].ntri;
    } while (triangles < 512);
    if (M.cutBlock == 2) {
        M.mesh[mk] = ebld_mesh_end(M.cutTemp);
        M.cutTemp = PackMesh();
        if (mk == EBM_CUT || mk == EBM_CUTDMG) bservice_mesh(M, M.mesh[mk]);
        M.cutBlock = 0; M.cutPiece = 0; M.cutNext++;
    }
    const Uint64 elapsed = SDL_GetPerformanceCounter() - t0;
    M.cutPrepareMs += (double)elapsed * 1000.0 / (double)SDL_GetPerformanceFrequency();
    if (perf_trace_frame_active())
        g_destructTrace[19] += elapsed * 1000000u / SDL_GetPerformanceFrequency();
    if (M.cutNext == total) {
        M.cutBuilt = true;
        std::vector<float>().swap(M.cutAngles);
        if (perf_trace_frame_active()) g_destructTrace[16]++;
        long tris = 0;
        for (int mk = EBM_CUT; mk <= EBM_GLOWX; mk++)
            if (M.mesh[mk] >= 0) tris += (long)g_pack.mesh[M.mesh[mk]].tris.size();
        fprintf(stderr, "ebld: %s cut meshes built, %d meshes, %ld triangles, %.1f ms\n",
                ty, total, tris, M.cutPrepareMs);
    }
    return true;
}
static void ebld_build_cut(EbldModel& M)
{
    if (M.cutBuilt || !M.attached || M.nset < 2) return;
    const Uint64 start = SDL_GetPerformanceCounter();
    while (ebld_build_cut_step(M)) {}
    g_ebldPrepareMetrics.demandCuts++;
    g_ebldPrepareMetrics.demandCutUs += (SDL_GetPerformanceCounter() - start)
                                       * 1000000u / SDL_GetPerformanceFrequency();
}

/* After every load of the mission pack, beside n64fix_attach and in the same place:
   append each model's textures and meshes, if the pack has the cartridge type it stands
   in for. pack_free deletes them with the rest of the pack, because they ARE pack
   textures and meshes. */
/* ONE MODEL ONTO THE PACK NOW LOADED: its texture slots, its intact mesh and its scorch.
   False: not attached (the pack has no such type, another theater's look, or a wall
   mask whose wall is not attached), or it is a wall mask. */
static bool ebld_attach_one(size_t mI, int pth, const std::set<std::string>& ownLook)
{
    g_ebldResolveGen++;
    EbldModel& M = g_ebld[mI];
    M.attached = false;
    bservice_bind(M,M.service);
    for (int i = 0; i < EBM_N; i++) {
        M.mesh[i] = -1; M.rng[i].clear();
        M.waveMesh[i].clear(); M.wavePosed[i] = -1.0e30f;
    }
    /* (a tree's stage is attached with its tree, after it, and borrows its pack type) */
    const bool stage = M.stageOf >= 0 && M.stageOf < (int)g_ebld.size();
    std::map<std::string, PackType>::const_iterator it =
        g_pack.type.find(stage ? g_ebld[M.stageOf].type : M.type);
    const bool packless = ebld_packless_type(stage ? g_ebld[M.stageOf].type.c_str()
                                                   : M.type.c_str());
    if (!g_raModule && !packless && it == g_pack.type.end()) return false;
    if (stage) {
        if (!g_ebld[M.stageOf].attached) return false;
    } else if (M.theater >= 0 ? M.theater != pth : ownLook.count(M.type) != 0) {
        return false;
    }
    int cmi = (g_raModule || it == g_pack.type.end()) ? -1 : it->second.mesh;
    M.resident = M.residentAll = false;
    if (M.texOwner >= 0) {
        /* A WALL MASK'S MODEL draws with its wall's textures, attached just before it */
        const EbldModel& O = g_ebld[M.texOwner];
        if (!O.attached) return false;
        M.tex0 = O.tex0;
        cmi = -1;
    } else {
        /* THE TEXTURES ARE PLACEHOLDERS until the model is first drawn (ebld_resident):
           the slots are reserved now so every mesh can name them, and they are filled
           from the file then. */
        M.tex0 = (int)g_pack.tex.size();
        for (size_t t = 0; t < M.tex.size(); t++) {
            PackTex pt;
            pt.w = pt.uw = M.tex[t].w;
            pt.h = pt.uh = M.tex[t].h;
            pt.gl = pt.gl_gdi = 0;
            for (int e = 0; e < 7; e++) pt.gl_extra[e] = 0;
            g_pack.tex.push_back(pt);
        }
    }
    const char* ty = M.type.c_str();
    /* intact: the INTACT set, then the cartridge's own ground-shadow plate. The
       INTACT set has no broken faces, so its second block is empty and the plate
       follows the surfaces directly. */
    {
        PackMesh pm;
        ebld_mesh_begin(pm, ty, "intact");
        ebld_fill_mesh(pm, M, M.set[EBS_INTACT], EBT_TEX, -1, M.rng[EBM_INTACT],
                       &M.waveMesh[EBM_INTACT]);
        M.shadow0 = (int)pm.tris.size();
        /* BORROWED, as the fixed cartridge buildings borrow it, so a Classic-style
           shadow plate still exists under the building while the Enhanced sun
           shadow is switched off. */
        if (cmi >= 0 && cmi < (int)g_pack.mesh.size()) {
            const PackMesh& cart = g_pack.mesh[cmi];
            for (size_t t = 0; t < cart.tris.size(); t++)
                if (cart.tris[t].mode == MODE_SHADOW) {
                    pm.tris.push_back(cart.tris[t]);
                    for (int q = 0; q < 3; q++) {
                        pm.nrm.push_back(0.0f); pm.nrm.push_back(1.0f); pm.nrm.push_back(0.0f);
                    }
                    pm.tfl.push_back(0);
                }
        }
        M.shadow1 = (int)pm.tris.size();
        M.mesh[EBM_INTACT] = ebld_mesh_end(pm);
        bservice_mesh(M,M.mesh[EBM_INTACT]);
    }
    M.cutBuilt = false; M.cutNext = 0; M.cutPrepareMs = 0.0;
    M.cutTemp = PackMesh(); M.cutBlock = 0; M.cutPiece = 0;
    std::vector<float>().swap(M.cutAngles);
    /* THE SCORCH, a flat square on the ground centred on the footprint. It is a GRID
       rather than two triangles because it is drawn with the ground sampled per
       vertex, and two triangles spanning three cells would cut through any rise in
       between; eight by eight lets it lie on the terrain. North (-z) is the top row
       of its image, v down, as a pack texture. An intact-only model's is the same grid
       at its size per footprint cell, stretched to the building's footprint where it
       is drawn. A wall has none. */
    if (M.param[EBP_SCORCH_TEX] >= 0.0f) {
        PackMesh pm;
        ebld_mesh_begin(pm, ty, "scorch");
        const float S = M.param[EBP_SCORCH_SIZE] / MODEL_SCALE;
        const int st = (int)M.param[EBP_SCORCH_TEX];
        const int N = 8;
        for (int j = 0; j < N; j++)
            for (int i = 0; i < N; i++) {
                float x[4], z[4], u[4], v[4];
                const int ci[4] = { i, i + 1, i + 1, i }, cj[4] = { j, j, j + 1, j + 1 };
                for (int c = 0; c < 4; c++) {
                    u[c] = (float)ci[c] / (float)N;
                    v[c] = (float)cj[c] / (float)N;
                    x[c] = (u[c] - 0.5f) * S;
                    z[c] = (v[c] - 0.5f) * S;
                }
                static const int TRI[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
                for (int h = 0; h < 2; h++) {
                    PackTri pt;
                    pt.tex = M.tex0 + st;
                    pt.mode = MODE_XLU;
                    pt.wrap = 0;
                    for (int k = 0; k < 3; k++) {
                        const int c = TRI[h][k];
                        PackVert& d = pt.v[k];
                        d.x = x[c]; d.y = 0.0f; d.z = z[c];
                        d.u = u[c]; d.v = v[c];
                        d.r = d.g = d.b = d.a = 255;
                    }
                    pm.tris.push_back(pt);
                    for (int q = 0; q < 3; q++) {
                        pm.nrm.push_back(0.0f); pm.nrm.push_back(1.0f); pm.nrm.push_back(0.0f);
                    }
                    pm.tfl.push_back(0);
                }
            }
        M.mesh[EBM_SCORCH] = ebld_mesh_end(pm);
    }
    M.attached = true;
    /* (counted with the buildings: neither a wall's mask nor a tree's stage) */
    return M.wallMask < 0 && M.stageOf < 0;
}

static void ebld_attach(void)
{
    g_ebldPrepareGeneration++;
    g_ebldPrepareMetrics = EbldPrepareMetrics();
    /* a new pack: whatever liveries were built were built for the last one, and none of
       its textures are on the card yet */
    g_liveryLive = false;
    g_ebldResidentBytes = 0;
    ebld_share_reset();
    ebld_load_needed();
    if (g_ebld.empty()) return;
    int attached = 0;
    /* A THEATER'S OWN LOOK of a type, where one was read for this pack's theater, stands in
       for the type; the plain file stays detached */
    const int pth = ebld_pack_theater();
    std::set<std::string> ownLook;
    for (size_t mI = 0; mI < g_ebld.size(); mI++)
        if (g_ebld[mI].theater >= 0 && g_ebld[mI].theater == pth) ownLook.insert(g_ebld[mI].type);
    for (size_t mI = 0; mI < g_ebld.size(); mI++)
        if (ebld_attach_one(mI, pth, ownLook)) attached++;
    g_ebldPackSerial++;
    int top = 0;
    for (size_t i = 0; i < g_ebld.size(); i++)
        if (g_ebld[i].wallMask < 0 && g_ebld[i].stageOf < 0) top++;
    fprintf(stderr, "ebld: %d of %d remastered buildings and walls attached to %s\n",
            attached, top, g_pack.scen);
}

/* ================================================================================
 * RESIDENCY. A model's textures go to the card the first time it is drawn in a pack, not
 * when the pack loads: all twenty-five models together are over 400 MB of texture, and a
 * mission shows a handful of them. And in two halves: the whole model's with its first
 * draw, the cut's, the damage's, the overlays' and the glows' (`all`) with the first draw
 * of one of its buildings that has been hit, which in most missions is few of them. Read back out of the file (EbldTex), which must still
 * be the file that was checked (the same size); uploaded with the bleed, filter note and
 * REPEAT wrap a fixed cartridge texture gets; given its skirmish liveries if the pack's
 * have already been built. Called from the draws, where the GL context is current. A
 * model whose file cannot be read back is detached, and the cartridge draws its type.
 * ============================================================================== */
/* A remastered texture with its mipmap chain (gl_upload_mips), the bleed and wrap a
   cartridge texture gets, trilinear. It stays out of the bilinear switch's register: the
   switch would take its chain away. */
/* THE SAME PICTURE GOES ON THE CARD ONCE. A tree clump's file carries its member trees'
   own atlases (compose_clump.py copies each look's sheets in), byte for byte the trees'
   files' ones, and drawn from a copy each those were a second copy of every member's
   sheets on the card (on GDI 1, 52.8 MB of remastered textures became 126.4 MB). So a
   tree's or a clump's texture whose compressed pixels and upload are identical to one
   already on the card for the pack now loaded takes that card texture instead of making
   its own. Only a plain texture of a tree or a clump is shared (no house paint and no
   building-service frames: those are rebuilt per model, liveries and all). The key is
   the stream's FNV-1a hash with its size, its dimensions and its kind of upload; a hit is
   checked against the owner's size too. The table lives as long as the pack (ebld_attach
   empties it), and g_ebldShareRefs counts the slots holding each shared name, so a
   rolled-back preparation (ebld_prepare_restore_texture) deletes a name only when its
   last holder goes. --noebldshare is the A/B: every file its own copies. */
struct EbldShared { GLuint gl; int w, h; bool mips; };
static std::unordered_map<uint64_t, EbldShared> g_ebldShare;
static std::map<GLuint, int> g_ebldShareRefs;
static bool g_ebldShareOn = true;
static long g_ebldShareHits = 0, g_ebldShareSaved = 0;
static bool ebld_share_type(const char* t)
{
    return t && t[0] == 'T' && ((t[1] >= '0' && t[1] <= '9') || ebld_packless_type(t));
}
static uint64_t ebld_share_key(const std::vector<unsigned char>& c, const EbldTex& T, bool mips)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < c.size(); i++) { h ^= c[i]; h *= 1099511628211ull; }
    const uint64_t meta[5] = { (uint64_t)c.size(), (uint64_t)T.w, (uint64_t)T.h,
                               (uint64_t)T.role, (uint64_t)((T.cover ? 1 : 0) | (T.bled ? 2 : 0)
                                                            | (mips ? 4 : 0)) };
    for (int i = 0; i < 5; i++) h ^= meta[i] + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h;
}
static void ebld_share_reset(void)
{
    g_ebldShare.clear();
    g_ebldShareRefs.clear();
    g_ebldShareHits = g_ebldShareSaved = 0;
}

static GLuint ebld_upload_mips(const std::vector<unsigned char>& px, int w, int h, bool bled)
{
    std::vector<unsigned char> prepared;
    const unsigned char* pixels = px.data();
    if (!bled || !mesh_perf_switch("OPENCNC3D_EBLD_UPLOAD_REUSE")) {
        prepared = px;
        if (!bled) fx_bleed_rgba(prepared.data(), w, h, 4);
        pixels = prepared.data();
    }
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl_upload_mips(pixels, w, h);
    return id;
}
/* THE SAME FOR A CUT-OUT SHEET THAT MUST NOT GO BALD (EbldTex::cover, the trees' spray
   cards): every level box-filtered from the one above, then its alpha scaled until the
   share of texels that pass the cut (one half, the cutout pass's test) is level 0's, as
   the export's own chain was made (tools/art/remaster/foliage, the Tree recipe's mip
   chain; game/bake_tree3d.py's mip_chain). A plain box-filtered chain averages a spray's
   needles into a half-transparent haze that fails the test, so the crown thins to its
   limbs as it recedes. Fixed function: the levels are uploaded one by one. */
static GLuint ebld_upload_cover(std::vector<unsigned char> px, int w, int h, bool bled)
{
    if (!bled) fx_bleed_rgba(&px[0], w, h, 4);
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_FALSE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);
    long pass0 = 0;
    for (size_t i = 3; i < px.size(); i += 4) if (px[i] > 127) pass0++;
    const double target = (double)pass0 / (double)((size_t)w * h);
    std::vector<unsigned char> cur(px);
    std::vector<float> a;
    int cw = w, ch = h;
    for (int lev = 1; cw > 1 || ch > 1; lev++) {
        const int nw = cw > 1 ? cw / 2 : 1, nh = ch > 1 ? ch / 2 : 1;
        std::vector<unsigned char> nx((size_t)nw * nh * 4);
        a.assign((size_t)nw * nh, 0.0f);
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++) {
                float sum[4] = { 0, 0, 0, 0 };
                int n = 0;
                for (int dy = 0; dy < (ch > 1 ? 2 : 1); dy++)
                    for (int dx = 0; dx < (cw > 1 ? 2 : 1); dx++) {
                        const unsigned char* q = &cur[((size_t)(y * (ch > 1 ? 2 : 1) + dy) * cw
                                                       + (size_t)(x * (cw > 1 ? 2 : 1) + dx)) * 4];
                        for (int c = 0; c < 4; c++) sum[c] += q[c];
                        n++;
                    }
                unsigned char* o = &nx[((size_t)y * nw + x) * 4];
                for (int c = 0; c < 3; c++) o[c] = (unsigned char)(sum[c] / n + 0.5f);
                a[(size_t)y * nw + x] = sum[3] / n / 255.0f;
            }
        /* the scale whose share above the cut matches level 0's, by bisection */
        float lo = 0.05f, hi = 20.0f;
        if (target > 0.0)
            for (int it = 0; it < 24; it++) {
                const float mid = 0.5f * (lo + hi);
                long above = 0;
                for (size_t i = 0; i < a.size(); i++) if (a[i] * mid > 0.5f) above++;
                if ((double)above / (double)a.size() > target) hi = mid; else lo = mid;
            }
        const float sc = target > 0.0 ? 0.5f * (lo + hi) : 1.0f;
        for (size_t i = 0; i < a.size(); i++) {
            float v = a[i] * sc * 255.0f;
            if (v > 255.0f) v = 255.0f;
            nx[i * 4 + 3] = (unsigned char)(v + 0.5f);
        }
        glTexImage2D(GL_TEXTURE_2D, lev, GL_RGBA, nw, nh, 0, GL_RGBA, GL_UNSIGNED_BYTE, &nx[0]);
        cur.swap(nx);
        cw = nw; ch = nh;
    }
    return id;
}
/* Inflate directly into the three original pixel arrays. The old temporary `raw`
   duplicated up to 36 MiB for one 2048 house atlas, on top of its compressed copy.
   Output segmentation changes no byte; a one-byte overflow sink proves exact length.
   The input chunk is bounded independently of the texture/file's compressed size. */
/* THE SPECULAR MAPS: <stem>.spec beside a .ebld or .evhc (tools/art/remaster/bake_spec.py),
   "EVSP", version 2, a count, then per record the texture's index, its size, a scale and
   either the index of the texture whose map it takes (flag 1) or a zlib stream of its red
   (how strongly a texel reflects) and green (how tight its highlight is). A record that does
   not match its texture's size, or a file that cannot be read whole, is refused and the
   model draws without one. Read by the remastered buildings and vehicles alike. */
static void ebld_spec_read(const char* modelPath, EbldModel& M)
{
    M.hasSpec = false;
    M.spec.assign(M.tex.size(), EbldSpecRec());
    std::string sp(modelPath);
    const size_t dot = sp.rfind('.');
    if (dot == std::string::npos) return;
    sp = sp.substr(0, dot) + ".spec";
    FILE* f = fopen(sp.c_str(), "rb");
    if (!f) return;
    char magic[4];
    unsigned int hdr[2];
    bool ok = fread(magic, 1, 4, f) == 4 && memcmp(magic, "EVSP", 4) == 0
              && fread(hdr, 4, 2, f) == 2 && hdr[0] == 2 && hdr[1] <= 64;
    int n = 0;
    for (unsigned int r = 0; ok && r < hdr[1]; r++) {
        unsigned int rec[6];              /* index, w, h, scale, flags, from or csize */
        ok = fread(rec, 4, 6, f) == 6 && rec[0] < M.tex.size()
             && (int)rec[1] == M.tex[rec[0]].w && (int)rec[2] == M.tex[rec[0]].h && rec[3] <= 255;
        if (!ok) break;
        EbldSpecRec& R = M.spec[rec[0]];
        R.scale = (int)rec[3];
        R.sheen = (int)((rec[4] >> 8) & 255u);
        if (rec[4] & 1) {
            ok = rec[5] < M.tex.size() && M.tex[rec[5]].w == (int)rec[1] && M.tex[rec[5]].h == (int)rec[2];
            R.from = (int)rec[5];
        } else {
            R.from = (int)rec[0];
            R.off = ftell(f);
            R.csize = (long)rec[5];
            ok = fseek(f, R.csize, SEEK_CUR) == 0;
        }
        n++;
    }
    if (ok) {
        fseek(f, 0, SEEK_END);
        for (size_t t = 0; t < M.spec.size() && ok; t++)
            if (M.spec[t].from >= 0 && M.spec[M.spec[t].from].csize > 0
                && M.spec[M.spec[t].from].off + M.spec[M.spec[t].from].csize > ftell(f)) ok = false;
    }
    fclose(f);
    if (!ok) {
        fprintf(stderr, "%s: %s could not be read -- %s draws without a specular map\n",
                M.tag.empty() ? "ebld" : M.tag.c_str(), sp.c_str(), M.type.c_str());
        M.spec.assign(M.tex.size(), EbldSpecRec());
        return;
    }
    M.specPath = sp;
    M.hasSpec = n > 0;
}

/* Texture t's specular map as RGBA for the card (red, green, the map's upright sheen, the
   texture's own alpha from `rgba`), or false when it has none. */
static bool ebld_spec_pixels(const EbldModel& M, size_t t, const std::vector<unsigned char>& rgba,
                             std::vector<unsigned char>& out)
{
    if (!M.hasSpec || t >= M.spec.size() || M.spec[t].from < 0) return false;
    const EbldSpecRec& R = M.spec[t];
    const EbldSpecRec& S = M.spec[R.from];
    if (S.csize <= 0) return false;
    const size_t n = (size_t)M.tex[t].w * M.tex[t].h;
    if (rgba.size() < n * 4) return false;
    std::vector<unsigned char> z((size_t)S.csize), rg(n * 2);
    FILE* f = fopen(M.specPath.c_str(), "rb");
    if (!f) return false;
    bool ok = fseek(f, S.off, SEEK_SET) == 0 && fread(z.data(), 1, z.size(), f) == z.size();
    fclose(f);
    uLongf got = (uLongf)rg.size();
    if (!ok || uncompress(rg.data(), &got, z.data(), (uLong)z.size()) != Z_OK || got != rg.size())
        return false;
    out.resize(n * 4);
    for (size_t q = 0; q < n; q++) {
        out[q * 4 + 0] = (unsigned char)((rg[q * 2] * R.scale + 127) / 255);
        out[q * 4 + 1] = rg[q * 2 + 1];
        out[q * 4 + 2] = (unsigned char)((S.sheen * R.scale + 127) / 255);
        out[q * 4 + 3] = rgba[q * 4 + 3];
    }
    return true;
}

static bool ebld_inflate_texture(FILE* file, const EbldTex& T,
                                 std::vector<unsigned char>& rgba,
                                 std::vector<unsigned char>& nod,
                                 std::vector<unsigned char>& mask)
{
    const size_t size = (size_t)T.w * T.h * 4;
    if (T.house & 1) nod.resize(size);
    if (T.house & 2) mask.resize(size / 4);
    unsigned char input[65536], overflow = 0;
    z_stream z = {};
    if (inflateInit(&z) != Z_OK) return false;
    std::vector<unsigned char>* output[3] = { &rgba, &nod, &mask };
    size_t part = 0;
    long remaining = T.csize;
    int result = Z_OK;
    bool ok = true;
    for (;;) {
        if (!z.avail_in && remaining > 0) {
            const size_t n = (size_t)remaining > sizeof input ? sizeof input : (size_t)remaining;
            if (fread(input, 1, n, file) != n) { ok = false; break; }
            remaining -= (long)n; z.next_in = input; z.avail_in = (uInt)n;
        }
        if (!z.avail_out) {
            while (part < 3 && output[part]->empty()) part++;
            if (part < 3) {
                z.next_out = output[part]->data(); z.avail_out = (uInt)output[part]->size(); part++;
            } else { z.next_out = &overflow; z.avail_out = 1; }
        }
        const uLong before = z.total_out;
        const uInt inputBefore = z.avail_in;
        result = inflate(&z, Z_NO_FLUSH);
        const size_t want = rgba.size() + nod.size() + mask.size();
        if (z.total_out > want) { ok = false; break; }
        if (result == Z_STREAM_END) { ok = z.total_out == want; break; }
        if (result != Z_OK || (z.total_out == before && z.avail_in == inputBefore
                               && (!remaining || z.avail_in))) { ok = false; break; }
    }
    inflateEnd(&z);
    return ok;
}
/* TWO RED ALERT STRUCTURES PAINTED DARKER THAN THE REST. The lab's paint pulls every flat
   face toward its building's own reference light, and the Dog Kennel's (0.554: dark planks
   and posts) and the Allied Barracks' olive canvas sit well under the others' (0.65 to
   0.68). In the game the kennel read as a dark lump beside every other building, and the
   tents' shaded walls and roof hollows went nearly black (22 of 255 on the south wall).
   Their paint is lifted as it is loaded, by a gamma under one, so the dark canvas and
   planks come up most and the lit faces only a little; the light, the shadows and the
   normals stay the game's as for every other building. 1 for every other type. */
static float ebld_ra_paint_lift(const std::string& type)
{
    if (!g_raModule) return 1.0f;
    if (type == "KENN") return 0.74f;
    if (type == "TENT") return 0.66f;
    return 1.0f;
}
static void ebld_lift_paint(std::vector<unsigned char>& px, float gamma)
{
    if (gamma == 1.0f || px.empty()) return;
    unsigned char lut[256];
    for (int v = 0; v < 256; v++)
        lut[v] = (unsigned char)lroundf(255.0f * powf((float)v / 255.0f, gamma));
    for (size_t q = 0; q + 3 < px.size(); q += 4) {
        px[q] = lut[px[q]]; px[q + 1] = lut[px[q + 1]]; px[q + 2] = lut[px[q + 2]];
    }
}

static bool ebld_resident(EbldModel& M, bool all = false, size_t maxTextures = (size_t)-1,
                          size_t onlyTexture = (size_t)-1, uint64_t wantedTextures = ~uint64_t(0))
{
    /* a wall mask's stage model draws with its wall's textures */
    if (M.texOwner >= 0 && M.texOwner < (int)g_ebld.size() && &g_ebld[M.texOwner] != &M) {
        EbldModel& O = g_ebld[M.texOwner];
        const bool ok = ebld_resident(O, all, maxTextures, onlyTexture, wantedTextures);
        M.resident = O.resident;
        M.residentAll = O.residentAll;
        if (!O.attached && M.attached) { M.attached = false; g_ebldResolveGen++; }
        return ok;
    }
    if (M.resident && (M.residentAll || !all)) return true;
    if (!M.attached) return false;
    const bool selective = wantedTextures != ~uint64_t(0);
    if (selective) {
        bool ready = true;
        for (size_t texture = 0; texture < M.tex.size() && texture < 64; texture++)
            if ((wantedTextures & (uint64_t(1) << texture))
                && ((size_t)M.tex0 + texture >= g_pack.tex.size()
                    || !g_pack.tex[(size_t)M.tex0 + texture].gl)) {
                ready = false;
                break;
            }
        if (ready) return true;
    }
    const bool first = !M.resident;
    const Uint64 t0 = SDL_GetPerformanceCounter();
    const char* profileSetting = getenv("OPENCNC3D_EBLD_TEXTURE_PROFILE");
    const bool profileTextures = profileSetting && profileSetting[0] && strcmp(profileSetting, "0");
    /* the caller's texture binding is handed back: this can run in the middle of a pass */
    GLint bound = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    FILE* tf = fopen(M.path.c_str(), "rb");
    if (tf) {
        fseek(tf, 0, SEEK_END);
        if (ftell(tf) != M.fileSize) { fclose(tf); tf = NULL; }
    }
    bool readOk = (tf != NULL);
    long bytes = 0;
    int n = 0, shared = 0;
    const size_t tex0 = (size_t)M.tex0;
    std::vector<size_t> done;
    for (size_t t = 0; t < M.tex.size() && readOk && (size_t)n < maxTextures; t++) {
        if (onlyTexture != (size_t)-1 && t != onlyTexture) continue;
        if (selective && (t >= 64 || !(wantedTextures & (uint64_t(1) << t)))) continue;
        /* the whole model's textures first; the cut's, the damage's and the wreck's with
           the first draw of a building that has been hit */
        if (!(M.texIntact[t] ? first : all)) continue;
        if (tex0 + t < g_pack.tex.size() && g_pack.tex[tex0 + t].gl) continue;
        const EbldTex& T = M.tex[t];
        const size_t sz = (size_t)T.w * T.h * 4;
        const Uint64 textureBegin = profileTextures ? SDL_GetPerformanceCounter() : 0;
        /* (a texture another tree's or clump's file already put on the card: shared) */
        uint64_t shareKey = 0;
        if (g_ebldShareOn && T.csize > 0 && T.house == 0 && ebld_share_type(M.type.c_str())
            && M.service.tex.find((int)t) == M.service.tex.end()) {
            std::vector<unsigned char> comp((size_t)T.csize);
            fseek(tf, T.off, SEEK_SET);
            if (fread(comp.data(), 1, comp.size(), tf) != comp.size()) { readOk = false; break; }
            shareKey = ebld_share_key(comp, T, M.version >= 5 && T.role != 6);
            const auto hit = g_ebldShare.find(shareKey);
            if (hit != g_ebldShare.end() && hit->second.w == T.w && hit->second.h == T.h
                && tex0 + t < g_pack.tex.size()) {
                PackTex& pt = g_pack.tex[tex0 + t];
                pt.gl = hit->second.gl;
                pt.mips = hit->second.mips;
                g_ebldShareRefs[pt.gl]++;
                g_ebldShareHits++;
                g_ebldShareSaved += (long)sz * (pt.mips ? 4 : 3) / 3;
                shared++;
                n++;
                continue;
            }
        }
        std::vector<unsigned char> rgba(sz), nod, mask;
        fseek(tf, T.off, SEEK_SET);
        if (T.csize > 0) {
            readOk = ebld_inflate_texture(tf, T, rgba, nod, mask);
        } else {
            readOk = fread(&rgba[0], 1, sz, tf) == sz;
            if (readOk && (T.house & 1)) {
                nod.resize(sz);
                readOk = fread(&nod[0], 1, sz, tf) == sz;
            }
            if (readOk && (T.house & 2)) {
                mask.resize(sz / 4);
                readOk = fread(&mask[0], 1, sz / 4, tf) == sz / 4;
            }
        }
        if (!readOk || tex0 + t >= g_pack.tex.size()) { readOk = false; break; }
        if (T.role != 6) {
            const float lift = ebld_ra_paint_lift(M.type);
            ebld_lift_paint(rgba, lift);
            ebld_lift_paint(nod, lift);
        }
        const Uint64 decodeEnd = profileTextures ? SDL_GetPerformanceCounter() : 0;
        bytes += bservice_upload(M,(int)t,rgba);
        const Uint64 serviceEnd = profileTextures ? SDL_GetPerformanceCounter() : 0;
        PackTex& pt = g_pack.tex[tex0 + t];
        if (!strncmp(M.type.c_str(),"ROCK",4) && !pt.gl_stone)
            pt.gl_stone=stone_map_for_rgba(rgba,T.w,T.h);
        if (!g_raModule && !nod.empty() && !mask.empty())
            livery_faction_load(pt, M.path.c_str(), (unsigned)t, rgba, nod, mask);
        const Uint64 factionEnd = profileTextures ? SDL_GetPerformanceCounter() : 0;
        /* VERSION 5 MIPMAPS every texture but the zone overlays, whose alpha is the
           patch noise the overlay blends by (a mipmap would average it to a flat half) */
        if (g_raModule && !nod.empty()) {
            nod = rgba;
            for (size_t q = 0; q < mask.size(); q++)
                if (mask[q]) livery_recolour(&rgba[q * 4], mask[q], 1, &nod[q * 4]);
        }
        const bool mips = (M.version >= 5 && T.role != 6);
        /* its specular map, made as the texture is (mipmapped with it), before the pixels
           are handed on below */
        if (M.hasSpec && !pt.gl_spec) {
            std::vector<unsigned char> sp;
            if (ebld_spec_pixels(M, t, rgba, sp))
                pt.gl_spec = mips ? ebld_upload_mips(sp, T.w, T.h, false) : n64fix_upload(sp, T.w, T.h);
        }
        const bool reusePixels = !g_raModule && T.bled
                                 && mesh_perf_switch("OPENCNC3D_EBLD_UPLOAD_REUSE");
        pt.mips = mips;
        if (mips) {
            pt.gl = T.cover ? ebld_upload_cover(nod.empty() ? rgba : nod, T.w, T.h, T.bled)
                            : ebld_upload_mips(nod.empty() ? rgba : nod, T.w, T.h, T.bled);
            if (!nod.empty()) {
                pt.gl_gdi = T.cover ? ebld_upload_cover(rgba, T.w, T.h, T.bled)
                                    : ebld_upload_mips(rgba, T.w, T.h, T.bled);
                pt.gdi.swap(rgba);
                pt.mask.swap(mask);
            }
            bytes += (long)sz * (nod.empty() ? 1 : 2) * 4 / 3;
        } else if (nod.empty()) {
            pt.gl = n64fix_upload(rgba, T.w, T.h, reusePixels);
            bytes += (long)sz;
        } else {
            /* HOUSE PAINT: a two-decode texture exactly like a cartridge house texture,
               so draw_mesh's house slot picks it with no help from here. gl is the Nod
               decode (Nod, and every house that is not GDI outside a skirmish), gl_gdi
               the GDI look, and the unbled GDI decode is kept with the mask for the
               skirmish liveries to be painted from. The Nod decode is not kept: a
               masked texture is painted from the GDI look and the mask alone. */
            pt.gl = n64fix_upload(nod, T.w, T.h, reusePixels);
            pt.gl_gdi = n64fix_upload(rgba, T.w, T.h, reusePixels);
            pt.gdi.swap(rgba);
            pt.mask.swap(mask);
            bytes += 2 * (long)sz;
        }
        if (shareKey && pt.gl) {
            EbldShared S;
            S.gl = pt.gl; S.w = T.w; S.h = T.h; S.mips = pt.mips;
            g_ebldShare[shareKey] = S;
            g_ebldShareRefs[pt.gl]++;
        }
        done.push_back(tex0 + t);
        n++;
        if (profileTextures) {
            const Uint64 uploadEnd = SDL_GetPerformanceCounter();
            const double microseconds = 1000000.0 / (double)SDL_GetPerformanceFrequency();
            fprintf(stderr, "EBLDTEXPROFILE|type=%s|texture=%zu|role=%d|width=%d|height=%d"
                            "|compressed=%ld|bled=%d|cover=%d|prepare=%d|reuse=%d"
                            "|decode_us=%.3f|service_us=%.3f|faction_us=%.3f|upload_us=%.3f\n",
                    M.type.c_str(), t, T.role, T.w, T.h, T.csize, T.bled ? 1 : 0,
                    T.cover ? 1 : 0, maxTextures != (size_t)-1 ? 1 : 0, reusePixels ? 1 : 0,
                    (decodeEnd-textureBegin)*microseconds, (serviceEnd-decodeEnd)*microseconds,
                    (factionEnd-serviceEnd)*microseconds, (uploadEnd-factionEnd)*microseconds);
        }
    }
    if (tf) fclose(tf);
    glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
    if (!readOk) {
        fprintf(stderr, "%s: %s changed, went away or could not be read back -- %s is "
                        "drawn by the cartridge from here on\n",
                M.tag.empty() ? "ebld" : M.tag.c_str(), M.path.c_str(), M.type.c_str());
        M.attached = false; g_ebldResolveGen++;
        if (maxTextures == (size_t)-1) g_ebldPrepareMetrics.demandFailures++;
        return false;
    }
    /* A bounded preparation call may stop after one texture. Only publish whole-model
       readiness once all corresponding slots exist; an early draw finishes the missing
       textures through the ordinary demand path. */
    M.resident = M.residentAll = true;
    for (size_t t = 0; t < M.tex.size(); t++)
        if (tex0 + t >= g_pack.tex.size() || !g_pack.tex[tex0 + t].gl) {
            M.residentAll = false;
            if (M.texIntact[t]) M.resident = false;
        }
    /* the liveries of exactly the textures just uploaded: a texture's are made once */
    const Uint64 liveryBegin = profileTextures ? SDL_GetPerformanceCounter() : 0;
    for (size_t i = 0; i < done.size(); i++) livery_build_range(done[i], done[i] + 1);
    if (profileTextures && !done.empty()) {
        const double liveryUs = (SDL_GetPerformanceCounter()-liveryBegin)*1000000.0
                               / (double)SDL_GetPerformanceFrequency();
        fprintf(stderr, "EBLDTEXPROFILE|type=%s|textures=%zu|prepare=%d|livery_us=%.3f\n",
                M.type.c_str(), done.size(), maxTextures != (size_t)-1 ? 1 : 0, liveryUs);
    }
    glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
    g_ebldResidentBytes += bytes;
    if (n && perf_trace_frame_active()) {
        g_destructTrace[17]++;
        g_destructTrace[18] += (Uint64)bytes;
        g_destructTrace[20] += (SDL_GetPerformanceCounter()-t0)*1000000u/SDL_GetPerformanceFrequency();
    }
    if (n && maxTextures == (size_t)-1) {
        g_ebldPrepareMetrics.demandTextures += (unsigned)n;
        g_ebldPrepareMetrics.demandTextureUs += (SDL_GetPerformanceCounter() - t0)
                                               * 1000000u / SDL_GetPerformanceFrequency();
    }
    if (n)
        fprintf(stderr, "%s: %s textures on the card%s, %d textures, %.1f MB (%.1f MB in "
                "all), %.1f ms%s\n", M.tag.empty() ? "ebld" : M.tag.c_str(), M.type.c_str(),
                first ? "" : " (its damage)", n,
                (double)bytes / 1048576.0, (double)g_ebldResidentBytes / 1048576.0,
                (double)(SDL_GetPerformanceCounter() - t0) * 1000.0
                    / (double)SDL_GetPerformanceFrequency(),
                shared ? (" (" + std::to_string(shared) + " of them already on the card from "
                          "another tree's file, shared)").c_str() : "");
    return true;
}

static bool ebld_resident_stage(EbldModel& model, int stage)
{
    if (!ebld_stage_residency_enabled(model)) return ebld_resident(model, true);
    return ebld_resident(model, true, (size_t)-1, (size_t)-1,
                         ebld_texture_mask_for_stage(model, stage));
}

/* A TYPE READ AT FIRST SIGHT (ebld_type_lazy): the first time the game asks for it, its
   file (the plain one, or this theater's look) is read and attached to the pack now
   loaded, wall masks and all. Each file is tried once a process. True: something was
   read and attached. */
static bool ebld_ensure(const char* type, size_t maxSourceBytes)
{
    if (!type || !*type || !ebld_type_lazy(type)) return false;
    const int th = ebld_pack_theater();
    bool any = false;
    /* this theater's own look first; the plain file only where there is none */
    for (int pass = 0; pass < 2; pass++)
    for (size_t i = 0; i < g_ebldFiles.size(); i++) {
        EbldFile& F = g_ebldFiles[i];
        if (F.tried || !F.lazy || strcmp(EBLD_TYPES[F.ti], type)) continue;
        if (pass == 0 ? (F.th < 0 || F.th != th) : F.th >= 0) continue;
        if (pass == 1) {
            bool own = false;
            for (size_t mI = 0; mI < g_ebld.size() && !own; mI++)
                own = g_ebld[mI].type == type && g_ebld[mI].theater == th && th >= 0;
            if (own) continue;
        }
        if (!g_raModule && !ebld_packless_type(type)
            && g_pack.type.find(type) == g_pack.type.end()) continue;
        F.tried = true;
        const Uint64 t0 = SDL_GetPerformanceCounter();
        const size_t m0 = g_ebld.size();
        if (!ebld_read_file(F, maxSourceBytes)) {
            if (maxSourceBytes) return false;
            continue;
        }
        /* (a theater's look stands in for the plain file, which stays detached) */
        std::set<std::string> ownLook;
        for (size_t mI = 0; mI < g_ebld.size(); mI++)
            if (g_ebld[mI].theater >= 0 && g_ebld[mI].theater == th)
                ownLook.insert(g_ebld[mI].type);
        for (size_t mI = 0; mI < m0; mI++)
            if (g_ebld[mI].type == type && g_ebld[mI].theater < 0 && ownLook.count(type))
                g_ebld[mI].attached = false;
        for (size_t mI = m0; mI < g_ebld.size(); mI++) ebld_attach_one(mI, th, ownLook);
        g_ebldPackSerial++;
        any = true;
        fprintf(stderr, "ebld: %s read at first sight and attached to %s, %.1f ms\n",
                F.path.c_str(), g_pack.scen,
                (double)(SDL_GetPerformanceCounter() - t0) * 1000.0
                    / (double)SDL_GetPerformanceFrequency());
    }
    return any;
}

/* ---- the rule ---------------------------------------------------------------------- */
static bool ebld_rule_now(void) { return g_ebldOn && g_fx.enabled; }

/* Whether the pack meshes the whole model draws from are in the current pack: the intact
   mesh and the scorch (if it has one). The cut's are made on the first hit
   (ebld_cut_ok). */
static bool ebld_meshes_ok(const EbldModel& M)
{
    if (!M.attached) return false;
    for (int k = 0; k < EBM_N; k++) {
        const bool need = k == EBM_INTACT
                        || (k == EBM_SCORCH && M.param[EBP_SCORCH_TEX] >= 0.0f);
        if (!need) continue;
        if (M.mesh[k] < 0 || M.mesh[k] >= (int)g_pack.mesh.size()) return false;
    }
    return true;
}

/* Whether the cut's meshes (and the wreck's, where there is a DESTROYED set) are made and
   in the current pack. */
static bool ebld_cut_ok(const EbldModel& M)
{
    if (M.nset < 2) return true;
    if (!M.cutBuilt) return false;
    const int last = (M.nset == EBS_N) ? EBM_GLOWX : EBM_OVER0 + 7;
    for (int k = EBM_CUT; k <= last; k++)
        if (M.mesh[k] < 0 || M.mesh[k] >= (int)g_pack.mesh.size()) return false;
    return true;
}

/* The building model standing in for this type in the current pack, or -1. A wall's
   model is never a building's. */
static int ebld_model_of_scan(const char* type)
{
    type = ebld_alias(type);
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < g_ebld.size(); i++) {
            const EbldModel& M = g_ebld[i];
            if (!M.attached || M.type != type || (M.flags & EBX_WALL) || M.wallMask >= 0)
                continue;
            return ebld_meshes_ok(M) ? (int)i : -1;
        }
        /* a type read at first sight: now */
        if (pass || !ebld_rule_now() || !ebld_ensure(type)) break;
    }
    return -1;
}
/* AND ASKED ONCE PER TYPE PER FRAME. Every pass asks again for every building, tree and
   rock it draws, two to four times an object through the readiness checks, and each ask
   walks the whole model list comparing names. The answer is kept for the frame under the
   type's name, and the whole memo is dropped when anything it reads could have moved: a
   new frame, a model read or attached (the list's size, the pack serial, the resolve
   generation, which the detaching paths bump), a pack's meshes, types or files, the rule
   or the module. The first ask of a frame always walks, so a type read at first sight is
   read exactly when it was. OPENCNC3D_RESOLVE_MEMO=0 walks every time;
   OPENCNC3D_RESOLVE_VERIFY=1 walks anyway and stops the program if the two disagree. */
static unsigned long g_ebldResolveHits = 0, g_ebldResolveScans = 0;
static int ebld_model_of(const char* type)
{
    static const bool memo = mesh_perf_switch("OPENCNC3D_RESOLVE_MEMO");
    static const bool verify = [](){ const char* v = getenv("OPENCNC3D_RESOLVE_VERIFY"); return v && !strcmp(v, "1"); }();
    if (!memo || !type) { g_ebldResolveScans++; return ebld_model_of_scan(type); }
    size_t n = 0;
    while (n < 16 && type[n]) n++;
    if (n >= 16) { g_ebldResolveScans++; return ebld_model_of_scan(type); }
    struct Sig {
        unsigned frame, gen; int serial; size_t models, meshes, types, files; bool rule, ra; char theater[sizeof g_pack.theater];
        bool operator==(const Sig& o) const {
            return frame == o.frame && gen == o.gen && serial == o.serial && models == o.models
                && meshes == o.meshes && types == o.types && files == o.files && rule == o.rule && ra == o.ra
                && !memcmp(theater, o.theater, sizeof theater);
        }
    };
    /* (the theater picks a type's own look and its aliases) */
    const auto signature = [](void) {
        Sig s = { g_drawFrameSerial, g_ebldResolveGen, g_ebldPackSerial, g_ebld.size(), g_pack.mesh.size(),
                  g_pack.type.size(), g_ebldFiles.size(), ebld_rule_now(), g_raModule != 0, {0} };
        strncpy(s.theater, g_pack.theater, sizeof s.theater);
        return s;
    };
    struct Slot { char name[16]; int model; bool used; };
    enum { SLOTS = 128 };
    static Slot slots[SLOTS];
    static Sig held = {};
    static bool heldValid = false;
    const Sig now = signature();
    if (!heldValid || !(now == held)) {
        for (int k = 0; k < SLOTS; k++) slots[k].used = false;
        held = now; heldValid = true;
    }
    uint32_t h = 2166136261u;
    for (size_t k = 0; k < n; k++) h = (h ^ (unsigned char)type[k]) * 16777619u;
    for (int probe = 0, at = (int)(h & (SLOTS - 1)); probe < SLOTS; probe++, at = (at + 1) & (SLOTS - 1)) {
        Slot& S = slots[at];
        if (S.used) {
            if (memcmp(S.name, type, n + 1)) continue;
            g_ebldResolveHits++;
            if (verify) {
                const int fresh = ebld_model_of_scan(type);
                if (fresh != S.model) {
                    fprintf(stderr, "RESOLVE|ebld_model_of|MISMATCH|type=%s|memo=%d|scan=%d\n", type, S.model, fresh);
                    fflush(stderr); abort();
                }
            }
            return S.model;
        }
        g_ebldResolveScans++;
        const int model = ebld_model_of_scan(type);
        /* a walk that read or attached a model has moved what the memo was keyed on */
        const Sig after = signature();
        if (after == held) { memcpy(S.name, type, n + 1); S.model = model; S.used = true; }
        return model;
    }
    g_ebldResolveScans++;
    return ebld_model_of_scan(type);
}

/* RED ALERT'S CIVILIAN BUILDINGS ARE TIBERIAN DAWN'S. The houses, farms and the oil pump
   (V01 to V19), the hospital, the bio lab and the technology center are the same 1995
   pictures in both games, and the Red Alert lab reuses OpenCNC3D's approved remasters of
   them (tools/ra3d/stage-reuse.py copies the files into ra3d/ebld). Those files carry all
   three stages, and under Red Alert they play them exactly as Tiberian Dawn does: worn
   chip by chip, broken at half strength, blown apart and sunk over their scorch when they
   die (ebld_step, ebld_draw). Red Alert's own structures do the same where their files
   carry their stages (ebld_ra_staged); an intact-only one keeps its 1996 states. */
static bool ebld_ra_civil(const char* t)
{
    if (!t) return false;
    if (t[0] == 'V' && t[1] >= '0' && t[1] <= '9' && t[2] >= '0' && t[2] <= '9' && !t[3])
        return true;
    return !strcmp(t, "BIO") || !strcmp(t, "HOSP") || !strcmp(t, "MISS");
}
static int etree_stage_model(const SimObject& o, int k);
static const EbldBld* ebld_find(const SimObject& o);
/* RED ALERT'S OWN STRUCTURES WITH THEIR STAGES. A Red Alert structure's file is either the
   approved standing model alone (intact only: it stands whole at every strength, and its
   1996 states play the rest) or the model with its stages, baked from the Red Alert lab as
   Tiberian Dawn's structures are (tools/ra3d/bake-structures.py). A staged file plays them
   exactly as a reused civilian building does: worn chip by chip from the side it is hit
   on, broken at half strength, blown apart and sunk over its scorch when it dies, its own
   emitters burning and smoking in their stage. */
static bool ebld_ra_staged(const SimObject& o)
{
    if (!g_raModule || o.kind != K_BUILDING || ebld_ra_civil(o.type)) return false;
    const int m = ebld_model_of(o.type);
    return m >= 0 && g_ebld[m].nset >= 2;
}
/* Keep the approved standing model through damage until a reviewed damage model is
   available. Construction, sale and an open factory door use the engine animation, unless
   the model's placement or its own door plays them. A civilian building's model and a
   staged structure's (ebld_ra_staged) are the building's at every strength; a reused tree
   (ebld_terr.h) is drawn whole, burning or as its charred snag, while its type has the
   stage. */
static bool eplc_owns(const SimObject& o);
static bool eplc_has_type(const char* type);
static bool ra3d_prop_claims(const SimObject& o);   /* (ra3d_props_mod.h) */
static bool ra3d_ore_mine_claims(const SimObject& o);   /* (ra3d_ore_mod.h) */
static bool ra3d_props_on(void);
static float ra3d_prop_phase(const SimObject& o, const EbldClip& C);
static bool ra3d_mine_hidden(const SimObject& o);
static bool ebld_ra_ready(const SimObject& o)
{
    if (!g_raModule || !ebld_rule_now() || g_fx.dos_classic) return false;
    /* A STRUCTURE ITS PLACEMENT OWNS (ebld_place.h: Red Alert's Construction Yard deploying
       out of its MCV, a structure built from the sidebar, or one being sold) is its
       remastered model's while it goes up, as Tiberian Dawn's are: its 1996 build-up card
       stands down and the placement draws it */
    if (o.kind == K_BUILDING && !o.limbo && o.str > 0 && eplc_owns(o)) {
        const int m = ebld_model_of(o.type);
        if (m < 0 || !ebld_resident(g_ebld[m])) return false;
        livery_build_range(g_ebld[m].tex0, g_ebld[m].tex0 + g_ebld[m].tex.size());
        return true;
    }
    if (o.kind == K_TERRAIN) {
        /* (Red Alert's own props stood as terrain objects: the crate stacks, the ice floes,
           and the cell decorations drawn through here, ra3d_props_mod.h) */
        if (ra3d_prop_claims(o)) return true;
        /* (the ore mine, ra3d_ore_mod.h) */
        if (ra3d_ore_mine_claims(o)) return true;
        if (!eterr_tree_claims(o) || (o.str <= 0 && etree_stage_model(o, 2) < 0)) return false;
        const int m = ebld_model_of(o.type);
        return m >= 0 && ebld_resident(g_ebld[m]);
    }
    if (o.kind != K_BUILDING
        || o.str <= 0 || building_constructing(o)
        || construction_frac(o) < 1.0f || o.limbo)
        return false;
    /* (a mine another side laid: the 1996 engine never shows it to the player) */
    if (ra3d_mine_hidden(o)) return false;
    /* (Red Alert's props, the barrels and the mines, off for an A/B: ra3d_props_mod.h) */
    if (!ra3d_props_on() && (!strcmp(o.type, "BARL") || !strcmp(o.type, "BRL3")
                             || !strcmp(o.type, "MINV") || !strcmp(o.type, "MINP")))
        return false;
    /* (sold: the 1996 card plays the sale backwards, unless the type's placement does, which
       takes it over on the tick the engine starts it; until then the model stands) */
    if (!strcmp(o.mission, "Selling") && !eplc_has_type(o.type)) return false;
    const int m = ebld_model_of(o.type);
    if (m < 0 || !ebld_resident(g_ebld[m])) return false;
    /* THE WAR FACTORY'S DOOR: the 1996 card's while it is open, unless the model's own door
       clip opens with it (RA_DRIVE) */
    if ((!strcmp(o.type, "WEAP") || !strcmp(o.type, "WEAF")) && o.doorstage > 0
        && !(g_ebld[m].raDrive >= 0 && g_ebld[m].clipIntact >= 0))
        return false;
    livery_build_range(g_ebld[m].tex0, g_ebld[m].tex0 + g_ebld[m].tex.size());
    return true;
}

/* ---- Red Alert's driven clips ------------------------------------------------------------
   THE MOTION THE 1996 GAME DRAWS FROM A BUILDING'S STATE, played on the model's own clip.
   Red Alert's engine runs each structure's moving picture from its BState (bdata.cpp's
   _anims rows: the Missile Silo's cover opening, held and closing, the Tesla Coil's charge,
   the Construction Yard's crane while it builds) or from its door (the War Factory's, its
   Door_Stage), and the dump reports both (doing=, dostage=, door=). A model whose type has a
   row here has its intact clip (remaster.json `anim`) played by that state instead of on
   its own clock, and its cut's clip (the damaged model's, frame for frame the intact one's)
   with it. A model without a clip keeps its 1996 state where the row says so (the War
   Factory's open door is its card) and is otherwise still.

   Four kinds:
     DOOR   out as far as the engine's door stage says (0 shut to stages-1 open)
     RAMP   out while the engine's state is one of `on`, back otherwise
     STAGE  out as far as its anim's stage says while the state is `on[0]`, back otherwise
     CYCLE  loops while the state is `on[0]`, and finishes its loop to rest when it ends
     CHARGE the Tesla Coil's charge, which BState does not carry: Charging_AI counts the
            stage 0 to 9 at rate 3 while it charges and stops it on 9 once charged, when
            Shape_Number draws the held frame 3 instead; 0 when it lets go
     FIRED  plays once from the tick the player fires the special it serves (`mission` names
            it), at the 1996 anim's rate (`out` ticks a frame): the 1996 game never starts
            the Iron Curtain's ACTIVE anim, so its sphere is played when the curtain is used,
            a choice of ours
     ANIM   follows an anim of the engine's own laid on the building (the Allied Tech
            Center's launch doors are SPUTDOOR's frames): its stage, scaled to the clip, and
            the anim's 1996 card stands down while the model plays it (ra3d_anim_card)
   The first three move at the 1996 anim's own speed (ticks out, ticks back), eased between
   ticks; where the clip lies is read from the clip itself (ebld_ra_drive_prepare): the
   frames before anything moves, the frames it is fully out, and, if it comes back by
   itself (an open-held-shut clip), the frames of its return.
   A CLIP OF THE 1996 PICTURES instead (the Red Alert lab's clip_rm.py: one frame per frame of
   the building's own 1996 animation, as many as its _anims rows draw, `sprites`) shows the
   very frame the 1996 game draws: the building's stage, which the engine keeps as the frame
   number itself (BuildingClass::Shape_Number; the War Factory's door: the door stage), frame 0
   at rest. Which of the two a file is,
   is told by its frame count. */
enum { ERD_DOOR = 1, ERD_RAMP, ERD_STAGE, ERD_CYCLE, ERD_ANIM, ERD_CHARGE, ERD_FIRED };
struct EbldRaDrive {
    const char* type;
    int kind;
    int on[2];            /* the BStates it is out in (-1 none); RAMP: also by mission */
    const char* mission;  /* RAMP: out while the building is on this mission */
    int out, back;        /* ticks from rest to fully out, and back */
    /* THE 1996 PICTURES: how many frames the building's own animation has (its _anims rows,
       0 none) */
    int sprites;
    const char* anim;     /* ANIM: the engine's anim it follows */
    /* A FLASH, not a motion: a glow or a light that the 1996 game switches frame by frame,
       which a clip of the 1996 pictures shows frame for frame. A motion (a door, a cover,
       an arm, a crane) is always played by time, one clip frame after another and blended
       between them as the picture is drawn, over the 1996 motion's own duration. */
    bool flash;
};
/* BSTATE (redalert/defines.h): 1 IDLE, 2 ACTIVE, 3 FULL, 4 AUX1, 5 AUX2 */
static const EbldRaDrive RA_DRIVE[] = {
    /* War Factory: its door, Open_Door(8, 5) in BuildingClass::Mission_Unload: the stage runs
       0 to 3 at eight ticks, 3 standing open; its 1996 door overlay (WEAP2) draws the stage
       itself, frame for frame (a 5-frame clip: frame n the stage n) */
    { "WEAP", ERD_DOOR,  { -1, -1 }, NULL, 32, 32, 5, NULL, false },
    /* Missile Silo: the cover opens (ACTIVE, 5 stages at rate 2), is held (AUX1) through
       the launch, and closes (AUX2, 3 stages at rate 2) */
    { "MSLO", ERD_RAMP,  {  2,  4 }, NULL, 10, 6, 8, NULL, false },
    /* Allied Tech Center: its launch door, for the GPS satellite (Mission_Missile; the 1996
       game draws it as an anim of its own, SPUTDOOR, of 35 frames, over the building) */
    { "ATEK", ERD_ANIM,  { -1, -1 }, NULL, 0, 0, 35, "SPUTDOOR", false },
    /* Tesla Coil: its charge (Charging_AI: 10 stages at rate 3, frame 3 held charged) */
    { "TSLA", ERD_CHARGE, { -1, -1 }, NULL, 3, 3, 10, NULL, true },
    /* Iron Curtain: the sphere (its 11-frame ACTIVE row at rate 3, played when it is used) */
    { "IRON", ERD_FIRED, { -1, -1 }, "SW_IronCurtain", 3, 3, 11, NULL, true },
    /* Chronosphere: the portal (IDLE 4 stages, ACTIVE 16 stages at rate 3 after them) */
    { "PDOX", ERD_STAGE, {  2, -1 }, NULL, 45, 15, 20, NULL, true },
    /* Camo Pillbox: its hatch and slits (ACTIVE, 2 stages at rate 1) */
    { "HBOX", ERD_STAGE, {  2, -1 }, NULL, 2, 4, 2, NULL, false },
    /* Construction Yard: the crane while it builds (ACTIVE, 26 stages at rate 3: one turn
       of the loop is 78 ticks, `out`) */
    { "FACT", ERD_CYCLE, {  2, -1 }, NULL, 78, 0, 26, NULL, false },
    /* Service Depot: while it repairs (ACTIVE, 7 stages at rate 2: 14 ticks a turn) */
    { "FIX",  ERD_CYCLE, {  2, -1 }, NULL, 14, 0, 7, NULL, false },
    /* Airfield: its beacon (IDLE and AUX1, 8 stages at rate 3: bdata.cpp's later IDLE row
       is the one Init_Anim keeps) */
    { "AFLD", ERD_CYCLE, {  4, -1 }, NULL, 0, 0, 8, NULL, true },
};
/* Whether a model's driven clip is a motion played by time (not a flash). */
static bool ebld_ra_motion(const EbldModel& M)
{
    return g_raModule && M.raDrive >= 0 && M.clipIntact >= 0 && !RA_DRIVE[M.raDrive].flash;
}
static int ebld_ra_drive_row(const char* type)
{
    for (size_t i = 0; i < sizeof RA_DRIVE / sizeof RA_DRIVE[0]; i++)
        if (!strcmp(RA_DRIVE[i].type, type)) return (int)i;
    return -1;
}
/* How far frame f of the intact clip has moved its parts from frame 0 (pack units and
   matrix terms alike: a turn of a part far from its pivot is a long way). */
static float ebld_ra_drive_dist(const EbldClip& C, int f)
{
    float d = 0.0f;
    for (int p = 0; p < C.nparts; p++) {
        const float* a = &C.mat[((size_t)f * C.nparts + p) * 12];
        const float* z = &C.mat[(size_t)p * 12];
        for (int k = 0; k < 12; k++) {
            const float e = fabsf(a[k] - z[k]) * ((k & 3) == 3 ? 1.0f : 64.0f);
            if (e > d) d = e;
        }
    }
    return d;
}
/* At load, under Red Alert: whether the model's type is driven, and where in its clip the
   motion lies. A driven clip never runs on its own clock, whatever the lab played it as. */
static void ebld_ra_drive_prepare(EbldModel& m)
{
    m.raDrive = -1;
    if (m.clipIntact < 0 || m.stageOf >= 0) return;
    const int r = ebld_ra_drive_row(m.type.c_str());
    if (r < 0) return;
    m.raDrive = r;
    for (size_t c = 0; c < m.clip.size(); c++) m.clip[c].idle = false;
    const EbldClip& C = m.clip[m.clipIntact];
    const int n = C.frames;
    m.drvSeg[0] = 0; m.drvSeg[1] = n - 1; m.drvSeg[2] = n - 1; m.drvSeg[3] = -1;
    m.drvSprite = RA_DRIVE[r].flash && RA_DRIVE[r].sprites > 0 && n == RA_DRIVE[r].sprites;
    if (m.drvSprite || RA_DRIVE[r].kind == ERD_CYCLE || RA_DRIVE[r].kind >= ERD_ANIM || n < 2) return;
    float top = 0.0f;
    std::vector<float> d((size_t)n);
    for (int f = 0; f < n; f++) { d[(size_t)f] = ebld_ra_drive_dist(C, f); if (d[(size_t)f] > top) top = d[(size_t)f]; }
    if (!(top > 0.0f)) return;
    const float still = top * 0.002f, full = top * 0.998f;
    int first = 0;
    while (first < n && d[(size_t)first] <= still) first++;
    m.drvSeg[0] = first > 0 ? first - 1 : 0;
    int s1 = m.drvSeg[0];
    while (s1 < n - 1 && d[(size_t)s1] < full) s1++;
    m.drvSeg[1] = s1;
    int s2 = s1;
    while (s2 + 1 < n && d[(size_t)s2 + 1] >= full) s2++;
    m.drvSeg[2] = s2;
    int s3 = s2;
    while (s3 + 1 < n && d[(size_t)s3] > still) s3++;
    m.drvSeg[3] = (d[(size_t)s3] <= still && s3 > s2) ? s3 : -1;
    /* (a clip whose last frames come back toward rest without reaching it, as a clip of the
       1996 pictures does: its return runs on into frame 0, written as frame n) */
    if (m.drvSeg[3] < 0 && s2 < n - 1 && d[(size_t)n - 1] < d[(size_t)s2]) m.drvSeg[3] = n;
}
/* ONE ENGINE TICK of a driven clip (ebld_step), from the building's state this tick. */
static void ebld_ra_drive_step(EbldBld& E, const EbldModel& M, const SimObject& o)
{
    E.drvPrev = E.drvCur;
    E.cycPrev = E.cycCur;
    if (M.raDrive < 0 || M.clipIntact < 0) return;
    const EbldRaDrive& R = RA_DRIVE[M.raDrive];
    const bool alive = o.str > 0;
    if (R.kind == ERD_ANIM) {
        /* the anim's stage, where one of its name stands on the building's plot */
        float f = 0.0f;
        const float hx = 0.5f * (float)(o.fw > 0 ? o.fw : 1) + 0.5f;
        const float hz = 0.5f * (float)(o.fh > 0 ? o.fh : 1) + 0.5f;
        for (size_t i = 0; alive && i < g_efxAnims.size(); i++) {
            const EfxAnim& an = g_efxAnims[i];
            if (strcmp(an.name, R.anim) || fabsf(an.wx - o.ex) > hx || fabsf(an.wz - o.ez) > hz)
                continue;
            f = (float)(an.stage > 0 ? an.stage : 0);
            break;
        }
        const int n = M.clip[M.clipIntact].frames;
        if (!M.drvSprite && R.sprites > 1) f = f * (float)(n - 1) / (float)(R.sprites - 1);
        E.cycCur = f > (float)(n - 1) ? (float)(n - 1) : f;
        E.drvCur = E.cycCur > 0.0f ? 1.0f : 0.0f;
        return;
    }
    if (R.kind == ERD_CHARGE || R.kind == ERD_FIRED) {
        const int n = M.clip[M.clipIntact].frames, last = R.sprites > 0 ? R.sprites - 1 : n - 1;
        int f = 0;
        if (alive && R.kind == ERD_CHARGE) {
            f = o.dostage >= last ? 3 : (o.dostage > 0 ? o.dostage : 0);
        } else if (alive && R.mission && !strcmp(g_sbSuperFiredName, R.mission)
                   && g_playerHouse[0] && !strcmp(o.house, g_playerHouse)) {
            /* (the tick the fire was first seen here) */
            static int seen = 0, at = -1000000;
            if (seen != g_sbSuperFiredSerial) { seen = g_sbSuperFiredSerial; at = g_engineFrame; }
            const int k = (g_engineFrame - at) / (R.out > 0 ? R.out : 1);
            f = (k >= 0 && k <= last) ? k : 0;
        }
        /* (a clip of another length: the same moment, scaled onto it) */
        float ff = (float)f;
        if (!M.drvSprite && last > 0) ff = ff * (float)(n - 1) / (float)last;
        E.cycCur = ff > (float)(n - 1) ? (float)(n - 1) : ff;
        E.drvCur = E.cycCur > 0.0f ? 1.0f : 0.0f;
        return;
    }
    if (M.drvSprite) {
        /* the frame the 1996 game draws this tick */
        int f = 0;
        if (alive && R.kind == ERD_DOOR) {
            f = o.doorstage > 0 ? o.doorstage : 0;
        } else if (alive && o.doing >= 1 && o.dostage > 0) {
            /* (the stage IS the picture: Begin_Mode and AI set it to the row's first frame,
               and Shape_Number draws Fetch_Stage() as it is) */
            f = o.dostage;
        }
        const int top = M.clip[M.clipIntact].frames - 1;
        E.cycCur = (float)(f > top ? top : f);
        E.drvCur = E.cycCur > 0.0f ? 1.0f : 0.0f;
        return;
    }
    if (R.kind == ERD_CYCLE) {
        const EbldClip& C = M.clip[M.clipIntact];
        const bool on = alive && o.doing == R.on[0];
        if (on || E.cycCur > 0.0f) {
            /* (one turn of the clip over the 1996 loop's own ticks, `out`; or at the clip's
               own rate) */
            E.cycCur += R.out > 0 ? (float)C.frames / (float)R.out : C.fps / 15.0f;
            if (E.cycCur >= (float)C.frames) E.cycCur = on ? fmodf(E.cycCur, (float)C.frames) : 0.0f;
        }
        return;
    }
    float want = 0.0f;
    if (alive) {
        if (R.kind == ERD_DOOR) {
            /* (DoorClass runs its stage 0 to DOOR_STAGES - 2: the War Factory's opens 0, 1, 2
               and stands open at 3) */
            const int top = 3;
            want = o.doorstage <= 0 ? 0.0f : (float)(o.doorstage > top ? top : o.doorstage) / (float)top;
        } else if (R.kind == ERD_RAMP) {
            want = ((R.on[0] >= 0 && o.doing == R.on[0]) || (R.on[1] >= 0 && o.doing == R.on[1])
                    || (R.mission && !strcmp(o.mission, R.mission))) ? 1.0f : 0.0f;
        } else if (o.doing == R.on[0] && o.dostage >= 0) {
            want = 1.0f;
        }
    }
    if (want > E.drvCur) {
        E.drvCur += 1.0f / (float)(R.out > 0 ? R.out : 1);
        if (E.drvCur > want) E.drvCur = want;
        E.drvClosing = false;
    } else if (want < E.drvCur) {
        E.drvCur -= 1.0f / (float)(R.back > 0 ? R.back : 1);
        if (E.drvCur < want) E.drvCur = want;
        E.drvClosing = true;
    }
    if (E.drvCur <= 0.0f) E.drvClosing = false;
}
/* The intact clip's frame a driven building shows, `alpha` of the way from the last tick
   to this one (the draw's g_tickAlpha; 1 on the tick itself). */
static float ebld_ra_drive_frame(const EbldBld& E, const EbldModel& M, float alpha)
{
    if (M.raDrive < 0 || M.clipIntact < 0) return -1.0f;
    const EbldClip& C = M.clip[M.clipIntact];
    if (M.drvSprite || RA_DRIVE[M.raDrive].kind >= ERD_ANIM) return E.cycCur;
    if (RA_DRIVE[M.raDrive].kind == ERD_CYCLE) {
        float d = E.cycCur - E.cycPrev;
        if (d < 0.0f) d = E.cycCur > 0.0f ? d + (float)C.frames : (float)C.frames - E.cycPrev;
        const float f = E.cycPrev + d * alpha;
        return f >= (float)C.frames ? (E.cycCur > 0.0f ? fmodf(f, (float)C.frames) : 0.0f) : f;
    }
    float k = E.drvPrev + (E.drvCur - E.drvPrev) * alpha;
    k = k < 0.0f ? 0.0f : (k > 1.0f ? 1.0f : k);
    if (E.drvClosing && M.drvSeg[3] >= 0)
        return (float)M.drvSeg[2] + (1.0f - k) * (float)(M.drvSeg[3] - M.drvSeg[2]);
    return (float)M.drvSeg[0] + k * (float)(M.drvSeg[1] - M.drvSeg[0]);
}

/* Whether an engine anim is one a structure's model plays itself (an ANIM row of RA_DRIVE whose
   model carries the clip, standing on the building's plot): its 1996 card stands down. */
static bool ebld_ra_anim_played(const EfxAnim& an)
{
    if (!g_raModule || !ebld_rule_now() || g_fx.dos_classic) return false;
    for (size_t r = 0; r < sizeof RA_DRIVE / sizeof RA_DRIVE[0]; r++) {
        if (RA_DRIVE[r].kind != ERD_ANIM || strcmp(an.name, RA_DRIVE[r].anim)) continue;
        for (size_t i = 0; i < g_objects.size(); i++) {
            const SimObject& o = g_objects[i];
            if (o.kind != K_BUILDING || strcmp(ebld_alias(o.type), RA_DRIVE[r].type)) continue;
            const float hx = 0.5f * (float)(o.fw > 0 ? o.fw : 1) + 0.5f;
            const float hz = 0.5f * (float)(o.fh > 0 ? o.fh : 1) + 0.5f;
            if (fabsf(an.wx - o.ex) > hx || fabsf(an.wz - o.ez) > hz || !ebld_ra_ready(o)) continue;
            const int m = ebld_model_of(o.type);
            if (m >= 0 && g_ebld[m].raDrive == (int)r && g_ebld[m].clipIntact >= 0) return true;
        }
    }
    return false;
}

/* THE FRAME OF A MODEL'S CLIP UNDER RED ALERT. A reused civilian model: Tiberian Dawn reads
   a clip that follows the cartridge's (the Oil Pump's linkage) off the cartridge model's
   own clock, object_anim_t; Red Alert's scenes have no cartridge model to read. The same
   arm turns the engine's own idle stage into that clip's frame (structure_anim_frame), and
   Red Alert's bdata.cpp runs the pump's idle as Tiberian Dawn's does (STRUCT_PUMP,
   BSTATE_IDLE, 14 stages at rate 4), so its walking beam keeps the 1996 sprite's pace.
   A Red Alert structure's driven clip: the frame its state has it at (RA_DRIVE), its rest
   before it has an entry. -1 for a model with no such clip (an idle clip plays on the
   engine clock). */
static float ebld_ra_clip_at(const EbldModel& M, const SimObject& o)
{
    if (M.clipIntact < 0) return -1.0f;
    if (M.raDrive >= 0) {
        const EbldBld* E = ebld_find(o);
        return E ? ebld_ra_drive_frame(*E, M, g_tickAlpha) : (float)M.drvSeg[0];
    }
    if (!ebld_ra_civil(o.type)) return -1.0f;
    const EbldClip& C = M.clip[M.clipIntact];
    return C.idle ? -1.0f : structure_anim_frame(o, C.frames);
}

/* THE OBJECTS WHOSE STAGES THIS FILE PLAYS UNDER RED ALERT, at their death
   (ebld_claims_death) and with their fires (ebld_anims_filter): a civilian building with a
   reused model, a structure with its stages (ebld_ra_staged), and a reused tree. */
static bool ebld_ra_stages(const SimObject& o)
{
    if (!g_raModule) return false;
    if (o.kind == K_BUILDING)
        return ebld_ra_civil(o.type) ? ebld_model_of(o.type) >= 0 : ebld_ra_staged(o);
    if (o.kind == K_TERRAIN) return eterr_tree_claims(o);
    return false;
}
/* AND THE ONES IT KEEPS AN ENTRY FOR ON THE TICK (ebld_step): those, and a structure whose
   standing model carries emitters of its own (its idle smoke) or a clip its state drives
   (RA_DRIVE). An intact-only structure without either has none, and stays as it was. */
static bool ebld_ra_entry(const SimObject& o)
{
    if (ebld_ra_stages(o)) return true;
    if (!g_raModule || o.kind != K_BUILDING || ebld_ra_civil(o.type)) return false;
    const int m = ebld_model_of(o.type);
    return m >= 0 && (!g_ebld[m].emit.empty() || (g_ebld[m].raDrive >= 0 && g_ebld[m].clipIntact >= 0));
}

/* WHETHER RED ALERT'S 1996 PICTURE OF THIS OBJECT STANDS DOWN (ra3d_sprites_mod.h): while
   its model draws it, and for a civilian building or a staged structure also while it is
   in pieces, wrecked or sinking, which ebld_draw draws from its entry. */
static bool ebld_ra_owns(const SimObject& o)
{
    if (ebld_ra_ready(o)) return true;
    if (!g_raModule || o.kind != K_BUILDING || !(ebld_ra_civil(o.type) || ebld_ra_staged(o)))
        return false;
    const EbldBld* E = ebld_find(o);
    return E && (E->stage != EB_INTACT || E->deathClaimed);
}

/* Selection and action cursors follow the displayed intact geometry. */
static int ebld_ra_pick_mesh(const SimObject& o)
{
    if (!ebld_ra_ready(o)) return -1;
    const int m = ebld_model_of(o.type);
    return m >= 0 ? g_ebld[m].mesh[EBM_INTACT] : -1;
}

/* Whether the cartridge's per-building emitter rows stand down for this model: the
   healthy arm (0) or the damaged arm (1). A full model replaces both with its own. An
   intact-only model replaces an arm only where it carries emitters of that state of its
   own (the Advanced Power Plant's steam stands in for the cartridge's coolant bubbles);
   otherwise the cartridge's smoke and fire are still how its damage reads. */
static bool ebld_replaces_arm(const EbldModel& M, int arm)
{
    return M.nset >= 2 || M.emitState[arm & 1];
}

static const EbldBld* ebld_find(const SimObject& o);
static void ebld_emit_reset(EbldBld& E, const EbldModel& M);
/* The authored art set's choice for an object (model3d_mod.h, included after this file). */
static int object_model3d(const SimObject& o);
/* Whether this system is what the building is drawn and animated by right now: the
   cartridge's per-building emitter rows ask, and stand down when it is.
   A HURT BUILDING WITH NO ENTRY YET IS STILL THE CARTRIDGE'S. Entries are made on the
   tick edge only, so between Enhanced being switched on and the next tick (for as long
   as the game stays paused, when it is switched from the options) a building already
   below full strength has none, and this system cannot say what it looks like. The draw
   (ebld_draw_object) hands it to the cartridge's damaged model for those frames, and
   this answer agrees with it, so the cartridge's overlay and smoke go with it. */
static bool ebld_active(const SimObject& o)
{
    if (g_raModule) return false;
    if (o.kind != K_BUILDING || g_ebld.empty() || !ebld_rule_now()
        || ebld_model_of(o.type) < 0 || object_model3d(o) >= 0)
        return false;
    return o.str >= o.maxstr || ebld_find(o) != NULL;
}

/* The cartridge's emitter rows ask this, per arm (0 healthy, 1 damaged): whether this
   system has the building AND its model replaces that arm (ebld_replaces_arm). */
static bool ebld_arm_stands_down(const SimObject& o, int arm)
{
    if (!ebld_active(o)) return false;
    const int m = ebld_model_of(o.type);
    return m >= 0 && ebld_replaces_arm(g_ebld[m], arm);
}

/* THIS BUILDING'S ENTRY, handling a recycled heap slot. An entry belongs to the object
   that made it only while the type and the origin cell agree, and a wreck belongs to a
   dead building only: the same slot showing a live building of the same type on the
   same cell is a new building put down where the old one died. A wreck in a slot that
   has moved on keeps going under a key of its own; anything else is simply dropped. */
static void ebld_entry_init(EbldBld& E, int m, const char* type, int cx, int cy, float wx,
                            float wz, int fw, int fh);
static EbldBld* ebld_entry(const SimObject& o, bool create)
{
    const long long key = (long long)smooth_key(o);
    std::map<long long, EbldBld>::iterator it = g_ebldBld.find(key);
    if (it != g_ebldBld.end()) {
        EbldBld& E = it->second;
        const bool same = !strcmp(E.type, o.type) && E.cx == o.cx && E.cy == o.cy
                        && !(E.stage == EB_WRECKED && o.str > 0);
        if (same) return &E;
        if (E.stage == EB_WRECKED) {
            EbldBld keep = E;
            g_ebldBld.erase(it);
            g_ebldBld[-(++g_ebldOrphan)] = keep;
        } else {
            g_ebldBld.erase(it);
        }
    }
    if (!create) return NULL;
    const int m = ebld_model_of(o.type);
    if (m < 0) return NULL;
    EbldBld& E = g_ebldBld[key];
    ebld_entry_init(E, m, o.type, o.cx, o.cy, o.wx, o.wz, o.fw > 0 ? o.fw : 1,
                    o.fh > 0 ? o.fh : 1);
    E.id = o.id;
    E.kind = (int)o.kind;
    E.house = mesh_house(o);
    E.str = o.str; E.maxstr = o.maxstr;
    E.face = o.face;
    return &E;
}

/* A NEW ENTRY for model m standing at (wx, wz): whole, at rest, nothing launched. */
static void ebld_entry_init(EbldBld& E, int m, const char* type, int cx, int cy, float wx,
                            float wz, int fw, int fh)
{
    const EbldModel& M = g_ebld[m];
    E.model = m;
    E.stage = EB_INTACT;
    E.id = -1;
    E.kind = -1;
    snprintf(E.type, sizeof E.type, "%s", type);
    E.cx = cx; E.cy = cy;
    E.wx = wx; E.wz = wz;
    E.ground = terrain_y(wx, wz);
    E.fw = fw;
    E.fh = fh;
    E.house = 0;
    E.str = E.maxstr = 1;
    E.applied = 0;
    for (int z = 0; z < 8; z++) E.want[z] = E.cur[z] = E.curPrev[z] = 0.0f;
    E.lastZone = -1;
    E.chips = 0;
    E.launched.assign(M.set[EBS_CUT].piece.size(), 0);
    EbldBody none;
    memset(&none, 0, sizeof none);
    none.q[0] = none.pq[0] = 1.0f;
    E.cut.assign(M.set[EBS_CUT].piece.size(), none);
    E.wreck.clear();
    E.stageTick = g_engineFrame;
    E.drainTick = E.damagedTick = E.blastTick = 0;
    E.heldTick = g_engineFrame;
    E.face = 0;
    E.fillF = -1.0f;
    E.fillH0 = E.fillWreck = 0.0f;
    E.fresh = true;
    E.deathClaimed = E.sinking = E.dropped = E.seen = E.standDown = E.blush = false;
    E.rose = false;
    E.hurt = false;
    E.animF = -1.0f;
    E.clipI0 = 0;
    E.idlePhase = 0.0f;
    E.doorF = -1.0f;
    E.doorStage = -1;
    E.drvCur = E.drvPrev = 0.0f;
    E.drvClosing = false;
    E.cycCur = E.cycPrev = 0.0f;
    E.treeStage = 0;
    E.treeTick = g_engineFrame;
    E.fireTick = -1000000;
    E.orphan = false;
    E.acc.assign(M.emit.size(), 0.0f);
    ebld_emit_reset(E, M);
    E.boomBorn = E.boomLast = E.deathTick = -1;
    E.boomHouse = 0;
    E.boomX = wx; E.boomY = 0.0f; E.boomZ = wz;
    E.boomParts = E.boomChunks = 0;
    EbldLoopCache lc0 = { 0, -1.0 };
    E.lc.assign((size_t)M.nmotion, lc0);
}

/* The draw's lookup: never creates, never moves anything. */
static const EbldBld* ebld_find(const SimObject& o)
{
    if (g_ebldBld.empty()) return NULL;
    std::map<long long, EbldBld>::const_iterator it =
        g_ebldBld.find((long long)smooth_key(o));
    if (it == g_ebldBld.end()) return NULL;
    const EbldBld& E = it->second;
    if (strcmp(E.type, o.type) || E.cx != o.cx || E.cy != o.cy) return NULL;
    if (E.stage == EB_WRECKED && o.str > 0) return NULL;
    return &E;
}

/* THE DEATH IS OBSERVED IN THE PER-TICK PARSE, where the death shed and the old shatter
   observe it, and the decision is LATCHED there: once this answers yes, the building's
   collapse belongs to this system whatever the Enhanced switch does next, so toggling
   it mid-collapse cannot leave the building drawn by both or by neither. The collapse
   itself happens on the tick edge (ebld_step), where the frame number is current. A
   building going up or being sold when it dies keeps the old shatter: what is standing
   then is the cartridge's half-built shell, not this model. */
/* THE DEATH'S JOLT, in footprint cells: the lab's own number for a remastered building
   whose file carries it (the lab sizes it by the cartridge model's larger extent, which is
   not always the engine's footprint), else `own`, the footprint's. Asked where the death is
   observed, for a death this system has claimed. */
static float ebld_death_cells(const SimObject& o, float own)
{
    const int m = ebld_model_of(o.type);
    return (m >= 0 && g_ebld[m].boomShake > 0.0f) ? g_ebld[m].boomShake : own;
}

static bool ebld_claims_death(const SimObject& o)
{
    if (g_raModule && !ebld_ra_stages(o)) return false;
    if (o.kind != K_BUILDING || o.id < 0 || g_ebld.empty()) return false;
    if (!ebld_rule_now() || ebld_model_of(o.type) < 0) return false;
    if (building_constructing(o)) return false;
    EbldBld* E = ebld_entry(o, true);
    if (!E) return false;
    E->deathClaimed = true;
    /* AND THE LAB'S EXPLOSION IS ARMED, from the same observation: it goes off on the next
       particle tick (ebld_boom_step, inside efx_step), the one this death's tick runs, at
       the death's centre: the footprint's, or the blocked cells' (blast_x, blast_z) */
    if (E->boomBorn == -1) {
        const EbldModel& M = g_ebld[E->model];
        E->boomBorn = -2;
        E->boomHouse = obj_is_gdi(o);
        E->boomX = o.wx + M.param[EBP_BLAST_X] * MODEL_SCALE;
        E->boomZ = o.wz + M.param[EBP_BLAST_Z] * MODEL_SCALE;
    }
    return true;
}

/* ---- small rotation helpers, quaternions (w, x, y, z) ------------------------------ */
static void eb_qmat(const float* q, float* R)
{
    const float w = q[0], x = q[1], y = q[2], z = q[3];
    R[0] = 1.0f - 2.0f * (y * y + z * z); R[1] = 2.0f * (x * y - w * z); R[2] = 2.0f * (x * z + w * y);
    R[3] = 2.0f * (x * y + w * z); R[4] = 1.0f - 2.0f * (x * x + z * z); R[5] = 2.0f * (y * z - w * x);
    R[6] = 2.0f * (x * z - w * y); R[7] = 2.0f * (y * z + w * x); R[8] = 1.0f - 2.0f * (x * x + y * y);
}
static void eb_qnorm(float* q)
{
    const float L = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (L < 1e-8f) { q[0] = 1.0f; q[1] = q[2] = q[3] = 0.0f; return; }
    for (int i = 0; i < 4; i++) q[i] /= L;
}
/* q = rotation by angle |w| h about w, applied AFTER q (w is in world axes) */
static void eb_qspin(float* q, const float* w, float h)
{
    const float wl = sqrtf(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    if (wl < 1e-7f) return;
    const float a = 0.5f * wl * h, s = sinf(a) / wl;
    const float d[4] = { cosf(a), w[0] * s, w[1] * s, w[2] * s };
    const float r[4] = {
        d[0] * q[0] - d[1] * q[1] - d[2] * q[2] - d[3] * q[3],
        d[0] * q[1] + d[1] * q[0] + d[2] * q[3] - d[3] * q[2],
        d[0] * q[2] - d[1] * q[3] + d[2] * q[0] + d[3] * q[1],
        d[0] * q[3] + d[1] * q[2] - d[2] * q[1] + d[3] * q[0] };
    for (int i = 0; i < 4; i++) q[i] = r[i];
    eb_qnorm(q);
}
static void eb_mulv(const float* R, const float* v, float* o)
{
    o[0] = R[0] * v[0] + R[1] * v[1] + R[2] * v[2];
    o[1] = R[3] * v[0] + R[4] * v[1] + R[5] * v[2];
    o[2] = R[6] * v[0] + R[7] * v[1] + R[8] * v[2];
}
static void eb_cross(const float* a, const float* b, float* o)
{
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
/* A random fraction 0..1 from one byte of a hash, as the contract's r. */
static float eb_r(unsigned h, int byte) { return (float)((h >> (8 * byte)) & 0xFFu) / 255.0f; }

/* A piece's rest pose: exactly where draw_mesh puts it when the whole building is drawn
   at its anchor, so a piece that starts moving does not jump. */
static void ebld_rest_pos(const EbldBld& E, const EbldPiece& P, float* p)
{
    p[0] = E.wx + P.c[0] * MODEL_SCALE;
    p[1] = E.ground + P.c[1] * MODEL_SCALE;
    p[2] = E.wz + P.c[2] * MODEL_SCALE;
}
static void ebld_body_at(EbldBody& B, const float* p, const float* q)
{
    for (int i = 0; i < 3; i++) { B.p[i] = B.pp[i] = p[i]; B.v[i] = B.w[i] = 0.0f; }
    for (int i = 0; i < 4; i++) B.q[i] = B.pq[i] = q[i];
    B.y0 = p[1];
    B.still = 0;
    B.ghost = 0;
    B.live = true;
    B.asleep = false;
    B.pinned = false;
    B.tore = g_engineFrame;
    B.wet = 0;
    B.sunk = false;
}

/* A rotation matrix (row-major 3x3) as a unit quaternion (w, x, y, z). */
static void eb_matq(const float* R, float* q)
{
    const float tr = R[0] + R[4] + R[8];
    if (tr > 0.0f) {
        const float s = sqrtf(tr + 1.0f) * 2.0f;
        q[0] = 0.25f * s;
        q[1] = (R[7] - R[5]) / s; q[2] = (R[2] - R[6]) / s; q[3] = (R[3] - R[1]) / s;
    } else if (R[0] > R[4] && R[0] > R[8]) {
        const float s = sqrtf(1.0f + R[0] - R[4] - R[8]) * 2.0f;
        q[0] = (R[7] - R[5]) / s;
        q[1] = 0.25f * s; q[2] = (R[1] + R[3]) / s; q[3] = (R[2] + R[6]) / s;
    } else if (R[4] > R[8]) {
        const float s = sqrtf(1.0f + R[4] - R[0] - R[8]) * 2.0f;
        q[0] = (R[2] - R[6]) / s;
        q[1] = (R[1] + R[3]) / s; q[2] = 0.25f * s; q[3] = (R[5] + R[7]) / s;
    } else {
        const float s = sqrtf(1.0f + R[8] - R[0] - R[4]) * 2.0f;
        q[0] = (R[3] - R[1]) / s;
        q[1] = (R[2] + R[6]) / s; q[2] = (R[5] + R[7]) / s; q[3] = 0.25f * s;
    }
    eb_qnorm(q);
}

/* ---- a rigid placement in pack units: x -> R x + t ----------------------------------- */
struct EbXf { float R[9], t[3]; };
static void ebxf_id(EbXf& X)
{
    for (int i = 0; i < 9; i++) X.R[i] = (i % 4 == 0) ? 1.0f : 0.0f;
    X.t[0] = X.t[1] = X.t[2] = 0.0f;
}
/* A after B: x -> A(B x) */
static EbXf ebxf_mul(const EbXf& A, const EbXf& B)
{
    EbXf C;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++)
            C.R[i * 3 + j] = A.R[i * 3] * B.R[j] + A.R[i * 3 + 1] * B.R[3 + j]
                           + A.R[i * 3 + 2] * B.R[6 + j];
        C.t[i] = A.R[i * 3] * B.t[0] + A.R[i * 3 + 1] * B.t[1] + A.R[i * 3 + 2] * B.t[2]
               + A.t[i];
    }
    return C;
}
/* A turn of `ang` radians about the unit axis k through the point c: the right-handed
   rotation, x -> R (x - c) + c. */
static void ebxf_turn(EbXf& X, const float* c, const float* k, float ang)
{
    const float cs = cosf(ang), sn = sinf(ang), t = 1.0f - cs;
    const float x = k[0], y = k[1], z = k[2];
    const float R[9] = {
        t * x * x + cs,     t * x * y - sn * z, t * x * z + sn * y,
        t * x * y + sn * z, t * y * y + cs,     t * y * z - sn * x,
        t * x * z - sn * y, t * y * z + sn * x, t * z * z + cs };
    for (int i = 0; i < 9; i++) X.R[i] = R[i];
    for (int i = 0; i < 3; i++)
        X.t[i] = c[i] - (R[i * 3] * c[0] + R[i * 3 + 1] * c[1] + R[i * 3 + 2] * c[2]);
}
/* As draw_mesh's 3x4 extra matrix. */
static void ebxf_mat(const EbXf& X, float* Mx)
{
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) Mx[i * 4 + j] = X.R[i * 3 + j];
        Mx[i * 4 + 3] = X.t[i];
    }
}

/* A ROTATION R ABOUT A PIVOT c as draw_mesh's 3x4 extra matrix (mesh units): v' = R(v - c)
   + c. The rotation is the right-handed one about the unit axis k by `ang` radians, in the
   pack frame, which has the viewer's axes (x east, y up, z south). */
static void ebld_pivot_matrix(const float* c, const float* k, float ang, float* Mx)
{
    EbXf X;
    ebxf_turn(X, c, k, ang);
    ebxf_mat(X, Mx);
}

/* ---- the moving parts of the intact model, and the cut pieces that turn with them ---- */
/* How full the owning house's store is, 0..1, for a fill; the file's own rest level when
   the engine has not said (no house line yet, or a house with no storage at all). The
   same two numbers the cartridge's Silo readout reads (texbook_state_slot), taken whole
   rather than in its five steps, since the remaster's fill is continuous. -1: not known. */
static float ebld_store_level(const char* house)
{
    std::map<std::string, HouseStore>::const_iterator it = g_houseStore.find(house);
    /* (Red Alert's house with no store yet, as its capacity is counted a moment into the
       mission: empty, as the 1996 silo is, rather than the model's own rest level) */
    if (g_raModule && it != g_houseStore.end() && it->second.capacity <= 0 && it->second.tiberium >= 0)
        return 0.0f;
    if (it == g_houseStore.end() || it->second.capacity <= 0 || it->second.tiberium < 0)
        return -1.0f;
    float f = (float)it->second.tiberium / (float)it->second.capacity;
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    /* RED ALERT'S SILO SHOWS FIVE LEVELS, as its 1996 picture does (BuildingClass::Shape_Number:
       the house's stored ore times five over its whole capacity, the refinery's included,
       bounded to 0..4): empty, a quarter, half, three quarters, full, the same in every silo
       of the house */
    if (g_raModule) {
        int level = (int)((long long)it->second.tiberium * 5 / it->second.capacity);
        level = level < 0 ? 0 : (level > 4 ? 4 : level);
        f = (float)level / 4.0f;
    }
    return f;
}
static float ebld_fill_level(const SimObject& o, const EbldMove& mv)
{
    const float f = ebld_store_level(o.house);
    return f < 0.0f ? mv.a[2] : f;
}

/* How far a turning or aiming part is turned this frame, radians. A SPIN turns by -rpm/60
   turns a second about its axis, or swings to and fro `swing` degrees either side of rest
   at rpm swings a minute, exactly as the viewer turns it, on the engine clock (so two
   --shot runs agree). A TURRET faces the building's own facing: its offset from the way
   it was modelled, turned in the sense draw_mesh turns a cartridge turret part
   (turret_delta's rule). */
static float ebld_move_angle(const EbldMove& mv, int face, float now)
{
    if (mv.kind == EBV_SPIN) {
        double turns = (double)now / 15.0 * (double)mv.a[6] / 60.0;
        turns -= floor(turns);
        const float ph = (float)(turns * 2.0 * 3.14159265358979);
        return (mv.a[7] > 0.0f) ? -mv.a[7] * (EBLD_PI / 180.0f) * sinf(ph) : -ph;
    }
    const int fwd = (int)mv.a[8];
    const int d = (face >= 0 && !g_legacy) ? ((face - fwd) & 255) : 0;
    return -(float)d * (2.0f * EBLD_PI / 256.0f);
}

/* Where a moving part is this frame. A FILL is a level, not a matrix (fillY). False: draw
   nothing of it this frame. */
static bool ebld_move_pose(const EbldMove& mv, const SimObject& o, float* Mx, float* fillY,
                           float* seen)
{
    if (mv.kind == EBV_FILL) {
        const float f = ebld_fill_level(o, mv);
        *seen = f;
        if (f <= 0.0f) return false;                   /* empty: nothing to see */
        *fillY = mv.a[0] + (mv.a[1] - mv.a[0]) * f;
        return true;
    }
    const float ang = ebld_move_angle(mv, o.face, engine_time());
    *seen = ang * (180.0f / EBLD_PI);
    ebld_pivot_matrix(mv.a, mv.a + 3, ang, Mx);
    return true;
}

/* ================================================================================
 * CLIPS (version 6): per-frame part animation. A clip is a 3x4 matrix per frame and part,
 * in the pack frame, applied to the part's vertices where they rest in the model (the
 * lab's pose(): the matrix moves the model-space vertex). An INTACT clip shows, per
 * frame, either the frame the cartridge's own model of the object shows (object_anim_t,
 * the Oil Pump's nodding, so the remaster and the cartridge's pump keep the same time),
 * or, flagged IDLE, an idle loop of its own on the engine clock (the Blossom Trees). The
 * CUT clip is the damaged stage's own loop (the pump struggling), played from the
 * damaged switch on the engine clock, starting at the tick whose phase is nearest the
 * intact frame shown at the switch, so the switch shows no jump. All of it is the lab's
 * CLIPS and idle loop, number for number; matrices on the CPU, nothing a Voodoo 2 could
 * not draw.
 * ============================================================================== */
/* The frame an INTACT clip shows: IDLE, floor(T * fps) mod frames at idle clock T
   (seconds); otherwise the cartridge's frame `cartF` (-1 when the cartridge shows none:
   then -1 here too, the model's rest pose). */
static int ebld_clip_frame(const EbldClip& C, float cartF, double idleT)
{
    if (C.idle) {
        /* (a whole engine tick lands on its frame, not a hair below it: now / 15 * fps
           in floating point can fall short of the integer it means) */
        double f = floor(idleT * (double)C.fps + 1e-6);
        f = fmod(f, (double)C.frames);
        if (f < 0.0) f += (double)C.frames;
        return (int)f;
    }
    if (cartF < 0.0f) return -1;
    int f = (int)floorf(cartF);
    if (f >= C.frames) f = C.frames - 1;
    return f < 0 ? 0 : f;
}
/* Part `part` of a clip at frame f as a placement (identity for f < 0). */
static void ebld_clip_xf(const EbldClip& C, int f, int part, EbXf* X)
{
    ebxf_id(*X);
    if (f < 0 || f >= C.frames || part < 0 || part >= C.nparts) return;
    const float* m = &C.mat[((size_t)f * C.nparts + part) * 12];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) X->R[i * 3 + j] = m[i * 4 + j];
        X->t[i] = m[i * 4 + 3];
    }
}
/* Part `part` of a clip at a fractional frame `at`, blended between the two frames either
   side (the one past the last is frame 0: a loop's turn, or a door's return to rest), so a
   motion played by time moves on every picture drawn and not only on each new frame. */
static void ebld_clip_xf_at(const EbldClip& C, float at, int part, EbXf* X)
{
    ebxf_id(*X);
    if (at < 0.0f || C.frames < 1 || part < 0 || part >= C.nparts) return;
    int f0 = (int)floorf(at);
    float t = at - (float)f0;
    if (f0 >= C.frames) { f0 = C.frames - 1; t = 0.0f; }
    const int f1 = f0 + 1 >= C.frames ? 0 : f0 + 1;
    const float* a = &C.mat[((size_t)f0 * C.nparts + part) * 12];
    const float* b = &C.mat[((size_t)f1 * C.nparts + part) * 12];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) X->R[i * 3 + j] = a[i * 4 + j] + (b[i * 4 + j] - a[i * 4 + j]) * t;
        X->t[i] = a[i * 4 + 3] + (b[i * 4 + 3] - a[i * 4 + 3]) * t;
    }
}
/* The idle clock of an object, seconds: the engine's, plus the object's own head start
   (EbldBld::idlePhase), so the sync'd bursts and the loop they ride stay together. */
static double ebld_idle_t(float now, float phase)
{
    return (double)now / 15.0 + (double)phase;
}
/* The tick of the damaged loop the switch starts on: the one whose phase is nearest the
   intact frame f0 on show (0 at rest), measured round the intact clip's turn (the lab's
   clipSetup). */
static int ebld_clip_start(const EbldClip& C, float f0)
{
    const double turn = C.turn > 0 ? (double)C.turn : (double)C.frames;
    int i0 = 0;
    double best = 1e9;
    for (int i = 0; i < (int)C.phase.size(); i++) {
        const double e = fabs(fmod(fmod((double)C.phase[i] - (double)f0, turn) + turn * 1.5,
                                   turn) - turn / 2.0);
        if (e < best - 1e-9) { best = e; i0 = i; }
    }
    return i0;
}
/* The tick of the damaged loop shown at `now`. */
static int ebld_clip_tick(const EbldClip& C, int i0, int damagedTick, float now)
{
    const double t = ((double)now - (double)damagedTick) / 15.0;
    long long i = (long long)i0 + (long long)floor(t * (double)C.fps);
    i %= (long long)C.frames;
    if (i < 0) i += C.frames;
    return (int)i;
}

/* ================================================================================
 * LOOPING MOTIONS (version 4). A standing piece that keeps moving once damaged, as a
 * broken mechanism does: the Construction Yard's boom sagging at its broken elbow and
 * trying to lift, the Weapons Factory's jammed door trying to close. The viewer's
 * loopValue, loopDelta and loopsApply, ported number for number (its hash included, in
 * double precision as it computes), and driven by the ticks since the damaged switch, so
 * the struggle is irregular yet the same on every run.
 * ============================================================================== */
static double eb_loophash(double s, double i)
{
    const double x = sin(s * 127.1 + i * 311.7) * 43758.5453;
    return x - floor(x);
}
/* One motion's offset (degrees or pack units) at T seconds since its switch, and its
   current stroke fraction, shudder weight and try. */
static float ebld_loop_value(const EbldMotion& m, double T, double seed, EbldLoopCache* c,
                             float* f, float* w, int* tryk)
{
    const double settle = m.settle;
    double k = T < 0.0 ? 0.0 : T / settle;
    if (k > 1.0) k = 1.0;
    const double v = (double)m.rest
                   * (1.0 - exp(-6.0 * k) * cos(3.0 * 3.14159265358979 * k) * (1.0 - k));
    *f = 0.0f; *w = 0.0f; *tryk = -1;
    const double first = settle + 0.5;
    const int ns = (int)m.tries.size();
    if (!ns || T < first) return (float)v;
    if (c->t < 0.0 || T < c->t) { c->k = 0; c->t = first; }
    for (;;) {
        const std::vector<float>& S = m.tries[(int)floor(eb_loophash(seed, c->k + 0.5) * ns) % ns];
        double gap = (double)m.period[0]
                   + (double)(m.period[1] - m.period[0]) * eb_loophash(seed, c->k);
        const double last = S[S.size() - 3];
        if (last > gap) gap = last;
        if (T < c->t + gap) break;
        c->t += gap;
        c->k++;
    }
    const std::vector<float>& S = m.tries[(int)floor(eb_loophash(seed, c->k + 0.5) * ns) % ns];
    const double u = T - c->t;
    const double st = (double)m.stroke[0]
                    + (double)(m.stroke[1] - m.stroke[0]) * eb_loophash(seed, c->k + 0.25);
    double ff = 0.0, ww = 0.0;
    const int nk = (int)S.size() / 3;
    for (int i = 0; i + 1 < nk; i++) {
        const double t0 = S[i * 3], f0 = S[i * 3 + 1], w0 = S[i * 3 + 2];
        const double t1 = S[i * 3 + 3], f1 = S[i * 3 + 4], w1 = S[i * 3 + 5];
        if (u < t0 || u > t1) continue;
        const double e = t1 > t0 ? (u - t0) / (t1 - t0) : 1.0, sm = e * e * (3.0 - 2.0 * e);
        ff = f0 + (f1 - f0) * sm;
        ww = w0 + (w1 - w0) * e;
        break;
    }
    const double hz = m.shudder[1];
    const double sh = (double)m.shudder[0] * ww
                    * (0.6 * sin(2.0 * 3.14159265358979 * hz * T)
                       + 0.4 * sin(2.0 * 3.14159265358979 * hz * 1.618 * T + 1.3));
    *f = (float)ff; *w = (float)ww; *tryk = c->k;
    return (float)(v + st * ff + sh);
}

/* The seconds a piece has been looping: from the damaged switch, or for a piece that
   loops in the held stage too, from the first hit. */
static double ebld_loop_T(const EbldBld& E, const EbldModel& M, int k, float now)
{
    const int t0 = (M.px[k].flags & EBQ_LOOPHELD) ? E.heldTick : E.damagedTick;
    return ((double)now - (double)t0) / 15.0;
}
/* Whether a piece's loops are playing in this stage. */
static bool ebld_loops_now(const EbldBld& E, const EbldModel& M, int k)
{
    const int fl = M.px[k].flags;
    if (!(fl & EBQ_LOOP)) return false;
    return E.stage == EB_DAMAGED || (E.stage == EB_HELD && (fl & EBQ_LOOPHELD));
}
/* A piece's own loops at its T, applied at its rest pose in the order listed. */
static EbXf ebld_loop_own(const EbldBld& E, const EbldModel& M, int k, float now)
{
    EbXf X;
    ebxf_id(X);
    const EbldPX& P = M.px[k];
    const double T = ebld_loop_T(E, M, k, now);
    for (size_t i = 0; i < P.mo.size(); i++) {
        const EbldMotion& m = P.mo[i];
        float f, w;
        int tk;
        EbldLoopCache* c = &E.lc[(size_t)M.motion0[k] + i];
        const float v = ebld_loop_value(m, T, (double)P.seed + (double)i * 17.0, c, &f, &w, &tk);
        EbXf D;
        if (m.type == 0) {
            ebxf_turn(D, m.pivot, m.axis, v * (EBLD_PI / 180.0f));
        } else {
            ebxf_id(D);
            for (int c3 = 0; c3 < 3; c3++) D.t[c3] = m.axis[c3] * v;
        }
        X = ebxf_mul(D, X);                   /* this motion after those before it */
    }
    return X;
}
/* The piece's loops and, after them, those of the pieces carrying it. */
static EbXf ebld_loop_chain(const EbldBld& E, const EbldModel& M, int k, float now, int depth)
{
    EbXf X = ebld_loop_own(E, M, k, now);
    const int p = M.px[k].parent;
    if (p >= 0 && depth < 8 && ebld_loops_now(E, M, p))
        X = ebxf_mul(ebld_loop_chain(E, M, p, now, depth + 1), X);
    return X;
}
/* The turn of the turning part a piece rides: its own, else that of the piece whose loops
   carry it. -1: none. */
static int ebld_turn_of(const EbldModel& M, int k)
{
    for (int d = 0; k >= 0 && d < 8; d++) {
        if (M.px[k].spin >= 0) return M.px[k].spin;
        k = (M.px[k].flags & EBQ_LOOP) ? M.px[k].parent : -1;
    }
    return -1;
}

/* Whether a building's clips are a Hand of Nod's gate, which the engine has no state for:
   it opens for each soldier trained (hand_door_at), whatever stage the building is in. */
static bool ebld_hand_door(const EbldBld& E, const EbldModel& M)
{
    return M.clipIntact >= 0 && !M.clip[M.clipIntact].idle && E.kind == (int)K_BUILDING
        && !strcmp(E.type, "HAND");
}

/* WHERE A HAND OF NOD'S GATE OPENS, in world cells at ground level: the middle of the
   pieces its intact door clip moves (the gate's two leaves) under the remastered model
   standing on building `b`, which is centred on its footprint. A trained soldier is drawn
   coming out of it (hand_exit_*). False when the Hand of Nod draws no remastered model with
   a door (Classic, the cartridge's look, --noebld, an authored model3d): the soldier is
   then drawn where the engine puts him. */
static bool ebld_gate_of(const EbldModel& M, float wx, float wz, float* x, float* z)
{
    if (M.clipIntact < 0 || M.clip[M.clipIntact].idle) return false;
    const EbldSet& S = M.set[EBS_INTACT];
    double sx = 0.0, sz = 0.0;
    int n = 0;
    for (size_t k = 0; k < S.piece.size() && k < M.moveOf.size(); k++) {
        const int mv = M.moveOf[k];
        if (mv < 0 || M.move[mv].kind != EBV_CLIP) continue;
        sx += S.piece[k].c[0];
        sz += S.piece[k].c[2];
        n++;
    }
    if (!n) return false;
    *x = wx + (float)(sx / n) * MODEL_SCALE;
    *z = wz + (float)(sz / n) * MODEL_SCALE;
    return true;
}
static bool ebld_hand_gate(const SimObject& b, float* x, float* z)
{
    if (g_raModule || !ebld_rule_now() || g_fx.dos_classic || object_model3d(b) >= 0)
        return false;
    const int m = ebld_model_of(b.type);
    return m >= 0 && ebld_gate_of(g_ebld[m], b.wx, b.wz, x, z);
}

/* ================================================================================
 * THE WEAPONS FACTORY'S DOOR. Its clips (version 6) are its door, and they follow the
 * engine's door rather than any clock: the frame the cartridge gives the door stage
 * (weap_door_frame, 0 shut to 59 open, back down as it closes), for the intact model's clip
 * and, once damaged, for the cut's (door_WEAP.py writes both on the cartridge's numbering).
 * At every strength: the cartridge's own frame freezes its door once the building is below
 * half, and the damaged remaster opens its door for every vehicle instead. A door piece with
 * loops of its own (the jammed lower panel trying to close) takes its part of the clip
 * AFTER its loops, so the door's travel carries it on down from wherever its struggle has
 * it and brings it back there, with no jump either way. Matrices on the CPU, as every clip.
 * ============================================================================== */
/* The door frame an object's clips show, -1: its clips are not a door. */
static float ebld_door_at(const SimObject& o)
{
    if (g_raModule || o.kind != K_BUILDING || strcmp(o.type, "WEAP")) return -1.0f;
    return weap_door_frame(o);
}
/* Whether a cut piece rides the damaged door now. */
static bool ebld_door_piece(const EbldBld& E, const EbldModel& M, int k)
{
    return E.doorF >= 0.0f && E.stage == EB_DAMAGED && M.clipCut >= 0
        && k >= 0 && k < (int)M.clipPart.size() && M.clipPart[k] >= 0;
}
/* Whether a damaged door piece is also moved by loops of its own (the dump calls it one). */
static bool ebld_door_loops(const EbldBld& E, const EbldModel& M, int k)
{
    return ebld_door_piece(E, M, k) && ebld_loops_now(E, M, k);
}

/* WHERE A STANDING CUT PIECE IS, as a placement of its rest pose: turned with its turning
   part, moved by its loops (and those carrying it), or bent about its hinge, in the stage
   the building is in. False: it stands at rest, unmoved. The turn is the intact model's
   own at the same moment, so the switch to the cut model shows no jump. */
static bool ebld_stand_xf(const EbldBld& E, const EbldModel& M, int k, float now, EbXf* out)
{
    if (k < 0 || k >= (int)M.px.size()) return false;
    const EbldPX& P = M.px[k];
    bool moved = false;
    EbXf X;
    ebxf_id(X);
    /* THE WEAPONS FACTORY'S DAMAGED DOOR: a door piece on the cut's clip at the door's
       frame, after its own loops (no turning part carries either panel) */
    if (ebld_door_piece(E, M, k)) {
        const EbldClip& C = M.clip[M.clipCut];
        if (ebld_ra_motion(M))
            ebld_clip_xf_at(C, ebld_ra_drive_frame(E, M, g_tickAlpha), M.clipPart[k], &X);
        else
            ebld_clip_xf(C, ebld_clip_frame(C, E.doorF, 0.0), M.clipPart[k], &X);
        if (ebld_loops_now(E, M, k)) X = ebxf_mul(X, ebld_loop_chain(E, M, k, now, 0));
        *out = X;
        return true;
    }
    /* A PIECE OF A CLIP'S PART (version 6): held, its INTACT part at the intact frame on
       show, so the first hit shows no jump; damaged, its part of the cut's own loop */
    if (k < (int)M.clipPart.size() && M.clipPart[k] >= 0
        && (E.stage == EB_HELD || E.stage == EB_DAMAGED)) {
        if (ebld_hand_door(E, M) && M.clipIPart[k] >= 0) {
            /* A HAND OF NOD'S GATE, hit or damaged: its leaves are the intact door clip's
               parts at the frame its door shows (hand_door_at), as the intact model draws
               them, so a soldier trained in the damaged building still comes out through
               an open gate. The cut clip's own loop is the lab's demonstration only. */
            const EbldClip& C = M.clip[M.clipIntact];
            ebld_clip_xf(C, ebld_clip_frame(C, hand_door_at(E.id, now) * (float)(C.frames - 1),
                                            0.0),
                         M.clipIPart[k], &X);
        } else if (E.stage == EB_DAMAGED && M.clipCut >= 0) {
            const EbldClip& C = M.clip[M.clipCut];
            ebld_clip_xf(C, ebld_clip_tick(C, E.clipI0, E.damagedTick, now), M.clipPart[k], &X);
        } else if (M.clipIntact >= 0) {
            const EbldClip& C = M.clip[M.clipIntact];
            ebld_clip_xf(C, ebld_clip_frame(C, E.animF, ebld_idle_t(now, E.idlePhase)),
                         M.clipIPart[k], &X);
        }
        *out = X;
        return true;
    }
    if (ebld_loops_now(E, M, k)) {
        X = ebld_loop_chain(E, M, k, now, 0);
        moved = true;
    } else if ((P.flags & EBQ_BEND) && E.stage == EB_DAMAGED) {
        /* quickly, then settling: 1 - (1 - k)^3 of its angle over its seconds */
        float kk = ((now - (float)E.damagedTick) / 15.0f) / P.bend[7];
        if (kk < 0.0f) kk = 0.0f;
        if (kk > 1.0f) kk = 1.0f;
        const float e = 1.0f - (1.0f - kk) * (1.0f - kk) * (1.0f - kk);
        ebxf_turn(X, P.bend, P.bend + 3, P.bend[6] * (EBLD_PI / 180.0f) * e);
        moved = true;
    }
    const int sp = ebld_turn_of(M, k);
    if (sp >= 0) {
        const EbldMove& mv = M.move[sp];
        EbXf S;
        ebxf_turn(S, mv.a, mv.a + 3, ebld_move_angle(mv, E.face, now));
        X = ebxf_mul(S, X);
        moved = true;
    }
    if (moved) *out = X;
    return moved;
}
/* Whether a piece moves while it stands in this stage (so it cannot join a run). */
static bool ebld_stand_moves(const EbldBld& E, const EbldModel& M, int k)
{
    const EbldPX& P = M.px[k];
    if (k < (int)M.clipPart.size() && M.clipPart[k] >= 0
        && (E.stage == EB_HELD || E.stage == EB_DAMAGED))
        return true;
    return P.spin >= 0 || ebld_loops_now(E, M, k)
        || ((P.flags & EBQ_BEND) && E.stage == EB_DAMAGED) || ebld_turn_of(M, k) >= 0;
}
/* A standing piece's centre and turn in the world: where a body taking it over starts,
   and where an emitter riding it is. */
static void ebld_stand_world(const EbldBld& E, const EbldModel& M, int k, float now,
                             float* p, float* R)
{
    const EbldPiece& P = M.set[EBS_CUT].piece[k];
    EbXf X;
    if (!ebld_stand_xf(E, M, k, now, &X)) {
        ebld_rest_pos(E, P, p);
        for (int i = 0; i < 9; i++) R[i] = (i % 4 == 0) ? 1.0f : 0.0f;
        return;
    }
    float c[3];
    for (int i = 0; i < 3; i++)
        c[i] = X.R[i * 3] * P.c[0] + X.R[i * 3 + 1] * P.c[1] + X.R[i * 3 + 2] * P.c[2] + X.t[i];
    p[0] = E.wx + c[0] * MODEL_SCALE;
    p[1] = E.ground + c[1] * MODEL_SCALE;
    p[2] = E.wz + c[2] * MODEL_SCALE;
    for (int i = 0; i < 9; i++) R[i] = X.R[i];
}
/* A body taking over a standing piece: where it stands and as it is turned. */
static void ebld_body_from_stand(EbldBld& E, const EbldModel& M, int k, float now,
                                 EbldBody& B, float* R)
{
    float p[3], q[4];
    ebld_stand_world(E, M, k, now, p, R);
    eb_matq(R, q);
    ebld_body_at(B, p, q);
}
/* ================================================================================
 * THE SOLVER. One engine tick of one body, in four substeps, in cells and seconds.
 *
 * The numbers are the viewer's physics: gravity -6 cells/s^2, restitution 0.2,
 * friction 0.6, linear damping 0.05/s, angular 0.3/s, speed capped at 6 cells/s, and a
 * sphere inertia I = size^2 / 6 for a unit mass. Contact is tested at every hull point
 * against the ground under it; the deepest penetration lifts the body out, and each
 * penetrating point takes a normal impulse and a Coulomb friction impulse.
 *
 * THREE ADDITIONS OF OURS, all there so that a piece can come to rest at all, which is
 * what lets it sleep and costs nothing afterwards:
 *   - an approach slower than 0.3 cells/s does not bounce. With 0.2 restitution on every
 *     contact, gravity's own 0.1 cells/s per substep comes back as a 0.02 cells/s hop
 *     forever, which is exactly the sleep threshold and so never passes it;
 *   - the impulses are ACCUMULATED over several passes of the contact set, the total at
 *     each point held at or above zero and its friction inside mu times that total.
 *     Applied once per point instead, a small chip's large inverse inertia turned each
 *     corner's correction into a spin the next corner had to undo, and MEASURED on the
 *     Power Plant's chips that left most of them rocking at 0.05 to 0.4 rad/s half a
 *     minute after they landed, never asleep;
 *   - a body in contact loses 2% of its spin per substep, the rolling resistance the
 *     viewer's own fallback solver uses, so a shard on its face stops rocking.
 *
 * WHAT THE GROUND IS. The terrain always; the top of the building's slab over its
 * footprint while the slab stands (the Power Plant's); and, for a model with the REST
 * flag, the standing building itself (ebld_support_at), except for a piece still in its
 * ghost moment, which passes through the building as it leaves its hole. A point that
 * went through a standing face during the substep is caught by it: the face may lie as
 * far above the point as the point fell.
 *
 * A BALL (the Hand of Nod's globe) touches the ground at one point, straight under its
 * centre at its radius, with a solid sphere's inertia, its own restitution and angular
 * damping, and the lab's ball friction, 0.7: so it rolls.
 * ============================================================================== */
struct EbldGround {
    bool  slab;                               /* the slab's top counts as ground */
    float x0, x1, z0, z1, top;                /* over this rectangle, at this height */
    const EbldSupport* sup;                   /* the standing building, or NULL */
    float ax, az, ay;                         /* its anchor and the terrain under it */
};
/* The ground at (x, z) for a point at height yq (world cells): its height, the normal to
   use (n, when it is not the terrain's) and which ground it is, 0 terrain, 1 slab, 2 the
   standing building. */
static float ebld_ground_at(const EbldGround& G, float x, float z, float yq, bool support,
                            float* n, int* kind)
{
    float g = efx_ground(x, z);
    *kind = 0;
    if (G.slab && x >= G.x0 && x <= G.x1 && z >= G.z0 && z <= G.z1 && G.top > g) {
        g = G.top;
        *kind = 1;
    }
    if (support && G.sup) {
        float sy = 0.0f;
        if (ebld_support_at(*G.sup, (x - G.ax) / MODEL_SCALE, (z - G.az) / MODEL_SCALE,
                            (yq - G.ay) / MODEL_SCALE, &sy, n)) {
            sy = G.ay + sy * MODEL_SCALE;
            if (sy > g) { g = sy; *kind = 2; }
        }
    }
    return g;
}
static void ebld_ground_of(const EbldBld& E, const EbldModel& M, EbldGround& G)
{
    const bool shards = (E.stage == EB_WRECKED && M.nset == EBS_N);
    const int set = shards ? EBS_DESTROYED : EBS_CUT;
    const EbldSet& S = M.set[set];
    const bool rest = (M.flags & EBX_REST) != 0;
    /* THE WRECK'S SHARDS land on the terrain and on what of the wreck stays (its slab, a
       floor), never on the standing building, which is gone: it became the shards */
    G.slab = (shards || !rest) && S.hasSlab && !(E.stage == EB_WRECKED && E.sinking);
    const float hw = 0.5f * M.param[EBP_FOOT_W], hh = 0.5f * M.param[EBP_FOOT_H];
    G.x0 = E.wx - hw; G.x1 = E.wx + hw;
    G.z0 = E.wz - hh; G.z1 = E.wz + hh;
    G.top = E.ground + S.slabTop * MODEL_SCALE;
    G.sup = (rest && M.sup.on && !shards) ? &M.sup : NULL;
    G.ax = E.wx; G.az = E.wz; G.ay = E.ground;
}

static void ebld_body_tick(EbldBody& B, const EbldPiece& P, const EbldPX* X,
                           const EbldGround& G)
{
    for (int i = 0; i < 3; i++) B.pp[i] = B.p[i];
    for (int i = 0; i < 4; i++) B.pq[i] = B.q[i];
    if (!B.live || B.asleep || B.pinned) return;
    const float h = 1.0f / (15.0f * 4.0f);
    float sz = P.size * MODEL_SCALE;
    if (sz < 0.02f) sz = 0.02f;
    const bool ball = X && (X->flags & EBQ_BALL);
    const float br = ball ? X->ball[0] : 0.0f;
    const float invI = ball ? 1.0f / (0.4f * br * br) : 6.0f / (sz * sz);
    const float rest = ball ? X->ball[1] : 0.2f;
    const float mu = ball ? 0.7f : 0.6f;
    const float angDamp = ball ? X->ball[2] : 0.3f;
    const bool support = (B.ghost <= 0);
    /* a piece the file gave no hull still touches the ground at its centre */
    const int nh = ball ? 1 : (P.nhull > 0 ? P.nhull : 1);
    for (int sub = 0; sub < 4; sub++) {
        B.v[1] += -6.0f * h;
        for (int i = 0; i < 3; i++) B.p[i] += B.v[i] * h;
        eb_qspin(B.q, B.w, h);
        float R[9];
        eb_qmat(B.q, R);
        float r[16][3], pen[16], sn[16][3];
        int gk[16];
        float maxPen = 0.0f;
        const float fell = 0.02f + (B.v[1] < 0.0f ? -B.v[1] * h : 0.0f);
        for (int k = 0; k < nh; k++) {
            float hp[3] = { 0.0f, 0.0f, 0.0f };
            if (ball) {
                r[k][0] = 0.0f; r[k][1] = -br; r[k][2] = 0.0f;
            } else {
                if (P.nhull > 0)
                    for (int c = 0; c < 3; c++) hp[c] = P.hull[k][c] * MODEL_SCALE;
                eb_mulv(R, hp, r[k]);
            }
            const float wx = B.p[0] + r[k][0], wy = B.p[1] + r[k][1], wz = B.p[2] + r[k][2];
            pen[k] = ebld_ground_at(G, wx, wz, wy + fell, support, sn[k], &gk[k]) - wy;
            if (pen[k] > maxPen) maxPen = pen[k];
        }
        if (maxPen > 0.0f) {
            B.p[1] += maxPen;
            /* the contact set: every point that was in the ground, with its normal, its
               effective mass along it, the velocity it arrived with (for the bounce) and
               the impulses it has taken so far */
            int nc = 0;
            float cr[16][3], cn[16][3], ck[16], cv0[16], cl[16], cf[16][3];
            for (int k = 0; k < nh; k++) {
                if (pen[k] <= 0.0f) continue;
                float* n = cn[nc];
                n[0] = 0.0f; n[1] = 1.0f; n[2] = 0.0f;
                if (gk[k] == 0) {
                    const float px = B.p[0] + r[k][0], pz = B.p[2] + r[k][2], e = 0.25f;
                    const float gx = (efx_ground(px + e, pz) - efx_ground(px - e, pz)) / (2 * e);
                    const float gz = (efx_ground(px, pz + e) - efx_ground(px, pz - e)) / (2 * e);
                    const float L = sqrtf(gx * gx + 1.0f + gz * gz);
                    n[0] = -gx / L; n[1] = 1.0f / L; n[2] = -gz / L;
                } else if (gk[k] == 2) {
                    n[0] = sn[k][0]; n[1] = sn[k][1]; n[2] = sn[k][2];
                }
                for (int c = 0; c < 3; c++) cr[nc][c] = r[k][c];
                float rn[3], wr[3];
                eb_cross(cr[nc], n, rn);
                ck[nc] = 1.0f + invI * (rn[0] * rn[0] + rn[1] * rn[1] + rn[2] * rn[2]);
                eb_cross(B.w, cr[nc], wr);
                cv0[nc] = (B.v[0] + wr[0]) * n[0] + (B.v[1] + wr[1]) * n[1] + (B.v[2] + wr[2]) * n[2];
                cl[nc] = 0.0f;
                cf[nc][0] = cf[nc][1] = cf[nc][2] = 0.0f;
                nc++;
            }
            /* NORMAL IMPULSES, iterated with the running total held at or above zero, so a
               corner that was pushed too hard can hand some back: that is what lets a flat
               piece resting on several corners settle instead of rocking on them */
            for (int it = 0; it < 8; it++)
                for (int k = 0; k < nc; k++) {
                    float wr[3], rn[3];
                    eb_cross(B.w, cr[k], wr);
                    const float vn = (B.v[0] + wr[0]) * cn[k][0] + (B.v[1] + wr[1]) * cn[k][1]
                                   + (B.v[2] + wr[2]) * cn[k][2];
                    const float e = (-cv0[k] > 0.3f) ? rest : 0.0f;
                    const float target = (cv0[k] < 0.0f) ? -e * cv0[k] : 0.0f;
                    float nl = cl[k] + (target - vn) / ck[k];
                    if (nl < 0.0f) nl = 0.0f;
                    const float d = nl - cl[k];
                    cl[k] = nl;
                    if (d == 0.0f) continue;
                    eb_cross(cr[k], cn[k], rn);
                    for (int c = 0; c < 3; c++) {
                        B.v[c] += d * cn[k][c];
                        B.w[c] += invI * d * rn[c];
                    }
                }
            /* FRICTION, likewise accumulated, the total at each point held inside mu
               times the normal impulse that point took */
            for (int it = 0; it < 4; it++)
                for (int k = 0; k < nc; k++) {
                    if (cl[k] <= 0.0f) continue;
                    float wr[3], vp[3], vt[3];
                    eb_cross(B.w, cr[k], wr);
                    for (int c = 0; c < 3; c++) vp[c] = B.v[c] + wr[c];
                    const float vn = vp[0] * cn[k][0] + vp[1] * cn[k][1] + vp[2] * cn[k][2];
                    for (int c = 0; c < 3; c++) vt[c] = vp[c] - vn * cn[k][c];
                    const float tl = sqrtf(vt[0] * vt[0] + vt[1] * vt[1] + vt[2] * vt[2]);
                    if (tl < 1e-7f) continue;
                    float t[3], rt[3];
                    for (int c = 0; c < 3; c++) t[c] = vt[c] / tl;
                    eb_cross(cr[k], t, rt);
                    const float kt = 1.0f + invI * (rt[0] * rt[0] + rt[1] * rt[1] + rt[2] * rt[2]);
                    float nf[3];
                    for (int c = 0; c < 3; c++) nf[c] = cf[k][c] - t[c] * (tl / kt);
                    const float fl = sqrtf(nf[0] * nf[0] + nf[1] * nf[1] + nf[2] * nf[2]);
                    const float lim = mu * cl[k];
                    if (fl > lim && fl > 0.0f)
                        for (int c = 0; c < 3; c++) nf[c] *= lim / fl;
                    float d[3], rd[3];
                    for (int c = 0; c < 3; c++) { d[c] = nf[c] - cf[k][c]; cf[k][c] = nf[c]; }
                    eb_cross(cr[k], d, rd);
                    for (int c = 0; c < 3; c++) {
                        B.v[c] += d[c];
                        B.w[c] += invI * rd[c];
                    }
                }
            for (int c = 0; c < 3; c++) B.w[c] *= 0.98f;
        }
        for (int c = 0; c < 3; c++) {
            B.v[c] *= (1.0f - 0.05f * h);
            B.w[c] *= ball ? (1.0f - angDamp * h) : (1.0f - 0.3f * h);
        }
        const float sp = sqrtf(B.v[0] * B.v[0] + B.v[1] * B.v[1] + B.v[2] * B.v[2]);
        if (sp > 6.0f)
            for (int c = 0; c < 3; c++) B.v[c] *= 6.0f / sp;
    }
    if (B.ghost > 0) B.ghost--;
    /* a body the arithmetic has lost goes back to where it was and stops */
    bool ok = true;
    for (int c = 0; c < 3; c++)
        if (!ebld_finite(B.p[c]) || !ebld_finite(B.v[c]) || !ebld_finite(B.w[c])) ok = false;
    for (int c = 0; c < 4; c++) if (!ebld_finite(B.q[c])) ok = false;
    if (!ok) {
        for (int c = 0; c < 3; c++) { B.p[c] = B.pp[c]; B.v[c] = B.w[c] = 0.0f; }
        for (int c = 0; c < 4; c++) B.q[c] = B.pq[c];
        B.asleep = true;
        return;
    }
    const float sp = sqrtf(B.v[0] * B.v[0] + B.v[1] * B.v[1] + B.v[2] * B.v[2]);
    const float wl = sqrtf(B.w[0] * B.w[0] + B.w[1] * B.w[1] + B.w[2] * B.w[2]);
    if (sp < 0.02f && wl < 0.05f) {
        if (++B.still >= 10) {
            B.asleep = true;
            for (int c = 0; c < 3; c++) B.v[c] = B.w[c] = 0.0f;
        }
    } else {
        B.still = 0;
    }
}

/* ================================================================================
 * THE SEA (Red Alert: a building standing in the water, its file's EBP_SEA tail). A piece
 * that comes down on a water cell (evhc_cell_wet, the rule a ship's wreck follows) and not
 * on the building's own deck or slab goes in: a splash where it went in, and from then it
 * no longer bounces: it drifts on, slowing (sea_drag) and stopping turning (sea_spin), and
 * sinks at sea_sink until it is sea_depth under the surface, where it is gone (no longer
 * drawn). The surface is the water as the game draws it there: the sea is laid on the
 * terrain's corners, so it is the ground's height at the piece.
 * ================================================================================ */
static bool evhc_cell_wet(float wx, float wz);
static void ebld_sea_splash(const EbldModel& M, float x, float y, float z, unsigned key)
{
    for (int b = 0; b < 3; b++) {
        const float* f = &M.param[(size_t)EBP_SEA_BURST + (size_t)b * EBP_SEA_BN];
        const int src = (int)f[0], n = (int)(f[1] + 0.5f);
        if (src < 0 || n <= 0) continue;
        unsigned char sheet = 0, sys = 6;
        if (src == 0) {
            /* the steam sheet, as a building's own steam is drawn; without it, the smoke */
            sheet = efx_src_sheet("LandingSmoke");
            sys = sheet ? 1 : 6;
        } else if (src == 5) {
            sys = 0;                                  /* the glow's own sheet */
        }
        for (int i = 0; i < n; i++) {
            const unsigned h = efx_hash(0x5345410Au, key, (unsigned)b, (unsigned)i);
            const float a = 2.0f * EBLD_PI * eb_r(h, 0), d = f[2] * sqrtf(eb_r(h, 1));
            /* thrown up and out: a direction on the upper half, steeper than flat */
            const float az = 2.0f * EBLD_PI * eb_r(h, 2), up = 0.4f + 0.6f * eb_r(h, 3);
            const float out = sqrtf(1.0f - up * up);
            const float v[3] = { f[7] + cosf(az) * out * f[10], f[8] + up * f[10],
                                 f[9] + sinf(az) * out * f[10] };
            const float life = f[3] + eb_r(h, 4) * (f[4] - f[3]);
            int lt = (int)(life * 15.0f + 0.5f);
            if (lt < 1) lt = 1;
            float vm = 1.0f, rise = -f[12];
            const float kL = f[11] * life;
            if (f[11] > 0.0f && kL > 1e-4f) {
                const float ex = expf(-kL);
                vm = (1.0f - ex) / kL;
                rise = 2.0f * (rise / f[11]) * (life - (1.0f - ex) / f[11]) / (life * life);
            }
            EfxPart q;
            memset(&q, 0, sizeof q);
            q.x = x + d * cosf(a); q.y = y; q.z = z + d * sinf(a);
            q.sx = q.x; q.sy = q.y; q.sz = q.z;
            q.vx = v[0] * vm / 15.0f; q.vy = v[1] * vm / 15.0f; q.vz = v[2] * vm / 15.0f;
            q.ay = rise / 225.0f;
            q.hw = 0.5f * f[5];
            q.grow = 0.5f * (f[6] - f[5]) / (float)lt;
            q.life = (short)lt;
            q.sys = sys;
            const float al = f[16] < 0.0f ? 0.0f : (f[16] > 1.0f ? 1.0f : f[16]);
            const unsigned char aa = (unsigned char)(al * 255.0f + 0.5f);
            const float fd = f[17] > 0.01f ? f[17] : 0.2f;
            int ar = (int)((float)aa / (fd * (float)lt) + 0.5f);
            q.arate = (unsigned char)(ar < 1 ? 1 : (ar > 255 ? 255 : ar));
            q.r = (unsigned char)f[13]; q.g = (unsigned char)f[14]; q.b = (unsigned char)f[15];
            q.a = aa;
            q.seed = h;
            q.sheet = sheet;
            efx_push(q);
        }
    }
}
/* Whether a body has just come down on the water: it goes in. Called after its tick. */
static void ebld_body_sea_in(const EbldBld& E, const EbldModel& M, EbldBody& B, const EbldPiece& P,
                             const EbldGround& G, int frame, long long key, int idx)
{
    if (!M.sea || !g_raModule || B.wet || !B.live || B.pinned) return;
    if (!evhc_cell_wet(B.p[0], B.p[2])) return;
    float sz = 0.5f * P.size * MODEL_SCALE;
    if (sz < 0.02f) sz = 0.02f;
    const float sea = efx_ground(B.p[0], B.p[2]);
    if (B.p[1] - sz > sea + 0.02f) return;            /* still in the air */
    float n[3];
    int kind = 0;
    const float g = ebld_ground_at(G, B.p[0], B.p[2], B.p[1] + sz, B.ghost <= 0, n, &kind);
    if (kind != 0 && g > sea + 0.01f) return;          /* on the deck, the slab: it lies there */
    B.wet = frame + 1;
    B.asleep = false;
    if (g_efxPoolLive)
        ebld_sea_splash(M, B.p[0], sea, B.p[2], (unsigned)(key * 977 + idx));
    printf("EBLDSEA|in|%s|key=%lld|piece=%d|frame=%d|x=%.3f|z=%.3f|sea=%.3f\n", E.type, key, idx,
           frame, B.p[0], B.p[2], sea);
}
/* One tick of a body in the water. */
static void ebld_body_sea_tick(const EbldBld& E, const EbldModel& M, EbldBody& B, const EbldPiece& P,
                               int frame, long long key, int idx)
{
    for (int i = 0; i < 3; i++) B.pp[i] = B.p[i];
    for (int i = 0; i < 4; i++) B.pq[i] = B.q[i];
    if (B.sunk) return;
    const float dt = 1.0f / 15.0f;
    const float kv = expf(-M.param[EBP_SEA_DRAG] * dt), kw = expf(-M.param[EBP_SEA_SPIN] * dt);
    B.v[0] *= kv; B.v[2] *= kv;
    for (int c = 0; c < 3; c++) B.w[c] *= kw;
    /* (down at the sinking speed, eased into it over the first half second) */
    const float t = (float)(frame + 1 - B.wet) * dt;
    const float sink = M.param[EBP_SEA_SINK] * (t < 0.5f ? t / 0.5f : 1.0f);
    B.v[1] = -sink;
    for (int c = 0; c < 3; c++) B.p[c] += B.v[c] * dt;
    eb_qspin(B.q, B.w, dt);
    float sz = 0.5f * P.size * MODEL_SCALE;
    if (sz < 0.02f) sz = 0.02f;
    if (B.p[1] + sz < efx_ground(B.p[0], B.p[2]) - M.param[EBP_SEA_DEPTH]) {
        B.sunk = true;
        B.asleep = true;
        printf("EBLDSEA|gone|%s|key=%lld|piece=%d|frame=%d\n", E.type, key, idx, frame);
    }
}

/* ================================================================================
 * THE HIT DIRECTION. Nothing the engine exports says who hit a building, so the side
 * is inferred, deterministically and from this tick's dump only, in this order:
 *   a. a bullet that was in last tick's dump, is gone from this one, and was last seen
 *      near the building: the side it was on, or, if it was already inside the
 *      footprint, the reverse of its heading;
 *   b. something whose target cell is this building's target cell: its side, nearest
 *      first;
 *   c. an explosion born this tick near the building but not on its centre (splash);
 *   d. the side the last hit came from, and failing that the south, the camera's side.
 * ============================================================================== */
static int ebld_zone_of(float dx, float dz)
{
    const float a = atan2f(-dz, dx) / (EBLD_PI / 4.0f);
    int z = (int)floorf(a + 0.5f);
    z %= 8;
    if (z < 0) z += 8;
    return z;
}

struct EbldVanished { float x, z, d; int face, id; };

static int ebld_hit_zone(const EbldBld& E, const SimObject& o, int k,
                         const std::vector<EfxBullet>& gone, const char** how)
{
    const float hx = 0.5f * (float)E.fw, hz = 0.5f * (float)E.fh;
    /* a. the bullets that vanished beside it, nearest first, then by id */
    {
        std::vector<EbldVanished> cand;
        for (size_t i = 0; i < gone.size(); i++) {
            const float dx = gone[i].wx - o.ex, dz = gone[i].wz - o.ez;
            if (fabsf(dx) > hx + 1.5f || fabsf(dz) > hz + 1.5f) continue;
            EbldVanished v;
            v.x = dx; v.z = dz; v.d = dx * dx + dz * dz;
            v.face = gone[i].face; v.id = gone[i].id;
            cand.push_back(v);
        }
        if (!cand.empty()) {
            for (size_t i = 1; i < cand.size(); i++)
                for (size_t j = i; j > 0; j--) {
                    const EbldVanished& a = cand[j - 1];
                    const EbldVanished& b = cand[j];
                    if (a.d < b.d || (a.d == b.d && a.id <= b.id)) break;
                    std::swap(cand[j - 1], cand[j]);
                }
            /* several shells landing in one tick each take their own side */
            const EbldVanished& v = cand[(size_t)k % cand.size()];
            *how = "bullet";
            if (fabsf(v.x) <= hx && fabsf(v.z) <= hz) {
                /* inside the footprint: where it came from is behind its heading
                   (face 0 north, clockwise; north is -z) */
                const float a = (float)v.face * (2.0f * EBLD_PI / 256.0f);
                return ebld_zone_of(-sinf(a), cosf(a));
            }
            return ebld_zone_of(v.x, v.z);
        }
    }
    /* b. the things aiming at it */
    {
        const int tcell = o.tcy * g_brainStride + o.tcx;
        int best = -1;
        float bd = 0.0f;
        for (size_t i = 0; i < g_objects.size(); i++) {
            const SimObject& a = g_objects[i];
            if (a.tar != tcell || a.tar < 0 || a.limbo) continue;
            if (a.kind == o.kind && a.id == o.id) continue;
            const float dx = a.ex - o.ex, dz = a.ez - o.ez, d = dx * dx + dz * dz;
            if (best >= 0) {
                const SimObject& b = g_objects[best];
                if (d > bd) continue;
                if (d == bd && ((int)a.kind > (int)b.kind
                                || ((int)a.kind == (int)b.kind && a.id > b.id))) continue;
            }
            best = (int)i;
            bd = d;
        }
        if (best >= 0 && bd > 1e-6f) {
            *how = "attacker";
            return ebld_zone_of(g_objects[best].ex - o.ex, g_objects[best].ez - o.ez);
        }
    }
    /* c. a newborn explosion near it, off its centre */
    {
        int best = -1;
        float bd = 0.0f;
        for (size_t i = 0; i < g_efxAnims.size(); i++) {
            const EfxAnim& an = g_efxAnims[i];
            if (efx_classify(an.name) != EFXC_EXPLOSION || an.attref >= 0) continue;
            std::map<std::string, std::set<int> >::const_iterator pv = g_efxLivePrev.find(an.name);
            if (pv != g_efxLivePrev.end() && pv->second.count(an.id)) continue;
            const float dx = an.wx - o.ex, dz = an.wz - o.ez, d = dx * dx + dz * dz;
            if (d <= 0.09f) continue;
            if (fabsf(dx) > hx + 1.5f || fabsf(dz) > hz + 1.5f) continue;
            if (best >= 0 && (d > bd || (d == bd && an.id > g_efxAnims[best].id))) continue;
            best = (int)i;
            bd = d;
        }
        if (best >= 0) {
            *how = "anim";
            return ebld_zone_of(g_efxAnims[best].wx - o.ex, g_efxAnims[best].wz - o.ez);
        }
    }
    if (E.lastZone >= 0) { *how = "last"; return E.lastZone; }
    *how = "default";
    return 6;
}

/* ---- the three launches ------------------------------------------------------------ */
/* ONE CHIP OFF THE STRUCK SIDE, thrown back toward it. The struck zone's own chips go
   first, else the nearest zone that still has one; among equals the lowest piece.
   WITH THE REST FLAG, as the lab throws it now: the throw is blended with the way the
   chip's own outer surface faces, so a neighbouring zone's chip running along its own
   wall does not catch the edge of its hole, it passes through the building for a moment
   (0.15 s) as it leaves, and its spin is the lab's (7 and 4 rad/s either way). A chip
   carved from a turning part leaves from where the part has turned it. */
static int ebld_launch_chip(EbldBld& E, const EbldModel& M, int zone, int frame, int k,
                            unsigned key)
{
    const EbldSet& S = M.set[EBS_CUT];
    int best = -1, bd = 9;
    for (size_t i = 0; i < S.piece.size(); i++) {
        const EbldPiece& P = S.piece[i];
        if (!(P.flags & EBF_CHIP) || E.launched[i]) continue;
        const int a = (P.zone - zone + 8) % 8, b = (zone - P.zone + 8) % 8;
        const int d = a < b ? a : b;
        if (d < bd) { bd = d; best = (int)i; }
    }
    if (best < 0) return -1;
    const EbldPiece& P = S.piece[best];
    E.launched[best] = 1;
    E.chips++;
    EbldBody& B = E.cut[best];
    float R[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    const bool rest = (M.flags & EBX_REST) != 0;
    if (rest) {
        ebld_body_from_stand(E, M, best, (float)frame, B, R);
    } else {
        float p[3];
        const float q[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        ebld_rest_pos(E, P, p);
        ebld_body_at(B, p, q);
    }
    const unsigned h1 = efx_hash(0x45424843u, key, (unsigned)frame, (unsigned)(best | (k << 16)));
    const unsigned h2 = efx_hash(h1, 0x9E3779B9u, (unsigned)best, key);
    const float a = (float)zone * (EBLD_PI / 4.0f);
    float Dx = cosf(a), Dz = -sinf(a);
    if (rest && (M.px[best].flags & EBQ_FACING)) {
        float o[3];
        eb_mulv(R, P.push, o);
        float bx = Dx + o[0], bz = Dz + o[2];
        float L = sqrtf(bx * bx + bz * bz);
        if (L < 0.3f) {
            bx = o[0]; bz = o[2];
            L = sqrtf(bx * bx + bz * bz);
        }
        if (L > 1e-6f) { Dx = bx / L; Dz = bz / L; }
    }
    const float sp = 1.8f + 1.0f * eb_r(h1, 0);
    B.v[0] = Dx * sp + (eb_r(h1, 2) - 0.5f) * 0.6f;
    B.v[1] = 1.6f + 1.0f * eb_r(h1, 1);
    B.v[2] = Dz * sp + (eb_r(h1, 3) - 0.5f) * 0.6f;
    for (int c = 0; c < 3; c++) B.w[c] = (eb_r(h2, c) - 0.5f) * (rest && c == 1 ? 8.0f : 14.0f);
    if (rest) B.ghost = 2;                    /* 0.15 s */
    return best;
}

/* The longest a first-seen building's fallen pieces are run, silently, to come to rest:
   forty seconds of engine ticks. Measured, they are all asleep well inside it. */
enum { EBLD_SETTLE_TICKS = 600 };

/* The level the fill stands at now, pack units y, from the owner's store (or the file's
   rest level when the engine has not said), sunk by its drain once the building has
   broken: from where it stood then, toward the breach's foot, and never rising again.
   -1e30 when the model has no fill. */
static float ebld_fill_y(const EbldBld& E, const EbldModel& M, float now, float* frac)
{
    *frac = 0.0f;
    for (size_t i = 0; i < M.liquid.size(); i++) {
        const EbldLiquid& L = M.liquid[i];
        if (L.kind != EBL_FILL) continue;
        const float f = E.fillF < 0.0f ? L.rest : E.fillF;
        *frac = f;
        float h = L.a + (L.b - L.a) * f;
        if (E.stage == EB_DAMAGED) {
            const float T = L.seconds > 0.01f ? L.seconds : 0.01f;
            float k = ((now - (float)E.damagedTick) / 15.0f) / T;
            if (k < 0.0f) k = 0.0f;
            if (k > 1.0f) k = 1.0f;
            const float d = L.c, up = E.fillH0 - d > 0.0f ? E.fillH0 - d : 0.0f;
            const float s = d + up * powf(1.0f - k, 1.6f);
            if (s < h) h = s;
        }
        return h;
    }
    return -1.0e30f;
}

/* THE DAMAGE PULSE: a collapse, not an explosion. Every loose piece gets a short shove
   the way its outer surface faces, stronger near the middle, and a small lift that grows
   with its height, so the pieces topple and fall around the building. A piece with a
   shove of its own in the cut spec (PUSH) takes that instead, each component within 15%
   of it, and passes through the building for its ghost moment; a piece of a turning part
   falls from where the part had turned it, its shove turned with it.
   `mode`: 0 live (the camera jolts at half a death's strength), 1 first seen already below
   half (nothing jolts, the water long gone, the fires already burning, the loops and bends
   long begun), 2 at a death from above half, with no DESTROYED set (the death jolts; the
   pulse is what breaks it). */
static int ebld_enter_damaged(EbldBld& E, const EbldModel& M, int frame, long long lkey,
                              int mode)
{
    const unsigned key = (unsigned)lkey;
    const EbldSet& S = M.set[EBS_CUT];
    const bool rest = (M.flags & EBX_REST) != 0;
    int n = 0;
    /* the fill's level as it broke, before the stage changes under it */
    {
        float fr = 0.0f;
        E.fillH0 = ebld_fill_y(E, M, (float)frame, &fr);
    }
    for (size_t i = 0; i < S.piece.size(); i++) {
        const EbldPiece& P = S.piece[i];
        if (P.flags & (EBF_FIXED | EBF_CHIP | EBF_WATER | EBF_WATERFALL)) continue;
        /* a piece that bends stays attached and turns about its hinge instead */
        if (M.px[i].flags & EBQ_BEND) continue;
        EbldBody& B = E.cut[i];
        if (B.live) continue;
        const EbldPX& X = M.px[i];
        float R[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        if (rest) {
            ebld_body_from_stand(E, M, (int)i, (float)frame, B, R);
        } else {
            float p[3];
            const float q[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
            ebld_rest_pos(E, P, p);
            ebld_body_at(B, p, q);
        }
        const unsigned h1 = efx_hash(0x45424450u, key, (unsigned)frame, (unsigned)i);
        const unsigned h2 = efx_hash(h1, 0x7F4A7C15u, (unsigned)i, key);
        if (X.flags & EBQ_PUSH) {
            const unsigned h3 = efx_hash(h2, 0x50555348u, (unsigned)i, key);
            float v[3], w[3];
            for (int c = 0; c < 3; c++) {
                v[c] = X.v[c] * (0.85f + 0.3f * eb_r(h1, c));
                w[c] = X.w[c] * (0.85f + 0.3f * eb_r(h3, c));
            }
            eb_mulv(R, v, B.v);
            eb_mulv(R, w, B.w);
            B.ghost = (int)(X.ghost * 15.0f + 0.5f);
        } else {
            float push[3] = { P.push[0], P.push[1], P.push[2] };
            if (rest) eb_mulv(R, P.push, push);
            float cx = B.p[0] - E.wx, cz = B.p[2] - E.wz;
            float dist = rest ? sqrtf(cx * cx + cz * cz)
                              : sqrtf(P.c[0] * P.c[0] + P.c[2] * P.c[2]) * MODEL_SCALE;
            if (dist < 0.15f) dist = 0.15f;
            const float kk = 0.45f / (0.5f + dist);
            B.v[0] = push[0] * kk * (0.7f + 0.6f * eb_r(h1, 0));
            B.v[2] = push[2] * kk * (0.7f + 0.6f * eb_r(h1, 1));
            B.v[1] = 0.25f + 0.45f * eb_r(h1, 2) + 0.3f * (P.c[1] * MODEL_SCALE);
            for (int c = 0; c < 3; c++)
                B.w[c] = (eb_r(h2, c) - 0.5f) * (rest && c == 1 ? 2.0f : 4.0f);
        }
        n++;
    }
    /* THE DAMAGED LOOP starts at the tick nearest the intact frame on show (version 6) */
    if (M.clipCut >= 0)
        E.clipI0 = ebld_clip_start(M.clip[M.clipCut], E.animF < 0.0f ? 0.0f : floorf(E.animF));
    E.stage = EB_DAMAGED;
    E.damagedTick = frame;
    E.drainTick = frame;
    for (int z = 0; z < 8; z++) E.want[z] = E.cur[z] = E.curPrev[z] = 1.0f;
    for (size_t e = 0; e < E.acc.size(); e++) E.acc[e] = 0.0f;
    ebld_emit_reset(E, M);
    if (mode == 1) {
        /* first seen already below half: no jolt, the water long gone, and its fires
           already burning at full rate rather than catching again */
        E.drainTick = frame - (int)(M.param[EBP_WATER_SECONDS] * 15.0f * 1.2f) - 1;
        E.damagedTick = frame - EBLD_SETTLE_TICKS;
        for (size_t i = 0; i < M.liquid.size(); i++) {
            const int back = (int)(M.liquid[i].seconds * 15.0f * 1.2f) + 1;
            if (frame - back < E.drainTick) E.drainTick = frame - back;
        }
    } else if (mode == 0) {
        /* HALF THE DEATH JOLT, and through shake_tap so repeats cannot pile up. Enhanced
           only by construction: nothing in this file runs under Classic. */
        shake_tap(0.5f * M.param[EBP_SHAKE_PER_CELL]
                  * (M.boomShake > 0.0f ? M.boomShake : (float)(E.fw > E.fh ? E.fw : E.fh)));
        cinematic_audio_note(CIN_DAMAGE, E.wx, E.wz, (float)(E.fw > E.fh ? E.fw : E.fh));
    }
    printf("EBLD|damaged|key=%lld|frame=%d|pieces=%d\n", lkey, frame, n);
    return n;
}

/* THE WRECK. With a DESTROYED set (the Power Plant): everything still standing becomes
   the destroyed shards. A shard of a piece that had already fallen starts where that
   piece lies, turned with it; every other one starts where it stood. Then a blast throws
   every shard but the slab: outward and high, an explosion this time, strong enough to
   scatter the wreck round its footprint and not across the map. The building's own death
   jolt has already fired where the death was observed, so none is added here. */
static void ebld_enter_wrecked(EbldBld& E, const EbldModel& M, int frame, long long key)
{
    /* AN INTACT-ONLY BUILDING HAS NO WRECK. Its model stays whole for as long as the
       engine keeps the dead object (ebld_draw_object draws it), the engine's own death
       explosions and jolt play over it, and when the object is gone so is the model. What
       outlives it is the scorch, on the same timeline as a full model's: the entry is
       WRECKED with nothing left to sink or drop. The old shatter was already refused for
       it where the death was observed. */
    if (M.nset == 1) {
        E.wreck.clear();
        E.stage = EB_WRECKED;
        E.blastTick = frame;
        E.sinking = E.dropped = true;
        printf("EBLD|wreck|key=%lld|frame=%d|pieces=0\n", key, frame);
        return;
    }
    /* NO DESTROYED SET: the building dies as it stands. A building killed from above half
       breaks first (the damage pulse, without its jolt: the death's own jolt fires). Then
       every piece is kept where it is: one still falling goes on falling until the wreck
       starts to sink, one standing is held at the pose it has at that moment (a turning
       part stops turning, a looping one stops trying), the water and the spills go, and
       at sink_after everything sinks as it fades while the slab fades in place, as the
       Power Plant's wreck does, over the scorch. There is no blast: the engine's own death
       explosions play over it. */
    if (M.nset == 2) {
        if (E.stage != EB_DAMAGED) ebld_enter_damaged(E, M, frame, key, 2);
        const EbldSet& S = M.set[EBS_CUT];
        float fr = 0.0f;
        E.fillWreck = ebld_fill_y(E, M, (float)frame, &fr);
        if (fr <= 0.0f) E.fillWreck = -1.0e30f;
        E.wreck.resize(S.piece.size());
        int n = 0;
        for (size_t i = 0; i < S.piece.size(); i++) {
            EbldBody& B = E.wreck[i];
            if (M.liqOf[i] >= 0 && M.liquid[M.liqOf[i]].kind != EBL_FILL) {
                memset(&B, 0, sizeof B);
                B.q[0] = B.pq[0] = 1.0f;
                continue;                     /* the water and the spills go */
            }
            if (E.cut[i].live) {
                B = E.cut[i];
            } else {
                float R[9];
                ebld_body_from_stand(E, M, (int)i, (float)frame, B, R);
                B.pinned = true;
            }
            n++;
        }
        for (size_t i = 0; i < E.cut.size(); i++) E.cut[i].live = false;
        E.stage = EB_WRECKED;
        E.blastTick = frame;
        E.sinking = E.dropped = false;
        printf("EBLD|wreck|key=%lld|frame=%d|pieces=%d\n", key, frame, n);
        return;
    }
    const EbldSet& S = M.set[EBS_DESTROYED];
    const EbldSet& C = M.set[EBS_CUT];
    const unsigned hk = (unsigned)key;
    E.wreck.resize(S.piece.size());
    for (size_t i = 0; i < S.piece.size(); i++) {
        const EbldPiece& P = S.piece[i];
        EbldBody& B = E.wreck[i];
        float p[3], q[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        const int par = P.parent;
        if (par >= 0 && par < (int)E.cut.size() && E.cut[par].live) {
            const EbldBody& PB = E.cut[par];
            float R[9], off[3], ro[3];
            eb_qmat(PB.q, R);
            for (int c = 0; c < 3; c++) off[c] = (P.c[c] - C.piece[par].c[c]) * MODEL_SCALE;
            eb_mulv(R, off, ro);
            for (int c = 0; c < 3; c++) p[c] = PB.p[c] + ro[c];
            for (int c = 0; c < 4; c++) q[c] = PB.q[c];
        } else if (par >= 0 && par < (int)M.px.size() && E.stage != EB_INTACT
                   && ebld_stand_moves(E, M, par)) {
            /* a shard of a standing piece that had moved (a turret turned to its target,
               a dish part way round, a door half shut) starts where that piece stood,
               turned with it, as the lab's wreck starts from every piece where it lies */
            float pc[3], R[9], off[3], ro[3];
            ebld_stand_world(E, M, par, (float)frame, pc, R);
            for (int c = 0; c < 3; c++) off[c] = (P.c[c] - C.piece[par].c[c]) * MODEL_SCALE;
            eb_mulv(R, off, ro);
            for (int c = 0; c < 3; c++) p[c] = pc[c] + ro[c];
            eb_matq(R, q);
        } else if (par >= 0 && par < (int)M.clipIPart.size() && E.stage == EB_INTACT
                   && M.clipIPart[par] >= 0 && M.clipIntact >= 0) {
            /* killed whole: a shard of a clip's part starts where the intact model's part
               stood at that frame (version 6: the pump's beam where it had nodded to) */
            const EbldClip& Cl = M.clip[M.clipIntact];
            EbXf X;
            ebld_clip_xf(Cl, ebld_clip_frame(Cl, E.animF, ebld_idle_t((float)frame, E.idlePhase)),
                         M.clipIPart[par], &X);
            float c3[3];
            for (int i = 0; i < 3; i++)
                c3[i] = X.R[i * 3] * P.c[0] + X.R[i * 3 + 1] * P.c[1] + X.R[i * 3 + 2] * P.c[2]
                      + X.t[i];
            p[0] = E.wx + c3[0] * MODEL_SCALE;
            p[1] = E.ground + c3[1] * MODEL_SCALE;
            p[2] = E.wz + c3[2] * MODEL_SCALE;
            eb_matq(X.R, q);
        } else if (par >= 0 && par < (int)M.px.size() && E.stage == EB_INTACT
                   && M.px[par].spin >= 0) {
            /* killed whole: a shard of a turning part starts where the intact model's
               part stood turned (its turret's facing, its fan's angle) */
            const EbldMove& mv = M.move[M.px[par].spin];
            EbXf X;
            ebxf_turn(X, mv.a, mv.a + 3, ebld_move_angle(mv, E.face, (float)frame));
            float c3[3];
            for (int i = 0; i < 3; i++)
                c3[i] = X.R[i * 3] * P.c[0] + X.R[i * 3 + 1] * P.c[1] + X.R[i * 3 + 2] * P.c[2]
                      + X.t[i];
            p[0] = E.wx + c3[0] * MODEL_SCALE;
            p[1] = E.ground + c3[1] * MODEL_SCALE;
            p[2] = E.wz + c3[2] * MODEL_SCALE;
            eb_matq(X.R, q);
        } else {
            ebld_rest_pos(E, P, p);
        }
        ebld_body_at(B, p, q);
        if (P.flags & EBF_SLAB) { B.pinned = true; continue; }
        const unsigned h1 = efx_hash(0x45424258u, hk, (unsigned)frame, (unsigned)i);
        const unsigned h2 = efx_hash(h1, 0x2545F491u, (unsigned)i, hk);
        /* outward from the death's centre: the footprint's, or (version 6) the blocked
           cells' a civilian building stands on */
        float dx = p[0] - (E.wx + M.param[EBP_BLAST_X] * MODEL_SCALE),
              dz = p[2] - (E.wz + M.param[EBP_BLAST_Z] * MODEL_SCALE);
        float dl = sqrtf(dx * dx + dz * dz);
        if (dl < 0.05f) {
            dx = eb_r(h2, 3) - 0.5f;
            dz = eb_r(h1, 3) - 0.5f;
            dl = sqrtf(dx * dx + dz * dz);
            if (dl < 1e-4f) { dx = 1.0f; dz = 0.0f; dl = 1.0f; }
        }
        dx /= dl; dz /= dl;
        const float height = p[1] - E.ground;
        const float out = 0.6f + 0.7f * eb_r(h1, 0);
        const float up = 1.5f + 1.0f * eb_r(h1, 1) + (height < 0.5f ? 0.5f - height : 0.0f);
        B.v[0] = dx * out; B.v[1] = up; B.v[2] = dz * out;
        for (int c = 0; c < 3; c++) B.w[c] = (eb_r(h2, c) - 0.5f) * 9.0f;
    }
    /* the CUT set's bodies are gone: the shards are the building now */
    for (size_t i = 0; i < E.cut.size(); i++) E.cut[i].live = false;
    E.stage = EB_WRECKED;
    E.blastTick = frame;
    E.sinking = E.dropped = false;
    for (size_t e = 0; e < E.acc.size(); e++) E.acc[e] = 0.0f;
    ebld_emit_reset(E, M);
    printf("EBLD|wreck|key=%lld|frame=%d|pieces=%d\n", key, frame, (int)S.piece.size());
    if (M.param[EBP_BLAST_X] != 0.0f || M.param[EBP_BLAST_Z] != 0.0f)
        printf("EBLD|blast|key=%lld|x=%.3f|z=%.3f\n", key,
               E.wx + M.param[EBP_BLAST_X] * MODEL_SCALE, E.wz + M.param[EBP_BLAST_Z] * MODEL_SCALE);
}

/* The set the wreck's bodies belong to: the shards, or with no DESTROYED set the CUT
   pieces themselves. */
static int ebld_wreck_set(const EbldModel& M) { return M.nset == EBS_N ? EBS_DESTROYED : EBS_CUT; }

/* A FIRST-SEEN BUILDING'S PIECES ARE ALREADY LYING WHERE THEY FELL. Its chips and loose
   pieces are launched exactly as live hits would launch them and then run through the
   same solver, off screen and at once, until every one is asleep, so the first frame
   shows them at rest rather than a second collapse replayed in front of the player. Last
   tick's pose is set to this one, so nothing is lerped in from the building. */
static void ebld_settle(EbldBld& E, const EbldModel& M)
{
    EbldGround G;
    ebld_ground_of(E, M, G);
    const EbldSet& S = M.set[EBS_CUT];
    for (int t = 0; t < EBLD_SETTLE_TICKS; t++) {
        bool moving = false;
        for (size_t i = 0; i < E.cut.size() && i < S.piece.size(); i++) {
            EbldBody& B = E.cut[i];
            if (!B.live || B.asleep) continue;
            ebld_body_tick(B, S.piece[i], &M.px[i], G);
            moving = true;
        }
        if (!moving) break;
    }
    for (size_t i = 0; i < E.cut.size(); i++) {
        EbldBody& B = E.cut[i];
        for (int c = 0; c < 3; c++) B.pp[c] = B.p[c];
        for (int c = 0; c < 4; c++) B.pq[c] = B.q[c];
    }
}

/* A building repaired all the way back to full strength is whole again. */
static void ebld_reset_intact(EbldBld& E, const EbldModel& M, int frame)
{
    E.stage = EB_INTACT;
    E.applied = 0;
    for (int z = 0; z < 8; z++) E.want[z] = E.cur[z] = E.curPrev[z] = 0.0f;
    E.lastZone = -1;
    E.chips = 0;
    for (size_t i = 0; i < E.launched.size(); i++) E.launched[i] = 0;
    for (size_t i = 0; i < E.cut.size(); i++) E.cut[i].live = false;
    E.stageTick = frame;
    for (size_t e = 0; e < E.acc.size(); e++) E.acc[e] = 0.0f;
    ebld_emit_reset(E, M);
    for (size_t i = 0; i < E.lc.size(); i++) { E.lc[i].k = 0; E.lc[i].t = -1.0; }
}

/* ================================================================================
 * THE EMITTERS, into the existing particle pool. Each is a steady rate of particles at
 * weighted points (on a piece, riding it, or on the building), built the way the
 * cartridge's per-building emitter rows build theirs and borrowing an Enhanced sheet.
 * The pool integrates constant velocity and acceleration only, so the viewer's DRAG
 * is folded in as the constant velocity and acceleration that travel the same distance
 * over the particle's life.
 *
 * VERSION 4 adds the viewer's sparks and gates, stepped on the tick as the viewer steps
 * them per frame, and with its randomness hashed:
 *   burst     `count` particles at once, then a wait of `every` seconds (the first within
 *             the shortest wait of the start); the ramp scales the count, `rate` unused;
 *   gravity   pulling the particles down on top of `rise`; they skip off the ground;
 *   until     the emitter stops that long after its state began, easing out over
 *             until_fade before it;
 *   burst_on  a burst when the named piece's looping motion strains (its shudder weight
 *             above `above`), at most one in each try of each of its motions;
 *   fill      emits only while the building's fill holds some and stands above the
 *             height: the level now while it stands whole or held, the level it stood at
 *             when it broke once damaged;
 *   light     a flickering point light at its first point (Tier 2), flashing with each
 *             burst and gone in about a fifth of a second.
 * An emitter on a piece is born at that piece's pose: falling, turned with its turning
 * part, moved by its loops or bent; one of the intact state riding a turning part turns
 * with the intact model's part.
 * ============================================================================== */
static void ebld_emit_reset(EbldBld& E, const EbldModel& M)
{
    E.es.resize(M.emit.size());
    for (size_t e = 0; e < M.emit.size(); e++) {
        EbldEmitState& S = E.es[e];
        S.next = -1.0f;
        S.syncNext = -1.0;
        S.flash = -1;                  /* (what it has given off is kept: the dump's count) */
        const int op = M.emit[e].onPiece;
        const size_t nm = (op >= 0 && op < (int)M.px.size()) ? M.px[op].mo.size() : 0;
        S.tries.assign(nm, -2);
    }
}

/* Where an emitter's points are this tick: the piece's pose (a body's, or a standing
   piece's, which a turn, a loop or a bend may move) or the building's anchor. */
static void ebld_emit_frame(const EbldBld& E, const EbldModel& M, const EbldEmit& em,
                            float now, float* R, float* base)
{
    for (int i = 0; i < 9; i++) R[i] = (i % 4 == 0) ? 1.0f : 0.0f;
    if (em.piece >= 0) {
        const EbldBody& B = E.cut[em.piece];
        if (B.live) {
            eb_qmat(B.q, R);
            for (int c = 0; c < 3; c++) base[c] = B.p[c];
        } else {
            ebld_stand_world(E, M, em.piece, now, base, R);
        }
    } else {
        base[0] = E.wx; base[1] = E.ground; base[2] = E.wz;
    }
}

static void ebld_emit_one(const EbldBld& E, const EbldModel& M, const EbldEmit& em,
                          int ei, int n, int frame, unsigned key)
{
    unsigned char sheet = 0, sys = 0;
    switch (em.src) {
        case 0:
            sheet = efx_src_sheet("LandingSmoke"); sys = 1;
            /* without its own sheet this would be drawn as fire: no steam instead */
            if (!sheet) return;
            break;
        case 1: sheet = efx_src_sheet("BldgEmber_ENH"); sys = 8; break;
        case 2: sheet = 0; sys = 6; break;
        case 3: sheet = 0; sys = 5; break;
        case 4:
            /* sparks: the Enhanced muzzle flash's sheet, drawn with the fire system; without
               the sheet they would be drawn as flames, so none instead */
            sheet = efx_src_sheet("MuzzleFlash_ENH"); sys = 5;
            if (!sheet) return;
            break;
        case 5: sheet = 0; sys = 0; break;            /* the glow's own sheet */
        default: sheet = 0; sys = 7; break;           /* the ion spark's own sheet */
    }
    const unsigned h0 = efx_hash(0x45424550u, key, (unsigned)frame, (unsigned)((ei << 16) | n));
    const unsigned h1 = efx_hash(h0, 0x9E3779B9u, (unsigned)ei, key);
    const unsigned h2 = efx_hash(h1, 0x7F4A7C15u, (unsigned)n, key);
    int pi = 0;
    if (em.npoint > 1 && em.wsum > 0.0f) {
        float r = (float)(h0 & 0xFFFFu) / 65536.0f * em.wsum;
        while (pi < em.npoint - 1 && (r -= em.pt[(size_t)pi * 4 + 3]) > 0.0f) pi++;
    }
    float lp[3] = { em.pt[(size_t)pi * 4], em.pt[(size_t)pi * 4 + 1], em.pt[(size_t)pi * 4 + 2] };
    if (em.disc > 0.0f) {
        const float a = 2.0f * EBLD_PI * eb_r(h0, 2), d = em.disc * sqrtf(eb_r(h0, 3));
        lp[0] += d * cosf(a) / MODEL_SCALE;
        lp[2] += d * sinf(a) / MODEL_SCALE;
    }
    float R[9], base[3];
    ebld_emit_frame(E, M, em, (float)frame, R, base);
    float off[3], pos[3], dirw[3];
    for (int c = 0; c < 3; c++) lp[c] *= MODEL_SCALE;
    eb_mulv(R, lp, off);
    for (int c = 0; c < 3; c++) pos[c] = base[c] + off[c];
    eb_mulv(R, em.dir, dirw);
    float v[3];
    for (int c = 0; c < 3; c++)
        v[c] = em.vel[c] + dirw[c] * em.speed + (eb_r(h1, c) * 2.0f - 1.0f) * em.jitter;
    const float life = em.life[0] + eb_r(h1, 3) * (em.life[1] - em.life[0]);
    int lt = (int)(life * 15.0f + 0.5f);
    if (lt < 1) lt = 1;
    if (lt > 30000) lt = 30000;
    /* gravity is a downward rise, folded with the drag the same way */
    float vm = 1.0f, rise = em.rise;
    if (em.eflags & EBE_GRAVITY) rise = em.rise - em.gravity;
    const float kL = em.drag * life;
    if (em.drag > 0.0f && kL > 1e-4f) {
        const float ex = expf(-kL);
        const float r0 = rise;
        vm = (1.0f - ex) / kL;
        rise = 2.0f * (r0 / em.drag) * (life - (1.0f - ex) / em.drag) / (life * life);
    }
    EfxPart p;
    memset(&p, 0, sizeof p);
    p.x = pos[0]; p.y = pos[1]; p.z = pos[2];
    p.sx = p.x; p.sy = p.y; p.sz = p.z;
    p.vx = v[0] * vm / 15.0f;
    p.vy = v[1] * vm / 15.0f;
    p.vz = v[2] * vm / 15.0f;
    p.ax = 0.0f;
    p.ay = rise / 225.0f;
    p.az = 0.0f;
    p.hw = 0.5f * em.size[0];
    p.grow = 0.5f * (em.size[1] - em.size[0]) / (float)lt;
    p.life = (short)lt;
    p.age = 0;
    p.sys = sys;
    /* THE POOL'S ALPHA LAW is min(life left * slope, opacity), so fading over the last
       `fade` of the life is a slope of opacity / (fade * life). */
    float fade = em.fade > 0.0f ? em.fade : 0.2f;
    if (fade < 0.01f) fade = 0.01f;
    int ar = (int)((float)em.rgba[3] / (fade * (float)lt) + 0.5f);
    if (ar < 1) ar = 1;
    if (ar > 255) ar = 255;
    p.arate = (unsigned char)ar;
    p.r = em.rgba[0]; p.g = em.rgba[1]; p.b = em.rgba[2]; p.a = em.rgba[3];
    p.seed = h2;
    p.sheet = sheet;
    p.flat = 0;
    /* a spark skips off the ground rather than sinking through it */
    p.bounce = (em.eflags & EBE_GRAVITY) ? 1 : 0;
    efx_push(p);
}

/* How much an emitter is emitting now, 0 to 1, from its gates (1 without them): `until`
   at the emitter's clock t, and the fill. */
static float ebld_emit_live(const EbldBld& E, const EbldModel& M, const EbldEmit& em, float t,
                            int frame)
{
    float k = 1.0f;
    if (em.eflags & EBE_UNTIL) {
        if (t >= em.until) k = 0.0f;
        else if (em.untilFade > 0.0f) {
            const float f = (em.until - t) / em.untilFade;
            k = f < 1.0f ? f : 1.0f;
        }
    }
    if (em.eflags & EBE_FILL) {
        float frac = 0.0f;
        const float now = ebld_fill_y(E, M, (float)frame, &frac);
        const float h = (E.stage == EB_DAMAGED) ? E.fillH0 : now;
        if (!(frac > 0.0f) || !(h > em.fill)) k = 0.0f;
    }
    return k;
}

/* THE LIGHTS the building emitters carry, gathered on the tick and lit per frame by the
   renderer's light pass (fx_gather_lights), which ranks them with the explosions'. */
struct EbldLight {
    float x, y, z, r, g, b, k, range, delay, ramp, live;
    int t0, flash;
    bool burst;
};
static std::vector<EbldLight> g_ebldLights;
/* One light's power this frame, as the viewer flickers it: two sines on the emitter's
   clock, its ramp, a flash that dies in about a fifth of a second for a bursting emitter,
   and its gates. */
static float ebld_light_power(const EbldLight& L, float now)
{
    const float t = (now - (float)L.t0) / 15.0f;
    float f = 0.7f + 0.18f * sinf(t * 13.1f) + 0.12f * sinf(t * 29.7f + 1.3f);
    float rp = (t - L.delay) / (L.ramp > 0.0f ? L.ramp : 0.001f);
    if (rp < 0.0f) rp = 0.0f;
    if (rp > 1.0f) rp = 1.0f;
    f *= L.k * rp;
    if (L.burst) f *= (L.flash < 0) ? 0.0f : expf(-((now - (float)L.flash) / 15.0f) / 0.07f);
    return f * L.live;
}

static void ebld_emit(EbldBld& E, const EbldModel& M, int frame, unsigned key)
{
    if (!g_efxPoolLive) return;
    /* an intact-only model never leaves INTACT: its damaged emitters, if it has any,
       follow the cartridge's half-strength rule instead, as the cartridge's own arms do */
    /* a wreck plays the destroyed state's own (a wall's collapse dust), from the blast */
    int state = (E.stage == EB_DAMAGED) ? 1 : (E.stage == EB_WRECKED ? 2 : 0);
    if (M.nset == 1 && state == 0 && E.hurt) state = 1;
    if (state == 2 && !M.emitState[2]) return;
    if (E.es.size() != M.emit.size()) ebld_emit_reset(E, M);
    const int t0 = (state == 2 ? E.blastTick : state == 1 ? E.damagedTick : E.stageTick);
    const float t = (float)(frame - t0) / 15.0f;
    for (size_t e = 0; e < M.emit.size() && e < E.acc.size(); e++) {
        const EbldEmit& em = M.emit[e];
        if (em.state != state || t < em.delay) continue;
        EbldEmitState& ES = E.es[e];
        float ramp = 1.0f;
        if (em.ramp > 0.0f) {
            ramp = (t - em.delay) / em.ramp;
            if (ramp > 1.0f) ramp = 1.0f;
        }
        const float live = (em.eflags & (EBE_UNTIL | EBE_FILL))
                         ? ebld_emit_live(E, M, em, t, frame) : 1.0f;
        int n = 0;
        if (em.eflags & EBE_BURST) {
            if (ES.next < 0.0f) {
                const unsigned hs = efx_hash(0x45424253u, key, (unsigned)e, 0xFFFFu);
                ES.next = em.delay + eb_r(hs, 0) * em.burstEvery[0];
            }
            for (int guard = 0; t >= ES.next && guard < 16; guard++) {
                const unsigned hb = efx_hash(0x45424254u, key, (unsigned)e, (unsigned)ES.nburst);
                const float c = em.burstCount[0] + eb_r(hb, 0) * (em.burstCount[1] - em.burstCount[0]);
                int nn = (int)floorf(c * ramp + 0.5f);
                if (nn < 1) nn = 1;
                if (live < 1.0f) nn = (int)floorf((float)nn * live + 0.5f);
                if (nn > 0) ES.flash = frame;
                for (int i = 0; i < nn && n < 64; i++) ebld_emit_one(E, M, em, (int)e, n++, frame, key);
                float gap = em.burstEvery[0] + eb_r(hb, 1) * (em.burstEvery[1] - em.burstEvery[0]);
                if (gap < 0.02f) gap = 0.02f;
                ES.next += gap;
                ES.nburst++;
            }
        } else if (!(em.eflags & EBE_BURSTON)) {
            E.acc[e] += (em.eflags & (EBE_UNTIL | EBE_FILL)) ? em.rate / 15.0f * ramp * live
                                                             : em.rate / 15.0f * ramp;
            while (E.acc[e] >= 1.0f && n < 64) {
                E.acc[e] -= 1.0f;
                ebld_emit_one(E, M, em, (int)e, n++, frame, key);
            }
        }
        /* a burst each time the piece's looping motion strains, once a try */
        if ((em.eflags & EBE_BURSTON) && em.onPiece >= 0 && !E.cut[em.onPiece].live
            && ebld_loops_now(E, M, em.onPiece)) {
            const EbldPX& X = M.px[em.onPiece];
            const double T = ebld_loop_T(E, M, em.onPiece, (float)frame);
            for (size_t i = 0; i < X.mo.size() && i < ES.tries.size(); i++) {
                float f, w;
                int tk;
                EbldLoopCache* c = &E.lc[(size_t)M.motion0[em.onPiece] + i];
                ebld_loop_value(X.mo[i], T, (double)X.seed + (double)i * 17.0, c, &f, &w, &tk);
                if (!(w > em.onAbove) || ES.tries[i] == tk) continue;
                ES.tries[i] = tk;
                const unsigned hb = efx_hash(0x4542424Fu, key, (unsigned)e, (unsigned)ES.nburst);
                const float cnt = em.onCount[0] + eb_r(hb, 0) * (em.onCount[1] - em.onCount[0]);
                int nn = (int)floorf(cnt * ramp + 0.5f);
                if (nn < 1) nn = 1;
                if (live < 1.0f) nn = (int)floorf((float)nn * live + 0.5f);
                if (nn > 0) ES.flash = frame;
                for (int j = 0; j < nn && n < 64; j++) ebld_emit_one(E, M, em, (int)e, n++, frame, key);
                ES.nburst++;
            }
        }
        /* VERSION 6, SYNC: `count` particles at `at` seconds into every `period` of the
           idle clock (the clock the model's own loop plays on), so a burst lands on the
           same moment of the loop every time (the lab's sync) */
        if (em.eflags & EBE_SYNC) {
            const double T = ebld_idle_t((float)frame, E.idlePhase);
            const double at = em.syncAt, per = em.syncPeriod;
            if (ES.syncNext < 0.0) {
                const double k = ceil((T - at) / per);
                ES.syncNext = at + (k > 0.0 ? k : 0.0) * per;
            }
            for (int guard = 0; T >= ES.syncNext && guard < 4; guard++) {
                const unsigned hb = efx_hash(0x45425359u, key, (unsigned)e, (unsigned)ES.nburst);
                const float c = em.syncCount[0] + eb_r(hb, 0) * (em.syncCount[1] - em.syncCount[0]);
                int nn = (int)floorf(c * ramp + 0.5f);
                if (nn < 1) nn = 1;
                if (live < 1.0f) nn = (int)floorf((float)nn * live + 0.5f);
                if (nn > 0) ES.flash = frame;
                for (int j = 0; j < nn && n < 64; j++) ebld_emit_one(E, M, em, (int)e, n++, frame, key);
                ES.syncNext += per;
                ES.nburst++;
            }
        }
        ES.spawned += n;
        /* its light, at its first point on the piece, a little above it */
        if (em.eflags & EBE_LIGHT) {
            float R[9], base[3], off[3];
            ebld_emit_frame(E, M, em, (float)frame, R, base);
            const float lp[3] = { em.pt[0] * MODEL_SCALE, em.pt[1] * MODEL_SCALE + 0.12f,
                                  em.pt[2] * MODEL_SCALE };
            eb_mulv(R, lp, off);
            EbldLight L;
            L.x = base[0] + off[0]; L.y = base[1] + off[1]; L.z = base[2] + off[2];
            L.r = em.light[0] / 255.0f; L.g = em.light[1] / 255.0f; L.b = em.light[2] / 255.0f;
            L.k = em.light[3]; L.range = em.light[4];
            L.delay = em.delay; L.ramp = em.ramp; L.live = live;
            L.t0 = t0; L.flash = ES.flash;
            L.burst = (em.eflags & (EBE_BURST | EBE_BURSTON | EBE_SYNC)) != 0;
            g_ebldLights.push_back(L);
        }
    }
}

/* ================================================================================
 * THE TICK. From the deduplicated tick edge, right after efx_step (so the pool is not
 * cleared under a fresh push) and before the cartridge's building emitters and the old
 * shatter. Never from a draw.
 * ============================================================================== */
/* ================================================================================
 * A REMASTERED TREE BURNS, AND BURNS OUT (Enhanced only). The engine's own story
 * (terrain.cpp): a fire warhead that hurts a flammable tree sets it on fire (Catch_Fire),
 * attaching two fire anims, BURN-L at the tree's centre 80 leptons north and BURN-M 224
 * north; it burns while either lives, about eleven seconds; when the last goes out
 * (Fire_Out) a tree whose strength is gone starts to crumble (its sprite's crumble frames,
 * under a SMOKE_M puff) and is deleted at the last frame. A tree destroyed by anything else
 * crumbles at once. One that still has strength when the fire goes out stands on, drawn
 * whole again, as the 1995 sprite is.
 *
 * THE REMASTER'S STAGES follow it exactly: while the engine's fire is on the tree its
 * burning model is drawn (the lab's: scorched and charred cards, the painted flame
 * flipbook on crossed cards and a glow inside the crown, cut out and self-lit, with its
 * smoke plume, embers and the fire's light), and the engine's two fire anims on that tree
 * stand down, since the burning model is the tree's fire; from the moment it crumbles the
 * charred snag is drawn (no cards, the limbs burnt short, all coal), with its own smoke and
 * embers fading from 18 seconds to nothing at 30. The engine deletes the tree a second or
 * so into that; the snag stays at its place on its own entry until its smoke is out, then
 * settles into the ground over three seconds and is gone. What is left there is what the
 * 1995 game leaves: the scorch mark BURN-L puts down when the fire starts (the engine's
 * smudge, drawn as every smudge is). No stump stays.
 *
 * THE FIRE IS READ FROM THE ANIMS, NOT GUESSED: the dump names the kind an anim rides
 * (attkind 9, RTTI_TERRAIN) but not which terrain object, and Catch_Fire's two offsets from
 * the tree's centre are exact in leptons, so a fire matches one tree only (etree_fire_on).
 * The crumble is the engine's own state too: a tree in the dump with no strength and no
 * fire on it is crumbling. A type with no stage file keeps what it drew before: the intact
 * remaster under the engine's fire, and the cartridge's tree for the crumble.
 * --notreefire is the A/B. Tier 2 only, as everything in this file.
 * ============================================================================== */
/* (ebld_terr.h, after this file: a remastered terrain object off the picture) */
static bool eterr_object_off(const SimObject& o);
static bool ebld_draw_object(int mi, const SimObject& o, int pass);
static std::set<int> g_etreeFire;            /* terrain heap ids the engine's fire is on */
static bool g_etreeOn = true;                /* --notreefire */
static const float ETREE_SNAG_SECONDS = 30.0f;   /* the snag's smoke is out (fx.json until) */
static const float ETREE_SINK_SECONDS = 3.0f;    /* and it settles into the ground */
static float g_etreeSink = 0.0f;                 /* the draw's: cells the snag has settled */
static const int ETREE_FIRE_HOLD = 20;           /* ticks a burning look outlasts its fire */
static long g_etreeStood = 0;                /* engine fire anims stood down, all told */
/* (the snags drawn on their own entries in the last colour frame, for the dump) */
static long g_etreeOrphanDrawn = 0;

/* The kind a dump's anim rides when it rides a terrain object: RTTI_TERRAIN, which the two
   engines number differently (Tiberian Dawn 9, Red Alert 24; Red Alert's Catch_Fire attaches
   the same two fires at the same offsets). */
static int etree_rtti_terrain(void) { return g_raModule ? 24 : 9; }
/* Whether anim a is one of the two fires Catch_Fire attaches to terrain object o. */
static bool etree_fire_on(const EfxAnim& a, const SimObject& o)
{
    if (a.attkind != etree_rtti_terrain() || o.kind != K_TERRAIN || o.id < 0) return false;
    float dz;
    if (!strcmp(a.name, "BURN-L")) dz = 80.0f / 256.0f;
    else if (!strcmp(a.name, "BURN-M")) dz = 224.0f / 256.0f;
    else return false;
    return fabsf(a.wx - o.ex) < 0.5f / 256.0f && fabsf((o.ez - a.wz) - dz) < 0.5f / 256.0f;
}
/* The tree a fire anim rides, or -1. */
static int etree_fire_owner(const EfxAnim& a)
{
    if (a.attkind != etree_rtti_terrain()) return -1;
    for (size_t j = 0; j < g_objects.size(); j++)
        if (etree_fire_on(a, g_objects[j])) return (int)j;
    return -1;
}
/* On every parse, before anything is taken out of the anims: which trees burn. */
static void etree_fires_seen(void)
{
    g_etreeFire.clear();
    for (size_t i = 0; i < g_efxAnims.size(); i++) {
        const int oi = etree_fire_owner(g_efxAnims[i]);
        if (oi >= 0) g_etreeFire.insert(g_objects[(size_t)oi].id);
    }
}
/* The stage model k (1 burning, 2 snag) of the remastered tree o, ready to draw, or -1. */
static int etree_stage_model(const SimObject& o, int k)
{
    if (!g_etreeOn || o.kind != K_TERRAIN || !eterr_tree_claims(o)) return -1;
    const int m = ebld_model_of(o.type);
    if (m < 0) return -1;
    const int s = g_ebld[(size_t)m].stageModel[k];
    return (s >= 0 && s < (int)g_ebld.size() && ebld_meshes_ok(g_ebld[(size_t)s])) ? s : -1;
}
/* THE TICK'S DECISION for a remastered tree's entry: its stage from the engine's state, and
   on a change the entry's model and clocks switched, so the new stage's emitters play from
   its first tick. m is the tree's own model. A snag never goes back. */
static void etree_step(EbldBld& E, const SimObject& o, int m, int frame)
{
    const bool fire = g_etreeFire.count(o.id) != 0;
    if (fire) E.fireTick = frame;
    /* (a fire that goes out while the tree still stands and is lit again within a second,
       as a flame tank's next burst does, is one fire: the burning model holds through the
       gap rather than flashing the green tree for a few ticks) */
    const bool held = !fire && o.str > 0 && E.treeStage == 1
                   && frame - E.fireTick < ETREE_FIRE_HOLD;
    int want = (E.treeStage == 2 || (o.str <= 0 && !fire)) ? 2 : (fire || held) ? 1 : 0;
    int wm = want ? etree_stage_model(o, want) : m;
    if (wm < 0) { want = 0; wm = m; }
    if (want == E.treeStage && wm == E.model) return;
    printf("ETREE|stage|%s|%d|%d|id=%d|from=%d|to=%d|str=%d|frame=%d\n", o.type, o.cx, o.cy,
           o.id, E.treeStage, want, o.str, frame);
    E.treeStage = want;
    E.treeTick = frame;
    E.model = wm;
    E.stageTick = frame;
    const EbldModel& M = g_ebld[(size_t)wm];
    E.acc.assign(M.emit.size(), 0.0f);
    E.es.clear();
    ebld_emit_reset(E, M);
    EbldLoopCache lc0 = { 0, -1.0 };
    E.lc.assign((size_t)M.nmotion, lc0);
}
/* The model the draw shows for remastered tree o (m its own): its entry's stage, or the snag
   for a crumbling tree no tick has seen yet; -2 when the cartridge draws it (a crumbling
   tree whose type has no snag). */
static int etree_draw_model(const SimObject& o, const EbldBld* E, int m)
{
    if (o.kind != K_TERRAIN || m < 0) return m;
    if (E && E->treeStage > 0 && E->model >= 0 && E->model < (int)g_ebld.size()
        && g_ebld[(size_t)E->model].stageOf == m)
        return ebld_meshes_ok(g_ebld[(size_t)E->model]) ? E->model : m;
    if (o.str <= 0) {
        const int s = etree_stage_model(o, 2);
        return s >= 0 ? s : -2;
    }
    return m;
}
/* How far a snag on its own entry has settled into the ground, cells (0 until its smoke is
   out, then eased over ETREE_SINK_SECONDS to its own height). */
static float etree_sink(const EbldBld& E, float now)
{
    const float t = (now - (float)E.treeTick) / 15.0f - ETREE_SNAG_SECONDS;
    if (t <= 0.0f) return 0.0f;
    float u = t / ETREE_SINK_SECONDS;
    if (u > 1.0f) u = 1.0f;
    const EbldModel& M = g_ebld[(size_t)E.model];
    float top = 0.0f;
    const EbldSet& S = M.set[EBS_INTACT];
    for (size_t i = 0; i < S.tri.size(); i++)
        for (int k = 0; k < 3; k++) if (S.tri[i].v[k].y > top) top = S.tri[i].v[k].y;
    return u * u * (top / 1024.0f + 0.02f);
}

static void ebld_wall_step(int frame, const std::vector<EfxBullet>& gone);
static void ebld_step(int frame)
{
    /* (under Red Alert the civilian buildings and trees reused from OpenCNC3D, the
       structures with their stages, idle emitters or driven clips, ebld_ra_entry, and the
       walls its engine's map put in g_walls) */
    if (g_ebld.empty()) return;
    if (frame == g_ebldLastTick) return;
    const Uint64 traceStart = perf_trace_frame_active() ? SDL_GetPerformanceCounter() : 0;
    if (frame < g_ebldLastTick) g_ebldBld.clear();
    g_ebldLastTick = frame;
    for (std::map<long long, EbldBld>::iterator it = g_ebldBld.begin(); it != g_ebldBld.end(); ++it)
        it->second.seen = false;
    g_ebldLights.clear();
    const bool rule = ebld_rule_now();
    /* the bullets that vanished between the last dump and this one: same id AND name */
    std::vector<EfxBullet> gone;
    for (size_t i = 0; i < g_ebldBulletsPrev.size(); i++) {
        const EfxBullet& b = g_ebldBulletsPrev[i];
        bool still = false;
        for (size_t j = 0; j < g_efxBullets.size() && !still; j++)
            if (g_efxBullets[j].id == b.id && !strcmp(g_efxBullets[j].name, b.name)) still = true;
        if (!still) gone.push_back(b);
    }

    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        /* (a terrain object with a remaster, version 6: its idle loop's emitters only) */
        if ((o.kind != K_BUILDING && o.kind != K_TERRAIN) || o.id < 0 || o.limbo) continue;
        if (g_raModule && !ebld_ra_entry(o)) continue;
        if (o.kind == K_TERRAIN) {
            const int tm = ebld_model_of(o.type);
            if (tm < 0 || g_ebld[tm].nset != 1) continue;
        }
        const long long key = (long long)smooth_key(o);
        EbldBld* E = ebld_entry(o, false);
        if (E && E->stage == EB_WRECKED) { E->seen = true; E->str = o.str; continue; }
        if (E && E->deathClaimed) {
            E->seen = true;
            E->str = o.str;
            ebld_build_cut(g_ebld[E->model]);
            ebld_enter_wrecked(*E, g_ebld[E->model], frame, key);
            continue;
        }
        const int m = ebld_model_of(o.type);
        if (!rule || m < 0 || (o.kind == K_BUILDING && o.str <= 0)) {
            /* switched off, not ours, or dead without being claimed (the old shatter
               has it): the cartridge draws it */
            if (E) g_ebldBld.erase(key);
            continue;
        }
        if (!E) E = ebld_entry(o, true);
        if (!E) continue;
        /* a remastered tree: standing, burning or burnt out (etree_step) */
        if (o.kind == K_TERRAIN) etree_step(*E, o, m, frame);
        const EbldModel& M = g_ebld[E->model];
        E->seen = true;
        E->wx = o.wx; E->wz = o.wz;
        E->ground = terrain_y(o.wx, o.wz);
        E->fw = o.fw > 0 ? o.fw : 1;
        E->fh = o.fh > 0 ? o.fh : 1;
        E->house = mesh_house(o);
        E->str = o.str; E->maxstr = o.maxstr;
        E->blush = (o.blush != 0);
        E->face = o.face;
        E->serviceDoing=o.doing; E->serviceStage=o.dostage;
        E->servicePowerOff=(o.poweroff!=0);
        E->fillF = ebld_store_level(o.house);
        /* VERSION 6: the frame the cartridge's own model of it shows this tick, for a model
           whose parts follow the cartridge's clip (the Oil Pump), and a terrain object's
           idle loop's head start: a whole number of frames from its cell, so a stand of
           them does not move in step; its sync'd bursts keep to its own loop */
        E->animF = -1.0f;
        if (M.clipIntact >= 0) {
            const EbldClip& C = M.clip[M.clipIntact];
            if (!C.idle && g_raModule) {
                E->animF = ebld_ra_clip_at(M, o);
            } else if (!C.idle) {
                std::map<std::string, PackType>::const_iterator ct = g_pack.type.find(o.type);
                if (ct != g_pack.type.end() && ct->second.mesh >= 0
                    && ct->second.mesh < (int)g_pack.mesh.size())
                    E->animF = object_anim_t(ct->second.mesh, o);
            } else if (E->fresh && o.kind == K_TERRAIN) {
                const unsigned h = efx_hash(0x4542494Cu, (unsigned)o.cx, (unsigned)o.cy, 0u);
                E->idlePhase = (float)(h % (unsigned)C.frames) / C.fps;
            }
        }
        /* (the Weapons Factory's clips are its door, on the engine's door stage) */
        E->doorF = ebld_door_at(o);
        E->doorStage = E->doorF >= 0.0f ? o.doorstage : -1;
        if (E->doorF >= 0.0f && M.clipIntact >= 0 && !M.clip[M.clipIntact].idle)
            E->animF = E->doorF;
        /* (a Red Alert structure's clip its state drives, RA_DRIVE: the intact clip's frame,
           and the damaged model's clip on the same frame, as the War Factory's door) */
        if (g_raModule && M.raDrive >= 0 && M.clipIntact >= 0) {
            ebld_ra_drive_step(*E, M, o);
            E->animF = E->doorF = ebld_ra_drive_frame(*E, M, 1.0f);
            E->doorStage = o.doorstage;
        }
        /* (a placement still going up part by part past the engine's own construction,
           ebld_place.h, does not hold the idle effects back: they start on the tick the
           engine says the building is up, as they do without the placement, so the
           building a placement leaves is the building the cartridge's reveal leaves. A
           sale played backwards holds everything, and so does a placement still playing
           on a building hit while it went up: the damaged stage would otherwise draw the
           finished building in pieces over the placement from the tick the engine's
           build-up ends. Its damage shows when the placement is done, as the 1995
           build-up's damaged frame shows when the build-up is done) */
        E->standDown = building_constructing(o) || construction_frac(o) < 1.0f
                     || (eplc_holds(o) && (eplc_selling(o) || o.str < o.maxstr));
        /* the cut's meshes are made the first time a building of the type is hurt */
        if (M.nset >= 2 && o.str < o.maxstr) ebld_build_cut(g_ebld[E->model]);
        const float step = M.param[EBP_CHIP_STEP] * (float)(o.maxstr > 0 ? o.maxstr : 1);
        int steps = (int)floorf((float)(o.maxstr - o.str) / step + 1e-4f);
        if (steps < 0) steps = 0;
        E->hurt = cart_is_damaged(o);
        /* (a tree's stages carry their own emitters, as the intact state's: never the
           cartridge's half-strength rule) */
        if (o.kind == K_TERRAIN) E->hurt = false;
        /* GOING UP OR BEING SOLD (the placement draws it, ebld_place.h), and nothing is
           counted as a hit, so a building finished short of full strength does not
           throw its missing chips all at once when it is done. */
        if (E->standDown) {
            E->applied = steps;
            if (E->fresh) E->rose = true;
            /* the idle effects' clock starts when it is up, not pre-warmed */
            E->stageTick = frame;
            continue;
        }
        /* INTACT ONLY: no chips, no stages. It stays INTACT and is drawn whole at every
           strength; the cartridge's damage smoke and fire say how hurt it is. */
        if (M.nset == 1) {
            E->fresh = false;
            E->applied = steps;
            continue;
        }
        if (E->fresh) {
            /* FIRST SEEN ALREADY HURT -- a mission that starts with a damaged building,
               a saved game loaded, or Enhanced switched on mid-battle: the state it is
               in, as it would look had the hits that put it there been seen, with no
               EBLD|hit line and nothing moving on screen. Its missing chips are off the
               side the fire is coming from now (the last-resort south when nothing is),
               its damage sits in that side already, and below half its loose pieces
               are down too, all lying where they came to rest. Otherwise a reload put
               every chip back on and then played the whole collapse again.
               One that was first seen going up or being sold is the exception the
               stand-down above describes: it finished short of full strength with its
               chips still on, since none was ever knocked off it in view. */
            E->fresh = false;
            E->applied = steps;
            if (o.str < o.maxstr) {
                E->stage = EB_HELD;
                E->heldTick = frame;
                if (!E->rose && steps > 0) {
                    const char* how = "default";
                    const int zone = ebld_hit_zone(*E, o, 0, gone, &how);
                    E->lastZone = zone;
                    for (int s = 0; s < steps; s++) {
                        E->want[zone] += M.param[EBP_ZONE_ADD];
                        if (E->want[zone] > 1.0f) E->want[zone] = 1.0f;
                        ebld_launch_chip(*E, M, zone, frame, s, (unsigned)key);
                    }
                    for (int z = 0; z < 8; z++) E->cur[z] = E->curPrev[z] = E->want[z];
                }
            }
            if (cart_is_damaged(o))
                ebld_enter_damaged(*E, M, frame, key, 1);
            if (E->stage != EB_INTACT) ebld_settle(*E, M);
            continue;
        }
        if (o.str >= o.maxstr && E->stage != EB_INTACT) ebld_reset_intact(*E, M, frame);
        if (steps < E->applied) E->applied = steps;       /* repaired: nothing undone */
        int k = 0;
        while (E->applied < steps && o.str > 0) {
            const char* how = "default";
            const int zone = ebld_hit_zone(*E, o, k, gone, &how);
            E->lastZone = zone;
            if (E->stage != EB_DAMAGED) {
                if (E->stage == EB_INTACT) { E->stage = EB_HELD; E->heldTick = frame; }
                E->want[zone] += M.param[EBP_ZONE_ADD];
                if (E->want[zone] > 1.0f) E->want[zone] = 1.0f;
            }
            const int chip = ebld_launch_chip(*E, M, zone, frame, k, (unsigned)key);
            printf("EBLD|hit|key=%lld|zone=%d|how=%s|chip=%s|frame=%d\n", key, zone, how,
                   chip >= 0 ? M.set[EBS_CUT].piece[chip].name : "none", frame);
            E->applied++;
            k++;
        }
        if (cart_is_damaged(o) && o.str > 0 && E->stage != EB_DAMAGED)
            ebld_enter_damaged(*E, M, frame, key, 0);
    }
    /* THE WALLS' CELLS: their damage levels and the cells that were destroyed */
    ebld_wall_step(frame, gone);

    /* THE PHYSICS, THE WRECK TIMELINE AND THE EMITTERS, for every entry including wrecks
       whose building has already left the heap. */
    for (std::map<long long, EbldBld>::iterator it = g_ebldBld.begin(); it != g_ebldBld.end(); ) {
        EbldBld& E = it->second;
        const long long key = it->first;
        /* A SNAG OUTLIVES ITS TREE: the engine has deleted it, and its entry goes on under a
           key of its own until the snag has smoked out and settled (etree_*), unless the
           remasters were switched off */
        if (!E.seen && E.stage != EB_WRECKED && E.treeStage == 2 && rule && g_etreeOn
            && frame - E.treeTick < (int)((ETREE_SNAG_SECONDS + ETREE_SINK_SECONDS) * 15.0f)) {
            if (!E.orphan) {
                EbldBld keep = E;
                keep.orphan = true;
                printf("ETREE|orphan|%s|%d|%d|key=%lld|frame=%d\n", E.type, E.cx, E.cy, key,
                       frame);
                g_ebldBld.erase(it++);
                g_ebldBld[-(++g_ebldOrphan)] = keep;
                continue;
            }
        } else if (!E.seen && E.stage != EB_WRECKED) {
            if (E.orphan) printf("ETREE|gone|%s|%d|%d|frame=%d\n", E.type, E.cx, E.cy, frame);
            g_ebldBld.erase(it++);
            continue;
        }
        const EbldModel& M = g_ebld[E.model];
        EbldGround G;
        ebld_ground_of(E, M, G);
        if (E.stage == EB_HELD) {
            for (int z = 0; z < 8; z++) {
                E.curPrev[z] = E.cur[z];
                E.cur[z] += (E.want[z] - E.cur[z]) * 0.2f;   /* the viewer's 3/s at 15 Hz */
            }
        }
        if (E.stage == EB_HELD || E.stage == EB_DAMAGED) {
            for (size_t i = 0; i < E.cut.size(); i++) {
                if (E.cut[i].wet) {
                    ebld_body_sea_tick(E, M, E.cut[i], M.set[EBS_CUT].piece[i], frame, key, (int)i);
                    continue;
                }
                ebld_body_tick(E.cut[i], M.set[EBS_CUT].piece[i], &M.px[i], G);
                ebld_body_sea_in(E, M, E.cut[i], M.set[EBS_CUT].piece[i], G, frame, key, (int)i);
            }
        }
        if (E.stage == EB_WRECKED) {
            const int t = frame - E.blastTick;
            const int sinkAt = (int)(M.param[EBP_SINK_AFTER] * 15.0f + 0.5f);
            const int sinkEnd = sinkAt + (int)(M.param[EBP_SINK_TIME] * 15.0f + 0.5f);
            const int gone = (int)((M.param[EBP_SCORCH_SECONDS] + M.param[EBP_SCORCH_FADE])
                                   * 15.0f + 0.5f);
            if (!E.sinking) {
                const int ws = ebld_wreck_set(M);
                for (size_t i = 0; i < E.wreck.size(); i++) {
                    if (E.wreck[i].wet) {
                        ebld_body_sea_tick(E, M, E.wreck[i], M.set[ws].piece[i], frame, key, (int)i);
                        continue;
                    }
                    ebld_body_tick(E.wreck[i], M.set[ws].piece[i],
                                   ws == EBS_CUT ? &M.px[i] : NULL, G);
                    ebld_body_sea_in(E, M, E.wreck[i], M.set[ws].piece[i], G, frame, key, (int)i);
                }
                if (t >= sinkAt) {
                    /* the wreck stops moving and begins to go into the ground */
                    E.sinking = true;
                    for (size_t i = 0; i < E.wreck.size(); i++) {
                        EbldBody& B = E.wreck[i];
                        for (int c = 0; c < 3; c++) B.pp[c] = B.p[c];
                        for (int c = 0; c < 4; c++) B.pq[c] = B.q[c];
                        B.y0 = B.p[1];
                    }
                    printf("EBLD|sink|key=%lld|frame=%d\n", key, frame);
                }
            }
            /* (a piece in the sea goes on sinking by itself while the wreck on land goes
               into the ground) */
            if (E.sinking && !E.dropped) {
                const int ws = ebld_wreck_set(M);
                for (size_t i = 0; i < E.wreck.size(); i++)
                    if (E.wreck[i].wet)
                        ebld_body_sea_tick(E, M, E.wreck[i], M.set[ws].piece[i], frame, key, (int)i);
            }
            if (E.sinking && !E.dropped && t >= sinkEnd) {
                E.dropped = true;
                E.wreck.clear();
            }
            if (E.dropped && t >= gone && !E.seen) {
                printf("EBLD|gone|key=%lld|frame=%d\n", key, frame);
                g_ebldBld.erase(it++);
                continue;
            }
        }
        if ((E.seen || E.stage == EB_WRECKED || E.orphan) && !E.standDown)
            ebld_emit(E, M, frame, (unsigned)key);
        ++it;
    }
    g_ebldBulletsPrev = g_efxBullets;
    if (traceStart)
        g_destructTrace[21] += (SDL_GetPerformanceCounter()-traceStart)*1000000u/SDL_GetPerformanceFrequency();
    fflush(stdout);
}

/* ================================================================================
 * THE WALLS' STAGES (version 5, a staged wall). A wall is not an object: it is an overlay
 * on a cell, and all the engine says of it is its kind, its connection mask and its
 * damage level (WALL lines). So a wall cell's stages are read from that:
 *   - the damage level as the part of its strength lost: level d of the type's levels
 *     (the engine's own: sandbags 1, chain-link 2, concrete 3) is d / levels gone. Each
 *     5% of it is a chip, as a building's hits are; from half it is DAMAGED. Sandbags go
 *     from whole to gone in one level, so their damaged stage never shows in play; the
 *     chain-link is damaged at its first level; the concrete wall is held at its first
 *     and damaged at its second;
 *   - a cell that held a remastered wall last tick and holds none now is DESTROYED: its
 *     mask's shards are blasted from the cell's centre, its collapse dust plays, and the
 *     camera jolts as the lab jolts it for one cell (shake_cells 1).
 * A cell is played by its mask's own model (wallSub), latched when it is first damaged,
 * so a neighbour going later does not swap the pieces under a broken cell. Enhanced only,
 * like everything in this file.
 * ============================================================================== */
struct EbldWallSeen { int kind, icon; };
static std::map<long long, EbldWallSeen> g_ebldWallPrev;
/* (odata.cpp: sandbags 1, chain-link 2, concrete 3, barbed wire 1, the wooden fence 1: the
   last two go from whole to gone in one level, as the sandbags do) */
/* (Red Alert's fence, kind 5, has two, as its odata.cpp gives the chain link) */
static int ebld_wall_levels(int kind) { return kind == 1 || kind == 5 ? 2 : kind == 2 ? 3 : 1; }
static EbldBld* ebld_wall_entry(int x, int y, int kind, int icon, bool create)
{
    const long long key = ebld_wall_key(x, y);
    std::map<long long, EbldBld>::iterator it = g_ebldBld.find(key);
    if (it != g_ebldBld.end()) {
        if (!strcmp(it->second.type, EBLD_WALLS[kind])) return &it->second;
        g_ebldBld.erase(it);                  /* another wall on the cell now */
    }
    if (!create) return NULL;
    ebld_wall_models();
    const int o = g_ebldWallModel[kind];
    if (o < 0 || !g_ebld[o].wallStaged) return NULL;
    const int m = g_ebld[o].wallSub[icon & 15];
    if (m < 0 || !g_ebld[m].attached) return NULL;
    EbldBld& E = g_ebldBld[key];
    ebld_entry_init(E, m, EBLD_WALLS[kind], x, y, (float)x + 0.5f, (float)y + 0.5f, 1, 1);
    E.maxstr = ebld_wall_levels(kind);
    E.str = E.maxstr;
    return &E;
}
/* The side a wall cell was struck from: a bullet that vanished beside it, else the side
   it was struck from last, else the south. */
static int ebld_wall_zone(const EbldBld& E, const std::vector<EfxBullet>& gone, int k,
                          const char** how)
{
    std::vector<EbldVanished> cand;
    for (size_t i = 0; i < gone.size(); i++) {
        const float dx = gone[i].wx - E.wx, dz = gone[i].wz - E.wz;
        if (fabsf(dx) > 2.0f || fabsf(dz) > 2.0f) continue;
        EbldVanished v;
        v.x = dx; v.z = dz; v.d = dx * dx + dz * dz; v.face = gone[i].face; v.id = gone[i].id;
        cand.push_back(v);
    }
    if (!cand.empty()) {
        for (size_t i = 1; i < cand.size(); i++)
            for (size_t j = i; j > 0; j--) {
                const EbldVanished& a = cand[j - 1];
                const EbldVanished& b = cand[j];
                if (a.d < b.d || (a.d == b.d && a.id <= b.id)) break;
                std::swap(cand[j - 1], cand[j]);
            }
        const EbldVanished& v = cand[(size_t)k % cand.size()];
        *how = "bullet";
        if (fabsf(v.x) <= 0.5f && fabsf(v.z) <= 0.5f) {
            const float a = (float)v.face * (2.0f * EBLD_PI / 256.0f);
            return ebld_zone_of(-sinf(a), cosf(a));
        }
        return ebld_zone_of(v.x, v.z);
    }
    if (E.lastZone >= 0) { *how = "last"; return E.lastZone; }
    *how = "default";
    return 6;
}
static void ebld_wall_step(int frame, const std::vector<EfxBullet>& gone)
{
    const bool rule = ebld_rule_now();
    /* the walls read at first sight: a kind with a cell on the map (under Red Alert every
       kind, since every type of its is read at first sight, ebld_type_lazy) */
    if (rule) {
        bool seen[EBLD_NWALL] = { false, false, false, false, false, false };
        for (size_t i = 0; i < g_walls.size(); i++)
            if (g_walls[i].kind < EBLD_NWALL) seen[g_walls[i].kind] = true;
        for (int k = g_raModule ? 0 : 3; k < EBLD_NWALL; k++)
            if (seen[k]) ebld_ensure(EBLD_WALLS[k]);
    }
    ebld_wall_models();
    std::map<long long, EbldWallSeen> now;
    for (size_t i = 0; rule && i < g_walls.size(); i++) {
        const WallCell& w = g_walls[i];
        if (w.kind >= EBLD_NWALL || (w.kind == 5 && !ra3d_props_on())) continue;
        const int o = g_ebldWallModel[w.kind];
        if (o < 0 || !g_ebld[o].wallStaged) continue;
        const long long key = ebld_wall_key(w.x, w.y);
        EbldWallSeen ws = { (int)w.kind, (int)(w.icon & 15) };
        now[key] = ws;
        const int lv = ebld_wall_levels(w.kind);
        const int d = (int)w.dmg < lv ? (int)w.dmg : lv - 1;
        EbldBld* E = ebld_wall_entry(w.x, w.y, w.kind, w.icon, d > 0);
        if (!E) continue;
        if (E->stage == EB_WRECKED) continue;
        /* a cell that was a whole wall last tick is damaged before our eyes, not found so */
        if (E->fresh && g_ebldWallPrev.count(key)) E->fresh = false;
        E->seen = true;
        const EbldModel& M = g_ebld[E->model];
        ebld_build_cut(g_ebld[E->model]);
        E->str = lv - d;
        E->maxstr = lv;
        const int steps = (int)floorf(((float)d / (float)lv) / M.param[EBP_CHIP_STEP] + 1e-4f);
        const bool hurt = 2 * d >= lv;
        E->hurt = hurt;
        if (E->fresh) {
            /* first seen already damaged (a mission that starts with it): its stage as it
               would be, nothing thrown on screen */
            E->fresh = false;
            E->applied = steps;
            if (d > 0) {
                E->stage = EB_HELD;
                E->heldTick = frame;
                const char* how = "default";
                const int zone = ebld_wall_zone(*E, gone, 0, &how);
                E->lastZone = zone;
                for (int st = 0; st < steps; st++) {
                    E->want[zone] += M.param[EBP_ZONE_ADD];
                    if (E->want[zone] > 1.0f) E->want[zone] = 1.0f;
                    ebld_launch_chip(*E, M, zone, frame, st, (unsigned)key);
                }
                for (int z = 0; z < 8; z++) E->cur[z] = E->curPrev[z] = E->want[z];
            }
            if (hurt) ebld_enter_damaged(*E, M, frame, key, 1);
            if (E->stage != EB_INTACT) ebld_settle(*E, M);
            continue;
        }
        int k = 0;
        while (E->applied < steps) {
            const char* how = "default";
            const int zone = ebld_wall_zone(*E, gone, k, &how);
            E->lastZone = zone;
            if (E->stage != EB_DAMAGED) {
                if (E->stage == EB_INTACT) { E->stage = EB_HELD; E->heldTick = frame; }
                E->want[zone] += M.param[EBP_ZONE_ADD];
                if (E->want[zone] > 1.0f) E->want[zone] = 1.0f;
            }
            const int chip = ebld_launch_chip(*E, M, zone, frame, k, (unsigned)key);
            printf("EBLD|hit|key=%lld|zone=%d|how=%s|chip=%s|frame=%d\n", key, zone, how,
                   chip >= 0 ? M.set[EBS_CUT].piece[chip].name : "none", frame);
            E->applied++;
            k++;
        }
        if (d > 0 && E->stage == EB_INTACT) { E->stage = EB_HELD; E->heldTick = frame; }
        if (hurt && E->stage != EB_DAMAGED) ebld_enter_damaged(*E, M, frame, key, 0);
    }
    /* THE CELLS THAT WERE A WALL LAST TICK AND ARE NONE NOW: destroyed */
    for (std::map<long long, EbldWallSeen>::const_iterator it = g_ebldWallPrev.begin();
         rule && it != g_ebldWallPrev.end(); ++it) {
        if (now.count(it->first)) continue;
        const int x = (int)((it->first - EBLD_WALL_KEY0) % 512);
        const int y = (int)((it->first - EBLD_WALL_KEY0) / 512);
        EbldBld* E = ebld_wall_entry(x, y, it->second.kind, it->second.icon, true);
        if (!E || E->stage == EB_WRECKED) continue;
        E->seen = true;
        E->str = 0;
        ebld_build_cut(g_ebld[E->model]);
        ebld_enter_wrecked(*E, g_ebld[E->model], frame, it->first);
        /* the lab's death jolt for one cell */
        shake_note(g_ebld[E->model].param[EBP_SHAKE_PER_CELL]);
        cinematic_audio_note(CIN_COLLAPSE, E->wx, E->wz, 1);
    }
    g_ebldWallPrev.swap(now);
}

/* ================================================================================
 * THE DEATH'S EXPLOSION AND THE ENGINE'S OWN EFFECTS STANDING DOWN (Enhanced only).
 *
 * A remastered building dies as the remaster lab's Destroy plays it: one explosion, the
 * lab's recipe out of the file (its BOOM trailer: FRAG3's Enhanced flash, fireball, shock
 * ring, chunks with their ember streaks, sparks, dust, smoke column, fire, lingering smoke,
 * embers on the scorch and their wisps), at the death's centre, at the lab's timings, in
 * the approved effect sheets, through the game's own particle pool. The blast that throws
 * the wreck, the scorch mark and the jolt were this system's already.
 *
 * And nothing of the engine's own: BuildingClass::Take_Damage's death arm puts a fireball
 * (FBALL1) on every cell the building blocks, small and medium fires (FIRE3, FIRE2) on
 * half of them, and Drop_Debris a smoke column (SMOKE_M) when the wreck is cleared; its
 * damage arms attach fires to the building (FIRE3; BURN-S/M/L from fire; FLMSPT on the oil
 * pump). Those anims are taken out of the live anim list on the parse that reports them
 * (ebld_anims_filter), so neither the particle recipes, the dynamic lights nor anything
 * else sees them. The damaged stage's own fires, embers and smoke are the file's damaged
 * emitters. The brain is untouched: the anims still exist in the engine and in Classic.
 * ============================================================================== */
/* --eboomlab: every explosion's particles hashed exactly as the lab hashes its one (its
   anim key and tick 0), so a shot can be laid beside the lab's particle for particle. Off
   in play: each building's own key and death tick, so two deaths side by side differ. */
static bool g_ebldBoomLab = false;
static long g_ebldBoomFired = 0, g_ebldBoomParts = 0, g_ebldBoomChunks = 0;

/* THE ENGINE'S OWN TABLE as a recipe, for a file without the trailer: Enhanced's FRAG3 rows
   (efx_enhanced.h), at the slot indices they have in the game (after the cartridge's four,
   which Enhanced replaces). The anim's own length is FRAG3's, the lab's dur and until. */
static const std::vector<EbldBoomSlot>& ebld_boom_engine(void)
{
    static std::vector<EbldBoomSlot> v;
    if (!v.empty()) return v;
    for (int r = 0; r < 4; r++) {
        EbldBoomSlot S;
        memset(&S, 0, sizeof S);
        snprintf(S.name, sizeof S.name, "cartridge%d", r);
        S.flags = EBB_REP;
        S.every = 1;
        S.src.nvar = 1;
        v.push_back(S);
    }
    for (int j = 0; j < EFX_ENH_NEXTRA; j++) {
        const EfxExtra& x = EFX_ENH_EXTRA[j];
        if (strcmp(x.recipe, "FRAG3")) continue;
        EbldBoomSlot S;
        memset(&S, 0, sizeof S);
        snprintf(S.name, sizeof S.name, "%s", x.src.name);
        for (int i = 0; i < EFX_ENH_NSRCSHEET; i++)
            if (!strcmp(EFX_ENH_SRCSHEET[i].source, x.src.name))
                snprintf(S.sheet, sizeof S.sheet, "%s", EFX_ENH_SRCSHEET[i].sheet);
        S.src = x.src;
        S.src.name = NULL;
        S.stage = x.stage;
        S.flags = x.flat ? EBB_FLAT : 0;
        S.hold = x.hold;
        S.every = x.every > 0 ? x.every : 1;
        S.dx = x.dx; S.dy = x.dy; S.dz = x.dz;
        v.push_back(S);
    }
    return v;
}

/* One stage of one explosion: every slot that emits on it, by the lab's own rules
   (boomTick): a burst slot for its burst's ticks, its count falling by its multiplier; a
   continuous one while the anim lives, or for its own hold, on every Nth tick. */
static void ebld_boom_stage(EbldBld& E, const std::vector<EbldBoomSlot>& R, int dur, int st,
                            unsigned key, int born)
{
    for (size_t i = 0; i < R.size(); i++) {
        const EbldBoomSlot& S = R[i];
        if (S.flags & EBB_REP) continue;
        if (efx_explosion_ember(S.name, S.sheet)) continue;     /* (effects_mod.h) */
        const int k = st - S.stage;
        if (k < 0) continue;
        float count = S.src.ppt;
        if (S.src.burst) {
            if (k >= S.src.burst) continue;
            for (int j = 0; j < k; j++) count *= S.src.mult;
        } else {
            if (S.hold) { if (k > S.hold) continue; }
            else if (st >= dur) continue;
            if (S.every > 1 && k % S.every) continue;
        }
        const int n = (int)count;
        if (n <= 0) continue;
        EfxSourceRec src = S.src;
        src.name = S.name;
        const float ex = E.boomX + (float)S.dx * EFX_U;
        const float ey = E.boomY + (float)S.dy * EFX_U;
        const float ez = E.boomZ + (float)S.dz * EFX_U;
        if (src.sys == 2 || src.sys == 3) {
            const int before = g_efxNChunk;
            efx_emit_chunks(src, ex, ey, ez, n, key, born, (int)i, k, E.boomHouse);
            E.boomChunks += g_efxNChunk - before;
            g_ebldBoomChunks += g_efxNChunk - before;
        } else {
            efx_emit(src, ex, ey, ez, n, key, born, (int)i, k,
                     (unsigned char)((S.flags & EBB_FLAT) ? 1 : 0),
                     efx_sheet_slot(S.sheet, src.sys), true);
            E.boomParts += n;
            g_ebldBoomParts += n;
        }
    }
}

/* THE PARTICLE TICK'S OWN HOOK (g_efxBeforeIntegrate): after the anims' recipes fired and
   before the pool integrates, so a stage's particles take this tick's step as the lab's
   do. An explosion armed at a death is born here, on the death's own tick; a stage the
   clock skipped (a long stall) is fired late rather than lost. */
static void ebld_boom_step(int frame)
{
    for (std::map<long long, EbldBld>::iterator it = g_ebldBld.begin(); it != g_ebldBld.end(); ++it) {
        EbldBld& E = it->second;
        if (E.boomBorn == -1) continue;
        const EbldModel& M = g_ebld[E.model];
        const bool own = !M.boom.empty();
        const std::vector<EbldBoomSlot>& R = own ? M.boom : ebld_boom_engine();
        const int dur = own ? M.boomDur : 18, until = own ? M.boomUntil : 229;
        if (E.boomBorn == -2) {
            E.boomBorn = frame;
            E.boomLast = -1;
            E.boomY = efx_ground(E.boomX, E.boomZ);
            g_ebldBoomFired++;
            /* (and the jolt it was given where the death was seen, in footprint cells) */
            printf("EBLD|boom|key=%lld|frame=%d|x=%.3f|z=%.3f|recipe=%s|slots=%d|jolt=%.3f\n",
                   it->first, frame, E.boomX, E.boomZ, own ? "file" : "engine", (int)R.size(),
                   M.boomShake > 0.0f ? M.boomShake : (float)(E.fw > E.fh ? E.fw : E.fh));
        }
        const int k = frame - E.boomBorn;
        if (E.boomLast > until + 16) continue;
        const unsigned key = g_ebldBoomLab ? ((4u << 16) | 1u)
                           : efx_hash(0x424F4F4Du, (unsigned)it->first,
                                      (unsigned)((unsigned long long)it->first >> 32), 0u);
        const int born = g_ebldBoomLab ? 0 : E.boomBorn;
        for (int st = E.boomLast + 1; st <= k && st <= until + 16; st++)
            ebld_boom_stage(E, R, dur, st, key, born);
        E.boomLast = k;
    }
}

/* THE ENGINE'S ANIMS THAT STAND DOWN, by name: the death's (Take_Damage's fireballs and
   fires, Drop_Debris's smoke column) and the damage's (the fires its damage arms attach to
   the building, the oil pump's flame spout). */
static bool ebld_anim_death_name(const char* n)
{
    return !strcmp(n, "FBALL1") || !strcmp(n, "FIRE1") || !strcmp(n, "FIRE2")
        || !strcmp(n, "FIRE3") || !strcmp(n, "FIRE4") || !strcmp(n, "SMOKE_M");
}
static bool ebld_anim_burn_name(const char* n)
{
    return !strcmp(n, "FIRE1") || !strcmp(n, "FIRE2") || !strcmp(n, "FIRE3")
        || !strcmp(n, "FIRE4") || !strcmp(n, "BURN-S") || !strcmp(n, "BURN-M")
        || !strcmp(n, "BURN-L") || !strcmp(n, "FLMSPT") || !strcmp(n, "SMOKE_M");
}
/* how long after a death its anims are matched: the eight ticks the dead building stays,
   the tick Drop_Debris runs on, and its smoke column's delay of up to five */
#define EBLD_DEATH_WINDOW 24
/* how far outside the footprint a death anim may sit: the fires are scattered half a cell
   from the centre of the cell they belong to */
#define EBLD_DEATH_MARGIN 0.6f
struct EbldAnimOff { int stage, frame; const char* why; };
/* Whether a free anim stands on a building this system draws now: inside its footprint,
   EBLD_DEATH_MARGIN about. */
static bool ebld_anim_on_active(const EfxAnim& a, long long* owner)
{
    for (size_t j = 0; j < g_objects.size(); j++) {
        const SimObject& o = g_objects[j];
        if (o.kind != K_BUILDING || o.limbo || o.str <= 0) continue;
        const float hx = 0.5f * (float)(o.fw > 0 ? o.fw : 1) + EBLD_DEATH_MARGIN;
        const float hz = 0.5f * (float)(o.fh > 0 ? o.fh : 1) + EBLD_DEATH_MARGIN;
        if (fabsf(a.wx - o.wx) > hx || fabsf(a.wz - o.wz) > hz) continue;
        if (!ebld_active(o)) continue;
        *owner = (long long)smooth_key(o);
        return true;
    }
    return false;
}
static std::map<int, EbldAnimOff> g_ebldAnimOff;            /* (type << 16 | id) -> held */
static std::map<int, std::pair<int, int> > g_ebldAnimFirst; /* -> (first seen, last stage) */
static int g_ebldAnimFrame = -1;
static std::map<std::string, long> g_ebldAnimOffN[2];       /* death, damage: name -> count */

/* Called once the dump is parsed (every parse: a tick re-dumped is filtered again, to the
   same answer): the anims this system's buildings own are taken out of g_efxAnims. An anim
   once taken stays taken while it is reported dump after dump, so a fire that loses its
   building, loops, or outlives the death window, does not reappear mid-flame. */
static void ebld_anims_filter(int frame)
{
    if (frame < g_ebldAnimFrame) { g_ebldAnimOff.clear(); g_ebldAnimFirst.clear(); }
    g_ebldAnimFrame = frame;
    /* which remastered trees burn, read before any anim is taken out (etree_*) */
    etree_fires_seen();
    if (g_ebld.empty() || !ebld_rule_now()) {
        g_ebldAnimOff.clear();
        g_ebldAnimFirst.clear();
        return;
    }
    for (std::map<long long, EbldBld>::iterator it = g_ebldBld.begin(); it != g_ebldBld.end(); ++it)
        if (it->second.deathClaimed && it->second.deathTick < 0) it->second.deathTick = frame;
    std::map<int, EbldAnimOff> keep;
    std::map<int, std::pair<int, int> > first;
    size_t w = 0;
    for (size_t i = 0; i < g_efxAnims.size(); i++) {
        const EfxAnim& a = g_efxAnims[i];
        const int key = (a.type << 16) | (a.id & 0xFFFF);
        int born = frame;
        std::map<int, std::pair<int, int> >::const_iterator f = g_ebldAnimFirst.find(key);
        if (f != g_ebldAnimFirst.end() && a.stage >= f->second.second) born = f->second.first;
        first[key] = std::make_pair(born, a.stage);
        const char* why = NULL;
        long long owner = 0;
        std::map<int, EbldAnimOff>::const_iterator h = g_ebldAnimOff.find(key);
        /* (held while it is seen dump after dump, whatever its stage does: a fire loops,
           and its stage starting again is the same fire) */
        if (h != g_ebldAnimOff.end() && frame - h->second.frame <= 4) {
            why = h->second.why;
        } else if (a.attkind == (g_raModule ? 5 : 7) /* RTTI_BUILDING, each brain's */
                   && a.attref >= 0 && ebld_anim_burn_name(a.name)) {
            /* (under Red Alert, a civilian building reused with its stages or a staged structure, whose damaged
               models carry their own fire: ebld_ra_stages) */
            for (size_t j = 0; j < g_objects.size(); j++) {
                const SimObject& o = g_objects[j];
                if (o.kind != K_BUILDING || o.id != a.attref) continue;
                if (g_raModule ? (g_efxEnhOn && ebld_ra_stages(o) && ebld_find(o) != NULL)
                               : ebld_active(o)) {
                    why = "damage"; owner = (long long)smooth_key(o);
                }
                break;
            }
        } else if (a.attkind == etree_rtti_terrain() && ebld_anim_burn_name(a.name)) {
            /* A REMASTERED TREE'S FIRE: its burning model carries the tree's own flames,
               so the engine's two fire anims on it stand down (etree_*) */
            const int oi = etree_fire_owner(a);
            if (oi >= 0 && etree_stage_model(g_objects[(size_t)oi], 1) >= 0) {
                why = "treefire";
                owner = (long long)smooth_key(g_objects[(size_t)oi]);
            }
        } else if (a.attref < 0 && ebld_anim_burn_name(a.name)
                   && ebld_anim_on_active(a, &owner)) {
            /* the engine's invisible companion of a fire it attached (a *_VIRTUAL anim,
               spawned free beside it), or a fire that came loose: on a building of ours */
            why = "damage";
        }
        /* (under Red Alert too while its 3D view draws OpenCNC3D's effects, which is when
           the lab's blast for a reused civilian building shows, ra3d_effects_mod.h) */
        if (!why && (!g_raModule || g_efxEnhOn) && a.attref < 0 && ebld_anim_death_name(a.name)) {
            for (std::map<long long, EbldBld>::const_iterator it = g_ebldBld.begin();
                 it != g_ebldBld.end() && !why; ++it) {
                const EbldBld& E = it->second;
                if (E.deathTick < 0 || frame - E.deathTick > EBLD_DEATH_WINDOW
                    || born < E.deathTick || it->first >= EBLD_WALL_KEY0)
                    continue;
                const float hx = 0.5f * (float)E.fw + EBLD_DEATH_MARGIN;
                const float hz = 0.5f * (float)E.fh + EBLD_DEATH_MARGIN;
                if (fabsf(a.wx - E.wx) <= hx && fabsf(a.wz - E.wz) <= hz) {
                    why = "death";
                    owner = it->first;
                }
            }
        }
        if (why) {
            if (h == g_ebldAnimOff.end() || h->second.frame < frame - 4) {
                g_ebldAnimOffN[strcmp(why, "death") ? 1 : 0][a.name]++;
                if (!strcmp(why, "treefire")) g_etreeStood++;
                printf("EBLD|standdown|anim=%s|id=%d|why=%s|key=%lld|at=%.2f,%.2f|frame=%d\n",
                       a.name, a.id, why, owner, a.wx, a.wz, frame);
            }
            EbldAnimOff k;
            k.stage = a.stage; k.frame = frame; k.why = why;
            keep[key] = k;
            continue;
        }
        g_efxAnims[w++] = a;
    }
    g_efxAnims.resize(w);
    g_ebldAnimOff.swap(keep);
    g_ebldAnimFirst.swap(first);
}

/* AND THE MARKS THE ENGINE'S DEATH LEAVES ON THE GROUND: a crater on every cell the
   building blocks as it dies (Take_Damage), a scorch or a crater on each again when the
   wreck is cleared (Drop_Debris). They are the engine's death, as its fireballs are, and they
   lie under the lab's own scorch mark and outlast it for good, so they stand down too: a
   crater or scorch (SmudgeType 0..11; never a building's apron) that appears on a cell of a
   remastered building's footprint, or a cell beside it (Drop_Debris scatters its crater up to
   half a cell), within its death window. One that was there before the death (a shell's
   crater) stays. Taken out of g_smudges on every parse while it lies unchanged; a new mark
   on the cell later (a shell landing on the bare ground) is drawn. */
/* (the filter's memory, one per list it runs on: Tiberian Dawn's g_smudges, and the craters
   Red Alert's 3D pass reads from its engine's map, ebld_marks_filter_ra) */
struct EbldSmudgeHold {
    std::map<int, int> prev;   /* cell -> type << 8 | data, last dump, all */
    std::map<int, int> off;    /* cell -> the mark held back */
    int frame;
    EbldSmudgeHold() : frame(-1) {}
};
static EbldSmudgeHold g_ebldSmudgeHold, g_ebldSmudgeHoldRa;
static long g_ebldSmudgeOffN = 0;
static void ebld_smudges_filter_on(std::vector<SmudgeCell>& cells, EbldSmudgeHold& H, int frame)
{
    if (frame < H.frame) { H.prev.clear(); H.off.clear(); }
    const bool newTick = frame != H.frame;
    H.frame = frame;
    std::map<int, int> now;
    for (size_t i = 0; i < cells.size(); i++)
        now[(int)cells[i].y * 512 + cells[i].x] = (cells[i].type << 8) | cells[i].data;
    if (g_ebld.empty() || !ebld_rule_now()) {
        H.off.clear();
        if (newTick) H.prev.swap(now);
        return;
    }
    std::map<int, int> keep;
    size_t w = 0;
    for (size_t i = 0; i < cells.size(); i++) {
        const SmudgeCell& c = cells[i];
        const int ck = (int)c.y * 512 + c.x, v = (c.type << 8) | c.data;
        std::map<int, int>::const_iterator h = H.off.find(ck);
        bool off = (h != H.off.end() && h->second == v);
        if (!off && c.type < 12) {
            std::map<int, int>::const_iterator pv = H.prev.find(ck);
            /* (new since the last tick's dump: a tick re-dumped compares with the same) */
            const bool fresh = (pv == H.prev.end() || pv->second != v);
            for (std::map<long long, EbldBld>::const_iterator it = g_ebldBld.begin();
                 fresh && it != g_ebldBld.end() && !off; ++it) {
                const EbldBld& E = it->second;
                if (E.deathTick < 0 || frame - E.deathTick > EBLD_DEATH_WINDOW
                    || it->first >= EBLD_WALL_KEY0)
                    continue;
                if (c.x >= E.cx - 1 && c.x <= E.cx + E.fw && c.y >= E.cy - 1 && c.y <= E.cy + E.fh) {
                    off = true;
                    if (!newTick) break;          /* (said on the tick's first parse) */
                    g_ebldSmudgeOffN++;
                    printf("EBLD|standdown|smudge=%d|cell=%d,%d|why=death|key=%lld|frame=%d\n",
                           (int)c.type, (int)c.x, (int)c.y, it->first, frame);
                }
            }
        }
        if (off) { keep[ck] = v; continue; }
        cells[w++] = c;
    }
    cells.resize(w);
    H.off.swap(keep);
    if (newTick) H.prev.swap(now);
}
static void ebld_smudges_filter(int frame)
{
    ebld_smudges_filter_on(g_smudges, g_ebldSmudgeHold, frame);
}
/* RED ALERT'S CRATERS, the same rule. Red Alert's dump has no SMUDGE| lines: its craters
   and scorch marks are read from the engine's map on the same parse (ra3d_cells_from_map)
   and handed here after the deaths are observed, so a civilian building remastered from
   OpenCNC3D's art does not lie over its own engine's death craters either. Only the stages this file plays
   under Red Alert claim a death (ebld_ra_stages): an intact-only structure's is not held. */
static void ebld_marks_filter_ra(std::vector<SmudgeCell>& cells, int frame)
{
    ebld_smudges_filter_on(cells, g_ebldSmudgeHoldRa, frame);
}

/* Mission boundary and shutdown: the entries hold engine frames and positions from the
   timeline being abandoned. The models stay; they do not depend on the mission. */
static void ebld_reset(void)
{
    g_ebldBld.clear();
    g_ebldWallPrev.clear();
    g_ebldLights.clear();
    g_ebldPoseSeen.clear();
    g_ebldBulletsPrev.clear();
    g_ebldLastTick = -1;
    g_ebldOrphan = 0;
    g_ebldAnimOff.clear();
    g_ebldAnimFirst.clear();
    g_ebldAnimFrame = -1;
    g_ebldSmudgeHold = EbldSmudgeHold();
    g_ebldSmudgeHoldRa = EbldSmudgeHold();
}

/* ================================================================================
 * THE DRAW. Every piece through draw_mesh, with the same 3x4 and triangle range the old
 * shatter uses, so there is one vertex pipeline and the shadow, reflection and relight
 * passes see exactly the geometry the colour pass does.
 * ============================================================================== */

/* THE UV HOOK for one run: a flipbook's frame, and a pouring liquid's scroll. The frame is
   f = floor(t * fps) mod frames of a grid x grid sheet, row-major from the top, which in
   the pack's v-down convention is a scale of 1/grid and an offset of
   (f % grid, f / grid) / grid. A flipbook on a piece that has fallen is held at its first
   frame (`still`): a lamp on a fallen piece is out.
   THE SCROLL IS +speed * seconds IN v-DOWN. The viewer scrolls its texture offset by
   -speed * seconds in its own v-up texture space, and v_pack = 1 - v_up turns that into
   +speed * seconds on the pack's v: the same rows pass the same way down the surface. */
static void ebld_uv_set(const EbldModel& M, int texIdx, float fallSec, float speed = 0.0f,
                        bool still = false)
{
    float su = 1.0f, sv = 1.0f, ou = 0.0f, ov = 0.0f;
    if (texIdx >= 0 && texIdx < (int)M.tex.size()) {
        const EbldTex& T = M.tex[texIdx];
        if (T.grid > 1) {
            int f = 0;
            if (T.frames > 1 && T.fpsf > 0.0f && !still) {
                f = (int)floorf(engine_time() / 15.0f * T.fpsf);
                f %= T.frames;
                if (f < 0) f += T.frames;
            }
            su = sv = 1.0f / (float)T.grid;
            ou = (float)(f % T.grid) / (float)T.grid;
            ov = (float)(f / T.grid) / (float)T.grid;
        }
    }
    if (fallSec >= 0.0f) {
        const float s = speed * fallSec;
        ov += s - floorf(s);
    }
    g_meshUVScaleU = su; g_meshUVScaleV = sv;
    g_meshUVOffU = ou;   g_meshUVOffV = ov;
}
static void ebld_uv_clear(void)
{
    g_meshUVScaleU = g_meshUVScaleV = 1.0f;
    g_meshUVOffU = g_meshUVOffV = 0.0f;
}
/* ================================================================================
 * THE WAVE (version 5): a sheet moving in the wind, the Barracks' flag, as the lab's
 * WAVES plays it (index.html: waveAt, waveRest, waveLight, waveApply), number for
 * number. Every waved triangle's corners are moved across the cloth by the moving wave
 * less the still one the model was built with, pulled in toward the hoist so the cloth
 * keeps its length, their normals turned with the cloth, and their colour given the
 * light of the folds. A pure function of the time, so the same moment is the same pose
 * however it was reached; a piece torn off keeps the pose it had when it tore. The pack
 * mesh's own vertices are rewritten in place before the runs that hold them are drawn
 * (the meshes are shared by every building of the type, and the pose depends only on the
 * time). Plain C++ on the CPU, so the Win98 build could do the same.
 * THE FOLD LIGHT GOES ABOVE WHITE (up to about 1.4 of the painted colour), which a vertex
 * colour cannot carry: a waved triangle of a colour mesh is flagged (PackMesh::tfl bit
 * 2) and stores half its colour, and draw_mesh draws those triangles with the texture
 * environment's colour scale at 2. The lab multiplies a linear colour; here the factor
 * goes through the display's 2.2 so the folds read the same.
 * ============================================================================== */
static float ebld_wave_gust(const float* a, float t)
{
    return 1.0f - a[20] * (0.5f + 0.5f * sinf(2.0f * EBLD_PI * a[21] * t)
                                      * sinf(2.0f * EBLD_PI * a[22] * t + 1.0f));
}
/* the moving wave's sideways offset at (s, z, t) and its slopes along and up the sheet */
static void ebld_wave_at(const EbldWave& W, float s, float z, float t, float gust,
                         float* q, float* qs, float* qz)
{
    const float* a = W.a;
    const float L = a[7], zm = 0.5f * (a[8] + a[9]);
    float zh = 0.5f * (a[9] - a[8]);
    if (zh < 1e-6f) zh = 1e-6f;
    const float f0 = (z - zm) / zh, f = f0 * f0 < 1.0f ? f0 * f0 : 1.0f;
    const float fz = fabsf(f0) < 1.0f ? 2.0f * f0 / zh : 0.0f;
    const float u = -f0, uz = -1.0f / zh;
    const float x = (s > 1e-6f ? s : 1e-6f) / L;
    float Q = 0.0f, QS = 0.0f, QZ = 0.0f;
    for (size_t m = 0; m + 6 < W.mode.size(); m += 7) {
        const float* md = &W.mode[m];
        const float amp = md[0], wl = md[1], hz = md[2], pw = md[3], lag = md[4], skew = md[5];
        const float env = powf(x, pw), denv = pw / L * powf(x, pw - 1.0f);
        const float k = 2.0f * EBLD_PI / wl;
        const float ph = k * s - 2.0f * EBLD_PI * hz * t - lag * f - skew * u + md[6];
        const float sn = sinf(ph), cs = cosf(ph);
        Q += amp * env * sn;
        QS += amp * (denv * sn + env * cs * k);
        QZ -= amp * env * cs * (lag * fz + skew * uz);
    }
    *q = Q * gust; *qs = QS * gust; *qz = QZ * gust;
}
/* the still wave the model carries, and its slope along the sheet */
static void ebld_wave_rest(const EbldWave& W, float s, float* r, float* rs)
{
    const float* a = W.a;
    const float L = a[7], x = (s > 1e-6f ? s : 1e-6f) / L, P = 2.0f * EBLD_PI / a[16];
    const float env = powf(x, a[17]), denv = a[17] / L * powf(x, a[17] - 1.0f);
    *r = a[15] * env * sinf(P * s);
    *rs = a[15] * (denv * sinf(P * s) + env * cosf(P * s) * P);
}
/* the fold light the lab's pass paints on a cloth normal (pack frame) */
static float ebld_wave_light(const float* a, const float* n)
{
    float L[3] = { a[24], a[26], -a[25] };
    const float l = sqrtf(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
    if (l > 1e-6f) for (int c = 0; c < 3; c++) L[c] /= l;
    float lam = (n[0] * L[0] + n[1] * L[1] + n[2] * L[2] + a[29]) / (1.0f + a[29]);
    if (lam < 0.0f) lam = 0.0f;
    if (lam > 1.0f) lam = 1.0f;
    return a[27] + a[28] * lam;
}
static void eb_unit(float* v)
{
    const float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-12f) for (int c = 0; c < 3; c++) v[c] /= l;
}
/* Pose the waved triangles of one of a model's pack meshes at t seconds, those in
   [r0, r1) or all of them. */
static void ebld_wave(EbldModel& M, int mk, float t, int r0 = 0, int r1 = 0x7fffffff)
{
    if (mk < 0 || mk >= EBM_N || M.waveMesh[mk].empty()) return;
    const bool full = (r0 == 0 && r1 == 0x7fffffff);
    if (full && M.wavePosed[mk] == t) return;
    const int mi = M.mesh[mk];
    if (mi < 0 || mi >= (int)g_pack.mesh.size()) return;
    PackMesh& pm = g_pack.mesh[mi];
    pm.gpuDeformed = true; /* positions, normals and fold colours change in place */
    pm.gpuPoseRevision++;
    if (pm.gpuWaveTris.size() != M.waveMesh[mk].size()) {   /* the triangles that move */
        pm.gpuWaveTris.clear();
        for (size_t e = 0; e < M.waveMesh[mk].size(); e++)
            pm.gpuWaveTris.push_back(M.waveMesh[mk][e].first);
        std::sort(pm.gpuWaveTris.begin(), pm.gpuWaveTris.end());
    }
    const int q = (mk == EBM_INTACT) ? 0 : 1;
    const bool colour = (mk == EBM_INTACT || mk == EBM_CUT || mk == EBM_CUTDMG);
    const bool nrm = pm.nrm.size() == pm.tris.size() * 9;
    for (size_t e = 0; e < M.waveMesh[mk].size(); e++) {
        const int mt = M.waveMesh[mk][e].first;
        if (mt < r0 || mt >= r1 || mt >= (int)pm.tris.size()) continue;
        const int record = M.waveMesh[mk][e].second;
        const EbldWaveT& T = M.waveT[q][record];
        EbldWavePose* cached = NULL;
        bool cacheHit = false;
        if (record >= 0 && (size_t)record < M.waveCache[q].size()) {
            EbldWaveCache& cache = M.waveCache[q][record];
            for (unsigned slot = 0; slot < 2; slot++) {
                if (cache.pose[slot].valid && cache.pose[slot].time == t) {
                    cached = &cache.pose[slot];
                    cacheHit = true;
                    cache.next = 1 - slot;
                    break;
                }
            }
            if (!cached) {
                cached = &cache.pose[cache.next];
                cache.next = (cache.next + 1) % 2;
                cached->valid = false;
                cached->time = t;
            }
        }
        const EbldWave& W = M.wave[T.w];
        const float* a = W.a;
        const float gust = ebld_wave_gust(a, t), g = 1.0f;
        const float AL[3] = { a[2], 0.0f, -a[3] }, AC[3] = { a[4], 0.0f, -a[5] };
        for (int k = 0; k < 3; k++) {
            const EbldWaveV& V = T.v[k];
            PackVert& P = pm.tris[mt].v[k];
            float n[3] = { V.n[0], V.n[1], V.n[2] };
            float kc = 1.0f;
            if (cacheHit) {
                P.x = cached->position[k * 3];
                P.y = cached->position[k * 3 + 1];
                P.z = cached->position[k * 3 + 2];
                for (int component = 0; component < 3; component++)
                    n[component] = cached->normal[k * 3 + component];
                kc = cached->light[k];
            } else {
                float qv, qs, qz, r, rs;
                ebld_wave_at(W, V.s, V.z, t, gust, &qv, &qs, &qz);
                ebld_wave_rest(W, V.s, &r, &rs);
                const float dq = g * (qv - r), es = rs + g * (qs - rs), ez = g * qz;
                /* the cloth keeps its length along each row: the free edge comes in */
                float ds = 0.0f;
                if (a[23] > 0.0f && V.s > 0.0f) {
                    for (int st = 0; st < 12; st++) {
                        const float sk = V.s * ((float)st + 0.5f) / 12.0f;
                        float q2, a2, z2, r2, b2;
                        ebld_wave_at(W, sk, V.z, t, gust, &q2, &a2, &z2);
                        ebld_wave_rest(W, sk, &r2, &b2);
                        const float mm = b2 + g * (a2 - b2);
                        ds -= 0.5f * (mm * mm - b2 * b2) * V.s / 12.0f;
                    }
                    ds *= a[23];
                }
                P.x = V.p[0] + 1024.0f * (a[4] * dq + a[2] * ds);
                P.y = V.p[1];
                P.z = V.p[2] - 1024.0f * (a[5] * dq + a[3] * ds);
                if (V.side) {
                    float zf = (V.z - a[8]) / (a[9] - a[8] > 1e-6f ? a[9] - a[8] : 1e-6f);
                    if (zf < 0.0f) zf = 0.0f;
                    if (zf > 1.0f) zf = 1.0f;
                    const float sp = V.s > 0.0f ? V.s : 0.0f;
                    const float dds = a[18] > 0.0f
                                    ? a[18] * 2.0f * sp / (a[7] * a[7]) * (a[19] + (1.0f - a[19]) * zf)
                                    : 0.0f;
                    float wts[3], wtz[3], wd[3], wr[3];
                    for (int c = 0; c < 3; c++) { wts[c] = AL[c] + AC[c] * es; wtz[c] = AC[c] * ez; }
                    wts[1] -= dds;
                    wtz[1] += 1.0f;
                    eb_cross(wtz, wts, wd);
                    for (int c = 0; c < 3; c++) wd[c] *= (float)V.side;
                    eb_unit(wd);
                    const float up[3] = { 0.0f, 1.0f, 0.0f };
                    for (int c = 0; c < 3; c++) wts[c] = AL[c] + AC[c] * rs;
                    wts[1] -= dds;
                    eb_cross(up, wts, wr);
                    for (int c = 0; c < 3; c++) wr[c] *= (float)V.side;
                    eb_unit(wr);
                    for (int c = 0; c < 3; c++) n[c] += wd[c] - wr[c];
                    eb_unit(n);
                    if (a[31] > 0.5f)
                        kc = 1.0f + a[30] * (ebld_wave_light(a, wd) / ebld_wave_light(a, wr) - 1.0f);
                }
                if (cached) {
                    cached->position[k * 3] = P.x;
                    cached->position[k * 3 + 1] = P.y;
                    cached->position[k * 3 + 2] = P.z;
                    for (int component = 0; component < 3; component++)
                        cached->normal[k * 3 + component] = n[component];
                    cached->light[k] = kc;
                }
            }
            if (nrm)
                for (int c = 0; c < 3; c++) pm.nrm[(size_t)mt * 9 + k * 3 + c] = n[c];
            if (colour) {
                if (kc < 0.0f) kc = 0.0f;
                kc = powf(kc, 1.0f / 2.2f);
                const unsigned char* c0 = V.c;
                unsigned char* d[3] = { &P.r, &P.g, &P.b };
                for (int c = 0; c < 3; c++) {
                    float v = (float)c0[c] * kc * 0.5f + 0.5f;
                    if (v > 255.0f) v = 255.0f;
                    *d[c] = (unsigned char)v;
                }
            }
        }
        if (cached) cached->valid = true;
        if (colour && (size_t)mt < pm.tfl.size()) pm.tfl[mt] |= 2;
    }
    M.wavePosed[mk] = full ? t : -1.0e30f;
}
/* The draws hold their model const; posing writes only the shared pack mesh and its
   cache, which belong to the draw. */
static void ebld_wave_c(const EbldModel& M, int mk, float t, int r0 = 0, int r1 = 0x7fffffff)
{
    if (M.wave.empty()) return;
    ebld_wave(const_cast<EbldModel&>(M), mk, t, r0, r1);
}
/* A fallen piece's flag keeps the pose it had when it tore: both of its runs. */
static void ebld_wave_body(const EbldModel& M, int mk, const int* r, const EbldBody& B)
{
    if (M.wave.empty()) return;
    const float t = (float)B.tore / 15.0f;
    ebld_wave_c(M, mk, t, r[EBR_A], r[EBR_B]);
    ebld_wave_c(M, mk, t, r[EBR_RA], r[EBR_RB]);
}

/* THE DRAW STATE the viewer gives these pieces, which the rest of this renderer does not
   use, and without which the cut model does not look like the whole one:
   - ONE-SIDED. The viewer draws front faces only, except on the pieces the file marks
     DOUBLE (the water, and any loose open shell). Drawn two-sided, the downward faces
     of a cut show through the surfaces above them: a vent's grille came out covered by
     the underside of its own cut, and a chip lying face up showed its lining.
   - THE BROKEN FACES PUSHED BACK. A rim runs along a cut at right angles to the surface
     and a lining lies a hair under it, so both meet a surface along whole edges. A small
     depth offset on them (factor 1, units 2, the viewer's) makes the surface win
     wherever they meet, instead of dark seams fighting through along every cut line.
   The normal target uses the same culling and broken-face offset as the colour
   picture, so an internal lining cannot supply a false normal along a roof's cut seam.
   Sun casters use the same front faces but retain the sun pass's own slope bias.
   Reflection and relight keep their auxiliary state. Saved and restored per draw. */
struct EbldGL {
    bool on;
    GLboolean cull, offset;
    GLint cullMode, front;
    GLfloat factor, units;
};
static EbldGL ebld_gl_begin(bool aux)
{
    EbldGL s;
    memset(&s, 0, sizeof s);
    /* Colour, normals and sun agree on the shell's front faces. The normal target
       also keeps internal cut faces behind the surface, avoiding false shadow seams. */
    s.on = !aux || g_m3CasterDepth || g_m3NormalPass;
    if (!s.on) return s;
    s.cull = glIsEnabled(GL_CULL_FACE);
    s.offset = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    s.cullMode=gls_cull_mode();
    s.front=gls_front_face();
    gls_polygon_offset_values(&s.factor,&s.units);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_CULL_FACE);
    if (!g_m3CasterDepth) {
        glPolygonOffset(1.0f, 2.0f);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    return s;
}
static void ebld_gl_end(const EbldGL& s)
{
    if (!s.on) return;
    if (s.cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (s.offset) glEnable(GL_POLYGON_OFFSET_FILL); else glDisable(GL_POLYGON_OFFSET_FILL);
    glCullFace((GLenum)s.cullMode);
    glFrontFace((GLenum)s.front);
    glPolygonOffset(s.factor, s.units);
}
/* inside a begin/end pair: a two-sided piece, and the block of broken faces */
static void ebld_gl_sides(const EbldGL& s, bool two)
{
    if (!s.on) return;
    if(!mesh_fragment_polygon_set(GL_CULL_FACE,!two)) {
        if (two) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
    }
}
static void ebld_gl_broken(const EbldGL& s, bool broken)
{
    if (!s.on || g_m3CasterDepth) return; /* retain the sun pass's slope bias */
    if(!mesh_fragment_polygon_set(GL_POLYGON_OFFSET_FILL,broken)) {
        if (broken) glEnable(GL_POLYGON_OFFSET_FILL); else glDisable(GL_POLYGON_OFFSET_FILL);
    }
}

/* A WHOLE WALL CELL IS ONE-SIDED TOO, as the viewer draws it. draw_walls runs with culling
   off (the cartridge's own draw state), and a remastered wall drawn that way showed the
   faces turned away from the camera wherever nothing solid stood in front of them. On the
   chain-link fence, the barbed wire and the wooden fence that is every cut-out panel: each
   is two faces back to back, one per side, lit and painted for its own side. Drawn
   two-sided, the two lie at the same depth and the depth test picks one by the order they
   come in the piece: the far side's face, seen from behind, in the arms that run one way,
   the near side's in the arms that run the other. So a run shaded half a cell dark, half a
   cell light, alternately (measured on a chain-link run: the wire at 34 against 70 grey),
   in the colour picture and in the normal target alike, whose shader turns a back face's
   normal round. Culled, the camera sees each panel's near face only, everywhere, as the
   viewer shows it; the solid walls draw the same but for a few silhouette pixels.
   The colour picture and the normal target only: the sun's depth wants both faces (a
   panel casts its shadow either way), the water's reflection mirrors the winding, and the
   relight's bake is not a view. The state is handed back as found. */
struct EbldWallCull { bool on, cull; GLint mode, front; };
/* HELD ACROSS A RUN OF WALL CELLS. Every remastered cell enters the one-sided state and
   leaves it again, so a line of wall toggled the cull state twice a cell. While draw_walls
   holds it (ebld_wall_cull_hold), the state is entered at the first cell that asks and
   left only before a draw that is not bracketed (the cartridge's own wall, a shadow set)
   and at the end of the walk, which restores what the first cell found: every draw sees
   the state it saw before, without the pair of toggles per cell. */
static bool g_ebldWallCullLazy = false, g_ebldWallCullHeld = false;
static unsigned long g_ebldWallCullEntered = 0, g_ebldWallCullCells = 0;
static EbldWallCull g_ebldWallCullSaved = { false, false, GL_BACK, GL_CCW };
static void ebld_wall_cull_end(const EbldWallCull& s);
static EbldWallCull ebld_wall_cull_begin(void)
{
    EbldWallCull s = { false, false, GL_BACK, GL_CCW };
    if (g_m3CasterDepth || g_m3Mirrored || g_meshBakePass) return s;
    if (g_ebldWallCullLazy) {
        g_ebldWallCullCells++;
        if (!g_ebldWallCullHeld) {
            g_ebldWallCullEntered++;
            g_ebldWallCullLazy = false;
            g_ebldWallCullSaved = ebld_wall_cull_begin();
            g_ebldWallCullLazy = g_ebldWallCullHeld = true;
        }
        return s;      /* (not on: the end leaves the state held) */
    }
    s.on = true;
    s.cull = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
    s.mode=gls_cull_mode();
    s.front=gls_front_face();
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_CULL_FACE);
    return s;
}
static void ebld_wall_cull_end(const EbldWallCull& s)
{
    if (!s.on) return;
    if (s.cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glCullFace((GLenum)s.mode);
    glFrontFace((GLenum)s.front);
}
/* Leave a held wall state now (before an unbracketed draw); the hold stays. */
static void ebld_wall_cull_release(void)
{
    if (!g_ebldWallCullHeld) return;
    g_ebldWallCullHeld = false;
    ebld_wall_cull_end(g_ebldWallCullSaved);
}
/* draw_walls' hold: on for its walk, off (and released) at its end. */
static void ebld_wall_cull_hold(bool on)
{
    if (!on) ebld_wall_cull_release();
    g_ebldWallCullLazy = on;
}

/* THE SUN'S DEPTH PASS LEAVES THE BROKEN FACES OUT for a model with the REST flag: the
   linings lie inside their pieces and the rims are a hundredth of a cell thick, so they
   add nothing to a shadow, and they are close to half a cut model's triangles. Set by
   ebld_draw for that pass only. */
static bool g_ebldNoBroken = false;

/* A run of a mesh at rest in its building, optionally through a matrix (the drain, a turn,
   a loop, a bend). */
static void ebld_draw_rest(const EbldBld& E, int mi, int r0, int r1, int pass, const float* Mx)
{
    if (r1 <= r0) return;          /* triLast 0 would mean "to the end" */
    const EbldModel& M=g_ebld[E.model];
    const int phase=bservice_phase(M.service.kind,E.serviceDoing,E.serviceStage,
                                  E.servicePowerOff,E.str>0 && E.stage!=EB_WRECKED);
    bservice_draw(M,mi,r0,r1,phase,[&](int mesh,int a,int b){
        draw_mesh(mesh, E.wx, E.wz, -1, pass, 0, 0.0f, E.house, 1.0f, 0.0f, false,
                  WOBBLE_NONE, -1.0f, Mx, (size_t)a, (size_t)b);
    });
}
/* THE PIECES STILL STANDING GO AS FEW DRAWS AS POSSIBLE. Every mesh keeps its pieces'
   runs contiguous and in piece order within each of its two blocks, so consecutive
   standing pieces are one range; a piece that has come off leaves a gap, and the gap is
   what ends a run. */
struct EbldRuns {
    const EbldBld* E;
    int mi, pass, r0, r1;
    void add(int a, int b)
    {
        if (b <= a) return;
        if (r0 >= 0 && a != r1) flush();
        if (r0 < 0) r0 = a;
        r1 = b;
    }
    void flush()
    {
        if (r0 >= 0) ebld_draw_rest(*E, mi, r0, r1, pass, NULL);
        r0 = r1 = -1;
    }
};
static EbldRuns ebld_runs(const EbldBld& E, int mi, int pass)
{
    EbldRuns R;
    R.E = &E; R.mi = mi; R.pass = pass; R.r0 = R.r1 = -1;
    return R;
}
/* The end of a piece's plain surfaces: where its flipbook triangles begin, or its end. */
static int ebld_fa(const int* r) { return r[EBR_FA] >= 0 ? r[EBR_FA] : r[EBR_B]; }
/* EVERY STANDING PIECE the filter lets through, surfaces (less their flipbook triangles,
   drawn apart with their frame) then broken faces, each block as few runs as possible. */
template <class Keep>
static void ebld_draw_standing(const EbldBld& E, const EbldSet& S, int mi,
                               const std::vector<int>& rg, int pass, const EbldGL& st,
                               Keep keep)
{
    for (int blk = 0; blk < (g_ebldNoBroken ? 1 : 2); blk++) {
        ebld_gl_broken(st, blk == 1);
        EbldRuns run = ebld_runs(E, mi, pass);
        for (size_t k = 0; k < S.piece.size(); k++) {
            if (E.cut[k].live || !keep(k)) continue;
            const int* r = &rg[k * EBR];
            run.add(blk ? r[EBR_RA] : r[EBR_A], blk ? r[EBR_RB] : ebld_fa(r));
        }
        run.flush();
    }
    ebld_gl_broken(st, false);
}
/* A piece's flipbook triangles, each texture's run with its own frame. */
static void ebld_draw_tail(const EbldBld& E, const EbldModel& M, int mi, const int* r, int pass,
                           const float* Mx, bool still, float px, float pz, float lift)
{
    const int a = ebld_fa(r), b = r[EBR_B];
    if (b <= a || mi < 0 || mi >= (int)g_pack.mesh.size()) return;
    const std::vector<PackTri>& T = g_pack.mesh[mi].tris;
    int i = a;
    while (i < b) {
        int j = i + 1;
        while (j < b && T[j].tex == T[i].tex) j++;
        ebld_uv_set(M, T[i].tex - M.tex0, -1.0f, 0.0f, still);
        const int phase=bservice_phase(M.service.kind,E.serviceDoing,E.serviceStage,
                         E.servicePowerOff,!still && E.str>0 && E.stage!=EB_WRECKED);
        bservice_draw(M,mi,i,j,phase,[&](int mesh,int a,int b){
            draw_mesh(mesh, px, pz, -1, pass, 0, 0.0f, E.house, 1.0f, lift, false, WOBBLE_NONE,
                      -1.0f, Mx, (size_t)a, (size_t)b);
        });
        ebld_uv_clear();
        i = j;
    }
}
/* One piece of a mesh at a body's pose, lerped between the last two ticks: its surfaces,
   then its broken faces pushed back. `sink` lowers a sinking wreck from where it
   stopped; the slab fades where it lies. `fillY` flattens a fill to its level. */
static void ebld_draw_body(const EbldBld& E, const EbldModel& M, int mi, const int* r, int pass,
                           const EbldPiece& P, const EbldBody& B, bool sinking, float sink,
                           const EbldGL& st, float fillY = 3.0e38f)
{
    if (r[EBR_B] <= r[EBR_A] && r[EBR_RB] <= r[EBR_RA]) return;
    if (B.sunk) return;                             /* gone under the sea */
    /* (a piece in the sea sinks by itself: it is drawn where its own motion has it) */
    if (B.wet) sinking = false;
    const float a = sinking ? 0.0f : g_tickAlpha;   /* 0 under --shot and --script */
    float p[3], q[4];
    for (int c = 0; c < 3; c++) p[c] = B.pp[c] + (B.p[c] - B.pp[c]) * a;
    float d = B.pq[0] * B.q[0] + B.pq[1] * B.q[1] + B.pq[2] * B.q[2] + B.pq[3] * B.q[3];
    const float sg = d < 0.0f ? -1.0f : 1.0f;
    for (int c = 0; c < 4; c++) q[c] = B.pq[c] + (sg * B.q[c] - B.pq[c]) * a;
    eb_qnorm(q);
    if (sinking) p[1] = (P.flags & EBF_SLAB) ? B.y0 : B.y0 - sink;
    float R[9], Mx[12];
    eb_qmat(q, R);
    for (int i = 0; i < 3; i++) {
        for (int c = 0; c < 3; c++) Mx[i * 4 + c] = R[i * 3 + c];
        Mx[i * 4 + 3] = -(R[i * 3] * P.c[0] + R[i * 3 + 1] * P.c[1] + R[i * 3 + 2] * P.c[2]);
    }
    const float lift = p[1] - terrain_y(p[0], p[2]);
    const bool two = (P.flags & EBF_DOUBLE) != 0;
    if (two) ebld_gl_sides(st, true);
    const bool cacheWas = g_meshBatchRestPose;
    g_meshBatchRestPose = (B.asleep || B.pinned) && !sinking;
    g_meshFillY = fillY;
    for (int blk = 0; blk < (g_ebldNoBroken ? 1 : 2); blk++) {
        const int r0 = blk ? r[EBR_RA] : r[EBR_A], r1 = blk ? r[EBR_RB] : ebld_fa(r);
        if (r1 > r0) {
            ebld_gl_broken(st, blk == 1);
            bservice_draw(M,mi,r0,r1,0,[&](int mesh,int first,int last){
                draw_mesh(mesh, p[0], p[2], -1, pass, 0, 0.0f, E.house, 1.0f, lift, false,
                          WOBBLE_NONE, -1.0f, Mx, (size_t)first, (size_t)last);
            });
        }
        if (blk == 0) {
            ebld_gl_broken(st, false);
            ebld_draw_tail(E, M, mi, r, pass, Mx, true, p[0], p[2], lift);
        }
    }
    g_meshFillY = 3.0e38f;
    ebld_gl_broken(st, false);
    if (two) ebld_gl_sides(st, false);
    g_meshBatchRestPose = cacheWas;
}
/* A STANDING PIECE THAT MOVES (turned, looping, bent), at its pose this frame, through
   draw_mesh's extra matrix: its surfaces, its flipbook triangles with their frame, then
   its broken faces pushed back. */
static void ebld_draw_moving(const EbldBld& E, const EbldModel& M, int mi, const int* r, int pass,
                             const EbldPiece& P, const float* Mx, const EbldGL& st, bool tail)
{
    const bool two = (P.flags & EBF_DOUBLE) != 0;
    if (two) ebld_gl_sides(st, true);
    ebld_draw_rest(E, mi, r[EBR_A], ebld_fa(r), pass, Mx);
    if (tail) ebld_draw_tail(E, M, mi, r, pass, Mx, false, E.wx, E.wz, 0.0f);
    ebld_gl_broken(st, true);
    if (!g_ebldNoBroken) ebld_draw_rest(E, mi, r[EBR_RA], r[EBR_RB], pass, Mx);
    ebld_gl_broken(st, false);
    if (two) ebld_gl_sides(st, false);
}

/* Which passes are not the player's colour picture: the sun's depth, the water's
   reflection, the relight's bake and the normal target. The overlay, the glow and the
   scorch belong to the colour picture only. */
static bool ebld_aux_pass(void)
{
    return g_m3CasterDepth || g_m3Mirrored || g_meshBakePass || g_m3NormalPass
        || g_placeGhostPass;
}
/* What the player can see of the building, by the rule visible() applies to it. */
static bool ebld_anchor_shown(const EbldBld& E)
{
    if (!g_vShowBuildings) return false;
    if (g_objShroud) return shroud_vis_at(E.wx, E.wz) > 0.0f;
    return !shroud_cell_hidden((int)E.wx, (int)E.wz);
}

/* THE SHROUD, PRICED ONCE A BUILDING. draw_mesh darkens every vertex by the shroud where
   it stands, which is ten lookups a vertex, and a cut model has ten times the vertices of
   a whole one. Where no corner of the shroud field under the building and everything it
   can have thrown is darkened at all, every vertex would subtract nothing, so the draw is
   told to skip the lookups: the same picture, computed once instead of per vertex. */
static bool ebld_shroud_clear(const EbldBld& E)
{
    if (!g_objShroud || g_shroudEditorOff) return true;
    if (!g_shroudRimOn && (!g_shroudOn || !g_shroudValid || g_shroudRevealed)) return true;
    shroud_corners_sync();
    const float r = 0.5f * (float)(E.fw > E.fh ? E.fw : E.fh) + 3.5f;
    const int x0 = (int)floorf(E.wx - r), x1 = (int)floorf(E.wx + r) + 1;
    const int z0 = (int)floorf(E.wz - r), z1 = (int)floorf(E.wz + r) + 1;
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++)
            if (shroud_corner_a(x, z) != 0.0f) return false;
    return true;
}

/* OFF THE PLAYER'S SCREEN, NOT DRAWN in the colour picture: a building and all it can have
   thrown lie outside the box the camera can see this frame (the terrain's own cull box,
   with room for the tallest piece). The sun and the reflection see more than the player
   and keep drawing it. */
static bool ebld_off_screen(const EbldBld& E, bool aux)
{
    if (aux || !g_cullValid || !g_cullOn) return false;
    const float r = 0.5f * (float)(E.fw > E.fh ? E.fw : E.fh) + 3.5f + 3.0f;
    return E.wx + r < g_cullX0 || E.wx - r > g_cullX1 || E.wz + r < g_cullZ0 || E.wz - r > g_cullZ1;
}

/* A drain, as the viewer steps it: the level falls on an ease-out curve, the body of
   liquid shrinks onto its floor, and it fades after a third of the time. A pour starts at
   once, thins as the level falls, and is gone soon after. */
static void ebld_drain(const EbldBld& E, const EbldLiquid& L, float* scale, float* alpha,
                       float* fall, float* shift = NULL)
{
    const float T = L.seconds > 0.01f ? L.seconds : 0.01f;
    float t = (engine_time() - (float)E.drainTick) / 15.0f;
    if (t < 0.0f) t = 0.0f;
    float k = t / T;
    if (k > 1.0f) k = 1.0f;
    const float lvl0 = L.a, drop = L.b;
    const float flo = L.c;
    const float lvl = lvl0 - (lvl0 - drop) * (1.0f - powf(1.0f - k, 1.6f));
    float s = 1.0f;
    if (lvl0 - flo > 1e-3f) s = (lvl - flo) / (lvl0 - flo);
    if (s < 0.001f) s = 0.001f;
    *scale = s;
    /* (version 6, SHIFT: a drain with no floor is moved down by the level's drop instead) */
    if (shift) *shift = lvl - lvl0;
    float al = 1.0f - ((k - 0.35f) / 0.65f > 0.0f ? (k - 0.35f) / 0.65f : 0.0f);
    *alpha = al < 0.0f ? 0.0f : al;
    /* the pour starts at once, thins as the level falls, and is gone soon after */
    const float on = t / 0.4f < 1.0f ? t / 0.4f : 1.0f;
    float off = 1.0f - ((t - T * 0.55f) / (T * 0.55f) > 0.0f ? (t - T * 0.55f) / (T * 0.55f) : 0.0f);
    if (off < 0.0f) off = 0.0f;
    *fall = 0.9f * on * off;
}

/* How far a sinking wreck has gone: e runs 0..1 over the sink, and the depth follows it
   on a smoothstep. Both 0 until the wreck starts to sink. */
static void ebld_sink(const EbldBld& E, const EbldModel& M, float now, float* e, float* sink)
{
    *e = 0.0f;
    *sink = 0.0f;
    if (!E.sinking) return;
    const float T = M.param[EBP_SINK_TIME] > 0.01f ? M.param[EBP_SINK_TIME] : 0.01f;
    float k = ((now - (float)E.blastTick) / 15.0f - M.param[EBP_SINK_AFTER]) / T;
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;
    *e = k;
    *sink = M.param[EBP_SINK_DEPTH] * k * k * (3.0f - 2.0f * k);
}

/* Whether the scorch was made for one footprint cell and is stretched to the building. */
static bool ebld_scorch_cell(const EbldModel& M)
{
    return M.nset == 1 || (M.flags & EBX_SCORCHCELL) != 0;
}
/* A wreck piece's fill level (pack y): the level the fill stood at when the building died,
   -1e30 when it was empty, and no level (3e38) for any other piece. */
static float ebld_wreck_fill(const EbldBld& E, const EbldModel& M, int k)
{
    if (M.nset != 2 || k >= (int)M.liqOf.size() || M.liqOf[k] < 0
        || M.liquid[M.liqOf[k]].kind != EBL_FILL)
        return 3.0e38f;
    return E.fillWreck;
}

/* THE OVERLAY'S DRAW STATE, for one building's zones. The overlay used to be cut in with
   an alpha test whose threshold fell as the zone's progress rose. A zone's weight is a
   corner value, 1 on the zone's own bearing and 0 a whole zone away, and a yard's slab is
   a few large triangles, so the test's pass line was the straight iso-line of a linearly
   interpolated weight across the slab's flat face; near full progress it lay where the
   weight reached zero, the sector's own edge: a perfectly straight seam between sooted
   and clean concrete, with the whole step of the soot across it (a damaged atlas is about
   a quarter darker than its standing one over most of its area). BLENDED INSTEAD, by the
   same weight times the progress, the soot thins to nothing along the same line and the
   noise breaks it into patches, so there is no edge anywhere. The alpha test stays on,
   parked at zero, only so a fragment the blend could not change is dropped rather than
   written.
   THE TEXTURE ENVIRONMENT (GL_COMBINE, GL 1.3, the stage the doubled flags already use)
   scales the alpha. The noise alpha was equalised so a threshold t passed a fraction t of
   the texels, and averages a half, so blended as it is a fully struck zone would show at
   most half of its soot and the damaged stage would then arrive as a jump. Scaled by
   EBLD_OVERLAY_GAIN the darker patches saturate first: a fully struck zone shows most of
   its soot, as the test showed it, and a single hit, a quarter of progress, reads as a
   light mottled haze whose darkest patches are already full. Everything is handed back as
   found: the cutout pass runs with the blend off and its test at a half, and nothing else
   in it reads the stage. */
static const float EBLD_OVERLAY_GAIN = 4.0f;   /* 1 to 4: the stage scales by 4 at most */
struct EbldOverlayGL { GLint env, src, dst; GLboolean blend; GLfloat scale; };
static void ebld_overlay_begin(EbldOverlayGL& s)
{
    glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &s.env);
    glGetTexEnvfv(GL_TEXTURE_ENV, GL_ALPHA_SCALE, &s.scale);
    glGetIntegerv(GL_BLEND_SRC, &s.src);
    glGetIntegerv(GL_BLEND_DST, &s.dst);
    s.blend = glIsEnabled(GL_BLEND);
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
    glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
    glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 4.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glAlphaFunc(GL_GREATER, 0.0f);
}
/* The draw's alpha scale for a zone at progress c: the corner weight times this, times
   the stage's four, is the gain times the progress. */
static unsigned char ebld_overlay_alpha(float c)
{
    float k = c * (EBLD_OVERLAY_GAIN / 4.0f);
    if (k > 1.0f) k = 1.0f;
    return (unsigned char)(255.0f * k + 0.5f);
}
static void ebld_overlay_end(const EbldOverlayGL& s)
{
    g_meshAlphaScale = 255;
    glAlphaFunc(GL_GREATER, 0.5f);
    glBlendFunc((GLenum)s.src, (GLenum)s.dst);
    if (!s.blend) glDisable(GL_BLEND);
    glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, s.scale);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, s.env);
}

/* ONE ENTRY'S PIECES in one pass. `fading` selects the pieces below full opacity, which
   need the blended micro-pass; everything else draws in its own mode's pass. */
static void ebld_draw_entry(const EbldBld& E, const EbldModel& M, int pass, bool fading,
                            bool aux, const EbldGL& st)
{
    const float now = engine_time();
    if (E.stage == EB_HELD || E.stage == EB_DAMAGED) {
        const bool dmg = (E.stage == EB_DAMAGED);
        const int mk = dmg ? EBM_CUTDMG : EBM_CUT;
        const int mi = M.mesh[mk];
        const EbldSet& S = M.set[EBS_CUT];
        const std::vector<int>& rg = M.rng[mk];
        ebld_wave_c(M, mk, now / 15.0f);         /* the flag on what stands, now */
        if (!E.standDown) {
            if (!fading) {
                g_meshFlash = g_flashOn && E.blush;
                /* glass is drawn from both sides, as the viewer draws it */
                const bool two = (pass == MODE_XLU && M.hasTwo);
                if (two) ebld_gl_sides(st, true);
                /* the pieces still standing, merged into as few runs as possible */
                ebld_draw_standing(E, S, mi, rg, pass, st, [&](size_t k) {
                    return M.liqOf[k] < 0 && !ebld_stand_moves(E, M, (int)k);
                });
                /* their flipbooks (a glowing pool, a signal chase), each with its frame */
                for (size_t k = 0; k < S.piece.size(); k++) {
                    const int* r = &rg[k * EBR];
                    if (E.cut[k].live || M.liqOf[k] >= 0 || ebld_fa(r) >= r[EBR_B]
                        || ebld_stand_moves(E, M, (int)k))
                        continue;
                    ebld_draw_tail(E, M, mi, r, pass, NULL, false, E.wx, E.wz, 0.0f);
                }
                if (two) ebld_gl_sides(st, false);
                /* the standing pieces that move: turned with their turning part, looping,
                   bent */
                for (size_t k = 0; k < S.piece.size(); k++) {
                    if (E.cut[k].live || M.liqOf[k] >= 0 || !ebld_stand_moves(E, M, (int)k))
                        continue;
                    EbXf X;
                    float Mx[12];
                    if (!ebld_stand_xf(E, M, (int)k, now, &X)) continue;
                    ebxf_mat(X, Mx);
                    if (two) ebld_gl_sides(st, true);
                    ebld_draw_moving(E, M, mi, &rg[k * EBR], pass, S.piece[k], Mx, st, true);
                    if (two) ebld_gl_sides(st, false);
                }
            }
            /* THE LIQUIDS, one run each with their own UVs: a body of water or a pool
               standing, then once broken shrinking onto its floor as it fades; a pour
               (a waterfall, a spill) only once broken, scrolling and fading; the fill at
               the owner's store, sinking once broken */
            for (size_t k = 0; k < S.piece.size(); k++) {
                const EbldPiece& P = S.piece[k];
                if (E.cut[k].live || M.liqOf[k] < 0) continue;
                const EbldLiquid& L = M.liquid[M.liqOf[k]];
                const int* r = &rg[k * EBR];
                const bool fall = (L.kind == EBL_FALL);
                if (fall && !dmg) continue;              /* nothing pours until it breaks */
                float al = 1.0f, fillY = 3.0e38f;
                const float* Mx = NULL;
                float Wm[12];
                if (L.kind == EBL_FILL) {
                    float fr = 0.0f;
                    fillY = ebld_fill_y(E, M, now, &fr);
                    if (!(fr > 0.0f)) continue;         /* empty: nothing to see */
                } else if (dmg) {
                    float wScale = 1.0f, wAlpha = 1.0f, fAlpha = 0.0f, wShift = 0.0f;
                    ebld_drain(E, L, &wScale, &wAlpha, &fAlpha, &wShift);
                    if (!fall) {
                        const float flo = L.c;
                        const bool sh = (L.flags & EBL_SHIFT) != 0;
                        const float W[12] = { 1, 0, 0, 0,
                                              0, sh ? 1.0f : wScale, 0,
                                              sh ? wShift : flo * (1.0f - wScale),
                                              0, 0, 1, 0 };
                        memcpy(Wm, W, sizeof W);
                        Mx = Wm;
                        al = wAlpha;
                    } else {
                        al = fAlpha;
                        /* a spill fed by the fill pours only while the silo holds some,
                           and only if its level stood above the spill when it broke */
                        if (L.flags & EBL_GATE) {
                            float fr = 0.0f;
                            (void)ebld_fill_y(E, M, now, &fr);
                            if (!(fr > 0.0f) || !(E.fillH0 > L.gate)) al = 0.0f;
                        }
                    }
                }
                if (al <= 0.0f) continue;
                int as = (int)(255.0f * al + 0.5f);
                if (aux) { if (as < 128) continue; as = 255; }
                else if ((as < 255) != fading) continue;
                const bool fl = g_meshFlash;
                g_meshFlash = false;
                g_meshAlphaScale = (unsigned char)as;
                ebld_gl_sides(st, (P.flags & EBF_DOUBLE) != 0);
                ebld_uv_set(M, r[EBR_TEX], fall ? (now - (float)E.drainTick) / 15.0f : -1.0f,
                            L.speed);
                g_meshFillY = fillY;
                ebld_draw_rest(E, mi, r[EBR_A], r[EBR_B], pass, Mx);
                g_meshFillY = 3.0e38f;
                ebld_uv_clear();
                ebld_gl_sides(st, false);
                g_meshAlphaScale = 255;
                g_meshFlash = fl;
            }
            g_meshFlash = false;
            /* THE DAMAGE CREEPING IN, zone by zone: the same surfaces again, blended over
               the standing ones by the zone's weight at each corner times the zone's
               progress, through a texture whose alpha is patch noise, so the soot arrives
               in patches rather than as a flat tint (ebld_overlay_begin has the state and
               the reason). Same vertices, same draw, so they win the depth test with no
               bias. Colour picture only, and only on pieces still standing.
               NOT WHILE IT BLUSHES: the surfaces under it have just been drawn white, and
               the overlay would put its textured patches back on top of them, so a struck
               side flashed white with its damage blotches still showing. The cartridge's
               own overlay whitens with the rest of the object for the same reason. */
            if (!dmg && pass == MODE_CUTOUT && !fading && !aux && !(g_flashOn && E.blush)) {
                const float a = g_tickAlpha;
                EbldOverlayGL ov;
                bool on = false;
                for (int z = 0; z < 8; z++) {
                    const float c = E.curPrev[z] + (E.cur[z] - E.curPrev[z]) * a;
                    if (c <= 0.01f) continue;
                    if (!on) { ebld_overlay_begin(ov); on = true; }
                    g_meshAlphaScale = ebld_overlay_alpha(c);
                    const std::vector<int>& og = M.rng[EBM_OVER0 + z];
                    const int omi = M.mesh[EBM_OVER0 + z];
                    ebld_wave_c(M, EBM_OVER0 + z, now / 15.0f);
                    EbldRuns orun = ebld_runs(E, omi, pass);
                    for (size_t k = 0; k < S.piece.size(); k++)
                        if (!E.cut[k].live && !ebld_stand_moves(E, M, (int)k))
                            orun.add(og[k * EBR + EBR_A], og[k * EBR + EBR_B]);
                    orun.flush();
                    /* and on the standing pieces that move, where they are now */
                    for (size_t k = 0; k < S.piece.size(); k++) {
                        if (E.cut[k].live || !ebld_stand_moves(E, M, (int)k)) continue;
                        const int o0 = og[k * EBR + EBR_A], o1 = og[k * EBR + EBR_B];
                        EbXf X;
                        float Mx[12];
                        if (o1 <= o0 || !ebld_stand_xf(E, M, (int)k, now, &X)) continue;
                        ebxf_mat(X, Mx);
                        ebld_draw_rest(E, omi, o0, o1, pass, Mx);
                    }
                }
                if (on) ebld_overlay_end(ov);
            }
        }
        /* the pieces that are off: chips, and once it breaks the loose parts */
        if (!fading)
            for (size_t k = 0; k < S.piece.size(); k++) {
                if (!E.cut[k].live) continue;
                ebld_wave_body(M, mk, &rg[k * EBR], E.cut[k]);
                ebld_draw_body(E, M, mi, &rg[k * EBR], pass, S.piece[k], E.cut[k], false, 0.0f,
                               st);
            }
        return;
    }
    if (E.stage == EB_WRECKED) {
        if (!E.dropped) {
            float e = 0.0f, sink = 0.0f;
            ebld_sink(E, M, now, &e, &sink);
            int as = (int)(255.0f * (1.0f - e) + 0.5f);
            bool show = as > 0;
            if (aux) { show = show && as >= 128; as = 255; }
            else if ((as < 255) != fading) show = false;
            if (show) {
                /* the shards, or with no DESTROYED set the CUT pieces in their damaged look */
                const int wm = (M.nset == EBS_N) ? EBM_WRECK : EBM_CUTDMG;
                const int mi = M.mesh[wm];
                const EbldSet& S = M.set[ebld_wreck_set(M)];
                const std::vector<int>& rg = M.rng[wm];
                /* A FADING WRECK LAYS ITS DEPTH DOWN FIRST. The micro-pass it fades in
                   writes no depth, so its two hundred shards would composite in piece
                   order rather than depth order: a shard behind drawn after one in front
                   painted over it, the ground slab (drawn late) covered everything lying
                   on it, and the ember glow that follows, testing against a depth buffer
                   with none of the wreck in it, added through every hidden broken face.
                   The pile scrambled and brightened on the tick it began to sink. So the
                   same bodies go down once into depth only, cutouts under their own alpha
                   test, and the colour is then drawn where it is nearest: the front
                   surface shows, as the viewer's transparent material draws it, and the
                   glow is hidden where the shards hide it. The translucent faces keep
                   writing no depth, as they do everywhere. Fixed function only: a colour
                   mask and a depth mask.
                   THE SLAB IS LEFT OUT OF IT. It stays where it lay and fades in place
                   rather than sinking, so its depth would outlast its picture: a slab
                   almost gone still hid the infantry and the fire drawn after it. Tested
                   but not written, it still cannot paint over a shard lying on it. The
                   shards themselves sink as they fade, so what depth they leave goes
                   into the ground with them.
                   AND ONLY WHILE IT IS AT LEAST HALF THERE, the same line the sun and
                   reflection casters draw it to (`aux` above). Past it the shards are
                   more than half sunk and their glow under half strength, and a shard
                   still writing depth at a tenth of its opacity showed as a dark cut-out
                   in the fire drawn behind it. */
                const bool prime = fading && !aux && pass != MODE_XLU && as >= 128;
                GLint depthFunc = GL_LEQUAL, alphaFunc = GL_GREATER;
                GLfloat alphaRef = 0.5f;
                GLboolean depthMask = GL_FALSE, alphaTest = GL_FALSE;
                if (prime) {
                    glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
                    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
                    alphaTest = glIsEnabled(GL_ALPHA_TEST);
                    glGetIntegerv(GL_ALPHA_TEST_FUNC, &alphaFunc);
                    glGetFloatv(GL_ALPHA_TEST_REF, &alphaRef);
                    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                    glDepthMask(GL_TRUE);
                    glDepthFunc(GL_LEQUAL);
                    if (pass == MODE_CUTOUT) {
                        glEnable(GL_ALPHA_TEST);
                        glAlphaFunc(GL_GREATER, 0.5f);
                    }
                    g_meshAlphaScale = 255;
                    {
                        MeshFragmentScope fragments(&E,0u+(unsigned)pass*4u);
                        for (size_t k = 0; k < S.piece.size() && k < E.wreck.size(); k++) {
                            if (S.piece[k].flags & (EBF_WATER | EBF_WATERFALL)) continue;
                            if ((S.piece[k].flags & EBF_SLAB) || !E.wreck[k].live) continue;
                            ebld_draw_body(E, M, mi, &rg[k * EBR], pass, S.piece[k], E.wreck[k],
                                           E.sinking, sink, st, ebld_wreck_fill(E, M, (int)k));
                        }
                    }
                    if (pass == MODE_CUTOUT) {
                        if (!alphaTest) glDisable(GL_ALPHA_TEST);
                        glAlphaFunc((GLenum)alphaFunc, alphaRef);
                    }
                    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                    glDepthMask(depthMask);
                }
                g_meshAlphaScale = (unsigned char)as;
                {
                    MeshFragmentScope fragments(&E,1u+(unsigned)pass*4u);
                    for (size_t k = 0; k < S.piece.size() && k < E.wreck.size(); k++) {
                        if (S.piece[k].flags & (EBF_WATER | EBF_WATERFALL)) continue;
                        if (!E.wreck[k].live) continue;
                        const float fy = ebld_wreck_fill(E, M, (int)k);
                        if (fy < -1.0e29f) continue;          /* an empty fill: nothing there */
                        ebld_draw_body(E, M, mi, &rg[k * EBR], pass, S.piece[k], E.wreck[k],
                                       E.sinking, sink, st, fy);
                    }
                }
                g_meshAlphaScale = 255;
                if (prime) glDepthFunc((GLenum)depthFunc);
            }
        }
        /* THE SCORCH, on the ground under it all, following the terrain vertex by
           vertex. It stays, then fades. Colour picture only. */
        if (pass == MODE_XLU && !fading && !aux) {
            const float t = (now - (float)E.blastTick) / 15.0f;
            const float fd = M.param[EBP_SCORCH_FADE] > 0.01f ? M.param[EBP_SCORCH_FADE] : 0.01f;
            float f = 1.0f - (t - M.param[EBP_SCORCH_SECONDS]) / fd;
            if (f > 1.0f) f = 1.0f;
            if (f > 0.0f) {
                g_meshAlphaScale = (unsigned char)(255.0f * f + 0.5f);
                g_meshGroundPerVertex = true;
                ebld_gl_sides(st, true);
                const int smi = M.mesh[EBM_SCORCH];
                const int n = (int)g_pack.mesh[smi].tris.size();
                /* a mark sized per footprint cell (an intact-only model's, and one borrowed
                   by a model with no DESTROYED set) is stretched to the footprint of the
                   building it lies under */
                const float Sx[12] = { (float)E.fw, 0, 0, 0,  0, 1, 0, 0,  0, 0, (float)E.fh, 0 };
                /* centred on the death's centre (version 6: a civilian building's blocked
                   cells), the footprint's centre otherwise */
                if (n > 0)
                    draw_mesh(smi, E.wx + M.param[EBP_BLAST_X] * MODEL_SCALE,
                              E.wz + M.param[EBP_BLAST_Z] * MODEL_SCALE, -1, MODE_XLU, 0, 0.0f,
                              E.house, 1.0f, 0.012f,
                              false, WOBBLE_NONE, -1.0f, ebld_scorch_cell(M) ? Sx : NULL, 0,
                              (size_t)n);
                ebld_gl_sides(st, false);
                g_meshGroundPerVertex = false;
                g_meshAlphaScale = 255;
            }
        }
    }
}

/* Every entry's pieces, from the same sites as the old shatter's draw (the opaque,
   cutout and translucent passes and the fading micro-pass) and from the sun and
   reflection casters and the relight bake, so the pieces cast shadows and are relit
   like everything else. */
/* A snag on its own entry, drawn whole through ebld_draw_object as the tree it stands
   for: a stand-in object of the tree's type on its cell, with no strength (so the snag
   model is the one drawn), settled by etree_sink. */
static void etree_draw_orphan(const EbldBld& E, int pass, bool aux)
{
    if (E.model < 0 || E.model >= (int)g_ebld.size() || !g_etreeOn) return;
    if (!cell_shown(E.cx, E.cy) || shroud_cell_hidden(E.cx, E.cy)) return;
    if (!ebld_anchor_shown(E) || ebld_off_screen(E, aux)) return;
    SimObject o = SimObject();
    o.kind = K_TERRAIN;
    snprintf(o.type, sizeof o.type, "%s", E.type);
    o.cx = o.tcx = E.cx; o.cy = o.tcy = E.cy;
    o.wx = o.ex = E.wx; o.wz = o.ez = E.wz;
    o.fw = o.fh = 1;
    o.face = o.tface = -1;
    o.id = -1;
    o.str = 0; o.maxstr = 1;
    o.act = o.pips = o.maxpips = o.doing = o.dostage = o.remap = o.makecnt = -1;
    o.doorstage = o.status = o.flashme = o.group = o.nav = o.tar = -1;
    o.wobmode = -1;
    if (eterr_object_off(o)) return;
    const int mi = mesh_for(o);
    if (mi < 0) return;
    g_etreeSink = etree_sink(E, (float)engine_time());
    if (ebld_draw_object(mi, o, pass) && pass == MODE_OPAQUE && !aux) g_etreeOrphanDrawn++;
    g_etreeSink = 0.0f;
}

static void ebld_draw(int pass, bool fading)
{
    if (g_ebldBld.empty()) return;
    const bool aux = ebld_aux_pass();
    if (aux && fading) return;
    const bool rule = ebld_rule_now();
    bool began = false;
    EbldGL st;
    memset(&st, 0, sizeof st);
    for (std::map<long long, EbldBld>::const_iterator it = g_ebldBld.begin();
         it != g_ebldBld.end(); ++it) {
        const EbldBld& E = it->second;
        /* A SNAG WHOSE TREE THE ENGINE HAS DELETED: drawn here, from its own entry, in every
           pass ebld_draw is asked for, as the tree's whole model was (etree_*) */
        if (E.orphan) {
            if (!fading && rule) etree_draw_orphan(E, pass, aux);
            continue;
        }
        if (E.stage == EB_INTACT) continue;                 /* draw_object_mesh has it */
        if (E.stage != EB_WRECKED && !rule) continue;       /* switched off: the cartridge */
        if (E.model < 0 || E.model >= (int)g_ebld.size()) continue;
        if (!ebld_meshes_ok(g_ebld[E.model]) || !ebld_cut_ok(g_ebld[E.model])
            || !ebld_anchor_shown(E) || ebld_off_screen(E, aux)) continue;
        if (!ebld_resident_stage(g_ebld[E.model], E.stage)) continue;
        const EbldModel& M = g_ebld[E.model];
        if (!began) { st = ebld_gl_begin(aux); began = true; }
        const bool ns = g_meshNoShroud;
        if (ebld_shroud_clear(E)) g_meshNoShroud = true;
        g_ebldNoBroken = g_m3CasterDepth && (M.flags & EBX_REST);
        ebld_draw_entry(E, M, pass, fading, aux, st);
        g_ebldNoBroken = false;
        g_meshNoShroud = ns;
    }
    if (began) ebld_gl_end(st);
    g_meshAlphaScale = 255;
    g_meshFlash = false;
}

/* THE EMBER GLOW, additively over the damaged and wrecked pieces, in the colour picture
   only and inside the scene target so the bloom sees it. Its strength flickers on two
   sines of the engine clock, phased per building, the way the viewer scrolls two
   noise layers over it. It is drawn one-sided and with the broken faces pushed back,
   exactly as the pieces it lies on were, so it meets their depth exactly. */
static void ebld_draw_glow(void)
{
    if (g_ebldBld.empty() || ebld_aux_pass()) return;
    const bool oldTransient = g_meshTransientColor;
    g_meshTransientColor = mesh_perf_switch("OPENCNC3D_MESH_GLOW_DYNAMIC");
    const bool rule = ebld_rule_now();
    GLint depthFunc = GL_LEQUAL;
    GLboolean depthMask = GL_FALSE;
    bool began = false;
    EbldGL st;
    memset(&st, 0, sizeof st);
    const float now = engine_time();
    for (std::map<long long, EbldBld>::const_iterator it = g_ebldBld.begin();
         it != g_ebldBld.end(); ++it) {
        const EbldBld& E = it->second;
        if (E.stage != EB_DAMAGED && E.stage != EB_WRECKED) continue;
        if (E.stage == EB_DAMAGED && !rule) continue;
        if (E.stage == EB_WRECKED && E.dropped) continue;
        if (E.model < 0 || E.model >= (int)g_ebld.size()) continue;
        const EbldModel& M = g_ebld[E.model];
        /* the wreck glows on the shards, or with no DESTROYED set on the CUT pieces */
        const int gx = (M.nset == EBS_N) ? EBM_GLOWX : EBM_GLOWC;
        if (!M.attached || M.mesh[gx] < 0 || M.mesh[gx] >= (int)g_pack.mesh.size()
            || M.mesh[EBM_GLOWC] < 0 || M.mesh[EBM_GLOWC] >= (int)g_pack.mesh.size())
            continue;
        if (!ebld_cut_ok(M) || !ebld_anchor_shown(E) || ebld_off_screen(E, false)
            || !ebld_resident_stage(g_ebld[E.model], E.stage))
            continue;
        if (!began) {
            began = true;
            glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
            st = ebld_gl_begin(false);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glDepthMask(GL_FALSE);
            glDepthFunc(GL_LEQUAL);
        }
        const unsigned ph = efx_hash(0x4542464Cu, (unsigned)it->first, 0u, 0u);
        const float t = now / 15.0f;
        float fl = 0.5f + 0.3f * sinf(t * 2.3f + 6.2831853f * eb_r(ph, 0))
                        + 0.2f * sinf(t * 5.7f + 6.2831853f * eb_r(ph, 1));
        if (fl < 0.0f) fl = 0.0f;
        if (fl > 1.0f) fl = 1.0f;
        float k = 0.45f + 0.55f * fl;
        const bool ns = g_meshNoShroud;
        if (ebld_shroud_clear(E)) g_meshNoShroud = true;
        if (E.stage == EB_DAMAGED) {
            const int mi = M.mesh[EBM_GLOWC];
            const EbldSet& S = M.set[EBS_CUT];
            const std::vector<int>& rg = M.rng[EBM_GLOWC];
            g_meshAlphaScale = (unsigned char)(255.0f * k + 0.5f);
            {
                MeshFragmentScope fragments(&E,2u);
                for (size_t i = 0; i < S.piece.size(); i++)
                    if (E.cut[i].live) {
                        ebld_wave_body(M, EBM_GLOWC, &rg[i * EBR], E.cut[i]);
                        ebld_draw_body(E, M, mi, &rg[i * EBR], MODE_OPAQUE, S.piece[i], E.cut[i],
                                       false, 0.0f, st);
                    }
            }
            ebld_wave_c(M, EBM_GLOWC, now / 15.0f);
            /* standing pieces are the cartridge's while it is being sold */
            if (!E.standDown) {
                ebld_draw_standing(E, S, mi, rg, MODE_OPAQUE, st, [&](size_t i) {
                    return !(S.piece[i].flags & (EBF_WATER | EBF_WATERFALL))
                        && !ebld_stand_moves(E, M, (int)i);
                });
                for (size_t i = 0; i < S.piece.size(); i++) {
                    if (E.cut[i].live || !ebld_stand_moves(E, M, (int)i)) continue;
                    EbXf X;
                    float Mx[12];
                    if (!ebld_stand_xf(E, M, (int)i, now, &X)) continue;
                    ebxf_mat(X, Mx);
                    ebld_draw_moving(E, M, mi, &rg[i * EBR], MODE_OPAQUE, S.piece[i], Mx, st,
                                     false);
                }
            }
        } else {
            float e = 0.0f, sink = 0.0f;
            ebld_sink(E, M, now, &e, &sink);
            k *= (1.0f - e);
            if (k <= 0.0f) { g_meshNoShroud = ns; continue; }
            const int mi = M.mesh[gx];
            const EbldSet& S = M.set[ebld_wreck_set(M)];
            const std::vector<int>& rg = M.rng[gx];
            g_meshAlphaScale = (unsigned char)(255.0f * k + 0.5f);
            {
                MeshFragmentScope fragments(&E,3u);
                for (size_t i = 0; i < S.piece.size() && i < E.wreck.size(); i++)
                    if (E.wreck[i].live)
                        ebld_draw_body(E, M, mi, &rg[i * EBR], MODE_OPAQUE, S.piece[i], E.wreck[i],
                                       E.sinking, sink, st);
            }
        }
        g_meshNoShroud = ns;
    }
    g_meshAlphaScale = 255;
    g_meshTransientColor = oldTransient;
    if (began) {
        ebld_gl_end(st);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(depthMask);
        glDepthFunc((GLenum)depthFunc);
    }
}

/* THE STANDING BUILDING, asked by draw_object_mesh right after the old shatter's gate
   and before the authored and fixed cartridge models. True: this system has answered
   for the object in this pass (drawn it whole, or it is in pieces and ebld_draw draws
   them). False: the cartridge path draws it as before. */
static bool eplc_draw_object(int mi, const SimObject& o, int pass);
static bool ebld_draw_object(int mi, const SimObject& o, int pass)
{
    if (g_raModule && !ebld_ra_ready(o)) return false;
    /* Construction owns the same geometry in colour, normals and lighting in both
       games. TD's normal pass reaches this entry directly too. */
    if (eplc_draw_object(mi, o, pass)) return true;
    /* (a terrain object, version 6: a remaster that stands whole, its idle loop playing) */
    if ((o.kind != K_BUILDING && o.kind != K_TERRAIN) || g_ebld.empty()) return false;
    /* The same tree choice gates lit, normals, shadows, reflection and charred snags. */
    if (o.kind == K_TERRAIN && o.type[0] == 'T'
        && ((o.type[1] >= '0' && o.type[1] <= '9') || ebld_packless_type(o.type))
        && !g_fx.tree3d) return false;
    const EbldBld* E = ebld_find(o);
    int m = -1;
    bool dead = false;
    if (E && E->stage == EB_WRECKED) {
        /* LATCHED AT DEATH. A full model's pieces are ebld_draw's. An intact-only model
           has none, and stands whole until the engine lets the dead object go, whatever
           the switch does in between (the old shatter was already refused for it). */
        if (E->model < 0 || E->model >= (int)g_ebld.size()) return true;
        m = E->model;
        if (g_ebld[m].nset >= 2 || !ebld_meshes_ok(g_ebld[m])) return true;
        dead = true;
    } else {
        if (!ebld_rule_now()) return false;
        m = ebld_model_of(o.type);
        if (m < 0) return false;
        /* THE AUTHORED MODEL WINS where the experimental art set has one and is switched
           on (the Construction Yard's models3d/FACT.c3m): it is the opted-in look, and
           every other pass (its normals, the baked-light removal) draws it too, so the
           colour picture must not show a different model over its normals. */
        if (object_model3d(o) >= 0) return false;
        /* A REMASTERED TREE'S STAGE (etree_*): burning, or the charred snag, which is drawn
           while the engine crumbles the tree with no strength left; -2 hands the crumble of
           a type with no snag to the cartridge */
        const bool snag = o.kind == K_TERRAIN && o.str <= 0;
        if (o.kind == K_TERRAIN) {
            m = etree_draw_model(o, E, m);
            if (m < 0) return false;
        }
        /* going up, being sold, or dead without being claimed: the cartridge's orders */
        if ((o.str <= 0 && !snag) || building_constructing(o) || construction_frac(o) < 1.0f)
            return false;
        /* hurt, and not yet seen by a tick (see ebld_active): the cartridge's damaged
           model for those frames rather than this model whole */
        if (!g_raModule && !E && o.str < o.maxstr && o.kind == K_BUILDING) return false;
        /* a terrain object is drawn whole and nothing else */
        if (o.kind == K_TERRAIN && g_ebld[m].nset != 1) return false;
    }
    const bool broken = E && E->stage != EB_INTACT && !dead;
    /* in pieces, the pieces are ebld_draw's, and only the shadow plate is left here */
    if (broken && pass != MODE_SHADOW) return true;
    const bool cullBeforeResidency = mesh_perf_switch("OPENCNC3D_EBLD_CULL_BEFORE_RESIDENCY");
    const auto ensureResident = [&]() {
        if (broken && ebld_stage_residency_enabled(g_ebld[m])) return g_ebld[m].attached;
        return ebld_resident(g_ebld[m]);
    };
    if (!cullBeforeResidency && !ensureResident()) return dead;
    /* OUTSIDE THIS PASS'S VIEW, NOTHING TO DO. Every pass (the view, the sun's map, the
       sea's reflection, the normal target) walks every building, tree and prop the shroud
       shows, the
       whole map once it is explored, and each draw_mesh below already drops a piece whose
       box is outside the pass's clip (mesh_batch_outside), after building its key and
       finding it. A box round the whole building tested against the same clip first:
       the footprint either way of where it is drawn plus room for anything that
       overhangs it, from under the lowest ground to well above any roof. Outside it,
       every piece would have been dropped, so the picture is the same.
       OPENCNC3D_OBJ_CULL=0 turns it off for an A/B. */
    if ((o.kind == K_BUILDING || o.kind == K_TERRAIN) && mesh_perf_switch("OPENCNC3D_OBJ_CULL")) {
        const float r = (float)(o.fw > o.fh ? o.fw : o.fh) + 4.0f;
        const float lo[3] = { o.wx - r, g_terrainHBot - 2.0f, o.wz - r };
        const float hi[3] = { o.wx + r, g_terrainHTop + 12.0f, o.wz + r };
        if (mesh_box_outside(lo, hi)) return true;
    }
    if (cullBeforeResidency && !ensureResident()) return dead;
    /* AND IN THE SUN'S PASS, OUTSIDE EVERY TEXEL THE LIGHT PASS READS (fx_post.h, the
       receiver window): the same box asked whether any of it can land where a pixel
       looks. Asked after the model is made resident, so what is loaded and when is the
       same as without it. Everything below sets its own state and the brackets around
       each piece put back what they change. The only difference a building left out
       here can make is the current texture coordinate, normal and colour that a culled
       piece would have written on its way past, and every sun caster sets those before
       it reads them. */
    if ((o.kind == K_BUILDING || o.kind == K_TERRAIN) && g_fxRecvArmed) {
        const float r = (float)(o.fw > o.fh ? o.fw : o.fh) + 4.0f;
        const float lo[3] = { o.wx - r, g_terrainHBot - 2.0f, o.wz - r };
        const float hi[3] = { o.wx + r, g_terrainHTop + 12.0f, o.wz + r };
        if (fx_recv_object_outside(lo, hi)) return true;
    }
    /* A PASS THE MODEL HAS NO TRIANGLE OF draws nothing below: every piece reaches
       draw_mesh, which returns at its own pass test before any GL call. So such a pass is
       answered here, after every refusal above and after residency, which happen exactly
       as before. The meshes asked are the intact mesh and every one the service rig can
       draw in its place. Not the opaque pass, whose walk also records what it drew; not
       under the debug pack, which swaps meshes inside draw_mesh; and only while the
       per-draw hooks the walk would hand back are already at rest, so the state left
       behind is the walk's. The flag is still posed, as the walk would have posed it.
       OPENCNC3D_EBLD_PASS_EARLY_OUT=0 walks every piece as before. */
    if (pass != MODE_OPAQUE && pass >= 0 && pass < 32 && !debug_meshes_on()
        && g_meshFillY >= 3.0e38f && g_meshUVScaleU == 1.0f && g_meshUVScaleV == 1.0f
        && g_meshUVOffU == 0.0f && g_meshUVOffV == 0.0f
        && mesh_perf_switch("OPENCNC3D_EBLD_PASS_EARLY_OUT")) {
        const EbldModel& Q = g_ebld[m];
        const int imi = Q.mesh[EBM_INTACT];
        const unsigned bit = 1u << pass;
        const auto has = [&](int k) { return k >= 0 && k < (int)g_pack.mesh.size() && (g_pack.mesh[k].modeMask & bit); };
        bool any = imi < 0 || imi >= (int)g_pack.mesh.size() || has(imi);
        if (!any && Q.service.kind) {
            const std::map<int, BServiceMesh>::const_iterator it = Q.service.meshes.find(imi);
            if (it != Q.service.meshes.end()) {
                any = has(it->second.body);
                for (int k = 0; k < 6 && !any; k++) any = has(it->second.part[k]);
            }
        }
        if (!any) {
            /* (a broken model's walk is its shadow plate alone, and touches neither) */
            if (!broken) {
                if (!Q.wave.empty()) ebld_wave(g_ebld[m], EBM_INTACT, engine_time() / 15.0f);
                g_meshFlash = false;
                g_meshSwayT = -1.0f;
            }
            g_ebldPassEarlyOuts++;
            return true;
        }
    }
    MeshArrayScope arrays;
    const EbldModel& M = g_ebld[m];
    const int imi = M.mesh[EBM_INTACT];
    const EbldSet& S = M.set[EBS_INTACT];
    const std::vector<int>& rg = M.rng[EBM_INTACT];
    /* THE SAME ARGUMENTS THE BASE MODEL WOULD HAVE BEEN GIVEN, so the remastered model
       stands exactly where the cartridge's stood, in every pass. */
    struct Draw {
        int mi, pass, face, house, tur, wob;
        float x, z, frac, lift, at, rot;
        const EbldModel* model; int servicePhase;
        bool tree,stone;float treeMatrix[12];
        void run(int a, int b, const float* mx = NULL) const
        {
            if (b <= a) return;
            float composed[12];const float* transform=mx;
            if(tree && (g_fx.etree_height!=1.0f || g_fx.etree_width!=1.0f || g_fx.etree_vary!=0.0f || g_fx.etree_lean!=0.0f)) {
                if(mx) {
                    for(int r=0;r<3;r++)for(int c=0;c<4;c++) {
                        composed[r*4+c]=c==3?treeMatrix[r*4+3]:0.0f;
                        for(int k=0;k<3;k++)composed[r*4+c]+=treeMatrix[r*4+k]*mx[k*4+c];
                    }
                }else memcpy(composed,treeMatrix,sizeof composed);
                transform=composed;
            }
            const float gain=g_meshTreeGain;const bool stoneWas=g_meshNeutralStone;
            g_meshNeutralStone=stone;
            if(tree)g_meshTreeGain=g_fx.etree_gain;
            bservice_draw(*model,mi,a,b,servicePhase,[&](int mesh,int first,int last){
                draw_mesh(mesh, x, z, face, pass, tur, rot, house, frac, lift, false, wob, at,
                          transform, (size_t)first, (size_t)last);
            });
            g_meshTreeGain=gain;g_meshNeutralStone=stoneWas;
        }
    } D;
    D.stone=o.kind==K_TERRAIN && !strncmp(o.type,"ROCK",4) && fx_unified_lighting()
        && fx_texset_get()==FX_TEX_ENH && ebld_pack_theater()==1;
    D.tree=o.kind==K_TERRAIN&&o.type[0]=='T'&&((o.type[1]>='0'&&o.type[1]<='9')||o.type[1]=='C');
    const float variation=((float)(efx_hash(0x54524545u,(unsigned)o.cx,(unsigned)o.cy,0u)%2001u)/1000.0f-1.0f)*g_fx.etree_vary;
    const float leanAngle=(float)(efx_hash(0x4c45414eu,(unsigned)o.cx,(unsigned)o.cy,0u)%6283u)*0.001f;
    const float width=g_fx.etree_width*(1.0f+variation),height=g_fx.etree_height*(1.0f+variation);
    const float tm[12]={width,sinf(leanAngle)*g_fx.etree_lean,0,0, 0,height,0,0, 0,cosf(leanAngle)*g_fx.etree_lean,width,0};
    memcpy(D.treeMatrix,tm,sizeof tm);
    D.model=&M;D.servicePhase=bservice_phase(M.service.kind,o.doing,o.dostage,
                              o.poweroff!=0,o.str>0 && !dead);
    D.mi = imi; D.pass = pass; D.face = draw_facing(o); D.house = mesh_house(o);
    D.tur = turret_delta(o); D.wob = wobble_of(o);
    D.x = o.wx; D.z = o.wz; D.frac = dead ? 1.0f : construction_frac(o); D.lift = alt_lift(o);
    /* (a snag smoked out settles into the ground, etree_sink) */
    D.lift -= g_etreeSink;
    /* (a tree clump has no cartridge mesh, mi -1, and so no cartridge clip to follow) */
    D.at = g_raModule ? ebld_ra_clip_at(M, o) : (mi >= 0 ? object_anim_t(mi, o) : -1.0f);
    D.rot = rotor_spin(o);
    /* (a standing Weapons Factory's intact clip is its door, on the engine's door stage at
       every strength: THE WEAPONS FACTORY'S DOOR) */
    if (D.at >= 0.0f && ebld_door_at(o) >= 0.0f) D.at = ebld_door_at(o);
    /* THE GROUND-SHADOW PLATE OUTLIVES THE FIRST HIT. It is the one thing the cartridge's
       shadow pass draws, and it exists only in the intact mesh (no cut set carries shadow
       triangles), so a worn or broken building still puts it down from here, while the
       building stands. Without this it vanished on the first chip wherever the sun shadow
       is off, where the cartridge's own plate had stayed through the damage. */
    if (broken) {
        D.run(M.shadow0, M.shadow1);
        return true;
    }
    if (pass == MODE_OPAQUE && !ebld_aux_pass()) g_ebld[m].drawn++;
    /* the flag waves on the engine clock (version 5) */
    if (!M.wave.empty()) ebld_wave(g_ebld[m], EBM_INTACT, engine_time() / 15.0f);
    /* the blush covers the whole object, as the cartridge path brackets it */
    g_meshFlash = g_flashOn && o.blush != 0;
    /* A TREE'S WIND (draw_mesh, g_meshSwayT): its own clock, the engine's in seconds with a
       head start from its cell, so a stand of them does not lean in step */
    if (!M.sway.empty() && g_meshSwayOn && pass == MODE_OPAQUE && !ebld_aux_pass())
        g_ebld[m].windDrawn++;
    if (!M.sway.empty())
        g_meshSwayT = engine_time() / 15.0f
                    + (float)(efx_hash(0x57494E44u, (unsigned)o.cx, (unsigned)o.cy, 0u) % 997u)
                      * 0.0371f;
    int run0 = -1, run1 = -1;
    if (pass != MODE_SHADOW)
        for (size_t k = 0; k < S.piece.size(); k++) {
            const EbldPiece& P = S.piece[k];
            const int* r = &rg[k * EBR];
            const int r0 = r[EBR_A], r1 = r[EBR_B], tx = r[EBR_TEX];
            const int mv = (k < M.moveOf.size()) ? M.moveOf[k] : -1;
            if (P.flags & EBF_WATERFALL) continue;       /* nothing pours from it intact */
            if (!(P.flags & EBF_WATER) && !ebld_flipbook(M, tx) && mv < 0) {
                if (r1 <= r0) continue;
                if (run0 >= 0 && r0 != run1) { D.run(run0, run1); run0 = -1; }
                if (run0 < 0) run0 = r0;
                run1 = r1;
                continue;
            }
            if (run0 >= 0) { D.run(run0, run1); run0 = -1; }
            /* THE ANIMATED PIECES, one run each: a flipbook's frame through the UV hook, a
               turning or aiming part through the extra matrix, a fill through the level
               hook. Each hook is handed back straight after its run. */
            float Mx[12], fillY = 3.0e38f;
            const float* mx = NULL;
            if (mv >= 0) {
                float seen = 0.0f;
                bool show = true;
                if (M.move[mv].kind == EBV_CLIP) {
                    /* A PART OF A CLIP (version 6): the cartridge's own frame for the object,
                       or the idle loop's at the object's idle clock */
                    const EbldClip& C = M.clip[(int)M.move[mv].a[0]];
                    /* (a Hand of Nod's intact clip is its gate, which the engine has no state
                       for: it opens for each soldier trained, hand_door_open) */
                    const float at = (C.set == 0 && !C.idle && !strcmp(o.type, "HAND"))
                                     ? hand_door_open(o) * (float)(C.frames - 1) : D.at;
                    const int f = ebld_clip_frame(C, at, ebld_idle_t(engine_time(),
                                                                     E ? E->idlePhase
                                                                       : ra3d_prop_phase(o, C)));
                    EbXf X;
                    /* (a Red Alert motion played by time: blended between its frames) */
                    if (ebld_ra_motion(M) && C.set == 0 && !C.idle && at >= 0.0f)
                        ebld_clip_xf_at(C, at, (int)M.move[mv].a[1], &X);
                    else
                        ebld_clip_xf(C, f, (int)M.move[mv].a[1], &X);
                    ebxf_mat(X, Mx);
                    seen = (float)f;
                } else {
                    show = ebld_move_pose(M.move[mv], o, Mx, &fillY, &seen);
                }
                if (pass == MODE_OPAQUE && !ebld_aux_pass()) {
                    EbldPoseSeen& ps = g_ebldPoseSeen[std::make_pair(
                        (long long)smooth_key(o), (int)k)];
                    snprintf(ps.type, sizeof ps.type, "%s", o.type);
                    ps.id = o.id;
                    ps.kind = M.move[mv].kind;
                    ps.value = seen;
                }
                if (!show) continue;
                if (M.move[mv].kind != EBV_FILL) mx = Mx;
            }
            if (ebld_flipbook(M, tx)) ebld_uv_set(M, tx, -1.0f);
            g_meshFillY = fillY;
            D.run(r0, r1, mx);
            g_meshFillY = 3.0e38f;
            ebld_uv_clear();
        }
    /* and the borrowed ground-shadow plate, the only thing the shadow pass draws */
    if (run0 >= 0 && run1 == M.shadow0) {
        D.run(run0, M.shadow1);
    } else {
        if (run0 >= 0) D.run(run0, run1);
        D.run(M.shadow0, M.shadow1);
    }
    g_meshFlash = false;
    g_meshSwayT = -1.0f;
    return true;
}

/* ================================================================================
 * THE WALLS. A remastered wall is sixteen pieces, one per connection mask, each authored
 * on its cell with its arms already where that mask puts them, so a cell draws piece
 * `icon` (the engine's own connection nibble, 1 north, 2 east, 4 south, 8 west) at the
 * cell centre, unturned, and neighbouring cells meet as the lab joined them. Asked by
 * draw_walls for the body passes only: the cartridge's wall shadows (the separate
 * shadow sets) still draw where the cartridge's shadows are drawn, and the damaged brick
 * set is not remastered, so a damaged brick wall shows its remaster whole.
 * ============================================================================== */

static bool ebld_wall_draw(int kind, int icon, float cx, float cz, int pass)
{
    if (kind < 0 || kind >= EBLD_NWALL || icon < 0 || icon > 15 || g_ebld.empty()
        || !ebld_rule_now())
        return false;
    if (kind == 5 && !ra3d_props_on()) return false;   /* (Red Alert's fence, for an A/B) */
    if (pass != MODE_OPAQUE && pass != MODE_CUTOUT) return false;
    ebld_wall_models();
    const int m = g_ebldWallModel[kind];
    if (m < 0) return false;
    /* A CELL IN PIECES is drawn by ebld_draw, from its entry: nothing whole here */
    if (!g_ebldBld.empty()) {
        std::map<long long, EbldBld>::const_iterator it =
            g_ebldBld.find(ebld_wall_key((int)floorf(cx), (int)floorf(cz)));
        if (it != g_ebldBld.end() && it->second.stage != EB_INTACT) return true;
    }
    EbldModel& M = g_ebld[m];
    if (!ebld_resident(M)) return false;
    const int* r = &M.rng[EBM_INTACT][(size_t)icon * EBR];
    if (pass == MODE_OPAQUE && !ebld_aux_pass()) M.drawn++;
    if (r[EBR_B] > r[EBR_A]) {
        const EbldWallCull cs = ebld_wall_cull_begin();     /* one-sided, as the viewer */
        draw_mesh(M.mesh[EBM_INTACT], cx, cz, -1, pass, 0, 0.0f, 0, 1.0f, g_wallLiftNow, false,
                  WOBBLE_NONE, -1.0f, NULL, (size_t)r[EBR_A], (size_t)r[EBR_B]);
        ebld_wall_cull_end(cs);
    }
    return true;
}

/* ================================================================================
 * THE NORMAL TARGET. The post chain's light pass (FX_FS_LIGHT_BODY, fx_post.h) reads one
 * normal per pixel and a class in its alpha from a target the terrain, the trees and the
 * authored models draw into; a pixel nothing drew is given the plane of its own facet,
 * rebuilt from the depth buffer. Here the remastered buildings, their pieces and the
 * walls draw their exported smooth normals into it, so a round part's shading and its
 * sun-shadow terminator follow the surface rather than stepping facet by facet, as the
 * lab lights them. Two classes of their own:
 *   0.38  a remastered surface: the light pass uses this normal for the shadow map's
 *         offset and slope bias, the back-face term, the contact march, the relight and
 *         the occlusion's hemisphere, and weights the occlusion as an authored hull's
 *         (inside its object band, above the soldiers' 0.30, below foliage's 0.40);
 *   0.15  SELF-LIT (the export's `emissive`: a molten pool, a lit lens, a status light):
 *         under every band the light pass reads as a normal, and the light pass leaves
 *         such a pixel without sun, shadow, cloud, relight or occlusion, as the lab's own
 *         self-lit mask does. The grade, the rolloff and the bloom still reach it.
 * Drawn against the scene's own depth (the target's setup in fx_world_end), opaque and
 * cut-out only: glass, water and smoke write no normal, as they write no depth. Tier 2
 * only; Enhanced does not exist on the Win98 build.
 * ============================================================================== */
#define EBLD_NORMAL_CLASS 0.38f
#define EBLD_SELFLIT_CLASS 0.15f
/* uPose: the turn of a part a placement is moving on the card (ebld_place.h,
   eplc_draw_run), applied to the normal the batch built for the part at rest; identity
   for everything else this program draws, set after every link (a uniform starts at zero,
   and a zero turn would black every normal). */
static const char* EBLD_NRM_VS =
    "#version 120\n"
    "uniform mat4 uPose;\n"
    "varying vec3 vN,vW;varying vec2 vUV;varying float vEmit;\n"
    "void main(){vN=mat3(uPose)*gl_Normal;vW=gl_Vertex.xyz;vUV=gl_MultiTexCoord0.xy;vEmit=gl_Color.a;\n"
    "gl_ClipVertex=gl_ModelViewMatrix*gl_Vertex;gl_Position=ftransform();}\n";
static const float EBLD_POSE_IDENTITY[16] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                              0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
static void ebld_pose_identity(GLuint p)
{
    if (!p) return;
    GLint was = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &was);
    fx_glUseProgram(p);
    fx_setmat(p, "uPose", EBLD_POSE_IDENTITY);
    fx_glUseProgram((GLuint)was);
}
static std::string ebld_nrm_fs(void)
{
    return std::string("#version 120\n"
        "varying vec3 vN,vW;varying vec2 vUV;varying float vEmit;\n"
        "uniform sampler2D uTex,uStoneMap;uniform int uCut,uStoneMapOn;\n"
        "uniform float uClass,uEmitClass;\n")+STONE_TBN_GLSL+
        "void main(){\n"
        "if(uCut==1&&texture2D(uTex,vUV).a<=0.5)discard;\n"
        "vec3 N=normalize(vN);if(!gl_FrontFacing)N=-N;float cls=uClass;\n"
        "if(uStoneMapOn==1){vec4 m=texture2D(uStoneMap,vUV);N=stoneNormal(N,vW,vUV,normalize(m.rgb*2.0-1.0));\n"
        "if(cls>0.86&&cls<0.89)cls=0.861+0.028*clamp((m.a-0.48)/0.42,0.0,1.0); }\n"
        "gl_FragColor=vec4(N*0.5+0.5,vEmit>0.5?uEmitClass:cls); }\n";
}
static GLuint g_ebldNrmProg = 0;
static bool g_ebldNrmTried = false;
static GLuint ebld_nrm_prog(void)
{
    if (g_ebldNrmProg || g_ebldNrmTried) return g_ebldNrmProg;
    g_ebldNrmTried = true;
    if (!fx_gl_ready) return 0;
    GLuint vs = fx_compile(GL_VERTEX_SHADER, EBLD_NRM_VS, "ebld-normals");
    if (!vs) return 0;
    GLuint fs = fx_compile(GL_FRAGMENT_SHADER, ebld_nrm_fs().c_str(), "ebld-normals");
    if (!fs) { fx_glDeleteShader(vs); return 0; }
    GLuint p = fx_glCreateProgram();
    fx_glAttachShader(p, vs);
    fx_glAttachShader(p, fs);
    fx_glLinkProgram(p);
    GLint ok = 0;
    fx_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        fprintf(stderr, "ebld: the normal target's program FAILED TO LINK\n");
        fx_glDeleteProgram(p);
        p = 0;
    }
    fx_glDeleteShader(vs);
    fx_glDeleteShader(fs);
    g_ebldNrmProg = p;g_stoneMeshNormalProgram=p;
    ebld_pose_identity(p);
    return p;
}
/* Buildings drawn whole into the target in the last frame, for the dump. */
static long g_ebldNrmDraws = 0;
static void ebld_draw_normals(void)
{
    if (g_ebld.empty() || !ebld_rule_now() || !g_ebldNrmOn) return;
    const GLuint prog = ebld_nrm_prog();
    if (!prog) return;
    GLint was = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &was);
    fx_glUseProgram(prog);
    fx_set1i(prog, "uTex", 0);
    fx_set1f(prog, "uClass", EBLD_NORMAL_CLASS);
    fx_set1f(prog, "uEmitClass", EBLD_SELFLIT_CLASS);
    const int was3 = g_m3NormalPass;
    g_m3NormalPass = 1;                       /* an auxiliary pass for every check here */
    g_meshNrmOut = 1;
    g_ebldNrmDraws = 0;
    for (int pi = 0; pi < 2; pi++) {
        const int pass = pi ? MODE_CUTOUT : MODE_OPAQUE;
        fx_set1i(prog, "uCut", pi);
        for (size_t i = 0; i < g_objects.size(); i++) {
            const SimObject& o = g_objects[i];
            if (o.kind != K_BUILDING || !visible(o)) continue;
            if (ebld_draw_object(mesh_for(o), o, pass)) g_ebldNrmDraws++;
        }
        ebld_draw(pass, false);
        draw_walls(pi ? WALLPASS_CUTOUT : WALLPASS_OPAQUE);
    }
    g_meshNrmOut = 0;
    g_m3NormalPass = was3;
    fx_glUseProgram((GLuint)was);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* ebldump -- the models, and every building this system holds. The event lines (hit,
   damaged, wreck, sink, gone) print as they happen, from ebld_step. */
static void ebld_dump(void)
{
    /* (the models the files are: a staged wall's sixteen masks are not counted apart) */
    int attached = 0, loaded = 0;
    for (size_t i = 0; i < g_ebld.size(); i++)
        if (g_ebld[i].wallMask < 0 && g_ebld[i].stageOf < 0) {
            loaded++;
            if (g_ebld[i].attached) attached++;
        }
    if (g_raModule) {
        for (size_t i = 0; i < g_objects.size(); i++) {
            const SimObject& o = g_objects[i];
            if (o.kind != K_BUILDING) continue;
            printf("RASTRUCT|%s|id=%d|cell=%d,%d|str=%d/%d|doing=%d|door=%d|remap=%d|draw=%s\n",
                   o.type, o.id, o.cx, o.cy, o.str, o.maxstr, o.doing, o.doorstage, o.remap,
                   ebld_ra_ready(o) ? "model" : "original");
        }
    }
    /* `on` is whether the system is in force now: its switch AND the Enhanced picture */
    printf("EBLD|loaded=%d|attached=%d|on=%d|enhanced=%d\n", loaded, attached,
           ebld_rule_now() ? 1 : 0, g_fx.enabled ? 1 : 0);
    for (size_t i = 0; i < g_ebld.size(); i++) {
        const EbldModel& M = g_ebld[i];
        if (M.wallMask >= 0 || M.stageOf >= 0) continue;     /* (a tree's stage: ETREE) */
        int n[3] = { -1, -1, -1 };
        const int which[3] = { EBM_INTACT, EBM_CUT, EBM_WRECK };
        for (int k = 0; k < 3; k++)
            if (M.attached && M.mesh[which[k]] >= 0 && M.mesh[which[k]] < (int)g_pack.mesh.size())
                n[k] = (int)g_pack.mesh[M.mesh[which[k]]].tris.size();
        /* the cut's and the wreck's meshes, made at the first hit, hold every triangle of
           their set: said from the set before they are made */
        if (M.attached && !M.cutBuilt && M.nset >= 2) {
            n[1] = (int)M.set[EBS_CUT].tri.size();
            if (M.nset == EBS_N) n[2] = (int)M.set[EBS_DESTROYED].tri.size();
        }
        printf("EBLDMESH|%s%s%s|intact=%d|cut=%d|wreck=%d|pieces=%d,%d|chips=%d|tex=%d\n",
               M.type.c_str(), M.theater >= 0 ? "_" : "",
               M.theater >= 0 ? EBLD_THEATERS[M.theater % 3] : "", n[0], n[1], n[2], (int)M.set[EBS_CUT].piece.size(),
               (int)M.set[EBS_DESTROYED].piece.size(), M.nchip, (int)M.tex.size());
    }
    /* THE ART, one line a model: what kind of file it is, its moving parts and
       flipbooks, how many times the colour picture drew it whole since the last dump
       (the observer), and where its intact model lies against the cartridge model it
       stands in for, both in pack units: x from west to east, z from north to south, and
       the highest y. A wall's own piece is its straight east-west one, which is what the
       cartridge's plain wall mesh is. */
    for (size_t i = 0; i < g_ebld.size(); i++) {
        EbldModel& M = g_ebld[i];
        if (M.wallMask >= 0 || M.stageOf >= 0) continue;     /* (a tree's stage: ETREE) */
        int nk[4] = { 0, 0, 0, 0 }, nflip = 0;
        for (size_t k = 0; k < M.move.size(); k++) nk[M.move[k].kind & 3]++;
        for (size_t t = 0; t < M.tex.size(); t++) if (M.tex[t].grid > 1) nflip++;
        float b[2][5];
        for (int w = 0; w < 2; w++) {
            b[w][0] = b[w][2] = b[w][4] = -1e9f;
            b[w][1] = b[w][3] = 1e9f;
        }
        if (M.attached) {
            const EbldSet& S = M.set[EBS_INTACT];
            const bool wall = (M.flags & EBX_WALL) != 0;
            for (size_t k = 0; k < S.piece.size(); k++) {
                if (wall && k != 10) continue;
                const EbldPiece& P = S.piece[k];
                for (int t = P.tri0; t < P.tri0 + P.ntri; t++)
                    for (int q = 0; q < 3; q++) {
                        const EbldVert& v = S.tri[t].v[q];
                        if (v.x < b[0][1]) b[0][1] = v.x;
                        if (v.x > b[0][0]) b[0][0] = v.x;
                        if (v.z < b[0][3]) b[0][3] = v.z;
                        if (v.z > b[0][2]) b[0][2] = v.z;
                        if (v.y > b[0][4]) b[0][4] = v.y;
                    }
            }
            std::map<std::string, PackType>::const_iterator ct = g_pack.type.find(M.type);
            if (ct != g_pack.type.end() && ct->second.mesh >= 0
                && ct->second.mesh < (int)g_pack.mesh.size()) {
                const PackMesh& cm = g_pack.mesh[ct->second.mesh];
                for (size_t t = 0; t < cm.tris.size(); t++) {
                    if (cm.tris[t].mode == MODE_SHADOW) continue;
                    for (int q = 0; q < 3; q++) {
                        const PackVert& v = cm.tris[t].v[q];
                        if (v.x < b[1][1]) b[1][1] = v.x;
                        if (v.x > b[1][0]) b[1][0] = v.x;
                        if (v.z < b[1][3]) b[1][3] = v.z;
                        if (v.z > b[1][2]) b[1][2] = v.z;
                        if (v.y > b[1][4]) b[1][4] = v.y;
                    }
                }
            }
        }
        /* (version 6 appends: the parts its clips move, idle or not, and whether its cut
           has a loop of its own; the death's centre; the theater whose look it is) */
        printf("EBLDART|%s%s%s|v=%d|sets=%d|wall=%d|spin=%d|turret=%d|fill=%d|flipbooks=%d"
               "|emit=%d|drawn=%ld|x=%.0f,%.0f|z=%.0f,%.0f|top=%.0f|cartx=%.0f,%.0f"
               "|cartz=%.0f,%.0f|carttop=%.0f|clipparts=%d|idle=%d|cutclip=%d|blast=%.0f,%.0f"
               "|wind=%d|winddrawn=%ld\n",
               M.type.c_str(), M.theater >= 0 ? "_" : "",
               M.theater >= 0 ? EBLD_THEATERS[M.theater % 3] : "", M.version, M.nset, (M.flags & EBX_WALL) ? 1 : 0, nk[EBV_SPIN],
               nk[EBV_TURRET], nk[EBV_FILL], nflip, (int)M.emit.size(), M.drawn,
               b[0][1], b[0][0], b[0][3], b[0][2], b[0][4],
               b[1][1], b[1][0], b[1][3], b[1][2], b[1][4], nk[EBV_CLIP],
               (M.clipIntact >= 0 && M.clip[M.clipIntact].idle) ? 1 : 0,
               M.clipCut >= 0 ? (int)M.clip[M.clipCut].nparts : 0,
               M.param[EBP_BLAST_X], M.param[EBP_BLAST_Z], (int)(M.sway.size() / 6),
               M.windDrawn);
        M.drawn = 0;
        M.windDrawn = 0;
    }
    static const char* const MOVEK[4] = { "spin", "turret", "fill", "clip" };
    for (std::map<std::pair<long long, int>, EbldPoseSeen>::const_iterator it =
             g_ebldPoseSeen.begin(); it != g_ebldPoseSeen.end(); ++it)
        printf("EBLDPOSE|%s|key=%lld|id=%d|piece=%d|kind=%s|%s=%.3f\n", it->second.type,
               it->first.first, it->second.id, it->first.second, MOVEK[it->second.kind & 3],
               it->second.kind == EBV_FILL ? "level" : it->second.kind == EBV_CLIP ? "frame"
                                                                                : "deg",
               it->second.value);
    g_ebldPoseSeen.clear();
    /* THE DEATHS' EXPLOSIONS fired since the mission began and what they emitted, and the
       engine's own anims that stood down for this system's buildings, by name: those of a
       death and those of the damage */
    {
        std::string nm[2];
        for (int w = 0; w < 2; w++) {
            for (std::map<std::string, long>::const_iterator it = g_ebldAnimOffN[w].begin();
                 it != g_ebldAnimOffN[w].end(); ++it) {
                char one[48];
                snprintf(one, sizeof one, "%s%s:%ld", nm[w].empty() ? "" : ",",
                         it->first.c_str(), it->second);
                nm[w] += one;
            }
            if (nm[w].empty()) nm[w] = "none";
        }
        printf("EBLDOFF|booms=%ld|parts=%ld|chunks=%ld|lab=%d|death=%s|damage=%s|smudges=%ld\n",
               g_ebldBoomFired, g_ebldBoomParts, g_ebldBoomChunks, g_ebldBoomLab ? 1 : 0,
               nm[0].c_str(), nm[1].c_str(), g_ebldSmudgeOffN);
    }
    static const char* const STAGE[4] = { "INTACT", "HELD", "DAMAGED", "WRECKED" };
    for (std::map<long long, EbldBld>::const_iterator it = g_ebldBld.begin();
         it != g_ebldBld.end(); ++it) {
        const EbldBld& E = it->second;
        int bodies = 0, resting = 0;
        for (size_t k = 0; k < E.cut.size(); k++)
            if (E.cut[k].live) { bodies++; if (E.cut[k].asleep) resting++; }
        for (size_t k = 0; k < E.wreck.size(); k++)
            if (E.wreck[k].live) {
                bodies++;
                if (E.wreck[k].asleep || E.wreck[k].pinned || E.sinking) resting++;
            }
        printf("EBLDOBJ|%s|key=%lld|id=%d|stage=%s|str=%d|max=%d|steps=%d|chips=%d"
               "|zones=%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f|bodies=%d|resting=%d\n",
               E.type, it->first, E.id, STAGE[E.stage & 3], E.str, E.maxstr, E.applied,
               E.chips, E.want[0], E.want[1], E.want[2], E.want[3], E.want[4], E.want[5],
               E.want[6], E.want[7], bodies, resting);
        /* a Hand of Nod's gate: how far it is open now and where its opening is (world
           cells), which every stage's leaves follow and a trained soldier comes out of */
        if (E.model >= 0 && E.model < (int)g_ebld.size() && ebld_hand_door(E, g_ebld[E.model])) {
            float gx = 0.0f, gz = 0.0f;
            const bool g = ebld_gate_of(g_ebld[E.model], E.wx, E.wz, &gx, &gz);
            printf("EBLDDOOR|%s|key=%lld|id=%d|stage=%s|open=%.3f|gate=%d|x=%.3f|z=%.3f\n",
                   E.type, it->first, E.id, STAGE[E.stage & 3], hand_door_at(E.id, engine_time()),
                   g ? 1 : 0, gx, gz);
        }
        if (E.boomBorn != -1)
            printf("EBLDBOOM|%s|key=%lld|born=%d|stage=%d|parts=%ld|chunks=%ld|x=%.3f|z=%.3f\n",
                   E.type, it->first, E.boomBorn, E.boomLast, E.boomParts, E.boomChunks,
                   E.boomX, E.boomZ);
        if (E.model < 0 || E.model >= (int)g_ebld.size()) continue;
        const EbldModel& M = g_ebld[E.model];
        const float now = engine_time();
        /* VERSION 6, the clips: the intact frame the cartridge shows it at (or the idle
           loop's), and once damaged the tick of the cut's own loop and where it started */
        if (M.clipIntact >= 0) {
            const EbldClip& C = M.clip[M.clipIntact];
            const int fi = ebld_clip_frame(C, E.animF, ebld_idle_t(now, E.idlePhase));
            const int tick = (E.stage == EB_DAMAGED && M.clipCut >= 0)
                           ? ebld_clip_tick(M.clip[M.clipCut], E.clipI0, E.damagedTick, now) : -1;
            printf("EBLDCLIP|%s|key=%lld|stage=%s|intact=%d|idle=%d|phase=%.3f|cut=%d|start=%d\n",
                   E.type, it->first, STAGE[E.stage & 3], fi, C.idle ? 1 : 0, E.idlePhase, tick,
                   E.stage == EB_DAMAGED ? E.clipI0 : -1);
        }
        /* A RED ALERT STRUCTURE'S DRIVEN CLIP (RA_DRIVE): the engine's state and door stage
           this tick, how far out the motion is and whether it is on its way back (or a
           loop's frame), the intact clip's frame shown, and where the motion lies in it */
        if (g_raModule && M.raDrive >= 0 && M.clipIntact >= 0) {
            static const char* const KIND[8] = { "none", "door", "ramp", "stage", "cycle", "anim", "charge", "fired" };
            /* how far the clip's parts stand moved at the frame drawn (the largest translation
               of a part, pack units: a roller door's lift) and at the motion's full extent */
            const EbldClip& C = M.clip[M.clipIntact];
            float lift = 0.0f, full = 0.0f;
            for (int pt = 0; pt < C.nparts; pt++) {
                EbXf X;
                ebld_clip_xf(C, ebld_clip_frame(C, E.animF, ebld_idle_t(now, E.idlePhase)), pt, &X);
                lift = fmaxf(lift, sqrtf(X.t[0] * X.t[0] + X.t[1] * X.t[1] + X.t[2] * X.t[2]));
                ebld_clip_xf(C, M.drvSeg[1] >= 0 ? M.drvSeg[1] : C.frames - 1, pt, &X);
                full = fmaxf(full, sqrtf(X.t[0] * X.t[0] + X.t[1] * X.t[1] + X.t[2] * X.t[2]));
            }
            printf("EBLDDRIVE|%s|key=%lld|kind=%s|sprite=%d|stage=%s|doing=%d|door=%d|out=%.3f|closing=%d"
                   "|cycle=%.2f|frame=%.2f|seg=%d,%d,%d,%d|frames=%d|lift=%.4f|full=%.4f\n", E.type, it->first,
                   KIND[RA_DRIVE[M.raDrive].kind >= 1 && RA_DRIVE[M.raDrive].kind <= 7 ? RA_DRIVE[M.raDrive].kind : 0],
                   M.drvSprite ? 1 : 0,
                   STAGE[E.stage & 3], E.serviceDoing, E.doorStage, E.drvCur,
                   E.drvClosing ? 1 : 0, E.cycCur, ebld_ra_drive_frame(E, M, 1.0f),
                   M.drvSeg[0], M.drvSeg[1], M.drvSeg[2], M.drvSeg[3],
                   M.clip[M.clipIntact].frames, lift, full);
        }
        /* THE WEAPONS FACTORY'S DOOR: the engine's door stage, the frame the door clips show
           for it, and the frames the intact and damaged clips are at (-1 none) */
        if (E.doorF >= 0.0f) {
            const int fi = M.clipIntact >= 0 ? ebld_clip_frame(M.clip[M.clipIntact], E.animF, 0.0) : -1;
            const int fc = (E.stage == EB_DAMAGED && M.clipCut >= 0)
                         ? ebld_clip_frame(M.clip[M.clipCut], E.doorF, 0.0) : -1;
            printf("EBLDWEAPDOOR|%s|key=%lld|stage=%s|door=%d|at=%.3f|intact=%d|cut=%d\n", E.type,
                   it->first, STAGE[E.stage & 3], E.doorStage, E.doorF, fi, fc);
        }
        /* a wall cell's entry: its cell, and the connection mask whose stages play it */
        if (M.wallMask >= 0)
            printf("EBLDWALL|%s|key=%lld|cell=%d,%d|mask=%d|level=%d/%d|cutchips=%d\n", E.type,
                   it->first, E.cx, E.cy, M.wallMask, E.maxstr - E.str, E.maxstr, M.nchip);
        /* THE STANDING PIECES THAT MOVE, where they are this frame: the angle their pose
           is turned by (degrees), how far their centre has moved (pack units), and for a
           looping one its try and shudder weight. */
        if ((E.stage == EB_HELD || E.stage == EB_DAMAGED) && M.nset >= 2)
            for (size_t k = 0; k < M.set[EBS_CUT].piece.size(); k++) {
                if (E.cut[k].live || !ebld_stand_moves(E, M, (int)k)) continue;
                EbXf X;
                if (!ebld_stand_xf(E, M, (int)k, now, &X)) continue;
                const EbldPiece& P = M.set[EBS_CUT].piece[k];
                float tr = (X.R[0] + X.R[4] + X.R[8] - 1.0f) * 0.5f;
                if (tr > 1.0f) tr = 1.0f;
                if (tr < -1.0f) tr = -1.0f;
                float d = 0.0f;
                for (int i = 0; i < 3; i++) {
                    const float c = X.R[i * 3] * P.c[0] + X.R[i * 3 + 1] * P.c[1]
                                  + X.R[i * 3 + 2] * P.c[2] + X.t[i] - P.c[i];
                    d += c * c;
                }
                const EbldPX& PX = M.px[k];
                const char* kind = (k < M.clipPart.size() && M.clipPart[k] >= 0
                                    && !ebld_door_loops(E, M, (int)k)) ? "clip"
                                 : ebld_loops_now(E, M, (int)k) ? "loop"
                                 : ((PX.flags & EBQ_BEND) && E.stage == EB_DAMAGED) ? "bend"
                                 : "turn";
                int tk = -1;
                float w = 0.0f;
                if (!strcmp(kind, "loop") && !PX.mo.empty()) {
                    float f;
                    EbldLoopCache c = E.lc[(size_t)M.motion0[k]];
                    for (size_t i = 0; i < PX.mo.size(); i++) {
                        float wi;
                        int ti;
                        c = E.lc[(size_t)M.motion0[k] + i];
                        ebld_loop_value(PX.mo[i], ebld_loop_T(E, M, (int)k, now),
                                        (double)PX.seed + (double)i * 17.0, &c, &f, &wi, &ti);
                        if (ti > tk) tk = ti;
                        if (wi > w) w = wi;
                    }
                }
                printf("EBLDMOVE|%s|key=%lld|piece=%s|kind=%s|deg=%.3f|moved=%.2f|try=%d"
                       "|weight=%.3f\n", E.type, it->first, P.name, kind,
                       acosf(tr) * (180.0f / EBLD_PI), sqrtf(d), tk, w);
            }
        /* THE EMITTERS, what each has given off on this building so far */
        for (size_t e = 0; e < E.es.size() && e < M.emit.size(); e++) {
            const EbldEmit& em = M.emit[e];
            static const char* const SRC[7] = { "steam", "embers", "smoke", "fire", "sparks",
                                                "glow", "ion" };
            printf("EBLDEMIT|%s|key=%lld|e=%d|src=%s|state=%d|piece=%s|flags=%d|spawned=%ld"
                   "|bursts=%d\n", E.type, it->first, (int)e, SRC[em.src % 7], em.state,
                   em.piece >= 0 ? M.set[EBS_CUT].piece[em.piece].name : "-", em.eflags,
                   E.es[e].spawned, E.es[e].nburst);
        }
        /* THE LIQUIDS: how far a drain has gone (0..1 of its time), and the fill's level */
        if (M.nset >= 2)
            for (size_t i = 0; i < M.liquid.size(); i++) {
                const EbldLiquid& L = M.liquid[i];
                static const char* const LK[3] = { "drain", "fall", "fill" };
                float k = 0.0f, fr = 0.0f, y = 0.0f;
                if (E.stage == EB_DAMAGED) {
                    k = ((now - (float)E.drainTick) / 15.0f) / (L.seconds > 0.01f ? L.seconds : 0.01f);
                    if (k > 1.0f) k = 1.0f;
                }
                if (L.kind == EBL_FILL) y = ebld_fill_y(E, M, now, &fr);
                /* how opaque it is drawn now: a drain's surface, a pour (with its gate) */
                float shown = (L.kind == EBL_FALL) ? 0.0f : 1.0f;
                if (E.stage == EB_DAMAGED && L.kind != EBL_FILL) {
                    float sc = 1.0f, al = 1.0f, fa = 0.0f;
                    ebld_drain(E, L, &sc, &al, &fa);
                    shown = (L.kind == EBL_FALL) ? fa : al;
                    if (L.kind == EBL_FALL && (L.flags & EBL_GATE)) {
                        float f2 = 0.0f;
                        (void)ebld_fill_y(E, M, now, &f2);
                        if (!(f2 > 0.0f) || !(E.fillH0 > L.gate)) shown = 0.0f;
                    }
                }
                printf("EBLDLIQ|%s|key=%lld|piece=%s|kind=%s|drained=%.3f|fill=%.3f|level=%.1f"
                       "|shown=%.3f\n", E.type, it->first, M.set[EBS_CUT].piece[L.piece].name,
                       LK[L.kind % 3], k, fr, y, shown);
            }
    }
    printf("EBLDLIGHT|lights=%d\n", (int)g_ebldLights.size());
    /* VERSION 5: the normal target's draw (whole buildings it drew last frame) and the
       waves (how many triangles move, and the time each mesh was last posed at) */
    printf("EBLDNRM|on=%d|objects=%ld\n", (ebld_rule_now() && g_ebldNrmOn) ? 1 : 0,
           g_ebldNrmDraws);
    for (size_t i = 0; i < g_ebld.size(); i++) {
        const EbldModel& M = g_ebld[i];
        if (M.wave.empty() || M.wallMask >= 0) continue;
        printf("EBLDWAVE|%s|waves=%d|tris=%d,%d|posed=%.3f,%.3f\n", M.type.c_str(),
               (int)M.wave.size(), (int)M.waveT[0].size(), (int)M.waveT[1].size(),
               M.wavePosed[EBM_INTACT] > -1.0e29f ? M.wavePosed[EBM_INTACT] : -1.0f,
               M.wavePosed[EBM_CUT] > -1.0e29f ? M.wavePosed[EBM_CUT] : -1.0f);
    }
    fflush(stdout);
}

/* ebldbodies -- every moving piece, one line each, for judging the physics from numbers:
   where it is (world cells), how far it lies from the building's anchor, how high its
   lowest contact point sits above the ground under that point (negative is into the
   ground), which way its outer surface faces (the world y of its first surface
   triangle's normal: +1 face up, -1 face down), and whether it is asleep. */
static void ebld_dump_bodies(void)
{
    for (std::map<long long, EbldBld>::const_iterator it = g_ebldBld.begin();
         it != g_ebldBld.end(); ++it) {
        const EbldBld& E = it->second;
        if (E.model < 0 || E.model >= (int)g_ebld.size()) continue;
        const EbldModel& M = g_ebld[E.model];
        for (int s = 0; s < 2; s++) {
            const std::vector<EbldBody>& V = s ? E.wreck : E.cut;
            const EbldSet& S = M.set[s ? ebld_wreck_set(M) : EBS_CUT];
            for (size_t k = 0; k < V.size() && k < S.piece.size(); k++) {
                const EbldBody& B = V[k];
                if (!B.live) continue;
                const EbldPiece& P = S.piece[k];
                float R[9];
                eb_qmat(B.q, R);
                float low = 1e9f, bottom = 1e9f;
                const int nh = P.nhull > 0 ? P.nhull : 1;
                for (int h = 0; h < nh; h++) {
                    float hp[3] = { 0.0f, 0.0f, 0.0f }, r[3];
                    if (P.nhull > 0)
                        for (int c = 0; c < 3; c++) hp[c] = P.hull[h][c] * MODEL_SCALE;
                    eb_mulv(R, hp, r);
                    const float gap = B.p[1] + r[1] - efx_ground(B.p[0] + r[0], B.p[2] + r[2]);
                    if (gap < low) low = gap;
                    if (B.p[1] + r[1] < bottom) bottom = B.p[1] + r[1];
                }
                float up = 0.0f;
                for (int t = P.tri0; t < P.tri0 + P.ntri; t++) {
                    const EbldTri& T = S.tri[t];
                    if (T.kind != 0) continue;
                    const EbldVert *v0 = &T.v[0], *v1 = &T.v[1], *v2 = &T.v[2];
                    const float a[3] = { v1->x - v0->x, v1->y - v0->y, v1->z - v0->z };
                    const float b[3] = { v2->x - v0->x, v2->y - v0->y, v2->z - v0->z };
                    float n[3], wn[3];
                    eb_cross(a, b, n);
                    const float L = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                    if (L < 1e-6f) continue;
                    for (int c = 0; c < 3; c++) n[c] /= L;
                    eb_mulv(R, n, wn);
                    up = wn[1];
                    break;
                }
                const float dx = B.p[0] - E.wx, dz = B.p[2] - E.wz;
                printf("EBLDBODY|key=%lld|set=%s|piece=%s|at=%.3f,%.3f,%.3f|dist=%.3f|low=%.4f"
                       "|bottom=%.4f|ground=%.4f|up=%.2f|v=%.3f|asleep=%d|pinned=%d\n",
                       it->first, s ? "wreck" : "cut", P.name, B.p[0], B.p[1], B.p[2],
                       sqrtf(dx * dx + dz * dz), low, bottom, efx_ground(B.p[0], B.p[2]), up,
                       sqrtf(B.v[0] * B.v[0] + B.v[1] * B.v[1] + B.v[2] * B.v[2]),
                       B.asleep ? 1 : 0, B.pinned ? 1 : 0);
            }
        }
    }
    fflush(stdout);
}
#include "ebld_prepare.h"

#endif

static void bservice_dump(void)
{
    for(size_t i=0;i<g_objects.size();i++){
        const SimObject& o=g_objects[i];const int m=ebld_model_of(o.type);
        if(m<0 || !g_ebld[m].service.kind)continue;
        const EbldModel& M=g_ebld[m];const int phase=bservice_phase(M.service.kind,o.doing,
            o.dostage,o.poweroff!=0,o.str>0);
        printf("BSERVICE|%s|id=%d|house=%s|doing=%d|stage=%d|poweroff=%d|phase=%d|str=%d/%d\n",
               o.type,o.id,o.house,o.doing,o.dostage,o.poweroff,phase,o.str,o.maxstr);
    }
}
