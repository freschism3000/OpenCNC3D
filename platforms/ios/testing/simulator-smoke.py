#!/usr/bin/env python3
"""Background CoreSimulator smoke checks. Never opens/focuses Simulator.app."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import time
import wave
from array import array

HERE = Path(__file__).resolve().parent
IOS = HERE.parent
ENV = dict(os.environ, DEVELOPER_DIR=os.environ.get("DEVELOPER_DIR", "/Applications/Xcode.app/Contents/Developer"))
RUNTIME = "com.apple.CoreSimulator.SimRuntime.iOS-26-2"
DEVICE_TYPE = "com.apple.CoreSimulator.SimDeviceType.iPad-Pro-11-inch-M5-12GB"
DEVICE_NAME = "OpenCNC3D iPad Smoke 2026-10-18"
MOBILE_CHECKS = (
    "native fixture available",
    "select real visible infantry",
    "navigation preserves exact recipients",
    "send move through native command API",
    "engine NavCom or actual position confirms move",
    "engine clears selected movement on Stop",
    "start actual Power Plant production",
    "native credits debited and completed Ready reported",
    "enter real placement",
    "find engine-legal visible foundation",
    "position preview without committing",
    "first location press sends no placement",
    "cancel preview preserves completed Ready",
    "explicit confirm sends legal placement",
    "engine owns one new structure after confirmation",
)

def verify_mobile_selftest(mission_log):
    """Require every actual engine check and its concrete end-state summary."""
    if "|FAIL" in mission_log:
        raise RuntimeError("Native mobile self-test reported a failed engine check")
    if "IOS|mobile-fixture|PASS|" not in mission_log:
        raise RuntimeError("Native mobile self-test did not verify the real mission fixture")
    checks = re.findall(r"IOS\|mobile-(?:check|selftest)\|PASS\|([^\r\n|]+)\|tick=(-?\d+)", mission_log)
    names = [name for name, _ in checks]
    if names != list(MOBILE_CHECKS):
        raise RuntimeError(f"Incomplete or unexpected native mobile checks: {names}")
    summary = re.search(
        r"IOS\|mobile-selftest\|PASS\|structures=(\d+)->(\d+)\|credits=(\d+)->(\d+)\|placement-requests=(\d+)",
        mission_log)
    if not summary:
        raise RuntimeError("Native mobile self-test success summary is missing")
    old_structures, new_structures, old_credits, new_credits, requests = map(int, summary.groups())
    if new_structures != old_structures + 1 or new_credits >= old_credits or requests != 1:
        raise RuntimeError("Native mobile self-test end state does not prove one paid placement")
    return {"checks": [{"name": name, "tick": int(tick)} for name, tick in checks],
            "check_count": len(checks), "structures_before": old_structures,
            "structures_after": new_structures, "credits_before": old_credits,
            "credits_after": new_credits, "placement_requests_sent": requests,
            "physical_touch_tested": False}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare-only", action="store_true", help="Boot the isolated simulator and execute the real brain API check only")
    parser.add_argument("--app", type=Path, default=IOS / "build/iphonesimulator/OpenCNC3DiOS.app")
    parser.add_argument("--brain", type=Path, default=IOS / "brain/build-simulator/opencnc3d-ios-brain-link-check")
    parser.add_argument("--udid", help="Use this explicitly chosen simulator instead of finding/creating the task simulator")
    parser.add_argument("--timeout", type=int, default=120, help="Mission launch timeout in seconds")
    parser.add_argument("--mode", choices=("shot", "loop", "mobile-selftest"), default="shot", help="60-tick still, bounded real loop with silent WAV recording, or native mobile command/construction checks")
    parser.add_argument("--seconds", type=float, default=3, help="Real game-loop duration for --mode loop")
    parser.add_argument("--shutdown-on-finish", action="store_true")
    args = parser.parse_args()
    stamp = time.strftime("%Y%m%d-%H%M%S", time.gmtime())
    evidence = HERE / "evidence" / stamp
    evidence.mkdir(parents=True)
    report = {"started_utc": stamp, "visible_ui_opened": False, "runtime": RUNTIME,
              "device_type": DEVICE_TYPE, "prepare_only": args.prepare_only, "mode": args.mode,
              "steps": [], "passed": False}
    udid = None
    bundle_id = None

    def run(label, command, timeout=120, check=True):
        print(f"SMOKE|{label}", flush=True)
        started = time.monotonic()
        log = evidence / f"{label}.log"
        try:
            result = subprocess.run(command, env=ENV, text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=timeout)
        except subprocess.TimeoutExpired as error:
            captured = error.stdout or ""
            if isinstance(captured, bytes):
                captured = captured.decode("utf-8", errors="replace")
            log.write_text(captured)
            report["steps"].append({"name": label, "timed_out": True, "timeout_seconds": timeout,
                                    "seconds": round(time.monotonic() - started, 3), "log": str(log)})
            raise
        log.write_text(result.stdout)
        report["steps"].append({"name": label, "exit_code": result.returncode,
                                "seconds": round(time.monotonic() - started, 3), "log": str(log)})
        if check and result.returncode:
            raise RuntimeError(f"{label} failed ({result.returncode}): {result.stdout[-5000:]}")
        return result

    def sim(label, *parts, **kwargs):
        return run(label, ["xcrun", "simctl", *map(str, parts)], **kwargs)

    def launch_mission(arguments):
        # SDL deliberately leaves UIKit's run loop alive after SDL_main returns.
        # Observe the bootstrap's clean-completion marker, then terminate the
        # isolated simulator app explicitly; no forced exit is added to game code.
        label = "mission"
        log = evidence / "mission.log"
        command = ["xcrun", "simctl", "launch", "--console", "--terminate-running-process", udid, bundle_id, *map(str, arguments)]
        started = time.monotonic()
        print("SMOKE|mission", flush=True)
        with log.open("w") as output:
            process = subprocess.Popen(command, env=ENV, stdout=output, stderr=subprocess.STDOUT)
            try:
                while True:
                    text = log.read_text()
                    match = re.search(r"IOS\|exit\|(-?\d+)", text)
                    if match:
                        if match[1] != "0":
                            raise RuntimeError(f"Bootstrap exited with {match[1]}: {text[-4000:]}")
                        sim("terminate-completed-app", "terminate", udid, bundle_id, check=False)
                        try:
                            process.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            process.terminate()
                            process.wait(timeout=5)
                        report["steps"].append({"name": label, "bootstrap_exit_code": 0,
                                                "console_exit_code": process.returncode,
                                                "completion": "IOS|exit|0 then simctl terminate",
                                                "seconds": round(time.monotonic() - started, 3), "log": str(log)})
                        return log.read_text()
                    if process.poll() is not None:
                        raise RuntimeError(f"Simulator app/console ended without IOS|exit|0 ({process.returncode}): {text[-4000:]}")
                    if time.monotonic() - started > args.timeout:
                        raise RuntimeError(f"Mission timed out after {args.timeout}s: {text[-4000:]}")
                    time.sleep(0.25)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)

    try:
        runtimes = json.loads(sim("runtimes", "list", "-j", "runtimes").stdout)["runtimes"]
        if not any(r["identifier"] == RUNTIME and r.get("isAvailable") for r in runtimes):
            raise RuntimeError("Required installed iOS 26.2 simulator runtime is unavailable")
        devices = json.loads(sim("devices", "list", "-j", "devices", "available").stdout)["devices"].get(RUNTIME, [])
        if args.udid:
            matches = [d for d in devices if d["udid"] == args.udid]
            if len(matches) != 1:
                raise RuntimeError("Requested UDID is not an available iOS 26.2 simulator")
            device = matches[0]
        else:
            matches = [d for d in devices if d["name"] == DEVICE_NAME and d["deviceTypeIdentifier"] == DEVICE_TYPE]
            if len(matches) > 1:
                raise RuntimeError("Several task simulators exist; select one explicitly with --udid")
            if matches:
                device = matches[0]
            else:
                udid = sim("create", "create", DEVICE_NAME, DEVICE_TYPE, RUNTIME).stdout.strip()
                device = {"udid": udid, "name": DEVICE_NAME, "state": "Shutdown", "deviceTypeIdentifier": DEVICE_TYPE}
        udid = device["udid"]
        report["simulator"] = {k: device[k] for k in ("udid", "name", "deviceTypeIdentifier")}
        (HERE / "device.json").write_text(json.dumps(report["simulator"], indent=2) + "\n")
        print(f"SMOKE|simulator|{udid}", flush=True)
        if device["state"] != "Booted":
            sim("boot", "boot", udid)
        sim("bootstatus", "bootstatus", udid, "-b", timeout=300)

        if not args.brain.is_file():
            raise RuntimeError(f"Build the simulator brain first: {args.brain}")
        brain_copy = HERE / "staged" / args.brain.name
        brain_copy.parent.mkdir(exist_ok=True)
        shutil.copy2(args.brain, brain_copy)
        run("sign-brain", ["codesign", "--force", "--sign", "-", str(brain_copy)])
        brain = sim("brain-api", "spawn", udid, brain_copy.resolve())
        if "Tiberian Dawn static iOS brain: 67 exports; API " not in brain.stdout:
            raise RuntimeError(f"Unexpected real brain API result: {brain.stdout}")
        report["brain_api"] = brain.stdout.strip()
        print(f"SMOKE|brain|{report['brain_api']}", flush=True)

        if not args.prepare_only:
            if not args.app.is_dir():
                raise RuntimeError(f"App bundle is not built yet: {args.app}")
            app_copy = HERE / "staged" / args.app.name
            if app_copy.exists():
                shutil.rmtree(app_copy)
            shutil.copytree(args.app, app_copy)
            with (app_copy / "Info.plist").open("rb") as handle:
                info = plistlib.load(handle)
            bundle_id = info["CFBundleIdentifier"]
            executable = app_copy / info["CFBundleExecutable"]
            report["app"] = {"source": str(args.app.resolve()), "bundle_identifier": bundle_id,
                             "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest()}
            run("sign-app", ["codesign", "--force", "--deep", "--sign", "-", str(app_copy)])
            run("verify-signature", ["codesign", "--verify", "--deep", "--strict", str(app_copy)])
            sim("install", "install", udid, app_copy.resolve())
            container = Path(sim("container", "get_app_container", udid, bundle_id, "data").stdout.strip())
            shot = container / "Documents/ios-first-mission.png"
            audio = container / "Documents/ios-audio.wav"
            if args.mode == "shot":
                if shot.exists():
                    shot.unlink()
                mission_log = launch_mission(["--shot", shot, "--ticks", "60"])
            elif args.mode == "loop":
                if args.seconds <= 0:
                    raise RuntimeError("Loop duration must be greater than zero")
                if audio.exists():
                    audio.unlink()
                mission_log = launch_mission(["--run", str(args.seconds), "--audiowav", audio])
            else:
                mission_log = launch_mission(["--mobile-selftest", "--nosound"])
            if "IOS|mission|booted|SCG01EA" not in mission_log or "IOS|exit|0" not in mission_log:
                raise RuntimeError("Mission did not report a successful real SCG01EA boot and clean exit")
            if args.mode == "shot":
                if "shot: 60 ticks," not in mission_log or not shot.is_file() or shot.stat().st_size < 1000:
                    raise RuntimeError("60-tick mission screenshot is missing or empty")
                local_shot = evidence / "ios-first-mission.png"
                shutil.copy2(shot, local_shot)
                report["screenshot"] = {"path": str(local_shot), "bytes": local_shot.stat().st_size,
                                        "sha256": hashlib.sha256(local_shot.read_bytes()).hexdigest()}
                print(f"SMOKE|screenshot|{local_shot}", flush=True)
            elif args.mode == "loop":
                loop = re.search(r"GAMELOOP\|end\|ticks=(\d+)\|frames=(\d+)\|objects=(\d+)\|reason=(\d+)", mission_log)
                if not loop or min(int(loop[1]), int(loop[2])) <= 0:
                    raise RuntimeError("Real game loop did not report advancing engine ticks and rendered frames")
                if not audio.is_file():
                    raise RuntimeError("Silent audio recording was not written")
                local_audio = evidence / "ios-audio.wav"
                shutil.copy2(audio, local_audio)
                with wave.open(str(local_audio), "rb") as recording:
                    if recording.getsampwidth() != 2:
                        raise RuntimeError("Expected 16-bit PCM recording")
                    samples = array("h", recording.readframes(recording.getnframes()))
                    if sys.byteorder != "little":
                        samples.byteswap()
                    peak = max(map(abs, samples), default=0)
                    nonzero = sum(v != 0 for v in samples)
                    report["audio"] = {"path": str(local_audio), "frames": recording.getnframes(),
                                       "channels": recording.getnchannels(), "sample_rate": recording.getframerate(),
                                       "seconds": recording.getnframes() / recording.getframerate(),
                                       "peak": peak, "nonzero_samples": nonzero,
                                       "sha256": hashlib.sha256(local_audio.read_bytes()).hexdigest()}
                if not peak or not nonzero:
                    raise RuntimeError("Recorded audio contains only silence")
                report["game_loop"] = dict(zip(("ticks", "frames", "objects", "reason"), map(int, loop.groups())))
                print(f"SMOKE|game-loop|{report['game_loop']}", flush=True)
                print(f"SMOKE|audio|peak={peak}|nonzero={nonzero}|{local_audio}", flush=True)
            else:
                report["mobile_selftest"] = verify_mobile_selftest(mission_log)
                print(f"SMOKE|mobile-selftest|{report['mobile_selftest']}", flush=True)
        report["passed"] = True
    except (RuntimeError, OSError, subprocess.TimeoutExpired, ValueError, KeyError) as error:
        report["error"] = str(error)
        print(f"SMOKE|failed|{error}", file=sys.stderr, flush=True)
        if udid and bundle_id:
            try:
                sim("terminate-after-failure", "terminate", udid, bundle_id, check=False)
            except (OSError, subprocess.TimeoutExpired):
                pass
    finally:
        if udid and args.shutdown_on_finish:
            try:
                sim("shutdown", "shutdown", udid, check=False)
            except (OSError, subprocess.TimeoutExpired):
                pass
        (evidence / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        (HERE / "latest-report.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"SMOKE|report|{evidence / 'report.json'}", flush=True)
    return 0 if report["passed"] else 1

if __name__ == "__main__":
    raise SystemExit(main())
