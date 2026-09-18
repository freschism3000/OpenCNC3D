/* ====================================================================================
 *  grass_draw.h -- THE GRASS SHADER PAIR AND THE PER-FRAME DRAW PATH.
 *  ENHANCED / TIER 2 ONLY.  CLASSIC AND THE WIN98 BUILD NEVER REACH A LINE OF IT.
 *
 *  This file owns exactly three things:
 *    1. GRASS_VS and GRASS_FS, GLSL 1.20.
 *    2. grass_draw(), the world-pass draw, and the state it takes and gives back.
 *    3. The LOD cut-off and the per-tile cull.
 *
 *  It owns NOTHING else. The vertex buffer, the index buffer, the tile table, the
 *  position scale and the ground-colour field are built by grass_bake.h; the crush,
 *  cover and shroud field is built by grass_field.h. Both are consumed here by name
 *  and nothing from either is redeclared. Include order is
 *      grass_bake.h  ->  grass_field.h  ->  grass_draw.h
 *  and the two #error guards below enforce it, because the renderer's headers are one
 *  translation unit of file-static state: a second declaration would be a second
 *  object rather than a reference to the first.
 *
 *  ------------------------------------------------------------------------------
 *  THE SCREEN SCALE, WHICH IS WHY EVERYTHING HERE IS AS SMALL AS IT IS
 *
 *  At 1280x720 a cell is 47.06 px across at the far end of the zoom range and
 *  82.35 px at the near end. A 0.13-cell blade, foreshortened by the camera's own
 *  pitch (52.71 deg far, 44.69 deg near), is therefore 3.71 to 7.61 SCREEN PIXELS
 *  tall. A 0.045-cell card is 2.12 to 3.71 px wide face-on, and because the yaw is
 *  baked per blade its average on-screen width is 2/pi of that, 1.35 to 2.36 px.
 *
 *  Every decision below falls out of those four numbers:
 *    two triangles a blade, because nine would be one triangle per pixel of height;
 *    a soft ramp instead of an alpha test, because a hard cut on a 2 px column
 *      crawls as the camera pans and there is no multisample buffer in this context
 *      to resolve it (the GL attributes ask for double buffer, 24-bit depth and
 *      8/8/8 colour and nothing else), and that ramp is sized PER TILE out of these
 *      same pixels rather than as a fraction of the blade, because a ramp narrower
 *      than one sample is the hard cut it was meant to replace;
 *    and no per-blade lighting at all, because a 4 px feature has no light response
 *      worth computing.
 *
 *  ------------------------------------------------------------------------------
 *  THE KEYSTONE:  GRASS WRITES NO DEPTH, SO THE GROUND LIGHTS IT.
 *
 *  The blade is alpha blended with GL_DEPTH_TEST ON and glDepthMask(GL_FALSE), and
 *  no alpha test. Because it writes no depth, the pixel's depth still belongs to the
 *  terrain triangle behind it, so fx_draw_terrain_normals (game/cnc_eyes.cpp:19963)
 *  fills the ground-normal buffer at that pixel with the ground's own corner normal
 *  at alpha 1.0, and the light pass reads ground = 1.0 (game/fx_post.h:233) and
 *  applies the ground's one-sun lambert (game/fx_post.h:333-337) and the ground's own
 *  SSAO weight (game/fx_post.h:375) to the grass exactly as to the bare ground beside
 *  it. That is the whole lighting model.
 *
 *  TWO CONSEQUENCES, BOTH LOAD BEARING:
 *
 *  (a) THERE IS NO N DOT L ANYWHERE IN GRASS_FS. Adding one would multiply two suns
 *      together, which is the bug the tree pass had to be rescued from
 *      (game/cnc_eyes.cpp:8164-8166 records it).
 *
 *  (b) GRASS IS NOT DRAWN INTO fx_draw_terrain_normals. There is deliberately no
 *      grass_draw_normals() in this file. tree3d_draw_normals exists because a tree
 *      writes depth and would otherwise punch a hole in the buffer; grass cannot,
 *      because it never wins the depth write.
 *
 *  THE KEYSTONE INVERTS WHEREVER A BLADE OVERLAPS A SILHOUETTE, and it inverts TWO
 *  WAYS, because the ground-normal buffer has three states and not two
 *  (game/fx_post.h:251-253).
 *
 *    gn.a 0, NOTHING DREW HERE: the building, the unit, the sea, the void. The ground
 *      lambert (game/fx_post.h:352-355) is skipped and the SSAO weight is 1.0 against
 *      the 0.5 the blade beside it gets (game/fx_state.h:1115), which is a DARK halo
 *      one blade tall around every structure.
 *    gn.a 0.5, THE FOLIAGE STATE, which the trees stamp into the same buffer
 *      (game/cnc_eyes.cpp:8165). The lambert is skipped just the same, and the weight
 *      is ssao_foliage 0.35 (game/fx_state.h:1046), so this halo is a LIGHT one.
 *
 *  THE FIX IS GEOMETRIC AND IT IS IN TWO PLACES, NEITHER OF THEM THIS SHADER:
 *
 *    STATIC EDGES  water, cliffs, roads and the map rim. grass_bake.h measures the
 *      distance from each blade's root to the nearest blocked cell and bakes it into
 *      the vertex as `sd`. THIS FILE fades the blade out over uSkirt, the live blade
 *      height turned into ground cells by the camera's own tilt, h*cot(pitch), so the
 *      cleared band covers exactly one blade's worth of screen height at whatever the
 *      slider is set to and no more.
 *    MOVING EDGES  buildings and vehicles. grass_field.h's COVER channel carries the
 *      footprint plus a skirt of its own, sized from the flattest camera the rig
 *      reaches rather than from the live pitch so that it cannot breathe with the zoom,
 *      and this file kills the blade there.
 *
 *  AND TREES ARE IN NEITHER OF THEM. THAT HALO IS OPEN, NOT CLOSED. The static mask is
 *  the terrain art's own green texels plus the water veto plus the map border and
 *  carries no object term at all; the COVER channel is the building footprint grid plus
 *  K_UNIT hulls. A tree is K_TERRAIN, and it is alpha tested and depth written in the
 *  cutout pass (game/cnc_eyes.cpp:20840), which is BEFORE this pass
 *  (game/cnc_eyes.cpp:20874), so a blade standing on the camera side of a trunk or
 *  under the lower rim of a canopy blends over it and still passes its depth test. On
 *  level ground the lambert is 1.0 on both sides and only the occlusion weight differs,
 *  a few percent. On a slope facing away the grass beside the tree is multiplied by
 *  sun_floor 0.55 (game/fx_state.h:1040) and the fringe over the tree by 1.0, an 82
 *  percent step, so every tree on a shaded hillside wears a bright rim of grass around
 *  its foot and every tree on a sunlit one a darker rim. CLOSING IT IS A STAMP AND NOT
 *  A SHADER TERM: either the tick half claims each tree through grass_crush_note_rect,
 *  the extra-ground hook grass_field.h already exposes and nothing yet calls, or the
 *  bake blocks the cells a tree stands in, which is the cheaper of the two because a
 *  tree does not move.
 *
 *  SO: a halo at a wall's foot is the skirt or the cover channel, and a halo at a
 *  tree's foot is the tree claim that is not yet made. Neither one is fixable in the
 *  fragment stage and neither must be chased there.
 *
 *  ------------------------------------------------------------------------------
 *  THE STATE CONTRACT.  READ THIS BEFORE MOVING THE CALL.
 *
 *  CALLED FROM: draw_frame, immediately after the two infantry sprite loops and the
 *  glDisable pair that closes them, and immediately before the combat effects. That
 *  slot is chosen and not inherited, and it is BELOW the sprites on purpose.
 *
 *  IT USED TO SIT AT THE END OF PASS 5, one line after the cutout walls, and that put
 *  the grass under the infantry. A blade writes no depth and so cannot occlude anything
 *  drawn after it; every sprite pass is after it; so a rifleman standing in a field was
 *  painted over the blades in front of his own boots. Moving the pass down rather than
 *  moving four sprite blocks up is the smaller change and the one that breaks nothing:
 *  the men are alpha tested with depth writes ON, so the depth test now decides, and a
 *  blade behind a man is rejected while a blade in front of him covers him.
 *
 *  WHAT IS BEHIND IT NOW, all wanted: every opaque body and every cutout, which wrote
 *  depth long before; the translucent model faces of pass 5b, which write none, so a
 *  blade nearer than a windscreen draws over it while a blade behind the vehicle is
 *  rejected by the vehicle's own opaque depth; and the infantry ground shadows, which a
 *  blade standing in front of should cover.
 *
 *  WHAT IS STILL IN FRONT OF IT, also all wanted: the combat effects, the shroud
 *  blanket, and the whole UI. THE SHROUD ONE IS LOAD BEARING. The blanket rides just
 *  above the ground with its own depth writes off, so it composites OVER a blade rather
 *  than being rejected by one, and that is what makes the field's shroud channel a
 *  colour term instead of a sorting problem. Moving this pass below the blanket would
 *  put lit grass on unexplored map.
 *
 *  It is still a long way before fx_world_end, so the whole post chain runs over the
 *  finished picture.
 *
 *  grass_crush_sim() must have run THIS FRAME, and it belongs on the line immediately
 *  above the grass_draw() call, in this slot and not in the ground group. A blade is
 *  blended and depth tested against hulls; drawn with the ground decals, before a
 *  single unit or building has written depth, every blade in the game would paint over
 *  every hull standing in it.
 *
 *  AND IT BELONGS INSIDE THE SAME GATE AS THE DRAW, because the field is not free. It is
 *  one RGBA16F target, 7.51 MiB on a mission map and 32.00 MiB at the cap
 *  (grass_field.h:188-193), plus a decay pass and the shroud rows every engine tick.
 *  grass_crush_sim asks only for g_fxActive, its own entry points and a live grid
 *  (grass_field.h:1025-1028), so left ungated it builds and steps that whole field for a
 *  map where grass_draw declines on its second line and not one blade will ever appear.
 *  Both refusals that matter are known BEFORE the sim: the feature's own dial, and
 *  whether a field was baked at all. grass_bake turns away a grid over GRASS_GRID_MAX and
 *  a vertex buffer over GRASS_VBO_BUDGET (grass_bake.h:79-87), which today is every map
 *  above 64 cells a side, and leaves g_grassHave 0. So the call site reads
 *
 *      if (g_fxActive && g_fx.grass) {
 *          grass_bake_sync(grass_bake_cfg_default());
 *          if (g_grassHave && grass_crush_sim())
 *              grass_draw();
 *      }
 *
 *  grass_bake_sync stays OUTSIDE the g_grassHave test because it is the call that SETS
 *  g_grassHave: test it first and the bake never runs, and there is never any grass.
 *
 *  THE STATE IT FINDS THERE, and every bit of it is put back:
 *    GL_DEPTH_TEST   enabled, GL_LEQUAL      (set once at game/cnc_eyes.cpp:20544-20545)
 *    GL_DEPTH_WRITEMASK  GL_TRUE
 *    GL_ALPHA_TEST   DISABLED                (the sprite pass above turns it on at
 *                                             GREATER 0.5 for the men and turns it off
 *                                             again on its way out, two lines above this
 *                                             call. It is read back and restored either
 *                                             way, so the pass does not care which it
 *                                             finds; what it must not do is assume.)
 *    GL_BLEND        disabled
 *    GL_CULL_FACE    disabled at frame start, but an object pass may have moved it
 *    GL_POLYGON_OFFSET_FILL  disabled
 *
 *  WHAT grass_draw CHANGES AND RESTORES, ONE FOR ONE:
 *    depth write         -> GL_FALSE, restored from glGetBooleanv(GL_DEPTH_WRITEMASK)
 *    GL_BLEND            -> on, SRC_ALPHA / ONE_MINUS_SRC_ALPHA, restored from
 *                           glIsEnabled + glGetIntegerv(GL_BLEND_SRC / GL_BLEND_DST)
 *    GL_ALPHA_TEST       -> off, restored from glIsEnabled
 *    GL_CULL_FACE        -> off, restored from glIsEnabled
 *    GL_POLYGON_OFFSET_FILL -> on at (-1,-1), enable and both floats restored
 *    the three client array enables, each restored from glIsEnabled
 *    GL_CURRENT_COLOR    restored from glGetFloatv
 *    the bound program   -> 0, which is also what it found: every shader pass in this
 *                           renderer ends by unbinding
 *    ARRAY_BUFFER and ELEMENT_ARRAY_BUFFER unbound, and nothing else in the tree binds
 *                           a buffer at all, so 0 is the caller's value here too
 *
 *  THE ONE THING IT DOES NOT PUT BACK IS THE TWO TEXTURE BINDINGS. Units 1 and 0 are
 *  left UNBOUND rather than restored, with the active unit back at GL_TEXTURE0, so that
 *  is the one item an audit of entry state against exit state will find changed. It is
 *  the idiom the other shader passes here use, and it is safe because every draw after
 *  this one binds its own texture before it draws, and a unit left enabled over an
 *  unbound name samples an incomplete texture, which behaves as if texturing were off
 *  for that unit. A later pass that expected to INHERIT a binding would break at this
 *  seam: give it one and both units have to be read back on the way in with
 *  glActiveTexture + glGetIntegerv(GL_TEXTURE_BINDING_2D) and put back on the way out.
 *
 *  IT DOES NOT TOUCH: the depth func, either matrix stack (the blade is transformed
 *  by gl_ModelViewProjectionMatrix, the world's own), the texture environment, the
 *  viewport, the framebuffer binding, or GL_TEXTURE_2D's enable. A GLSL program is
 *  bound for the whole pass, so the fixed-function texture enable cannot affect what
 *  the samplers read, and leaving it alone is one less bit to get wrong.
 *
 *  glIsEnabled + glGetIntegerv, not glPushAttrib: glPushAttrib appears ZERO times
 *  across the renderer and the house idiom is the explicit query.
 *
 *  IT NEVER CALLS fx_fullscreen_quad. That helper disables the depth test, blend, the
 *  alpha test and culling, restores none of them, and forces the depth mask back to
 *  GL_TRUE (game/fx_gl.h:419-441). Called from inside the world pass it would turn
 *  this pass's blend into an overwrite and hand the rest of the frame a depth mask
 *  the caller did not ask for. That trap has already shipped one bug in this project
 *  (the flashing shadows), so it is named here rather than assumed known.
 *
 *  WHY THE POLYGON OFFSET. The root vertex sits exactly on the terrain triangle's
 *  plane, so its window depth and the terrain's interpolated depth are the same value
 *  up to the last bits of two different arithmetic paths. glDepthFunc is GL_LEQUAL so
 *  a tie passes, but roughly half the root fragments land one ulp over and are
 *  rejected, which stipples the bottom scanline of every blade: precisely the band the
 *  root fade exists to soften. Depth WRITES being off does not help, because it is the
 *  TEST that rejects. This engine has written the same fix four times and says so each
 *  time; the closest precedent is the ground-normal pass, which runs glPolygonOffset
 *  with the depth mask already GL_FALSE (game/fx_post.h:919-923).
 *
 *  ------------------------------------------------------------------------------
 *  DETERMINISM.  Gate G36c requires five --shot runs to produce identical digests.
 *
 *  The only clock is engine_time() (game/cnc_eyes.cpp:2995), and it does not even
 *  reach the shader: the host folds three wind phases into [0,1) in DOUBLE, from
 *  (double)g_engineFrame + (double)g_tickAlpha, before a float ever sees the product.
 *  Nothing else in this pass varies with time. There is no hash in either shader
 *  either: the blade's own height, bearing and beat are BAKED bytes from the bake's
 *  integer hash of (cell x, cell y, slot), so two runs of the same script hand the
 *  same numbers to the same blades by construction.
 *
 *  ------------------------------------------------------------------------------
 *  WHAT WAS MEASURED RATHER THAN ASSUMED, on GL 2.1 ATI-7.1.6 / Radeon PRO W6800X:
 *
 *    GRASS_VS and GRASS_FS compile and link, and every uniform below resolves; none is
 *      optimised away, so every dial reaches the picture.
 *    The three arrays draw from a buffer object with no GL error and real pixels on
 *      the screen: GL_SHORT x3 through glVertexPointer, GL_SHORT x3 through
 *      glTexCoordPointer (GL_UNSIGNED_BYTE there is rejected outright with
 *      GL_INVALID_ENUM and no message) and GL_UNSIGNED_BYTE x4 through glColorPointer,
 *      interleaved at a 16-byte stride.
 *    Two vertex-stage texture2DLod fetches run with GL_LINEAR filtering;
 *      GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS is 16.
 *    sizeof(GrassVert) is 16 with the root at 0, the corner triple at 6 and the colour
 *      at 12, which is what the three pointer offsets below assume.
 *    Every state query the restore block depends on reads back correctly:
 *      GL_DEPTH_WRITEMASK, GL_BLEND_SRC, GL_BLEND_DST, GL_POLYGON_OFFSET_FACTOR,
 *      GL_POLYGON_OFFSET_UNITS and GL_CURRENT_COLOR.
 * ==================================================================================== */
#ifndef CNC3D_GRASS_DRAW_H
#define CNC3D_GRASS_DRAW_H

#ifndef CNC3D_GRASS_BAKE_H
#error "grass_draw.h consumes grass_bake.h; include grass_bake.h first"
#endif
#ifndef CNC3D_GRASS_CRUSH_FIELD_H
#error "grass_draw.h consumes grass_field.h; include grass_field.h first"
#endif

/* Two triangles and four vertices a blade, which is the locked blade. The bake writes
   the corners as 0 root-left, 1 root-right, 2 tip-left, 3 tip-right and its shared
   index list draws (0,1,2) and (2,1,3). Guarded in case the bake later names them. */
#ifndef GRASS_VPB
#define GRASS_VPB 4
#endif
#ifndef GRASS_IPB
#define GRASS_IPB 6
#endif

/* ---------------------------------------------------------------------------------- *
 *  WHAT THIS FILE READS FROM ITS TWO SUPPLIERS, so a change on either side breaks
 *  loudly here rather than quietly in the picture.
 *
 *  FROM grass_bake.h
 *    struct GrassVert {                                       16 bytes, offsets 0/6/12
 *        GLshort px, py, pz;      the ROOT, identical on the four corners, in
 *                                 g_grassPosScale units a cell   -> gl_Vertex.xyz
 *        GLshort side, b2t, sd;   the corner across the card (+-GRASS_SIDE_ONE), up the
 *                                 card (0..GRASS_B2T_ONE), and the root's distance to
 *                                 the nearest static silhouette edge in GRASS_SD_STEP
 *                                 units a cell            -> gl_MultiTexCoord0.xyz
 *        GLubyte hgt, yaw, phase, slot;
 *                                 the blade's own height draw (0..255 of the variation
 *                                 range), its bearing (0..255 of a turn), its wind beat
 *                                 (0..255 of a cycle), and its slot as the byte
 *                                 round(255 * slot / 127)               -> gl_Color
 *    };
 *    struct GrassTile { unsigned short cx, cy; unsigned int vert0, blades;
 *                       float y0, y1; unsigned short cum[GRASS_SLOTS]; };
 *      cum[i] is the number of blades in the tile whose slot is i OR LOWER, so
 *      cum[GRASS_SLOTS-1] is the tile total and full density is expressible.
 *    GRASS_TILE, GRASS_SLOTS, GRASS_SIDE_ONE, GRASS_B2T_ONE, GRASS_SD_STEP
 *    g_grassVBO, g_grassIBO, g_grassGroundTex, g_grassTile, g_grassPosScale,
 *    g_grassHave, g_grassBufReady, gr_glBindBuffer
 *
 *  FROM grass_field.h
 *    grass_crush_ready()  1 when the field exists
 *    grass_crush_tex()    the RGBA16F field: R tread, G cover, B shroud, A reserved
 *
 *  WHY THE SHROUD RIDES IN THAT FIELD RATHER THAN IN DRAW ORDER. The shroud blanket
 *  rides 0.11 world units above the ground (SHROUD_BLACK_Y 0.050 at
 *  game/shroud_mod.h:363 plus the 0.06 pad, stated at game/shroud_mod.h:642-643), it is
 *  drawn after this pass, and its own depth writes are off. Draw order is therefore not
 *  the failure: the blade wrote no depth either (the keystone above), so the pixel's
 *  depth still belongs to the terrain, the blanket rides nearer than that and passes,
 *  and it covers a blade exactly as it covers the bare ground beside it. The failure is
 *  that the blanket is TRANSLUCENT by design. A mapped-but-unseen cell is covered at
 *  SHROUD_DARK_ALPHA 0.45 (game/shroud_mod.h:358), which leaves 55% of whatever lies
 *  under it showing, and the feather band is capped at SHROUD_SOFT_MAXA 0.784
 *  (game/shroud_mod.h:486), which still leaves 21.6% along the whole reveal perimeter.
 *  A blade not darkened before the blanket lands stands at full brightness over ground
 *  that the same coverage byte has correctly dimmed. Multiplying the blade by the
 *  field's B channel is that darkening, and it cannot disagree with the blanket about
 *  where the edge is because it is the same per-corner coverage array the blanket's own
 *  vertex fade uses (game/shroud_mod.h:567, and the account at
 *  game/cnc_eyes.cpp:6895-6907 of why the terrain takes the same byte).
 * ---------------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------------- *
 *  1. THE VERTEX PROGRAM.
 * ---------------------------------------------------------------------------------- */
static const char* GRASS_VS =
    "#version 120\n"
    /* the bake's own scales: the draw does not get to invent these */
    "uniform float uPosScale, uSideScale, uB2TScale, uSdScale;\n"
    /* geometry, all live dials */
    "uniform float uHeight, uVary, uWidth, uTaper, uLean, uSkirt;\n"
    /* the wind, one bearing over the map */
    "uniform vec2  uWindDir;\n"
    "uniform vec2  uK1, uK2;\n"           /* wave vectors, cells^-1                    */
    "uniform float uPh1, uPh2, uPh3;\n"   /* three phases, each folded to [0,1) in double */
    "uniform float uBend, uArc, uShiver;\n"
    "uniform vec3  uTouch;\n"        /* where the pointer stands on the ground, and how hard */
    "uniform float uTouchR;\n"       /* how far that reaches, in cells                       */
    /* the fields */
    "uniform sampler2D uGround;\n"        /* unit 0: per cell, rgb ground, a coverage   */
    "uniform sampler2D uCrush;\n"         /* unit 1: r tread, g cover, b shroud         */
    "uniform vec2  uFieldScale;\n"        /* 1/gridW, 1/gridH, both fields              */
    "uniform float uLit;\n"               /* the light draw_terrain sends this frame    */
    /* the LOD and the colour patch */
    "uniform vec2  uCamXZ;\n"             /* the eye on the ground plane, in cells      */
    "uniform vec2  uFade;\n"              /* x the last cell with a blade, y 1 / band   */
    "uniform float uDensity;\n"           /* the dial in blades a cell, clamped         */
    "uniform float uCrushGone;\n"         /* where the last of a pressed blade vanishes */
    "uniform float uPatchLen;\n"          /* cells between colour patches               */
    /* out */
    "varying vec4  vShape;\n"             /* x b2t, y side, z keep, w patch             */
    "varying vec4  vGnd;\n"               /* rgb the ground colour, a the light         */
    "void main() {\n"
    "    vec3  root = gl_Vertex.xyz * uPosScale;\n"            /* world cells           */
    "    float side = gl_MultiTexCoord0.x * uSideScale;\n"     /* -1 .. +1              */
    "    float b2t  = gl_MultiTexCoord0.y * uB2TScale;\n"      /*  0 .. 1               */
    "    float sd   = gl_MultiTexCoord0.z * uSdScale;\n"       /* cells to an edge      */
    "    float hgt  = gl_Color.r;\n"
    "    float yaw  = gl_Color.g * 6.2831853;\n"
    "    float beat = gl_Color.b;\n"
    "    float slot = gl_Color.a;\n"                           /* the slot byte / 255   */
    "    vec2  sideDir = vec2(cos(yaw), sin(yaw));\n"
    "    vec2  fwd     = vec2(-sideDir.y, sideDir.x);\n"
    /* THE BLADE'S OWN HEIGHT. hgt is a flat draw in [0,1] from the bake's integer hash,
       so the dial's value and the dial's spread are both live here. The bake sizes its
       silhouette skirt from the TOP of both ranges, which is what stops the height
       slider invalidating the whole buffer. */
    "    float bh = uHeight * (1.0 + uVary * (hgt * 2.0 - 1.0));\n"
    "    if (bh < 0.0) bh = 0.0;\n"
    "\n"
    /* THE TWO FIELD READS, BOTH AT THE ROOT AND BOTH IN THE VERTEX STAGE. The root is
       identical on the four corners, so every varying below is CONSTANT across the card
       and the blade fades, presses and takes its colour AS ONE THING. Read per fragment
       instead, a leaning tip samples the road it is leaning over, and a blade crossed by
       a tread dissolves from the tip downward, which is not what a wheel does to grass.
       texture2DLod is the vertex-stage form; the vertex texture units are 16 here and
       GL_LINEAR works in the vertex stage, both measured. */
    "    vec2  fuv = root.xz * uFieldScale;\n"
    "    vec4  fld = texture2DLod(uGround, fuv, 0.0);\n"
    "    vec4  crs = texture2DLod(uCrush, fuv, 0.0);\n"
    "    float tread  = clamp(crs.r, 0.0, 1.0);\n"
    "    float cover  = clamp(crs.g, 0.0, 1.0);\n"
    "    float shroud = clamp(crs.b, 0.0, 1.0);\n"
    /* THE LIGHT IS THE GROUND'S OWN, NOT A SECOND ONE. draw_terrain sends
       (oneSun ? 160/255 : terrain_shade) times the shroud byte as its vertex alpha
       (game/cnc_eyes.cpp:6809-6815), and under ENHANCED with the shipped dials that is
       a FLAT 160/255 with the slope arriving later from the post chain. uLit carries
       the same expression, and the shroud comes from the same per-corner array. */
    "    float lit = uLit * shroud;\n"
    "    vGnd = vec4(fld.rgb * lit, lit);\n"
    "\n"
    /* WHETHER THIS BLADE EXISTS AT ALL. Four independent reasons it might not, and all
       four are one multiply so a blade never half-exists in one term and fully in
       another.
         THE LOD: the cut-off is THIS BLADE'S OWN, taken from its root. keep is
           clamp(sc - s) in slot units, where sc is uDensity times the smoothstepped
           range and s is this blade's WHOLE slot number, and the host draws
           cum[floor(sc)] blades for the NEAREST ground in the tile, so every blade the
           cut could still want is present and the ones past their own cut leave through
           the clip volume just below. The blade ON the cut-off is worth the fractional
           part and every blade under it is worth 1, so the count and the fade name the
           same number in a uniform tile and a ragged one alike, and neither the density
           dial nor the distance thinning can pop. Reading the range per TILE instead
           quantises the thinning to four cells against a band that is a little over one
           tile wide at the shipped distance and under half a tile at the dial's
           minimum, which turns the far edge of the field into a straight tile boundary
           with most of a full count of blades on one side of it. The root is identical
           on the four corners, so this range is constant across the card like every
           other term here.
           s IS RECOVERED FROM THE BYTE AND NOT USED AS A FRACTION. The vertex carries
           the slot as a byte holding round(255 * s / 127), so its normalised value is
           s/127 only to within half a byte step, and a difference scaled by 127 turns
           that step into a quarter of a whole slot: at the instant the count first
           admits slot s the fade would read up to 0.247 instead of 0, and above slot
           63 the sign flips and a counted blade is discarded for the first quarter of
           its slot. The byte is always 2s or 2s + 1, so floor(byte * 127.5 + 0.25) is
           exactly s for all 128 slots and the two sides of the LOD share one integer
           axis. It stays exact if the bake ever stores a plain 2s instead.
         THE COVER: a building or a hull has claimed this ground.
         THE TREAD: the last of a pressed blade shrinks away rather than lying flat, so
           a track shows soil instead of a green mat.
         THE SKIRT: sd is the baked distance to the nearest static silhouette edge, and
           uSkirt is the ground run a live blade's own screen height covers at the
           camera's tilt, which is a cotangent and not a secant of it. Fading
           over that band is what stops a blade putting ink over the sea, a cliff or the
           map rim, where the keystone inverts and the post chain would ring it with a
           dark halo. */
    "    float kd = clamp((uFade.x - length(root.xz - uCamXZ)) * uFade.y, 0.0, 1.0);\n"
    "    kd = kd * kd * (3.0 - 2.0 * kd);\n"
    "    float keep = clamp(uDensity * kd - floor(slot * 127.5 + 0.25), 0.0, 1.0);\n"
    "    keep *= 1.0 - cover;\n"
    "    keep *= 1.0 - smoothstep(uCrushGone, 1.0, tread);\n"
    "    keep *= smoothstep(0.0, max(uSkirt, 1.0e-4), sd);\n"
    "    if (keep <= 0.0) {\n"
    /* OFF THE CLIP VOLUME, NOT A DISCARD. All four corners take the same point, so the
       two triangles are clipped whole and never reach the rasteriser. */
    "        vShape = vec4(0.0); gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return;\n"
    "    }\n"
    "\n"
    /* THE CARD, before the wind. The width tapers toward the tip and the resting lean
       is anchored by b2t squared so the ROOT does not move; both are live dials, because
       the bake writes the card at unit half width and unit height and lets the shader
       scale it. A width or a height read at bake time is a dead slider that still round
       trips through the cfg file, which is the worst kind of dead control. */
    "    float w = uWidth * 0.5 * mix(1.0, uTaper, b2t) * side;\n"
    "    vec3  off = vec3(sideDir.x * w, bh * b2t, sideDir.y * w);\n"
    "    off.xz += fwd * (uLean * bh * b2t * b2t);\n"
    "\n"
    /* THE GUST IS A TRAVELLING WAVE, NOT ONE GLOBAL SINE. Two waves at two wavelengths
       and two slightly different bearings, so they beat and the gust front is not a
       ruled line crossing the field. The tree's own amp is one sine with a per-tree
       phase, so a wood breathes in unison; with grass beside it that difference will be
       visible, and it is on purpose.
       NEITHER TERM GROWS WITH THE CLOCK. uPh1..3 arrive already folded into [0,1) by
       the host, in double, and NOTHING HERE MULTIPLIES A FOLDED PHASE: sin(2pi*(uPh*1.7
       + ...)) would unfold it straight back into an unbounded number, which is why there
       are three phase uniforms rather than one with three multipliers. The other term,
       dot(root, K), is bounded by the map over the wavelength: at the 256-cell ceiling
       (game/c3d_ceiling.h:45) that diagonal is 362.04 cells and the shortest wavelength
       the dial allows is 0.8 cells, so 452.5 cycles, a sine argument of 2843 radians,
       and a float ulp there of 2.44e-4 rad = 0.014 degrees. That number is the same
       after a minute and after a week, which is the entire point. The tree's live wind
       is the counterexample and it must not be copied: it hands sin() 6.2831853 * uTime
       * 3.1 with uTime in seconds since the mission started (game/cnc_eyes.cpp:8047 with
       :7853), which is 140,241 radians after two hours and loses about one ulp of a
       frame's travel per frame after roughly 60 hours. */
    "    float s1 = sin(6.2831853 * (uPh1 + dot(root.xz, uK1)));\n"
    "    float s2 = sin(6.2831853 * (uPh2 + dot(root.xz, uK2)));\n"
    "    float gust = 0.55 + 0.45 * (0.65 * s1 + 0.35 * s2);\n"   /* 0.10 .. 1.00      */
    "\n"
    "    float bend = uBend * bh * gust * b2t * b2t;\n"
    "    off.xz += uWindDir * bend;\n"
    /* A BLADE OF FIXED LENGTH THAT LEANS OVER GETS SHORTER. Without the drop the bend is
       a pure shear, and a field of shears reads as a carpet sliding sideways, which is
       the fault the trees' first build was called out for. bend squared over twice the
       height preserves the arc length to second order and uArc carries the 1/2h. */
    "    off.y  -= uArc * bend * bend;\n"
    /* AND ITS OWN BEAT ACROSS THE WIND, or the whole field is one rigid wave. The beat
       is the bake's own byte, not a hash computed here: an integer hash of the cell and
       the slot is bit exact on every machine, which a fract(sin(dot())) is only in
       practice. */
    "    off.xz += vec2(-uWindDir.y, uWindDir.x)\n"
    "              * (uShiver * bend * sin(6.2831853 * (uPh3 + beat)));\n"
    "\n"
    /* AND THE POINTER PARTS IT. The same bend the wind uses, anchored at the root by the
       same b2t squared, but pushed radially AWAY from where the pointer is standing on
       the ground rather than along a bearing: blades lean out of the way and spring back
       as it passes, which is the whole of what makes a field feel touched rather than
       painted. Quadratic falloff so the edge of the circle is soft and its middle is
       firm; a hard-edged one drags a visible disc across the field.
       uTouch.z IS THE SWITCH as well as the strength. It is zero whenever there is no
       pointer on the map, which is every headless run, so a screenshot cannot depend on
       where a mouse happens to be and the five-run digest holds. It is also the reason
       there is no branch on a uniform here that a driver has to be trusted to skip: at
       strength zero the whole term multiplies out. */
    "    vec2  toC = root.xz - uTouch.xy;\n"
    "    float dC  = length(toC);\n"
    "    float gC  = max(0.0, 1.0 - dC / max(uTouchR, 1.0e-4));\n"
    "    float tb  = uTouch.z * bh * gC * gC * b2t * b2t;\n"
    "    off.xz   += (dC > 1.0e-4 ? toC / dC : vec2(1.0, 0.0)) * tb;\n"
    "    off.y    -= uArc * tb * tb;\n"
    "\n"
    /* CRUSHED, WHICH IS A ROTATION ABOUT THE ROOT AND NOT A SCALE. A scaled blade reads
       as mown; a rotated one reads as crushed. It falls along its OWN bearing, so a
       tread leaves a chaotic mat rather than a combed one. */
    "    if (tread > 0.0) {\n"
    "        float th = tread * 1.5707963;\n"
    "        off.xz += fwd * (off.y * sin(th));\n"
    "        off.y  *= cos(th);\n"
    "    }\n"
    "\n"
    /* THE PATCH COLOUR IS WORLD SPACE AND HAS NO CLOCK IN IT AT ALL. Per-blade colour
       jitter reads as static; a low-frequency wave over the world means neighbours agree
       and the field breaks into patches, which is what a field does. The bearing is a
       unit vector, so uPatchLen is honestly in cells. */
    "    float patch = 0.5 + 0.5 * sin(6.2831853\n"
    "                  * dot(root.xz, vec2(0.5145, 0.8575)) / uPatchLen);\n"
    "    vShape = vec4(b2t, side, keep, patch);\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * vec4(root + off, 1.0);\n"
    "}\n";

/* ---------------------------------------------------------------------------------- *
 *  2. THE FRAGMENT PROGRAM.  NO TEXTURE FETCH, NO LIGHTING, ONE DISCARD.
 * ---------------------------------------------------------------------------------- */
static const char* GRASS_FS =
    "#version 120\n"
    "uniform vec3  uGrassCol;\n"
    "uniform float uTint, uPatch;\n"
    "uniform float uRootFade, uRootAlpha, uEdge, uAO, uTip, uGain;\n"
    "varying vec4  vShape;\n"
    "varying vec4  vGnd;\n"
    "void main() {\n"
    "    float b2t = vShape.x;\n"
    /* THE SILHOUETTE IS THE TRAPEZOID PLUS A RAMP. No cut-out texture, no alpha test and
       no dither. There is no multisample buffer and no temporal filter in this context
       to resolve either a hard cut or a dither pattern, and on a card that averages 1.35
       to 2.36 px wide an unresolved dither IS the blade. uEDGE IS A UNIFORM AND NOT A
       CONSTANT because the ramp has to be ONE SAMPLE wide and a card's own width is not:
       the host turns a pixel into a fraction of the half width per tile and sends it
       here, which is the only lever left against far-field shimmer and costs this
       program nothing. uRootAlpha arrives the same way, one sample of blade height. */
    "    float a = 1.0 - smoothstep(1.0 - uEdge, 1.0, abs(vShape.y));\n"
    "    a *= smoothstep(0.0, uRootAlpha, b2t);\n"
    "    a *= vShape.z;\n"
    /* The only discard, and it is a bandwidth backstop rather than a shape: a blade the
       LOD, the tread or the skirt has already taken to nothing costs no blend. Grass
       writes no depth, so the early depth TEST is unaffected by it. */
    "    if (a < 0.004) discard;\n"
    /* THE ROOT FADE IS COLOUR FIRST AND ALPHA SECOND, AND MOSTLY COLOUR. At b2t = 0 the
       blade IS the ground under it, so there is nothing to sort and nothing to see
       through: on a dense field what shows through an alpha-faded root is the blade
       behind it, not the ground. The alpha ramp above stays, over the bottom few percent
       only, to kill the hard line where a flat card meets the terrain. */
    "    vec3 own  = uGrassCol * (1.0 - 0.5 * uPatch + uPatch * vShape.w) * vGnd.a;\n"
    "    vec3 body = mix(own, vGnd.rgb, uTint);\n"
    "    float rf  = smoothstep(0.0, uRootFade, b2t);\n"
    "    vec3 col  = mix(vGnd.rgb, body, rf);\n"
    /* THE BLADE'S OWN VERTICAL SHADING, AND THAT IS ALL THE SHADING IT DOES. Darker at
       the root where it stands among its neighbours, paler at the tip where the sky
       reaches it. THERE IS NO N DOT L: the ground-normal buffer behind this pixel still
       carries the terrain at alpha 1.0, so the light pass has already given this
       fragment the ground's one-sun lambert and the ground's occlusion weight. A second
       light term here would be two suns multiplied together.
       ALL THREE OF THEM RIDE THE ROOT RAMP, AND THAT IS THE POINT OF THE RAMP. Multiplied
       in after the colour mix they go straight through it, and at b2t = 0 the blade lands
       at (1 - uAO) times the ground instead of ON the ground, which with the shipped
       dials is 0.45 of it: a dark step along the bottom scanline of every blade, and the
       exact artefact the root fade exists to remove. uGain rides the ramp for the same
       reason. Left outside it, it scales the root as well, so the one band that has to be
       invisible moves with a dial, and in either direction. Above the fade band every
       dial still has its full effect. */
    "    float shade = ((1.0 - uAO) + uAO * b2t) * (1.0 + uTip * b2t * b2t) * uGain;\n"
    "    col *= mix(1.0, shade, rf);\n"
    "    gl_FragColor = vec4(col, a);\n"
    "}\n";

/* ---------------------------------------------------------------------------------- *
 *  3. THE PROGRAM, AND THE THREE UNIFORM LOCATIONS THAT ARE SET PER TILE.
 *
 *  fx_program (game/fx_gl.h) always links FX_VS and tree3d_link always links TREE_VS,
 *  so the grass needs its own two-source linker. It is tree3d_link
 *  (game/cnc_eyes.cpp:7969-7994) with the vertex source passed in.
 *
 *  uEdge, uRootAlpha and uLit are looked up ONCE and set through fx_glUniform1f
 *  directly. fx_set1f does a glGetUniformLocation on every call, which is a driver
 *  string lookup; at 84 visible tiles that would be 252 of them a frame for three
 *  numbers. Those three are here because each of them genuinely CARRIES a per-tile
 *  value: the two ramp widths are pixels turned into fractions of the blade the tile
 *  actually shows, and the light is the tile's own terrain shade whenever the ground is
 *  not going down flat. Anything that cannot change inside the pass goes up once above
 *  the loop with the other dials instead, and the LOD cut-off is no longer among these
 *  at all: it is read per blade in the vertex stage from one pass-level ramp.
 * ---------------------------------------------------------------------------------- */
/* WHERE THE POINTER IS STANDING, handed IN rather than read.
   The renderer's cursor lives a long way below this header's include point, so this file
   cannot see it and must not try. The host sets these three immediately before it calls
   grass_draw, exactly as it hands the field its recorded ticks, and the strength is the
   switch: left at zero, which is where it starts and where every headless run leaves it,
   the whole term multiplies out and a screenshot cannot depend on a mouse. */
static float g_grassTouchX = 0.0f, g_grassTouchZ = 0.0f, g_grassTouchS = 0.0f;

static void grass_touch_set(float wx, float wz, float strength)
{
    g_grassTouchX = wx; g_grassTouchZ = wz;
    g_grassTouchS = strength > 0.0f ? strength : 0.0f;
}

static GLuint g_grassProg = 0;
static int    g_grassProgTried = 0;
static GLint  g_grassLocEdge = -1;
static GLint  g_grassLocRootAlpha = -1;
static GLint  g_grassLocLit = -1;

static GLuint grass_link(const char* vs_src, const char* fs_src, const char* label)
{
    if (!fx_gl_ready)
        return 0;
    GLuint vs = fx_compile(GL_VERTEX_SHADER, vs_src, label);
    if (!vs) return 0;
    GLuint fs = fx_compile(GL_FRAGMENT_SHADER, fs_src, label);
    if (!fs) { fx_glDeleteShader(vs); return 0; }
    GLuint p = fx_glCreateProgram();
    fx_glAttachShader(p, vs);
    fx_glAttachShader(p, fs);
    fx_glLinkProgram(p);
    GLint ok = 0;
    fx_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096]; GLsizei n = 0;
        if (fx_glGetProgramInfoLog) fx_glGetProgramInfoLog(p, (GLsizei)sizeof log, &n, log);
        log[n < (GLsizei)sizeof log ? n : (GLsizei)sizeof log - 1] = 0;
        fprintf(stderr, "FX|program|%s FAILED TO LINK\n%s\n", label, log);
        fx_glDeleteProgram(p);
        p = 0;
    }
    fx_glDeleteShader(vs);
    fx_glDeleteShader(fs);
    return p;
}

static GLuint grass_prog(void)
{
    if (!g_grassProg && !g_grassProgTried) {
        g_grassProgTried = 1;
        g_grassProg = grass_link(GRASS_VS, GRASS_FS, "grass");
        if (g_grassProg) {
            g_grassLocEdge    = fx_glGetUniformLocation(g_grassProg, "uEdge");
            g_grassLocRootAlpha = fx_glGetUniformLocation(g_grassProg, "uRootAlpha");
            g_grassLocLit     = fx_glGetUniformLocation(g_grassProg, "uLit");
        }
    }
    return g_grassProg;
}

/* ---------------------------------------------------------------------------------- *
 *  4. THE WIND PHASES.  FOLDED ON THE HOST, IN DOUBLE, EVERY FRAME.
 *
 *  Three separate phases because a folded phase may never be multiplied in the shader.
 *  Folded in DOUBLE and from the tick counter rather than from engine_time()'s float:
 *  engine_time returns (float)g_engineFrame + g_tickAlpha (game/cnc_eyes.cpp:2995-2998),
 *  and after a couple of hours that float's own ulp has coarsened past a hundredth of a
 *  tick, so even the fold's INPUT would have started to step. Reading the two parts as
 *  doubles costs nothing and removes it. fmodf on a float, which is what the cloud drift
 *  does today, is measurably lossy at these speeds; a double fold is not.
 *
 *  THE BEARING IS NOT A GRASS DIAL. It is tree3d_wind_dir, which game/fx_state.h:468
 *  already calls "the bearing the wind blows along, degrees, one for the map", and the
 *  direction vector is built exactly as tree3d_place builds it,
 *  (cos(bearing), sin(bearing)) in xz (game/cnc_eyes.cpp:8058), so a gust cannot cross
 *  the wood one way and the field another. The SPEED is tree3d_wind_speed times a grass
 *  multiplier, so at 1.0 a gust reaches both at the same rate. Both are read whether or
 *  not g_fx.tree3d is on: they are state fields, not gated ones.
 * ---------------------------------------------------------------------------------- */
static void grass_wind_phases(float* p1, float* p2, float* p3)
{
    const double tsec = ((double)g_engineFrame + (double)g_tickAlpha) / 15.0;
    const double sp   = (double)g_fx.tree3d_wind_speed * (double)g_fx.grass_wind_rate;
    const double a = tsec * sp, b = tsec * sp * 1.7, c = tsec * sp * 3.3;
    *p1 = (float)(a - floor(a));
    *p2 = (float)(b - floor(b));
    *p3 = (float)(c - floor(c));
}

/* ---------------------------------------------------------------------------------- *
 *  5. THE LOD.  A FRACTIONAL SLOT CUT-OFF, PER BLADE, AND THE COUNT FOLLOWS IT.
 *
 *  sc is a fractional slot number in [0, GRASS_SLOTS - 1]. A blade whose slot is under
 *  floor(sc) is worth 1 and the blade ON the cut-off is worth frac(sc), and the host
 *  draws cum[floor(sc)] blades, so count and fade name the same number. That is what
 *  makes the density dial and the distance LOD the same mechanism and keeps both of them
 *  from popping. A cut-off expressed as a fraction of the tile's ACTUAL blade count
 *  instead puts the fade twenty-four slots above the highest blade drawn on a uniform
 *  tile, and the LOD then drops a whole slot at every band boundary.
 *
 *  THE RANGE IS READ PER BLADE, IN THE VERTEX STAGE, AND THE TILE ONLY BOUNDS THE COUNT.
 *  The band the field thins over is a quarter of the distance dial, which at the shipped
 *  distance is a little over one tile wide and at the dial's minimum is under half a
 *  tile. Sampled once per tile that is not a fade at all: the field ends on a straight,
 *  axis-aligned tile boundary carrying most of a full count of blades on one side and
 *  bare ground on the other, and at the low end of the dial the whole field switches off
 *  in a single tile row. Widening the band does not rescue it either, because the step
 *  across a tile edge stays a fixed fraction, one tile over the band, of the ramp.
 *
 *  Reading the range per blade is safe because the bake writes the ROOT on all four
 *  corners of a card, so a range taken from root.xz is CONSTANT across the card exactly
 *  as keep already is and the blade still fades as one thing. It is the range to the
 *  moving VERTEX that would fade a 0.13-cell blade's tip by a visibly different amount
 *  from its root, about 0.097 cells of range apart on this camera, and that is not what
 *  is measured here.
 *
 *  So the host asks each tile for the count its NEAREST ground could still want, which
 *  can only over-draw, and every blade past its own cut-off leaves through the clip
 *  volume in the vertex program without ever reaching the rasteriser.
 * ---------------------------------------------------------------------------------- */
/* THE RAMP'S THREE NUMBERS, IN ONE PLACE. The host runs this per tile to bound the
   count and the vertex stage runs the same ramp again per blade, so a second copy of the
   expression would be a disagreement waiting to happen. The density dial is in blades a
   cell and the bake's ceiling is one blade a slot, so the dial and the slot axis are the
   same number; clamped rather than trusted, because a dial range wider than the bake's
   ceiling would otherwise index off the table. */
static void grass_lod_ramp(float* far_, float* invBand, float* den)
{
    const float f = g_fx.grass_fade > 1.0f ? g_fx.grass_fade : 1.0f;
    float d = g_fx.grass_density;
    if (d > (float)GRASS_SLOTS) d = (float)GRASS_SLOTS;
    if (d < 0.0f) d = 0.0f;
    *far_    = f;
    *invBand = 1.0f / (f * 0.25f);                   /* it thins over the last quarter */
    *den     = d;
}

static unsigned grass_tile_lod(const GrassTile& t)
{
    /* THE NEAREST POINT OF THE TILE'S FOOTPRINT, not its centre and not its nearest
       corner. This count has to be an upper bound over every blade in the tile, so it is
       read where the ground is closest: clamped onto the footprint, which is an edge and
       not a corner whenever the eye sits inside one of the tile's two slabs. Read from
       the centre it would under-draw the near half of every tile the band crosses. */
    const float x0 = (float)t.cx, x1 = x0 + (float)GRASS_TILE;
    const float z0 = (float)t.cy, z1 = z0 + (float)GRASS_TILE;
    const float ex = g_fxCamPos[0], ez = g_fxCamPos[2];
    const float dx = ex < x0 ? x0 - ex : (ex > x1 ? ex - x1 : 0.0f);
    const float dz = ez < z0 ? z0 - ez : (ez > z1 ? ez - z1 : 0.0f);
    const float d  = sqrtf(dx * dx + dz * dz);       /* cells, on the ground plane */

    float far_, invBand, den;
    grass_lod_ramp(&far_, &invBand, &den);
    float k = (far_ - d) * invBand;
    if (k > 1.0f) k = 1.0f; else if (k < 0.0f) k = 0.0f;
    k = k * k * (3.0f - 2.0f * k);                   /* the same ramp the shader runs */

    const float sc = den * k;
    if (sc <= 0.0f) return 0u;
    int si = (int)sc;                                /* floor, sc > 0 */
    if (si > GRASS_SLOTS - 1) si = GRASS_SLOTS - 1;
    unsigned n = t.cum[si];
    if (n > t.blades) n = t.blades;
    return n;
}

/* ---------------------------------------------------------------------------------- *
 *  6. THE PER-TILE CULL.
 *
 *  The SAME box draw_terrain's own cells are tested against, asked once for sixteen
 *  cells instead of sixteen times: g_cullX0..g_cullZ1 with cell_in_view's 2-cell float
 *  slop (game/cnc_eyes.cpp:6467-6475), plus the blade's own reach so a tile whose grass
 *  leans into frame from just outside the box is not clipped at the screen edge.
 *
 *  IT IS THAT BOX AND NOT THE TIGHTER g_viewX0..g_viewZ1 RECT ON PURPOSE. The cull box
 *  is the union over the terrain's own height bracket and is a PROVEN superset of every
 *  visible surface point (the proof is written out at game/cnc_eyes.cpp:6425-6440); the
 *  view rect is the y = 0 trapezoid's bounding box and under-covers raised ground. Grass
 *  culled where the ground under it still draws is a bald patch at the top of the screen
 *  that grows and shrinks as you scroll. The tighter rect saves roughly a third of the
 *  tiles, which is a fraction of a static-VBO draw and not worth a second correctness
 *  proof over eight height planes.
 *
 *  It honours g_cullOn, g_cullValid and g_clip_to_map exactly as cell_in_view does, so
 *  --noclip and the editor's free camera behave here the way they behave for the ground.
 * ---------------------------------------------------------------------------------- */
static inline bool grass_tile_in_view(const GrassTile& t, float reach)
{
    if (!g_cullOn || !g_cullValid || !g_clip_to_map)
        return true;
    const float m = 2.0f + reach;
    const float x0 = (float)t.cx, x1 = x0 + (float)GRASS_TILE;
    const float z0 = (float)t.cy, z1 = z0 + (float)GRASS_TILE;
    return !(x1 < g_cullX0 - m || x0 > g_cullX1 + m ||
             z1 < g_cullZ0 - m || z0 > g_cullZ1 + m);
}

/* ---------------------------------------------------------------------------------- *
 *  7. THE PASS.  Returns true when it drew, false when it declined and why.
 * ---------------------------------------------------------------------------------- */
static bool grass_draw(void)
{
    if (!g_fxActive || !g_fx.grass || !fx_gl_ready)
        return false;
    if (!g_grassHave || !g_grassBufReady || g_grassVBO == 0 || g_grassIBO == 0
        || g_grassTile.empty() || g_grassGroundTex == 0)
        return false;
    /* THE CRUSH FIELD IS NOT OPTIONAL, and grass_field.h says so in the same words.
       Without it there is no COVER channel, so blades stand through buildings and
       hulls, and no SHROUD channel, so they stand lit on unexplored map. Grass with no
       field is not degraded grass, it is broken grass. */
    if (!grass_crush_ready()) {
        static int said = 0;
        if (!said) {
            said = 1;
            fprintf(stderr, "GRASS|off|no crush field this frame; grass needs its cover "
                            "and shroud channels, the rest of the chain is unaffected\n");
        }
        return false;
    }
    const GLuint prog = grass_prog();
    if (!prog)
        return false;

    /* ---- state in, every bit of it read back first ------------------------------- */
    GLboolean sDepthMask = GL_TRUE;
    GLboolean sBlend, sAlpha, sCull, sPoly, sVA, sTA, sCA;
    GLint     sSrc = GL_ONE, sDst = GL_ZERO;
    GLfloat   sPolyF = 0.0f, sPolyU = 0.0f, sColour[4];
    glGetBooleanv(GL_DEPTH_WRITEMASK, &sDepthMask);
    sBlend = glIsEnabled(GL_BLEND);
    sAlpha = glIsEnabled(GL_ALPHA_TEST);
    sCull  = glIsEnabled(GL_CULL_FACE);
    sPoly  = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    sVA    = glIsEnabled(GL_VERTEX_ARRAY);
    sTA    = glIsEnabled(GL_TEXTURE_COORD_ARRAY);
    sCA    = glIsEnabled(GL_COLOR_ARRAY);
    /* GL_BLEND_SRC and GL_BLEND_DST are the GL 1.1 queries and are all this renderer
       needs: glBlendFuncSeparate is called nowhere in the tree, and it is absent from
       the GL 1.1 header the Windows half cross-compiles against, so asking for the
       separate pairs here would compile on one platform and not the other. */
    glGetIntegerv(GL_BLEND_SRC, &sSrc);
    glGetIntegerv(GL_BLEND_DST, &sDst);
    glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &sPolyF);
    glGetFloatv(GL_POLYGON_OFFSET_UNITS, &sPolyU);
    glGetFloatv(GL_CURRENT_COLOR, sColour);

    glDepthMask(GL_FALSE);                 /* THE KEYSTONE. Depth TEST stays on.      */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_ALPHA_TEST);              /* pass 5 left it ON at GREATER 0.5        */
    glDisable(GL_CULL_FACE);               /* a blade card has no back                */
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);         /* the root is coplanar with the ground    */

    /* ---- the two fields ---------------------------------------------------------- */
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, grass_crush_tex());
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_grassGroundTex);

    fx_glUseProgram(prog);
    fx_set1i(prog, "uGround", 0);
    fx_set1i(prog, "uCrush",  1);

    /* THE BAKE'S OWN SCALES. The draw does not get to invent these: the vertex is
       shorts, and every one of these four reciprocals is the unit the bake wrote in. */
    fx_set1f(prog, "uPosScale",  1.0f / g_grassPosScale);
    fx_set1f(prog, "uSideScale", 1.0f / GRASS_SIDE_ONE);
    fx_set1f(prog, "uB2TScale",  1.0f / GRASS_B2T_ONE);
    fx_set1f(prog, "uSdScale",   1.0f / GRASS_SD_STEP);

    /* geometry, every one a live slider */
    fx_set1f(prog, "uHeight", g_fx.grass_height);
    fx_set1f(prog, "uVary",   g_fx.grass_vary);
    fx_set1f(prog, "uWidth",  g_fx.grass_width);
    fx_set1f(prog, "uTaper",  g_fx.grass_taper);
    fx_set1f(prog, "uLean",   g_fx.grass_lean);

    /* THE SKIRT, and it is the live height and not the baked ceiling. THE FACTOR IS A
       COTANGENT, NOT A SECANT. The modelview is a dolly back and then a tilt about X
       (game/cnc_eyes.cpp:5862-5863), so at pitch p a world vertical h rises h*cos(p) up
       the screen while a ground step d further from the camera rises d*sin(p): a blade's
       tip lands on the screen row of ground h*cot(p) away, and that is exactly how far
       from a silhouette edge the last blade may stand. h/cos(p) is not merely more
       generous, it moves the WRONG WAY with the camera, growing as the tilt steepens and
       the requirement shrinks; the two agree only at 38.17 degrees, and at the flat end
       the band falls under half of what one blade needs. The bake reserved the band for
       the TOP of the height dial; this shrinks it to whatever the dial is set to now, so
       lowering the height gives the cleared band back instead of leaving a bald verge. */
    {
        /* n64_pitch (game/cnc_eyes.cpp:5524) is the live tilt in radians and already
           carries the isometric row and the editor's free camera, so this follows a
           zoom or a dial change without a second copy of the rule. THE FLOOR IS ON THE
           TANGENT, because a cotangent runs away at the FLAT end: the isometric tilt is
           a dial floored only just above half the field of view, which is 26 degrees at
           the shipped field and 14.5 at the narrowest, and an unbounded cot there would
           ask for a fade band several cells wide. A floor on the cosine guards the far
           end instead, where the band should already be near nothing. */
        float tp = tanf(n64_pitch());
        if (tp < 0.35f) tp = 0.35f;                 /* 19.3 degrees and flatter */
        /* THE GENEROSITY IS AN EXPLICIT MULTIPLIER, not the wrong trigonometry standing
           in for one. A blade that leans and bends in the wind reaches further than one
           standing up, and a slightly wide bare margin costs far less than the halo the
           skirt exists to prevent. */
        /* AND IT IS THE TALLEST BLADE'S HEIGHT, NOT THE MEAN ONE. Every blade in the
           field is faded over one uSkirt, but the vertex stage draws each blade at
           uHeight * (1 + uVary * (hgt * 2 - 1)), so the tallest standing on the map is
           uHeight * (1 + uVary). A band sized from the mean is short by exactly the
           variation factor, which at the shipped spread is 31 per cent, and the blades
           it fails to cover are the tall ones along a coast and along the map rim,
           which are the most visible ones there are. */
        float skirt = g_fx.grass_height * (1.0f + g_fx.grass_vary) * 1.35f / tp;
        /* AND NEVER WIDER THAN THE BAKED DISTANCE CAN SAY. sd saturates at
           g_grassSdRange cells, so a band longer than that never reaches 1.0 for ANY
           blade on the map and the whole field, open ground included, drops to one
           uniform partial alpha. Said once, not once a frame. */
        if (skirt > g_grassSdRange) {
            static int toldSkirt = 0;
            if (!toldSkirt) {
                toldSkirt = 1;
                fprintf(stderr, "GRASS|skirt|%.3f cells clamped to the baked range "
                                "%.3f; lower the height or raise the bake ceiling\n",
                        (double)skirt, (double)g_grassSdRange);
            }
            skirt = g_grassSdRange;
        }
        fx_set1f(prog, "uSkirt", skirt);
    }

    /* the wind: one bearing over the map, the trees' */
    {
        float p1, p2, p3;
        grass_wind_phases(&p1, &p2, &p3);
        const float wdir = g_fx.tree3d_wind_dir * 0.01745329f;
        const float wx = cosf(wdir), wz = sinf(wdir);
        float len = g_fx.grass_gust_len;
        if (len < 0.5f) len = 0.5f;
        /* the second wave is shorter and turned 12 degrees, so the two beat instead of
           adding into one ruled front */
        const float c12 = 0.97814760f, s12 = 0.20791169f;
        const float w2x = wx * c12 - wz * s12;
        const float w2z = wx * s12 + wz * c12;
        fx_set2f(prog, "uWindDir", wx, wz);
        fx_set2f(prog, "uK1", wx / len, wz / len);
        fx_set2f(prog, "uK2", w2x / (len * 0.4f), w2z / (len * 0.4f));
        fx_set1f(prog, "uPh1", p1);
        fx_set1f(prog, "uPh2", p2);
        fx_set1f(prog, "uPh3", p3);
    }
    fx_set1f(prog, "uBend",   g_fx.grass_wind);
    fx_set1f(prog, "uArc",    g_fx.grass_arc);
    fx_set1f(prog, "uShiver", g_fx.grass_shiver);

    /* The pointer, as the host last handed it in. The strength dial is folded in here
       rather than at the call site so that turning it down to nothing in the panel is the
       same thing as having no pointer at all. */
    fx_set3f(prog, "uTouch", g_grassTouchX, g_grassTouchZ,
             g_grassTouchS * g_fx.grass_touch);
    fx_set1f(prog, "uTouchR", g_fx.grass_touch_r);

    /* Both fields are indexed by world position over the whole grid, which is the water
        field's convention exactly (game/water_mod.h:1530): the uv is world.xz
        over the grid. */
    fx_set2f(prog, "uFieldScale", 1.0f / (float)g_gridW, 1.0f / (float)g_gridH);

    /* THE DISTANCE LOD'S THREE NUMBERS, SET ONCE FOR THE PASS. The vertex stage runs the
       same ramp per blade that grass_tile_lod runs per tile, so the count a tile draws
       and the cut-off a blade obeys are read from one expression and cannot disagree. */
    {
        float far_, invBand, den;
        grass_lod_ramp(&far_, &invBand, &den);
        fx_set2f(prog, "uCamXZ", g_fxCamPos[0], g_fxCamPos[2]);
        fx_set2f(prog, "uFade", far_, invBand);
        fx_set1f(prog, "uDensity", den);
    }
    fx_set1f(prog, "uCrushGone", 0.80f);
    fx_set1f(prog, "uPatchLen", g_fx.grass_patch_len > 0.5f ? g_fx.grass_patch_len : 0.5f);

    /* colour and shape */
    fx_set3f(prog, "uGrassCol", g_fx.grass_r, g_fx.grass_g, g_fx.grass_b);
    fx_set1f(prog, "uTint",      g_fx.grass_tint);
    fx_set1f(prog, "uPatch",     g_fx.grass_patch);
    fx_set1f(prog, "uRootFade",  g_fx.grass_root_fade);
    /* uRootAlpha is NOT sent here. It is a fraction of the blade's HEIGHT, and how big
       a fraction has to be depends on how many pixels tall the blade is, which is a
       per-tile number. It goes down beside uEdge in the tile loop. */
    fx_set1f(prog, "uAO",        g_fx.grass_ao);
    fx_set1f(prog, "uTip",       g_fx.grass_tip);
    fx_set1f(prog, "uGain",      g_fx.grass_gain);

    /* THE LIGHT draw_terrain IS SENDING THIS FRAME, read through the same expression
       rather than reproduced from memory. Under ENHANCED with the shipped dials both
       terrain_normals and sun_lambert are 1 (game/fx_state.h:1023-1024), so the ground
       goes down at a FLAT 160/255 and the post chain supplies the slope; a blade that
       used terrain_shade there would be darkened on every hill by a term the ground
       beneath it was not drawn with. With either dial off the ground takes the
       cartridge's own baked corner shade, and the blade then takes it per TILE, which is
       as close as this pass can get without a per-corner sampler. */
    const bool oneSun = g_fxActive && g_fx.sun_lambert && g_fx.terrain_normals;
    const float flatLit = 160.0f / 255.0f;
    if (oneSun && g_grassLocLit >= 0)
        fx_glUniform1f(g_grassLocLit, flatLit);

    /* ---- the arrays and the one shared index list -------------------------------- */
    gr_glBindBuffer(GL_ARRAY_BUFFER, g_grassVBO);
    gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_grassIBO);
    /* THE CLIENT TEXTURE UNIT IS ASSUMED TO BE ZERO, and that assumption is the
       renderer's own: the one place that moves it is the tree's skinned bone pair, and
       tree3d_arrays_on puts it back to GL_TEXTURE0 before it returns for exactly this
       reason (game/cnc_eyes.cpp:8127-8135). Left on unit 1, the glTexCoordPointer below
       would arm the wrong unit and every blade would read a zero corner. */
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    /* HOW FAR A BLADE REACHES PAST ITS OWN CELL, for the cull margin only. Taken from
       the dials rather than from a baked constant, so the margin cannot fall behind a
       slider: the tallest a blade stands is uHeight * (1 + vary), and the lean and the
       wind bend both push its tip sideways by a fraction of that again. The two jobs a
       single reach constant used to do, "how tall a blade is" and "how far to widen the
       cull", differ by about 40 percent, and that disagreement is the kind that hides
       behind cell_in_view's own 2-cell slop until the day it does not. */
    const float reach = g_fx.grass_height * (1.0f + g_fx.grass_vary)
                        * (1.0f + g_fx.grass_lean + g_fx.grass_wind);

    /* BOTH SOFTENING RAMPS ARE SIZED IN PIXELS, NOT IN FRACTIONS OF A BLADE, and this is
       the block that converts one into the other. uEdge is a fraction of a card's HALF
       WIDTH and uRootAlpha a fraction of a blade's HEIGHT; at the far end of the zoom
       those two bodies are 0.67 px and 3.71 px, so the shipped dials buy a 0.20 px edge
       ramp and a 0.22 px root ramp. Both sit under the sampling grid. Neither softens
       anything: the card rasterises as exactly the hard-edged sub-pixel column the ramp
       exists to avoid, and the root ramp fires only on the blades whose foot happens to
       land within a fifth of a pixel of a fragment centre, which is a per-blade dimming
       of the bottom scanline rather than the join it promises.
       Converted per tile, each ramp is one sample wide wherever the tile actually is,
       and the two dials become the FLOOR rather than the value, so they still bite close
       up where a blade really is several pixels across.
       THE VIEWPORT AND NOT THE WINDOW. The world pass draws into the supersampled buffer
       while every projection still takes the window size, so the sample pitch is the
       window's scaled by the viewport's own height. Asking the viewport rather than the
       render-scale dial keeps this honest when the buffer is capped below what the dial
       asked for. */
    GLint vp[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, vp);
    const float ssY = (g_fxH > 0 && vp[3] > 0) ? (float)vp[3] / (float)g_fxH : 1.0f;
    /* the mean of the absolute cosine over a full turn. The yaw is baked per blade, so
       this is what a card's face-on width is worth on screen averaged over the field. */
    const float yawAvg = 0.63661977f;
    /* the same foreshortening the skirt above uses and for the same reason: a vertical
       blade is shortened on screen by the cosine of the camera's tilt. The floor is a
       guard, not a value. */
    float cpx = cosf(n64_pitch());
    if (cpx < 0.20f) cpx = 0.20f;
    /* one sample. A ramp narrower than this is a hard edge with extra arithmetic. */
    const float rampPx = 1.0f;

    unsigned drawnTiles = 0, drawnBlades = 0;
    for (size_t i = 0; i < g_grassTile.size(); i++) {
        const GrassTile& t = g_grassTile[i];
        if (t.blades == 0u)
            continue;
        if (!grass_tile_in_view(t, reach))
            continue;
        const unsigned blades = grass_tile_lod(t);
        if (blades == 0u)
            continue;

        /* uEdge and uRootAlpha are set HERE, per tile, and they EARN the per-tile
           upload: both are fractions of the blade's own body, and how wide a fraction
           has to be is decided by how big that body is on screen, which changes tile by
           tile. Sent as the bare dial they could not change inside the loop at all, and
           a value that cannot change belongs above it with the rest of the dials.
           px_per_cell_at is the renderer's own answer to the screen-size question, so
           this follows the zoom, the isometric row, the free camera and the orthographic
           mode without a second copy of the rule. */
        {
            const float tx = (float)t.cx + GRASS_TILE * 0.5f;
            const float tz = (float)t.cy + GRASS_TILE * 0.5f;
            const float pxCell   = px_per_cell_at(tx, tz, g_fxW, g_fxH) * ssY;
            const float halfWpx  = 0.5f * g_fx.grass_width * pxCell * yawAvg;
            const float bladeHpx = g_fx.grass_height * pxCell * cpx;
            float edge = halfWpx > 1.0e-3f ? rampPx / halfWpx : 1.0f;
            if (edge < g_fx.grass_edge) edge = g_fx.grass_edge;
            /* the whole half width and no further: past 1 the spine itself starts to
               fade, which trades the crawl for a field that washes out */
            if (edge > 1.0f) edge = 1.0f;
            float rootA = bladeHpx > 1.0e-3f ? rampPx / bladeHpx : 0.35f;
            if (rootA < g_fx.grass_root_alpha) rootA = g_fx.grass_root_alpha;
            /* a third of a blade is as far up as the join may reach before the blade
               reads as floating rather than rooted */
            if (rootA > 0.35f) rootA = 0.35f;
            if (g_grassLocEdge >= 0) fx_glUniform1f(g_grassLocEdge, edge);
            if (g_grassLocRootAlpha >= 0) fx_glUniform1f(g_grassLocRootAlpha, rootA);
        }
        if (!oneSun && g_grassLocLit >= 0)
            fx_glUniform1f(g_grassLocLit,
                           terrain_shade((int)t.cx + GRASS_TILE / 2,
                                         (int)t.cy + GRASS_TILE / 2));

        /* GL 2.1 HAS NO glDrawElementsBaseVertex (that is 3.2), so the only way to aim
           one shared 16-bit index list at a tile deep inside a 26 MB buffer is to move
           the three attribute pointers to that tile's first vertex and keep every index
           tile-relative. Three calls a tile, measured free against the draw itself. */
        const size_t base = (size_t)t.vert0 * sizeof(GrassVert);
        glVertexPointer  (3, GL_SHORT,         sizeof(GrassVert),
                          (const void*)(base + offsetof(GrassVert, px)));
        glTexCoordPointer(3, GL_SHORT,         sizeof(GrassVert),
                          (const void*)(base + offsetof(GrassVert, side)));
        glColorPointer   (4, GL_UNSIGNED_BYTE, sizeof(GrassVert),
                          (const void*)(base + offsetof(GrassVert, hgt)));
        glDrawElements(GL_TRIANGLES, (GLsizei)(blades * GRASS_IPB),
                       GL_UNSIGNED_SHORT, (const void*)0);
        drawnTiles++;
        drawnBlades += blades;
    }

    /* ---- state out, in the reverse order and nothing left over -------------------- */
    if (!sCA) glDisableClientState(GL_COLOR_ARRAY);
    if (!sTA) glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    if (!sVA) glDisableClientState(GL_VERTEX_ARRAY);

    /* THE UNBIND IS NOT HOUSEKEEPING, IT IS THE WHOLE BUG. With an ARRAY_BUFFER still
       bound, a later glVertexPointer given a HOST pointer is reinterpreted as a byte
       OFFSET into that buffer: glGetError stays clean and the draw puts nothing on the
       screen. tree3d_arrays_on uses host pointers (game/cnc_eyes.cpp:8123-8126), tree3d
       draws through a host INDEX pointer (game/cnc_eyes.cpp:8195), and
       draw_tiberium_solid uses host client arrays as well; every one of them runs later
       in the frame than this pass. Leave the buffer bound and the tiberium and every
       tree in the game vanish, silently. */
    gr_glBindBuffer(GL_ARRAY_BUFFER, 0);
    gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    fx_glUseProgram(0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);

    /* The factor and the units go back whether or not the enable does, because this pass
       wrote them and a later caller that enables the offset without setting them would
       inherit ours. */
    glPolygonOffset(sPolyF, sPolyU);
    if (!sPoly) glDisable(GL_POLYGON_OFFSET_FILL);
    if (sCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    /* THE ALPHA TEST GOES BACK EXACTLY AS IT WAS FOUND. The tree pass shipped a bug by
       disabling it on the way out: pass 5 enables it once and every cutout draw after
       that inherits it, so one stray glDisable silently turns the cut off for every
       fence, billboard and sprite drawn later in the frame. */
    if (sAlpha) glEnable(GL_ALPHA_TEST); else glDisable(GL_ALPHA_TEST);
    if (sBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFunc((GLenum)sSrc, (GLenum)sDst);
    glDepthMask(sDepthMask);
    glColor4f(sColour[0], sColour[1], sColour[2], sColour[3]);

    /* ONE LINE, ONCE. The gate scripts capture stderr, and a line that fired whenever
       the visible tile count changed would fire on most scrolling frames. */
    static int announced = 0;
    if (!announced && drawnTiles > 0u) {
        announced = 1;
        fprintf(stderr, "GRASS|draw|tiles=%u of %u|blades=%u|tris=%u\n",
                drawnTiles, (unsigned)g_grassTile.size(), drawnBlades, drawnBlades * 2u);
    }
    return drawnTiles > 0u;
}

/* ---------------------------------------------------------------------------------- *
 *  8. WHAT IS DELIBERATELY NOT HERE, so nobody looks for it and nobody adds it back by
 *     accident.
 *
 *  NO grass_draw_normals(). Grass writes no depth, so it can never punch a hole in the
 *  ground-normal buffer, so it must not be drawn into it. Drawing it there with the
 *  foliage alpha 0.5 would EXCLUDE the grass from the ground lambert the terrain under
 *  it receives and swap its SSAO weight, which is the exact inversion the keystone note
 *  at the top of this file describes, applied deliberately over every ground pixel.
 *
 *  NO SUN CASTER. The sun map is 4096 texels and the penumbra disc is about ten of them
 *  across at the default box, so a 0.045-cell blade is under three texels and at most
 *  8 percent of the disc's area; the receiver normal offset alone is 18 percent of the
 *  blade's own height, so a blade would largely bias itself off its own shadow. It
 *  cannot resolve, so it is not paid for.
 *
 *  NO TRANSMISSION TERM. The sun coming through a blade lit from behind is a real thing
 *  and the blade design has a dial for it, but it is a second light term on a pass whose
 *  whole premise is that the ground has already lit it. Left out until somebody looks at
 *  the picture and asks for it, rather than shipped and then argued about.
 *
 *  NO GRASS IN THE WATER MIRROR. water_reflection_render draws draw_terrain and the
 *  caster list only (game/water_mod.h:207-238), so the reflection has bare ground where
 *  the shore has grass. That is a line for the gap log, not an oversight.
 *
 *  NO CM TINT AT THE ROOT. draw_terrain's combiner is (texel - tint) * lit
 *  (game/cnc_eyes.cpp:6498-6501) and g_grassGroundTex carries the texel alone, so on the
 *  packs that ship a nonzero CM layer the blade root is up to 25/255 brighter than the
 *  ground it grows out of. Closing it means the bake folding terrain_tint into the
 *  ground field, which is that file's call and not this one's. Registered, not hidden.
 * ---------------------------------------------------------------------------------- */

#endif /* CNC3D_GRASS_DRAW_H */
