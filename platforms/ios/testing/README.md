# Background iOS smoke checks

`simulator-smoke.py` uses CoreSimulator command-line tools only. It never opens
or focuses Simulator.app or a visible game window. It creates/reuses only the
task-named iPad simulator unless an explicit `--udid` is passed. Existing unrelated
simulators and native Dock applications are not changed.

The required runtime is installed iOS 26.2. The device type is iPad Pro 11-inch
(M5, 12GB). The task simulator is:

```
OpenCNC3D iPad Smoke 2026-10-18
310A7468-5A77-4CDA-BA6B-C8AF2D6564AD
```

```sh
# Boot only and execute the actual static Tiberian Dawn API probe.
python3 platforms/ios/testing/simulator-smoke.py --prepare-only

# Real first mission, 60 simulated ticks, PNG captured by the actual renderer.
python3 platforms/ios/testing/simulator-smoke.py

# Actual bounded game loop, recording the mixed audio without audible playback.
python3 platforms/ios/testing/simulator-smoke.py --mode loop --seconds 3
```

The script needs access to the user's CoreSimulator service, so the sandboxed
agent must run it with appropriate approval/escalation. It defaults to the full
Xcode developer directory. The app is expected at
`../build/iphonesimulator/OpenCNC3DiOS.app`, and the real-brain probe at
`../brain/build-simulator/opencnc3d-ios-brain-link-check`.

The script copies binaries/bundles into its own ignored `staged/` directory and
applies an ad hoc simulator signature to those copies. It does not use an Apple
developer identity, change account settings, or sign/install on a physical device.
It installs the copied bundle in the isolated simulator, derives the sandbox's
Documents directory, and starts the mission through `simctl launch --console`.
After `IOS|exit|0` it explicitly terminates the completed app: SDL deliberately
keeps UIKit's outer run loop alive after SDL_main returns. A log marker alone is
insufficient: the shot check also requires the 60-tick log and a real PNG, while
the loop check requires advancing engine ticks/frames and nonzero WAV samples.

Every run stores command output and a JSON report under `evidence/<UTC stamp>/`.
`latest-report.json` references the most recent run. Failures/timeouts remain
recorded rather than being overwritten by a later successful run.
`--shutdown-on-finish` shuts down the chosen simulator after the check.

## Verified 18 October 2026

The actual Tiberian Dawn brain executed inside the simulator and reported
**67 exports; API 258**. Two checks passed against app executable SHA-256
`f96639b0e85bb45553dec453c5629e4e1eb591ec6a279935ea49416189b2a7b7`:

- `evidence/20261018-144721/report.json`: SCG01EA booted and advanced 60 ticks,
  producing `ios-first-mission.png` at 2420×1668. The actual PNG was opened and
  visually inspected: textured terrain, water, shoreline, landing craft, infantry,
  vehicle, smooth dark fog, original right sidebar and credits are visible.
  Bootstrap reported `IOS|exit|0`, followed by explicit simulator-app termination.
- `evidence/20261018-144733/report.json`: actual `game_loop` completed with
  **29 engine ticks, 11 rendered frames, 60 objects, exit reason 0**.
  Recorded WAV is 44.1kHz stereo 16-bit PCM, 41,277 frames (0.936 seconds),
  peak 32,767 with 82,466 nonzero samples. This proves audio mixing/recording,
  not physical audio output. The existing mix reported 400 hard-clipped samples
  out of 82,554 (0.485%); audio quality has not been accepted.

The requested three-second loop actually took 12.92 seconds because the existing
Enhanced hidden warm-up consumed 11.35 seconds. Its logged average 0.9 fps includes
that warm-up and comes from the simulator; this is execution evidence, **not a
physical-device performance claim**. Device rendering, performance, signing,
touch usability, lifecycle and audio playback still require device validation.
UIKit logged unbalanced appearance-transition warnings during automated shutdown.

The first shot attempt in `evidence/20261018-144406/` produced a correct PNG and
clean bootstrap-exit marker, but the original harness incorrectly waited for the
UIKit process itself to exit and timed out. That failed report is preserved;
the completed-app termination behavior above fixed the harness and both later
checks passed.
