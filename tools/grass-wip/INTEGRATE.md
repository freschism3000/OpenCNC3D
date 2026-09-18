# Landing the grass: what a human changes in the repository

> **EXECUTED 9 Sep 2026. This sheet is now a record, not an instruction.** The three headers
> are in `game/`, all seven insertion points are in `game/cnc_eyes.cpp`, section 2's rows are
> in `game/fx_state.h` and G224 is in `game/gates.sh`. Read it for the reasoning, not for the
> steps, and do not follow it again. Four things in it turned out to be wrong and are corrected
> in the code and in the gap register: the panel row count was one stale, so the table holds
> 205 rows and not 204; Appendix A2b renames a constant that no longer exists; section 1.7's
> table says the tick observer is gated on `g_fxActive`, which is only true inside the world
> pass and made that observer record nothing at all; and the halo section 4's gate asserts as
> a twentyfold differential measures 1.10.


Three new headers land beside the renderer's other feature modules:

| file | what it is | copied to |
|---|---|---|
| `grass_bake.h`  | the once-at-load half: mask, geometry, one static VBO, the ground colour | `game/grass_bake.h` |
| `grass_field.h` | the crush field: tread, cover, shroud, one RGBA16F target | `game/grass_field.h` |
| `grass_draw.h`  | the GLSL pair, the LOD, the cull, `grass_draw()` | `game/grass_draw.h` |

They are one translation unit of file-static state, like every other module here, and the
include order is fixed by two `#error` guards in `grass_draw.h:176-181`:

    grass_bake.h  ->  grass_field.h  ->  grass_draw.h

Nothing else in the tree includes them. No `.c` or `.cpp` file is added, so
`tools/win/sources.sh` and `tools/win/check-sources.sh` are untouched (`cnc_eyes.cpp:9032`
already writes down why a header needs no entry there, and why that is a weaker fact than
it looks).

**Read Appendix A before touching `cnc_eyes.cpp`.** Four things inside the three files have
to change first, and two of them are correctness rather than tidiness:

* A1 the skirt band the draw asks for is narrower than the tallest blade, by the height
  variation factor, so the halo contract B exists to prevent still fires on the tallest
  blades along a coast.
* A2 the same band is unbounded above and can exceed what the bake measured, at which
  point every blade in the map fades. It starts happening at `grass_height` 0.1436 with
  a steep camera, and the shipped default is 0.13.
* A3 the crush dials are not yet bridged to `g_fx`, so six of the new panel rows would be
  dead on arrival, and the tick observer still runs with the feature switched off.
* A4 `grass_bake.h:1187` forward-declares a static `grass_field_tick()` that nothing
  defines and nothing calls.

---

## 1. Insertion points in `game/cnc_eyes.cpp`

Seven of them. Line numbers are as the file stands today (32,779 lines); every quoted line
was read out of the file, not remembered.

### 1.1 The include, after `water_mod.h` at line 6979

Everything the three files read is in scope by here: `PackCell` (`:998`), `Pack` (`:1022`),
the play rectangle `g_mapX..g_mapH` (`:1123`), `g_gridW`/`g_gridH` (`:1137`),
`terrain_shade` (`:1418`), `terrain_y` (`:1188`), `ObjKind K_UNIT` (`:2315`), `g_objects`
(`:2510`), `g_engineFrame` (`:2692`), `SMOOTH_JUMP_CELLS` (`:2794`), `smooth_key` (`:2888`),
`g_bldCellNow` (`:3249`), `mesh_for` (forward-declared `:3283`), `n64_pitch` (`:5524`),
`g_viewX0..g_viewZ1` (`:6428`), the cull box `g_cullX0..g_cullZ1` (`:6441`), `cell_in_view`
(`:6467`), `cell_shown` (`:6477`), `terrain_atlas_index` (`:6642`), plus `shroud_corner_vis`
(`shroud_mod.h:567`, included at `:438`) and, through `remaster_tex.h` at `:149`, `TT_TS` /
`TT_PITCH` / `TT_COLS` (`terrain_tiles.h:14-17`) and `edit_land_of` (`edit_tables.h:277`).

Today:

```
6977	   the console's UV constants and the corner helpers it re-uses, so draw_water can hand
6978	   the frame to it. */
6979	#include "water_mod.h"
6980	
6981	/* The sea floor. Opaque, depth write ON, exactly one BOTTOM tile per world cell (the
```

Insert between 6979 and 6980:

```c
#include "water_mod.h"

/* ENHANCED: real grass. Three files, and the order is enforced by #error guards in the
   third: the bake owns the vertex format and the static buffer, the field owns the
   crush/cover/shroud target the blade reads, and the draw owns the program. Included
   here for the same reason water_mod.h is: by this point the pack, the play rectangle,
   the terrain helpers, the cull box and the shroud are all in scope, and nothing below
   needs a forward declaration. Every one of the three is gated on g_fxActive and draws
   nothing under CLASSIC. */
#include "grass_bake.h"
#include "grass_field.h"
#include "grass_draw.h"
```

### 1.2 The one forward declaration, beside the other per-tick observers at line 3283

`grass_crush_note_tick()` is called from `refresh_objects` at line 4043, which is 2,900
lines above the include point. It is declared `static`, not `extern`: this is one
translation unit and every peer helper is static, so an `extern` declaration of a function
that will be defined static is a linkage conflict. Lines 3283-3284 are the existing
precedent, and so are `water_terrain_gl` / `water_terrain_soft` at `:6664-6665`.

Today:

```
3282	static void shatter_step(int frame);
3283	static int  mesh_for(const SimObject& o);      /* both defined far below; the death is */
3284	static int  draw_facing(const SimObject& o);   /* observed here and nowhere else       */
3285	static float alt_lift(const SimObject& o);     /* an aircraft's height above the ground */
```

Insert after 3287 (`shake_note`), at column 0:

```c
/* THE GRASS'S PER-TICK OBSERVER, defined in grass_field.h far below. It records what the
   tick did (the building footprint runs, and one entry per vehicle with its previous
   position and its beam), touches no GL, and is replayed into the crush field by
   grass_crush_sim() in the draw path. The split is the same ruling the tread phase
   already made at :2806-2810: a draw does not happen on every tick under a script, and a
   permanent mark advanced per DRAWN frame makes two --shot runs disagree. */
static void grass_crush_note_tick(void);
```

### 1.3 The per-tick field step, in `refresh_objects` (`:3504`), at line 4043

The once-per-engine-tick block. `bld_footprints_age()` is the right neighbour because the
crush field reads `g_bldCellNow`, which this same block maintains, and because the guard
around it is the heartbeat guard that stops a re-dumped tick being counted twice.

Today:

```
4041	        g_efxLoopCX = g_camX;
4042	        g_efxLoopCZ = g_camZ;
4043	        bld_footprints_age();
4044	        /* THE VANISH SWEEP. Any unit that was in the previous dump and is not in this
```

Insert between 4043 and 4044, at eight spaces of indent:

```c
        bld_footprints_age();
        /* AND THE GRASS'S RECORD OF THE SAME TICK, immediately after the footprints are
           aged so it reads a stable map. No GL here: this only records. */
        grass_crush_note_tick();
```

### 1.4 The world draw pass, in `draw_frame` (`:20266`), after line 20639

`fx_world_begin` runs at `:20288` and `fx_world_end` at `:21053`, so `g_fxActive` is already
correct here. Line 20639 is the end of pass 5, the alpha-tested cutout pass: every hull has
written depth, so a blade depth-tests against it, and the model's own translucent faces
(pass 5b, from 20641) still draw after us.

Today:

```
20638	       mode 0, and they are drawn by the wall SHADOW pass above. */
20639	    draw_walls(WALLPASS_CUTOUT);
20640	
20641	    /* 5b: TRANSLUCENT geometry -- blended, depth tested, no depth write, no alpha test.
```

Insert between 20639 and 20640, at four spaces of indent:

```c
    draw_walls(WALLPASS_CUTOUT);

    /* 5c: THE GRASS. Here and not earlier, because a blade must depth-test against every
       hull that has already written depth; here and not later, because it is BLENDED with
       depth writes OFF and belongs before 5b's translucent faces rather than after them.
       The order of the three calls is the whole of the contract:
         grass_bake_sync   builds or repairs the static field. It is the same lazy guard
                           water_fx_draw applies with water_field_sync (water_mod.h:1464),
                           and it has three outcomes: a new scenario rebuilds everything, a
                           --texset flip rebuilds the COLOUR alone (the mask comes from the
                           cartridge sheet and rebuilding it off the DOS sheet would carpet
                           every road in grass), and anything else is a no-op.
         grass_crush_sim   replays the recorded ticks into the crush field. It binds its
                           own framebuffer, saves and restores fifteen pieces of state and
                           both matrix stacks, and never calls fx_fullscreen_quad.
         grass_draw        the pass itself.
       IF grass_crush_sim RETURNS 0 THE GRASS MUST NOT DRAW. With no cover channel every
       blade overlaps a silhouette, the keystone inverts, and the post chain rings every
       building and every hull with a dark halo one blade tall. grass_draw asks
       grass_crush_ready() on its own account and declines, so this is belt and braces. */
    if (g_fxActive && g_fx.grass) {
        grass_bake_sync(grass_bake_cfg_default());
        if (grass_crush_sim())
            grass_draw();
    }
```

`grass_draw()` reads back and restores nine pieces of state on its own, including
`GL_ALPHA_TEST`, which pass 5 leaves ON at `GL_GREATER 0.5` and which every later cutout
inherits. That is the exact bug the tree pass shipped once, so do not "simplify" the
restore away.

### 1.5 Mission teardown, in `game_shutdown` (`:32233`), at line 32299

The static buffer, the ground colour texture and the crush target all belong to the pack
that is about to be freed, and `g_grassTile` holds offsets into a buffer whose name is
about to be reused.

Today:

```
32297	    tib3d_free();
32298	    doscrate_free();
32299	    shroud_free();
32300	    pack_free(g_pack);
```

Insert between 32298 and 32299, at four spaces of indent:

```c
    doscrate_free();
    /* THE GRASS BELONGS TO THE MISSION. g_grassTile holds byte offsets into a buffer name
       the next mission will reuse, and the ground colour texture was read back off THIS
       pack's atlas. Freed before pack_free for the same reason the shatter cache is: the
       indices it holds are about to stop meaning anything. */
    grass_free();
    grass_crush_free();
    shroud_free();
    pack_free(g_pack);
```

### 1.6 The save-load discontinuity, in `game_load_slot` (`:27664`), at line 27732

A load is a jump in the world's state. Every tread mark and every claimed footprint in the
field belongs to the timeline being abandoned, and `g_engineFrame` restarts, which would
make the recorded tick queue read as a 30,000-tick backlog.

Today:

```
27731	       mission. g_deathFirstFrame is cleared for the same reason: engine frames restart. */
27732	    shatter_reset();
27733	    dmg_reset();
27734	    efx_mesh_reset();
```

Insert between 27732 and 27733, at four spaces of indent:

```c
    shatter_reset();
    /* The tread marks and the claimed footprints belong to the timeline just abandoned,
       and the recorded tick queue is keyed on g_engineFrame, which restarts. Both go.
       The static geometry does NOT: the terrain and the play rectangle are the same, and
       rebuilding a 20 MB buffer on a load would be a visible stall for no reason. */
    grass_crush_reset();
    dmg_reset();
```

### 1.7 Where CLASSIC and the Win98 build must skip it, and where they already do

Nothing new has to be written for either tier. What has to be true is written down here so
that the next person does not "tidy" one of these guards away.

**CLASSIC.** Every entry point is gated, and the gates are already in the three files:

| call | guard | where |
|---|---|---|
| `grass_bake_sync` | the call site's own `g_fxActive && g_fx.grass` (1.4) | this document |
| `grass_crush_sim` | `if (!g_fxActive) return 0; if (!fx_gl_ready) return 0;` | `grass_field.h:1025-1026` |
| `grass_draw` | `if (!g_fxActive \|\| !g_fx.grass \|\| !fx_gl_ready) return false;` | `grass_draw.h:607` |
| `grass_crush_note_tick` | `if (!g_fxActive) { g_crushRectExtra.clear(); return; }`, and it touches no GL at all | `grass_field.h:569`, plus A3b |

`g_fxActive` is set by `fx_world_begin` (`fx_post.h:877`) and cleared at `fx_post.h:858`
whenever `g_fx.enabled` is off or the scene target could not be allocated, so CLASSIC never
reaches a single line of the three files. This is the same shape `draw_tiberium_solid` uses
(`if (!g_fx.enabled || !g_fx.tib3d) return;`).

**The one place a call must NOT go.** `draw_terrain` (`:6666`) and
`fx_draw_terrain_normals` (`:19852`) with `draw_terrain_casters` (`:19892`). Grass is deliberately absent
from the ground-normal pass. That is the keystone: because a blade writes no depth, the
terrain's own triangle still fills the normal buffer at alpha 1.0 behind it, and the post
chain lights the blade exactly as it lights the ground beside it. Adding grass to the
normals pass would give every blade the FOLIAGE alpha of 0.5, which `fx_post.h:333` reads as
"skip the ground's one-sun lambert" and `fx_post.h:375` reads as "take `uAoFoliage` instead
of `ssao_ground`", switching both on and off per pixel over the whole screen at 2 to 11 px
granularity. There is no N dot L anywhere in the grass shader for the same reason.

**Win98 / Tier 1.** `tools/win98/build.sh` builds `tier1/*.c` and never compiles
`game/cnc_eyes.cpp`, so nothing here reaches the Voodoo path at runtime. What matters is
that the WINDOWS 11 cross-compile (`tools/win/build-win.sh`, which does compile
`cnc_eyes.cpp` against `compat/win/OpenGL/gl.h`, a frozen GL 1.1 header plus `glext.h`
tokens only) still builds. Three properties keep that true, and each is a rule rather than
an accident:

1. No new GL entry point is ever NAMED as a function. The five buffer calls go through
   `grass_bake.h`'s own typedefs and `SDL_GL_GetProcAddress` (`grass_bake.h:185-235`), and
   `glBlendEquation` goes through `grass_field.h:147-202` the same way. `compat/win/OpenGL/gl.h:22-30`
   records that forgetting this has already broken only Windows, twice.
2. `glBlendFuncSeparate` and `glBlendEquationSeparate` are never named. `grass_field.h:777-836`
   restores the RGB pair to both and REPORTS the one case where that assumption would
   matter instead of guessing.
3. Every GL token the three files need is `#ifndef`-guarded, because `glext.h` defines some
   of them on Windows and the framework header defines others on macOS.

A Tier 1 gap row belongs in `docs/tier1-gap.md`; see Appendix B.

---

## 2. New entries for `game/fx_state.h`

34 new rows: 28 grass and 6 crush. **The table today holds 170 non-group rows and 17 group
rows; after this it holds 204 and 19.** G36e derives that count from the written preset
rather than from a constant (`gates.sh:2580`), so nothing has to be told the new number, but
a run whose `written=` figure is not 204 has lost a row somewhere.

The F5 panel scrolls (`g_fxpScroll`, `fx_panel.h:222`), so 34 more rows do not overflow it,
and `fxp_after_change` (`fx_panel.h`) needs no new case: `grass_bake_sync` compares every
build-time value it reads on every frame, so a dragged slider is picked up by the next draw.

`grass_trans` from the blade draft is deliberately ABSENT. There is no transmission term in
the fragment stage and no lighting of any kind, so a `grass_trans` row would be a dead
control that still round-trips through the preset file, which this file calls the worst
kind (`fx_state.h:583` and the note above `grass_bake_cfg_default`).

### 2a. Struct fields, after `float tree3d_anim, tree3d_anim_wind;` (`fx_state.h:515`)

```c
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
```

### 2b. `FX_PARAMS` rows, inserted between the `tib3d_decal` row (`fx_state.h:916-917`) and the DEBUG group comment at `:918-920`

The DEBUG group stays last, which `fx_state.h:919-920` says is on purpose.

```c
{ FXP_GROUP, "",  "12d  GRASS  (ours)", 0,0,0,0, "" },
{ FXP_BOOL,  "grass", "real grass", FXO(grass), 0,1,0,
  "stand grass on the ground instead of leaving it to the tile art alone; off is what Classic draws" },
{ FXP_FLOAT, "grass_density", "blades a cell", FXO(grass_density), 0.0f, 128.0f, 0,
  "how many blades stand in one cell. This is the whole per-frame cost of the feature, and it only ever draws fewer blades out of a buffer that was already built" },
{ FXP_STEP,  "grass_density_max", "bake ceiling", FXO(grass_density_max), 1.0f, 128.0f, 1.0f,
  "the most blades a cell the LOAD will generate. Raising it costs memory at load and nothing per frame; moving it rebuilds the whole field" },
{ FXP_FLOAT, "grass_height", "blade height (cells)", FXO(grass_height), 0.03f, 0.30f, 0,
  "how tall a blade stands. 0.13 is about six screen pixels at the default zoom and under four at the furthest. The TOP of this range is baked into the silhouette clearance, so widening the range rebuilds the field and moving the slider does not" },
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
  "how much greener than neutral a theater's CLEAR1 tiles must be before that theater grows grass at all. Raise it to take grass off a bank whose art is not green; a build-time judgement, so moving it rebuilds the field" },
{ FXP_FLOAT, "grass_colour_sd", "mask generosity", FXO(grass_colour_sd), 1.0f, 6.0f, 0,
  "how many deviations below the CLEAR1 mean the per-texel cut sits. Larger takes in more texels, which widens the verge along every road and cliff; a build-time judgement" },
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
```

### 2c. Defaults, appended to `fx_defaults` after `s->tree3d_anim_wind = 0.45f;` (`fx_state.h:1210`)

```c
    /* 12d. GRASS, ON under Enhanced. The height is the one number here that is a
            judgement rather than a measurement, and it is the first question in the
            handover: 0.13 cells is 5.7 screen pixels at the far zoom on a 720p frame,
            and a botanically honest blade is 0.068 cells, which is 1.9 px and cannot
            read at all. */
    s->grass = 1;
    s->grass_density = 96.0f;
    s->grass_density_max = 128.0f;
    s->grass_height = 0.13f;
    s->grass_width = 0.045f;
    s->grass_taper = 0.30f;
    s->grass_vary = 0.45f;
    s->grass_lean = 0.22f;
    s->grass_wind = 0.28f;
    s->grass_wind_rate = 1.00f;
    s->grass_gust_len = 9.00f;
    s->grass_arc = 0.90f;
    s->grass_shiver = 0.35f;
    s->grass_tint = 0.85f;
    s->grass_r = 0.42f; s->grass_g = 0.52f; s->grass_b = 0.24f;
    s->grass_patch = 0.25f;
    s->grass_patch_len = 7.00f;
    s->grass_root_fade = 0.22f;
    s->grass_root_alpha = 0.06f;
    s->grass_edge = 0.30f;
    s->grass_ao = 0.55f;
    s->grass_tip = 0.35f;
    s->grass_gain = 1.00f;
    s->grass_fade = 26.0f;
    s->grass_theater_cut = 0.08f;
    s->grass_colour_sd = 3.00f;

    /* 12e. The two life dials land on a whole number of the field's own quantisation
            step, which is what makes the decay exact in half floats: 60 s asks for two
            ulps a tick and achieves 68.3, and 18 s asks for eight and achieves 17.1. The
            achieved value is printed rather than the asked-for one. */
    s->crush_tread_life = 60.0f;
    s->crush_cover_life = 18.0f;
    s->crush_gauge = 0.78f;
    s->crush_band = 0.22f;
    s->crush_press = 1.00f;
    s->crush_skirt = 1.00f;
```

**Two ranges above are not free choices, and this is the part to get right.** The bake sizes
the silhouette clearance it measures from the TOP of `grass_height`'s range times the top of
`grass_vary`'s (`grass_bake.h:377-391`), and `GrassBakeCfg` carries those two ceilings as
constants 0.30 and 0.45. The rows above use exactly those numbers. If the DEBUG group's
ranges and the bake's constants ever disagree, the disagreement is silent: raise
`grass_height`'s `hi` to 0.35 without touching `grass_bake_cfg_default` and the tallest
blades along every coast start inking over the sea again. See Appendix A2.

---

## 3. The optional buffer entry-point group for `game/fx_gl.h`

### Recommendation: do not add it. `fx_gl.h` needs no change at all.

`grass_bake.h:175-235` already resolves the five buffer pointers in a self-contained group
with its own `g_grassBufReady` flag and its own `g_grassBufWhy` message, and
`grass_draw.h` consumes `gr_glBindBuffer` from there. Nothing else in the tree wants a
buffer object: a repo-wide search for `glGenBuffers`, `glBindBuffer`, `glBufferData`,
`glBufferSubData` and `glDeleteBuffers` over `game/` finds only the three grass files.
Moving the group into `fx_gl.h` would mean deleting `grass_bake.h:175-235` and adding a
second readiness flag to a file whose entire opening argument (`fx_gl.h:26-45`) is that
there is exactly one flag and it is derived from the resolve lines rather than maintained
by hand.

### If a second consumer appears, this is the block, and it is `FX_OPT` only in spirit

`FX_OPT` is not enough on its own: `fx_proc`'s non-required branch prints a line about
shader error text (`fx_gl.h:199`), which is wrong for a buffer call, and it leaves the
caller with no way to ask "did the group resolve". So the group gets its own resolver and
its own flag, and it is deliberately NOT folded into `fx_gl_ready`. Insert after the
framebuffer typedefs at `fx_gl.h:141` and after `fx_gl_load()`'s closing brace at `:253`:

```c
/* ---- Vertex buffer objects: AN OPTIONAL GROUP WITH ITS OWN FLAG ------------------
   NOT FX_REQ, and this is the one place in this file where that matters enough to be
   written down. fx_gl_load's promise (:246) is all-or-nothing: one missing required
   pointer sets fx_gl_missing, fx_gl_ready goes 0, and bloom, shadows, water and the
   trees all stop together. Buffer objects are wanted by exactly one feature. A driver
   that has GLSL and framebuffers but no VBOs must lose that feature and keep the chain,
   so this group resolves separately, reports separately, and is never consulted by
   fx_gl_ready. GL_ARB_vertex_buffer_object is core in 1.5 and the ARB spelling is kept
   for the same reason every other pair here keeps one. */
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER          0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER  0x8893
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW           0x88E4
#endif

typedef void (APIENTRY *FXPFNGENBUFFERS)(GLsizei, GLuint*);
typedef void (APIENTRY *FXPFNDELETEBUFFERS)(GLsizei, const GLuint*);
typedef void (APIENTRY *FXPFNBINDBUFFER)(GLenum, GLuint);
typedef void (APIENTRY *FXPFNBUFFERDATA)(GLenum, FxGLsizeiptr, const void*, GLenum);
typedef void (APIENTRY *FXPFNBUFFERSUBDATA)(GLenum, FxGLsizeiptr, FxGLsizeiptr, const void*);

static FXPFNGENBUFFERS     fx_glGenBuffers;
static FXPFNDELETEBUFFERS  fx_glDeleteBuffers;
static FXPFNBINDBUFFER     fx_glBindBuffer;
static FXPFNBUFFERDATA     fx_glBufferData;
static FXPFNBUFFERSUBDATA  fx_glBufferSubData;

static int  fx_buf_ready = 0;     /* 1 = all five resolved. NOT part of fx_gl_ready.  */
static int  fx_buf_tried = 0;
static char fx_buf_why[160];

/* Resolved on first use, not from fx_gl_load, so that a build with the buffer consumer
   compiled out never pays for the lookup and never prints about it. */
static void* fx_buf_proc(const char* core, const char* alt)
{
    void* p = SDL_GL_GetProcAddress(core);
    if (!p && alt) p = SDL_GL_GetProcAddress(alt);
    if (!p && fx_buf_why[0] == 0)
        snprintf(fx_buf_why, sizeof fx_buf_why, "%s is not exported by this driver", core);
    return p;
}

static int fx_buf_load(void)
{
    if (fx_buf_tried) return fx_buf_ready;
    fx_buf_tried = 1;
    fx_buf_why[0] = 0;

    fx_glGenBuffers    = (FXPFNGENBUFFERS)    fx_buf_proc("glGenBuffers", "glGenBuffersARB");
    fx_glDeleteBuffers = (FXPFNDELETEBUFFERS) fx_buf_proc("glDeleteBuffers", "glDeleteBuffersARB");
    fx_glBindBuffer    = (FXPFNBINDBUFFER)    fx_buf_proc("glBindBuffer", "glBindBufferARB");
    fx_glBufferData    = (FXPFNBUFFERDATA)    fx_buf_proc("glBufferData", "glBufferDataARB");
    fx_glBufferSubData = (FXPFNBUFFERSUBDATA) fx_buf_proc("glBufferSubData", "glBufferSubDataARB");

    fx_buf_ready = (fx_glGenBuffers && fx_glDeleteBuffers && fx_glBindBuffer
                    && fx_glBufferData && fx_glBufferSubData) ? 1 : 0;
    if (!fx_buf_ready) {
        if (fx_buf_why[0] == 0)
            snprintf(fx_buf_why, sizeof fx_buf_why, "vertex buffer objects are absent");
        fprintf(stderr, "FX|buffers|%s; features that need them are off and the rest of "
                        "the chain is unaffected\n", fx_buf_why);
    }
    return fx_buf_ready;
}
```

Adopting it means deleting `grass_bake.h:175-235` and rewriting the five `gr_gl*` call
sites in `grass_bake.h` and the one in `grass_draw.h:754-755` to the `fx_gl*` names. That
is a mechanical rename, but it is a change to a file this document otherwise leaves alone,
so it should be a separate commit from the landing.

---

## 4. The gate for `game/gates.sh`

Append as **G224**, between G223 (ends at `gates.sh:13789`) and the summary block at
`:13791`. Numbering follows G223; the helpers `gbegin` / `grun` / `gshots` / `ok` / `bad`
and `$BASE` / `$GRC` are the file's own (`:51`, `:63-64`, `:126-146`).

**What it photographs.** Four shots of the first GDI mission at one tick, with the chain on:
grass off, grass on, grass on with the silhouette clearance removed, and one repeat run for
the digest. Plus two log-only runs: a `--texset` flip, and the two map sizes that are
refused.

**What it measures, and why each arm cannot pass vacuously.**

1. **It draws, and only in the world.** Changed pixels between grass off and grass on, over
   the whole frame and over the sidebar's rightmost 150 columns separately. The sidebar
   arm is the same inversion G36f already applies to bilinear (`gates.sh:2548-2551`): a pass
   that reached the UI would still pass a test that only asked the world to change.
2. **THE KEYSTONE, measured as a differential.** This is the arm the whole design rests on.
   With `crush_skirt 0` a blade is allowed to overlap a building's and a hull's silhouette;
   there the scene depth belongs to the object, the ground normal buffer's alpha is 0, the
   ground's one-sun lambert is skipped and the occlusion weight doubles from `ssao_ground`
   0.5 to 1.0, which is a dark ring one blade tall around everything solid. So the gate
   counts pixels that got MUCH darker than the grass-off frame, once with the clearance
   removed and once with it at its shipped value, and requires the first to be large and
   the second to be a twentieth of it. The large half is what stops the small half passing
   on a frame with no silhouettes in it.
3. **The mask comes from the cartridge sheet and the colour from the drawn one.** Across a
   run that flips `texset` from 0 to 1 there must be exactly ONE `GRASS|mask|` line and
   ONE `GRASS|vbo|` line, and at least TWO `GRASS|colour|` lines. A second mask line is
   the failure this arm exists for: rebuilt off the 1995 DOS sheet, whose CLEAR1 palette
   matches 73.4% of its own ROAD texels at zero tolerance, the mask carpets every road in
   grass. The blade count in the single `GRASS|vbo|` line is the proof the geometry did
   not move.
4. **Deterministic.** Two runs of the same script produce the same PNG. Grass has a wind
   and a growing crush field, and both are pure functions of `g_engineFrame` plus the
   recorded tick queue, so this localises a G36c failure to the feature rather than to the
   chain.
5. **Both refusals fire on real files in the run folder.** `USER50.pack` is 256x256 and is
   turned away by `GRASS_GRID_MAX` before a texel is read; `USER01.pack` is 128x128 and is
   turned away by `GRASS_VBO_BUDGET` after the mask pass, with the density ceiling that
   would have fitted. Both runs must still exit 0 and still write a frame, because grass is
   the only thing that is allowed to stop.

**What it fails at today.** Every arm. `grass` is not an `FX_PARAMS` key, so `gfx grass 1`
prints `GFX|unknown dial 'grass'` and increments `g_scriptFails` (`cnc_eyes.cpp:25698`), the
run exits non-zero, `$GRC` is non-zero and arm 1 reports `GRC=1`. No `GRASS|` line is
emitted anywhere, so arms 3 and 5 report `mask=0 colour=0 vbo=0` and `refused=0`. The four
shots are byte identical, so arms 1 and 2 report `changed=0` and `halo0=0`, and arm 2's
message says in as many words that a zero there means the gate measured nothing rather than
that the halo is absent.

```sh
# ---------------------------------------------------------------------------
# G224 THE GRASS STANDS, AND IT LEAVES NO HALO.
#
# Under ENHANCED the ground grows real blades: game/grass_bake.h reads WHERE from the
# CARTRIDGE atlas per texel and puts every blade in the map into one static vertex buffer
# built at load and never rewritten; game/grass_field.h keeps what the battle has done to
# the ground; game/grass_draw.h draws it, alpha blended, DEPTH WRITES OFF.
#
# THE DEPTH WRITE IS THE WHOLE DESIGN AND ALSO THE WHOLE RISK. Because a blade leaves no
# depth behind, the terrain's own triangle still fills that pixel of the ground normal
# buffer at alpha 1.0, and the post chain lights the blade exactly as it lights the ground
# beside it: no second lighting model, no N dot L in the grass shader, no grass in the
# normals pass. WHEREVER A BLADE OVERLAPS AN OBJECT SILHOUETTE THAT INVERTS. There the
# scene depth belongs to the object, gn.a is 0, the ground's one-sun lambert is skipped
# (fx_post.h:333) and the occlusion weight doubles from ssao_ground 0.5 to 1.0
# (fx_post.h:375, fx_state.h:1115), which is a dark ring one blade tall around every
# building and every unit in the frame. The fix is geometric and it is a DIAL: crush_skirt
# clears the grass one blade height past every hull and every footprint. So this gate takes
# the halo away and puts it back, and requires both.
#
# FIVE CLAIMS, and each one is built so it cannot pass on an empty frame:
#   (1) it draws, and it stops at the sidebar;
#   (2) removing the clearance produces the halo and restoring it removes the halo. The
#       first half is what makes the second half mean something;
#   (3) a --texset flip repaints the grass and NEVER rebuilds the mask. At zero tolerance
#       the 1995 DOS sheet's CLEAR1 palette already matches 73.4% of its own ROAD texels,
#       so a mask rebuilt from it carpets every road in grass. One mask line, one vbo
#       line, two or more colour lines;
#   (4) two runs of one script give one digest;
#   (5) the two refusals fire on real files: USER50 is 256x256 and USER01 is 128x128, and
#       in both cases grass is the ONLY thing that stops.
# =====================================================================================
cat > /tmp/g224.txt <<'G224EOF'
cam 30 34
tick 30
gfx grass 0
shot shots/g224_off.png
gfx grass 1
shot shots/g224_on.png
gfx crush_skirt 0
shot shots/g224_noskirt.png
gfx crush_skirt 1
quit
G224EOF
cat > /tmp/g224b.txt <<'G224EOF'
cam 30 34
tick 30
gfx grass 1
shot shots/g224_on2.png
quit
G224EOF
# THE TEXSET LEG IS ITS OWN RUN, because it has to see the bake's first line and then a
# flip, and the mask line is printed once per BAKE rather than once per frame.
cat > /tmp/g224t.txt <<'G224EOF'
tick 10
gfx grass 1
shot shots/g224_tex0.png
gfx texset 1
shot shots/g224_tex1.png
gfx texset 0
shot shots/g224_tex2.png
quit
G224EOF
gbegin shots/g224_off.png shots/g224_on.png shots/g224_noskirt.png shots/g224_on2.png \
       shots/g224_tex0.png shots/g224_tex1.png shots/g224_tex2.png \
       shots/g224.log shots/g224b.log shots/g224t.log
grun shots/g224.log  --noshroud --scen SCG01EA --pack SCG01EA.pack $BASE --w 1280 --h 800 \
     --script /tmp/g224.txt --gfx
grun shots/g224b.log --noshroud --scen SCG01EA --pack SCG01EA.pack $BASE --w 1280 --h 800 \
     --script /tmp/g224b.txt --gfx
grun shots/g224t.log --noshroud --scen SCG01EA --pack SCG01EA.pack $BASE --w 1280 --h 800 \
     --script /tmp/g224t.txt --gfx
gshots shots/g224_off.png shots/g224_on.png shots/g224_noskirt.png shots/g224_on2.png

# The bake's own report. skirt= is the band the LOAD measured, in cells; the draw's live
# band is height*(1+vary)/cos(pitch) and must fit inside it, which at the shipped ranges
# is 0.718 cells. A number below that means somebody widened a dial range without moving
# the bake's ceiling, and the halo comes back at the top of the slider.
G224MASK=$(grep -c '^GRASS|mask|'   shots/g224t.log)
G224COL=$(grep -c  '^GRASS|colour|' shots/g224t.log)
G224VBO=$(grep -c  '^GRASS|vbo|'    shots/g224t.log)
G224BLADES=$(sed -n 's/^GRASS|vbo|.*|blades=\([0-9]*\)|.*/\1/p' shots/g224.log | head -1)
G224SKIRT=$(sed -n 's/^GRASS|mask|.*|skirt=\([0-9.]*\) cells/\1/p' shots/g224.log | head -1)
G224DREW=$(grep -c '^GRASS|draw|' shots/g224.log)
G224R="0 0 0 0"
if [ "$GRC" = "0" ]; then
  G224R=$(python3 - <<'PY'
import numpy as np
from PIL import Image
def luma(p):
    a = np.asarray(Image.open(p).convert('RGB')).astype(int)
    return (a[:,:,0]*299 + a[:,:,1]*587 + a[:,:,2]*114) // 1000, a
loff, aoff = luma('shots/g224_off.png')
lon,  aon  = luma('shots/g224_on.png')
lns,  ans  = luma('shots/g224_noskirt.png')
changed  = int((np.abs(aon - aoff).sum(axis=2) > 24).sum())
sidebar  = int((np.abs(aon - aoff)[:, -150:].sum(axis=2) > 24).sum())
# THE HALO IS A LARGE DARKENING, not a change. Grass itself is a mid tone laid over a mid
# tone and moves a ground pixel by tens; losing the ground's sun term and doubling its
# occlusion moves it by far more, which is why the threshold is 60 and not 24.
halo0 = int(((loff - lns) > 60).sum())
halo1 = int(((loff - lon) > 60).sum())
print(changed, sidebar, halo0, halo1)
PY
)
fi
set -- $G224R
G224CHG="$1"; G224SB="$2"; G224H0="$3"; G224H1="$4"
G224SAME=0
if [ -s shots/g224_on.png ] && [ -s shots/g224_on2.png ] \
   && cmp -s shots/g224_on.png shots/g224_on2.png; then G224SAME=1; fi

# THE TWO REFUSALS, on real files in this folder. Grass stops; nothing else does, which is
# what the exit code and the written frame are here to prove.
# gbegin RESETS GRC, so the first three runs' verdict is taken off it FIRST. A gate that let
# a later gbegin overwrite it would report the refusal leg's exit code beside the halo leg's
# numbers, which is the shape of message that sends somebody looking in the wrong place.
G224GRC1="$GRC"
printf 'tick 4\nshot shots/g224_big.png\nquit\n' > /tmp/g224big.txt
gbegin shots/g224_big.png shots/g224_mid.png shots/g224big.log shots/g224mid.log
grun shots/g224big.log --noshroud --scen USER50 --pack USER50.pack $BASE \
     --dir missions/user_maps/ --w 1280 --h 800 --script /tmp/g224big.txt --gfx
G224BIGRC=$?
printf 'tick 4\nshot shots/g224_mid.png\nquit\n' > /tmp/g224mid.txt
grun shots/g224mid.log --noshroud --scen USER01 --pack USER01.pack $BASE \
     --dir missions/user_maps/ --w 1280 --h 800 --script /tmp/g224mid.txt --gfx
G224MIDRC=$?
G224RGRID=$(grep -c 'GRASS|refused|.*GRASS_GRID_MAX' shots/g224big.log)
G224RBUD=$(grep -c  'GRASS|refused|.*MiB' shots/g224mid.log)
G224ALIVE=0
[ -s shots/g224_big.png ] && [ -s shots/g224_mid.png ] \
  && [ "${G224BIGRC:-1}" = "0" ] && [ "${G224MIDRC:-1}" = "0" ] && G224ALIVE=1

if [ "$G224GRC1" = "0" ] && [ "$GRC" = "0" ] && [ "${G224CHG:-0}" -ge 20000 ] && [ "${G224SB:-1}" -eq 0 ] \
   && [ "${G224H0:-0}" -ge 2000 ] && [ "${G224H1:-999999}" -le $((G224H0 / 20)) ] \
   && [ "${G224MASK:-0}" -eq 1 ] && [ "${G224VBO:-0}" -eq 1 ] && [ "${G224COL:-0}" -ge 2 ] \
   && [ "${G224SAME}" = "1" ] && [ "${G224DREW:-0}" -ge 1 ] \
   && [ "${G224RGRID:-0}" -ge 1 ] && [ "${G224RBUD:-0}" -ge 1 ] && [ "$G224ALIVE" = "1" ]; then
  ok "G224 the grass stands and leaves no halo: ${G224BLADES:-?} blades from one static buffer change $G224CHG px and $G224SB of the sidebar; taking the clearance away darkens $G224H0 px around the silhouettes and putting it back leaves $G224H1; a texset flip gives $G224MASK mask, $G224VBO vbo and $G224COL colour builds; two runs one digest; a 256x256 and a 128x128 map are both refused with the frame still drawn; baked skirt ${G224SKIRT:-?} cells"
else
  bad "G224 the grass stands and leaves no halo: changed=${G224CHG}(want >=20000) sidebar=${G224SB}(want 0, else the pass is reaching the UI) halo-with-no-clearance=${G224H0}(want >=2000; ZERO MEANS THIS GATE MEASURED NOTHING, not that the halo is absent -- check the camera actually has a building or a hull in frame) halo-shipped=${G224H1}(want <=$((${G224H0:-0} / 20))) mask-builds=${G224MASK}(want exactly 1; 2 means a texset flip rebuilt the mask off the DOS sheet and every road is now grass) vbo-builds=${G224VBO}(want 1) colour-builds=${G224COL}(want >=2) drew=${G224DREW}(want >=1) same=${G224SAME}(want 1) grid-refusal=${G224RGRID}(want >=1 on USER50) budget-refusal=${G224RBUD}(want >=1 on USER01) chain-alive=${G224ALIVE}(want 1; 0 means a refused map took something other than the grass down with it) skirt=${G224SKIRT:-none}(want >=0.71) GRC=$G224GRC1/$GRC"
fi
```

Two numbers in that gate are thresholds rather than measurements and must be re-read on the
first green run, then written into the comment with the figure they were set from: the
`>=20000` changed-pixel floor and the `>=2000` halo floor. Everything else in it is exact.

---

## 5. What `make-build.sh` must copy

**Nothing.** No change to `game/make-build.sh` at all.

Grass ships no asset. The mask is read from the cartridge atlas the pack already keeps
(`cnc_eyes.cpp:2094-2098`), the colour is read back off the GPU from whichever atlas the
terrain is drawing, and the geometry is generated from an integer hash. There is no
`grass.pack`, so there is no `check_asset` row (`make-build.sh:355-367`) and no baker to run
before the compile.

Two related files, and both are deliberately left alone:

* **`game/build.sh:174-177`, `BUILD_SOURCES`.** This is the content stamp `playable/Editor.app`
  reads to warn that the binary predates its sources. It already omits `water_mod.h`,
  `tree3d_mod.h`, `tib3d_mod.h`, `shroud_mod.h`, `fx_post.h` and about twenty other feature
  headers, so adding three grass headers and not those would make the list inconsistent
  rather than complete. The honest consequence, stated rather than left to be found: editing
  `grass_draw.h` alone will not move the stamp, so Editor.app will not offer a rebuild for
  it. That is the existing behaviour for every module header in the tree, and fixing it is
  a separate job on the whole list.
* **`tools/win/sources.sh` and `tools/win/check-sources.sh`.** These compare the `.c` and
  `.cpp` sets between the two builds. Headers are not in either set, and
  `check-sources.sh` runs first inside `build-win.sh` and would fail the build if they were.

The one thing to do at build time is the one thing that is easy to skip: `build.sh` stages
the binary into `../playable` itself (`build.sh:188-189`), and every gate runs
`../playable/cnc_eyes`. A build that succeeded and was not staged is how a gate passes on
code that is already broken.

---

## 6. The three questions that are not the code's to answer

1. **How tall is a blade, in cells?** A botanically honest blade at this scale is 0.068
   cells, which is 1.9 screen pixels at the far zoom on a 720p frame and cannot read as
   anything but noise. The shipped default is 0.13 cells, which is 5.7 px there and 3.7 px
   at the furthest zoom; 0.20 cells is 5.7 to 11.7 px depending on zoom and resolution, and
   reads as grass at a glance. Everything above 0.13 buys legibility with a taller
   silhouette clearance, and past 0.1436 cells the clearance the load measured stops being
   able to express the one the draw asks for at a steep camera (see Appendix A2). So the
   number is a picture decision with a measured cost attached, and it is the first one.
2. **Does the winter theatre get grass at all?** Its atlas is the 1995 DOS temperate art
   and it fails the green gate: the theatre gate compares the CLEAR1 bank's green fraction
   against neutral, and winter does not clear it. The code's answer today is "no grass on
   that theatre", announced in the log as `GRASS|off|theater ... green excess ... does not
   clear the gate`. The alternatives are to lower `grass_theater_cut` until it passes and
   accept grass the colour of that art, or to leave winter bare, which is defensible because
   snow is what the tile is drawing. This is a call about what winter should look like, not
   about a threshold.
3. **Is the DOS terrain art worth tuning against?** Enhanced ships with the DOS sheet drawn
   by default, and against it the grass mask's signal-to-texture ratio measures 1.6 to 2.3
   where the cartridge sheet measures 7.6 to 10.2. The mask is always read from the
   cartridge sheet, so WHERE the grass grows is unaffected either way; what the DOS sheet's
   lower ratio costs is the COLOUR, which is averaged out of it per cell and is that much
   noisier. The question is whether the default preview art should keep being the DOS sheet
   now that something is reading its colour, or whether the grass tuning session should be
   held on the cartridge sheet and the DOS look accepted as whatever falls out.

---

## Appendix A. Corrections inside the three files, before they land

Five changes. A1 and A2 are correctness, A3 and A3b are dead-control and dead-cost fixes, A4 is tidiness.

### A1. The clearance band the draw asks for is too narrow by the variation factor

`grass_draw.h:696` sets the band from the MEAN blade height:

```c
        fx_set1f(prog, "uSkirt", g_fx.grass_height / cp);
```

but the blade the band has to cover is the TALLEST one, `grass_height * (1 + grass_vary)`,
which is exactly the product `grass_sd_range_of` uses at bake time
(`grass_bake.h:377-391`). At the shipped `grass_vary` of 0.45 the band is 31% too narrow, so
the tallest blades along a coast and along the map rim still ink over the silhouette and
still take the halo. Replace with:

```c
        fx_set1f(prog, "uSkirt", g_fx.grass_height * (1.0f + g_fx.grass_vary) / cp);
```

Measured against the shipped camera (`n64_pitch` 0.92 rad, cos 0.6058): the band goes from
0.215 cells to 0.311 cells, and the bake measured 0.718, so it still fits.

### A2. The same band is unbounded above, and past the bake's saturation the whole field fades

The comment above that line says "the shipped range is 0.78 to 0.92 rad and the isometric
pitch is floored above half the field of view, so cos never approaches it". The floor is
real; there is no CEILING. `n64_pitch()` (`cnc_eyes.cpp:5524-5544`) returns `g_fx.iso_pitch`
whenever the isometric camera is active, and that dial's range is `0.0f, 85.0f` degrees
(`fx_state.h:622`). At 85 degrees the cosine is 0.0872 and hits the 0.20 guard, so
`uSkirt = 5 * grass_height`.

The vertex program's fade is `keep *= smoothstep(0.0, max(uSkirt, 1.0e-4), sd)`
(`grass_draw.h:320`), and `sd` SATURATES at `g_grassSdRange`, which is 0.718 cells on the
shipped ceilings. So once `uSkirt` exceeds 0.718 every blade in the map, including blades
nowhere near an edge, is partially faded:

| `grass_height` | `uSkirt` at the 0.20 guard | alpha of a blade in open ground |
|---|---|---|
| 0.13 (shipped) | 0.650 | 1.000 |
| 0.1436 | 0.718 | 1.000 |
| 0.20 | 1.000 | 0.806 |
| 0.30 (range top) | 1.500 | 0.468 |

A field at 47% alpha with no edge anywhere near it reads as a density bug, not as a skirt
bug, which is why this is worth fixing before anyone tunes on the height slider. Two changes,
and both are wanted:

**A2a, the safety net, one line in `grass_draw.h`.** Never ask for a band wider than the
load measured, because a wider one cannot be honoured and can only dim the map:

```c
        float sk = g_fx.grass_height * (1.0f + g_fx.grass_vary) / cp;
        if (sk > g_grassSdRange) sk = g_grassSdRange;   /* the bake's saturation */
        fx_set1f(prog, "uSkirt", sk);
```

**A2b, the real fix, one constant in `grass_bake.h:309`.** `grass_bake_cfg_default` sets
`c.pitch_max = 0.92f` and cites the cartridge rig's own pitch. The rig is not the only
camera. Raise it to the isometric dial's ceiling so the bake measures far enough that A2a's
clamp rarely bites:

```c
    c.pitch_max = 1.4835f;   /* 85 degrees: the top of the iso_pitch dial's own range */
```

That takes `g_grassSdRange` from 0.718 to 4.991 cells and `g_grassSdCells` from 1 to 5, which
widens `grass_skirt_dist`'s search from 9 rectangle tests to 121. It is paid once at load,
only by cells flagged in `g_grassNearBlock`, and the encoded distance 4.991 * 256 = 1278
still fits a `GLshort` with three decimal orders to spare. `pitch_max` is compared by
`grass_bake_sync`, so changing it is a clean rebuild.

Do A2a unconditionally. Do A2b once the director has answered question 6.1, because it is
the height answer that decides whether the clamp would ever bite.

### A3. Bridge the crush dials to `g_fx`

`grass_field.h` reads only `g_crushDials`, which ships with its own defaults so the file
compiles and runs before `fx_state.h` grows a row. Once section 2 lands, six panel rows
exist that nothing reads. Add at the top of `grass_crush_sim` (`grass_field.h:1023`), after
the four early-out guards and before the size block:

```c
    /* THE ONE PLACE THE PANEL AND THIS FILE MEET. Copied per frame rather than watched,
       because every one of them is cheap and a dial that is read once at boot is a dial
       the panel cannot move. blade_height is the grass's own height dial: the clearance
       is measured in blade heights, so the two cannot be allowed to disagree. */
    g_crushDials.tread_life   = g_fx.crush_tread_life;
    g_crushDials.cover_life   = g_fx.crush_cover_life;
    g_crushDials.gauge        = g_fx.crush_gauge;
    g_crushDials.band         = g_fx.crush_band;
    g_crushDials.press        = g_fx.crush_press;
    g_crushDials.skirt        = g_fx.crush_skirt;
    g_crushDials.blade_height = g_fx.grass_height;
```

### A3b. And add the grass dial to the tick observer's own guard

`grass_crush_note_tick` opens with `if (!g_fxActive) { g_crushRectExtra.clear(); return; }`
(`grass_field.h:569`) and its own comment asks, in as many words, for the feature's dial to
be added there once it exists. Otherwise a player who has turned grass off still pays for a
walk over every object fifteen times a second. Change that line to:

```c
    if (!g_fxActive || !g_fx.grass) { g_crushRectExtra.clear(); return; }
```

The field is correct from the tick after it is switched back on, because the queue's own
backlog cap (`GRASS_CRUSH_MAXSTEPS`, 120 ticks) empties it and raises the overflow flag, and
the draw half then clears the field instead of replaying a partial history. A switched-off
field is a freshly cleared one, which is what makes this safe rather than merely cheap.

### A4. `grass_field_tick()` is declared and never defined

`grass_bake.h:1187` reads:

```c
static bool grass_draw(void);
static void grass_field_tick(void);
```

`grass_draw` is defined by `grass_draw.h:605`. `grass_field_tick` is defined by nothing and
called by nothing: the crush file split that job into `grass_crush_note_tick()` (the tick
half) and `grass_crush_sim()` (the draw half). It is legal C++ while it is never used, and
`game/build.sh:142` passes no `-Wall`, so it will not warn in the real build, but it warns
under `-Wall` and it is a false trail for the next reader. Delete the second line, or replace
it with the pair that does exist:

```c
static bool grass_draw(void);
/* The live half of the field is grass_field.h's, and it is two functions rather than one:
   grass_crush_note_tick() records the tick with no GL, grass_crush_sim() replays it. */
```

### A5. Not a defect, but note it

`grass_bake.h`'s tail note flags the file-name collision with the terrain-colour design
sketch, which was also called `grass_field.h`. That is resolved: the colour build was
absorbed into `grass_bake.h` as `grass_colour_build` (`:951`) and the sketch does not land.
`grass_field.h` as shipped takes `CNC3D_GRASS_CRUSH_FIELD_H` and the `grass_crush_*` /
`g_crush*` prefixes, and `grass_bake.h` takes `CNC3D_GRASS_BAKE_H` and `g_grass*`. No symbol
is defined twice.

---

## Appendix B. The two documentation rows this owes

Per the project's own rule that a known gap is written down as it is found rather than
substituted for silently:

* **`docs/tier1-gap.md`**, a new section in the shape of the "Solid tiberium"
  row at `:345`. Tier 2 is a static VBO, a GLSL 1.20 program with two vertex-stage texture
  fetches, and an RGBA16F render target updated with `GL_MAX` and a reverse-subtract blend.
  The Tier 1 answer is that Win98 draws the cartridge's own ground with no grass, which is
  the picture this project has always shipped there. The vertex texture fetch and the half
  float target are the two things a Glide port could not follow even if `tier1/*.c` grew a
  reader, so this is a deeper gap than the tiberium one and should say so.
* **the gap log**, three entries, all measured rather than suspected: a building pad
  or the editor's height brush moves the ground under baked vertices and there is no
  incremental rebuild path (`grass_bake.h` keeps the mask and coverage arrays and takes a
  destination pointer precisely so that one is a caller and not a rewrite); a texset flip
  reads the same atlas off the GPU twice, once for the grass colour and once in
  `water_field_sync`; and a grid above `GRASS_GRID_MAX` is refused rather than windowed,
  which is a real refusal on `USER50.pack` in the run folder and not a hypothetical one.
