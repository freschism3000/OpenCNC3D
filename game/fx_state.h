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

/* Same spelling fx_filter.h uses, and defined here too because this header is included
   by translation units that never pull that one in. */
#if defined(__GNUC__) || defined(__clang__)
#define FX_STATE_MAYBE_UNUSED __attribute__((unused))
#else
#define FX_STATE_MAYBE_UNUSED
#endif

/* WHICH TERRAIN TILE ART. Written into the saved preset as a number, so the order is
   part of the file format: append, never renumber. */
enum { FX_TEX_N64 = 0, FX_TEX_DOS = 1, FX_TEX_REMASTER = 2 };

/* WHICH INFANTRY ART. Same three choices and the same numbering as the terrain above, on
   purpose: the two drop lists sit beside each other and a reader should not have to hold
   two orders in their head. Written into the saved preset as a number, so append, never
   renumber. */
enum { FX_INF_N64 = 0, FX_INF_DOS = 1, FX_INF_REMASTER = 2 };

/* WHICH PERSPECTIVE (v0.6.8). 0 is the cartridge's camera, north straight up the screen
   as on DOS; 1 turns the world 45 degrees under the same tilt and distance, the angle of
   an isometric RTS, by the angle the iso_yaw dial sets. A number in the saved preset, so
   append, never renumber. */
enum { FX_PERSP_CLASSIC = 0, FX_PERSP_ISO = 1 };

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

    /* 0c-2. WHICH TERRAIN TILE ART DRAWS, and the fourth thing here that is not a post pass.
          The cartridge and the 1995 PC game draw the same 24x24 cell from the same
          (template, icon) -- the cart's own TL4/TL8 tables say so -- but the cartridge's
          temperate bank puts 315 of its 758 tiles at 4bpp, sixteen colours, and CLEAR1
          is one of them, so the plain grass under most of a map has the least colour to
          spend. Its art is also smoothed where the PC's is dithered, which costs about
          half the per-texel detail. So this is a CHOICE of three, not a switch:

            FX_TEX_N64       the cartridge's own bank. The faithful one, and CLASSIC.
            FX_TEX_DOS       the 1995 game's tile art, and ENHANCED's default. Rides in
                             the pack as a second atlas (PKG) that n64_terrain.py bakes.
            FX_TEX_REMASTER  the 2020 Remastered Collection's art, read from the
                             player's OWN install at runtime and never shipped by us.
                             Selectable only when that install is found.

          Choosing something the build cannot supply falls back rather than drawing
          nothing: see fx_texset_effective. A theater with no DOS original (SNOW, SAND)
          likewise has nothing to swap to and keeps the cartridge.
          The measurement behind the default: the cartridge puts 315 of TEMPERAT's 758
          tiles at 4bpp and CLEAR1 is one of them, so most of a map is 16-colour.
          Note the same rule as bilinear above: this does not swap anything by itself.
          fx_texset_on starts at FX_TEX_N64 and only fx_texset_set moves it, so a bare
          cnc_eyes run still draws the cartridge and every pixel gate measures what it
          always did. */
    /* FLOAT, not int, and that is a type contract rather than a preference: FX_PARAMS
       declares this row FXP_STEP, and every non-FXP_BOOL row is written through
       fx_fptr() -- a float pointer. Declared int, the F5 panel and the `gfx texset N`
       verb wrote a float bit pattern into an int field and the value came out garbage,
       while the Visuals dialog (which assigns the field directly) worked, so it looked
       fine everywhere it was first tested. ss_scale is the other FXP_STEP row and is a
       float for the same reason. */
    float texset;

    /* 0c-3. WHICH INFANTRY ART DRAWS, the fifth thing here that is not a post pass.
          FX_INF_N64 is the cartridge's billboards and DETH strips; FX_INF_DOS is the 1995
          sprite art, which is what has shipped and stays the default; FX_INF_REMASTER is
          the 2020 art, read from the player's own install at runtime and never shipped by
          us, selectable only when that install is found.
          House colour is OURS in all three. The Remaster ships one green sprite per type
          and recolours it from its own team-colour table, which cannot express this
          engine's eight PlayerColorType seats -- so the uniform is repainted to the same
          LIVERY_BAND the DOS art uses and all three sets agree.
          Same rule as bilinear and the terrain: fx_infset_on starts at FX_INF_DOS, which
          is what a bare cnc_eyes run has always drawn, and only fx_infset_set moves it. */
    float infset;

    /* 0d. SOFT DECAL EDGES. Ground decals cut hard against the terrain, and the edges
          should be able to blend into the ground below. They are hard because the cartridge's art is a strict 1-bit alpha key and
          the renderer draws it with a 0.5 alpha TEST, which is what the console does. This
          blurs the alpha at load and blends instead. 0 restores the console exactly, byte
          for byte, which is why the unsoftened sheet is kept in memory. Not a post pass
          and not under the master switch, like the three above it. */
    float decal_soft;

    /* 0e. SWAPPED MOUSE BUTTONS, the fifth thing here that is not a post pass. With it on
          the two tactical buttons trade jobs: the one that selects, orders and builds
          moves to the right, the one that cancels, holds a cameo and navigates the radar
          moves to the left. OFF by default, and the memset at the top of fx_defaults is
          what makes it so, which is why no line down there sets it.
          IT LIVES IN THIS STRUCT because this is the struct the Visuals screen writes and
          cnc3d-fx.cfg remembers. A second settings mechanism for one boolean would be a
          second thing to keep in step. It is read live at the SDL event boundary, so
          nothing has to be pushed anywhere when it moves. */
    int   swap_buttons;

    /* 0f. THE RIGHT BUTTON PUSHES THE VIEW. Holding it and moving past the click
          threshold pushes the camera: the further the pointer stands from the middle of
          the screen the faster it travels, with a small flat spot at the centre. ON by
          default. Like the one above it this is NOT under the master switch: CLASSIC
          governs the picture, and how the game is driven stays the player's. */
    int   right_drag_scroll;

    /* 0g. THE CREDIT TICK. The credits readout sounds a short tone on every step it takes
          toward the bank: TONE15 going up, TONE16 going down, the two clips the 1995 game
          played from the same comparison (the cartridge sounds one tone for both). ON by
          default; a player who finds a long count-up wearing turns it off here. A SOUND
          and not a picture, so like the two input switches above it is NOT under the
          master switch, and the Visuals page's Reset to defaults carries it across
          untouched: that page writes the preset on the way out, and a reset that cleared
          this would erase a player's OFF from the file on disc. It sits on the Gameplay
          page beside the two above it. */
    int   cash_tick;

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
    /* 3c. THE GROUND, SMOOTHED. The light pass has no normal buffer; it
          rebuilt a FLAT per-triangle normal from depth, so the terminator, the bias and
          the occlusion were constant across each cell and flipped at its edge: the
          ground read as lit or shadowed facets with straight lines between them.
          terrain_normals   light the ground from the console's own per-corner normal
                            (the loader already forms it for the baked shade and threw
                            it away), drawn into a normal target and Gouraud across the
                            cell, so nothing below can step at a cell edge.
          shadow_noff       the receiver is pushed along that normal by this many shadow
                            texels before the depth test (a normal offset): the acne
                            killer that does not detach contact shadows.
          shadow_pen        the penumbra in WORLD CELLS, a 12-tap rotated disc; the old
                            softness is in texels and reached 0.01 of a cell at 4096.
          shadow_terrain_rise  the ground casts only from cells that rise at least this
                            much corner to corner; 0 = every cell casts, as before.
          ssao_ground       occlusion on ground pixels is scaled by this, so the crease
                            between two facets stops reading as dirt while contact
                            shadow under units keeps its strength. */
    int   terrain_normals;
    float shadow_noff, shadow_pen, shadow_terrain_rise, ssao_ground;
    /* the occlusion's share on FOLIAGE, which is neither ground nor a hull: its
       hemisphere is built from a reconstructed normal and every tap lands on another
       leaf, and the trees already carry occlusion baked from the dense original. */
    float ssao_foliage;
    /* sun_lambert  ONE SUN, NOT TWO. The console bakes a per-corner shade into the
                   ground (its own light, from the south-west), Gouraud across each
                   cell, and the Enhanced sun then darkens the same slopes again; the
                   join of the two is a darker cell with straight edges beside every
                   cliff. With this on, under an Enhanced chain, the ground is drawn
                   at the flat shade (160/255, exactly what level ground gets) and the
                   light pass applies one Lambert term per pixel from the smooth
                   normal, normalised so level ground keeps its brightness. */
    int   sun_lambert;
    /* sun_floor  HOW DARK A SLOPE FACING AWAY FROM THE SUN MAY GO, as a fraction of level
       ground. It was a bare 0.25 buried in the shader, and a ground pixel turned away from
       the sun is darkened TWICE for the same geometric fact: once by the shadow pass,
       whose "turned away" clause applies the full shadow tint with nothing casting, and
       again here. Their product put a ravine wall at about a tenth of level brightness,
       the ground occlusion took it lower, and the grade's black point then clipped it to
       true black. There is no ambient floor anywhere else in the chain, so this is it. */
    float sun_floor;
    /* veh_slope  A GROUND VEHICLE LEANS WITH THE GROUND UNDER IT. 0 is the console's own
       behaviour, which is to stand a tank upright on any hill and let it cut into the
       slope: the cartridge's terrain-orienting shear is draw flag 0x400 and a ground
       vehicle issues 0x180, so it never orients one. This is OURS and is registered as a
       deliberate departure, which is why it is a dial with an off.
       veh_slope_span  how far apart the two height samples are, in cells. A tank spans
       its own hull rather than balancing on a point, so the slope is measured across a
       contact patch; a central difference is also continuous, so nothing pops as a
       vehicle crosses a cell edge.
       veh_slope_max  a cap in degrees, so a cliff edge cannot stand a jeep on its nose. */
    float veh_slope;
    float veh_slope_span;
    float veh_slope_max;
    /* spr_shadow_scale  HOW BIG AN INFANTRY SHADOW IS DRAWN, about the man's own ground
       line so his boots stay on it. The shadow chain charges every caster the same fixed
       WORLD costs -- a bias dead zone, a normal offset and a penumbra diameter -- and
       those were sized against a cliff. A man is five to ten times smaller than anything
       they were tuned on, so at full camera distance the three of them between them eat
       most of his shadow. Growing the caster is the one lever that pays that back without
       touching the cliff work. */
    float spr_shadow_scale;

    /* 3b. CLOUD SHADOWS. Weather crossing the battlefield: one perfectly tiling mask
          (fx_cloud.h generates it, and says there why it is generated rather than
          shipped) sampled in WORLD cells and drifting on the wind, darkening the ground
          and everything standing on it.

          IT IS THE SAME SUN. The mask is read where the sun's ray through this pixel
          crosses the cloud deck at cloud_height, so a shadow on a roof lines up with
          the shadow on the ground beside it. The two terms combine with a MAX and not
          a product: ground already inside a hard cast shadow does not darken a second
          time when a cloud crosses it, which is what happens outdoors. The tint is the
          sun's own shadow_r/g/b for the same reason -- one shadow colour, not two that
          can drift apart under the panel.

          NOTHING HERE IS THE CARTRIDGE'S. No N64 model, texture or table carries a
          cloud and the 1995 game has none either, so this is an addition rather than a
          fidelity feature, it is registered as one, and it is OFF by default.

          THE DRIFT IS NOT IN THIS STRUCT. It is a phase the host computes from the
          engine frame and hands to the chain, because a clock inside a post pass is the
          one thing the chain's determinism promise does not allow. See fx_post.h. */
    int   cloud_on;
    float cloud_strength;             /* 0 = invisible, 1 = the full shadow tint       */
    float cloud_cover;                /* fraction of the ground under cloud, and it is
                                         exact rather than approximate: the mask is
                                         histogram-equalised, so 0.40 here means 40% of
                                         the map at EVERY sharpness. fx_cloud.h note 3. */
    float cloud_sharp;                /* 0 = a haze with no edge, 1 = a cut-out edge   */
    float cloud_scale;                /* WORLD CELLS the mask repeats over, and so how
                                         big one mass is: roughly an eighth of it. The
                                         view is about 33 cells wide. LARGE (100+) gives
                                         slabs that drift across; SMALL gives fine tonal
                                         variation, and the tuned set is the small end.
                                         What does not work is a scale NEAR 33: one mass
                                         then covers the screen and the dial reads as
                                         broken rather than as subtle.                 */
    float cloud_speed;                /* cells per second the deck travels             */
    float cloud_rot;                  /* degrees: which way the wind blows             */
    float cloud_height;               /* cells above the ground the deck sits at       */

    /* 3c. THE ENHANCED SEA (ours, water_mod.h). water_fx swaps the cartridge's two-tile
          wash for a shader that knows where the coast is: a signed distance field
          built from the terrain art's own alpha holes, a flow that turns shoreward
          inside the foam band, crests rolling in, a wet rim, thinner wash over the
          shallows and a darker deep. Off draws the cartridge's pass through the same
          binary. Every distance is in world cells, every speed in cells (or tile
          repeats) per second of engine time. */
    int   water_fx;
    float water_flow;                 /* tile repeats the UV is pushed over a cycle      */
    float water_flow_rate;            /* cycles per second of that push                 */
    float water_current;              /* compass bearing the open sea drifts on         */
    float water_foam;                 /* crest brightness, 0 = no foam                  */
    float water_foam_width;           /* cells from the coast the foam band reaches     */
    float water_wave_len;             /* cells between two crests                       */
    float water_wave_speed;           /* cells per second the crests roll shoreward     */
    float water_shallow;              /* how much the wash thins over the shallows      */
    float water_deep;                 /* how much the deep water darkens                */
    float water_edge;                 /* the wet rim on the coast itself                */
    float water_fade;                 /* how strongly the beach continues over the water */
    float water_fade_width;           /* cells out from the coast it fades over          */
    float water_foam_cross;           /* cells inland the wash laps and the foam rolls   */
    float water_pan;                  /* tile repeats per second the sea slides on the current */
    float water_river_width;          /* a body nowhere wider than twice this is a river  */
    float water_river_speed;          /* a river's run, as a multiple of the flow push     */
    float water_bump;                 /* the ripple normal's strength                      */
    float water_reflect;              /* how much of the sky (and later the scene) shows   */
    float water_spec;                 /* the sun's glint                                   */
    float water_refract;              /* how far the ripple bends what is under the water  */
    float water_fresnel;              /* reflection looking straight down; grazing is 1    */
    int   water_reflect_scene;        /* mirror the world into the water, not just a sky   */
    float water_natural;              /* eases the cobalt toward a greyer, greener water   */
    float water_dark;                 /* plainly darker water, on top of the tint          */
    float water_wake;                 /* a moving boat's trail; 0 draws none               */
    float water_wake_width;           /* how wide the V opens                              */
    int   water_wake_sim;             /* the wave field runs at all                        */
    float water_wake_bump;            /* how much the field bends the surface normal       */
    float water_wake_ring;            /* how far a ripple travels before it dies           */
    float water_wake_speed;           /* the waves' own speed, cells a tick: under the      */
                                      /* boat's and the wake opens into a V, over it a ring */

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

    /* 12. THE HEALTH-BAR SHOW RULE. Like the shatter dials above it, and unlike
           everything before those, this changes what the renderer DECIDES rather than
           how a post pass looks: 0 off, 1 selected only, 2 selected or damaged. It
           lives in this struct so that it saves and loads through cnc3d-fx.cfg with no
           second mechanism. A float because FXP_STEP rows are floats; game/shroud_mod.h
           rounds and clamps it once, in hb_show_mode. */
    float hb_mode;

    /* 13. THE DISPLAY AND THE UI SCALE, the Advanced page's top rows. Floats
           for the FXP_STEP reason texset gives above.
           ui_scale      0 = 1x, the largest whole-number HUD zoom that fits the window;
                         1 = -2x, that zoom halved and ROUNDED to a whole number, never
                         below 1 (the -3x and -4x steps were offered and taken away the
                         same evening, by request). Whole numbers only, because a
                         fractional zoom resamples pixel art (sb_layout's own note in
                         cnc_sidebar.h). Enhanced and the new HUD only; the DOS bar
                         ignores it.
           display_mode  0 windowed, 1 windowed borderless (the desktop's own size, which
                         is what the game has always asked for), 2 true fullscreen: a real
                         mode change to res_w x res_h.
           res_w, res_h  the size asked for in windowed and true fullscreen; 0 = desktop.
           Like new_hud these reach the sidebar and the window ONLY through the Enhanced
           path and the Visuals dialog, never from --gfx, so every gate keeps its picture. */
    float ui_scale, display_mode, res_w, res_h;
    /* perspective  FX_PERSP_CLASSIC or FX_PERSP_ISO: the Advanced page's Perspective
                    row (v0.6.8). Read by the renderer every frame, ahead of the camera
                    clamp; Enhanced only, the way the terrain art is. */
    float perspective;
    /* THE ISOMETRIC CAMERA'S DIALS (F5 group 0), read only while perspective is
       FX_PERSP_ISO and the chain is on (cam_option_active in the renderer), so Classic
       never sees them. iso_yaw in degrees (positive turns the eye to the player's left;
       16 is the shipped angle); iso_pitch in degrees, 0 = the console's zoom-linked
       tilt; iso_fov in vertical degrees, 50 = the cartridge's; iso_dist a scale on the
       zoom distance, 1 = as zoomed. Floats, for the FXP_STEP reason texset gives. */
    float iso_yaw, iso_pitch, iso_fov, iso_dist;
    /* cursor_scale  the 3D cursor meshes' size under Enhanced; 1 = the console's. */
    float cursor_scale;
    /* inf_scale     how big an infantry card draws under Enhanced, all three art sets at
                    once: 1 is each set's own native size, 0.75 a quarter smaller (what
                    ships). Applied as a divisor on the strip's texels-per-unit, never as
                    a multiply on the finished quad, so the Remastered ox/drop offsets (in
                    texels, divided by the same tpu) scale with the card and the man's
                    ground line stays on the ground. Classic never multiplies. */
    float inf_scale;
    /* SOLID TIBERIUM (F5 group 12b), Enhanced only, the way the terrain art is. The
       console has no tiberium model at all, so every one of these moves imported
       geometry and none of them is a decode.
         tib3d          draw crystal clumps standing on each tiberium cell
         tib3d_size     how tall the biggest clump stands, in cells
         tib3d_density  a multiplier on how many clumps a cell's density earns; below
                        1 thins a field out without changing which clumps it picks
         tib3d_pod      the brown rock each crystal stands in, as a multiple of that
                        crystal's own footprint. 0 draws no pod at all
         tib3d_glow     how hard the crystals light themselves through the emissive
                        mask baked into the sheet's alpha. 0 leaves them lit by the
                        sun alone
         tib3d_ground   how brightly the ground cracks under a crystal glow. Its own
                        dial and not tib3d_glow's, because there is one decal per
                        CRYSTAL and they overlap: what looks right on one clump stacks
                        four deep in the middle of a field. 0 draws none. The size of
                        a decal is not a dial -- it follows the crystal it belongs to
         tib3d_seat     how far up its pod a crystal stands: 1 puts its base on the
                        pod's own surface at the middle, measured from the mesh, which
                        is where it belongs; 0 puts it back on the ground, which is
                        inside the pod's crater
         tib3d_growtime how long, in seconds of SIMULATED time, a clump takes to scale
                        up out of nothing when a field grows and back down into it when
                        a harvester takes it. 0 is the old instant pop
         tib3d_pulse    how deep the glow breathes, as a fraction of itself: 0 is a
                        steady light, 1 swings all the way to dark. Every crystal and
                        the cracks under it share one phase, and the phase is a hash of
                        the clump, so a field shimmers rather than throbbing as one.
                        It reaches the cast light too, but ONLY while the crystals are
                        drawing -- with them off the chain's tiberium light is exactly
                        what it was
         tib3d_sink     how far a clump is pushed into the ground, as a fraction of
                        its own height, so it beds in rather than perching on a slope
         tib3d_decal    keep the cartridge's flat overlay under them. OFF: the decal
                        draws the field's footprint at cell resolution, but under
                        solid crystals it is a second, flatter tiberium field seen
                        through the first and the two are hard to read apart. The
                        edge of a field is then the crystals' own.
       The two switches are int and the three sliders are float, because that is what
       the table's row kind decides: fx_load and the panel write an FXP_BOOL through an
       int pointer and everything else through a float one. */
    int   tib3d, tib3d_decal;
    float tib3d_size, tib3d_density, tib3d_pod, tib3d_sink, tib3d_seat;
    float tib3d_glow, tib3d_ground, tib3d_pulse, tib3d_growtime;

    /* 12c. LEAFY TREES. The cartridge's tree model is a decode and stays what Classic
            draws; this replaces it under Enhanced with a tree rebuilt from a CC0 scan
            (bake_tree3d.py). The wind is a shear off the engine clock, and burning is
            the engine's own str/maxstr rather than anything decided here.

         tree3d            replace the cartridge tree model with the rebuilt one
         tree3d_size       how tall a tree stands, in cells
         tree3d_wind       how far the crown travels, in cells. The trunk never moves:
                           the sway weight is baked zero at the ground and cubed toward
                           the crown, so this is the crown's own travel and nothing else.
         tree3d_wind_speed sways a second, off the ENGINE clock
         tree3d_wind_dir   the bearing the wind blows along, degrees, one for the map
         tree3d_vary       how much trees differ in height from one another
         tree3d_burn       how far the engine's own damage takes a tree toward char
         tree3d_lean       how far a tree may lean off vertical, radians */
    int   tree3d;
    float tree3d_size, tree3d_wind, tree3d_wind_speed, tree3d_wind_dir;
    float tree3d_vary, tree3d_burn, tree3d_lean;
    /* the shading half, all of it read only by the tree's own GLSL program:
         tree3d_ao       how far the baked occlusion is allowed to darken the canopy.
                         This is the tree's self-shadowing: the sun map cannot resolve
                         leaf on leaf at this scale, so it was measured at bake time.
         tree3d_trans    the sun coming THROUGH a leaf, gated on the baked thickness
         tree3d_sheen    the waxy specular off a leaf, sharpened by the roughness map
         tree3d_ambient  the sky and ground term, before occlusion
         tree3d_flutter  how hard each card moves on its OWN phase, as a multiple of the
                         wind. Zero is the first build's behaviour: the whole tree
                         sliding sideways as one rigid body. */
    float tree3d_ao, tree3d_trans, tree3d_sheen, tree3d_ambient, tree3d_flutter;
    /* the shape of the canopy's own light, all four read only by the tree's program:
         tree3d_wrap        how far round the leaf the sun reaches. A canopy has no hard
                            terminator; at 0.55 a leaf past a right angle from the sun
                            got nothing at all and read as a black card.
         tree3d_ao_floor    how dark the occlusion is allowed to make a leaf. The inside
                            of a canopy is dark; it is not a hole.
         tree3d_ao_gamma    lifts the bottom of the occlusion range, where the eye reads
                            the difference between deep and deeper.
         tree3d_ao_direct   how much of the DIRECT sun the occlusion may take. Occlusion
                            is a statement about sky, and letting it dim the sun as hard
                            as it dims the sky is what made the shaded cards holes. */
    float tree3d_wrap, tree3d_ao_floor, tree3d_ao_gamma, tree3d_ao_direct;
    /* tree3d_gain      one exposure over the whole tree. Separate from the terms above
                        because the SHAPE of the canopy's light and the LEVEL of it are
                        different questions: softening the terminator enough to stop the
                        black holes also lifted the tree above the ground it stands in,
                        and pulling the ambient back down to fix that would have put the
                        holes back. */
    float tree3d_gain;

    /* 3d. RAIN (ours, rain_mod.h). Enhanced only, like the sea and the clouds: streaks
          in the air drawn from a lattice the engine clock advances, a wet look on every
          surface the light pass shades (darker and richer, a sheen of the sky, a sun
          glint off drop-impact rings, puddles on flat ground, haze and an overcast
          tint), and drops landing in the wave field and on the water's surface. rain_fx
          is OFF here on purpose: the compiled defaults are what every measuring gate
          runs with (see `enabled` below), and rain moves between two frames of one tick,
          which a determinism gate must never see unasked. THE F5 PANEL IS THE ONLY DOOR:
          there is deliberately no row for it on the Visuals dialog, so a release build,
          which compiles the panel out, cannot reach the rain at all until the look is
          settled. rain_fx 0 draws exactly what drew before. */
    int   rain_fx;
    float rain_amount;                /* 0..1, the master intensity everything scales on */
    float rain_streaks;               /* how visible the falling streaks are            */
    float rain_speed;                 /* cells a second a drop falls                     */
    float rain_len;                   /* cells of motion blur behind a drop              */
    float rain_width;                 /* cells across one streak                         */
    float rain_height;                /* cells above the ground the rain volume starts   */
    float rain_slant;                 /* sideways travel per unit of fall (the wind)     */
    float rain_wind;                  /* compass bearing the rain blows toward           */
    float rain_wet;                   /* how wet the surfaces get, 0 leaves them dry     */
    float rain_dark;                  /* how much darker a wet surface is                */
    float rain_gloss;                 /* the glint's tightness (specular exponent)       */
    float rain_spec;                  /* the glint's strength                            */
    float rain_ripple;                /* drop-impact rings on upward faces               */
    float rain_ripple_scale;          /* ring lattice cells per world cell               */
    float rain_ripple_rate;           /* rings a second each lattice cell throws         */
    float rain_puddle;                /* fraction of flat ground under puddles           */
    float rain_haze;                  /* distance haze toward the overcast sky           */
    float rain_overcast;              /* grey tint over the whole world                  */
    float rain_drops;                 /* drops landing in the water's wave field         */
    float rain_drop_force;            /* how hard one drop presses the field             */
    float rain_water_ripple;          /* the impact rings on the water's surface         */
    float rain_ripple_density;        /* fraction of the ring lattice that rings at all   */
    float rain_ripple_shade;          /* how strongly a ring shades itself                */
    float rain_sheen;                 /* the glossy sky reflection on wet surfaces        */
    float rain_puddle_size;           /* cells per repeat of the puddle pattern           */
    float rain_fog;                   /* ground fog: how dense                            */
    float rain_fog_height;            /* cells above the ground it thins out over         */
    float rain_fog_scale;             /* cells per repeat of its drifting pattern         */
    float rain_fog_speed;             /* cells a second the pattern drifts on the wind    */
    float rain_fog_detail;            /* how patchy the fog is, 0 is an even mist         */
    float rain_relief;                /* surface relief derived from the art, on ground   */
    float rain_relief_body;           /* the same, on hulls, walls and men                */
    float rain_puddle_mirror;         /* how completely a puddle mirrors the sky          */
    float rain_puddle_edge;           /* how crisp a puddle's outline is                  */
    float rain_puddle_flat;           /* how level the ground must be to hold water       */
    float rain_sound;                 /* the rain's own bed, authored not decoded        */
    float rain_splash;                /* crowns thrown up where drops land in puddles    */
    float rain_splash_size;           /* how big one crown is, in cells                  */
    float rain_splash_rate;           /* crowns a second from one site                   */
    float rain_sea;                   /* how grey the rain turns the open water          */
    float rain_puddle_ssr;            /* how much of the real scene a puddle reflects     */
    /* tree3d_anim      how strongly the artist's own rig plays, as a multiple of the
                        authored motion. Nought is the tree standing still and playing
                        the shader's shear only. The rig peaks at 1.6% of the tree's
                        height, which at this camera is about one screen pixel, so it
                        needs exaggerating before it reads at all.
       tree3d_anim_wind how much of the global shear a RIGGED tree keeps. The rig moves
                        branches against one another and knows nothing about wind_dir;
                        the shear moves the whole tree along that bearing and cannot move
                        one branch against another. Both at full weight is a crown
                        travelling twice as far as the wind dial says. */
    float tree3d_anim, tree3d_anim_wind;
    /* 12d. GRASS (ours). The cartridge draws grass as a 24x24 texel of the CLEAR1 tile
            and nothing else, so there is no original here to be faithful to and every
            number below is authored. ENHANCED ONLY.

            WHERE grass grows is read once at load from the CARTRIDGE atlas, per texel,
            which every pack keeps (:2094-2098). WHAT COLOUR it is comes from whichever
            atlas is being DRAWN, read back off the GPU. Those are different sheets on
            purpose: at zero tolerance the 1995 DOS sheet's CLEAR1 palette already matches
            73.4% of its own ROAD texels, so a mask built from it would carpet every road.

         grass            stand real grass on the ground
         grass_density    blades in one cell, live. The whole per-frame cost of the
                          feature is this number, and it only ever indexes further into a
                          buffer that was built once.
         grass_density_max the BAKE ceiling in blades a cell. Raising it costs memory at
                          load and nothing per frame; lowering it below grass_density
                          silently caps the field, so it is the ceiling and not the value.
                          Moving it rebuilds the whole buffer.
         grass_height     how tall a blade stands, in cells. The RANGE of this row is
                          load bearing: the bake sizes its silhouette skirt from the top
                          of it, so widening the range invalidates the buffer and moving
                          the value does not.
         grass_width      how wide a blade is at its root, in cells
         grass_taper      the tip's width as a fraction of the root's
         grass_vary       how far blades differ in height and width from one another. Its
                          range is load bearing for the same reason grass_height's is.
         grass_lean       how far a blade leans off vertical with no wind at all, as a
                          fraction of its own height. Grass never stands to attention. */
    int   grass;
    float grass_density, grass_density_max, grass_height, grass_width;
    float grass_taper, grass_vary, grass_lean;
    /* the wind, and it is the TREES' wind: fx_state.h already calls tree3d_wind_dir "the
       bearing the wind blows along, one for the map", so there is deliberately no grass
       bearing dial and no second wind over the same field.
         grass_wind       how far the TIP travels, as a fraction of the blade's height.
                          The root never moves: the bend is anchored by the square of the
                          height up the blade.
         grass_wind_rate  the grass's speed as a MULTIPLE of tree3d_wind_speed. At 1.0 a
                          gust reaches the wood and the field at the same rate.
         grass_gust_len   how far apart the crests are as the wind rolls across the field,
                          in cells. Small is a nervous ripple, large a swell.
         grass_arc        how far the tip drops as it leans. 0 makes the bend a shear and
                          the whole field slides sideways as one carpet.
         grass_shiver     how much a blade moves on its OWN beat, across the wind */
    float grass_wind, grass_wind_rate, grass_gust_len, grass_arc, grass_shiver;
    /* the blade's colour and shape, all read only by the grass program. There is no light
       term here at all: the blade writes no depth, so the terrain's own normal triangle
       still fills that pixel of the ground normal buffer at alpha 1.0 and the post chain
       has already applied the ground's sun and the ground's occlusion to the fragment.
         grass_tint       how much of the blade is the TERRAIN's colour under it
         grass_r/g/b      the colour it eases toward where the terrain's is not taken
         grass_patch      how far the colour drifts from patch to patch across a field
         grass_patch_len  how many cells apart those patches are
         grass_root_fade  how far up the blade its COLOUR becomes the ground's. At the
                          root the blade IS the ground, so there is nothing to sort and
                          nothing to see through: this is what hides the cut where a flat
                          card meets terrain.
         grass_root_alpha how far up the blade the ALPHA ramp runs, which is a different
                          and much shorter distance than the colour blend above
         grass_edge       how soft a blade's own side is, as a fraction of its half width.
                          Hard edges crawl at three pixels wide and there is no
                          multisampling in this context to resolve them.
         grass_ao         how much darker a blade is at the root, among its neighbours
         grass_tip        how much paler it is at the tip
         grass_gain       one exposure over the whole field
         grass_fade       how far from the camera the last blade stands, in cells. The
                          density thins to nothing over the last quarter of it, so the
                          field recedes rather than a ring of it switching off. */
    float grass_tint, grass_r, grass_g, grass_b, grass_patch, grass_patch_len;
    float grass_root_fade, grass_root_alpha, grass_edge, grass_ao, grass_tip;
    float grass_gain, grass_fade;
    /* the two build-time judgements, both read only by the bake and both compared by its
       sync guard, so neither is a dead control:
         grass_theater_cut how much greener than neutral a theater's CLEAR1 tiles must be
                           before that theater grows grass at all. The winter and desert
                           banks are what this exists to answer.
         grass_colour_sd   how many deviations below the CLEAR1 mean the per texel cut
                           sits. Larger is a more generous mask and a wider verge. */
    float grass_theater_cut, grass_colour_sd;
    /* the pointer's own bend, which is the wind's shape from a different source:
         grass_touch    how far the pointer pushes a tip, as a fraction of the blade's
                        height. The root never moves, exactly as it does not in the wind.
         grass_touch_r  how far that reaches from the pointer, in cells */
    float grass_touch, grass_touch_r;
    /* 12e. WHAT THE BATTLE DOES TO THE GRASS (ours). One map-wide field: R tread, G the
            ground a building or a hull is claiming, B the shroud. The two LIFE dials are
            in SECONDS rather than as a per-tick factor, because a slider whose useful
            range is 0.97 to 0.9999 is three hundredths of travel and nobody can tune on
            it. The ceiling is honest arithmetic rather than taste: one ulp a tick is the
            slowest step guaranteed to move at all, which is 2048 ticks at 15 Hz, which is
            136.5 seconds. Anything longer is clamped and announced.
         crush_tread_life how long a vehicle's tracks take to fade to nothing
         crush_cover_life how long grass takes to grow back over released ground
         crush_gauge      how far apart a vehicle's two tracks are, over its beam
         crush_band       how wide one track is, over its beam. Never thinner than one
                          texel of the field: at the tented width an axis-aligned Recon
                          Bike left a measured mean of 0.023 due east against 1.000 at 45
                          degrees, which is a vehicle that leaves no mark when it drives
                          north.
         crush_press      how flat a track presses the grass
         crush_skirt      how far past a building or a hull the grass is cleared, in blade
                          heights. THIS IS NOT DECORATION. It is the geometric half of the
                          rule that keeps a blade from ever overlapping an object
                          silhouette, where the ground normal buffer's alpha is 0, the
                          ground's own sun term is skipped and the occlusion weight
                          doubles against ssao_ground. At 0 the field grows a dark halo one
                          blade tall around every building and every unit. */
    float crush_tread_life, crush_cover_life, crush_gauge;
    float crush_band, crush_press, crush_skirt;
    /* debug  the DEBUG group's only row. When it is on and a debug pack was found beside
              the mission's, every type whose mesh differs between the two is drawn from
              the debug pack instead, so an updated model can be compared against the one
              it replaces without restarting. Off by default and it reaches nothing on the
              gate path, because no gate passes --gfx. */
    float debug;
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

/* THE TERRAIN ART SWITCH, kept here beside the preference it answers to rather than in
   fx_filter.h, because unlike the filter it touches no GL state: draw_terrain simply asks
   which atlas to bind. FX_TEX_N64 is the shipped picture and it stays there until
   something calls the setter -- which is what keeps every bare cnc_eyes run and every
   pixel gate on the cartridge whatever the preference says. */
static int fx_texset_on = FX_TEX_N64;

FX_STATE_MAYBE_UNUSED static const char* fx_texset_name(int v)
{
    return v == FX_TEX_DOS ? "DOS" : v == FX_TEX_REMASTER ? "Remastered" : "cartridge";
}

FX_STATE_MAYBE_UNUSED static void fx_texset_set(int v)
{
    if (v < FX_TEX_N64 || v > FX_TEX_REMASTER) v = FX_TEX_N64;
    if (v == fx_texset_on) return;
    fx_texset_on = v;
    fprintf(stderr, "FX|texset|%s terrain art\n", fx_texset_name(v));
}

FX_STATE_MAYBE_UNUSED static int fx_texset_get(void) { return fx_texset_on; }

/* THE GRASS FOLLOWS THE SHEET IT WAS TUNED AGAINST.
 *
 * Where grass grows is read from the cartridge's own tile art and never moves. What
 * COLOUR it is comes from whichever sheet the terrain is drawing, and the field is tuned
 * against the 1995 bank, which is what Enhanced puts on the ground. Switch the ground to
 * the cartridge bank and the grass is suddenly sitting on art nobody balanced it for, so
 * the feature turns itself off rather than showing a version of itself that was never
 * meant to be looked at.
 *
 * IT REMEMBERS, because a one-way switch would be a trap in exactly the session this
 * exists for. Someone tuning the field flips to the cartridge art to compare, flips back,
 * and must not find the tick gone: the value the switch took away is the value coming
 * back restores. A tick the PLAYER moves while the cartridge art is up is left alone,
 * because this runs only when the sheet changes and never on its own account.
 *
 * NOT INSIDE fx_texset_set, and that is the whole reason this is a separate function.
 * Choosing CLASSIC calls that setter to put the cartridge art back, and the code around
 * it is careful to leave every preference tick standing so that choosing ENHANCED again
 * gives the picture back. Clearing the grass tick from in there would break that promise
 * for one dial. So this is called from the three places a PERSON picks a sheet: the
 * panel row, the Visuals dialog, and the command line.
 */
static int fx_grass_before_cart = -1;   /* -1: nothing remembered */

FX_STATE_MAYBE_UNUSED static void fx_grass_follow_texset(void)
{
    if (fx_texset_get() == FX_TEX_N64) {
        if (fx_grass_before_cart < 0) fx_grass_before_cart = g_fx.grass ? 1 : 0;
        if (g_fx.grass) {
            g_fx.grass = 0;
            fprintf(stderr, "FX|grass|off with the cartridge terrain art, which it is "
                            "not tuned against; the 1995 art brings it back\n");
        }
    } else {
        if (fx_grass_before_cart > 0 && !g_fx.grass) {
            g_fx.grass = 1;
            fprintf(stderr, "FX|grass|back on with the 1995 terrain art\n");
        }
        fx_grass_before_cart = -1;
    }
}

/* FORGET THE TICK THE CARTRIDGE ART TOOK AWAY.
 *
 * Only for a caller that is REPLACING the switch's value rather than changing sheets. The
 * Advanced page's Reset to defaults is the one: it writes the whole shipped state, grass
 * switch included, and the sheet goes back to the 1995 bank in the same breath. Without
 * this the memory above would hand the tick straight back over the top of a reset that had
 * just cleared it, and the button would leave the grass standing. A caller that is merely
 * swapping sheets must NOT call this, or the round trip the memory exists for is lost. */
FX_STATE_MAYBE_UNUSED static void fx_grass_forget_cart(void)
{
    fx_grass_before_cart = -1;
}

/* THE INFANTRY ART SWITCH. Starts where the shipped picture is -- the DOS sprites -- so a
   bare cnc_eyes run and every pixel gate draw what they always did. */
static int fx_infset_on = FX_INF_DOS;

FX_STATE_MAYBE_UNUSED static const char* fx_infset_name(int v)
{
    return v == FX_INF_DOS ? "DOS" : v == FX_INF_REMASTER ? "Remastered" : "cartridge";
}

FX_STATE_MAYBE_UNUSED static void fx_infset_set(int v)
{
    if (v < FX_INF_N64 || v > FX_INF_REMASTER) v = FX_INF_DOS;
    if (v == fx_infset_on) return;
    fx_infset_on = v;
    fprintf(stderr, "FX|infset|%s infantry art\n", fx_infset_name(v));
}

FX_STATE_MAYBE_UNUSED static int fx_infset_get(void) { return fx_infset_on; }

static const FxParam FX_PARAMS[] = {
{ FXP_GROUP, "",  "MASTER", 0,0,0,0, "" },
{ FXP_BOOL,  "bilinear", "bilinear filtering", FXO(bilinear), 0,1,0,
  "3D art AND the sidebar; independent of the chain" },
{ FXP_BOOL,  "smooth_anim", "smooth animations", FXO(smooth_anim), 0,1,0,
  "sub-tick interpolation of movement and animation; off = the original stepping" },
{ FXP_BOOL,  "new_hud", "new HUD", FXO(new_hud), 0,1,0,
  "the 640x480 sidebar; off = the 1995 DOS bar" },
{ FXP_STEP,  "texset", "terrain art", FXO(texset), 0,2,1,
  "0 = the cartridge's 16-colour bank, 1 = the 1995 DOS art, 2 = Remastered" },
{ FXP_STEP,  "infset", "infantry art", FXO(infset), 0,2,1,
  "0 = the cartridge's billboards, 1 = the 1995 DOS sprites, 2 = Remastered" },
{ FXP_STEP,  "ui_scale", "UI scale", FXO(ui_scale), 0,1,1,
  "0 = 1x, the largest that fits; 1 = -2x, half of it rounded to a whole zoom (the new HUD only)" },
{ FXP_STEP,  "display_mode", "display mode", FXO(display_mode), 0,2,1,
  "0 windowed, 1 windowed borderless, 2 true fullscreen; applied by the Visuals dialog" },
/* A STEP OF 1, NOT 8: a display's size is not a multiple of anything. A step of 8
   rounded 3008x1692 to 3008x1696 on the way in from the preset, the Visuals dialog then
   found no such entry in the list, showed the desktop's, and wrote it back as 0x0. The
   ceiling is a sanity bound only; the list itself is the display's word. */
{ FXP_STEP,  "res_w", "resolution width", FXO(res_w), 0,16384,1, "0 = the desktop's" },
{ FXP_STEP,  "res_h", "resolution height", FXO(res_h), 0,16384,1, "0 = the desktop's" },
{ FXP_FLOAT, "cursor_scale", "3D cursor size", FXO(cursor_scale), 0.4f, 1.5f, 0,
  "the console's cursor meshes, Enhanced only; 1 = the console's size, 0.7 by request" },
{ FXP_FLOAT, "inf_scale", "infantry size", FXO(inf_scale), 0.5f, 1.5f, 0,
  "all three infantry art sets, Enhanced only; 1 = each set's own size, 0.75 by request" },
{ FXP_FLOAT, "decal_soft", "soft decal edges", FXO(decal_soft), 0.0f, 1.0f, 0,
  "scorches, craters and building aprons; 0 = the cartridge's hard 1-bit edge" },
{ FXP_BOOL,  "swap_buttons", "swap mouse buttons", FXO(swap_buttons), 0,1,0,
  "select/order moves to the right button; off is the shipped binding" },
{ FXP_BOOL,  "right_drag_scroll", "right button scrolls", FXO(right_drag_scroll), 0,1,0,
  "hold the right button and push: the view follows. off = the right button only cancels" },
{ FXP_BOOL,  "cash_tick", "credit tick sound", FXO(cash_tick), 0,1,0,
  "the credits readout's step tone (TONE15 up, TONE16 down); off = the readout moves silently" },
{ FXP_BOOL,  "enabled", "post chain", FXO(enabled), 0,1,0,
  "off = the picture this project has always shipped" },

/* THE ISOMETRIC CAMERA, ours (v0.6.8). The Advanced page's Perspective row is the
   first dial; the four under it are the tuning surface for the look and reach the
   player only through a preset. Each is read only while perspective is Isometric. */
{ FXP_GROUP, "",  "0  ISOMETRIC  (ours)", 0,0,0,0, "" },
{ FXP_STEP,  "perspective", "perspective", FXO(perspective), 0,1,1,
  "0 Classic (north straight up, the cartridge's camera), 1 Isometric (turned by iso_yaw)" },
{ FXP_STEP,  "iso_yaw", "yaw (degrees)", FXO(iso_yaw), -180.0f, 180.0f, 1.0f,
  "the turn under Isometric; 16 is the shipped angle, positive = eye to the player's left" },
{ FXP_STEP,  "iso_pitch", "tilt (0 = console's)", FXO(iso_pitch), 0.0f, 85.0f, 1.0f,
  "a fixed tilt in degrees under Isometric; 0 = the console's zoom-linked tilt; floored at half the fov + 2" },
{ FXP_STEP,  "iso_fov", "vertical fov (degrees)", FXO(iso_fov), 25.0f, 75.0f, 1.0f,
  "50 = the cartridge's guPerspectiveF; narrower flattens the perspective toward true isometric" },
{ FXP_STEP,  "iso_dist", "distance x", FXO(iso_dist), 0.50f, 2.00f, 0.05f,
  "scale on the zoom distance under Isometric; 1.00 = as zoomed (the wheel still zooms)" },

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
{ FXP_BOOL,  "terrain_normals", "smooth ground", FXO(terrain_normals), 0,1,0,
  "light the ground from the console's per-corner normal; off = the flat facet normal from depth" },
{ FXP_BOOL,  "sun_lambert", "one sun", FXO(sun_lambert), 0,1,0,
  "the console's baked slope shade replaced by one per-pixel sun term on the ground (needs smooth ground)" },
{ FXP_FLOAT, "sun_floor", "shade floor", FXO(sun_floor), 0.0f,1.0f,0.01f,
  "How dark ground turned away from the sun may go, against level ground. Higher is a\n"
  "more subtle heightmap shade; 0.25 was the old hardcoded value and reads as black." },
{ FXP_FLOAT, "veh_slope", "vehicles lean", FXO(veh_slope), 0.0f,1.0f,0.05f,
  "How much a ground vehicle leans with the ground under it. 0 stands it upright, which\n"
  "is what the console does; 1 lays it flat on the slope." },
{ FXP_FLOAT, "veh_slope_span", "lean span", FXO(veh_slope_span), 0.1f,1.5f,0.05f,
  "How far apart, in cells, the two height samples that measure the slope are taken." },
{ FXP_FLOAT, "veh_slope_max", "lean cap", FXO(veh_slope_max), 0.0f,60.0f,1.0f,
  "The most a vehicle may lean, in degrees, so a cliff edge cannot stand it on its nose." },
{ FXP_FLOAT, "spr_shadow_scale", "infantry shade", FXO(spr_shadow_scale), 1.0f,2.5f,0.05f,
  "How big an infantry shadow is drawn, about the man's own ground line. 1 is his exact\n"
  "silhouette, which the shadow chain's fixed world costs all but eat when zoomed out." },
{ FXP_FLOAT, "shadow_noff", "normal offset (texels)", FXO(shadow_noff), 0.0f, 4.0f, 0,
  "the receiver pushed along its normal before the depth test; the acne killer" },
{ FXP_FLOAT, "shadow_pen", "penumbra (cells)", FXO(shadow_pen), 0.0f, 0.50f, 0,
  "12-tap rotated disc in world cells; 0 = the texel softness alone" },
{ FXP_FLOAT, "shadow_terrain_rise", "ground casts above rise", FXO(shadow_terrain_rise), 0.0f, 2.0f, 0,
  "cells of rise a cell needs before it casts; 0 = every cell casts" },

{ FXP_GROUP, "",  "3b  CLOUD SHADOWS  (ours)", 0,0,0,0, "" },
{ FXP_BOOL,  "cloud_on", "cloud shadows", FXO(cloud_on), 0,1,0,
  "weather crossing the map; nothing in the cartridge has this" },
{ FXP_FLOAT, "cloud_strength", "strength", FXO(cloud_strength), 0.0f, 1.0f, 0,
  "how dark under a cloud; tinted like the sun's own shadow" },
{ FXP_FLOAT, "cloud_cover", "coverage", FXO(cloud_cover), 0.0f, 1.0f, 0,
  "fraction of the ground in shadow, and it is exact at any sharpness" },
{ FXP_FLOAT, "cloud_sharp", "sharpness", FXO(cloud_sharp), 0.0f, 1.0f, 0,
  "0 = a blurred haze, 1 = a hard cut edge" },
{ FXP_FLOAT, "cloud_scale", "size", FXO(cloud_scale), 20.0f, 400.0f, 0,
  "cells the pattern repeats over; one mass is about an eighth of it" },
{ FXP_FLOAT, "cloud_speed", "speed", FXO(cloud_speed), 0.0f, 8.0f, 0,
  "cells per second the deck drifts; 0 parks it" },
{ FXP_FLOAT, "cloud_rot", "wind bearing", FXO(cloud_rot), 0.0f, 360.0f, 0,
  "degrees: which way the weather travels, and how the mask is turned" },
{ FXP_FLOAT, "cloud_height", "deck height", FXO(cloud_height), 0.0f, 40.0f, 0,
  "cells up. higher slides the shadow further off a tall roof" },

{ FXP_GROUP, "",  "3c  WATER  (ours)", 0,0,0,0, "" },
{ FXP_BOOL,  "water_fx", "Water Shader", FXO(water_fx), 0,1,0,
  "the sea knows where its coast is: a soft shore, shallows, a flow, rivers that run; off is the cartridge's two-tile wash. On by default under Enhanced, never in Classic" },
{ FXP_FLOAT, "water_flow", "flow", FXO(water_flow), 0.0f, 1.0f, 0,
  "how far the pattern is carried along the current; 0 slides as one sheet like the cartridge" },
{ FXP_FLOAT, "water_flow_rate", "flow rate", FXO(water_flow_rate), 0.02f, 1.0f, 0,
  "cycles per second of the carry; slower reads as a broad river, faster as chop" },
{ FXP_FLOAT, "water_current", "current bearing", FXO(water_current), 0.0f, 360.0f, 0,
  "degrees: which way the open sea drifts; near the coast it turns to run ashore" },
{ FXP_FLOAT, "water_foam", "foam", FXO(water_foam), 0.0f, 1.0f, 0,
  "white water along the coast and around rocks; off by default, the edge is water meeting sand" },
{ FXP_FLOAT, "water_foam_width", "edge width (cells)", FXO(water_foam_width), 0.1f, 2.5f, 0,
  "how far out from the coast the bright edge (and any foam) reaches; half that on a river" },
{ FXP_FLOAT, "water_wave_len", "crest spacing (cells)", FXO(water_wave_len), 0.1f, 2.0f, 0,
  "distance between two lines of surf" },
{ FXP_FLOAT, "water_wave_speed", "crest speed", FXO(water_wave_speed), 0.0f, 2.0f, 0,
  "cells per second the surf rolls in; 0 holds it still" },
{ FXP_FLOAT, "water_shallow", "shallows", FXO(water_shallow), 0.0f, 1.0f, 0,
  "the wash thins near the coast so the sea floor shows through" },
{ FXP_FLOAT, "water_deep", "deep water", FXO(water_deep), 0.0f, 1.0f, 0,
  "the open sea darkens away from the coast" },
{ FXP_FLOAT, "water_edge", "bright edge", FXO(water_edge), 0.0f, 1.0f, 0,
  "how much paler the water is where it reaches a shore or a bank; 0 is the flat wash" },
{ FXP_FLOAT, "water_fade", "shore fades in", FXO(water_fade), 0.0f, 1.0f, 0,
  "the beach continues out over the water and fades away instead of stopping at a hard cut" },
{ FXP_FLOAT, "water_fade_width", "fade width (cells)", FXO(water_fade_width), 0.05f, 1.5f, 0,
  "how far out the beach is still visible under the water" },
{ FXP_FLOAT, "water_foam_cross", "over the edge (cells)", FXO(water_foam_cross), 0.0f, 1.0f, 0,
  "how far up the sand the wash laps and a crest may roll" },
{ FXP_FLOAT, "water_pan", "pan speed", FXO(water_pan), 0.0f, 2.0f, 0,
  "tile repeats a second the open sea slides along the current bearing; 0 parks it" },
{ FXP_FLOAT, "water_river_width", "river width (cells)", FXO(water_river_width), 0.5f, 8.0f, 0,
  "a body of water nowhere wider than twice this is a river and runs downstream instead of panning" },
{ FXP_FLOAT, "water_river_speed", "river run", FXO(water_river_speed), 0.0f, 4.0f, 0,
  "how hard a river runs, as a multiple of the flow" },
{ FXP_FLOAT, "water_bump", "ripple", FXO(water_bump), 0.0f, 1.5f, 0,
  "the surface's ripple normal; 0 is a flat sheet" },
{ FXP_FLOAT, "water_reflect", "reflection", FXO(water_reflect), 0.0f, 1.0f, 0,
  "how much of what is above the water shows in it at a grazing angle" },
{ FXP_FLOAT, "water_spec", "sun glint", FXO(water_spec), 0.0f, 3.0f, 0,
  "the sun's sparkle on the ripples" },
{ FXP_FLOAT, "water_refract", "refraction", FXO(water_refract), 0.0f, 0.06f, 0,
  "how far the ripples bend the sea floor seen through the water, as a fraction of the screen" },
{ FXP_FLOAT, "water_fresnel", "reflection floor", FXO(water_fresnel), 0.0f, 0.5f, 0,
  "how reflective the water is looking straight down; grazing views are always fully reflective" },
{ FXP_BOOL,  "water_reflect_scene", "mirror the world", FXO(water_reflect_scene), 0,1,0,
  "cliffs, buildings and units reflected in the water, drawn once more from under it each frame; off reflects a plain sky" },
{ FXP_FLOAT, "water_natural", "natural colour", FXO(water_natural), 0.0f, 1.0f, 0,
  "eases the cartridge's cobalt toward a greyer, greener water; 0 is the console's colour" },
{ FXP_FLOAT, "water_dark", "darkness", FXO(water_dark), 0.0f, 0.5f, 0,
  "how much darker the water is than the cartridge's; separate from the tint above" },
{ FXP_FLOAT, "water_wake", "boat wake", FXO(water_wake), 0.0f, 2.0f, 0,
  "how hard a moving boat pushes the water, and how white its crests break; nothing once it stops" },
{ FXP_FLOAT, "water_wake_width", "wake width", FXO(water_wake_width), 0.3f, 2.5f, 0,
  "how wide a hull presses on the water" },
{ FXP_BOOL,  "water_wake_sim", "wave physics", FXO(water_wake_sim), 0,1,0,
  "a wave equation on the surface: boats push the water, ripples spread and bounce off the shore" },
{ FXP_FLOAT, "water_wake_bump", "wave height", FXO(water_wake_bump), 0.0f, 12.0f, 0,
  "how much the wave field bends the surface: ripples, refraction and glint on a wake" },
{ FXP_FLOAT, "water_wake_ring", "wave travel", FXO(water_wake_ring), 0.0f, 1.0f, 0,
  "how far a ripple runs before it dies away; 1 keeps it going longest" },
{ FXP_FLOAT, "water_wake_speed", "wave speed", FXO(water_wake_speed), 0.005f, 0.20f, 0,
  "how fast the waves themselves travel, in cells a tick. Below a boat's own speed the wake opens into a V behind it; above it the ripples close ahead of the hull and the wake is a ring" },

{ FXP_GROUP, "",  "3d  RAIN  (ours)", 0,0,0,0, "" },
{ FXP_BOOL,  "rain_fx", "Rain", FXO(rain_fx), 0,1,0,
  "rain over the battlefield: streaks in the air, wet ground and hulls, drops on the water. Off by default; Enhanced only" },
{ FXP_FLOAT, "rain_amount", "amount", FXO(rain_amount), 0.0f, 1.0f, 0,
  "how hard it rains; everything below scales on this" },
{ FXP_FLOAT, "rain_streaks", "streaks", FXO(rain_streaks), 0.0f, 1.0f, 0,
  "how visible the falling drops are; 0 leaves the wet look with nothing in the air" },
{ FXP_FLOAT, "rain_speed", "fall speed", FXO(rain_speed), 2.0f, 30.0f, 0,
  "cells a second a drop falls" },
{ FXP_FLOAT, "rain_len", "streak length (cells)", FXO(rain_len), 0.05f, 1.5f, 0,
  "the motion blur behind each drop" },
{ FXP_FLOAT, "rain_width", "streak width (cells)", FXO(rain_width), 0.005f, 0.08f, 0,
  "how wide one streak is; about a screen pixel at the default zoom is 0.02" },
{ FXP_FLOAT, "rain_height", "volume height (cells)", FXO(rain_height), 1.0f, 12.0f, 0,
  "how high above the ground the rain starts; above the camera it is never seen" },
{ FXP_FLOAT, "rain_slant", "slant", FXO(rain_slant), 0.0f, 1.5f, 0,
  "sideways travel per unit of fall; 0 falls straight down" },
{ FXP_FLOAT, "rain_wind", "wind bearing", FXO(rain_wind), 0.0f, 360.0f, 0,
  "degrees: which way the rain blows, read like the cloud bearing" },
{ FXP_FLOAT, "rain_wet", "wetness", FXO(rain_wet), 0.0f, 1.0f, 0,
  "how wet the ground, the hulls and the men look; 0 keeps them dry under the rain" },
{ FXP_FLOAT, "rain_dark", "wet darkening", FXO(rain_dark), 0.0f, 1.0f, 0,
  "a wet surface is darker and richer than a dry one; this is how much" },
{ FXP_FLOAT, "rain_gloss", "glint tightness", FXO(rain_gloss), 4.0f, 256.0f, 0,
  "the sun's highlight on wet surfaces: low is a broad sheen, high a hard sparkle" },
{ FXP_FLOAT, "rain_spec", "glint", FXO(rain_spec), 0.0f, 3.0f, 0,
  "how bright the sun and the sky shine off wet surfaces" },
{ FXP_FLOAT, "rain_ripple", "impact rings", FXO(rain_ripple), 0.0f, 2.0f, 0,
  "rings spreading where drops land on anything facing up; 0 draws none" },
{ FXP_FLOAT, "rain_ripple_scale", "ring size", FXO(rain_ripple_scale), 1.0f, 12.0f, 0,
  "ring lattice cells per world cell: higher is smaller, denser rings" },
{ FXP_FLOAT, "rain_ripple_rate", "ring rate", FXO(rain_ripple_rate), 0.2f, 4.0f, 0,
  "rings a second each patch of ground throws" },
{ FXP_FLOAT, "rain_puddle", "puddles", FXO(rain_puddle), 0.0f, 1.0f, 0,
  "the fraction of flat ground that gathers a puddle mirroring the sky. OFF by default: from this camera a patch of standing water reads as a grey stain on the ground" },
{ FXP_FLOAT, "rain_haze", "haze", FXO(rain_haze), 0.0f, 1.0f, 0,
  "distance haze toward the overcast; the far ground greys out" },
{ FXP_FLOAT, "rain_overcast", "overcast", FXO(rain_overcast), 0.0f, 1.0f, 0,
  "a grey, cool tint over the whole world while it rains" },
{ FXP_FLOAT, "rain_sound", "rain sound", FXO(rain_sound), 0.0f, 1.5f, 0,
  "the rain's own bed. The one sound here that is not off a disc: neither game has weather" },
{ FXP_FLOAT, "rain_splash", "splashes in puddles", FXO(rain_splash), 0.0f, 1.0f, 0,
  "crowns thrown up where drops land in standing water; only in puddles" },
{ FXP_FLOAT, "rain_splash_size", "splash size (cells)", FXO(rain_splash_size), 0.05f, 1.0f, 0,
  "how tall one crown stands" },
{ FXP_FLOAT, "rain_splash_rate", "splash rate", FXO(rain_splash_rate), 0.5f, 12.0f, 0,
  "crowns a second from one site; the period the clock folds to" },
{ FXP_FLOAT, "rain_sea", "sea takes the weather", FXO(rain_sea), 0.0f, 1.0f, 0,
  "how grey the rain turns the open water; 0 leaves the sea its sunny blue" },
{ FXP_FLOAT, "rain_drops", "drops on water", FXO(rain_drops), 0.0f, 1.0f, 0,
  "how many drops land in the wave field a tick; needs wave physics on" },
{ FXP_FLOAT, "rain_drop_force", "drop force", FXO(rain_drop_force), 0.0f, 0.30f, 0,
  "how hard one drop presses the water; the ring it throws follows" },
{ FXP_FLOAT, "rain_water_ripple", "water rings", FXO(rain_water_ripple), 0.0f, 2.0f, 0,
  "the impact rings drawn on the water's own surface, over the wave field" },
{ FXP_FLOAT, "rain_ripple_density", "ring count", FXO(rain_ripple_density), 0.0f, 1.0f, 0,
  "how many rings are live at once: the fraction of the ring lattice that is ringing" },
{ FXP_FLOAT, "rain_ripple_shade", "ring contrast", FXO(rain_ripple_shade), 0.0f, 2.0f, 0,
  "how strongly a ring lights its sunward side and darkens the other; 0 leaves rings to the glint alone" },
{ FXP_FLOAT, "rain_sheen", "wet sheen", FXO(rain_sheen), 0.0f, 2.0f, 0,
  "the glossy grey of the sky reflected off every wet surface, strongest at a glancing angle" },
{ FXP_FLOAT, "rain_puddle_size", "puddle size (cells)", FXO(rain_puddle_size), 2.0f, 40.0f, 0,
  "cells per repeat of the puddle pattern: larger is fewer, broader puddles" },
{ FXP_FLOAT, "rain_fog", "ground fog", FXO(rain_fog), 0.0f, 1.0f, 0,
  "a low fog over the ground, drifting on the wind; 0 draws none" },
{ FXP_FLOAT, "rain_fog_height", "fog height (cells)", FXO(rain_fog_height), 0.2f, 6.0f, 0,
  "how high above the ground the fog reaches before it thins away; hills rise out of it" },
{ FXP_FLOAT, "rain_fog_scale", "fog patch size (cells)", FXO(rain_fog_scale), 2.0f, 60.0f, 0,
  "cells per repeat of the fog's pattern: larger is broader banks" },
{ FXP_FLOAT, "rain_fog_speed", "fog drift", FXO(rain_fog_speed), 0.0f, 4.0f, 0,
  "cells a second the fog drifts along the wind bearing; 0 holds it still" },
{ FXP_FLOAT, "rain_fog_detail", "fog patchiness", FXO(rain_fog_detail), 0.0f, 1.0f, 0,
  "0 is an even mist, 1 is banks and clear gaps" },
{ FXP_FLOAT, "rain_relief", "surface relief: ground", FXO(rain_relief), 0.0f, 0.5f, 0,
  "how much the ground's own painted detail tilts the wet surface. Nothing in this game carries a normal map, so the relief is read back out of the art's own shading" },
{ FXP_FLOAT, "rain_relief_body", "surface relief: bodies", FXO(rain_relief_body), 0.0f, 0.5f, 0,
  "the same, on hulls, walls and men, where the plate is smoother than a field" },
{ FXP_FLOAT, "rain_puddle_mirror", "puddle mirror", FXO(rain_puddle_mirror), 0.0f, 1.0f, 0,
  "how completely a puddle takes the sky instead of the ground under it; low is a wet patch, high is standing water" },
{ FXP_FLOAT, "rain_puddle_edge", "puddle edge", FXO(rain_puddle_edge), 0.005f, 0.30f, 0,
  "how crisp a puddle's outline is; wide makes a soft stain, narrow a water line" },
{ FXP_FLOAT, "rain_puddle_flat", "puddle flatness", FXO(rain_puddle_flat), 0.80f, 0.999f, 0,
  "how level the ground must be before water will lie on it. High keeps puddles out of open desert, where every cell is flat and they otherwise cover the board" },
{ FXP_FLOAT, "rain_puddle_ssr", "puddle reflection", FXO(rain_puddle_ssr), 0.0f, 1.0f, 0,
  "how much of the world around it a puddle reflects: trees, cliffs and hulls as dark shapes against the sky. 0 mirrors a plain sky" },

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
{ FXP_FLOAT, "ssao_bias", "bias (cells)", FXO(ssao_bias), 0.005f, 0.500f, 0,
  "how much nearer a surface has to be before it counts as occluding, as a distance in world cells. It used to be compared in raw depth-buffer units, where one value means a hair's breadth close to the camera and an enormous gap far from it, and the shipped setting was larger than any gap the scene produces: the whole feature changed nothing at all" },
{ FXP_FLOAT, "ssao_power", "contrast", FXO(ssao_power), 0.5f, 4.0f, 0, "" },
{ FXP_STEP,  "ssao_samples", "samples", FXO(ssao_samples), 4.0f, 24.0f, 2.0f, "" },
{ FXP_FLOAT, "ssao_ground", "ground weight", FXO(ssao_ground), 0.0f, 1.0f, 0,
  "occlusion on ground pixels scaled by this; units and walls keep 1" },
{ FXP_FLOAT, "ssao_foliage", "occlusion on foliage", FXO(ssao_foliage), 0.0f, 1.00f, 0,
  "the screen-space occlusion's share on a canopy. It means less there than on a crease, and the trees carry occlusion baked from the dense original" },

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
{ FXP_GROUP, "",  "12  HEALTH BARS", 0,0,0,0, "" },
/* THE LEGEND IS IN THE LABEL, deliberately. The panel draws a label, a track and a
   number and nothing else: FxParam::help is carried by every row in this table and
   rendered by none of them, so a bare "2" on a three-position dial tells a player
   nothing -- and the player this dial exists for is the one asking how to switch the
   bars off. 26 characters at the 6px advance is 156px, against the 168px the label
   column has between FXP_LABX and the track at FXP_TRKX, so it fits without running
   under the slider. Widen it further and it will not. */
{ FXP_STEP,  "hb_mode", "health bars 0off 1sel 2dmg", FXO(hb_mode), 0.0f, 2.0f, 1.0f,
  "0 none; 1 selected only, the 1995 default; 2 selected or damaged, shipped" },
/* SOLID TIBERIUM. Imported art, so the group says so in its own name: nothing under it
   comes off the cartridge and there is no console picture for it to be measured against.
   Every dial is read only under Enhanced. */
{ FXP_GROUP, "",  "12b  SOLID TIBERIUM  (imported)", 0,0,0,0, "" },
{ FXP_BOOL,  "tib3d", "solid crystals", FXO(tib3d), 0,1,0,
  "stand crystal clumps on each tiberium cell; off = the flat overlay alone, which is what the console draws" },
{ FXP_FLOAT, "tib3d_size", "crystal height", FXO(tib3d_size), 0.08f, 0.60f, 0,
  "how tall the biggest clump stands, in cells" },
{ FXP_FLOAT, "tib3d_density", "density", FXO(tib3d_density), 0.25f, 2.00f, 0,
  "a multiplier on how many clumps a cell's own growth stage earns" },
{ FXP_FLOAT, "tib3d_pod", "brown pod size", FXO(tib3d_pod), 0.0f, 1.60f, 0,
  "the rock each crystal stands in, relative to that crystal; 0 = no pod, crystals straight on the ground" },
{ FXP_FLOAT, "tib3d_seat", "stand on the pod", FXO(tib3d_seat), 0.0f, 1.50f, 0,
  "how far up its pod a crystal stands; 1 = on the pod's own surface, 0 = down in its crater" },
{ FXP_FLOAT, "tib3d_growtime", "grow in and out (s)", FXO(tib3d_growtime), 0.0f, 4.00f, 0,
  "seconds a clump takes to scale up when it grows and down when it is harvested; 0 = it pops" },
{ FXP_FLOAT, "tib3d_pulse", "glow breathes", FXO(tib3d_pulse), 0.0f, 1.00f, 0,
  "how deep the glow varies over time; 0 = a steady light. Off the engine clock, so two screenshot runs agree" },
{ FXP_FLOAT, "tib3d_sink", "bed into ground", FXO(tib3d_sink), 0.0f, 0.30f, 0,
  "how far a clump sits into the ground, as a fraction of its height" },
{ FXP_FLOAT, "tib3d_glow", "crystal glow", FXO(tib3d_glow), 0.0f, 1.50f, 0,
  "how hard the crystals light themselves; 0 = lit by the sun alone" },
{ FXP_FLOAT, "tib3d_ground", "ground crack glow", FXO(tib3d_ground), 0.0f, 1.50f, 0,
  "how brightly the ground cracks under each crystal; 0 = none, and the crystals stand on untouched terrain" },
/* LEAFY TREES. Imported art again, and the group name says so: the geometry is rebuilt
   from a CC0 scan, and the cartridge's own tree model is what Classic keeps drawing. */
{ FXP_GROUP, "",  "12c  LEAFY TREES  (imported)", 0,0,0,0, "" },
{ FXP_BOOL,  "tree3d", "leafy trees", FXO(tree3d), 0,1,0,
  "replace the cartridge's tree model with the rebuilt one; off = the console's tree, which is what Classic draws" },
{ FXP_FLOAT, "tree3d_size", "tree height", FXO(tree3d_size), 0.40f, 2.50f, 0,
  "how tall a tree stands, in cells" },
{ FXP_FLOAT, "tree3d_wind", "wind sway", FXO(tree3d_wind), 0.0f, 0.20f, 0,
  "how far the crown travels, in cells; 0 = a still wood. The trunk does not move at any setting" },
{ FXP_FLOAT, "tree3d_wind_speed", "wind speed", FXO(tree3d_wind_speed), 0.05f, 3.00f, 0,
  "sways a second, off the engine clock, so two screenshot runs of one script agree" },
{ FXP_FLOAT, "tree3d_wind_dir", "wind bearing", FXO(tree3d_wind_dir), 0.0f, 360.0f, 0,
  "which way the wind blows, in degrees; one bearing for the whole map" },
{ FXP_FLOAT, "tree3d_vary", "size variation", FXO(tree3d_vary), 0.0f, 0.60f, 0,
  "how much trees differ in height from one another; 0 = a plantation" },
{ FXP_FLOAT, "tree3d_burn", "burn darkening", FXO(tree3d_burn), 0.0f, 1.00f, 0,
  "how far a damaged tree is taken toward char. The damage itself is the engine's, not ours" },
{ FXP_FLOAT, "tree3d_lean", "random lean", FXO(tree3d_lean), 0.0f, 0.40f, 0,
  "how far a tree may lean off vertical, in radians; a wood of upright trees reads as a grid" },
{ FXP_FLOAT, "tree3d_ao", "canopy self-shadow", FXO(tree3d_ao), 0.0f, 1.00f, 0,
  "how deep the baked occlusion darkens the inside of the canopy. This is the tree shadowing itself; the sun map cannot resolve leaf on leaf at this size" },
{ FXP_FLOAT, "tree3d_trans", "leaves glow through", FXO(tree3d_trans), 0.0f, 1.50f, 0,
  "sun coming THROUGH a leaf toward the camera, gated on how much canopy is behind it. The one term that separates foliage from a green solid" },
{ FXP_FLOAT, "tree3d_sheen", "leaf sheen", FXO(tree3d_sheen), 0.0f, 1.00f, 0,
  "the waxy specular off a leaf, sharpened by the roughness the scan shipped with" },
{ FXP_FLOAT, "tree3d_ambient", "sky and ground light", FXO(tree3d_ambient), 0.0f, 1.50f, 0,
  "how much light the canopy takes from the sky above and the ground below, before occlusion" },
{ FXP_FLOAT, "tree3d_flutter", "leaves flutter", FXO(tree3d_flutter), 0.0f, 2.00f, 0,
  "how hard each card moves on its OWN phase, as a multiple of the wind. 0 is the whole tree sliding sideways as one body" },
{ FXP_FLOAT, "tree3d_wrap", "soft terminator", FXO(tree3d_wrap), 0.0f, 1.50f, 0,
  "how far round a leaf the sun reaches. A canopy has no hard terminator; at 0.55 a leaf turned past a right angle from the sun got nothing and read as a black card" },
{ FXP_FLOAT, "tree3d_ao_floor", "shadow floor", FXO(tree3d_ao_floor), 0.0f, 1.00f, 0,
  "how dark the occlusion may make a leaf. The inside of a canopy is dark; it is not a hole" },
{ FXP_FLOAT, "tree3d_ao_gamma", "shadow curve", FXO(tree3d_ao_gamma), 0.30f, 2.00f, 0,
  "lifts the bottom of the occlusion range, where the eye reads deep against deeper" },
{ FXP_FLOAT, "tree3d_ao_direct", "shadow on sunlight", FXO(tree3d_ao_direct), 0.0f, 1.00f, 0,
  "how much of the DIRECT sun the occlusion takes. 1 lets it dim the sun as hard as it dims the sky, which is what made shaded cards into holes" },
{ FXP_FLOAT, "tree3d_gain", "canopy exposure", FXO(tree3d_gain), 0.30f, 2.00f, 0,
  "one brightness over the whole tree, kept separate from the terms above so the shape of the canopy's light and its level can be set independently" },
{ FXP_FLOAT, "tree3d_anim", "authored sway", FXO(tree3d_anim), 0.0f, 5.00f, 0,
  "how strongly each tree plays the animation it was rigged with. The authored motion peaks at about one screen pixel at this camera, so it wants exaggerating; nought turns the rig off and leaves the shader's own shear" },
{ FXP_FLOAT, "tree3d_anim_wind", "shear on a rigged tree", FXO(tree3d_anim_wind), 0.0f, 1.00f, 0,
  "how much of the global wind shear a rigged tree keeps on top of its own animation. The rig cannot lean into wind_dir and the shear cannot move one branch against another, so both run, at less than full weight each" },
{ FXP_BOOL,  "tib3d_decal", "keep the flat overlay", FXO(tib3d_decal), 0,1,0,
  "draw the cartridge's own tiberium decal under the crystals as well; off is the shipped look and leaves the ground bare between them" },
{ FXP_GROUP, "",  "12d  GRASS  (ours)", 0,0,0,0, "" },
{ FXP_BOOL,  "grass", "real grass", FXO(grass), 0,1,0,
  "stand grass on the ground instead of leaving it to the tile art alone; off is what Classic draws" },
{ FXP_FLOAT, "grass_density", "blades a cell", FXO(grass_density), 0.0f, 128.0f, 0,
  "how many blades stand in one cell. This is the whole per-frame cost of the feature, and it only ever draws fewer blades out of a buffer that was already built" },
{ FXP_STEP,  "grass_density_max", "bake ceiling", FXO(grass_density_max), 1.0f, 128.0f, 1.0f,
  "the most blades a cell the LOAD will generate. Raising it costs memory at load and nothing per frame; moving it rebuilds the whole field" },
{ FXP_FLOAT, "grass_height", "blade height (cells)", FXO(grass_height), 0.03f, 0.30f, 0,
  "how tall a blade stands. 0.20, the shipped value, is about nine screen pixels at the default zoom and under six at the furthest; 0.13 is two thirds of that and a botanically honest blade is 0.068, which cannot read at all. The TOP of this range is baked into the silhouette clearance, so widening the range rebuilds the field and moving the slider does not" },
{ FXP_FLOAT, "grass_width", "blade width (cells)", FXO(grass_width), 0.010f, 0.090f, 0,
  "how wide a blade is at its root" },
{ FXP_FLOAT, "grass_taper", "tip width", FXO(grass_taper), 0.0f, 1.0f, 0,
  "the tip's width as a fraction of the root's; 0 is a spike, 1 a ribbon" },
{ FXP_FLOAT, "grass_vary", "size variation", FXO(grass_vary), 0.0f, 0.45f, 0,
  "how far blades differ in height and width from one another; 0 is a lawn. The top of this range is baked with the height's, for the same reason" },
{ FXP_FLOAT, "grass_lean", "resting lean", FXO(grass_lean), 0.0f, 0.50f, 0,
  "how far a blade leans off vertical with no wind at all; 0 stands the field to attention" },
{ FXP_FLOAT, "grass_wind", "wind bend", FXO(grass_wind), 0.0f, 0.60f, 0,
  "how far the tip travels in the wind, as a fraction of the blade's height; the root never moves at any setting" },
{ FXP_FLOAT, "grass_wind_rate", "wind speed", FXO(grass_wind_rate), 0.10f, 4.00f, 0,
  "the grass's speed as a multiple of the trees'. The BEARING is the trees' own wind_dir, because there is one wind over the map" },
{ FXP_FLOAT, "grass_gust_len", "gust length (cells)", FXO(grass_gust_len), 0.8f, 40.0f, 0,
  "how far apart the crests are as the wind rolls across the field. It is a travelling wave, not one sine over the whole map" },
{ FXP_FLOAT, "grass_arc", "tip drop", FXO(grass_arc), 0.0f, 3.0f, 0,
  "how far a leaning blade shortens. 0 turns the bend into a shear and the field slides sideways as one carpet, which is the fault the trees' first build was called out for" },
{ FXP_FLOAT, "grass_shiver", "shiver", FXO(grass_shiver), 0.0f, 1.0f, 0,
  "how much each blade moves on its own beat across the wind; 0 is one rigid wave" },
{ FXP_FLOAT, "grass_tint", "terrain colour", FXO(grass_tint), 0.0f, 1.0f, 0,
  "how much of a blade is the colour of the ground it grows out of, read from the sheet the terrain is actually drawing" },
{ FXP_FLOAT, "grass_r", "grass red", FXO(grass_r), 0.0f, 1.0f, 0, "" },
{ FXP_FLOAT, "grass_g", "grass green", FXO(grass_g), 0.0f, 1.0f, 0, "" },
{ FXP_FLOAT, "grass_b", "grass blue", FXO(grass_b), 0.0f, 1.0f, 0,
  "the colour a blade eases toward where the terrain's own is not taken" },
{ FXP_FLOAT, "grass_patch", "patch variation", FXO(grass_patch), 0.0f, 0.60f, 0,
  "how far the colour drifts from patch to patch. Per-blade jitter reads as static; a slow wave over the world means neighbours agree and the field breaks into patches, which is what a field does" },
{ FXP_FLOAT, "grass_patch_len", "patch size (cells)", FXO(grass_patch_len), 2.0f, 40.0f, 0,
  "how many cells apart those patches are" },
{ FXP_FLOAT, "grass_root_fade", "root fade", FXO(grass_root_fade), 0.0f, 0.60f, 0,
  "how far up the blade its colour is the ground's. At the very bottom the blade IS the ground under it, so there is nothing to sort and nothing to see through" },
{ FXP_FLOAT, "grass_root_alpha", "root softness", FXO(grass_root_alpha), 0.0f, 0.20f, 0,
  "how far up the blade the alpha ramp runs, which is a much shorter distance than the colour blend above and is what stops the foot of a blade cutting a hard line" },
{ FXP_FLOAT, "grass_edge", "edge softness", FXO(grass_edge), 0.02f, 0.60f, 0,
  "how soft a blade's own side is. Hard edges crawl at three pixels wide and this context has no multisampling to resolve them" },
{ FXP_FLOAT, "grass_ao", "root darkening", FXO(grass_ao), 0.0f, 1.0f, 0,
  "how much darker a blade is at the bottom, down among its neighbours. Shape, not light: the post chain has already lit this pixel as ground" },
{ FXP_FLOAT, "grass_tip", "tip lightening", FXO(grass_tip), 0.0f, 0.80f, 0,
  "how much paler a blade is at the tip, where the sky reaches it" },
{ FXP_FLOAT, "grass_gain", "field exposure", FXO(grass_gain), 0.30f, 2.00f, 0,
  "one exposure over the whole field, kept separate from the terms above so the shape of the field's colour and its level can be set independently" },
{ FXP_FLOAT, "grass_fade", "distance (cells)", FXO(grass_fade), 6.0f, 60.0f, 0,
  "how far out the last blade stands. The density thins over the last quarter of it, so the field recedes instead of a ring switching off" },
{ FXP_FLOAT, "grass_theater_cut", "theater green gate", FXO(grass_theater_cut), 0.0f, 0.40f, 0,
  "how much greener than neutral a theater's CLEAR1 tiles must be before that theater grows grass at all. The shipped 0.035 admits temperate (+0.1723) and winter (+0.0513) and turns away sand (+0.0187), snow and desert, the last two being BELOW neutral. A build-time judgement, so moving it rebuilds the field" },
{ FXP_FLOAT, "grass_colour_sd", "mask generosity", FXO(grass_colour_sd), 1.0f, 6.0f, 0,
  "how many deviations below the CLEAR1 mean the per-texel cut sits. Larger takes in more texels, which widens the verge along every road and cliff; a build-time judgement" },
{ FXP_FLOAT, "grass_touch", "pointer bend", FXO(grass_touch), 0.0f, 1.20f, 0,
  "how far the mouse pointer pushes the grass aside as it moves over it, as a fraction of a blade's height. The root stays put, so the blades lean out of the way and spring back; 0 is a field that does not notice the pointer" },
{ FXP_FLOAT, "grass_touch_r", "pointer reach (cells)", FXO(grass_touch_r), 0.2f, 6.0f, 0,
  "how far from the pointer that reaches. The falloff is quadratic, so the middle is firm and the edge is soft; a hard edge drags a visible disc over the field" },
{ FXP_GROUP, "",  "12e  WHAT THE BATTLE DOES TO IT  (ours)", 0,0,0,0, "" },
{ FXP_FLOAT, "crush_tread_life", "tread mark life (s)", FXO(crush_tread_life), 0.07f, 136.0f, 0,
  "how long a vehicle's tracks take to fade to nothing. Seconds, not a per-tick factor; 136 is the arithmetic ceiling and anything above it is clamped and said out loud" },
{ FXP_FLOAT, "crush_cover_life", "regrowth (s)", FXO(crush_cover_life), 0.07f, 136.0f, 0,
  "how long grass takes to grow back over ground a building or a vehicle has left. A razed footprint greens over on this clock" },
{ FXP_FLOAT, "crush_gauge", "track spacing", FXO(crush_gauge), 0.0f, 1.2f, 0,
  "how far apart a vehicle's two tracks are, as a fraction of its beam; 0 lays one band down the centre line" },
{ FXP_FLOAT, "crush_band", "track width", FXO(crush_band), 0.0f, 0.6f, 0,
  "how wide one track is, as a fraction of the beam. Never thinner than one texel of the field, which is what stops a vehicle driving due north leaving no mark at all" },
{ FXP_FLOAT, "crush_press", "track depth", FXO(crush_press), 0.0f, 1.0f, 0,
  "how hard a track presses. 1 flattens the blade completely, and the last of it shrinks away rather than lying flat, so a track shows soil instead of a green mat" },
{ FXP_FLOAT, "crush_skirt", "clearance", FXO(crush_skirt), 0.0f, 2.0f, 0,
  "how far past a building or a hull the grass is cleared, in blade heights. NOT decoration: at 0 a blade overlaps the silhouette, the ground normal buffer has no ground there, and the post chain rings every structure and every unit with a dark halo one blade tall" },
/* THE DEBUG GROUP, and it is last on purpose: it is a workshop control rather than a
   presentation one, and nothing above it should move when it is toggled. */
{ FXP_GROUP, "",  "13  DEBUG", 0,0,0,0, "" },
{ FXP_BOOL,  "debug", "debug: updated meshes", FXO(debug), 0,1,0,
  "draw every type that a debug.pack beside the mission's pack redefines; off, or with no debug pack, the mission's own art is drawn" },
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

       THE WHOLE PICTURE COMES FROM A TUNING SESSION, not from this file. The settings a
       tuned session was left sitting on were taken back dial for dial, which is what
       docs/tuning/cnc3d-fx.cfg now holds: 248 rows, and re-checked mechanically rather
       than trusted, the saved file and this block agree on 246 of them. EXACTLY TWO ARE
       NOT COPIED, and each is marked again where it is set:

         enabled      the file says 1 because it was saved from a running Enhanced
                      session. It stays 0 here because fx_defaults is what every gate in
                      the project renders from, and the suite must measure the SHIPPED
                      picture; the game turns the chain on for a player at boot, in
                      game_visuals_default_enhanced. Baking 1 would switch the chain on
                      inside every measuring instrument here.
         smooth_anim  the file says 0. That is one machine's preference for how
                      animation reads, not a decision about what the game ships as, and
                      a preference belongs in a player's own settings file where it
                      already is. It stays ON here, which is what a new player gets.

       Anything else that ever stands off this block is drift, not a decision. If these
       change again, change them HERE and re-copy the file; the two must not drift.

       THE SAVED FILE CANNOT TELL YOU WHICH TWO THOSE ARE, so do not go looking there for
       the list. fx_save writes its own header and that header explains one thing only:
       that a trailing comment marks a row standing off the default it shipped with. Both
       rows above carry such a marker, and so would any row somebody had simply nudged on
       the panel before saving, which is the whole difficulty. The list is the two named
       here, and each is marked again at the line that sets it. */
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
       loop turning it on, so --shot and --script are unaffected whatever this says.

       NOT COPIED FROM THE FILE (1 of 2). The tuned file carries smooth_anim 0, and that
       is the one row in it that must NOT be baked: it is a preference about how
       animation reads on one machine, held in that machine's own settings file, and not
       a statement about what the game ships as. A new player gets it on. */
    s->smooth_anim = 1;

    /* ON, by project decision, and harmless to the gates for the same reason the two above are: the
       value is only ever pushed into the sidebar by the Enhanced path or the Visuals
       dialog, and a bare cnc_eyes run reaches neither. */
    s->new_hud = 1;

    /* DOS by project decision: Enhanced Visuals draws the 1995 tile art.
       Harmless to the gates for exactly the reason bilinear's note above gives -- the
       value is a preference and fx_texset_on is the switch, and only the Enhanced path
       and the Visuals dialog move it. Classic goes back to the cartridge. */
    s->texset = (float)FX_TEX_DOS;

    /* The 1995 sprites, which is what this project has drawn since dosinfantry.pack
       landed. Changing the default would change every existing screenshot. */
    s->infset = (float)FX_INF_DOS;

    /* -2x IS THE DEFAULT UI SCALE (5 Sep 2026, by request), and it changes the picture a
       PLAYER sees, not the picture a gate measures: the divisor reaches the sidebar only
       through the Enhanced path and the Visuals dialog, exactly as new_hud does, so a
       bare cnc_eyes run keeps the largest zoom. Borderless at the desktop's size is what
       the game has always asked for, so it stays the display default. */
    s->ui_scale = 1.0f;
    s->display_mode = 1.0f;
    s->res_w = 0.0f;
    s->res_h = 0.0f;
    s->perspective = (float)FX_PERSP_CLASSIC;
    /* THE ISOMETRIC DEFAULTS are the director's own, tuned on the F5 panel and sent
       back: a 16-degree turn to the left, a fixed 53-degree tilt, a
       48-degree field and 1.1x the distance. The first picture was -45 / 0 / 50 / 1. */
    s->iso_yaw = 16.0f; s->iso_pitch = 53.0f; s->iso_fov = 48.0f; s->iso_dist = 1.10f;
    s->cursor_scale = 0.7f;
    s->debug = 0.0f;            /* a workshop control: never on unless asked for */
    s->inf_scale = 0.75f;      /* a quarter smaller, by request */

    /* The softening is wanted, so it is on. Not 1.0: a full blur of a 24x24 frame
       loses the crater's shape as well as its edge. 0.6 takes the staircase off without
       turning a scorch mark into a smudge of the wrong kind. A panel dial, so it stays
       open to tuning. */
    s->decal_soft = 0.60f;

    /* ON, by decision: the gesture was asked for and a player should not have to go
       looking for it. Harmless to the gates because it arms only from a real
       right-button press that has travelled past the click threshold. */
    s->right_drag_scroll = 1;

    /* ON: it is what both source games did. Harmless to the picture gates, which are
       silent runs; the sound gates that measure a silent stretch turn it off by name. */
    s->cash_tick = 1;

    /* 2. NOT COPIED FROM THE FILE (2 of 2). The saved preset says enabled 1 because it
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

    /* 1x, AND THE TUNED FILE AGREES. An earlier preset carried ss_scale 2, saved from a
       session with the Visuals SUPERSAMPLE box ticked, and was deliberately not copied:
       fx_defaults reaches every gate, so baking 2 would quietly make the whole measuring
       suite render at twice the resolution. Supersampling is already a checkbox, and the
       box reads its own state back off this dial, so a player who wants it still gets
       it. Kept as a note because it is the trap a future re-copy would walk into. */
    s->ss_scale = 1.00000f;

    /* THE SUN. Tuned on the panel and kept, not the cartridge's, and knowingly so: the
       ROM's own light is 45.0 / 35.264 and this is 20.12 degrees off it. Registered as a
       deliberate deviation as a known gap; do not "correct" it back. */
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
    /* THE SMOOTH GROUND, on: the per-corner normal, a 1.5-texel normal
       offset, a penumbra of 0.08 cells (about five texels at the default box), every
       cell still casting. Tuned on the SCB05EA cliff; docs/CHANGELOG.md has the why. */
    s->terrain_normals = 1;
    s->sun_lambert = 1;
    s->sun_floor = 0.55f;
    s->veh_slope = 1.0f;
    s->veh_slope_span = 0.40f;
    s->veh_slope_max = 32.0f;
    s->spr_shadow_scale = 1.7f;
    s->shadow_noff = 1.5f; s->shadow_pen = 0.08f; s->shadow_terrain_rise = 0.0f;
    s->ssao_foliage = 0.79464f;

    /* CLOUD SHADOWS. TUNED THROUGH THE PANEL 4 Sep 2026 and ON from here, which is a
       reversal of the day before: they shipped off, on the argument that the cartridge
       has no weather and a bare run should draw the picture this project has always
       drawn. That argument is answered by WHERE the switch sits rather than by its
       value. The chain does not run at all in CLASSIC -- fx_world_begin returns on
       !enabled before anything is drawn -- so a cloud cannot reach the classic picture
       whatever this says, and ENHANCED is the mode that already carries the sun, the
       occlusion and the bloom, none of which the cartridge has either. On here means
       on in ENHANCED and nowhere else, and that is the whole of it.

       THE SET IS DELIBERATELY SUBTLE and reads as weather in the light rather than as
       shapes crossing the ground: strength 0.473 takes about 28 levels out of the
       terrain at its deepest, over about a third of the view. cloud_scale is 20, which
       is the BOTTOM OF ITS RANGE -- one mass is then about two and a half cells, so
       this is fine tonal variation and not the drifting slabs the first draft aimed
       at. If it is ever wanted finer still, the range is what has to move, not this
       number; see the row in FX_PARAMS. */
    s->cloud_on = 1;
    s->cloud_strength = 0.47300f;
    s->cloud_cover = 0.34800f;
    s->cloud_sharp = 0.03600f;
    s->cloud_scale = 20.00000f;
    s->cloud_speed = 0.57100f;
    s->cloud_rot = 67.50000f;
    s->cloud_height = 12.00000f;

    /* The sea, 7 Sep 2026: a first set by eye, not yet tuned through the panel. On in
       Enhanced the way the clouds are. */
    s->water_fx = 1;
    s->water_flow = 0.35000f;
    s->water_flow_rate = 0.25000f;
    s->water_current = 67.50000f;
    s->water_foam = 0.00000f;
    s->water_foam_width = 0.90000f;
    s->water_wave_len = 0.50000f;
    s->water_wave_speed = 0.35000f;
    s->water_shallow = 0.30000f;
    s->water_deep = 0.30000f;
    s->water_edge = 0.45000f;
    s->water_fade = 0.90000f;
    s->water_fade_width = 0.90000f;
    s->water_foam_cross = 0.30000f;
    s->water_pan = 0.35000f;
    s->water_river_width = 3.00000f;
    s->water_river_speed = 1.50000f;
    s->water_bump = 0.42000f;
    s->water_reflect = 0.26000f;
    s->water_spec = 0.85000f;
    s->water_refract = 0.02000f;
    s->water_fresnel = 0.13000f;
    s->water_reflect_scene = 1;
    s->water_natural = 0.35000f;
    s->water_dark = 0.16000f;
    s->water_wake = 1.10000f;
    s->water_wake_width = 1.20000f;
    s->water_wake_sim = 1;
    s->water_wake_bump = 1.20000f;
    s->water_wake_ring = 0.12000f;
    s->water_wake_speed = 0.02000f;

    s->bloom_on = 1;
    s->bloom_threshold = 0.68000f; s->bloom_knee = 0.15000f;
    s->bloom_intensity = 0.28571f; s->bloom_radius = 1.60000f; s->bloom_passes = 4.0f;

    s->ssao_on = 1;
    s->ssao_radius = 0.27634f; s->ssao_intensity = 1.35714f;
    s->ssao_bias = 0.01384f;   s->ssao_power = 1.31250f; s->ssao_samples = 12.0f;
    s->ssao_ground = 0.5f;

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
    s->exposure = 0.10700f;  s->contrast = 1.10300f; s->saturation = 1.08000f;
    s->temperature = 0.12500f; s->tint = 0.0f; s->lift = 0.0f; s->gain = 1.10300f;

    /* OFF, AND THE TUNED FILE AGREES, which makes the tube's own dials below whatever
       the panel last showed rather than a chosen look. They are copied anyway so the
       file and this block agree dial for dial. */
    s->crt_on = 0;
    s->crt_scanline = 0.10714f; s->crt_mask = 0.71429f; s->crt_curve = 0.00536f;
    s->crt_bleed = 0.48214f; s->crt_vignette = 0.32143f; s->crt_hud = 1;
    /* 3. BUILDING SHATTER AND CAMERA SHAKE ARE A TUNED SET, taken off the panel dial for
       dial and replacing the first cut's guesses. What the tuning settled on: a harder
       outward blast (0.85 -> 1.098) with much LESS tumble (1.0 -> 0.295), pieces that
       roll a little less far (1.0 -> 0.688), a short linger at the slider's floor
       (3.0 -> 0.5, so the ground clears quickly), and a much heavier jolt (1.0 -> 2.518).

       CENTRE PULSE IS THE ONE DIAL NOT TAKEN FROM THAT SET. It was on there and shipped
       that way at first, then went back OFF once the reason was understood: the SHOCK
       texture is still baked through a misaligned window and draws about half a
       sprite-height BELOW the blast, which is registered as a defect of the bake. Turning
       it on shows that defect rather than the effect. It stays off until the sequences
       change and the effects pack is rebaked, and then it can be switched on as a fix
       rather than as a preference. */
    s->shatter_on = 1; s->shatter_pulse = 0; s->shake_on = 1;
    s->shake_amount = 2.518f;
    s->shatter_force = 1.098f; s->shatter_spin = 0.295f;
    s->shatter_roll = 0.688f; s->shatter_linger = 0.5f;

    /* 12. SELECTED ONLY, which is what the vanilla engine itself defaults to:
           Special.HealthBarDisplayMode = HB_SELECTED (tiberiandawn/special.h:76). This
           renderer shipped on the OTHER position, selected-or-damaged, and it was wrong
           in the way an always-on debug overlay is wrong -- every damaged buggy on the
           field wore a bar nobody had asked to see. The literal is HB_MODE_SELECTED,
           which cannot be named here: game/shroud_mod.h declares it and is included well
           below this header. The pip row reads the same rule and moves with it, which is
           also what the engine does. A cfg written before this dial existed carries no
           hb_mode line and now inherits selected-only. */
    s->hb_mode = 1.0f;

    /* 12b. SOLID TIBERIUM, ON under Enhanced. It is the whole reason the group exists,
            and Classic never reads any of it, so the shipped picture and every gate --
            none of which passes --gfx -- are untouched. The size is judged by eye
            against a unit: a full clump stands about half a medium tank's height. */
    s->tib3d = 1;
    s->tib3d_size = 0.27036f;
    s->tib3d_density = 1.56250f;
    s->tib3d_pod = 0.85f;
    s->tib3d_seat = 1.0f;
    s->tib3d_sink = 0.02143f;
    s->tib3d_glow = 0.70982f;
    s->tib3d_ground = 0.45536f;
    s->tib3d_pulse = 0.63393f;
    s->tib3d_growtime = 0.8f;
    s->tib3d_decal = 0;

    /* 12c. LEAFY TREES, ON under Enhanced. The height is judged by eye against a unit,
            the way the crystals are. The wind is deliberately gentle: a tree that waves
            is a tree nobody stops noticing. */
    s->tree3d = 1;
    s->tree3d_size = 1.56250f;
    s->tree3d_wind = 0.04107f;
    s->tree3d_wind_speed = 0.12902f;
    s->tree3d_wind_dir = 183.21429f;
    s->tree3d_vary = 0.22f;
    s->tree3d_burn = 1.0f;
    s->tree3d_lean = 0.16071f;
    s->tree3d_ao = 0.82143f;
    s->tree3d_trans = 0.62946f;
    s->tree3d_sheen = 0.52679f;
    s->tree3d_ambient = 0.76339f;
    s->tree3d_flutter = 0.32143f;
    s->tree3d_wrap = 0.81696f;
    s->tree3d_ao_floor = 0.38393f;
    s->tree3d_ao_gamma = 0.70982f;
    s->tree3d_ao_direct = 0.90179f;
    s->tree3d_gain = 0.36071f;

    /* THE RAIN, TUNED THROUGH THE PANEL and no longer the first set by eye. The numbers
       below are what the tuning panel's Rain switch turns on. There is no row for it on
       the Advanced page: rain ships off under ENHANCED as well as CLASSIC and stays a
       panel-only feature until its look is settled, so a release build, whose panel is
       compiled out, cannot reach it. */
    /* OFF by decision, and the tuned file agrees. The gates need it off as well: every
       --gfx gate reaches fx_defaults, and the inland arm of the camera gate requires two
       sub-tick phases of one tick to agree within fifty pixels, which falling rain
       cannot do. An earlier preset carried rain_fx 1 and was not copied. */
    s->rain_fx = 0;
    s->rain_amount = 0.76786f;
    s->rain_streaks = 0.84821f;
    s->rain_speed = 17.25000f;
    s->rain_len = 0.45f;
    s->rain_width = 0.020f;
    s->rain_height = 6.99107f;
    s->rain_slant = 0.25f;
    s->rain_wind = 154.28572f;
    s->rain_wet = 0.74107f;
    s->rain_dark = 0.49107f;
    s->rain_gloss = 26.50000f;
    s->rain_spec = 0.48214f;
    s->rain_ripple = 0.53571f;
    s->rain_ripple_scale = 6.99107f;
    s->rain_ripple_rate = 1.2f;
    /* THE PUDDLES ARE BACK ON, small. What made the first one a grey stain was a soft
       mask and a mid-grey fill; with a crisp outline, the sky held inside it and the
       rings breaking that up, a patch of standing water reads as standing water. */
    /* SMALL, and it has to be: on the first desert map every cell is level, so a tenth
       of the noise field became a rash of pale patches over the whole board. */
    s->rain_puddle = 0.37500f;
    s->rain_haze = 0.00000f;
    s->rain_overcast = 0.38393f;
    s->rain_drops = 0.00000f;
    s->rain_drop_force = 0.00000f;
    s->rain_water_ripple = 0.00000f;
    /* The second set, after the first was seen: the rings were far too large and too
       many and read as puddles everywhere, so they are a third the size, sparse and
       quieter; the puddles are a few patches rather than a quarter of the ground; the
       wet look leans on a glossy sky sheen instead; and a low fog drifts on the wind. */
    s->rain_ripple_density = 0.07143f;
    s->rain_ripple_shade = 0.00000f;
    s->rain_sheen = 0.07143f;
    s->rain_puddle_size = 34.91071f;
    s->rain_fog = 0.07143f;
    s->rain_fog_height = 1.08036f;
    s->rain_fog_scale = 21.16071f;
    s->rain_fog_speed = 1.00000f;
    s->rain_fog_detail = 1.00000f;
    /* THE RELIEF, and it is the answer to "this needs proper normals for everything":
       there are none in the data, so it is derived from the art. Ground carries more
       of it than a rolled steel plate does. */
    s->rain_relief = 0.06250f;
    s->rain_relief_body = 0.08036f;
    s->rain_puddle_mirror = 0.24107f;
    s->rain_puddle_edge = 0.26049f;
    s->rain_puddle_flat = 0.80355f;
    s->rain_sound = 0.45f;
    s->rain_splash = 0.00000f;
    s->rain_splash_size = 0.05000f;
    s->rain_splash_rate = 0.50000f;
    s->rain_sea = 0.69643f;
    s->rain_puddle_ssr = 0.62500f;
    s->tree3d_anim = 2.27679f;
    s->tree3d_anim_wind = 0.50000f;
    /* 12d. GRASS, OFF by default under ENHANCED too, and reachable only through the
            tuning panel, like the rain: no Advanced row until the look is settled.

            THE HEIGHT IS A PICTURE DECISION AND IT HAS BEEN MADE: 0.184 cells. It is
            not a measurement and it is not the botanically honest number, which is
            0.068 cells and comes out at 1.9 screen pixels on a 720p frame, well under
            what can read as anything but noise. 0.184 is 5.3 to 10.8 px depending on
            the zoom and reads as grass at a glance. The cost is a taller silhouette
            clearance, and it is paid: the draw asks for height*(1+vary)*1.35/tan(pitch),
            which at 0.184 is 0.28 cells at the shipped camera and 1.03 at the flattest
            the isometric row reaches, against the 1.216 the load measures and bakes. So
            it fits at both ends without the draw's clamp ever biting. */
    /* OFF by decision, and the tuned file agrees. An earlier preset said grass 1,
       saved from a session with the row ticked, and was not copied. It ships OFF so the
       ground a player meets first is the cartridge's; the tuning panel turns it on. Two
       legs of the grass gate set it by hand for the same reason. */
    s->grass = 0;
    s->grass_density = 128.0f;
    s->grass_density_max = 128.0f;
    s->grass_height = 0.18429f;
    s->grass_width = 0.03286f;
    s->grass_taper = 0.33929f;
    s->grass_vary = 0.45f;
    s->grass_lean = 0.22f;
    s->grass_wind = 0.28f;
    s->grass_wind_rate = 1.00f;
    s->grass_gust_len = 9.00f;
    s->grass_arc = 0.90f;
    s->grass_shiver = 0.35f;
    s->grass_tint = 0.22321f;
    s->grass_r = 0.42f; s->grass_g = 0.52f; s->grass_b = 0.24f;
    s->grass_patch = 0.25f;
    s->grass_patch_len = 7.00f;
    s->grass_root_fade = 0.22f;
    s->grass_root_alpha = 0.06f;
    s->grass_edge = 0.30f;
    s->grass_ao = 0.55f;
    s->grass_tip = 0.40714f;
    s->grass_gain = 1.00f;
    s->grass_fade = 26.0f;
    /* THE THEATRE GATE ADMITS TEMPERATE AND WINTER AND NOTHING ELSE, which is a
       decision about what the game should look like rather than a threshold. Measured
       green excess of each bank's CLEAR1 mean over neutral: TEMPERAT +0.1723, WINTER
       +0.0513, SAND +0.0187, SNOW -0.0111, DESERT -0.0182. A gate at 0.035 sits between
       winter and sand with room on both sides: it clears winter by a factor of 1.5 and
       turns sand away by 1.9. Desert stays bare on purpose. Its blades would take the
       sand's own colour, because a blade is mostly the ground it grows out of, so
       admitting it draws a quarter of a million cards that change the picture by an
       average of six levels a pixel and read as texture rather than as grass. */
    s->grass_theater_cut = 0.035f;
    s->grass_colour_sd = 3.00f;
    /* A NUDGE, NOT A BOW WAVE. The pointer is a hand passing over a field, so the tip
       moves about half the blade's height and the circle is a little under two cells,
       a shade wider than the 3D cursor's own footprint on the ground. */
    s->grass_touch = 0.54643f;
    s->grass_touch_r = 1.85714f;

    /* 12e. The two life dials land on a whole number of the field's own quantisation
            step, which is what makes the decay exact in half floats: 60 s asks for two
            ulps a tick and achieves 68.3, and 18 s asks for eight and achieves 17.1. The
            achieved value is printed rather than the asked-for one. */
    s->crush_tread_life = 60.0f;
    s->crush_cover_life = 18.0f;
    s->crush_gauge = 0.47143f;
    s->crush_band = 0.05893f;
    s->crush_press = 0.30357f;
    s->crush_skirt = 1.00f;
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
