# sidebar_redesign

The 640x480 sidebar HUD pipeline. It carries the coordinate table, the outstanding art list and the integration plan.

Live scripts only; superseded iterations are in `attic/`.

| file | does |
|---|---|
| `slice_ref.py` | crop, scale and MEASURE a reference into `chunks/` + `layout.json` |
| `dosfont.py` | read 6POINT / 8POINT / GRAD6FNT out of `dossidebar.pack` |
| `tabs.py` | OPTIONS / CREDITS plates, lettered from GRAD6FNT |
| `frames.py` | build multi-frame sprite strips -> `chunks/frames.json` |
| `hud.py` | the renderer: `render(cameos, power, radar_img)` |
| `measure_reference.py` | diagnostic: detect slots, write an overlay to eyeball |
| `import_art.py` | split a multi-piece art sheet into islands |

`hud.py` reads `layout.json`, so code and art cannot drift apart. After new art lands in
`incoming/`, re-run `slice_ref.py`, `tabs.py`, `frames.py` in that order.

`hud-testbed.html` is a self-contained browser test bed for the states and hit boxes.
