#!/usr/bin/env python3
"""Write the notice for the modules included in a binary package.

The full repository notice stays unchanged. Public single-module packages retain
that module's source attribution and the original EA additional terms; ordinary
packages receive the repository notice byte for byte.
"""
import argparse
import os
from pathlib import Path
import re
import tempfile


FORBIDDEN = re.compile(r"red\s*alert|redalert|ra3d|ra_bootstrap", re.IGNORECASE)
TERMS_HEADING = "ADDITIONAL TERMS per GNU GPL Section 7"


def td_notice(root, package_root):
    original = (root / "NOTICE.md").read_text(encoding="utf-8")
    art_start = original.index("## Third-party art committed here")
    art_end = original.index("\n## ", art_start + 1)
    art = original[art_start:art_end].rstrip()
    patch = (root / "brain/patches/vanilla-opencnc3d.patch").read_text(encoding="utf-8")
    modified = []
    for match in re.finditer(r"^diff --git a/(\S+) b/(\S+)$", patch, re.MULTILINE):
        name = match.group(2)
        if name.startswith(("common/", "tiberiandawn/")) and name not in modified:
            if not (root / "brain/vanilla" / name).is_file():
                raise ValueError("source inventory names a missing file: " + name)
            modified.append(name)
    if not modified:
        raise ValueError("the Tiberian Dawn modification inventory is empty")
    pins = [line.strip() for line in (root / "brain/patches/UPSTREAM.txt").read_text().splitlines()
            if line.strip() and not line.lstrip().startswith("#")]
    if len(pins) != 1 or not re.fullmatch(r"[0-9a-f]{40}", pins[0]):
        raise ValueError("the upstream source commit pin is invalid")
    licence = (root / "brain/vanilla/License.txt").read_text(encoding="utf-8")
    if licence.count(TERMS_HEADING) != 1:
        raise ValueError("the original EA additional terms could not be resolved")
    terms = licence[licence.index(TERMS_HEADING):].rstrip()
    inventory = "\n".join("    " + name for name in modified)
    xl = ""
    if package_root and any(p.is_file() and re.fullmatch(r"tiberiandawnxl\.(dll|dylib)", p.name,
                                                       re.IGNORECASE)
                            for p in package_root.iterdir()):
        xl = """
### Large-map engine

This package also includes the maintained Tiberian Dawn large-map source fork
under `brain/xl/`. Its `common/` source is checked against the standard engine by
`tools/check-xl-common.sh`; `brain/xl/DIVERGENCE.md` records the large-map changes.
The same EA licence and additional terms apply. This is a modified version of
EA's program, not the original program.
"""
    text = f"""# Notices

OpenCNC3D recreates the Nintendo 64 presentation of Command & Conquer: Tiberian
Dawn. This notice describes the program and assets included in this package.

## Licence

The program is distributed under the GNU General Public License, version 3
(`LICENSE`). Original project code includes the renderer, audio and video
layers, menus, sidebar, reverse-engineering tools and build scripts. The renderer
and game logic run together through the flat `CNC_*` C ABI as a single GPL work.
Original game data and third-party art retain their respective rights and terms.

## Third-party code: the game logic

Copyright 2020 Electronic Arts Inc.

The Tiberian Dawn game logic was released by Electronic Arts Inc. in 2020 under
GPL version 3, with the additional GPL Section 7 terms reproduced verbatim below.
EA's full original licence is `brain/vanilla/License.txt` in the corresponding
source. Its per-file copyright headers are retained unchanged.

Upstream is the Vanilla Conquer project. The source commit is `{pins[0]}`,
as recorded in `brain/patches/UPSTREAM.txt`.

### Modifications to the GPL source

**This program contains modified versions of EA's GPL source and is not the
original program.** Changes include simulation behaviour, exported state,
portability and build integration; they are not limited to read-only accessors.
File-specific dated modification notices are retained in the source files.

The standard engine uses `brain/vanilla/common/` and `brain/vanilla/tiberiandawn/`.
The following applicable upstream files appear in the committed modification
patch (the inventory is derived from that patch when the package is made):

{inventory}

The project-authored additions `common/wwkeyboard_null.cpp` and
`tiberiandawn/opencnc3d_compat.h` are separate from those upstream modifications.
The complete modification record is `brain/patches/vanilla-opencnc3d.patch`;
`tools/check-brain-checkout.sh` checks that upstream plus that patch reproduces
the standard engine source.
{xl}
## EA additional terms

{terms}

{art}

These art attributions also cover optional assets when present. Their source
provenance is retained in the referenced project records.

## Original game data and trademarks

The GPL covers the program code; original or proprietary game data is not ours
to license under the GPL. Command & Conquer, Tiberian Dawn, Westwood Studios and
related marks belong to their respective owners. This project is not affiliated
with or endorsed by Electronic Arts Inc.
"""
    if FORBIDDEN.search(text):
        raise ValueError("a package notice includes a module outside its scope")
    if terms not in text or art not in text:
        raise ValueError("the package notice lost an attribution or original term")
    return text.encode("utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("output", type=Path)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--package-root", type=Path)
    parser.add_argument("--no-ra", choices=("0", "1"), default="0")
    args = parser.parse_args()
    source = (args.root / "NOTICE.md").read_bytes()
    output = td_notice(args.root, args.package_root) if args.no_ra == "1" else source
    if args.no_ra == "0" and output != source:
        raise ValueError("the complete package notice is not the original notice")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=args.output.parent, prefix=".notice-", delete=False) as f:
            temporary = Path(f.name)
            f.write(output)
        os.chmod(temporary, 0o644)
        os.replace(temporary, args.output)
        temporary = None
        if args.output.read_bytes() != output:
            raise ValueError("the written package notice did not verify")
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    print("package notice: " + ("Tiberian Dawn" if args.no_ra == "1" else "complete") + " verified")


if __name__ == "__main__":
    main()
