#!/bin/bash
# One-time setup on an Apple Silicon Mac with Xcode: build tools, vcpkg, and the
# arm64-ios-ftl toolchain with its device and simulator triplets.
set -e
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)

# cmake, ninja, swig and lua with lpeg and lfs (Homebrew), and vcpkg
"$REPO_ROOT/buildscripts/ci/setup-macos.sh"

cp "$REPO_ROOT/.devcontainer/toolchains/arm64-ios-ftl.cmake" "$REPO_ROOT/vcpkg/scripts/toolchains/"
cp "$REPO_ROOT/.devcontainer/triplets/arm64-ios-ftl.cmake" "$REPO_ROOT/vcpkg/triplets/"
cp "$REPO_ROOT/.devcontainer/triplets/arm64-iossim-ftl.cmake" "$REPO_ROOT/vcpkg/triplets/"
cp "$REPO_ROOT/.devcontainer/triplets/ios-no-system.h" "$REPO_ROOT/vcpkg/triplets/"
