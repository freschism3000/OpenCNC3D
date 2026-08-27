/* ====================================================================================
 *  fx_state.h -- every dial the Tier 2 post chain has, in ONE table.
 *
 *  THE POINT OF THIS FILE. There is a single list of parameters, FX_PARAMS, and three
 *  separate things read it: the F5 panel builds its own controls from it, the Save
 *  button writes it, and the loader parses it back. Adding a dial is one line here and
 *  nothing anywhere else. The alternative -- a struct, a panel that names its fields, a
 *  writer that names them again and a reader that names them a third time -- is four
 *  copies of the same list, and this project has already been bitten twice by a
 *  second copy of something outliving a fix to the first.
 *
 *  THE SAVED FILE is plain text, one `key value` per line, with the defaults written in
 *  as comments beside anything that has been moved. That is what comes back: it is
 *  diffable, it is readable without the game, and turning it into new defaults is a
 *  matter of copying numbers into FX_DEFAULTS below.
 *
 *  NOTHING HERE IS DECODED FROM THE CARTRIDGE except one number, and it is labelled:
 *  the VI output gamma. Everything else in this file is authored by us for the desktop
 *  build and belongs as a known gap, which is where it is.
 * ==================================================================================== */
#ifndef CNC3D_FX_STATE_H
#define CNC3D_FX_STATE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct FxState {
    /* 0. BILINEAR FILTERING, and it is deliberately NOT under the master switch.
          It is not a post pass -- it changes how every texture in the program is
          sampled, the 3D pack art and the 1995 sidebar, cameos, fonts, cursors,
          movies and menu alike -- so it stands on its own and works with the rest of
          the chain switched off. fx_filter.h owns the mechanism. Note it is arguably
          a fidelity feature rather than a modern one: the N64's RDP filtered its
          textures bilinearly, and point sampling is what WE chose. */
    int   bilinear;

    /* 0b. SMOOTH ANIMATIONS, and like bilinear it is NOT a post pass and NOT under the
          master switch's chain. It is the sub-tick interpolation: unit movement, mesh
          keyframes and the structure animation stages. Off gives the original stepped
          presentation, which is what the cartridge and the 1995 game both do, so this is
          an addition rather than a fidelity feature. It is wanted as a visible
          toggle, on by default. */
    int   smooth_anim;

    /* 0c. THE 640x480 HUD, the third thing here that is not a post pass. On in ENHANCED,
          off in CLASSIC, which is the original framing: enhanced gets the new sidebar and
          classic gets the 1995 DOS bar. Applied ONLY through the Enhanced path and the
          Visuals dialog, never from --gfx: see sb_set_hud_new's note for why the
          measuring instruments must keep the DOS bar whatever this says. */
    int   new_hud;

    /* 0d. SOFT DECAL EDGES. Ground decals cut hard against the terrain, and the edges
          should be able to blend into the ground below. They are hard because the cartridge's art is a strict 1-bit alpha key and
          the renderer draws it with a 0.5 alpha TEST, which is what the console does. This
          blurs the alpha at load and blends instead. 0 restores the console exactly, byte
          for byte, which is why the unsoftened sheet is kept in memory. Not a post pass
          and not under the master switch, like the three above it. */
    float decal_soft;

    int   enabled;          /* the master switch. 0 = the chain does not run at all   */

    /* 1. The N64's video interface output gamma. THE ONE DECODED NUMBER IN THIS FILE:
          VI_CTRL = 0x0000320E sets GAMMA_ON in all four of the cartridge's mode structs
          and nothing ever clears it, so the console applies an output curve we have
          never reproduced. That is the honest answer to "our
          grass reads slightly dark". The hardware curve is close to out = in^(1/2);
          1.00 here is the switch turned off. */
    int   gamma_on;
    float gamma;

    /* 2. Supersampling. The world is rendered at this multiple of the window and box
          filtered down on the way out. The console anti-aliased (RDP coverage plus the
          VI's own filter) and we never have, so our edges are HARDER than the
          cartridge's, not softer. */
    float ss_scale;

    /* 3. The sun. Screen space: the shadow map is projected onto the depth buffer
          after the world is drawn, so not one world draw call changes. When this is on,
          the cartridge's own baked shadow geometry is SUPPRESSED (all three kinds: the
          models' own shadow faces, the wall shadow quads and the infantry sprite blob),
          because two shadows per object is worse than either alone. */
    int   shadow_on;
    float sun_az, sun_el;             /* degrees: compass bearing, and height          */
    float shadow_strength;            /* 0 = invisible, 1 = full tint                  */
    float shadow_soft;                /* PCF radius in shadow-map texels               */
    float shadow_bias;                /* depth bias in WORLD CELLS, not normalised    */
    float shadow_span;                /* ortho box size as a multiple of the view rect */
    float shadow_ox, shadow_oz;       /* box centre offset from the view, in cells     */
    float shadow_res;                 /* shadow map edge, quantised to a power of two  */
    float shadow_r, shadow_g, shadow_b;/* what a shadowed pixel is multiplied BY       */
    int   shadow_terrain;             /* let the ground cast into itself (hills)       */

    /* 4. Bloom, off the composited world only, never the sidebar. */
    int   bloom_on;
    float bloom_threshold, bloom_knee, bloom_intensity, bloom_radius;
    float bloom_passes;               /* blur ping-pongs, 1..6                         */

    /* 5. Ambient occlusion. Depth only: the normal is reconstructed from the depth
          buffer's own derivatives, so it needs no G-buffer and no change to any draw. */
    int   ssao_on;
    float ssao_radius, ssao_intensity, ssao_bias, ssao_power, ssao_samples;

    /* 6. Dynamic light from the things that are actually burning. Sources come from
          the engine's own live anim list (g_efxAnims), classified by the cartridge's
          own anim names, so an explosion lights the ground because the ENGINE says
          there is an explosion there and not because a timer said so. */
    int   lights_on;
    float light_intensity, light_radius, light_falloff, light_fade, light_fade_muzzle;
    float light_r, light_g, light_b;
    int   light_explosions, light_muzzle, light_fire;
    int   light_tiberium;
    float tib_glow;

    /* 7. Colour grade. Sliders now; a LUT is the same shader with a texture lookup
          bolted on the end, and the day that is wanted this struct grows one path. */
    int   grade_on;
    float exposure, contrast, saturation, temperature, tint, lift, gain;

    /* 10. The CRT. The one effect that deliberately covers the SIDEBAR as well, because
           the 1995 pixel art and the 3D half only agree with each other through a tube.
           crt_hud=0 keeps it to the tactical view. */
    int   crt_on;
    float crt_scanline, crt_mask, crt_curve, crt_bleed, crt_vignette;
    int   crt_hud;

    /* 11. BUILDING SHATTER. Not a post pass and not under the master switch: it is a
           GEOMETRY event, so it stands on its own the way bilinear and smooth
           animations do. Tier 2 only -- Win98 keeps the section shed alone. See
           game/shatter_mod.h for what in it is the cartridge's and what is ours.
           WARNING, AND IT IS DIFFERENT IN KIND FROM EVERY OTHER GROUP HERE: these dials
           change the SIMULATION, not a post pass. Every SHATTER| number G91 asserts on
           moves if a stray cnc3d-fx.cfg sets them, because the launch speed, the tumble
           rate and the roll friction are all read straight out of this struct. */
    int   shatter_on, shatter_pulse, shake_on;
    float shatter_force, shatter_spin, shatter_roll, shatter_linger, shake_amount;
};

static FxState g_fx;

/* ---- The parameter table ---------------------------------------------------------- */

enum { FXP_END = 0, FXP_GROUP, FXP_BOOL, FXP_FLOAT, FXP_STEP };

struct FxParam {
    int         kind;
    const char* key;        /* what the saved file calls it. Never change one of these */
    const char* label;      /* what the panel calls it                                 */
    size_t      off;        /* byte offset into FxState                                */
    float       lo, hi;     /* slider range                                            */
    float       step;       /* FXP_STEP: quantise to this. 0 = continuous              */
    const char* help;
};

#define FXO(f) ((size_t)((char*)&(((FxState*)0)->f) - (char*)0))

static const FxParam FX_PARAMS[] = {
{ FXP_GROUP, "",  "MASTER", 0,0,0,0, "" },
{ FXP_BOOL,  "bilinear", "bilinear filtering", FXO(bilinear), 0,1,0,
  "3D art AND the sidebar; independent of the chain" },
{ FXP_BOOL,  "smooth_anim", "smooth animations", FXO(smooth_anim), 0,1,0,
  "sub-tick interpolation of movement and animation; off = the original stepping" },
{ FXP_BOOL,  "new_hud", "new HUD", FXO(new_hud), 0,1,0,
  "the 640x480 sidebar; off = the 1995 DOS bar" },
{ FXP_FLOAT, "decal_soft", "soft decal edges", FXO(decal_soft), 0.0f, 1.0f, 0,
  "scorches, craters and building aprons; 0 = the cartridge's hard 1-bit edge" },
{ FXP_BOOL,  "enabled", "post chain", FXO(enabled), 0,1,0,
  "off = the picture this project has always shipped" },

{ FXP_GROUP, "",  "1  VI OUTPUT GAMMA  (decoded)", 0,0,0,0, "" },
{ FXP_BOOL,  "gamma_on", "apply", FXO(gamma_on), 0,1,0, "" },
{ FXP_FLOAT, "gamma", "exponent", FXO(gamma), 0.30f, 1.00f, 0,
  "console is near 0.50; 1.00 is off" },

{ FXP_GROUP, "",  "2  SUPERSAMPLING", 0,0,0,0, "" },
{ FXP_STEP,  "ss_scale", "render scale", FXO(ss_scale), 1.0f, 4.0f, 0.25f,
  "world drawn at this multiple, boxed down" },

{ FXP_GROUP, "",  "3  SUN AND SHADOWS", 0,0,0,0, "" },
{ FXP_BOOL,  "shadow_on", "sun shadows", FXO(shadow_on), 0,1,0,
  "ON suppresses the cartridge's own baked shadows" },
{ FXP_FLOAT, "sun_az", "sun bearing", FXO(sun_az), 0.0f, 360.0f, 0, "degrees" },
{ FXP_FLOAT, "sun_el", "sun height", FXO(sun_el), 8.0f, 88.0f, 0, "degrees above ground" },
{ FXP_FLOAT, "shadow_strength", "strength", FXO(shadow_strength), 0.0f, 1.0f, 0, "" },
{ FXP_FLOAT, "shadow_soft", "softness", FXO(shadow_soft), 0.0f, 4.0f, 0, "PCF radius, texels" },
{ FXP_FLOAT, "shadow_bias", "bias (cells)", FXO(shadow_bias), 0.002f, 0.400f, 0,
  "world cells, not depth units. raise until the acne goes, no further" },
{ FXP_FLOAT, "shadow_span", "box size", FXO(shadow_span), 0.6f, 3.0f, 0,
  "smaller = sharper and more likely to clip" },
{ FXP_FLOAT, "shadow_ox", "box east", FXO(shadow_ox), -24.0f, 24.0f, 0, "cells" },
{ FXP_FLOAT, "shadow_oz", "box south", FXO(shadow_oz), -24.0f, 24.0f, 0, "cells" },
{ FXP_STEP,  "shadow_res", "map size", FXO(shadow_res), 512.0f, 4096.0f, 512.0f, "" },
{ FXP_FLOAT, "shadow_r", "shadow red", FXO(shadow_r), 0.0f, 1.0f, 0, "" },
{ FXP_FLOAT, "shadow_g", "shadow green", FXO(shadow_g), 0.0f, 1.0f, 0, "" },
{ FXP_FLOAT, "shadow_b", "shadow blue", FXO(shadow_b), 0.0f, 1.0f, 0, "" },
{ FXP_BOOL,  "shadow_terrain", "ground casts", FXO(shadow_terrain), 0,1,0,
  "hills shadowing themselves; off if it speckles" },

{ FXP_GROUP, "",  "4  BLOOM", 0,0,0,0, "" },
{ FXP_BOOL,  "bloom_on", "bloom", FXO(bloom_on), 0,1,0, "" },
{ FXP_FLOAT, "bloom_threshold", "threshold", FXO(bloom_threshold), 0.0f, 1.0f, 0,
  "below this, nothing glows" },
{ FXP_FLOAT, "bloom_knee", "knee", FXO(bloom_knee), 0.0f, 0.5f, 0, "soft edge on the threshold" },
{ FXP_FLOAT, "bloom_intensity", "intensity", FXO(bloom_intensity), 0.0f, 2.0f, 0, "" },
{ FXP_FLOAT, "bloom_radius", "radius", FXO(bloom_radius), 0.5f, 4.0f, 0, "" },
{ FXP_STEP,  "bloom_passes", "blur passes", FXO(bloom_passes), 1.0f, 6.0f, 1.0f, "" },

{ FXP_GROUP, "",  "5  AMBIENT OCCLUSION", 0,0,0,0, "" },
{ FXP_BOOL,  "ssao_on", "occlusion", FXO(ssao_on), 0,1,0, "" },
{ FXP_FLOAT, "ssao_radius", "radius", FXO(ssao_radius), 0.05f, 2.00f, 0, "world cells" },
{ FXP_FLOAT, "ssao_intensity", "intensity", FXO(ssao_intensity), 0.0f, 2.0f, 0, "" },
{ FXP_FLOAT, "ssao_bias", "bias", FXO(ssao_bias), 0.001f, 0.100f, 0, "" },
{ FXP_FLOAT, "ssao_power", "contrast", FXO(ssao_power), 0.5f, 4.0f, 0, "" },
{ FXP_STEP,  "ssao_samples", "samples", FXO(ssao_samples), 4.0f, 24.0f, 2.0f, "" },

{ FXP_GROUP, "",  "6  DYNAMIC LIGHT", 0,0,0,0, "" },
{ FXP_BOOL,  "lights_on", "dynamic light", FXO(lights_on), 0,1,0,
  "sources are the engine's own live anims" },
{ FXP_FLOAT, "light_intensity", "intensity", FXO(light_intensity), 0.0f, 4.0f, 0, "" },
{ FXP_FLOAT, "light_radius", "radius", FXO(light_radius), 1.0f, 16.0f, 0, "cells" },
{ FXP_FLOAT, "light_falloff", "falloff", FXO(light_falloff), 1.0f, 4.0f, 0, "" },
{ FXP_FLOAT, "light_fade", "fade out", FXO(light_fade), 0.0f, 2.0f, 0, "seconds" },
{ FXP_FLOAT, "light_fade_muzzle", "fade out, muzzle", FXO(light_fade_muzzle),
  0.0f, 2.0f, 0, "seconds" },
{ FXP_FLOAT, "light_r", "light red", FXO(light_r), 0.0f, 1.0f, 0, "" },
{ FXP_FLOAT, "light_g", "light green", FXO(light_g), 0.0f, 1.0f, 0, "" },
{ FXP_FLOAT, "light_b", "light blue", FXO(light_b), 0.0f, 1.0f, 0, "" },
{ FXP_BOOL,  "light_explosions", "from explosions", FXO(light_explosions), 0,1,0, "" },
{ FXP_BOOL,  "light_muzzle", "from muzzle flashes", FXO(light_muzzle), 0,1,0, "" },
{ FXP_BOOL,  "light_fire", "from burning", FXO(light_fire), 0,1,0, "" },
{ FXP_BOOL,  "light_tiberium", "tiberium glow", FXO(light_tiberium), 0,1,0, "" },
{ FXP_FLOAT, "tib_glow", "tiberium strength", FXO(tib_glow), 0.0f, 2.0f, 0, "" },

{ FXP_GROUP, "",  "7  COLOUR GRADE", 0,0,0,0, "" },
{ FXP_BOOL,  "grade_on", "grade", FXO(grade_on), 0,1,0, "" },
{ FXP_FLOAT, "exposure", "exposure", FXO(exposure), -2.0f, 2.0f, 0, "stops" },
{ FXP_FLOAT, "contrast", "contrast", FXO(contrast), 0.5f, 2.0f, 0, "" },
{ FXP_FLOAT, "saturation", "saturation", FXO(saturation), 0.0f, 2.0f, 0, "" },
{ FXP_FLOAT, "temperature", "temperature", FXO(temperature), -1.0f, 1.0f, 0, "cool to warm" },
{ FXP_FLOAT, "tint", "tint", FXO(tint), -1.0f, 1.0f, 0, "green to magenta" },
{ FXP_FLOAT, "lift", "lift (blacks)", FXO(lift), -0.20f, 0.20f, 0, "" },
{ FXP_FLOAT, "gain", "gain (whites)", FXO(gain), 0.5f, 2.0f, 0, "" },

{ FXP_GROUP, "",  "10  CRT", 0,0,0,0, "" },
{ FXP_BOOL,  "crt_on", "CRT", FXO(crt_on), 0,1,0, "" },
{ FXP_FLOAT, "crt_scanline", "scanlines", FXO(crt_scanline), 0.0f, 1.0f, 0, "" },
{ FXP_FLOAT, "crt_mask", "aperture mask", FXO(crt_mask), 0.0f, 1.0f, 0, "" },
{ FXP_FLOAT, "crt_curve", "curvature", FXO(crt_curve), 0.0f, 0.30f, 0, "" },
{ FXP_FLOAT, "crt_bleed", "signal bleed", FXO(crt_bleed), 0.0f, 1.0f, 0, "composite smear" },
{ FXP_FLOAT, "crt_vignette", "vignette", FXO(crt_vignette), 0.0f, 1.0f, 0, "" },
{ FXP_BOOL,  "crt_hud", "cover the sidebar", FXO(crt_hud), 0,1,0,
  "a real tube does not stop at the tactical view" },

{ FXP_GROUP, "",  "11  BUILDING SHATTER", 0,0,0,0, "" },
{ FXP_BOOL,  "shatter_on", "shatter", FXO(shatter_on), 0,1,0,
  "dead buildings come apart into flying pieces; off = the section shed alone" },
{ FXP_FLOAT, "shatter_force", "blast force", FXO(shatter_force), 0.0f, 3.0f, 0,
  "1.0 = the cartridge's own debris launch speed; the default is lower because a\n   building should COLLAPSE with an outward pulse, not be thrown" },
{ FXP_FLOAT, "shatter_spin", "tumble", FXO(shatter_spin), 0.0f, 3.0f, 0,
  "1.0 = the cartridge's own chunk tumble rate" },
{ FXP_FLOAT, "shatter_roll", "roll", FXO(shatter_roll), 0.0f, 1.0f, 0,
  "how far a piece rolls downhill before it settles" },
{ FXP_FLOAT, "shatter_linger", "linger", FXO(shatter_linger), 0.5f, 8.0f, 0,
  "seconds a settled piece stays before it fades" },
{ FXP_BOOL,  "shake_on", "camera shake", FXO(shake_on), 0,1,0,
  "the ground jolts when a building goes up. OURS -- neither original has one" },
{ FXP_FLOAT, "shake_amount", "shake amount", FXO(shake_amount), 0.0f, 3.0f, 0,
  "1.0 is a small jolt scaled by the building's footprint" },
{ FXP_BOOL,  "shatter_pulse", "centre pulse", FXO(shatter_pulse), 0,1,0,
  "the cartridge's own unused shock sprite at the blast centre. OFF by default:\n   the SHOCK bake window is misaligned, so the ring draws low. See missing.md" },
{ FXP_END, "", "", 0,0,0,0, "" }
};

static inline float* fx_fptr(FxState* s, const FxParam* p)
{ return (float*)((char*)s + p->off); }
static inline int* fx_iptr(FxState* s, const FxParam* p)
{ return (int*)((char*)s + p->off); }

/* ---- Defaults --------------------------------------------------------------------
   A STARTING POINT, not an answer. Every number below that is not the VI gamma is a
   first guess made without the cartridge in front of it, which is exactly why the
   panel exists. Tune, save, and the saved file becomes this block. */
static void fx_defaults(FxState* s)
{
    /* THESE ARE THE TUNED NUMBERS, chosen, and this block is a COPY of
       docs/tuning/cnc3d-fx.cfg rather than a set of taste judgements made here.
       Until that evening the compiled defaults were the pre-tuning set (sun 315/52,
       gamma 0.62, bloom 0.55, supersampling 2x) and the tuned picture existed only in a
       cfg file that shipped on neither platform, which is why Windows and the Mac did
       not agree, and why the Windows shadows fell in the wrong place.
       Two dials are deliberately NOT copied from the file, both marked below.

       If these change again, change them HERE and re-copy the file; the two must not
       drift, and docs/tuning/cnc3d-fx.cfg's own header says which is which. */
    memset(s, 0, sizeof *s);

    /* 1. BILINEAR IS ON, on both platforms, and it is one default rather than two.
          it belongs in Enhanced mode, and a PER-PLATFORM default is what caused
          the divergence in the first place, so there is exactly one answer here.
          Note this does not filter anything by itself: fx_filter_on starts at 0 and
          only fx_filter_set moves it, so the bare renderer still samples point-wise
          and the pixel gates measure what they always did. */
    s->bilinear = 1;

    /* ON, deliberately. Note this is a DEFAULT and not a contract with
       the measuring instruments: the smoothing it enables is still gated behind the live
       loop turning it on, so --shot and --script are unaffected whatever this says. */
    s->smooth_anim = 1;

    /* ON, by project decision, and harmless to the gates for the same reason the two above are: the
       value is only ever pushed into the sidebar by the Enhanced path or the Visuals
       dialog, and a bare cnc_eyes run reaches neither. */
    s->new_hud = 1;

    /* The softening is wanted, so it is on. Not 1.0: a full blur of a 24x24 frame
       loses the crater's shape as well as its edge. 0.6 takes the staircase off without
       turning a scorch mark into a smudge of the wrong kind. His to tune. */
    s->decal_soft = 0.60f;

    /* 2. NOT COPIED FROM THE FILE (1 of 2). The saved preset says enabled 1 because it
          was saved from a running Enhanced session. The compiled default stays OFF: the
          gates must see the shipped picture, and the GAME turns the chain on for itself
          in game_visuals_default_enhanced(). Baking 1 here would switch the chain on
          inside every measuring instrument in the project. */
    s->enabled = 0;

    /* THE VI GAMMA IS OFF DELIBERATELY, and it is the only decoded number in this file
       (VI_CTRL = 0x0000320E sets GAMMA_ON in all four of the cartridge's mode structs).
       It was 1/0.62 here before tuning; off is where it landed. Recorded as a
       deliberate deviation rather than quietly reinstated: this is the one dial where
       the console has an opinion and it has been overruled. */
    s->gamma_on = 0;    s->gamma = 1.00000f;

    /* Supersampling off, down from 2x. */
    s->ss_scale = 1.00000f;

    /* THE SUN. His, not the cartridge's, and knowingly so: the ROM's own light is
       45.0 / 35.264 and this is 20.12 degrees off it. Registered as a deliberate
       deviation as a known gap; do not "correct" it back. */
    s->shadow_on = 1;
    s->sun_az = 67.50000f; s->sun_el = 28.71428f;
    s->shadow_strength = 1.00000f;
    s->shadow_soft = 0.64286f;
    s->shadow_bias = 0.03043f;
    s->shadow_span = 1.41429f;
    s->shadow_ox = 11.57143f; s->shadow_oz = 7.71428f;
    s->shadow_res = 4096.0f;
    s->shadow_r = 0.49107f; s->shadow_g = 0.58000f; s->shadow_b = 0.68750f;
    s->shadow_terrain = 1;

    s->bloom_on = 1;
    s->bloom_threshold = 0.68000f; s->bloom_knee = 0.15000f;
    s->bloom_intensity = 0.28571f; s->bloom_radius = 1.60000f; s->bloom_passes = 4.0f;

    s->ssao_on = 1;
    s->ssao_radius = 0.71161f; s->ssao_intensity = 0.87500f;
    s->ssao_bias = 0.02000f;   s->ssao_power = 1.50000f; s->ssao_samples = 12.0f;

    s->lights_on = 1;
    s->light_intensity = 0.32143f; s->light_radius = 2.74107f;
    s->light_falloff = 1.88393f;
    s->light_fade = 0.26786f;   /* 19 Aug: a real light does not stop, it fades */
    /* And a gun is not a fireball. the same evening: the muzzle lights 'linger a
       bit too long before fading away'. A shot is two ticks of emission, so its
       afterglow should be a blink; a burning wreck's can hang about. Separate dials
       rather than one scaled by a hidden constant, because these are two things
       judged separately, by eye. */
    s->light_fade_muzzle = 0.15000f;
    s->light_r = 1.00000f; s->light_g = 0.72000f; s->light_b = 0.36000f;
    s->light_explosions = 1; s->light_muzzle = 1; s->light_fire = 1;
    s->light_tiberium = 1;   s->tib_glow = 0.14286f;   /* deliberately ON */

    s->grade_on = 1;
    s->exposure = -0.03571f; s->contrast = 1.07589f; s->saturation = 1.08000f;
    s->temperature = 0.12500f; s->tint = 0.0f; s->lift = 0.0f; s->gain = 1.10268f;

    /* NOT COPIED FROM THE FILE (2 of 2), and it is a no-op rather than a decision: the
       preset carries crt_on 0, so the tube's own dials below are whatever the panel
       last showed and not a chosen look. They are copied for completeness so the
       file and this block agree dial for dial. */
    s->crt_on = 0;
    s->crt_scanline = 0.10714f; s->crt_mask = 0.71429f; s->crt_curve = 0.00536f;
    s->crt_bleed = 0.48214f; s->crt_vignette = 0.32143f; s->crt_hud = 1;
    /* 3. BUILDING SHATTER AND CAMERA SHAKE ARE THE TUNED PANEL, read off the
       screenshot he sent dial for dial. The previous set was the first cut's guesses.
       The picture he tuned to: a harder outward blast (0.85 -> 1.098) with much LESS
       tumble (1.0 -> 0.295), pieces that roll a little less far (1.0 -> 0.688), a short
       linger at the slider's floor (3.0 -> 0.5, so the ground clears quickly), and a
       much heavier jolt (1.0 -> 2.518).

       CENTRE PULSE IS THE ONE DIAL NOT TAKEN FROM HIS PANEL. It was ON in the
       screenshot and went in that way, and it was put back OFF the same day once the
       reason was on the table: the SHOCK texture is still baked through a misaligned
       window and draws about half a sprite-height BELOW the blast (missing.md, found
       24 Aug 2026). Turning it on shows that defect rather than the effect. It stays
       off until the SEQUENCES change and the efx.pack re-bake that entry describes are
       done, and then it can be switched on as a fix rather than as a preference. */
    s->shatter_on = 1; s->shatter_pulse = 0; s->shake_on = 1;
    s->shake_amount = 2.518f;
    s->shatter_force = 1.098f; s->shatter_spin = 0.295f;
    s->shatter_roll = 0.688f; s->shatter_linger = 0.5f;
}

static inline float fx_clampf(float v, float lo, float hi)
{ return v < lo ? lo : (v > hi ? hi : v); }

static float fx_quant(const FxParam* p, float v)
{
    v = fx_clampf(v, p->lo, p->hi);
    if (p->kind == FXP_STEP && p->step > 0.0f)
        v = p->lo + p->step * floorf((v - p->lo) / p->step + 0.5f);
    return fx_clampf(v, p->lo, p->hi);
}

/* ---- Save ------------------------------------------------------------------------
   THIS IS THE FILE THE PANEL WRITES OUT. Written so it can be read on its own: every line
   that has been moved off its default carries the default in a trailing comment, so
   the diff between "what was tuned" and "what shipped" is in the file itself and
   does not need the game to work it out. */
static int fx_save(const FxState* s, const char* path)
{
    FILE* f = fopen(path, "w");
    if (!f) return 0;
    FxState d; fx_defaults(&d);

    fprintf(f, "# CNC3D Tier 2 post chain -- tuned values\n"
               "#\n"
               "# Written by the F5 panel. Load with:  cnc3d --gfx <this file>\n"
               "# Send this file back and the numbers in it become the new defaults in\n"
               "# game/fx_state.h (fx_defaults). A trailing comment marks anything that\n"
               "# has been moved off the default it shipped with.\n"
               "#\n");

    for (const FxParam* p = FX_PARAMS; p->kind != FXP_END; p++) {
        if (p->kind == FXP_GROUP) { fprintf(f, "\n# ---- %s\n", p->label); continue; }
        if (p->kind == FXP_BOOL) {
            const int v = *(const int*)((const char*)s + p->off);
            const int dv = *(const int*)((const char*)&d + p->off);
            fprintf(f, "%-18s %d%s\n", p->key, v, v != dv ? "    # moved" : "");
        } else {
            const float v = *(const float*)((const char*)s + p->off);
            const float dv = *(const float*)((const char*)&d + p->off);
            if (fabsf(v - dv) > 1e-6f)
                fprintf(f, "%-18s %-10.5f  # was %.5f\n", p->key, v, dv);
            else
                fprintf(f, "%-18s %.5f\n", p->key, v);
        }
    }
    fclose(f);
    return 1;
}

/* ---- Load ------------------------------------------------------------------------
   Unknown keys are REPORTED, not skipped in silence. A preset written against an older
   build and quietly half-applied is the same trap as a test that passes because
   nothing objected. */
static int fx_load(FxState* s, const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    char line[512];
    int applied = 0, unknown = 0;
    while (fgets(line, sizeof line, f)) {
        char* h = strchr(line, '#'); if (h) *h = 0;
        char key[64], val[64];
        if (sscanf(line, "%63s %63s", key, val) != 2) continue;
        const FxParam* hit = NULL;
        for (const FxParam* p = FX_PARAMS; p->kind != FXP_END; p++)
            if (p->kind != FXP_GROUP && !strcmp(p->key, key)) { hit = p; break; }
        if (!hit) {
            fprintf(stderr, "FX|preset|unknown key '%s' ignored\n", key);
            unknown++;
            continue;
        }
        if (hit->kind == FXP_BOOL) *(int*)((char*)s + hit->off) = atoi(val) ? 1 : 0;
        else *(float*)((char*)s + hit->off) = fx_quant(hit, (float)atof(val));
        applied++;
    }
    fclose(f);
    fprintf(stderr, "FX|preset|%s: %d applied, %d unknown\n", path, applied, unknown);
    return 1;
}

#endif /* CNC3D_FX_STATE_H */
