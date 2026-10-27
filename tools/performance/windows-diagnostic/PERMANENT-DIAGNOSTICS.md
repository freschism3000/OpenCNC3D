# Automatic Windows performance reports

Run `OpenCNC3D-Diagnostics.exe` beside the existing complete game installation. It opens the normal game and uses the same installed data, settings and saves. It does not change graphics quality, resolution, fullscreen or VSync. The diagnostic executable is optimized, like the normal game; it is not an unoptimized debug build.

Play normally, reproduce the slowdown and exit the game. Compress the new `Logs/session-…-UTC-…` folder and attach it to the report. If the installation is read-only, the folder is created under `%LOCALAPPDATA%\OpenCNC3D\Logs`. Neither destination is included as a personal path in the report. If both destinations fail, an ordinary interactive startup explains that logging is unavailable and the game still runs. Automated/background runs never show that dialog.

| File | Evidence |
| --- | --- |
| `seconds.csv` | Full-session, per-second mission summaries: CPU frame mean/worst, simulation, refresh, render and swap split, CPU render passes, asynchronous GPU coverage, workload and cache gauges. |
| `frames-context.csv` | Every second's worst retained frame, plus bounded context around slow frames. Includes actual drawable/display/VSync, graphics choices, camera, simulation tick, particles/debris, object-kind counts and per-pass mesh workload. |
| `gpu-completions.csv` | Asynchronous GPU spans joined by session-wide frame ID and mission. Unmatched or late samples are identified, rather than silently attributed to another frame. |
| `timeline.csv` | Startup/menu/movie/load/mission/shutdown phases, periodic heartbeats, full settings snapshots, speed/display changes and observed model-stage changes with known structure type and engine frame. Model-stage observations are sampled, not a complete gameplay command log. |
| `settings-….cfg` | Full numeric visual configuration on startup/change, including settings retained by each graphics mode. |
| `hardware-and-process.csv` | OS, CPU, RAM, GPU model/driver and GL build/renderer metadata; process CPU, working set/private commit and available RAM. Windows-reported clock values are labelled as reports, not measured effective clocks or temperature. |
| `schema.csv` | Names for CPU-pass and extra-counter columns. |
| `status.json` | Clean exit, recording errors, bounded memory/disk limits, queue/gap/schema/event/context losses and matching GPU coverage. |

CPU `frame_total_ms` is the instrumented game loop's wall time. `inter_present_ms` measures between consecutive retained swap endpoints; it is blank across missing/cancelled records or mission changes. It is **not displayed FPS**. The GPU timestamp span is asynchronous GL execution elapsed time; it is not GPU utilization. CPU driver waits may appear inside render or swap timings. The report cannot independently prove what the compositor displayed. Mesh draw/upload counters cover the named model paths, rather than every GL API call in the process.

The producer uses a fixed queue and never waits for file output. A worker drains it, writes buffered records and updates partial reports about once per second. Detailed context retains at most 1,024 frames and targets two seconds before/after a frame above 33.3 ms or a substantial increase over its rolling baseline. At unusually high frame rates the time window may be shorter; this is counted. Total streaming budgets per session are 192 MiB for summaries, 48 MiB for detailed/GPU rows, 8 MiB for settings and 8 MiB for events/hardware/schema. Budget or queue losses are explicit; the game continues.

A clean exit drains the queue and closes files. A blocked writer is given a bounded 500 ms exit wait and is then abandoned safely; a repeated exit call does not wait again. An abnormal exit usually leaves flushed records and an unfinished status. The final CSV row may be incomplete and should be ignored. Logging and GPU queries add observer overhead; compare with the normal executable on the same PC before interpreting small changes.

Raw stdout/stderr, file paths, user/map/save names, network addresses, player identity, computer names and device serials are excluded. Only known built-in structure identifiers are named; other models are generic. No report is uploaded automatically. Inspect the folder before sharing it.

The normal frontend never starts automatic diagnostics. Its existing explicit `OPENCNC3D_PERFTRACE` workflow remains separate. The dedicated frontend contains the retained entry marker `OPENCNC3D|diagnostics|1` and uses the window title `OpenCNC3D Diagnostics`, so packaging can verify that it is the automatic entry.

Data-only verification:

```sh
python3 tools/performance/windows-diagnostic/test-diagnostic-session.py --repo .
python3 tools/performance/windows-diagnostic/test-trace.py
```

Native Windows driver performance and compositor timing still require a tester run. Mocked SDL/GL and Windows metadata tests do not substitute for that measurement.

Standard Windows full and binaries-only packages include `OpenCNC3D-Diagnostics.exe` beside the normal game. Their packager verifies the dedicated entry marker, PE32/large-address-aware shape and matching build identities; a normal `vVERSION|cooked` release needs no diagnostic revision suffix or special launcher.

The separate add-on workflow below is for a source-stamped diagnostic build (`vVERSION+revision|cooked`), rather than normal release packaging. The older explicit trace add-on remains separate.

Reproducible add-on packaging after building that dedicated frontend from committed main:

```sh
python3 tools/performance/windows-diagnostic/make-permanent-bundle.py \
  --binary-dir build/win --source-commit "$(git rev-parse HEAD)" \
  --runtime-asset playable/ebld/FACT.ebld.fclv \
  --output build/diagnostics/OpenCNC3D-Windows-Diagnostics.zip
```

The packager checks PE32, large-address awareness, matching cooked build provenance,
the dedicated entry marker and exact ZIP member hashes. It reads the repository
version for future builds. The add-on preserves the complete installation's existing
libraries; it imposes no launcher fingerprint gate.
