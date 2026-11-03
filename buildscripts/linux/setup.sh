#!/bin/bash
# One-time setup on an aarch64 Linux host: vcpkg plus the arm64-linux-ftl toolchain and
# triplet. Also needs: clang and lld, cmake, ninja, swig, lua with lpeg and lfs, and
# the port's libc++ (see README.md).
set -e
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)

if [ ! -d "$REPO_ROOT/vcpkg" ]; then
    git clone https://github.com/microsoft/vcpkg.git "$REPO_ROOT/vcpkg"
fi
# vcpkg has no prebuilt cmake and ninja for aarch64 Linux: it uses the system's
export VCPKG_FORCE_SYSTEM_BINARIES=1
(cd "$REPO_ROOT/vcpkg" && ./bootstrap-vcpkg.sh -disableMetrics)
cp "$REPO_ROOT/.devcontainer/toolchains/arm64-linux-ftl.cmake" "$REPO_ROOT/vcpkg/scripts/toolchains/"
cp "$REPO_ROOT/.devcontainer/triplets/arm64-linux-ftl.cmake" "$REPO_ROOT/vcpkg/triplets/"
