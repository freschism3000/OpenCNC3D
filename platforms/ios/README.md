# OpenCNC3D iOS bring-up

18 October 2026. Native universal iPhone/iPad development target, arm64, iOS/iPadOS
17 or newer. The first milestone runs the real Tiberian Dawn SCG01EA mission using
the current authoritative source at `$HOME/CNC3D`, not the browser simulation.

The native touch prototype uses the real mission with its existing original
graphics. A UIKit overlay supplies a right-hand navigation map and production
sidebar, unit selection, camera gestures, directional orders and two-tap building
placement. Enhanced rendering parity, physical-device performance and full mobile
lifecycle testing remain later work.
The desktop Dock installation is not replaced by this target.

## Touch prototype

- Tap a friendly unit to select it. Tap a destination or hostile unit for the
  existing contextual order. All, Infantry and Vehicles select visible forces;
  Box select arms a selection rectangle for the next drag.
- Small unit silhouettes have a 22-point acquisition radius, constrained to
  visible, unobscured engine targets. The initial view frames the base or MCV
  within the clear field; Base returns there without issuing a combat order.
- Drag the battlefield to pan; pinch to zoom around the contact point. The
  navigation map uses the engine's existing explored-terrain and visible-object
  plotter. It does not reveal the map or change the engine's radar unlock state.
- Hold the battlefield, drag in a direction and release: Advance (up), Focus
  fire (right), Stop (down), Move (left). Return to the centre to cancel. Holding
  Orders uses the same compass, then asks for a separate destination contact.
  Focus fire refuses ground or friendly targets.
  Sector changes and confirmed placement have native haptic feedback, which
  requires physical-device review.
- Deploy the MCV with its normal contextual tap, then open Build. Production
  reads real costs, credits, progress and Ready flags. Hold, Resume and Refund
  use native sidebar requests. Tap Building ready to begin placement.
- The first location tap positions the complete foundation including its bib;
  the second separate tap at the same cell confirms. The hint sits near that
  position. The whole footprint must remain visible and engine-legal. Navigation,
  a second finger, cancellation, a changed building or a modal clears the choice.
  Cancel placement keeps the completed building Ready.

The iOS-only bridge includes the selected current game source in one translation
unit and calls its existing static UI functions. Shared renderer/simulation
sources are not patched. The original desktop HUD remains available to headless
baseline captures; normal native play replaces it with the touch overlay.

## Build

Prerequisites: full Xcode with the iOS SDK, CMake, Ninja and Python3. No Homebrew
SDL or macOS SDL binaries enter the app. Public dependency source is downloaded
and verified against the SHA-256 lock before compilation.

```sh
platforms/ios/build.sh iphonesimulator
platforms/ios/build.sh iphoneos
```

Each command builds the pinned dependencies when absent, rebuilds the current real
simulation as a static archive, and compiles the renderer plus iOS shell. Output:
`platforms/ios/build/<sdk>/OpenCNC3DiOS.app`. Ninja builds are unsigned development
artifacts. The app stages only the allowlisted 33.62 MiB mission data; nothing is
uploaded or redistributed. `OPENCNC3D_SOURCE_ROOT` can explicitly select the source
checkout; `OPENCNC3D_RUNTIME_SOURCE` is the brain helper's equivalent setting.

## Device signing

```sh
platforms/ios/configure-xcode.sh [APPLE_TEAM_ID]
```

This generates `derived/device/OpenCNC3DiOS.xcodeproj`. In Xcode choose the
OpenCNC3DiOS scheme, connect the target device, and choose the existing Apple
Developer team under Signing & Capabilities. Automatic signing is configured;
account login, certificate/provisioning setup and installation require the local
Apple development setup. Generation itself does not create a certificate or
change an account. No valid code-signing identity or connected physical iOS
device was found during initial bring-up.

## Checks

```sh
python3 -m unittest platforms/ios/test_platform_contract.py
python3 -m unittest discover -s platforms/ios/resources -p 'test_*.py'
python3 platforms/ios/testing/simulator-smoke.py
python3 platforms/ios/testing/simulator-smoke.py --mode mobile-selftest --shutdown-on-finish
python3 platforms/ios/testing/ui/run.py --device ipad
python3 platforms/ios/testing/ui/run.py --test-only --device iphone
```

The simulator harness uses a task-specific background iPad simulator, signs only a
local simulator copy ad hoc, and never opens Simulator.app or a visible game
window. It verifies the real brain API, starts SCG01EA, advances 60 simulation
ticks and collects a native renderer PNG and console evidence. Simulator output
is evidence of native integration, not physical iPad GPU speed or touch comfort.
The separate XCUITest runner sends actual system touch events to the native
UIKit overlay and preserves full-window screenshots, including the touch HUD.
It uses its own simulator and never opens Simulator.app. `--mobile-fixture`
deploys the existing first-mission MCV and advances the real simulation for a
repeatable construction start; it grants no money, units or free buildings.

## Platform boundaries

- The simulation's real exports are bound statically with their original types.
  The existing renderer ABI is retained; no game-code dylib loading is used.
- SDL2 owns UIKit, input, audio and the native OpenGL ES2 context. GL4ES translates
  desktop rendering calls. The state-cache bridge prevents macro overrides from
  accidentally reaching raw native GLES.
- Native framebuffer-zero calls are mapped to SDL's UIKit screen FBO. GL4ES is
  flushed before presentation and the correct native color renderbuffer is bound.
- The mission loop blocks during background events; writable saves use SDL's
  sandbox preference directory. Rotation is initially landscape only.
- External original game assets and all generated binaries/caches remain local.
  See the dependency licenses and resource manifest for provenance.
