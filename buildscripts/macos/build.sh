#!/bin/bash
# Build Hyperspace.1.6.21.arm64.dylib for FTL for iPad running as a Mac app on Apple
# Silicon (iOS-FTL-for-macOS).
#   buildscripts/macos/build.sh [Release|Debug]
set -e
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BUILD_TYPE="${1:-Release}"
BUILD_DIR="$REPO_ROOT/build-macos-$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"
VCPKG_ROOT="$REPO_ROOT/vcpkg"

cd "$REPO_ROOT"
cmake -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET=arm64-darwin-ftl \
    -DVCPKG_HOST_TRIPLET=arm64-darwin-ftl \
    -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/toolchains/arm64-darwin-ftl.cmake" \
    -DHS_IPAD_MACOS=ON -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -S . -B "$BUILD_DIR" -G Ninja
ninja -C "$BUILD_DIR"
echo "Built: $BUILD_DIR/Hyperspace.1.6.21.arm64.dylib"
