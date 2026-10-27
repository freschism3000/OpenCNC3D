/* ====================================================================================
 *  fx_post.h -- the Tier 2 post chain: sun shadows, occlusion, dynamic light, bloom,
 *               grade, the VI gamma, supersampling and the CRT.
 *
 *  TIER 2 ONLY. See the header of fx_gl.h and the row this feature owns in
 *  docs/tier1-gap.md. On Win98 none of this runs and the picture is the shipped one.
 *
 *  ---------------------------------------------------------------------------------
 *  THE SHAPE OF IT, because the order is the whole design.
 *
 *      draw_frame()
 *        fx_world_begin()      bind sceneRT (ss x the window), viewport scaled
 *        ... every existing world pass, UNCHANGED ...
 *        fx_world_end()        the chain runs, result lands in a NATIVE-sized target
 *        ... sidebar, radar, band select, cursor: native pixels, untouched ...
 *        fx_present()          blit to the real framebuffer, CRT if asked
 *
 *  Two things fall out of that and both are deliberate:
 *
 *  1. NOT ONE WORLD DRAW CALL CHANGES. The chain reads the finished colour and depth
 *     buffers. That is why a feature this size can be added to a renderer measured
 *     this carefully: every existing gate draws the identical geometry in the
 *     identical order, and with the chain off it draws it to the identical place.
 *
 *  2. THE SIDEBAR IS NEVER POST-PROCESSED. It is 1995 pixel art at 1:1 and blooming
 *     or occluding it would be vandalism. The ONE exception is the CRT, which covers
 *     the sidebar when crt_hud is set, because a real tube does not stop at the edge
 *     of the tactical view. That is a switch, not an assumption.
 *
 *  ---------------------------------------------------------------------------------
 *  WHAT IS OURS HERE, stated plainly because this project's rule is that invention
 *  gets named rather than smuggled:
 *
 *  EVERYTHING except the VI gamma. The sun does not exist in the cartridge -- no N64
 *  model carries dynamic shadow geometry, the console's own shadows are the baked
 *  quads this renderer already draws -- so a shadow map is an addition, and when it is
 *  on the cartridge's four shadow mechanisms are switched OFF rather than drawn twice.
 *  Occlusion, dynamic light, bloom, the grade and the CRT are likewise ours.
 *
 *  The VI gamma is not ours: VI_CTRL = 0x0000320E sets GAMMA_ON in all four of the
 *  cartridge's mode structs and nothing clears it. Applying it is closing a gap this
 *  project has had recorded against itself since wave 4.
 *
 *  DETERMINISM. Every pass is a pure function of the CURRENT frame: no temporal
 *  accumulation, no history buffer, and no clock the chain reads for itself. The
 *  visibility kernels have fixed orientations and reconstruct depth at texel centres.
 *  The cloud deck DOES move, and it is the one thing here that
 *  advances with time: it is driven by g_fxTime, which the host sets from the engine
 *  tick and not from the wall or from a frame count, so the same scenario at the same
 *  tick is the same picture. Two --shot runs stay byte identical with the chain on,
 *  and gate G36c proves it -- five runs, one digest.
 *
 *  (That reference used to read G32, which is the move-order marker and has nothing to
 *  do with this file. Corrected rather than left standing: a cross-reference to the
 *  wrong gate is worse than none, because it invites someone to trust a green G32.)
 * ==================================================================================== */
#ifndef OPENCNC3D_FX_POST_H
#define OPENCNC3D_FX_POST_H

#include "fx_gl.h"
#include "fx_state.h"
#include "fx_cloud.h"
#include "rain_glsl.h"   /* the drop-impact ripple, shared with the water surface */

/* ---- What the host hands us ------------------------------------------------------ */

#define FX_MAX_LIGHTS 24

struct FxLight {
    float x, y, z;      /* world cells, y is up   */
    float radius;       /* cells                  */
    float r, g, b;      /* colour, 0..1           */
    float power;        /* intensity multiplier   */
};

static FxLight g_fxLight[FX_MAX_LIGHTS];
static int     g_fxNLight = 0;

/* THE VEHICLES' LAMPS (evhc_mod.h): spot lights, apart from the 24 point lights above,
   with slots of their own. Each is a cone: full strength inside its inner half angle,
   nothing past its outer one (a smoothstep between the two cosines), fading to nothing
   at its range as (1 - (d / range)^2)^2, and taking a wrapped Lambert, (N.L + 0.5) / 1.5;
   what a surface receives is ADDED as that surface's own colour times the light, as the
   remaster lab draws it, so a headlight on dark ground lights it and does not paint it.
   The colour carries the lamp's strength, the lab's gain and its fade in and out. A lamp
   that may cast shadows (a headlight) is stopped by whatever stands between it and the
   surface, found by a short march through the depth buffer toward the lamp. */
#define FX_MAX_SPOTS 48
struct FxSpot {
    float x, y, z, range;        /* world cells */
    float dx, dy, dz;            /* the way it points, unit */
    float cosOuter, cosInner;    /* of the half angles */
    float r, g, b;               /* linear colour times strength */
    int   shadows;
};
static FxSpot g_fxSpot[FX_MAX_SPOTS];
static int    g_fxNSpot = 0;
static float  g_fxSpotSteps = 10.0f;     /* the shadow march's steps, 0 for none */

/* The shadow-caster callback. cnc_eyes.cpp sets this to a function that re-draws the
   opaque and cutout world from whatever matrices are current. It is called ONCE per
   frame, into a depth-only target, with colour writes already off. */
typedef void (*FxCasterFn)(void);
static FxCasterFn g_fxCasters = 0;
/* The host's ground-normal draw (fx_draw_terrain_normals), rendered by fx_world_end
   into g_fxGNorm and read by the light pass for ground pixels. */
static FxCasterFn g_fxGroundNormals = 0;
static FxRT g_fxGNorm;
/* THE BAKE TARGET: what the CARTRIDGE'S OWN LIGHTING did to each mesh pixel.
   The world pass writes texture times a per-vertex colour, and that colour is the
   Nintendo 64's baked shade. This target holds the shade alone, from a second draw of
   the same meshes with the textures unbound, which is the one piece of information
   needed to undo the console's lighting and put a real one in its place:

       forward colour = albedo x bakedShade      so      albedo = colour / bakedShade

   Alpha is the class and is 1 exactly where a mesh wrote. Everything else in the frame
   -- terrain, sprites, water, every decal and every effect -- leaves it at zero and is
   passed through untouched, which is what keeps this from erasing the things drawn on
   top of the world. */
static FxRT g_fxBake;
static FxCasterFn g_fxMeshBake = 0;
/* THE SPECULAR TARGET: per pixel of a remastered vehicle that carries a specular map, how
   strongly it reflects (red) and how tight its highlight is (green), alpha 1 where one
   drew (evhc_draw_spec, evhc_mod.h). Drawn against the scene's depth like the two above,
   only when g_fxSpecWant says a vehicle with a map is loaded, so a frame without one
   allocates nothing and lights as it did. Tier 2 only. */
static FxRT g_fxSpec;
static FxCasterFn g_fxSpecDraw = 0;
static bool (*g_fxSpecWant)(void) = 0;
static float g_fxSunR = 1.0f;           /* the sun box's half-width, in cells */
/* THE SUN BOX AS NUMBERS, not just as a matrix. fx_sun_matrix already builds an eye, an
   orthonormal basis and a near/far pair on its way to the matrix; keeping them means the
   HOST can ask whether a given caster is inside the box before it submits one, which is
   the only cull a shadow pass may use. The camera's own frustum is the wrong question
   here: a wood standing behind the player still casts into the frame in front of them. */
static float g_fxSunEye[3] = { 0.0f, 0.0f, 0.0f };
static float g_fxSunS[3]   = { 1.0f, 0.0f, 0.0f };
static float g_fxSunU[3]   = { 0.0f, 1.0f, 0.0f };
static float g_fxSunNear = 0.0f, g_fxSunFar = 0.0f;

/* WHICH PASS IS ASKING FOR THE CASTERS. g_fxCasters is called from two places with two
   entirely different matrices loaded -- the sun's orthographic box, and the camera's own
   projection with the modelview mirrored about the sea -- and until the host is told
   which, it cannot cull either one. Set immediately before the call and left alone
   otherwise; the sun is the default because it is the one that existed first. */
#define FXCAST_SUN     0
#define FXCAST_REFLECT 1
static int   g_fxCasterPass   = FXCAST_SUN;
static float g_fxCasterPlaneY = 0.0f;    /* the sea's plane, for FXCAST_REFLECT */

/* The ground rectangle the camera can currently see, in cells, set by the host from
   its own view_ground_bounds. The sun's box is fitted to this, which is what keeps the
   shadow texels dense no matter how far the player has zoomed out. */
static float g_fxViewX0 = 0, g_fxViewX1 = 64, g_fxViewZ0 = 0, g_fxViewZ1 = 64;

/* THE ONLY CLOCK THE CHAIN IS ALLOWED, in seconds, and it is the host's job to make it
   a deterministic one. Nothing in here calls SDL_GetTicks or counts rendered frames.

   THIS FIELD IS WHERE THE DETERMINISM PROMISE AT THE TOP OF THIS FILE IS KEPT OR LOST,
   so it is worth being exact about. The cloud deck has to move, and a moving thing
   needs a clock; the rule is not that the chain may not read time, it is that the
   chain may not read a clock a second run of the same input cannot reproduce. The host
   sets this from the ENGINE frame -- the brain's own Frame counter, which advances once
   per simulated tick and not once per drawn frame -- so two --shot runs of the same
   scenario at the same tick get the same number, and the clouds do not speed up on a
   faster machine either. A rendered-frame counter would satisfy neither. */
static float g_fxTime = 0.0f;

/* WHETHER THE LAST LIGHT PASS TOOK THE STRUCTURES GROUP'S PATH (fx_state.h, 12p): one of
   its dials off neutral and a normal target to tell the structures' pixels by. Read by
   the host's dump line, never by the chain. */
static int g_fxStructOn = 0;
static int g_fxVehicleOn = 0;
/* Optional card mask: Red Alert sprites keep their existing underlying normals. */
static void (*g_fxVehicleCards)(void) = NULL;
static FxRT g_fxVehicleMask;

/* ---- Internal state -------------------------------------------------------------- */

/* HIGH RANGE: WANTED, AND SEPARATELY, ACHIEVED. The dial says what the player asked
   for; g_fxHdrLive says what the driver gave, and the tonemap keys off the second. A
   machine that cannot make a float target gets the picture it had before rather than a
   tonemap applied to values that were clipped at white before it ever saw them, which
   would be worse than not having the feature. */
static int   g_fxHdrLive = 0;
static int   g_fxHdrSaid  = 0;   /* the report above has been made at least once */
static inline int fx_hdr_want(void);

static FxRT  g_fxScene;                 /* world colour + depth, ss resolution        */
static FxRT  g_fxLit;                   /* after shadow/AO/light, ss resolution       */
// Actual luminous explosion-particle isolation, sharing scene depth. No world grade changes.
static FxRT g_fxExplosion;
static FxCasterFn g_fxExplosionDraw = 0;
static bool (*g_fxExplosionWant)(void) = 0;
static FxRT  g_fxBloomA, g_fxBloomB;    /* half of ss, ping-pong                      */
static FxRT  g_fxNativeA, g_fxNativeB;  /* window resolution                          */
static FxRT  g_fxShadow;                /* depth only, square                         */
static FxRT  g_fxTerrainShadow;         /* terrain-only depth when its strength differs */
static int   g_fxTerrainShadowLive = 0;
/* Moves whenever that target is made again or freed, so a target that is new (its
   contents undefined, its name perhaps a recycled one) can never pass for the one the
   host's terrain cache last drew into (terrain_shadow_cached). */
static unsigned g_fxTerrainShadowSerial = 0;
/* THE 3D SOLDIERS' SUN MAP (fx_state.h, m3inf_shadow). The host sets g_fxInfWant each
   frame and supplies g_fxInfCasters, which draws the posed soldiers and nothing else;
   g_fxInfLive says the map was drawn this frame, which is what the shared caster pass
   reads to leave the soldiers out of its own map. */
static FxCasterFn g_fxInfCasters = 0;
static int   g_fxInfWant = 0, g_fxInfLive = 0;
static int   g_fxInfSoldiers = 0;       /* 3D soldiers may be drawn this frame at all */
static FxRT  g_fxInfShadow;
static float g_fxInfSunVP[16];
static float g_fxInfR = 1.0f, g_fxInfSpan = 1.0f;

static GLuint g_fxPLight, g_fxPBright, g_fxPBlur, g_fxPResolve, g_fxPCrt, g_fxPCopy;

/* The cloud mask. One texture for the life of the process: it is generated arithmetic,
   not art, so nothing a dial does can invalidate it -- scale, rotation, drift, coverage
   and edge width are all applied when it is READ. See fx_cloud.h. */
static GLuint g_fxCloudTex = 0;

static int   g_fxBooted = 0;        /* booted for this mission                        */
static int   g_fxBuilt  = 0;        /* programs compiled, once per process            */
static int   g_fxUsable = 0;        /* the chain can run at all                       */
static int   g_fxActive = 0;        /* it IS running this frame (world begin said so) */
static int   g_fxHudRT  = 0;        /* which native RT the HUD is being drawn into    */

static bool fx_unified_lighting(void)
{ return g_fxActive && g_fx.unified_lighting && !g_fx.dos_classic; }
static float fx_sun_energy(void) { return fx_unified_lighting() ? g_fx.sun_energy : 1.0f; }
static float fx_sky_energy(void) { return fx_unified_lighting() ? g_fx.sky_energy : 1.0f; }
static int   g_fxW = 0, g_fxH = 0;  /* the native window size this frame              */

/* Camera matrices captured after the world's own set_camera, for the depth-to-world
   reconstruction every screen-space pass needs. */
static float g_fxVP[16], g_fxInvVP[16], g_fxCamPos[3];
static float g_fxSunVP[16], g_fxSunDir[3];

/* How many WORLD CELLS the sun's depth buffer spans, end to end. The shadow bias is a
   distance in cells on the panel, and the shader compares normalised depths, so one of
   them has to know this number to convert. It is not a constant: the sun's box is
   fitted to the view rectangle every frame, so its depth range grows with the zoom.

   THIS IS THE BUG THIS FIELD EXISTS TO PREVENT, and it was a real one. The bias was
   first written as a raw normalised figure, 0.0028. At the zoom the game opens on, the
   sun's range is about 580 cells, so 0.0028 of it is 1.6 cells of world depth -- taller
   than most of the buildings casting the shadows. Every cast shadow was biased out of
   existence and what was left on screen was the terminator rim on the objects
   themselves, which looks close enough to "shadows are working" to be believed. */
static float g_fxSunSpan = 1.0f;

/* ---- Shaders --------------------------------------------------------------------- */

/* Depth to world, once, shared by every pass that needs it. GLSL 1.20 has no
   #include, so this is a string the programs below are built from. */
static const char* FX_COMMON =
    "uniform mat4 uInvVP;\n"
    "uniform mat4 uVP;\n"
    "vec3 fx_world(vec2 t, float d) {\n"
    "    vec4 c = vec4(t.x*2.0-1.0, t.y*2.0-1.0, d*2.0-1.0, 1.0);\n"
    "    vec4 w = uInvVP * c;\n"
    "    return w.xyz / w.w;\n"
    "}\n";

/* ---- Pass 1: shadow, occlusion and dynamic light, all in one -----------------------
   One pass rather than three because all three want the same two things -- the world
   position behind this pixel and the surface normal there -- and reconstructing those
   three times over would be three times the arithmetic for the same answer.

   THE NORMAL IS RECONSTRUCTED FROM DEPTH, not read from a G-buffer, and that is what
   lets the chain leave 12,000 lines of glBegin alone. The cost is honest and is worth
   writing down: across a silhouette the two derivatives straddle a depth cliff and the
   normal there is meaningless. It shows as a one-pixel rim on the occlusion term. A
   real normal buffer would need every draw call in the renderer to emit one. */
static const char* FX_FS_LIGHT =
    "#version 120\n"
    "varying vec2 uv;\n"
    "uniform sampler2D uColor;\n"
    "uniform sampler2D uDepth;\n"
    "uniform sampler2D uShadow;\n"
    "uniform sampler2D uTerrainShadow, uTerrainVisibility, uTerrainWater;\n"
    "uniform int uTerrainWaterOn, uTerrainArtShadeOn;\n"
    "uniform vec2 uTerrainWaterScale;\n"
    "uniform float uTerrainDryEdge;\n"
    "uniform vec2 uTerrainDryFade;\n"
    "uniform vec2 uTerrainVisSize;\n"
    "uniform int uTerrainShadowOn, uTerrainGradeOn;\n"
    "uniform float uTerrainStrength;\n"
    "uniform vec2 uTerrainGrade; uniform float uTerrainSaturation;\n"
    "uniform vec3 uTerrainBalance;\n"
    "uniform mat4 uSunVP;\n"
    "uniform vec3 uCamPos;\n"
    "uniform vec3 uSunDir;\n"
    "uniform int   uUnified; uniform float uSunEnergy, uSkyEnergy, uTerrainAmbient;\n"
    "uniform int   uShadowOn;\n"
    "uniform float uSStrength, uSSoft, uSBias, uSTexel;\n"
    "uniform vec3  uSTint;\n"
    "uniform sampler2D uGNorm;\n"
    "uniform sampler2D uBake;\n"
    "uniform int   uBakeOn;\n"
    "uniform sampler2D uSpecMap;\n"
    "uniform int   uSpecOn;\n"
    "uniform float uSpecSun, uSpecSky, uSpecGloss;\n"
    "uniform float uReRough, uReSun, uReAmb, uReFloor, uReSpec, uReClamp;\n"
    "uniform float uAoFoliage;\n"
    "uniform float uTreeShadowFill;\n"
    "uniform float uObjectShadowFill;\n"
    "uniform float uAoObject;\n"
    "uniform int   uGNormOn;\n"
    "uniform float uSNormOff, uSPen, uAoGround, uSunFloor;\n"
    "uniform float uSlopeWhite, uSlopeKnee;\n"
    "uniform int   uSunLambert, uTerrainSmooth;\n"
    "uniform sampler2D uCloudTex;\n"
    "uniform int   uCloudOn;\n"
    "uniform float uCStrength, uCEdge, uCWidth, uCInvScale, uCDrift, uCHeight;\n"
    "uniform vec2  uCDir;\n"
    "uniform int   uAoOn, uAoSamples;\n"
    "uniform int   uCtOn, uCtSteps;\n"
    "uniform float uCtLen, uCtThick, uCtBias, uCtStrength;\n"
    "uniform sampler2D uInfShadow;\n"
    "uniform int   uInfShOn;\n"
    "uniform mat4  uInfSunVP;\n"
    "uniform float uInfBias, uInfRad, uInfNOff;\n"
    "uniform int   uCtSkipInf;\n"     /* 1 only while 3D soldiers are drawn */
    "uniform vec2  uGNormSize, uDepthSize;\n"
    "uniform float uAoRadius, uAoIntensity, uAoBias, uAoPower;\n"
    "uniform int   uNLights;\n"
    "uniform vec4  uLightPos[24];\n"
    "uniform vec4  uLightCol[24];\n"
    "uniform float uLightFalloff;\n"
    "uniform vec4  uSpotA[48];\n"    /* position, range (negative: it casts shadows) */
    "uniform vec4  uSpotB[48];\n"    /* direction, cos of the outer half angle */
    "uniform vec4  uSpotC[48];\n"    /* colour, cos of the inner half angle */
    "uniform int   uNSpots, uSpotSteps;\n"
    "uniform float uSpotNear, uSpotThick, uSpotStart;\n"
    /* THE STRUCTURES GROUP (fx_state.h, 12p). uStOn is 1 while a structure dial or the
       independent object self-shadow fill needs the class mask. With both neutral its
       blocks below are a uniform branch not taken. uStGrade is gamma, gain, contrast
       and saturation; uStLight is shade depth, sun strength, glow and occlusion. */
    "uniform int   uStOn;\n"
    "uniform vec4  uStGrade, uStLight;\n"
    "uniform int   uVhOn;\n"
    "uniform vec4  uVhGrade, uVhLight;\n"
    "uniform sampler2D uVhCards; uniform int uVhCardsOn;\n"
    /* THE RAIN (rain_mod.h sets these; all zero when it is off, so every rain branch
       below is a uniform branch that costs nothing). uRainWater is the sea's own field,
       its alpha 1 over water: the surface draws its own rain and must not be wetted
       again as if it were ground. */
    "uniform float uRainWet, uRainDark, uRainGloss, uRainSpec, uRainRipple;\n"
    "uniform float uRainRippleScale, uRainRippleRate, uRainPuddle, uRainHaze, uRainHazeNear, uRainOvercast, uRainTime;\n"
    "uniform vec3  uRainSky;\n"
    "uniform sampler2D uRainWater; uniform int uRainWaterOn; uniform vec2 uRainWaterScale;\n"
    "uniform float uRainRippleDensity, uRainRippleShade, uRainSheen, uRainPuddleInv;\n"
    "uniform float uRainRelief, uRainReliefBody, uRainPuddleMirror;\n"
    "uniform float uRainPuddleEdge, uRainPuddleFlat, uRainPuddleSSR;\n"
    "uniform float uRainFog, uRainFogHeight, uRainFogBase, uRainFogInvScale, uRainFogDetail, uRainFogDrift1, uRainFogDrift2;\n"
    "uniform vec2  uRainFogDir;\n"
    /* THE DISTANCE FOG (distfog_mod.h sets these; uFogAmt is zero when it is off, so
       its block is a uniform branch that costs nothing). uFogNear is the eye's distance
       to the ground it looks at plus the start dial, uFogInvDepth one over the cells
       the haze thickens over. */
    "uniform float uFogAmt, uFogNear, uFogInvDepth;\n"
    "uniform vec3  uFogCol;\n"
    /* THE TIBERIUM'S LIGHT AND FOG (tibglow_mod.h sets these; uTibLight and uTibFog are
       zero when off, so both blocks are uniform branches that cost nothing). uTibField
       is the whole map, two texels a cell: red the glow for lighting, green the glow for
       the fog, blue the ground's height between uTibH.x and uTibH.x + uTibH.y. */
    "uniform sampler2D uTibField;\n"
    "uniform float uTibInv, uTibLight, uTibLightH, uTibFog, uTibFogH, uTibFogGlow, uTibFogAmb;\n"
    "uniform float uTibFogWisp, uTibFogDrift, uTibFogTop, uTibPulse, uTibTime;\n"
    "uniform int   uTibFogSteps;\n"
    "uniform vec3  uTibCol;\n"
    "uniform vec2  uTibH;\n";

static const char* FX_FS_LIGHT_BODY =
    "\n"
    "void main() {\n"
    "    vec3 col = texture2D(uColor, uv).rgb;\n"
    "    float d  = texture2D(uDepth, uv).r;\n"
    /* THE POSITION AND THE NORMAL ARE COMPUTED BEFORE ANY BRANCH, AND THAT ORDER IS
       LOAD BEARING. dFdx and dFdy are evaluated across a 2x2 quad of fragments, so
       taking them after a non-uniform early return is undefined behaviour in GLSL: at
       the horizon, where one fragment of a quad is sky and its neighbour is ground, the
       surviving fragment reads a derivative from an invocation that has already exited.
       It was written that way first and it cost a real bug -- one pixel of a 1280x720
       shot, one level of green, flipping about half the time across runs, which is
       exactly the kind of "it passed twice" failure gate G36c exists to catch. The
       far-plane fragments still return early, just AFTER the derivative is taken.
       d is clamped for the reconstruction so a sky fragment cannot divide by a zero w
       and hand its neighbours a NaN, which would be the same bug wearing a hat. */
    "    vec3 P = fx_world(uv, min(d, 0.999989));\n"
    "    vec3 dPx = dFdx(P), dPy = dFdy(P);\n"
    "    vec3 n = normalize(cross(dPx, dPy));\n"
    "    if (dot(n, uCamPos - P) < 0.0) n = -n;\n"
    /* Geometry owns visibility tests. Authored smooth normals still own lighting. */
    "    vec3 geometricN = n;\n"
    "    float pixelReach = max(length(dPx),length(dPy));\n"
    /* ---- SURFACE RELIEF FOR EVERYTHING, AND WHERE IT COMES FROM -------------------
       Nothing in this game carries a normal map. The terrain writes one normal per
       cell corner off the heightfield, a mesh vertex carries a position, a texture
       coordinate and a baked colour and nothing else, and a sprite is a flat card.
       So a per-texel normal has to be DERIVED, and the only per-texel surface signal
       every one of those has in common is the art itself: the tile, the hull texture
       and the sprite were all painted with their own relief already shaded into them.

       This reads that painting back. The scene's luminance is treated as a height
       field and its gradient across the surface tilts the normal, which is the
       oldest bump-map trick there is. It is a derivation and not a measurement, and
       it is honest about what it can be wrong about: a painted stripe with no relief
       under it tilts the normal as a real groove would. At the strength a wet sheen
       wants, that reads as surface texture rather than as error, and the dial goes
       to zero.

       The screen-space gradient becomes a surface gradient through the same two
       derivatives that built the normal: dPx and dPy span the tangent plane, so
       dividing each by its own squared length turns "per pixel across" into "per
       cell across". Clamped, because at a silhouette the two taps straddle a depth
       cliff and the luminance step there is a cliff too. */
    "    float relL = dot(col, vec3(0.299, 0.587, 0.114));\n"
    "    float relX = dFdx(relL), relY = dFdy(relL);\n"
    "    float lx2 = max(dot(dPx, dPx), 1e-6), ly2 = max(dot(dPy, dPy), 1e-6);\n"
    "    vec3  relG = dPx * (relX / lx2) + dPy * (relY / ly2);\n"
    "    relG -= n * dot(n, relG);\n"
    "    float relLen = length(relG);\n"
    "    if (relLen > 6.0) relG *= 6.0 / relLen;\n"
    /* THE SURFACE'S OWN NORMAL, when the host drew one, and the alpha says WHOSE.
       This channel used to answer one question, "is this ground", and it now answers
       two, because they turned out to be different questions:

         0.00  nothing drew here. Walls, sprites and water keep the flat reconstruction.
         0.28  planar WATER. Own sun/sky response; no land grade or opaque SSAO.
         0.35  an AUTHORED OBJECT. Use this normal, treat the surface as a hull.
         0.50  FOLIAGE. Use this normal, but it is not ground.
         0.78  generated gravel. Ground lighting plus per-pixel wet roughness.
         0.92  generated rock. Ground lighting plus its dry dielectric material.
         1.00  other ground. Use this normal and the ground's own one-sun slope.

       THE THIRD BAND WAS ADDED WITHOUT MOVING EITHER OF THE OTHER TWO, deliberately:
       foliage still writes 0.50 and lands in the foliage test, ground still writes 1.00
       and lands in the ground test, so a frame that holds no authored model holds the
       identical picture it held before. The band exists because an authored vehicle is
       the first thing in this scene carrying a real per-pixel normal that is NOT a
       field: it wants the normal used, and the occlusion and the wetness of rolled
       steel, which is what a hull got back when it had no normal at all.

       The split exists because a canopy needs the first without the second. The
       reconstruction above is cross(dFdx(P), dFdy(P)), which is right on a hull because
       a hull is a few large connected faces, and noise on a tree because a canopy is
       hundreds of independently angled alpha-cut cards: it gave every card its own plane
       and fired the sun's terminator per card at random. Marking foliage as ground
       instead would have fixed the normal and then lit the tree twice, once by its own
       shader and once by the ground's lambert. */
    "    vec4 gn = texture2D(uGNorm, uv);\n"
    "    float materialClass=texture2D(uGNorm,(floor(uv*uGNormSize)+0.5)/uGNormSize).a;\n"
    "    float waterSurface = (uGNormOn == 1 && materialClass > 0.26 && materialClass < 0.30) ? 1.0 : 0.0;\n"
    "    bool rawStone = uUnified == 1 && materialClass > 0.86 && materialClass < 0.89;\n"
    "    float haveN  = (uGNormOn == 1 && gn.a > 0.25) ? 1.0 : 0.0;\n"
    "    float ground = (uGNormOn == 1 && gn.a > 0.75) ? 1.0 : 0.0;\n"
    "    float rock = (uGNormOn == 1 && gn.a > 0.82 && gn.a < 1.0)\n"
    "                 ? clamp((1.0-gn.a)/0.08,0.0,1.0) : 0.0;\n"
    "    float gravel = (uGNormOn == 1 && gn.a > 0.755 && gn.a <= 0.82) ? 1.0 : 0.0;\n"
    "    float foliage = (uGNormOn == 1 && gn.a > 0.40 && gn.a <= 0.75) ? 1.0 : 0.0;\n"
    "    float object  = haveN * (1.0 - ground) * (1.0 - foliage) * (1.0-waterSurface);\n"
    "    if (haveN > 0.5) { vec3 g = gn.xyz * 2.0 - 1.0; if (dot(g, g) > 0.01 && (ground < 0.5 || uTerrainSmooth == 1 || rawStone)) n = normalize(g); }\n"
    "    if (waterSurface > 0.5) n = vec3(0.0,1.0,0.0);\n"
    /* A SELF-LIT PIXEL (class 0.15, a remastered building's glowing pool, lit lens or
       status light, ebld_mod.h): it makes its own light, so the sun's shadow, the back-
       face term, the contact march, the cloud deck, the relight and the occlusion all
       leave it as it is; the grade, the rolloff and the bloom after this pass still
       reach it. Read at the texel's centre so a filtered edge between two classes cannot
       pass through this band. Everything below is still computed for every pixel and
       only its effect is withheld, so no derivative is taken inside a branch. */
    "    float selfLit = 0.0;\n"
    "    vec4 illumination = uBakeOn == 1 ? texture2D(uBake, (floor(uv*uDepthSize)+0.5)/uDepthSize) : vec4(0.0);\n"
    /* A STRUCTURE'S PIXEL, for the STRUCTURES group, from the same texel-centre read:
       a remastered surface (0.38) or its self-lit part (0.15), both written by the
       remasters' own normals (ebld_mod.h), or 0.05, which marks a building or wall the
       cartridge's model draws (str_draw_class in cnc_eyes.cpp). 0.05 sits under every
       band above that reads as a normal, so such a pixel keeps the plane rebuilt from
       depth, as it did before it was marked; and it is only written while the group is
       in use. Only read while the group is in use, so a frame at its neutral settings
       computes nothing new. */
    "    float stc = 0.0, vhc = 0.0, authoredVehicle = 0.0;\n"
    "    if (uGNormOn == 1) {\n"
    "        float sa = texture2D(uGNorm, (floor(uv * uGNormSize) + 0.5) / uGNormSize).a;\n"
    "        authoredVehicle = (sa > 0.335 && sa < 0.355) ? 1.0 : 0.0;\n"
    "        float stEmit = (sa > 0.10 && sa < 0.20) ? 1.0 : 0.0;\n"
    "        float vhEmit = (sa > 0.20 && sa < 0.24) ? 1.0 : 0.0;\n"
    "        selfLit = max(stEmit, vhEmit);\n"
    "        if (uVhOn == 1)\n"
    "            vhc = ((sa > 0.08 && sa < 0.10) || vhEmit > 0.5\n"
    "                   || (sa > 0.335 && sa < 0.345)) ? 1.0 : 0.0;\n"
    "        if (uStOn == 1)\n"
    "            stc = ((sa > 0.03 && sa < 0.08) || stEmit > 0.5\n"
    "                   || (sa > 0.365 && sa < 0.395)) ? 1.0 : 0.0;\n"
    "    }\n"
    /* Cards use a separate painter-order mask so their lighting and normals stay intact. */
    "    if (uVhOn == 1 && uVhCardsOn == 1 && texture2D(uVhCards, uv).a > 0.5) vhc = 1.0;\n"
    /* Apply the existing bounded painted-shade response uniformly. Whole-cell
       cliff eligibility made otherwise identical ground brighter only beside
       cliffs. Ground class, visibility and water remain the actual boundaries. */
    "    if (uTerrainArtShadeOn == 1) {\n"
    "        float tc = texture2D(uGNorm, (floor(uv * uGNormSize) + 0.5) / uGNormSize).a;\n"
    "        if (tc > 0.75) {\n"
    "            float vis = texture2D(uTerrainVisibility, (P.xz + 0.5) / uTerrainVisSize).r;\n"
    "            if (uTerrainWaterOn == 1) {\n"
    "                float shore = texture2D(uTerrainWater, P.xz * uTerrainWaterScale).r;\n"
    "                if (uTerrainDryFade.y > uTerrainDryFade.x)\n"
    "                    vis *= 1.0-smoothstep(uTerrainDryFade.x,uTerrainDryFade.y,shore);\n"
    "                else if (shore >= uTerrainDryEdge) vis = 0.0;\n"
    "            }\n"
    "            float peak = max(col.r, max(col.g, col.b));\n"
    "            float dark = 1.0 - smoothstep(0.10, 0.25, peak);\n"
    "            float green = (col.g-max(col.r,col.b))/max(peak,0.001);\n"
    "            float stone = 1.0 - smoothstep(0.05,0.20,green);\n"
    "            float shadeGain = inversesqrt(max(peak, 0.04));\n"
    "            col *= 1.0 + vis * dark * stone * (1.0-uTerrainStrength) * (shadeGain-1.0);\n"
    "        }\n"
    "    }\n"
    /* Grade only terrain-class pixels, before sun, fire and tiberium lighting.
       A texel-centre class read keeps a filtered normal edge off adjacent objects.
       Gate the additive contrast term on visibility so black shroud stays black. */
    "    if (uTerrainGradeOn == 1) {\n"
    "        float tc = texture2D(uGNorm, (floor(uv * uGNormSize) + 0.5) / uGNormSize).a;\n"
    "        if (tc > 0.75) {\n"
    "            float vis = texture2D(uTerrainVisibility, (P.xz + 0.5) / uTerrainVisSize).r;\n"
    "            vis *= smoothstep(0.015, 0.150, max(col.r, max(col.g, col.b)));\n"
    "            if (uTerrainWaterOn == 1) {\n"
    "                float shore = texture2D(uTerrainWater, P.xz * uTerrainWaterScale).r;\n"
    "                if (uTerrainDryFade.y > uTerrainDryFade.x)\n"
    "                    vis *= 1.0-smoothstep(uTerrainDryFade.x,uTerrainDryFade.y,shore);\n"
    "                else if (shore >= uTerrainDryEdge) vis = 0.0;\n"
    "            }\n"
    "            vec3 graded = col;\n"
    "            if (uTerrainSaturation != 1.0) graded = mix(vec3(dot(graded,vec3(0.299,0.587,0.114))),graded,uTerrainSaturation);\n"
    "            if (uTerrainGrade.x != 1.0) graded *= uTerrainGrade.x;\n"
    "            if (uTerrainGrade.y != 1.0) graded = (graded - 0.5) * uTerrainGrade.y + 0.5;\n"
    "            graded *= uTerrainBalance;\n"
    "            col = mix(col, max(graded, vec3(0.0)), vis);\n"
    "        }\n"
    "    }\n"
    /* ---- THE STRUCTURE'S OWN COLOUR (STRUCTURES group) -------------------------------
       Graded here, before anything lights it, so the sun, the shadow and the occlusion
       below fall on the graded colour as they fell on the painted one. The order and
       the luminance weights are the colour grade's (FX_FS_RESOLVE), so the dials read
       as that grade does, applied to the structures alone. Weighted by the scene's
       luminance as the other additive terms here are, so the shroud's black over the
       part of a building it covers stays black when the contrast is lowered. Each step,
       and each block below, is skipped while its own dial is at 1, so a dial left
       neutral is exact while another one is moved. */
    "    if (stc > 0.5 && uStGrade != vec4(1.0)) {\n"
    "        float stLum = smoothstep(0.015, 0.150, max(col.r, max(col.g, col.b)));\n"
    "        vec3 g = max(col, vec3(0.0));\n"
    "        if (uStGrade.x != 1.0) g = pow(g, vec3(1.0 / uStGrade.x));\n"
    "        if (uStGrade.y != 1.0) g *= uStGrade.y;\n"
    "        if (uStGrade.z != 1.0) g = (g - 0.5) * uStGrade.z + 0.5;\n"
    "        if (uStGrade.w != 1.0) {\n"
    "            float gLum = dot(g, vec3(0.2126, 0.7152, 0.0722));\n"
    "            g = mix(vec3(gLum), g, uStGrade.w);\n"
    "        }\n"
    "        col = mix(col, max(g, vec3(0.0)), stLum);\n"
    "    }\n"
    /* Vehicle colour uses the same grade, before the shared light. */
    "    if (vhc > 0.5 && uVhGrade != vec4(1.0)) {\n"
    "        float vhLum = smoothstep(0.015, 0.150, max(col.r, max(col.g, col.b)));\n"
    "        vec3 g = max(col, vec3(0.0));\n"
    "        if (uVhGrade.x != 1.0) g = pow(g, vec3(1.0 / uVhGrade.x));\n"
    "        if (uVhGrade.y != 1.0) g *= uVhGrade.y;\n"
    "        if (uVhGrade.z != 1.0) g = (g - 0.5) * uVhGrade.z + 0.5;\n"
    "        if (uVhGrade.w != 1.0) {\n"
    "            float gLum = dot(g, vec3(0.2126, 0.7152, 0.0722));\n"
    "            g = mix(vec3(gLum), g, uVhGrade.w);\n"
    "        }\n"
    "        col = mix(col, max(g, vec3(0.0)), vhLum);\n"
    "    }\n"
    /* THE RAIN'S TWO MASKS, read here for the same derivative reason as P and n: the
       puddle mask is the cloud deck's own tiling noise at a finer scale (it is
       histogram equalised, so a threshold on it is a coverage), and the water bit is
       the sea's shore field. Both are sampled by every fragment, and only the rain
       block below decides whether they mean anything. */
    /* THE PUDDLE'S OUTLINE, two octaves of the deck's own tiling noise. One octave is
       a blob and a blob is what a stain looks like; the second breaks the outline into
       the thin, branching shape water makes when it lies in the dips of a surface. */
    "    float rainPm = rn_puddle_mask(texture2D(uCloudTex, rn_puddle_uv1(P.xz, uRainPuddleInv)).r,\n"
    "                                  texture2D(uCloudTex, rn_puddle_uv2(P.xz, uRainPuddleInv)).r);\n"
    "    float rainWater = texture2D(uRainWater, P.xz * uRainWaterScale).a * float(uRainWaterOn);\n"
    "\n"
    /* The far plane is the sky and the shroud's black: nothing there has a surface. */
    "    if (d >= 0.99999) { gl_FragColor = vec4(col, 1.0); return; }\n"
    "\n"
    /* ---- the rain, part one: a wet surface is darker, and its top rings ------------
       Before the sun, so a cast shadow on wet ground is a shadow on the darker colour.
       The darkening is a power curve, which is what wetting does to a diffuse surface
       (the water film traps light, so dark pigment gets darker and saturation rises),
       plus a plain dimming. Flat ground (ground class, normal near vertical) gathers
       puddles where the noise is above the coverage; a puddle is darker still and is
       mirrored below. Drops landing on anything that faces up bend its normal with
       the impact rings, into nw; the shadow lookup keeps the smooth n. The sea keeps
       none of this: its own shader rains on it. */
    "    float wet = uRainWet * (1.0 - rainWater);\n"
    "    float rainLum = smoothstep(0.015, 0.150, max(col.r, max(col.g, col.b)));\n"
    "    float pud = 0.0;\n"
    "    vec3 nw = n;\n"
    /* THE RELIEF GOES ON THE WET NORMAL ONLY, never on the one the sun and the
       shadow map use: the ground's own lambert and the shadow lookup are geometry
       questions and this is a surface-detail answer. Ground and foliage get one
       strength, everything else -- an authored hull, and anything the target does not
       claim at all, walls and sprites -- gets its own, because a rolled steel plate is
       smoother than a field. Driven off ground and foliage rather than off haveN so an
       authored hull, which now DOES carry a normal, still reads as steel. */
    "    if (uRainWet > 0.0) {\n"
    "        float rel = mix(uRainReliefBody, uRainRelief, max(ground, foliage));\n"
    "        nw = normalize(n - relG * rel);\n"
    "    }\n"
    "    if (uRainWet > 0.0) {\n"
    "        float up = clamp(n.y, 0.0, 1.0);\n"
    /* WATER LIES WHERE THE GROUND IS FLAT, and on a desert map the whole board is
       flat, which is how a tenth of the noise field became a rash of grey patches
       across every dune. The flatness test is a dial now and it is asked of the
       GEOMETRY normal, never of the relief-bent one, or the derived detail would
       carve puddles out of the art's own grain. */
    "        pud = rn_puddle_cover(rainPm, uRainPuddle, uRainPuddleEdge)\n"
    "              * ground * smoothstep(uRainPuddleFlat, uRainPuddleFlat + 0.02, n.y) * wet;\n"
    "        vec3 wetc = pow(col, vec3(1.0 + 0.5 * uRainDark)) * (1.0 - 0.30 * uRainDark);\n"
    "        col = mix(col, wetc, wet * (0.55 + 0.45 * up));\n"
    "        col = mix(col, col * 0.55, pud);\n"
    "        if (uRainRipple > 0.0 && up > 0.3) {\n"
    /* A RING IS SHARPEST ON STANDING WATER and is a faint dimple on wet ground, which
       is what the eye expects: the rings a downpour draws are the ones in the puddles. */
    "            vec2 g = rn_ripple(P.xz, uRainTime, uRainRippleScale, uRainRippleRate, uRainRippleDensity);\n"
    "            nw = normalize(nw + vec3(g.x, 0.0, g.y) * (uRainRipple * 0.06 * up * wet * (1.0 + 24.0 * pud)));\n"
    /* A RING HAS TO SHADE OR IT IS NOT THERE. From this rig the sun sits too low
       and the eye too high for a wet ring's glint to fire on level ground, and the
       sheen barely moves with a tilt, so the rings were computed and never seen.
       This is the ring's own diffuse shading: the ratio of the bent normal's
       lambert to the flat one's, so the side of a ring turned to the sun is
       lighter and the far side darker, the way a bump map reads at any exposure.
       Bounded, and scaled by the wetness, so a dry surface keeps its own light. */
    "            float lf = max(dot(n,  -uSunDir), 0.08);\n"
    "            float lb = max(dot(nw, -uSunDir), 0.0);\n"
    "            float rs = 0.45 * uRainRippleShade;\n"
    "            col *= mix(1.0, clamp(lb / lf, 1.0 - rs, 1.0 + rs), wet);\n"
    "        }\n"
    "    }\n"
    "\n"
    /* ---- the sun ---- */
    "    float sh = 0.0;\n"
    "    if (uShadowOn == 1) {\n"
    /* NORMAL OFFSET: the receiver is looked up a little way out along its normal, so a
       surface never tests against the shadow texel it is itself standing in. */
    "        vec4 sc = uSunVP * vec4(P + n * uSNormOff, 1.0);\n"
    "        vec3 s  = (sc.xyz / sc.w) * 0.5 + 0.5;\n"
    "        if (s.x > 0.001 && s.x < 0.999 && s.y > 0.001 && s.y < 0.999 && s.z < 1.0) {\n"
    "            float ndl  = dot(n, -uSunDir);\n"
    /* Slope-scaled bias: a face nearly edge-on to the sun spans many depth units
       across one shadow texel and needs more room than a face square to it. */
    "            float bias = uSBias * (1.0 + 3.0 * (1.0 - clamp(ndl, 0.0, 1.0)));\n"
    /* THE PENUMBRA: twelve Poisson taps on a disc whose radius is the larger of the
       texel softness and the penumbra dial converted from world cells, with a fixed
       orientation because this pass has neither temporal accumulation nor a denoiser. The 3x3 grid it replaces reached 0.01 of a cell at
       4096 texels and drew every cliff's shadow as a polygon. */
    "            float rad = max(uSSoft, uSPen) * uSTexel;\n"
    /* No temporal accumulation or denoiser follows this pass. Rotating a sparse
       kernel independently at every pixel stamped salt-and-pepper onto vehicles
       and snow. Keep the same twelve-disc coverage with a stable orientation. */
    "            float ang = 0.0;\n"
    "            float ca = cos(ang), sa = sin(ang);\n"
    "            vec2 pd[12];\n"
    "            pd[0]=vec2(-0.326,-0.406); pd[1]=vec2(-0.840,-0.074); pd[2]=vec2(-0.696, 0.457); pd[3]=vec2(-0.203, 0.621);\n"
    "            pd[4]=vec2( 0.962,-0.195); pd[5]=vec2( 0.473,-0.480); pd[6]=vec2( 0.519, 0.767); pd[7]=vec2( 0.185,-0.893);\n"
    "            pd[8]=vec2( 0.507, 0.064); pd[9]=vec2( 0.896, 0.412); pd[10]=vec2(-0.322,-0.933); pd[11]=vec2(-0.792,-0.598);\n"
    "            float sum  = 0.0;\n"
    "            for (int i = 0; i < 12; i++) {\n"
    "                vec2 o = vec2(pd[i].x * ca - pd[i].y * sa, pd[i].x * sa + pd[i].y * ca) * rad;\n"
    "                float dz = texture2D(uShadow, s.xy + o).r;\n"
    "                float blocked = (s.z - bias > dz) ? 1.0 : 0.0;\n"
    "                if (uTerrainShadowOn == 1) {\n"
    "                    float td = texture2D(uTerrainShadow, s.xy + o).r;\n"
    "                    blocked = max(blocked, (s.z - bias > td) ? uTerrainStrength : 0.0);\n"
    "                }\n"
    "                sum += blocked;\n"
    "            }\n"
    "            sh = sum / 12.0;\n"
    /* A surface turned away from the sun is unlit whether or not anything stands
       between them. Without this the dark side of a building is as bright as the lit
       one and only the cast shadow reads, which looks like a decal rather than light. */
    /* Terrain's unlit face must follow the terrain dial too: it can dominate
       the cast map at max(), otherwise zero still leaves the whole cliff dark.
       Object and foliage faces retain their own sun shading. */
    "            float faceShade = 1.0 - smoothstep(-0.10, 0.30, ndl);\n"
    "            if (ground > 0.5 && uTerrainStrength != 1.0) faceShade *= uTerrainStrength;\n"
    "            if (uUnified == 0) sh = max(sh, faceShade);\n"
    "        }\n"
    /* ---- THE 3D SOLDIERS' OWN MAP -------------------------------------------------
       The same sun and the same test, from a map that holds nothing but the posed
       soldiers. Its softness is its own texel softness with a one texel floor and
       nothing more: the penumbra dial above is 0.08 of a cell, right for a cliff or a
       canopy and wider than a soldier's leg, and through it a man's shadow was a
       smudge. That is the reason for a second map at all, since one map cannot soften
       one kind of caster and not another. Folded in with max() like everything else in
       this block. Eight taps rather than twelve: the kernel is a texel or two wide. */
    "        if (uInfShOn == 1) {\n"
    "            vec4 ic = uInfSunVP * vec4(P + n * uInfNOff, 1.0);\n"
    "            vec3 is = (ic.xyz / ic.w) * 0.5 + 0.5;\n"
    "            if (is.x > 0.001 && is.x < 0.999 && is.y > 0.001 && is.y < 0.999 && is.z < 1.0) {\n"
    "                float indl = clamp(dot(n, -uSunDir), 0.0, 1.0);\n"
    "                float ib = uInfBias * (1.0 + 3.0 * (1.0 - indl));\n"
    "                float ia = 0.0;\n"
    "                float ica = cos(ia), isa = sin(ia);\n"
    "                float isum = 0.0;\n"
    "                for (int k = 0; k < 8; k++) {\n"
    "                    float a = float(k) * 0.785398 + 0.39;\n"
    "                    float r = (k < 4) ? 0.5 : 1.0;\n"
    "                    vec2 o = vec2(cos(a) * ica - sin(a) * isa, cos(a) * isa + sin(a) * ica) * r * uInfRad;\n"
    "                    float dz = texture2D(uInfShadow, is.xy + o).r;\n"
    "                    isum += (is.z - ib > dz) ? 1.0 : 0.0;\n"
    "                }\n"
    "                sh = max(sh, isum / 8.0);\n"
    "            }\n"
    "        }\n"
    /* ---- CONTACT SHADOWS ------------------------------------------------------------
       The shadow map is one 4096 square fitted to everything the camera can see, so a
       texel of it is several centimetres of world and its bias is tuned to stop a
       hillside shadowing itself. Both of those together mean it cannot draw the few
       pixels of darkness directly under a hull, and that darkness is most of what makes
       an object look like it is standing on the ground rather than hovering above it.
       This walks a short way from the surface toward the sun THROUGH THE DEPTH BUFFER
       and asks whether anything is in the way. It is screen space, so it only knows
       about what the camera can see, and it reaches a fraction of a cell -- which is
       exactly the range the shadow map is worst at and the range that matters here.

       INSIDE uShadowOn, so "sun shadows off" still means no sun shadows of any kind and
       the existing A/B keeps its meaning. OUTSIDE the in-bounds test above, so a pixel
       beyond the edge of the sun's box still gets one.

       FOLDED IN WITH max(), like the back-face term and the cloud deck, so it cannot
       double-darken what the shadow map already found: the darkest of the answers wins
       rather than the product of them.

       The midpoint samples are fixed, so two runs of one script are one picture. */
    /* ITS OWN ndl. The one above is declared inside the sun box's in-bounds branch,
       and this block deliberately sits outside that branch so a pixel beyond the edge
       of the box still gets a contact shadow. Reaching into the other scope is what
       made the whole light program fail to compile, which the chain reports and then
       carries on without -- so every frame came out looking untouched rather than
       broken. */
    "        float ctNdl = dot(n, -uSunDir);\n"
    "        if (uCtOn == 1 && ctNdl > 0.0) {\n"
    /* Fixed midpoints preserve the full ray length and sample count. A randomized
       first step followed by a binary any-hit test gave adjacent identical pixels
       unrelated full-strength shadows. */
    "            float j = 0.5;\n"
    "            vec3 planeN = dot(geometricN,n)>0.5 ? geometricN : n;\n"
    "            vec3 sp = P + planeN * uCtBias;\n"
    "            float occ = 0.0;\n"
    "            for (int i = 0; i < 16; i++) {\n"
    "                if (i >= uCtSteps) break;\n"
    "                float t = (float(i) + j) / float(uCtSteps);\n"
    "                vec3  wp = sp - uSunDir * (t * uCtLen);\n"
    "                vec4  cp = uVP * vec4(wp, 1.0);\n"
    "                if (cp.w <= 0.0) continue;\n"
    "                vec2  suv = (cp.xy / cp.w) * 0.5 + 0.5;\n"
    "                if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;\n"
    /* The nearest depth belongs to its texel centre, not the continuous ray UV.
       Reconstructing it at a different UV displaced flat panels off their plane. */
    "                suv = (floor(suv*uDepthSize)+0.5)/uDepthSize;\n"
    "                vec3 scene = fx_world(suv, min(texture2D(uDepth, suv).r, 0.999989));\n"
    "                if (dot(scene-P,planeN) <= max(0.0005,pixelReach*0.25)) continue;\n"
    /* A 3D SOLDIER IS NOT A BLOCKER HERE. The thickness below is a hull's; a man is a
       seventh of a cell deep, and taking his silhouette as a 0.6 cell solid drew a
       hard-edged slab on the ground beside him. His own map above casts him. */
    /* Read at the texel's centre: the target is linearly filtered, and between a hull's
       0.35 and the empty 0 a blend passes through the soldier's band at every edge. */
    /* Vehicle self-shadowing comes from the sun map. Treating the nearest visible
       vehicle surface as a thick contact blocker overlaid a second, stippled copy of
       its seats and rails on its panels. Keep vehicle contact on ground and contact
       from other surface classes; reject only vehicle-to-vehicle screen-depth hits. */
    "                if (uGNormOn == 1) {\n"
    "                    vec2 tc = (floor(suv * uGNormSize) + 0.5) / uGNormSize;\n"
    "                    float gc = texture2D(uGNorm, tc).a;\n"
    "                    if (uCtSkipInf == 1 && gc > 0.27 && gc < 0.33) continue;\n"
    "                    if (authoredVehicle > 0.5 && gc > 0.335 && gc < 0.355) continue;\n"
    "                }\n"
    /* THE THICKNESS TEST IS WHAT STOPS A SKYLINE SHADOWING THE WHOLE MAP. A depth
       buffer records a surface, not a solid, so a sample that lands far behind the
       recorded surface is behind a thin thing and must not count: without this every
       pixel under the horizon reads as occluded by the horizon. */
    "                float dz = length(scene - uCamPos) - length(wp - uCamPos);\n"
    "                float edge = max(0.002, pixelReach);\n"
    "                float hit = smoothstep(0.0005, edge+0.0005, -dz)\n"
    "                          * (1.0-smoothstep(max(0.0,uCtThick-edge),uCtThick,-dz));\n"
    "                occ = max(occ, hit);\n"
    "            }\n"
    "            sh = max(sh, occ * uCtStrength);\n"
    "        }\n"
    "    }\n"
    /* Fill only object receivers. The same caster still makes its full shadow on the
       ground; this dial lifts the sun shadow on tree/structure surfaces themselves. */
    "    sh *= 1.0 - uObjectShadowFill * max(stc, foliage);\n"
    "\n"
    /* ---- the weather ----------------------------------------------------------------
       Where does the sun's ray through THIS pixel cross the cloud deck? Walk back from
       the surface point along -uSunDir until y reaches uCHeight, and read the mask
       there. That one step is the difference between a cloud shadow and a texture
       pasted onto the ground: sampled at P.xz a roof would carry a different piece of
       cloud from the ground it stands on, and the shadow would break at every wall.

       THE CLAMP ON dy IS NOT DEFENSIVE, it is a design limit. sun_el goes down to 8
       degrees, where 1/sin(el) is 7.2, and a 12-cell deck would then be sampled 86
       cells away from the pixel being shaded -- two and a half screens. The pattern
       stops being anchored to anything the player can see and simply swims. Holding
       the ray no shallower than about 14 degrees keeps the slide bounded; the cost is
       that at a very low sun the cloud shadows stop lengthening, which is a smaller
       lie than the alternative and is written down as one.

       This is a uniform branch, so the implicit derivative in texture2D below is taken
       by every fragment or by none. That is the rule the note above P and n is about,
       and it is why this block can afford to look like ordinary code. */
    "    float cl = 0.0;\n"
    "    if (uCloudOn == 1) {\n"
    "        float dy = max(-uSunDir.y, 0.25);\n"
    "        float t  = max(uCHeight - P.y, 0.0) / dy;\n"
    "        vec2  g  = P.xz - uSunDir.xz * t;\n"
    "        vec2  q  = vec2(dot(g, uCDir), dot(g, vec2(-uCDir.y, uCDir.x)))\n"
    "                   * uCInvScale;\n"
    "        q.x -= uCDrift;\n"
    "        float m = texture2D(uCloudTex, q).r;\n"
    "        cl = smoothstep(uCEdge - uCWidth, uCEdge + uCWidth, m);\n"
    /* A cloud cannot darken what the sun is not reaching in the first place. The
       shroud is drawn in the WORLD pass and writes no depth, so shrouded ground arrives
       here looking to the depth buffer exactly like ground: without this gate a cloud
       crossing the reveal edge modulates the fog's own grey and the fog appears to
       drift. Keyed on the scene's luminance, which costs nothing and is the same
       reasoning a real overcast follows. It does not close the case -- the 45% dark
       band over bright grass is still bright enough to shadow -- and the remainder is
       registered as a known gap rather than left to be discovered. */
    "        cl *= uUnified == 1 ? texture2D(uTerrainVisibility,(P.xz+0.5)/uTerrainVisSize).r : smoothstep(0.015,0.150,max(col.r,max(col.g,col.b)));\n"
    "    }\n"
    "\n"
    /* MAX, not a sum and not a product. Ground already inside a hard cast shadow does
       not get darker still when a cloud crosses it -- there is only so much sun to
       take away, and taking it twice is what makes stacked shadow terms read as dirt.
       One tint for both, for the same reason: they are the same sun. */
    /* THE SURFACE'S OWN COLOUR for the vehicles' lamps below, before the sun's shadow
       and the cloud take their share: a headlight lights the ground in a building's
       shadow as brightly as the ground in the sun. */
    "    vec3 vlBase = col;\n"
    "    sh *= 1.0 - selfLit;\n"
    "    cl *= 1.0 - selfLit;\n"
    /* ON A STRUCTURE (STRUCTURES group) the same tint, taken apart into the two things
       it is: the shadow colour, tint, and what the sun adds above it where it reaches,
       (1 - a)(1 - tint), so that mix(1, tint, a) = tint + (1 - a)(1 - tint). Shade depth
       scales how far the shadow colour sits below white, sun strength scales what the
       sun adds above it; at 1 and 1 this is the line below. A self-lit pixel has a of
       zero and keeps its colour, as it does there. */
    "    vec3 treeSkyFill = vec3(0.0);\n"
    "    bool splitLight = uUnified == 1 && !rawStone && illumination.a > 0.19 && illumination.a < 0.45;\n"
    "    bool legacyLight = uUnified == 1 && illumination.a > 0.9;\n"
    "    if(uUnified == 1 && waterSurface > 0.5) {\n"
    "        float visibility=1.0-max(sh*uSStrength,cl*uCStrength);\n"
    "        col *= uTerrainAmbient*uSkyEnergy + uSunEnergy*max(dot(n,-uSunDir),0.0)*visibility;\n"
    "    } else if (splitLight && selfLit < 0.5) {\n"
    "        float visibility = 1.0 - max(sh*uSStrength,cl*uCStrength);\n"
    "        vec3 share = clamp(illumination.rgb,0.0,1.0);\n"
    "        float fillDial = stc > 0.5 ? max(0.0,2.0-uStLight.x) : vhc > 0.5 ? max(0.0,2.0-uVhLight.x) : 1.0;\n"
    "        float sunDial = stc > 0.5 ? uStLight.y : vhc > 0.5 ? uVhLight.y : 1.0;\n"
    "        vec3 attenuation = (1.0-share)*fillDial + share*visibility*sunDial;\n"
    "        col *= illumination.a < 0.31 ? pow(max(attenuation,vec3(0.0)),vec3(1.0/2.2)) : attenuation;\n"
    "    } else if (uUnified == 1 && ground < 0.5 && !legacyLight && selfLit < 0.5) {\n"
    "        // Native primitive-colour faces have no separable baked shade. Treat their colour as reflectance.\n"
    "        float visibility=1.0-max(sh*uSStrength,cl*uCStrength);\n"
    "        col *= uReAmb*uSkyEnergy*(0.6+0.4*max(n.y,0.0)) + uReSun*uSunEnergy*max(dot(n,-uSunDir),0.0)*visibility;\n"
    "    } else if (uUnified == 1 && (ground > 0.5 || legacyLight)) {\n"
    "        // Direct visibility is consumed once by the terrain/legacy BRDF below.\n"
    "    } else\n"
    "    if (stc > 0.5 && selfLit < 0.5 && (uStLight.x != 1.0 || uStLight.y != 1.0)) {\n"
    "        float a = max(sh * uSStrength, cl * uCStrength);\n"
    "        vec3 shc = max(vec3(1.0) - uStLight.x * (vec3(1.0) - uSTint), vec3(0.0));\n"
    "        col *= shc + uStLight.y * (1.0 - a) * (vec3(1.0) - shc);\n"
    "    } else\n"
    "    if (vhc > 0.5 && selfLit < 0.5 && (uVhLight.x != 1.0 || uVhLight.y != 1.0)) {\n"
    "        float a = max(sh * uSStrength, cl * uCStrength);\n"
    "        vec3 shc = max(vec3(1.0) - uVhLight.x * (vec3(1.0) - uSTint), vec3(0.0));\n"
    "        col *= shc + uVhLight.y * (1.0 - a) * (vec3(1.0) - shc);\n"
    "    } else\n"
    "    col *= mix(vec3(1.0), uSTint, max(sh * uSStrength, cl * uCStrength));\n"
    /* ONE SUN: the ground's slope shading, once, per pixel, from the smooth normal, and
       normalised to level ground so a flat field keeps exactly the brightness it had
       (the host drew it at the flat baked shade). Capped at 1.35 so a slope turned
       into the sun brightens a little and never blows out; the far side falls to the
       sun_floor dial, and the shadow tint does the rest.

       THAT FLOOR WAS 0.25 AND READ AS BLACK. A slope facing away is shaded twice for one
       geometric fact -- the shadow pass tints it as well, with nothing casting -- and the
       product landed near a tenth of level brightness before occlusion and the grade's
       black point finished it off. Nothing else in the chain has an ambient floor, so
       this literal was the whole of it, and it is a dial now. */
    "    if ((uSunLambert == 1 || uUnified == 1) && ground > 0.5) {\n"
    "        float flatL = max(-uSunDir.y, 0.05);\n"
    "        float lam = clamp(dot(n, -uSunDir), 0.0, 1.0) / flatL;\n"
    /* Scale only the shaded side. Sun-facing slopes keep their existing boost,
       and neutral skips the interpolation to preserve the original pixels. */
    "        float slopeLight = clamp(lam, uSunFloor, 1.35);\n"
    "        if (slopeLight < 1.0 && uTerrainStrength != 1.0)\n"
    "            slopeLight = mix(1.0, slopeLight, uTerrainStrength);\n"
    "        if (uUnified == 1) {\n"
    "            float visibility = 1.0-max(sh*uSStrength,cl*uCStrength);\n"
    "            float sky = uTerrainAmbient*uSkyEnergy*(0.65+0.35*max(n.y,0.0));\n"
    "            float direct = uSunEnergy*max(dot(n,-uSunDir),0.0)*visibility;\n"
    "            slopeLight = sky+direct;\n"
    "        }\n"
    /* A SUN-FACING SLOPE OF PALE GROUND KEEPS ITS PAINTING (uSlopeWhite > 0, Red Alert's
       ground only). Snow on level ground already sits a little under the level the grade
       turns into white, so 1.35 times more on a slope went straight through it and the
       grade clipped broad hillsides to flat white with no texture left. The extra light
       here rolls off into the headroom that is actually left: uSlopeWhite is the value
       this buffer may reach before the grade clips (fx_grade_white_in), and above
       uSlopeKnee of it the boost bends towards that level without reaching it, the way
       the highlight rolloff bends the finished picture. Never darker than the same
       ground lying level; one factor on all three channels, so the hue stays; and below
       the knee nothing moves, so darker ground lights as before. The unified branch
       supplies its final sky/direct factor first, so it cannot overwrite this bound. */
    "        if (uSlopeWhite > 0.0 && slopeLight > 1.0) {\n"
    "            float m0 = max(col.r, max(col.g, col.b));\n"
    "            float k = max(m0, uSlopeKnee * uSlopeWhite);\n"
    "            float m1 = m0 * slopeLight;\n"
    "            if (m1 > k) {\n"
    "                float s = uSlopeWhite - k;\n"
    "                slopeLight = s > 1e-4 ? (k + s * (1.0 - exp(-(m1 - k) / s))) / max(m0, 1e-4) : 1.0;\n"
    "            }\n"
    "        }\n"
    "        col *= slopeLight;\n"
    "    }\n"
    /* Dry cliffs and damp gravel share dielectric lighting. The gravel alpha band
       carries its authored roughness: compacted wet ruts are smoother, while loose
       stones keep a broad response. Its per-pixel normal reaches this same pass. */
    "    bool stoneSpec = rawStone && uSpecOn==1 && texture2D(uSpecMap,uv).a>0.5;\n"
    "    if ((rock > 0.5 || gravel > 0.5) && !stoneSpec) {\n"
    "        float roughness = gravel > 0.5 ? clamp((gn.a-0.76)/0.06,0.48,0.82) : (rawStone ? 0.48+0.42*clamp((gn.a-0.861)/0.028,0.0,1.0) : 0.82);\n"
    "        const float metallic = 0.0;\n"
    "        vec3 V = normalize(uCamPos - P), L = -uSunDir, H = normalize(V + L);\n"
    "        float NoL = max(dot(n,L),0.0), NoV = max(dot(n,V),0.001), NoH = max(dot(n,H),0.0);\n"
    "        float VoH = max(dot(V,H),0.0);\n"
    "        float a = roughness*roughness, a2 = a*a;\n"
    "        float den = NoH*NoH*(a2-1.0)+1.0;\n"
    "        float D = a2/max(3.14159265*den*den,0.00001);\n"
    "        float k = (roughness+1.0)*(roughness+1.0)*0.125;\n"
    "        float G = (NoL/(NoL*(1.0-k)+k))*(NoV/(NoV*(1.0-k)+k));\n"
    "        vec3 F0 = mix(vec3(mix(0.04,0.055,gravel)), col, metallic);\n"
    "        vec3 F = F0+(1.0-F0)*pow(1.0-VoH,5.0);\n"
    "        float sunlit = 1.0-max(sh*uSStrength,cl*uCStrength);\n"
    "        col += (uUnified == 1 ? uSunEnergy : 1.0)*max(rock,gravel)*F*D*G*NoL*sunlit/max(4.0*NoL*NoV,0.001);\n"
    "    }\n"
    "\n"
    /* ---- THE RELIGHT: the cartridge's own art, lit by this sun ---------------------
       Everything above this point darkens or adds to a colour the world pass already
       finished. For a cartridge mesh that colour is the texture multiplied by the
       Nintendo 64's baked vertex shade, and no amount of darkening turns a 1995 bake
       into a light that answers a dial. This replaces it.

           albedo = colour / bakedShade            (the bake target holds the shade)
           colour = albedo x BRDF(sun, sky, this surface)

       The albedo recovered that way is exactly what the requirement asked for: the
       asset's own texture as a flat base colour, with roughness and metalness as
       constants off two dials rather than per-texel maps the cartridge never had.

       WHAT IT DELIBERATELY DOES NOT TOUCH. Alpha in the bake target is 1 only where a
       TEXTURED MESH TRIANGLE drew. Terrain, sprites, water, decals, the shroud and
       every effect leave it zero and fall through with the picture they already had, so
       this cannot erase the things composited over the world -- which is the failure
       that makes a naive deferred pass unshippable in this renderer.

       THE DIVISION IS FLOORED, not because of a corner case but because the shade goes
       to almost nothing in the cartridge's own deep shadow, and dividing by almost
       nothing turns film grain into a supernova.

       THE NORMAL IS THE RECONSTRUCTED ONE. A pack vertex carries no normal and none can
       be decoded, but cross(dFdx(P), dFdy(P)) is the exact face normal of a flat
       triangle, and a cartridge hull is a few dozen flat plates. So the hull comes out
       FACETED, which is a fidelity ceiling and is registered as one. */
    "    if (uBakeOn == 1) {\n"
    "        vec4 bk = uUnified == 1 ? illumination : texture2D(uBake, uv);\n"
    "        if (bk.a > 0.9 && selfLit < 0.5 && !rawStone) {\n"
    "            vec3 shade = max(bk.rgb, vec3(uReFloor));\n"
    "            vec3 V2    = normalize(uCamPos - P);\n"
    "            vec3 L2    = -uSunDir;\n"
    "            vec3 H2    = normalize(L2 + V2);\n"
    "            float NoL2 = max(dot(n, L2), 0.0);\n"
    "            float NoV2 = max(dot(n, V2), 1e-4);\n"
    "            float NoH2 = max(dot(n, H2), 0.0);\n"
    "            float VoH2 = max(dot(V2, H2), 0.0);\n"
    "            float a2   = max(uReRough * uReRough, 1e-4);\n"
    "            float dd   = NoH2 * NoH2 * (a2 * a2 - 1.0) + 1.0;\n"
    "            float D2   = (a2 * a2) / max(3.14159265 * dd * dd, 1e-7);\n"
    "            float gv2  = NoL2 * sqrt(NoV2 * NoV2 * (1.0 - a2 * a2) + a2 * a2);\n"
    "            float gl2  = NoV2 * sqrt(NoL2 * NoL2 * (1.0 - a2 * a2) + a2 * a2);\n"
    "            float Vs   = 0.5 / max(gv2 + gl2, 1e-6);\n"
    "            float F2   = 0.04 + 0.96 * pow(1.0 - VoH2, 5.0);\n"
    /* The sun arrives already shadowed: sh is what the shadow map, the contact march,
       the cloud deck and the back-face term between them decided, so a relit hull sits
       in the same shadow as the ground it stands on rather than in one of its own. */
    "            float sun2 = (1.0 - sh * uSStrength);\n"
    "            float lnew = uReSun * NoL2 * sun2\n"
    "                       + uReAmb * (0.6 + 0.4 * n.y)\n"
    "                       + F2 * D2 * Vs * NoL2 * sun2 * uReSpec;\n"
    /* A RATIO, NOT A REPLACEMENT, and that is the whole reason this works at all.
       The colour in the buffer is gamma encoded -- nothing in the world pass
       linearises anything -- so computing a linear radiance here and writing it back
       crushes the picture: measured, it took this frame's buildings to near black.
       Dividing the console's own shade out and multiplying a new one in leaves the
       encoding on both sides of the division, where it cancels. What changes is
       WHERE THE LIGHT COMES FROM, which is the entire point; what does not change is
       the colour space the frame is in.
       Clamped, because the baked shade goes to almost nothing in the cartridge's own
       deep shadow and a ratio against almost nothing is how grain becomes a supernova. */
    "            if (uUnified == 1) {\n"
    "                float visibility = 1.0-max(sh*uSStrength,cl*uCStrength);\n"
    "                float skyDial = stc > 0.5 ? max(0.0,2.0-uStLight.x) : vhc > 0.5 ? max(0.0,2.0-uVhLight.x) : 1.0;\n"
    "                float sunDial = stc > 0.5 ? uStLight.y : vhc > 0.5 ? uVhLight.y : 1.0;\n"
    "                lnew = uReAmb*uSkyEnergy*(0.6+0.4*n.y)*skyDial\n"
    "                    + uSunEnergy*visibility*sunDial*(uReSun*NoL2+F2*D2*Vs*NoL2*uReSpec);\n"
    "            }\n"
    /* Current remastered trees use this legacy-shade-removal path, not TREE_FS.
       Add reflected sky light in their shaded regions, before the display grade.
       Keep this minimum indirect contribution outside screen-space AO so packed
       branches cannot reduce the control to zero. It neither removes casters nor
       restores sunlight: no sky energy means no fill, and source/shroud colour
       remains multiplicative. The legacy bushes' separate shader is unchanged. */
    "            if (uUnified == 1 && foliage > 0.5 && uTreeShadowFill > 0.0) {\n"
    "                float shaded = uSunEnergy > 0.0 ? 1.0-clamp(NoL2*(1.0-max(sh*uSStrength,cl*uCStrength)),0.0,1.0) : 1.0;\n"
    "                treeSkyFill = col * clamp(vec3(uSkyEnergy*uTreeShadowFill*shaded*texture2D(uTerrainVisibility,(P.xz+0.5)/uTerrainVisSize).r)/shade,vec3(0.0),vec3(uReClamp));\n"
    "            }\n"
    "            vec3 ratio = vec3(lnew) / shade;\n"
    "            col *= clamp(ratio, vec3(0.0), vec3(uReClamp));\n"
    "        }\n"
    "    }\n"
    /* ---- occlusion ---- */
    "    float ao = 1.0;\n"
    "    if (uAoOn == 1) {\n"
    "        vec3 t = abs(n.y) < 0.99 ? normalize(cross(vec3(0.0,1.0,0.0), n))\n"
    "                                 : vec3(1.0,0.0,0.0);\n"
    "        vec3 b = cross(n, t);\n"
    /* Fixed hemisphere samples; independently rotating them per pixel requires a
       denoiser that this pass does not have. */
    "        float rot = 0.0;\n"
    "        float occ = 0.0, cnt = 0.0;\n"
    "        for (int i = 0; i < 24; i++) {\n"
    "            if (i >= uAoSamples) break;\n"
    "            float fi = (float(i) + 0.5) / float(uAoSamples);\n"
    "            float z  = fi;\n"
    "            float r  = sqrt(max(0.0, 1.0 - z*z));\n"
    "            float a  = 2.39996323 * float(i) + rot;\n"
    "            vec3 h   = vec3(r*cos(a), r*sin(a), z);\n"
    "            vec3 sp  = P + (t*h.x + b*h.y + n*h.z) * uAoRadius * (0.35 + 0.65*fi);\n"
    "            vec4 cp  = uVP * vec4(sp, 1.0);\n"
    "            if (cp.w <= 0.0) continue;\n"
    "            vec2 st  = (cp.xy / cp.w) * 0.5 + 0.5;\n"
    "            if (st.x < 0.0 || st.x > 1.0 || st.y < 0.0 || st.y > 1.0) continue;\n"
    "            st = (floor(st*uDepthSize)+0.5)/uDepthSize;\n"
    "            float sd = texture2D(uDepth, st).r;\n"
    "            cnt += 1.0;\n"
    /* THE OCCLUSION TEST IS IN WORLD CELLS, AND IT USED TO BE IN WINDOW DEPTH. That is
       the whole of why this pass did nothing. Window depth is what the depth buffer
       stores and it is not linear in distance: perspective crushes the far half of the
       scene into the last sliver of the range, so one fixed epsilon there is a hair's
       breadth near the eye and an enormous gap further out. Compared that way the shipped
       bias of 0.02 was larger than any depth difference this camera produces, and nothing
       ever counted as occluding anything.

       MEASURED BEFORE THE CHANGE, on a 1280x800 frame with a base in shot: toggling the
       feature at the shipped dials moved ZERO pixels. It only began to move the picture
       at all at a bias of 0.001, the very bottom of the slider, and the ladder in between
       was 9,051 pixels at 0.001, 4,567 at 0.002, 741 at 0.005, 36 at 0.010 and nothing
       from 0.020 up. A dial whose only useful setting is its own minimum is a dial that
       does not work.

       Both points are now reconstructed to world space and compared as distances from the
       eye, so the bias is a length in CELLS and means the same thing everywhere in the
       frame. The cost is one reconstruction per SAMPLE rather than one per hit: a matrix
       multiply and a divide, inside a loop that already fetches a texture each time
       round. */
    "            vec3 sw = fx_world(st, sd);\n"
    "            vec3 planeN = dot(geometricN,n)>0.5 ? geometricN : n;\n"
    "            if (dot(sw-P,planeN)>max(0.0005,pixelReach*0.25)\n"
    "                && length(sw-uCamPos)<length(sp-uCamPos)-uAoBias) {\n"
    /* The range check stops a distant wall behind a unit from occluding it: only a
       surface within the sample radius counts as contact. */
    "                float dist = length(sw - P);\n"
    "                float depthGap = length(sp-uCamPos)-length(sw-uCamPos)-uAoBias;\n"
    "                occ += smoothstep(0.0,max(0.002,pixelReach),depthGap)\n"
    "                     * (1.0-smoothstep(uAoRadius*0.5,uAoRadius,dist));\n"
    "            }\n"
    "        }\n"
    "        if (cnt > 0.0) {\n"
    "            float f = pow(clamp(occ / cnt, 0.0, 1.0), uAoPower) * uAoIntensity\n"
    "                      * mix(mix(mix(1.0, uAoObject, object), uAoFoliage, foliage),\n"
    "                            uAoGround, ground);\n"
    "            ao = clamp(1.0 - f, 0.0, 1.0);\n"
    "        }\n"
    "    }\n"
    /* the STRUCTURES group's occlusion and glow: the darkening scaled on a structure,
       and a self-lit structure pixel's colour scaled where the line below leaves it */
    "    if (stc > 0.5 && uStLight.w != 1.0) ao = clamp(1.0 - (1.0 - ao) * uStLight.w, 0.0, 1.0);\n"
    "    if (stc > 0.5 && selfLit > 0.5 && uStLight.z != 1.0) col *= uStLight.z;\n"
    "    if (vhc > 0.5 && uVhLight.w != 1.0) ao = clamp(1.0 - (1.0 - ao) * uVhLight.w, 0.0, 1.0);\n"
    "    if (vhc > 0.5 && selfLit > 0.5 && uVhLight.z != 1.0) col *= uVhLight.z;\n"
    "    ao = mix(ao, 1.0, uObjectShadowFill * max(stc, foliage));\n"
    "    col *= mix(ao, 1.0, max(selfLit,waterSurface));\n"
    "    col += treeSkyFill;\n"
    /* ---- THE SPECULAR MAPS: a remastered vehicle's own shine --------------------------
       Where a vehicle with a specular map drew (the target's alpha), its red says how
       strongly the surface reflects and its green how tight the highlight is. The sun's
       highlight is a normalised Blinn-Phong lobe on this pixel's normal (the export's
       smooth normal, from the normal target), so it slides over a round tank as the hull
       turns; it lies only where the sun reaches (the shadow map, the contact march and the
       cloud deck, as decided above) and takes the sun's warm white. The sky's sheen is a
       Schlick Fresnel term of a pale sky blue at grazing angles, kept out of the occlusion.
       Added after the occlusion, before the rain and the fog, so the haze still covers it.
       Read at the texel's centre: a filtered edge would smear a chrome rim onto the ground. */
    "    if (uSpecOn == 1) {\n"
    "        vec4 sp = texture2D(uSpecMap, (floor(uv * uGNormSize) + 0.5) / uGNormSize);\n"
    "        if (sp.a > 0.5 && sp.r > 0.004) {\n"
    "            vec3 Vs = normalize(uCamPos - P);\n"
    "            vec3 Ls = -uSunDir;\n"
    "            vec3 Hs = normalize(Ls + Vs);\n"
    "            float NoLs = max(dot(n, Ls), 0.0);\n"
    "            float NoHs = max(dot(n, Hs), 0.0);\n"
    "            float NoVs = max(dot(n, Vs), 0.0);\n"
    "            float pw = exp2(mix(3.0, 9.0, sp.g)) * uSpecGloss;\n"
    /* THE UPRIGHT SHEEN (the map's blue, 0 everywhere but where a model asks for it: Red
       Alert's steel drums, ore and gems). The half vector between the sun and the game
       camera is steep, so an upright side's normal never comes within the tight lobe and a
       glossy drum (or a nugget's knob, a crystal's facet) showed little highlight. Its blue
       widens the lobe toward a broad one and raises the highlight's weight, so the sun's
       highlight lies where the surface turns toward the half vector and moves with the sun. */
    "            float hiK = 0.12;\n"
    "            if (sp.b > 0.002) {\n"
    "                pw = mix(pw, 6.0, sp.b);\n"
    "                hiK = 0.12 + 0.9 * sp.b;\n"
    "            }\n"
    "            float lobe = pow(NoHs, pw) * (pw + 8.0) / 25.13274;\n"
    "            float lit = 1.0 - max(sh * uSStrength, cl * uCStrength);\n"
    "            float fres = 0.04 + 0.96 * pow(1.0 - NoVs, 5.0);\n"
    "            vec3 sunHi = vec3(1.0, 0.96, 0.88) * (lobe * NoLs * lit * uSpecSun * (uUnified == 1 ? uSunEnergy : 1.0));\n"
    "            vec3 skyHi = vec3(0.55, 0.64, 0.76) * (fres * uSpecSky * 0.45 * ao * (uUnified == 1 ? uSkyEnergy : 1.0));\n"
    "            col += sp.r * (sunHi * hiK + skyHi);\n"
    "        }\n"
    "    }\n"
    "\n"
    /* ---- the rain, part two: what shines off a wet surface --------------------------
       A Fresnel sheen of the overcast sky at grazing angles, a Blinn glint of the sun
       where the sun reaches (the shadow and cloud terms already computed), and the
       puddles mirroring the sky. A camera-facing card is a sprite, not a surface: its
       reconstructed normal looks straight at the eye, which would glint everywhere, so
       the mirror terms fade out on it and it keeps the darkening alone. Every additive
       term is gated on the scene's own luminance the way the cloud term is, so nothing
       shines under the shroud's black. */
    "    if (uRainWet > 0.0) {\n"
    "        vec3 V = normalize(uCamPos - P);\n"
    "        float ndv = max(dot(nw, V), 0.0);\n"
    "        float fres = pow(1.0 - ndv, 4.0);\n"
    "        float bill = 1.0 - smoothstep(0.985, 0.999, max(dot(n, V), 0.0));\n"
    "        vec3 H = normalize(V - uSunDir);\n"
    "        float sunlit = 1.0 - max(sh * uSStrength, cl * uCStrength);\n"
    "        float glint = pow(max(dot(nw, H), 0.0), uRainGloss) * uRainSpec * wet * sunlit\n"
    "                      * (0.5 + 0.5 * clamp(nw.y, 0.0, 1.0)) * (1.0 + 6.0 * pud) * bill;\n"
    /* THE SHEEN IS THE WET LOOK. A film of water is a glossy layer over whatever is
       under it: it mirrors the sky, more at a glancing angle (the Fresnel term) but
       never nothing, because a wet surface's reflectance floor is well above a dry
       one's, and games lift that floor further than physics does so the eye reads
       it. The sky it mirrors is a hemisphere, brighter overhead, read along the
       reflected view ray off the ring-bent normal, so a hull's roof and a road both
       take a grey gloss and a wall takes it where it turns away from the eye. */
    "        vec3 Rv = reflect(-V, nw);\n"
    "        vec3 skyR = uRainSky * (0.75 + 0.35 * clamp(Rv.y, 0.0, 1.0));\n"
    "        float fresW = 0.12 + 0.88 * pow(1.0 - ndv, 5.0);\n"
    "        float sheen = wet * bill * uRainSheen * fresW;\n"
    /* STANDING WATER IS A MIRROR, and the thing that makes one read is CONTRAST: a
       bright sky held inside a dark wet outline, with the rings breaking it up. A
       broad soft blend of a mid grey is a stain, which is what the first one was.
       So the puddle takes the sky almost entirely, brightened where the ring-bent
       normal turns it up, and the ground it sits in is already the darkest thing
       around it. */
    /* AND IT MUST STAY INSIDE THE PICTURE. At the sky's own value and a lift on top
       the puddles came out blown to white and read as snow rather than water: the
       sky this camera sees is already bright against ground the rain has just
       darkened, so the mirror only has to be brighter than what surrounds it. */
    /* WHAT MAKES A PUDDLE READ IS WHAT IS IN IT, and a flat fill has nothing in it.
       The reflected ray is marched against the depth buffer this pass already has
       bound, exactly as the occlusion marches its hemisphere: where it runs into
       something, that pixel's colour is what the water shows; where it runs out into
       nothing, the water shows the sky. From a camera looking down at this angle the
       ray leaves almost vertically, so most of it finds sky and the rest finds the
       tall things standing beside the water -- a tree, a cliff, a hull -- as dark
       shapes against it. That contrast is the whole of the effect.

       The step grows geometrically so twelve taps cover a useful distance without
       crawling; the hit test wants the sample to be BEHIND the stored depth by more
       than the bias and not by a mile, because a ray passing far in front of a distant
       cliff has not hit it. Note the taps are inside a branch that is not uniform
       across a quad: that is legal here for the same reason the occlusion's is, the
       two targets carry no mipmaps at all, so the implicit derivative selects a level
       that does not exist and level zero is sampled either way. */
    /* A PUDDLE SEEN FROM ABOVE IS MOSTLY SKY, and that is geometry rather than a
       shortcoming: the reflected ray leaves a level surface at the angle the view
       arrived on, which from this rig is steeply upward, so it finds open air within
       a cell or two and only rarely a tree or a cliff. A photograph taken standing on
       a road shows a puddle full of trees because THAT ray runs nearly flat.

       So what makes this one read as water is not the content of the reflection, it
       is the RIPPLES breaking it. The sky is given a steep gradient between a bright
       zenith and a dark horizon, and the ring-bent normal then swings the sample
       across that gradient: a millimetre of tilt becomes a visible light or dark
       band, which is exactly how still water announces that it is not a painted
       patch. On damp ground the same term is a whisper, because the bend there is a
       twenty-fourth of the one a puddle gets. */
    "        float pupR = clamp(Rv.y, 0.0, 1.0);\n"
    "        vec3 pudC = mix(uRainSky * 0.30, uRainSky * 1.25, pupR * pupR);\n"
    "        if (uRainPuddleSSR > 0.0 && pud > 0.002) {\n"
    "            float rt = 0.12, rstep = 0.12;\n"
    "            for (int i = 0; i < 12; i++) {\n"
    "                vec3 sp = P + Rv * rt;\n"
    "                vec4 cp = uVP * vec4(sp, 1.0);\n"
    "                if (cp.w <= 0.0) break;\n"
    "                vec2 st = (cp.xy / cp.w) * 0.5 + 0.5;\n"
    "                if (st.x < 0.0 || st.x > 1.0 || st.y < 0.0 || st.y > 1.0) break;\n"
    "                float sd = texture2D(uDepth, st).r;\n"
    "                float sz = (cp.z / cp.w) * 0.5 + 0.5;\n"
    "                if (sz > sd + 0.00002 && sz < sd + 0.004) {\n"
    "                    vec3 hitc = texture2D(uColor, st).rgb;\n"
    "                    pudC = mix(pudC, hitc * 0.75, uRainPuddleSSR);\n"
    "                    break;\n"
    "                }\n"
    "                rstep *= 1.45;\n"
    "                rt += rstep;\n"
    "            }\n"
    "        }\n"
    /* THE OUTLINE FADES, THE MIRROR DOES NOT. A puddle with a hard cut round it reads
       as a decal laid on the ground, so the coverage now ramps over a real band and the
       darkening follows it all the way out. The MIRROR cannot follow it: a broad soft
       wash of pale sky is the grey stain this went round twice already, and it is what
       an ungated soft mask always turns into. So the sky is held to the CORE, a
       smoothstep of the same coverage with no second noise fetch, and what reaches the
       rim is the darkening and the wetting alone. Water is dark at its edge and bright
       in its middle, which is the right way round anyway.

       the mirror is gated on luminance like the additive terms: ground the shroud has
       darkened (the rim ring, the dark band) is drawn black and carries the ground
       class, and an ungated puddle painted pale sky over it */
    "        float pudCore = smoothstep(0.35, 1.0, pud);\n"
    "        col = mix(col, pudC, pudCore * uRainPuddleMirror * rainLum);\n"
    "        col += (skyR * sheen + vec3(1.0, 0.97, 0.90) * glint) * rainLum;\n"
    "    }\n"
    "\n"
    /* ---- the things that are actually burning ---- */
    "    vec3 add = vec3(0.0);\n"
    "    for (int i = 0; i < 24; i++) {\n"
    "        if (i >= uNLights) break;\n"
    "        vec3  L    = uLightPos[i].xyz - P;\n"
    "        float dist = length(L);\n"
    "        float rad  = uLightPos[i].w;\n"
    "        if (dist >= rad) continue;\n"
    "        float att = pow(max(0.0, 1.0 - dist / rad), uLightFalloff);\n"
    "        float ndl = max(0.0, dot(n, L / max(dist, 1e-4)));\n"
    /* 0.35 of the light lands whatever way the surface faces. A pure N.L term makes a
       fireball light the ground and leave the unit beside it black, because the
       reconstructed normal on a vertical face points away from a light at its feet. */
    "        add += uLightCol[i].rgb * uLightCol[i].w * att * (0.35 + 0.65 * ndl);\n"
    "    }\n"
    "    col += add;\n"
    /* ---- the vehicles' lamps (FxSpot above) ---- */
    "    if (uNSpots > 0 && selfLit < 0.5) {\n"
    "        vec3 vl = vec3(0.0);\n"
    "        float vj = fract(sin(dot(floor(gl_FragCoord.xy), vec2(12.9898,78.233))) * 43758.5453);\n"
    "        for (int i = 0; i < 48; i++) {\n"
    "            if (i >= uNSpots) break;\n"
    "            vec4  sa  = uSpotA[i];\n"
    "            vec3  Lv  = sa.xyz - P;\n"
    "            float d   = length(Lv);\n"
    "            float rng = abs(sa.w);\n"
    "            if (d >= rng || d < 1e-4) continue;\n"
    "            vec3  L   = Lv / d;\n"
    "            vec4  sb  = uSpotB[i];\n"
    "            float cs  = dot(sb.xyz, -L);\n"
    "            if (cs <= sb.w) continue;\n"
    "            vec4  sc  = uSpotC[i];\n"
    "            float q   = clamp(1.0 - (d / rng) * (d / rng), 0.0, 1.0);\n"
    "            float k   = smoothstep(sb.w, sc.w, cs) * q * q\n"
    "                      * clamp((dot(n, L) + 0.5) / 1.5, 0.0, 1.0);\n"
    /* THE HEADLIGHT'S SHADOW: from a little way off the surface toward the lamp,
       stopping short of it so the lamp's own housing does not shadow its beam, through
       the depth buffer; a step that lands behind something nearer the camera, by less
       than a hull's thickness, is in its shadow. Jittered per pixel by the hash the
       occlusion uses (never a clock). Only on a surface turned toward the lamp: one
       turned away takes the wrapped Lambert's last light and no shadow, which marched
       would find its own back and come out as speckle. */
    "            if (sa.w < 0.0 && k > 0.004 && uSpotSteps > 0 && dot(n, L) > 0.05) {\n"
    "                vec3  sp    = P + n * 0.04 + L * uSpotStart;\n"
    "                float reach = max(d - uSpotNear - uSpotStart, 0.0);\n"
    "                for (int s = 0; s < 16; s++) {\n"
    "                    if (s >= uSpotSteps) break;\n"
    "                    float t  = (float(s) + vj) / float(uSpotSteps);\n"
    "                    vec3  wp = sp + L * (t * reach);\n"
    "                    vec4  cp = uVP * vec4(wp, 1.0);\n"
    "                    if (cp.w <= 0.0) continue;\n"
    "                    vec2  su = (cp.xy / cp.w) * 0.5 + 0.5;\n"
    "                    if (su.x < 0.0 || su.x > 1.0 || su.y < 0.0 || su.y > 1.0) continue;\n"
    "                    vec3  sw = fx_world(su, min(texture2D(uDepth, su).r, 0.999989));\n"
    "                    float dz = length(sw - uCamPos) - length(wp - uCamPos);\n"
    "                    if (dz < -0.0005 && dz > -uSpotThick) { k = 0.0; break; }\n"
    "                }\n"
    "            }\n"
    "            vl += sc.rgb * k;\n"
    "        }\n"
    "        col += vlBase * vl;\n"
    "    }\n"
    "\n"
    /* ---- the light a tiberium field gives off ----------------------------------------
       Read under the pixel from the field texture, so every crystal contributes and the
       glow follows the field's own outline. It is light from the ground, so it falls off
       with height above the ground under the pixel (a tank's hull takes it, its turret
       top less), lands mostly on surfaces that face up, and breathes slowly, each part
       of the field on its own phase from the cloud deck's noise. Added, like the other
       lights; gated on luminance so a shrouded cell stays black. */
    "    if (uTibLight > 0.0) {\n"
    "        vec4 tf = texture2D(uTibField, P.xz * uTibInv);\n"
    "        float above = max(P.y - (uTibH.x + tf.b * uTibH.y), 0.0);\n"
    "        float ph = texture2D(uCloudTex, P.xz * 0.025).r;\n"
    "        float br = 1.0 - uTibPulse * 0.3 + uTibPulse * 0.3 * sin(6.2831853 * (0.21 * uTibTime + ph));\n"
    "        float g = tf.r * exp(-above / uTibLightH) * br * uTibLight * (0.40 + 0.60 * max(n.y, 0.0));\n"
    /* mostly a light ON the surface, so dark grass takes a tint and a pale hull takes
       more, with a little added on top so it still reads on black */
    "        col += (col * 2.2 + vec3(0.05)) * uTibCol * (g * rainLum);\n"
    "    }\n"
    /* ---- the glowing fog over a field ------------------------------------------------
       A march along the view ray from where it enters the fog's ceiling down to the
       surface. The density at each sample is the field's fog spread under it, times a
       falloff with height above the ground there, times wisps from two octaves of the
       cloud deck's noise drifting on the engine clock. Each sample is lit by the field's
       glow under it in the crystals' colour, plus a grey of its own; the light it adds
       is blocked by the fog in front of it (front to back), and the surface behind is
       dimmed by all of it. Jittered per pixel by the same deterministic hash the rain
       fog uses, so a short march reads as grain rather than bands. */
    "    if (uTibFog > 0.0) {\n"
    "        vec3 rd = P - uCamPos;\n"
    "        float len = length(rd);\n"
    "        rd /= max(len, 1e-4);\n"
    "        float t0 = 0.0;\n"
    "        if (uCamPos.y > uTibFogTop && rd.y < -1e-4) t0 = (uTibFogTop - uCamPos.y) / rd.y;\n"
    "        t0 = clamp(t0, 0.0, len);\n"
    "        float dt = (len - t0) / float(uTibFogSteps);\n"
    "        float jit = fract(sin(dot(floor(gl_FragCoord.xy), vec2(12.9898,78.233))) * 43758.5453);\n"
    "        float trans = 1.0;\n"
    "        vec3  inl = vec3(0.0);\n"
    "        vec3  grey = vec3(0.62, 0.66, 0.64) * uTibFogAmb;\n"
    /* the fog is paler than the light it carries: a glowing mist reads as mist only
       where it is lighter than the lit ground under it */
    "        vec3  tibFogC = mix(uTibCol, vec3(1.0), 0.40);\n"
    "        for (int i = 0; i < 32; i++) {\n"
    "            if (i >= uTibFogSteps) break;\n"
    "            vec3 sp = uCamPos + rd * (t0 + (float(i) + jit) * dt);\n"
    "            vec4 tf = texture2D(uTibField, sp.xz * uTibInv);\n"
    "            float hgt = sp.y - (uTibH.x + tf.b * uTibH.y);\n"
    "            float hf = hgt < 0.0 ? 1.0 : exp(-hgt * 1.0986 / uTibFogH);\n"
    "            vec2 q = sp.xz * 0.06;\n"
    "            float n1 = texture2D(uCloudTex, q + vec2(uTibFogDrift * 0.031, uTibFogDrift * 0.011)).r;\n"
    "            float n2 = texture2D(uCloudTex, q * 2.9 - vec2(uTibFogDrift * 0.047, -uTibFogDrift * 0.023)).r;\n"
    "            float nz = n1 * 0.6 + n2 * 0.4;\n"
    "            float wisp = mix(1.0, smoothstep(0.38, 0.68, nz) * 2.2, uTibFogWisp);\n"
    "            float dens = tf.g * hf * wisp * uTibFog;\n"
    "            float a = 1.0 - exp(-dens * dt);\n"
    "            vec3 c = grey + tibFogC * (uTibFogGlow * 0.40 * (0.25 + 0.75 * tf.r));\n"
    "            inl += trans * a * c;\n"
    "            trans *= 1.0 - a;\n"
    "        }\n"
    "        col = mix(col, col * trans + inl, rainLum);\n"
    "    }\n"
    "\n"
    /* ---- the distance fog ------------------------------------------------------------
       Every lit pixel past a set distance from the eye is drawn toward the theater's
       haze colour, rising smoothly over a set depth to its fullest. Under this tilted
       camera the far pixels are the top of the screen, so the top sinks into the haze
       and the middle keeps its colour; a unit standing out there fades with the ground
       under it, because the test is on the pixel's own position. Gated on luminance,
       the same gate the rain uses, so the shroud and the void past the map edge stay
       black rather than turning into a pale fog bank. */
    "    if (uFogAmt > 0.0) {\n"
    "        float fd = clamp((length(P - uCamPos) - uFogNear) * uFogInvDepth, 0.0, 1.0);\n"
    "        float fog = fd * fd * (3.0 - 2.0 * fd) * uFogAmt * rainLum;\n"
    "        col = mix(col, uFogCol, fog);\n"
    "    }\n"
    "\n"
    /* ---- the rain, part three: the air -----------------------------------------------
       THE GROUND FOG is a short march along the view ray from where it enters the
       fog's ceiling down to the surface, through the cloud deck's own tiling noise
       read as two octaves drifting on the wind: eight steps, jittered per pixel by
       the same deterministic hash the occlusion uses, so the banding an eight-step
       march would otherwise draw becomes grain that never moves between runs. The
       density thins upward from the mainland level, so hills stand out of it and
       the sea sits deeper in it. It is a uniform branch and the taps take implicit
       derivatives, which is the rule the note above P and n is about. Gated on
       luminance so the shroud stays black. Then the distance haze toward the
       overcast, and a cool grey over everything, gated the same way. */
    "    if (uRainFog > 0.0) {\n"
    "        vec3 rd = P - uCamPos;\n"
    "        float len = length(rd);\n"
    "        rd /= max(len, 1e-4);\n"
    "        float top = uRainFogBase + 4.0 * uRainFogHeight;\n"
    "        float t0 = 0.0;\n"
    "        if (uCamPos.y > top && rd.y < -1e-4) t0 = (top - uCamPos.y) / rd.y;\n"
    "        t0 = clamp(t0, 0.0, len);\n"
    "        float dt = (len - t0) / 8.0;\n"
    "        float jit = fract(sin(dot(floor(gl_FragCoord.xy), vec2(12.9898,78.233))) * 43758.5453);\n"
    "        vec2 fdx = uRainFogDir, fdy = vec2(-uRainFogDir.y, uRainFogDir.x);\n"
    "        float trans = 1.0;\n"
    "        for (int i = 0; i < 8; i++) {\n"
    "            vec3 sp = uCamPos + rd * (t0 + (float(i) + jit) * dt);\n"
    "            vec2 q = vec2(dot(sp.xz, fdx), dot(sp.xz, fdy)) * uRainFogInvScale;\n"
    "            float n1 = texture2D(uCloudTex, q - vec2(uRainFogDrift1, 0.0)).r;\n"
    "            float n2 = texture2D(uCloudTex, q * 2.7 - vec2(uRainFogDrift2, -0.37)).r;\n"
    "            float nz = n1 * 0.65 + n2 * 0.35;\n"
    "            float dens = mix(1.0, nz * nz * 2.2, uRainFogDetail);\n"
    "            float hgt = exp(-max(sp.y - uRainFogBase, 0.0) / uRainFogHeight);\n"
    "            trans *= exp(-uRainFog * 0.30 * dens * hgt * dt);\n"
    "        }\n"
    "        trans = mix(1.0, trans, rainLum);\n"
    "        col = mix(uRainSky * 0.92, col, trans);\n"
    "    }\n"
    "    if (uRainHaze > 0.0 || uRainOvercast > 0.0) {\n"
    "        float dist = length(P - uCamPos);\n"
    "        float fog = (1.0 - exp(-max(dist - uRainHazeNear, 0.0) * uRainHaze * 0.12)) * rainLum;\n"
    "        col = mix(col, uRainSky * 0.85, fog);\n"
    "        col *= mix(vec3(1.0), vec3(0.80, 0.82, 0.86), uRainOvercast);\n"
    "    }\n"
    "\n"
    "    gl_FragColor = vec4(col, 1.0);\n"
    "}\n";

static const char* FX_FS_BRIGHT =
    "#version 120\n"
    "varying vec2 uv;\n"
    "uniform sampler2D uTex, uExplosion;\n"
    "uniform float uExplosionGain, uMuzzleGain;\n"
    "uniform int uExplosionOn;\n"
    "uniform float uThreshold, uKnee, uClampIn;\n"
    "void main() {\n"
    "    vec3 c = texture2D(uTex, uv).rgb;\n"
    /* A CEILING ON WHAT MAY BLOOM, which an eight bit buffer used to provide for free at
       1.0. A microfacet highlight on an authored hull can reach the tens where the
       specular lobe is narrow and the surface is smooth, and one such texel smeared by
       four blur passes is a disc across the frame rather than a glint. Clamped rather
       than thresholded, so the highlight still blooms at full strength; only its
       runaway tail is cut. */
    "    c = min(c, vec3(uClampIn));\n"
    "    float l = max(c.r, max(c.g, c.b));\n"
    /* Soft knee, so a slider crossing the threshold fades things in instead of
       switching them on. A hard cut reads as popping when a unit drives into sunlight. */
    "    float k = max(uKnee, 1e-4);\n"
    "    float w = clamp((l - uThreshold + k) / (2.0 * k), 0.0, 1.0);\n"
    "    w = w * w * max(l - uThreshold + k, 0.0);\n"
    "    w = max(w, l - uThreshold);\n"
    "    w = max(w, 0.0);\n"
    "    vec3 halo = c * (w / max(l, 1e-4));\n"
    "    if (uExplosionOn == 1) {\n"
    "        vec3 ep = texture2D(uExplosion, uv).rgb;\n"
    "        float coverage = smoothstep(0.01, 0.35, ep.r);\n"
    "        halo *= mix(1.0, uExplosionGain, coverage);\n"
    "        halo *= mix(1.0, uMuzzleGain, smoothstep(0.01,0.35,ep.g));\n"
    "    }\n"
    "    gl_FragColor = vec4(halo, 1.0);\n"
    "}\n";

static const char* FX_FS_BLUR =
    "#version 120\n"
    "varying vec2 uv;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uDir;\n"
    "void main() {\n"
    /* Nine-tap gaussian, separable, run once per axis per ping-pong. */
    "    vec3 c = texture2D(uTex, uv).rgb * 0.2270270270;\n"
    "    c += texture2D(uTex, uv + uDir * 1.3846153846).rgb * 0.3162162162;\n"
    "    c += texture2D(uTex, uv - uDir * 1.3846153846).rgb * 0.3162162162;\n"
    "    c += texture2D(uTex, uv + uDir * 3.2307692308).rgb * 0.0702702703;\n"
    "    c += texture2D(uTex, uv - uDir * 3.2307692308).rgb * 0.0702702703;\n"
    "    gl_FragColor = vec4(c, 1.0);\n"
    "}\n";

/* ---- Pass 4: resolve ---------------------------------------------------------------
   Supersample down, add the bloom, grade, and apply the VI gamma LAST.

   THE GAMMA GOES HERE AND NOT IN fx_present, and the reason is a fidelity one. It is
   the console's OUTPUT transform, and what it is entitled to transform is what the
   console drew. The sidebar is 1995 MS-DOS art off a PC CD that never went near an
   N64's video interface, so putting the gamma after the sidebar would apply a console
   correction to PC pixels. It stops at the edge of the tactical view, which is exactly
   where the cartridge's own picture stops. */
static const char* FX_FS_RESOLVE =
    "#version 120\n"
    "varying vec2 uv;\n"
    "uniform sampler2D uTex;\n"
    "uniform sampler2D uBloom;\n"
    "uniform vec2  uDstTexel;\n"
    "uniform int   uTapN;\n"
    "uniform int   uBloomOn;\n"
    "uniform float uBloomI;\n"
    "uniform int   uGradeOn;\n"
    "uniform float uShadowBlackLimit;\n"
    "uniform float uExposure, uContrast, uSaturation, uTemperature, uTint, uLift, uGain;\n"
    "uniform int   uGammaOn;\n"
    "uniform float uGamma;\n"
    "uniform int   uToneOn;\n"
    "uniform float uToneKnee;\n"
    /* THE HIGHLIGHT ROLLOFF, AND WHY IT IS NOT ACES.
       A film curve like ACES or AgX maps SCENE RADIANCE onto a screen, and it is only
       the right transform if the buffer holds radiance. This one does not. Nothing in
       the world pass linearises anything: the terrain atlas, the 1995 sprites and every
       pack texture are uploaded and modulated as they were authored, so what lands in
       the scene target is a gamma-encoded picture that happens now to have room above
       white. Putting a film curve on that is not colour science, it is a curve chosen
       by eye, and it costs a fifth of the frame's brightness for nothing: measured on
       the test map, a fitted ACES curve took the mean from 30.1 to 7.5 and the median
       to zero.
       So this is the honest transform for the buffer that actually exists. Below the
       knee it is the IDENTITY, to the bit, which is what keeps every frame that never
       exceeds white exactly as it was and what keeps the gates that classify by
       absolute colour meaning what they meant. Above the knee it bends what used to be
       clipped flat into the remaining headroom, approaching white without reaching it.
       Continuous and with slope 1 at the knee, so there is no visible seam where it
       starts.
       When the world pass does one day linearise, THIS is the function that should be
       replaced by a real film curve, and not before. */
    "float fx_roll1(float x, float k) {\n"
    "    if (x <= k) return x;\n"
    "    float s = max(1.0 - k, 1e-4);\n"
    "    return k + s * (1.0 - exp(-(x - k) / s));\n"
    "}\n"
    "vec3 fx_rolloff(vec3 c, float k) {\n"
    "    return vec3(fx_roll1(c.r, k), fx_roll1(c.g, k), fx_roll1(c.b, k));\n"
    "}\n"
    "void main() {\n"
    "    vec3 c = vec3(0.0);\n"
    "    if (uTapN <= 1) {\n"
    "        c = texture2D(uTex, uv).rgb;\n"
    "    } else {\n"
    /* A grid of taps over exactly ONE destination pixel's footprint. For an integer
       render scale this is the exact box filter; for 1.5 or 2.5 it is a close one. */
    "        float w = 0.0;\n"
    "        for (int y = 0; y < 4; y++) {\n"
    "            if (y >= uTapN) break;\n"
    "            for (int x = 0; x < 4; x++) {\n"
    "                if (x >= uTapN) break;\n"
    "                vec2 o = ((vec2(float(x), float(y)) + 0.5) / float(uTapN) - 0.5)\n"
    "                         * uDstTexel;\n"
    "                c += texture2D(uTex, uv + o).rgb;\n"
    "                w += 1.0;\n"
    "            }\n"
    "        }\n"
    "        c /= max(w, 1.0);\n"
    "    }\n"
    "\n"
    "    if (uBloomOn == 1) c += texture2D(uBloom, uv).rgb * uBloomI;\n"
    "\n"
    "    vec3 toeInput=max(c,vec3(0.0));\n"
    "    if (uGradeOn == 1) {\n"
    "        c *= exp2(uExposure);\n"
    /* Temperature and tint as plain channel gains: warm lifts red and drops blue,\n"
       tint trades green against magenta. Kept simple on purpose, because the day a\n"
       real film curve is wanted it arrives as a LUT and replaces this block whole. */
    "        c *= vec3(1.0 + 0.30 * uTemperature - 0.10 * uTint,\n"
    "                  1.0 + 0.20 * uTint,\n"
    "                  1.0 - 0.30 * uTemperature - 0.10 * uTint);\n"
    "        c = c * uGain + uLift;\n"
    "        c = (c - 0.5) * uContrast + 0.5;\n"
    "        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
    "        c = mix(vec3(l), c, uSaturation);\n"
    "    }\n"
    /* A C1 toe replaces destructive negative clipping. It preserves ordering,
       texture chroma and zero radiance; the available sky scales its reach. */
    "    if (uShadowBlackLimit > 0.0) {\n"
    "        vec3 x=max(c,vec3(0.0));\n"
    "        vec3 t=vec3(uShadowBlackLimit)*(vec3(1.0)-exp(-toeInput*32.0));\n"
    "        c=max(c,x+t*exp(-x/max(t,vec3(0.00001))));\n"
    "    }\n"
    /* THE ROLLOFF IS LAST, immediately before the clamp, and it took a gate to establish
       that it belongs there. It was first placed between the grade's exposure and its
       contrast, on the reasoning that a film curve consumes scene light and the rest is
       a statement about the finished picture. That reasoning is correct for a film
       curve and this is not one: the contrast and gain that ran after it pushed every
       highlight it had just rescued straight back onto the clamp, and the measured
       rescue was ONE PIXEL in a frame with eighteen hundred clipped.
       Its job is to be the last thing between the pipeline and a display that stops at
       white. So it goes where the clamp is, and the clamp becomes the thing that only
       catches what is still out of range afterwards. */
    "    if (uToneOn == 1) c = fx_rolloff(c, uToneKnee);\n"
    "    c = clamp(c, 0.0, 1.0);\n"
    "    if (uGammaOn == 1) c = pow(c, vec3(uGamma));\n"
    "    gl_FragColor = vec4(c, 1.0);\n"
    "}\n";

static const char* FX_FS_CRT =
    "#version 120\n"
    "varying vec2 uv;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2  uSize;\n"
    "uniform float uScan, uMask, uCurve, uBleed, uVig;\n"
    "void main() {\n"
    "    vec2 t = uv;\n"
    "    if (uCurve > 0.0) {\n"
    "        vec2 p = t * 2.0 - 1.0;\n"
    "        float r2 = dot(p, p);\n"
    "        p *= 1.0 + uCurve * r2;\n"
    "        t = p * 0.5 + 0.5;\n"
    "        if (t.x < 0.0 || t.x > 1.0 || t.y < 0.0 || t.y > 1.0) {\n"
    "            gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0); return;\n"
    "        }\n"
    "    }\n"
    "    vec3 c = texture2D(uTex, t).rgb;\n"
    /* Composite bleed smears to the RIGHT only, the way a real signal's rise time
       does: the beam is still catching up with the last transition. A symmetric blur
       is just a blur and reads as an out-of-focus monitor instead of a cable. */
    "    if (uBleed > 0.0) {\n"
    "        float px = 1.0 / uSize.x;\n"
    "        vec3 s1 = texture2D(uTex, t - vec2(px, 0.0)).rgb;\n"
    "        vec3 s2 = texture2D(uTex, t - vec2(px * 2.0, 0.0)).rgb;\n"
    "        c = mix(c, c * 0.55 + s1 * 0.30 + s2 * 0.15, uBleed);\n"
    "    }\n"
    /* Scanlines off the OUTPUT row, so they stay one pixel apart whatever the window
       size and never alias into a moire against the render scale. */
    "    float sl = abs(sin(t.y * uSize.y * 3.14159265));\n"
    "    c *= mix(1.0, sl, uScan * 0.85);\n"
    /* The aperture grille: three-phosphor triads across the screen. */
    "    float ph = mod(floor(gl_FragCoord.x), 3.0);\n"
    "    vec3 mask = vec3(ph == 0.0 ? 1.0 : 0.72,\n"
    "                     ph == 1.0 ? 1.0 : 0.72,\n"
    "                     ph == 2.0 ? 1.0 : 0.72);\n"
    "    c *= mix(vec3(1.0), mask, uMask);\n"
    /* Scanlines and the mask both take light away; give it back so moving the slider
       changes the TEXTURE of the picture and not its exposure. */
    "    c *= 1.0 + 0.55 * uScan + 0.25 * uMask;\n"
    "    if (uVig > 0.0) {\n"
    "        vec2 p = uv * 2.0 - 1.0;\n"
    "        c *= 1.0 - uVig * 0.6 * dot(p, p);\n"
    "    }\n"
    "    gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);\n"
    "}\n";

static const char* FX_FS_COPY =
    "#version 120\n"
    "varying vec2 uv;\n"
    "uniform sampler2D uTex;\n"
    "void main() { gl_FragColor = vec4(texture2D(uTex, uv).rgb, 1.0); }\n";

/* The host owns theatre and visibility; air volumes run before bloom. */
static const FxRT* atmosphere_apply(const FxRT* source, int w, int h);
static void atmosphere_shutdown(void);

/* ---- Boot ------------------------------------------------------------------------ */

/* Generate the cloud mask and hand it to GL, once.

   THE MIP CHAIN IS UPLOADED LEVEL BY LEVEL rather than asked for, and that is a
   deliberate refusal to widen this file's contract. fx_gl.h resolves its entry points
   one at a time and treats a missing REQUIRED one as a reason to switch the entire
   chain off; glGenerateMipmap is not among the ones it resolves, and adding it would
   put shadows, occlusion, bloom, the grade and the CRT behind a pointer that only this
   one texture needs. Everything used below is OpenGL 1.1 and cannot be absent.

   A failure here is not fatal to anything else: g_fxCloudTex stays 0, the uniform
   binding sees that and forces uCloudOn off, and the rest of the chain runs. */
static void fx_cloud_boot(void)
{
    if (g_fxCloudTex) return;

    int levels = 0, off[16];
    unsigned char* tex = fxc_bake(&levels, off);
    if (!tex) {
        fprintf(stderr, "FX|cloud|the mask could not be generated; cloud shadows stay off\n");
        return;
    }

    glGenTextures(1, &g_fxCloudTex);
    glBindTexture(GL_TEXTURE_2D, g_fxCloudTex);
    /* REPEAT is the whole point of the thing: the mask is periodic by construction
       (fx_cloud.h), so the wrap is not a join, it is the continuation. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int L = 0, s = FXC_SIZE; L < levels; L++, s >>= 1)
        glTexImage2D(GL_TEXTURE_2D, L, GL_LUMINANCE, s, s, 0,
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, tex + off[L]);
    glBindTexture(GL_TEXTURE_2D, 0);
    free(tex);
    fprintf(stderr, "FX|cloud|mask generated, %dx%d, %d levels\n", FXC_SIZE, FXC_SIZE, levels);
}

static int fx_boot(void)
{
    if (g_fxBooted) return g_fxUsable;
    g_fxBooted = 1;
    if (!fx_gl_load()) { g_fxUsable = 0; return 0; }

    /* ONCE PER PROCESS, not once per mission: fx_shutdown says why. */
    if (!g_fxBuilt) {
        /* The lighting program is three strings: its uniforms, the shared reconstruction,
           then its body. Concatenated here because GLSL 1.20 has no #include and a
           three-source glShaderSource call is harder to read than one buffer. */
        size_t n = strlen(FX_FS_LIGHT) + strlen(FX_COMMON) + strlen(RAIN_GLSL_RIPPLE)
                 + strlen(RAIN_GLSL_PUDDLE) + strlen(FX_FS_LIGHT_BODY) + 1;
        char* lit = (char*)malloc(n);
        if (!lit) return 0;
        strcpy(lit, FX_FS_LIGHT); strcat(lit, FX_COMMON); strcat(lit, RAIN_GLSL_RIPPLE);
        strcat(lit, RAIN_GLSL_PUDDLE);
        strcat(lit, FX_FS_LIGHT_BODY);

        g_fxPLight   = fx_program(lit, "light");
        free(lit);
        g_fxPBright  = fx_program(FX_FS_BRIGHT,  "bright");
        g_fxPBlur    = fx_program(FX_FS_BLUR,    "blur");
        g_fxPResolve = fx_program(FX_FS_RESOLVE, "resolve");
        g_fxPCrt     = fx_program(FX_FS_CRT,     "crt");
        g_fxPCopy    = fx_program(FX_FS_COPY,    "copy");
        g_fxBuilt = 1;
    }

    g_fxUsable = (g_fxPLight && g_fxPBright && g_fxPBlur &&
                  g_fxPResolve && g_fxPCrt && g_fxPCopy) ? 1 : 0;
    /* After the programs, and only if they built: an unusable chain has no reader for
       the mask, and generating a quarter of a megabyte of noise for nobody is a boot
       cost paid on exactly the machines least able to afford it. */
    if (g_fxUsable) fx_cloud_boot();
    if (!g_fxUsable)
        fprintf(stderr, "FX|unavailable|a program failed to build; the chain stays off\n");
    else
        fprintf(stderr, "FX|ready|shaders and framebuffer objects present\n");
    /* AND ON STDOUT, WHERE A GATE CAN READ IT. One shader that fails to compile takes
       the whole chain down, and a chain that is off still draws a complete picture --
       the Classic one. So the failure looks exactly like "the new feature changed
       nothing", which is the most expensive way a renderer can lie. This line lets a
       gate assert that the thing it is measuring was switched on at all. Found the hard
       way: one variable referenced from the wrong scope in the light program made four
       separate features appear to do nothing, in the same run, with no failure in
       sight. */
    printf("FX|chain|usable=%d|light=%d|bright=%d|blur=%d|resolve=%d|crt=%d|copy=%d\n",
           g_fxUsable, g_fxPLight ? 1 : 0, g_fxPBright ? 1 : 0, g_fxPBlur ? 1 : 0,
           g_fxPResolve ? 1 : 0, g_fxPCrt ? 1 : 0, g_fxPCopy ? 1 : 0);
    fflush(stdout);
    return g_fxUsable;
}

static void fx_shutdown(void)
{
    if (!g_fxBooted) return;
    atmosphere_shutdown();
    fx_rt_free(&g_fxScene);   fx_rt_free(&g_fxLit);
    fx_rt_free(&g_fxBake);
    fx_rt_free(&g_fxVehicleMask);
    fx_rt_free(&g_fxSpec);
    fx_rt_free(&g_fxBloomA);  fx_rt_free(&g_fxBloomB);
    fx_rt_free(&g_fxExplosion);
    fx_rt_free(&g_fxNativeA); fx_rt_free(&g_fxNativeB);
    fx_rt_free(&g_fxShadow);
    fx_rt_free(&g_fxTerrainShadow);
    g_fxTerrainShadowSerial++;
    g_fxTerrainShadowLive = 0;
    fx_rt_free(&g_fxInfShadow);
    /* THE PROGRAMS OUTLIVE THE MISSION, these six and the atmosphere's two, as the
       authored models and the Unity explosion already do: the process has one GL
       context, and they are built from constant source. They used to be deleted here and
       compiled again by the next mission, and on macOS's Metal-backed OpenGL that cost
       memory for good: each rebuilt program's first draws compiled render pipeline state
       inside the driver (under glEnd in fx_fullscreen_quad) that glDeleteProgram never
       gave back. Measured with malloc stack logging: about 620 KiB of live heap a mission,
       every mission, all of it under these passes (some 475 KiB the chain's, 145 KiB the
       atmosphere's), which put the app round trip's sixteen-mission trend at +11347 KiB
       against its 6000 KiB limit. Carrying a program into the next mission changes
       nothing it draws: every uniform these programs read is set on every frame that
       draws with them, which is already what lets one mission's frames follow each
       other. */
    if (g_fxCloudTex) { glDeleteTextures(1, &g_fxCloudTex); g_fxCloudTex = 0; }
    g_fxBooted = 0; g_fxUsable = 0; g_fxActive = 0;
}

/* Is the chain going to run this frame? Asked by the renderer to decide whether to
   draw the cartridge's own shadows, so it must be answerable BEFORE fx_world_begin. */
static int fx_shadows_replace_cartridge(void)
{
    return g_fx.enabled && g_fxUsable && (g_fx.shadow_on || (g_fx.unified_lighting && !g_fx.dos_classic));
}

/* ---- Matrices --------------------------------------------------------------------- */

static void fx_capture_camera(void)
{
    float mv[16], pr[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, mv);
    glGetFloatv(GL_PROJECTION_MATRIX, pr);
    fx_mat_mul(pr, mv, g_fxVP);
    if (!fx_mat_invert(g_fxVP, g_fxInvVP))
        memset(g_fxInvVP, 0, sizeof g_fxInvVP);
    float imv[16];
    if (fx_mat_invert(mv, imv)) {
        g_fxCamPos[0] = imv[12]; g_fxCamPos[1] = imv[13]; g_fxCamPos[2] = imv[14];
    }
}

/* An orthographic look-at, built by hand into GL's column-major order. Written out
   rather than composed from glOrtho and gluLookAt because the sun's matrix has to
   exist as NUMBERS for the shader uniform as well as on the matrix stack, and reading
   it back off the stack to get it would be the same arithmetic twice. */
/* The direction light TRAVELS: down from the sky, on the given bearing. Its own
   function because the ground's one-sun term wants it whether or not the
   shadow pass runs this frame; with it computed only inside the shadow matrix, a
   chain with shadows off lit every field against no direction at all. */
/* Does the chain want a float target this frame. Its own function because three
   allocations ask it and they must never disagree: a scene target in float beside a
   bloom target in fixed would tonemap one and clip the other.
   HIGH DYNAMIC RANGE IS RETIRED, so the answer is always no, whatever hdr_on says: the
   scene and the bloom are the eight bit targets, and the highlight rolloff, which keys
   off a live float target, has nothing to bend. hdr_on stays in the dial table only so
   older settings files still read cleanly, and fx_load sets it to 0. */
static inline int fx_hdr_want(void)
{
    return 0;
}

static void fx_sun_dir(void)
{
    const float DEG = 3.14159265358979f / 180.0f;
    const float az = g_fx.sun_az * DEG, el = g_fx.sun_el * DEG;
    float d[3];
    d[0] =  sinf(az) * cosf(el);
    d[1] = -sinf(el);
    d[2] = -cosf(az) * cosf(el);
    float dl = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    d[0] /= dl; d[1] /= dl; d[2] /= dl;
    g_fxSunDir[0] = d[0]; g_fxSunDir[1] = d[1]; g_fxSunDir[2] = d[2];
}

static void fx_sun_matrix(float* out)
{
    fx_sun_dir();
    float d[3];
    d[0] = g_fxSunDir[0]; d[1] = g_fxSunDir[1]; d[2] = g_fxSunDir[2];

    /* The box is fitted to what the camera can see, so the texel density follows the
       zoom instead of being a constant that is too coarse close in and wasted far out. */
    const float cx = (g_fxViewX0 + g_fxViewX1) * 0.5f + g_fx.shadow_ox;
    const float cz = (g_fxViewZ0 + g_fxViewZ1) * 0.5f + g_fx.shadow_oz;
    float ext = (g_fxViewX1 - g_fxViewX0);
    const float extz = (g_fxViewZ1 - g_fxViewZ0);
    if (extz > ext) ext = extz;
    if (ext < 4.0f) ext = 4.0f;
    /* 0.71 = half of the rectangle, times root two, so a box rotated to any bearing
       still covers the corners of the view rather than clipping them. */
    /* Keep the entire original receiver box when the user offsets its centre.
       At close zoom, fixed offsets can exceed its radius and the map's validity
       boundary cuts a diagonal through visible terrain. Expand coverage instead
       of changing saved tuning or blending missing shadows into the picture. */
    const float receiverSpan = std::max(1.0f, g_fx.shadow_span);
    float R = ext * 0.71f * receiverSpan
        + sqrtf(g_fx.shadow_ox*g_fx.shadow_ox + g_fx.shadow_oz*g_fx.shadow_oz);
    R += std::max(0.0f, g_fx.shadow_pen);
    // Leave room for the complete PCF kernel and the receiver normal offset.
    const float filterTexels = std::max(g_fx.shadow_soft, 1.0f) + g_fx.shadow_noff + 2.0f;
    R /= 1.0f - std::min(0.25f, 0.002f + 2.0f*filterTexels/std::max(g_fx.shadow_res, 64.0f));

    const float eye[3] = { cx - d[0]*R*3.0f, -d[1]*R*3.0f, cz - d[2]*R*3.0f };

    /* Basis: forward is the light direction, up is world up unless the sun is nearly
       overhead, in which case anything perpendicular will do. */
    float up[3] = { 0.0f, 1.0f, 0.0f };
    if (fabsf(d[1]) > 0.995f) { up[0] = 0.0f; up[1] = 0.0f; up[2] = -1.0f; }
    float s[3] = { d[1]*up[2] - d[2]*up[1], d[2]*up[0] - d[0]*up[2], d[0]*up[1] - d[1]*up[0] };
    float sl = sqrtf(s[0]*s[0] + s[1]*s[1] + s[2]*s[2]);
    s[0] /= sl; s[1] /= sl; s[2] /= sl;
    float u[3] = { s[1]*d[2] - s[2]*d[1], s[2]*d[0] - s[0]*d[2], s[0]*d[1] - s[1]*d[0] };

    /* view = rows (s, u, -d) with the eye translated out */
    float V[16];
    V[0]=s[0]; V[4]=s[1]; V[8] =s[2];  V[12] = -(s[0]*eye[0] + s[1]*eye[1] + s[2]*eye[2]);
    V[1]=u[0]; V[5]=u[1]; V[9] =u[2];  V[13] = -(u[0]*eye[0] + u[1]*eye[1] + u[2]*eye[2]);
    V[2]=-d[0];V[6]=-d[1];V[10]=-d[2]; V[14] =  (d[0]*eye[0] + d[1]*eye[1] + d[2]*eye[2]);
    V[3]=0;    V[7]=0;    V[11]=0;     V[15] = 1.0f;

    /* Fitted to the box rather than left generous. The eye sits 3R back along the
       light and the scene lies within about 1.42R of the centre, so everything worth
       drawing falls between 1.5R and 4.5R; a decade of slack either side buys nothing
       and costs depth precision, which is the whole currency of a shadow map. */
    const float nearp = R * 0.5f, farp = R * 5.5f;
    g_fxSunSpan = farp - nearp;
    g_fxSunR = R;
    /* the same box, kept as numbers for the host's caster cull */
    g_fxSunEye[0] = eye[0]; g_fxSunEye[1] = eye[1]; g_fxSunEye[2] = eye[2];
    g_fxSunS[0] = s[0]; g_fxSunS[1] = s[1]; g_fxSunS[2] = s[2];
    g_fxSunU[0] = u[0]; g_fxSunU[1] = u[1]; g_fxSunU[2] = u[2];
    g_fxSunNear = nearp; g_fxSunFar = farp;
    float P[16];
    memset(P, 0, sizeof P);
    P[0]  =  1.0f / R;
    P[5]  =  1.0f / R;
    P[10] = -2.0f / (farp - nearp);
    P[14] = -(farp + nearp) / (farp - nearp);
    P[15] =  1.0f;

    fx_mat_mul(P, V, out);
}

/* ---- THE RECEIVER WINDOW ------------------------------------------------------------
   The sun's box is fitted to the view and then made generous (shadow_span, the offset,
   and a low sun that stretches it four times its width along the light), so on a
   shipped map it reaches every revealed caster there is. But the three sun maps have
   exactly one reader, the light pass, and that pass reads a map only at pixels that
   carry depth, at the texel under the pixel's own world point (pushed out along its
   normal by the normal offset) plus a kernel of bounded radius. Every such world point
   lies inside the camera's frustum, which the light pass rebuilds from the very matrix
   kept here (g_fxInvVP), and on or above the lowest ground anything is drawn on (the
   host's floor, g_fxRecvFloor). So the texels any pixel can read all lie inside the
   map's projection of that clipped frustum, padded by the kernel, the normal offset and
   half a texel. That convex polygon is the window.

   A caster whose box projects wholly outside one of the window's edges has no fragment
   on any texel that is read. Depth-only rasterisation writes only the texel centres a
   primitive covers, and polygon offset moves the stored value, never the coverage. So
   leaving such a caster out changes the map only where nothing looks, and every texel
   that is looked at receives exactly the fragments it received before, in the same
   order and with the same state. The box, its resolution, the bias, the kernel and the
   shaders are untouched.

   The window is worked out in double from the float matrices the GPU is given, and
   padded by a texel and a half more than the reach needs, which is far beyond either
   precision's rounding. It fails open (culls nothing) whenever the host cannot vouch
   for a floor (the editor, a scene with no ground yet), when anything is not finite,
   and when the window would cover the whole box anyway.

   It is armed only around the sun casters' object loop and the soldiers' caster pass
   (g_fxRecvArmed), and asked only at reviewed sites, so the reflection, the normal
   target, the bake and the colour picture never see it.
   OPENCNC3D_SUN_RECV_CULL=0 restores the old path exactly. */
struct FxRecvWin {
    int on;                     /* planes valid and narrower than the box               */
    int n;                      /* planes: a*x + b*y <= c inside, in the map's own NDC   */
    double a[24], b[24], c[24];
    int nh;                     /* the unpadded hull, for the capture and the log        */
    double hx[24], hy[24];
    double m, need;             /* the pad, and the part of it a read actually reaches   */
    double area;                /* the padded hull's bounding box, as a share of the map */
};
static FxRecvWin g_fxRecvSun, g_fxRecvInf;
static const FxRecvWin* g_fxRecvArmed = 0;
static float g_fxRecvFloor = 0.0f;
static int   g_fxRecvFloorOk = 0;   /* the host vouches for a floor this frame */
/* this frame's work, for the SUNRECV line */
static unsigned long g_fxRecvObjTested = 0, g_fxRecvObjCulled = 0;
static unsigned long g_fxRecvChunkTested = 0, g_fxRecvChunkCulled = 0;
static unsigned long g_fxRecvPieceCulled = 0;

static bool fx_recv_on(void) { return mesh_perf_switch("OPENCNC3D_SUN_RECV_CULL"); }
/* OPENCNC3D_SUN_RECV_VERIFY=1: read the scene's depth back in the light pass and count
   every pixel whose reach falls outside a window it was culled against, and every pixel
   below the floor (fx_recv_verify). Nothing it does changes a pixel; it costs a full
   depth readback a frame, so it is for gates only. */
static bool fx_recv_verify_on(void)
{
    static int on = -1;
    if (on < 0) { const char* v = getenv("OPENCNC3D_SUN_RECV_VERIFY"); on = (v && *v && strcmp(v, "0")) ? 1 : 0; }
    return on == 1;
}

/* vp: the map's own matrix. kernel, noff: the light pass's tap radius and normal offset
   for this map, both in its texels (the light pass: uSSoft/uSPen, uSNormOff; uInfRad,
   uInfNOff). tap: the farthest tap's share of that radius. res: the map's resolution. */
static void fx_recv_window(const float* vp, double kernel, double tap, double noff, double res,
                           FxRecvWin* w)
{
    w->on = 0; w->n = 0; w->nh = 0; w->area = 1.0;
    if (!fx_recv_on() || !g_fxRecvFloorOk || res < 1.0) return;
    /* the reach of one read: the kernel's farthest tap, the normal offset at most its
       full length in either axis, and half a texel for the nearest texel the sample
       lands in; NDC is twice texture space */
    w->need = (2.0 * tap * fabs(kernel) + 2.0 * fabs(noff) + 1.0) / res;
    w->m    = (2.0 * fabs(kernel) + 2.0 * fabs(noff) + 4.0) / res;
    const double F = (double)g_fxRecvFloor;
    double C[8][3];
    for (int i = 0; i < 8; i++) {
        const double c[4] = { (i & 1) ? 1.0 : -1.0, (i & 2) ? 1.0 : -1.0, (i & 4) ? 1.0 : -1.0, 1.0 };
        double h[4];
        for (int r = 0; r < 4; r++) {
            h[r] = 0.0;
            for (int k = 0; k < 4; k++) h[r] += (double)g_fxInvVP[k * 4 + r] * c[k];
        }
        if (!std::isfinite(h[3]) || fabs(h[3]) < 1e-12) return;
        for (int k = 0; k < 3; k++) {
            C[i][k] = h[k] / h[3];
            if (!std::isfinite(C[i][k])) return;
        }
    }
    /* THE FRUSTUM ABOVE THE FLOOR, exactly: the corners on or above it, and every one of
       the twelve edges' crossings of it. Corner i takes x from bit 0, y from bit 1 and
       depth from bit 2, so two corners share an edge exactly when they differ in one bit. */
    double px[20], py[20];
    int np = 0;
    for (int i = 0; i < 8; i++)
        if (C[i][1] >= F) {
            const double* p = C[i];
            px[np] = (double)vp[0] * p[0] + (double)vp[4] * p[1] + (double)vp[8] * p[2] + (double)vp[12];
            py[np] = (double)vp[1] * p[0] + (double)vp[5] * p[1] + (double)vp[9] * p[2] + (double)vp[13];
            np++;
        }
    for (int i = 0; i < 8; i++)
        for (int bit = 1; bit <= 4; bit <<= 1) {
            const int j = i ^ bit;
            if (j < i) continue;
            const double di = C[i][1] - F, dj = C[j][1] - F;
            if (!((di < 0.0 && dj > 0.0) || (di > 0.0 && dj < 0.0))) continue;
            const double t = di / (di - dj);
            double p[3];
            for (int k = 0; k < 3; k++) p[k] = C[i][k] + t * (C[j][k] - C[i][k]);
            px[np] = (double)vp[0] * p[0] + (double)vp[4] * p[1] + (double)vp[8] * p[2] + (double)vp[12];
            py[np] = (double)vp[1] * p[0] + (double)vp[5] * p[1] + (double)vp[9] * p[2] + (double)vp[13];
            np++;
        }
    /* the map is orthographic, so its w is one and NDC is clip space */
    if (fabs((double)vp[3]) + fabs((double)vp[7]) + fabs((double)vp[11]) > 1e-9
        || fabs((double)vp[15] - 1.0) > 1e-6) return;
    if (np < 3) return;
    for (int i = 0; i < np; i++) if (!std::isfinite(px[i]) || !std::isfinite(py[i])) return;
    /* the convex hull, monotone chain, counter-clockwise */
    int ord[20];
    for (int i = 0; i < np; i++) ord[i] = i;
    for (int i = 1; i < np; i++)
        for (int j = i; j > 0; j--) {
            const int a = ord[j - 1], b = ord[j];
            if (px[a] < px[b] || (px[a] == px[b] && py[a] <= py[b])) break;
            ord[j - 1] = b; ord[j] = a;
        }
    int H[42], nh = 0;
    for (int pass = 0; pass < 2; pass++) {
        const int base = nh;
        for (int s = 0; s < np; s++) {
            const int i = pass ? ord[np - 1 - s] : ord[s];
            while (nh >= base + 2) {
                const int o = H[nh - 2], a = H[nh - 1];
                const double cr = (px[a] - px[o]) * (py[i] - py[o]) - (py[a] - py[o]) * (px[i] - px[o]);
                if (cr > 0.0) break;
                nh--;
            }
            H[nh++] = i;
        }
        nh--;                       /* each half's last point is the other's first */
    }
    if (nh < 3 || nh > 20) return;
    double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
    for (int k = 0; k < nh; k++) {
        const int i = H[k];
        w->hx[k] = px[i]; w->hy[k] = py[i];
        if (px[i] < x0) x0 = px[i];
        if (px[i] > x1) x1 = px[i];
        if (py[i] < y0) y0 = py[i];
        if (py[i] > y1) y1 = py[i];
    }
    w->nh = nh;
    /* THE PAD IS A SQUARE, a Minkowski sum: each edge moves out by m times the sum of its
       normal's two components, and the square's own four edges join them. */
    const double m = w->m;
    int n = 0;
    for (int k = 0; k < nh; k++) {
        const double ax = w->hx[k], ay = w->hy[k];
        const double bx = w->hx[(k + 1) % nh], by = w->hy[(k + 1) % nh];
        double nx = by - ay, ny = -(bx - ax);          /* outward, for counter-clockwise */
        const double l = sqrt(nx * nx + ny * ny);
        if (!(l > 1e-12)) continue;
        nx /= l; ny /= l;
        w->a[n] = nx; w->b[n] = ny; w->c[n] = nx * ax + ny * ay + m * (fabs(nx) + fabs(ny));
        n++;
    }
    w->a[n] =  1.0; w->b[n] =  0.0; w->c[n] =  x1 + m; n++;
    w->a[n] = -1.0; w->b[n] =  0.0; w->c[n] = -(x0 - m); n++;
    w->a[n] =  0.0; w->b[n] =  1.0; w->c[n] =  y1 + m; n++;
    w->a[n] =  0.0; w->b[n] = -1.0; w->c[n] = -(y0 - m); n++;
    w->n = n;
    const double cx0 = x0 - m < -1.0 ? -1.0 : x0 - m, cx1 = x1 + m > 1.0 ? 1.0 : x1 + m;
    const double cy0 = y0 - m < -1.0 ? -1.0 : y0 - m, cy1 = y1 + m > 1.0 ? 1.0 : y1 + m;
    w->area = (cx1 > cx0 && cy1 > cy0) ? (cx1 - cx0) * (cy1 - cy0) * 0.25 : 0.0;
    /* a window that holds all four corners of the map holds all of it: nothing to gain */
    bool covers = true;
    for (int k = 0; k < 4 && covers; k++) {
        const double qx = (k & 1) ? 1.0 : -1.0, qy = (k & 2) ? 1.0 : -1.0;
        for (int p = 0; p < n; p++)
            if (w->a[p] * qx + w->b[p] * qy > w->c[p]) { covers = false; break; }
    }
    w->on = covers ? 0 : 1;
}

/* Is a box, given in the space of the CURRENT matrices (clip = projection x modelview,
   column-major, gls_clip), wholly outside the armed window? Each edge becomes a plane in
   clip space (a*x + b*y - c*w), so a world box and a box under a pushed model matrix are
   asked the same way. Only ever true while a window is armed. Non-finite answers never
   reject. */
static bool fx_recv_box_outside(const float lo[3], const float hi[3], const float* clip)
{
    const FxRecvWin* w = g_fxRecvArmed;
    if (!w || !w->on) return false;
    for (int p = 0; p < w->n; p++) {
        double row[4];
        double scale = 0.0;
        for (int k = 0; k < 4; k++) {
            row[k] = w->a[p] * (double)clip[k * 4 + 0] + w->b[p] * (double)clip[k * 4 + 1]
                     - w->c[p] * (double)clip[k * 4 + 3];
            scale += fabs(row[k]);
        }
        /* the box's corner deepest inside this edge */
        double d = row[3];
        for (int k = 0; k < 3; k++) d += row[k] * (row[k] >= 0.0 ? (double)lo[k] : (double)hi[k]);
        if (std::isfinite(d) && d > 1e-4 + scale * 1e-6) return true;
    }
    return false;
}
/* A whole object's box, at the object-level sites (aux_object_outside, ebld_draw_object),
   against the current matrices. */
static bool fx_recv_object_outside(const float lo[3], const float hi[3])
{
    if (!g_fxRecvArmed || !g_fxRecvArmed->on) return false;
    g_fxRecvObjTested++;
    if (!fx_recv_box_outside(lo, hi, gls_clip())) return false;
    g_fxRecvObjCulled++;
    return true;
}

/* THE PROOF, run on the frame it guards (OPENCNC3D_SUN_RECV_VERIFY=1). Every pixel the
   light pass will light is rebuilt from the scene's own depth, as the shader does, and
   asked two things: is it on or above the floor, and does its whole reach (fx_recv_window's
   `need`) lie inside every edge of each window that culled this frame. A count above
   nought on either is a pixel that could have read a texel the culling changed. */
static void fx_recv_verify(void)
{
    if (!fx_recv_verify_on() || !g_fxScene.depth || g_fxScene.w <= 0 || g_fxScene.h <= 0) return;
    const int W = g_fxScene.w, Hh = g_fxScene.h;
    std::vector<float> depth((size_t)W * Hh);
    GLint active = GL_TEXTURE0, bound = 0, align = 4;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    glGetIntegerv(GL_PACK_ALIGNMENT, &align);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, g_fxScene.depth);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
    glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
    glPixelStorei(GL_PACK_ALIGNMENT, align);
    glActiveTexture((GLenum)active);
    const FxRecvWin* wins[2] = { &g_fxRecvSun, g_fxInfLive ? &g_fxRecvInf : 0 };
    const float* vps[2] = { g_fxSunVP, g_fxInfSunVP };
    long pixels = 0, below = 0, out[2] = { 0, 0 };
    double minY = 1e300, slack[2] = { 1e300, 1e300 };
    for (int j = 0; j < Hh; j++)
        for (int i = 0; i < W; i++) {
            double d = depth[(size_t)j * W + i];
            if (d >= 0.99999) continue;
            if (d > 0.999989) d = 0.999989;
            const double c[4] = { (i + 0.5) / W * 2.0 - 1.0, (j + 0.5) / Hh * 2.0 - 1.0, d * 2.0 - 1.0, 1.0 };
            double h[4];
            for (int r = 0; r < 4; r++) {
                h[r] = 0.0;
                for (int k = 0; k < 4; k++) h[r] += (double)g_fxInvVP[k * 4 + r] * c[k];
            }
            const double P[3] = { h[0] / h[3], h[1] / h[3], h[2] / h[3] };
            pixels++;
            if (P[1] < minY) minY = P[1];
            if (P[1] < (double)g_fxRecvFloor) below++;
            for (int m = 0; m < 2; m++) {
                const FxRecvWin* w = wins[m];
                if (!w || !w->on) continue;
                const float* vp = vps[m];
                const double qx = vp[0] * P[0] + vp[4] * P[1] + vp[8] * P[2] + vp[12];
                const double qy = vp[1] * P[0] + vp[5] * P[1] + vp[9] * P[2] + vp[13];
                double worst = -1e300;
                for (int p = 0; p < w->n; p++) {
                    const double s = w->a[p] * qx + w->b[p] * qy
                                     + w->need * (fabs(w->a[p]) + fabs(w->b[p])) - w->c[p];
                    if (s > worst) worst = s;
                }
                if (worst > 0.0) out[m]++;
                if (-worst < slack[m]) slack[m] = -worst;
            }
        }
    printf("SUNRECV|verify|pixels=%ld|floor=%.4f|min_y=%.4f|below_floor=%ld"
           "|sun=%s|sun_violations=%ld|sun_min_slack_texels=%.2f"
           "|inf=%s|inf_violations=%ld|inf_min_slack_texels=%.2f\n",
           pixels, g_fxRecvFloor, minY, below,
           g_fxRecvSun.on ? "on" : "off", out[0], g_fxRecvSun.on ? slack[0] * g_fxShadow.w * 0.5 : 0.0,
           (wins[1] && g_fxRecvInf.on) ? "on" : "off", out[1],
           (wins[1] && g_fxRecvInf.on) ? slack[1] * g_fxInfShadow.w * 0.5 : 0.0);
}

static void fx_recv_log(void)
{
    static int on = -1;
    if (on < 0) { const char* v = getenv("OPENCNC3D_SUN_RECV_LOG"); on = ((v && *v && strcmp(v, "0")) || fx_recv_verify_on()) ? 1 : 0; }
    if (on != 1) return;
    printf("SUNRECV|floor=%.4f|floor_ok=%d|sun=%s|sun_area=%.3f|inf=%s|inf_area=%.3f"
           "|objects=%lu/%lu|chunks=%lu/%lu|pieces_culled=%lu\n",
           g_fxRecvFloor, g_fxRecvFloorOk, g_fxRecvSun.on ? "on" : "off", g_fxRecvSun.area,
           g_fxRecvInf.on ? "on" : "off", g_fxRecvInf.area,
           g_fxRecvObjCulled, g_fxRecvObjTested, g_fxRecvChunkCulled, g_fxRecvChunkTested,
           g_fxRecvPieceCulled);
}

/* ---- The shadow map --------------------------------------------------------------
   Depth only, colour writes off, and drawn by the HOST's own caster function, which
   calls the same draw_terrain / draw_object_mesh / draw_walls the tactical view uses.
   That matters: a shadow cast by a different mesh from the one on screen is a bug
   waiting to be found on the one model where the two disagree. */
static float fx_terrain_shadow_strength(void);

static void fx_render_shadow_map(void)
{
    const int res = (int)(g_fx.shadow_res + 0.5f);
    if (!fx_rt_init(&g_fxShadow, res, res, 2, GL_NEAREST)) return;
    if (!g_fxCasters) return;

    /* Neutral retains the original combined map with no second target or fetch.
       At other strengths draw ground into its own target and objects into the
       original one. Both use the same sun matrix, bias and PCF taps. */
    if (g_fx.shadow_terrain && fx_terrain_shadow_strength() > 0.0f
        && fx_terrain_shadow_strength() != 1.0f) {
        /* fx_rt_init keeps a target already at this size and format, and makes it anew
           otherwise */
        if (!(g_fxTerrainShadow.fbo && g_fxTerrainShadow.w == res && g_fxTerrainShadow.h == res
              && g_fxTerrainShadow.hdr == 0))
            g_fxTerrainShadowSerial++;
        g_fxTerrainShadowLive = fx_rt_init(&g_fxTerrainShadow, res, res, FX_RT_DEPTH, GL_NEAREST);
    }
    fx_sun_matrix(g_fxSunVP);
    {
        /* the shared map's reach (the light pass): twelve taps on a disc whose radius is
           the larger of the texel softness and the penumbra in texels, the farthest tap
           at 0.9924 of it (pd[11]); the normal offset is shadow_noff texels */
        const double cpt = 2.0 * (double)g_fxSunR / (double)res;
        const double pen = cpt > 0.0 ? fabs((double)g_fx.shadow_pen) / cpt : 0.0;
        const double soft = fabs((double)g_fx.shadow_soft);
        fx_recv_window(g_fxSunVP, soft > pen ? soft : pen, 0.9924, g_fx.shadow_noff,
                       (double)g_fxShadow.w, &g_fxRecvSun);
    }

    /* The draw and read buffers were set to GL_NONE when this FBO was created and
       that state belongs to the OBJECT, so it does not have to be set again here --
       and must not be undone on the way out either, which is why nothing below
       touches them. Calling glDrawBuffer(GL_BACK) while a framebuffer object is bound
       is an error, not a tidy-up. */
    fx_glBindFramebuffer(GL_FRAMEBUFFER, g_fxShadow.fbo);
    glViewport(0, 0, res, res);
    glClear(GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadMatrixf(g_fxSunVP);
    /* The whole sun transform is loaded on the MODELVIEW stack with an identity
       projection. It is one matrix either way, and keeping it on one stack means the
       caster draw cannot accidentally be given half of it. */

    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    /* Slope-scaled offset in ADDITION to the shader's bias. The two answer different
       halves of the same problem: this one moves the stored depth, the shader's moves
       the comparison, and a ground plane lit at a grazing angle needs both. */
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);

    g_fxCasterPass = FXCAST_SUN;
    g_fxCasters();

    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);  glPopMatrix();
}

/* THE SOLDIERS' BOX: the same sun and the same basis as fx_sun_matrix, centred on the
   view with no offset and sized to cover it once. The shared box is the view's extent
   times the span dial and carries an offset, because a wood or a cliff off screen
   casts into the frame; a soldier off screen casts at most his own shadow's length,
   which is the margin added here (the tallest of them is 0.55 of a cell, and his
   shadow reaches that over the tangent of the sun's height). */
static void fx_inf_sun_matrix(float* out)
{
    fx_sun_dir();
    const float d[3] = { g_fxSunDir[0], g_fxSunDir[1], g_fxSunDir[2] };
    const float cx = (g_fxViewX0 + g_fxViewX1) * 0.5f;
    const float cz = (g_fxViewZ0 + g_fxViewZ1) * 0.5f;
    float ext = (g_fxViewX1 - g_fxViewX0);
    const float extz = (g_fxViewZ1 - g_fxViewZ0);
    if (extz > ext) ext = extz;
    if (ext < 4.0f) ext = 4.0f;
    const float el = (g_fx.sun_el > 8.0f ? g_fx.sun_el : 8.0f) * 3.14159265f / 180.0f;
    const float reach = 0.6f / tanf(el);
    const float R = ext * 0.71f + reach;
    const float eye[3] = { cx - d[0]*R*3.0f, -d[1]*R*3.0f, cz - d[2]*R*3.0f };
    float up[3] = { 0.0f, 1.0f, 0.0f };
    if (fabsf(d[1]) > 0.995f) { up[0] = 0.0f; up[1] = 0.0f; up[2] = -1.0f; }
    float s[3] = { d[1]*up[2] - d[2]*up[1], d[2]*up[0] - d[0]*up[2], d[0]*up[1] - d[1]*up[0] };
    const float sl = sqrtf(s[0]*s[0] + s[1]*s[1] + s[2]*s[2]);
    s[0] /= sl; s[1] /= sl; s[2] /= sl;
    const float u[3] = { s[1]*d[2] - s[2]*d[1], s[2]*d[0] - s[0]*d[2], s[0]*d[1] - s[1]*d[0] };
    float V[16];
    V[0]=s[0]; V[4]=s[1]; V[8] =s[2];  V[12] = -(s[0]*eye[0] + s[1]*eye[1] + s[2]*eye[2]);
    V[1]=u[0]; V[5]=u[1]; V[9] =u[2];  V[13] = -(u[0]*eye[0] + u[1]*eye[1] + u[2]*eye[2]);
    V[2]=-d[0];V[6]=-d[1];V[10]=-d[2]; V[14] =  (d[0]*eye[0] + d[1]*eye[1] + d[2]*eye[2]);
    V[3]=0;    V[7]=0;    V[11]=0;     V[15] = 1.0f;
    const float nearp = R * 0.5f, farp = R * 5.5f;
    g_fxInfR = R;
    g_fxInfSpan = farp - nearp;
    float P[16];
    memset(P, 0, sizeof P);
    P[0]  =  1.0f / R;
    P[5]  =  1.0f / R;
    P[10] = -2.0f / (farp - nearp);
    P[14] = -(farp + nearp) / (farp - nearp);
    P[15] =  1.0f;
    fx_mat_mul(P, V, out);
}

/* Drawn BEFORE the shared map, so the shared caster pass can already see that the
   soldiers have a map of their own and leave them out of it: one soldier in both would
   put a smudge around the sharp shadow. Same state as the shared map, same host. */
static void fx_render_inf_shadow_map(void)
{
    g_fxInfLive = 0;
    if (!g_fxInfWant || !g_fxInfCasters) return;
    const int res = (int)(g_fx.shadow_res + 0.5f);
    if (!fx_rt_init(&g_fxInfShadow, res, res, 2, GL_NEAREST)) return;

    fx_inf_sun_matrix(g_fxInfSunVP);
    /* the soldiers' reach: eight taps out to the full radius, max(shadow_soft, 1) texels */
    fx_recv_window(g_fxInfSunVP, fabs((double)g_fx.shadow_soft) > 1.0 ? fabs((double)g_fx.shadow_soft) : 1.0,
                   1.0, g_fx.shadow_noff, (double)g_fxInfShadow.w, &g_fxRecvInf);
    fx_glBindFramebuffer(GL_FRAMEBUFFER, g_fxInfShadow.fbo);
    glViewport(0, 0, res, res);
    glClear(GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadMatrixf(g_fxInfSunVP);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);

    g_fxCasterPass = FXCAST_SUN;
    g_fxInfCasters();

    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);  glPopMatrix();
    g_fxInfLive = 1;
}

/* A MEASURING INSTRUMENT, not a debug leftover. The shadow map is the one buffer in
   the chain whose contents cannot be inferred from the picture: a map drawn from the
   wrong matrix and a map drawn correctly but biased away both look like "no shadows".
   Written as a plain PGM so nothing has to be linked to read it. */
static const char* g_fxShadowDump = 0;

static void fx_dump_shadow_map(void)
{
    if (!g_fxShadowDump || !g_fxShadow.depth) return;
    const int w = g_fxShadow.w, h = g_fxShadow.h;
    float* d = (float*)malloc((size_t)w * h * sizeof(float));
    if (!d) return;
    glBindTexture(GL_TEXTURE_2D, g_fxShadow.depth);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, d);
    glBindTexture(GL_TEXTURE_2D, 0);
    float lo = 1.0f, hi = 0.0f;
    long written = 0;
    for (long i = 0; i < (long)w * h; i++) {
        if (d[i] < lo) lo = d[i];
        if (d[i] > hi) hi = d[i];
        if (d[i] < 0.9999f) written++;
    }
    FILE* f = fopen(g_fxShadowDump, "wb");
    if (f) {
        fprintf(f, "P5\n%d %d\n255\n", w, h);
        for (long i = 0; i < (long)w * h; i++) {
            const float t = (hi > lo) ? (d[i] - lo) / (hi - lo) : 0.0f;
            const unsigned char v = (unsigned char)(t * 255.0f + 0.5f);
            fwrite(&v, 1, 1, f);
        }
        fclose(f);
    }
    fprintf(stderr, "FX|shadowmap|%dx%d|written=%ld of %ld texels|depth %.6f..%.6f|%s\n",
            w, h, written, (long)w * h, lo, hi, g_fxShadowDump);
    free(d);
    g_fxShadowDump = 0;                 /* one dump per request, not one per frame */
}

/* Called from inside draw_frame, AFTER the camera is set and the view rectangle is
   known, so the sun's box is fitted to the ground the player can actually see this
   frame rather than to last frame's. Renders the map, then puts the world's own
   target and state back exactly as it found them. */
static void fx_shadow_stage(void)
{
    g_fxInfLive = 0;
    g_fxTerrainShadowLive = 0;
    g_fxRecvSun.on = g_fxRecvInf.on = 0;
    g_fxRecvArmed = 0;
    if (!g_fxActive || !g_fx.shadow_on) return;
    g_fxRecvObjTested = g_fxRecvObjCulled = g_fxRecvChunkTested = g_fxRecvChunkCulled = 0;
    g_fxRecvPieceCulled = 0;
    fx_render_inf_shadow_map();
    fx_render_shadow_map();
    g_fxRecvArmed = 0;
    fx_recv_log();
    fx_dump_shadow_map();
    fx_rt_bind(&g_fxScene);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glShadeModel(GL_SMOOTH);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* ---- The seams the renderer calls ------------------------------------------------ */

/* Returns the size the world should be drawn at. When the chain is off that is the
   window, unchanged, and nothing has been bound. */
static void fx_world_begin(int fbw, int fbh, int* outW, int* outH)
{
    *outW = fbw; *outH = fbh;
    g_fxActive = 0;
    g_fxW = fbw; g_fxH = fbh;

    if (!g_fx.enabled) return;
    if (!fx_boot())    return;

    float s = fx_clampf(g_fx.ss_scale, 1.0f, 4.0f);
    int rw = (int)(fbw * s + 0.5f), rh = (int)(fbh * s + 0.5f);
    /* A cap, because a 4x buffer on a 5K window is 100 megapixels and the honest
       failure there is "the slider does less than it says", not a stall. */
    /* HALVED WHILE THE CHAIN IS IN HIGH RANGE, because every scene-referred target
       doubles: at four times scale the scene, the lit buffer and the bloom ping-pong
       together are twice the bytes they were, in a THIRTY TWO BIT process whose driver
       may keep a system-memory copy of each. The failure this avoids is an allocation
       that comes back incomplete and switches the whole chain off, which would hand the
       player the Classic picture -- a far larger change than losing a step of
       supersampling. The comment above still holds: the slider does less than it says,
       and says so. */
    const long MAXPX = (fx_hdr_want() ? 20L : 40L) * 1000L * 1000L;
    while ((long)rw * rh > MAXPX && s > 1.0f) {
        s -= 0.25f;
        rw = (int)(fbw * s + 0.5f); rh = (int)(fbh * s + 0.5f);
    }

    /* THE SCENE TARGET IS WHERE HIGH RANGE BEGINS OR DOES NOT EXIST. Every pass after
       this one reads it, so asking for float here is what lets a highlight survive as
       far as the tonemap; with the switch off it is the eight bit target it always was
       and the whole chain behaves exactly as it did. */
    if (!fx_rt_init(&g_fxScene, rw, rh, FX_RT_COLOUR | FX_RT_DEPTH | fx_hdr_want(),
                    GL_LINEAR)) return;
    /* WHAT THE DRIVER ACTUALLY GAVE, SAID OUT LOUD, ONCE PER CHANGE. A float colour
       attachment is a request, and a driver that refuses it leaves the scene buffer
       clipped at white before the rolloff ever sees it: the curve then runs on nothing
       and the frame with it on is BYTE IDENTICAL to the frame with it off. That is the
       same lie the usable= line exists to stop, one level down -- the shaders all
       compiled, the switch is on, and the feature is absent. Printed on the transition
       rather than per frame, so a log carries it without drowning in it. */
    if (g_fxScene.hdr != g_fxHdrLive || !g_fxHdrSaid) {
        printf("FX|hdr|asked=%d|live=%d|%s\n",
               fx_hdr_want() ? 1 : 0, g_fxScene.hdr,
               g_fxScene.hdr  ? "float colour attachment granted"
               : !fx_hdr_want() ? "high range switched off: the scene buffer is the eight "
                                  "bit one it has always been"
               : "driver refused a float colour attachment: the scene buffer is eight "
                 "bit and the highlight rolloff cannot act");
        fflush(stdout);
        g_fxHdrSaid = 1;
    }
    g_fxHdrLive = g_fxScene.hdr;

    fx_rt_bind(&g_fxScene);
    g_fxActive = 1;
    *outW = rw; *outH = rh;
}

static void fx_draw_with(GLuint prog, const FxRT* dst)
{
    fx_rt_bind(dst);
    fx_glUseProgram(prog);
    fx_fullscreen_quad();
    fx_glUseProgram(0);
}

/* Everything between the last world draw and the first sidebar draw. */
/* THE RAIN'S SIDE OF THE LIGHT PASS, defined in rain_mod.h (included by the host after
   the sea, whose field it binds). Forward-declared here the way the terrain declares
   the coast's hooks: the light program is this file's, the rain's numbers are not. */
static void rain_light_uniforms(GLuint prog);
static void distfog_light_uniforms(GLuint prog);   /* distfog_mod.h */
static void tibglow_light_uniforms(GLuint prog);   /* tibglow_mod.h */
static void tibglow_light_bind(void);
static void tibglow_light_unbind(void);
static int terrain_visibility_bind(GLuint prog); /* atmosphere_mod.h */
static void rain_light_bind(GLuint prog);
static void rain_light_unbind(void);

static void (*g_fxAfterResolve)(void) = NULL;

static bool fx_desert_terrain(void);
static bool fx_snow_terrain(void);
static void fx_terrain_colour_balance(float out[3]);
static inline bool ra3d_own_ground(void);   /* cnc_eyes.cpp: Red Alert draws its own ground */

/* THE LEVEL THE LIGHT PASS MAY REACH BEFORE THE COLOUR GRADE CLIPS IT TO WHITE. The grade
   (FX_FS_RESOLVE) is exposure, the temperature and tint channel gains, gain and lift, then
   contrast about one half; this inverts that for an output of 1 on whichever channel gets
   there first. Saturation, bloom and the rolloff are left out: on pale ground they move
   the answer by little. 1 with the grade off, since the eight bit buffer stops there. */
static float fx_grade_white_in(void)
{
    if (!g_fx.grade_on || g_fx.contrast < 0.05f) return 1.0f;
    const float top = 0.5f / g_fx.contrast + 0.5f - g_fx.lift;
    const float e = exp2f(g_fx.exposure) * g_fx.gain;
    const float ch[3] = { 1.0f + 0.30f * g_fx.temperature - 0.10f * g_fx.tint,
                          1.0f + 0.20f * g_fx.tint,
                          1.0f - 0.30f * g_fx.temperature - 0.10f * g_fx.tint };
    float w = 1.0f;
    for (int i = 0; i < 3; i++)
        if (e * ch[i] > 1e-4f && top / (e * ch[i]) < w) w = top / (e * ch[i]);
    return w < 0.2f ? 0.2f : w;
}

static void fx_world_end(void)
{
    g_fxStructOn = 0;
    g_fxVehicleOn = 0;
    if (!g_fxActive) return;

    const int rw = g_fxScene.w, rh = g_fxScene.h;

    fx_sun_dir();      /* the sun exists whether or not it casts this frame */

    /* ---- 0. the ground's normal ----
       The host draws the terrain once more, untextured, with the console's per-corner
       normal as the colour, depth-tested (writes off) against the scene it just
       finished through the scene's own depth texture attached here for the duration.
       The camera is the captured VP loaded as one matrix, the way the sun's is; a hair
       of polygon offset toward the viewer covers the last-bit difference between that
       and the fixed pipe's frustum+modelview, or half the ground would fail its own
       depth by a rounding. */
    int gnOn = 0;
    const bool snow=fx_snow_terrain();
    const bool desert=fx_desert_terrain();
    const float terrainBrightness=g_fx.terrain_brightness*(snow?g_fx.snow_brightness:desert?g_fx.sand_brightness:1.0f);
    const float terrainContrast=g_fx.terrain_contrast*(snow?g_fx.snow_contrast:desert?g_fx.sand_contrast:1.0f);
    const float terrainSaturation=snow?g_fx.snow_saturation:1.0f;
    float terrainBalance[3];fx_terrain_colour_balance(terrainBalance);
    const int terrainGrade = terrainBrightness!=1.0f || terrainContrast!=1.0f || terrainSaturation!=1.0f
        || terrainBalance[0]!=1.0f || terrainBalance[1]!=1.0f || terrainBalance[2]!=1.0f;
    const int terrainArtShade = !fx_unified_lighting() && fx_texset_get() == FX_TEX_ENH && fx_terrain_shadow_strength() != 1.0f;
    if ((g_fx.terrain_normals || terrainGrade || terrainArtShade || fx_unified_lighting()) && g_fxGroundNormals && g_fxScene.depth
        && fx_rt_init(&g_fxGNorm, rw, rh, 1, GL_LINEAR)) {
        float clearc[4];
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clearc);
        fx_rt_bind(&g_fxGNorm);
        fx_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                                  g_fxScene.depth, 0);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadMatrixf(g_fxVP);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -2.0f);
        g_fxGroundNormals();
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDepthMask(GL_TRUE);
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW);  glPopMatrix();
        fx_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        glClearColor(clearc[0], clearc[1], clearc[2], clearc[3]);
        gnOn = 1;
    }
    fprof_mark("post: ground normals");

    int vhCardsOn = 0;
    if (g_fxVehicleCards && fx_vehicle_active(&g_fx) && g_fxScene.depth
        && fx_rt_init(&g_fxVehicleMask, rw, rh, FX_RT_COLOUR, GL_NEAREST)) {
        float clearc[4];
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clearc);
        fx_rt_bind(&g_fxVehicleMask);
        fx_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                                  g_fxScene.depth, 0);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadMatrixf(g_fxVP);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE);
        g_fxVehicleCards();
        glDepthMask(GL_TRUE);
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW); glPopMatrix();
        fx_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        glClearColor(clearc[0], clearc[1], clearc[2], clearc[3]);
        vhCardsOn = 1;
    }


    /* ---- THE BAKE TARGET: the console's own lighting, on its own ------------------
       The same arrangement as the normals above and for the same reasons: the scene's
       depth borrowed with LEQUAL and writes off, so only surfaces that actually won the
       frame contribute, and a hair of polygon offset to cover the rounding between this
       matrix and the fixed pipeline's. Cleared to zero, which is the class saying "no
       mesh here" for every pixel the pass does not reach.
       Eight bit on purpose: it holds a shade the console stored in eight bits. */
    int bakeOn = 0;
    if ((g_fx.relight_on || fx_unified_lighting()) && g_fxMeshBake && g_fxScene.depth
        && fx_rt_init(&g_fxBake, rw, rh, FX_RT_COLOUR, GL_LINEAR)) {
        float clearb[4];
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clearb);
        fx_rt_bind(&g_fxBake);
        fx_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                                  g_fxScene.depth, 0);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadMatrixf(g_fxVP);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        glDisable(GL_ALPHA_TEST);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -2.0f);
        g_fxMeshBake();
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDepthMask(GL_TRUE);
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW);  glPopMatrix();
        fx_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        glClearColor(clearb[0], clearb[1], clearb[2], clearb[3]);
        bakeOn = 1;
    }

    /* ---- THE SPECULAR TARGET: the vehicles' specular maps, the same arrangement ---- */
    int specOn = 0;
    if (gnOn && g_fxSpecDraw && g_fxSpecWant && g_fxSpecWant() && g_fxScene.depth
        && fx_rt_init(&g_fxSpec, rw, rh, FX_RT_COLOUR, GL_NEAREST)) {
        float clears[4];
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clears);
        fx_rt_bind(&g_fxSpec);
        fx_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                                  g_fxScene.depth, 0);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadMatrixf(g_fxVP);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        glDisable(GL_ALPHA_TEST);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -2.0f);
        g_fxSpecDraw();
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDepthMask(GL_TRUE);
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW);  glPopMatrix();
        fx_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        glClearColor(clears[0], clears[1], clears[2], clears[3]);
        specOn = 1;
    }
    fprof_mark("post: specular");

    /* ---- 1. shadow, occlusion and light ---- */
    if (g_fx.shadow_on && g_fxShadow.depth) fx_recv_verify();
    const FxRT* litSrc = &g_fxScene;
    if (fx_rt_init(&g_fxLit, rw, rh, FX_RT_COLOUR | fx_hdr_want(), GL_LINEAR)) {
        fx_glUseProgram(g_fxPLight);
        fx_set1i(g_fxPLight, "uColor", 0);
        fx_set1i(g_fxPLight, "uDepth", 1);
        fx_set1i(g_fxPLight, "uShadow", 2);
        fx_set1i(g_fxPLight, "uTerrainShadow", 8);
        fx_set1i(g_fxPLight, "uTerrainShadowOn", g_fxTerrainShadowLive);
        fx_set1f(g_fxPLight, "uTerrainStrength", fx_terrain_shadow_strength());
        fx_set1i(g_fxPLight, "uTerrainArtShadeOn", 0);
        const int terrainMaskOn = gnOn && (terrainGrade || terrainArtShade || fx_unified_lighting()) && terrain_visibility_bind(g_fxPLight);
        const int terrainGradeOn = terrainMaskOn && terrainGrade;
        fx_set1i(g_fxPLight, "uTerrainGradeOn", terrainGradeOn);
        fx_set2f(g_fxPLight, "uTerrainGrade", terrainBrightness, terrainContrast);
        fx_set1f(g_fxPLight,"uTerrainSaturation",terrainSaturation);
        fx_set3f(g_fxPLight,"uTerrainBalance",terrainBalance[0],terrainBalance[1],terrainBalance[2]);
        fx_setmat(g_fxPLight, "uVP", g_fxVP);
        fx_setmat(g_fxPLight, "uInvVP", g_fxInvVP);
        fx_set2f(g_fxPLight, "uDepthSize", (float)g_fxScene.w, (float)g_fxScene.h);
        fx_setmat(g_fxPLight, "uSunVP", g_fxSunVP);
        fx_set3f(g_fxPLight, "uCamPos", g_fxCamPos[0], g_fxCamPos[1], g_fxCamPos[2]);
        fx_set3f(g_fxPLight, "uSunDir", g_fxSunDir[0], g_fxSunDir[1], g_fxSunDir[2]);
        const int shOn = (g_fx.shadow_on && g_fxShadow.depth) ? 1 : 0;
        fx_set1i(g_fxPLight, "uShadowOn", shOn);
        fx_set1i(g_fxPLight, "uUnified", fx_unified_lighting()?1:0);
        fx_set1f(g_fxPLight, "uSunEnergy", fx_sun_energy());
        fx_set1f(g_fxPLight, "uSkyEnergy", fx_sky_energy());
        fx_set1f(g_fxPLight, "uTerrainAmbient", g_fx.terrain_ambient);
        fx_set1f(g_fxPLight, "uSStrength", g_fx.shadow_strength);
        fx_set1f(g_fxPLight, "uSSoft", g_fx.shadow_soft);
        /* Cells on the panel, normalised depth in the shader. */
        fx_set1f(g_fxPLight, "uSBias",
                 g_fx.shadow_bias / (g_fxSunSpan > 0.001f ? g_fxSunSpan : 1.0f));
        fx_set1f(g_fxPLight, "uSTexel", g_fxShadow.w ? 1.0f / (float)g_fxShadow.w : 0.0f);
        fx_set3f(g_fxPLight, "uSTint", g_fx.shadow_r, g_fx.shadow_g, g_fx.shadow_b);
        /* The smooth ground, the normal offset (texels on the panel, world cells in the
           shader: one shadow texel is 2R/res cells) and the penumbra (cells on the
           panel, texels in the shader, the same ratio the other way). */
        {
            const float res = g_fxShadow.w > 0 ? (float)g_fxShadow.w : 1.0f;
            const float cellsPerTexel = 2.0f * g_fxSunR / res;
            fx_set1i(g_fxPLight, "uGNorm", 4);
            fx_set1i(g_fxPLight, "uBake", 5);
            fx_set1i(g_fxPLight, "uBakeOn", bakeOn);
            fx_set1i(g_fxPLight, "uSpecMap", 14);
            fx_set1i(g_fxPLight, "uSpecOn", specOn);
            fx_set1f(g_fxPLight, "uSpecSun", g_fx.vspec_sun);
            fx_set1f(g_fxPLight, "uSpecSky", g_fx.vspec_sky);
            fx_set1f(g_fxPLight, "uSpecGloss", g_fx.vspec_gloss);
            fx_set1f(g_fxPLight, "uReRough", g_fx.relight_rough);
            fx_set1f(g_fxPLight, "uReSun",   g_fx.relight_sun);
            fx_set1f(g_fxPLight, "uReSpec",  g_fx.relight_spec);
            fx_set1f(g_fxPLight, "uReClamp", g_fx.relight_clamp);
            fx_set1f(g_fxPLight, "uReAmb",   g_fx.relight_amb);
            fx_set1f(g_fxPLight, "uReFloor", g_fx.relight_floor);
            fx_set1i(g_fxPLight, "uGNormOn", gnOn && (g_fx.terrain_normals || fx_unified_lighting()));
            fx_set1i(g_fxPLight, "uTerrainSmooth", g_fx.terrain_normals);
            /* SCREEN-SPACE OCCLUSION MEANS LESS ON A CANOPY THAN ON A CREASE. Its
               hemisphere is built from the same reconstructed normal, and with the
               radius against cards packed a cell across nearly every tap lands on
               another leaf. The trees carry occlusion baked from the model that still
               had a million leaves in it, so this one only has to stay out of the way. */
            fx_set1f(g_fxPLight, "uAoFoliage", g_fx.ssao_foliage);
            fx_set1f(g_fxPLight, "uTreeShadowFill", g_fx.etree_shadow_fill);
            fx_set1f(g_fxPLight, "uObjectShadowFill", g_fx.object_shadow_fill);
            fx_set1f(g_fxPLight, "uAoObject",  g_fx.ssao_object);
            fx_set1f(g_fxPLight, "uSNormOff", g_fx.shadow_noff * cellsPerTexel);
            fx_set1f(g_fxPLight, "uSunFloor", g_fx.sun_floor);
            fx_set1f(g_fxPLight, "uSPen", cellsPerTexel > 0.0f ? g_fx.shadow_pen / cellsPerTexel : 0.0f);
            fx_set1f(g_fxPLight, "uAoGround", g_fx.ssao_ground);
            fx_set1i(g_fxPLight, "uSunLambert", (g_fx.sun_lambert && gnOn) ? 1 : 0);
            /* Red Alert's ground only, under the highlight rolloff dial and its knee: its
               heightmapped snow is the pale ground that slopes. Tiberian Dawn's ground is
               not reached, so its pictures stay as they were. */
            fx_set1f(g_fxPLight, "uSlopeWhite",
                     (ra3d_own_ground() && g_fx.tonemap >= 0.5f) ? fx_grade_white_in() : 0.0f);
            fx_set1f(g_fxPLight, "uSlopeKnee", g_fx.tone_knee);
            /* THE STRUCTURES GROUP. Its pixels are known by their class in the normal
               target, so with no target there is nothing to tell them by and the group
               stands down rather than reaching everything. Gamma is passed as it is and
               inverted in the shader; its floor keeps the inversion finite. */
            const int stOn = (gnOn && g_fx.terrain_normals && fx_struct_active(&g_fx)) ? 1 : 0;
            fx_set1i(g_fxPLight, "uStOn", stOn);
            const float stGrade[4] = { fx_struct_dial(g_fx.st_gamma > 0.05f ? g_fx.st_gamma
                                                                         : 0.05f),
                                       fx_struct_dial(g_fx.st_bright),
                                       fx_struct_dial(g_fx.st_contrast),
                                       fx_struct_dial(g_fx.st_sat) };
            const float stLight[4] = { fx_struct_dial(g_fx.st_shade),
                                       fx_struct_dial(g_fx.st_sun),
                                       fx_struct_dial(g_fx.st_glow),
                                       fx_struct_dial(g_fx.st_ao) };
            fx_set4fv(g_fxPLight, "uStGrade", 1, stGrade);
            fx_set4fv(g_fxPLight, "uStLight", 1, stLight);
            g_fxStructOn = stOn;
            /* Vehicle classes share the same normal target, with independent dials. */
            const int vhOn = ((gnOn && g_fx.terrain_normals) || vhCardsOn)
                && fx_vehicle_active(&g_fx) ? 1 : 0;
            fx_set1i(g_fxPLight, "uVhOn", vhOn);
            fx_set1i(g_fxPLight, "uVhCards", 13);
            fx_set1i(g_fxPLight, "uVhCardsOn", vhCardsOn);
            const float vhGrade[4] = { fx_struct_dial(g_fx.vh_gamma > 0.05f ? g_fx.vh_gamma
                                                                         : 0.05f),
                                       fx_struct_dial(g_fx.vh_bright),
                                       fx_struct_dial(g_fx.vh_contrast),
                                       fx_struct_dial(g_fx.vh_sat) };
            const float vhLight[4] = { fx_struct_dial(g_fx.vh_shade),
                                       fx_struct_dial(g_fx.vh_sun),
                                       fx_struct_dial(g_fx.vh_glow),
                                       fx_struct_dial(g_fx.vh_ao) };
            fx_set4fv(g_fxPLight, "uVhGrade", 1, vhGrade);
            fx_set4fv(g_fxPLight, "uVhLight", 1, vhLight);
            g_fxVehicleOn = vhOn;
        }
        /* The soldiers' map, in the same units as the shared one: bias in cells over
           its own depth span, normal offset in its own texels, and a kernel that is
           its texel softness with a one texel floor. */
        {
            const int infOn = (shOn && g_fxInfLive && g_fxInfShadow.depth) ? 1 : 0;
            const float ires = g_fxInfShadow.w > 0 ? (float)g_fxInfShadow.w : 1.0f;
            const float icpt = 2.0f * g_fxInfR / ires;
            fx_set1i(g_fxPLight, "uInfShadow", 6);
            fx_set1i(g_fxPLight, "uInfShOn", infOn);
            /* The contact march's soldier test, off whenever no 3D soldier can be on
               screen, so every other look draws exactly the picture it drew before. */
            fx_set1i(g_fxPLight, "uCtSkipInf", g_fxInfSoldiers ? 1 : 0);
            fx_set2f(g_fxPLight, "uGNormSize", (float)(g_fxGNorm.w > 0 ? g_fxGNorm.w : 1),
                     (float)(g_fxGNorm.h > 0 ? g_fxGNorm.h : 1));
            fx_setmat(g_fxPLight, "uInfSunVP", g_fxInfSunVP);
            fx_set1f(g_fxPLight, "uInfBias",
                     g_fx.shadow_bias / (g_fxInfSpan > 0.001f ? g_fxInfSpan : 1.0f));
            fx_set1f(g_fxPLight, "uInfNOff", g_fx.shadow_noff * icpt);
            fx_set1f(g_fxPLight, "uInfRad",
                     (g_fx.shadow_soft > 1.0f ? g_fx.shadow_soft : 1.0f) / ires);
            glActiveTexture(GL_TEXTURE6);
            glBindTexture(GL_TEXTURE_2D, infOn ? g_fxInfShadow.depth : 0);
            glActiveTexture(GL_TEXTURE0);
        }

        /* ---- the weather ----
           Every one of these is a panel dial turned into the form the shader wants,
           and the arithmetic is here rather than in the shader so it happens once a
           frame instead of once a pixel. */
        const int clOn = (g_fx.cloud_on && g_fxCloudTex) ? 1 : 0;
        fx_set1i(g_fxPLight, "uCloudTex", 3);
        fx_set1i(g_fxPLight, "uCloudOn", clOn);
        fx_set1f(g_fxPLight, "uCStrength", g_fx.cloud_strength);
        /* COVERAGE IS A THRESHOLD, and it can be one because the mask is histogram
           equalised: the fraction of it above t is exactly 1-t. So the dial reads as
           the fraction of ground in shadow and stays true when the edge width moves. */
        fx_set1f(g_fxPLight, "uCEdge", 1.0f - fx_clampf(g_fx.cloud_cover, 0.0f, 1.0f));
        /* Half-width of the ramp. Never zero: smoothstep is undefined when its two
           edges meet, and the sharp end of the dial is one texel-ish, not a step. */
        fx_set1f(g_fxPLight, "uCWidth",
                 0.40f - 0.39f * fx_clampf(g_fx.cloud_sharp, 0.0f, 1.0f));
        const float clScale = g_fx.cloud_scale > 1.0f ? g_fx.cloud_scale : 1.0f;
        fx_set1f(g_fxPLight, "uCInvScale", 1.0f / clScale);
        fx_set1f(g_fxPLight, "uCHeight", g_fx.cloud_height);
        /* The bearing, read the same way sun_az is: a compass angle, and the deck
           travels along it. The mask is turned with the wind rather than drifting
           across a fixed pattern, so a change of bearing looks like the weather
           turning rather than like the ground rotating under it. */
        const float clRad = g_fx.cloud_rot * (3.14159265358979f / 180.0f);
        fx_set2f(g_fxPLight, "uCDir", sinf(clRad), -cosf(clRad));
        /* THE FOLD IS LOAD BEARING, not tidiness. The phase is in tile periods and the
           mask repeats every 1.0 of them, so fmodf into [0,1) is exact -- it removes
           nothing the shader could have seen. Without it the phase grows without bound
           and a long session hands the sampler a float whose spacing has coarsened past
           a texel, at which point the clouds stop drifting and start stepping. */
        fx_set1f(g_fxPLight, "uCDrift",
                 fmodf(g_fx.cloud_speed * g_fxTime / clScale, 1.0f));

        fx_set1i(g_fxPLight, "uAoOn", g_fx.ssao_on ? 1 : 0);
        fx_set1i(g_fxPLight, "uAoSamples", (int)(g_fx.ssao_samples + 0.5f));
        fx_set1f(g_fxPLight, "uAoRadius", g_fx.ssao_radius);
        fx_set1f(g_fxPLight, "uAoIntensity", g_fx.ssao_intensity);
        fx_set1f(g_fxPLight, "uAoBias", g_fx.ssao_bias);
        fx_set1f(g_fxPLight, "uAoPower", g_fx.ssao_power);
            fx_set1i(g_fxPLight, "uCtOn", g_fx.contact_on ? 1 : 0);
            fx_set1i(g_fxPLight, "uCtSteps", (int)(g_fx.contact_steps + 0.5f));
            fx_set1f(g_fxPLight, "uCtLen", g_fx.contact_len);
            fx_set1f(g_fxPLight, "uCtThick", g_fx.contact_thick);
            fx_set1f(g_fxPLight, "uCtBias", g_fx.contact_bias);
            fx_set1f(g_fxPLight, "uCtStrength", g_fx.contact_strength);

        int nl = g_fx.lights_on ? g_fxNLight : 0;
        if (nl > FX_MAX_LIGHTS) nl = FX_MAX_LIGHTS;
        float lp[FX_MAX_LIGHTS * 4], lc[FX_MAX_LIGHTS * 4];
        for (int i = 0; i < nl; i++) {
            lp[i*4+0] = g_fxLight[i].x;  lp[i*4+1] = g_fxLight[i].y;
            lp[i*4+2] = g_fxLight[i].z;  lp[i*4+3] = g_fxLight[i].radius;
            lc[i*4+0] = g_fxLight[i].r;  lc[i*4+1] = g_fxLight[i].g;
            lc[i*4+2] = g_fxLight[i].b;  lc[i*4+3] = g_fxLight[i].power;
        }
        fx_set1i(g_fxPLight, "uNLights", nl);
        if (nl > 0) {
            fx_set4fv(g_fxPLight, "uLightPos", nl, lp);
            fx_set4fv(g_fxPLight, "uLightCol", nl, lc);
        }
        fx_set1f(g_fxPLight, "uLightFalloff", g_fx.light_falloff);
        {
            /* the vehicles' lamps (FxSpot); their own slots */
            const int ns = g_fxNSpot > FX_MAX_SPOTS ? FX_MAX_SPOTS : g_fxNSpot;
            float sa[FX_MAX_SPOTS * 4], sb[FX_MAX_SPOTS * 4], sc[FX_MAX_SPOTS * 4];
            for (int i = 0; i < ns; i++) {
                const FxSpot& S = g_fxSpot[i];
                sa[i*4+0] = S.x; sa[i*4+1] = S.y; sa[i*4+2] = S.z;
                sa[i*4+3] = S.shadows ? -S.range : S.range;
                sb[i*4+0] = S.dx; sb[i*4+1] = S.dy; sb[i*4+2] = S.dz; sb[i*4+3] = S.cosOuter;
                sc[i*4+0] = S.r; sc[i*4+1] = S.g; sc[i*4+2] = S.b; sc[i*4+3] = S.cosInner;
            }
            fx_set1i(g_fxPLight, "uNSpots", ns);
            if (ns > 0) {
                fx_set4fv(g_fxPLight, "uSpotA", ns, sa);
                fx_set4fv(g_fxPLight, "uSpotB", ns, sb);
                fx_set4fv(g_fxPLight, "uSpotC", ns, sc);
            }
            fx_set1i(g_fxPLight, "uSpotSteps", (int)(g_fxSpotSteps + 0.5f));
            /* THE MARCH STOPS A THIRD OF A CELL SHORT OF THE LAMP, and counts as a blocker
               only what is less than a third of a cell in front of a step. Nearer the lamp,
               its own vehicle (between the camera and the beam in most views) covers the
               step on screen and every lit pixel came out speckled by a shadow that is not
               there; a hull's thickness behind a step took the same vehicle for a blocker
               further out. Measured on a Medium Tank lighting the Weapons Factory: at 0.12
               and 0.6 half the lit wall was speckled, at a third of a cell clean, and a
               tree's or a soldier's shadow a cell out still drawn. */
            fx_set1f(g_fxPLight, "uSpotNear", 0.35f);
            fx_set1f(g_fxPLight, "uSpotThick", 0.35f);
            fx_set1f(g_fxPLight, "uSpotStart", 0.08f);
        }
        rain_light_uniforms(g_fxPLight);
        distfog_light_uniforms(g_fxPLight);
        tibglow_light_uniforms(g_fxPLight);
        rain_light_bind(g_fxPLight);          /* unit 12: the sea's field, or nothing */
        tibglow_light_bind();                 /* unit 7: the tiberium's field         */

        glActiveTexture(GL_TEXTURE0 + 13);
        glBindTexture(GL_TEXTURE_2D, vhCardsOn ? g_fxVehicleMask.tex : 0);
        glActiveTexture(GL_TEXTURE0 + 8);
        glBindTexture(GL_TEXTURE_2D, g_fxTerrainShadowLive ? g_fxTerrainShadow.depth : 0);
        glActiveTexture(GL_TEXTURE0 + 14);
        glBindTexture(GL_TEXTURE_2D, specOn ? g_fxSpec.tex : 0);
        glActiveTexture(GL_TEXTURE4);
        glEnable(GL_TEXTURE_2D);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, bakeOn ? g_fxBake.tex : 0);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, gnOn ? g_fxGNorm.tex : 0);
        glActiveTexture(GL_TEXTURE3);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_fxCloudTex);
        glActiveTexture(GL_TEXTURE2);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_fxShadow.depth);
        glActiveTexture(GL_TEXTURE1);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_fxScene.depth);
        glActiveTexture(GL_TEXTURE0);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_fxScene.tex);

        fx_rt_bind(&g_fxLit);
        fx_fullscreen_quad();
        fx_glUseProgram(0);

        /* The unbind is not optional: the HUD is drawn with fixed-function state right
           after this and a live unit 3 would modulate it. */
        rain_light_unbind();
        tibglow_light_unbind();
        glActiveTexture(GL_TEXTURE0 + 15); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0 + 14); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0 + 13); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0 + 11); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0 + 10); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0 + 9); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0 + 8); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
        glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
        glActiveTexture(GL_TEXTURE0);
        litSrc = &g_fxLit;
    }

    fprof_mark("post: light");
    litSrc = atmosphere_apply(litSrc, rw, rh);
    fprof_mark("post: atmosphere");

    // Render only explosion particles to an isolation target. This is a pure redraw;
    // no tick, emission, RNG, particle pose or ordinary scene pixel changes.
    int explosionOn = 0;
    if (g_fx.bloom_on && (g_fx.explosion_bloom != 1.0f || g_fx.muzzle_bloom != 1.0f) && g_fxExplosionWant
        && g_fxExplosionWant() && g_fxExplosionDraw && g_fxScene.depth
        && fx_rt_init(&g_fxExplosion,rw,rh,FX_RT_COLOUR,GL_LINEAR)) {
        float clearc[4];glGetFloatv(GL_COLOR_CLEAR_VALUE,clearc);
        fx_rt_bind(&g_fxExplosion);
        fx_glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,g_fxScene.depth,0);
        glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadMatrixf(g_fxVP);
        glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);
        glDisable(GL_ALPHA_TEST);glDisable(GL_CULL_FACE);glActiveTexture(GL_TEXTURE0);
        glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
        g_fxExplosionDraw();
        glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);glPopMatrix();
        fx_glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,0,0);
        glClearColor(clearc[0],clearc[1],clearc[2],clearc[3]);explosionOn=1;
    }
    /* ---- 2. bloom ---- */
    int bloomReady = 0;
    if (g_fx.bloom_on) {
        const int bw = rw / 2 > 1 ? rw / 2 : 1, bh = rh / 2 > 1 ? rh / 2 : 1;
        /* The bloom ping-pong is scene referred too: the bright pass subtracts a
           threshold and keeps the remainder, and in eight bits that remainder was
           whatever survived the clip at white, which is why bloom measured nothing at
           all on a lit hull. */
        if (fx_rt_init(&g_fxBloomA, bw, bh, FX_RT_COLOUR | fx_hdr_want(), GL_LINEAR) &&
            fx_rt_init(&g_fxBloomB, bw, bh, FX_RT_COLOUR | fx_hdr_want(), GL_LINEAR)) {
            glBindTexture(GL_TEXTURE_2D, litSrc->tex);
            fx_glUseProgram(g_fxPBright);
            fx_set1i(g_fxPBright, "uTex", 0);
            fx_set1f(g_fxPBright, "uThreshold", g_fx.bloom_threshold);
            fx_set1f(g_fxPBright, "uClampIn", g_fx.bloom_clamp);
            fx_set1f(g_fxPBright, "uKnee", g_fx.bloom_knee);
            fx_set1i(g_fxPBright,"uExplosionOn",explosionOn);
            fx_set1i(g_fxPBright,"uExplosion",1);
            fx_set1f(g_fxPBright,"uExplosionGain",g_fx.explosion_bloom);
            fx_set1f(g_fxPBright,"uMuzzleGain",g_fx.muzzle_bloom);
            glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,explosionOn?g_fxExplosion.tex:0);
            glActiveTexture(GL_TEXTURE0);
            fx_rt_bind(&g_fxBloomA);
            fx_fullscreen_quad();

            glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,0);glActiveTexture(GL_TEXTURE0);
            const int passes = (int)(g_fx.bloom_passes + 0.5f);
            fx_glUseProgram(g_fxPBlur);
            fx_set1i(g_fxPBlur, "uTex", 0);
            for (int p = 0; p < passes; p++) {
                const float step = g_fx.bloom_radius * (float)(1 << (p > 3 ? 3 : p));
                glBindTexture(GL_TEXTURE_2D, g_fxBloomA.tex);
                fx_set2f(g_fxPBlur, "uDir", step / (float)bw, 0.0f);
                fx_rt_bind(&g_fxBloomB);
                fx_fullscreen_quad();

                glBindTexture(GL_TEXTURE_2D, g_fxBloomB.tex);
                fx_set2f(g_fxPBlur, "uDir", 0.0f, step / (float)bh);
                fx_rt_bind(&g_fxBloomA);
                fx_fullscreen_quad();
            }
            fx_glUseProgram(0);
            bloomReady = 1;
        }
    }

    /* ---- 3. resolve to native ---- */
    /* 1 | 2 = COLOUR AND DEPTH, and the depth half is load-bearing rather than tidy.
       The HUD is drawn INTO this target (fx_rt_bind just below the resolve), and the
       enhanced sidebar's 3D faction emblem is the one thing under that seam that depth
       tests. With colour only there is no depth buffer to test against, so per the GL
       spec the test always passes, the model's six batches paint in array order, and
       its LAST batch -- an untextured, flat, full-radius disc -- covers the emblem.
       That is the flat grey ellipse the project owner recorded.

       It hid from every gate because the app turns this chain on unconditionally
       (game_visuals_default_enhanced sets g_fx.enabled = 1, called on every non-classic
       start) while a bare `cnc_eyes --script` run never calls it and therefore draws the
       sidebar into the real back buffer, which HAS depth. Same binary, two framebuffers,
       and only the one nobody tested was broken. Reproduces headlessly with --gfx.

       Everything else below the seam is indifferent: begin_overlay disables the depth
       test and depth writes for every 2D HUD pass, so attaching depth only makes the
       chain-ON path behave like the known-good chain-OFF one. */
    if (!fx_rt_init(&g_fxNativeA, g_fxW, g_fxH, 1 | 2, GL_LINEAR)) { g_fxActive = 0; return; }

    fx_glUseProgram(g_fxPResolve);
    fx_set1i(g_fxPResolve, "uTex", 0);
    fx_set1i(g_fxPResolve, "uBloom", 1);
    fx_set2f(g_fxPResolve, "uDstTexel", 1.0f / (float)g_fxW, 1.0f / (float)g_fxH);
    {
        int tap = (int)(g_fx.ss_scale + 0.999f);
        if ((float)rw <= (float)g_fxW * 1.001f) tap = 1;
        if (tap < 1) tap = 1; if (tap > 4) tap = 4;
        fx_set1i(g_fxPResolve, "uTapN", tap);
    }
    fx_set1i(g_fxPResolve, "uBloomOn", bloomReady);
    fx_set1f(g_fxPResolve, "uBloomI", g_fx.bloom_intensity);
    fx_set1i(g_fxPResolve, "uGradeOn", g_fx.grade_on ? 1 : 0);
    /* THE CURVE RUNS ONLY WHERE THERE IS A RANGE TO MAP. With the switch off, or on a
       driver that would not give a float target, the buffer was clipped at white long
       before this pass and a tonemap on top of it would only crush what survived. */
    fx_set1i(g_fxPResolve, "uToneOn",
             (g_fxHdrLive && (int)(g_fx.tonemap + 0.5f) == 1) ? 1 : 0);
    fx_set1f(g_fxPResolve, "uToneKnee", g_fx.tone_knee);
    fx_set1f(g_fxPResolve, "uExposure", g_fx.exposure);
    fx_set1f(g_fxPResolve, "uShadowBlackLimit", fx_unified_lighting() ? g_fx.shadow_black_limit*fx_sky_energy() : 0.0f);
    fx_set1f(g_fxPResolve, "uContrast", g_fx.contrast);
    fx_set1f(g_fxPResolve, "uSaturation", g_fx.saturation);
    fx_set1f(g_fxPResolve, "uTemperature", g_fx.temperature);
    fx_set1f(g_fxPResolve, "uTint", g_fx.tint);
    fx_set1f(g_fxPResolve, "uLift", g_fx.lift);
    fx_set1f(g_fxPResolve, "uGain", g_fx.gain);
    fx_set1i(g_fxPResolve, "uGammaOn", g_fx.gamma_on ? 1 : 0);
    fx_set1f(g_fxPResolve, "uGamma", g_fx.gamma);

    if (bloomReady) {
        glActiveTexture(GL_TEXTURE1);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_fxBloomA.tex);
        glActiveTexture(GL_TEXTURE0);
    }
    glBindTexture(GL_TEXTURE_2D, litSrc->tex);
    fx_rt_bind(&g_fxNativeA);
    fx_fullscreen_quad();
    fx_glUseProgram(0);
    if (bloomReady) {
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D); glActiveTexture(GL_TEXTURE0);
    }

    /* ---- 3b. a layer the host lays over the finished view ----
       The Unity explosion (unityvfx_mod.h): after the grade and the gamma, because it
       brings its own tonemap and must not be graded twice, and before the CRT and the
       sidebar. It may replace g_fxNativeA with a target of the same size and format. */
    if (g_fxAfterResolve) g_fxAfterResolve();

    /* ---- 4. the CRT, if it is NOT covering the sidebar ---- */
    g_fxHudRT = 0;
    if (g_fx.crt_on && !g_fx.crt_hud &&
        /* B needs depth for the same reason A does: g_fxHudRT selects THIS target
           whenever the CRT is on without covering the sidebar, so fixing only A would
           leave the bug live for anyone playing with the CRT. */
        fx_rt_init(&g_fxNativeB, g_fxW, g_fxH, 1 | 2, GL_LINEAR)) {
        fx_glUseProgram(g_fxPCrt);
        fx_set1i(g_fxPCrt, "uTex", 0);
        fx_set2f(g_fxPCrt, "uSize", (float)g_fxW, (float)g_fxH);
        fx_set1f(g_fxPCrt, "uScan", g_fx.crt_scanline);
        fx_set1f(g_fxPCrt, "uMask", g_fx.crt_mask);
        fx_set1f(g_fxPCrt, "uCurve", g_fx.crt_curve);
        fx_set1f(g_fxPCrt, "uBleed", g_fx.crt_bleed);
        fx_set1f(g_fxPCrt, "uVig", g_fx.crt_vignette);
        glBindTexture(GL_TEXTURE_2D, g_fxNativeA.tex);
        fx_rt_bind(&g_fxNativeB);
        fx_fullscreen_quad();
        fx_glUseProgram(0);
        g_fxHudRT = 1;
    }

    /* The sidebar and everything after it draws HERE, at 1:1, into a plain RGBA8
       target. Restoring the depth test and the viewport is not optional: the HUD is
       drawn by code that expects the state draw_frame left, not the state a
       fullscreen quad left. */
    fx_rt_bind(g_fxHudRT ? &g_fxNativeB : &g_fxNativeA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_BLEND);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

/* The last thing draw_frame does. Puts the finished picture on the real framebuffer. */
static void fx_present(void)
{
    if (!g_fxActive) return;
    const FxRT* src = g_fxHudRT ? &g_fxNativeB : &g_fxNativeA;

    fx_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    /* Framebuffer zero IS the screen, so this pass goes through the screen-viewport
       seam: during an in-editor playtest the composite lands in the editor's map
       viewport, not over the panel. */
    cnc_screen_viewport(g_fxW, g_fxH);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, src->tex);

    if (g_fx.crt_on && g_fx.crt_hud) {
        fx_glUseProgram(g_fxPCrt);
        fx_set1i(g_fxPCrt, "uTex", 0);
        fx_set2f(g_fxPCrt, "uSize", (float)g_fxW, (float)g_fxH);
        fx_set1f(g_fxPCrt, "uScan", g_fx.crt_scanline);
        fx_set1f(g_fxPCrt, "uMask", g_fx.crt_mask);
        fx_set1f(g_fxPCrt, "uCurve", g_fx.crt_curve);
        fx_set1f(g_fxPCrt, "uBleed", g_fx.crt_bleed);
        fx_set1f(g_fxPCrt, "uVig", g_fx.crt_vignette);
    } else {
        fx_glUseProgram(g_fxPCopy);
        fx_set1i(g_fxPCopy, "uTex", 0);
    }
    fx_fullscreen_quad();
    fx_glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    g_fxActive = 0;
}

#endif /* OPENCNC3D_FX_POST_H */
