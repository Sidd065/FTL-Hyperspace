# Hyperspace on the Switch (FTL for iPad)

Hyperspace built as `libHyperspace.a` for ftl.nro, Switch homebrew that runs the arm64
binary of FTL for iPad (1.6.21) on a Switch with Atmosphère, with the loader and
frameworks of [ftl-android](https://github.com/Sidd065/ftl-android). It's the same iPad
port as on Android and Linux (the definitions, hooks and iPad code paths, see
[../android/README.md](../android/README.md)), linked into ftl.nro instead of loaded:
Horizon has no dynamic loader. ftl.nro calls `hs_android_init` as the other ports do.

## What's different on the Switch

- `HS_IPAD_SWITCH` (a CMake option) builds a static library with the Switch port's
  toolchain (host clang and lld, devkitPro's newlib and libnx) and the port's libc++,
  without vcpkg: only Boost's header-only parts are used, Lua 5.3 is compiled here, and
  minizip and zlib are devkitPro's. CMake defines `HS_ANDROID`, `HS_APPLE_GAME` and
  `HS_IPAD_SWITCH`.
- Code memory: Horizon never makes a page writable and executable, and a mapping's
  permissions can't be changed back and forth, so `mmap` and `mprotect` don't apply.
  The host passes two callbacks in `hs_host_info`: `alloc_code` gives the trampoline
  pool (next to the game, within reach of a branch) and `write_code` writes code, game
  or trampolines, through a second, writable view of it and flushes it.
  `Arm64Detour::SetHostCode` takes them; every code write, the no-op definitions
  included, goes through `Arm64Detour::WriteCode`. The game's data stays writable, so
  vtable patches need nothing.
- Errors: `show_error` in `hs_host_info` shows Hyperspace's error messages (a broken
  `hyperspace.xml`, a mod's `Hyperspace.ErrorMessage` from Lua) the way the PC builds
  show a message box; ftl.nro opens the Switch's error applet. Without it they are only
  logged, as before.
- No `dladdr`: the callers logged for a missing function are plain addresses (for
  `llvm-symbolizer` with ftl.elf). `__ftl_find_macho_unwind` is ftl.nro's own.

The three fields are optional and at the end of `hs_host_info`: hosts that don't pass
them (`size` from before them, `HS_HOST_INFO_SIZE_V1`) work as they did, so the Android
app and ftl-linux need no change.

## Building

Needs the Switch port's toolchain set up (its `switch/setup.sh`: devkitPro's libnx,
newlib and the switch-zlib portlib), its libc++ (`libcxx/build.sh switch`), clang and
lld, cmake, ninja, swig 4.0, Lua 5.3 with lpeg and lfs, and curl:

    DEVKITPRO=/path/to/devkitpro FTL_LIBCXX=/path/to/libcxx/out/switch \
    FTL_SWITCH_TOOLCHAIN=/path/to/the/port/switch/toolchain.cmake \
        buildscripts/switch/build.sh

The result is `build-switch-release/libHyperspace.a`, with the Lua it was compiled
against in `build-switch-deps/lib/liblua.a`. ftl.nro links both; most of Hyperspace's
files only register their hooks from static constructors, so it links the library
whole (`--whole-archive`).
