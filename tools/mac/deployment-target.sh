#!/bin/sh
# THE OLDEST macOS THIS PROJECT'S BINARIES RUN ON. Sourced, not executed:
#
#     . tools/mac/deployment-target.sh      (from app/build.sh, game/build.sh, ...)
#
# WHY THIS EXISTS (measured, true of every macOS build ever cut).
#
# No build script here ever set a deployment target, so clang stamped LC_BUILD_VERSION
# with whatever SDK it found on the machine that compiled. On a current macOS SDK that is
#
#     minos 26.0    sdk 26.2
#
# on app/cnc3d, game/cnc_eyes AND the brain, and dyld REFUSES to load a Mach-O whose
# minos is newer than the running system. So every Mac below macOS 26 rejected the
# binaries before main() ran -- which is most Macs, macOS 26 having shipped in late 2025.
# That sat directly on top of the SDL bundling work: bundling SDL makes a release
# playable on a Mac that is not this one, and a 26.0 floor made sure it was not.
#
# WHY 14.0 AND NOT LOWER. The Homebrew SDL libraries we bundle beside the binaries
# (tools/bundle-sdl.sh) themselves declare minos 14.0:
#
#     otool -l playable/libSDL2-2.0.0.dylib | grep -A3 LC_BUILD_VERSION   ->  minos 14.0
#     otool -l playable/libSDL3.dylib       | grep -A3 LC_BUILD_VERSION   ->  minos 14.0
#
# A binary whose floor is below its own dependencies' floor has a floor on paper only:
# dyld would accept our Mach-O on macOS 12 and then refuse the SDL beside it. 14.0 is
# therefore the lowest number reachable without ALSO re-sourcing or rebuilding SDL, which
# changes what ships and is a project decision rather than a flag.
#
# IT IS ALL THREE BINARIES OR NONE. cnc3d, cnc_eyes and TiberianDawn.dylib all load in
# one process; two of them fixed and the third left at 26.0 still refuses to launch and
# merely looks fixed. game/make-build.sh asserts the floor over the whole play folder
# after it is assembled, so this cannot drift back one binary at a time.
#
# MACOSX_DEPLOYMENT_TARGET is read by clang and by cc directly. CMake reads
# CMAKE_OSX_DEPLOYMENT_TARGET instead, and initialises it from this same variable when
# the cache does not already hold one, which is why tools/mac/build-brain-mac.sh passes
# it explicitly rather than relying on inheritance through an existing build directory.
CNC3D_MACOS_MIN=14.0
export CNC3D_MACOS_MIN
MACOSX_DEPLOYMENT_TARGET="$CNC3D_MACOS_MIN"
export MACOSX_DEPLOYMENT_TARGET
