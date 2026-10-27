#!/bin/bash
# Isolated, pinned iOS dependencies. No global install or native Dock files touched.
set -euo pipefail
deps_dir="$(cd "$(dirname "$0")" && pwd)"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
sdl_version=2.32.10
gl4es_commit=a025615bc79ac43ec911dced8fe5b8eb45a0b72d
sdl_sha=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
gl4es_sha=b4e68ee01c45c114d89d0df290618ea7aa6f4078db11c276a893af39b9ad9b76
deployment_target="${IOS_DEPLOYMENT_TARGET:-17.0}"
jobs="${BUILD_JOBS:-8}"
mkdir -p "$deps_dir"/{cache,sources,builds,install}

fetch() {
  local archive="$1" url="$2" sha="$3"
  if [[ ! -f "$deps_dir/cache/$archive" ]]; then
    curl --fail --location --retry 3 "$url" --output "$deps_dir/cache/$archive.part"
    mv "$deps_dir/cache/$archive.part" "$deps_dir/cache/$archive"
  fi
  printf '%s  %s\n' "$sha" "$deps_dir/cache/$archive" | shasum -a 256 --check --status
}
fetch "SDL2-$sdl_version.tar.gz" "https://github.com/libsdl-org/SDL/releases/download/release-$sdl_version/SDL2-$sdl_version.tar.gz" "$sdl_sha"
fetch "gl4es-$gl4es_commit.tar.gz" "https://codeload.github.com/ptitSeb/gl4es/tar.gz/$gl4es_commit" "$gl4es_sha"
[[ -d "$deps_dir/sources/SDL2-$sdl_version" ]] || tar -xzf "$deps_dir/cache/SDL2-$sdl_version.tar.gz" -C "$deps_dir/sources"
[[ -d "$deps_dir/sources/gl4es-$gl4es_commit" ]] || tar -xzf "$deps_dir/cache/gl4es-$gl4es_commit.tar.gz" -C "$deps_dir/sources"
python3 "$deps_dir/patch_gl4es.py" "$deps_dir/sources/gl4es-$gl4es_commit"

build_sdk() {
  local sdk="$1"
  local sdk_path prefix sdl_build gl_build
  sdk_path="$(xcrun --sdk "$sdk" --show-sdk-path)"
  prefix="$deps_dir/install/$sdk-arm64"
  sdl_build="$deps_dir/builds/$sdk-arm64-sdl2"
  gl_build="$deps_dir/builds/$sdk-arm64-gl4es"
  local common=(-G Ninja -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$sdk_path" -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY)
  cmake -S "$deps_dir/sources/SDL2-$sdl_version" -B "$sdl_build" "${common[@]}" \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF \
    -DSDL_TESTS=OFF -DSDL_INSTALL_TESTS=OFF -DSDL_HIDAPI=OFF -DSDL_METAL=OFF \
    -DSDL_RENDER_METAL=OFF -DSDL_VULKAN=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=ON
  cmake --build "$sdl_build" --parallel "$jobs"
  cmake --install "$sdl_build"
  cmake -S "$deps_dir/sources/gl4es-$gl4es_commit" -B "$gl_build" "${common[@]}" \
    -DNOX11=ON -DNOEGL=ON -DSTATICLIB=ON -DNO_LOADER=ON -DNO_INIT_CONSTRUCTOR=ON \
    -DDEFAULT_ES=2 -DGBM=OFF -DUSE_CCACHE=OFF
  cmake --build "$gl_build" --parallel "$jobs"
  mkdir -p "$prefix/lib" "$prefix/include/gl4es"
  cp "$gl_build/lib/libGL.a" "$prefix/lib/libGL4ES.a"
  cp -R "$deps_dir/sources/gl4es-$gl4es_commit/include/." "$prefix/include/gl4es/"
  xcrun lipo -info "$prefix/lib/libSDL2.a" "$prefix/lib/libGL4ES.a"
}

if [[ $# -eq 0 ]]; then
  build_sdk iphoneos
  build_sdk iphonesimulator
else
  for sdk in "$@"; do
    [[ "$sdk" == iphoneos || "$sdk" == iphonesimulator ]] || { echo "Expected iphoneos or iphonesimulator" >&2; exit 2; }
    build_sdk "$sdk"
  done
fi
