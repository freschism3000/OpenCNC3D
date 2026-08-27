# data/rom — the Nintendo 64 cartridge

**Empty on purpose.** This is where your own dump of the C&C Nintendo 64 cartridge goes.
Nothing in this repository ships it, and nothing here will produce it for you.

## What goes here

| File | What it is | Needed for |
|---|---|---|
| `cnc_eu.z64` | The **European** cartridge, big-endian `.z64`, 32 MB | Everything below |
| `cnc_usa.n64.zip` | The US cartridge, optional | Comparison only |

**The European cartridge is the one the project is built against**, and not by accident:
the EU cart bundles the Covert Operations missions, so it carries more content than the
US one. Every offset, segment address and table in `docs/re-findings.md` is quoted against
it. A US dump will not line up.

Byte order matters. `.z64` is big-endian, which is the format the tools read. A `.n64` or
`.v64` dump is byte-swapped and has to be converted first.

## Where the project reads it from

Everything looks here by default, and takes an override:

```
CNC3D_ROM=/path/to/cnc_eu.z64      # names the cartridge directly
CNC3D_ROOT=/path/to/checkout       # for tools run from outside the tree
```

## What is extracted from it

The 3D half of the game, and it exists nowhere else: 3D models and their mount transforms,
`.IMG` textures, height-mapped terrain, the particle effect art, the house colour tables,
the per-model scale table, and the camera parameters recovered from the ROM's own maths.

`tools/romdump/` and `tools/bakery/` do the reading. `docs/re-findings.md` explains the
formats, all of which were recovered from the cartridge's own debug strings.

## Why it is not in the repository

It is Nintendo's and Westwood's work. Dumping a cartridge you own is one thing;
redistributing the dump is another, and this project does not do the second.
