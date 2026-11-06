#!/bin/bash
# Build Hyperspace.1.6.21.arm64.dylib for FTL for iPad on iOS, for a device or the
# simulator.
#   buildscripts/ios/build.sh [device|simulator] [Release|Debug]
set -e
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
TARGET="${1:-device}"
BUILD_TYPE="${2:-Release}"
case "$TARGET" in
    device) TRIPLET=arm64-ios-ftl; SYSROOT=iphoneos ;;
    simulator) TRIPLET=arm64-iossim-ftl; SYSROOT=iphonesimulator ;;
    *) echo "usage: $0 [device|simulator] [Release|Debug]" >&2; exit 1 ;;
esac
BUILD_DIR="$REPO_ROOT/build-ios-$TARGET-$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"
VCPKG_ROOT="$REPO_ROOT/vcpkg"

cd "$REPO_ROOT"
cmake -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET=$TRIPLET \
    -DVCPKG_HOST_TRIPLET=arm64-osx \
    -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/toolchains/arm64-ios-ftl.cmake" \
    -DCMAKE_OSX_SYSROOT=$SYSROOT \
    -DHS_IPAD_IOS=ON -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -S . -B "$BUILD_DIR" -G Ninja
ninja -C "$BUILD_DIR"
echo "Built: $BUILD_DIR/Hyperspace.1.6.21.arm64.dylib"
