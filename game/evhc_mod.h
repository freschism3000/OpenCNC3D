/* ================================================================================
 * ENHANCED VEHICLES (evhc) -- the remastered vehicles and aircraft, drawn, animated and
 * broken apart by the renderer alone, under Enhanced.
 *
 * WHAT IT IS. A vehicle or aircraft type with a baked evhc/<TYPE>.evhc file is not drawn
 * from the cartridge's model under Enhanced. The file (tools/art/remaster/bake_evhc.py)
 * carries the remastered model as one piece per motion and a record per moving piece:
 *
 *   hull        everything fixed to the body
 *   turret      turns with the engine's turret facing, exactly as the cartridge's turret
 *               part does (the tanks, the Hum-vee, the buggy, the gun boat, the rocket
 *               launcher's launcher)
 *   belt, wflip a track's links scroll and its wheels step through their flipbook by how
 *               far that side of the vehicle has travelled
 *   wheel       rolls by its side's travel over its radius, and steers into a turn
 *   rotor       an aircraft's blades and, separately, their blur disc: blades on the
 *               ground, the disc in the air, spinning up on take-off and down on landing
 *   spin        a part turning at its own rate (the hovercraft's propellers, the Mobile
 *               HQ's radar)
 *   hinge       a door, a ramp, a hopper, a launcher's elevation, a duct's tilt, driven by
 *               the unit's own state
 *   clip        follows the cartridge's own animation of one of its parts (the
 *               harvester's collector arms, which work only while it lifts Tiberium:
 *               object_anim_t's harvester gate)
 *   deploy      one of the MCV's five groups, moved by the cartridge's deploy rig
 *   recoil      a barrel that kicks back along its axis when the unit fires
 *
 * and a DESTROYED set: the pieces the vehicle breaks into, each knowing the intact piece it
 * came from, so it starts exactly where that piece stood and turned.
 *
 * THE SIBLING OF ebld_mod.h. The file shares the remastered buildings' texture, piece and
 * triangle records, and this module keeps each model in the buildings' own EbldModel, so
 * the residency (textures read from the file the first time the model is drawn), the
 * skirmish liveries through the house mask, the pack meshes and the rigid-body solver are
 * the buildings' and are not copied. What is a vehicle's own is here: the part records, the
 * per-unit motion state, the draw with a matrix per piece, the dock and deploy rigs, and the
 * death.
 *
 * WHO OWNS WHAT. Everything is gated on one rule: the Enhanced picture is on, this switch
 * is on (--noevhc is the A/B), the object is a unit or an aircraft and a model is attached
 * for its type. Classic never reaches any of it. Under the rule, for such a unit: its
 * cartridge model and transport door stand down, the refinery's dock rig and the MCV's
 * deploy rig draw the remastered vehicle in the cartridge vehicle's place, the old unit
 * shatter does not spawn when it dies (this system's wreck does), and the deck riders on a
 * hovercraft stand on the remastered deck. The authored art set (experimental art) still
 * wins where it has a model, as it does over the remastered buildings.
 *
 * THE CLOCK. Every state change (travel, rotor spin, door, tilt, elevation, a shot, the
 * wreck's physics) advances on the engine tick edge (evhc_step), never per drawn frame;
 * draws lerp between the last two ticks with g_tickAlpha, which is 0 under --shot and
 * --script, so two runs of one script draw the same frames. Randomness is efx_hash keyed
 * on the unit's kind-qualified key, the tick and the piece.
 *
 * THE DRAW is fixed function throughout: every piece is a draw_mesh call with a 3x4 extra
 * matrix and a triangle range; a track's scroll and a wheel's flipbook frame are the
 * texture-coordinate hook the remastered buildings' water uses. Nothing in the colour
 * picture needs more than GL 1.1.
 *
 * THE SMOOTH NORMALS (file version 2). Every corner carries the export's smoothed normal,
 * and evhc_draw_normals draws every piece, posed exactly as the colour picture posed it,
 * into the light pass's normal target with the remastered buildings' small program, so a
 * round hull, a turret or a nose is lit by its surface and not facet by facet. A version 1
 * file still reads; its corners take their faces' normals. --noevhcnormals is the A/B.
 *
 * TIER 1. Enhanced does not exist on the Win98 build, so neither does this; see
 * docs/tier1-gap.md. The Classic path, the cartridge models and the old unit shatter, is
 * what that build keeps.
 * ================================================================================ */
#ifndef EVHC_MOD_H
#define EVHC_MOD_H
#include "vehicle_suspension.h"

/* ---- the file's vocabulary (tools/art/remaster/bake_evhc.py) ---------------------- */
enum { EVP_SPIN = 0, EVP_TURRET, EVP_ROTOR, EVP_WHEEL, EVP_BELT, EVP_WFLIP, EVP_HINGE,
       EVP_CLIP, EVP_DEPLOY, EVP_RECOIL, EVP_N };
/* (after the hovercraft's stern ramp, Red Alert's, driven only under Red Alert: the Ore
   Truck's scoop, its dump body rising and tipping, and the ore load it carries; the V2
   Rocket's erector, the rocket on it, the erector's two rams and their rods; the
   Transport's four ramps; evhc_step's RED ALERT blocks). Then the Harvester's, driven in
   either game by whether it is harvesting: its claws' lift, the lift rams' barrels and
   rods, and its mouth's lower jaw (evhc_step's THE HARVESTER'S CLAWS). Last, Red Alert's
   landing gear: each gear leg and gear door of an aircraft that lands on an Airfield (the
   MiG first), down on the ground and stowed in flight (evhc_step's THE LANDING GEAR). And
   the Harvester's hoop, swung forward over its cab in the Refinery's dock before the tank
   tips (evhc_dock_draw, THE HOOP SWINGS BEFORE THE TANK TIPS). */
enum { EVD_NONE = 0, EVD_APC_DOOR, EVD_TRAN_RAMP, EVD_LST_BOW, EVD_C17_RAMP,
       EVD_HARV_HOPPER, EVD_ELEVATION, EVD_DUCT, EVD_LST_STERN,
       EVD_ORE_SCOOP, EVD_ORE_DUMP, EVD_ORE_LOAD,
       EVD_V2_ERECTOR, EVD_V2_ROCKET, EVD_V2_RAM, EVD_V2_ROD, EVD_RA_RAMP,
       EVD_HARV_LIFT, EVD_HARV_RAM, EVD_HARV_ROD, EVD_HARV_JAW, EVD_GEAR, EVD_HARV_HOOP, EVD_N };
enum { EVC_TRACKED = 0, EVC_WHEELED, EVC_HALF, EVC_HELI, EVC_PLANE, EVC_HOVER, EVC_BOAT };
enum { EVQ_CLASS = 0, EVQ_GAUGE, EVQ_SINK_AFTER, EVQ_SINK_TIME, EVQ_SINK_DEPTH, EVQ_SHADE,
       EVQ_DOCK_X, EVQ_DOCK_Z, EVQ_DOCK_YAW, EVQ_DECK,
       /* A SUBMARINE'S DIVE, an optional tail (bake_evhc.py: painter.submerge): how far under
          the sea (cells) it is drawn awash, at periscope depth and fully under */
       EVQ_DIVE_AWASH, EVQ_DIVE_PERISCOPE, EVQ_DIVE_UNDER, EVQ_N_DIVE, EVQ_N_MIN = 6 };
enum { EVF_DISC = 1 };
static const char* const EVP_NAME[EVP_N] = { "spin", "turret", "rotor", "wheel", "belt",
                                             "wflip", "hinge", "clip", "deploy", "recoil" };

struct EvhcPart { int piece, kind, parent, driver, side, flags; float a[12]; };
/* A LIGHT the export gives the vehicle (version 2's light table, lights.json): where it
   sits on its piece at rest (pack units), the way it points (pack frame), its colour,
   cone (half angles, degrees), range (cells) and strength, and its lens: the way the lens
   faces (pack frame) and its radius (cells). */
enum { EVL_HEAD = 0, EVL_TAIL, EVL_NAV, EVL_BEACON, EVL_OTHER, EVL_N };
enum { EVLF_SHADOWS = 1, EVLF_LENS = 2 };
struct EvhcLight {
    char  name[17];
    int   piece, kind, flags;
    float pos[3], dir[3], col[3], cone[2], range, intensity, lensN[3], lensR, lensUV[2];
};
struct EvhcModel {
    /* THE BUILDINGS' STRUCT, so their residency, liveries, mesh filling and piece drawing
       serve these too: tex, set[EBS_INTACT] (the model) and set[EBS_DESTROYED] (the wreck,
       its parents INTACT pieces), path and size, the attachment (mesh[EBM_INTACT] and
       mesh[EBM_WRECK] with their piece runs in rng[]) and residency. */
    EbldModel m;
    std::vector<EvhcPart> part;
    std::vector<int> partOf;                  /* per INTACT piece, its part or -1 */
    int  fileVer;                             /* the file's format version, 1 or 2 */
    std::vector<EvhcLight> light;             /* version 2: the vehicle's lights */
    /* per wreck piece: its launch up (a range), outward (a range), cells a second, and its
       spin, radians a second (the lab's blast by role) */
    std::vector<float> blast;
    int cls;
    float gauge;                              /* cells between the two sides' running gear */
    bool turret, rotors, discs;
    int  nbarrel;                             /* recoil parts */
    /* PER PACK: the rig parts the dock (PROCANIM) and the deploy (MCVANIM) move each
       piece with, and the deploy rig's frame fitted onto the remaster (evhc_attach). */
    std::vector<int> rigPart;
    float rigYaw, rigX, rigZ, rigErr;
    bool  rigOk;
    /* the Harvester's claws' lift part (EVD_HARV_LIFT), -1 for a vehicle without one; the
       lift the dock rig last drew it at, degrees (the dump) */
    int   liftPart;
    float rigLift;
    /* the Harvester's hoop (EVD_HARV_HOOP), -1 for a vehicle without one; in the dock rig
       since the last dump: the hoop's share and the tank's rig frame last drawn, and the
       draws with the hoop part way (mid), with the tank off its rest and the hoop fully
       swung (full), and with the tank off its rest while the hoop was not (bad: never) */
    int   hoopPart;
    float rigHoop, rigTank;
    /* Red Alert's Ore Truck's load (EVD_ORE_LOAD), -1 for a vehicle without one, and the
       lowest point of its piece at rest, pack units up: the bed's floor it fills from */
    int   loadPart;
    float loadFloor;
    long  rigHoopMid, rigHoopFull, rigHoopBad;
    /* it carries landing gear (EVD_GEAR hinges): Red Alert's aircraft that land */
    bool  gear;
    long  drawn;                              /* whole-model draws since the last dump */
    long  rigDrawn;                           /* drawn in the dock or deploy rig, likewise */
    /* RESIDENCY IN TWO HALVES: the textures the standing model draws with go to the card the
       first time it is drawn, the wreck's (its charred atlases, broken material and glow,
       more than half the bytes) only the first time one of its wrecks is. */
    std::vector<unsigned char> texUp;         /* per texture: on the card this pack */
    std::vector<unsigned char> texIntact;     /* per texture: the standing model uses it */
    bool intactUp, allUp;
};
static std::vector<EvhcModel> g_evhc;
static bool g_evhcLoaded = false;
/* --noevhc clears it: the A/B for every judgement about this system. */
static bool g_evhcOn = true;
/* --noharvlift clears it (and `harvlift 0`): the Harvester's claws, their rams and its
   mouth's lower jaw stand at the model's rest (claws lowered, jaw shut) instead of
   following the harvest. */
static bool g_evhcHarvLiftOn = true;
static bool g_evhcHarvSuspOn = true; /* script A/B; normal Enhanced launches use suspension */
/* Inside evhc_draw_normals: the pieces go into the light pass's normal target; a wreck
   the colour picture draws fading (blended, no depth) does not. The counts are what the
   last frame drew there, for the dump. */
static bool g_evhcNrmPass = false;
static long g_evhcNrmObjs = 0, g_evhcNrmRigs = 0, g_evhcNrmWrecks = 0;
/* --noevhcspec clears it (and `evhcspec 0|1`): no vehicle's specular map is drawn, and the
   light pass adds no highlight from one. The F5 panel's vspec_on is the player's switch. */
static bool g_evhcSpecOn = true;
static long g_evhcSpecObjs = 0, g_evhcSpecRigs = 0, g_ebldSpecObjs = 0;
static int  g_evhcSpecFrames = 0;
static int  g_evhcPackSerial = 0;

/* ---- one unit's motion state, advanced on the tick edge ----------------------------- */
struct EvhcUnit {
    int   model, id, kind;
    char  type[16];
    int   tick;                               /* last tick advanced */
    bool  have;
    float ex, ez;                             /* last tick's engine position */
    int   face;                               /* last tick's engine facing (DirType) */
    float trav[2], travP[2];                  /* travel per side, cells: 0 right, 1 left */
    VehicleSuspension suspension;
    float dSuspension[3];
    float v, w;                               /* this tick: cells forward, radians left */
    int   td;                                 /* this tick's turret delta (the death's aim) */
    float turn, turnP;                        /* steering, -1 (right) .. 1 (left) */
    float spin, spinP;                        /* rotor clock, seconds at full rate */
    float rate;                               /* rotor rate, 0..1 */
    float disc;                               /* the blur disc's opacity, 0..1 */
    float duct, ductP;                        /* the Orca's duct tilt, degrees */
    float elev, elevP;                        /* the launcher's elevation, degrees */
    float door, doorP;                        /* a door or ramp, 0 shut .. 1 open */
    /* Red Alert's Ore Truck: its scoop up (0 down .. 1 at the top of its swing), its dump
       body risen on its rams and tipped (each 0 .. 1), from the engine's harvest and dump
       frames; and the frame it was last told (doing 1 lifting, 2 dumping) */
    float scoop, scoopP, lift, liftP, tip, tipP;
    int   oreDoing, oreFrame;
    /* Red Alert's V2 Rocket: how far its launch section is through its lift (0 down .. 1
       at the firing lift; drawn eased), from the engine's launcher state (doing 3 while it
       stands to fire, dostage the ticks before it may fire), the rocket's rounds as the
       engine last said them (ammo, -1 unknown), the ticks it was told it would fire in */
    float erect, erectP;
    int   v2Doing, v2Ammo, v2FireIn;
    /* Red Alert's Transport: each ramp's angle (degrees open from closed), in the order of
       its ramp parts, the door state the engine last said, the tick its closing began, and
       which ramp was the far one when the door began to open */
    float ramp[4], rampP[4];
    int   lstDoor, lstClose, lstFar, lstStageAt, lstFace;
    /* the Harvester's claws: how far through their change they stand (0 lowered to
       harvest .. 1 raised), whether it was harvesting on the last tick, and whether the
       state has been set (a harvester first seen takes its pose at once) */
    float claw, clawP;
    bool  clawSet, clawHarv;
    /* Red Alert's Ore Truck's load: how full its bed is drawn (0 empty .. 1 the heap as
       modelled), eased toward the engine's pips, and whether it has been set (a truck
       first seen takes its load at once) */
    float fill, fillP, fillWant;
    bool  fillSet;
    /* Red Alert's landing gear: how far it is stowed (0 down .. 1 up, drawn eased), from
       the aircraft's altitude, and the altitude the engine said on the last tick */
    float gear, gearP;
    int   gearAlt;
    int   shots;
    int   fire[4];                            /* the tick each barrel last fired */
    bool  seen;
    /* what the colour picture last drew, for the dump */
    int   dTur, dFrame[2];
    float dTrav[2], dRotor, dDisc, dDuct, dElev, dDoor, dTurn, dKick, dClip, dErect, dClaw;
    float dGear;
    float dDive;                              /* how far under the sea it was drawn (cells) */
    int   dCloak, dCstage;                    /* the engine's cloak state and stage then */
    /* the lean the hull was drawn at, radians about world X and Z (draw_mesh's witness):
       a ship's roll on the sea, which changes as it sails */
    float dWob[2];
    bool  dRocket;
    bool  drawnNow;
};
static std::map<int, EvhcUnit> g_evhcUnit;
static int g_evhcLastTick = -1;

/* ---- a wreck: the DESTROYED pieces, falling, settling, sinking ---------------------- */
struct EvhcWreck {
    int   model, id, kind, house;
    char  type[16];
    int   deathTick, impactTick;
    bool  air, falling, sinking, dropped;
    bool  wet;                                /* it came to rest on a water cell */
    EbldBody carrier;                         /* an aircraft falls whole until it lands */
    std::vector<float> off;                   /* per piece: offset in the carrier, cells */
    std::vector<float> qoff;                  /* per piece: rotation in the carrier */
    std::vector<EbldBody> body;
    float wx, wz, ground;
};
static std::map<long long, EvhcWreck> g_evhcWreck;
static long long g_evhcWreckSerial = 0;

/* WATER IS THE MAP'S OWN: a cell with holes in its ground is one the sea shows through
   (the radar paints those blue). A wreck that comes to rest on one goes down at once,
   whatever it was: a boat, a hovercraft off the beach, an aircraft shot down over the sea.
   A hovercraft killed on dry sand stays on the sand like any vehicle. */
static bool evhc_cell_wet(float wx, float wz)
{
    const int cx = (int)floorf(wx), cz = (int)floorf(wz);
    /* RED ALERT'S OWN GROUND keeps its sea in the water's own source (water_mod.h), cell by
       cell, not in the scene pack's holes: a ship sunk there goes down too */
    if (water_src_ra()) {
        for (size_t i = 0; i < g_waterRa.cell.size(); i++)
            if (g_waterRa.cell[i].x == cx && g_waterRa.cell[i].y == cz)
                return g_waterRa.cell[i].holes != 0;
        return false;
    }
    return cx >= 0 && cx < g_gridW && cz >= 0 && cz < g_gridH
        && g_cellHoles[cz * g_gridW + cx] != 0;
}
static bool evhc_on_water(const EvhcWreck& E)
{
    return E.wet;
}

static bool evhc_finite(float f) { return f == f && f < 3.0e38f && f > -3.0e38f; }

/* ================================================================================
 * THE FILE. Read whole, checked completely, refused whole on any inconsistency.
 * ============================================================================== */
static bool evhc_read_one(const char* path, EvhcModel& V)
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
    EbldModel& m = V.m;
    EbldIn in;
    in.b = &b; in.o = 0; in.bad = false; in.why[0] = 0;
    char msg[200];
    char magic[4] = { 0, 0, 0, 0 }, type[9];
    in.take(magic, 4);
    if (!in.bad && memcmp(magic, "EVHC", 4) != 0) in.fail("it is not an EVHC file");
    const uint32_t ver = in.u32();
    if (!in.bad && ver != 1 && ver != 2) {
        snprintf(msg, sizeof msg, "it is format version %u and this build reads 1 and 2", ver);
        in.fail(msg);
    }
    memset(type, 0, sizeof type);
    in.take(type, 8);
    type[8] = 0;
    const uint32_t ntex = in.u32(), nset = in.u32(), npart = in.u32(), nparam = in.u32();
    if (!in.bad && (ntex == 0 || ntex > 64)) in.fail("its texture count is out of range");
    if (!in.bad && nset != 1 && nset != 2) in.fail("it carries neither one set nor two");
    if (!in.bad && npart > 64) in.fail("its part count is out of range");
    if (!in.bad && (nparam < EVQ_N_MIN || nparam > 256))
        in.fail("its parameter table is too short or too long");
    m.type = type;
    m.tag = "evhc";
    m.version = 3;
    m.param.clear();
    for (uint32_t i = 0; i < nparam && !in.bad; i++) m.param.push_back(in.f32());
    /* the textures and the sets are the buildings' records; the wreck is the buildings'
       DESTROYED set, its parents the INTACT pieces. Version 2's pieces and triangles are
       the buildings' version 5 records, each corner with its smooth normal; version 1's
       are version 3's, and each corner takes its face's normal. */
    static const int SETOF[2] = { EBS_INTACT, EBS_DESTROYED };
    ebld_read_body(in, b, m, 3, ntex, nset, 11, SETOF, ver >= 2 ? 5 : 3);
    m.nset = (int)nset;
    const int nip = (int)m.set[EBS_INTACT].piece.size();
    if (!in.bad)
        for (size_t k = 0; k < m.set[EBS_DESTROYED].piece.size(); k++)
            if (m.set[EBS_DESTROYED].piece[k].parent < 0
                || m.set[EBS_DESTROYED].piece[k].parent >= nip) {
                in.fail("a wreck piece names an intact piece that does not exist");
                break;
            }
    V.part.clear();
    int last = 0;
    for (uint32_t k = 0; k < npart && !in.bad; k++) {
        EvhcPart p;
        p.piece = in.u16();
        p.kind = in.u8();
        p.parent = (signed char)in.u8();
        p.driver = in.u8();
        p.side = in.u8();
        p.flags = in.u8();
        (void)in.u8();
        for (int c = 0; c < 12; c++) p.a[c] = in.f32();
        if (in.bad) break;
        if (p.piece <= last || p.piece >= nip || p.kind >= EVP_N || p.parent >= (int)k
            || p.parent < -1 || p.driver >= EVD_N || p.side > 2) {
            snprintf(msg, sizeof msg, "part %u is out of order or impossible", k);
            in.fail(msg);
            break;
        }
        last = p.piece;
        V.part.push_back(p);
    }
    V.liftPart = -1;
    V.rigLift = -1.0f;
    V.hoopPart = -1;
    V.rigHoop = V.rigTank = -1.0f;
    V.rigHoopMid = V.rigHoopFull = V.rigHoopBad = 0;
    V.gear = false;
    for (size_t k = 0; k < V.part.size(); k++) {
        if (V.part[k].kind == EVP_HINGE && V.part[k].driver == EVD_HARV_LIFT)
            V.liftPart = (int)k;
        if (V.part[k].kind == EVP_HINGE && V.part[k].driver == EVD_GEAR) V.gear = true;
        if (V.part[k].kind == EVP_HINGE && V.part[k].driver == EVD_HARV_HOOP)
            V.hoopPart = (int)k;
    }
    V.blast.clear();
    if (!in.bad) {
        const uint32_t nb = in.u32();
        if (!in.bad && nb != (uint32_t)m.set[EBS_DESTROYED].piece.size())
            in.fail("its blast table does not match the wreck's pieces");
        for (uint32_t k = 0; k < nb * 5 && !in.bad; k++) V.blast.push_back(in.f32());
    }
    /* VERSION 2: the lights, after everything */
    V.light.clear();
    if (!in.bad && ver >= 2) {
        const uint32_t nl = in.u32();
        if (!in.bad && nl > 64) in.fail("its light count is out of range");
        for (uint32_t k = 0; k < nl && !in.bad; k++) {
            EvhcLight L;
            memset(&L, 0, sizeof L);
            in.take(L.name, 16);
            L.name[16] = 0;
            L.piece = in.u16();
            L.kind = in.u8();
            L.flags = in.u8();
            for (int c = 0; c < 3; c++) L.pos[c] = in.f32();
            for (int c = 0; c < 3; c++) L.dir[c] = in.f32();
            for (int c = 0; c < 3; c++) L.col[c] = in.f32();
            L.cone[0] = in.f32(); L.cone[1] = in.f32();
            L.range = in.f32();
            L.intensity = in.f32();
            for (int c = 0; c < 3; c++) L.lensN[c] = in.f32();
            L.lensR = in.f32();
            L.lensUV[0] = in.f32(); L.lensUV[1] = in.f32();
            if (in.bad) break;
            bool fin = evhc_finite(L.lensR);
            for (int c = 0; c < 3; c++)
                fin = fin && evhc_finite(L.pos[c]) && evhc_finite(L.dir[c]) && evhc_finite(L.col[c])
                      && evhc_finite(L.lensN[c]);
            if (!fin || L.piece >= nip || L.kind >= EVL_N || (L.flags & ~3)
                || !(L.cone[0] >= 0.0f && L.cone[0] <= L.cone[1] && L.cone[1] <= 90.0f)
                || !(L.range > 0.0f) || !(L.intensity >= 0.0f) || !(L.lensR > 0.0f)) {
                snprintf(msg, sizeof msg, "light %u is impossible", k);
                in.fail(msg);
                break;
            }
            V.light.push_back(L);
        }
    }
    if (!in.bad && in.o != b.size()) {
        snprintf(msg, sizeof msg, "%lu bytes follow the %s table",
                 (unsigned long)(b.size() - in.o), ver >= 2 ? "light" : "blast");
        in.fail(msg);
    }
    if (!in.bad && (int)V.part.size() != nip - 1)
        in.fail("an intact piece moves with no part record, or the hull does");
    if (in.bad) {
        fprintf(stderr, "evhc: %s refused: %s\n", path, in.why);
        return false;
    }
    V.fileVer = (int)ver;
    m.path = path;
    m.fileSize = (long)b.size();
    m.flags = 0;
    m.emit.clear();
    m.move.clear();
    m.moveOf.assign(nip, -1);
    m.emitState[0] = m.emitState[1] = false;
    m.nchip = 0;
    m.attached = false;
    m.tex0 = -1;
    for (int i = 0; i < EBM_N; i++) m.mesh[i] = -1;
    m.shadow0 = m.shadow1 = 0;
    m.drawn = 0;
    m.resident = false;
    for (int s = 0; s < EBS_N; s++) { m.set[s].hasSlab = false; m.set[s].slabTop = 0.0f; }
    V.partOf.assign(nip, -1);
    V.turret = V.rotors = V.discs = false;
    V.nbarrel = 0;
    for (size_t k = 0; k < V.part.size(); k++) {
        const EvhcPart& p = V.part[k];
        V.partOf[p.piece] = (int)k;
        if (p.kind == EVP_TURRET) V.turret = true;
        if (p.kind == EVP_ROTOR) { V.rotors = true; if (p.flags & EVF_DISC) V.discs = true; }
        if (p.kind == EVP_RECOIL) V.nbarrel++;
    }
    V.loadPart = -1;
    V.loadFloor = 0.0f;
    for (size_t k = 0; k < V.part.size(); k++) {
        const EvhcPart& p = V.part[k];
        if (p.kind != EVP_HINGE || p.driver != EVD_ORE_LOAD) continue;
        const EbldPiece& q = m.set[EBS_INTACT].piece[p.piece];
        float lo = 3.0e38f;
        for (int t = q.tri0; t < q.tri0 + q.ntri && t < (int)m.set[EBS_INTACT].tri.size(); t++)
            for (int c = 0; c < 3; c++)
                if (m.set[EBS_INTACT].tri[t].v[c].y < lo) lo = m.set[EBS_INTACT].tri[t].v[c].y;
        if (lo < 3.0e38f) { V.loadPart = (int)k; V.loadFloor = lo; }
    }
    V.cls = (int)m.param[EVQ_CLASS];
    V.gauge = m.param[EVQ_GAUGE];
    V.rigOk = false;
    V.rigYaw = V.rigX = V.rigZ = V.rigErr = 0.0f;
    V.drawn = 0;
    V.rigDrawn = 0;
    return true;
}

/* Every vehicle and aircraft the engine knows, scanned by name, so installing a new one is
   dropping its file in. Once per process: the files do not depend on the mission. */
static void evhc_load(const char* dir)
{
    if (g_evhcLoaded) return;
    g_evhcLoaded = true;
    static const char* const TYPES_TD[] = {
        "HTNK", "MTNK", "LTNK", "FTNK", "STNK", "APC", "ARTY", "MSAM", "HARV", "JEEP",
        "BGGY", "BIKE", "MCV", "MHQ", "BOAT", "LST", "HELI", "ORCA", "TRAN", "A10", "C17",
        "MLRS", NULL
    };
    /* RED ALERT'S MODULE reads its own folder, ra3d/evhc, named by Red Alert's types: every
       unit and aircraft its engine knows. A file is either a Red Alert remaster baked under
       its own name or an OpenCNC3D remaster the Red Alert lab approved as reuse, staged under
       the Red Alert type with its header renamed (tools/ra3d/stage-vehicles.py), so the type
       check below holds for both. A type with no file keeps its 1996 sprite card. */
    /* Its vessels are read too (the brain's VESSEL heap: the Gun Boat, the submarines, the
       Destroyer, the Cruiser, the Transport, the expansions' Missile Submarine and
       Carrier), drawn at sea by the same rule as its units (evhc_for). */
    static const char* const TYPES_RA[] = {
        "1TNK", "2TNK", "3TNK", "4TNK", "V2RL", "ARTY", "HARV", "MCV", "JEEP", "APC",
        "MNLY", "TRUK", "MRJ", "MGG", "STNK", "CTNK", "TTNK", "QTNK", "DTRK",
        "TRAN", "BADR", "U2", "MIG", "YAK", "HELI", "HIND",
        "PT", "SS", "DD", "CA", "LST", "MSUB", "CARR", NULL
    };
    const char* const* TYPES = g_raModule ? TYPES_RA : TYPES_TD;
    std::string names;
    for (size_t i = 0; TYPES[i]; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s%s/%s.evhc", dir && *dir ? dir : "",
                 g_raModule ? "ra3d/evhc" : "evhc", TYPES[i]);
        EvhcModel V;
        if (!evhc_read_one(path, V))
            continue;
        if (V.m.type != TYPES[i]) {
            fprintf(stderr, "evhc: %s says it is %s -- refused\n", path, V.m.type.c_str());
            continue;
        }
        ebld_spec_read(path, V.m);
        if (V.m.hasSpec) {
            int k = 0;
            for (size_t t = 0; t < V.m.spec.size(); t++) if (V.m.spec[t].from >= 0) k++;
            fprintf(stderr, "evhc: %s carries %d specular map%s\n", V.m.type.c_str(), k, k == 1 ? "" : "s");
        }
        names += (names.empty() ? "" : " ") + V.m.type;
        g_evhc.push_back(V);
    }
    fprintf(stderr, "evhc: %d remastered vehicle%s and aircraft for Enhanced%s%s\n",
            (int)g_evhc.size(), g_evhc.size() == 1 ? "" : "s",
            names.empty() ? "" : ": ", names.c_str());
}

/* ---- small matrix helpers: 3x4 row-major, v' = M v, pack units ---------------------- */
static void ev_ident(float* M)
{
    static const float I[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
    memcpy(M, I, sizeof I);
}
/* R = A * B: B first, then A */
static void ev_mul(const float* A, const float* B, float* R)
{
    float o[12];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++)
            o[i * 4 + j] = A[i * 4] * B[j] + A[i * 4 + 1] * B[4 + j] + A[i * 4 + 2] * B[8 + j];
        o[i * 4 + 3] = A[i * 4] * B[3] + A[i * 4 + 1] * B[7] + A[i * 4 + 2] * B[11] + A[i * 4 + 3];
    }
    memcpy(R, o, sizeof o);
}
static void ev_apply(const float* M, const float* v, float* o)
{
    for (int i = 0; i < 3; i++)
        o[i] = M[i * 4] * v[0] + M[i * 4 + 1] * v[1] + M[i * 4 + 2] * v[2] + M[i * 4 + 3];
}
static bool ev_is_ident(const float* M)
{
    static const float I[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
    for (int i = 0; i < 12; i++) if (fabsf(M[i] - I[i]) > 1e-6f) return false;
    return true;
}

/* THE CARTRIDGE'S CLIP, for one part at one time: the same lerp between two baked frames
   draw_mesh makes for its own animated parts, handed out as the matrix instead of spent on
   the cartridge's triangles. False (identity) when the mesh or part carries none. `vis`
   takes whether the node is drawn on that frame. */
static bool evhc_anim_mat(int mi, int part, float animT, float* M, bool* vis)
{
    ev_ident(M);
    if (vis) *vis = true;
    if (mi < 0 || mi >= (int)g_pack.mesh.size() || animT < 0.0f) return false;
    const PackMesh& mesh = g_pack.mesh[mi];
    const size_t np = mesh.parts.size();
    if (mesh.animFrames <= 0 || part < 0 || part >= (int)np
        || mesh.animMat.size() < (size_t)mesh.animFrames * np * 12)
        return false;
    const int nf = mesh.animFrames;
    int f0 = (int)animT;
    float mix = animT - (float)f0;
    if (f0 >= nf) { f0 = nf - 1; mix = 0.0f; }
    int f1 = f0 + 1;
    if (f1 >= nf) f1 = nf - 1;
    if (vis && !mesh.animVis.empty()) *vis = mesh.animVis[(size_t)f0 * np + part] != 0;
    const float* m0 = &mesh.animMat[((size_t)f0 * np + part) * 12];
    const float* m1 = &mesh.animMat[((size_t)f1 * np + part) * 12];
    for (int q = 0; q < 12; q++) M[q] = m0[q] + (m1[q] - m0[q]) * mix;
    return true;
}

/* A rotation about the vertical by `ang` radians in draw_mesh's own sense (its turret
   and facing turn: x' = x c + z s, z' = -x s + z c), about a point in the plane. */
static void ev_yaw_about(float px, float pz, float ang, float* M)
{
    const float c = cosf(ang), s = sinf(ang);
    const float R[12] = { c, 0, s, px - c * px - s * pz,
                          0, 1, 0, 0,
                          -s, 0, c, pz + s * px - c * pz };
    memcpy(M, R, sizeof R);
}

/* ================================================================================
 * ATTACHING TO THE PACK just loaded: textures reserved for residency, the intact and
 * the wreck meshes built, and the dock and deploy rigs matched to the pieces.
 * ============================================================================== */
static int procrig_mesh(void);
static int mcvrig_mesh(void);

/* The centre of a rig part's triangles, pack units, at its rest (frame 0 of its clip). */
static bool evhc_rig_centre(int rmi, int part, float* c)
{
    if (rmi < 0 || rmi >= (int)g_pack.mesh.size()) return false;
    const PackMesh& rm = g_pack.mesh[rmi];
    if (part < 0 || part >= (int)rm.parts.size() || rm.parts[part].ntris <= 0) return false;
    double s[3] = { 0, 0, 0 };
    int n = 0;
    float A[12];
    bool vis = true;
    evhc_anim_mat(rmi, part, 0.0f, A, &vis);
    for (int t = rm.parts[part].tri0; t < rm.parts[part].tri0 + rm.parts[part].ntris; t++)
        for (int k = 0; k < 3; k++) {
            const float v[3] = { rm.tris[t].v[k].x, rm.tris[t].v[k].y, rm.tris[t].v[k].z };
            float w[3];
            ev_apply(A, v, w);
            for (int q = 0; q < 3; q++) s[q] += w[q];
            n++;
        }
    if (!n) return false;
    for (int q = 0; q < 3; q++) c[q] = (float)(s[q] / n);
    return true;
}
/* The centre of an intact piece, pack units. */
static void evhc_piece_centre(const EvhcModel& V, int k, float* c)
{
    const EbldPiece& P = V.m.set[EBS_INTACT].piece[k];
    for (int q = 0; q < 3; q++) c[q] = P.c[q];
}

/* THE HARVESTER'S PLACE IN THE REFINERY'S DOCK, from the file (build_HARV.py's fit of the
   remaster onto the rig's own harvester): a turn and a shift in the refinery's frame. */
static void evhc_dock_matrix(const EvhcModel& V, float* M)
{
    const float yaw = V.m.param.size() > EVQ_DOCK_YAW ? V.m.param[EVQ_DOCK_YAW] : 0.0f;
    const float dx = V.m.param.size() > EVQ_DOCK_X ? V.m.param[EVQ_DOCK_X] : 0.0f;
    const float dz = V.m.param.size() > EVQ_DOCK_Z ? V.m.param[EVQ_DOCK_Z] : 0.0f;
    const float c = cosf(yaw), s = sinf(yaw);
    const float R[12] = { c, 0, s, dx,  0, 1, 0, 0,  -s, 0, c, dz };
    memcpy(M, R, sizeof R);
}

static void evhc_attach(void)
{
    g_evhcPackSerial++;
    if (g_evhc.empty()) return;
    int attached = 0;
    for (size_t i = 0; i < g_evhc.size(); i++) {
        EvhcModel& V = g_evhc[i];
        EbldModel& M = V.m;
        M.attached = false;
        V.rigOk = false;
        V.rigPart.assign(M.set[EBS_INTACT].piece.size(), -1);
        for (int k = 0; k < EBM_N; k++) { M.mesh[k] = -1; M.rng[k].clear(); }
        /* (Red Alert's scene packs carry no cartridge types: every staged file attaches, as
           its remastered buildings do) */
        std::map<std::string, PackType>::const_iterator it = g_pack.type.find(M.type);
        if (!g_raModule && it == g_pack.type.end()) continue;
        M.tex0 = (int)g_pack.tex.size();
        M.resident = false;
        V.texUp.assign(M.tex.size(), 0);
        V.texIntact.assign(M.tex.size(), 0);
        V.intactUp = V.allUp = false;
        for (size_t t = 0; t < M.set[EBS_INTACT].tri.size(); t++) {
            const int ti = M.set[EBS_INTACT].tri[t].tex;
            if (ti >= 0 && ti < (int)M.tex.size()) V.texIntact[ti] = 1;
        }
        for (size_t t = 0; t < M.tex.size(); t++) {
            PackTex pt;
            pt.w = pt.uw = M.tex[t].w;
            pt.h = pt.uh = M.tex[t].h;
            pt.gl = pt.gl_gdi = 0;
            for (int e = 0; e < 7; e++) pt.gl_extra[e] = 0;
            g_pack.tex.push_back(pt);
        }
        const char* ty = M.type.c_str();
        {
            PackMesh pm;
            memset(pm.name, 0, sizeof pm.name);
            snprintf(pm.name, sizeof pm.name, "evhc:%s:intact", ty);
            ebld_fill_mesh(pm, M, M.set[EBS_INTACT], EBT_TEX, -1, M.rng[EBM_INTACT]);
            M.shadow0 = M.shadow1 = (int)pm.tris.size();
            M.mesh[EBM_INTACT] = ebld_mesh_end(pm);
        }
        if (M.nset == 2) {
            PackMesh pm;
            memset(pm.name, 0, sizeof pm.name);
            snprintf(pm.name, sizeof pm.name, "evhc:%s:wreck", ty);
            ebld_fill_mesh(pm, M, M.set[EBS_DESTROYED], EBT_TEX, -1, M.rng[EBM_WRECK]);
            M.mesh[EBM_WRECK] = ebld_mesh_end(pm);
            /* and its glow: the triangles with an emit map, drawn additively over it */
            PackMesh pg;
            memset(pg.name, 0, sizeof pg.name);
            snprintf(pg.name, sizeof pg.name, "evhc:%s:glowx", ty);
            ebld_fill_mesh(pg, M, M.set[EBS_DESTROYED], EBT_GLOW, -1, M.rng[EBM_GLOWX]);
            M.mesh[EBM_GLOWX] = ebld_mesh_end(pg);
        }
        M.attached = true;
        attached++;
        /* THE RIGS. The refinery's dock rig moves the harvester's hull as its part 0 and
           its hopper as part 2 (the file's hinge record names it); its collector arms
           ride whichever rig part lies nearest their own centre once the harvester is in
           the dock. The MCV's deploy groups name their rig part in the file. */
        const EbldSet& S = M.set[EBS_INTACT];
        if (M.type == "HARV") {
            const int rmi = procrig_mesh();
            if (rmi >= 0 && rmi < (int)g_pack.mesh.size()) {
                float D[12];
                evhc_dock_matrix(V, D);
                const int nrp = (int)g_pack.mesh[rmi].parts.size();
                for (size_t k = 0; k < S.piece.size(); k++) {
                    const int pi = V.partOf[k];
                    int rp = 0;
                    if (pi >= 0 && V.part[pi].kind == EVP_HINGE
                        && V.part[pi].driver == EVD_HARV_HOPPER)
                        rp = (int)V.part[pi].a[9];
                    /* (arms on the claws' lift ride it, and so the hull's rig part, as
                       the booms they are pinned to do) */
                    else if (pi >= 0 && V.part[pi].kind == EVP_CLIP && V.part[pi].parent < 0) {
                        float c[3], w[3];
                        evhc_piece_centre(V, (int)k, c);
                        ev_apply(D, c, w);
                        float best = 1e30f;
                        for (int p = 1; p < nrp; p++) {
                            if (p == 2) continue;
                            float rc[3];
                            if (!evhc_rig_centre(rmi, p, rc)) continue;
                            const float d = (rc[0] - w[0]) * (rc[0] - w[0])
                                          + (rc[2] - w[2]) * (rc[2] - w[2]);
                            if (d < best) { best = d; rp = p; }
                        }
                    }
                    if (rp < 0 || rp >= nrp) rp = 0;
                    V.rigPart[k] = rp;
                }
                V.rigOk = true;
            }
        }
        if (M.type == "MCV") {
            /* THE DEPLOY RIG'S FRAME, fitted: the rig holds the MCV turned and shifted from
               the way it drives. The best turn and shift in the plane that puts the five
               deploy groups' centres onto their rig parts' centres at the rig's first
               frame (a two-dimensional Procrustes fit, closed form). */
            const int rmi = mcvrig_mesh();
            std::vector<float> A, B;
            for (size_t k = 0; k < S.piece.size(); k++) {
                const int pi = V.partOf[k];
                if (pi < 0 || V.part[pi].kind != EVP_DEPLOY) continue;
                const int rp = (int)V.part[pi].a[9];
                float rc[3], c[3];
                if (!evhc_rig_centre(rmi, rp, rc)) continue;
                evhc_piece_centre(V, (int)k, c);
                A.push_back(c[0]); A.push_back(c[2]);
                B.push_back(rc[0]); B.push_back(rc[2]);
                V.rigPart[k] = rp;
            }
            const int n = (int)A.size() / 2;
            if (n >= 2) {
                double ax = 0, az = 0, bx = 0, bz = 0;
                for (int j = 0; j < n; j++) { ax += A[2*j]; az += A[2*j+1]; bx += B[2*j]; bz += B[2*j+1]; }
                ax /= n; az /= n; bx /= n; bz /= n;
                double sc = 0, ss = 0;
                for (int j = 0; j < n; j++) {
                    const double x = A[2*j] - ax, z = A[2*j+1] - az;
                    const double X = B[2*j] - bx, Z = B[2*j+1] - bz;
                    /* the angle in ev_yaw_about's sense: X = x c + z s, Z = -x s + z c */
                    sc += x * X + z * Z;
                    ss += z * X - x * Z;
                }
                const double ang = atan2(ss, sc);
                const double c = cos(ang), s = sin(ang);
                V.rigYaw = (float)ang;
                V.rigX = (float)(bx - (ax * c + az * s));
                V.rigZ = (float)(bz - (-ax * s + az * c));
                double err = 0;
                for (int j = 0; j < n; j++) {
                    const double x = A[2*j] * c + A[2*j+1] * s + V.rigX - B[2*j];
                    const double z = -A[2*j] * s + A[2*j+1] * c + V.rigZ - B[2*j+1];
                    err += sqrt(x * x + z * z);
                }
                V.rigErr = (float)(err / n);
                V.rigOk = true;
                /* the running gear rides its group: the same rig part as its parent */
                for (size_t k = 0; k < S.piece.size(); k++) {
                    int pi = V.partOf[k];
                    while (pi >= 0 && V.part[pi].kind != EVP_DEPLOY) pi = V.part[pi].parent;
                    if (pi >= 0) V.rigPart[k] = (int)V.part[pi].a[9];
                }
                fprintf(stderr, "evhc: MCV deploy rig fitted: turned %.1f degrees, shifted "
                        "%.3f, %.3f cells, %.3f cells off on average over %d groups\n",
                        V.rigYaw * 180.0f / 3.14159265f, V.rigX * MODEL_SCALE,
                        V.rigZ * MODEL_SCALE, V.rigErr * MODEL_SCALE, n);
            }
        }
    }
    fprintf(stderr, "evhc: %d of %d remastered vehicles and aircraft attached to %s\n",
            attached, (int)g_evhc.size(), g_pack.scen);
}

/* THE UPLOAD, the buildings' (ebld_resident) done for a subset: the standing model's
   textures, or every one. Read back out of the file, which must still be the one checked;
   bled, filtered and wrapped as a remastered building's; its liveries painted if the pack's
   are already built. A file that cannot be read back detaches the model, and the cartridge
   draws its type. */
static bool evhc_resident(EvhcModel& V, bool all)
{
    EbldModel& M = V.m;
    if (all ? V.allUp : V.intactUp) return true;
    if (!M.attached) return false;
    const Uint64 t0 = SDL_GetPerformanceCounter();
    GLint bound = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    FILE* tf = fopen(M.path.c_str(), "rb");
    if (tf) {
        fseek(tf, 0, SEEK_END);
        if (ftell(tf) != M.fileSize) { fclose(tf); tf = NULL; }
    }
    bool readOk = (tf != NULL);
    long bytes = 0;
    int n = 0;
    const size_t tex0 = (size_t)M.tex0;
    for (size_t t = 0; t < M.tex.size() && readOk; t++) {
        if (V.texUp[t] || (!all && !V.texIntact[t])) continue;
        const EbldTex& T = M.tex[t];
        const size_t sz = (size_t)T.w * T.h * 4;
        std::vector<unsigned char> rgba(sz), nod, mask;
        fseek(tf, T.off, SEEK_SET);
        readOk = fread(&rgba[0], 1, sz, tf) == sz;
        if (readOk && (T.house & 1)) { nod.resize(sz); readOk = fread(&nod[0], 1, sz, tf) == sz; }
        if (readOk && (T.house & 2)) {
            mask.resize(sz / 4);
            readOk = fread(&mask[0], 1, sz / 4, tf) == sz / 4;
        }
        if (!readOk || tex0 + t >= g_pack.tex.size()) { readOk = false; break; }
        PackTex& pt = g_pack.tex[tex0 + t];
        /* RED ALERT'S SIDES ARE NOT TIBERIAN DAWN'S: the base slot is the Allies' light
           blue (livery_slot), so it takes the GDI look recoloured through the house mask
           on Red Alert's light blue ramp, as the remastered buildings' textures do under
           Red Alert (ebld_mod.h), and not the Nod decode; the gold slot keeps the GDI look
           and every other colour, the Soviets' red among them, is painted from it by the
           liveries */
        if (g_raModule && !nod.empty()) {
            nod = rgba;
            for (size_t q = 0; q < mask.size(); q++)
                if (mask[q]) livery_recolour(&rgba[q * 4], mask[q], 1, &nod[q * 4]);
        }
        /* its specular map, the same size and filtering as the texture (alpha the
           texture's own, so the cut-out pass cuts the same holes) */
        if (!pt.gl_spec) {
            std::vector<unsigned char> sp;
            if (ebld_spec_pixels(M, t, rgba, sp)) pt.gl_spec = n64fix_upload(sp, T.w, T.h);
        }
        if (nod.empty()) {
            pt.gl = n64fix_upload(rgba, T.w, T.h);
            bytes += (long)sz;
        } else {
            pt.gl = n64fix_upload(nod, T.w, T.h);
            pt.gl_gdi = n64fix_upload(rgba, T.w, T.h);
            pt.gdi.swap(rgba);
            pt.mask.swap(mask);
            bytes += 2 * (long)sz;
        }
        V.texUp[t] = 1;
        livery_build_range(tex0 + t, tex0 + t + 1);
        n++;
    }
    if (tf) fclose(tf);
    glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
    if (!readOk) {
        fprintf(stderr, "evhc: %s changed, went away or could not be read back -- %s is drawn "
                        "by the cartridge from here on\n", M.path.c_str(), M.type.c_str());
        M.attached = false;
        return false;
    }
    V.intactUp = true;
    if (all) V.allUp = true;
    M.resident = V.allUp;
    g_ebldResidentBytes += bytes;
    fprintf(stderr, "evhc: %s %s textures on the card, %d textures, %.1f MB (%.1f MB of "
            "remastered textures in all), %.1f ms\n", M.type.c_str(),
            all ? "wreck" : "standing", n, (double)bytes / 1048576.0,
            (double)g_ebldResidentBytes / 1048576.0,
            (double)(SDL_GetPerformanceCounter() - t0) * 1000.0
                / (double)SDL_GetPerformanceFrequency());
    return true;
}

/* ---- the rule ---------------------------------------------------------------------- */
static bool evhc_rule_now(void) { return g_evhcOn && g_fx.enabled; }

static bool evhc_meshes_ok(const EvhcModel& V)
{
    const EbldModel& M = V.m;
    if (!M.attached) return false;
    if (M.mesh[EBM_INTACT] < 0 || M.mesh[EBM_INTACT] >= (int)g_pack.mesh.size()) return false;
    if (M.nset == 2 && (M.mesh[EBM_WRECK] < 0 || M.mesh[EBM_WRECK] >= (int)g_pack.mesh.size()))
        return false;
    return true;
}
static int evhc_model_of(const char* type)
{
    for (size_t i = 0; i < g_evhc.size(); i++) {
        const EvhcModel& V = g_evhc[i];
        if (V.m.attached && V.m.type == type) return evhc_meshes_ok(V) ? (int)i : -1;
    }
    return -1;
}
/* The authored art set's choice for an object (model3d_mod.h). */
static int object_model3d(const SimObject& o);
/* A KIND THIS SYSTEM DRAWS: units and aircraft, and under Red Alert its vessels (Tiberian
   Dawn's gunboat and hovercraft are units; its brain exports no vessel). */
static bool evhc_kind(int kind)
{
    return kind == K_UNIT || kind == K_AIRCRAFT || (g_raModule && kind == K_VESSEL);
}
/* Whether this system draws this object: a unit or aircraft (or a Red Alert vessel) of a
   type with a model, under the rule, where the authored art set has none. */
static int evhc_for(const SimObject& o)
{
    if (!evhc_kind(o.kind) || g_evhc.empty() || !evhc_rule_now())
        return -1;
    const int m = evhc_model_of(o.type);
    if (m < 0) return -1;
    if (object_model3d(o) >= 0) return -1;
    return m;
}

/* Only TD's remastered Harvester: cartridge and Red Alert motion remain unchanged. */
static bool evhc_suspension_vehicle(const SimObject& o)
{
    return !g_raModule && o.kind == K_UNIT && !strcmp(o.type, "HARV") && evhc_for(o) >= 0;
}

/* The deck the hovercraft's riders stand on: the remaster's, where the hovercraft is
   drawn remastered; the cartridge's otherwise. */
static float evhc_deck_y(const char* type, float cart)
{
    if (!evhc_rule_now()) return cart;
    const int m = evhc_model_of(type);
    if (m < 0) return cart;
    const EvhcModel& V = g_evhc[m];
    if ((int)V.m.param.size() <= EVQ_DECK || V.m.param[EVQ_DECK] <= 0.0f) return cart;
    return V.m.param[EVQ_DECK];
}

/* The hull's facing: PrimaryFacing for a vehicle (the rocket launcher's too: its launcher
   is a turret part here and follows the turret facing on its own), the aircraft's body
   facing for an aircraft, both through the type's authored-forward bias. */
static int evhc_face(const SimObject& o)
{
    if (o.kind == K_AIRCRAFT) return draw_facing(o);
    if (o.face < 0) return -1;
    const int bias = g_nofacefix ? (((int)g_faceBias) & 255) : model_face_bias(o.type);
    return (o.face + bias) & 255;
}
static int evhc_turret_delta(const EvhcModel& V, const SimObject& o)
{
    /* (a Red Alert vessel's turret facing is its Turret_Facing, the brain's tface=) */
    if (!V.turret || (o.kind != K_UNIT && o.kind != K_VESSEL) || o.face < 0 || o.tface < 0
        || g_legacy) return 0;
    return (o.tface - o.face) & 255;
}

/* Keep tick observations above integral; only posed/drawn geometry interpolates. */
static float evhc_draw_face(const SimObject& o)
{
    const int face = evhc_face(o);
    if (face < 0) return (float)face;
    const bool secondary = o.kind == K_AIRCRAFT && o.tface >= 0;
    const int raw = secondary ? o.tface : o.face;
    return heading_wrap((float)face + smooth_heading_of(o, secondary) - (float)raw);
}
static float evhc_draw_turret_delta(const EvhcModel& V, const SimObject& o)
{
    if (!V.turret || (o.kind != K_UNIT && o.kind != K_VESSEL)
        || o.face < 0 || o.tface < 0 || g_legacy) return 0.0f;
    return heading_wrap(smooth_heading_of(o,true) - smooth_heading_of(o,false));
}

/* ================================================================================
 * RED ALERT. Its 3D modes draw every object as its 1996 sprite card (ra3d_sprites_mod.h)
 * except where a model is staged for its type: a unit or aircraft whose type has a file in
 * ra3d/evhc is drawn, moved and broken apart here exactly as Tiberian Dawn's are, and its
 * card (body, turret, rotors, shadow) stands down. The engine's dump carries the same
 * fields for both games (facing, turret facing, altitude brought to Tiberian Dawn's scale
 * at the parse, strength, cloak), so nothing below is Red Alert's own but the side colours
 * (the base slot is the Allies' light blue, evhc_resident) and the shots' names (evhc_step).
 * Classic and the hidden N64 mode keep the cards: the rule needs Enhanced.
 * ============================================================================== */
static bool evhc_resident(EvhcModel& V, bool all);
/* Whether this system draws `o` in Red Alert's 3D pass now: its model attached and its
   standing textures on the card, so the card can stand down. Asked inside a frame (it may
   upload the textures, as the remastered buildings' readiness does). */
static bool evhc_ra_ready(const SimObject& o)
{
    if (!g_raModule || g_fx.dos_classic) return false;
    const int m = evhc_for(o);
    if (m < 0 || !evhc_resident(g_evhc[m], false)) return false;
    /* a side's colour first seen after the textures went up is painted now (cheap once
       painted: livery_build_one answers at once for a colour it already has) */
    livery_build_range((size_t)g_evhc[m].m.tex0,
                       (size_t)g_evhc[m].m.tex0 + g_evhc[m].m.tex.size());
    return true;
}
/* The pick volume of such a unit: its standing model, at the facing it is drawn with and
   lifted as an aircraft is drawn, so a click lands on the picture. -1 for anything else. */
static int evhc_ra_pick(const SimObject& o, int* face, float* lift)
{
    if (!evhc_ra_ready(o)) return -1;
    const int m = evhc_for(o);
    *face = evhc_face(o);
    *lift = alt_lift(o);
    return g_evhc[m].m.mesh[EBM_INTACT];
}

/* ================================================================================
 * THE TICK. From the deduplicated tick edge, after efx_step (the shots are read off the
 * newborn muzzle anims) and after the smoothing window has been recorded.
 * ============================================================================== */
/* THE LANDING GEAR'S TARGET, 0 down .. 1 stowed, from the aircraft's altitude (0 on the
   ground .. 24 at flight level, the renderer's scale for both games). The engine climbs and
   descends a fixed wing aircraft one step a tick (Red Alert's Landing_Takeoff_AI), so the
   gear swings up between EVHC_GEAR_LO and EVHC_GEAR_HI on the climb, over about half a
   second after it leaves the runway, and comes down over the same heights on the approach,
   down and locked before it touches. Parked on its Airfield, taxiing or landed: down. */
static const float EVHC_GEAR_LO = 2.0f, EVHC_GEAR_HI = 10.0f;
static float evhc_gear_want(const SimObject& o)
{
    if (o.kind != K_AIRCRAFT || o.alt <= 0) return 0.0f;
    const float u = ((float)o.alt - EVHC_GEAR_LO) / (EVHC_GEAR_HI - EVHC_GEAR_LO);
    return u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;
}

static EvhcUnit* evhc_unit(const SimObject& o, int m)
{
    const int key = smooth_key(o);
    std::map<int, EvhcUnit>::iterator it = g_evhcUnit.find(key);
    if (it != g_evhcUnit.end() && it->second.model == m && !strcmp(it->second.type, o.type))
        return &it->second;
    EvhcUnit U;
    memset(&U, 0, sizeof U);
    U.model = m;
    U.id = o.id;
    U.kind = (int)o.kind;
    snprintf(U.type, sizeof U.type, "%s", o.type);
    U.tick = -1;
    for (int b = 0; b < 4; b++) U.fire[b] = -100000;
    /* A craft first seen in the air is already flying: its rotor at speed, its disc up. */
    if (o.kind == K_AIRCRAFT && o.alt > 0) { U.rate = 1.0f; U.disc = o.alt >= 8 ? 1.0f : o.alt / 8.0f; }
    else U.rate = 0.35f;
    U.elev = U.elevP = 18.7f;
    U.lstDoor = -1;
    /* and its gear already where its altitude puts it */
    U.gear = U.gearP = evhc_gear_want(o);
    U.gearAlt = o.alt;
    g_evhcUnit[key] = U;
    return &g_evhcUnit[key];
}

static float ev_ease(float cur, float want, float step)
{
    if (cur < want) return cur + step > want ? want : cur + step;
    if (cur > want) return cur - step < want ? want : cur - step;
    return cur;
}

/* THE DOOR'S TARGET, 0 shut .. 1 open, from what the unit itself says. */
static float evhc_door_want(const EvhcModel& V, const SimObject& o, int driver)
{
    switch (driver) {
    case EVD_APC_DOOR:
    case EVD_TRAN_RAMP:
        /* The engine's own door: for a unit or an aircraft the brain exports -1 while it
           is shut, 0 while it closes, 1 + Door_Stage() while it opens or stands open
           (dllinterface.cpp). An older brain says -1 always, and the door stays shut. */
        return o.doorstage >= 1 ? 1.0f : 0.0f;
    case EVD_LST_BOW:
        /* NOTHING IN THE ENGINE OPENS A HOVERCRAFT'S RAMP; the renderer decides from its
           unload: a hovercraft on its unload run that has stopped (no destination, not
           driving) is at the beach putting its cargo ashore, and raises the ramp as it
           drives off again. */
        return (!strcmp(o.mission, "Unload") && !o.navset && !o.driving) ? 1.0f : 0.0f;
    default:
        (void)V;
        return 0.0f;
    }
}

static void evhc_note_fire(int key, int frame);
static void evhc_wreck_step(int frame);
static const int EVHC_V2_RAISE = 8;             /* ticks the V2's launch section rises over */
static void evhc_v2_launch(const SimObject& o, int m, const EvhcUnit* U, int frame);
static void evhc_lst_step(const SimObject& o, const EvhcModel& V, EvhcUnit* U, int frame);
static void evhc_v2_launch_step(int frame);
static void evhc_v2_flight_step(int frame);

static void evhc_step(int frame)
{
    if (g_evhc.empty()) return;
    if (frame == g_evhcLastTick) return;
    if (frame < g_evhcLastTick) { g_evhcUnit.clear(); g_evhcWreck.clear(); }
    g_evhcLastTick = frame;
    const bool rule = evhc_rule_now();
    for (std::map<int, EvhcUnit>::iterator it = g_evhcUnit.begin(); it != g_evhcUnit.end(); ++it)
        it->second.seen = false;
    for (size_t i = 0; rule && i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        if (!evhc_kind(o.kind) || o.id < 0) continue;
        const int m = evhc_model_of(o.type);
        if (m < 0) continue;
        const EvhcModel& V = g_evhc[m];
        EvhcUnit* U = evhc_unit(o, m);
        U->seen = true;
        for (int s = 0; s < 2; s++) U->travP[s] = U->trav[s];
        U->turnP = U->turn;
        U->spinP = U->spin;
        U->ductP = U->duct;
        U->elevP = U->elev;
        U->doorP = U->door;
        U->gearP = U->gear;
        if (o.limbo) { U->suspension.initialized = false; U->have = false; U->tick = frame; continue; }
        /* TRAVEL, from the engine's own position: the forward distance along the hull's
           heading this tick, and the turn from the facing's change. Each side's travel is
           v - w g / 2 (left) and v + w g / 2 (right), w positive in a left turn, so turning
           on the spot runs the two sides opposite and reversing runs both backward. A jump
           of more than a cell is not travel (a unit put down somewhere new). */
        float v = 0.0f, w = 0.0f;
        if (U->have && o.face >= 0 && U->face >= 0) {
            const float dx = o.ex - U->ex, dz = o.ez - U->ez;
            if (dx * dx + dz * dz <= 1.0f) {
                const float a = (float)o.face * (2.0f * 3.14159265f / 256.0f);
                v = dx * sinf(a) - dz * cosf(a);      /* heading: sin a east, -cos a south */
                const int df = ((o.face - U->face + 128) & 255) - 128;
                w = -(float)df * (2.0f * 3.14159265f / 256.0f);
            }
        }
        if (!g_raModule && V.m.type == "HARV") {
            /* Read the axle locations from this installed model, not an older
               Harvester design or a guessed generic wheelbase. */
            float sn, cs; facing_rot((float)evhc_face(o), &sn, &cs);
            float mean = 0.0f; int contacts = 0;
            for (size_t k = 0; k < V.part.size(); k++) if (V.part[k].kind == EVP_WHEEL) {
                const float x = V.part[k].a[0] * MODEL_SCALE, z = V.part[k].a[2] * MODEL_SCALE;
                mean += terrain_y(o.ex + x * cs + z * sn, o.ez - x * sn + z * cs);
                contacts++;
            }
            mean = contacts ? mean / (float)contacts : terrain_y(o.ex, o.ez);
            const float dx = o.ex - U->ex, dz = o.ez - U->ez;
            const bool reset = !U->have || dx * dx + dz * dz > 1.0f;
            vehicle_suspension_step(U->suspension, v, w,
                (U->trav[0] + U->trav[1]) * 0.5f, mean - terrain_y(o.ex, o.ez), reset);
        }
        U->v = v;
        U->w = w;
        const float g = V.gauge;
        U->trav[1] += v - w * g * 0.5f;
        U->trav[0] += v + w * g * 0.5f;
        /* THE STEER, as the viewer's: (right - left) / 2v of full lock, eased so a one-tick
           facing step does not flick the wheels */
        float want = 0.0f;
        if (fabsf(w) > 1e-6f) {
            want = (fabsf(v) > 1e-4f) ? (w * g) / (2.0f * fabsf(v)) : (w > 0 ? 1.0f : -1.0f);
            if (want > 1.0f) want = 1.0f;
            if (want < -1.0f) want = -1.0f;
        }
        U->turn += (want - U->turn) * 0.35f;
        U->ex = o.ex; U->ez = o.ez;
        U->face = o.face;
        U->td = evhc_turret_delta(V, o);
        U->have = true;
        /* THE ROTORS: idling on the ground at the cartridge's landed rate (half, and a
           little under: the blades are seen there), up to speed as the craft lifts, over
           about a second each way; the blur disc comes up over the first third of the
           climb and goes as the craft settles. */
        if (V.rotors) {
            const float rw = (o.kind == K_AIRCRAFT && o.alt > 0) ? 1.0f : 0.35f;
            U->rate = ev_ease(U->rate, rw, 0.045f);
            U->spin += U->rate / 15.0f;
            float dw = o.kind == K_AIRCRAFT ? (float)o.alt / 8.0f : 0.0f;
            if (dw > 1.0f) dw = 1.0f;
            U->disc = ev_ease(U->disc, dw, 0.08f);
        } else {
            U->spin += 1.0f / 15.0f;
        }
        /* THE ORCA'S DUCTS: tilted forward with its speed at flight level (90 degrees at
           the engine's fast speed, 0.156 cells a tick), upright while it hovers, lands or
           takes off, eased at 90 degrees a second */
        {
            float want = 0.0f;
            if (o.kind == K_AIRCRAFT && o.alt >= 24) {
                const float sp = sqrtf(U->v * U->v) / 0.15625f;
                want = 90.0f * (sp > 1.0f ? 1.0f : sp);
            }
            U->duct = ev_ease(U->duct, want, 6.0f);
        }
        /* THE LAUNCHER: lowered to drive, raised to fire, at its modelled rest otherwise */
        {
            float want = 18.7f;
            if (o.driving || o.navset) want = 0.0f;
            else if (o.tar >= 0) want = 40.0f;
            U->elev = ev_ease(U->elev, want, 2.5f);
        }
        /* DOORS AND RAMPS */
        for (size_t k = 0; k < V.part.size(); k++) {
            const EvhcPart& p = V.part[k];
            if (p.kind != EVP_HINGE) continue;
            if (p.driver == EVD_APC_DOOR || p.driver == EVD_TRAN_RAMP || p.driver == EVD_LST_BOW) {
                const float step = p.driver == EVD_APC_DOOR ? 0.1f
                                 : p.driver == EVD_TRAN_RAMP ? 1.0f / 15.0f : 1.0f / 20.0f;
                U->door = ev_ease(U->door, evhc_door_want(V, o, p.driver), step);
                break;
            }
        }
        /* THE LANDING GEAR (Red Alert's aircraft that land): it follows the altitude,
           eased so that no step of the engine's moves it more than a sixth of its swing
           (a craft put down somewhere new swings its gear rather than snapping it) */
        if (V.gear && g_raModule) {
            U->gear = ev_ease(U->gear, evhc_gear_want(o), 1.0f / 6.0f);
            U->gearAlt = o.alt;
        }
        /* THE HARVESTER'S CLAWS: raised whenever it is not harvesting, lowered while it
           harvests, by the four tests the cartridge's own arm clip opens on (harvesting,
           the facing settled, no NavCom, not driving: object_anim_t's harvester gate), so
           the claws come down exactly when the clip starts to sweep them. One phase, 0
           lowered .. 1 raised, runs linearly over the change's seconds (the lift part's
           a7) and turns back wherever it stands if the harvest stops or starts again
           part way; the lift, the rams and the lower jaw are each a function of it
           (evhc_claw_lift, evhc_claw_jaw). A harvester first seen takes its pose at once.
           Docked, it is in limbo and keeps the pose it arrived in (raised). */
        if (V.liftPart >= 0) {
            const EvhcPart& lp = V.part[V.liftPart];
            const bool harv = o.harv && !o.rotating && !o.navset && !o.driving;
            const float want = harv ? 0.0f : 1.0f;
            if (!U->clawSet) { U->claw = want; U->clawSet = true; }
            U->clawP = U->claw;
            U->clawHarv = harv;
            const float sec = lp.a[7] > 0.05f ? lp.a[7] : 0.8f;
            U->claw = ev_ease(U->claw, want, 1.0f / (sec * 15.0f));
        }
        /* RED ALERT'S ORE TRUCK: the scoop and the dump body follow the frame of the 1996
           animation the engine is showing (the brain's doing= 1 while it lifts ore, 2 while
           it dumps, dostage= that animation's frame), each eased on the tick so a frame's
           step is a swing and not a jump. The part's own numbers (the baker's, from the
           export's frames) say which frames raise the scoop and lower it, and over which
           the body rises and then tips. Red Alert only: a Tiberian Dawn file carries none
           of these drivers and none is driven outside the module. */
        U->scoopP = U->scoop; U->liftP = U->lift; U->tipP = U->tip;
        U->erectP = U->erect;
        U->oreDoing = g_raModule ? o.doing : -1;
        U->oreFrame = g_raModule ? o.dostage : -1;
        for (size_t k = 0; g_raModule && k < V.part.size(); k++) {
            const EvhcPart& p = V.part[k];
            if (p.kind != EVP_HINGE) continue;
            const float f = (float)o.dostage;
            if (p.driver == EVD_ORE_SCOOP) {
                /* a8..a9 the frames it rises over, a10..a11 those it comes down over */
                float want = 0.0f;
                if (o.doing == 1) {
                    if (f <= p.a[9])
                        want = p.a[9] > p.a[8] ? (f - p.a[8]) / (p.a[9] - p.a[8]) : 1.0f;
                    else
                        want = p.a[11] > p.a[10] ? (p.a[11] - f) / (p.a[11] - p.a[10]) : 0.0f;
                    want = want < 0.0f ? 0.0f : want > 1.0f ? 1.0f : want;
                }
                U->scoop = ev_ease(U->scoop, want, 0.34f);
            } else if (p.driver == EVD_ORE_DUMP) {
                /* a8 the frame the rams are up by, a9..a10 the frames it tips over */
                float wl = 0.0f, wt = 0.0f;
                if (o.doing == 2) {
                    wl = p.a[8] > 0.0f ? f / p.a[8] : 1.0f;
                    wt = p.a[10] > p.a[9] ? (f - p.a[9]) / (p.a[10] - p.a[9]) : 0.0f;
                    wl = wl < 0.0f ? 0.0f : wl > 1.0f ? 1.0f : wl;
                    wt = wt < 0.0f ? 0.0f : wt > 1.0f ? 1.0f : wt;
                }
                U->lift = ev_ease(U->lift, wl, 0.25f);
                U->tip = ev_ease(U->tip, wt, 0.2f);
            } else if (p.driver == EVD_V2_ERECTOR) {
                /* RED ALERT'S V2 ROCKET, its firing: the launch section (the erector with the
                   rocket, the rams extending) pivots up to its firing lift while the engine
                   says the launcher stands to fire with a rocket on the rail (doing= 3, ammo=
                   not 0), the rocket leaves when the engine fires, and the empty section
                   pivots back down to rest while it reloads; the rocket is back on the rail
                   when the engine's reload puts it there (ammo= 1), not before.
                   THE RAISE IS DONE BY THE SHOT, and the shot never waits for it: dostage=
                   is the ticks before the engine will fire (its turn onto the target, or
                   with the rail empty the reload still to run), so the section rises over
                   eight ticks, faster when the engine will fire sooner, and is at the top on
                   the last tick before the shot. A V2 that keeps its target through the
                   reload raises the empty section in the reload's last eight ticks, so the
                   rocket is back on a raised rail and leaves at once, as the engine fires it
                   the moment it is loaded. Down over a second. Each move is drawn eased (the
                   pose). */
                const int shotIn = (o.doing == 3 && o.dostage > 0) ? o.dostage : 0;
                const bool loaded = o.ammo != 0;
                const float want = (o.doing == 3 && (loaded || shotIn <= EVHC_V2_RAISE))
                                   ? 1.0f : 0.0f;
                float rate = 1.0f / 15.0f;
                if (want > U->erect) {
                    const float need = (1.0f - U->erect) / (float)(shotIn > 0 ? shotIn : 1);
                    rate = need > 1.0f / (float)EVHC_V2_RAISE ? need : 1.0f / (float)EVHC_V2_RAISE;
                }
                U->erect = ev_ease(U->erect, want, rate);
                U->v2FireIn = o.doing == 3 ? o.dostage : -1;
            }
        }
        /* THE ORE TRUCK'S LOAD fills its bed as the engine's pips rise (7 pips, one per four
           bales: Pip_Count), eased so each pip is a heap growing over half a second, not a
           step; while it dumps the load pours out as the body tips, and the engine's pips
           go to 0 at the end of the dump. A brain or unit without pips says -1: full. */
        if (V.loadPart >= 0 && g_raModule) {
            float want = (o.maxpips > 0 && o.pips >= 0)
                       ? (float)(o.pips < o.maxpips ? o.pips : o.maxpips) / (float)o.maxpips : 1.0f;
            if (o.doing == 2) want *= 1.0f - U->tip;
            U->fillWant = want;
            if (!U->fillSet) { U->fill = want; U->fillSet = true; }
            U->fillP = U->fill;
            U->fill = ev_ease(U->fill, want, want < U->fill ? 1.0f / 12.0f : 1.0f / 45.0f);
        }
        if (g_raModule) evhc_lst_step(o, V, U, frame);
        /* THE LAUNCH, the tick the engine says the rail is empty (evhc_v2_launch) */
        if (g_raModule && U->v2Ammo > 0 && o.ammo == 0)
            evhc_v2_launch(o, m, U, frame);
        U->v2Doing = g_raModule ? o.doing : -1;
        U->v2Ammo = g_raModule ? o.ammo : -1;
        U->tick = frame;
    }
    /* units no longer in the dump: their entries go (a death has already taken what it
       needed from them, in the vanish sweep) */
    for (std::map<int, EvhcUnit>::iterator it = g_evhcUnit.begin(); it != g_evhcUnit.end(); ) {
        if (!it->second.seen) g_evhcUnit.erase(it++);
        else ++it;
    }
    /* THE SHOTS: a muzzle anim born this tick on a unit this system draws */
    static std::set<int> raShotsPrev;
    std::set<int> raShotsNow;
    for (size_t a = 0; rule && a < g_efxAnims.size(); a++) {
        const EfxAnim& an = g_efxAnims[a];
        if (an.attref < 0) continue;
        if (g_raModule) {
            /* RED ALERT numbers its anims its own way, so the recipe table (Tiberian Dawn's
               AnimType order), and the effects' bookkeeping keyed on it, cannot name them.
               Its two muzzle anims are known by name instead, the cannon's GUNFIRE
               (ANIM_MUZZLE_FLASH) and the machine gun's MINIGUN (ANIM_GUN_N..NE), the
               weapons' Anim= in its RULES.INI, and one is newborn when the last tick
               stepped here did not have it */
            const int gun = !strcmp(an.name, "GUNFIRE") ? 1 : !strcmp(an.name, "MINIGUN") ? 2 : 0;
            if (!gun) continue;
            const int key = (gun << 16) | (an.id & 0xFFFF);
            raShotsNow.insert(key);
            if (raShotsPrev.count(key)) continue;
        } else {
            const int r = efx_recipe_index(an.type);
            if (r < 0) continue;
            const char* nm = EFX_RECIPE[r].name;
            if (!nm || (strcmp(nm, "MUZZLE_FLASH") != 0 && strncmp(nm, "GUN_", 4) != 0)) continue;
            std::map<int, EfxLiveAnim>::const_iterator la =
                g_efxAnimLive.find((an.type << 16) | (an.id & 0xFFFF));
            if (la == g_efxAnimLive.end() || la->second.bornFrame != frame) continue;
        }
        const ObjKind k = efx_att_objkind(an.attkind);
        if (!evhc_kind(k)) continue;
        evhc_note_fire((int)k * 1000000 + an.attref, frame);
    }
    if (g_raModule) raShotsPrev.swap(raShotsNow);
    evhc_v2_launch_step(frame);
    evhc_wreck_step(frame);
    fflush(stdout);
}

/* A shot: the next barrel in turn kicks (a twin gun fires its barrels alternately, as the
   engine's two-shooters do, one shot and one muzzle anim each). */
static void evhc_note_fire(int key, int frame)
{
    std::map<int, EvhcUnit>::iterator it = g_evhcUnit.find(key);
    if (it == g_evhcUnit.end()) return;
    EvhcUnit& U = it->second;
    const EvhcModel& V = g_evhc[U.model];
    const int nb = V.nbarrel > 0 ? (V.nbarrel > 4 ? 4 : V.nbarrel) : 1;
    /* A TWIN GUN'S BARRELS TAKE TURNS, the left first. The engine does pick a side per
       shot (its second-shot flag, techno.cpp Fire_At), but the Mammoth's missiles flip the
       same flag in the same tick as the cannon, so the flag the tick leaves behind names no
       barrel: measured, it read the same after every cannon shot of a force-fire. The
       barrel that kicks is therefore ours, alternating per muzzle flash. */
    const int b = U.shots % nb;
    U.fire[b] = frame;
    U.shots++;
    printf("EVHC|fire|key=%d|type=%s|barrel=%d|frame=%d\n", key, U.type, b, frame);
}

/* ================================================================================
 * THE POSE. Every part's matrix, in pack units, inside the hull's (draw_mesh then turns
 * the lot with the facing and lifts it with the altitude). A child rides its parent's
 * matrix: an Orca's fan inside its tilting duct, the launcher's box on the turret.
 * ============================================================================== */
struct EvhcPose {
    float suspension[3];                     /* relative sprung body: heave, pitch, roll */
    float td;                                 /* fractional render turret delta, DirType */
    float trav[2], turn, spin, disc, duct, elev, door, t;
    float clipT;                              /* the cartridge clip's time, -1 for none */
    int   cartMi;
    bool  air;
    float scoop, lift, tip;                   /* Red Alert's Ore Truck, 0 .. 1 each */
    bool  load;                               /* it carries ore (its load part shows) */
    float fill;                               /* how full the bed is drawn, 0 .. 1 */
    float erect;                              /* Red Alert's V2 Rocket's erector, 0 .. 1 */
    float ramp[4];                            /* Red Alert's Transport's ramps, degrees */
    bool  rocket;                             /* the V2's rocket is on the rail */
    float claw;                               /* the Harvester's claws, 0 lowered .. 1
                                                 raised; below 0 they stand at rest */
    float gear;                               /* Red Alert's landing gear, 0 down .. 1
                                                 stowed (eased as drawn) */
    float hoop;                               /* the Harvester's hoop, 0 at rest over the
                                                 tank .. 1 swung over the cab (eased) */
};

/* THE KICK, the lab's curve (its recoil declaration): back to the full distance over
   `back` seconds, then home on (1 - u)^2 over `ret`; a machine gun's burst repeats it
   `count` times `every` seconds apart, and the furthest-back of them is what shows. The
   offset, in the part's own units, along the bore away from the muzzle. */
static float evhc_kick_curve(float t, float d, float back, float ret)
{
    if (t < 0.0f) return 0.0f;
    if (t < back) return back > 0.0f ? d * t / back : d;
    const float u = ret > 0.0f ? (t - back) / ret : 1.0f;
    if (u >= 1.0f) return 0.0f;
    return d * (1.0f - u) * (1.0f - u);
}
static float evhc_recoil(const EvhcUnit* U, const EvhcPart& p, float now)
{
    if (!U) return 0.0f;
    const int b = (int)p.a[8];
    if (b < 0 || b > 3) return 0.0f;
    const float t = (now - (float)U->fire[b]) / 15.0f;
    const int count = p.driver > 0 ? p.driver : 1;
    float off = 0.0f;
    for (int n = 0; n < count; n++) {
        const float k = evhc_kick_curve(t - (float)n * p.a[11], p.a[6], p.a[10], p.a[7]);
        if (k > off) off = k;
    }
    return off;
}

/* A TRANSPORT'S RAMP RESTS WHERE IT MEETS THE GROUND. Off a beach the surface a ramp swings
   down onto rises (the shallows and the sand, terrain_y), and the ramp's full swing would
   put its far end into it; it stops instead where its far edge first meets the surface,
   lying on the beach. The far edge is the ramp piece's corners farthest from its hinge
   line (the farthest one and the two ends of that edge along the hinge), swung in steps
   of two degrees; the raft as it will lie (the facing it is turning to, `face`), without
   its swell, so the rest does not tremble with the waves. At open sea the full swing
   clears the water and is kept. */
static float evhc_lst_rest(const SimObject& o, const EvhcModel& V, int pi, int face)
{
    const EvhcPart& p = V.part[pi];
    const float full = p.a[6];
    const EbldSet& S = V.m.set[EBS_INTACT];
    if (p.piece < 0 || p.piece >= (int)S.piece.size()) return full;
    const EbldPiece& Pc = S.piece[p.piece];
    const float* c = p.a;
    const float* k = p.a + 3;
    /* the far edge: the farthest corner from the hinge line, and of the corners nearly as
       far, the two lying farthest each way along the hinge */
    float rmax = 0.0f, far3[3][3];
    int have = 0;
    for (int pass = 0; pass < 2; pass++) {
        float lo = 1e9f, hi = -1e9f;
        for (int t = Pc.tri0; t < Pc.tri0 + Pc.ntri && t < (int)S.tri.size(); t++)
            for (int j = 0; j < 3; j++) {
                const EbldVert& v = S.tri[t].v[j];
                const float d[3] = { v.x - c[0], v.y - c[1], v.z - c[2] };
                const float al = d[0] * k[0] + d[1] * k[1] + d[2] * k[2];
                const float q[3] = { d[0] - al * k[0], d[1] - al * k[1], d[2] - al * k[2] };
                const float r = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
                if (pass == 0) {
                    if (r > rmax) {
                        rmax = r;
                        far3[0][0] = v.x; far3[0][1] = v.y; far3[0][2] = v.z;
                        have = 1;
                    }
                } else if (r >= 0.95f * rmax) {
                    if (al < lo) { lo = al; far3[1][0] = v.x; far3[1][1] = v.y; far3[1][2] = v.z; }
                    if (al > hi) { hi = al; far3[2][0] = v.x; far3[2][1] = v.y; far3[2][2] = v.z; }
                    have = 3;
                }
            }
    }
    if (!have) return full;
    float rs, rc;
    SimObject at = o;
    at.face = face;
    facing_rot(evhc_face(at), &rs, &rc);
    const float base = terrain_y(o.wx, o.wz) + alt_lift(o) + o.ylift;
    float rest = full;
    for (float ang = 60.0f; ang <= full; ang += 2.0f) {
        float M[12];
        ebld_pivot_matrix(c, k, ang * (3.14159265f / 180.0f), M);
        bool touch = false;
        for (int j = 0; j < have && !touch; j++) {
            const float* v = far3[j];
            const float lx = M[0] * v[0] + M[1] * v[1] + M[2] * v[2] + M[3];
            const float ly = M[4] * v[0] + M[5] * v[1] + M[6] * v[2] + M[7];
            const float lz = M[8] * v[0] + M[9] * v[1] + M[10] * v[2] + M[11];
            const float wx = o.wx + (lx * rc + lz * rs) * MODEL_SCALE;
            const float wz = o.wz + (-lx * rs + lz * rc) * MODEL_SCALE;
            const float wy = base + ly * MODEL_SCALE;
            if (wy < terrain_y(wx, wz)) touch = true;
        }
        if (touch) { rest = ang - 2.0f; break; }
    }
    return rest < full ? rest : full;
}

/* RED ALERT'S TRANSPORT, ITS RAMPS. The engine opens its door to take passengers aboard and
   to put them ashore (VesselClass::LST_Open_Door, Open_Door(5, 6): the door's stages 0 to
   4 five ticks apart, standing open at 4, the frame the 1996 art draws fully open) and
   closes it after; the brain's door= is -1 shut, 0 closing, 1 + the stage while it opens
   or stands open. The four ramps follow the 1996 frames 1 to 4 of that door, each ramp's
   angle fitted to each frame's outline: the far ramp first (the one facing away from the
   camera, north, when the door began to open), the two side ramps next, the near one
   last; standing open all four lie on the water. The ramps move through each stage over
   its five ticks rather than jumping at its start, and closing plays the table backwards
   on the engine's own five ticks a stage. Each ramp eases toward its angle at up to 24
   degrees a tick, and goes no farther than where it rests on the ground (evhc_lst_rest). */
static void evhc_lst_step(const SimObject& o, const EvhcModel& V, EvhcUnit* U, int frame)
{
    int idx[4], n = 0;
    for (size_t k = 0; k < V.part.size() && n < 4; k++)
        if (V.part[k].kind == EVP_HINGE && V.part[k].driver == EVD_RA_RAMP) idx[n++] = (int)k;
    for (int r = 0; r < 4; r++) U->rampP[r] = U->ramp[r];
    U->lstFace = evhc_face(o);
    if (!n) return;
    /* the 1996 frames 0 to 4, degrees open: far, sides, near (-1: the ramp's full swing,
       lying on the water) */
    static const float TABLE[5][3] = { { 0, 0, 0 }, { 50, 0, 0 }, { 100, 119, 0 },
                                       { 160, 167, 0 }, { -1, -1, -1 } };
    const int door = o.doorstage;
    if (door >= 1 && U->lstDoor < 1) {
        /* the far ramp: the one whose hinge will face most nearly north once the raft lies
           where it is turning to. The engine opens the door while the hull is still turning
           to the shore; for a vessel with no turret the brain's tface= is the facing its
           hull is turning to. */
        float best = -2.0f;
        int bi = 0;
        float rs, rc;
        SimObject at = o;
        if (o.tface >= 0) at.face = o.tface;
        facing_rot(evhc_face(at), &rs, &rc);
        for (int r = 0; r < n; r++) {
            const float* c = V.part[idx[r]].a;
            const float wz = -c[0] * rs + c[2] * rc;
            const float l = sqrtf(c[0] * c[0] + c[2] * c[2]);
            const float north = l > 1e-6f ? -wz / l : 0.0f;
            if (north > best) { best = north; bi = r; }
        }
        U->lstFar = bi;
    }
    if (door == 0 && U->lstDoor != 0) U->lstClose = frame;
    if (door >= 1 && door != U->lstDoor) U->lstStageAt = frame;
    U->lstDoor = door;
    float stage = 0.0f;
    if (door >= 1) {
        stage = (float)(door - 1);
        if (door < 5) stage += fminf(1.0f, (float)(frame - U->lstStageAt) / 5.0f);
    } else if (door == 0)
        stage = 4.0f - (float)(frame - U->lstClose) / 5.0f;
    if (stage < 0.0f) stage = 0.0f;
    if (stage > 4.0f) stage = 4.0f;
    const int s0 = (int)floorf(stage), s1 = s0 < 4 ? s0 + 1 : 4;
    const float f = stage - (float)s0;
    /* the near ramp is the one opposite the far one: the ramp whose hinge is farthest
       from the far one's */
    const float* fc = V.part[idx[U->lstFar]].a;
    int nearR = U->lstFar;
    float most = -1.0f;
    for (int r = 0; r < n; r++) {
        const float* c = V.part[idx[r]].a;
        const float d2 = (c[0] - fc[0]) * (c[0] - fc[0]) + (c[2] - fc[2]) * (c[2] - fc[2]);
        if (d2 > most) { most = d2; nearR = r; }
    }
    for (int r = 0; r < n; r++) {
        const float full = door == -1 ? 0.0f
                         : evhc_lst_rest(o, V, idx[r], o.tface >= 0 ? o.tface : o.face);
        const int col = r == U->lstFar ? 0 : r == nearR ? 2 : 1;
        const float a0 = TABLE[s0][col] < 0.0f ? full : fminf(TABLE[s0][col], full);
        const float a1 = TABLE[s1][col] < 0.0f ? full : fminf(TABLE[s1][col], full);
        float want = a0 + (a1 - a0) * f;
        if (door == -1) want = 0.0f;
        /* closing, a ramp only rises: the raft turning away as it closes moves where each
           ramp would rest, and a ramp opening farther while the door shuts reads wrong */
        if (door == 0 && want > U->ramp[r]) want = U->ramp[r];
        U->ramp[r] = ev_ease(U->ramp[r], want, 24.0f);
    }
}

/* A V2 ERECTOR RAM AT THE POSE'S LIFT (EVD_V2_RAM the cylinder, EVD_V2_ROD its rod and eye
   riding it): the point its eye is pinned to on the erector (a8..a10) turned with the
   erector about the erector's hinge, the cylinder turned about its own pin (a0..a2, axis
   a3..a5) onto that point, and the rod slid out along the ram by how much farther the point
   now is from the pin. The rod's matrix is its own slide; it rides the cylinder's turn. */
static void evhc_ram_follow(const EvhcPart& p, const EvhcPart& er, float lift, bool rod,
                            float* L);
static void evhc_v2_ram(const EvhcModel& V, const EvhcPart& p, const EvhcPose& P, float* L)
{
    ev_ident(L);
    const EvhcPart* er = NULL;
    for (size_t k = 0; k < V.part.size(); k++)
        if (V.part[k].kind == EVP_HINGE && V.part[k].driver == EVD_V2_ERECTOR) er = &V.part[k];
    if (!er) return;
    const float lift = (er->a[6] * P.erect - er->a[7]) * (3.14159265f / 180.0f);
    evhc_ram_follow(p, *er, lift, p.driver == EVD_V2_ROD, L);
}

/* A RAM THAT FOLLOWS A LIFT: the V2's erector rams and the Harvester's lift rams. `er` is
   the hinge it follows, `lift` its angle (radians); p's a8..a10 the point its eye is pinned
   to on the lifted part at rest, a0..a2 its own fixed pin, a3..a5 the axis it turns about.
   The cylinder (the barrel) turns about its pin onto where the point has gone; the rod,
   riding it, slides out along it by how much farther the point now is from the pin. */
static void evhc_ram_follow(const EvhcPart& p, const EvhcPart& er, float lift, bool rod,
                            float* L)
{
    ev_ident(L);
    float E[12];
    ebld_pivot_matrix(er.a, er.a + 3, lift, E);
    const float* eye0 = p.a + 8;
    float eye[3];
    ev_apply(E, eye0, eye);
    const float* pin = p.a;
    const float* k = p.a + 3;
    float d0[3], d1[3];
    for (int i = 0; i < 3; i++) { d0[i] = eye0[i] - pin[i]; d1[i] = eye[i] - pin[i]; }
    /* across the ram's axis: the turn is about it */
    const float k0 = d0[0] * k[0] + d0[1] * k[1] + d0[2] * k[2];
    const float k1 = d1[0] * k[0] + d1[1] * k[1] + d1[2] * k[2];
    float a0[3], a1[3];
    for (int i = 0; i < 3; i++) { a0[i] = d0[i] - k0 * k[i]; a1[i] = d1[i] - k1 * k[i]; }
    const float cx = a0[1] * a1[2] - a0[2] * a1[1];
    const float cy = a0[2] * a1[0] - a0[0] * a1[2];
    const float cz = a0[0] * a1[1] - a0[1] * a1[0];
    const float turn = atan2f(cx * k[0] + cy * k[1] + cz * k[2],
                              a0[0] * a1[0] + a0[1] * a1[1] + a0[2] * a1[2]);
    if (!rod) {
        ebld_pivot_matrix(pin, k, turn, L);
        return;
    }
    const float l0 = sqrtf(d0[0] * d0[0] + d0[1] * d0[1] + d0[2] * d0[2]);
    const float l1 = sqrtf(d1[0] * d1[0] + d1[1] * d1[1] + d1[2] * d1[2]);
    if (l0 < 1e-6f) return;
    const float slide = (l1 - l0) / l0;
    L[3] = d0[0] * slide; L[7] = d0[1] * slide; L[11] = d0[2] * slide;
}

/* THE HARVESTER'S CLAWS AT PHASE u (0 lowered .. 1 raised; below 0, at rest), degrees.
   The lift runs over the first `share` of the phase (its part's a8), eased, so raising it
   lifts over the first part of the change and lowering it comes down over the last part.
   The lower jaw (its own a8) is shut over the last share of the phase: shutting under the
   claws once they are well up when raising, open before they come down when lowering. One
   phase gives both schedules, and a change turned back part way runs back through the
   same poses. */
static float evhc_ease01(float x)
{
    x = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
    return x * x * (3.0f - 2.0f * x);
}
static float evhc_claw_lift(const EvhcPart& p, float u)
{
    if (u < 0.0f) return 0.0f;
    const float share = (p.a[8] > 0.0f && p.a[8] <= 1.0f) ? p.a[8] : 1.0f;
    return p.a[6] * evhc_ease01(u / share);
}
static float evhc_claw_jaw(const EvhcPart& p, float u)
{
    if (u < 0.0f) return 0.0f;
    const float share = (p.a[8] > 0.0f && p.a[8] <= 1.0f) ? p.a[8] : 1.0f;
    return p.a[6] * (1.0f - evhc_ease01((u - (1.0f - share)) / share));
}

static void evhc_part_local(const EvhcModel& V, const EvhcPart& p, const EvhcPose& P,
                            const EvhcUnit* U, float* L)
{
    ev_ident(L);
    const float* c = p.a;
    const float* ax = p.a + 3;
    switch (p.kind) {
    case EVP_TURRET: {
        /* (a6: the bearing a second turret's guns rest at, 256ths from the bow: turned that
           much less, so it points along the one turret facing, as the 1996 Cruiser draws
           both its turrets) */
        const float ang = -((float)P.td - p.a[6]) * (2.0f * 3.14159265f / 256.0f);
        ev_yaw_about(c[0], c[2], ang, L);
        break;
    }
    case EVP_SPIN: {
        double turns = (double)P.t / 15.0 * (double)p.a[6] / 60.0;
        turns -= floor(turns);
        const float ph = (float)(turns * 2.0 * 3.14159265358979);
        const float ang = (p.a[7] > 0.0f) ? -p.a[7] * (3.14159265f / 180.0f) * sinf(ph) : -ph;
        ebld_pivot_matrix(c, ax, ang, L);
        break;
    }
    case EVP_ROTOR: {
        double turns = (double)P.spin * (double)p.a[6] / 60.0;
        turns -= floor(turns);
        ebld_pivot_matrix(c, ax, -(float)(turns * 2.0 * 3.14159265358979), L);
        break;
    }
    case EVP_WHEEL: {
        const int side = p.side == 0 ? 0 : 1;
        const float r = p.a[6] > 1.0f ? p.a[6] * MODEL_SCALE : 0.05f;
        const float roll = P.trav[side] / r;
        float R1[12], R2[12];
        ebld_pivot_matrix(c, ax, roll, R1);
        if (p.a[7] > 0.0f) {
            const float up[3] = { 0.0f, 1.0f, 0.0f };
            ebld_pivot_matrix(c, up, p.a[7] * (3.14159265f / 180.0f) * P.turn, R2);
            ev_mul(R2, R1, L);
        } else {
            memcpy(L, R1, sizeof R1);
        }
        break;
    }
    case EVP_HINGE: {
        float ang = 0.0f;
        switch (p.driver) {
        case EVD_APC_DOOR: case EVD_TRAN_RAMP: case EVD_LST_BOW:
            ang = p.a[6] * P.door; break;
        case EVD_ELEVATION: ang = P.elev - p.a[7]; break;
        case EVD_DUCT:      ang = P.duct; break;
        case EVD_ORE_SCOOP: ang = g_raModule ? p.a[6] * P.scoop : 0.0f; break;
        case EVD_ORE_DUMP:  ang = g_raModule ? p.a[6] * P.tip : 0.0f; break;
        /* the V2's erector to its firing lift, eased (P.erect is drawn eased already) */
        case EVD_V2_ERECTOR: ang = g_raModule ? p.a[6] * P.erect - p.a[7] : 0.0f; break;
        case EVD_RA_RAMP: {
            /* the Transport's ramp, its angle by its order among the model's ramps */
            int r = 0;
            for (size_t k = 0; k < V.part.size() && &V.part[k] != &p; k++)
                if (V.part[k].driver == EVD_RA_RAMP) r++;
            ang = (g_raModule && r < 4) ? P.ramp[r] : 0.0f;
            break;
        }
        case EVD_V2_RAM: case EVD_V2_ROD:
            /* AN ERECTOR RAM FOLLOWS THE ERECTOR: its rod's eye stays on the point of the
               erector it is pinned to (a8..a10 at rest), so the cylinder turns about its
               own pin onto where that point has gone and the rod slides out by the
               distance it has grown; worked out at every lift, not interpolated */
            if (g_raModule) evhc_v2_ram(V, p, P, L);
            ang = 0.0f;
            break;
        /* RED ALERT'S LANDING GEAR: each leg and door swings its full stowing angle (a6)
           about its own hinge as the gear goes up, all together, eased at both ends */
        case EVD_GEAR: ang = g_raModule ? p.a[6] * evhc_ease01(P.gear) : 0.0f; break;
        /* THE HARVESTER: the claws' lift (its booms; the jaws ride it), the mouth's lower
           jaw, and the lift rams following the booms' pins as the V2's follow its erector */
        case EVD_HARV_LIFT: ang = evhc_claw_lift(p, P.claw); break;
        /* the hoop and its arms about the pins on the cab's rear shoulder corners, swung
           forward over the cab in the Refinery's dock (evhc_dock_draw, THE HOOP) */
        case EVD_HARV_HOOP: ang = p.a[6] * P.hoop; break;
        case EVD_HARV_JAW:  ang = evhc_claw_jaw(p, P.claw); break;
        /* THE ORE TRUCK'S LOAD, heaped as high as the bed is full: the modelled heap
           (floor to just over the rim) squashed toward the bed's floor, so the lumps and
           rocks keep their order and nothing goes under the floor or through a wall */
        case EVD_ORE_LOAD:
            if (g_raModule && P.fill < 1.0f) {
                const float k = P.fill > 0.0f ? P.fill : 0.0f;
                L[5] = k;
                L[7] = V.loadFloor * (1.0f - k);
            }
            ang = 0.0f;
            break;
        case EVD_HARV_RAM: case EVD_HARV_ROD:
            if (V.liftPart >= 0 && P.claw >= 0.0f)
                evhc_ram_follow(p, V.part[V.liftPart],
                                evhc_claw_lift(V.part[V.liftPart], P.claw) * (3.14159265f / 180.0f),
                                p.driver == EVD_HARV_ROD, L);
            ang = 0.0f;
            break;
        default:            ang = 0.0f; break;
        }
        if (ang != 0.0f) ebld_pivot_matrix(c, ax, ang * (3.14159265f / 180.0f), L);
        /* THE DUMP BODY RISES ON ITS RAMS FIRST (a7, pack units up), then tips about the
           hinge where it stands once risen: the rise goes in before the turn */
        if (p.driver == EVD_ORE_DUMP && g_raModule && P.lift > 0.0f) {
            const float up = p.a[7] * P.lift;
            L[3] += L[1] * up; L[7] += L[5] * up; L[11] += L[9] * up;
        }
        break;
    }
    case EVP_CLIP:
        if (P.clipT >= 0.0f) evhc_anim_mat(P.cartMi, (int)p.a[9], P.clipT, L, NULL);
        break;
    case EVP_RECOIL: {
        const float k = evhc_recoil(U, p, P.t);
        L[3] = -ax[0] * k; L[7] = -ax[1] * k; L[11] = -ax[2] * k;
        break;
    }
    default:
        break;
    }
    (void)V;
}

/* The wheels/tyres and their children retain the terrain contact pose. Everything
   above them, including the collector and its rams/lights, rides the sprung chassis. */
static bool evhc_contact_part(const EvhcModel& V, int k)
{
    for (; k >= 0; k = V.part[k].parent) {
        const int kind = V.part[k].kind;
        if (kind == EVP_WHEEL || kind == EVP_BELT || kind == EVP_WFLIP) return true;
    }
    return false;
}

/* Every part's matrix, parents first (the file orders them so). */
static void evhc_pose_all(const EvhcModel& V, const EvhcPose& P, const EvhcUnit* U,
                          std::vector<float>& mats)
{
    mats.assign(V.part.size() * 12, 0.0f);
    for (size_t k = 0; k < V.part.size(); k++) {
        float L[12];
        evhc_part_local(V, V.part[k], P, U, L);
        const int par = V.part[k].parent;
        if (par >= 0) ev_mul(&mats[par * 12], L, &mats[k * 12]);
        else memcpy(&mats[k * 12], L, sizeof L);
    }
    if (!g_raModule && V.m.type == "HARV") {
        /* An extra matrix is the static hull's pose. Keep the file and its part
           hierarchy intact; apply the body transform only after resolving parents. */
        float body[12], pitch[12], roll[12];
        float seat[3] = { 0, 0, 0 }; int contacts = 0;
        for (size_t k = 0; k < V.part.size(); k++) if (V.part[k].kind == EVP_WHEEL) {
            for (int c = 0; c < 3; c++) seat[c] += V.part[k].a[c];
            contacts++;
        }
        if (contacts) for (int c = 0; c < 3; c++) seat[c] /= (float)contacts;
        const float ax[3] = { 1, 0, 0 }, az[3] = { 0, 0, 1 };
        ebld_pivot_matrix(seat, ax, P.suspension[1], pitch);
        ebld_pivot_matrix(seat, az, P.suspension[2], roll);
        ev_mul(roll, pitch, body); body[7] += P.suspension[0] / MODEL_SCALE;
        for (size_t k = 0; k < V.part.size(); k++)
            if (!evhc_contact_part(V, (int)k)) ev_mul(body, &mats[k * 12], &mats[k * 12]);
        mats.insert(mats.end(), body, body + 12);
    }
}
static const float* evhc_piece_pose(const EvhcModel& V, const std::vector<float>& mats, int piece)
{
    const int pi = piece >= 0 && piece < (int)V.partOf.size() ? V.partOf[piece] : -1;
    if (pi >= 0) return &mats[pi * 12];
    return mats.size() > V.part.size() * 12 ? &mats[V.part.size() * 12] : NULL;
}

static void evhc_pose_of(const EvhcModel& V, const SimObject& o, const EvhcUnit* U,
                         EvhcPose& P)
{
    const float a = g_tickAlpha;
    memset(&P, 0, sizeof P);
    if (U && evhc_suspension_vehicle(o) && g_evhcHarvSuspOn && o.ylift <= 0.0f)
        for (int k = 0; k < 3; k++) P.suspension[k] = U->suspension.previous[k]
            + (U->suspension.value[k] - U->suspension.previous[k]) * a;
    /* Limit chassis compression at the model's actual axle mounts. This is a
       conservative .012-cell animation stop, not a change to authored geometry. */
    float longitudinal = 0.0f, lateral = 0.0f;
    for (size_t k = 0; k < V.part.size(); k++) if (V.part[k].kind == EVP_WHEEL) {
        longitudinal = fmaxf(longitudinal, fabsf(V.part[k].a[2]) * MODEL_SCALE);
        lateral = fmaxf(lateral, fabsf(V.part[k].a[0]) * MODEL_SCALE);
    }
    const float compression = -P.suspension[0] + fabsf(P.suspension[1]) * longitudinal
                               + fabsf(P.suspension[2]) * lateral;
    if (compression > 0.012f)
        for (int k = 0; k < 3; k++) P.suspension[k] *= 0.012f / compression;
    P.td = evhc_draw_turret_delta(V, o);
    P.t = engine_time();
    P.cartMi = mesh_for(o);
    P.clipT = (P.cartMi >= 0) ? object_anim_t(P.cartMi, o) : -1.0f;
    P.air = o.kind == K_AIRCRAFT && o.alt > 0;
    /* the ore load shows while the truck carries ore: the engine's pips are its load (a
       brain or a unit without them says -1, and the load shows) */
    P.load = !(g_raModule && o.maxpips > 0 && o.pips == 0);
    /* and the bed is drawn as full as it is (the tick's fill, eased; full without one) */
    P.fill = 1.0f;
    if (U && U->fillSet && V.loadPart >= 0 && g_raModule) {
        P.fill = U->fillP + (U->fill - U->fillP) * a;
        P.load = P.fill > 0.004f;
    }
    /* the V2's rocket is on the rail unless the engine says it carries none (ammo= 0, from
       the launch until it reloads; a brain without the field says -1, and it shows) */
    P.rocket = !(g_raModule && o.ammo == 0);
    P.claw = -1.0f;
    if (U && V.liftPart >= 0 && g_evhcHarvLiftOn)
        P.claw = U->clawP + (U->claw - U->clawP) * a;
    if (U) {
        for (int s = 0; s < 2; s++) P.trav[s] = U->travP[s] + (U->trav[s] - U->travP[s]) * a;
        P.turn = U->turnP + (U->turn - U->turnP) * a;
        P.spin = U->spinP + (U->spin - U->spinP) * a;
        P.disc = U->disc;
        P.duct = U->ductP + (U->duct - U->ductP) * a;
        P.elev = U->elevP + (U->elev - U->elevP) * a;
        P.door = U->doorP + (U->door - U->doorP) * a;
        P.scoop = U->scoopP + (U->scoop - U->scoopP) * a;
        P.lift = U->liftP + (U->lift - U->liftP) * a;
        P.tip = U->tipP + (U->tip - U->tipP) * a;
        for (int r = 0; r < 4; r++) P.ramp[r] = U->rampP[r] + (U->ramp[r] - U->rampP[r]) * a;
        /* eased: each move of the launch section starts and ends slowly */
        const float e = U->erectP + (U->erect - U->erectP) * a;
        P.erect = e * e * (3.0f - 2.0f * e);
        P.gear = U->gearP + (U->gear - U->gearP) * a;
    } else {
        P.gear = evhc_gear_want(o);
        P.elev = 18.7f;
        P.spin = P.t / 15.0f * 0.35f;
        P.disc = (o.kind == K_AIRCRAFT && o.alt > 0) ? 1.0f : 0.0f;
    }
}

/* ================================================================================
 * THE DRAW
 * ============================================================================== */
/* THE UV HOOK for one piece: a belt's scroll and a wheel flipbook's frame, both from the
   side's travel. The scroll is -(travel / pitch) mod 1 on the pack's v-down coordinate
   (the viewer adds +(travel / pitch) to its v-up one); the frame is floor(travel / loop *
   frames) mod frames of the grid x grid sheet, row-major from the top, as the remastered
   buildings' flipbooks are addressed. `frame` takes the frame for the dump. */
static void evhc_uv(const EvhcModel& V, const EvhcPart& p, int texIdx, const EvhcPose& P,
                    int* frame)
{
    const int side = p.side == 0 ? 0 : 1;
    const float trav = P.trav[side];
    if (p.kind == EVP_BELT) {
        const float pitch = p.a[6] * MODEL_SCALE;
        float s = pitch > 1e-5f ? trav / pitch : 0.0f;
        s -= floorf(s);
        g_meshUVScaleU = g_meshUVScaleV = 1.0f;
        g_meshUVOffU = 0.0f;
        g_meshUVOffV = -s;
        return;
    }
    if (p.kind == EVP_WFLIP && texIdx >= 0 && texIdx < (int)V.m.tex.size()) {
        const EbldTex& T = V.m.tex[texIdx];
        const float loop = p.a[6] * MODEL_SCALE;
        const int n = T.frames > 0 ? T.frames : 1;
        int f = loop > 1e-5f ? (int)floorf(trav / loop * (float)n) : 0;
        f %= n;
        if (f < 0) f += n;
        if (frame) *frame = f;
        const int grid = T.grid > 0 ? T.grid : 1;
        g_meshUVScaleU = g_meshUVScaleV = 1.0f / (float)grid;
        g_meshUVOffU = (float)(f % grid) / (float)grid;
        g_meshUVOffV = (float)(f / grid) / (float)grid;
    }
}

/* One piece of a model at a matrix, through draw_mesh with every argument the cartridge
   model would have been given. */
struct EvhcDraw {
    int mi, pass, house, wob;
    float x, z, lift, face;
    void run(int a, int b, const float* Mx) const
    {
        if (b <= a) return;
        draw_mesh(mi, x, z, face, pass, 0, 0.0f, house, 1.0f, lift, false, wob, -1.0f,
                  Mx, (size_t)a, (size_t)b);
    }
};

/* Whether a piece shows now: an aircraft's blades on the ground and its blur disc in the
   air (the disc's opacity is set by the caller). */
static bool evhc_piece_shows(const EvhcModel& V, int pi, const EvhcPose& P)
{
    if (pi < 0) return true;
    const EvhcPart& p = V.part[pi];
    /* the Ore Truck's load, shown only while it carries ore (Red Alert's pips) */
    if (p.kind == EVP_HINGE && p.driver == EVD_ORE_LOAD) return P.load;
    /* the V2's rocket, shown while it is loaded (Red Alert's ammo) */
    if (p.kind == EVP_HINGE && p.driver == EVD_V2_ROCKET) return P.rocket;
    if (p.kind != EVP_ROTOR) return true;
    if (p.flags & EVF_DISC) return P.disc > 0.01f;
    /* The authored Orca adds a translucent streak disc OVER its fan assembly.
       Its texture leaves the spinner clear, and the part also contains that hub.
       Removing the part makes both the hub and the blade underlay disappear.
       Keep the authored underlay; the larger helicopter rotors retain their
       existing blades-to-disc transition. No texture or geometry changes. */
    if (V.m.type == "ORCA") return true;
    return !(V.discs && P.disc >= 0.5f);
}

/* THE STANDING VEHICLE, asked by draw_object_mesh after the authored model and before the
   cartridge's. True: this system has answered for the object in this pass. The riders on
   a hovercraft's deck are asked the same (their lift is the deck's). */
/* A SUBMARINE'S DIVE. Red Alert's submarine submerges as the engine cloaks it
   (TechnoClass::Cloak: 1 CLOAKING, 2 CLOAKED, 3 UNCLOAKING, its device's stage running to
   MAX_UNCLOAK_STAGE, 38): instead of fading as the 1996 sprite does, the model goes down
   through the sea, awash at a third of the way, at periscope depth at two thirds, fully under
   when cloaked, and comes back up the same way as it uncloaks (to fire, or hit). Cells under
   the sea; 0 for every model without the dive. A fully dived submarine that is not the
   player's is not drawn at all (visible: cloak_alpha); the player's own is drawn under, seen
   faintly through the water drawn over it. */
static long g_evhcGhosts = 0;                /* dived submarines drawn faint (the dump) */
static float evhc_dive(const EvhcModel& V, const SimObject& o)
{
    if (V.m.param.size() < (size_t)EVQ_N_DIVE || o.cloak == 0) return 0.0f;
    const float* q = &V.m.param[EVQ_DIVE_AWASH];
    float u = (float)o.cstage / 38.0f;
    u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
    if (o.cloak == 2) u = 1.0f;
    else if (o.cloak == 3) u = 1.0f - u;           /* (the stage counts back toward seen) */
    const float x = u * 3.0f;
    if (x <= 1.0f) return q[0] * x;
    if (x <= 2.0f) return q[0] + (q[1] - q[0]) * (x - 1.0f);
    return q[1] + (q[2] - q[1]) * (x - 2.0f);
}

static bool evhc_draw_object(const SimObject& o, int pass)
{
    const int m = evhc_for(o);
    if (m < 0) return false;
    EvhcModel& V = g_evhc[m];
    /* no ground-shadow triangles: a vehicle's plate is the cartridge's own quad, drawn by
       type elsewhere, whatever model stands over it */
    if (pass == MODE_SHADOW) return true;
    if (!evhc_resident(V, false)) return false;
    /* off this pass's view in a pass that only draws (aux_object_outside); room above for
       an aircraft's flight and a rider's lift */
    if (aux_object_outside(o.wx, o.wz, 4.0f, 16.0f)) return true;
    /* A PASS THE MODEL HAS NO TRIANGLE OF (most have no cut-out, many nothing
       translucent): answered with nothing, before the pose is worked out, which every
       piece's draw would then only have thrown away */
    if (pass != MODE_OPAQUE && pass >= 0 && pass < 32
        && !(g_pack.mesh[V.m.mesh[EBM_INTACT]].modeMask & (1u << pass)))
        return true;
    const EbldModel& M = V.m;
    const EbldSet& S = M.set[EBS_INTACT];
    const std::vector<int>& rg = M.rng[EBM_INTACT];
    std::map<int, EvhcUnit>::iterator ui = g_evhcUnit.find(smooth_key(o));
    EvhcUnit* U = (ui != g_evhcUnit.end() && ui->second.model == m) ? &ui->second : NULL;
    if (o.ylift > 0.0f) U = NULL;               /* a rider's copy: posed at rest */
    EvhcPose P;
    evhc_pose_of(V, o, U, P);
    std::vector<float> mats;
    evhc_pose_all(V, P, U, mats);
    EvhcDraw D;
    D.mi = M.mesh[EBM_INTACT]; D.face = evhc_draw_face(o); D.pass = pass; D.house = mesh_house(o);
    D.wob = wobble_of(o); D.x = o.wx; D.z = o.wz; D.lift = alt_lift(o) + o.ylift;
    /* (a submarine diving or surfacing: lowered through the sea) */
    const float dive = evhc_dive(V, o);
    D.lift -= dive;
    const bool colour = (pass == MODE_OPAQUE || pass == MODE_CUTOUT || pass == MODE_XLU)
                        && !ebld_aux_pass();
    if (pass == MODE_OPAQUE && colour) {
        V.drawn++;
        if (U) { U->drawnNow = true; U->dDive = dive; U->dCloak = o.cloak; U->dCstage = o.cstage; }
    }
    if (pass == MODE_OPAQUE && colour) g_meshWobX = g_meshWobZ = -9.0f;
    g_meshFlash = g_flashOn && o.blush != 0;
    int dframe[2] = { -1, -1 };
    /* THE NORMAL TARGET'S OPAQUE PASS reads no texture and no alpha, so the pieces that
       stand where the model put them (no matrix: the hull, a turret at rest, the belts and
       wheel flipbooks, whose motion is only in their texture) go down as one draw where
       their runs meet; each draw costs more than its triangles here. */
    const bool merge = g_evhcNrmPass && pass == MODE_OPAQUE;
    int ma = -1, mb = -1;
    for (size_t k = 0; k < S.piece.size(); k++) {
        const int* r = &rg[k * EBR];
        const int r0 = r[EBR_A], r1 = r[EBR_B];
        if (r1 <= r0) continue;
        const int pi = V.partOf[k];
        if (!evhc_piece_shows(V, pi, P)) continue;
        const float* Mx = evhc_piece_pose(V, mats, (int)k);
        if (Mx && ev_is_ident(Mx)) Mx = NULL;
        if (merge && !Mx) {
            if (ma >= 0 && r0 == mb) { mb = r1; continue; }
            if (ma >= 0) D.run(ma, mb, NULL);
            ma = r0; mb = r1;
            continue;
        }
        bool hook = false;
        unsigned char alpha = 255;
        if (pi >= 0) {
            const EvhcPart& p = V.part[pi];
            if (p.kind == EVP_BELT || p.kind == EVP_WFLIP) {
                int f = -1;
                evhc_uv(V, p, r[EBR_TEX], P, &f);
                if (f >= 0) dframe[p.side == 0 ? 0 : 1] = f;
                hook = true;
            }
            if (p.kind == EVP_ROTOR && (p.flags & EVF_DISC)) {
                float d = P.disc;
                if (d > 1.0f) d = 1.0f;
                alpha = (unsigned char)(255.0f * d + 0.5f);
                if (!colour && alpha < 128) continue;
            }
        }
        g_meshAlphaScale = alpha;
        D.run(r0, r1, Mx);
        g_meshAlphaScale = 255;
        if (hook) ebld_uv_clear();
    }
    if (ma >= 0) D.run(ma, mb, NULL);
    g_meshFlash = false;
    if (U && pass == MODE_OPAQUE && colour) {
        memcpy(U->dSuspension, P.suspension, sizeof U->dSuspension);
        U->dTur = P.td;
        U->dWob[0] = g_meshWobX; U->dWob[1] = g_meshWobZ;
        U->dTrav[0] = P.trav[0]; U->dTrav[1] = P.trav[1];
        U->dFrame[0] = dframe[0]; U->dFrame[1] = dframe[1];
        U->dTurn = P.turn;
        U->dRotor = P.spin;
        U->dDisc = P.disc;
        U->dDuct = P.duct; U->dElev = P.elev; U->dDoor = P.door;
        U->dClip = P.clipT;
        U->dErect = P.erect; U->dRocket = P.rocket;
        U->dClaw = P.claw;
        U->dGear = P.gear;
        float kick = 0.0f;
        for (size_t k = 0; k < V.part.size(); k++) {
            if (V.part[k].kind != EVP_RECOIL || V.part[k].a[6] <= 0.0f) continue;
            const float f = evhc_recoil(U, V.part[k], P.t) / V.part[k].a[6];
            if (f > kick) kick = f;
        }
        U->dKick = kick;
    }
    return true;
}

/* THE PLAYER'S OWN SUBMARINE FULLY UNDER, seen faintly through the sea: the sea drawn over
   it hides it (it writes depth), so it is drawn again once the world is down, its colour
   blended faintly in the sea's blue over the water, whatever the depths. Another house's is not drawn at all
   (visible). Enhanced only, as the models are. Called in the frame's fading micro-pass
   (blend on, depth test on, depth write off), and leaves that state as it found it. */
/* glBlendColor is GL_VERSION_1_2 (ARB_imaging, EXT_blend_color before it): the mingw GL/gl.h
   the Windows build compiles against is frozen at 1.1 and declares neither it nor the
   constant-alpha factors, so it is looked up at run time, once; a driver without it draws
   no faint submarine (the sea hides it, as before) and says so. */
#ifndef GL_CONSTANT_ALPHA
#define GL_CONSTANT_ALPHA 0x8003
#endif
#ifndef GL_ONE_MINUS_CONSTANT_ALPHA
#define GL_ONE_MINUS_CONSTANT_ALPHA 0x8004
#endif
typedef void (APIENTRY *EVHCPFNBLENDCOLOR)(GLfloat, GLfloat, GLfloat, GLfloat);
static EVHCPFNBLENDCOLOR evhc_glBlendColor(void)
{
    static int tried = 0;
    static EVHCPFNBLENDCOLOR fn = 0;
    if (!tried) {
        tried = 1;
        void* q = SDL_GL_GetProcAddress("glBlendColor");
        if (!q) q = SDL_GL_GetProcAddress("glBlendColorEXT");
        fn = (EVHCPFNBLENDCOLOR)q;
        if (!fn)
            fprintf(stderr, "EVHC|dive|glBlendColor is not exported by this driver; a dived "
                            "submarine of the player's is not drawn faint through the sea\n");
    }
    return fn;
}
static void evhc_draw_dived_ghosts(void)
{
    bool any = false;
    for (size_t i = 0; i < g_objects.size() && !any; i++) {
        const SimObject& o = g_objects[i];
        if (o.cloak != 2 || !g_playerHouse[0] || strcmp(o.house, g_playerHouse)) continue;
        const int m = evhc_for(o);
        any = m >= 0 && g_evhc[m].m.param.size() >= (size_t)EVQ_N_DIVE;
    }
    EVHCPFNBLENDCOLOR blendColor = any ? evhc_glBlendColor() : 0;
    if (!any || !blendColor) return;
    struct Each {
        void operator()(int pass) const
        {
            for (size_t i = 0; i < g_objects.size(); i++) {
                const SimObject& o = g_objects[i];
                if (o.cloak != 2 || !g_playerHouse[0] || strcmp(o.house, g_playerHouse)
                    || o.limbo || !evhc_ra_ready(o)) continue;
                const int m = evhc_for(o);
                if (m < 0 || g_evhc[m].m.param.size() < (size_t)EVQ_N_DIVE) continue;
                evhc_draw_object(o, pass);
                if (pass == MODE_OPAQUE) g_evhcGhosts++;
            }
        }
    } each;
    /* (no depth of its own: the frame's later passes read the depth the sea left, and a
       submarine's depth written there would shade the water over it; drawn over the sea
       whatever its depth, it shows through it) */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    /* (a constant share of the model over the sea, whatever alpha the draw's own path
       writes: the model's pixels at 18%) */
    blendColor(0.0f, 0.0f, 0.0f, 0.18f);
    glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
    g_meshTintR = 150; g_meshTintG = 195; g_meshTintB = 220;
    glEnable(GL_TEXTURE_2D);
    each(MODE_OPAQUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    g_meshTintR = g_meshTintG = g_meshTintB = 255;
    glDepthFunc(GL_LEQUAL);
    /* (back to the fading micro-pass's own state: blended, depth tested, no depth written) */
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}


/* ================================================================================
 * THE RIGS. The refinery's dock and the MCV's deploy are the cartridge's own clips on a
 * model of their own, drawn at the building's anchor; each carries the cartridge vehicle
 * inside it. Under the rule the remastered vehicle is drawn there instead, each piece
 * moved by the rig part that moves the same thing, and only the rest of the rig (the
 * Construction Yard rising round the MCV) is drawn from the cartridge.
 * ============================================================================== */
static int procrig_for(const SimObject& o, float* animT);
static int mcvrig_for(const SimObject& o, float* animT);
static int draw_facing(const SimObject& o);

/* The docked harvester, drawn for the refinery `o` while its dock rig plays. True: drawn
   here, and the rig's own harvester must not be. */
static bool evhc_dock_draw(const SimObject& o, int pass)
{
    if (o.kind != K_BUILDING || strcmp(o.type, "PROC") != 0 || !evhc_rule_now()) return false;
    const int m = evhc_model_of("HARV");
    if (m < 0) return false;
    EvhcModel& V = g_evhc[m];
    if (!V.rigOk) return false;
    float rigT = -1.0f;
    const int rig = procrig_for(o, &rigT);
    if (rig < 0) return false;
    if (pass == MODE_SHADOW) return true;
    if (!evhc_resident(V, false)) return false;
    const EbldSet& S = V.m.set[EBS_INTACT];
    const std::vector<int>& rg = V.m.rng[EBM_INTACT];
    float Dk[12];
    evhc_dock_matrix(V, Dk);
    EvhcPose P;
    memset(&P, 0, sizeof P);
    P.t = engine_time();
    P.clipT = -1.0f;
    P.cartMi = -1;
    /* THE CLAWS STAY AS THEY ARRIVE: raised (a harvester drives in, it does not harvest
       there), with the rams drawn in and the mouth's lower jaw shut */
    P.claw = (V.liftPart >= 0 && g_evhcHarvLiftOn) ? 1.0f : -1.0f;
    /* THE HOOP SWINGS BEFORE THE TANK TIPS, as in the 1995 Refinery's docking frames (12 and
       13: the hoop over the cab, then the tank up into the hall). The arm starts the rig at
       frame 10 (the refinery's stage 12, the first of the docking) and walks it to 80 by
       stage 19; it walks it back down on the way out (stage 24 at frame 50 to stage 28 at
       0). Over frames 10 to 20 the hoop swings forward (eased) with the tank held at its
       rest; over 20 to 80 the tank plays the cartridge's whole unload, its 0 to 80. On the
       way out the same frames run backward: the tank is down by frame 20 and the hoop swings
       back over 20 to 10. A model with no hoop plays the rig as it always did. */
    float tankT = rigT;
    if (V.hoopPart >= 0) {
        P.hoop = evhc_ease01((rigT - 10.0f) / 10.0f);
        tankT = rigT <= 20.0f ? 0.0f : (rigT - 20.0f) * (80.0f / 60.0f);
    }
    std::vector<float> mats;
    evhc_pose_all(V, P, NULL, mats);
    if (pass == MODE_OPAQUE && !ebld_aux_pass()) {
        V.rigDrawn++;
        V.rigLift = V.liftPart >= 0 ? evhc_claw_lift(V.part[V.liftPart], P.claw) : -1.0f;
        if (V.hoopPart >= 0) {
            V.rigHoop = P.hoop;
            V.rigTank = tankT;
            if (P.hoop > 0.0f && P.hoop < 1.0f) V.rigHoopMid++;
            if (tankT > 0.0f) { if (P.hoop >= 1.0f) V.rigHoopFull++; else V.rigHoopBad++; }
        }
    }
    for (size_t k = 0; k < S.piece.size(); k++) {
        const int* r = &rg[k * EBR];
        if (r[EBR_B] <= r[EBR_A]) continue;
        const int pi = V.partOf[k];
        const int rp = (k < V.rigPart.size() && V.rigPart[k] >= 0) ? V.rigPart[k] : 0;
        float A[12], T1[12], T2[12];
        bool vis = true;
        const bool tank = pi >= 0 && V.part[pi].kind == EVP_HINGE && V.part[pi].driver == EVD_HARV_HOPPER;
        evhc_anim_mat(rig, rp, tank ? tankT : rigT, A, &vis);
        if (!vis) continue;
        ev_mul(A, Dk, T1);
        /* the rig moves the hopper itself (its own part) and the arms that have no lift to
           ride (the rig's arm parts); every other part takes its pose inside the hull, the
           claws' lift and its rams and jaws included, and the hoop's swing */
        const bool rigs = pi >= 0 && ((V.part[pi].kind == EVP_CLIP && V.part[pi].parent < 0) || tank);
        if (pi >= 0 && !rigs)
            ev_mul(T1, &mats[pi * 12], T2);
        else
            memcpy(T2, T1, sizeof T1);
        draw_mesh(V.m.mesh[EBM_INTACT], o.wx, o.wz, -1, pass, 0, 0.0f, mesh_house(o), 1.0f,
                  0.0f, false, WOBBLE_NONE, -1.0f, T2, (size_t)r[EBR_A], (size_t)r[EBR_B]);
    }
    return true;
}

/* The MCV unfolding, drawn for the Construction Yard `o` while its deploy rig plays. True:
   the MCV was drawn here, and the rig's own MCV parts (1 to 5) must not be. */
static bool evhc_deploy_draw(const SimObject& o, int pass, int rig, float rigT)
{
    if (o.kind != K_BUILDING || strcmp(o.type, "FACT") != 0 || !evhc_rule_now()) return false;
    const int m = evhc_model_of("MCV");
    if (m < 0 || rig < 0) return false;
    EvhcModel& V = g_evhc[m];
    if (!V.rigOk) return false;
    if (pass == MODE_SHADOW) return true;
    if (!evhc_resident(V, false)) return false;
    const EbldSet& S = V.m.set[EBS_INTACT];
    const std::vector<int>& rg = V.m.rng[EBM_INTACT];
    if (pass == MODE_OPAQUE && !ebld_aux_pass()) V.rigDrawn++;
    float F[12];
    ev_yaw_about(0.0f, 0.0f, V.rigYaw, F);
    F[3] += V.rigX;
    F[11] += V.rigZ;
    for (size_t k = 0; k < S.piece.size(); k++) {
        const int* r = &rg[k * EBR];
        if (r[EBR_B] <= r[EBR_A]) continue;
        const int rp = (k < V.rigPart.size()) ? V.rigPart[k] : -1;
        if (rp < 0) continue;
        float A0[12], A0i[12], A[12], T1[12], T2[12];
        bool vis = true;
        evhc_anim_mat(rig, rp, rigT, A, &vis);
        if (!vis) continue;
        /* the rig's matrices are deltas from ITS first frame, which is where the fit put
           the remaster: frame 0's own delta is taken back out */
        evhc_anim_mat(rig, rp, 0.0f, A0, NULL);
        {
            /* the inverse of a rigid 3x4 (a scale in it is kept: it is the rig's own) */
            float R[9] = { A0[0], A0[1], A0[2], A0[4], A0[5], A0[6], A0[8], A0[9], A0[10] };
            const float det = R[0] * (R[4] * R[8] - R[5] * R[7]) - R[1] * (R[3] * R[8] - R[5] * R[6])
                            + R[2] * (R[3] * R[7] - R[4] * R[6]);
            if (fabsf(det) < 1e-8f) { ev_ident(A0i); }
            else {
                const float id = 1.0f / det;
                float I[9];
                I[0] = (R[4] * R[8] - R[5] * R[7]) * id; I[1] = (R[2] * R[7] - R[1] * R[8]) * id;
                I[2] = (R[1] * R[5] - R[2] * R[4]) * id; I[3] = (R[5] * R[6] - R[3] * R[8]) * id;
                I[4] = (R[0] * R[8] - R[2] * R[6]) * id; I[5] = (R[2] * R[3] - R[0] * R[5]) * id;
                I[6] = (R[3] * R[7] - R[4] * R[6]) * id; I[7] = (R[1] * R[6] - R[0] * R[7]) * id;
                I[8] = (R[0] * R[4] - R[1] * R[3]) * id;
                for (int i = 0; i < 3; i++) {
                    for (int j = 0; j < 3; j++) A0i[i * 4 + j] = I[i * 3 + j];
                    A0i[i * 4 + 3] = -(I[i * 3] * A0[3] + I[i * 3 + 1] * A0[7] + I[i * 3 + 2] * A0[11]);
                }
            }
        }
        ev_mul(A, A0i, T1);
        ev_mul(T1, F, T2);
        draw_mesh(V.m.mesh[EBM_INTACT], o.wx, o.wz, draw_facing(o), pass, 0, 0.0f,
                  mesh_house(o), 1.0f, 0.0f, false, WOBBLE_NONE, -1.0f, T2, (size_t)r[EBR_A],
                  (size_t)r[EBR_B]);
    }
    return true;
}

/* ================================================================================
 * THE DEATH. The vanish sweep in refresh_objects decides a unit died (a newborn explosion
 * on the cell it left); under the rule, for a type with a model, this system takes the
 * death instead of the old unit shatter: the wreck's pieces start where the intact pieces
 * stood (turned as they were, a turret at its aim), with the unit's own motion.
 *
 * A VEHICLE is thrown apart at once: outward from its centre and up, spinning.
 * AN AIRCRAFT falls whole from where it flew, keeping its forward speed and turning (a
 * helicopter spins down), under the engine's own debris gravity, until any piece touches
 * the ground; there it breaks, the pieces thrown from the impact with the craft's speed,
 * with fire and smoke where it came down and a jolt.
 * Then, as the remastered buildings' wreck: they settle on the terrain, lie a few seconds,
 * sink as they fade, and are gone.
 * ============================================================================== */
struct EvhcDeathIn {
    char  type[16];
    int   kind, id, house, face;
    float ex, ez, vx, vz, lift;
};

static bool evhc_claims_death(const char* type, int kind)
{
    if (!evhc_kind(kind)) return false;
    if (!evhc_rule_now()) return false;
    const int m = evhc_model_of(type);
    return m >= 0 && g_evhc[m].m.nset == 2;
}

/* The world rotation (quaternion w, x, y, z) of a yaw by draw_mesh's facing. */
static void ev_q_yaw(float ang, float* q)
{
    q[0] = cosf(ang * 0.5f); q[1] = 0.0f; q[2] = sinf(ang * 0.5f); q[3] = 0.0f;
}
static void ev_q_mul(const float* a, const float* b, float* o)
{
    const float r[4] = {
        a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3],
        a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2],
        a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1],
        a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0] };
    memcpy(o, r, sizeof r);
}
/* A rotation matrix (the 3x3 of a 3x4) as a quaternion. */
static void ev_q_of(const float* M, float* q)
{
    const float m00 = M[0], m01 = M[1], m02 = M[2], m10 = M[4], m11 = M[5], m12 = M[6];
    const float m20 = M[8], m21 = M[9], m22 = M[10];
    const float tr = m00 + m11 + m22;
    if (tr > 0.0f) {
        const float s = sqrtf(tr + 1.0f) * 2.0f;
        q[0] = 0.25f * s; q[1] = (m21 - m12) / s; q[2] = (m02 - m20) / s; q[3] = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
        q[0] = (m21 - m12) / s; q[1] = 0.25f * s; q[2] = (m01 + m10) / s; q[3] = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
        q[0] = (m02 - m20) / s; q[1] = (m01 + m10) / s; q[2] = 0.25f * s; q[3] = (m12 + m21) / s;
    } else {
        const float s = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
        q[0] = (m10 - m01) / s; q[1] = (m02 + m20) / s; q[2] = (m12 + m21) / s; q[3] = 0.25f * s;
    }
    eb_qnorm(q);
}

/* A wreck piece's launch: up and out as the lab's blast for its role gives them, and a
   spin, from two hashes. */
static void evhc_blast(const EvhcModel& V, size_t i, unsigned h1, unsigned h2, float* up,
                       float* out, float* spin)
{
    float b[5] = { 0.9f, 1.5f, 0.3f, 0.7f, 3.0f };
    if (V.blast.size() >= (i + 1) * 5)
        for (int c = 0; c < 5; c++) b[c] = V.blast[i * 5 + c];
    *up = b[0] + (b[1] - b[0]) * eb_r(h1, 1);
    *out = b[2] + (b[3] - b[2]) * eb_r(h1, 0);
    *spin = b[4];
    (void)h2;
}

static void evhc_note_death(const EvhcDeathIn& d, int key, int frame)
{
    const int m = evhc_model_of(d.type);
    if (m < 0) return;
    const EvhcModel& V = g_evhc[m];
    const EbldSet& W = V.m.set[EBS_DESTROYED];
    if (W.piece.empty()) return;
    /* the pose it died in: last tick's motion state, if it had one */
    std::map<int, EvhcUnit>::iterator ui = g_evhcUnit.find(key);
    const EvhcUnit* U = (ui != g_evhcUnit.end() && ui->second.model == m) ? &ui->second : NULL;
    SimObject o;
    memset(&o, 0, sizeof o);
    o.kind = (ObjKind)d.kind;
    snprintf(o.type, sizeof o.type, "%s", d.type);
    o.face = U ? U->face : -1;
    o.tface = -1;
    o.alt = d.lift > 0.01f ? 24 : 0;
    EvhcPose P;
    evhc_pose_of(V, o, U, P);
    P.td = U ? U->td : 0;
    std::vector<float> mats;
    evhc_pose_all(V, P, U, mats);
    const float ground = terrain_y(d.ex, d.ez);
    /* the hull's facing as this system drew it (the sweep's is the cartridge draw's, which
       turns a turretless rocket launcher's whole body to its launcher) */
    int face = d.face;
    if (U && (d.kind == K_UNIT || d.kind == K_VESSEL) && U->face >= 0)
        face = (U->face + (g_nofacefix ? (((int)g_faceBias) & 255) : model_face_bias(d.type))) & 255;
    const float fa = face >= 0 ? -(float)face * (2.0f * 3.14159265f / 256.0f) : 0.0f;
    float qf[4];
    ev_q_yaw(fa, qf);
    const float cf = cosf(fa), sf = sinf(fa);
    const unsigned hk = (unsigned)key;
    EvhcWreck E;
    E.model = m; E.id = d.id; E.kind = d.kind; E.house = d.house;
    snprintf(E.type, sizeof E.type, "%s", d.type);
    E.deathTick = frame; E.impactTick = -1;
    E.air = d.lift > 0.05f;
    E.falling = E.air;
    E.sinking = E.dropped = false;
    E.wx = d.ex; E.wz = d.ez; E.ground = ground;
    E.wet = !E.air && evhc_cell_wet(E.wx, E.wz);
    E.body.resize(W.piece.size());
    E.off.assign(W.piece.size() * 3, 0.0f);
    E.qoff.assign(W.piece.size() * 4, 0.0f);
    memset(&E.carrier, 0, sizeof E.carrier);
    const float base[3] = { d.ex, ground + d.lift, d.ez };
    for (size_t i = 0; i < W.piece.size(); i++) {
        const EbldPiece& Pc = W.piece[i];
        const int par = Pc.parent;
        const int pi = (par >= 0 && par < (int)V.partOf.size()) ? V.partOf[par] : -1;
        float Mx[12];
        const float* posed = evhc_piece_pose(V, mats, par);
        if (posed) memcpy(Mx, posed, sizeof Mx); else ev_ident(Mx);
        float lc[3];
        ev_apply(Mx, Pc.c, lc);                  /* its centre, posed, pack units */
        /* into the world: the facing, the model scale, the anchor */
        float p[3];
        p[0] = base[0] + (lc[0] * cf + lc[2] * sf) * MODEL_SCALE;
        p[1] = base[1] + lc[1] * MODEL_SCALE;
        p[2] = base[2] + (-lc[0] * sf + lc[2] * cf) * MODEL_SCALE;
        float qp[4], q[4];
        ev_q_of(Mx, qp);
        ev_q_mul(qf, qp, q);
        EbldBody& B = E.body[i];
        ebld_body_at(B, p, q);
        const unsigned h1 = efx_hash(0x45564844u, hk, (unsigned)frame, (unsigned)i);
        const unsigned h2 = efx_hash(h1, 0x2545F491u, (unsigned)i, hk);
        const float ivx = d.vx * 15.0f, ivz = d.vz * 15.0f;
        if (E.air) {
            /* riding the falling craft: its offset and turn inside the carrier */
            for (int c = 0; c < 3; c++) E.off[i * 3 + c] = p[c] - base[c];
            for (int c = 0; c < 4; c++) E.qoff[i * 4 + c] = q[c];
            B.v[0] = ivx; B.v[2] = ivz;
            continue;
        }
        float dx = p[0] - d.ex, dz = p[2] - d.ez;
        float dl = sqrtf(dx * dx + dz * dz);
        if (dl < 0.03f) {
            dx = eb_r(h2, 3) - 0.5f; dz = eb_r(h1, 3) - 0.5f;
            dl = sqrtf(dx * dx + dz * dz);
            if (dl < 1e-4f) { dx = 1.0f; dz = 0.0f; dl = 1.0f; }
        }
        dx /= dl; dz /= dl;
        /* THE BLAST, by the piece's role as the lab gave it: a turret leaps highest (the
           shot everyone expects of a tank killed), shards fly furthest and spin fastest */
        float up, out, spin;
        evhc_blast(V, i, h1, h2, &up, &out, &spin);
        B.v[0] = ivx + dx * out; B.v[1] = up; B.v[2] = ivz + dz * out;
        for (int c = 0; c < 3; c++) B.w[c] = (eb_r(h2, c) - 0.5f) * 2.0f * spin;
    }
    if (E.air) {
        /* THE CARRIER: the craft's origin in the air, its forward speed, a little sink, and
           a turn: a helicopter spins down about its mast, a plane rolls and noses over */
        const float q0[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        ebld_body_at(E.carrier, base, q0);
        const unsigned h = efx_hash(0x45564341u, hk, (unsigned)frame, 7u);
        E.carrier.v[0] = d.vx * 15.0f;
        E.carrier.v[2] = d.vz * 15.0f;
        E.carrier.v[1] = -0.2f;
        const bool heli = V.cls == EVC_HELI;
        const float spinDir = (h & 1) ? 1.0f : -1.0f;
        E.carrier.w[1] = heli ? spinDir * (3.0f + 2.0f * eb_r(h, 1)) : 0.4f * spinDir;
        /* roll about the direction of flight, pitch the nose down */
        E.carrier.w[0] = (eb_r(h, 2) - 0.5f) * (heli ? 1.2f : 2.0f);
        E.carrier.w[2] = (eb_r(h, 3) - 0.5f) * (heli ? 1.2f : 2.0f);
    }
    const long long wk = ++g_evhcWreckSerial;
    g_evhcWreck[wk] = E;
    printf("EVHC|death|key=%d|type=%s|air=%d|pieces=%d|lift=%.3f|frame=%d\n", key, d.type,
           E.air ? 1 : 0, (int)W.piece.size(), d.lift, frame);
}

/* Whether a death of this type is this system's, asked by the sweep before it looks for
   the explosion (an aircraft's is placed off its ground point). */
static bool evhc_claims_type(const char* type)
{
    return evhc_claims_death(type, K_AIRCRAFT) || evhc_claims_death(type, K_UNIT)
        || evhc_claims_death(type, K_VESSEL);
}

/* THE SWEEP'S ENTRY, declared far above where the death is observed: true when this
   system takes the death (and the old unit shatter must not). */
static bool evhc_take_death(const char* type, int kind, int id, int house, int face,
                            float ex, float ez, float vx, float vz, float lift, int key,
                            int frame)
{
    if (!evhc_claims_death(type, kind)) return false;
    EvhcDeathIn d;
    snprintf(d.type, sizeof d.type, "%s", type);
    d.kind = kind; d.id = id; d.house = house; d.face = face;
    d.ex = ex; d.ez = ez; d.vx = vx; d.vz = vz; d.lift = lift;
    evhc_note_death(d, key, frame);
    return true;
}

/* The debris gravity the engine's own chunks fall with (EFX_CHUNK_YACC over EFX_U, per
   tick squared), in cells a second squared, so a falling craft keeps pace with the
   cartridge's own chunks thrown out beside it. */
static const float EVHC_FALL_G = 2.0f / 256.0f * 225.0f;

/* The impact: the craft breaks where it came down. Fire and smoke there, and a jolt. */
static void evhc_impact(EvhcWreck& E, const EvhcModel& V, int frame, long long key)
{
    E.falling = false;
    E.impactTick = frame;
    if (evhc_finite(E.carrier.p[0]) && evhc_finite(E.carrier.p[2])) {
        E.wx = E.carrier.p[0]; E.wz = E.carrier.p[2];
    }
    E.wet = evhc_cell_wet(E.wx, E.wz);
    float R[9];
    eb_qmat(E.carrier.q, R);
    const EbldSet& W = V.m.set[EBS_DESTROYED];
    const unsigned hk = (unsigned)key;
    for (size_t i = 0; i < E.body.size() && i < W.piece.size(); i++) {
        EbldBody& B = E.body[i];
        float ro[3];
        eb_mulv(R, &E.off[i * 3], ro);
        float p[3], q[4];
        for (int c = 0; c < 3; c++) p[c] = E.carrier.p[c] + ro[c];
        ev_q_mul(E.carrier.q, &E.qoff[i * 4], q);
        eb_qnorm(q);
        ebld_body_at(B, p, q);
        /* the carrier's velocity at the piece (v + w x r), then thrown out of the impact */
        float wr[3];
        eb_cross(E.carrier.w, ro, wr);
        const unsigned h1 = efx_hash(0x45564958u, hk, (unsigned)frame, (unsigned)i);
        const unsigned h2 = efx_hash(h1, 0x7F4A7C15u, (unsigned)i, hk);
        float dx = ro[0], dz = ro[2];
        float dl = sqrtf(dx * dx + dz * dz);
        if (dl < 0.03f) { dx = eb_r(h2, 3) - 0.5f; dz = eb_r(h1, 3) - 0.5f; dl = sqrtf(dx * dx + dz * dz) + 1e-4f; }
        dx /= dl; dz /= dl;
        float up, out, spin;
        evhc_blast(V, i, h1, h2, &up, &out, &spin);
        B.v[0] = E.carrier.v[0] * 0.6f + wr[0] + dx * out;
        B.v[1] = up * 0.8f;
        B.v[2] = E.carrier.v[2] * 0.6f + wr[2] + dz * out;
        for (int c = 0; c < 3; c++) B.w[c] = (eb_r(h2, c) - 0.5f) * 2.0f * spin;
    }
    /* fire and black smoke where it hit, into the effects pool */
    if (g_efxPoolLive) {
        for (int n = 0; n < 18; n++) {
            const unsigned h = efx_hash(0x45564650u, hk, (unsigned)frame, (unsigned)n);
            EfxPart p;
            memset(&p, 0, sizeof p);
            const float a = 6.2831853f * eb_r(h, 0), rr = 0.35f * sqrtf(eb_r(h, 1));
            p.x = E.carrier.p[0] + rr * cosf(a);
            p.z = E.carrier.p[2] + rr * sinf(a);
            p.y = efx_ground(p.x, p.z) + 0.05f;
            p.sx = p.x; p.sy = p.y; p.sz = p.z;
            const bool fire = n < 10;
            p.vx = (eb_r(h, 2) - 0.5f) * 0.03f;
            p.vz = (eb_r(h, 3) - 0.5f) * 0.03f;
            p.vy = fire ? 0.02f + 0.02f * eb_r(h, 1) : 0.015f + 0.01f * eb_r(h, 2);
            p.ay = fire ? 0.0006f : 0.0004f;
            p.hw = fire ? 0.18f + 0.12f * eb_r(h, 3) : 0.22f;
            const int life = fire ? 14 + (int)(10 * eb_r(h, 0)) : 40 + (int)(30 * eb_r(h, 1));
            p.grow = (fire ? 0.004f : 0.012f);
            p.life = (short)life;
            p.sys = fire ? 5 : 6;
            p.r = p.g = p.b = 255;
            p.a = fire ? 255 : 200;
            int ar = (int)((float)p.a / (0.4f * (float)life) + 0.5f);
            p.arate = (unsigned char)(ar < 1 ? 1 : ar > 255 ? 255 : ar);
            p.seed = h;
            efx_push(p);
        }
    }
    shake_tap(SHAKE_VEHICLE * 2.0f);
    printf("EVHC|impact|key=%lld|type=%s|frame=%d|at=%.3f,%.3f\n", key, E.type, frame,
           E.carrier.p[0], E.carrier.p[2]);
}


/* The wrecks' physics and timeline, on the tick edge. */
static void evhc_wreck_step(int frame)
{
    for (std::map<long long, EvhcWreck>::iterator it = g_evhcWreck.begin(); it != g_evhcWreck.end(); ) {
        EvhcWreck& E = it->second;
        const long long key = it->first;
        if (E.model < 0 || E.model >= (int)g_evhc.size()) { g_evhcWreck.erase(it++); continue; }
        const EvhcModel& V = g_evhc[E.model];
        const EbldSet& W = V.m.set[EBS_DESTROYED];
        const float sinkAfter = V.m.param[EVQ_SINK_AFTER], sinkTime = V.m.param[EVQ_SINK_TIME];
        if (E.falling) {
            /* the carrier: integrated as the engine's chunks are (position, then speed) */
            EbldBody& C = E.carrier;
            for (int c = 0; c < 3; c++) C.pp[c] = C.p[c];
            for (int c = 0; c < 4; c++) C.pq[c] = C.q[c];
            const float h = 1.0f / 15.0f;
            for (int c = 0; c < 3; c++) C.p[c] += C.v[c] * h;
            C.v[1] -= EVHC_FALL_G * h;
            eb_qspin(C.q, C.w, h);
            /* the pieces ride it (their last pose kept for the draw's lerp) */
            float R[9];
            eb_qmat(C.q, R);
            bool hit = false;
            for (size_t i = 0; i < E.body.size() && i < W.piece.size(); i++) {
                EbldBody& B = E.body[i];
                for (int c = 0; c < 3; c++) B.pp[c] = B.p[c];
                for (int c = 0; c < 4; c++) B.pq[c] = B.q[c];
                float ro[3];
                eb_mulv(R, &E.off[i * 3], ro);
                for (int c = 0; c < 3; c++) B.p[c] = C.p[c] + ro[c];
                ev_q_mul(C.q, &E.qoff[i * 4], B.q);
                eb_qnorm(B.q);
                /* touching: any contact point of any piece at or under the ground */
                const EbldPiece& P = W.piece[i];
                float Rb[9];
                eb_qmat(B.q, Rb);
                for (int k = 0; k < P.nhull && !hit; k++) {
                    float hp[3] = { P.hull[k][0] * MODEL_SCALE, P.hull[k][1] * MODEL_SCALE,
                                    P.hull[k][2] * MODEL_SCALE }, r[3];
                    eb_mulv(Rb, hp, r);
                    if (B.p[1] + r[1] <= efx_ground(B.p[0] + r[0], B.p[2] + r[2])) hit = true;
                }
            }
            /* and a fall that never lands (off the map, into water under the terrain) is
               ended by the clock */
            if (hit || frame - E.deathTick > 120) evhc_impact(E, V, frame, key);
            ++it;
            continue;
        }
        const int t0 = E.impactTick >= 0 ? E.impactTick : E.deathTick;
        const int t = frame - t0;
        int sinkAt = (int)(sinkAfter * 15.0f + 0.5f);
        int sinkEnd = sinkAt + (int)(sinkTime * 15.0f + 0.5f);
        /* A WRECK ON WATER GOES DOWN WHERE IT CAME TO REST: the pieces would otherwise
           lie on the seabed the ground height describes. They are thrown, then sink
           within the second as they fade. */
        if (evhc_on_water(E)) { sinkAt = 8; sinkEnd = sinkAt + 30; }
        if (!E.sinking) {
            EbldGround G;
            memset(&G, 0, sizeof G);
            for (size_t i = 0; i < E.body.size() && i < W.piece.size(); i++)
                ebld_body_tick(E.body[i], W.piece[i], NULL, G);
            if (t >= sinkAt) {
                E.sinking = true;
                for (size_t i = 0; i < E.body.size(); i++) {
                    EbldBody& B = E.body[i];
                    for (int c = 0; c < 3; c++) B.pp[c] = B.p[c];
                    for (int c = 0; c < 4; c++) B.pq[c] = B.q[c];
                    B.y0 = B.p[1];
                }
                printf("EVHC|sink|key=%lld|type=%s|frame=%d|wet=%d\n", key, E.type, frame, E.wet ? 1 : 0);
            }
        }
        if (E.sinking && t >= sinkEnd) {
            printf("EVHC|gone|key=%lld|type=%s|frame=%d\n", key, E.type, frame);
            g_evhcWreck.erase(it++);
            continue;
        }
        ++it;
    }
}

/* A wreck's piece pose, lerped between the last two ticks (not while sinking: the sink is
   the draw's own clock), as the 3x4 draw_mesh takes and the world point it stands at. */
static void evhc_body_pose(const EbldBody& B, const EbldPiece& P, bool sinking, float sink,
                           float* Mx, float* p)
{
    const float a = sinking ? 0.0f : g_tickAlpha;
    float q[4];
    for (int c = 0; c < 3; c++) p[c] = B.pp[c] + (B.p[c] - B.pp[c]) * a;
    const float dd = B.pq[0] * B.q[0] + B.pq[1] * B.q[1] + B.pq[2] * B.q[2] + B.pq[3] * B.q[3];
    const float sg = dd < 0.0f ? -1.0f : 1.0f;
    for (int c = 0; c < 4; c++) q[c] = B.pq[c] + (sg * B.q[c] - B.pq[c]) * a;
    eb_qnorm(q);
    if (sinking) p[1] = B.y0 - sink;
    float R[9];
    eb_qmat(q, R);
    for (int i = 0; i < 3; i++) {
        for (int c = 0; c < 3; c++) Mx[i * 4 + c] = R[i * 3 + c];
        Mx[i * 4 + 3] = -(R[i * 3] * P.c[0] + R[i * 3 + 1] * P.c[1] + R[i * 3 + 2] * P.c[2]);
    }
}
/* How far a wreck has sunk: e 0..1 over the sink time, the depth on a smoothstep. */
static void evhc_sink(const EvhcWreck& E, const EvhcModel& V, float now, float* e, float* sink)
{
    *e = 0.0f;
    *sink = 0.0f;
    if (!E.sinking) return;
    const int t0 = E.impactTick >= 0 ? E.impactTick : E.deathTick;
    const float after = evhc_on_water(E) ? 8.0f / 15.0f : V.m.param[EVQ_SINK_AFTER];
    const float time = evhc_on_water(E) ? 2.0f : V.m.param[EVQ_SINK_TIME];
    float k = ((now - (float)t0) / 15.0f - after) / (time > 0.01f ? time : 0.01f);
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;
    *e = k;
    *sink = (evhc_on_water(E) ? 0.6f : V.m.param[EVQ_SINK_DEPTH]) * k * k * (3.0f - 2.0f * k);
}
static bool evhc_wreck_shown(const EvhcWreck& E)
{
    if (!g_vShowUnits) return false;
    return g_objShroud ? shroud_vis_at(E.wx, E.wz) > 0.0f
                       : !shroud_cell_hidden((int)E.wx, (int)E.wz);
}

/* The wrecks, drawn from the same sites as the remastered buildings' pieces, in the same
   state: one-sided, the broken faces (caps and linings) pushed back in depth so a surface
   they touch always wins (ebld_gl_begin). */
static void evhc_draw_flights(int pass);
static void evhc_draw_wrecks(int pass, bool fading)
{
    /* the V2's rockets in flight go down in every pass the wrecks do (they outlive the
       launcher's own draw as the wrecks outlive their vehicle) */
    if (!fading) evhc_draw_flights(pass);
    /* (`evhc 0` in a run takes the wrecks out of the picture with the models: --noevhc never
       makes one) */
    if (g_evhcWreck.empty() || pass == MODE_SHADOW || !g_evhcOn) return;
    const bool aux = ebld_aux_pass();
    if (aux && fading) return;
    const float now = engine_time();
    bool began = false;
    EbldGL st;
    memset(&st, 0, sizeof st);
    for (std::map<long long, EvhcWreck>::const_iterator it = g_evhcWreck.begin();
         it != g_evhcWreck.end(); ++it) {
        const EvhcWreck& E = it->second;
        if (E.model < 0 || E.model >= (int)g_evhc.size()) continue;
        EvhcModel& V = g_evhc[E.model];
        if (!evhc_meshes_ok(V) || V.m.nset != 2 || !evhc_wreck_shown(E)) continue;
        if (!evhc_resident(V, true)) continue;
        float e = 0.0f, sink = 0.0f;
        evhc_sink(E, V, now, &e, &sink);
        int as = (int)(255.0f * (1.0f - e) + 0.5f);
        if (as <= 0) continue;
        if (aux) { if (as < (g_evhcNrmPass ? 255 : 128)) continue; as = 255; }
        else if ((as < 255) != fading) continue;
        if (!began) { st = ebld_gl_begin(aux); began = true; }
        if (g_evhcNrmPass && pass == MODE_OPAQUE) g_evhcNrmWrecks++;
        const int mi = V.m.mesh[EBM_WRECK];
        const EbldSet& W = V.m.set[EBS_DESTROYED];
        const std::vector<int>& rg = V.m.rng[EBM_WRECK];
        g_meshAlphaScale = (unsigned char)as;
        for (size_t k = 0; k < W.piece.size() && k < E.body.size(); k++) {
            const int* r = &rg[k * EBR];
            if (r[EBR_B] <= r[EBR_A] && r[EBR_RB] <= r[EBR_RA]) continue;
            const bool restWas=g_meshBatchRestPose;
            g_meshBatchRestPose=(E.body[k].asleep||E.body[k].pinned)&&!E.sinking;
            const EbldPiece& P = W.piece[k];
            float Mx[12], p[3];
            evhc_body_pose(E.body[k], P, E.sinking, sink, Mx, p);
            const float lift = p[1] - terrain_y(p[0], p[2]);
            const bool two = (P.flags & EBF_DOUBLE) != 0;
            if (two) ebld_gl_sides(st, true);
            for (int blk = 0; blk < 2; blk++) {
                const int r0 = blk ? r[EBR_RA] : r[EBR_A], r1 = blk ? r[EBR_RB] : r[EBR_B];
                if (r1 <= r0) continue;
                ebld_gl_broken(st, blk == 1);
                draw_mesh(mi, p[0], p[2], -1, pass, 0, 0.0f, E.house, 1.0f, lift, false,
                          WOBBLE_NONE, -1.0f, Mx, (size_t)r0, (size_t)r1);
            }
            ebld_gl_broken(st, false);
            if (two) ebld_gl_sides(st, false);
            g_meshBatchRestPose=restWas;
        }
        g_meshAlphaScale = 255;
    }
    if (began) ebld_gl_end(st);
}

/* THE WRECKS' EMBER GLOW: the destroyed model's emit maps, added over the pieces in the
   colour picture only, flickering on two sines of the engine clock phased per wreck and
   fading with the wreck, as the remastered buildings' glow does. */
static void evhc_draw_glow(void)
{
    if (g_evhcWreck.empty() || ebld_aux_pass() || !g_evhcOn) return;
    const bool transientWas=g_meshTransientColor;
    g_meshTransientColor=mesh_perf_switch("OPENCNC3D_MESH_GLOW_DYNAMIC");
    const float now = engine_time();
    GLint depthFunc = GL_LEQUAL;
    GLboolean depthMask = GL_FALSE;
    bool began = false;
    EbldGL st;
    memset(&st, 0, sizeof st);
    for (std::map<long long, EvhcWreck>::const_iterator it = g_evhcWreck.begin();
         it != g_evhcWreck.end(); ++it) {
        const EvhcWreck& E = it->second;
        if (E.model < 0 || E.model >= (int)g_evhc.size()) continue;
        EvhcModel& V = g_evhc[E.model];
        if (!evhc_meshes_ok(V) || V.m.nset != 2) continue;
        const int mi = V.m.mesh[EBM_GLOWX];
        if (mi < 0 || mi >= (int)g_pack.mesh.size() || g_pack.mesh[mi].tris.empty()) continue;
        if (!evhc_wreck_shown(E) || !evhc_resident(V, true)) continue;
        float e = 0.0f, sink = 0.0f;
        evhc_sink(E, V, now, &e, &sink);
        const unsigned ph = efx_hash(0x4556474Cu, (unsigned)it->first, 0u, 0u);
        const float t = now / 15.0f;
        float fl = 0.5f + 0.3f * sinf(t * 2.3f + 6.2831853f * eb_r(ph, 0))
                        + 0.2f * sinf(t * 5.7f + 6.2831853f * eb_r(ph, 1));
        if (fl < 0.0f) fl = 0.0f;
        if (fl > 1.0f) fl = 1.0f;
        const float k = (0.45f + 0.55f * fl) * (1.0f - e);
        if (k <= 0.0f) continue;
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
        const EbldSet& W = V.m.set[EBS_DESTROYED];
        const std::vector<int>& rg = V.m.rng[EBM_GLOWX];
        g_meshAlphaScale = (unsigned char)(255.0f * k + 0.5f);
        for (size_t i = 0; i < W.piece.size() && i < E.body.size(); i++) {
            const int* r = &rg[i * EBR];
            if (r[EBR_B] <= r[EBR_A] && r[EBR_RB] <= r[EBR_RA]) continue;
            float Mx[12], p[3];
            evhc_body_pose(E.body[i], W.piece[i], E.sinking, sink, Mx, p);
            const float lift = p[1] - terrain_y(p[0], p[2]);
            for (int blk = 0; blk < 2; blk++) {
                const int r0 = blk ? r[EBR_RA] : r[EBR_A], r1 = blk ? r[EBR_RB] : r[EBR_B];
                if (r1 <= r0) continue;
                ebld_gl_broken(st, blk == 1);
                draw_mesh(mi, p[0], p[2], -1, MODE_OPAQUE, 0, 0.0f, E.house, 1.0f, lift, false,
                          WOBBLE_NONE, -1.0f, Mx, (size_t)r0, (size_t)r1);
            }
            ebld_gl_broken(st, false);
        }
    }
    g_meshAlphaScale = 255;g_meshTransientColor=transientWas;
    if (began) {
        ebld_gl_end(st);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(depthMask);
        glDepthFunc((GLenum)depthFunc);
    }
}

/* ================================================================================
 * THE NORMAL TARGET. The light pass is given each pixel's normal by whatever drew there
 * (fx_draw_terrain_normals); a pixel nothing drew takes the plane of its own facet, rebuilt
 * from the depth buffer, which is how a remastered vehicle was lit until now: every facet
 * edge of a round hull showed as a band. Here every piece the colour picture drew solid,
 * posed by the same matrices (the facing, the lean, the turret, the wheels and tracks,
 * the rotors, the doors, ramps, ducts and the launcher, the recoil, the dock and deploy
 * rigs, each wreck piece's own body), is drawn again with its corners' smooth normals.
 * The walk is the colour picture's own: the same objects, the same checks, in the
 * opaque and cut-out passes; translucent pieces (the blur discs) and a fading wreck write
 * no depth and so no normal.
 *
 * THE CLASS IS AN AUTHORED HULL'S, 0.35 (model3d_mod.h), not the remastered buildings'
 * 0.38. The light pass treats the two alike (the object band: the normal used for the
 * shadow map's offset, the back-face term, the contact march, the relight and the
 * occlusion's hemisphere, which takes the object weight), but 0.38 is also how the F5
 * panel's STRUCTURES group knows a structure, and a vehicle is not one. A self-lit
 * surface (none of the vehicles has one today) uses 0.22 while vehicle tuning is
 * active. Vehicle normals then use 0.34, apart from authored building normals.
 * At neutral the original 0.35/0.15 classes remain byte-identical.
 *
 * Drawn against the scene's own depth by the program the remastered buildings use
 * (ebld_nrm_prog). Tier 2 only: Enhanced does not exist on the Win98 build.
 * ============================================================================== */
#define EVHC_NORMAL_CLASS 0.35f
static bool g_evhcNrmOn = true;                  /* --noevhcnormals clears it */
static int  dosmake_strip_for(const SimObject& o);
static void evhc_draw_normals(void)
{
    g_evhcNrmObjs = g_evhcNrmRigs = g_evhcNrmWrecks = 0;
    /* (Red Alert's 3D modes reach it for the types its module stages a file for; every other
       Red Alert object is a sprite card, which has no normal to draw) */
    if (g_evhc.empty() || !evhc_rule_now() || !g_evhcNrmOn) return;
    const GLuint prog = ebld_nrm_prog();
    if (!prog) return;
    GLint was = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &was);
    fx_glUseProgram(prog);
    fx_set1i(prog, "uTex", 0);
    fx_set1f(prog, "uClass", fx_vehicle_active(&g_fx) ? 0.34f : EVHC_NORMAL_CLASS);
    fx_set1f(prog, "uEmitClass", fx_vehicle_active(&g_fx) ? 0.22f : EBLD_SELFLIT_CLASS);
    const int was3 = g_m3NormalPass;
    g_m3NormalPass = 1;                       /* an auxiliary pass for every check here */
    g_meshNrmOut = 1;
    g_evhcNrmPass = true;
    for (int pi = 0; pi < 2; pi++) {
        const int pass = pi ? MODE_CUTOUT : MODE_OPAQUE;
        fx_set1i(prog, "uCut", pi);
        for (size_t i = 0; i < g_objects.size(); i++) {
            const SimObject& o = g_objects[i];
            if (o.kind == K_INFANTRY || !visible(o) || dosmake_strip_for(o) >= 0) continue;
            if (o.kind == K_BUILDING) {
                /* the MCV in the Construction Yard's deploy rig, the harvester in the
                   refinery's dock: where the colour picture reaches them */
                float rigT = -1.0f;
                const int rig = mcvrig_for(o, &rigT);
                if (rig >= 0) {
                    if (evhc_deploy_draw(o, pass, rig, rigT) && !pi) g_evhcNrmRigs++;
                    continue;
                }
                if (mesh_for(o) < 0 || shatter_owns(o)) continue;
                if (evhc_dock_draw(o, pass) && !pi) g_evhcNrmRigs++;
                continue;
            }
            if (!evhc_kind(o.kind)) continue;
            /* (a Red Alert unit has no cartridge model to stand in for) */
            if ((!g_raModule && mesh_for(o) < 0) || shatter_owns(o)) continue;
            if (evhc_draw_object(o, pass) && !pi) g_evhcNrmObjs++;
        }
        /* the riders on a hovercraft's deck */
        for (size_t k = 0; g_ridersDraw && k < g_riders.size(); k++) {
            const SimObject& r = g_riders[k];
            if (!visible(r)) continue;
            if (evhc_draw_object(r, pass) && !pi) g_evhcNrmObjs++;
        }
        evhc_draw_wrecks(pass, false);
    }
    g_evhcNrmPass = false;
    g_meshNrmOut = 0;
    g_m3NormalPass = was3;
    fx_glUseProgram((GLuint)was);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* ================================================================================
 * THE SPECULAR MAPS. A vehicle whose export carries one (<TYPE>.spec beside its file)
 * is drawn a second time, after the normals, into the light pass's specular target
 * (fx_post.h, g_fxSpec): the same walk, poses and matrices as the normal target, against
 * the scene's own depth, with every texture bind answering the texture's specular map
 * (g_meshSpecPass, livery_texture). The target then holds, per pixel of such a vehicle,
 * how strongly it reflects (red) and how tight its highlight is (green), alpha 1 where
 * one drew; the light pass adds the sun's highlight on the normal the normal target holds,
 * outside the sun's shadow, and a sheen of the sky at grazing angles. Because the normal
 * turns with the hull, the highlight slides over the tank as the vehicle turns, which a
 * painted highlight cannot do.
 *
 * Only the vehicles that carry a map are drawn here: a vehicle in front of one has
 * already won the scene's depth, so the one behind it writes nothing where it is hidden.
 * Wrecks carry none. Tier 2 only: the target and its shader do not exist on the Win98
 * build, where a vehicle keeps the highlights its texture has painted in.
 * ============================================================================== */
static const char* EVHC_SPEC_FS =
    "#version 120\n"
    "varying vec3 vN;\n"
    "varying vec2 vUV;\n"
    "varying float vEmit;\n"
    "uniform sampler2D uTex;\n"
    "uniform int uCut;\n"
    "void main() {\n"
    "    vec4 s = texture2D(uTex, vUV);\n"
    "    if (uCut == 1 && s.a <= 0.5) discard;\n"
    /* a self-lit texel (a lit lens) makes its own light and takes no highlight */
    /* (blue, the upright sheen, 0 on every map but the ones that ask for it) */
    "    gl_FragColor = vEmit > 0.5 ? vec4(0.0, 0.0, 0.0, 1.0) : vec4(s.rgb, 1.0);\n"
    "}\n";
static GLuint g_evhcSpecProg = 0;
static bool g_evhcSpecTried = false;
static GLuint evhc_spec_prog(void)
{
    if (g_evhcSpecProg || g_evhcSpecTried) return g_evhcSpecProg;
    g_evhcSpecTried = true;
    if (!fx_gl_ready) return 0;
    GLuint vs = fx_compile(GL_VERTEX_SHADER, EBLD_NRM_VS, "evhc-specular");
    if (!vs) return 0;
    GLuint fs = fx_compile(GL_FRAGMENT_SHADER, EVHC_SPEC_FS, "evhc-specular");
    if (!fs) { fx_glDeleteShader(vs); return 0; }
    GLuint p = fx_glCreateProgram();
    fx_glAttachShader(p, vs);
    fx_glAttachShader(p, fs);
    fx_glLinkProgram(p);
    GLint ok = 0;
    fx_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        fprintf(stderr, "evhc: the specular target's program FAILED TO LINK\n");
        fx_glDeleteProgram(p);
        p = 0;
    }
    fx_glDeleteShader(vs);
    fx_glDeleteShader(fs);
    g_evhcSpecProg = p;
    ebld_pose_identity(p);
    return p;
}

/* Whether this frame wants the specular target: the rule, both switches, and a vehicle
   with a map in view (a frame without one allocates and draws nothing, and the light pass
   is the picture it was). */
static bool evhc_spec_want(void)
{
    if (!g_fx.enabled || !g_evhcSpecOn || !g_fx.vspec_on) return false;
    /* (Red Alert's ore and gems carry maps of their own: ra3d_ore_mod.h) */
    if (g_raModule && ra3d_ore_spec_want()) return evhc_spec_prog() != 0;
    if (g_evhc.empty() && g_ebld.empty()) return false;
    for (size_t i = 0; i < g_evhc.size(); i++)
        if (g_evhc[i].m.hasSpec && g_evhc[i].m.attached) return evhc_spec_prog() != 0;
    for (size_t i = 0; i < g_ebld.size(); i++)
        if (g_ebld[i].hasSpec && g_ebld[i].attached) return evhc_spec_prog() != 0;
    return false;
}

static void evhc_draw_spec(void)
{
    g_evhcSpecObjs = g_evhcSpecRigs = 0;
    const GLuint prog = evhc_spec_prog();
    if (!prog) return;
    GLint was = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &was);
    fx_glUseProgram(prog);
    fx_set1i(prog, "uTex", 0);
    const int was3 = g_m3NormalPass;
    g_m3NormalPass = 1;
    g_meshNrmOut = 1;
    g_meshSpecPass = 1;
    const int harv = evhc_model_of("HARV");
    const bool dockSpec = harv >= 0 && g_evhc[harv].m.hasSpec && evhc_rule_now() && g_evhcNrmOn;
    const bool vehSpec = evhc_rule_now() && g_evhcNrmOn;
    g_ebldSpecObjs = 0;
    for (int pi = 0; pi < 2; pi++) {
        const int pass = pi ? MODE_CUTOUT : MODE_OPAQUE;
        fx_set1i(prog, "uCut", pi);
        for (size_t i = 0; i < g_objects.size(); i++) {
            const SimObject& o = g_objects[i];
            if (o.kind == K_INFANTRY || !visible(o) || dosmake_strip_for(o) >= 0) continue;
            if (o.kind == K_BUILDING) {
                /* a remastered building that carries a map, drawn as its normals are */
                const int bm = ebld_model_of(o.type);
                if (bm >= 0 && g_ebld[bm].hasSpec && ebld_rule_now() && g_ebldNrmOn
                    && ebld_draw_object(mesh_for(o), o, pass) && !pi) g_ebldSpecObjs++;
                if (!dockSpec || mesh_for(o) < 0 || shatter_owns(o)) continue;
                if (evhc_dock_draw(o, pass) && !pi) g_evhcSpecRigs++;
                continue;
            }
            if (o.kind==K_TERRAIN && !strncmp(o.type,"ROCK",4)) {
                const int bm=ebld_model_of(o.type);
                if(bm>=0 && g_ebld[bm].hasSpec && ebld_rule_now() && g_ebldNrmOn
                    && ebld_draw_object(mesh_for(o),o,pass) && !pi)g_ebldSpecObjs++;
                continue;
            }
            if (!vehSpec || !evhc_kind(o.kind)) continue;
            if ((!g_raModule && mesh_for(o) < 0) || shatter_owns(o)) continue;
            const int m = evhc_for(o);
            if (m < 0 || !g_evhc[m].m.hasSpec) continue;
            if (evhc_draw_object(o, pass) && !pi) g_evhcSpecObjs++;
        }
    }
    g_meshSpecPass = 0;
    g_meshNrmOut = 0;
    g_m3NormalPass = was3;
    if (g_raModule) ra3d_ore_draw_spec();   /* the ore's and the gems' shine */
    fx_glUseProgram((GLuint)was);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    g_evhcSpecFrames++;
}

/* ================================================================================
 * THE LAMPS. Every headlight, tail light, marker and beacon a vehicle's export names
 * (version 2's light table, lights.json) is a real light under Enhanced: a cone handed to
 * the light pass (FxSpot, fx_post.h), lighting the ground, the units and the buildings in
 * front of it as the remaster lab lights them, and its lens a small glowing disc.
 *
 * WHERE: each lamp rides its piece, so it is posed by that piece's matrix (a turret's
 * searchlight turns with the turret, a lamp on a door with the door), then by the hull's
 * facing, the lean, the model scale and the anchor with an aircraft's lift, exactly as
 * draw_mesh places the piece's corners.
 *
 * WHICH: every lamp of every remastered vehicle the player can see, out while the vehicle
 * is in limbo, below half its strength (the 1995 game's own "damaged" line; the lab puts
 * its lamps out on the damaged model) or wrecked. Rear, marker, navigation and beacon
 * lamps never cast shadows; a headlight may (vlight_shadows).
 *
 * THE BUDGET: FX_MAX_SPOTS slots. The lamps whose cone reaches into the view are ranked by
 * strength and reach over their distance from the middle of the view; the best take the
 * slots, a few kept back for lamps fading out, and a lamp in view that gains or loses a
 * slot fades in or out over EVHC_LAMP_FADE seconds of the engine's clock rather than
 * popping (the engine clock, so two runs of one script are one picture). A lamp coming
 * into view, from past the picture's edge, comes in at the weight its rank gives it.
 *
 * --novehiclelights is the A/B (evhclamps 0|1 in a script); the F5 panel's vlight dials
 * scale it. Classic never reaches it.
 * ============================================================================== */
static bool  g_evhcLampsOn = true;                 /* --novehiclelights clears it */
static const float EVHC_LAMP_GAIN = 2.5f;         /* the lab's gain */
static const float EVHC_LAMP_FADE = 0.35f;        /* seconds in or out */
static const int   EVHC_LAMP_SPARE = 8;           /* slots kept for lamps fading out */
struct EvhcLamp {
    long long key;                                /* unit key * 64 + its light */
    int   model, light;
    float p[3], d[3], n[3];                       /* world: the lens, the beam, the lens's facing */
    float col[3], cosI, cosO, range, score, lensR, opacity;
    bool  shadows, view;
};
static std::vector<EvhcLamp> g_evhcLamp;          /* this frame's lamps, posed */
static std::map<long long, float> g_evhcLampW;    /* each lamp's slot weight, 0..1 */
static float g_evhcLampClock = -1.0f;
static int   g_evhcLampLit = 0, g_evhcLampShadowed = 0, g_evhcLampGlows = 0;

/* A point (or, with point false, a direction) of a piece posed by `M` (the part's 3x4,
   NULL for none) into the world, as draw_mesh places that piece's corners. */
static void evhc_lamp_world(const SimObject& o, const float* M, const float* v, bool point,
                            float* out)
{
    float l[3] = { v[0], v[1], v[2] };
    if (M) {
        l[0] = M[0] * v[0] + M[1] * v[1] + M[2] * v[2] + (point ? M[3] : 0.0f);
        l[1] = M[4] * v[0] + M[5] * v[1] + M[6] * v[2] + (point ? M[7] : 0.0f);
        l[2] = M[8] * v[0] + M[9] * v[1] + M[10] * v[2] + (point ? M[11] : 0.0f);
    }
    float rs, rc;
    facing_rot(evhc_draw_face(o), &rs, &rc);
    const float mx = l[0] * rc + l[2] * rs, mz = -l[0] * rs + l[2] * rc;
    const float sc = point ? MODEL_SCALE : 1.0f;
    const float lx = mx * sc, lz = mz * sc;
    const float ly = l[1] * sc + (point ? alt_lift(o) + o.ylift : 0.0f);
    float wax = 0.0f, waz = 0.0f;
    mesh_wobble(wobble_of(o), o.wx, o.wz, &wax, &waz);
    const float wsx = sinf(wax), wcx = cosf(wax), wsz = sinf(waz), wcz = cosf(waz);
    const float y1 = ly * wcx - lz * wsx, z1 = ly * wsx + lz * wcx;
    const float x2 = lx * wcz - y1 * wsz, y2 = lx * wsz + y1 * wcz;
    if (point) {
        out[0] = o.wx + x2; out[1] = terrain_y(o.wx, o.wz) + y2; out[2] = o.wz + z1;
    } else {
        const float L = sqrtf(x2 * x2 + y2 * y2 + z1 * z1);
        const float k = L > 1e-9f ? 1.0f / L : 0.0f;
        out[0] = x2 * k; out[1] = y2 * k; out[2] = z1 * k;
    }
}

/* ================================================================================
 * RED ALERT'S V2 ROCKET LEAVING THE RAIL. The tick the engine says the rail is empty, the
 * rocket's nose is found where the pose has it (the launch section at its firing lift),
 * OpenCNC3D's cannon flash and the SAM's launch smoke go off there along the rocket's line,
 * and the engine's own V2 projectile, which the engine launches from the lorry's middle on
 * the ground, is drawn from the nose instead and eased onto its own path over its first
 * ticks (evhc_v2_bullet_from, asked by the projectile's card). Nothing here changes when or
 * where the engine fires: the shot is the engine's, its flight the engine's after the ease.
 * ============================================================================== */
struct EvhcLaunch {
    int   key, frame, bullet, face;
    float tip[3], dir[3];
    bool  seen;                                 /* the projectile's own point at first sight */
    float first[3];
};
static std::vector<EvhcLaunch> g_evhcLaunch;
static std::map<int, int> g_evhcLaunches;       /* launches per unit key, for the dump */
static std::vector<std::pair<int, int> > g_evhcV2Fires;   /* this dump's V2 shots: key, bullet */
static void (*g_evhcFirePrev)(const EfxFire& f) = NULL;
static const int EVHC_LAUNCH_EASE = 8;          /* ticks the projectile is eased over */

/* The engine's shots, read for the V2's: which bullet each launched (EFX|FIRE), handed on
   to whoever else listens. */
static void evhc_on_fire(const EfxFire& f)
{
    if (g_raModule && f.kind == 28 && !strcmp(f.type, "V2RL") && f.bullet >= 0)
        g_evhcV2Fires.push_back(std::make_pair((int)K_UNIT * 1000000 + f.id, f.bullet));
    if (g_evhcFirePrev) g_evhcFirePrev(f);
}
static void evhc_fire_hook(void)
{
    if (g_efxOnFire == evhc_on_fire) return;
    g_evhcFirePrev = g_efxOnFire;
    g_efxOnFire = evhc_on_fire;
}

static float evhc_v2_lift_of(const EvhcModel& V)
{
    for (size_t k = 0; k < V.part.size(); k++)
        if (V.part[k].kind == EVP_HINGE && V.part[k].driver == EVD_V2_ERECTOR) return V.part[k].a[6];
    return 0.0f;
}
static int evhc_v2_launches_of(int key)
{
    std::map<int, int>::const_iterator it = g_evhcLaunches.find(key);
    return it == g_evhcLaunches.end() ? 0 : it->second;
}

/* THE V2'S ROCKET AT REST, ITS LINE: the rocket part, its nose and the way it points (pack
   frame), and its length (pack units). The axis runs from the middle of its tail's corners
   to the middle of its nose's (the last eighth of its length at each end), so the fins,
   which stand off the line, do not tilt it; the nose is the farthest point along it. */
static int evhc_v2_rocket_part(const EvhcModel& V)
{
    for (size_t k = 0; k < V.part.size(); k++)
        if (V.part[k].kind == EVP_HINGE && V.part[k].driver == EVD_V2_ROCKET) return (int)k;
    return -1;
}
static bool evhc_v2_rocket_line(const EvhcModel& V, int rp, float* n0, float* a0, float* len)
{
    const EbldSet& S = V.m.set[EBS_INTACT];
    if (rp < 0 || V.part[rp].piece < 0 || V.part[rp].piece >= (int)S.piece.size()) return false;
    const EbldPiece& Pc = S.piece[V.part[rp].piece];
    float zmin = 1e9f, zmax = -1e9f;
    for (int t = Pc.tri0; t < Pc.tri0 + Pc.ntri && t < (int)S.tri.size(); t++)
        for (int c = 0; c < 3; c++) {
            zmin = fminf(zmin, S.tri[t].v[c].z);
            zmax = fmaxf(zmax, S.tri[t].v[c].z);
        }
    if (zmax <= zmin) return false;
    const float band = 0.125f * (zmax - zmin);
    float f[3] = { 0, 0, 0 }, b[3] = { 0, 0, 0 };
    int nf = 0, nb = 0;
    for (int t = Pc.tri0; t < Pc.tri0 + Pc.ntri && t < (int)S.tri.size(); t++)
        for (int c = 0; c < 3; c++) {
            const EbldVert& v = S.tri[t].v[c];
            if (v.z >= zmax - band) { f[0] += v.x; f[1] += v.y; f[2] += v.z; nf++; }
            if (v.z <= zmin + band) { b[0] += v.x; b[1] += v.y; b[2] += v.z; nb++; }
        }
    if (!nf || !nb) return false;
    for (int k = 0; k < 3; k++) { f[k] /= (float)nf; b[k] /= (float)nb; a0[k] = f[k] - b[k]; }
    const float l = sqrtf(a0[0] * a0[0] + a0[1] * a0[1] + a0[2] * a0[2]);
    if (l < 1e-3f) return false;
    for (int k = 0; k < 3; k++) a0[k] /= l;
    float lo = 1e9f, hi = -1e9f;
    for (int t = Pc.tri0; t < Pc.tri0 + Pc.ntri && t < (int)S.tri.size(); t++)
        for (int c = 0; c < 3; c++) {
            const EbldVert& v = S.tri[t].v[c];
            const float u = (v.x - b[0]) * a0[0] + (v.y - b[1]) * a0[1] + (v.z - b[2]) * a0[2];
            lo = fminf(lo, u); hi = fmaxf(hi, u);
        }
    for (int k = 0; k < 3; k++) n0[k] = b[k] + a0[k] * hi;
    *len = hi - lo;
    return true;
}

/* THE V2'S ROCKET IN FLIGHT, one record per rocket from its launch until a little after
   its projectile has gone: the model it is the rocket of, the house it is painted for, the
   nose and line it left the rail on, and the engine's projectile as each tick has it. */
struct EvhcFlight {
    int   bullet, key, model, house, frame;
    float tip[3], dir0[3];                      /* the nose and line on the rail at launch */
    float n0[3], a0[3], len;                    /* the rocket's nose, line and length at rest */
    bool  seen;
    float first[3];                             /* the projectile's own point at first sight */
    float prev[2], cur[2], alt;                 /* its ground point the last two ticks, height */
    float step;                                 /* the longest step it has made in a tick */
    int   curTick, ended;
    bool  target;
    float tx, tz;                               /* where its fuse goes off */
    bool  drawn;
    float nose[3], dir[3], pitchMax, pitchMin;
    int   drawnN;
};
static std::vector<EvhcFlight> g_evhcFlight;
static int g_evhcV2Cards = 0, g_evhcV2StandDown = 0;
static const float EVHC_V2_ARC = 1.6f;           /* the flight's highest bulge, cells */

static void evhc_v2_launch(const SimObject& o, int m, const EvhcUnit* U, int frame)
{
    const EvhcModel& V = g_evhc[m];
    const int rp = evhc_v2_rocket_part(V);
    if (rp < 0) return;
    /* the rocket's nose and line at rest */
    float nose[3], axis[3], rlen = 0.0f;
    if (!evhc_v2_rocket_line(V, rp, nose, axis, &rlen)) return;
    /* posed as the last tick left it, before this tick began to lower it: the section at
       the top of its lift when the raise was in time for the shot */
    EvhcPose P;
    evhc_pose_of(V, o, U, P);
    const float e = U ? U->erectP : 1.0f;
    P.erect = e * e * (3.0f - 2.0f * e);
    std::vector<float> mats;
    evhc_pose_all(V, P, U, mats);
    const float* M = &mats[rp * 12];
    EvhcLaunch L;
    memset(&L, 0, sizeof L);
    L.key = smooth_key(o);
    L.frame = frame;
    L.bullet = -1;
    for (size_t i = 0; i < g_evhcV2Fires.size(); i++)
        if (g_evhcV2Fires[i].first == L.key) L.bullet = g_evhcV2Fires[i].second;
    L.face = o.face;
    evhc_lamp_world(o, M, nose, true, L.tip);
    evhc_lamp_world(o, M, axis, false, L.dir);
    g_evhcLaunch.push_back(L);
    g_evhcLaunches[L.key]++;
    /* the rocket that flies, from the rail's nose along the rail's line */
    if (L.bullet >= 0) {
        for (size_t i = 0; i < g_evhcFlight.size(); )
            if (g_evhcFlight[i].bullet == L.bullet) g_evhcFlight.erase(g_evhcFlight.begin() + i);
            else i++;
        EvhcFlight F;
        memset(&F, 0, sizeof F);
        F.bullet = L.bullet; F.key = L.key; F.model = m; F.house = mesh_house(o);
        F.frame = frame; F.ended = -1; F.curTick = frame;
        for (int k = 0; k < 3; k++) {
            F.tip[k] = L.tip[k]; F.dir0[k] = L.dir[k]; F.n0[k] = nose[k]; F.a0[k] = axis[k];
        }
        F.len = rlen;
        F.pitchMax = -90.0f; F.pitchMin = 90.0f;
        g_evhcFlight.push_back(F);
    }
    printf("EVHC|launch|key=%d|type=%s|bullet=%d|tip=%.3f,%.3f,%.3f|dir=%.3f,%.3f,%.3f"
           "|ground=%.3f|frame=%d\n", L.key, o.type, L.bullet, L.tip[0], L.tip[1], L.tip[2],
           L.dir[0], L.dir[1], L.dir[2], terrain_y(o.wx, o.wz), frame);
}

/* The launch's flash and smoke, tick by tick, on the effects' own emitters: OpenCNC3D's
   cannon flash (Enhanced's CannonFlash) at the nose and the SAM site's launch smoke for
   the rocket's heading, the cartridge's Sam_N .. Sam_NW, each over its own burst. Enhanced
   effects only; the records go when their projectile has been eased onto its path. */
static void evhc_v2_launch_step(int frame)
{
    if (!g_raModule) return;
    evhc_fire_hook();
    for (size_t i = 0; i < g_evhcLaunch.size(); ) {
        EvhcLaunch& L = g_evhcLaunch[i];
        const int k = frame - L.frame;
        if (k > EVHC_LAUNCH_EASE + 2 || k < 0) { g_evhcLaunch.erase(g_evhcLaunch.begin() + i); continue; }
        if (g_efxEnhOn) {
            const unsigned key = 0x56324C00u ^ (unsigned)L.key;
            const EfxSourceRec* flash = NULL;
            for (int j = 0; j < EFX_ENH_NEXTRA; j++)
                if (!strcmp(EFX_ENH_EXTRA[j].recipe, "MUZZLE_FLASH")) flash = &EFX_ENH_EXTRA[j].src;
            const int f8 = ((((L.face < 0 ? 0 : L.face) + 16) & 255) >> 5) & 7;
            const EfxSourceRec& smoke = EFX_SRC[1 + f8];
            const EfxSourceRec* src[2] = { flash, &smoke };
            for (int s = 0; s < 2; s++) {
                if (!src[s]) continue;
                const EfxSourceRec& r = *src[s];
                const int burst = r.burst > 0 ? r.burst : 5;
                if (k >= burst) continue;
                float count = r.ppt;
                for (int j = 0; j < k && r.burst > 0; j++) count *= r.mult;
                if ((int)count > 0)
                    efx_emit(r, L.tip[0], L.tip[1], L.tip[2], (int)count, key, L.frame, s, k);
            }
        }
        i++;
    }
    g_evhcV2Fires.clear();
    evhc_v2_flight_step(frame);
}

/* WHERE A V2'S PROJECTILE IS DRAWN in its first ticks: from the rocket's nose on the
   raised rail, eased onto the engine's own path (the offset from the nose to where the
   engine had it at first sight shrinks to nothing over EVHC_LAUNCH_EASE ticks). `c` is the
   card's centre in the world, changed in place; false for any other projectile. */
static bool evhc_v2_bullet_from(int bullet, float* c)
{
    for (size_t i = 0; i < g_evhcLaunch.size(); i++) {
        EvhcLaunch& L = g_evhcLaunch[i];
        if (L.bullet != bullet || bullet < 0) continue;
        if (!L.seen) { L.seen = true; for (int k = 0; k < 3; k++) L.first[k] = c[k]; }
        float u = ((float)(g_engineFrame - L.frame) + g_tickAlpha) / (float)EVHC_LAUNCH_EASE;
        u = u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;
        const float w = 1.0f - u * u * (3.0f - 2.0f * u);
        for (int k = 0; k < 3; k++) c[k] += (L.tip[k] - L.first[k]) * w;
        return true;
    }
    return false;
}

/* WHERE THE FLYING ROCKET IS, at `alpha` into the tick after the engine's last (carried
   on from the engine's last point at its last step, so it moves smoothly between the
   engine's points and stands on each as its tick comes): its nose and the way it points,
   world. Across the ground it follows the engine's projectile, eased off the rail's nose
   as the card is; in height it flies its own arc, from the rail's nose on the rail's
   slope (the launch section's 45 degrees) over a bulge of at most EVHC_V2_ARC cells down
   to the ground where the engine's fuse is set to go off, by how far across the ground it
   has come: pitched up as it leaves, levelling over the top, nosing down onto the target.
   The engine's own projectile flies level at its flight height and bursts within a step
   of that point, so the arc comes down a step short of it and the burst comes as it
   reaches the ground. A projectile whose fuse point the brain does not give flies at the
   engine's height. */
/* The flight's arc over a ground point: its height there and its slope, and the way
   across the ground it runs (from the rail's nose toward the fuse point). */
static void evhc_v2_arc(const EvhcFlight& F, float hx, float hz, float* y, float* slope,
                        float* ux_, float* uz_)
{
    float ux = F.tx - F.tip[0], uz = F.tz - F.tip[2];
    float D = sqrtf(ux * ux + uz * uz);
    if (D < 1e-3f) { ux = F.dir0[0]; uz = F.dir0[2]; D = sqrtf(ux * ux + uz * uz); }
    if (D < 1e-3f) { ux = 0.0f; uz = -1.0f; D = 1.0f; }
    ux /= D; uz /= D;
    D = sqrtf((F.tx - F.tip[0]) * (F.tx - F.tip[0]) + (F.tz - F.tip[2]) * (F.tz - F.tip[2]))
        - F.step;
    if (D < 0.5f) D = 0.5f;
    float x = (hx - F.tip[0]) * ux + (hz - F.tip[2]) * uz;
    x = x < 0.0f ? 0.0f : x > D ? D : x;
    const float s = x / D;
    const float y0 = F.tip[1], y1 = terrain_y(F.tx, F.tz) + 0.02f;
    const float run = sqrtf(F.dir0[0] * F.dir0[0] + F.dir0[2] * F.dir0[2]);
    const float k0 = run > 1e-3f ? F.dir0[1] / run : 1.0f;
    float P = (k0 * D - (y1 - y0)) * 0.25f;
    P = P < 0.0f ? 0.0f : P > EVHC_V2_ARC ? EVHC_V2_ARC : P;
    *y = y0 + (y1 - y0) * s + 4.0f * P * s * (1.0f - s);
    *slope = ((y1 - y0) + 4.0f * P * (1.0f - 2.0f * s)) / D;
    *ux_ = ux; *uz_ = uz;
}

static void evhc_v2_flight_pose(const EvhcFlight& F, float alpha, float* nose, float* dir)
{
    const float e[2] = { F.cur[0] + (F.cur[0] - F.prev[0]) * alpha,
                         F.cur[1] + (F.cur[1] - F.prev[1]) * alpha };
    float u = ((float)(F.curTick - F.frame) + alpha) / (float)EVHC_LAUNCH_EASE;
    u = u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;
    const float w = 1.0f - u * u * (3.0f - 2.0f * u);
    const float hx = e[0] + (F.tip[0] - F.first[0]) * w;
    const float hz = e[1] + (F.tip[2] - F.first[2]) * w;
    float tan_[3];
    float y;
    if (F.target) {
        float slope, ux, uz;
        evhc_v2_arc(F, hx, hz, &y, &slope, &ux, &uz);
        tan_[0] = ux; tan_[1] = slope; tan_[2] = uz;
    } else {
        y = terrain_y(e[0], e[1]) + F.alt + 0.05f + (F.tip[1] - F.first[1]) * w;
        tan_[0] = F.cur[0] - F.prev[0]; tan_[1] = 0.0f; tan_[2] = F.cur[1] - F.prev[1];
        if (tan_[0] * tan_[0] + tan_[2] * tan_[2] < 1e-8f) { tan_[0] = F.dir0[0]; tan_[2] = F.dir0[2]; }
    }
    float tl = sqrtf(tan_[0] * tan_[0] + tan_[1] * tan_[1] + tan_[2] * tan_[2]);
    if (tl < 1e-6f) tl = 1.0f;
    for (int k = 0; k < 3; k++) dir[k] = F.dir0[k] * w + tan_[k] / tl * (1.0f - w);
    float dl = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (dl < 1e-6f) { dir[0] = F.dir0[0]; dir[1] = F.dir0[1]; dir[2] = F.dir0[2]; dl = 1.0f; }
    for (int k = 0; k < 3; k++) dir[k] /= dl;
    nose[0] = hx; nose[1] = y; nose[2] = hz;
}

/* The flights' tick: each projectile's point from the engine, and the record ended a tick
   after the engine's projectile has gone (it burst). The trail is the engine's own: the
   V2's projectile lays its smoke puffs as it flies, drawn as OpenCNC3D's SMOKEY, and each
   is laid on the flight's arc over its ground point (evhc_v2_trail_y) rather than at the
   engine's flight height. */
static void evhc_v2_flight_step(int frame)
{
    for (size_t i = 0; i < g_evhcFlight.size(); ) {
        EvhcFlight& F = g_evhcFlight[i];
        const EfxBullet* b = NULL;
        for (size_t k = 0; k < g_efxBullets.size(); k++)
            if (g_efxBullets[k].id == F.bullet) b = &g_efxBullets[k];
        if (b && F.ended < 0) {
            const float alt = efx_bullet_lift(*b);
            if (!F.seen) {
                F.seen = true;
                F.first[0] = b->wx; F.first[2] = b->wz;
                F.first[1] = terrain_y(b->wx, b->wz) + alt + 0.05f;
                F.prev[0] = F.cur[0] = b->wx; F.prev[1] = F.cur[1] = b->wz;
            } else if (frame != F.curTick) {
                F.prev[0] = F.cur[0]; F.prev[1] = F.cur[1];
                F.cur[0] = b->wx; F.cur[1] = b->wz;
                const float dx = F.cur[0] - F.prev[0], dz = F.cur[1] - F.prev[1];
                F.step = fmaxf(F.step, sqrtf(dx * dx + dz * dz));
            }
            F.curTick = frame;
            F.alt = alt;
            if (!F.target && b->tx == b->tx && b->tz == b->tz) {
                F.target = true; F.tx = b->tx; F.tz = b->tz;
            }
        } else if ((F.seen || frame - F.frame > 4) && F.ended < 0) {
            F.ended = frame;
        }
        if (F.ended >= 0 && frame - F.ended > 60) { g_evhcFlight.erase(g_evhcFlight.begin() + i); continue; }
        i++;
    }
}

/* A trail puff's height when the projectile that laid it is a V2's rocket drawn on its arc:
   the arc's height over the puff's ground point. False for any other projectile. */
static bool evhc_v2_trail_y(int bullet, float wx, float wz, float* y)
{
    if (!evhc_rule_now()) return false;
    for (size_t i = 0; i < g_evhcFlight.size(); i++) {
        const EvhcFlight& F = g_evhcFlight[i];
        if (F.bullet != bullet || !F.seen || F.ended >= 0 || !F.target) continue;
        float slope, ux, uz;
        evhc_v2_arc(F, wx, wz, y, &slope, &ux, &uz);
        return true;
    }
    return false;
}

/* Whether a projectile is drawn as its V2's rocket now (its card then stands down). */
static bool evhc_v2_flight_has(int bullet)
{
    if (!evhc_rule_now() || bullet < 0) return false;
    for (size_t i = 0; i < g_evhcFlight.size(); i++) {
        const EvhcFlight& F = g_evhcFlight[i];
        if (F.bullet != bullet || !F.seen || F.ended >= 0) continue;
        if (F.model < 0 || F.model >= (int)g_evhc.size()) return false;
        return evhc_meshes_ok(g_evhc[F.model]);
    }
    return false;
}

/* THE FLYING ROCKET DRAWN: the model's own rocket piece, in its paint and at its size,
   its nose at the flight's point and its line along the flight's, kept upright about its
   line (no roll), in every pass the wrecks are drawn in. */
static void evhc_draw_flights(int pass)
{
    if (g_evhcFlight.empty() || pass == MODE_SHADOW || !evhc_rule_now()) return;
    for (size_t i = 0; i < g_evhcFlight.size(); i++) {
        EvhcFlight& F = g_evhcFlight[i];
        if (!F.seen || F.ended >= 0 || F.model < 0 || F.model >= (int)g_evhc.size()) continue;
        EvhcModel& V = g_evhc[F.model];
        if (!evhc_meshes_ok(V) || !evhc_resident(V, false)) continue;
        const int mi = V.m.mesh[EBM_INTACT];
        if (pass != MODE_OPAQUE && pass >= 0 && pass < 32
            && !(g_pack.mesh[mi].modeMask & (1u << pass)))
            continue;
        const int rp = evhc_v2_rocket_part(V);
        if (rp < 0) continue;
        const int* r = &V.m.rng[EBM_INTACT][V.part[rp].piece * EBR];
        if (r[EBR_B] <= r[EBR_A]) continue;
        float nose[3], d[3];
        evhc_v2_flight_pose(F, g_tickAlpha, nose, d);
        /* the rotation taking the rocket's rest frame (its line, up off it, the side) onto
           the flight's */
        const float* a = F.a0;
        float u0[3] = { -a[1] * a[0], 1.0f - a[1] * a[1], -a[1] * a[2] };
        float u1[3] = { -d[1] * d[0], 1.0f - d[1] * d[1], -d[1] * d[2] };
        float l0 = sqrtf(u0[0] * u0[0] + u0[1] * u0[1] + u0[2] * u0[2]);
        float l1 = sqrtf(u1[0] * u1[0] + u1[1] * u1[1] + u1[2] * u1[2]);
        if (l0 < 1e-4f || l1 < 1e-4f) continue;
        for (int k = 0; k < 3; k++) { u0[k] /= l0; u1[k] /= l1; }
        const float s0[3] = { u0[1] * a[2] - u0[2] * a[1], u0[2] * a[0] - u0[0] * a[2],
                              u0[0] * a[1] - u0[1] * a[0] };
        const float s1[3] = { u1[1] * d[2] - u1[2] * d[1], u1[2] * d[0] - u1[0] * d[2],
                              u1[0] * d[1] - u1[1] * d[0] };
        float Mx[12];
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 3; col++)
                Mx[row * 4 + col] = s1[row] * s0[col] + u1[row] * u0[col] + d[row] * a[col];
            Mx[row * 4 + 3] = -(Mx[row * 4] * F.n0[0] + Mx[row * 4 + 1] * F.n0[1]
                                + Mx[row * 4 + 2] * F.n0[2]);
        }
        const float lift = nose[1] - terrain_y(nose[0], nose[2]);
        draw_mesh(mi, nose[0], nose[2], -1, pass, 0, 0.0f, F.house, 1.0f, lift, false,
                  WOBBLE_NONE, -1.0f, Mx, (size_t)r[EBR_A], (size_t)r[EBR_B]);
        if (pass == MODE_OPAQUE && !ebld_aux_pass()) {
            F.drawn = true;
            F.drawnN++;
            for (int k = 0; k < 3; k++) { F.nose[k] = nose[k]; F.dir[k] = d[k]; }
            const float pitch = asinf(d[1] < -1.0f ? -1.0f : d[1] > 1.0f ? 1.0f : d[1])
                                * (180.0f / 3.14159265f);
            F.pitchMax = fmaxf(F.pitchMax, pitch);
            F.pitchMin = fminf(F.pitchMin, pitch);
        }
    }
}

/* Whether a lamp's beam reaches into the view: its lens or the middle and end of its
   beam projected inside the picture, with a margin. */
static bool evhc_lamp_in_view(const float* p, const float* d, float range)
{
    const int W = g_fbW > 0 ? g_fbW : 1280, H = g_fbH > 0 ? g_fbH : 720;
    for (int k = 0; k < 3; k++) {
        const float t = 0.5f * (float)k * range;
        float c = 0.0f, r = 0.0f;
        world_to_screen(p[0] + d[0] * t, p[1] + d[1] * t, p[2] + d[2] * t, W, H, &c, &r);
        if (c > -0.2f * W && c < 1.2f * W && r > -0.2f * H && r < 1.2f * H) return true;
    }
    return false;
}

/* THE GATHER, once a frame before the light pass: every lamp posed, ranked, faded, and
   the chosen ones handed to the light pass's spot slots. */
static void evhc_gather_lamps(void)
{
    g_fxNSpot = 0;
    g_evhcLamp.clear();
    g_evhcLampLit = g_evhcLampShadowed = 0;
    const bool on = g_evhcLampsOn && g_fx.vlight_on && evhc_rule_now() && !g_evhc.empty();
    const float now = engine_time();
    if (g_evhcLampClock < 0.0f || now < g_evhcLampClock) g_evhcLampW.clear();
    const float dt = g_evhcLampW.empty() ? 0.0f : (now - g_evhcLampClock) / 15.0f;
    g_evhcLampClock = now;
    if (!on) { g_evhcLampW.clear(); return; }
    g_fxSpotSteps = g_fx.vlight_shadows ? 10.0f : 0.0f;
    const float camX = g_camX, camZ = g_camZ;
    std::vector<float> mats;
    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        const int m = evhc_for(o);
        if (m < 0 || !visible(o) || o.limbo) continue;
        const EvhcModel& V = g_evhc[m];
        if (V.light.empty()) continue;
        /* out from half strength down, as the lab's damaged model */
        if (o.maxstr > 0 && 2 * o.str <= o.maxstr) continue;
        std::map<int, EvhcUnit>::iterator ui = g_evhcUnit.find(smooth_key(o));
        const EvhcUnit* U = (ui != g_evhcUnit.end() && ui->second.model == m) ? &ui->second : NULL;
        EvhcPose P;
        evhc_pose_of(V, o, U, P);
        evhc_pose_all(V, P, U, mats);
        for (size_t li = 0; li < V.light.size() && li < 64; li++) {
            const EvhcLight& L = V.light[li];
            const int pi = (L.piece >= 0 && L.piece < (int)V.partOf.size()) ? V.partOf[L.piece] : -1;
            const float* M = evhc_piece_pose(V, mats, L.piece);
            EvhcLamp E;
            E.key = (long long)smooth_key(o) * 64 + (long long)li;
            E.model = m; E.light = (int)li;
            evhc_lamp_world(o, M, L.pos, true, E.p);
            evhc_lamp_world(o, M, L.dir, false, E.d);
            evhc_lamp_world(o, M, L.lensN, false, E.n);
            const bool head = L.kind == EVL_HEAD;
            const float dial = head ? g_fx.vlight_head : g_fx.vlight_tail;
            const float s = L.intensity * dial;
            for (int c = 0; c < 3; c++) E.col[c] = L.col[c] * s * EVHC_LAMP_GAIN;
            const float cs = g_fx.vlight_cone > 0.0f ? g_fx.vlight_cone : 1.0f;
            float ao = L.cone[1] * cs, ai = L.cone[0] * cs;
            if (ao > 89.0f) ao = 89.0f;
            if (ai > ao) ai = ao;
            E.cosO = cosf(ao * (3.14159265f / 180.0f));
            E.cosI = cosf(ai * (3.14159265f / 180.0f));
            if (E.cosI - E.cosO < 1e-4f) E.cosI = E.cosO + 1e-4f;
            E.range = L.range * (g_fx.vlight_range > 0.0f ? g_fx.vlight_range : 1.0f);
            E.shadows = head && (L.flags & EVLF_SHADOWS) != 0;
            E.lensR = L.lensR;
            E.opacity = fminf(1.0f, 0.55f + 0.45f * sqrtf(L.intensity));
            E.view = evhc_lamp_in_view(E.p, E.d, E.range);
            const float dx = E.p[0] - camX, dz = E.p[2] - camZ;
            E.score = s * E.range / (1.0f + sqrtf(dx * dx + dz * dz));
            g_evhcLamp.push_back(E);
        }
    }
    /* THE RANK: the lamps in view, best first (ties to the lower key, so every run picks
       the same) */
    std::vector<size_t> rank;
    for (size_t k = 0; k < g_evhcLamp.size(); k++)
        if (g_evhcLamp[k].view && g_evhcLamp[k].score > 0.0f) rank.push_back(k);
    struct ByScore {
        static bool less(const EvhcLamp& a, const EvhcLamp& b)
        { return a.score > b.score || (a.score == b.score && a.key < b.key); }
    };
    for (size_t a = 1; a < rank.size(); a++)
        for (size_t b = a; b > 0 && ByScore::less(g_evhcLamp[rank[b]], g_evhcLamp[rank[b - 1]]); b--) {
            const size_t t = rank[b]; rank[b] = rank[b - 1]; rank[b - 1] = t;
        }
    const size_t want = (size_t)(FX_MAX_SPOTS - EVHC_LAMP_SPARE);
    std::map<long long, float> next;
    const float step = dt / EVHC_LAMP_FADE;
    for (size_t k = 0; k < g_evhcLamp.size(); k++) {
        const EvhcLamp& E = g_evhcLamp[k];
        bool chosen = false;
        for (size_t r = 0; r < rank.size() && r < want; r++) if (rank[r] == k) { chosen = true; break; }
        if (!E.view) continue;
        std::map<long long, float>::const_iterator wi = g_evhcLampW.find(E.key);
        /* a lamp coming into view takes its rank's weight at once (its beam comes in from
           past the picture's edge); one already in view fades toward it. Kept at 0 while
           in view, so a lamp that later wins a slot fades in rather than popping. */
        float w = chosen ? 1.0f : 0.0f;
        if (wi != g_evhcLampW.end())
            w = chosen ? fminf(1.0f, wi->second + step) : fmaxf(0.0f, wi->second - step);
        next[E.key] = w;
    }
    g_evhcLampW.swap(next);
    /* THE SLOTS: every lamp with any weight, the strongest first */
    std::vector<std::pair<float, size_t> > lit;
    for (size_t k = 0; k < g_evhcLamp.size(); k++) {
        std::map<long long, float>::const_iterator wi = g_evhcLampW.find(g_evhcLamp[k].key);
        if (wi != g_evhcLampW.end() && wi->second > 0.0f)
            lit.push_back(std::make_pair(-wi->second * g_evhcLamp[k].score, k));
    }
    std::sort(lit.begin(), lit.end());
    for (size_t r = 0; r < lit.size() && g_fxNSpot < FX_MAX_SPOTS; r++) {
        const EvhcLamp& E = g_evhcLamp[lit[r].second];
        const float w = g_evhcLampW[E.key];
        FxSpot& S = g_fxSpot[g_fxNSpot++];
        S.x = E.p[0]; S.y = E.p[1]; S.z = E.p[2]; S.range = E.range;
        S.dx = E.d[0]; S.dy = E.d[1]; S.dz = E.d[2];
        S.cosOuter = E.cosO; S.cosInner = E.cosI;
        S.r = E.col[0] * w; S.g = E.col[1] * w; S.b = E.col[2] * w;
        S.shadows = E.shadows ? 1 : 0;
        g_evhcLampLit++;
        if (E.shadows) g_evhcLampShadowed++;
    }
}

/* THE LENS'S GLOW, the lab's: a soft round disc over the lens, facing the way the lens
   faces, added in the lamp's colour; in the colour picture only, after everything
   translucent, depth tested and not written. Every lamp of a vehicle in view glows,
   whether or not its light won a slot. The lab's disc is 1.8 times the lens's radius,
   which is a pixel at the game's closest zoom; here it is EVHC_GLOW_SIZE times that, so a
   lens reads as lit at the distance the game is played from (ours, not the lab's). */
static const float EVHC_GLOW_SIZE = 3.0f;
static GLuint g_evhcGlowTex = 0;
static GLuint evhc_glow_tex(void)
{
    if (g_evhcGlowTex) return g_evhcGlowTex;
    const int n = 64;
    std::vector<unsigned char> d((size_t)n * n * 4);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            const float r = sqrtf((x + 0.5f - n / 2) * (x + 0.5f - n / 2)
                                  + (y + 0.5f - n / 2) * (y + 0.5f - n / 2)) / (n / 2);
            const float f = r < 1.0f ? 1.0f - r : 0.0f;
            float v = (r < 0.45f ? 0.55f : 0.0f) + f * f * 0.9f;
            if (v > 1.0f) v = 1.0f;
            unsigned char* p = &d[((size_t)y * n + x) * 4];
            p[0] = p[1] = p[2] = (unsigned char)(255.0f * v + 0.5f);
            p[3] = 255;
        }
    GLint bound = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    glGenTextures(1, &g_evhcGlowTex);
    glBindTexture(GL_TEXTURE_2D, g_evhcGlowTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, n, n, 0, GL_RGBA, GL_UNSIGNED_BYTE, &d[0]);
    glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
    return g_evhcGlowTex;
}
static void evhc_draw_lamp_glows(void)
{
    g_evhcLampGlows = 0;
    if (g_evhcLamp.empty() || ebld_aux_pass() || g_fx.vlight_glow <= 0.0f) return;
    GLint depthFunc = GL_LEQUAL, bsrc = GL_SRC_ALPHA, bdst = GL_ONE_MINUS_SRC_ALPHA;
    GLboolean depthMask = GL_FALSE;
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    glGetIntegerv(GL_BLEND_SRC, &bsrc);
    glGetIntegerv(GL_BLEND_DST, &bdst);
    const GLboolean blendWas = glIsEnabled(GL_BLEND), cullWas = glIsEnabled(GL_CULL_FACE);
    const GLboolean alphaWas = glIsEnabled(GL_ALPHA_TEST), texWas = glIsEnabled(GL_TEXTURE_2D);
    GLint bound = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, evhc_glow_tex());
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glBegin(GL_QUADS);
    for (size_t k = 0; k < g_evhcLamp.size(); k++) {
        const EvhcLamp& E = g_evhcLamp[k];
        if (!E.view) continue;
        const EvhcLight& L = g_evhc[E.model].light[E.light];
        const float* n = E.n;
        /* two directions across the lens */
        float a[3] = { 0.0f, 1.0f, 0.0f };
        if (fabsf(n[1]) > 0.9f) { a[0] = 1.0f; a[1] = 0.0f; }
        float u[3] = { a[1] * n[2] - a[2] * n[1], a[2] * n[0] - a[0] * n[2], a[0] * n[1] - a[1] * n[0] };
        float ul = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
        if (ul < 1e-6f) continue;
        for (int c = 0; c < 3; c++) u[c] /= ul;
        const float v[3] = { n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0] };
        const float h = (E.lensR > 0.006f ? E.lensR : 0.006f) * 1.8f * EVHC_GLOW_SIZE;
        float c0[3];
        for (int c = 0; c < 3; c++) c0[c] = E.p[c] + n[c] * 0.0015f;
        const float g = g_fx.vlight_glow;
        glColor4f(fminf(1.0f, L.col[0] * g), fminf(1.0f, L.col[1] * g), fminf(1.0f, L.col[2] * g),
                  E.opacity);
        glTexCoord2f(0.0f, 0.0f);
        glVertex3f(c0[0] - u[0] * h - v[0] * h, c0[1] - u[1] * h - v[1] * h, c0[2] - u[2] * h - v[2] * h);
        glTexCoord2f(1.0f, 0.0f);
        glVertex3f(c0[0] + u[0] * h - v[0] * h, c0[1] + u[1] * h - v[1] * h, c0[2] + u[2] * h - v[2] * h);
        glTexCoord2f(1.0f, 1.0f);
        glVertex3f(c0[0] + u[0] * h + v[0] * h, c0[1] + u[1] * h + v[1] * h, c0[2] + u[2] * h + v[2] * h);
        glTexCoord2f(0.0f, 1.0f);
        glVertex3f(c0[0] - u[0] * h + v[0] * h, c0[1] - u[1] * h + v[1] * h, c0[2] - u[2] * h + v[2] * h);
        g_evhcLampGlows++;
    }
    glEnd();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
    if (!texWas) glDisable(GL_TEXTURE_2D);
    if (alphaWas) glEnable(GL_ALPHA_TEST);
    if (cullWas) glEnable(GL_CULL_FACE);
    if (!blendWas) glDisable(GL_BLEND);
    glBlendFunc((GLenum)bsrc, (GLenum)bdst);
    glDepthMask(depthMask);
    glDepthFunc((GLenum)depthFunc);
}

/* evhclamps: the lamps of the last frame: EVHCLAMP the switch and the counts, EVHCLAMPL
   each lamp (its world position, beam, slot weight, whether it is in view). */
static void evhc_lamp_dump(void)
{
    printf("EVHCLAMP|on=%d|lamps=%d|lit=%d|shadowed=%d|glows=%d|slots=%d\n",
           (g_evhcLampsOn && g_fx.vlight_on && evhc_rule_now()) ? 1 : 0, (int)g_evhcLamp.size(),
           g_evhcLampLit, g_evhcLampShadowed, g_evhcLampGlows, FX_MAX_SPOTS);
    for (size_t k = 0; k < g_evhcLamp.size(); k++) {
        const EvhcLamp& E = g_evhcLamp[k];
        const EvhcLight& L = g_evhc[E.model].light[E.light];
        std::map<long long, float>::const_iterator wi = g_evhcLampW.find(E.key);
        printf("EVHCLAMPL|%s|key=%lld|name=%s|kind=%d|at=%.3f,%.3f,%.3f|dir=%.3f,%.3f,%.3f"
               "|ground=%.3f|w=%.3f|view=%d|shadows=%d\n", g_evhc[E.model].m.type.c_str(),
               E.key / 64, L.name, L.kind, E.p[0], E.p[1], E.p[2], E.d[0], E.d[1], E.d[2],
               terrain_y(E.p[0], E.p[2]), wi != g_evhcLampW.end() ? wi->second : 0.0f,
               E.view ? 1 : 0, E.shadows ? 1 : 0);
    }
    fflush(stdout);
}

/* Mission boundary and shutdown. */
static void evhc_reset(void)
{
    g_evhcUnit.clear();
    g_evhcWreck.clear();
    g_evhcLastTick = -1;
    g_evhcWreckSerial = 0;
    g_evhcLamp.clear();
    g_evhcLampW.clear();
    g_evhcLampClock = -1.0f;
}

/* ================================================================================
 * THE DUMP. evhcdump: the models, every unit this system holds and what the colour
 * picture last drew of it. The event lines (fire, death, impact, sink, gone) print as
 * they happen.
 * ============================================================================== */
/* Corners of the standing model whose normal is not their face's own: the smooth
   normals the file carries, counted for the dump (0 for a version 1 file). */
static int evhc_smooth_corners(const EvhcModel& V)
{
    const EbldSet& S = V.m.set[EBS_INTACT];
    int n = 0;
    for (size_t t = 0; t < S.tri.size(); t++) {
        const EbldTri& T = S.tri[t];
        const float e1[3] = { T.v[1].x - T.v[0].x, T.v[1].y - T.v[0].y, T.v[1].z - T.v[0].z };
        const float e2[3] = { T.v[2].x - T.v[0].x, T.v[2].y - T.v[0].y, T.v[2].z - T.v[0].z };
        float f[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                       e1[0] * e2[1] - e1[1] * e2[0] };
        const float L = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        if (!(L > 1e-12f)) continue;
        for (int k = 0; k < 3; k++) {
            const float d = (T.v[k].nx * f[0] + T.v[k].ny * f[1] + T.v[k].nz * f[2]) / L;
            if (d < 0.9995f) n++;              /* more than about 1.8 degrees off its face */
        }
    }
    return n;
}

static void evhc_dump(void)
{
    int attached = 0;
    for (size_t i = 0; i < g_evhc.size(); i++) if (g_evhc[i].m.attached) attached++;
    printf("EVHC|loaded=%d|attached=%d|on=%d|enhanced=%d\n", (int)g_evhc.size(), attached,
           evhc_rule_now() ? 1 : 0, g_fx.enabled ? 1 : 0);
    /* what the last frame drew into the normal target: units and aircraft (riders among
       them), vehicles in a rig, wrecks */
    printf("EVHCNRM|on=%d|objects=%ld|rigs=%ld|wrecks=%ld\n",
           (g_evhcNrmOn && evhc_rule_now()) ? 1 : 0, g_evhcNrmObjs, g_evhcNrmRigs,
           g_evhcNrmWrecks);
    /* which types carry a specular map, and what the last frame drew into its target */
    {
        std::string types;
        for (size_t i = 0; i < g_evhc.size(); i++)
            if (g_evhc[i].m.hasSpec) types += (types.empty() ? "" : ",") + g_evhc[i].m.type;
        int bt = 0;
        for (size_t i = 0; i < g_ebld.size(); i++) if (g_ebld[i].hasSpec) bt++;
        printf("EVHCSPEC|on=%d|panel=%d|types=%s|objects=%ld|rigs=%ld|frames=%d|buildingtypes=%d|buildings=%ld\n",
               g_evhcSpecOn ? 1 : 0, g_fx.vspec_on, types.empty() ? "-" : types.c_str(),
               g_evhcSpecObjs, g_evhcSpecRigs, g_evhcSpecFrames, bt, g_ebldSpecObjs);
    }
    static const char* const CLS[7] = { "tracked", "wheeled", "half-track", "helicopter",
                                        "fixed-wing", "hovercraft", "boat" };
    for (size_t i = 0; i < g_evhc.size(); i++) {
        EvhcModel& V = g_evhc[i];
        const EbldModel& M = V.m;
        int nk[EVP_N];
        for (int k = 0; k < EVP_N; k++) nk[k] = 0;
        for (size_t k = 0; k < V.part.size(); k++) nk[V.part[k].kind]++;
        /* where the remaster lies against the cartridge model it stands in for, pack units:
           x west to east, z north to south, and the highest y */
        float b[2][5];
        for (int w = 0; w < 2; w++) { b[w][0] = b[w][2] = b[w][4] = -1e9f; b[w][1] = b[w][3] = 1e9f; }
        if (M.attached) {
            const EbldSet& S = M.set[EBS_INTACT];
            for (size_t t = 0; t < S.tri.size(); t++)
                for (int q = 0; q < 3; q++) {
                    const EbldVert& v = S.tri[t].v[q];
                    if (v.x < b[0][1]) b[0][1] = v.x;
                    if (v.x > b[0][0]) b[0][0] = v.x;
                    if (v.z < b[0][3]) b[0][3] = v.z;
                    if (v.z > b[0][2]) b[0][2] = v.z;
                    if (v.y > b[0][4]) b[0][4] = v.y;
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
        printf("EVHCART|%s|class=%s|pieces=%d|wreck=%d|turret=%d|belt=%d|wflip=%d|wheel=%d"
               "|rotor=%d|spin=%d|hinge=%d|clip=%d|deploy=%d|recoil=%d|tex=%d|drawn=%ld"
               "|x=%.0f,%.0f|z=%.0f,%.0f|top=%.0f|cartx=%.0f,%.0f|cartz=%.0f,%.0f|carttop=%.0f"
               "|rig=%d|rigdrawn=%ld|fmt=%d|smooth=%d|lights=%d|riglift=%.1f"
               "|hoop=%d|righoop=%.3f|rigtank=%.1f|hoopmid=%ld|hoopfull=%ld|hoopbad=%ld\n",
               M.type.c_str(), CLS[V.cls >= 0 && V.cls < 7 ? V.cls : 0],
               (int)M.set[EBS_INTACT].piece.size(), (int)M.set[EBS_DESTROYED].piece.size(),
               nk[EVP_TURRET], nk[EVP_BELT], nk[EVP_WFLIP], nk[EVP_WHEEL], nk[EVP_ROTOR],
               nk[EVP_SPIN], nk[EVP_HINGE], nk[EVP_CLIP], nk[EVP_DEPLOY], nk[EVP_RECOIL],
               (int)M.tex.size(), V.drawn, b[0][1], b[0][0], b[0][3], b[0][2], b[0][4],
               b[1][1], b[1][0], b[1][3], b[1][2], b[1][4], V.rigOk ? 1 : 0, V.rigDrawn,
               V.fileVer, evhc_smooth_corners(V), (int)V.light.size(), V.rigLift,
               V.hoopPart >= 0 ? 1 : 0, V.rigHoop, V.rigTank, V.rigHoopMid, V.rigHoopFull,
               V.rigHoopBad);
        V.drawn = 0;
        V.rigDrawn = 0;
        V.rigLift = -1.0f;
        V.rigHoop = V.rigTank = -1.0f;
        V.rigHoopMid = V.rigHoopFull = V.rigHoopBad = 0;
    }
    for (std::map<int, EvhcUnit>::iterator it = g_evhcUnit.begin(); it != g_evhcUnit.end(); ++it) {
        EvhcUnit& U = it->second;
        printf("EVHCOBJ|%s|key=%d|id=%d|drawn=%d|tur=%d|trav=%.4f,%.4f|frame=%d,%d|turn=%.3f"
               "|rotor=%.4f|disc=%.2f|duct=%.1f|elev=%.1f|door=%.2f|shots=%d|kick=%.3f|clip=%.1f"
               "|wob=%.4f,%.4f\n",
               U.type, it->first, U.id, U.drawnNow ? 1 : 0, U.dTur, U.dTrav[1], U.dTrav[0],
               U.dFrame[1], U.dFrame[0], U.dTurn, U.dRotor, U.dDisc, U.dDuct, U.dElev,
               U.dDoor, U.shots, U.dKick, U.dClip, U.dWob[0], U.dWob[1]);
        if (!g_raModule && !strcmp(U.type, "HARV"))
            printf("EVHCSUSP|HARV|on=%d|heave=%.6f|pitch=%.6f|roll=%.6f|speed=%.5f\n",
                   g_evhcHarvSuspOn ? 1 : 0, U.dSuspension[0], U.dSuspension[1],
                   U.dSuspension[2], U.suspension.speed);
        /* A SUBMARINE'S DIVE: the engine's cloak state and stage it was last drawn at, and
           how far under the sea that put it */
        if (U.model >= 0 && U.model < (int)g_evhc.size()
            && g_evhc[U.model].m.param.size() >= (size_t)EVQ_N_DIVE)
            printf("EVHCDIVE|%s|key=%d|id=%d|drawn=%d|cloak=%d|cstage=%d|dive=%.4f|ghosts=%ld\n",
                   U.type, it->first, U.id, U.drawnNow ? 1 : 0, U.dCloak, U.dCstage, U.dDive,
                   g_evhcGhosts);
        /* THE HARVESTER'S CLAWS: whether it was harvesting on the last tick, the phase then
           (0 lowered .. 1 raised) and the lift and lower jaw it gives, degrees; the same as
           last drawn; how far each lift ram's rod eye stood from the boom's pin it rides,
           pack units, at the drawn pose (the rams follow the booms, so it stays near 0);
           and the first hub cap's roll, radians, from its side's travel */
        if (U.model >= 0 && U.model < (int)g_evhc.size() && g_evhc[U.model].liftPart >= 0) {
            const EvhcModel& V = g_evhc[U.model];
            const EvhcPart& lp = V.part[V.liftPart];
            const EvhcPart* jp = NULL;
            for (size_t k = 0; k < V.part.size(); k++)
                if (V.part[k].kind == EVP_HINGE && V.part[k].driver == EVD_HARV_JAW)
                    jp = &V.part[k];
            EvhcPose P;
            memset(&P, 0, sizeof P);
            P.clipT = -1.0f;
            P.cartMi = -1;
            P.claw = U.dClaw;
            std::vector<float> mats;
            evhc_pose_all(V, P, NULL, mats);
            float gap = 0.0f;
            int rams = 0;
            for (size_t k = 0; k < V.part.size(); k++) {
                const EvhcPart& rp = V.part[k];
                if (rp.kind != EVP_HINGE || rp.driver != EVD_HARV_ROD) continue;
                float a[3], b[3];
                ev_apply(&mats[k * 12], rp.a + 8, a);
                ev_apply(&mats[V.liftPart * 12], rp.a + 8, b);
                const float d = sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1])
                                      + (a[2] - b[2]) * (a[2] - b[2]));
                if (d > gap) gap = d;
                rams++;
            }
            float roll = 0.0f;
            for (size_t k = 0; k < V.part.size(); k++)
                if (V.part[k].kind == EVP_WHEEL) {
                    const float r = V.part[k].a[6] > 1.0f ? V.part[k].a[6] * MODEL_SCALE : 0.05f;
                    roll = U.dTrav[V.part[k].side == 0 ? 0 : 1] / r;
                    break;
                }
            printf("EVHCHARV|%s|key=%d|id=%d|on=%d|harvesting=%d|claw=%.3f|lift=%.1f|jaw=%.1f"
                   "|drawnclaw=%.3f|drawnlift=%.1f|drawnjaw=%.1f|rams=%d|ramgap=%.3f|roll=%.3f\n",
                   U.type, it->first, U.id, g_evhcHarvLiftOn ? 1 : 0, U.clawHarv ? 1 : 0, U.claw,
                   evhc_claw_lift(lp, U.claw), jp ? evhc_claw_jaw(*jp, U.claw) : -1.0f, U.dClaw,
                   evhc_claw_lift(lp, U.dClaw), jp ? evhc_claw_jaw(*jp, U.dClaw) : -1.0f, rams,
                   gap, roll);
        }
        /* Red Alert's Ore Truck: what the engine said (doing 1 lifting, 2 dumping, the 1996
           frame) and where the scoop and the body stand; how full its bed is drawn and the
           fill its pips ask for */
        bool ore = false, v2 = false, lst = false;
        if (g_raModule && U.model >= 0 && U.model < (int)g_evhc.size())
            for (size_t k = 0; k < g_evhc[U.model].part.size(); k++) {
                const EvhcPart& p = g_evhc[U.model].part[k];
                if (p.kind != EVP_HINGE) continue;
                if (p.driver >= EVD_ORE_SCOOP && p.driver <= EVD_ORE_LOAD) ore = true;
                if (p.driver >= EVD_V2_ERECTOR && p.driver <= EVD_V2_ROD) v2 = true;
                if (p.driver == EVD_RA_RAMP) lst = true;
            }
        if (ore)
            printf("EVHCORE|%s|key=%d|doing=%d|frame=%d|scoop=%.3f|lift=%.3f|tip=%.3f"
                   "|fill=%.3f|fillwant=%.3f\n",
                   U.type, it->first, U.oreDoing, U.oreFrame, U.scoop, U.lift, U.tip,
                   U.fillSet ? U.fill : -1.0f, U.fillSet ? U.fillWant : -1.0f);
        /* Red Alert's V2 Rocket: what the engine said (doing 3 while it stands to fire, its
           ammo) and where the erector stood and whether the rocket was on it as last drawn */
        if (v2)
            printf("EVHCV2|%s|key=%d|doing=%d|firein=%d|ammo=%d|erect=%.3f|drawnerect=%.3f"
                   "|lift=%.1f|rocket=%d|launches=%d\n",
                   U.type, it->first, U.v2Doing, U.v2FireIn, U.v2Ammo, U.erect, U.dErect,
                   evhc_v2_lift_of(g_evhc[U.model]) * U.dErect, U.dRocket ? 1 : 0,
                   evhc_v2_launches_of(it->first));
        /* Red Alert's Transport: the engine's door, the heading it is drawn at and where each
           ramp stands */
        if (lst)
            printf("EVHCLST|%s|key=%d|door=%d|far=%d|face=%d|ramps=%.1f,%.1f,%.1f,%.1f\n",
                   U.type, it->first, U.lstDoor, U.lstFar, U.lstFace, U.ramp[0], U.ramp[1],
                   U.ramp[2], U.ramp[3]);
        /* Red Alert's landing gear: the altitude the engine said on the last tick, how far
           the gear stood stowed then and as last drawn (0 down .. 1 up), and the angle that
           drew each gear hinge at, degrees, in the file's order */
        if (g_raModule && U.model >= 0 && U.model < (int)g_evhc.size() && g_evhc[U.model].gear) {
            const EvhcModel& GV = g_evhc[U.model];
            char angs[160] = "";
            size_t n = 0;
            for (size_t k = 0; k < GV.part.size() && n + 12 < sizeof angs; k++) {
                const EvhcPart& p = GV.part[k];
                if (p.kind != EVP_HINGE || p.driver != EVD_GEAR) continue;
                n += (size_t)snprintf(angs + n, sizeof angs - n, "%s%.1f", n ? "," : "",
                                      p.a[6] * evhc_ease01(U.dGear));
            }
            printf("EVHCGEAR|%s|key=%d|id=%d|alt=%d|gear=%.3f|drawngear=%.3f|angles=%s\n",
                   U.type, it->first, U.id, U.gearAlt, U.gear, U.dGear, angs);
        }
        /* each part turning at its own rate (a radar dish, the Gap Generator's fork): its
           rate, the engine time it stands at now, and how far it is turned from its rest,
           degrees counterclockwise seen from above (the draw's turn of -rpm about its axis,
           projected on the upright), with the hull's engine facing */
        if (U.model >= 0 && U.model < (int)g_evhc.size()) {
            const EvhcModel& V = g_evhc[U.model];
            const float now = engine_time();
            for (size_t k = 0; k < V.part.size(); k++) {
                const EvhcPart& p = V.part[k];
                if (p.kind != EVP_SPIN || p.a[7] > 0.0f) continue;
                double turns = (double)now / 15.0 * (double)p.a[6] / 60.0;
                turns -= floor(turns);
                double ccw = -turns * 360.0 * (double)p.a[4];
                ccw -= 360.0 * floor(ccw / 360.0);
                printf("EVHCSPIN|%s|key=%d|id=%d|part=%d|rpm=%.3f|t=%.3f|ccw=%.3f|face=%d\n",
                       U.type, it->first, U.id, (int)k, p.a[6], now, ccw, U.face);
            }
        }
        U.drawnNow = false;
    }
    /* the V2's rockets in flight: drawn since the last dump, where and how pitched, and the
       V2 projectile cards drawn under Enhanced against those stood down for a rocket */
    for (size_t i = 0; i < g_evhcFlight.size(); i++) {
        EvhcFlight& F = g_evhcFlight[i];
        const float pitch = asinf(F.dir[1] < -1.0f ? -1.0f : F.dir[1] > 1.0f ? 1.0f : F.dir[1])
                            * (180.0f / 3.14159265f);
        printf("EVHCFLY|bullet=%d|key=%d|launch=%d|seen=%d|ended=%d|drawn=%d|frames=%d"
               "|nose=%.3f,%.3f,%.3f|ground=%.3f|pitch=%.1f|pitchmax=%.1f|pitchmin=%.1f"
               "|target=%d|tip=%.3f,%.3f,%.3f\n",
               F.bullet, F.key, F.frame, F.seen ? 1 : 0, F.ended, F.drawn ? 1 : 0, F.drawnN,
               F.nose[0], F.nose[1], F.nose[2], terrain_y(F.nose[0], F.nose[2]), pitch,
               F.pitchMax, F.pitchMin, F.target ? 1 : 0, F.tip[0], F.tip[1], F.tip[2]);
        F.drawn = false;
    }
    if (g_raModule)
        printf("EVHCFLYCARDS|v2cards=%d|standdown=%d\n", g_evhcV2Cards, g_evhcV2StandDown);
    for (std::map<long long, EvhcWreck>::const_iterator it = g_evhcWreck.begin();
         it != g_evhcWreck.end(); ++it) {
        const EvhcWreck& E = it->second;
        int asleep = 0;
        float top = -1e9f, low = 1e9f;
        for (size_t i = 0; i < E.body.size(); i++) {
            if (E.body[i].asleep) asleep++;
            if (E.body[i].p[1] > top) top = E.body[i].p[1];
            if (E.body[i].p[1] < low) low = E.body[i].p[1];
        }
        printf("EVHCWRECK|%s|key=%lld|air=%d|falling=%d|sinking=%d|pieces=%d|asleep=%d"
               "|high=%.3f|low=%.3f|ground=%.3f|wet=%d|at=%.2f,%.2f\n", E.type, it->first,
               E.air ? 1 : 0, E.falling ? 1 : 0, E.sinking ? 1 : 0, (int)E.body.size(), asleep,
               top, low, E.ground, E.wet ? 1 : 0, E.wx, E.wz);
    }
    fflush(stdout);
}
#endif
