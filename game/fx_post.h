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
 *  occlusion pass's per pixel rotation is hashed from gl_FragCoord, which is the same
 *  value on every run. The cloud deck DOES move, and it is the one thing here that
 *  advances with time: it is driven by g_fxTime, which the host sets from the engine
 *  tick and not from the wall or from a frame count, so the same scenario at the same
 *  tick is the same picture. Two --shot runs stay byte identical with the chain on,
 *  and gate G36c proves it -- five runs, one digest.
 *
 *  (That reference used to read G32, which is the move-order marker and has nothing to
 *  do with this file. Corrected rather than left standing: a cross-reference to the
 *  wrong gate is worse than none, because it invites someone to trust a green G32.)
 * ==================================================================================== */
#ifndef CNC3D_FX_POST_H
#define CNC3D_FX_POST_H

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

/* The shadow-caster callback. cnc_eyes.cpp sets this to a function that re-draws the
   opaque and cutout world from whatever matrices are current. It is called ONCE per
   frame, into a depth-only target, with colour writes already off. */
typedef void (*FxCasterFn)(void);
static FxCasterFn g_fxCasters = 0;
/* The host's ground-normal draw (fx_draw_terrain_normals), rendered by fx_world_end
   into g_fxGNorm and read by the light pass for ground pixels. */
static FxCasterFn g_fxGroundNormals = 0;
static FxRT g_fxGNorm;
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

/* ---- Internal state -------------------------------------------------------------- */

static FxRT  g_fxScene;                 /* world colour + depth, ss resolution        */
static FxRT  g_fxLit;                   /* after shadow/AO/light, ss resolution       */
static FxRT  g_fxBloomA, g_fxBloomB;    /* half of ss, ping-pong                      */
static FxRT  g_fxNativeA, g_fxNativeB;  /* window resolution                          */
static FxRT  g_fxShadow;                /* depth only, square                         */

static GLuint g_fxPLight, g_fxPBright, g_fxPBlur, g_fxPResolve, g_fxPCrt, g_fxPCopy;

/* The cloud mask. One texture for the life of the process: it is generated arithmetic,
   not art, so nothing a dial does can invalidate it -- scale, rotation, drift, coverage
   and edge width are all applied when it is READ. See fx_cloud.h. */
static GLuint g_fxCloudTex = 0;

static int   g_fxBooted = 0;        /* programs compiled                              */
static int   g_fxUsable = 0;        /* the chain can run at all                       */
static int   g_fxActive = 0;        /* it IS running this frame (world begin said so) */
static int   g_fxHudRT  = 0;        /* which native RT the HUD is being drawn into    */
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
    "uniform mat4 uSunVP;\n"
    "uniform vec3 uCamPos;\n"
    "uniform vec3 uSunDir;\n"
    "uniform int   uShadowOn;\n"
    "uniform float uSStrength, uSSoft, uSBias, uSTexel;\n"
    "uniform vec3  uSTint;\n"
    "uniform sampler2D uGNorm;\n"
    "uniform float uAoFoliage;\n"
    "uniform int   uGNormOn;\n"
    "uniform float uSNormOff, uSPen, uAoGround, uSunFloor;\n"
    "uniform int   uSunLambert;\n"
    "uniform sampler2D uCloudTex;\n"
    "uniform int   uCloudOn;\n"
    "uniform float uCStrength, uCEdge, uCWidth, uCInvScale, uCDrift, uCHeight;\n"
    "uniform vec2  uCDir;\n"
    "uniform int   uAoOn, uAoSamples;\n"
    "uniform float uAoRadius, uAoIntensity, uAoBias, uAoPower;\n"
    "uniform int   uNLights;\n"
    "uniform vec4  uLightPos[24];\n"
    "uniform vec4  uLightCol[24];\n"
    "uniform float uLightFalloff;\n"
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
    "uniform vec2  uRainFogDir;\n";

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

         0.00  nothing drew here. Units, walls and water keep the flat reconstruction.
         0.50  FOLIAGE. Use this normal, but it is not ground.
         1.00  ground. Use this normal AND the ground's own one-sun slope term below.

       The split exists because a canopy needs the first without the second. The
       reconstruction above is cross(dFdx(P), dFdy(P)), which is right on a hull because
       a hull is a few large connected faces, and noise on a tree because a canopy is
       hundreds of independently angled alpha-cut cards: it gave every card its own plane
       and fired the sun's terminator per card at random. Marking foliage as ground
       instead would have fixed the normal and then lit the tree twice, once by its own
       shader and once by the ground's lambert. */
    "    vec4 gn = texture2D(uGNorm, uv);\n"
    "    float haveN  = (uGNormOn == 1 && gn.a > 0.25) ? 1.0 : 0.0;\n"
    "    float ground = (uGNormOn == 1 && gn.a > 0.75) ? 1.0 : 0.0;\n"
    "    float foliage = haveN * (1.0 - ground);\n"
    "    if (haveN > 0.5) { vec3 g = gn.xyz * 2.0 - 1.0; if (dot(g, g) > 0.01) n = normalize(g); }\n"
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
       strength, everything the target does not claim -- hulls, walls, sprites -- gets
       its own, because a rolled steel plate is smoother than a field. */
    "    if (uRainWet > 0.0) {\n"
    "        float rel = mix(uRainReliefBody, uRainRelief, haveN);\n"
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
       texel softness and the penumbra dial converted from world cells, turned per pixel
       by the same deterministic hash the occlusion uses (never a clock: two --shot runs
       must agree byte for byte). The 3x3 grid it replaces reached 0.01 of a cell at
       4096 texels and drew every cliff's shadow as a polygon. */
    "            float rad = max(uSSoft, uSPen) * uSTexel;\n"
    "            float ang = fract(sin(dot(floor(gl_FragCoord.xy), vec2(12.9898,78.233))) * 43758.5453) * 6.2831853;\n"
    "            float ca = cos(ang), sa = sin(ang);\n"
    "            vec2 pd[12];\n"
    "            pd[0]=vec2(-0.326,-0.406); pd[1]=vec2(-0.840,-0.074); pd[2]=vec2(-0.696, 0.457); pd[3]=vec2(-0.203, 0.621);\n"
    "            pd[4]=vec2( 0.962,-0.195); pd[5]=vec2( 0.473,-0.480); pd[6]=vec2( 0.519, 0.767); pd[7]=vec2( 0.185,-0.893);\n"
    "            pd[8]=vec2( 0.507, 0.064); pd[9]=vec2( 0.896, 0.412); pd[10]=vec2(-0.322,-0.933); pd[11]=vec2(-0.792,-0.598);\n"
    "            float sum  = 0.0;\n"
    "            for (int i = 0; i < 12; i++) {\n"
    "                vec2 o = vec2(pd[i].x * ca - pd[i].y * sa, pd[i].x * sa + pd[i].y * ca) * rad;\n"
    "                float dz = texture2D(uShadow, s.xy + o).r;\n"
    "                sum += (s.z - bias > dz) ? 1.0 : 0.0;\n"
    "            }\n"
    "            sh = sum / 12.0;\n"
    /* A surface turned away from the sun is unlit whether or not anything stands
       between them. Without this the dark side of a building is as bright as the lit
       one and only the cast shadow reads, which looks like a decal rather than light. */
    "            sh = max(sh, 1.0 - smoothstep(-0.10, 0.30, ndl));\n"
    "        }\n"
    "    }\n"
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
    "        cl *= smoothstep(0.015, 0.150, max(col.r, max(col.g, col.b)));\n"
    "    }\n"
    "\n"
    /* MAX, not a sum and not a product. Ground already inside a hard cast shadow does
       not get darker still when a cloud crosses it -- there is only so much sun to
       take away, and taking it twice is what makes stacked shadow terms read as dirt.
       One tint for both, for the same reason: they are the same sun. */
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
    "    if (uSunLambert == 1 && ground > 0.5) {\n"
    "        float flatL = max(-uSunDir.y, 0.05);\n"
    "        float lam = clamp(dot(n, -uSunDir), 0.0, 1.0) / flatL;\n"
    "        col *= clamp(lam, uSunFloor, 1.35);\n"
    "    }\n"
    "\n"
    /* ---- occlusion ---- */
    "    float ao = 1.0;\n"
    "    if (uAoOn == 1) {\n"
    "        vec3 t = abs(n.y) < 0.99 ? normalize(cross(vec3(0.0,1.0,0.0), n))\n"
    "                                 : vec3(1.0,0.0,0.0);\n"
    "        vec3 b = cross(n, t);\n"
    /* Deterministic per-pixel rotation: hashed from the fragment's own integer
       coordinate, never from a clock. Two --shot runs must agree byte for byte. */
    "        float rot = fract(sin(dot(floor(gl_FragCoord.xy), vec2(12.9898,78.233)))\n"
    "                          * 43758.5453) * 6.2831853;\n"
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
    "            if (length(sw - uCamPos) < length(sp - uCamPos) - uAoBias) {\n"
    /* The range check stops a distant wall behind a unit from occluding it: only a
       surface within the sample radius counts as contact. */
    "                float dist = length(sw - P);\n"
    "                occ += clamp(uAoRadius / max(dist, 1e-4), 0.0, 1.0);\n"
    "            }\n"
    "        }\n"
    "        if (cnt > 0.0) {\n"
    "            float f = pow(clamp(occ / cnt, 0.0, 1.0), uAoPower) * uAoIntensity\n"
    "                      * mix(mix(1.0, uAoFoliage, foliage), uAoGround, ground);\n"
    "            ao = clamp(1.0 - f, 0.0, 1.0);\n"
    "        }\n"
    "    }\n"
    "    col *= ao;\n"
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
    "uniform sampler2D uTex;\n"
    "uniform float uThreshold, uKnee;\n"
    "void main() {\n"
    "    vec3 c = texture2D(uTex, uv).rgb;\n"
    "    float l = max(c.r, max(c.g, c.b));\n"
    /* Soft knee, so a slider crossing the threshold fades things in instead of
       switching them on. A hard cut reads as popping when a unit drives into sunlight. */
    "    float k = max(uKnee, 1e-4);\n"
    "    float w = clamp((l - uThreshold + k) / (2.0 * k), 0.0, 1.0);\n"
    "    w = w * w * max(l - uThreshold + k, 0.0);\n"
    "    w = max(w, l - uThreshold);\n"
    "    w = max(w, 0.0);\n"
    "    gl_FragColor = vec4(c * (w / max(l, 1e-4)), 1.0);\n"
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
    "uniform float uExposure, uContrast, uSaturation, uTemperature, uTint, uLift, uGain;\n"
    "uniform int   uGammaOn;\n"
    "uniform float uGamma;\n"
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
    return g_fxUsable;
}

static void fx_shutdown(void)
{
    if (!g_fxBooted) return;
    fx_rt_free(&g_fxScene);   fx_rt_free(&g_fxLit);
    fx_rt_free(&g_fxBloomA);  fx_rt_free(&g_fxBloomB);
    fx_rt_free(&g_fxNativeA); fx_rt_free(&g_fxNativeB);
    fx_rt_free(&g_fxShadow);
    if (fx_glDeleteProgram) {
        if (g_fxPLight)   fx_glDeleteProgram(g_fxPLight);
        if (g_fxPBright)  fx_glDeleteProgram(g_fxPBright);
        if (g_fxPBlur)    fx_glDeleteProgram(g_fxPBlur);
        if (g_fxPResolve) fx_glDeleteProgram(g_fxPResolve);
        if (g_fxPCrt)     fx_glDeleteProgram(g_fxPCrt);
        if (g_fxPCopy)    fx_glDeleteProgram(g_fxPCopy);
    }
    g_fxPLight = g_fxPBright = g_fxPBlur = g_fxPResolve = g_fxPCrt = g_fxPCopy = 0;
    if (g_fxCloudTex) { glDeleteTextures(1, &g_fxCloudTex); g_fxCloudTex = 0; }
    g_fxBooted = 0; g_fxUsable = 0; g_fxActive = 0;
}

/* Is the chain going to run this frame? Asked by the renderer to decide whether to
   draw the cartridge's own shadows, so it must be answerable BEFORE fx_world_begin. */
static int fx_shadows_replace_cartridge(void)
{
    return g_fx.enabled && g_fx.shadow_on && g_fxUsable;
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
    float R = ext * 0.71f * g_fx.shadow_span;

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

/* ---- The shadow map --------------------------------------------------------------
   Depth only, colour writes off, and drawn by the HOST's own caster function, which
   calls the same draw_terrain / draw_object_mesh / draw_walls the tactical view uses.
   That matters: a shadow cast by a different mesh from the one on screen is a bug
   waiting to be found on the one model where the two disagree. */
static void fx_render_shadow_map(void)
{
    const int res = (int)(g_fx.shadow_res + 0.5f);
    if (!fx_rt_init(&g_fxShadow, res, res, 2, GL_NEAREST)) return;
    if (!g_fxCasters) return;

    fx_sun_matrix(g_fxSunVP);

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
    if (!g_fxActive || !g_fx.shadow_on) return;
    fx_render_shadow_map();
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
    const long MAXPX = 40L * 1000L * 1000L;
    while ((long)rw * rh > MAXPX && s > 1.0f) {
        s -= 0.25f;
        rw = (int)(fbw * s + 0.5f); rh = (int)(fbh * s + 0.5f);
    }

    if (!fx_rt_init(&g_fxScene, rw, rh, 1 | 2, GL_LINEAR)) return;

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
static void rain_light_bind(GLuint prog);
static void rain_light_unbind(void);

static void fx_world_end(void)
{
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
    if (g_fx.terrain_normals && g_fxGroundNormals && g_fxScene.depth
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

    /* ---- 1. shadow, occlusion and light ---- */
    const FxRT* litSrc = &g_fxScene;
    if (fx_rt_init(&g_fxLit, rw, rh, 1, GL_LINEAR)) {
        fx_glUseProgram(g_fxPLight);
        fx_set1i(g_fxPLight, "uColor", 0);
        fx_set1i(g_fxPLight, "uDepth", 1);
        fx_set1i(g_fxPLight, "uShadow", 2);
        fx_setmat(g_fxPLight, "uVP", g_fxVP);
        fx_setmat(g_fxPLight, "uInvVP", g_fxInvVP);
        fx_setmat(g_fxPLight, "uSunVP", g_fxSunVP);
        fx_set3f(g_fxPLight, "uCamPos", g_fxCamPos[0], g_fxCamPos[1], g_fxCamPos[2]);
        fx_set3f(g_fxPLight, "uSunDir", g_fxSunDir[0], g_fxSunDir[1], g_fxSunDir[2]);
        const int shOn = (g_fx.shadow_on && g_fxShadow.depth) ? 1 : 0;
        fx_set1i(g_fxPLight, "uShadowOn", shOn);
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
            fx_set1i(g_fxPLight, "uGNormOn", gnOn);
            /* SCREEN-SPACE OCCLUSION MEANS LESS ON A CANOPY THAN ON A CREASE. Its
               hemisphere is built from the same reconstructed normal, and with the
               radius against cards packed a cell across nearly every tap lands on
               another leaf. The trees carry occlusion baked from the model that still
               had a million leaves in it, so this one only has to stay out of the way. */
            fx_set1f(g_fxPLight, "uAoFoliage", g_fx.ssao_foliage);
            fx_set1f(g_fxPLight, "uSNormOff", g_fx.shadow_noff * cellsPerTexel);
            fx_set1f(g_fxPLight, "uSunFloor", g_fx.sun_floor);
            fx_set1f(g_fxPLight, "uSPen", cellsPerTexel > 0.0f ? g_fx.shadow_pen / cellsPerTexel : 0.0f);
            fx_set1f(g_fxPLight, "uAoGround", g_fx.ssao_ground);
            fx_set1i(g_fxPLight, "uSunLambert", (g_fx.sun_lambert && gnOn) ? 1 : 0);
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
        rain_light_uniforms(g_fxPLight);
        rain_light_bind(g_fxPLight);          /* unit 5: the sea's field, or nothing */

        glActiveTexture(GL_TEXTURE4);
        glEnable(GL_TEXTURE_2D);
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
        glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
        glActiveTexture(GL_TEXTURE0);
        litSrc = &g_fxLit;
    }

    /* ---- 2. bloom ---- */
    int bloomReady = 0;
    if (g_fx.bloom_on) {
        const int bw = rw / 2 > 1 ? rw / 2 : 1, bh = rh / 2 > 1 ? rh / 2 : 1;
        if (fx_rt_init(&g_fxBloomA, bw, bh, 1, GL_LINEAR) &&
            fx_rt_init(&g_fxBloomB, bw, bh, 1, GL_LINEAR)) {
            glBindTexture(GL_TEXTURE_2D, litSrc->tex);
            fx_glUseProgram(g_fxPBright);
            fx_set1i(g_fxPBright, "uTex", 0);
            fx_set1f(g_fxPBright, "uThreshold", g_fx.bloom_threshold);
            fx_set1f(g_fxPBright, "uKnee", g_fx.bloom_knee);
            fx_rt_bind(&g_fxBloomA);
            fx_fullscreen_quad();

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
    fx_set1f(g_fxPResolve, "uExposure", g_fx.exposure);
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

#endif /* CNC3D_FX_POST_H */
