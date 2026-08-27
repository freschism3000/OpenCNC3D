# The console's terrain lighting — SETTLED from the cartridge

Companion to `heightmap_notes.md`. That note found *where the heights live*; this one
finds *why a cliff reads as a cliff on the console and as flat wallpaper in our build*.

ROM: `data/rom/cnc_eu.z64`, big-endian .z64, md5 `42da4c7d040f9e7cd046a42ec3e68027`.
Everything below is static disassembly (capstone 5.0.7) plus data reads. Nothing was
run on hardware or in an emulator; see **What is NOT settled** at the bottom.

Address resolution: resident `RAM = ROM - 0x1000 + 0x80000400`; CodeOverlay
`RAM = ROM + 0x8005E210` (`segments.py`).

---

## 1. The answer in one paragraph

Every terrain vertex on the console carries an 8-bit **shade** the game computes in
software from the heightfield itself, at cell-build time. The RSP's own lighting is
explicitly **off** for the terrain pass (`G_GEOMETRYMODE` clears `G_LIGHTING`), so this
software shade *is* the lighting. It lands in the F3D vertex's **alpha**, and the
terrain's combiner multiplies the texture by `SHADE_ALPHA`. The `CM<scen>.IMG` map is a
**second, much smaller** term: it becomes the vertex **RGB** and is *subtracted* from the
texel before the shade multiply. On SCG01EA it is zero for 3977 of 4225 corners and
never exceeds 16/255 in any channel, so to a very good approximation the console draws

```
terrain_pixel = texel * (shade / 255)
```

which is exactly `GL_MODULATE` with `glColor3f(s, s, s)`. Flat ground gets `shade = 160`
(0.627), not 255 — so the console's terrain is globally **darker** than ours, and the
relief comes from slopes running 125…247 around that 160.

---

## 2. The constants (deliverable 1)

Resident `.data`. Read out of the ROM, byte for byte:

| RAM | ROM | bytes | float | role |
|---|---|---|---|---|
| `0x800979A0` | `0x00985A0` | `41 80 00 00` | **16.0** | `TERRAIN_AMBIENT` |
| `0x800979A4` | `0x00985A4` | `43 7A 00 00` | **250.0** | `TERRAIN_DIFFUSE` |
| `0x800979A8` | `0x00985A8` | `C2 80 00 00` | **-64.0** | light `L.x` |
| `0x800979AC` | `0x00985AC` | `42 80 00 00` | **+64.0** | light `L.y` |
| `0x800979B0` | `0x00985B0` | `42 80 00 00` | **+64.0** | light `L.z` |
| `0x800979B4` | `0x00985B4` | `43 48 00 00` | **200.0** | CM colour-map gain |

Neighbours, for context (same block): `0x80097998` 100.0, `0x8009799C` 32.0,
`0x800979B8..C8` all zero.

**These are compile-time constants and nothing in the ROM writes them.** A whole-ROM
scan for `lui rX,0x8009` + a load/store at offset `0x79A0…0x79B4` returns exactly five
reads and zero writes, all five inside the gterrain vertex builder:

```
ROM 0x0196B40  lwc1  0x800979A8   L.x
ROM 0x0196B44  lwc1  0x800979AC   L.y
ROM 0x0196B48  lwc1  0x800979B0   L.z
ROM 0x0196B88  lwc1  0x800979A4   DIFFUSE
ROM 0x0196BA0  lwc1  0x800979A0   AMBIENT
```
(plus three reads of `0x800979B4` in the colour pass at `0x0196614 / 0x0196690 /
0x01966F8`). A second scan for any `lui 0x8009/0x800A` + `addiu/ori` that *materialises*
an address in `0x80097990…0x800979C4` returns nothing, so there is no pointer-based
writer either. `$gp` is effectively unused by this compiler (6 gp-based loads/stores in
the whole 790 KB resident segment, 5 in CodeOverlay), so no gp-relative writer either.

Two more constants live in the gterrain rodata, not in resident `.data`:

| RAM | ROM | bytes | float | role |
|---|---|---|---|---|
| `0x801C881C` | `0x016A60C` | `42 FE 00 00` | **127.0** | normal is rescaled to this length |
| `0x801C8804…18` | `0x016A5F4…608` | `C3800000` / `43800000` | ±256.0 | the cell pitch used to build the two edge vectors of each quadrant |
| `0x801C87EC/F4/FC` | `0x016A5DC/E4/EC` | `38 00 00 00` | 1/32768 | CM channel shift |
| `0x801C87F0/F8/800` | `0x016A5E0/E8/F0` | `4F 00 00 00` | 2³¹ | the IDO float→unsigned idiom, not a lighting value |

---

## 3. The light direction (deliverable 2)

**The finding's guess about where to look was wrong and is corrected here.** It said the
light comes from "the stack setup (sp+0x30..0x50) in the prologue of the function at
ROM 0x196b70". `0x196b70` is not a function; it is the `jal` to the dot-product helper,
and `sp+0x30…0x50` are the two edge vectors and the cross-product scratch used by each
of the four quadrant normals. **The light is not on the stack at all**: it is loaded
straight out of the three resident globals above and copied through `sp+0x70` into
`sp+0x60` (the argument slot) two instructions before the dot.

Vertex builder `gterrain.c`, RAM `0x801F4984` / ROM `0x196774`. The shading tail:

```
  0x196b00/0x801f4d10  lui        $at, 0x801d
  0x196b04/0x801f4d14  lwc1       $f20, -0x77e4($at)   ; f20 = *0x801C881C = 127.0
  0x196b08/0x801f4d18  addiu      $a0, $sp, 0x10       ; a0 = &N (the accumulated normal)
  0x196b0c/0x801f4d1c  mfc1       $a2, $f20
  0x196b10/0x801f4d20  jal        0x8007b0f8           ; N = normalise(N) * 127
  0x196b14/0x801f4d24  move       $a1, $a0
  0x196b18/0x801f4d28  addiu      $a0, $sp, 0x10
  0x196b1c/0x801f4d2c  mfc1       $a2, $f20
  0x196b20/0x801f4d30  jal        0x8007b0f8           ; ... called twice; idempotent
  0x196b24/0x801f4d34  move       $a1, $a0
  0x196b28/0x801f4d38  addiu      $a0, $sp, 0x80
  0x196b2c/0x801f4d3c  addiu      $s0, $sp, 0x60       ; s0 = &L
  0x196b30/0x801f4d40  move       $a1, $s0
  0x196b34/0x801f4d44  lui        $v0, 0x8009
  0x196b38/0x801f4d48  lui        $v1, 0x8009
  0x196b3c/0x801f4d4c  lui        $a2, 0x8009
  0x196b40/0x801f4d50  lwc1       $f0, 0x79a8($v0)     ; L.x = *0x800979A8 = -64.0
  0x196b44/0x801f4d54  lwc1       $f2, 0x79ac($v1)     ; L.y = *0x800979AC = +64.0
  0x196b48/0x801f4d58  lwc1       $f4, 0x79b0($a2)     ; L.z = *0x800979B0 = +64.0
  0x196b4c/0x801f4d5c  swc1       $f0, 0x70($sp)
  0x196b50/0x801f4d60  swc1       $f2, 0x74($sp)
  0x196b54/0x801f4d64  swc1       $f4, 0x78($sp)
  0x196b58..0x196b6c                                   ; copy sp+0x70..78 -> sp+0x60..68
  0x196b70/0x801f4d80  jal        0x8007aed0           ; *(sp+0x80) = dot(L, N)
  0x196b74/0x801f4d84  addiu      $a2, $sp, 0x10
  0x196b78/0x801f4d88  addiu      $a0, $sp, 0x84
  0x196b7c/0x801f4d8c  jal        0x8007b088           ; *(sp+0x84) = |L|
  0x196b80/0x801f4d90  move       $a1, $s0
  0x196b84/0x801f4d94  lui        $v0, 0x8009
  0x196b88/0x801f4d98  lwc1       $f4, 0x79a4($v0)     ; DIFFUSE = 250.0
  0x196b8c/0x801f4d9c  lwc1       $f0, 0x80($sp)       ; dot
  0x196b90/0x801f4da0  mul.s      $f4, $f4, $f0        ; 250 * dot
  0x196b94/0x801f4da4  lwc1       $f2, 0x84($sp)       ; |L|
  0x196b98/0x801f4da8  mul.s      $f2, $f2, $f20       ; |L| * 127
  0x196b9c/0x801f4dac  lui        $v0, 0x8009
  0x196ba0/0x801f4db0  lwc1       $f0, 0x79a0($v0)     ; AMBIENT = 16.0
  0x196ba4/0x801f4db4  div.s      $f4, $f4, $f2
  0x196ba8/0x801f4db8  add.s      $f0, $f0, $f4        ; 16 + 250*dot/(|L|*127)
  0x196bac/0x801f4dbc  trunc.w.s  $f8, $f0
  0x196bb0/0x801f4dc0  mfc1       $v1, $f8
  0x196bb4/0x801f4dc4  bltz       $v1, 0x801f4ddc      ; < 0   -> 0
  0x196bb8/0x801f4dc8  slti       $v0, $v1, 0x100
  0x196bbc/0x801f4dcc  beqz       $v0, 0x801f4de0      ; >= 256 -> 255
  0x196bc0/0x801f4dd0  addiu      $a0, $zero, 0xff
  0x196bc4/0x801f4dd4  j          0x801f4de0
  0x196bc8/0x801f4dd8  move       $a0, $v1
  0x196bcc/0x801f4ddc  move       $a0, $zero
  0x196bd0/0x801f4de0  sb         $a0, 6($s7)          ; Vtx byte +6 = SHADE
```

The five vector helpers, disassembled and identified (the finding had two of them
swapped; `0x8007aed0` is the **dot**, `0x8007af04` is the **cross**):

| RAM | ROM | what it is |
|---|---|---|
| `0x8007ae00` | `0x07ba00` | `vec_set(dst, x, y, z)` |
| `0x8007aed0` | `0x07bad0` | `*dstf = dot(a, b)` |
| `0x8007af04` | `0x07bb04` | `dst = a × b` (standard right-handed order) |
| `0x8007af68` | `0x07bb68` | `dst = a + b` |
| `0x8007b088` | `0x07bc88` | `*dstf = |v|` |
| `0x8007b0f8` | `0x07bcf8` | `dst = v * (s / |v|)` — normalise then scale; leaves `dst` untouched when `|v| == 0` |

Because `|N|` is forced to 127, `dot(L,N) / (|L| * 127)` is exactly `cos θ`, so

> **shade = clamp( trunc( 16 + 250 · cos θ ), 0, 255 )**

`L = (-64, +64, +64)`, i.e. normalised `(-0.5774, +0.5774, +0.5774)`. Axes are the
vertex builder's own: `vtx.x = X*256` (map column, world +X), `vtx.z = Y*256` (map row,
world +Z), `vtx.y = h*4` (up). So the light points **up, west (−X) and south (+Z, the
increasing-map-row direction)**; west-facing and south-facing slopes are the lit ones.
Verified numerically with synthetic ramps (`terrain_shade.py`): rising-to-the-east 162,
rising-to-the-west 158, rising-to-the-north 162, rising-to-the-south 158, flat 160.

### The normal

Not a central difference — the builder sums up to **four quadrant face normals**, each a
cross product of two edge vectors made from the ±1 neighbours (ROM `0x19686c…0x196afc`).
Guards (`X = cellX+dx`, `Y = cellY+dy`, `H(x,y) = heightmap[y*65+x] * 4`):

```
X>0  && Y>0   : N += (0, H(X,Y-1)-H, -256) × (-256, H(X-1,Y)-H,  0  )
X>0  && Y<64  : N += (-256, H(X-1,Y)-H, 0) × ( 0,   H(X,Y+1)-H, +256)
X<64 && Y>0   : N += (+256, H(X+1,Y)-H, 0) × ( 0,   H(X,Y-1)-H, -256)
X<64 && Y<64  : N += (0, H(X,Y+1)-H, +256) × (+256, H(X+1,Y)-H,  0  )
```

Each of the four gives `+65536` in Y, so the sum always points up. Map-edge corners get
fewer terms, which is why the console's own shade is slightly different at the border —
this is faithful, not a bug to fix. **Consequence worth knowing before implementing:**
the shade is per-*corner* and Gouraud-interpolated, and the four-quadrant sum smooths a
one-cell step across two cells. It produces a soft ramp, never a hard-edged facet.

---

## 4. The `CM<scen>.IMG` colour map (deliverable 3)

Colour pass `gterrain.c`, RAM `0x801F4794` / ROM `0x196584`. Called four times per cell
(ROM `0x196F14/34/54/74`) as `(vtx, cmap, cellX, cellY, dx, dy)`, immediately after the
four vertex-builder calls.

**Which vertices it indexes.** Exactly the heightmap's own 65×65 corner grid, same order:

```
  0x1965b8  addu $a0, $v1, $a0     ; a0 = cellX + dx
  0x1965c4  addu $v1, $a3, $v1     ; v1 = cellY + dy
  0x1965c8  sll  $v0, $v1, 6
  0x1965cc  addu $v0, $v0, $v1     ; v0 = (cellY+dy) * 65
  0x1965d0  addu $v0, $v0, $a0     ; + (cellX+dx)
  0x1965d4  sll  $v0, $v0, 1       ; u16 stride
  0x1965e0  lhu  $v1, ($v0)        ; cm[(cellY+dy)*65 + (cellX+dx)]
```

**What it is.** `u16` big-endian **RGBA5551**. The code unpacks
`R=(v>>11)&31`, `G=(v>>6)&31`, `B=(v>>1)&31` (`srl 0xb` / `srl 6 & 0x1f` / `srl 1 & 0x1f`)
and **never reads the alpha bit**. So per corner it is a 5-bit-per-channel RGB, not an
intensity.

**How it is combined.** Two stages, and the honest answer to "multiplied, instead-of, or
added" is *neither of the three at the vertex*: it is **multiplied into the shade to make
the vertex RGB, while the shade alone becomes the vertex alpha** — and then the RDP
combiner *subtracts* that RGB from the texel before applying the alpha.

```
  detail-gate: flag = *(u32*)0x80097A04            ; 1 in .data
  mod  = flag ? vtx[7] : 255
  lit  = (shade * mod) / 255                       ; the 0x80808081 magic-reciprocal /255
  R    = (u32)( float(lit * R5) * 200.0 * (1/32768) )     ; 0..48, NOT 0..255
  G    = (u32)( float(lit * G5) * 200.0 * (1/32768) )
  B    = (u32)( float(lit * B5) * 200.0 * (1/32768) )
  sb  B,   0xE($t1)   ; Vtx cn[2]
  sb  lit, 0xF($t1)   ; Vtx cn[3]  <-- ALPHA = the pure lighting term
  sb  R,   0xC($t1)   ; Vtx cn[0]
  sb  G,   0xD($t1)   ; Vtx cn[1]
```

(The `c.le.s` against 2³¹ around each channel is the IDO float→unsigned conversion
idiom, not a clamp; there is no clamp, and none is needed because the maximum possible
channel value is `255 * 31 * 200 / 32768 = 48`.)

**Measured contents.** `CMG01EA.IMG` is 8466 bytes = 16-byte IMG header + 65×65 u16,
`fmt=0 depth=2`. 3977 of 4225 corners are `0x0001` (RGB 0, alpha bit 1); 248 are
non-black; channel maxima R 15/31, G 11/31, B 4/31. Rendered as a picture it is a
handful of small green/amber blobs in the lower-centre-right of the map — it is a
sparse local tint, **not** a lighting map. `CMFLAT.IMG` (the fallback for scenarios with
no CM) is 4225 × `0x0001`, i.e. a pure no-op. Survey:

| scenario | CM file | non-black corners | max vertex tint (r,g,b) of 255 |
|---|---|---|---|
| SCG01EA | CMG01EA.IMG | 248 / 4225 | 16, 11, 3 |
| SCG02EA | CMG02EA.IMG | 174 / 4225 | 18, 11, 3 |
| SCG10EA | CMG10EA.IMG | 1926 / 4225 | 19, 13, 3 |
| SCB01EA | CMB01EA.IMG | 898 / 4225 | 33, 24, 20 |

### The per-corner byte `vtx[7]` — decoded, and it is the shroud

`vtx[7]` is written by the small function at RAM `0x801F4E40` / ROM `0x196C30`, between
the four builder calls and the four colour calls:

```
  v0  = (cellWord2 >> 3) & 0x3C          ; = ((cellWord2 >> 5) & 0xF) * 4
  vtx[i].byte7 = *(u8*)(0x80210090 + v0 + i)      for i = 0..3
```
and when the detail flag `0x80097A04` is 0 it writes `0xFF` to all four instead.
The 64-byte table at RAM `0x80210090` / ROM `0x01B1E80` (CodeOverlay initialised data)
is a plain 4-bit-mask expansion:

```
 idx 0: 0 0 0 0     idx 5: 0 255 0 255    idx 10: 255 0 255 0    idx 15: 255 255 255 255
 idx 1: 0 0 0 255   ...                          table[i][k] = (i & (8>>k)) ? 255 : 0
```

So bits 5–8 of a terrain cell's word2 are a **4-bit per-corner visibility mask** in the
same NW,SW,NE,SE order as the vertices, and the console's shroud is (at least in part)
drawn by taking the terrain vertex to black. Not this track's business, but the shroud
track should know it exists. **For our purposes: on fully-visible terrain `vtx[7] = 255`
and `lit = shade`.**

---

## 5. What the GL 1.1 pass should receive (deliverable 4)

### The console's own state for the terrain cell pass

The pass is F3DEX2 (proved by the `G_VTX` encoding: `w0 = 0x01004008` decodes under
F3DEX2 as `n = (0x4008>>12) = 4`, `v0 = (0x008>>1) - 4 = 0`, which is right; under F3D it
decodes to `n = 1`, which is not). Per-cell DL emitter RAM `0x801F51F4` / ROM `0x196FE4`:

```
  G_VTX   w0 = 0x01004008  w1 = cell->vtx      ; 4 verts into slots 0..3
  G_TRI2  w0 = 0x06000204  w1 = 0x00040206     ; tris (0,1,2) and (2,1,3)
```
with the builder's own vertex order `(dx,dy) = (0,0),(0,1),(1,0),(1,1)` = NW, SW, NE, SE,
so the shared diagonal is **SW–NE**. (Unchanged from the earlier finding; re-verified.)

The render state in force for that loop is emitted once, immediately before it, by the
shared render-state emitter RAM `0x801F822C` / ROM `0x19A01C`, called at ROM `0x19C4F0`.
Its mode word is assembled at ROM `0x19C440…0x19C4EC` entirely from read-only `.data`
constants plus one runtime term:

| field | source | value |
|---|---|---|
| bit 31 (textured) | `*(u32*)0x80097A8C` | 1 |
| bits 28–30 (filter) | `*(u32*)0x80097A90` | 2 |
| bit 20 (combine class) | `gTerrain[0] != 2` | 1 |
| bits 16–19 (blend class) | `*(u32*)0x80097A98` | 0 |
| bits 12–15 | `*(u32*)0x80097A94` | 0 |
| bit 11 (z) | `*(u32*)0x80097A9C` | 0 |

None of those five globals is written anywhere in the ROM (same scan as §2), and
`gTerrain[0]` is `GTerrain_Init`'s first argument (`sw $a0, -0x44b0($v0)` at ROM
`0x198554`), which the single call site at ROM `0x49610` supplies as the **N64Map
pointer** — a KSEG0 address, so `!= 2` is always true and bit 20 is always 1. Mode word
= `0xA0100000`. Walking the emitter with that word gives:

```
  G_GEOMETRYMODE  0xD9FFFFFE 0x00000000    clear G_ZBUFFER              (ROM 0x19A128)
  G_SETOTHERMODE_L 0xE200001C 0x0F0A4000   render mode                  (ROM 0x19A178)
  G_SETOTHERMODE_H 0xE3001201 0x00002000   G_TF_BILERP                  (ROM 0x19A4D0)
  G_TEXTURE        0xD7000002 0x80008000   texture on, s=t=0.5          (ROM 0x19A548)
  G_SETCOMBINE     0xFC15982B 0x4433FFFF                                (ROM 0x19A5AC)
  G_GEOMETRYMODE  0xD9FDFFFF 0x00000000    clear G_LIGHTING             (ROM 0x19A5C4)
```

`G_SETCOMBINE 0xFC15982B / 0x4433FFFF` decodes (canonical RDP mux layout) to:

```
  cycle0/1 RGB : (TEXEL0 - SHADE) * SHADE_ALPHA + 0
  cycle0/1 A   : (TEXEL0_ALPHA - 0) * SHADE_ALPHA + 0
```

The alternative arm (bit 20 = 0, unreachable here) is `0xFC15982B / 0xFF327F3F` =
`TEXEL0 * SHADE_ALPHA + SHADE`. **Both** arms use `SHADE_ALPHA` as the lighting
multiplier and `SHADE` RGB as the small CM tint, so the conclusion below does not depend
on which arm you believe.

Note also `D9FDFFFF` clearing `G_LIGHTING`: the RSP does no lighting on the terrain. The
per-vertex shade computed in software is the whole of it.

### Therefore, in GL 1.1

```c
/* pass is already GL_MODULATE; texture is the theater atlas */
glDisable(GL_LIGHTING);                       /* the console's own G_LIGHTING clear   */
/* per vertex, Gouraud: */
const float s = corner_shade(cx, cz) * (1.0f / 255.0f);   /* 0.49 .. 0.97 on SCG01EA */
glColor3f(s, s, s);
glTexCoord2f(u, v);
glVertex3f(x, y, z);
```

Nothing else changes: one `glColor3f` per vertex inside the existing
`glBegin(GL_TRIANGLES)` block, four distinct colours per cell, shared corner values so
neighbouring cells agree at the seam. No second pass, no blend-func change, no new
texture unit, no state that Glide lacks. `GL_MODULATE` gives `out = texel * color`,
which is the console's `TEXEL0 * SHADE_ALPHA` with `SHADE` set to 0.

The `(- SHADE)` term (the CM tint) has **no** `GL_MODULATE` equivalent — reproducing it
needs either `GL_COMBINE` (GL 1.3, not available on the Voodoo2 path) or a second
subtractive pass. On SCG01EA it is exactly zero on 94% of corners and at most 16/255 in
any channel elsewhere, so **leave it out and record it as a known gap** rather
than approximating it. SCB01EA is the worst case measured (33/255).

Absolute levels, which matter because our terrain art *is* the cartridge's own theater
art (the terrain baker takes DA4/DA8 + PA4/PA8 straight from the ROM):

* flat ground → `shade = 160` → **0.627**, so the whole map darkens by about a third;
* SCG01EA range 125…247 (0.49…0.97), mean 163;
* SCG02EA 92…255, SCG10EA 41…244, SCB01EA 3…249 (the desert canyon really does go black).

Measured on the proof render at cam 41.5,45 D2400: mean frame brightness drops from
65.23 to 44.58 (×0.683) and every one of the 691,200 drawn pixels changes.

---

## 6. Tools added

* `terrain_shade.py <SCEN>` — reproduces the formula exactly and writes
  `terrain_shade_<SCEN>.json` (65×65 `shade`, the raw `cm_rgba5551`, the constants with
  their ROM addresses, and stats) plus `terrain_shade_<SCEN>.png` (shade | CM tint ×5 |
  a 160-grey texel through the terrain combiner). Read-only on the ROM.
* `terrain_shade_proof.py` — a standalone software rasteriser (the console's own
  tessellation, `docs/n64-camera-math.md`'s camera) that renders a scenario unshaded /
  shaded / shade-only side by side. Not part of the game.
* `terrain_shade_proof_SCG01EA.png` — the proof, two cameras × three panels.

Self-check that the formula is not being fitted to expectation: over SCG01EA's 3078
corners whose 3×3 neighbourhood is perfectly level, the computed shade is **160 for every
single one**, which is the closed-form value `trunc(16 + 250/√3)`.

---

## 7. Does the shade fix the complaint? — the honest answer

**Partly, and not the part named.** Looking at
`terrain_shade_proof_SCG01EA.png`:

* The tile-boundary tearing along the rock band is **pixel-for-pixel identical** in the
  unshaded and shaded panels. The shade cannot fix it: it is the console's own
  compromise (24×24 art authored flat, warped per cell by the heightfield), and the
  cartridge tears in the same places.
* What changes is that the elevation becomes **legible**. In the top row the ridge picks
  up a lit west flank and a shaded east flank; in the bottom row (the SLOPE bowl at cells
  43–48 × 52–55) the difference is much larger — unshaded, the bowl is a flat brown mass;
  shaded, it reads as a depression. Once the eye reads "slope", the per-tile warping
  stops looking like misaligned wallpaper and starts looking like rock on a hill.
* The global darkening (×0.63 on flat ground) is real and is the console's. Expect every
  stored terrain reference PNG and every gate that digests terrain pixels to move.

So: ship the shade because it is what the cartridge does and it is now *known* rather
than guessed — but do not report it as fixing the seams. The seam entry in
the open question stays.

---

## 8. What is NOT settled

0. **The absolute brightness has not been checked against console footage.** The formula
   says flat ground is drawn at 0.627, and `FLAT.IMG` scenarios (every multiplayer map)
   therefore come out uniformly 0.627 — verified: `terrain_shade.py SCB20EA` gives 160
   for all 4225 corners. That is what the ROM computes, and 16 + 250 is clearly a
   deliberate strong key light (full white only on a ~54.7° slope facing it), but there is
   no reference frame in this repo to hold it against. If a console capture exists,
   sample a flat grass cell in it and compare with the same cell in our build ×0.627.
1. **Nothing here was executed.** It is all static disassembly. The confirming
   experiment: run the ROM in an emulator with a breakpoint at ROM `0x196BD0`
   (`sb $a0, 6($s7)`) on SCG01EA, dump `$a0` for a known corner, and compare against
   `terrain_shade_SCG01EA.json`. Second-best: dump the assembled terrain DL at the
   `G_SETCOMBINE` and confirm the word is `FC15982B 4433FFFF`.
2. **The combine arm** rests on `gTerrain[0] != 2` where `gTerrain[0]` is a pointer. I
   traced it back through `GTerrain_Init` to the map-loader's `a0` (stored at
   `0x80097150`, the N64Map global). If that trace is wrong the other arm applies —
   which changes only how the CM tint is applied, not the lighting.
3. **`0x80097A04` is a detail-level gate**, not a constant: ROM `0x4A414` sets it to
   `(*(u32*)0x800901A8 < 4)`. The `.data` default of `0x800901A8` is 0, so it is 1 in
   practice and the `vtx[7]` shroud modulation is live. I did not chase where the detail
   level can be changed from.
4. **Water and hole cells.** I did not check whether the water surface pass uses the same
   shade, or whether shore corners get a special value. The bowl in the proof shot
   contains a hole cell and it renders as ordinary shaded terrain here.
5. **The one-texel ST inset** flagged by the earlier finding (`s = dx*1536 + 64`,
   `G_TEXTURE` scale 0x8000 → texels 1.0…25.0 over a 24-texel tile) is untouched by
   anything here; it is still open and still cosmetic.
6. **`0x80097998` = 100.0 and `0x8009799C` = 32.0** sit immediately before the ambient in
   the same block. They are *not* part of the terrain path: their only readers are in the
   resident segment around ROM `0x49DBC`/`0x49DE0`, packing bytes into a struct at
   `0x80096FB8` together with `0x80097990` (150.0) and `0x80097994` (-12.0). I did not
   identify what that struct is; it is adjacent, not related.
