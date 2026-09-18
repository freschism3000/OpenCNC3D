# Tier 1 gap list (what the Windows 98 build gives up)

Two render tiers, see repository rules rule 1. Tier 2 is the modern desktop build and may use
shaders freely. Tier 1 is Win98 with a 3dfx Voodoo 2 through Glide: fixed function,
no shaders, no render targets, 640x480, 16-bit colour.

**This file is the contract.** Any feature that cannot run on Tier 1 belongs here, with
its Win98 fallback, before it ships. A feature that is Tier 2 only and NOT listed here is
a bug, because it converts the port from a known list of compromises into a surprise.

## Current entries

| Feature | Tier 2 | Tier 1 fallback |
|---|---|---|
| Texture wrap: clamp | `GL_CLAMP_TO_EDGE` (x4 sites, plus one in `game/dosopt_gl.h`) | `GL_CLAMP`; one texel of edge bleed, acceptable |
| Texture wrap: mirror | `GL_MIRRORED_REPEAT` (x1) | pre-mirrored texture baked at pack time, `GL_REPEAT` |
| Shatter: buildings, vehicles and aircraft (pieces fly, roll and settle) | a killed structure's display-list sections become rigid bodies: radial launch from the footprint centre, gravity, bounce, downhill roll on the PK9 heightfield, settle, linger, then sink into the ground while fading. `game/shatter_mod.h` | the section SHED alone (G90): the building disassembles over its eight-tick death window in the display list's own order and is gone. No flying pieces. **A VEHICLE GETS NOTHING AT ALL on Tier 1**, and cannot get the shed either: a vehicle has no death window to shed across -- measured, a Jeep goes from `str=12/150` straight to MISSING in one tick -- so Win98 keeps exactly what it has today, the cartridge's seven generic VehicleDamage debris chunks (DBRV0..6, baked and already firing: a dying Jeep spawns 129 particles and 15 chunks). That is the cartridge's own answer to a dying vehicle and it is unchanged. NOT a fidelity loss -- the cartridge has no authored building collapse either, it swaps to FRAG3 debris and that is all. **The blocker is not shaders and not the per-vertex 3x4** (12 multiplies on a vertex that already pays a bilinear shroud lookup); it is COUNT x DURATION x DRAW CALLS -- a full 128-piece pool is up to 384 separate `draw_mesh` calls and 128-plus texture binds per frame, for five to six seconds after the buildings are already gone. **The centre PULSE is excepted and ships on both tiers**: one particle in the existing sprite pool, fired above the shatter's own early-outs so switching the shatter off does not take it too. |
| Damaged building overlay | the cartridge's own per-building damage display list appended over the intact model when `Health_Ratio() < 0x80`: 2 to 78 triangles, one or two CI8 textures. BUILT 24 Aug 2026, decoded in `docs/damage-textures.md` | **SHIPS ON TIER 1, no gap.** Measured worst case if all 142 structures on the largest shipped map were visible and below half health at once: 2428 extra triangles, 7285 host vertex transforms, 568 `grTexSource` binds per frame, and 57.0 KB of texture against a 2 MB TMU. Realistic load is one to two orders of magnitude under that. No shader, no render target, no new state. The one Tier 1 detail is the z-fight epsilon, and the answer is the mesh-space Y bias already used at `cnc_eyes.cpp:5568`, not `grDepthBiasLevel`. |
| Landed-helicopter rotor rate (the cartridge's k=1 / k=2 rule) | `rotor_spin(o)` halves the sweep while `o.alt == 0`, so a helicopter on a pad or on the ground turns idling blades at exactly half the airborne rate. The 2:1 ratio is the console's, from RAM 0x80097A50 (k=2) and RAM 0x80097A4C (k=1); the absolute rate is ours. | NONE today, and the blocker is a MISSING FIELD, not the Voodoo. `tier1/w98_glidegame.c:1935` drives every rotor from one clock, `g_mesh.rotor = (ticks * 60) & 255`, with no altitude test, because `W98_Object` (`tier1/w98_brain.h:32`) carries no altitude and `wb_parse` (`tier1/w98_brain.c:820-843`) never reads `|alt=` -- although both brains already emit it (`dllinterface.cpp:8285` vanilla, `:8234` XL). The fix is three lines whenever Win98 is next touched: an `int alt;` in the struct, one `field_int(line, "|alt=", 0)`, and a conditional on the multiplier at :1935. No shader, no render state and no Glide call is involved. |

| Network multiplayer | deterministic lockstep over UDP: peers exchange 22-byte `EventClass` orders through a star, with a rendezvous for address exchange, hole punching, and a relay for the pairs that cannot punch through. 8 players on the classic brain, 16 to 24 on the Enhanced engine | **NONE. TIER 2 ONLY.** Not a Winsock limit, and that is worth stating because it is the guess everyone makes: Windows 98 SE ships Winsock 2.2, plain UDP is all a lockstep order stream needs, and hole punching requires no encryption, so the sockets themselves would be fine. Three other things rule it out, any one of them sufficient. **(1) Tier 1 is a separate program.** `tools/win98/build.sh` builds `w98glide.exe` out of `tier1/*.c` and never compiles `game/cnc_eyes.cpp`, so parity would mean writing the lobby, the turn scheduler and the transport twice, in two codebases that share no code. **(2) Lockstep runs at the pace of the slowest peer**, because every machine simulates every player's army; a Pentium-class box would set the frame rate for everyone else in the match. **(3) 16 to 24 players requires the Enhanced engine**, which never ships to Windows 98, so large matches are inherently Tier 2 whatever the transport does. **What stays possible and is not planned:** a LAN and direct-IP-only Tier 1 build, which needs no rendezvous, no relay and no encryption, and which is exactly what the 1995 game shipped. It would cost a session or two once the transport seam exists, and it is not promised. The menu already tells the truth today: `menu/dosmenu.c:62` marks Multiplayer disabled and `tier1/t1_menu.c:103` refuses the click. |

## The presentation chain (added 19 Aug 2026)

The desktop-only post chain: `game/fx_gl.h`, `fx_state.h`, `fx_post.h`, `fx_panel.h`,
plus `fx_filter.h` which is not part of the chain and is listed separately below.

**The whole chain is one row, because it has one fallback: it does not run.** With
`--gfx` absent and F5 never pressed the program draws exactly the picture it drew
before any of this existed, which gate G36 measures and the other 43 gates assume. On
Tier 1 the master switch is simply never on. Nothing about the Glide backend has to
know the chain exists.

| Feature | Tier 2 | Tier 1 fallback |
|---|---|---|
| Offscreen render target (`GL_EXT_framebuffer_object`, RGBA8 + a sampleable depth texture) | the world is drawn into it and the chain reads it back | none. The Voodoo 2 has no render target; the world goes straight to the front buffer as it does today |
| GLSL 1.20 fragment programs (six of them) | shadow/occlusion/light, bright pass, blur, resolve, CRT, copy | none. Fixed function only |
| Supersampling | world drawn at up to 4x the window, box filtered down | none. The Voodoo 2 renders 640x480 and that is the resolution. Note the console DID anti-alias (RDP coverage plus the VI filter), so this is a gap on Tier 1 against the cartridge as well as against Tier 2 |
| Sun shadow map (depth-only FBO, screen-space application) | replaces the cartridge's four baked shadow mechanisms while it is on | the four baked mechanisms, which is what ships today and is what the console does |
| Bloom, ambient occlusion, dynamic light, colour grade, CRT | shader passes | none, and none of them is a fidelity loss: the cartridge has no equivalent for any of them |
| **VI output gamma** | `pow(c, gamma)` in the resolve pass | **THIS ONE IS A REAL LOSS.** `VI_CTRL = 0x0000320E` sets GAMMA_ON on the console and nothing clears it, so Tier 1 without it is less faithful than Tier 2 with it. A Glide answer exists and is not written: `grGammaCorrectionValue()` sets the hardware gamma table on a Voodoo 2. It is one call and it belongs in the Glide backend when that is built |

### `fx_filter.h`: bilinear filtering, and it is NOT a Tier 2 debt

| Feature | Tier 2 | Tier 1 answer |
|---|---|---|
| One switch flipping every texture drawn IN THE WORLD between `GL_NEAREST` and `GL_LINEAR`: the 3D pack art and terrain atlas, the DOS infantry sprites, the tiberium, the decals, the construction scaffold. **Never the UI** -- not the sidebar, cameos, fonts, cursors, pause dialog, menus or movies | `glTexParameteri` over a register of every uploaded WORLD texture; UI textures are simply never registered, so the switch has no way to reach them | **the Voodoo 2 filters bilinearly in hardware and has since 1996.** `grTexFilterMode(GR_TEXTUREFILTER_BILINEAR)` per TMU, set per texture source rather than globally. This is one of the few additions Tier 1 can simply do |

Worth recording alongside it: the console's RDP filtered its textures bilinearly, so
point sampling is OUR choice, not the cartridge's. Whichever way the project owner sets this switch
it is a decision, and the default (off, point sampled) is the one that keeps every
existing gate's pixels where they were.

## Nothing else is Tier 2 only yet

Everything currently shipped (soft shroud, DOS sprites, particle effects, turret
rotation, the sidebar, the menu, the movies and the pause dialog) runs in fixed
function OpenGL 1.1 and maps to Glide.

The movie player (`video/movieplay.c`) is one `GL_NEAREST` texture and one quad, with
the palette conversion and the fade done on the CPU exactly as the DOS original faded
the VGA palette. The pause dialog (`game/dosopt.c` + `game/dosopt_gl.h`) is one 8-bit
surface, one texture and one quad, drawn with the same `dosbar.c` primitives as the
sidebar. Neither needs anything the Voodoo 2 does not have.

## Audio (added with the sound round)

The audio engine is under the same contract and currently costs Tier 1 **nothing**,
because nothing in the mixer touches a platform API.

| Piece | Tier 2 | Tier 1 answer |
|---|---|---|
| Mixer, `.AUD` decoders, MIX reader, sound bank, theme playlist, the WAV tap | plain C89, no platform call, no C++ | the identical files, handed to a different C compiler |
| The device | `audio/audio_sdl.c`: an SDL2 callback at 22050 Hz stereo, 1024 frame buffer | **a new file with the same five functions.** A DirectSound secondary buffer or a `waveOut` double buffer, calling `mixer_render()` at the refill point; `audio_backend_lock/unlock` become a critical section, or nothing at all if the refill happens on the main loop |
| Music streaming | `cnc_audio_update()` decodes on the GAME thread and pushes into a ring the device only reads | unchanged, and it is the whole reason a Win98 backend does not have to make stdio interrupt safe |
| Decoded clip cache | 8 MB LRU cap | unchanged. The cap exists precisely for this target: decoding all 249 sounds at once is about 12 MB, which is fine on a modern machine and is not fine on Win98 |

**`audio/audio_win98.c` IS WRITTEN NOW, and it is a new file, not a
rewrite.** It implements the same six functions `audio_sdl.c` does and touches nothing
else, over `waveOut` rather than DirectSound: winmm.dll is on every Windows 98 install
as shipped, where DirectSound would add a DirectX runtime to require, detect and fall
back from, and it buys nothing when the mixer already hands the device a finished stereo
stream.

The one delicate part is the threading, and it is written down in the file: a waveOut
callback runs in an interrupt-like context where Microsoft names the only legal calls,
and `mixer_render` is not among them. So the callback does exactly one legal thing,
`SetEvent`, and a worker thread refills and re-queues the four buffers. That makes
`audio_backend_lock/unlock` a CRITICAL_SECTION around the render, which is precisely the
guarantee `SDL_LockAudioDevice` gives on the other backend.

**What is proven and what is not.** It compiles as 32-bit Windows code with `-Wall
-Wextra` and no warnings, and it links against the whole audio core with `-lwinmm`,
which is what says the seam is satisfied rather than merely typed. **It has never run on
Windows 98**, because the renderer's own Win98 and Glide port has not started, so there
is no build on that machine to hear it. CI compiles it on every push, alone, so that it
cannot rot while it waits.

**A REAL Tier 1 finding fell out of that link, and it is not about audio.** The probe
binary imports `api-ms-win-crt-*.dll`: that is the UCRT, which Homebrew's mingw-w64
links by default and which **does not exist on Windows 98**. Ubuntu's mingw-w64 (what CI
uses) is the MSVCRT variant and imports `msvcrt.dll`, which does. `known-gap notes`
already records that the two toolchains differ and that both run on Windows 11; what is
new is that the difference is load-bearing for Tier 1. **The Win98 build must be made
with the MSVCRT variant**, whichever machine makes it, and that is a toolchain decision
to settle before the port starts rather than a link error to discover during it.

## Construction animation (added with the buildup round, 14 Aug 2026)

The 1995 buildup pass costs Tier 1 **nothing** by construction: `dosmake.pack` sheets
are all 256x256 or smaller (the baker PAGES a long strip across several sheets rather
than exceeding the Voodoo 2 texture limit -- TMPL's 36 frames span 4 sheets), the
loader uses GL 1.1 calls only (`GL_CLAMP`, no new tokens, same audited wrap-ledger
reasoning as the infantry loader), and the draw is one alpha-tested quad per building
under construction. The only Tier 1 cost is one texture bind per constructing
building per frame, on a 5-second one-shot animation.

## Wave 4: water, per-object shroud, health bars

Everything in this round is fixed-function and lands inside the Tier 1 contract. The one
item that adds a hardware requirement is the water, and the Voodoo 2 is the machine it was
designed for.

| Piece | Tier 2 | Tier 1 answer |
|---|---|---|
| **Soft decal edges** (`decal_soft`, 21 Aug 2026) | A box blur of the smudge sheet's ALPHA channel at load, per frame cell, then `GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA` with a floor-level alpha test instead of the 0.5 test | **NATIVE, and it is not even a compromise.** The blur is a CPU pass over 270 KB at load, which the Pentium-class target does once. The draw needs `grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA)` plus `grAlphaTestFunction(GR_CMP_GREATER)` at a low reference, which is one state change and exactly what the console's own FORCE_BL composite does. The alpha test path is kept for `decal_soft 0` and is the cheaper of the two, so a Voodoo 2 that wants the frames back can have the cartridge's hard edge for free. **This is a DEVIATION, not a fidelity feature:** the console draws these hard, and 0 restores it byte for byte |
| **Water surface** | Two texture units, both `GL_MODULATE`, primary colour `(1,1,1,170/255)`, one `GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA` blend, depth write off, `GL_LEQUAL` | **Native.** The Voodoo 2 has TWO TMUs: `grTexCombine` MODULATE on TMU0 against TMU1's output, iterated (Gouraud) alpha, `grAlphaBlendFunction(SRC_ALPHA, ONE_MINUS_SRC_ALPHA)`, `grDepthMask(FXFALSE)`, `GR_CMP_LEQUAL`. This is the card's home ground rather than a compromise. Watch item only: 16-bit colour will band the WATER1 x WATER2 product slightly, which is a 16-bit fact and not a feature loss. **If a single-TMU fallback is ever needed it is NOT the console's lerp** and must be registered as a deviation, not shipped quietly. |
| **Water UV warp** | Two `sinf` and two `cosf` per water corner per frame, on the CPU | Unchanged. SCG01EA has 389 water cells, so under 1600 corners per frame; a Pentium-class box handles that. If it is ever tight, a 256-entry sine table matches the console's own quantisation exactly (it truncates S/T to s16, i.e. 1/96 of a repeat). |
| **Seabed (BOTTOM.IMG)** | One extra opaque textured pass over water cells only, single TMU, `GL_REPEAT` on a 32x32 tile | Unchanged, and it is CHEAPER than the water pass above it. |
| **Coincident water/terrain depth** | Depth write off plus `GL_LEQUAL` on surfaces that share vertices | Same on Glide. Watch item: 16-bit Z precision at 640x480 on coincident surfaces. The console faces the identical situation and solves it the identical way, so if it sparkles the answer is a depth-range tweak, **not** `glPolygonOffset`, which Glide does not have. |
| **Per-object shroud darkening** | One bilinear lookup per vertex, subtracted from the vertex colour before submission | Unchanged: this is per-VERTEX COLOUR arithmetic done on the CPU, which is exactly what fixed function can consume. No shader, no second pass, no render target. The only Tier 1 cost is the lookup itself, and it is four array reads and a lerp. |
| **Vehicle washboard lean** | Two `sinf` per drawn vehicle per frame plus a 3x3 rotate folded into the existing per-vertex transform | Unchanged. Pure CPU arithmetic on geometry we already transform by hand. |
| **World-space health bars** | Two untextured quads per bar, depth tested and written, no blend | Unchanged, and strictly cheaper than the screen-space overlay it replaced (which needed an ortho projection switch per frame). |

Nothing in this round introduced a shader, a render target, a new GL token or a texture
larger than 256x256.

## Wave 5: the MCV deploy rig

Nothing here introduces a shader, a render target, a new GL token or a texture larger than
256x256. The rig rides the PKB node-animation path that already existed for the idle
clips, so its Tier 1 answer is that path's answer.

| Piece | Tier 2 | Tier 1 answer |
|---|---|---|
| **Per-node animated draw (`draw_mesh` animT)** | Per drawn part, one lerp between two baked 3x4 matrices, then a 3x4 multiply per vertex. Immediate-mode `glVertex3f`, no VBO, no shader | **Unchanged.** The vertices are transformed on the CPU already, in the same loop that applies facing, mount and lean; the animation is one more 3x4 before `grDrawTriangle`. Cost is bounded by the rig's 278 triangles and it draws for at most one object at a time. |
| **Baked clip storage** | 102 frames x 21 parts x 12 floats = 103 KB of `float` per pack, resident | **Watch item, not a blocker.** A Win98 target machine has this, but it is the largest single animation block in the pack and a 16 MB build should measure the pack's total resident size before adding more one-shot rigs. Halving it to `short` fixed-point is available and was NOT done, because nothing needs it yet. |
| **The rig REPLACES the yard's mesh** | One `draw_mesh` instead of another, same three passes | Unchanged: strictly fewer triangles than the finished Construction Yard it stands in for. |

## Wave 5: the cursor animations

Nothing here introduces a shader, a render target, a new GL token or a texture larger than
256x256. The ten animated cursor states reach the screen through two paths that already
had Tier 1 answers, so this section is mostly a statement that nothing new was spent.

| Piece | Tier 2 | Tier 1 answer |
|---|---|---|
| **Animated cursor nodes (codes 0x03, 0x04, 0x06, 0x08, 0x09)** | The PKB node-animation path: one lerp between two baked 3x4 matrices per part, then a 3x4 multiply per vertex, immediate mode | **Unchanged**, and it is the cheapest user that path will ever have: the whole cursor is 5 parts and at most 65 triangles, drawn once per frame. The clock is an integer multiply and a modulo. |
| **Baked cursor clips** | 100 frames x 5 parts x 12 floats = 24 KB per animated cursor, 5 of them, 120 KB per pack resident | **Watch item, not a blocker**, and it is the same watch item the MCV rig already raised: the packs' total resident animation is now about 230 KB. The same `short` fixed-point halving is available and again was not done because nothing needs it. |
| **Flipbook cursors (0x0A, 0x0B) as per-frame mesh variants** | 7 extra meshes (4 x 124 and 3 x 22 triangles) and 7 extra textures, at most 8x8 and 2x6 texels | **Native, and deliberately so.** The alternative -- rebinding a texture mid-mesh from a per-frame index table -- would also have worked on Glide, but the variant meshes need NO new mechanism at all: the renderer picks a mesh index and draws it, which is what it does for every other model. The textures are 256 and 24 bytes of RGBA each; `grTexDownloadMipMap` will not notice them. |
| **`cursor3dstate` / `cursor3danim` script verbs** | Test-only, no draw cost | Not shipped behaviour: they change which of the cartridge's own 20 state rows is used, and both default to the engine's answer. No Tier 1 surface at all. |


## Wave 5: the terrain CM tint layer

**This is the one place so far where the Voodoo 2 is BETTER than desktop GL fixed
function, and the answer is that it can express the console's combiner exactly, in one
TMU and one pass.** That is worth stating plainly because the reflex on reading
"subtract before modulate" is to assume a Tier 1 gap, and there is none here.

The cartridge's ground combiner (G_SETCOMBINE 0xFC15982B / 0x4433FFFF at ROM 0x19A5AC,
G_CYC_1CYCLE at ROM 0x19C3C0) is, per channel:

```
out = clamp8( ((TEXEL0 - tint) * lit + 0x80) >> 8 )
```

tint = the vertex RGB (the CM layer), lit = the vertex ALPHA (the per-corner light).

**It cannot fold into the vertex colour.** `GL_MODULATE` gives `T * C`, and matching the
console needs `C = lit/256 - (tint*lit)/(256*T)`, a function of the TEXEL. The subtracted
term is an offset, not a scale, so no vertex colour reproduces it on more than one texel
value. Both tiers therefore need a combine form, not a colour.

| Piece | Tier 2 | Tier 1 answer |
|---|---|---|
| **The subtract-then-modulate itself** | TWO texture env stages. Unit 0 `GL_COMBINE` / `GL_SUBTRACT(TEXTURE, PRIMARY_COLOR)`; unit 1 `GL_COMBINE` / `GL_MODULATE(PREVIOUS, PRIMARY_COLOR` with `GL_SRC_ALPHA` as operand 1`)`. Unit 1 samples nothing and is bound to the same atlas only because an env stage must be enabled to apply. New GL tokens: `GL_COMBINE`, `GL_SUBTRACT`, `GL_PREVIOUS`, `GL_PRIMARY_COLOR`, `GL_SOURCE0/1_RGB`, `GL_OPERAND0/1_RGB`, `GL_COMBINE_RGB/ALPHA`, `GL_SOURCE0_ALPHA`, `GL_OPERAND0_ALPHA` (GL 1.3 core / `ARB_texture_env_combine`) | **NATIVE, exactly, in ONE TMU.** Glide's colour combine has the form as a first-class function: `grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL, GR_COMBINE_FACTOR_LOCAL_ALPHA, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE)` = `(texture - iterated_rgb) * iterated_alpha`, which IS the cartridge's arm, with the same clamp at the combine output. The Gouraud vertex carries tint in RGB and light in alpha exactly as it does on GL. **So Tier 1 is one pass and one TMU where Tier 2 needs two stages, and TMU1 stays free for the water.** |
| **The alpha lane (the atlas water hole)** | Both stages `GL_REPLACE` the alpha from the texel, so the existing `glAlphaFunc(GL_GREATER, 0.5)` still cuts the river out of the ground | `grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, ..., GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE)` plus `grAlphaTestFunction(GR_CMP_GREATER)` / `grAlphaTestReferenceValue(128)`. The vertex alpha is the LIGHT here and must not reach the alpha test, on either tier. |
| **The CM data itself** | 4225 u16 in the pack (8450 bytes per pack) plus a 65x65x3 byte precomputed grid, built once at load with the light | **Unchanged and negligible**: 8 KB in the pack, 12 KB resident, no per-frame arithmetic at all. |
| **Cost when the layer is a no-op** | `terrain_tint_live()` falls back to the single-stage `GL_MODULATE` when the pack has no CM block or the scaled tint is zero everywhere (CMFLAT scenarios), so those maps pay nothing | Same branch, same reasoning. |

**The one deviation, and it is a rounding one on BOTH tiers.** The RDP divides by 256
with a `+0x80` round; GL's modulate divides by 255, and Glide's combine unit likewise
works in 1/255. So a terrain pixel can come out one level brighter than the console's.
Measured by the `cmcombine` script verb against read-back pixels over 180
(texel, tint, lit) triples: **146 exact, 34 one level brighter, 0 darker.** That is the
signature of the divide and nothing else. It is not new -- the plain modulate this
replaced carried the identical /255 -- and it is registered in `known-gap notes` rather
than corrected, because pre-scaling the vertex alpha by 255/256 would move every terrain
pixel that already exists for a sub-level gain.

**Not built, and it is a Tier 1 item too when it is:** the cartridge's SEABED pass uses
the SIBLING combiner arm (ROM 0x19A590, `(TEXEL0 - 0) * SHADE_ALPHA + SHADE`), i.e. it
ADDS the same tint to the sea floor. On Glide that is
`GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL` with the same factor, so it is equally
native when someone builds it. See `known-gap notes`.

## The software renderer (added 19 Aug 2026)

Tier 1 now has TWO possible backends, not one, and this table gained a column in spirit:
a **software rasteriser** (`tier1/softras.c`, running today on the box) and **Glide on the
Voodoo 2** (not yet built). the project owner's call on 19 Aug was to prove the path in software first,
because a CPU rasteriser removes the 3dfx driver from every early failure, can be gated on
the Mac before it reaches Win98 hardware, and unlike a Voodoo is visible over VNC.

The software backend changes what some rows above mean, so they are restated here rather
than left to be misread:

| Feature | Tier 2 | Tier 1 software | Tier 1 Glide |
|---|---|---|---|
| Texture filtering | `GL_LINEAR` or `GL_NEAREST` via `fx_filter.h` | point sampled only. Bilinear in software costs four texel fetches and three lerps per pixel and is not affordable on a 534 MHz Pentium III | bilinear free in hardware, `grTexFilterMode` |
| Texture wrap | `GL_CLAMP_TO_EDGE`, `GL_MIRRORED_REPEAT` | the rasteriser MASKS, so it is `GL_REPEAT` semantics only and every texture must be a power of two. Clamp and mirror have to be baked into the texture at pack time | `GL_CLAMP` equivalent exists |
| Texture size | any | power of two only, and no size limit beyond RAM | power of two, and the TMU is 4 MB total |
| Perspective correction | free | subdivided: one divide per 8 pixels, linear in between. This is the era's technique and it is in `SR_SPAN` | free in hardware |
| Lighting | per-vertex or per-fragment | **a 32 level palette shade table**, one indexed load per pixel. Not a multiply. Colour is palettised end to end, which is what the art already is | per-vertex iterated colour, and **Glide WRAPS a component above 255 to near-black instead of clamping**, so the caller must clamp |
| Depth buffer | 24 bit | a float 1/w buffer today, which is one float compare and one float store per pixel. A 16 bit fixed point buffer is the era's answer and is owed | hardware |
| Near plane clipping | free | **absent.** Triangles with any vertex at or behind the eye are dropped whole. See `known-gap notes` | absent for the same reason until written |
| Output | window or fullscreen GL | a 32 bit DIB section blitted with `BitBlt`. No page flip, no vsync, and it goes through GDI | fullscreen only; a Voodoo 2 has no 2D output of its own |
| VI output gamma | `pow(c, gamma)` in the resolve pass | **free and exact.** The gamma can be folded into the shade table when it is built, at zero per-pixel cost. This closes the one real fidelity loss the Glide row above admits to | `grGammaCorrectionValue()` |

**The software backend's speed is currently unexplained and roughly 50x off**, which is
recorded in `known-gap notes` rather than here, because it is a defect to fix and not a
declared compromise.

### Two Tier 1 decisions that are open, and are the project owner's

**1. The pixel format of the 3D art.** The software rasteriser is palettised end to end:
8-bit texture indices and a 32-level shade table, one indexed load per pixel. That is what
makes it fast enough to exist. The DOS 2D art is already 8-bit and fits perfectly. **The 3D
scenario art does not.** `game/cnc_eyes.cpp:18-20` describes the scenario packs as textures
"already decoded to RGBA" with meshes carrying "per-vertex UV and RGBA", and the terrain's
per-corner RGBA is a CM tint plus a light, which a single scalar cannot carry.

| Option | What it costs | What it keeps |
|---|---|---|
| (a) Quantise scenario art to a shared 256 colour palette at bake time | a bake pipeline change, some colour fidelity, and an answer for the per-vertex tint | the fast inner loop |
| (b) Give the rasteriser an RGBA path | the shade table trick, which is the difference between a playable frame rate and a slideshow | exact art |
| (c) Hybrid: palettised terrain and units, RGBA for the few surfaces that need it | two inner loops to maintain | most of both |

**2. The Tier 1 resolution. ANSWERED, 19 Aug 2026, by measuring the card.**

Fill rate arithmetic against real pack data puts an in-mission frame at roughly 5,700 to
8,000 triangles and about 1.1 million pixel writes at 640x480. Even at 15 cycles per pixel
that is 31 ms of fill alone on a 534 MHz Pentium III, so **640x480 at 30 FPS is not
reachable in SOFTWARE on this machine.**

It is comfortably reachable on the Voodoo 2, and that is now measured rather than argued.
`tier1/t1_glidetest.c` on the real card, drawing full-screen textured, z-buffered,
bilinear-filtered quads so the pixel count is exact rather than estimated from triangle
areas:

| load | FPS | fill |
|---|---|---|
| 4 full-screen quads per frame | 118.87 | 146.1 Mpixel/s |
| 16 full-screen quads per frame | 32.58 | 160.1 Mpixel/s |

**About 160 million textured, depth-tested pixels per second**, converging upward as the
per-frame overhead is amortised. Glide reports one board because an SLI pair presents as
one, which is consistent with roughly 90 Mpixel/s per Voodoo 2.

Against the software renderer's measured 254,541 pixels in 36.00 ms, which is
**7.07 Mpixel/s**, the card is **about 23 times faster at the thing that was the
constraint**. The same terrain frame costs 36.00 ms in software and about 1.6 ms on the
Voodoo. The depth clear (6.80 ms) and the GDI `BitBlt` (15.35 ms) both disappear entirely,
because the card clears and page-flips in hardware.

So the resolution question is not a compromise to be chosen: **Tier 1 runs at 640x480 and
the frame stops being fill bound at all.** What is left is CPU side, which is scene
assembly, the brain tick and the object dump.

Separately, the triangle rate was measured at roughly 15,900 per second with these large
triangles, and it stayed flat as the count went 50 -> 200 -> 800 while FPS scaled inversely,
confirming the card is fill bound and not setup bound at this geometry.


## Wave 7: what the SECOND Win98 pass added to Tier 1

Nothing in this round is a Tier 2 debt. Three of the four are places where Tier 1 needed a
mechanism the desktop build gets for free from GL, and the fourth is a straight Tier 1 WIN.

| Piece | Tier 2 | Tier 1 answer |
|---|---|---|
| **Texture coordinates** | GL takes normalised 0..1 and the driver scales by the bound texture's size | **A Voodoo's coordinates live in a fixed 0..256 space stretched over whatever LOD is bound**, so texels must be scaled by 256/max(w,h) at submission. There is no Tier 2 equivalent because there is no problem on Tier 2. Getting this wrong drew ONE TEXEL of every object texture over the whole polygon, and it did it silently for a fortnight because every page-shaped texture in the game is 256 across, where the scale is 1 |
| **Shadow textures** | `GL_ALPHA` or an RGBA upload; the desktop keeps the source RGBA and lets `GL_MODULATE` do the rest | **A dedicated `GR_TEXFMT_ALPHA_8` bank**, 18 KB for all 35, because the palettised bank cannot hold them at all: every shadow texture is white RGB with its coverage in ALPHA and 34 of the 35 peak below the converter's alpha >= 128 threshold, so they were already fully transparent |
| **The shadow combiner** | `glColor4ub` plus `GL_MODULATE` and an RGBA texture | **NATIVE, and closer to the cartridge than GL is.** The RDP's shadow arm is FCFFFFFF/FFFDF2F9 = "RGB = PRIM, alpha = TEXEL0_A", and Glide expresses each half as a first-class function: `grColorCombine(FUNCTION_LOCAL, ..., LOCAL_ITERATED, OTHER_NONE)` for the colour and `grAlphaCombine(FUNCTION_SCALE_OTHER, FACTOR_LOCAL, LOCAL_ITERATED, OTHER_TEXTURE)` for the alpha. That is the ROM's combiner term for term, in two calls |
| **VQA movie frames** | Decoded indices expanded to RGBA on the CPU every frame, then `glTexSubImage2D` | **NATIVE and CHEAPER.** A decoded VQA frame is 8-bit palette indices plus 768 bytes of palette, which is exactly `GR_TEXFMT_P_8` plus `grTexDownloadTable`. The whole RGBA expansion does not happen on this tier at all; the decoded rows are memcpy'd into four pages and uploaded as they are |

**One new watch item, and it is a real one.** The mesh bank grew from 0.75 MB to 1.92 MB
when the baked node animation (1,091 KB of pose) and the shadow alpha planes (18 KB) were
converted. That is resident on a machine with about 388 MB free, so it is comfortable
today, but it is the largest single block in the build and halving the pose to `short`
fixed point is still available and still not needed.


## Team colours on infantry (added with the per-seat livery round)

| Piece | Tier 2 | Tier 1 answer |
|---|---|---|
| **A skirmish seat's colour on its FOOT SOLDIERS** | up to six extra copies of the 1995 infantry sheets, the uniform band (palette 176..191) substituted per `PlayerColorType`. Measured on the shipping pack: 102 of its 397 strips qualify, 1.43 M texels, so **5.5 MB on the card per colour and 33 MB with all six in play** | **none by default, and that is a deliberate cut rather than an oversight.** It is thirty times what the mesh liveries cost (0.18 MB per colour) and a Voodoo 2 has 4 MB of texture memory in total, so Tier 1 keeps the pack's own two colourways on infantry and puts the seat's colour on vehicles, buildings, selection boxes, radar blips and rally flags only. **A much cheaper Glide answer exists and is not written:** these sheets are 8-bit and Glide is natively palettised, so `GR_TEXFMT_P_8` plus one `grTexDownloadTable` per colour makes a livery cost a 768 byte palette instead of a second copy of the art. On that route Tier 1 could carry all six for less than one costs on Tier 2 |


## The unit card

| Piece | Tier 2 | Tier 1 answer |
|---|---|---|
| **The selection card in the bottom-left corner of the Enhanced HUD** (cameo, name, health, damage, the rest of the selection as smaller cameos, ten control-group tabs) | composed on the CPU in `game/hud640.c` (`hud640_draw_card`, plain C89) out of assets in `hud640.pack`, uploaded through the bar's own `h6_upload` and drawn as ONE 240x166 textured quad per frame, alpha blended | **SHIPS WHEN THE 640x480 HUD SHIPS, with no new mechanism.** It is the same rasteriser and the same blit the bar already is on this tier; the one texture is 240x166 RGBA in a 256x256 page, 256 KB once. The alpha blend is only for the bezel's rounded corners and can be a chroma key on Glide. The Enhanced-only gate is the codex's gate (`codex_available`), and Tier 1 never enters Enhanced, so on Win98 the card is simply absent, exactly like the DATABASE plate. What Tier 1 does not have is the `group=` field in its own brain reader (`tier1/w98_brain.c`), which is the same three-line shape as the `alt=` gap recorded above. |

## The primary-factory label

| Piece | Tier 2 | Tier 1 answer |
|---|---|---|
| **The word "Primary" under a selected factory** | one 8-bit DOS surface rasterised once with `db_print` in 8POINT, widened to RGBA, uploaded as a 64x16 GL texture and drawn as one alpha-tested screen quad per label | **the same thing minus the widening step, and it is CHEAPER here than on the desktop.** `game/dosbar.c` is compiled into the Win98 build UNEDITED, so `db_font`, `db_print`, `db_string_width`, `db_font_palette_full` and the 8POINT glyphs are all present today at no additional cost. The plate is 8-bit palettised, which is the Voodoo's native `GR_TEXFMT_P_8`, so this tier skips `db_surface_to_rgba` entirely and lets the chroma key drop index 0 the way the HUD pages already do. 1 KB of a 4 MB TMU, uploaded once, because the string never changes. The projection is `t1_world_to_eye` plus the same divide `draw_brackets` already performs per selected object, and the brain field arrives through `field_int(line, "|primary=", 0)` beside `o->sel`, so an older brain reads 0 |

**The plate is authored 64x16 on BOTH tiers for this tier's sake.** The software rasteriser
masks rather than clamps, so every texture there must be a power of two; 42x11 of ink inside a
64x16 page satisfies that and costs the desktop nothing.

**What is actually absent on Tier 1 today is the whole per-object overlay family, not this.**
There are no health bars, no pip rows and no repair wrench on the Win98 build: it draws
selection brackets off the engine's own `sel` flag and nothing else per object. So the honest
row is that the label ships on Tier 1 with no new mechanism and no shader, one chroma-keyed
textured quad and 1 KB of texture, and it lands when that family lands, which it has not yet.
It is not a Tier 2 exclusive and it is not a blocker; it is one more quad on a list that is
currently empty.

Two traps to carry over when it is written, both already on the recorded list: texture
coordinates live in a fixed 0..256 space stretched over the bound LOD, so the UVs cropping the
ink out of the 64x16 page must be scaled by 256/64, and an unbounded quad wedges the card.

One placement difference to declare rather than discover: Tier 2 anchors the label to the
bottom of the object's projected MESH silhouette, and Tier 1 has no mesh-silhouette bound, so
it anchors to the object's ground point plus a fixed offset in HUD pixels. Identical on a squat
building, a little high on a tall one.

## Solid tiberium

| Feature | Tier 2 | Tier 1 answer |
|---|---|---|
| **Crystal clumps standing on a tiberium cell** (`game/tib3d_mod.h`, `draw_tiberium_ground`, `draw_tiberium_solid`) | three passes out of `tib3d.pack`: the ground cracks (one additive quad per CRYSTAL of a generated 128x128 tile, `GL_ONE, GL_ONE` over a premultiplied sheet), the clumps themselves (four indexed meshes on one 256x256 sheet, opaque, from a per-cell instance list rebuilt only when the field changes and drawn through client vertex arrays with `glDrawElements`), and a glow (the same batch added over itself with `GL_SRC_ALPHA, GL_ONE` through the emissive mask baked into that sheet's alpha) | **SHIPS ON TIER 1 as far as the API goes, and is NOT WIRED THERE.** Nothing in the three passes needs anything above GL 1.1: no shader, no render target, no multitexture, no buffer object, and the element type is `GL_UNSIGNED_SHORT` because the batch flushes before 65535 vertices rather than reaching for `GL_UNSIGNED_INT`. Two textures in the whole feature, 256x256 and 128x128, both powers of two at or under the Voodoo 2's per-side limit; at 16-bit that is 160 KB of a 4 MB TMU. The clump pass is opaque with depth writes on, so there is nothing to sort and no alpha test. **The emissive glow is the one part that costs the Voodoo a second look:** it is `GL_SRC_ALPHA, GL_ONE` over the same geometry, which Glide does natively, but it doubles the fill for a field and the Voodoo 2 is fill limited before it is triangle limited. Its dial is `tib3d_glow` and 0 removes the pass entirely, so the Tier 1 answer if it turns out too expensive is a number rather than a rewrite. The crack decals are additive too and there is one per crystal rather than per cell, so they are the other half of that fill; `tib3d_ground` is their own dial and 0 removes them. They bind with `GL_CLAMP`, chosen over `GL_CLAMP_TO_EDGE` for this tier, and rely on the tile's black border making a rotated sample outside 0..1 add nothing. What is missing is the same thing missing for the shatter and the overlay family: **Tier 1 is a separate program** (`tools/win98/build.sh` builds `w98glide.exe` out of `tier1/*.c` and never compiles `game/cnc_eyes.cpp`), so the pack reader, the instance list and the draw would have to be written a second time. Until they are, Win98 draws the flat cartridge overlay alone, which is the console's own picture and therefore not a fidelity loss. |

**The cost to watch when it is ported is triangle COUNT, not the API.** A field on screen
is a few hundred clumps of 84 to 252 triangles each: measured on the largest field in
SCG05EA, 450 clumps drawn of 618 placed over 140 cells. The `tib3d_density` dial exists
partly for this -- at 0.25 the same field is a quarter of the geometry -- and the view-bounds
cull is already in the pass rather than left to the card.

## Real grass on the ground

| Feature | Tier 2 | Tier 1 answer |
|---|---|---|
| **Blades standing on every grass cell** (`game/grass_bake.h`, `game/grass_field.h`, `game/grass_draw.h`, `grass_draw()`) | one static vertex buffer built at load holding every blade in the map as two triangles, drawn alpha blended with depth writes OFF through a GLSL 1.20 program that makes TWO VERTEX-STAGE texture fetches; a map-wide RGBA16F render target carrying tread, cover and shroud, updated with `GL_MAX` and a reverse-subtract blend equation; and the blade's colour read back off the GPU from whichever terrain atlas is drawn | **ABSENT ON TIER 1, and this is a deeper gap than the tiberium one.** Four separate things in it are above the Voodoo 2's line, and three of them have no fallback that keeps the feature recognisable. The **vertex texture fetch** is the hard one: the blade reads the ground colour and the crush field in the VERTEX stage so a whole card fades, presses and takes its colour as one thing, and a fixed-function pipeline cannot sample a texture per vertex at all. The **half-float render target** is the second: `GL_RGBA16F` needs a float-format framebuffer, and the field's exactness argument rests on one fp16 unit in the last place being the decay step, which an 8-bit target cannot express. The **buffer object** is the third, and it is the only one with a clean answer: without `GL_ARB_vertex_buffer_object` the geometry can be submitted as client arrays, which the measured numbers say costs 5 to 39 ms a frame against 0.07 to 0.30 from a buffer, so on a Voodoo 2 that answer is "do not draw grass" rather than "draw it slowly". The fourth is **fill**: a blade is a blended card two to eight screen pixels tall with no alpha test, and there are tens of thousands of them; the Voodoo 2 is fill limited before it is triangle limited and this is the worst kind of fill there is. **So Win98 draws the cartridge's own ground with no grass, which is the picture this project has always shipped there and is not a fidelity loss.** The whole feature is behind one dial, `grass`, and every entry point is gated on `g_fxActive`, so nothing here reaches the Voodoo path even if `tier1/*.c` later grows a reader. |

**What a Tier 1 grass would have to be, if it is ever wanted.** Not this feature with parts
removed. The blade would have to become a texture: the mask that says where grass grows is
already read per terrain texel at load, so a second ground layer drawn as a tiling alpha sheet
modulated by that mask is the shape a Voodoo 2 could carry, at one extra pass over the ground
rather than one card per blade. It would move in the wind only as a scrolling texture matrix,
it could not be pressed flat by a vehicle without a second sheet to modulate it, and it would
have no silhouette at all at a grazing camera. That is a different feature wearing the same
name, which is why it is written here as a rewrite and not as a fallback.
## The Remastered infantry sprites, and the fold at the ground line

| Piece | Tier 2 | Tier 1 answer |
|---|---|---|
| **The player's own Remastered Collection infantry art under Enhanced** (`game/remaster_inf.h`, the `infset` row of Visuals/Advanced) | strips built at run time out of the install's TGA frames, one per (type, action), on RGBA power-of-two sheets sized to the frame count (a 128-frame attack strip lands on a 2048-a-side sheet) at about five times the DOS texel density; drawn by the same `dosinf_draw_sprite` as the 1995 sprites | **NONE. TIER 2 ONLY, and it was never listed before.** The art is not in the tree and Win98 has no reader for it; the sheets are far past the Voodoo 2's 256x256 limit and would have to be cut down to fit 4 MB. Win98 draws the 1995 sprites, which is the picture this project has always shipped, and nothing is lost that was ever there. |
| **The fold: a cell's rows below the man's ground line lie flat on the ground in front of him** (`dosinf_draw_sprite`, the `dn > 0` branch) | two fixed-function `GL_QUADS` in place of one, sharing the crease's two vertices, same texture, alpha test and modulate colour; the flat quad's far corners take their own `terrain_y` and the same +0.012 z-fight lift the vehicle shadows use | **NOTHING ABOVE GL 1.1 IN IT, and Tier 1 never reaches it anyway:** every strip the 1995 pack bakes has `drop` 0, so the branch is only entered for the Remastered set above. If Tier 1 ever carries a strip with rows below the line, the fold is two textured quads and a division, which Glide draws as it is. |

## Rain

| Feature | Tier 2 | Tier 1 answer |
|---|---|---|
| **Rain under Enhanced** (`game/rain_mod.h`, `game/rain_glsl.h`; hooks in the light pass, the sea's wave field and surface, and one call in the frame after the effects) | three things on one switch: a lattice of 6000 thin quads placed by a GLSL 1.20 vertex shader from the engine clock alone (one client-array draw, no per-frame upload); a wet block inside the existing light program (darkening, a Fresnel sheen, a sun glint, drop-impact rings bending the reconstructed normal, puddles from the cloud deck's noise, distance haze, an overcast tint), which needs the offscreen scene, its depth texture and the ground-normal target; and drops pressed into the sea's RGBA16F wave field each step plus the same rings on its surface. Cost measured at 0.11 ms a frame over the chain at 1280x800, taking the minimum frame of 120 in alternating passes, because the mean drifts upward on a machine that is doing anything else | **NONE. TIER 2 ONLY, by the same row the whole chain has: on Win98 the chain does not run and the master switch is never on.** Stated piece by piece so the port is a list and not a surprise. The wet look cannot exist on Tier 1 at all: it is a screen-space term over a depth texture and a normal target, and the Voodoo 2 has neither a render target nor a shader. The drops on the water ride the wave field, which is itself a half-float render target with no Tier 1 form. The streaks are the one part a Glide build could draw, as fixed-function alpha-blended quads placed on the CPU from the same lattice and clock, and the number is 2545 of them at the shipped dials, not a few hundred: the count is 6000 times the square of the amount times the streak dial, which at 0.768 and 0.848 is 0.424 of the lattice. No shader is essential to them; the vertex shader is only what makes them free. A Glide build should be budgeted from that figure at 640x480, or from a lower streak dial chosen for this tier. None of it is written: with no wet ground under them they would be rain that leaves nothing wet, and the decision is to ship no rain on Win98 rather than half of it. There is no Visuals row at all: the rain is reached only through the F5 panel, which is compiled out of every release build, so on both tiers a player cannot turn it on and Tier 1 has nothing to answer for yet. |
| **The splash crowns** (`rain_splash_draw`) | a few hundred camera-facing quads a frame, placed on the CPU from the terrain heightfield and its corner normals, culled in a GLSL 1.20 vertex shader that samples the cloud noise and the sea's shore field with `texture2DLod`. Needs VERTEX TEXTURE FETCH, asked for once with `GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS`; a GL reporting none draws no crowns and says so | **NONE, and it would be the easiest part of the rain to give one.** The placing is already CPU work and the geometry is a few hundred blended quads, which a Voodoo 2 draws without complaint. What has no Tier 1 form is the CULL: the puddle rule reads a texture from a vertex shader, and the field that says where the sea is is a texture too. Both are readable on the CPU at the cost of keeping the baked cloud mask's level 0 in memory, 256 KB, and sampling the shore field there instead. It is not written, and it would not be worth writing before the wet ground the crowns stand in has a Tier 1 answer, which it does not. |
| **The rain's own sound** (`game/rain_audio.h`) | four seconds of mono 22050 synthesised at boot from a fixed generator and looped through `mixer_play_loop`. No shader, no GPU, no asset: plain C, integers and a noise generator | **IT ALREADY WORKS ON TIER 1 AND IS ABSENT ONLY BECAUSE THE RAIN IS.** This is the one part of the rain with no Tier 1 obstacle at all. `mixer_play_loop` is plain C whose wrap lives in `mixer_render`, so a Win98 DirectSound backend inherits it unchanged, and the synthesis is a few hundred lines of arithmetic over a 176 KB buffer. If Win98 ever gets rain of any kind, it can have the sound on the same day. |

## The texture-book second draw

| Feature | Tier 2 | Tier 1 answer |
|---|---|---|
| **A structure's texture-book variant drawn over its base mesh** (`texbook_overdraws`, the book loop in `draw_object_mesh`) | the building is drawn, then the book's variant mesh is drawn again at the same place. For PROC's first book, HPAD and FIX the variant's triangles head the base list and the second draw is pulled toward the camera with `glPolygonOffset(-1,-1)`; for FACT, SILO and PROC's second book the variant is bit-identical to the copy the base mesh already carries (posed with the book node's own transform, this round) and the second draw simply wins the `GL_LEQUAL` tie with no bias | **Half native, half a registered gap that predates this round and was never written down.** The bias-free half is Glide's home ground: `GR_CMP_LEQUAL` on identical vertices is exactly what the Voodoo 2 does with the water, and the silo's fill dome and the refinery's storage strip take that path on purpose. The biased half has no Glide form: `glPolygonOffset` does not exist there (see the water row above). The Tier 1 answer for those three books is the same bias-free path, which needs nothing more than drawing the variant after the base with depth `LEQUAL`; on a 16-bit Z buffer at 640x480 that tie may sparkle, and if it does the answer is a depth-range tweak, not an offset. None of this is wired: Tier 1 is a separate program that draws no texture books at all yet. |

## A mission start that is refused says why

| Feature | Tier 2 | Tier 1 answer |
|---|---|---|
| **The engine survives a missing or damaged theater archive** (`DisplayClass::Init_Theater` in both brains; the header guard in `common/mixfile.h`) | a `content/<THEATER>.MIX` that is absent, zero length or junk no longer faults inside `CNC_Start_Custom_Instance`: the palette is zeroed, one `CNC3D\|theater\|` line goes to the log, and the mission plays | **ALREADY COVERED, and by the same object.** `tier1/w98_brain.c` loads the same DLL through `LoadLibraryA` and calls the same `CNC_Start_Custom_Instance` (`wb_start`), so the guard is in the engine it runs and nothing in the Tier 1 program has to change. |
| **The "Unable to start mission" box** (`game_notice_open` in the renderer, `DOPT_PAGE_NOTICE` in `dosopt.c`, drawn by the shell after `game_boot` refuses) | the pause dialog's rasteriser lends its objective box: a caption, the refusal wrapped at 255 px and a lone OK, over black, until dismissed. One GL 1.1 textured quad, nothing above the Voodoo 2's line in the drawing itself | **TIER 2 ONLY, by decision rather than by capability.** The Win98 program has no dialog module and no shell menu to put a box in front of: a refused start there already comes back through `wb_open`/`wb_start`'s `err` string (`LoadLibraryA(...) failed, GetLastError()=...`, `missing CNC_* exports ...`), printed by the caller. That line is the Tier 1 answer, and it carries the same facts the box does. The box is not mirrored because there is nothing there to mirror it onto yet; if the Tier 1 program ever grows a menu, `dosopt.c` is plain C89 with no GL in it and the page comes with it. |
| **`BOOTFAIL\|` in the log** (`boot_refuse` in the renderer, printed by the shell as `BOOTFAIL\|<scen>\|<reason>`) | every refusal on the boot path writes one sentence naming the file or the step | **Equivalent exists.** The Win98 program's `err` string is the same sentence in a different prefix; it is not spelled `BOOTFAIL\|` and no gate reads it, which is written here so nobody greps a Win98 log for a line it will not carry. |
