# Mineral normal and specular integration, 21 October 2026

This is a technical material pass for the existing relief terrain, placed ROCK1–7,
and scattered stones. It adds no mesh, UV, gameplay footprint, colour or player
paint change. The existing source artwork and its artistic approval remain the
authority. A material-data check is not artistic approval or Dock delivery.

`tools/bakery/stone_materials.py` reads the **final** saved art and runtime files
into a separate staging directory. Run it after daylight, Desert albedo and rock
rebakes. It dynamically reads the complete daylight manifest and matches the
physical surface key order, including FORD/FALLS/BRIDGE/mineral additions. It
applies the exact final daylight albedo operations before deriving mineral detail,
and records raw source and delivered RGBA hashes separately. SAND shares Desert's
physical mineral layout and maps; its later global tint does not change this layout.

The scalar mineral signal removes detail broader than six texels, then derives
bounded tangent micro-normals from the remaining small-scale signal. These are
material normals inferred from existing mineral artwork, not a new high-poly bake
and not geometry decoded from dark painted areas. Saved explicit rock masks own
coverage; old bush crowns, water and ordinary sand never become stone by colour
classification. Macro terrain height remains entirely in the existing physical
mesh. Maximum micro-normal slope is .55 (about29 degrees). Dry mineral roughness
is spatially authored in .58–.90, with nonmetallic dielectric reflectance F0 .04.
No sun direction, highlight, cast shade or scenario elevation is baked into a map.

Each saved pair includes linear-data `normal_roughness.png` (RGB tangent normal,
A perceptual roughness), `specular.png` (R F0, G roughness, B metallic0, A explicit
coverage), an explicit all-black player-colour mask and source/UV/hash metadata.
Normal and specular images carry **exactly the same quantized roughness**. The
all-black mask preserves paired albedo under both red and blue player colours.

Runtime data contracts:

- `terrainmaterial_<THEATER>.pack`: `C3DMATL\0`, version1, cell144, count, the exact
  24-byte C3DSURF keys, compressed-byte count and zlib RGBA. Channels are tangent
  normal XY, perceptual roughness and explicit mineral coverage. The loader refuses
  mismatched physical keys and gives each tile a two-texel clamped gutter. It
  reconstructs Z and combines detail with the physical world normal through the UV
  derivative tangent frame. Existing GGX uses the current sun, camera, shadow and
  cloud visibility. G-buffer roughness uses the existing reserved neutral-stone
  alpha band (about7 quantized values); source/runtime atlases retain full8-bit data.
- Scattered `rock3d*.pack`: only the existing bark normal/roughness stream changes.
  Geometry, physics, UVs, code mapping, alpha and every other texture stream remain
  byte-identical. Every mip normal is renormalized. Existing forward stone lighting
  and the deferred neutral Desert path both consume the map.
- `stonematerials.pack`: `C3DSTNM\0`, version1, count; 20-byte records width, height,
  albedo-RGBA CRC32, payload offset and zipped-byte count. The payload is linear
  RGBA normal/roughness. Exact dimensions and decoded albedo CRC select a placed
  texture; wrong or missing assets fall back to geometric normals.
- `ebld/ROCK*.spec`: existing EVSPv2 sidecars, F0 in reflectance strength and gloss
  converted from that same roughness for the renderer's normalized specular lobe.
  The normal shader uses the original UVs, including mirrored islands, with a
  degenerate-UV fallback. Terrain ROCK objects now draw their existing specular
  target. Neutral stones with a sidecar skip the duplicate generic stone highlight.

The physical normal/depth/caster mesh is unchanged. The mapped rock normal remains
active when the unrelated smooth-ground preference is off. Existing specular and
universal sun/sky controls remain the tuning controls; no UI rows were introduced.
`OPENCNC3D_STONE_MAPS=0` is a diagnostic fallback for the new terrain atlas and placed
normal pack. Existing scatter streams and EVSP sidecars are separate data paths.
Foliage exits explicitly reset the shared tree shader's `uStone` to0; each batch
sets it by actual model kind, so a tree does not inherit a stone's normal treatment.

Example staging and verification (use a separate output directory):

```sh
python3 tools/bakery/stone_materials.py --source "$PWD" --runtime game --out /private/tmp/stone-material-stage
python3 tools/gate-stone-materials.py --runtime game --stage /private/tmp/stone-material-stage --output /private/tmp/stone-material-gate.json
```

Deliver seven `terrainmaterial_*.pack` files and `stonematerials.pack` at the shared
TD and RA runtime roots, the four rewritten `rock3d*.pack` files, and all14
`ebld/ROCK*.spec` sidecars beside their corresponding files. Preserve the saved
map images, explicit masks and manifest as authoring/provenance artifacts. A later
rock/albedo rebake must rerun this baker; it must not restore the flat normal stream.
The parent agent owns final current-main build, automatic staging hooks, both Dock
executables and installed validation.

The isolated arm64 renderer compiled and linked successfully. Hidden OpenGL2.1 on
Apple M5 Max loaded the Desert709-tile3996² atlas and14 exact UV-paired placed maps
without shader errors. SCB01EA hidden scripts at sun azimuth65/245 and sun elevation30
completed with0 failures. In the fixed visible cliff crop, opposed suns changed
40,903 pixels (mean RGB difference50.1); mapped versus diagnostic fallback changed
23,936 pixels (mean5.72). At zero sun and sky the same crop's maximum28/255 comes
from existing atmosphere/grade; the material BRDF adds exactly0 directional specular
at zero sun energy. These are temporary runtime proofs, not a claim that this child
agent deployed either Dock game. The saved data gate independently checks exact
keys, linear channels, unit normals, bounded slopes, source coverage, all preserved
scatter streams, albedo CRC pairings, explicit black masks and moving-sun response.

## Final installed verification

The parent delivered current-main TD and separate RA Dock executables and material
packs to both installations. [The final record](verification/2026-10-21-river-coverage-and-materials/README.md)
contains real captures, exact hashes and all installed checks:7banks/4,292cells,
180scatter looks,14placed files,829black masks. Opposed sun directions change
449,360mapped mineral pixels; zero sun and sky leave those pixels exactly black
with atmosphere disabled. Mesh, UV and albedo checks remain separate from approval.
