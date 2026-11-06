#!/bin/bash
# Build libHyperspace.a for FTL for iPad running on the Switch (ftl.nro), which links it
# in: Horizon has no dynamic loader. No vcpkg: the few dependencies are built here.
#   DEVKITPRO=... FTL_LIBCXX=... FTL_SWITCH_TOOLCHAIN=... buildscripts/switch/build.sh
# Output: build-switch-release/libHyperspace.a, and in build-switch-deps/lib/ the Lua
# (liblua.a) and SDL3 (libSDL3.a) it was built with, which ftl.nro links too.
set -euo pipefail
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BUILD_DIR="$REPO_ROOT/build-switch-release"
DEPS="$REPO_ROOT/build-switch-deps"
BOOST=1.87.0
LUA=5.3.6
# SDL 3.4 for the Switch: devkitPro's port (branch switch-sdl-3.4), with our controller
# driver (sdl3/SDL_sysjoystick.c). Only audio, controllers and their sensors; no video.
# newlib declares a few functions libnx doesn't have; SDL is told they're missing.
SDL_COMMIT=c329016c0e338ab4397ad930f83330c9fe058348

: "${DEVKITPRO:?set DEVKITPRO to devkitPro (libnx, newlib and the switch-* portlibs)}"
: "${FTL_LIBCXX:?set FTL_LIBCXX to the libc++ build of the Switch port (see buildscripts/switch/README.md)}"
: "${FTL_SWITCH_TOOLCHAIN:?set FTL_SWITCH_TOOLCHAIN to switch/toolchain.cmake of the Switch port}"
export DEVKITPRO FTL_LIBCXX FTL_SWITCH_TOOLCHAIN

# Boost's headers (only header-only parts are used), Lua 5.3 compiled for the Switch,
# minizip's headers (devkitPro's switch-zlib has the library)
mkdir -p "$DEPS/include" "$DEPS/lib" "$DEPS/obj" "$DEPS/src"
fetch() { [ -f "$2" ] || curl -fsSL -o "$2" "$1"; }
if [ ! -d "$DEPS/include/boost" ]; then
    T=boost_${BOOST//./_}
    fetch "https://archives.boost.io/release/$BOOST/source/$T.tar.bz2" "$DEPS/src/$T.tar.bz2"
    tar xf "$DEPS/src/$T.tar.bz2" -C "$DEPS/src" "$T/boost"
    mv "$DEPS/src/$T/boost" "$DEPS/include/boost"
fi
L=$DEPS/src/lua-$LUA/src
if [ ! -d "$L" ]; then
    fetch "https://www.lua.org/ftp/lua-$LUA.tar.gz" "$DEPS/src/lua-$LUA.tar.gz"
    tar xf "$DEPS/src/lua-$LUA.tar.gz" -C "$DEPS/src"
fi
cp "$L"/{lua.h,luaconf.h,lualib.h,lauxlib.h,lua.hpp} "$DEPS/include/"
cp -r "$DEVKITPRO/portlibs/switch/include/minizip" "$DEPS/include/"
NEWLIB=$DEVKITPRO/devkitA64/aarch64-none-elf
CF=(--target=aarch64-none-elf -march=armv8-a+crc+crypto -mtune=cortex-a57 -fPIE -femulated-tls
    -ffunction-sections -fdata-sections -D__SWITCH__ -nostdinc -isystem "$(clang -print-resource-dir)/include"
    -isystem "$DEVKITPRO/libnx/include" -isystem "$NEWLIB/include" -O2 -fsigned-char)
rm -f "$DEPS"/obj/*.o
for f in "$L"/*.c; do
    b=$(basename "$f" .c)
    [ "$b" = lua ] || [ "$b" = luac ] || clang "${CF[@]}" -c "$f" -o "$DEPS/obj/$b.o"
done
rm -f "$DEPS/lib/liblua.a" && llvm-ar rcs "$DEPS/lib/liblua.a" "$DEPS"/obj/*.o

S=$DEPS/src/SDL-$SDL_COMMIT
if [ ! -d "$S" ]; then
    fetch "https://github.com/devkitPro/SDL/archive/$SDL_COMMIT.tar.gz" "$DEPS/src/SDL-$SDL_COMMIT.tar.gz"
    tar xf "$DEPS/src/SDL-$SDL_COMMIT.tar.gz" -C "$DEPS/src"
fi
cp "$SCRIPT_DIR/sdl3/SDL_sysjoystick.c" "$S/src/joystick/switch/SDL_sysjoystick.c"
cmake -DCMAKE_TOOLCHAIN_FILE="$FTL_SWITCH_TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    -DSDL_VIDEO=OFF -DSDL_RENDER=OFF -DSDL_GPU=OFF -DSDL_CAMERA=OFF -DSDL_DIALOG=OFF -DSDL_TRAY=OFF \
    -DSDL_HIDAPI=OFF -DSDL_POWER=OFF -DCMAKE_INSTALL_PREFIX="$DEPS/sdl3" \
    -DHAVE_PTHREAD_SETNAME_NP=OFF -DHAVE_FDATASYNC=OFF -DHAVE_SIGACTION=OFF \
    -S "$S" -B "$DEPS/sdl3-build" -G Ninja >/dev/null
ninja -C "$DEPS/sdl3-build" install >/dev/null
rm -rf "$DEPS/include/SDL3" && cp -r "$DEPS/sdl3/include/SDL3" "$DEPS/include/"
cp "$DEPS/sdl3/lib/libSDL3.a" "$DEPS/lib/libSDL3.a"

cd "$REPO_ROOT"
cmake -DCMAKE_TOOLCHAIN_FILE="$REPO_ROOT/.devcontainer/toolchains/switch-ftl.cmake" \
    -DHS_IPAD_SWITCH=ON -DHS_DEPS="$DEPS" -DCMAKE_BUILD_TYPE=Release \
    -S . -B "$BUILD_DIR" -G Ninja
ninja -C "$BUILD_DIR"
echo "Built: $BUILD_DIR/libHyperspace.a (with $DEPS/lib/liblua.a and $DEPS/lib/libSDL3.a)"
