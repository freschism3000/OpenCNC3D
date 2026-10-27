#!/bin/bash
set -euo pipefail
ios_dir="$(cd "$(dirname "$0")" && pwd)"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
sdk="${1:-iphonesimulator}"
[[ "$sdk" == iphoneos || "$sdk" == iphonesimulator ]] || { echo 'Expected iphoneos or iphonesimulator' >&2;exit 2; }
runtime_source="${OPENCNC3D_SOURCE_ROOT:-$HOME/CNC3D}"
[[ -f "$runtime_source/game/cnc_eyes.cpp" ]] || { echo 'Missing runtime source' >&2;exit 2; }
if [[ ! -f "$ios_dir/dependencies/install/$sdk-arm64/lib/libGL4ES.a" || ! -f "$ios_dir/dependencies/install/$sdk-arm64/lib/libSDL2.a" ]]; then
  "$ios_dir/dependencies/build.sh" "$sdk"
fi
if [[ "$sdk" == iphoneos ]]; then brain_variant=device;else brain_variant=simulator;fi
OPENCNC3D_RUNTIME_SOURCE="$runtime_source" "$ios_dir/brain/build.sh" "$brain_variant"
sdk_path="$(xcrun --sdk "$sdk" --show-sdk-path)"
cmake -S "$ios_dir" -B "$ios_dir/build/$sdk" -G Ninja \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$sdk_path" -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -DOPENCNC3D_SOURCE_ROOT="${OPENCNC3D_SOURCE_ROOT:-$HOME/CNC3D}"
cmake --build "$ios_dir/build/$sdk" --parallel "${BUILD_JOBS:-6}"
