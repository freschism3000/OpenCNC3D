# Native bring-up checkpoint — 18 October 2026

Authoritative simulation and renderer: current `$HOME/CNC3D` main, base commit
`71fa4e2154a37c4c76f326d48b2a4a04b5867c14`, including its current local source fixes.
All iOS changes are additive under platforms/ios. No desktop source, executable,
Dock launcher, settings, saves or unrelated assets are replaced.

- Device and simulator native app bundles compile and link. Mach-O checks confirm
  arm64 IOS and arm64 IOSSIMULATOR respectively, minimum17.0, SDK26.2. All runtime
  dynamic dependencies are Apple iOS frameworks/system libraries; SDL2, GL4ES and
  the real Tiberian Dawn logic are static.
- The first-mission resource manifest verifies26files,35,255,323bytes. No source art,
  desktop executables, personal settings or full content library enters the bundle.
- Background iPad Pro11M5 simulator on iOS26.2 ran the real brain:67exports/API258.
- Screenshot mode advanced SCG01EA by60ticks, reported60objects, and wrote an actual
  native2420×1668 landscape render. It was visually inspected: terrain, water,
  infantry, vehicles, existing sidebar and smooth fog all draw; zero fallback boxes
  and zero invisible terrain reported. Clean IOS|exit|0.
- The real mission loop also ran:29ticks,11frames,60objects, reason0. Silent audio
  recording produced nonzero44.1kHz16-bit stereo samples. The short mixed combat
  recording contains some hard clipping; it is functional audio proof, not an audio
  quality acceptance test.
- Ten boundary/resource safety tests pass. Both complete dependency archives were
  force-loaded and linked for each SDK; real simulation archives fully link as well.
- A dedicated simulator was booted through CLI only. No Simulator.app or visible
  game window was opened; the test app was terminated after each run.

Raw evidence is local under testing/evidence/20261018-144721 (shot) and
20261018-144733 (loop/audio). These generated artifacts are ignored by Git.
This initial checkpoint uses existing original graphics as the renderer baseline.
The later native touch checkpoint below adds the mobile overlay. Full Enhanced
assets remain outside this bounded first-mission target.
Cold simulator warm-up took11.35seconds; this is not device speed or steady-state
frame-rate evidence. Physical-device signing, GPU performance, touch and lifecycle
checks remain outstanding. No connected iOS device or valid local signing identity
was found; an Apple Developer membership alone is not a local signing certificate.

## Native touch checkpoint — 18 October 2026

The touch UI and bridge are additive iOS platform files. The bridge includes the
selected authoritative current-main renderer source, then calls its existing UI
functions in that translation unit. No shared desktop source or Dock binary was
changed. The unsigned arm64 device target was built through the generated Xcode
project using the integrated current-main platform files.

The final v6 native engine test passes all 15 checks, including exact selection
preservation through navigation, an actual NavCom/position movement witness,
Stop clearing movement, normal credit debit, Ready production, a preview sending
no placement request, Cancel preserving Ready, and exactly one owned structure
appearing after confirmation. Structures increase 1→2, credits 2000→1700, and the
native command witness records exactly one placement request. Local report:
testing/evidence/20261018-165612/report.json. This test exercises the engine bridge;
the separate UIKit HID suite verifies physical-style input dispatch.

Native controls use safe-area geometry, a right-side fog-safe navigation map,
finger-sized unit acquisition, camera pan/pinch, box selection, contextual orders,
a hold/drag compass, production/hold/resume/refund and local two-tap placement.
Only an engine-legal complete footprint clear of the HUD can be confirmed.
Navigation, multitouch, changed recipients, modals and app interruption invalidate
pending choices. Haptics are implemented but cannot be accepted in a simulator.

Ten platform/resource boundary tests remain passing. Device and simulator bundle
verification checks arm64 platform linkage, Apple-only dynamic dependencies and
all 26 staged asset hashes. Generated test evidence and build reports remain local.

The v7 UIKit suite passes all six cases on the iPad Pro 11 M5/iOS26.2 simulator:
overlay/accessibility, minimap navigation with selection preservation and centre
cancel, a real unit tap followed by directional Move and destination tap, two
separate building taps, Cancel retaining Ready, and pan/pinch clearing a primed
building choice without any placement request. This uses XCUITest system touch
events, not synthetic SDL event injection. Report:
testing/ui/evidence/20261018-170115/report.json. Full native screen attachments
preserve both the game and UIKit HUD. The same runner supports the phone target;
physical-device installation, touch comfort and haptics still need device review.

The same v7 executable also passes all six cases on iPhone17Pro/iOS26.2 with no
failures or skips. Report: testing/ui/evidence/20261018-170506/report.json. The
second-tap screen shows the real additional Power Plant and increased power.
Both dedicated UI simulators shut down after testing. The executable SHA-256
before ad-hoc simulator signing is
398916d232cb47cb671f0b9c0e11796b11de6a5e8bf590a3e4dd5ad2403f8036.

Final v8 changes only callout drawing: it chooses clear space above, below or
beside the projected foundation rather than covering it at the phone's top edge.
The unchanged two-tap construction test was re-run on both devices (1/1 each),
and both full-footprint/non-overlapping callouts were visually inspected. Reports:
testing/ui/evidence/20261018-171146/report.json (iPad) and
testing/ui/evidence/20261018-171249/report.json (iPhone). Final simulator executable:
e8c55b19f648333fccd46bf46af0afd87cc38355ff0e99dea38acf4012fa566b.
The corresponding unsigned device app also builds and passes bundle/resource
verification. Device installation still awaits a connection and signing setup.
