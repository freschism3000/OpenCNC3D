# First real iOS mission resources

`OPENCNC3D_SOURCE_ROOT` selects the source checkout when needed.
`stage.py` copies a bounded allowlist from the **current-main** installation’s `playable` directory into an iOS bundle's `GameData` folder. It never changes that
installation, its Dock launcher, saves, settings or assets. The asset bytes are
local build inputs; they are not checked into this directory or downloaded.

```sh
python3 platforms/ios/resources/stage.py --output /private/tmp/OpenCNC3D-GameData --dry-run
python3 platforms/ios/resources/stage.py --output /private/tmp/OpenCNC3D-GameData
python3 platforms/ios/resources/stage.py --output /private/tmp/OpenCNC3D-GameData --verify
python3 -m unittest discover -s platforms/ios/resources -p 'test_*.py'
```

The audited 2026-10-18 stage contains **26 files, 35,255,323 bytes (33.62 MiB)**.
Its profile refuses an unexpected increase above 64 MiB. Every staged asset gets
its byte count, SHA-256 and source role recorded in `asset-manifest.json`; the
script verifies the copy. Missing required assets fail before writing anything.
Rerunning preserves unrelated output files and refuses to replace any unowned or
locally modified resource. It follows the installation's read-only `dosdata`
link when reading source assets, and never follows output symlinks.

The manifest includes the exact launch argument list. Launch from the bundle's
`GameData` directory or rewrite **all** relative paths to absolute bundle paths.
The `--dir missions/` trailing slash is required: the current simulation glues
the directory directly to `SCG01EA.INI` and `SCG01EA.BIN`.

This is a complete **first-mission resource profile**, not a full campaign or a
claim that iOS rendering, audio, fog or input already works. It uses the real
`SCG01EA` terrain/model pack, scenario, team/reinforcement/trigger rules, the
simulation's existing `TEMPERAT.MIX`, the original infantry/effects, briefing
text, acknowledgments, EVA speech and first GDI mission score (`AOI.AUD`). Existing
HUD packs and corrected `FACT`, `NUKE`, `PYLE`, `GUN` meshes are preserved. The
renderer owns visibility/fog; its state is not a separate external asset.

This profile intentionally excludes the other campaign missions, movie catalog,
full soundtrack, Enhanced terrain, authored remaster meshes, volumetric VFX and
replacement audio. The first port must keep Enhanced off; do not claim desktop
Enhanced parity from this bundle. Do not copy the desktop graphics/player config
into it, because that would enable features whose assets are outside the profile.

The allowlist was also checked through the current-main desktop executable from
the staged directory in a hidden run: real mission boot, 60 simulation ticks,
60 live objects, original and current HUD screenshots, no fallback boxes or
invisible terrain, and active valid fog (541 hidden, 46 dark, 113 clear cells).
Recorded original audio contains 174,636 stereo frames at 44,100 Hz (3.96 seconds),
with a nonzero peak and no hard-clipped samples. This verifies resource
sufficiency through the desktop renderer; it does **not** verify iOS graphics or
device sound. `audit-2026-10-18.json` records source/build hashes and results.

The current Dock installation has no `n64infantry.pack`; this is recorded as an
optional omission in the manifest. Animated DOS infantry are included and remain
the baseline. Some optional Enhanced loaders may log missing packs even while
Enhanced is off; this is not a reason to silently pull in gigabytes of assets.

The native app should use the existing `game_parse_args`, `game_boot`,
`game_loop`, `game_shutdown` seam in `game/cnc_game.h`. It must statically link
the ported brain on iOS, keep `GameOpts` strings alive, create the SDL window and
graphics context before boot, and use a sandbox-writable directory for saves,
logs and player settings. Bundle data is read-only. The desktop `main` app menu
and campaign post-mission flow are outside this first direct-mission target.
