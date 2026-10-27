#!/bin/sh
# Build the CURRENT authoritative simulation without editing it or Dock assets.
set -eu
IOS_BRAIN_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
IOS_RUNTIME_SOURCE=${OPENCNC3D_RUNTIME_SOURCE:-"$HOME/CNC3D"}
IOS_DEVELOPER_DIR=${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}
export DEVELOPER_DIR="$IOS_DEVELOPER_DIR"
IOS_VARIANT=${1:-simulator}
case "$IOS_VARIANT" in
    device) IOS_SDK=iphoneos ;;
    simulator) IOS_SDK=iphonesimulator ;;
    *) echo "usage: $0 [device|simulator]" >&2; exit 2 ;;
esac
IOS_SDK_PATH=$(xcrun --sdk "$IOS_SDK" --show-sdk-path)
IOS_BUILD_DIR="$IOS_BRAIN_DIR/build-$IOS_VARIANT"
cmake -S "$IOS_RUNTIME_SOURCE/brain/vanilla" -B "$IOS_BUILD_DIR" -G Ninja \
    -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$IOS_SDK_PATH" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_PROJECT_INCLUDE="$IOS_BRAIN_DIR/static-targets.cmake" \
    -DBUILD_REMASTERTD=ON -DBUILD_REMASTERRA=OFF \
    -DBUILD_VANILLATD=OFF -DBUILD_VANILLARA=OFF \
    -DSDL2=OFF -DOPENAL=OFF -DNETWORKING=OFF -DBUILD_TOOLS=OFF \
    -DMAKE_BUNDLE=OFF -DEIGHTPLAYERS=ON
cmake --build "$IOS_BUILD_DIR" --target TiberianDawn opencnc3d-ios-brain-link-check --parallel 8
xcrun --sdk "$IOS_SDK" libtool -static -o "$IOS_BUILD_DIR/libOpenCNC3DBrain.a" \
    "$IOS_BUILD_DIR/tiberiandawn/TiberianDawn.a" \
    "$IOS_BUILD_DIR/common/libcommonr.a" "$IOS_BUILD_DIR/common/libcommon.a"
git -C "$IOS_RUNTIME_SOURCE" rev-parse HEAD > "$IOS_BUILD_DIR/source-commit.txt"
xcrun --sdk "$IOS_SDK" vtool -show-build "$IOS_BUILD_DIR/opencnc3d-ios-brain-link-check"
echo "Static brain: $IOS_BUILD_DIR/libOpenCNC3DBrain.a"
