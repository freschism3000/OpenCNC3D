# data/assets — extracted and reference art

**Empty on purpose.** Everything that lived here was extracted from the cartridge or
downloaded from elsewhere, so none of it is this project's to publish. All of it can be
regenerated or re-fetched.

## What goes here

| Folder | What it is | How to get it back |
|---|---|---|
| `cameos_n64/` | The build cameos as the cartridge stores them, one PNG per icon | Extract from `data/rom/cnc_eu.z64` with `tools/romdump/` |
| `cameos_n64.zip` | The same set, zipped | Same |
| `sidebar_plates_n64/` | The cartridge's own sidebar plate art | Same |
| `cameos_wiki/` | Cameo reference sheets pulled from the C&C Fandom wiki, four sets | Re-download from cnc.fandom.com via its MediaWiki API |

`cameos_wiki/` is **reference only**. It was gathered to compare against, and includes art
from the Remastered Collection, which belongs to Electronic Arts. Nothing shipped in a
build is drawn from it.

Cameos actually used by the game come either from the cartridge (`cameos_n64/`) or from
`data/dosdata/CONQUER.MIX`.

## Not to be confused with

`art/cameos-tdr/` — 54 cameos at 128x96, **hand-drawn for this project**. That is authored source, it is
in the repository, and it stays there.
