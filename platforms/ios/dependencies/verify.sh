#!/bin/bash
# Link only: does not launch a game/app, sign, install, or touch a device.
set -euo pipefail
deps_dir="$(cd "$(dirname "$0")" && pwd)"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
target_sdks=("$@")
[[ ${#target_sdks[@]} -gt 0 ]] || target_sdks=(iphoneos iphonesimulator)
for target_sdk in "${target_sdks[@]}"; do
    prefix="$deps_dir/install/$target_sdk-arm64"
    sdk_path="$(xcrun --sdk "$target_sdk" --show-sdk-path)"
    target=arm64-apple-ios17.0
    [[ "$target_sdk" != iphonesimulator ]] || target=arm64-apple-ios17.0-simulator
    xcrun --sdk "$target_sdk" clang -target "$target" -isysroot "$sdk_path" \
      -I"$prefix/include/SDL2" -I"$prefix/include/gl4es" "$deps_dir/link-probe.c" \
      "$prefix/lib/libSDL2.a" -Wl,-force_load,"$prefix/lib/libGL4ES.a" \
      -framework CoreVideo -framework CoreAudio -framework AudioToolbox \
      -framework AVFoundation -framework CoreGraphics -framework CoreMotion \
      -framework Foundation -framework OpenGLES -framework QuartzCore \
      -framework UIKit -weak_framework GameController -weak_framework CoreHaptics \
      -lm -lpthread -o "$deps_dir/builds/link-probe-$target_sdk"
    xcrun vtool -show-build "$deps_dir/builds/link-probe-$target_sdk"
done
