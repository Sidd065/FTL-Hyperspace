#!/bin/bash
# Build libHyperspace.so for FTL for iPad running natively on aarch64 Linux (ftl-linux).
#   FTL_LIBCXX=... buildscripts/linux/build.sh [Release|Debug]
set -e
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BUILD_TYPE="${1:-Release}"
BUILD_DIR="$REPO_ROOT/build-linux-$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"
VCPKG_ROOT="$REPO_ROOT/vcpkg"

: "${FTL_LIBCXX:?set FTL_LIBCXX to the libc++ build of the Linux port (see buildscripts/linux/README.md)}"
export FTL_LIBCXX
export VCPKG_FORCE_SYSTEM_BINARIES=1

cd "$REPO_ROOT"
cmake -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET=arm64-linux-ftl \
    -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/toolchains/arm64-linux-ftl.cmake" \
    -DHS_IPAD_LINUX=ON -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -S . -B "$BUILD_DIR" -G Ninja
ninja -C "$BUILD_DIR"
echo "Built: $BUILD_DIR/libHyperspace.so"
