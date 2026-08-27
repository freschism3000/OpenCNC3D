# The terrain CM tint layer, decoded from the cartridge

Companion to `terrain_light_notes.md`, which settled the per-corner LIGHT. This one
settles the other half of the console's terrain colour: the per-corner **tint** the
RDP combiner subtracts from the texel before it modulates by that light.

Everything below carries the ROM offset it came from. Nothing here is fitted.

---

## 1. The combiner

Terrain ground pass, CodeOverlay (RAM = ROM + 0x8005E210).

```
ROM 0x19C3C0  G_SETOTHERMODE_H 0xE3000A01 / 0x00000000
              shift = 32 - 0x0A - 2 = 20, len 2 = G_MDSFT_CYCLETYPE, data 0
              => G_CYC_1CYCLE. Only the cycle-0 mux is live; the combiner is NOT
                 applied twice.

ROM 0x19C430..0x19C4EC  assembles the render-state mode word out of read-only
              resident .data (none of those five globals is written anywhere in
              the ROM):
                 bit31      *(u32*)0x80097A8C = 1   ROM 0x009868C  textured
                 bits28-30  *(u32*)0x80097A90 = 2   ROM 0x0098690  filter
                 bits20-23  (gTerrain[0] != 2) = 1                 COMBINE ARM
                 bits16-19  *(u32*)0x80097A98 = 0   ROM 0x0098698  blend arm
                 bits12-15  *(u32*)0x80097A94 = 0   ROM 0x0098694
                 bit11      *(u32*)0x80097A9C = 0   ROM 0x009869C  z
              => mode word 0xA0100000.

ROM 0x19C4F0  jal 0x801F822C, the shared render-state emitter. Inside it, at
              ROM 0x19A564..0x19A5B8, bits 20..23 are read as a NIBBLE and select
              one of two G_SETCOMBINE arms:
                 nibble 0 -> ROM 0x19A590   0xFC15982B / 0xFF327F3F
                 nibble 1 -> ROM 0x19A5AC   0xFC15982B / 0x4433FFFF   <-- TAKEN
                 otherwise -> no G_SETCOMBINE at all
```

`0xFC15982B / 0x4433FFFF` decodes (canonical gbi.h mux layout) to

```
cycle0 RGB   : (TEXEL0 - SHADE) * SHADE_ALPHA + 0
cycle0 ALPHA : (TEXEL0_ALPHA - TEXEL1_ALPHA) * SHADE_ALPHA + 0
```

The ALPHA lane is **dead**: the render mode emitted at ROM 0x19A178 is
`G_SETOTHERMODE_L 0xE200001C / 0x0F0A4000`, whose low half 0x4000 is FORCE_BL with
blender `p,a,m,b = CLR_IN, G_BL_0, CLR_IN, G_BL_1` = **G_RM_OPA_SURF**. The combiner
output goes straight to the framebuffer and nothing reads the alpha.

`G_GEOMETRYMODE 0xD9FDFFFF` (ROM 0x19A5C4) clears G_LIGHTING and `0xD9FFFFFE`
(ROM 0x19A128) clears G_ZBUFFER: the RSP does no lighting on the terrain and the
ground pass is not depth tested.

So, in numbers, per channel:

```
out = clamp8( ((TEXEL0 - tint) * lit + 0x80) >> 8 )
```

with **tint = the vertex RGB** and **lit = the vertex ALPHA**. The subtract is
signed and the clamp is at the CYCLE OUTPUT, so a negative difference lands on
zero. The multiplier is /256, not /255.

**Mux-layout validation** (the decoder is not trusted on its own):
`gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE)` = 0xFCFFFFFF / 0xFFFE793C decodes to
RGB = SHADE, A = SHADE_ALPHA, and the cartridge's own WATER combiner
0xFC1147FF / 0xFFFFFE38 (ROM 0x19B99C + 0x19BA08) decodes to
RGB = TEXEL0 * TEXEL1, A = SHADE_A * PRIM_A, which is exactly what wave 4 settled
independently from the water's own pixels.

**The sibling arm is reachable and does the opposite.** 0xFC15982B / 0xFF327F3F is
`(TEXEL0 - 0) * SHADE_ALPHA + SHADE`, i.e. it ADDS the tint. The SEABED pass
(ROM 0x19B488, gated on `*(u32*)0x80097AC0` = 1) masks the mode word's bits 20-23
and 16-19 to zero (ROM 0x19B4A4 / 0x19B4AC / 0x19B4F8 / 0x19B4FC) without OR-ing
anything back, and re-emits the TERRAIN cell's own vertex array (cell[+4],
ROM 0x19B820 / 0x19B844, the same field the ground pass's G_VTX uses at
ROM 0x196FE8). So the seabed ADDS the tint where the ground SUBTRACTS it. **Not
built**, recorded as a known gap.

---

## 2. The vertex colour

gterrain.c colour pass, RAM 0x801F4794 / ROM 0x196584. Signature
`(vtx, cmap, cellX, cellY, dx, dy)`; 20 call sites, all in gterrain.c, all on the
same 65x65 corner grid.

```
ROM 0x196598  lw   *(u32*)0x80097A04      detail gate, .data default 1
ROM 0x19659C  lbu  vtx[6]                 = shade (terrain_light_notes.md)
ROM 0x1965A8  lbu  vtx[7]                 = per-corner shroud byte (255 when
                                            the gate is 0 and on visible terrain)
ROM 0x1965C0  mult shade, vtx[7]
ROM 0x1965E4  ... /255                     -> lit = shade * vtx[7] / 255
ROM 0x1965D0  v0 = ((cellY+dy)*65 + (cellX+dx)) * 2   <- SAME index as the heightmap
ROM 0x1965E0  lhu  cm[idx]                 u16 BIG-ENDIAN
ROM 0x1965E8  R5 = v >> 11
ROM 0x196624  G5 = (v >> 6) & 0x1F
ROM 0x196634  B5 = (v >> 1) & 0x1F         bit 0, the RGBA5551 alpha, is NEVER read
ROM 0x196614 / 0x196690 / 0x1966F8   lwc1 *(f32*)0x800979B4 = 200.0   CM_GAIN
ROM 0x016A5DC / 0x016A5E4 / 0x016A5EC       = 0x38000000 = 1/32768    CM_SHIFT
ROM 0x016A5E0 / 0x016A5E8 / 0x016A5F0       = 0x4F000000 = 2^31, the IDO
                                              float->unsigned idiom, NOT a clamp
stores:
ROM 0x19675C  sb B   -> vtx[0xE]
ROM 0x196760  sb lit -> vtx[0xF]           the vertex ALPHA
ROM 0x196764  sb R   -> vtx[0xC]
ROM 0x196768  sb G   -> vtx[0xD]
```

so

```
tint_c = (u32)( (float)(lit * C5) * 200.0f * (1.0f/32768.0f) )     c in {R,G,B}
```

**Truncation, not rounding**, and there is no clamp because none is needed: the
ceiling is 255 * 31 * 200 / 32768 = 48.24, i.e. 48. Every intermediate is below
2^24, so the f32 maths is exact and reproduces bit for bit in f64 or in double.

Note `lit` appears TWICE in the final pixel: once folded into the stored tint here,
and once as the combiner's multiplier. That is the cartridge's own arrangement, not
a transcription slip.

---

## 3. Which file

n64engine.cpp loader, resident RAM 0x80048574 / ROM 0x49174.

```
ROM 0x49274  malloc(0x2102 = 65*65*2)            -> N64Map+0x34
ROM 0x49290  sprintf(buf, "%s.img"@ROM 0x525C, scenName@0x80096694)
ROM 0x492A0  sb 'c', buf[0]        overwrite the first TWO characters
ROM 0x492AC  sb 'm', buf[1]        -> "cmG01EA.img"
ROM 0x492A8  jal 0x800520FC        (file exists?)
ROM 0x492B0  beqz -> use "cmflat.img" @ ROM 0x5270 instead
ROM 0x492E4  copy 0x2102 bytes into N64Map+0x34
```

So the CM name is the scenario name with its first two characters replaced by "CM",
extension .IMG. SCG01EA -> CMG01EA.IMG. **54 CM maps exist in the cartridge.**
CMFLAT.IMG is 4225 copies of 0x0001, i.e. RGB 0 everywhere: an exact no-op, which
is what a scenario without a CM map gets.

---

## 4. How much of it is visible

Scaled per-corner tint at the corner's own light, whole 65x65 grid:

| scenario | CM file | non-black CM corners | non-zero tint corners | tint max (R,G,B) |
|---|---|---|---|---|
| SCG01EA | CMG01EA.IMG | 248 | 223 | 16, 11, 3 |
| SCG02EA | CMG02EA.IMG | 174 | 164 | 18, 11, 3 |
| SCB01EA | CMB01EA.IMG | 898 | 876 | 33, 24, 20 |
| SCG10EA | CMG10EA.IMG | 1926 | 1804 | 19, 13, 3 |

The raw 5-bit maxima are (15,11,4), (19,12,4), (26,19,16), (17,12,3): measure the
SCALED quantity, because 200/32768 floors most small values to nothing.

Inside the scenario's own playable rect, on a mid-grey 160 texel, worst channel:

| scenario | rect | tinted corners | max output loss | mean loss |
|---|---|---|---|---|
| SCB01EA | 37x24 @21,14 | 73.6% | 28 | 8.17 |
| SCG10EA | 55x57 @6,5 | 47.9% | 14 | 2.18 |
| SCG01EA | 26x23 @36,39 | 25.3% | 11 | 1.20 |
| SCG02EA | 31x31 @31,31 | 14.6% | 13 | 0.65 |

It is a real layer on the DESERT map and subtle on the temperate ones. The tint is
identically zero outside the rect (plus a small margin), so a whole-grid percentage
understates what is on screen.

Because R > G > B everywhere in these maps, the layer subtracts more red than blue:
tinted ground goes DARKER and slightly COOLER, not warmer. Measured on SCB01EA at
cam 28,36 zoom max: ground (92,81,60) becomes (66,62,45), a delta of (26,19,15)
which is the (33,24,20) tint ratio.

---

## 5. What the renderer does with it

`bake5.py` ships the RAW u16 map in the pack (PKC / CNC3DPKC v12, 4225 u16
little-endian, the first block of the tail) and `cnc_eyes.cpp` does the arithmetic
above against the light it already computes, in `terrain_tint_calc`. Splitting it
that way is deliberate: the tint is a function of the light, and the console
multiplies the two before either reaches a vertex.

`draw_terrain` cannot fold the tint into the vertex colour, because GL_MODULATE
gives `T * C` and matching the console needs
`C = lit/256 - (tint*lit)/(256*T)`, a function of the texel. It uses two texture
env stages instead: unit 0 `GL_SUBTRACT(TEXTURE, PRIMARY_COLOR)` and unit 1
`GL_MODULATE(PREVIOUS, PRIMARY_COLOR alpha)`. `cmcombine` (script verb) asserts
that chain against read-back pixels over 180 (texel, tint, lit) triples: 146 exact,
34 one level brighter, 0 darker, which is the exact signature of the 255-vs-256
divide and nothing else.

Extractor: the decode above was produced and re-verified by
`/tmp/wave5/terrain-cm/cm_tint.py`, which reproduces this file's counts from
independently written code.
