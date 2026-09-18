# Original Tiberium v4 (third-party mod archive)

The source art for `tib3d.pack`, the solid tiberium crystals the Enhanced renderer
stands on a tiberium cell. **Nothing here is ours and nothing here is the cartridge's.**

| | |
|---|---|
| Where | https://www.moddb.com/mods/original-tiberium-mod/downloads/original-tiberium-v4 |
| Downloaded as | `TiberiumMod.rar`, 1,955,245 bytes |
| sha256 of that rar | `a63d3a64a054da61e28f62d8d41932c2f94120961c2bade8efa229f2244b3d6c` |
| What is committed | the two files the rar contains, unmodified |
| sha256 of the .big | `17235f27e37f291321e30a8862954d105c53b3a08a3b6031605ccdd6b94513a3` |

The rar itself is not committed: it holds these two files and nothing else, and one of
them is 4.2 MB of the other's payload. `TiberiumMod_0.1.SkuDef` says `mod-game 1.9`,
which is Command & Conquer 3 Tiberium Wars, so the payload is a SAGE compiled asset
stream: `data/mod.bin`, `.manifest`, `.relo` and `.imp`, plus two shader files.
`game/bake_tib3d.py` carries every format it had to decode to read them, and is the
only thing in this repo that knows this archive exists.

## Licence: NONE FOUND

All eleven entries in the archive were listed. There is no licence file, no readme, no
author statement. So this is usable as a private research spike and **must not reach a
public release, a Release zip, or the published source snapshot** without the author's
permission in writing. The project's own gap log carries that as a standing entry,
along with what the pack does not reproduce.
