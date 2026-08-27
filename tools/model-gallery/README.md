# The model gallery

Every 3D asset on the Command & Conquer cartridge, in a browser, with a thumbnail, a
real name, a category, and an export button. 202 models, 11,080 triangles.

It replaces `tools/bakery/sharecopy/assets/model-viewer.html`, which was a rescued
build artifact from 13 August 2026 with no generator anywhere in the repository, and
which was wrong in three ways that could not be fixed in place:

* every mesh in it decoded through the **Nod/neutral TLUT** at ROM `0x99130`, because
  its payload was baked the day before the house-colour discovery. GDI kit drew grey.
* its `SBAG` / `CYCL` / `BRIK` / `BARB` / `WOOD` were the **bullet** models, the exact
  trap `tools/romdump/wall_models_notes.md` was written to close. The 20 real wall
  meshes were not in it at all.
* it carried no pivots (its payload was pre-posed), no animation, no parts, and no
  build sections, so nothing downstream of it could be trusted for art work.

## Where the data comes from

`export.py` reads the same **baked mission pack the game itself draws**
(`tools/bakery/game/SCB01EA.pack`, PKE v14). All 100 packs carry an identical asset
set, so one pack is the whole global corpus: 293 textures (74 with a GDI variant) and
220 meshes. Because it is the game's pack:

* the textures are the ones the renderer binds, both house palettes,
* the parts carry the cartridge's own **pivots and roles** (static / turret / rotor),
* the walls are the real walls, all 54 pieces including the shadow and damaged sets,
* every mesh carries its **build sections** and its **PKB animation clip** where it
  has one (22 of them do).

The 16 meshes the **briefing overlay names for itself** are added from the ROM through
`tools/romdump/fbx_export.py`: the GDI and Nod medallions, the GDI title logo, the EVA
wordmark, and the high-detail hero models a briefing screen shows. They never enter a
pack. Their Vtx bytes are packed normals rather than baked colour, so they are shaded
with the console's own resident directional light (ambient 16, diffuse 250,
L = -64/+64/+64, from `tools/romdump/terrain_light_notes.md`). That light is this
tool's choice and the UI says so on those models.

## Names and categories

`catalog.py`, and nothing in it is invented:

| Source | Gives |
|---|---|
| `brain/vanilla/tiberiandawn/*data.cpp` | which family a code belongs to, and its `TXT_` id |
| `brain/vanilla/tiberiandawn/conquer.h` | the English string for that id, in its own comment |
| `tools/bakery/support/unit_models.json` | the cartridge-only kinds: cursor, wall, bullet, door, marker |
| `tools/romdump/wall_models_notes.md` | which wall codes are the shadow set and which the damaged set |

The four GPL data files do not share a declaration shape (a unit puts `TXT_` before
its code, a terrain object puts its code first, a bullet has no `TXT_` at all), so the
parser takes the first of each within a declaration rather than a fixed argument
order. A wall's `S` suffix is re-checked against the mesh: it is only labelled a
shadow piece if every one of its triangles actually draws in a blended pass.

Anything with no name in any source keeps its cartridge code, and `export.py` prints
the list rather than letting a guess through. Today that list is empty.

## What counts as one asset

The pack's type table maps **547 codes onto 220 meshes**. Most of the surplus is
variant meshes: a building's turn states (`FIXT0..FIXT49` are 50 codes over 5 meshes)
and a cursor's texture flipbook (`CUR0AF0..F3`). Those 34 hang off the parent they
belong to instead of standing beside it, which is why the gallery lists 186 pack
assets rather than 220. Where several codes land on one mesh, the extra codes are
shown with the confidence `unit_models.json` gives them, so a disputed mapping reads
as disputed: `ROAD` shares the 120MM missile body and its own note says the mesh "is
not obviously a road".

## How the viewer draws

Three rules, all from `game/cnc_eyes.cpp` rather than guessed, and all three are what
make a mesh come out right:

1. **Unlit, modulated by the vertex colour.** `GL_LIGHTING` appears nowhere in the
   renderer. The baked per-vertex colour IS the shading (`cnc_eyes.cpp:5545`).
2. **Wrap is per triangle, not per texture.** `wrap = cmS | cmT<<2`, bit 1 CLAMP
   (wins), bit 0 MIRROR, else REPEAT (`cnc_eyes.cpp:1779`). 27.5% of the game's
   triangles have UVs outside 0..1, and a tree crown clamped as repeat renders as a
   hard-edged solid polygon.
3. **Four triangle modes.** opaque, cutout (alpha test > 0.5), shadow and xlu (both
   blended, drawn after the solids, no depth write).

UVs are pre-scaled by `uw/w` and `uh/h` in the exporter, matching `cnc_eyes.cpp:5510`,
and are not flipped: the pack's V is top-down and so is the PNG. (`fbxout.py` carries
a `1-v` only because FBX is bottom-left.)

The models are dark, which is right: a vehicle's baked shading averages about RGB 60.
The gallery lifts the *backdrop* rather than the model, so what is on screen is still
the game's own colour.

## Export

Every asset is pre-baked into three formats at build time, so the page stays a static
site and needs no server that can run Python:

| Format | Written by | Carries |
|---|---|---|
| **FBX 7.4 binary** (default) | `tools/art/fbxout.py`, and `tools/romdump/fbx_export.py` for the ROM meshes | node hierarchy, the cartridge's own pivots, PKB animation as real FBX curves, per-face materials |
| **OBJ + MTL** | here | geometry, UVs, materials. No pivots: OBJ has no node transforms, and the file says so in its own header |
| **glTF 2.0** | here | geometry, UVs, baked vertex colours as `COLOR_0`, `KHR_materials_unlit` |

One FBX serves both houses: the palettes differ only in the texture bytes, and the
file references its PNGs by the same relative names either way, so the palette is
chosen by which texture folder the download carries.

`EXPORT` takes the selected model; `EXPORT ALL` takes all 202, or only the ones the
category chip and the search box currently show. Both offer format, textures on or
off, and the GDI palette where a model has one. Anything larger than a single file is
zipped in the browser, store method, so the archive is byte-checkable and no deflate
implementation is involved.

## Running it

```
python3 tools/model-gallery/export.py      # rebuild data/ and the pre-baked exports
python3 tools/model-gallery/verify.py      # check every export against the asset table
python3 -m http.server 8110 --directory tools/model-gallery/public
```

`verify.py` is not a spot check: it walks all 202 assets and asserts, per format, that
the file exists, carries the right magic, and declares the same triangle count the
viewer draws. A silent disagreement between the three writers is exactly the failure
it catches.

The page must be **served**, not opened from the filesystem: it fetches `geo.bin` and
`assets.json`, which `file://` refuses.

## Known gaps

* **The viewer does not animate.** 22 meshes carry a clip and the gallery draws their
  rest pose; the detail panel says so on those models rather than letting a still
  stand in for a clip. The clip does go out with the FBX.
* **The briefing meshes' light is ours**, not the cartridge's own briefing light. It
  is the console's resident directional light, applied because those lists run with
  `G_LIGHTING` on and carry no baked colour to modulate. Which light the briefing
  screen actually sets has not been read out of the ROM.
* **`packinspect.py` is one format version behind on the pack tail** (1,732 unparsed
  bytes on a v14 pack). It does not touch the mesh or texture tables, so nothing here
  is affected, but `packinspect.py summary` still fails its trailing-bytes assert.
* **`TC01..TC05`, the five tree clusters, have no mesh** and are not listed. The
  cartridge composes them from the individual `T` trees.
