# Hyperspace on aarch64 Linux (FTL for iPad)

Hyperspace built as `libHyperspace.so` for ftl-linux, which runs the same arm64 binary
of FTL for iPad (1.6.21) natively on aarch64 Linux, with the loader and frameworks of
[ftl-android](https://github.com/Sidd065/ftl-android). It's the Android build with the
host's clang and glibc instead of the NDK: the same definitions, hooks and iPad code
paths (`HS_ANDROID`, `HS_APPLE_GAME`), and the same entry point, `hs_android_init`
(`src/android/HyperspaceHost.h`). Everything in
[../android/README.md](../android/README.md) applies, except what is about bionic and
the APK.

## What's different from Android

- `HS_IPAD_LINUX` (a CMake option) builds the iPad port target without the NDK; CMake
  then defines `HS_ANDROID`, `HS_APPLE_GAME` and `HS_IPAD_LINUX` itself. Code that is
  about the iPad game checks `HS_ANDROID`; only what needs bionic (`android/log.h`,
  system properties, the native bridge check for x86 devices) checks `__ANDROID__`.
- The `arm64-linux-ftl` triplet and toolchain (`.devcontainer/`): clang, lld and glibc,
  linked for any page size (4K, 16K and 64K kernels), against the port's libc++ like on
  Android. vcpkg leaves out OpenGL and discord-rpc for it, as for Android.
- SDL3 is the system's (`libSDL3.so.0`), not vcpkg's: ftl-linux runs on it too, so the
  game and Hyperspace share one SDL, already started by the time Hyperspace's features
  use it. Building needs its development files (`SDL3-devel`, `libsdl3-dev`).
- Without a host log callback, messages go to stderr instead of logcat.
- The version string says "aarch64 Linux".

## Building

On an aarch64 Linux host, with clang and lld, cmake, ninja, swig 4.0, SDL3's development
files, and Lua 5.3 with lpeg and lfs, plus the port's libc++ for Linux (`libcxx/build.sh linux` in the port
builds LLVM 19's libc++ with Apple's arm64 ABI settings; that is `FTL_LIBCXX` below):

    buildscripts/linux/setup.sh     # once: vcpkg, plus the Linux toolchain and triplet
    FTL_LIBCXX=/path/to/libcxx/out/linux buildscripts/linux/build.sh Release

The result is `build-linux-release/libHyperspace.so`. ftl-linux looks for it as
`Hyperspace.1.6.21.arm64.so` in the game folder, next to `ftl-linux` or in
`libexec/ftl-linux/`, and loads it the way the Android app loads its copy.

vcpkg has no prebuilt cmake and ninja for aarch64 Linux, so the scripts set
`VCPKG_FORCE_SYSTEM_BINARIES=1` and use the system's.
