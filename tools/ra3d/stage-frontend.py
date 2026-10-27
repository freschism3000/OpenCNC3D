#!/usr/bin/env python3
"""Stage original Red Alert campaigns, films and the current OpenCNC3D module."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import sys

from ramix import RaMix, mix_id

HERE = Path(__file__).resolve().parent


# The missions with a 3D scene of their own (tools/ra3d/bake-3d-mission.py).
from missions_3d import ALL as SCENES_3D


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True, type=Path)
    parser.add_argument("--brain", required=True, type=Path)
    parser.add_argument("--app", required=True, type=Path,
                        help="the RA3D opencnc3d binary, kept separate from the TD binary")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--terrain-pack", required=True, type=Path,
                        help="full-size temporary terrain pack for SCG01EA")
    args = parser.parse_args()
    if args.brain.name not in ("RedAlert.dylib", "RedAlert.dll"):
        parser.error("the brain must be named RedAlert.dylib or RedAlert.dll")
    if args.app.name not in ("opencnc3d", "opencnc3d-game.exe"):
        parser.error("the app must be named opencnc3d or opencnc3d-game.exe")
    for required in (args.data / "GENERAL.MIX", args.data / "LOCAL.MIX", args.data / "SOUNDS.MIX",
                     args.data / "SPEECH.MIX", args.data / "SCORES.MIX",
                     args.data / "movies" / "REDINTRO.VQA",
                     args.data / "movies" / "PROLOG.VQA",
                     args.brain, args.app, args.terrain_pack):
        if not required.is_file():
            parser.error(f"missing {required}")
    libraries = ("SDL2.dll",) if args.app.suffix == ".exe" else (
        "libSDL2-2.0.0.dylib", "libSDL3.dylib")
    for library in libraries:
        if not (args.app.parent / library).is_file():
            parser.error(f"missing {args.app.parent / library}")

    module = args.out / "ra3d"
    if (module / "data").is_symlink():
        (module / "data").unlink()
    (module / "movies").mkdir(parents=True, exist_ok=True)
    (module / "missions").mkdir(parents=True, exist_ok=True)
    (module / "data").mkdir(parents=True, exist_ok=True)
    general = RaMix(args.data / "GENERAL.MIX")
    local = RaMix(args.data / "LOCAL.MIX")
    mission_count = 0
    for side in "GU":
        for number in range(1, 16):
            for direction in "ENWS":
                for variation in "ABCD":
                    entry = f"SC{side}{number:02d}{direction}{variation}.INI"
                    content = general.read(entry)
                    if content is not None:
                        (module / "missions" / entry).write_bytes(content)
                        mission_count += 1
    # THE 24 MULTIPLAYER MAPS, which a skirmish or a network match is started on, and the
    # list a skirmish screen reads with a picture of each (bake-ra-map-previews.py).
    for number in range(1, 25):
        entry = f"SCM{number:02d}EA.INI"
        content = general.read(entry)
        if content is None:
            raise RuntimeError(f"{entry} missing from GENERAL.MIX")
        (module / "missions" / entry).write_bytes(content)
    subprocess.run([sys.executable, str(HERE / "bake-ra-map-previews.py"), str(module),
                    "--data", str(args.data)], check=True, stdout=subprocess.DEVNULL)
    for archive_name, archive, entry in (
        ("GENERAL.MIX", general, "TUTORIAL.INI"),
        ("LOCAL.MIX", local, "RULES.INI"),
    ):
        content = archive.read(entry)
        if content is None:
            raise RuntimeError(f"{entry} missing from {archive_name}")
        (module / "missions" / entry).write_bytes(content)
    if not (module / "missions" / "SCG01EA.INI").is_file() or not \
       (module / "missions" / "SCU01EA.INI").is_file():
        raise RuntimeError("both original first missions are required")
    for archive in args.data.glob("*.MIX"):
        shutil.copy2(archive, module / "data" / archive.name)
    dog_shape = RaMix(args.data / "CONQUER.MIX").read("DOG.SHP")
    if dog_shape is None:
        raise RuntimeError("original DOG.SHP missing from CONQUER.MIX")
    (module / "data" / "DOG.SHP").write_bytes(dog_shape)
    if args.terrain_pack.resolve() != (module / "SCG01EA.pack").resolve():
        shutil.copy2(args.terrain_pack, module / "SCG01EA.pack")
    subprocess.run([sys.executable, str(HERE / "stage-audio.py"), "--data", str(args.data),
                    "--out", str(module / "audio")], check=True)
    subprocess.run([sys.executable, str(HERE / "bake-menu.py"),
                    "--data", str(args.data), "--out",
                    str(module / "dosmenu.pack")], check=True)
    # Red Alert's Enhanced menu kit (menu/bake_ra_enhanced_menu.py); it reads data/radata
    subprocess.run([sys.executable, str(HERE.parents[1] / "menu" / "bake_ra_enhanced_menu.py"),
                    str(module / "enhanced-menu.pack")], check=True)
    (module / "enhanced-menu.json").unlink(missing_ok=True)
    subprocess.run([sys.executable, str(HERE / "bake-enhanced-hud.py"),
                    str(module / "enhanced-hud.pack")], check=True)
    subprocess.run([sys.executable, str(HERE / "bake-campaign.py"),
                    "--data", str(args.data), "--out",
                    str(module / "campaign.pack")], check=True)
    subprocess.run([sys.executable, str(HERE / "bake-classic-sprites.py"),
                    "--data", str(args.data), "--out",
                    str(module / "dosclassic.pack")], check=True)
    subprocess.run([sys.executable, str(HERE / "bake-classic-terrain.py"),
                    "--data", str(args.data), "--out",
                    str(module / "terrain.pack")], check=True)
    for theater in ("SNOW", "TEMPERAT", "INTERIOR"):
        subprocess.run([sys.executable, str(HERE / "bake-classic-cursors.py"),
                        "--data", str(args.data), "--theater", theater,
                        "--out", str(module / f"cursor-{theater.lower()}.pack")], check=True)
        subprocess.run([sys.executable, str(HERE / "bake-sidebar.py"),
                        "--data", str(args.data), "--theater", theater,
                        "--out", str(module / f"sidebar-{theater.lower()}.pack")], check=True)
        subprocess.run([sys.executable, str(HERE / "bake-sidebar.py"),
                        "--data", str(args.data), "--theater", theater, "--hires",
                        "--out", str(module / f"sidebar-{theater.lower()}-win95.pack")], check=True)
    # THE 3D SCENES. Every campaign mission and multiplayer map (missions_3d.py) carries its
    # own pack, ra3d/3d/<SCEN>.pack: Red Alert's own terrain on Red Alert's bank for its
    # theater and its auto heightmap (bake-3d-mission.py). A mission without one stays in
    # Classic. Under Enhanced the Red Alert ground (game/ra3d_terrain.h) lays those heights
    # under ra3d/terrainenh_RA_<TH>.pack: until Red Alert's own templates are painted,
    # Tiberian Dawn's 144-texel paintings carried to the Red Alert icons that are the same
    # picture (bake_ra_terrain3d.py ra144). Interior has no set and draws its 1995 art.
    root = HERE.parents[1]
    (module / "3d").mkdir(exist_ok=True)
    subprocess.run([sys.executable, str(HERE / "bake-3d-mission.py"), str(module / "3d")]
                   + list(SCENES_3D), check=True)
    for bank, td in (("RASNOW", "SNOW"), ("RATEMP", "TEMPERAT")):
        th = "SNOW" if bank == "RASNOW" else "TEMPERAT"
        # RED ALERT'S OWN SET, WHEN ONE IS COMMITTED (art/remaster/textures/<bank>, painted
        # on the bank's templates by tools/art/remaster/textures/tex.py): its cells, variants
        # and join strips, packed by the standard tool and renamed to the Red Alert ground's
        # keys, ship instead of the picture-matched packs
        own = root / "art" / "remaster" / "textures" / bank
        if (own / "set_144.json").is_file():
            subprocess.run([sys.executable, str(root / "tools" / "bakery" / "pack_remaster_terrain.py")],
                           check=True)
            subprocess.run([sys.executable, str(HERE / "bake_ra_terrain3d.py"), "frombank", bank,
                            str(own / f"terrainenh_{bank}.pack"), str(own / f"terrainseam_{bank}.pack"),
                            str(module / f"terrainenh_RA_{th}.pack"),
                            str(module / f"terrainseam_RA_{th}.pack")], check=True)
            for kind in ("terrainenh", "terrainseam"):
                (module / f"{kind}_{bank}.pack").unlink(missing_ok=True)
            continue
        src = root / "art" / "remaster" / "textures" / td / f"terrainenh_{td}.pack"
        if not src.is_file():
            subprocess.run([sys.executable, str(root / "tools" / "bakery" / "pack_remaster_terrain.py")],
                           check=True)
        subprocess.run([sys.executable, str(HERE / "bake_ra_terrain3d.py"), "ra144", bank, str(src),
                        str(module / f"terrainenh_RA_{th}.pack")], check=True)
        seam = src.parent / f"terrainseam_{td}.pack"
        subprocess.run([sys.executable, str(HERE / "bake_ra_terrain3d.py"), "raseam", bank, str(seam),
                        str(module / f"terrainseam_RA_{th}.pack")], check=True)
        (module / f"terrainenh_{bank}.pack").unlink(missing_ok=True)   # the bank-keyed form, retired
    movies = sorted((args.data / "movies").glob("*.VQA"))
    for movie in movies:
        shutil.copy2(movie, module / "movies" / movie.name)
    # The released disc's Allied ending has MIX ID C1FB1EE5, exactly the ID of
    # ALLYEND.VQA. The engine's VQName table instead asks for ALLIEND. Keep the
    # disc extraction under its unknown hash and stage this explicit runtime
    # alias so the original ending reel is found at the campaign finale.
    ending = args.data / "movies" / "_C1FB1EE5.BIN"
    if mix_id("ALLYEND.VQA") != 0xC1FB1EE5 or not ending.is_file():
        raise RuntimeError("the original Allied ending reel is missing")
    with ending.open("rb") as source:
        header = source.read(12)
    if header[:4] != b"FORM" or header[8:12] != b"WVQA":
        raise RuntimeError("the Allied ending reel is not a VQA movie")
    shutil.copy2(ending, module / "movies" / "ALLIEND.VQA")
    # Enhanced's 3-D soldiers made for Red Alert's own types (game/einf_ra, written by
    # tools/art/remaster/infantry/make_soldier.sh and make_dog.sh). The types whose 1996
    # sprite is Tiberian Dawn's wear the C&C models in the game folder's einf/, which the
    # renderer reads after ra3d/einf/ (model3d_mod.h).
    einf = Path(__file__).resolve().parents[2] / "game" / "einf_ra"
    models = sorted(einf.glob("*.c3m"))
    if not models:
        raise RuntimeError(f"no Red Alert soldier models in {einf}")
    (module / "einf").mkdir(parents=True, exist_ok=True)
    for model in models:
        shutil.copy2(model, module / "einf" / model.name)
    from importlib import import_module
    import_module("stage-structures").stage(module)
    if args.brain.resolve() != (module / args.brain.name).resolve():
        shutil.copy2(args.brain, module / args.brain.name)
    if args.app.resolve() != (module / args.app.name).resolve():
        shutil.copy2(args.app, module / args.app.name)
    for library in libraries:
        if (args.app.parent / library).resolve() != (module / library).resolve():
            shutil.copy2(args.app.parent / library, module / library)
    print(f"RA3D campaigns staged in {module}: {mission_count} mission INIs, "
          f"{len(movies) + 1} original movies including the Allied ending alias")
    print("The original simulation, Classic terrain and Win95 sidebar use Red Alert data")
    print(f"Enhanced's Red Alert soldiers: {len(models)} models in {module / 'einf'}")


if __name__ == "__main__":
    main()
