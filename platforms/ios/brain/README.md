# Static Tiberian Dawn engine for iOS

This is the real current OpenCNC3D simulation, compiled from the authoritative
runtime source. It is separate from the renderer, touch controls and app bundle.
No source in the authoritative checkout or Dock installation is modified.

Build with full Xcode selected (26.2 SDK verified), CMake, Ninja and Python 3:

```sh
OPENCNC3D_RUNTIME_SOURCE="$HOME/CNC3D" sh platforms/ios/brain/build.sh simulator
OPENCNC3D_RUNTIME_SOURCE="$HOME/CNC3D" sh platforms/ios/brain/build.sh device
```

Each build is arm64, targeting iOS 15.0 or newer. The independent output paths
prevent device and simulator objects from being mixed:

- `build-simulator/libOpenCNC3DBrain.a`
- `build-device/libOpenCNC3DBrain.a`

The final archive contains TiberianDawn, commonr and common. Link it into the app
with libc++; SDL and renderer dependencies belong to the application target.
The engine itself is configured headless: remaster TD on, vanilla executables,
Red Alert, SDL, OpenAL, networking and mod tools off. MEGAMAPS and EIGHTPLAYERS
match the current desktop TD configuration.

## Static loader binding

Include `static_brain.h`. Platform-only application compatibility code can map:

```c
#define dlopen  cnc_ios_dlopen
#define dlsym   cnc_ios_dlsym
#define dlclose cnc_ios_dlclose
#define dlerror cnc_ios_dlerror
```

Pass `GameOpts.dylib = "static:TiberianDawn"`. The loader accepts this name and
the usual TiberianDawn library paths. It refuses other modules; Red Alert is not
silently substituted. It performs no dynamic loading. Unknown exports return
null, preserving the desktop loader's optional-feature checks. The resolver's
error string is cleared when read, following the existing caller's expectations.

`opencnc3d_ios_brain_symbol(name)` is the direct alternative to loader hooks.
Cast addresses to the existing correctly typed API function pointers. The
generated resolver includes the actual engine interface in the same translation
unit, so function addresses use their real declarations, including C++ reference
parameters. The generated `ios-static-brain.exports.txt` records all 67 current
TD exports. The renderer's absent RA-only exports remain absent. The optional
`OpenCNC3D_ABI_Facts` export is also absent from the current TD source and keeps
its existing fallback.

## Build-only adaptations

`static-targets.cmake` is injected through `CMAKE_PROJECT_INCLUDE` into the real
`brain/vanilla` CMake root. It changes the TiberianDawn target from shared to
static, replaces the interface compilation unit with the generated wrapper, and
renames the unused legacy `main` in `startup.cpp`. All simulation source stays
unchanged. Keeping the original CMake root preserves its include paths and
generated build/version files.

`sdk_compat.h` addresses an iOS 26 SDK header collision: SDK `endian.h` imports
MacTypes' legacy global `Rect`, which conflicts with the engine's `Rect` class.
The forced include renames only that SDK type while loading the system header,
then removes the macro before engine headers. It changes no game geometry or
byte-order behavior.

## Verification

18 October 2026: real brain compiled and fully linked for both arm64 iOS device
and arm64 iOS simulator, SDK 26.2, minimum iOS 15.0. The bare unsigned
`opencnc3d-ios-brain-link-check` executable references the export resolver and
therefore links the actual interface and its simulation dependencies. It checks
the version and required exports if run. This is link evidence, not a signed
device app or proof that a mission has run. Device signing and application/runtime
validation are separate work.

The script prints `LC_BUILD_VERSION` from the resulting executable and records
the authoritative source commit in each build directory. See `verification.json`
for the verified archive hashes and platform metadata. Existing source warnings
remain visible in the build logs; they are not treated as runtime validation.
