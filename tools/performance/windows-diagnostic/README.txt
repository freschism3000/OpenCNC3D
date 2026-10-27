OpenCNC3D Windows performance diagnostic

This is diagnostic launcher revision 3 for the v0.8.4 game. It is not a numbered release.
It retains the fresh-install DLL validation fix and includes corrections for
native fullscreen and saved display mode, Repair/Sell tooltips, settings pointer
rendering, and multiplayer team paint/faction emblems. Enhanced quality stays intact and
VSync is not forced. Your display choices are honored, including native Desktop.

A combined bug-fix bundle may contain one verified Construction Yard faction atlas overlay:
assets/ebld/FACT.ebld.fclv. The script verifies its size/SHA256 before copying it
into the private game after the original game copy checks. It verifies it again
there and records the fingerprint. It never replaces this file in your installation.
Older diagnostic bundles without this optional asset are still supported.

1. Extract this diagnostic ZIP into its own folder. Close other copies of the game.
2. Double-click Run-Windows-Diagnostic.cmd. Choose OpenCNC3D.exe in the existing,
   complete v0.8.4 game installation when asked.
3. The script copies that installation into a separate dated diagnostics folder.
   It needs the size of your game folder plus 1 GB of free space. It verifies the
   copy and diagnostic executable hashes before launching anything. The installed
   engine/SDL libraries must match a published v0.8.4 variant. Both the full
   package and binaries-only package are supported; their engine DLL hashes differ.
   The selected libraries are preserved, verified after copying and recorded in
   library-fingerprints.json. Unknown fingerprints are reported with their actual
   size and SHA256; they are not silently substituted.
4. Start a mission at your usual Enhanced settings. Capture 30 seconds before
   the slowdown, then return to the menu so that baseline is saved separately.
   Play normally for 3-5 minutes to reproduce the slowdown or a large building
   being destroyed. Return to the menu or quit within 30 seconds of the problem.
5. The script prints the local report ZIP path. Review its contents before sharing.
   Add map, FPS and slowdown observations to report/player-notes.txt, then ZIP the
   report folder again if you edited the notes.

Keep the first run at normal full settings. For an OPTIONAL second run, change
only ONE option in the copied game's Visuals screen, reproduce the same scene,
and record the option and approximate time in player-notes.txt. Restore that
option before testing another. This is attribution, not a proposed quality cut.
Useful controls are Water Shader, 3D Trees, 3D Tiberium and Bilinear Filtering.
Also compare 20 seconds paused and 20 seconds unpaused at the same camera.
Changing all four together, or comparing a pause menu with combat, cannot show
which subsystem is expensive. Infantry/DOS Sprites does not disable the separate
3D Soldiers option.

The original installation is not patched. Settings and saves are copied, and
all Options/save/hall-of-fame writes go to the private diagnostic copy. The script
checks the original executables, configs, save files and logs after the run.
Enhanced save sidecars are included. If the normal app would carry old saves over,
the script reproduces that carry-over into the private copy only. It makes copied
configs/saves writable and creates a private writable log before starting.
The game may still use a process-specific temporary world-dump scratch file in
its normal per-user folder; it is normally deleted on close and can remain after
a crash. This is not a save or settings overwrite.

The report contains per-frame CPU/present intervals, asynchronous GPU timings
where supported, display/settings metadata, one-second process memory/CPU samples,
OS/CPU/GPU model and driver fields, and filtered performance log lines. It contains
an asset-fingerprints.json list of relative model/effect filenames, sizes and hashes,
read once before gameplay. This identifies asset revisions without copying the art;
different legitimate asset revisions are recorded rather than rejected. Relevant
model-load/destruction counts are retained in the filtered performance log.
It contains
no game assets, saves, user-map files, serial numbers or environment-variable dump.
Known local/home path prefixes are replaced in the performance log. Review it
before sharing; exception text can contain additional local information. The full
game log and private game/save copy remain on this PC, outside the report ZIP.
No files are uploaded automatically.

Normal mission exit flushes the buffered frame trace into frames-mission-001.csv,
frames-mission-002.csv and so on, with their own settings-start/end snapshots.
If the game crashes, send
the report anyway; process-memory.csv and the filtered log can still be useful.
Frame records use a bounded 16 MiB ring; at high FPS it can retain roughly the
latest 90 seconds, and longer at low FPS. This is why exiting the mission soon
after the problem matters. Check metadata for dropped/retained record counts.

Advanced: tracing-off control, with the same diagnostic executable:
  Run-Windows-Diagnostic.cmd -Control
Advanced: select folders without the picker:
  Run-Windows-Diagnostic.cmd -InstallDir "D:\Games\OpenCNC3D" -OutputRoot "D:\Diagnostics"

PowerShell 5.1 is sufficient; administrator privileges are not needed. The .cmd
allows this extracted local script for its own PowerShell invocation. If Windows
SmartScreen asks about a downloaded executable, verify the download/source first;
do not change system-wide security policy. Keep diagnostic-manifest.json with the
bundle: the script refuses missing or mismatched executable hashes.
