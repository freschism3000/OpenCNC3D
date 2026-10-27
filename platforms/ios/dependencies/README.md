# iOS dependencies

Builds use the official SDL2 release archive and an exact upstream gl4es commit.
`dependencies.lock.json` records upstream URLs, versions, commits and SHA-256
archive hashes. Nothing is installed globally or copied into the Dock app.

```sh
./platforms/ios/dependencies/build.sh
./platforms/ios/dependencies/verify.sh
```

Run `build.sh iphoneos` or `build.sh iphonesimulator` to build only one SDK.
The default developer directory is `/Applications/Xcode.app/Contents/Developer`;
override `DEVELOPER_DIR` if needed. `IOS_DEPLOYMENT_TARGET` defaults to 17.0.
Both architectures are arm64; simulator and device archives are **separate**.
The verify script currently checks the default 17.0 minimum.

Generated directories `cache/`, `sources/`, `builds/`, and `install/` are ignored
by Git. Network access is needed only for a missing source archive. Cached
archives are SHA-256 checked on every invocation. Remove the applicable source
directory to re-extract pristine upstream source before rebuilding if desired.

## Outputs

Each SDK prefix `install/{iphoneos,iphonesimulator}-arm64/` contains:

| File/directory | Use |
| --- | --- |
| `lib/libSDL2.a` | Static SDL2 runtime |
| `lib/libSDL2main.a` | UIKit app entry point calling the game's `SDL_main` |
| `lib/libGL4ES.a` | Static desktop GL 2.1 to OpenGL ES compatibility library |
| `include/SDL2` | SDL2 include directory |
| `include/gl4es` | Includes `GL/gl.h` and `gl4esinit.h` |
| `lib/cmake/SDL2` | Upstream SDL imported targets and transitive dependencies |

SDL uses `SDL_SHARED=OFF`, `SDL_STATIC=ON`, `SDL_OPENGLES=ON`. HIDAPI,
Metal, Vulkan, desktop OpenGL and tests are disabled. UIKit, audio, MFi controllers,
touch, timers and filesystem support remain enabled. Link required frameworks:
CoreVideo, CoreAudio, AudioToolbox, AVFoundation, CoreGraphics, CoreMotion,
Foundation, OpenGLES, QuartzCore and UIKit; weak GameController and CoreHaptics;
system `m` and `pthread` libraries. The upstream CMake export spells CoreMotion
as `Coremotion`, which works on the present case-insensitive host; manually
specifying the proper `CoreMotion` spelling avoids this portability issue.

gl4es flags are `NOX11=ON`, `NOEGL=ON`, `STATICLIB=ON`, `NO_LOADER=ON`,
`NO_INIT_CONSTRUCTOR=ON`, `DEFAULT_ES=2`, `GBM=OFF`, and `USE_CCACHE=OFF`.
Its local `patch_gl4es.py` makes only these changes, each checked against exact
upstream text:

1. Keep binary/archive output under each SDK's build tree, preventing device and
   simulator libraries from overwriting each other.
2. Replace two unsupported GCC function aliases with equivalent forwarding
   functions (`gl4es_glEnableClientStatei` and `gl4es_glDisableClientStatei`).

The rest of gl4es is unmodified. The Apple build already avoids exported native
`gl*` aliases, and its `GL/gl.h` maps game calls to `gl4es_gl*`. This distinction
lets SDL's native OpenGL ES calls keep binding to Apple's framework.

## Required context glue

Create and make current an SDL OpenGL ES 2 context before gl4es initialization.
Then provide `set_getprocaddress(native_proc)`,
`set_getmainfbsize(drawable_size_callback)`, and call `initialize_gl4es()`.
Use `gl4es_GetProcAddress` for the **game's desktop GL** dynamic symbol lookup.
The native callback must resolve **Apple OpenGL ES** functions from an explicit
OpenGLES framework handle, rather than looking up gl4es entry points.

SDL's iOS drawable framebuffer is not native FBO 0. `SDL_GetWindowWMInfo`
provides `info.uikit.framebuffer` and `info.uikit.colorbuffer`. gl4es has a size
callback but no public native-default-FBO setter. A native resolver wrapper for
`glBindFramebuffer`/`glBindFramebufferOES` should translate a requested FBO 0
to the current SDL drawable FBO; other FBO names pass through unchanged. Keep
gl4es `LIBGL_FB=0`. Refresh native drawable names after SDL recreation/resize.
Before `SDL_GL_SwapWindow`, bind the native SDL color renderbuffer; SDL explicitly
requires it. This preserves the wrapper's virtual framebuffer-0 semantics while
rendering into the actual UIKit drawable.

## Verification

Both libraries built for arm64 iPhoneOS and arm64 iPhoneSimulator with Xcode
26.2 (17C52), SDK 26.2, minimum iOS 17.0. `lipo -info` confirmed arm64.
`verify.sh` linked each SDK independently, force-loading the complete gl4es archive
and linking SDL2 plus the required Apple frameworks. Both linked successfully
without unresolved symbols. `vtool -show-build` confirmed the correct IOS and
IOSSIMULATOR platforms. This is compile/link evidence, not on-device rendering
or gameplay validation. The probe binaries are never launched.

Build logs: `builds/iphoneos.log`, `builds/iphonesimulator.log`,
`builds/link-probe.log`. gl4es emits upstream macro-redefinition, dangling-else
and qualifier warnings; none are build errors.

SDL2 and gl4es top-level licenses are preserved beside this file. Additional
upstream third-party notices remain in the source archives/source trees and must
be preserved when redistributing their applicable code.

Official upstream instructions:

- https://github.com/libsdl-org/SDL/blob/release-2.32.10/docs/README-ios.md
- https://github.com/ptitSeb/gl4es/blob/a025615bc79ac43ec911dced8fe5b8eb45a0b72d/COMPILE.md
- https://github.com/ptitSeb/gl4es/blob/a025615bc79ac43ec911dced8fe5b8eb45a0b72d/include/gl4esinit.h
