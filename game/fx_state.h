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
#ifndef OPENCNC3D_FX_STATE_H
#define OPENCNC3D_FX_STATE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "object_grade.h"

/* Same spelling fx_filter.h uses, and defined here too because this header is included
   by translation units that never pull that one in. */
#if defined(__GNUC__) || defined(__clang__)
#define FX_STATE_MAYBE_UNUSED __attribute__((unused))
#else
#define FX_STATE_MAYBE_UNUSED
#endif

/* WHICH TERRAIN TILE ART. Written into the saved preset as a number, so the order is
   part of the file format: append, never renumber. */
enum { FX_TEX_N64 = 0, FX_TEX_DOS = 1, FX_TEX_REMASTER = 2, FX_TEX_ENH = 3 };

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

/* THE THREE GRAPHICS MODES. Not a field of their own: the mode is read off the two
   switches that already decide it, so nothing can hold a mode that disagrees with what is
   drawn (fx_mode_of below).
     CLASSIC        original 320x200 top-down DOS sprites and sidebar
     N64            the cartridge-style 3D picture
     ENHANCED       the existing lit and graded 3D picture
   Written into the saved preset by NAME (FX_MODE_NAME), so the numbers are free to move,
   but keep them in this order: the dialog's buttons use the same numbering. */
enum { FX_MODE_CLASSIC = 0, FX_MODE_N64 = 1, FX_MODE_ENHANCED = 2, FX_MODE_COUNT = 3 };

/* THE ROWS EACH MODE KEEPS FOR ITSELF, one per switch on the Visuals dialog's Advanced
   page that belongs to a mode rather than to the machine. Every one of them is also an
   ordinary field of FxState below; the table that ties the two together is
   FX_MODE_ROWS, after the struct. Append, never renumber: the order is only the
   in-memory layout of FxState::mode_row, but a reader should not have to wonder. */
enum {
    FXR_SMOOTH = 0, FXR_NEWHUD, FXR_UISCALE, FXR_PERSP, FXR_BILINEAR, FXR_WATER,
    FXR_TREES, FXR_TIB3D, FXR_TEXSET, FXR_INFSET, FXR_GAMMA, FXR_SS, FXR_SHADOWS, FXR_AO,
    FXR_LIGHT, FXR_BLOOM, FXR_GRADE, FXR_CRT, FXR_GRASS, FXR_FOLIAGE, FXR_ROCKS,
    FXR_SOLDIERS, FXR_MODELS, FXR_UVFX, FXR_PATCHES, FXR_RELIEF, FXR_POINTER,
    FXR_NEWAUDIO, FXR_EINF, FXR_WATER_SWASH, FXR_COUNT
};

struct FxState {
    /* 0. BILINEAR FILTERING, and it is deliberately NOT under the master switch.
          It is not a post pass -- it changes how every texture in the program is
          sampled, the 3D pack art and the 1995 sidebar, cameos, fonts, cursors,
          movies and menu alike -- so it stands on its own and works with the rest of
          the chain switched off. fx_filter.h owns the mechanism. Note it is arguably
          a fidelity feature rather than a modern one: the N64's RDP filtered its
          textures bilinearly, and point sampling is what WE chose. */
    int   bilinear;
    /* show_stats  the resolution and frame-rate counter at the top of the screen
                   (fxp_draw_stats in fx_panel.h). A reading, not a look: off changes
                   no pixel of the picture. */
    int   show_stats;
    /* vsync  0 off, 1 on (the default, what always shipped), 2 adaptive where the
              driver has it. --vsync on the command line wins over it. */
    float vsync;

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
            FX_TEX_ENH       the 1995 tiles remastered at 144 texels a cell, ENHANCED's
                             default: shipped as terrainenh_<THEATER>.pack, built at
                             runtime into a sheet of its own size (24 x 6, one texel for
                             one), with its join strips in terrainseam_<THEATER>.pack
                             (terrain_seam.h); a theater with no pack draws the DOS art
            FX_TEX_REMASTER  the 2020 Remastered Collection's art, read from the
                             player's OWN install at runtime and never shipped by us.
                             WITHDRAWN: no menu offers it, and a saved file that holds
                             it loads as its mode's default (fx_art_withdraw). The
                             number stays so that FX_TEX_ENH stays 3.

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
    float sand_brightness, sand_contrast; /* Enhanced Desert ground grade, 1 neutral */
    float sand_saturation;           /* independent Enhanced Desert ground material colour */
    float clear_saturation;          /* saturation of the Enhanced clear-ground family */

    /* 0c-3. WHICH INFANTRY ART DRAWS, the fifth thing here that is not a post pass.
          FX_INF_N64 is the cartridge's billboards and DETH strips; FX_INF_DOS is the 1995
          sprite art, which is what has shipped and stays the default; FX_INF_REMASTER is
          the 2020 art, read from the player's own install at runtime and never shipped by
          us. Withdrawn as FX_TEX_REMASTER is: no menu offers it, and a saved file that
          holds it loads as its mode's default.
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
          opencnc3d-fx.cfg remembers. A second settings mechanism for one boolean would be a
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
    int   dos_classic;      /* original 320x200 top-down 2D presentation */

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
    int   experimental_textures;     /* generated temperate grass and cliffs in Remastered */
    int   experimental_relief;       /* GPU terrain displacement and surface material */
    float experimental_height;       /* terrain relief scale in world cells */
    float experimental_relief_mode;  /* 0 parallax occlusion mapping, 1 the 24x24 lattice */
    float experimental_relief_steps; /* the parallax march's steps through the height */
    int   new_audio;                 /* replacement sound effects from newaudio/ */
    int unified_lighting;
    float sun_energy, sky_energy, terrain_ambient;
    float shadow_black_limit; /* Display toe for shaded detail; no additive world light. */
    float object_shadow_fill; /* Lifts sun shadowing and AO on structures and trees. */
    /* Independent albedo colour balance for the four exterior biome groups. */
    float grass_terrain_warmth, grass_terrain_tint, grass_terrain_red, grass_terrain_green, grass_terrain_blue;
    float desert_terrain_warmth, desert_terrain_tint, desert_terrain_red, desert_terrain_green, desert_terrain_blue;
    float sand_terrain_warmth, sand_terrain_tint, sand_terrain_red, sand_terrain_green, sand_terrain_blue;
    float snow_terrain_warmth, snow_terrain_tint, snow_terrain_red, snow_terrain_green, snow_terrain_blue;

    int terrain_relief;
    float terrain_relief_height;
    float sun_az, sun_el;             /* degrees: compass bearing, and height          */
    float shadow_strength;            /* 0 = invisible, 1 = full tint                  */
    float shadow_soft;                /* PCF radius in shadow-map texels               */
    float shadow_bias;                /* depth bias in WORLD CELLS, not normalised    */
    float shadow_span;                /* ortho box size as a multiple of the view rect */
    float shadow_ox, shadow_oz;       /* box centre offset from the view, in cells     */
    float shadow_res;                 /* shadow map edge, quantised to a power of two  */
    float shadow_r, shadow_g, shadow_b;/* what a shadowed pixel is multiplied BY       */
    int   shadow_terrain;             /* let the ground cast into itself (hills)       */
    float desert_shadow_strength, sand_shadow_strength, snow_shadow_strength;
    float terrain_shadow_strength;   /* terrain casters only; object shadows retain their strength */
    float snow_brightness, snow_contrast, snow_saturation; /* Enhanced Snow terrain multipliers, 1 neutral */
    float terrain_brightness, terrain_contrast; /* ground colour before lighting; 1 is neutral */
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
    /* 3b. CONTACT SHADOWS (ours). The shadow map is one box over everything the camera
           can see, so one of its texels is a good fraction of a cell and its bias is
           set where a hillside stops shadowing itself. Neither leaves it able to draw
           the few pixels of darkness directly beneath a hull -- and that darkness is
           most of what stops an object looking like it hovers. This walks a short way
           from the surface toward the sun through the DEPTH BUFFER instead.
         contact_on       the switch, and it rides inside sun shadows: off there means
                          no sun shadow of any kind, which is what the existing A/B has
                          always meant.
         contact_len      how far the march reaches, in cells. A third of a cell is
                          about the gap under a tank; much further and it becomes a
                          second, worse ambient occlusion.
         contact_steps    samples along that reach. Each one is a depth fetch.
         contact_thick    how far behind the recorded surface a sample may land and
                          still count as blocked. A depth buffer stores a surface and
                          not a solid, so without a ceiling here the horizon shadows
                          the whole map.
         contact_bias     how far along the normal the march starts, which is what
                          stops a surface finding itself.
         contact_strength how dark a found blocker makes the pixel. */
    int   contact_on;
    float contact_len, contact_steps, contact_thick, contact_bias, contact_strength;
    /* the occlusion's share on FOLIAGE, which is neither ground nor a hull: its
       hemisphere is built from a reconstructed normal and every tap lands on another
       leaf, and the trees already carry occlusion baked from the dense original. */
    float ssao_foliage;
    float ssao_object;
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

    /* Independent drifting air volumes, measured in world cells. Tier 2 only. */
    int fog_fx, sand_fx;
    float fog_density, fog_height, fog_scale, fog_speed, fog_wind, fog_light;
    float sand_density, sand_height, sand_scale, sand_speed, sand_wind, sand_light;

    /* 3c. THE ENHANCED SEA (ours, water_mod.h). water_fx swaps the cartridge's two-tile
          wash for a shader that knows where the coast is: a signed distance field
          built from the terrain art's own alpha holes, a flow that turns shoreward
          inside the foam band, crests rolling in, a wet rim, thinner wash over the
          shallows and a darker deep. Off draws the cartridge's pass through the same
          binary. Every distance is in world cells, every speed in cells (or tile
          repeats) per second of engine time. */
    int   water_fx;
    int   water_swash;                /* animated beach wash and cellular foam, Enhanced */
    float water_swash_hold;           /* extra seconds at maximum beach run-up           */
    float water_swash_opacity;        /* alpha of the swash film and foam together        */
    float water_align_length;         /* full shore alignment distance, world cells      */
    float water_align_blend;          /* additional fade to the offshore current, cells  */
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
    float water_spec;                 /* the sun's glint                                   */
    float water_reflection_yaw;       /* water-only highlight bearing offset, degrees      */
    float water_refract;              /* how far the ripple bends what is under the water  */
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
    float muzzle_bloom, muzzle_flash_gain, muzzle_light_gain;
    float explosion_bloom; /* luminous explosion-particle bloom gain only */
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
    /* The light a tiberium field gives off (tibglow_mod.h): a field-wide glow read from
       a texture of the whole map, not one of the 24 point-light slots. tib_light is its
       strength, tib_light_spread how many cells it reaches past the field's edge, and
       tib_light_height how far up a unit or a wall it climbs before it fades. */
    float tib_light, tib_light_spread, tib_light_height;
    /* The remastered vehicles' own lamps (evhc_mod.h): every headlight, tail light,
       marker and beacon an export's lights.json names, as a cone of light on what stands
       in front of it, its lens glowing. vlight_head scales the headlights and
       searchlights, vlight_tail every other lamp; vlight_cone and vlight_range scale
       their cones and their reach; vlight_glow the lens's glow; vlight_shadows lets a
       headlight be stopped by what stands between it and a surface (a short march
       through the depth buffer, as the contact shadows take). Not one of the 24 slots:
       the lamps have their own (FX_MAX_SPOTS). */
    int   vlight_on, vlight_shadows;
    float vlight_head, vlight_tail, vlight_cone, vlight_range, vlight_glow;
    /* The remastered vehicles' specular maps (evhc_mod.h): a vehicle whose export carries
       one (<TYPE>.spec) shines where its map says, a sun highlight that slides over the
       hull as it turns and a faint sheen of the sky at grazing angles, both out of the
       sun's shadow. vspec_sun scales the highlight, vspec_sky the sheen, vspec_gloss
       tightens (above 1) or spreads (below) every highlight. */
    int   vspec_on;
    float vspec_sun, vspec_sky, vspec_gloss;

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
           moves if a stray opencnc3d-fx.cfg sets them, because the launch speed, the tumble
           rate and the roll friction are all read straight out of this struct. */
    int   shatter_on, shatter_pulse, shake_on;
    float shatter_force, shatter_spin, shatter_roll, shatter_linger, shake_amount;

    /* 12. THE HEALTH-BAR SHOW RULE. Like the shatter dials above it, and unlike
           everything before those, this changes what the renderer DECIDES rather than
           how a post pass looks: 0 off, 1 selected only, 2 selected or damaged. It
           lives in this struct so that it saves and loads through opencnc3d-fx.cfg with no
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
    /* inf_scale_exp  the same size, under experimental art instead of inf_scale, so the
                      experimental look (the 3D soldiers above all) can stand at its own
                      size without moving the Enhanced picture's. Read through
                      inf_scale_now() in cnc_eyes.cpp. */
    float inf_scale_exp;
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
    /* THE CRYSTALS' COLOUR (tibglow_mod.h). tib_hue is a position on the colour wheel in
       degrees (0 red, 120 green, 240 blue); the art's own crystals measure 105.1, which
       is the default and draws the original sheet untouched. tib_sat scales the
       crystals' saturation. The field's light and fog turn with them. */
    float tib_hue, tib_sat;
    /* THE GLOWING FOG OVER A FIELD (tibglow_mod.h), Tier 2, Enhanced only.
         tibfog_density  how thick it is at the ground in a field's middle
         tibfog_height   cells over which it thins upward (to a third)
         tibfog_spread   cells it reaches past the field's edge
         tibfog_glow     how hard the field's own glow lights it
         tibfog_ambient  how bright it is unlit, as grey haze
         tibfog_wisps    0 an even layer, 1 broken into drifting wisps
         tibfog_drift    how fast the wisps drift
         tibfog_steps    samples along each view ray: quality against cost */
    int   tibfog_on;
    float tibfog_density, tibfog_height, tibfog_spread, tibfog_glow, tibfog_ambient;
    float tibfog_wisps, tibfog_drift, tibfog_steps;

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
    float etree_height, etree_width, etree_vary, etree_lean, etree_gain;
    float etree_shadow_fill; /* Indirect canopy fill in the current remastered-tree path. */
    /* REMASTERED EFFECTS. efx_enh: draw the particle effects with the remastered art in
       efx_enh.pack (tools/art/remaster/fx) and add Enhanced mode's own emitters (muzzle
       flashes, the landing-zone flare, efx_enhanced.h). Off = the cartridge's art and
       nothing added, which is what Classic draws. Every particle still flies, grows and
       fades as the cartridge says. */
    int   efx_enh;
    /* THE PARTICLES' OWN GRADE under Enhanced (effects_mod.h, efx_grade_*): brightness
       multiplies, contrast is an S-curve that keeps black and white where they are (so an
       additive particle's black stays clear), saturation mixes toward grey. Each at 1 is
       exact: the particles draw as they always did, with no shader. */
    float efx_bright, efx_contrast, efx_sat;
    float efx_exposure, efx_gamma, efx_warmth, efx_tint, efx_red, efx_green, efx_blue;
    /* REMASTERED BUILDING APRONS (bibenh_mod.h). bib_enh: draw the dirt apron in front
       of a building from bibenh.pack, at twice the cartridge's texels, with a soft edge,
       multiplied into the ground under it. bib_enh_strength scales the whole apron, 0 = no
       apron at all, 1 = the art as baked. bib_enh_cover is how much of the painted dirt is
       laid over the ground before the multiply: 0 = a pure multiply, every hue of the
       ground carrying through, 1 = the painted dirt as opaque as its fade. Off = the
       cartridge's aprons, which is what Classic draws. */
    int   bib_enh;
    float bib_enh_strength, bib_enh_cover;
    /* REMASTERED CRATERS AND SCORCH MARKS (decalenh_mod.h). decal_enh: draw them from
       decalenh.pack, the 1995 PC art painted again at the ground's 144 texels a cell. Off
       = the cartridge's, which is what N64 draws. */
    int   decal_enh;
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

    /* 3e. DISTANCE FOG (ours, distfog_mod.h). Enhanced only. The far part of the view,
          the top of the screen under this tilted camera, sinks into a pale haze of the
          theater's own colour while the ground near the middle keeps its full colour
          and contrast: the atmospheric depth of a later 3D RTS, which neither the
          cartridge nor the 1995 game has. One set of dials per theater, because a
          warm dusty haze over sand and a cool damp one over grass are different looks.
          Temperate and Desert only; Winter and the rest draw none. Distances are cells
          measured from the ground the camera looks at, not from the eye: from this
          rig the nearest and the farthest ground on screen are only a few cells apart,
          so a fog measured from the eye washes the whole frame one even tint.
          ON in the compiled defaults, as tuned; dfog_on 0 draws exactly what drew
          before it existed, which is the switch a gate measuring something else can
          use if the haze ever gets in its way. */
    int   dfog_on;
    float dfog_t_amount;              /* Temperate: how thick the haze gets at its fullest */
    float dfog_t_start;               /* cells past the look-at point the haze begins      */
    float dfog_t_depth;               /* cells over which it thickens to its fullest       */
    float dfog_t_r, dfog_t_g, dfog_t_b; /* the haze colour                                 */
    float dfog_d_amount;              /* Desert: the same five                             */
    float dfog_d_start;
    float dfog_d_depth;
    float dfog_d_r, dfog_d_g, dfog_d_b;
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
                          blade tall around every building and every unit.
         crush_flatten    how long grass a vehicle drives over takes to bend down flat,
                          seconds of simulated time
         crush_fade       how long the flattened grass then takes to fade away once the
                          vehicle has left it, seconds of simulated time. What follows is
                          the regrowth clock above: the ground greens over on
                          crush_cover_life, and the blades stand back up on
                          crush_tread_life. Nothing a vehicle does removes grass in one
                          frame. */
    float crush_tread_life, crush_cover_life, crush_gauge;
    float crush_band, crush_press, crush_skirt;
    float crush_flatten, crush_fade;
    /*   crush_bld_ring   how far round a building put down during the mission the grass
                          is pressed flat and cleared, in cells from the footprint. It
                          stays cleared while the building stands and grows back once it
                          is sold or destroyed. */
    float crush_bld_ring;
    /* 12f. THE TRACKS A VEHICLE LEAVES ON THE GROUND (ours). The block above marks
            GRASS, in a field the grass shader samples, so on a map with no grass on it
            -- every desert mission and most of the rest -- it shows nothing. These
            marks are drawn on the ground itself, as a decal along the path the vehicle
            actually took. The HULL is the same measurement off the same mesh, so both
            features agree about which vehicle is wide; the two width dials are separate
            and do NOT ship at the same numbers, because the field's stamp has a
            one-texel floor under it that sets its width in practice and a decal has no
            such floor. See the defaults for the arithmetic.
         tmark_on     the switch. The only row here that is not a float.
         tmark_life   how long a mark takes to fade to nothing, in SECONDS of simulated
                      time. A vehicle that stops leaves its trail to fade on this clock.
         tmark_dist   how far behind the vehicle the trail is drawn, in cells, with the
                      last 40 per cent of it a ramp so the far end thins away rather
                      than ending at a line across the ground. Capped by how much path
                      one trail can hold, and the ceiling here is below that cap on
                      purpose so the dial is never the thing that is lying.
         tmark_dark   how far a fresh mark darkens the ground, before either fade.
         tmark_gauge  how far apart the two tracks are, as a fraction of the beam.
         tmark_band   how wide one track is, as a fraction of the beam.
         tmark_cleat  how far apart the cleat prints are along the track, in cells. The
                      pattern is generated rather than baked, so this is a real length
                      in the world and not a texture the art has fixed.
         tmark_r/g/b  the colour the GROUND IS MULTIPLIED BY under a full cleat print,
                      not a colour painted over it. A multiply is proportional, so a
                      track is as legible on dark grass as on bright sand and can never
                      come out brighter than the ground it lies on; below 1 on every
                      channel it darkens, and the three being unequal is what tips a
                      track warm. White is the identity and draws nothing.
         tmark_blood  seconds a vehicle's tracks are laid red after it drives over a
                      body, a severed piece or the blood under one; the red holds for
                      the first half and fades back to the ordinary track over the
                      second. 0 turns it off. */
    int   tmark_on;
    float tmark_life, tmark_dist, tmark_dark;
    float tmark_gauge, tmark_band, tmark_cleat;
    float tmark_r, tmark_g, tmark_b;
    float tmark_blood;
    /* 12g. THE AUTHORED VEHICLE MODELS (ours, and every part of them). A .c3m installed
            beside the missions replaces the cartridge's model for that type and is lit
            by a real light instead of by the baked vertex colours the console shipped.
            Classic draws the cartridge model and so does Win98; these dials reach only
            the second art set.
         m3_on       the switch. Off puts every type back on its cartridge model, which
                     is the A/B any judgement about this art has to be made against.
         m3_sun      how strong the directional sun is on an authored hull. The DIRECTION
                     is not here: it is the chain's own azimuth and elevation, shared
                     with the ground's light and with the shadow this hull casts, so one
                     dial cannot put the highlight and the shadow on different bearings.
         m3_ambient  the sky-and-bounce fill. Without it a surface turned from the sun is
                     black, which is what no light at all looks like rather than shade.
         m3_normal   how far the surface normal map tilts the shading. 0 is the raw
                     geometry, which is the honest floor for judging whether the relief
                     in a picture is modelled or painted.
         m3_ao       how far the baked occlusion darkens the fill. It reaches the ambient
                     only; occluding the sun as well would double-count a crease that
                     the shadow map has already darkened.
         m3_roughmin the floor under the roughness map. A perfectly smooth texel is a
                     mirror with nothing to reflect, and on a scene with no environment
                     probe that reads as a black dot rather than as polish.
         m3_specamb  how much of the fill the specular lobe may take. It is what makes
                     metal read as metal at all: metal has no diffuse term, so with this
                     at zero every bare-steel texel is black until it catches the sun. */
    int   m3_on;
    /* m3inf_on  the SKINNED infantry models, where one is installed for a type. Its own
                 row because a soldier is a different judgement from a tank: it replaces
                 the 1995 sprite and every pose the sprite has that the model's clips do
                 not (prone, crawling, dying) goes back to that sprite. */
    int   m3inf_on;
    /* User-authored Minigunner pixel art in 3D; independent of experimental textures. */
    int   new_sprites;
    /* Palette tuning for the replacement sprites only; neutral preserves source bytes. */
    float ns_bright, ns_contrast, ns_exposure, ns_gamma, ns_sat;
    /* einf_on   THE REMASTERED SOLDIERS (einf/<TYPE>.c3m, tools/art/remaster/infantry):
                 Enhanced's own animated infantry, drawn with experimental art OFF, as the
                 remastered buildings are. The experimental set's soldiers keep m3inf_on.
                 Poses a model has no clip for go back to the 1995 sprite. */
    int   einf_on;
    /* einf_scale  THEIR SIZE, on the model's own height (the 1995 sprite's 11 pixels, 0.46
                 of a cell), in place of inf_scale, which sizes the flat cards. 1 is that
                 height; the default stands them as tall ON SCREEN as the DOS sprite beside
                 the same map: the tilted 3D camera shortens anything upright by about a
                 quarter, and the sprite was drawn full height. */
    float einf_scale;
    /* WHAT THEY LEAVE AND THROW (infdeath_mod.h), Enhanced only:
         einf_corpse_keep  bodies stay where they fell instead of sinking away
         einf_corpse_max   at most this many bodies on a map; the oldest goes for the next
         einf_corpse_dark  the share of his own colour a body keeps (0.5: half as bright)
         einf_ragdoll      the fall finished by a ragdoll vehicles and soldiers shove
         einf_blood        blood pooled under each body
         einf_flash        a flash at the model's own muzzle on each shot
         einf_casings      spent casings thrown from the ejection port
         einf_gibs         a soldier killed by an explosion, or run over by a tracked
                           vehicle, comes apart at the neck, shoulders, hips and waist,
                           and a tracked vehicle driving over a body tears it further
         einf_spray        blood sprays from a torn joint for 3 seconds and leaves drops on
                           the ground and on whatever vehicle or building it lands on */
    int   einf_corpse_keep;
    float einf_corpse_max;
    float einf_corpse_dark;
    int   einf_ragdoll, einf_blood, einf_flash, einf_casings;
    int   einf_gibs, einf_spray;
    /* m3inf_sun     THE SOLDIERS' OWN LIGHT, as shares of the authored models' sun and
       m3inf_fill    fill (m3_sun, m3_ambient), so those two dials still move a soldier
                     and these trim him against the vehicles. A soldier needs his own
                     share because the model light's exposure is set for a tank's
                     paint, a few per cent reflectance, and the soldier's cloth is four
                     times that and his team plates (neutralised to a light grey by the
                     exporter) over twenty: at a share of 1, measured close in, 12 per
                     cent of his pixels clip to white, which is detail no darker dial
                     can bring back. At the shipped shares it is 1 per cent.
       m3inf_paint   how bright the team plates are under the house colour. The exporter
                     neutralises them to a light grey so any house colour reads as
                     itself; this scales that grey back toward the cloth around it.
       m3inf_shadow  the soldiers cast from their OWN sun shadow map, with the sharpness
                     a man needs. The shared map is softened by the penumbra dial,
                     0.08 of a cell, which suits a cliff or a canopy and is wider than a
                     soldier's leg, so his shadow came out as a smudge; a separate map is
                     the only way to give one kind of caster a different edge. It is
                     also fitted to the view once rather than one and a half times with
                     an offset, measured 0.012 of a cell a texel against the shared
                     0.017 at the closest zoom. Off puts him back in the shared map. */
    float m3inf_sun, m3inf_fill, m3inf_paint;
    /* Shared by Enhanced and experimental 3D soldiers only; neutral preserves the art. */
    float inf_bright, inf_contrast, inf_exposure, inf_gamma, inf_sat;
    float inf_warmth, inf_tint, inf_red, inf_green, inf_blue;
    int   m3inf_shadow;
    /* m3inf_runrate  THE GAIT DIAL: a multiple of the speed the 3D soldiers' walk, trot
                      and run blend reads (m3inf_gait in cnc_eyes.cpp). At 1 the blend
                      sees how fast he really moves and his feet plant; above 1 he takes
                      a faster gait sooner and his legs move faster than the ground,
                      which skates a little. The ground he covers is the engine's either
                      way. */
    float m3inf_runrate;
    /* freeinf_on  FREE INFANTRY MOVEMENT, a simulation rule and the one row here that is
                  not a picture: soldiers walk straight legs across several cells of their
                  path, at any angle, instead of one cell at a time (the brain's
                  OpenCNC3D_FreeInfantry). Off in a network match whatever this says. */
    int   freeinf_on;
    /* uvfx_on      the UNITY EXPLOSIONS (unityvfx_mod.h): Unity particle prefabs replayed
                    from recordings and drawn with their own shaders, bloom and tonemap in
                    place of the cartridge's explosions, one per engine explosion type.
       uvfx_scale   game cells per Unity metre. The prefab's fireball is about six metres
                    across; a third of a cell a metre makes it two cells, the footprint of
                    the explosion it replaces.
       uvfx_sun     the smoke's sun, as a share of the pack's demo sun (white at 3.14).
       uvfx_scatter URP's bloom scatter, the blend between one bloom mip and the next.
                    0.5 is what Unity was MEASURED to use for this pack, not a taste. */
    int   uvfx_on;
    float uvfx_scale, uvfx_sun, uvfx_scatter;
    /* uvfx_gunsize  the Unity MUZZLE FLASHES' size on top of the rule's own (vfx3d/
                     muzzles.txt). A rifle flash at its real size is a few pixels at the
                     game's distance and on screen for one tick, so it is drawn larger. */
    float uvfx_gunsize;
    /* uvfx_gamesun  light the smoke from the game's sun instead of from where the pack's
                     demo sun stands relative to the camera. Off is the look it has in
                     Unity: backlit, dark smoke. On is lit from the front, pale smoke. */
    int   uvfx_gamesun;
    /* curgreen_on     THE GREEN POINTER: a new model for the plain pointer (cartridge
                       cursor 0x00), drawn instead of the white slab under experimental
                       art. Every other cursor shape is the cartridge's own.
       curgreen_scale  its size, on its own dial because it is a new model judged on its
                       own. 1 is the cartridge pointer's own footprint, which the new
                       model keeps exactly; 0.7 is the size the white pointer is drawn at
                       by default, so the swap starts at the size the eye is used to. */
    int   curgreen_on;
    float curgreen_scale;
    /* ecur_on     THE REMASTERED CURSORS (ecur/ecur.bin, tools/art/remaster/cursors):
                   every cartridge cursor in the remastered set's finish, with its glow,
                   drawn in Enhanced with experimental art off, animated by the cartridge's
                   own tracks, flipbooks and spin (cursor3d_mod.h, ecur_*).
       ecur_scale  their size against the cartridge cursor each stands in for, inside the
                   3D cursor size dial: 0.75 by request, a quarter smaller. */
    int   ecur_on;
    float ecur_scale;
    /* fol_*  THE SCATTERED BUSHES AND GRASS CLUMPS (foliage3d_mod.h), Enhanced only,
              temperate only, on plain grass cells only. Counts are per eligible
              cell, heights are in cells, and fol_seed re-rolls the whole layout. */
    int   fol_on, fol_shadows, fol_grass_shadows;
    float fol_bush_amt, fol_grass_amt, fol_bush_size, fol_grass_size, fol_vary;
    float fol_cluster, fol_cluster_len, fol_margin, fol_bush_mix;
    float fol_g1, fol_g2, fol_g3, fol_tilt, fol_wind, fol_flutter, fol_gain, fol_grass_gain;
    float fol_seed, fol_bush_detail;
    int   fol_crush;
    float fol_crush_r, fol_flat_time, fol_fade_time;
    /* rock_*  THE ROCKS (foliage3d_mod.h), Enhanced only, any theater: stones
               from rock3d.pack at the foot of rock faces, and a rigid body each once
               something moves it. */
    int   rock_on, rock_shadows, rock_physics;
    float rock_amt, rock_spread, rock_falloff, rock_size, rock_vary, rock_shrink;
    float rock_detail, rock_c1, rock_c2, rock_c3, rock_sink, rock_margin, rock_gain;
    float rock_seed, rock_push, rock_mass, rock_friction, rock_bounce, rock_gravity;
    float rock_pulse, rock_pulse_r;
    int   mottle_on;         /* ground variation patches on plain terrain          */
    float mottle_strength;   /* how far the patch is allowed to cover the ground   */
    float mottle_density;    /* share of eligible cells that carry one, 0..1       */
    int   m3_env;
    float m3_sun, m3_ambient, m3_normal, m3_ao, m3_roughmin, m3_specamb;
    /* 12h. HIGH DYNAMIC RANGE (ours, and the foundation the other three sit on).
            Every render target in this chain was eight bit fixed point, so nothing in
            the scene could be brighter than white: a sun glint on metal was clipped
            flat before the bloom, the grade or anything else could see it, and
            measured, switching bloom off changed a lit hull by nothing at all because
            there was nothing left above the threshold to bloom. These make the
            scene-referred targets sixteen bit float and put a tonemap at the end, which
            is the whole difference between a picture that can be over-exposed on
            purpose and one that was flattened before anybody chose.
         hdr_on    retired: the chain always draws in eight bits (fx_hdr_want in
                   fx_post.h answers no), and a file saved with it on reads back off.
         tonemap   0 clips at white, which is what the eight bit chain did. 1 bends
                   what used to clip into the headroom instead. It is NOT a film curve
                   and must not become one while the world pass is still gamma encoded;
                   the note above fx_rolloff in fx_post.h says why at length.
         tone_knee where the rolloff begins. BELOW THIS THE CURVE IS THE IDENTITY, to
                   the bit, which is the property that lets this ship without re-tuning
                   the grade and without moving the gates that classify by absolute
                   colour. Lower it to bend more of the picture, and know that it stops
                   being free at that point.
         bloom_clamp the ceiling on what may bloom, which eight bits used to provide for
                   free at 1.0. A microfacet highlight on an authored hull reaches the
                   tens, and one such texel through four blur passes is a disc across
                   the frame rather than a glint. */
    /* 12i. RELIGHTING THE CARTRIDGE'S OWN ART (ours). The console baked its lighting
            into each model's vertex colours, so a 1995 mesh cannot answer a sun dial:
            the chain could darken it or add to it and never change where its light was
            coming from. The world's meshes are drawn a second time with their textures
            unbound, which gives the baked shade alone; dividing it out of the finished
            colour gives the asset's own texture back, and that is lit properly.
            Terrain, sprites, water and everything composited over the world are not
            touched, which is what stops this erasing them.
         relight_on    the switch, and the A/B for every judgement about it.
         relight_rough how rough the cartridge's surfaces are taken to be. One number
                       for the whole art set, because none of it carries a map and the
                       project owner's answer was a constant.
         relight_sun   how much light the new sun delivers, in the same units as the
                       shade it replaces. Near 1 keeps the overall brightness and moves
                       only the DIRECTION the light comes from, which is the point.
         relight_amb   the fill a relit surface gets where the sun does not reach it.
         relight_spec  how much of a specular highlight a relit surface gets. Low:
                       nothing the cartridge painted was shiny and a strong lobe over a
                       1995 texture reads as a bug rather than as gloss.
         relight_clamp the ceiling on the ratio. The baked shade goes to almost nothing
                       in the cartridge's own deep shadow, and a ratio against almost
                       nothing is how film grain becomes a supernova.
         relight_floor the smallest baked shade that may be divided by. The cartridge's
                       own deep shadow goes to almost nothing, and dividing by almost
                       nothing turns grain into a supernova. */
    int   relight_on;
    float relight_rough, relight_sun, relight_amb, relight_floor;
    float relight_spec, relight_clamp;
    int   hdr_on;
    float tonemap, tone_knee, bloom_clamp;
    /* 12p. THE STRUCTURES ON THEIR OWN (ours, Enhanced only). Buildings and walls are
            tuned apart from the ground, the units and the effects, in the light pass,
            which knows which pixels are a structure's from the class the normal target
            carries (fx_post.h, the light pass's STRUCTURES block). Every dial is
            NEUTRAL AT 1: with all of them there the light pass takes exactly the path
            it took before they existed, and the cartridge structures are not drawn
            into the normal target at all (fx_struct_active).
         st_gamma     a power curve on the structure's own colour, before it is lit:
                      above 1 lifts the mid-tones, below 1 deepens them; black and
                      white stay where they are.
         st_bright    a gain on that colour.
         st_contrast  spread about middle grey, as the colour grade's contrast.
         st_sat       saturation about the colour's own luminance, as the grade's.
         st_shade     how dark a structure goes in the sun's shadow, on its sides turned
                      from the sun and under a cloud: scales how far the shadow tint
                      takes it, so 0 leaves it unshaded and 2 twice as deep.
         st_sun       how much the sun lights a structure: scales what the unshadowed
                      part of a surface has above its shadow colour. 0 leaves only the
                      shadow colour, 2 lifts the sunlit faces above it twice as far.
         st_glow      the remasters' self-lit surfaces (lamps, signals, molten pools,
                      lit glass): a gain on their colour, and so on their bloom.
         st_ao        the screen-space occlusion's darkening on a structure. */
    float st_gamma, st_bright, st_contrast, st_sat;
    float st_shade, st_sun, st_glow, st_ao;
    /* 12q. Vehicle-only colour and lighting, in the same units as STRUCTURES.
       Neutral at 1; a distinct object class excludes structures and soldiers. */
    float vh_gamma, vh_bright, vh_contrast, vh_sat;
    float vh_shade, vh_sun, vh_glow, vh_ao;
    /* debug  the DEBUG group's only row. When it is on and a debug pack was found beside
              the mission's, every type whose mesh differs between the two is drawn from
              the debug pack instead, so an updated model can be compared against the one
              it replaces without restarting. Off by default and it reaches nothing on the
              gate path, because no gate passes --gfx. */
    float debug;

    /* THE OTHER TWO MODES' SETTINGS, kept while those modes are not the one on screen.
       Every row is also a live field above, and the live fields are the truth for the
       mode in use: the F5 panel and every draw read them. This is where a mode's rows
       wait while another mode is in use, so that choosing it again gives back what was
       set for it, and so that the Advanced page can edit a mode without switching to it.
       Stored as floats whatever the live field is; FX_MODE_ROWS says which are booleans.
       The entry for the mode in use is brought up to date from the live fields before it
       is written out (fx_mode_stash_live), so the file never carries a stale copy.
       CLASSIC IS THE EXCEPTION: its entry is always the truth, because the fields it
       leaves alone (the lit world's own switches) still hold another mode's values
       underneath a chain that is switched off. */
    float mode_row[FX_MODE_COUNT][FXR_COUNT];
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

/* ---- The graphics modes' rows ------------------------------------------------------
   Which live field each FXR_* row is, whether it is an int boolean or a float, and the
   key the saved preset calls it (the field's own FX_PARAMS key, so a reader of the file
   meets one name per switch). */
struct FxModeRow {
    const char* key;
    size_t      off;
    int         is_bool;
};

static const FxModeRow FX_MODE_ROWS[FXR_COUNT] = {
    { "smooth_anim",         FXO(smooth_anim),         1 },
    { "new_hud",             FXO(new_hud),             1 },
    { "ui_scale",            FXO(ui_scale),            0 },
    { "perspective",         FXO(perspective),         0 },
    { "bilinear",            FXO(bilinear),            1 },
    { "water_fx",            FXO(water_fx),            1 },
    { "tree3d",              FXO(tree3d),              1 },
    { "tib3d",               FXO(tib3d),               1 },
    { "texset",              FXO(texset),              0 },
    { "infset",              FXO(infset),              0 },
    { "gamma_on",            FXO(gamma_on),            1 },
    { "ss_scale",            FXO(ss_scale),            0 },
    { "shadow_on",           FXO(shadow_on),           1 },
    { "ssao_on",             FXO(ssao_on),             1 },
    { "lights_on",           FXO(lights_on),           1 },
    { "bloom_on",            FXO(bloom_on),            1 },
    { "grade_on",            FXO(grade_on),            1 },
    { "crt_on",              FXO(crt_on),              1 },
    { "grass",               FXO(grass),               1 },
    { "fol_on",              FXO(fol_on),              1 },
    { "rock_on",             FXO(rock_on),             1 },
    { "m3inf_on",            FXO(m3inf_on),            1 },
    { "m3_on",               FXO(m3_on),               1 },
    { "uvfx_on",             FXO(uvfx_on),             1 },
    { "mottle_on",           FXO(mottle_on),           1 },
    { "experimental_relief", FXO(experimental_relief), 1 },
    { "curgreen_on",         FXO(curgreen_on),         1 },
    { "new_audio",           FXO(new_audio),           1 },
    { "einf_on",             FXO(einf_on),             1 },
    { "water_swash",         FXO(water_swash),         1 },
};

/* The names the saved preset writes the modes under: `mode.enhanced.bloom_on 1`. */
static const char* const FX_MODE_NAME[FX_MODE_COUNT] = { "classic", "n64", "enhanced" };

FX_STATE_MAYBE_UNUSED static int fx_mode_of(const FxState* s)
{
    if (s->dos_classic) return FX_MODE_CLASSIC;
    return s->enabled ? FX_MODE_ENHANCED : FX_MODE_N64;
}

FX_STATE_MAYBE_UNUSED static float fx_mode_live(const FxState* s, int r)
{
    const char* p = (const char*)s + FX_MODE_ROWS[r].off;
    return FX_MODE_ROWS[r].is_bool ? (float)*(const int*)p : *(const float*)p;
}

FX_STATE_MAYBE_UNUSED static void fx_mode_set_live(FxState* s, int r, float v)
{
    char* p = (char*)s + FX_MODE_ROWS[r].off;
    if (FX_MODE_ROWS[r].is_bool) *(int*)p = (v >= 0.5f) ? 1 : 0;
    else                         *(float*)p = v;
}

/* THE MODE IN USE, COPIED INTO ITS OWN STASH ENTRY from the live fields, which are its
   truth (the F5 panel writes them directly). Not for CLASSIC, whose entry is its truth. */
FX_STATE_MAYBE_UNUSED static void fx_mode_stash_live(FxState* s)
{
    const int m = fx_mode_of(s);
    int r;
    if (m == FX_MODE_CLASSIC || m == FX_MODE_N64) return;
    for (r = 0; r < FXR_COUNT; r++) s->mode_row[m][r] = fx_mode_live(s, r);
}

/* The shipped settings of each mode. Classic is the DOS 2D scene, N64 uses the
   cartridge-style terrain and infantry settings, and Enhanced retains the live defaults. */
FX_STATE_MAYBE_UNUSED static void fx_mode_defaults(FxState* s)
{
    int r;
    for (r = 0; r < FXR_COUNT; r++) {
        const float v = fx_mode_live(s, r);
        s->mode_row[FX_MODE_ENHANCED][r] = v;
        s->mode_row[FX_MODE_N64][r]     = 0.0f;
        s->mode_row[FX_MODE_CLASSIC][r]  = 0.0f;
    }
    s->mode_row[FX_MODE_CLASSIC][FXR_TEXSET] = (float)FX_TEX_DOS;
    s->mode_row[FX_MODE_CLASSIC][FXR_INFSET] = (float)FX_INF_DOS;
    s->mode_row[FX_MODE_CLASSIC][FXR_SS]     = 1.0f;              /* no supersampling */
    s->mode_row[FX_MODE_N64][FXR_UISCALE] = 0.0f;                 /* 1x: the largest */
    s->mode_row[FX_MODE_N64][FXR_PERSP]   = (float)FX_PERSP_CLASSIC;
    s->mode_row[FX_MODE_N64][FXR_TEXSET]  = (float)FX_TEX_N64;
    s->mode_row[FX_MODE_N64][FXR_INFSET]  = (float)FX_INF_DOS;
    s->mode_row[FX_MODE_N64][FXR_SS]      = 1.0f;                 /* no supersampling */
}

/* THE TERRAIN ART SWITCH, kept here beside the preference it answers to rather than in
   fx_filter.h, because unlike the filter it touches no GL state: draw_terrain simply asks
   which atlas to bind. FX_TEX_N64 is the shipped picture and it stays there until
   something calls the setter -- which is what keeps every bare cnc_eyes run and every
   pixel gate on the cartridge whatever the preference says. */
static int fx_texset_on = FX_TEX_N64;

FX_STATE_MAYBE_UNUSED static const char* fx_texset_name(int v)
{
    return v == FX_TEX_DOS ? "DOS" : v == FX_TEX_REMASTER ? "Remastered"
         : v == FX_TEX_ENH ? "Enhanced" : "cartridge";
}

FX_STATE_MAYBE_UNUSED static void fx_texset_set(int v)
{
    if (v < FX_TEX_N64 || v > FX_TEX_ENH) v = FX_TEX_N64;
    if (v == fx_texset_on) return;
    fx_texset_on = v;
    fprintf(stderr, "FX|texset|%s terrain art\n", fx_texset_name(v));
}

/* THE EXPERIMENTAL ART CHOOSES ITS OWN BASE, and that is the whole of this function's
   second line.

   The generated temperate cells replace 173 of that theater's 758, and they are graded
   against the Remastered art. The other 585 therefore have to BE that art, or the new
   cells sit in a picture nothing painted them for. Before this, the switch simply did
   nothing whenever the selection happened to be something else, with no word anywhere
   on screen: it moved, the ground did not, and the only explanation was a line in a log
   file. A switch that needs a second switch set correctly before it does anything is a
   switch that reads as broken.

   THE SELECTION IS NOT TOUCHED. fx_texset_on and the panel row keep whatever was
   chosen, so turning the experiment off puts the ground back exactly where it was. And
   asking for an art set the machine cannot supply is already safe: terrain_texset_drawn
   falls back to the DOS art and then to the cartridge, and the Remastered loader says
   out loud that it found no installed Collection. */
FX_STATE_MAYBE_UNUSED static int fx_texset_get(void)
{
    if (g_fx.experimental_textures) return FX_TEX_REMASTER;
    return fx_texset_on;
}

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
{ FXP_BOOL,  "show_stats", "resolution and FPS counter", FXO(show_stats), 0,1,0,
  "show the resolution the game is drawing at and the frame rate it is getting, at the top of the screen: frames a second, the average frame time and the slowest frame of the last half second" },
{ FXP_STEP,  "vsync", "vertical sync", FXO(vsync), 0.0f, 2.0f, 1.0f,
  "1 on: each frame waits for the screen's next refresh, so there is no tearing, but a frame that misses one refresh waits for the next whole one: on a 60 Hz screen anything slower than 16.7 ms shows as 30 FPS, so 18 ms and 33 ms read the same. 0 off: frames go out as soon as they are drawn and the counter shows the real rate; the picture can tear. 2 adaptive: on while fast enough, off when a frame is late, where the driver offers it" },
{ FXP_BOOL,  "bilinear", "bilinear filtering", FXO(bilinear), 0,1,0,
  "3D art AND the sidebar; independent of the chain" },
{ FXP_BOOL,  "smooth_anim", "smooth animations", FXO(smooth_anim), 0,1,0,
  "sub-tick interpolation of movement and animation; off = the original stepping" },
{ FXP_BOOL,  "new_hud", "new HUD", FXO(new_hud), 0,1,0,
  "the 640x480 sidebar; off = the 1995 DOS bar" },
{ FXP_STEP,  "texset", "terrain art", FXO(texset), 0,3,1,
  "0 = the cartridge's 16-colour bank, 1 = the 1995 DOS art, 2 = Remastered, 3 = Enhanced" },
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
  "the console's cursor meshes, Enhanced only; 1 = the console's size, 0.4 as tuned" },
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
  "off = the native N64 presentation" },
{ FXP_BOOL,  "dos_classic", "DOS Classic", FXO(dos_classic), 0,1,0,
  "320x200 top-down 2D sprite presentation" },

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
  "Real sun shadow maps. Unified daylight always suppresses cartridge shadow plates, including when this is off" },
{ FXP_BOOL, "unified_lighting", "unified daylight", FXO(unified_lighting), 0,1,0,
  "One sun and sky fill: shadows remove direct sunlight, while ambient fill remains; Enhanced only" },
{ FXP_FLOAT, "sun_energy", "sun energy", FXO(sun_energy), 0.0f, 3.0f, 0.01f,
  "Direct sunlight for terrain, models and foliage; 1 is neutral; needs unified daylight" },
{ FXP_FLOAT, "sky_energy", "sky fill", FXO(sky_energy), 0.0f, 3.0f, 0.01f,
  "Ambient sky and ground bounce for terrain, models and foliage; unaffected by cast sun shadows" },
{ FXP_FLOAT, "shadow_black_limit", "shadow detail floor", FXO(shadow_black_limit), 0.0f, 0.15f, 0.001f,
  "Protect low lit-surface detail from the colour grade's black clipping. 0 restores hard clipping; sky fill scales the toe. Does not light unexplored fog or a zero-energy scene" },
{ FXP_FLOAT, "object_shadow_fill", "object shadow fill", FXO(object_shadow_fill), 0.0f, 1.0f, 0.01f,
  "Indirect fill for shadows on structures and trees; higher values lift their dark self-shadowing without changing shadows cast onto terrain" },
{ FXP_FLOAT, "terrain_ambient", "terrain sky fill", FXO(terrain_ambient), 0.0f, 1.5f, 0.01f,
  "Terrain ambient fill before sun and visibility; keeps unlit slopes readable without painting a shadow" },
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
  "Legacy slope-lighting switch. Unified daylight always uses the common sun and sky; smooth ground chooses interpolated normals" },
{ FXP_FLOAT, "sun_floor", "shade floor", FXO(sun_floor), 0.0f,1.0f,0.01f,
  "Legacy daylight only: how dark ground turned away from the sun may go. Unified daylight uses terrain sky fill.\n"
  "more subtle heightmap shade; 0.25 was the old hardcoded value and reads as black." },
{ FXP_BOOL,  "contact_on", "contact shadows", FXO(contact_on), 0,1,0,
  "a short march toward the sun through the depth buffer, which draws the darkness directly under a hull that the shadow map is too coarse and too biased to reach. Inside sun shadows: turning those off turns this off too" },
{ FXP_FLOAT, "contact_len", "reach (cells)", FXO(contact_len), 0.05f, 2.0f, 0,
  "how far the march looks along the sun ray. About the clearance under a tank; much further and it stops being a contact shadow and becomes a second, worse occlusion pass" },
{ FXP_STEP,  "contact_steps", "samples", FXO(contact_steps), 2.0f, 16.0f, 1.0f,
  "depth fetches along that reach. Fewer bands the shadow, more costs fill" },
{ FXP_FLOAT, "contact_thick", "blocker depth", FXO(contact_thick), 0.05f, 3.0f, 0,
  "how far behind the recorded surface a sample may be and still count as blocked. A depth buffer holds a surface and not a solid, so with no ceiling here the horizon shadows everything in front of it" },
{ FXP_FLOAT, "contact_bias", "start offset", FXO(contact_bias), 0.0f, 0.2f, 0,
  "how far along its own normal the march begins, which is what stops a surface shadowing itself" },
{ FXP_FLOAT, "contact_strength", "strength", FXO(contact_strength), 0.0f, 1.0f, 0,
  "how dark a found blocker makes the pixel. Folded in with the shadow map by taking the darker of the two, so it can never double-darken what that already found" },
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

{ FXP_GROUP, "", "TERRAIN", 0,0,0,0, "" },
{ FXP_BOOL, "terrain_relief", "geometric rock relief", FXO(terrain_relief), 0,1,0,
  "Saved physical rock footprints add real terrain geometry to colour, normals, depth, reflections and sun shadows; unified daylight only" },
{ FXP_FLOAT, "terrain_relief_height", "rock relief height", FXO(terrain_relief_height), 0.0f, 0.6f, 0.01f,
  "Maximum extra rock height in world cells; native map elevations and building pads remain authoritative" },
{ FXP_FLOAT, "clear_saturation", "grass texture saturation", FXO(clear_saturation), 0.0f, 1.5f, 0.01f,
  "colour strength of the three temperate grass textures; 1 keeps the source colour" },
{ FXP_FLOAT, "terrain_shadow_strength", "terrain shadow strength", FXO(terrain_shadow_strength), 0.0f, 1.0f, 0,
  "Terrain cast-shadow opacity; 0 clears terrain shadows, 1 is fully opaque. Legacy daylight also scales shaded slopes and painted shade. Object shadows keep their own setting" },
{ FXP_FLOAT, "terrain_brightness", "terrain brightness", FXO(terrain_brightness), 0.0f, 2.0f, 0,
  "ground colour before lighting; 1 is unchanged; buildings, units, water and the HUD keep their own brightness" },
{ FXP_FLOAT, "terrain_contrast", "terrain contrast", FXO(terrain_contrast), 0.0f, 2.0f, 0,
  "ground colour contrast around middle grey; 1 is unchanged; hidden terrain remains hidden" },

{ FXP_GROUP, "", "GRASS TERRAIN", 0,0,0,0, "" },

{ FXP_FLOAT, "grass_terrain_warmth", "warm / cool", FXO(grass_terrain_warmth), -1.0f, 1.0f, 0.01f,
  "Warmth: negative cools, positive warms; 0 is neutral; Temperate ground and cliffs only. Units, trees, water and HUD retain their own colour" },
{ FXP_FLOAT, "grass_terrain_tint", "green / magenta", FXO(grass_terrain_tint), -1.0f, 1.0f, 0.01f,
  "Tint: positive green, negative magenta; 0 is neutral; Temperate ground and cliffs only. Units, trees, water and HUD retain their own colour" },
{ FXP_FLOAT, "grass_terrain_red", "red balance", FXO(grass_terrain_red), 0.0f, 2.0f, 0.01f,
  "Red reflectance multiplier; 1 is neutral; Temperate ground and cliffs only. Units, trees, water and HUD retain their own colour" },
{ FXP_FLOAT, "grass_terrain_green", "green balance", FXO(grass_terrain_green), 0.0f, 2.0f, 0.01f,
  "Green reflectance multiplier; 1 is neutral; Temperate ground and cliffs only. Units, trees, water and HUD retain their own colour" },
{ FXP_FLOAT, "grass_terrain_blue", "blue balance", FXO(grass_terrain_blue), 0.0f, 2.0f, 0.01f,
  "Blue reflectance multiplier; 1 is neutral; Temperate ground and cliffs only. Units, trees, water and HUD retain their own colour" },

{ FXP_GROUP, "", "DESERT TERRAIN", 0,0,0,0, "" },
{ FXP_FLOAT, "desert_shadow_strength", "desert shadow strength", FXO(desert_shadow_strength), 0.0f, 1.0f, 0,
  "Desert terrain cast-shadow opacity; independent of Sand, Snow, other terrain and object shadows" },
{ FXP_FLOAT, "sand_brightness", "desert terrain brightness", FXO(sand_brightness), 0.0f, 2.0f, 0.01f,
  "Enhanced Desert ground brightness, relative to Terrain brightness; buildings, units and water keep their own settings" },
{ FXP_FLOAT, "sand_contrast", "desert terrain contrast", FXO(sand_contrast), 0.0f, 2.0f, 0.01f,
  "Enhanced Desert ground contrast; 1 is neutral; only Desert maps respond" },
{ FXP_FLOAT, "sand_saturation", "desert sand saturation", FXO(sand_saturation), 0.0f, 1.5f, 0.01f,
  "Colour intensity of the Enhanced Desert CLEAR materials; independent of Temperate grass." },


{ FXP_FLOAT, "desert_terrain_warmth", "warm / cool", FXO(desert_terrain_warmth), -1.0f, 1.0f, 0.01f,
  "Warmth: negative cools, positive warms; 0 is neutral; Desert ground and cliffs only. Units, trees, water and HUD retain their own colour" },
{ FXP_FLOAT, "desert_terrain_tint", "green / magenta", FXO(desert_terrain_tint), -1.0f, 1.0f, 0.01f,
  "Tint: positive green, negative magenta; 0 is neutral; Desert ground and cliffs only. Units, trees, water and HUD retain their own colour" },
{ FXP_FLOAT, "desert_terrain_red", "red balance", FXO(desert_terrain_red), 0.0f, 2.0f, 0.01f,
  "Red reflectance multiplier; 1 is neutral; Desert ground and cliffs only. Units, trees, water and HUD retain their own colour" },
{ FXP_FLOAT, "desert_terrain_green", "green balance", FXO(desert_terrain_green), 0.0f, 2.0f, 0.01f,
  "Green reflectance multiplier; 1 is neutral; Desert ground and cliffs only. Units, trees, water and HUD retain their own colour" },
{ FXP_FLOAT, "desert_terrain_blue", "blue balance", FXO(desert_terrain_blue), 0.0f, 2.0f, 0.01f,
  "Blue reflectance multiplier; 1 is neutral; Desert ground and cliffs only. Units, trees, water and HUD retain their own colour" },

{ FXP_GROUP, "", "SAND TERRAIN", 0,0,0,0, "" },
{ FXP_FLOAT, "sand_shadow_strength", "sand shadow strength", FXO(sand_shadow_strength), 0.0f, 1.0f, 0,
  "SAND theater hills and cliffs only; independent of Desert, Snow, other terrain and object shadows" },


{ FXP_FLOAT, "sand_terrain_warmth", "warm / cool", FXO(sand_terrain_warmth), -1.0f, 1.0f, 0.01f,
  "Warmth: negative cools, positive warms; 0 is neutral; SAND ground and cliffs only. Units, trees, water and HUD retain their own colour" },
{ FXP_FLOAT, "sand_terrain_tint", "green / magenta", FXO(sand_terrain_tint), -1.0f, 1.0f, 0.01f,
  "Tint: positive green, negative magenta; 0 is neutral; SAND ground and cliffs only. Units, trees, water and HUD retain their own colour" },
{ FXP_FLOAT, "sand_terrain_red", "red balance", FXO(sand_terrain_red), 0.0f, 2.0f, 0.01f,
  "Red reflectance multiplier; 1 is neutral; SAND ground and cliffs only. Units, trees, water and HUD retain their own colour" },
{ FXP_FLOAT, "sand_terrain_green", "green balance", FXO(sand_terrain_green), 0.0f, 2.0f, 0.01f,
  "Green reflectance multiplier; 1 is neutral; SAND ground and cliffs only. Units, trees, water and HUD retain their own colour" },
{ FXP_FLOAT, "sand_terrain_blue", "blue balance", FXO(sand_terrain_blue), 0.0f, 2.0f, 0.01f,
  "Blue reflectance multiplier; 1 is neutral; SAND ground and cliffs only. Units, trees, water and HUD retain their own colour" },

{ FXP_GROUP, "", "SNOW TERRAIN", 0,0,0,0, "" },
{ FXP_FLOAT, "snow_shadow_strength", "snow shadow strength", FXO(snow_shadow_strength), 0.0f, 1.0f, 0,
  "Snow terrain cast-shadow opacity in both games; independent of Desert, Sand and other terrain" },
{ FXP_FLOAT, "snow_brightness", "snow terrain brightness", FXO(snow_brightness), 0.0f, 2.0f, 0.01f,
  "Enhanced Snow ground brightness, relative to Terrain brightness; 1 preserves your current look; objects and water keep their own settings" },
{ FXP_FLOAT, "snow_contrast", "snow terrain contrast", FXO(snow_contrast), 0.0f, 2.0f, 0.01f,
  "Enhanced Snow ground contrast; 1 is neutral; only Snow maps respond" },
{ FXP_FLOAT, "snow_saturation", "snow terrain saturation", FXO(snow_saturation), 0.0f, 1.5f, 0.01f,
  "Enhanced Snow ground colour strength; 0 is greyscale, 1 preserves the original colour" },


{ FXP_FLOAT, "snow_terrain_warmth", "warm / cool", FXO(snow_terrain_warmth), -1.0f, 1.0f, 0.01f,
  "Warmth: negative cools, positive warms; 0 is neutral; Snow/Winter ground and cliffs only. Units, trees, water and HUD retain their own colour" },
{ FXP_FLOAT, "snow_terrain_tint", "green / magenta", FXO(snow_terrain_tint), -1.0f, 1.0f, 0.01f,
  "Tint: positive green, negative magenta; 0 is neutral; Snow/Winter ground and cliffs only. Units, trees, water and HUD retain their own colour" },
{ FXP_FLOAT, "snow_terrain_red", "red balance", FXO(snow_terrain_red), 0.0f, 2.0f, 0.01f,
  "Red reflectance multiplier; 1 is neutral; Snow/Winter ground and cliffs only. Units, trees, water and HUD retain their own colour" },
{ FXP_FLOAT, "snow_terrain_green", "green balance", FXO(snow_terrain_green), 0.0f, 2.0f, 0.01f,
  "Green reflectance multiplier; 1 is neutral; Snow/Winter ground and cliffs only. Units, trees, water and HUD retain their own colour" },
{ FXP_FLOAT, "snow_terrain_blue", "blue balance", FXO(snow_terrain_blue), 0.0f, 2.0f, 0.01f,
  "Blue reflectance multiplier; 1 is neutral; Snow/Winter ground and cliffs only. Units, trees, water and HUD retain their own colour" },

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

{ FXP_GROUP, "", "TEMPERATE MIST / FOG", 0,0,0,0, "" },
{ FXP_BOOL, "fog_fx", "moving mist / fog", FXO(fog_fx), 0,1,0, "cool mist in temperate and snow theatres; Enhanced only" },
{ FXP_FLOAT, "fog_density", "density", FXO(fog_density), 0.0f, 1.0f, 0, "amount of air scattering; zero clears the air" },
{ FXP_FLOAT, "fog_height", "height (cells)", FXO(fog_height), 0.1f, 6.0f, 0, "density falls with height; hills and tall roofs rise above it" },
{ FXP_FLOAT, "fog_scale", "wisp size (cells)", FXO(fog_scale), 2.0f, 80.0f, 0, "world size of the drifting three-dimensional noise" },
{ FXP_FLOAT, "fog_speed", "speed (cells/sec)", FXO(fog_speed), 0.0f, 8.0f, 0, "drift follows simulation time; zero parks the volume" },
{ FXP_FLOAT, "fog_wind", "wind bearing", FXO(fog_wind), 0.0f, 360.0f, 0, "degrees clockwise from north" },
{ FXP_FLOAT, "fog_light", "local light scattering", FXO(fog_light), 0.0f, 8.0f, 0, "fire, explosion, muzzle and tiberium light scattered inside the air" },

{ FXP_GROUP, "", "DESERT SAND DUST", 0,0,0,0, "" },
{ FXP_BOOL, "sand_fx", "moving sand dust", FXO(sand_fx), 0,1,0, "warm blowing dust in desert and sand theatres; Enhanced only" },
{ FXP_FLOAT, "sand_density", "density", FXO(sand_density), 0.0f, 1.0f, 0, "amount of air scattering; zero clears the air" },
{ FXP_FLOAT, "sand_height", "height (cells)", FXO(sand_height), 0.1f, 6.0f, 0, "density falls with height; hills and tall roofs rise above it" },
{ FXP_FLOAT, "sand_scale", "wisp size (cells)", FXO(sand_scale), 2.0f, 80.0f, 0, "world size of the drifting three-dimensional noise" },
{ FXP_FLOAT, "sand_speed", "speed (cells/sec)", FXO(sand_speed), 0.0f, 8.0f, 0, "drift follows simulation time; zero parks the volume" },
{ FXP_FLOAT, "sand_wind", "wind bearing", FXO(sand_wind), 0.0f, 360.0f, 0, "degrees clockwise from north" },
{ FXP_FLOAT, "sand_light", "local light scattering", FXO(sand_light), 0.0f, 8.0f, 0, "fire, explosion, muzzle and tiberium light scattered inside the air" },

{ FXP_GROUP, "",  "3c  WATER  (ours)", 0,0,0,0, "" },
{ FXP_BOOL,  "water_fx", "Water Shader", FXO(water_fx), 0,1,0,
  "the sea knows where its coast is: a soft shore, shallows, a flow, rivers that run; off is the cartridge's two-tile wash. On by default under Enhanced, never in Classic" },
{ FXP_BOOL,  "water_swash", "shoreline swash", FXO(water_swash), 0,1,0,
  "ocean waves wash up the beach with textured foam, then retreat; rivers are excluded; on by default in Enhanced, independent of the legacy foam amount" },
{ FXP_FLOAT, "water_swash_hold", "swash hold (sec)", FXO(water_swash_hold), 0.0f, 10.0f, 0,
  "seconds fully up the beach before retreat; zero reverses immediately, with no hidden easing pause; the shoreward current stays synchronized" },
{ FXP_FLOAT, "water_swash_opacity", "swash opacity", FXO(water_swash_opacity), 0.0f, 1.0f, 0,
  "opacity of the animated beach water film and textured foam together; zero hides swash without changing the ocean or its flow" },
{ FXP_FLOAT, "water_align_length", "shore reach (cells)", FXO(water_align_length), 0.0f, 8.0f, 0,
  "ocean waves fully face the shore within this distance; shore blend adds the transition back to the open-sea current" },
{ FXP_FLOAT, "water_align_blend", "shore blend (cells)", FXO(water_align_blend), 0.05f, 8.0f, 0,
  "width beyond shore reach over which aligned waves smoothly blend into the open ocean; independent of beach foam width" },
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
{ FXP_FLOAT, "water_spec", "sun glint", FXO(water_spec), 0.0f, 3.0f, 0,
  "the sun's sparkle on the ripples" },
{ FXP_FLOAT, "water_reflection_yaw", "Reflection rotation", FXO(water_reflection_yaw), -180.0f, 180.0f, 0,
  "degrees clockwise: rotates water reflection highlights independently of the sun; 0 follows the sun; terrain, shore and underwater lighting stay unchanged" },
{ FXP_FLOAT, "water_refract", "refraction", FXO(water_refract), 0.0f, 0.06f, 0,
  "how far the ripples bend the sea floor seen through the water, as a fraction of the screen" },
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

{ FXP_GROUP, "",  "3e  DISTANCE FOG  (ours)", 0,0,0,0, "" },
{ FXP_BOOL,  "dfog_on", "Distance fog", FXO(dfog_on), 0,1,0,
  "the far part of the view, the top of the screen, fades into a pale haze while the middle keeps its full colour. Enhanced only; Temperate and Desert each have their own dials below, other theaters draw none" },
{ FXP_FLOAT, "dfog_t_amount", "temperate: thickness", FXO(dfog_t_amount), 0.0f, 1.0f, 0,
  "how far the farthest ground fades toward the haze colour; 1 hides it completely" },
{ FXP_FLOAT, "dfog_t_start", "temperate: starts (cells)", FXO(dfog_t_start), -12.0f, 20.0f, 0,
  "how many cells beyond the middle of the screen the haze begins; below 0 it starts nearer than the middle" },
{ FXP_FLOAT, "dfog_t_depth", "temperate: thickens over (cells)", FXO(dfog_t_depth), 1.0f, 40.0f, 0,
  "how many cells past its start the haze takes to reach its full thickness; short is a band, long a gentle gradient" },
{ FXP_FLOAT, "dfog_t_r", "temperate: colour red", FXO(dfog_t_r), 0.0f, 1.0f, 0, "" },
{ FXP_FLOAT, "dfog_t_g", "temperate: colour green", FXO(dfog_t_g), 0.0f, 1.0f, 0, "" },
{ FXP_FLOAT, "dfog_t_b", "temperate: colour blue", FXO(dfog_t_b), 0.0f, 1.0f, 0, "" },
{ FXP_FLOAT, "dfog_d_amount", "desert: thickness", FXO(dfog_d_amount), 0.0f, 1.0f, 0,
  "how far the farthest ground fades toward the haze colour; 1 hides it completely" },
{ FXP_FLOAT, "dfog_d_start", "desert: starts (cells)", FXO(dfog_d_start), -12.0f, 20.0f, 0,
  "how many cells beyond the middle of the screen the haze begins; below 0 it starts nearer than the middle" },
{ FXP_FLOAT, "dfog_d_depth", "desert: thickens over (cells)", FXO(dfog_d_depth), 1.0f, 40.0f, 0,
  "how many cells past its start the haze takes to reach its full thickness; short is a band, long a gentle gradient" },
{ FXP_FLOAT, "dfog_d_r", "desert: colour red", FXO(dfog_d_r), 0.0f, 1.0f, 0, "" },
{ FXP_FLOAT, "dfog_d_g", "desert: colour green", FXO(dfog_d_g), 0.0f, 1.0f, 0, "" },
{ FXP_FLOAT, "dfog_d_b", "desert: colour blue", FXO(dfog_d_b), 0.0f, 1.0f, 0, "" },

{ FXP_GROUP, "",  "4  BLOOM", 0,0,0,0, "" },
{ FXP_FLOAT, "muzzle_bloom", "muzzle flash bloom", FXO(muzzle_bloom), 0.0f, 1.0f, 0,
  "halo from cannon, machine gun and missile muzzle particles only" },
{ FXP_FLOAT, "muzzle_flash_gain", "muzzle flash brightness", FXO(muzzle_flash_gain), 0.0f, 1.0f, 0,
  "brightness of muzzle particles; timing, shape, smoke and other particles stay unchanged" },
{ FXP_FLOAT, "muzzle_light_gain", "muzzle light spill", FXO(muzzle_light_gain), 0.0f, 1.0f, 0,
  "light cast by cannon, machine gun and missile muzzle flashes; scene lights retain their settings" },
{ FXP_FLOAT, "explosion_bloom", "explosion particle bloom", FXO(explosion_bloom), 0.0f, 1.0f, 0,
  "bloom from luminous explosion particles only; scene highlights, lamps and muzzle flashes retain their bloom" },
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
{ FXP_FLOAT, "ssao_object", "on authored hulls", FXO(ssao_object), 0.0f, 2.0f, 0,
  "how far the occlusion darkens a vehicle drawn from an authored model, which is the first thing in the scene that carries a real per-pixel normal without being ground" },
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
{ FXP_BOOL,  "light_tiberium", "tiberium glow", FXO(light_tiberium), 0,1,0,
  "a tiberium field lights the ground, units and buildings in and around it, in the crystals' colour" },
{ FXP_FLOAT, "tib_light", "tiberium strength", FXO(tib_light), 0.0f, 3.0f, 0, "" },
{ FXP_FLOAT, "tib_light_spread", "tiberium reach (cells)", FXO(tib_light_spread), 0.25f, 6.0f, 0,
  "how far past the field's edge its light reaches" },
{ FXP_FLOAT, "tib_light_height", "tiberium climbs (cells)", FXO(tib_light_height), 0.1f, 3.0f, 0,
  "how far up a unit or a wall the glow climbs before it fades" },
{ FXP_BOOL,  "vlight_on", "vehicle lamps", FXO(vlight_on), 0,1,0,
  "the remastered vehicles' headlights, tail lights and beacons light what stands in front of them" },
{ FXP_FLOAT, "vlight_head", "headlights", FXO(vlight_head), 0.0f, 3.0f, 0,
  "strength of the headlights and searchlights" },
{ FXP_FLOAT, "vlight_tail", "tail and marker lamps", FXO(vlight_tail), 0.0f, 3.0f, 0,
  "strength of every other lamp: tail, reversing, marker, navigation, beacon" },
{ FXP_FLOAT, "vlight_cone", "lamp cone", FXO(vlight_cone), 0.25f, 2.0f, 0,
  "scales every lamp's cone" },
{ FXP_FLOAT, "vlight_range", "lamp reach", FXO(vlight_range), 0.25f, 3.0f, 0,
  "scales how far every lamp reaches" },
{ FXP_BOOL,  "vlight_shadows", "headlight shadows", FXO(vlight_shadows), 0,1,0,
  "a headlight is stopped by what stands between it and a surface" },
{ FXP_FLOAT, "vlight_glow", "lens glow", FXO(vlight_glow), 0.0f, 3.0f, 0,
  "how brightly a lamp's lens glows" },
{ FXP_BOOL,  "vspec_on", "vehicle specular maps", FXO(vspec_on), 0,1,0,
  "a remastered vehicle with a specular map shines where its steel, glass and paint would" },
{ FXP_FLOAT, "vspec_sun", "sun highlight", FXO(vspec_sun), 0.0f, 4.0f, 0,
  "strength of the sun's highlight on a vehicle's specular map" },
{ FXP_FLOAT, "vspec_sky", "sky sheen", FXO(vspec_sky), 0.0f, 4.0f, 0,
  "strength of the sky's sheen at grazing angles" },
{ FXP_FLOAT, "vspec_gloss", "highlight tightness", FXO(vspec_gloss), 0.25f, 4.0f, 0,
  "above 1 every highlight is smaller and sharper, below 1 broader" },

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
{ FXP_FLOAT, "tib_hue", "crystal colour (hue)", FXO(tib_hue), 0.0f, 360.0f, 0,
  "degrees on the colour wheel: 0 red, 60 yellow, 120 green, 180 cyan, 240 blue, 300 magenta. 105 is the art's own green. The glow, its light and its fog follow" },
{ FXP_FLOAT, "tib_sat", "crystal saturation", FXO(tib_sat), 0.0f, 2.0f, 0,
  "0 grey crystals, 1 the art's own, 2 twice as vivid" },
/* THE GLOWING FOG over a field. Ours, Enhanced only, Tier 2 only. */
{ FXP_GROUP, "",  "12o  TIBERIUM FOG  (ours)", 0,0,0,0, "" },
{ FXP_BOOL,  "tibfog_on", "tiberium fog", FXO(tibfog_on), 0,1,0,
  "a low glowing fog hangs over every tiberium field" },
{ FXP_FLOAT, "tibfog_density", "thickness", FXO(tibfog_density), 0.0f, 2.0f, 0,
  "how thick it is at the ground in a field's middle" },
{ FXP_FLOAT, "tibfog_height", "height (cells)", FXO(tibfog_height), 0.1f, 3.0f, 0,
  "how far up it thins to a third" },
{ FXP_FLOAT, "tibfog_spread", "reach (cells)", FXO(tibfog_spread), 0.25f, 6.0f, 0,
  "how far past the field's edge it drifts" },
{ FXP_FLOAT, "tibfog_glow", "lit by the glow", FXO(tibfog_glow), 0.0f, 3.0f, 0,
  "how hard the crystals' glow lights the fog, in their colour" },
{ FXP_FLOAT, "tibfog_ambient", "own brightness", FXO(tibfog_ambient), 0.0f, 1.0f, 0,
  "how bright the fog is unlit, as a grey haze; 0 = only the glow lights it" },
{ FXP_FLOAT, "tibfog_wisps", "wisps", FXO(tibfog_wisps), 0.0f, 1.0f, 0,
  "0 an even layer, 1 broken into drifting wisps" },
{ FXP_FLOAT, "tibfog_drift", "drift speed", FXO(tibfog_drift), 0.0f, 2.0f, 0,
  "how fast the wisps drift; off the engine clock, so two screenshot runs agree" },
{ FXP_STEP,  "tibfog_steps", "quality (samples)", FXO(tibfog_steps), 4.0f, 32.0f, 2.0f,
  "samples along each view ray; fewer is faster and grainier" },
/* REMASTERED EFFECTS. New art on the cartridge's own particles, plus a few particles the
   cartridge never had. */
{ FXP_GROUP, "",  "12d  EFFECTS  (remastered art)", 0,0,0,0, "" },
{ FXP_BOOL,  "efx_enh", "remastered effects", FXO(efx_enh), 0,1,0,
  "explosions, fire and smoke in new 1995-style art, with muzzle flashes and a landing-zone flare; off = the cartridge's art, which is what Classic draws" },
{ FXP_FLOAT, "efx_bright", "effect brightness", FXO(efx_bright), 0.25f, 2.0f, 0,
  "brightness of Enhanced explosions, fire, smoke, sparks, muzzle flashes and effect meshes; 1 = as painted" },
{ FXP_FLOAT, "efx_contrast", "effect contrast", FXO(efx_contrast), 0.25f, 3.0f, 0,
  "contrast of the particle art; black and white stay put, above 1 the mid tones spread apart, below 1 they draw together" },
{ FXP_FLOAT, "efx_sat", "effect saturation", FXO(efx_sat), 0.0f, 2.0f, 0,
  "colour of the particle art; 0 = grey, 1 = as painted, 2 = twice as vivid" },
{ FXP_FLOAT, "efx_exposure", "effect exposure (EV)", FXO(efx_exposure), -3.00f, 3.00f, 0.01f,
  "Effect light in stops: +1 doubles light, -1 halves it; scene exposure is independent" },
{ FXP_FLOAT, "efx_gamma", "effect gamma", FXO(efx_gamma), 0.40f, 2.50f, 0.01f,
  "Effect midtones; 1 preserves the authored picture" },
{ FXP_FLOAT, "efx_warmth", "effect warm / cool", FXO(efx_warmth), -1.00f, 1.00f, 0.01f,
  "Warm or cool the effects only; 0 is neutral" },
{ FXP_FLOAT, "efx_tint", "effect green / magenta", FXO(efx_tint), -1.00f, 1.00f, 0.01f,
  "Effect green versus magenta balance; 0 is neutral" },
{ FXP_FLOAT, "efx_red", "effect red balance", FXO(efx_red), 0.00f, 2.00f, 0.01f,
  "Red in effects only; 1 is neutral" },
{ FXP_FLOAT, "efx_green", "effect green balance", FXO(efx_green), 0.00f, 2.00f, 0.01f,
  "Green in effects only; 1 is neutral" },
{ FXP_FLOAT, "efx_blue", "effect blue balance", FXO(efx_blue), 0.00f, 2.00f, 0.01f,
  "Blue in effects only; 1 is neutral" },
{ FXP_BOOL,  "bib_enh", "remastered building aprons", FXO(bib_enh), 0,1,0,
  "the dirt in front of a building in the 1995 PC art redrawn at 1.5x the detail, soft-edged and multiplied into the ground; off = the cartridge's aprons, which is what Classic draws" },
{ FXP_FLOAT, "bib_enh_strength", "apron strength", FXO(bib_enh_strength), 0.0f, 1.0f, 0,
  "how strongly the remastered apron marks the ground; 0 = none, 1 = as painted" },
{ FXP_FLOAT, "bib_enh_cover", "apron cover", FXO(bib_enh_cover), 0.0f, 1.0f, 0,
  "how much painted dirt is laid over the ground before the multiply; 0 = a pure multiply, the ground's own colours showing through, 1 = the painted dirt" },
{ FXP_BOOL,  "decal_enh", "remastered craters and scorch marks", FXO(decal_enh), 0,1,0,
  "the 1995 PC craters and scorch marks painted again at the ground's resolution; off = the cartridge's, which is what N64 draws" },
/* REMASTERED CURSORS. Every cartridge cursor in the remastered set's look. */
{ FXP_GROUP, "",  "12k  CURSORS  (remastered art)", 0,0,0,0, "" },
{ FXP_BOOL,  "ecur_on", "remastered cursors", FXO(ecur_on), 0,1,0,
  "draw every cursor as its remastered model, glowing, with the cartridge's own animation. Off puts the cartridge's cursors back" },
{ FXP_FLOAT, "ecur_scale", "remastered cursor size", FXO(ecur_scale), 0.30f, 1.5f, 0,
  "how big the remastered cursors are against the cartridge's, inside the 3D cursor size dial; 0.75 is a quarter smaller" },
/* REMASTERED SOLDIERS. Our own animated infantry in the remastered set's look. */
{ FXP_GROUP, "",  "12j  SOLDIERS  (remastered art)", 0,0,0,0, "" },
{ FXP_BOOL,  "einf_on", "remastered soldiers", FXO(einf_on), 0,1,0,
  "draw the remastered 3D soldier for any infantry type that has one, in Enhanced with experimental art off. Poses the model has no animation for yet are drawn with the 1995 sprite. Off puts every soldier back on the sprite" },
{ FXP_FLOAT, "inf_bright", "soldier brightness", FXO(inf_bright), 0.00f, 2.00f, 0.01f,
  "Brightness of 3D soldiers only; 1 preserves their authored colours" },
{ FXP_FLOAT, "inf_contrast", "soldier contrast", FXO(inf_contrast), 0.25f, 3.00f, 0.01f,
  "3D soldier contrast; black and white stay fixed; 1 is neutral" },
{ FXP_FLOAT, "inf_exposure", "soldier exposure (EV)", FXO(inf_exposure), -3.00f, 3.00f, 0.01f,
  "3D soldier light in stops; 0 is neutral; does not change scene exposure" },
{ FXP_FLOAT, "inf_gamma", "soldier gamma", FXO(inf_gamma), 0.40f, 2.50f, 0.01f,
  "3D soldier midtones; 1 is neutral" },
{ FXP_FLOAT, "inf_sat", "soldier saturation", FXO(inf_sat), 0.00f, 2.00f, 0.01f,
  "3D soldier colour intensity, including masked team paint; 0 is grey" },
{ FXP_FLOAT, "inf_warmth", "soldier warm / cool", FXO(inf_warmth), -1.00f, 1.00f, 0.01f,
  "3D soldier warm versus cool balance; 0 is neutral" },
{ FXP_FLOAT, "inf_tint", "soldier green / magenta", FXO(inf_tint), -1.00f, 1.00f, 0.01f,
  "3D soldier green versus magenta balance; 0 is neutral" },
{ FXP_FLOAT, "inf_red", "soldier red balance", FXO(inf_red), 0.00f, 2.00f, 0.01f,
  "3D soldier red balance; 1 is neutral" },
{ FXP_FLOAT, "inf_green", "soldier green balance", FXO(inf_green), 0.00f, 2.00f, 0.01f,
  "3D soldier green balance; 1 is neutral" },
{ FXP_FLOAT, "inf_blue", "soldier blue balance", FXO(inf_blue), 0.00f, 2.00f, 0.01f,
  "3D soldier blue balance; 1 is neutral" },
{ FXP_FLOAT, "m3inf_sun", "3D soldier sun", FXO(m3inf_sun), 0.0f, 2.0f, 0,
  "how hard the sun falls on the 3D soldiers, as a share of the authored models' sun strength, so that dial moves them too. Their uniforms are paler than a tank's paint, so at 1 the lit side washes out to white" },
{ FXP_FLOAT, "m3inf_fill", "3D soldier fill", FXO(m3inf_fill), 0.0f, 2.0f, 0,
  "the sky-and-bounce fill on the 3D soldiers, as a share of the authored models' fill strength. At 0 their shaded side is black" },
{ FXP_FLOAT, "m3inf_paint", "3D soldier team paint", FXO(m3inf_paint), 0.2f, 1.0f, 0,
  "how bright the team-coloured plates on a 3D soldier are (helmet, sleeves, thighs). 1 is the light grey the house colour is painted over, which reads as the house colour at a glance and glares in the sun" },
{ FXP_BOOL,  "m3inf_shadow", "3D soldier sharp shadows", FXO(m3inf_shadow), 0,1,0,
  "the 3D soldiers cast from a shadow map of their own, fitted to the screen and not softened, so the shadow on the ground has his arms, legs and rifle. Off puts them in the one shared with trees and buildings, whose soft edge is wider than a man's leg and draws a smudge" },
{ FXP_FLOAT, "m3inf_runrate", "3D soldier gait speed", FXO(m3inf_runrate), 0.5f, 3.0f, 0,
  "the 3D soldiers walk, jog or run by how fast they really move, blended between the three. This multiplies the speed that choice reads: at 1 their feet stay planted; above 1 they break into a jog or a run sooner and their legs move a little faster than the ground. They do not move any faster over the map" },
{ FXP_BOOL,  "einf_corpse_keep", "bodies stay", FXO(einf_corpse_keep), 0,1,0,
  "a soldier's body stays where he fell, darkened, instead of sinking away after a few seconds" },
{ FXP_FLOAT, "einf_corpse_max", "most bodies on a map", FXO(einf_corpse_max), 1.0f, 400.0f, 0,
  "past this many, the oldest body goes as the next soldier falls" },
{ FXP_FLOAT, "einf_corpse_dark", "body brightness", FXO(einf_corpse_dark), 0.2f, 1.0f, 0,
  "how much of his own colour a body keeps, so the dead do not read as men still in the fight" },
{ FXP_BOOL,  "einf_ragdoll", "ragdoll falls", FXO(einf_ragdoll), 0,1,0,
  "the death animation hands the fall to a ragdoll part way through; vehicles and soldiers shove bodies" },
{ FXP_BOOL,  "einf_blood", "blood under bodies", FXO(einf_blood), 0,1,0,
  "a dark red pool spreads on the ground under each body" },
{ FXP_BOOL,  "einf_flash", "rifle muzzle flashes", FXO(einf_flash), 0,1,0,
  "a flash at the 3D soldier's own muzzle on each shot (the 1995 game drew none for rifles and pistols)" },
{ FXP_BOOL,  "einf_casings", "spent casings", FXO(einf_casings), 0,1,0,
  "rifles and pistols throw out brass that bounces on the ground and fades after 3 seconds" },
{ FXP_BOOL,  "einf_gibs", "dismemberment", FXO(einf_gibs), 0,1,0,
  "a soldier killed by a grenade, a shell or a rocket comes apart, his pieces thrown outward; tracked vehicles tear apart the soldiers and bodies they drive over. Needs ragdoll falls" },
{ FXP_BOOL,  "einf_spray", "blood spray", FXO(einf_spray), 0,1,0,
  "a torn joint sprays blood for 3 seconds, leaving drops on the ground and on the vehicles and buildings it reaches" },
{ FXP_FLOAT, "einf_scale", "remastered soldier size", FXO(einf_scale), 0.5f, 2.5f, 0,
  "how tall the remastered soldiers stand; the default matches the DOS sprite on screen. The flat infantry cards keep 'infantry size'" },
/* LEAFY TREES. Imported art again, and the group name says so: the geometry is rebuilt
   from a CC0 scan, and the cartridge's own tree model is what Classic keeps drawing. */
{ FXP_GROUP, "",  "12c  ENHANCED TREES", 0,0,0,0, "" },
{ FXP_BOOL,  "tree3d", "Enhanced trees", FXO(tree3d), 0,1,0,
  "use the accepted Enhanced tree models; off keeps the cartridge trees" },
{ FXP_FLOAT, "etree_height", "tree height scale", FXO(etree_height), 0.50f, 1.80f, 0,
  "height of the current Enhanced models; 1 keeps their authored height and roots stay planted" },
{ FXP_FLOAT, "etree_width", "tree width scale", FXO(etree_width), 0.50f, 1.80f, 0,
  "width of the current Enhanced trees and clumps; 1 keeps their authored width" },
{ FXP_FLOAT, "etree_vary", "size variation", FXO(etree_vary), 0.0f, 0.40f, 0,
  "stable per-placement size variation, shared by the tree, its shadow and its burning stages" },
{ FXP_FLOAT, "etree_lean", "random lean", FXO(etree_lean), 0.0f, 0.20f, 0,
  "stable per-tree lean; the root anchor never moves" },
{ FXP_FLOAT, "etree_gain", "tree brightness", FXO(etree_gain), 0.50f, 1.50f, 0,
  "brightness of the current Enhanced trees; 1 preserves their authored colours" },
{ FXP_FLOAT, "etree_shadow_fill", "tree shadow fill", FXO(etree_shadow_fill), 0.0f, 1.0f, 0.01f,
  "Indirect sky fill on shaded Enhanced trees and clumps. Raise to soften canopy self-shadows; 0 keeps the previous shading. Uses sky energy, preserves real sun shadows and does not affect terrain or units" },
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
{ FXP_GROUP, "",  "12l  GROUND PATCHES  (ours)", 0,0,0,0, "" },
{ FXP_BOOL,  "mottle_on", "ground patches", FXO(mottle_on), 0,1,0,
  "lay patches of ground variation over plain terrain: only where a cell and all eight of its neighbours are plain ground, so a patch never reaches over a road, a river, a shore, a bridge or a cliff. Drawn into the ground before anything standing on it, so it cannot reach a vehicle or a structure" },
{ FXP_FLOAT, "mottle_strength", "patch strength", FXO(mottle_strength), 0.0f, 1.0f, 0,
  "how far a patch is allowed to cover the ground under it. Low is a tint, high is a replacement; the art is photographic and lighter than the ground it lies on, so full strength reads as something stuck on rather than something growing there" },
{ FXP_FLOAT, "mottle_density", "patch density", FXO(mottle_density), 0.0f, 1.0f, 0,
  "share of the eligible cells that carry a patch. Ground fully covered reads as a second texture rather than as variation" },
{ FXP_GROUP, "",  "12m  BUSHES AND FOLIAGE  (imported)", 0,0,0,0, "" },
{ FXP_BOOL,  "fol_on", "bushes and foliage", FXO(fol_on), 0,1,0,
  "scatter bushes and grass clumps over the map under Enhanced, temperate maps only, and only on plain grass: never on a road, a bridge, water, a shore, a cliff, tiberium, a wall or under a building, and a cell a building later covers loses its plants. Decoration only: units drive through it and nothing in the game can see it" },
{ FXP_BOOL,  "fol_shadows", "bush shadows", FXO(fol_shadows), 0,1,0,
  "the bushes cast into the sun's shadow map like the trees do. Off saves their cost in that pass and leaves the ground under them unshadowed" },
{ FXP_BOOL,  "fol_grass_shadows", "grass clump shadows", FXO(fol_grass_shadows), 0,1,0,
  "the grass clumps cast sun shadows too. Off by default: a clump is small, and its shadow on top of the screen-space contact shadow every object gets reads as a dark ring round each one" },
{ FXP_FLOAT, "fol_bush_amt", "bushes per cell", FXO(fol_bush_amt), 0.0f, 0.60f, 0,
  "how many bushes stand on an average grass cell. 0.05 is one bush in twenty cells; clustering below gathers them into thickets without changing the total much" },
{ FXP_FLOAT, "fol_grass_amt", "grass clumps per cell", FXO(fol_grass_amt), 0.0f, 4.0f, 0,
  "how many grass clumps stand on an average grass cell. This is most of the feature's cost: each clump is 320 triangles" },
{ FXP_STEP,  "fol_bush_detail", "bush detail", FXO(fol_bush_detail), 0.0f, 2.0f, 1.0f,
  "which of the pack's three versions of each bush: 2 the full one (6,000 and 28,000 triangles, leaves at 1024 texels), 1 the middle one (2,900 and 12,000, at 512), 0 the plainest (1,000 and 3,900, at 256), whose big leaf cards show up close. Each has its own leaves drawn by the artist, not a blurred copy" },
{ FXP_FLOAT, "fol_bush_size", "bush height (cells)", FXO(fol_bush_size), 0.10f, 1.50f, 0,
  "how tall a bush stands. A tree stands about 1.6" },
{ FXP_FLOAT, "fol_grass_size", "clump height (cells)", FXO(fol_grass_size), 0.04f, 0.60f, 0,
  "how tall a grass clump stands" },
{ FXP_FLOAT, "fol_vary", "size variation", FXO(fol_vary), 0.0f, 0.80f, 0,
  "how far plants differ in size from one another; 0 makes every bush the same height" },
{ FXP_FLOAT, "fol_cluster", "clustering", FXO(fol_cluster), 0.0f, 1.0f, 0,
  "0 scatters plants evenly over every grass cell; 1 gathers them into thickets with open grass between. The amounts above stay roughly the same, they are only moved" },
{ FXP_FLOAT, "fol_cluster_len", "thicket size (cells)", FXO(fol_cluster_len), 1.5f, 24.0f, 0,
  "how far apart the thickets are, and so how big each one is" },
{ FXP_FLOAT, "fol_margin", "distance from edges (cells)", FXO(fol_margin), 0.0f, 1.50f, 0,
  "how far a plant keeps from anything that is not plain grass: roads, water, shores, cliffs, tiberium, walls and buildings. 0 lets a plant stand right at the edge of its cell" },
{ FXP_FLOAT, "fol_bush_mix", "bush type mix", FXO(fol_bush_mix), 0.0f, 1.0f, 0,
  "which of the two bushes: 0 is all the round yellow-green one, 1 all the tall bright green one" },
{ FXP_FLOAT, "fol_g1", "grass type 1 share", FXO(fol_g1), 0.0f, 1.0f, 0,
  "how often the tall seed-headed clump is picked, against the other two" },
{ FXP_FLOAT, "fol_g2", "grass type 2 share", FXO(fol_g2), 0.0f, 1.0f, 0,
  "how often the broad golden-tipped clump is picked" },
{ FXP_FLOAT, "fol_g3", "grass type 3 share", FXO(fol_g3), 0.0f, 1.0f, 0,
  "how often the low wide green clump is picked" },
{ FXP_FLOAT, "fol_tilt", "tilt", FXO(fol_tilt), 0.0f, 0.40f, 0,
  "how far a plant leans off upright, each its own way" },
{ FXP_FLOAT, "fol_wind", "wind sway", FXO(fol_wind), 0.0f, 0.30f, 0,
  "how far the top of a plant moves in the wind, as a share of its height. Speed and bearing are the trees' wind" },
{ FXP_FLOAT, "fol_flutter", "leaf flutter", FXO(fol_flutter), 0.0f, 0.20f, 0,
  "how much the leaves and blades shiver on their own beat inside the sway" },
{ FXP_FLOAT, "fol_gain", "bush exposure", FXO(fol_gain), 0.10f, 2.0f, 0,
  "one brightness over the bushes, lit otherwise exactly as the trees are. The trees' own exposure is 0.36; a bush stands in more open sky and the screen-space contact shadow darkens a small plant far more than a tall one, so it wants more" },
{ FXP_FLOAT, "fol_grass_gain", "clump exposure", FXO(fol_grass_gain), 0.10f, 2.0f, 0,
  "one brightness over the grass clumps, on its own dial because a clump sits down in the contact shadow of the ground it grows from. Tuned so a clump reads about as bright as the field around it" },
{ FXP_BOOL,  "fol_crush", "vehicles flatten it", FXO(fol_crush), 0,1,0,
  "a vehicle driving over a bush or a clump lays it flat, and it then fades away and is gone for the rest of the mission. Infantry walk through it. Off leaves every plant standing" },
{ FXP_FLOAT, "fol_crush_r", "flatten margin (cells)", FXO(fol_crush_r), 0.0f, 1.0f, 0,
  "how far outside a vehicle's hull a plant's root can stand and still be flattened. The hull is measured off each vehicle's own model, turned to where it faces; the margin covers a crown that reaches over the hull before its root is under it" },
{ FXP_FLOAT, "fol_flat_time", "flatten time (s)", FXO(fol_flat_time), 0.05f, 2.0f, 0,
  "how long a plant takes to go down once a vehicle is on it, in game seconds" },
{ FXP_FLOAT, "fol_fade_time", "fade time (s)", FXO(fol_fade_time), 0.5f, 10.0f, 0,
  "how long a flattened plant takes to fade away once it is down, in game seconds" },
{ FXP_STEP,  "fol_seed", "layout seed", FXO(fol_seed), 0.0f, 99.0f, 1.0f,
  "re-rolls where every plant stands, keeping the amounts. The same seed gives the same map every time" },
/* ROCKS, scattered by the renderer at the foot of rock faces, and moved by what drives
   into them (foliage3d_mod.h). */
{ FXP_GROUP, "",  "12n  ROCKS  (imported)", 0,0,0,0, "" },
{ FXP_BOOL,  "rock_on", "rocks", FXO(rock_on), 0,1,0,
  "scatter stones at the foot of rock faces under Enhanced: cliffs and rock walls (the map's slope and boulder tiles) and steep ground. Only on plain ground, never on a road, water, tiberium or under a building, on any theater" },
{ FXP_BOOL,  "rock_shadows", "rock shadows", FXO(rock_shadows), 0,1,0,
  "the rocks cast into the sun's shadow map" },
{ FXP_FLOAT, "rock_amt", "rocks per cell at the face", FXO(rock_amt), 0.0f, 3.0f, 0,
  "how many rocks lie on a cell right beside a rock face; further out it thins along the falloff" },
{ FXP_FLOAT, "rock_spread", "spread from the face (cells)", FXO(rock_spread), 0.0f, 8.0f, 0,
  "how far out from a rock face rocks still lie. Past this, none" },
{ FXP_FLOAT, "rock_falloff", "falloff", FXO(rock_falloff), 0.2f, 4.0f, 0,
  "how quickly they thin out over that spread: 1 is even, higher keeps them close in at the face, lower spreads them far" },
{ FXP_FLOAT, "rock_size", "rock size (cells)", FXO(rock_size), 0.05f, 1.2f, 0,
  "the longest side of an average boulder; the pack's small detail stones are drawn at under half of this" },
{ FXP_FLOAT, "rock_vary", "size variation", FXO(rock_vary), 0.0f, 0.9f, 0,
  "how far rocks differ in size from one another" },
{ FXP_FLOAT, "rock_shrink", "smaller further out", FXO(rock_shrink), 0.0f, 1.0f, 0,
  "how much smaller rocks are at the edge of the spread than right at the face, the way scree lies. 0 keeps one size all the way out" },
{ FXP_FLOAT, "rock_detail", "small stone share", FXO(rock_detail), 0.0f, 1.0f, 0,
  "how many are the pack's small detail stones rather than boulders" },
{ FXP_FLOAT, "rock_c1", "grey rock share", FXO(rock_c1), 0.0f, 1.0f, 0, "how often the first colouring is picked: the dark grey stone, or with the painted stones the theater's darker stone" },
{ FXP_FLOAT, "rock_c2", "pale rock share", FXO(rock_c2), 0.0f, 1.0f, 0, "how often the second colouring is picked: the pale lichened stone, painted or not" },
{ FXP_FLOAT, "rock_c3", "red rock share", FXO(rock_c3), 0.0f, 1.0f, 0, "how often the third colouring is picked: the red-brown stone, or with the painted stones the theater's warm one" },
{ FXP_FLOAT, "rock_sink", "sunk into the ground", FXO(rock_sink), 0.0f, 0.5f, 0,
  "how much of a rock's size is buried, so it sits in the ground rather than on it" },
{ FXP_FLOAT, "rock_margin", "distance from edges (cells)", FXO(rock_margin), 0.0f, 1.5f, 0,
  "how far a rock keeps from a road, water, tiberium or a building" },
{ FXP_FLOAT, "rock_gain", "rock exposure", FXO(rock_gain), 0.1f, 2.0f, 0,
  "one brightness over all the rocks; the painted stones are drawn at 1.7 times it" },
{ FXP_STEP,  "rock_seed", "rock layout seed", FXO(rock_seed), 0.0f, 99.0f, 1.0f,
  "re-rolls where every rock lies, keeping the amounts" },
{ FXP_BOOL,  "rock_physics", "rock physics", FXO(rock_physics), 0,1,0,
  "a vehicle that drives into a rock shoves it, and a structure going up throws the rocks near it; each then rolls or tumbles on the ground by its own shape until it settles. Infantry do not move them. Decoration only: a rock never blocks anything" },
{ FXP_FLOAT, "rock_push", "vehicle shove", FXO(rock_push), 0.0f, 4.0f, 0,
  "how hard a vehicle sends a rock, as a multiple of the vehicle's own speed, for a rock of 0.3 cells" },
{ FXP_FLOAT, "rock_mass", "weight of size", FXO(rock_mass), 0.0f, 2.5f, 0,
  "how much less a big rock is moved than a small one by the same shove. 0 moves every rock alike; 1.5 is roughly by weight" },
{ FXP_FLOAT, "rock_friction", "ground grip", FXO(rock_friction), 0.05f, 1.5f, 0,
  "friction between rock and ground: low lets rocks slide, high makes them tip and roll" },
{ FXP_FLOAT, "rock_bounce", "bounce", FXO(rock_bounce), 0.0f, 0.8f, 0,
  "how much a rock bounces when it lands" },
{ FXP_FLOAT, "rock_gravity", "gravity (cells/s2)", FXO(rock_gravity), 1.0f, 40.0f, 0,
  "how fast a thrown rock falls back. The game's cell is several metres, so a real 9.8 reads as slow motion" },
{ FXP_FLOAT, "rock_pulse", "building pulse", FXO(rock_pulse), 0.0f, 15.0f, 0,
  "how hard a structure going up throws the rocks near it outward and up, in cells a second for a rock of 0.3 cells right at its edge" },
{ FXP_FLOAT, "rock_pulse_r", "building pulse reach (cells)", FXO(rock_pulse_r), 0.0f, 6.0f, 0,
  "how far from a new structure's footprint the pulse reaches; it weakens to nothing at this distance. The bushes and clumps inside it go down too" },
{ FXP_GROUP, "",  "12e  WHAT THE BATTLE DOES TO IT  (ours)", 0,0,0,0, "" },
{ FXP_FLOAT, "crush_tread_life", "tread mark life (s)", FXO(crush_tread_life), 0.07f, 136.0f, 0,
  "how long a vehicle's tracks, and the grass it flattened, take to stand back up. Seconds, not a per-tick factor; 136 is the arithmetic ceiling and anything above it is clamped and said out loud" },
{ FXP_FLOAT, "crush_cover_life", "regrowth (s)", FXO(crush_cover_life), 0.07f, 136.0f, 0,
  "how long grass takes to grow back over ground a building or a vehicle has left. A razed footprint greens over on this clock" },
{ FXP_FLOAT, "crush_gauge", "track spacing", FXO(crush_gauge), 0.0f, 1.2f, 0,
  "how far apart a vehicle's two tracks are, as a fraction of its beam; 0 lays one band down the centre line" },
{ FXP_FLOAT, "crush_band", "track width", FXO(crush_band), 0.0f, 0.6f, 0,
  "how wide one track is, as a fraction of the beam. Never thinner than one texel of the field, which is what stops a vehicle driving due north leaving no mark at all" },
{ FXP_FLOAT, "crush_press", "track depth", FXO(crush_press), 0.0f, 1.0f, 0,
  "how hard the two tracks press the grass they leave behind, once it has grown back. 1 lays it flat; the ground under the hull itself is flattened by the two dials below whatever this says" },
{ FXP_FLOAT, "crush_skirt", "clearance", FXO(crush_skirt), 0.0f, 2.0f, 0,
  "how far past a building or a hull the grass is cleared, in blade heights. NOT decoration: at 0 a blade overlaps the silhouette, the ground normal buffer has no ground there, and the post chain rings every structure and every unit with a dark halo one blade tall" },
{ FXP_FLOAT, "crush_flatten", "flatten time (s)", FXO(crush_flatten), 0.0f, 2.0f, 0,
  "how long grass takes to bend down flat under a vehicle driving over it. Seconds of simulated time; 0 lays it flat on the tick the hull arrives" },
{ FXP_FLOAT, "crush_fade", "fade time (s)", FXO(crush_fade), 0.0f, 20.0f, 0,
  "how long the flattened grass takes to fade away once the vehicle has driven off it. After that it grows back on the regrowth clock above and stands up again on the tread clock" },
{ FXP_FLOAT, "crush_bld_ring", "cleared round a new building (cells)", FXO(crush_bld_ring), 0.0f, 6.0f, 0,
  "how far round a structure put down during the mission the grass is pressed flat and faded out, in cells from its footprint, on the two clocks above. It stays gone while the structure stands and grows back once it is sold or destroyed. The mission's own buildings are left alone" },
{ FXP_GROUP, "",  "12f  THE TRACKS IT LEAVES ON THE GROUND  (ours)", 0,0,0,0, "" },
{ FXP_BOOL,  "tmark_on", "ground tracks", FXO(tmark_on), 0,1,0,
  "the two bands a vehicle leaves behind it on the ground. The block above marks grass; this marks the ground itself, so it shows on a desert map where there is no grass to press" },
{ FXP_FLOAT, "tmark_life", "track life (s)", FXO(tmark_life), 2.0f, 180.0f, 0,
  "how long a mark takes to fade to nothing. Seconds of simulated time, so it is the same length on any machine and in any script" },
{ FXP_FLOAT, "tmark_dist", "trail length (cells)", FXO(tmark_dist), 1.0f, 20.0f, 0,
  "how far behind the vehicle the trail is drawn. The last 40 per cent of it is a ramp, so the far end thins away instead of ending at a line across the ground" },
{ FXP_FLOAT, "tmark_dark", "strength", FXO(tmark_dark), 0.0f, 1.0f, 0,
  "how far a fresh mark darkens the ground, before either fade. 0 draws nothing at all" },
{ FXP_FLOAT, "tmark_gauge", "track spacing", FXO(tmark_gauge), 0.0f, 1.2f, 0,
  "how far apart the two tracks are, as a fraction of the vehicle's own beam; 0 lays one band down the centre line" },
{ FXP_FLOAT, "tmark_band", "track width", FXO(tmark_band), 0.02f, 0.6f, 0,
  "how wide one track is, as a fraction of the beam" },
{ FXP_FLOAT, "tmark_cleat", "cleat spacing (cells)", FXO(tmark_cleat), 0.05f, 1.0f, 0,
  "how far apart the cleat prints are along the track. A real length in the world, because the pattern is generated rather than baked" },
{ FXP_FLOAT, "tmark_r", "track red", FXO(tmark_r), 0.0f, 1.0f, 0, "" },
{ FXP_FLOAT, "tmark_g", "track green", FXO(tmark_g), 0.0f, 1.0f, 0, "" },
{ FXP_FLOAT, "tmark_b", "track blue", FXO(tmark_b), 0.0f, 1.0f, 0,
  "what the ground under a cleat print is MULTIPLIED by, so the track is a fraction of whatever colour the ground already is. 1,1,1 is the identity and draws nothing" },
{ FXP_FLOAT, "tmark_blood", "bloody tracks (s)", FXO(tmark_blood), 0.0f, 30.0f, 0,
  "for this long after a vehicle drives over a body or its blood, its tracks are laid red, fading back to the ordinary track over the second half" },
{ FXP_GROUP, "",  "12i  RELIGHT THE CARTRIDGE ART  (ours)", 0,0,0,0, "" },
{ FXP_BOOL,  "relight_on", "relight 1995 models", FXO(relight_on), 0,1,0,
  "take the console's baked lighting off every vehicle and building and light them with this sun instead. Their own texture becomes the base colour and the two dials below supply the surface. Off is the cartridge's own shading, which is what Classic and Windows 98 draw and what this has always looked like" },
{ FXP_FLOAT, "relight_rough", "roughness", FXO(relight_rough), 0.05f, 1.0f, 0,
  "how rough the whole cartridge art set is taken to be. One number for all of it, because none of it carries a roughness map and there is nothing to derive one from" },
{ FXP_FLOAT, "relight_sun", "sun", FXO(relight_sun), 0.0f, 3.0f, 0,
  "how much light the new sun delivers, in the units of the baked shade it replaces. Near 1 keeps the picture at the brightness it had and changes only which way the light comes from" },
{ FXP_FLOAT, "relight_spec", "highlight", FXO(relight_spec), 0.0f, 2.0f, 0,
  "how much of a specular highlight a relit surface gets. Low on purpose: nothing the cartridge painted was shiny, and a strong lobe over a 1995 texture reads as a fault rather than as gloss" },
{ FXP_FLOAT, "relight_clamp", "ratio ceiling", FXO(relight_clamp), 1.0f, 8.0f, 0,
  "the most the relight may brighten a pixel. The baked shade approaches zero in the cartridge own deep shadow, and dividing by that turns its grain into a supernova" },
{ FXP_FLOAT, "relight_amb", "fill", FXO(relight_amb), 0.0f, 3.0f, 0,
  "the light a relit surface gets where the sun does not reach it. At zero every shaded face is black" },
{ FXP_FLOAT, "relight_floor", "shade floor", FXO(relight_floor), 0.02f, 0.6f, 0,
  "the darkest baked shade that may be divided by when recovering the texture. The cartridge's own deep shadow approaches zero, and dividing by that turns its grain into a supernova" },
{ FXP_GROUP, "",  "12h  HIGH DYNAMIC RANGE  (retired)", 0,0,0,0, "" },
{ FXP_BOOL,  "hdr_on", "high dynamic range (retired)", FXO(hdr_on), 0,1,0,
  "RETIRED: the chain always draws in eight bits and this switch does nothing; it stays so older settings files read cleanly. It was: render the scene into a floating point buffer so light can be brighter than white, and map it back to a screen at the end. Off is the eight bit chain this shipped with, where a highlight was clipped flat before the bloom or the grade ever saw it" },
{ FXP_STEP,  "tonemap", "highlight rolloff", FXO(tonemap), 0.0f, 1.0f, 1.0f,
  "0 clips at white, which is what the eight bit chain did. 1 bends what used to clip into the headroom above it. Deliberately not a film curve: the world pass is gamma encoded, so a film curve here would be a look chosen by eye rather than a transform" },
{ FXP_FLOAT, "tone_knee", "rolloff knee", FXO(tone_knee), 0.2f, 1.0f, 0,
  "where the highlight rolloff begins. Below it the curve is the identity, so the picture is untouched; above it what used to clip flat at white bends into the headroom instead. At 1.0 nothing rolls off and the chain clips as it always did" },
{ FXP_FLOAT, "bloom_clamp", "bloom ceiling", FXO(bloom_clamp), 1.0f, 64.0f, 0,
  "the brightest a pixel may be when it enters the bloom. Eight bits used to cap this at white for free; a microfacet highlight now reaches the tens, and one of those through four blur passes is a disc across the frame" },
{ FXP_GROUP, "",  "12g  AUTHORED MODELS  (ours)", 0,0,0,0, "" },
{ FXP_BOOL,  "m3_env", "light from the sky", FXO(m3_env), 0,1,0,
  "light an authored hull off the sky image, and let metal reflect it, instead of off a two-colour fill. Off is that fill, which is also what a build with no sky pack draws. It rides the fill strength dial below, so turning that to zero still turns the whole ambient off" },
{ FXP_BOOL,  "m3_on", "authored models", FXO(m3_on), 0,1,0,
  "draw the high detail model for any type that has one installed, lit by a real light. Off puts every vehicle back on the cartridge's own model, which is what Classic draws and what a Windows 98 build draws. Under experimental art: this row takes a model out of that look, it does not put one into any other" },
{ FXP_FLOAT, "m3_sun", "sun strength", FXO(m3_sun), 0.0f, 3.0f, 0,
  "how hard the directional sun falls on an authored hull. Its BEARING is the chain's own azimuth and elevation, shared with the ground and with the shadow this hull casts" },
{ FXP_FLOAT, "m3_ambient", "fill strength", FXO(m3_ambient), 0.0f, 3.0f, 0,
  "the sky-and-bounce fill. At 0 everything turned away from the sun is black, which is what no light looks like rather than what shade looks like" },
{ FXP_FLOAT, "m3_normal", "surface relief", FXO(m3_normal), 0.0f, 2.0f, 0,
  "how far the normal map tilts the shading. 0 is the raw geometry, which is the honest floor for deciding whether relief in a picture is modelled or painted" },
{ FXP_FLOAT, "m3_ao", "baked occlusion", FXO(m3_ao), 0.0f, 1.0f, 0,
  "how far the baked occlusion map darkens the fill. It reaches the fill only, because the sun's own creases are already darkened by the shadow map" },
{ FXP_FLOAT, "m3_roughmin", "roughness floor", FXO(m3_roughmin), 0.0f, 0.5f, 0,
  "the floor under the roughness map. A perfectly smooth texel is a mirror with nothing to reflect, which reads as a black dot rather than as polish" },
{ FXP_FLOAT, "m3_specamb", "metal fill", FXO(m3_specamb), 0.0f, 2.0f, 0,
  "how much of the fill the specular lobe may take. At 0 every bare-steel texel is black until it catches the sun, because metal carries no diffuse light at all" },
/* THE STRUCTURES ON THEIR OWN, Enhanced only. Every row is neutral at 1, and with all
   of them there the picture is the one this group's absence drew, byte for byte. */
{ FXP_GROUP, "",  "12p  STRUCTURES  (ours)", 0,0,0,0, "" },
{ FXP_FLOAT, "st_gamma", "gamma", FXO(st_gamma), 0.40f, 2.50f, 0.01f,
  "a curve on every building's and wall's own colour, before it is lit: above 1 lifts the mid-tones, below 1 deepens them, black and white stay. The ground, the units and the effects are not touched. 1 = as drawn" },
{ FXP_FLOAT, "st_bright", "brightness", FXO(st_bright), 0.0f, 2.0f, 0.01f,
  "a gain on the buildings' and walls' own colour, before they are lit. 1 = as drawn" },
{ FXP_FLOAT, "st_contrast", "contrast", FXO(st_contrast), 0.0f, 2.0f, 0.01f,
  "spreads the buildings' and walls' own colour away from middle grey (above 1) or draws it in (below 1), as the colour grade's contrast does for the whole frame. 1 = as drawn" },
{ FXP_FLOAT, "st_sat", "saturation", FXO(st_sat), 0.0f, 2.0f, 0.01f,
  "the buildings' and walls' colour richness: 0 is grey, 2 twice as rich. 1 = as drawn" },
{ FXP_FLOAT, "st_shade", "shade depth", FXO(st_shade), 0.0f, 2.0f, 0.01f,
  "how dark a building or wall goes where the sun does not reach it: its sides turned from the sun, the shadows cast on it and the clouds' shadow. 0 leaves it unshaded, 2 twice as deep. The shadow tint of group 3 is the colour it goes toward. 1 = today" },
{ FXP_FLOAT, "st_sun", "sun strength", FXO(st_sun), 0.0f, 2.0f, 0.01f,
  "how much the sun lights a building or wall: 0 leaves its sunlit faces at the colour of its shaded ones, 2 lifts them twice as far above it. 1 = today" },
{ FXP_FLOAT, "st_glow", "self-lit glow", FXO(st_glow), 0.0f, 3.0f, 0.01f,
  "the remastered buildings' own lights: lamps, signal lights, molten pools, lit glass. 0 puts them out, above 1 they burn brighter and bloom more. A cartridge building carries no such surface. 1 = today" },
{ FXP_FLOAT, "st_ao", "occlusion", FXO(st_ao), 0.0f, 2.0f, 0.01f,
  "how much the ambient occlusion of group 5 darkens the creases and corners of a building or wall. 0 takes it off them, 2 doubles it. 1 = today" },
/* VEHICLE VISUALS, Enhanced only. Every row is neutral at 1, and with all
   of them there the picture is the one this group's absence drew, byte for byte. */
{ FXP_GROUP, "",  "12q  VEHICLES  (ours)", 0,0,0,0, "" },
{ FXP_FLOAT, "vh_gamma", "gamma", FXO(vh_gamma), 0.40f, 2.50f, 0.01f,
  "a curve on vehicle and aircraft colour before lighting: above 1 lifts mid-tones, below 1 deepens them. Buildings, infantry and terrain keep their own settings. 1 = as drawn" },
{ FXP_FLOAT, "vh_bright", "brightness", FXO(vh_bright), 0.0f, 2.0f, 0.01f,
  "a gain on vehicle and aircraft colour before lighting. 1 = as drawn" },
{ FXP_FLOAT, "vh_contrast", "contrast", FXO(vh_contrast), 0.0f, 2.0f, 0.01f,
  "spreads vehicle and aircraft colour away from middle grey above 1, or draws it in below 1. 1 = as drawn" },
{ FXP_FLOAT, "vh_sat", "saturation", FXO(vh_sat), 0.0f, 2.0f, 0.01f,
  "vehicle and aircraft colour richness: 0 is grey, 2 twice as rich. 1 = as drawn" },
{ FXP_FLOAT, "vh_shade", "shade depth", FXO(vh_shade), 0.0f, 2.0f, 0.01f,
  "how dark vehicles and aircraft go in sun and cloud shadows. 0 leaves them unshaded, 2 twice as deep. 1 = as drawn" },
{ FXP_FLOAT, "vh_sun", "sun strength", FXO(vh_sun), 0.0f, 2.0f, 0.01f,
  "how much the sun lights vehicles and aircraft: 0 leaves the shadow colour, 2 lifts sunlit faces twice as far above it. 1 = as drawn" },
{ FXP_FLOAT, "vh_glow", "self-lit glow", FXO(vh_glow), 0.0f, 3.0f, 0.01f,
  "a gain on authored vehicle self-lit surfaces, when the model has them. 0 puts them out, above 1 increases their glow and bloom. Does not change headlight beams cast onto the world. 1 = as drawn" },
{ FXP_FLOAT, "vh_ao", "occlusion", FXO(vh_ao), 0.0f, 2.0f, 0.01f,
  "how much ambient occlusion darkens vehicle creases and corners. 0 removes that darkening, 2 doubles it. 1 = as drawn" },
/* THE DEBUG GROUP, and it is last on purpose: it is a workshop control rather than a
   presentation one, and nothing above it should move when it is toggled. */
{ FXP_GROUP, "",  "13  DEBUG", 0,0,0,0, "" },
{ FXP_BOOL,  "debug", "debug: updated meshes", FXO(debug), 0,1,0,
  "draw every type that a debug.pack beside the mission's pack redefines; off, or with no debug pack, the mission's own art is drawn" },
{ FXP_GROUP, "", "EXPERIMENTAL", 0,0,0,0, "" },
{ FXP_BOOL, "new_sprites", "New Sprites", FXO(new_sprites), 0,1,0,
  "replace Tiberian Dawn Minigunners with the reviewed native pixel-art animation set. Off restores the selected original or 3D art. Classic and Red Alert retain their own sprites" },
{ FXP_FLOAT, "ns_bright", "sprite brightness", FXO(ns_bright), 0.0f,2.0f,0.01f,
  "brightness of New Sprites only. 1 preserves the approved palette; 0 is black. Terrain, other units and HUD are unchanged" },
{ FXP_FLOAT, "ns_contrast", "sprite contrast", FXO(ns_contrast), 0.0f,2.0f,0.01f,
  "contrast of New Sprites around mid-grey. 1 preserves the palette; higher separates highlights and shadows" },
{ FXP_FLOAT, "ns_exposure", "sprite exposure (EV)", FXO(ns_exposure), -3.0f,3.0f,0.01f,
  "exposure of New Sprites in linear-light stops. 0 is neutral; +1 doubles light, -1 halves it. Does not change the scene exposure" },
{ FXP_FLOAT, "ns_gamma", "sprite gamma", FXO(ns_gamma), 0.40f,2.50f,0.01f,
  "midtone brightness of New Sprites. 1 is neutral; higher lifts midtones while retaining black and white" },
{ FXP_FLOAT, "ns_sat", "sprite saturation", FXO(ns_sat), 0.0f,2.0f,0.01f,
  "colour intensity of New Sprites, including team paint. 1 is neutral, 0 is greyscale. All sprite controls apply live and save with this preset" },
{ FXP_BOOL, "experimental_relief", "terrain displacement", FXO(experimental_relief), 0,1,0,
  "move cliff and gravel road mesh vertices from height maps and light their surfaces with normals; gravel also uses a wetness-aware roughness map" },
{ FXP_STEP,  "experimental_relief_mode", "displacement method", FXO(experimental_relief_mode), 0.0f, 1.0f, 1.0f,
  "0 parallax occlusion mapping: each cliff and road cell is two triangles and the relief is found per pixel by walking the view ray through the height map, the cheap way. 1 true geometry: each such cell is a 24x24 mesh moved by the height map, which gives real silhouettes, depth and cast shadows and costs several milliseconds a frame" },
{ FXP_FLOAT, "experimental_relief_steps", "parallax steps", FXO(experimental_relief_steps), 4.0f, 48.0f, 0,
  "how many steps the parallax march takes through the relief's height. Fewer is cheaper and shows stair-steps on the tallest rocks at a glancing angle; more is smoother" },
{ FXP_FLOAT, "experimental_height", "relief scale", FXO(experimental_height), 0.0f,0.60f,0,
  "maximum displacement in world cells" },
{ FXP_BOOL,  "m3inf_on", "3D soldiers", FXO(m3inf_on), 0,1,0,
  "draw an animated 3D soldier for any infantry type that has one installed, under experimental art only. Poses the model has no animation for, lying prone, crawling and dying, are drawn with the sprite. Off puts every soldier back on the sprite; the authored vehicles row does not affect it" },
{ FXP_FLOAT, "inf_scale_exp", "infantry size (experimental)", FXO(inf_scale_exp), 0.5f, 1.5f, 0,
  "how big infantry stand under experimental art, the 3D soldiers and any sprite soldier alike. Replaces MASTER > infantry size while experimental art is on; that row keeps the size everywhere else" },
{ FXP_BOOL,  "freeinf_on", "free infantry movement", FXO(freeinf_on), 0,1,0,
  "soldiers walk straight across open ground at any angle, cutting the corners of their path, instead of stepping from cell to cell in eight directions. This changes the game, not only the picture: the route, the cells a soldier passes through and everything they do to him are the original rules. Off in a multiplayer match whatever this says" },
{ FXP_BOOL,  "uvfx_on", "Unity explosions", FXO(uvfx_on), 0,1,0,
  "the game's explosions are replaced by Unity ones from the Vefects pack, one chosen for each (vfx3d/claims.txt): replayed from Unity's own simulation and drawn with the pack's shaders, bloom and tonemap, laid over the finished picture. Off puts the cartridge's explosions back" },
{ FXP_FLOAT, "uvfx_scale", "Unity explosion size", FXO(uvfx_scale), 0.10f, 1.0f, 0,
  "game cells per Unity metre. At 0.33 the fireball is two cells across" },
{ FXP_FLOAT, "uvfx_sun", "Unity explosion sun", FXO(uvfx_sun), 0.0f, 2.0f, 0,
  "how strongly the sun lights the explosion's smoke, as a share of the light the effect was authored under" },
{ FXP_BOOL,  "uvfx_gamesun", "Unity smoke in game sun", FXO(uvfx_gamesun), 0,1,0,
  "light the explosion's smoke from the game's sun. Off lights it the way it was authored in Unity, from behind and above, which is why it reads dark there; on lights it from the front and it reads pale" },
{ FXP_FLOAT, "uvfx_scatter", "Unity bloom scatter", FXO(uvfx_scatter), 0.05f, 0.95f, 0,
  "how far the explosion's glow spreads. 0.5 matches Unity" },
{ FXP_FLOAT, "uvfx_gunsize", "muzzle flash size", FXO(uvfx_gunsize), 0.5f, 8.0f, 0,
  "how much larger than chosen the Unity muzzle flashes are drawn. At 1 a rifle flash is its real size, a speck at the game's distance" },
{ FXP_BOOL,  "curgreen_on", "green pointer", FXO(curgreen_on), 0,1,0,
  "the plain pointer becomes the new green arrow, a 3D model lying on the ground like the cartridge's own. Only the plain pointer changes; move, attack, sell and every other shape stay the cartridge's. Off puts the white pointer back" },
{ FXP_FLOAT, "curgreen_scale", "green pointer size", FXO(curgreen_scale), 0.30f, 2.0f, 0,
  "how big the green pointer is. 1 is the cartridge pointer's own size; 0.7 matches the size the white pointer is drawn at. The other cursor shapes keep the 3D cursor size dial" },
{ FXP_GROUP, "",  "3D GRASS", 0,0,0,0, "" },
{ FXP_BOOL,  "grass", "real grass", FXO(grass), 0,1,0,
  "stand grass on the ground in Enhanced mode. Adjust its density, height and colour below" },
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
/* BUSHES AND FOLIAGE, scattered by the renderer over plain grass (foliage3d_mod.h).
   Its own group, after the grass, because it is judged against the grass and the trees.
   Every dial here moves the picture live; the layout ones rebuild the scatter, which
   is a few milliseconds and happens once per change rather than per frame. */
/* NEW AUDIO, the experimental art's counterpart for the ears. Last so that nothing
   above it moves, and it is a switch rather than a dial: which sounds are replaced is
   decided by which files are in newaudio/, not here. */
{ FXP_GROUP, "",  "NEW AUDIO", 0,0,0,0, "" },
{ FXP_BOOL,  "new_audio", "new audio", FXO(new_audio), 0,1,0,
  "play the replacement sound effects in newaudio/ instead of the 1995 ones. A sound with no replacement keeps its 1995 version, and so does all of the music and speech unless a file names it. Takes effect on the next sound played" },
{ FXP_END, "", "", 0,0,0,0, "" }
};

static inline float* fx_fptr(FxState* s, const FxParam* p)
{ return (float*)((char*)s + p->off); }
static inline int* fx_iptr(FxState* s, const FxParam* p)
{ return (int*)((char*)s + p->off); }

/* ---- Red Alert Enhanced feature defaults ------------------------------------------
   Both games share the same saved Enhanced visual and theater tuning. This layer only
   turns on the Red Alert module's Enhanced HUD, tree models and grass by default; saved
   settings can still opt out. */
static int g_fxRedAlert = 0;

FX_STATE_MAYBE_UNUSED static void fx_red_alert_dials(int on) { g_fxRedAlert = on ? 1 : 0; }

static void fx_red_alert_layer(FxState* s)
{
    /* Classic/N64 rows remain off; later saved Enhanced choices remain editable. */
    s->new_hud = 1;
    s->tree3d = 1;
    s->grass = 1;
}

/* ---- Defaults --------------------------------------------------------------------
   Enhanced visual defaults are sourced from the dated F5 save included below.
   Both games use that shared tuned picture; machine/display choices remain local. */
static void fx_defaults(FxState* s)
{
    /* The include is generated from docs/tuning/opencnc3d-fx-2026-10-22-1351.cfg;
       machine/display controls and per-mode rows remain outside the shared defaults. */
    memset(s, 0, sizeof *s);
    s->ns_bright=s->ns_contrast=s->ns_gamma=s->ns_sat=1.0f;
    s->experimental_height = 0.42321f;
    s->experimental_relief_mode  = 0.0f;
    s->experimental_relief_steps = 15.78571f;

    /* 1. BILINEAR IS ON, on both platforms, and it is one default rather than two.
          it belongs in Enhanced mode, and a PER-PLATFORM default is what caused
          the divergence in the first place, so there is exactly one answer here.
          Note this does not filter anything by itself: fx_filter_on starts at 0 and
          only fx_filter_set moves it, so the bare renderer still samples point-wise
          and the pixel gates measure what they always did. */
    s->bilinear = 1;
    /* VERTICAL SYNC OFF BY DEFAULT. On, a frame that misses one refresh of a 60 Hz screen
       waits for the next whole one, so a frame of 18 ms shows as 30 FPS; measured on
       Windows players' machines, that cliff turned frames of 17 to 20 ms into a steady 28
       to 30 FPS, which they reported as the game being slow. Off, the counter shows the
       real rate and the picture may tear. On and adaptive (on while fast enough, off when
       a frame is late) stay one click away in Options, Advanced. */
    s->vsync = 0.0f;

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

    /* THE ENHANCED TEXTURES by project decision: Enhanced Visuals draws the 1995 tile
       art remastered at 30 texels a cell (it drew the 1995 art itself before). A theater
       with no pack falls back to the 1995 art at draw time. Harmless to the gates for
       exactly the reason bilinear's note above gives -- the value is a preference and
       fx_texset_on is the switch, and only the Enhanced path and the Visuals dialog move
       it. Classic keeps the 1995 art. */
    s->texset = (float)FX_TEX_ENH;
    s->clear_saturation = 0.42857f;
    s->sand_brightness = s->sand_contrast = 1.0f;
    s->unified_lighting = 1;
    s->sun_energy = s->sky_energy = 1.0f;
    s->terrain_ambient = 0.55f;
    s->shadow_black_limit = 0.035f;
    s->object_shadow_fill = 0.30f;
    s->etree_shadow_fill = 0.25f;
    s->grass_terrain_red = s->grass_terrain_green = s->grass_terrain_blue = 1.0f;
    s->desert_terrain_red = s->desert_terrain_green = s->desert_terrain_blue = 1.0f;
    s->sand_terrain_red = s->sand_terrain_green = s->sand_terrain_blue = 1.0f;
    s->snow_terrain_red = s->snow_terrain_green = s->snow_terrain_blue = 1.0f;

    s->terrain_relief = 1;
    s->terrain_relief_height = 0.32f;
    s->sand_saturation = 0.84375f;
    s->snow_brightness = s->snow_contrast = s->snow_saturation = 1.0f;

    /* The 1995 sprites, which is what this project has drawn since dosinfantry.pack
       landed. Changing the default would change every existing screenshot. */
    s->infset = (float)FX_INF_DOS;

    /* The tabbed Enhanced HUD uses the largest whole-number zoom that fits. The old
       half-size default reduced its eight-row housing to native size even at 1440p.
       Players can still choose the smaller setting in Visuals. This preference reaches
       the sidebar through Enhanced and Visuals; bare measurement runs are unchanged. */
    s->ui_scale = 0.0f;
    s->display_mode = 1.0f;
    s->res_w = 0.0f;
    s->res_h = 0.0f;
    s->perspective = (float)FX_PERSP_CLASSIC;
    /* THE ISOMETRIC DEFAULTS are the director's own, tuned on the F5 panel and sent
       back: a 16-degree turn to the left, a fixed 53-degree tilt, a
       48-degree field and 1.1x the distance. The first picture was -45 / 0 / 50 / 1. */
    s->iso_yaw = 16.0f; s->iso_pitch = 53.0f; s->iso_fov = 48.0f; s->iso_dist = 1.10f;
    s->cursor_scale = 0.4f;
    s->debug = 0.0f;            /* a workshop control: never on unless asked for */
    s->inf_scale_exp = 1.09821f;
    s->inf_scale = 0.76786f;      /* a quarter smaller, by request */

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
    s->sun_az = 67.50000f; s->sun_el = 27.28572f;
    s->shadow_strength = 0.86607f;
    s->shadow_soft = 1.35714f;
    s->shadow_bias = 0.002f;
    s->shadow_span = 1.45714f;
    s->shadow_ox = 11.57143f; s->shadow_oz = 7.71428f;
    s->shadow_res = 4096.0f;
    s->shadow_r = 0.49107f; s->shadow_g = 0.58000f; s->shadow_b = 0.68750f;
    s->shadow_terrain = 1;
    s->terrain_shadow_strength = 0.6875f;
    s->terrain_brightness = 0.91071f; s->terrain_contrast = 1.03571f;
    /* THE SMOOTH GROUND, on: the per-corner normal, a 1.5-texel normal
       offset, a penumbra of 0.08 cells (about five texels at the default box), every
       cell still casting. Tuned on the SCB05EA cliff; docs/CHANGELOG.md has the why. */
    s->terrain_normals = 1;
    s->sun_lambert = 1;
    s->sun_floor = 0.23214f;
    /* 0.30 of a cell is roughly the clearance under a tracked hull, which is the gap
       this exists to darken. Eight steps over that is a sample every four centimetres
       of world at the shipped cell size. The thickness is twice the reach, which is
       loose on purpose: too tight and a blocker seen at a grazing angle is missed. */
    s->contact_on       = 1;
    s->contact_len      = 0.13705f;
    s->contact_steps    = 3.0f;
    s->contact_thick    = 0.33973f;
    s->contact_bias     = 0.01786f;
    s->contact_strength = 0.60714f;
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

       THE STRENGTH IS THE TUNED PANEL'S 0.473 (docs/tuning/opencnc3d-fx.cfg), chosen by
       eye in play. It is the stronger of the two settings tried: at 0.473 the cloud
       takes about 28 levels out of the shore and can read as rectangular joins between
       templates where it crosses one, which is why it once stood at 0.16, where it
       stays visible without disguising the ground material. cloud_scale is 20, which
       is the BOTTOM OF ITS RANGE -- one mass is then about two and a half cells, so
       this is fine tonal variation and not the drifting slabs the first draft aimed
       at. If it is ever wanted finer still, the range is what has to move, not this
       number; see the row in FX_PARAMS. */
    s->cloud_on = 1;
    s->cloud_strength = 0.30357f;
    s->cloud_cover = 0.34800f;
    s->cloud_sharp = 0.03600f;
    s->cloud_scale = 20.00000f;
    s->cloud_speed = 0.57100f;
    s->cloud_rot = 67.50000f;
    s->cloud_height = 12.00000f;
    s->fog_fx = 0; s->sand_fx = 1;
    s->fog_density = 0.05357f; s->fog_height = 1.46964f; s->fog_scale = 2.00000f;
    s->fog_speed = 0.57143f; s->fog_wind = 64.28571f; s->fog_light = 4.50000f;
    s->sand_density = 0.20536f; s->sand_height = 0.8f; s->sand_scale = 10.0f;
    s->sand_speed = 1.85714f; s->sand_wind = 80.35714f; s->sand_light = 5.50000f;


    /* The sea, tuned through the panel in play: deeper and darker open water, a wider
       shore fade and rippled refraction. On in Enhanced the way the clouds are.
       Water reflections are retired; old preset values cannot restore them. */
    s->water_fx = 1;
    s->water_swash = 1;
    s->water_swash_hold = 0.0f;
    s->water_swash_opacity = 1.0f;
    s->water_align_length = 0.35f;
    s->water_align_blend = 2.15f;
    s->water_flow = 0.35000f;
    s->water_flow_rate = 0.25000f;
    s->water_current = 67.50000f;
    s->water_foam = 0.00000f;
    s->water_foam_width = 0.35714f;
    s->water_wave_len = 0.50000f;
    s->water_wave_speed = 0.35000f;
    s->water_shallow = 0.47321f;
    s->water_deep = 0.39286f;
    s->water_edge = 0.53571f;
    s->water_fade = 0.28571f;
    s->water_fade_width = 0.85268f;
    s->water_foam_cross = 0.13393f;
    s->water_pan = 0.35000f;
    s->water_river_width = 3.00000f;
    s->water_river_speed = 1.50000f;
    s->water_bump = 0.26786f;
    s->water_spec = 1.04464f;
    s->water_reflection_yaw = 0.0f;
    s->water_refract = 0.02036f;
    s->water_natural = 0.84821f;
    s->water_dark = 0.29464f;
    s->water_wake = 0.03571f;
    s->water_wake_width = 1.28214f;
    s->water_wake_sim = 1;
    s->water_wake_bump = 1.20000f;
    s->water_wake_ring = 0.12000f;
    s->water_wake_speed = 0.02000f;

    s->bloom_on = 1;
    s->bloom_threshold = 0.68000f; s->bloom_knee = 0.15000f;
    s->bloom_intensity = 0.92857f;
    s->explosion_bloom = 0.30f;
    s->muzzle_bloom = 0.25f; s->muzzle_flash_gain = 0.60f; s->muzzle_light_gain = 0.35f; s->bloom_radius = 1.93750f; s->bloom_passes = 4.0f;

    s->ssao_on = 1;
    s->ssao_radius = 0.20670f; s->ssao_intensity = 0.60714f;
    s->ssao_bias = 0.01384f;   s->ssao_power = 1.31250f; s->ssao_samples = 18.0f;
    s->ssao_ground = 0.5f;
    /* THE AUTHORED HULLS' OWN OCCLUSION STRENGTH, AND IT IS 1 BECAUSE THAT IS WHAT A
       HULL ALREADY HAD. Before the object class existed a vehicle fell into the "no
       normal" case, which takes the occlusion at full strength; anything else here
       would be a change to how vehicles are shaded smuggled in beside a change to
       which normal is used, and those are two different decisions. Measured on four
       Mammoths: at the foliage figure the occlusion moved the hull 11.2 levels against
       14.6 before, which is the pass quietly getting weaker for no stated reason. */
    s->ssao_object = 1.0f;

    /* ON BY DEFAULT again: the panel saved in play switched it back on once the
       Tiberium field's light was rebuilt as a map-wide glow, at the dials below. */
    s->lights_on = 1;
    s->light_intensity = 0.96429f; s->light_radius = 2.87500f;
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
    s->light_tiberium = 1;   /* on, as the panel saved in play has it */
    s->tib_light = 0.21429f; s->tib_light_spread = 2.25223f; s->tib_light_height = 0.80f;
    s->vlight_on = 1; s->vlight_shadows = 1;
    /* THE LAMPS AS THE LAB DRAWS THEM: every dial at 1, the strength, colour and reach
       being each lamp's own in its export (the lab's gain of 2.5 is carried in full). The
       lamps were retuned in the lab for the game's light, so nothing is scaled again here;
       the dials are for a player's taste. */
    s->vlight_head = 1.0f; s->vlight_tail = 1.0f; s->vlight_cone = 1.0f; s->vlight_range = 1.0f;
    s->vlight_glow = 1.0f;
    /* the specular maps as tuned in play over the remastered vehicles and buildings */
    s->vspec_on = 1; s->vspec_sun = 1.0f; s->vspec_sky = 1.0f; s->vspec_gloss = 1.0f;

    s->grade_on = 1;
    s->exposure = -0.10714f; s->contrast = 1.22321f; s->saturation = 1.12500f;
    s->temperature = 0.08929f; s->tint = 0.0f; s->lift = -0.03571f; s->gain = 1.55804f;

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
    s->tib3d_glow = 0.37500f;
    /* The crystals turned from the art's own 105.1 degrees toward yellow-green and a
       little richer, as tuned in the panel; 105.1 and 1 give the art untouched. */
    s->tib_hue = 73.92857f; s->tib_sat = 1.16071f;
    s->tibfog_on = 1;
    s->tibfog_density = 0.26786f; s->tibfog_height = 0.85089f; s->tibfog_spread = 2.91964f;
    s->tibfog_glow = 1.23214f; s->tibfog_ambient = 0.02679f;
    s->tibfog_wisps = 0.75f; s->tibfog_drift = 0.37500f; s->tibfog_steps = 16.0f;
    s->tib3d_ground = 0.17411f;
    s->tib3d_pulse = 0.63393f;
    s->tib3d_growtime = 0.8f;
    s->tib3d_decal = 0;

    /* 12c. LEAFY TREES, OFF in the saved Enhanced tuning. The height is judged against a unit,
            the way the crystals are. The wind is deliberately gentle: a tree that waves
            is a tree nobody stops noticing. */
    s->tree3d = 1; /* accepted Enhanced tree models; imported tree pack retired */
    s->etree_height=1.28929f;s->etree_width=1.13839f;s->etree_vary=0.09643f;s->etree_lean=0.0f;s->etree_gain=0.84821f;
    /* 12d. REMASTERED EFFECTS, ON under Enhanced, graded as painted. */
    s->efx_enh = 1;
    s->efx_bright = 1.0f; s->efx_contrast = 1.0f; s->efx_sat = 1.0f;
    s->efx_gamma = s->efx_red = s->efx_green = s->efx_blue = 1.0f;
    s->inf_bright = s->inf_contrast = s->inf_gamma = s->inf_sat = 1.0f;
    s->inf_red = s->inf_green = s->inf_blue = 1.0f;
    /* 12d. REMASTERED BUILDING APRONS, ON under Enhanced. */
    s->bib_enh = 1;
    s->bib_enh_strength = 0.65f;
    s->bib_enh_cover = 0.35f;
    s->decal_enh = 1;
    s->tree3d_size = 1.73125f;
    s->tree3d_wind = 0.04107f;
    s->tree3d_wind_speed = 0.12902f;
    s->tree3d_wind_dir = 183.21429f;
    s->tree3d_vary = 0.22f;
    s->tree3d_burn = 1.0f;
    s->tree3d_lean = 0.23929f;
    s->tree3d_ao = 0.82143f;
    s->tree3d_trans = 0.62946f;
    s->tree3d_sheen = 0.75893f;
    s->tree3d_ambient = 1.27232f;
    s->tree3d_flutter = 0.07143f;
    s->tree3d_wrap = 1.23214f;
    s->tree3d_ao_floor = 0.63393f;
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
    /* THE DISTANCE FOG: on, as tuned in the F5 panel. Temperate is a thin soft
       grey-green, the damp air over grass (a bluer grey read as frost), starting two
       cells past the middle of the view and thickening slowly; Desert a thicker warm
       pale sand, the dust over a dry plain. */
    s->dfog_on = 1;
    s->dfog_t_amount = 0.33036f; s->dfog_t_start = 2.0f; s->dfog_t_depth = 22.24107f;
    s->dfog_t_r = 0.70f; s->dfog_t_g = 0.74107f; s->dfog_t_b = 0.77f;
    s->dfog_d_amount = 0.31250f; s->dfog_d_start = 1.14286f; s->dfog_d_depth = 20.84822f;
    s->dfog_d_r = 0.83036f; s->dfog_d_g = 0.76f; s->dfog_d_b = 0.62f;
    s->tree3d_anim = 2.09821f;
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
    s->grass = 1; /* On by default for Enhanced (the tuned file's); Classic/N64 mode rows remain off. */
    s->grass_density = 121.14286f;
    s->grass_density_max = 83.0f;
    s->grass_height = 0.13607f;
    s->grass_width = 0.03286f;
    s->grass_taper = 0.33929f;
    s->grass_vary = 0.36562f;
    s->grass_lean = 0.20536f;
    s->grass_wind = 0.28f;
    s->grass_wind_rate = 1.00f;
    s->grass_gust_len = 9.2f;
    s->grass_arc = 0.90f;
    s->grass_shiver = 0.35f;
    s->grass_tint = 0.57143f;
    s->grass_r = 0.42f; s->grass_g = 0.52f; s->grass_b = 0.24f;
    s->grass_patch = 0.49286f;
    s->grass_patch_len = 14.89286f;
    s->grass_root_fade = 0.22f;
    s->grass_root_alpha = 0.06f;
    s->grass_edge = 0.30f;
    s->grass_ao = 0.55f;
    s->grass_tip = 0.40714f;
    s->grass_gain = 0.99821f;
    s->grass_fade = 11.30357f;
    /* THE THEATRE GATE ADMITS TEMPERATE AND WINTER AND NOTHING ELSE, which is a
       decision about what the game should look like rather than a threshold. Measured
       green excess of each bank's CLEAR1 mean over neutral: TEMPERAT +0.1723, WINTER
       +0.0513, SAND +0.0187, SNOW -0.0111, DESERT -0.0182. A gate at 0.035 sits between
       winter and sand with room on both sides: it clears winter by a factor of 1.5 and
       turns sand away by 1.9. Desert stays bare on purpose. Its blades would take the
       sand's own colour, because a blade is mostly the ground it grows out of, so
       admitting it draws a quarter of a million cards that change the picture by an
       average of six levels a pixel and read as texture rather than as grass. */
    s->grass_theater_cut = 0.03929f;
    s->grass_colour_sd = 1.0f;
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
    /* The two clocks of a vehicle driving over grass: bent flat in under a third of a
       second, then faded out over four, so the swath behind a moving tank reads as
       trampled grass going away rather than grass that was never there. */
    s->crush_flatten = 0.30f;
    s->crush_fade = 4.00f;
    /* A structure going up clears three cells round itself, on the same two clocks. */
    s->crush_bld_ring = 3.00f;

    /* 12f. THE GROUND TRACKS, on by default because they are part of what Enhanced
       looks like rather than weather somebody switches on. Note this block is reached
       only through the Enhanced tier switch, so every gate that renders from these
       defaults still renders the shipped picture: `enabled` is 0 here and the feature
       asks that question first.

       THE TWO WIDTHS ARE NOT THE CRUSH FIELD'S NUMBERS, and the difference is real
       rather than drift. crush_band is 0.059 of the beam, which is a hair over a
       millimetre of ground: it is that small because the field stamp has a one-texel
       floor under it and the floor is what actually sets the width there. A decal has
       no such floor, so it has to be told the width it wants. These are a tank's own
       proportions instead: the tracks sit near the outside of the hull and each one is
       about a quarter of the beam. */
    s->tmark_on    = 1;
    s->tmark_life  = 40.0f;
    s->tmark_dist  = 12.0f;
    s->tmark_dark  = 0.80f;
    s->tmark_gauge = 0.75f;
    s->tmark_band  = 0.26f;
    s->tmark_cleat = 0.22f;
    /* A MULTIPLY, so these are fractions of the ground rather than a paint: under a
       full cleat print at full strength the ground keeps 1 - 0.8*(1 - 0.45) = 0.56 of
       its red and 0.52 of its blue. Just over half, and a shade warmer than it was. */
    s->tmark_r     = 0.45f;
    s->tmark_g     = 0.41f;
    s->tmark_b     = 0.35f;
    s->tmark_blood = 10.0f;
    /* THE AUTHORED MODELS. On, because a .c3m has to be installed before any of this
       draws anything at all -- the switch decides what happens when one IS there, and
       an art set the player has gone to the trouble of installing should appear.
       The light is aimed at the sun the ground already uses rather than at a number
       that makes one render look good. The values below are the tuned panel's
       (docs/tuning/opencnc3d-fx.cfg): a slightly softer sun with a strong sky fill, gentler
       normals and ambient occlusion, as set by eye in play against the remastered
       buildings, vehicles and soldiers. The soldiers stand at 1.05 of the model's own
       height. */
    s->m3_on       = 1;
    s->m3inf_on    = 1;
    s->einf_on     = 1;
    s->einf_scale  = 0.96429f;
    s->einf_corpse_keep = 1;
    s->einf_corpse_max  = 200.00000f;
    s->einf_corpse_dark = 0.5f;
    s->einf_ragdoll = 1;
    s->einf_blood   = 1;
    s->einf_flash   = 1;
    s->einf_casings = 1;
    s->einf_gibs    = 1;
    s->einf_spray   = 1;
    /* SOLDIER_LIGHT_DEFAULTS: see the measurement at m3inf_sun. */
    s->m3inf_sun    = 0.30357f;
    s->m3inf_fill   = 0.16071f;
    s->m3inf_paint  = 0.55f;
    s->m3inf_shadow = 1;
    s->m3inf_runrate = 1.0f;     /* the gait blend as measured; see the field */
    s->freeinf_on  = 1;
    s->uvfx_on      = 1;
    s->uvfx_scale   = 0.33f;
    s->uvfx_sun     = 1.0f;
    s->uvfx_scatter = 0.5f;
    s->uvfx_gunsize = 3.0f;
    s->uvfx_gamesun = 1;
    s->curgreen_on    = 1;
    s->curgreen_scale = 0.7f;
    s->ecur_on        = 1;
    s->ecur_scale     = 0.75f;
    /* THE SCATTERED FOLIAGE. First guesses, made to be tuned on the panel; the block
       between the two FOLIAGE DEFAULTS markers is what tools/fol_defaults.py rewrites
       from a saved preset, so keep one assignment per line and nothing else in it. */
    /* FOLIAGE DEFAULTS BEGIN */
    s->fol_on          = 1;
    s->fol_shadows     = 0;
    s->fol_grass_shadows = 0;
    s->fol_bush_amt    = 0.0f;
    s->fol_grass_amt   = 0.35714f;
    s->fol_bush_size   = 0.325f;
    s->fol_grass_size  = 0.245f;
    s->fol_vary        = 0.22857f;
    s->fol_cluster     = 0.78571f;
    s->fol_cluster_len = 12.34821f;
    s->fol_margin      = 0.0f;
    s->fol_bush_mix    = 0.0f;
    s->fol_g1          = 1.0f;
    s->fol_g2          = 0.32143f;
    s->fol_g3          = 0.44643f;
    s->fol_tilt        = 0.08f;
    s->fol_wind        = 0.17679f;
    s->fol_flutter     = 0.00357f;
    s->fol_gain        = 0.1f;
    s->fol_grass_gain  = 0.38839f;
    s->fol_seed        = 11.0f;
    s->fol_bush_detail = 1.0f;
    s->fol_crush       = 1;
    s->fol_crush_r     = 0.25f;
    s->fol_flat_time   = 0.3f;
    s->fol_fade_time   = 4.0f;
    /* FOLIAGE DEFAULTS END */
    /* THE ROCKS. First guesses, to be tuned on the panel. */
    s->rock_on       = 1;
    s->rock_shadows  = 1;
    s->rock_physics  = 1;
    s->rock_amt      = 0.66964f;
    s->rock_spread   = 1.5f;
    s->rock_falloff  = 2.13393f;
    s->rock_size     = 0.30f;
    s->rock_vary     = 0.45f;
    s->rock_shrink   = 0.5f;
    s->rock_detail   = 0.61607f;
    s->rock_c1       = 0.625f;
    s->rock_c2       = 0.07143f;
    s->rock_c3       = 1.0f;
    s->rock_sink     = 0.40625f;
    s->rock_margin   = 0.15f;
    s->rock_gain     = 0.25268f;
    s->rock_seed     = 39.0f;
    s->rock_push     = 1.4f;
    s->rock_mass     = 1.2f;
    s->rock_friction = 0.6f;
    s->rock_bounce   = 0.2f;
    s->rock_gravity  = 12.0f;
    s->rock_pulse    = 3.0f;
    s->rock_pulse_r  = 3.0f;
    s->mottle_on         = 1;
    s->mottle_strength   = 0.09821f;
    /* Low, because a patch now covers up to sixteen cells rather than one and patches
       overlap on purpose. This is the share of cells that ANCHOR one, not the share of
       ground that ends up carrying any. */
    s->mottle_density    = 0.05357f;
    /* ON, and it does nothing at all without a sky pack installed: with none the shader
       takes the hemisphere branch, which is the picture this had before. */
    s->m3_env      = 1;
    /* ON, and it does nothing at all without a sky pack installed: with none the shader
       takes the hemisphere branch, which is the picture this had before. */
    s->m3_env      = 1;
    s->m3_sun      = 0.69643f;
    s->m3_ambient  = 3.00000f;
    s->m3_normal   = 0.76786f;
    s->m3_ao       = 0.51786f;
    s->m3_roughmin = 0.11607f;
    s->m3_specamb  = 0.76786f;
    /* ON, because the chain it replaces could not represent the light the authored
       models already produce: their shader computes values well past 1 and then had
       nowhere to put them. A driver with no float target falls back on its own and the
       picture is the old one. */
    /* OFF BY DEFAULT, and that is a deliberate difference from the other three. The
       other three change how the existing picture is presented; this one changes what
       the art IS, replacing the console's own lighting on every vehicle and building in
       the game. That is a decision about the project, not a rendering improvement, and
       it should be taken by switching it on rather than by not noticing it. */
    s->relight_on    = 0;
    /* CALIBRATED SO THE DEFAULT MOVES THE LIGHT AND NOT THE EXPOSURE. The console's
       baked shade on a surface facing up is around 0.75, and at the shipped sun
       elevation that surface sees N.L of 0.48; 0.95 x 0.48 + 0.28 lands at 0.74. So
       switching this on re-aims the light without re-grading the game, and the dials
       are there for anyone who wants it to do more than that. */
    s->relight_rough = 0.62f;
    s->relight_sun   = 0.95f;
    s->relight_amb   = 0.28f;
    s->relight_floor = 0.16f;
    s->relight_spec  = 0.35f;
    s->relight_clamp = 2.5f;
    s->hdr_on      = 0;
    s->tonemap     = 1.0f;
    /* 0.85, which leaves six sevenths of the range untouched. Measured on the test map:
       at this knee a frame whose brightest pixel is white comes out within one level of
       the eight bit chain's, so the switch buys headroom without buying a new look. */
    s->tone_knee   = 0.85f;
    s->bloom_clamp = 8.875f;
    /* THE STRUCTURES AND VEHICLES GROUPS, as tuned in the panel: structures a little
       brighter, less saturated and less glowing, much deeper in shade and occlusion;
       vehicles darker in the mid-tones, desaturated, less sunlit, more occluded. Neutral
       (every dial at 1) is the old path: see fx_struct_active below. Gates that test a
       group's mechanism set all eight to 1 first. */
    /* F5 tuning accepted 19 Oct 2026: exact saved visual dials; display/debug
       preferences remain per user. See docs/verification/2026-10-19-smooth-turns. */
    s->st_gamma    = 0.94375f;
    s->st_bright   = 0.92857f;
    s->st_contrast = 1.0f;
    s->st_sat      = 0.92857f;
    s->st_shade    = 1.32143f;
    s->st_sun      = 0.60714f;
    s->st_glow     = 0.75f;
    s->st_ao       = 1.60714f;
    s->vh_gamma    = 0.85f;
    s->vh_bright   = 0.76786f;
    s->vh_contrast = 0.89286f;
    s->vh_sat      = 0.83929f;
    s->vh_shade    = 0.875f;
    s->vh_sun      = 0.67857f;
    s->vh_glow     = 0.83036f;
    s->vh_ao       = 1.42857f;

    /* Both games use the same saved Enhanced visual picture. User display and mode
       preferences remain in their own settings files. */
    #include "enhanced_defaults_20261022.inc"
    #include "enhanced_defaults_20261025.inc"
    #include "enhanced_defaults_20261026.inc"

    /* Requested Enhanced defaults, including when importing an older tuning save. */
    s->tree3d = s->tib3d = s->fol_on = s->rock_on = s->mottle_on = 1;

    /* Red Alert feature toggles over the shared Enhanced picture. */
    if (g_fxRedAlert) fx_red_alert_layer(s);

    /* LAST, because it copies the live fields set above into the modes' own entries. */
    fx_mode_defaults(s);
}

static inline float fx_clampf(float v, float lo, float hi)
{ return v < lo ? lo : (v > hi ? hi : v); }

FX_STATE_MAYBE_UNUSED static FxObjectGrade fx_effect_grade(const FxState* s)
{
    const FxObjectGrade g={s->efx_bright,s->efx_contrast,s->efx_exposure,s->efx_gamma,
        s->efx_sat,s->efx_warmth,s->efx_tint,s->efx_red,s->efx_green,s->efx_blue};
    return g;
}
FX_STATE_MAYBE_UNUSED static FxObjectGrade fx_infantry_grade(const FxState* s)
{
    const FxObjectGrade g={s->inf_bright,s->inf_contrast,s->inf_exposure,s->inf_gamma,
        s->inf_sat,s->inf_warmth,s->inf_tint,s->inf_red,s->inf_green,s->inf_blue};
    return g;
}

/* DOES THE LIGHT PASS NEED STRUCTURE CLASSIFICATION? Its eight grade/light dials use
   neutral 1, and the independent self-shadow fill uses neutral 0. Only when one differs
   are cartridge structures drawn into the normal target. */
FX_STATE_MAYBE_UNUSED static int fx_struct_active(const FxState* s)
{
    const float v[8] = { s->st_gamma, s->st_bright, s->st_contrast, s->st_sat,
                         s->st_shade, s->st_sun, s->st_glow, s->st_ao };
    int i;
    /* The independent self-shadow control also needs the structure class mask. */
    if (s->object_shadow_fill > 1e-4f) return 1;
    for (i = 0; i < 8; i++)
        if (fabsf(v[i] - 1.0f) > 1e-4f) return 1;
    return 0;
}

/* The vehicle path, including its fallback class redraw, costs nothing at neutral. */
FX_STATE_MAYBE_UNUSED static int fx_vehicle_active(const FxState* s)
{
    const float v[8] = { s->vh_gamma, s->vh_bright, s->vh_contrast, s->vh_sat,
                         s->vh_shade, s->vh_sun, s->vh_glow, s->vh_ao };
    for (int i = 0; i < 8; i++)
        if (fabsf(v[i] - 1.0f) > 1e-4f) return 1;
    return 0;
}

/* One STRUCTURES dial as the light pass is handed it: exactly 1 when it is within the
   same ten-thousandth, because the shader skips a dial's step only at exactly 1. */
FX_STATE_MAYBE_UNUSED static float fx_struct_dial(float v)
{
    return fabsf(v - 1.0f) > 1e-4f ? v : 1.0f;
}

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

    fprintf(f, "# OpenCNC3D Tier 2 post chain -- tuned values\n"
               "#\n"
               "# Written by the F5 panel. Load with:  opencnc3d --gfx <this file>\n"
               "# Send this file back and the numbers in it become the new defaults in\n"
               "# game/fx_state.h (fx_defaults). A trailing comment marks anything that\n"
               "# has been moved off the default it shipped with.\n"
               "#\n");
    /* Written by every build that has the Enhanced textures: its absence marks a file
       saved when Enhanced's default terrain was the 1995 art (see fx_load). */
    fprintf(f, "enh_textures       1\nenh_tree_toggle    1\nenh_world_detail   1\n");
    if (g_fxRedAlert) fprintf(f, "ra_enhanced_features 1\n");

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

    /* THE GRAPHICS MODES, each one's own settings, under its own name. The mode in use is
       brought up to date from the live fields first, so the three blocks always describe
       what choosing that mode would give. Written as numbers whatever the row's kind, and
       marked against the shipped mode settings the same way the rows above are. */
    {
        FxState t = *s;
        int m, r;
        fx_mode_stash_live(&t);
        fprintf(f, "\n# ---- GRAPHICS MODES (each mode's own Advanced settings)\n");
        for (m = 0; m < FX_MODE_COUNT; m++)
            for (r = 0; r < FXR_COUNT; r++) {
                char key[64];
                const float v = t.mode_row[m][r], dv = d.mode_row[m][r];
                snprintf(key, sizeof key, "mode.%s.%s", FX_MODE_NAME[m], FX_MODE_ROWS[r].key);
                if (fabsf(v - dv) > 1e-6f)
                    fprintf(f, "%-32s %-10.5f  # was %.5f\n", key, v, dv);
                else
                    fprintf(f, "%-32s %.5f\n", key, v);
            }
    }
    fclose(f);
    return 1;
}

/* How many `mode.*` lines the last fx_load applied. Zero means the file predates the
   graphics modes, and the host then works out what its rows meant (fx_mode_migrate). */
static int fx_mode_rows_loaded = 0;

/* One `mode.<name>.<row>` key, or 0 when the key is not one. */
static int fx_mode_load_key(FxState* s, const char* key, const char* val)
{
    int m, r;
    if (strncmp(key, "mode.", 5) != 0) return 0;
    for (m = 0; m < FX_MODE_COUNT; m++) {
        const size_t n = strlen(FX_MODE_NAME[m]);
        if (strncmp(key + 5, FX_MODE_NAME[m], n) != 0 || key[5 + n] != '.') continue;
        for (r = 0; r < FXR_COUNT; r++)
            if (!strcmp(key + 6 + n, FX_MODE_ROWS[r].key)) {
                float v = (float)atof(val);
                if (FX_MODE_ROWS[r].is_bool) v = (v >= 0.5f) ? 1.0f : 0.0f;
                s->mode_row[m][r] = v;
                fx_mode_rows_loaded++;
                return 1;
            }
    }
    return 0;
}

/* A PRESET WRITTEN BEFORE THE GRAPHICS MODES. Its rows are one set, and what they meant
   depends on the switch it was saved under:
     - saved under ENHANCED they are that mode's own, and nothing moves:
       the live fields are already its truth.
     - saved under the former CLASSIC they are not DOS Classic's. The old Classic arm switched the chain
       off and left every tick standing so that choosing Enhanced again gave it back, so
       those rows are the player's ENHANCED settings. They become Enhanced's entry, and
       the live rows take Classic's own, which is what the old build drew.
   Called by the host after a load that carried no `mode.` lines at all. */
FX_STATE_MAYBE_UNUSED static void fx_mode_migrate(FxState* s)
{
    int r;
    if (fx_mode_of(s) != FX_MODE_N64) return;
    for (r = 0; r < FXR_COUNT; r++) {
        s->mode_row[FX_MODE_ENHANCED][r] = fx_mode_live(s, r);
        fx_mode_set_live(s, r, s->mode_row[FX_MODE_N64][r]);
    }
}

/* THE REMASTERED COLLECTION ART IS NO LONGER OFFERED. FX_TEX_REMASTER and FX_INF_REMASTER
   keep their numbers, so FX_TEX_ENH is still 3 in every saved file, but a row that holds
   one is read as its mode's shipped choice: a file saved while the choice was on the
   Advanced page loads as that mode's default, never as art no menu can select or a drop
   list showing a blank. `row` is FXR_COUNT long, a mode_row or the live fields copied out. */
FX_STATE_MAYBE_UNUSED static void fx_art_withdraw(float* row, int mode)
{
    FxState d;
    const int tex = (int)(row[FXR_TEXSET] + 0.5f) == FX_TEX_REMASTER;
    const int inf = (int)(row[FXR_INFSET] + 0.5f) == FX_INF_REMASTER;
    if (!tex && !inf) return;
    if (mode < 0 || mode >= FX_MODE_COUNT) mode = FX_MODE_ENHANCED;
    fx_defaults(&d);
    if (tex) row[FXR_TEXSET] = d.mode_row[mode][FXR_TEXSET];
    if (inf) row[FXR_INFSET] = d.mode_row[mode][FXR_INFSET];
}

/* Saved presets and script probes can still name the retired water controls.
   Accept plain names and mode.<name>.<key> aliases, but never map them to a field. */
static int fx_retired_water_key(const char* key)
{
    if (!strncmp(key, "mode.", 5)) {
        const char* dot = strchr(key + 5, '.');
        if (!dot || dot == key + 5) return 0;
        key = dot + 1;
    }
    return !strcmp(key, "water_reflect") || !strcmp(key, "water_fresnel") ||
           !strcmp(key, "water_reflect_scene");
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
    int saw_n64 = 0, saw_old_classic = 0, saw_dos_classic = 0, saw_enh = 0, saw_tree_toggle = 0;
    int vsync_untouched = 0, saw_einf_mode = 0, saw_swash_mode = 0, saw_ra_features = 0, saw_world_detail = 0;
    int saw_desert_shadow = 0, saw_sand_shadow = 0, saw_snow_shadow = 0;
    fx_mode_rows_loaded = 0;
    while (fgets(line, sizeof line, f)) {
        char* h = strchr(line, '#'); if (h) *h = 0;
        char key[64], val[64];
        if (sscanf(line, "%63s %63s", key, val) != 2) continue;
        /* fx_save marks every value off its default with "# was"; a bare "vsync 1" is the
           old default written out unchanged, not a choice (see below) */
        if (!strcmp(key, "vsync") && !h && atof(val) == 1.0) vsync_untouched = 1;
        if (!strncmp(key, "mode.n64.", 9)) saw_n64 = 1;
        if (!strncmp(key, "mode.classic.", 13)) saw_old_classic = 1;
        if (!strcmp(key, "dos_classic")) saw_dos_classic = 1;
        if (!strcmp(key, "experimental_textures")) { s->experimental_textures = 0; continue; }
        if (!strcmp(key, "enh_textures")) { saw_enh = 1; applied++; continue; }
        if (!strcmp(key, "enh_tree_toggle")) { saw_tree_toggle = 1; applied++; continue; }
        if (!strcmp(key, "enh_world_detail")) { saw_world_detail = 1; applied++; continue; }
        if (!strcmp(key, "ra_enhanced_features")) { saw_ra_features = 1; applied++; continue; }
        /* Retired deliberately: accept older saved presets without restoring either
           a mirrored-world pass or the shader's sky reflection. New saves and the
           panel no longer offer these controls. */
        if (fx_retired_water_key(key)) {
            fprintf(stderr, "FX|preset|retired key '%s' ignored (water reflections removed)\n", key);
            continue;
        }
        if (!strcmp(key, "mode.enhanced.einf_on")) saw_einf_mode = 1;
        if (!strcmp(key, "mode.enhanced.water_swash")) saw_swash_mode = 1;
        if (!strcmp(key, "desert_shadow_strength")) saw_desert_shadow = 1;
        if (!strcmp(key, "sand_shadow_strength")) saw_sand_shadow = 1;
        if (!strcmp(key, "snow_shadow_strength")) saw_snow_shadow = 1;
        if (fx_mode_load_key(s, key, val)) continue;
        if (!strncmp(key, "mode.reengineered.", 18)) continue;
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
    /* Missing theater keys inherit the final legacy value, regardless of row order.
       Save writes all three, so subsequent tuning remains independent. */
    if (!saw_desert_shadow) s->desert_shadow_strength = s->terrain_shadow_strength;
    if (!saw_sand_shadow) s->sand_shadow_strength = s->terrain_shadow_strength;
    if (!saw_snow_shadow) s->snow_shadow_strength = s->terrain_shadow_strength;
    /* VERTICAL SYNC WAS ON BY DEFAULT and every save wrote it out, so a file that says
       "vsync 1" with no "# was" beside it holds the old default, not a choice: it takes
       the new default (off), once. A player who turned it off, or who later chooses on or
       adaptive against the new default, has the "# was" mark and keeps the choice. */
    if (vsync_untouched) {
        FxState defaults; fx_defaults(&defaults);
        s->vsync = defaults.vsync;
        fprintf(stderr, "FX|preset|%s carries the old vertical sync default: it takes the new one (off)\n", path);
    }
    /* Earlier files kept the production soldiers only as a global switch. Preserve
       that exact Enhanced choice when adding its own per-mode row. */
    if (!saw_einf_mode)
        s->mode_row[FX_MODE_ENHANCED][FXR_EINF] = s->einf_on ? 1.0f : 0.0f;
    /* An older preset has neither swash key and inherits the new on default.
       A plain explicit key is also a choice; retain it when no mode row exists. */
    if (!saw_swash_mode)
        s->mode_row[FX_MODE_ENHANCED][FXR_WATER_SWASH] = s->water_swash ? 1.0f : 0.0f;
    /* Old tree3d=0 disabled an unrelated imported pack, not the Enhanced models.
       Migrate once; subsequent saved choices apply to the actual Enhanced trees. */
    if (!saw_tree_toggle) {
        s->tree3d = 1;
        s->mode_row[FX_MODE_ENHANCED][FXR_TREES] = 1.0f;
    }
    s->ss_scale = 1.0f;
    for (int m = 0; m < FX_MODE_COUNT; m++) s->mode_row[m][FXR_SS] = 1.0f;
    /* High dynamic range is retired: a file saved while it shipped on reads, and comes
       back off. */
    s->hdr_on = 0;
    s->ecur_on = 1;
    s->efx_enh = 1;
    /* Presets from the retired fourth look may still carry its hidden switch. */
    s->experimental_textures = 0;
    if (s->dos_classic) s->enabled = 0;
    if (saw_old_classic && !saw_n64 && !saw_dos_classic) {
        memcpy(s->mode_row[FX_MODE_N64], s->mode_row[FX_MODE_CLASSIC], sizeof s->mode_row[FX_MODE_N64]);
        FxState defaults; fx_defaults(&defaults);
        memcpy(s->mode_row[FX_MODE_CLASSIC], defaults.mode_row[FX_MODE_CLASSIC], sizeof s->mode_row[FX_MODE_CLASSIC]);
    }
    /* A FILE FROM BEFORE THE ENHANCED TEXTURES. Such a file saved every row, so its
       Enhanced terrain reads "the 1995 art" whether a player chose that or it was simply
       the default then, and read as it stands it would keep the new textures off screen
       for everyone who ever saved. It was the default: Enhanced's DOS becomes Enhanced's
       new textures, once. The next save writes enh_textures, so a player who then picks
       the 1995 art keeps it. */
    if (!saw_enh) {
        if ((int)(s->mode_row[FX_MODE_ENHANCED][FXR_TEXSET] + 0.5f) == FX_TEX_DOS)
            s->mode_row[FX_MODE_ENHANCED][FXR_TEXSET] = (float)FX_TEX_ENH;
        if (fx_mode_of(s) == FX_MODE_ENHANCED && (int)(s->texset + 0.5f) == FX_TEX_DOS)
            s->texset = (float)FX_TEX_ENH;
        fprintf(stderr, "FX|preset|%s predates the Enhanced textures: Enhanced's 1995 terrain "
                        "default becomes them\n", path);
    }
    /* AND THE WITHDRAWN REMASTERED ART becomes each mode's default, in every mode's row
       and in the live fields, which are the mode in use's. */
    {
        int m, r;
        float live[FXR_COUNT];
        for (m = 0; m < FX_MODE_COUNT; m++)
            fx_art_withdraw(s->mode_row[m], m);
        for (r = 0; r < FXR_COUNT; r++) live[r] = fx_mode_live(s, r);
        fx_art_withdraw(live, fx_mode_of(s));
        fx_mode_set_live(s, FXR_TEXSET, live[FXR_TEXSET]);
        fx_mode_set_live(s, FXR_INFSET, live[FXR_INFSET]);
    }
    /* Before RA's Enhanced controls were connected, saves carried disabled features
       and grass's old off default. Adopt the new defaults once, including the inactive
       Enhanced profile. A subsequent save writes the marker and preserves opt-outs. */
    if (g_fxRedAlert && !saw_ra_features) {
        const int rows[] = { FXR_NEWHUD, FXR_TREES, FXR_GRASS };
        for (int i = 0; i < 3; i++) {
            s->mode_row[FX_MODE_ENHANCED][rows[i]] = 1.0f;
            if (fx_mode_of(s) == FX_MODE_ENHANCED) fx_mode_set_live(s, rows[i], 1.0f);
        }
        fprintf(stderr, "FX|preset|RA Enhanced HUD, trees and grass defaults adopted once\n");
    }
    /* Older RA UI greyed these rows and reported visible ore as unchecked.
       Adopt the requested defaults once. New saves preserve deliberate opt-outs. */
    if (!saw_world_detail) {
        const int rows[] = { FXR_TREES, FXR_TIB3D, FXR_FOLIAGE, FXR_ROCKS, FXR_PATCHES };
        for (int i = 0; i < 5; i++) {
            s->mode_row[FX_MODE_ENHANCED][rows[i]] = 1.0f;
            if (fx_mode_of(s) == FX_MODE_ENHANCED) fx_mode_set_live(s, rows[i], 1.0f);
        }
        fprintf(stderr, "FX|preset|Enhanced trees, resources, bushes, rocks and patches defaults adopted once\n");
    }
    fprintf(stderr, "FX|preset|%s: %d applied, %d unknown\n", path, applied, unknown);
    return 1;
}

#endif /* OPENCNC3D_FX_STATE_H */
