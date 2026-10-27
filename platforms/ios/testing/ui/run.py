#!/usr/bin/env python3
"""Build a standalone UI-test runner and exercise a prebooted headless simulator."""
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

HERE = Path(__file__).resolve().parent
IOS = HERE.parents[1]
ENV = dict(os.environ, DEVELOPER_DIR=os.environ.get("DEVELOPER_DIR", "/Applications/Xcode.app/Contents/Developer"))
RUNTIME = "com.apple.CoreSimulator.SimRuntime.iOS-26-2"
TYPES = {"ipad": "com.apple.CoreSimulator.SimDeviceType.iPad-Pro-11-inch-M5-12GB",
         "iphone": "com.apple.CoreSimulator.SimDeviceType.iPhone-17-Pro"}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--build-only", action="store_true")
    mode.add_argument("--test-only", action="store_true")
    parser.add_argument("--app", type=Path, default=IOS / "build/iphonesimulator/OpenCNC3DiOS.app")
    parser.add_argument("--device", choices=TYPES, default="ipad")
    parser.add_argument("--probe-only", action="store_true", help="Diagnose fixture startup through the CLI console without waiting for UI discovery")
    parser.add_argument("--test", action="append", choices=("testNativeOverlayExists", "testTwoSeparateTapsConstructOneBuilding", "testCancelPlacementKeepsReadyBuilding", "testMinimapNavigatesAndOrdersCenterCancels", "testTapRealUnitThenHoldDragMove", "testNavigationClearsPlacementConfirmation"))
    args = parser.parse_args()
    if args.probe_only:
        args.test_only = True
    stamp = time.strftime("%Y%m%d-%H%M%S", time.gmtime())
    evidence = HERE / "evidence" / stamp
    evidence.mkdir(parents=True)
    report = {"started_utc": stamp, "visible_ui_opened": False, "steps": [], "passed": False}
    udid = None
    result_bundle = None

    def run(label, command, timeout=180, check=True):
        print(f"UI|{label}", flush=True)
        log = evidence / f"{label}.log"
        started = time.monotonic()
        with log.open("w") as output:
            result = subprocess.run(command, env=ENV, stdout=output, stderr=subprocess.STDOUT, timeout=timeout)
        text = log.read_text()
        report["steps"].append({"name": label, "exit_code": result.returncode,
                                "seconds": round(time.monotonic() - started, 3), "log": str(log)})
        if result.returncode and check:
            raise RuntimeError(f"{label} failed ({result.returncode}): {text[-6000:]}")
        return text

    def sim(label, *parts, **kwargs):
        return run(label, ["xcrun", "simctl", *map(str, parts)], **kwargs)

    try:
        if not args.test_only:
            run("generate", [sys.executable, str(HERE / "generate-project.py")])
            run("build-runner", ["xcodebuild", "build-for-testing", "-project", str(HERE / "generated/MobileControlsUITests.xcodeproj"),
                                  "-scheme", "MobileControlsUITests", "-sdk", "iphonesimulator", "-destination", "generic/platform=iOS Simulator",
                                  "-derivedDataPath", str(HERE / "derived"), "ARCHS=arm64", "CODE_SIGNING_ALLOWED=NO"], timeout=300)
        if args.build_only:
            report["passed"] = True
            return 0
        products = HERE / "derived/Build/Products"
        specifications = sorted(products.glob("*.xctestrun"))
        if len(specifications) != 1:
            raise RuntimeError("Expected one built .xctestrun specification; run --build-only first")
        if not args.app.is_dir():
            raise RuntimeError(f"Missing simulator app bundle: {args.app}")

        # No Simulator.app launch: boot the task's destination through CoreSimulator.
        name = f"OpenCNC3D Native Touch {args.device} 2026-10-18"
        devices = json.loads(sim("list-devices", "list", "-j", "devices", "available"))["devices"].get(RUNTIME, [])
        matches = [d for d in devices if d["name"] == name and d["deviceTypeIdentifier"] == TYPES[args.device]]
        if len(matches) > 1:
            raise RuntimeError("Several isolated touch-test simulators match; resolve duplicates before running")
        if matches:
            device = matches[0]
        else:
            identifier = sim("create", "create", name, TYPES[args.device], RUNTIME).strip()
            device = {"udid": identifier, "name": name, "deviceTypeIdentifier": TYPES[args.device], "state": "Shutdown"}
        udid = device["udid"]
        report["simulator"] = {key: device[key] for key in ("udid", "name", "deviceTypeIdentifier")}
        print(f"UI|simulator|{udid}", flush=True)
        if device["state"] != "Booted":
            sim("boot", "boot", udid)
        sim("bootstatus", "bootstatus", udid, "-b", timeout=180)

        staged = HERE / "staged" / args.app.name
        if staged.exists():
            shutil.rmtree(staged)
        shutil.copytree(args.app, staged)
        info = plistlib.loads((staged / "Info.plist").read_bytes())
        report["app"] = {"source": str(args.app.resolve()), "bundle_identifier": info["CFBundleIdentifier"],
                         "executable_sha256_before_test_signing": hashlib.sha256((staged / info["CFBundleExecutable"]).read_bytes()).hexdigest()}
        report["test_source_sha256"] = hashlib.sha256((HERE / "MobileControlsUITests.m").read_bytes()).hexdigest()
        run("sign-game", ["codesign", "--force", "--deep", "--sign", "-", str(staged)])
        sim("install-game", "install", udid, staged)
        if args.probe_only:
            command = ["xcrun", "simctl", "launch", "--console", "--terminate-running-process", udid,
                       "org.opencnc3d.ios.development", "--mobile-fixture", "--nosound", "--run", "2"]
            log = evidence / "fixture-console.log"
            started = time.monotonic()
            print("UI|fixture-probe", flush=True)
            with log.open("w") as output:
                process = subprocess.Popen(command, env=ENV, stdout=output, stderr=subprocess.STDOUT)
                try:
                    while True:
                        text = log.read_text()
                        completed = re.search(r"IOS\|exit\|(-?\d+)", text)
                        if completed:
                            report["fixture_exit_code"] = int(completed[1])
                            if completed[1] != "0":
                                raise RuntimeError(f"Fixture bootstrap failed: {text[-5000:]}")
                            report["passed"] = True
                            return 0
                        if process.poll() is not None:
                            raise RuntimeError(f"Fixture app ended without clean completion: {text[-5000:]}")
                        if time.monotonic() - started > 45:
                            raise RuntimeError(f"Fixture probe timed out: {text[-5000:]}")
                        time.sleep(.25)
                finally:
                    sim("terminate-fixture-probe", "terminate", udid, "org.opencnc3d.ios.development", check=False)
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.terminate()
                        process.wait(timeout=5)
        for index, runner in enumerate(products.glob("Debug-iphonesimulator/*-Runner.app")):
            run(f"sign-runner-{index}", ["codesign", "--force", "--deep", "--sign", "-", str(runner)])

        result_bundle = evidence / "NativeTouch.xcresult"
        report["result_bundle"] = str(result_bundle)
        command = ["xcodebuild", "test-without-building", "-xctestrun", str(specifications[0]),
                   "-destination", f"platform=iOS Simulator,id={udid}", "-parallel-testing-enabled", "NO",
                   "-maximum-concurrent-test-simulator-destinations", "1", "-resultBundlePath", str(result_bundle),
                   "-test-timeouts-enabled", "YES", "-default-test-execution-time-allowance", "120",
                   "-maximum-test-execution-time-allowance", "180"]
        for test in args.test or []:
            command.extend(["-only-testing", f"MobileControlsUITests/MobileControlsUITests/{test}"])
        run("native-touch-tests", command, timeout=420)
        summary = json.loads(run("test-summary", ["xcrun", "xcresulttool", "get", "test-results", "summary", "--path", str(result_bundle)]))
        report["test_summary"] = summary
        expected_count = len(args.test) if args.test else 6
        if summary.get("passedTests", 0) < expected_count or summary.get("failedTests", 0):
            raise RuntimeError("XCTest did not report all requested native touch tests as passed")
        report["passed"] = True
    except (RuntimeError, OSError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
        report["error"] = str(error)
        print(f"UI|failed|{error}", file=sys.stderr, flush=True)
    finally:
        if result_bundle and result_bundle.exists():
            try:
                run("export-attachments", ["xcrun", "xcresulttool", "export", "attachments", "--path", str(result_bundle),
                                           "--output-path", str(evidence / "attachments")], check=False)
            except (OSError, subprocess.TimeoutExpired):
                pass
        if udid:
            try:
                sim("terminate-game", "terminate", udid, "org.opencnc3d.ios.development", check=False)
                sim("shutdown", "shutdown", udid, check=False)
            except (OSError, subprocess.TimeoutExpired):
                pass
        path = evidence / "report.json"
        path.write_text(json.dumps(report, indent=2) + "\n")
        print(f"UI|report|{path}", flush=True)
    return 0 if report["passed"] else 1

if __name__ == "__main__":
    raise SystemExit(main())
