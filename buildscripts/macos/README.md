# Hyperspace on Apple Silicon Macs (FTL for iPad)

Hyperspace built as `Hyperspace.1.6.21.arm64.dylib` for
[iOS-FTL-for-macOS](https://github.com/The-Dumb-Dino/iOS-FTL-for-macOS), which runs the
arm64 binary of FTL for iPad (1.6.21) as a Mac Catalyst app. It is the iPad build of
the other ports: the same definitions, hooks and iPad code paths (`HS_ANDROID`,
`HS_APPLE_GAME`) and the same entry point, `hs_android_init`
(`src/android/HyperspaceHost.h`). Everything in [../android/README.md](../android/README.md)
applies, except what is about bionic and the APK.

## What's different from the other iPad ports

- `HS_IPAD_MACOS` (a CMake option) builds the iPad port target for macOS; CMake then
  defines `HS_ANDROID`, `HS_APPLE_GAME` and `HS_IPAD_MACOS`. The features of the desktop
  game on macOS (the freeze watchdog, the cursor hiding, a hotkey menu fix) check for
  `__APPLE__` without `HS_ANDROID`, so they stay out.
- No libc++ or libunwind of our own: the game runs under dyld with the system's libc++,
  which has the ABI it was built with, and the system's unwinder reads its unwind info.
  The `arm64-darwin-ftl` triplet and toolchain (`.devcontainer/`) build a plain macOS
  arm64 dylib, which loads into the Catalyst process; vcpkg leaves out OpenGL and
  discord-rpc for it, as for the other iPad ports. SDL3 is vcpkg's macOS build, linked
  into the dylib for Hyperspace's features (audio, controllers); it isn't tested in the
  Catalyst process yet.
- Writing code (`src/zhl/Arm64Detour.cpp`): the game's code is mapped from its signed
  file and `mprotect()` refuses to make it writable, so on macOS protections change with
  `mach_vm_protect()` and `VM_PROT_COPY` (a private copy of the page). A page is never
  writable and executable at once there: code is written while it isn't executable,
  which is safe because the hooks go in before the game's code runs. The free space for
  the trampolines is found with `mach_vm_region()` instead of `/proc/self/maps`.
- Without a host log callback, messages go to stderr.
- The version string says "aarch64 macOS" (the desktop game's macOS build says
  "amd64 macOS").

## Building

On an Apple Silicon Mac, with the Xcode Command Line Tools:

    buildscripts/macos/setup.sh     # once: Homebrew tools, vcpkg, the arm64-darwin-ftl toolchain and triplet
    buildscripts/macos/build.sh Release

The result is `build-macos-release/Hyperspace.1.6.21.arm64.dylib`. iOS-FTL-for-macOS
installs it with `ftl_to_mac.py --hyperspace <path>` (into the app's `Frameworks/`) and
starts it when the applied mods include Hyperspace (`data/hyperspace.xml` in `ftl.dat`).
