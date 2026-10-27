#!/bin/bash
# Generate a reviewable device project. Xcode owns Apple account sign-in and signing.
set -euo pipefail
ios_dir="$(cd "$(dirname "$0")" && pwd)"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
team="${1:-}"
sdk="$(xcrun --sdk iphoneos --show-sdk-path)"
cmake -S "$ios_dir" -B "$ios_dir/derived/device" -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$sdk" -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -DOPENCNC3D_SOURCE_ROOT="${OPENCNC3D_SOURCE_ROOT:-$HOME/CNC3D}" \
  -DOPENCNC3D_DEVELOPMENT_TEAM="$team"
printf '%s\n' "$ios_dir/derived/device/OpenCNC3DiOS.xcodeproj"
