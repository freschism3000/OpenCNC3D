# Native mobile touch tests

This is an additive standalone XCUITest runner. It launches the already-built game
by bundle identifier and sends system touch events through UIKit. It does not
push SDL events, replace game code, build the game, or open Simulator.app.

```sh
# Coordinate this runner build with other iOS builds; it does not rebuild the game.
python3 platforms/ios/testing/ui/run.py --build-only

# Fast fixture/console diagnosis before an expensive accessibility wait.
python3 platforms/ios/testing/ui/run.py --probe-only

# When the native overlay/fixture is available, install it on a fresh task simulator.
python3 platforms/ios/testing/ui/run.py --test-only --device ipad
python3 platforms/ios/testing/ui/run.py --test-only --device iphone

# Overlay-only smoke while construction integration is still in progress.
python3 platforms/ios/testing/ui/run.py --test-only --test testNativeOverlayExists
```

The runner uses the installed iOS26.2 runtime, explicit full Xcode developer
directory, arm64, and a minimum deployment target of iOS17. No account signing is
needed for simulator testing. The fixture probe observes console completion
within45seconds and explicitly terminates UIKit's remaining run loop. It signs only local simulator copies ad hoc and
shuts down the isolated destination afterward. Run it with CoreSimulator/Xcode
service access; sandbox escalation is needed in this agent environment.

Game bundle: `org.opencnc3d.ios.development`. Launch arguments:
`--mobile-fixture --nosound`. Fixture validation remains separate from normal
mission economy/art acceptance and physical-device input testing.

## Agreed accessibility contract

| Identifier | Native element |
| --- | --- |
| `mobilesurface` | Root native controls surface, defining normalized coordinates |
| `mobile.sidebar` | Sidebar container |
| `mobile.minimap` | Minimap |
| `mobile.orders` | Orders control |
| `mobile.build` | Build button |
| `mobile.cancel` | Context cancellation control |
| `mobile.status` | Diagnostic status element |
| `build.NUKE` | Power Plant construction entry |

`mobile.status.accessibilityValue` is JSON. The construction test requires:

```json
{
  "placed_count": 0,
  "construction_state": "ready",
  "placement_x": 0.5,
  "placement_y": 0.5,
  "placing": true,
  "placement_valid": true,
  "choice_selected": false,
  "ghost_committed": false,
  "placement_requests": 0,
  "first_mobile_x": 0.4,
  "first_mobile_y": 0.3,
  "order_requests": 0,
  "stop_requests": 0,
  "last_order_mode": 0
}
```

The fixture supplies a legal placement point normalized to the `mobilesurface`
frame. `construction_state` can be `ready` or `placing` once production is ready.
The native flow taps Build again when production is Ready to enter placement.
The first native coordinate tap must prime placement without increasing
`placed_count`; a separate second tap must increase that count by exactly one.
The test deliberately uses two independent `.tap()` calls, not `.doubleTap()`.
Screenshots are kept in the `.xcresult` bundle at all three milestones.

The six test cases cover overlay accessibility, first/second placement taps,
cancel preserving Ready production, minimap navigation and Orders center
cancellation, touching a real unit followed by a hold-drag Move command, and
clearing construction confirmation through a real field pan and two-finger pinch.
The last case checks that releasing a directional choice only arms a destination;
a separate terrain tap must increment the actual engine-command witness.
The test report requires the expected number of passed XCTest cases, so an empty
or skipped suite cannot be reported as success. Screenshot attachments are
exported beside the result bundle.

Generated project, build products, staged app and timestamped evidence remain
under this directory and are ignored by Git. The generated project contains only
the UI-test runner; its `XCUIApplication` explicitly selects the existing app by
bundle identifier, so no game source target is needed.

## Why XCUITest

The installed `simctl io` exposes screenshot, video, enumerate and poll operations,
and has no touch-injection verb. No idb/idb_companion installation was found.
Apple's public touch automation uses an XCUITest bundle; its coordinate APIs send
real tap/press/drag gestures. CoreSimulator renders without the Simulator host UI.

- [Apple coordinate tap/press/drag API](https://developer.apple.com/documentation/xcuiautomation/xcuicoordinate)
- [Apple normalized coordinate API](https://developer.apple.com/documentation/xcuiautomation/xcuielement/coordinate(withnormalizedoffset:))
- [Meta's simulator input and headless architecture](https://github.com/facebook/idb/blob/main/FBSimulatorControl/README.md)

This harness does not make a claim of tested native input until the runner builds
and actual UI tests pass against the native overlay. Physical-device testing is
still required for touch comfort and performance. The explicit full-Xcode
`devicectl list devices --timeout 10` check on18October2026 reported no devices.
