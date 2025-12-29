#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/macos-universal"

CXX="${CXX:-clang++}"
CXXFLAGS="-std=c++17 -O3 -Wall -Wextra"
MIN_VER="${MACOSX_DEPLOYMENT_TARGET:-11.0}"

# macOS does not support fully static binaries; this builds a single-file universal binary.
SOURCES=(
  main.cpp
  effect_generator.cpp
  json_util.cpp
  snowflake_effect.cpp
  laser_effect.cpp
  loopfade_effect.cpp
  wave_effect.cpp
  starfield_effect.cpp
  twinkle_effect.cpp
  fireworks_effect.cpp
)

ARCHS=(x86_64 arm64)

mkdir -p "$BUILD_DIR"

for arch in "${ARCHS[@]}"; do
  obj_dir="$BUILD_DIR/obj-$arch"
  mkdir -p "$obj_dir"

  objs=()
  for src in "${SOURCES[@]}"; do
    obj="$obj_dir/${src%.cpp}.o"
    objs+=("$obj")
    "$CXX" $CXXFLAGS -arch "$arch" -mmacosx-version-min="$MIN_VER" -c "$ROOT_DIR/$src" -o "$obj"
  done

  "$CXX" $CXXFLAGS -arch "$arch" -mmacosx-version-min="$MIN_VER" -o "$BUILD_DIR/effectgenerator-$arch" "${objs[@]}"
done

lipo -create -output "$BUILD_DIR/effectgenerator" \
  "$BUILD_DIR/effectgenerator-x86_64" \
  "$BUILD_DIR/effectgenerator-arm64"

lipo -info "$BUILD_DIR/effectgenerator"

echo "Universal binary created at: $BUILD_DIR/effectgenerator"
