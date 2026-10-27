#!/usr/bin/env python3
"""Run real headless TD/RA simulations for fair, per-seat AI regression checks."""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def fields(line):
    return dict(part.split("=", 1) for part in line.split("|") if "=" in part)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", type=Path, default=ROOT / "playable")
    parser.add_argument("--td", type=Path)
    parser.add_argument("--ra", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--ticks", type=int, default=12000)
    args = parser.parse_args()
    install, out = args.install.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    wire = out / "gate-ai-difficulty"
    subprocess.run(["cc", "-std=gnu89", "-O2", "-o", str(wire),
                    *[str(ROOT / "net" / name) for name in
                      ("gate_ai_difficulty.c", "lockstep.c", "net_udp.c", "roomcode.c")]], check=True)
    subprocess.run([str(wire)], check=True)
    binaries = {}
    for game in ("td", "ra"):
        binary = out / ("probe-" + game)
        command = ["clang++", "-std=c++14", "-O1", "-fms-extensions", "-fdeclspec"]
        if game == "ra":
            command += ["-DFAIR_AI_RED_ALERT"]
        command += ["-o", str(binary), str(ROOT / "brain/host/fair_ai_probe.cpp")]
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
        binaries[game] = binary
    libs = {"td": (args.td or install / "TiberianDawn.dylib").resolve(),
            "ra": (args.ra or install / "ra3d/RedAlert.dylib").resolve()}
    jobs = []
    for game in ("td", "ra"):
        scenario = "SCM01EA" if game == "td" else "SCM08EA"
        for level in range(4):
            jobs.append(dict(name=f"{game}-level-{level}", game=game, level=level,
                             scenario=scenario, save=3000))
        for suffix, options in [
                ("repeat", {}), ("hidden-money", {"perturb": 1}),
                ("context", {"context": 1}), ("same-country", {"perturb": 2}),
                ("mixed", {"level": -1, "bots": 4, "scenario": "SCM01EA", "save": 3000}),
                ("other-seed", {"seed": 4243})]:
            job = dict(name=f"{game}-{suffix}", game=game, level=1,
                       scenario=scenario, save=0)
            job.update(options)
            jobs.append(job)
        jobs.append(dict(name=f"{game}-lowtech", game=game, level=3,
                         scenario=scenario, save=3000, tech=1))
        if game == "ra":
            jobs.append(dict(name="ra-regrow", game=game, level=3,
                             scenario=scenario, save=3000, perturb=4))

    def run(job):
        game, folder = job["game"], out / job["name"]
        folder.mkdir(exist_ok=True)
        module = install if game == "td" else install / "ra3d"
        if game == "td":
            for name in ("CONQUER.MIX", "content"):
                link = folder / name
                if not link.exists():
                    link.symlink_to(module / name)
        command = [str(binaries[game]), str(libs[game]), str(module / "missions") + "/",
                   str(module / ("content" if game == "td" else "data")) + "/",
                   job["scenario"], str(job["level"]), str(args.ticks),
                   str(job.get("seed", 4242)), str(job.get("bots", 1)),
                   str(job.get("save", 0)), str(job.get("perturb", 0))]
        env = dict(os.environ, OPENCNC3D_BACKGROUND="1")
        if job.get("tech"):
            env["FAIR_AI_TECH"] = str(job["tech"])
        if job.get("context"):
            env["FAIR_AI_CONTEXT"] = str(job["context"])
        with (folder / "probe.log").open("w") as log:
            process = subprocess.run(command, cwd=folder, env=env, stdout=log,
                                     stderr=subprocess.STDOUT, timeout=120)
        text = (folder / "probe.log").read_text()
        assert process.returncode == 0 and "PROBE|pass=1|" in text, (job["name"], text[-1800:])
        if job.get("save"):
            assert "PROBE|roundtrip=pass|" in text, job["name"]
        snapshots = [fields(line) for line in text.splitlines() if line.startswith("PROBE|tick=")]
        diagnostics = [fields(line) for line in text.splitlines() if line.startswith("FAIRAI|")]
        rules = [fields(line) for line in text.splitlines() if line.startswith("FAIRRULE|")]
        assert diagnostics and rules, (job["name"], "missing real controller/rule telemetry")
        if job["level"] >= 0:
            assert all(int(row["level"]) == job["level"] for row in diagnostics), job["name"]
            assert int(diagnostics[-1].get("think", diagnostics[-1].get("thinks", 0))) > 0
            assert int(diagnostics[-1]["orders"]) > 0, (job["name"], "no orders")
            assert int(diagnostics[-1]["scouts"]) > 0, (job["name"], "no scouting")
        else:
            assert {int(row["level"]) for row in diagnostics} == {0, 1, 2, 3}, job["name"]
        assert all(int(row["difficulty"]) == 1 for row in rules), (job["name"], "non-neutral handicap")
        if job.get("perturb") == 2:
            human = next(row for row in rules if row["human"] == "1")
            computer = next(row for row in rules if row["human"] == "0")
            for key in ("fire", "ground", "air", "armor", "rof", "cost", "build"):
                assert human[key] == computer[key], (job["name"], key, human, computer)
        # Census deployed structures, deliberately excluding pending/limbo objects.
        maximum = 0
        max_refineries = 0
        for snapshot in snapshots:
            world = (folder / f"world-{int(snapshot['tick']):06d}.txt").read_text()
            humans = {line.split("|")[1] for line in world.splitlines()
                      if line.startswith("HOUSE|") and "|human=1|" in line}
            buildings = [line for line in world.splitlines() if line.startswith("OBJ|BUILDING|")
                         and line.split("|")[3] not in humans and "|limbo=0|" in line]
            maximum = max(maximum, len(buildings))
            max_refineries = max(max_refineries, sum(line.split("|")[2] == "PROC" for line in buildings))
        if job.get("tech") == 1:
            assert max_refineries >= 2, (job["name"], "unavailable factory blocked legal expansion", max_refineries)
            assert int(diagnostics[-1]["attacks"]) > 0, (job["name"], "no low-tech attacks")
        assert maximum >= 4, (job["name"], "economy failed to develop", maximum)
        result = dict(job, snapshots=snapshots, diagnostics=diagnostics[-job.get("bots", 1):],
                      deployed_buildings_max=maximum, refineries_max=max_refineries, rules=rules[-2:])
        print("PASS", job["name"], "deployed", maximum, "ticks", snapshots[-1]["tick"], flush=True)
        return job["name"], result

    results = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        for name, result in executor.map(run, jobs):
            results[name] = result
    for game in ("td", "ra"):
        base = results[f"{game}-level-1"]["snapshots"]
        for suffix in ("repeat", "context"):
            assert base == results[f"{game}-{suffix}"]["snapshots"], (game, suffix, "world or private AI state changed")
        hidden = results[f"{game}-hidden-money"]["snapshots"]
        assert [(x["tick"], x["state"]) for x in base] == [(x["tick"], x["state"]) for x in hidden], (game, "hidden enemy money changed decisions")
        assert base != hidden, (game, "negative control: hidden money perturbation did not change the world")
        if game == "ra":
            assert base != results[f"{game}-other-seed"]["snapshots"]
    (out / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print("PASS all profiles, per-seat assignment, rule parity, hidden-information independence, save/resume, repeatability and local contexts")


if __name__ == "__main__":
    main()
