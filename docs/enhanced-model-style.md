# OpenCNC3D Enhanced model style, v1

**Measured reference and starter kit.** The current production rules and stage
order are consolidated in the
canonical 3D model recipe.
Use this page for the baseline samples, fixed engine comparison and kit tools.
Its numeric swatches and thresholds are starting diagnostics, subject to the
asset's approved look and the recipe's material-class checks.

This is the shared **runtime appearance contract** for new Blender-authored Enhanced
models. The existing model recipe's concept/reference links remain the design source;
this document defines how their geometry and finish must read in OpenCNC3D. The shipped
Mammoth Tank and current Construction Yard are the first two in-game anchors. The
Yard's source finish was approved; its in-game appearance remains subject to user
review, so this v1 is an operational starting point rather than final art approval.

## Source and review references

- Mammoth runtime: [`HTNK.c3m`](../game/models3d/HTNK.c3m). The Mammoth authoring
  `.blend` is not in this checkout; the shipped model is the measurable source.
- Yard source, maps, correction history and engine proof:
  [`art/construction_yard/runtime_v01/README.md`](../art/construction_yard/runtime_v01/README.md),
  [`ConstructionYard_Runtime01.blend`](../art/construction_yard/runtime_v01/ConstructionYard_Runtime01.blend),
  [GDI](../art/construction_yard/runtime_v01/engine_gdi.png) and
  [Nod](../art/construction_yard/runtime_v01/engine_nod.png).
- [`enhanced_v1_baseline.json`](../art/style/enhanced_v1_baseline.json) holds
  measured area-weighted values for both `.c3m` files. It samples each triangle
  at three UV points, weighted by surface area. It is a baseline, not a texture
  palette to copy indiscriminately.
- [`enhanced_v1_catalog.json`](../art/style/enhanced_v1_catalog.json) records the
  same audit for all five currently shipped C3Ms, including infantry flag profiles.
- Fixed comparison: USER92, Mammoth at (32,32), Yard at (35,33), camera (34,33),
  max zoom, 1600 × 1000, Enhanced and F5 Experimental Art on. Compare GDI and Nod.
  Keep shader, sun, environment and exposure shared. Do not tune an asset by adding
  its own light or changing global exposure.
- [Current-build GDI/Nod comparison](../art/style/enhanced_v1_engine_comparison.png)
  and [capture report](../art/style/enhanced_v1_engine_comparison.json) were made
  with `python3 tools/art/style_review.py --runtime playable --out
  art/style/enhanced_v1_engine_comparison.png`. The full screenshots remain in
  `playable/shots/style/`. The contact sheet only crops them for inspection.

## The material language

| Surface | Starting sRGB anchor | Roughness starting point | Metallic | Use |
| --- | --- | ---: | ---: | --- |
| Painted armor | `70,54,31` Mammoth tan | 0.54 | 0 | Vehicle body paint; faction tint only on designated trim |
| Structural paint | `53,54,52` Yard neutral | 0.72 | 0 | Large building panels; retain the concept's local hue |
| Exposed steel | `144,148,149` Yard roof sample | 0.50 | 1 | Unpainted edges and mechanisms, never a blanket coating |
| Concrete or deep recess | `45,44,41` Yard dark sample | 0.88 | 0 | Foundations and shadowed cavities |
| Track or mechanism | `50,47,39` Mammoth track sample | 0.52 | 0.4 | Existing mixed track atlas reference; choose actual submaterial values by physical role |

These are **starting swatches**, recorded in
[`enhanced_v1.json`](../art/style/enhanced_v1.json). Preserve each unit's intended
identity; neither all vehicles nor all buildings should turn tan or grey. The
common language is restrained value, material response, wear scale and readable
chamfers. The approved Mammoth paint area has linear luminance mean 0.044 and
roughness median 0.537. The Yard's two mixed atlases have luminance means 0.069
and 0.056 and roughness medians 0.686 and 0.580. The Yard metal roof raises its
first atlas average; a whole-atlas average is not a paint colour target.

1. **Colour and value.** Work in sRGB BaseColor with no painted-in direct sunlight
   or exposure lift. Keep broad forms readable at game zoom. Use the reference
   swatches for value comparison under the same engine light, then adapt hue to
   the asset's role and the recipe's reference images. A new atlas with
   area-weighted linear luminance above 0.15 needs review; the previous Yard
   value around 0.18 looked too bright beside the Mammoth.
2. **Roughness.** Painted armor should start around 0.50–0.65, structural paint
   around 0.60–0.85, exposed steel around 0.42–0.58, and concrete around
   0.75–0.95. Vary within a surface for construction and use, not with large
   random noise. Paint below 0.46 median or an atlas below 0.40 median fails the
   automated gross-shine gate. The Yard's approved correction included a
   roughness floor of `max(source + 0.06, 0.58 - 0.12*metallic)`; it is a record
   of that asset's fix, **not** a formula to apply twice or to every future model.
3. **Metalness.** Coated paint and concrete are 0. Bare metal is near 1. Mixed
   numbers in the old Mammoth track atlas are an existing encoded look, not a
   rule to make all new materials partially metallic. Define metal/paint areas
   intentionally and keep the broad silhouette from becoming chrome.
4. **Wear and dirt.** Put chips on exposed corners, crew contact, tracks, joints
   and maintenance areas; collect grime below ledges and inside recesses. Keep
   large painted fields mostly intact. Do not draw a uniform bright outline on
   every edge or scatter equal-strength scratches across every face. Check wear
   at the fixed game zoom: it must add scale without reading as white speckles.
5. **Chamfers.** Give silhouette and major panel edges a visible but narrow light
   catch. Start at 0.025 Blender cell for vehicles/mechanisms and 0.040 for
   buildings/concrete, two segments, weighted normals. Adjust by object scale
   until a prominent edge reads about 1–3 lit pixels in USER92 at max zoom.
   Preserve broad Yard chamfers and Mammoth hard mechanical forms. Tiny detail
   edges may stay sharp; do not inflate every bolt or round off the silhouette.
6. **Texture detail.** Use broad colour blocks and secondary texture at a size
   that survives gameplay view. Keep normal relief subordinate to silhouette;
   bake concrete and coating height into tangent normals because C3M has no
   height channel. Avoid texture-only fake geometry on major readable edges.
   Check UV density and mirrored/repeating areas before export, with no visible
   seam or stretched scratch direction at game zoom.
7. **Faction paint.** Author an explicit grayscale PlayerColorMask on the same
   BakeUV as its BaseColor. Mask only designated paint/trim, never concrete,
   glass, rubber, grime or the Yard's bare-metal roof. Verify both GDI and Nod
   in game. Hue alone must never infer the mask.

Cloth, shoulder emblems and unrecoloured equipment are checked under their C3M
material flags with separate broad bounds. A white infantry insignia is not
expected to share the Mammoth paint's luminance, and a rifle can have a tighter
specular highlight than coated armor. The audit reads C3M v1 through v3 and its
no-argument form checks every shipped model. New surface roles without an
existing flag need an explicit documented profile before they are accepted.

## Blender and runtime contract

Use [`OpenCNC3D_Enhanced_Style_v1.blend`](../art/style/OpenCNC3D_Enhanced_Style_v1.blend)
as a copyable material and bevel starter. Its neutral render
([preview](../art/style/OpenCNC3D_Enhanced_Style_v1.png)) helps authoring; it does not
simulate the engine. For C3M, export separate BaseColor, Normal, Roughness,
Metallic, AO and PlayerColorMask source maps. BASE RGB is sRGB and BASE alpha is
the linear player mask; NORM is +Y tangent normal; ORM is AO, roughness, metallic.
Set colour-space flags accordingly. Blender dimensions are cells, Z up, and
export coordinates map `(X,Y,Z)` to `(1024X,1024Z,-1024Y)`. Preserve the asset's
approved footprint, origin, pivots, movement parts and gameplay silhouette.
Runtime triangle and texture budgets are asset-specific; use the C3M exporter and
existing delivery gate for each type.

## Gate for every new asset

1. Start from a copy of the style `.blend` or copy its named material presets.
   Record intended surfaces, swatches, masks, roughness, bevel width, texture size,
   reference links and exceptions in that asset's authoring manifest.
2. Export the C3M and run `python3 tools/art/style_audit.py path/to/model.c3m`.
   The audit reports per-material luminance, roughness, metalness, paint statistics
   and player-mask coverage; it fails missing/mismatched maps, empty or degenerate
   material draws, over-bright average and grossly shiny surfaces. Its thresholds
   are deliberately broad; passing is not art approval.
3. Capture the new asset beside the Mammoth and Yard in the **same game build**
   at the USER92 camera, in GDI and Nod, plus one closest gameplay zoom view.
   Compare colour value, specular footprint, edge width, wear frequency, detail
   readability and faction-mask boundaries against the two reference screenshots.
4. Record a short review: what matches, what differs by design, what was corrected,
   C3M hash, Blender source hash, audit JSON, engine screenshots and the approver.
   Freeze approved assets as versioned references. Do not silently regrade them
   when a later asset needs correction.

The numeric gate catches common drift. The final visual decision requires the
same engine scene and a human judgement of intentional art differences.
