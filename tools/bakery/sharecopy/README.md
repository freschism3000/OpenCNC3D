# tools/bakery/sharecopy — the bakery's working copy

**Empty on purpose.** This directory is the local asset store the pack baker reads from. It holds no source: every file in it is either a copy of the cartridge or
something extracted from it, which is why the public snapshot leaves it out.

## What goes here

```
tools/bakery/sharecopy/
  rom/cnc_eu.z64            a copy of the cartridge, kept beside the baker
  assets/extracted/         decoded cartridge files by type: IMG, BIN, DA8, PA8, ENG/FRE/GER
  assets/raw/dir_*/         raw directory-table dumps, one folder per ROM directory record
  assets/terrain/           terrain tile sheets
  assets/*.json             model submeshes and payload tables
  eyes/                     the baker scripts that run against the copy
  tools/mount/              the mount-transform investigation tools
```

## Rebuilding it

Put your own cartridge at `data/rom/cnc_eu.z64` (see the README there) and run the
extraction in `tools/romdump/`. Everything under `assets/` is derived output; nothing in
here is hand-authored, and nothing is lost by deleting it.

`CNC3D_ROM` overrides where the cartridge is read from.

## One thing that is genuinely missing

`eyes/bake_pk4.pyc` — the PK4 baker's source was lost, and
only the compiled form survived. It is not tracked here and cannot be rebuilt from this
repository. `tools/bakery/pk4mod.py`, which existed only to load and patch that bytecode,
is left out of the public snapshot for the same reason.
