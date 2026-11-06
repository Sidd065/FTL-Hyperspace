#!/bin/bash
# One-time setup on an Apple Silicon Mac: build tools, vcpkg, and the arm64-darwin-ftl
# toolchain and triplet. Like buildscripts/ci/setup-macos.sh, for the iPad game's target.
set -e
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)

# cmake, ninja, swig and lua with lpeg and lfs (Homebrew), and vcpkg
"$REPO_ROOT/buildscripts/ci/setup-macos.sh"

cp "$REPO_ROOT/.devcontainer/toolchains/arm64-darwin-ftl.cmake" "$REPO_ROOT/vcpkg/scripts/toolchains/"
cp "$REPO_ROOT/.devcontainer/triplets/arm64-darwin-ftl.cmake" "$REPO_ROOT/vcpkg/triplets/"
