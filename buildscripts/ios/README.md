# Hyperspace on iOS (FTL for iPad)

Hyperspace built as `Hyperspace.1.6.21.arm64.dylib` for
[FTL-iOS-Patcher](https://github.com/The-Dumb-Dino/FTL-iOS-Patcher), which runs it in the
iPad game on iPhone and iPad. It is the same iPad port as on macOS
([../macos/README.md](../macos/README.md)), built with `HS_IPAD_IOS`, so the version
string says "aarch64 iOS".

iOS doesn't let an app change its own code, so the hooks are written into the game
beforehand: FTL-iOS-Patcher records them by running the macOS build of the same commit,
and at run time `Arm64Detour` only checks that each hook is in place and fills in the
pointers its trampoline jumps through. A change to the hooks therefore needs a new
macOS build and a new recording, from the same commit as the iOS build.

## Building

On a Mac with Xcode:

    buildscripts/ios/setup.sh            # once: vcpkg and the iOS toolchain and triplets
    buildscripts/ios/build.sh device     # build-ios-device-release/
    buildscripts/ios/build.sh simulator  # build-ios-simulator-release/, for testing

## SDL3

vcpkg's iOS build (3.4.4), linked into the dylib for Hyperspace's features (audio,
controllers). `hs_android_init` calls `SDL_SetMainReady()`, since the game's `main()`
isn't SDL's. Tested in the iOS Simulator with the SDL test overlay (`HS_TEST_SDL`):
audio comes up on CoreAudio with one output device, the gamepad subsystem starts, and
the simulator's virtual controller ("Gamepad", no rumble or motion sensors) is noticed
when it connects and its stick read. Not tested yet: a real controller, rumble, motion
sensors, and a device.

The audio session: the game sets `AVAudioSessionCategoryPlayback` with
`MixWithOthers` itself. Opening an SDL audio device sets the session to SDL's own choice,
which with no `SDL_HINT_AUDIO_CATEGORY` is Playback with `MixWithOthers` and
`DuckOthers` (other apps' audio gets quieter while it's open), and SDL deactivates the
session briefly to do so. No value of the hint gives the game's exact setting, and SDL
has no way to leave the session alone. In the test, where the overlay opens its device
in `CApp::OnInit`, the session still had the game's setting afterwards (the game sets
it later in its start-up). A feature that opens SDL audio while the game runs should
set the game's category back afterwards.
