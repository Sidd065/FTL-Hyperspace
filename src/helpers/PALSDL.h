#pragma once

// Whether Hyperspace's features may use SDL3 (audio, controllers). Always on PC and the
// iPad ports, except on Android, where the app has to hand SDL its Java side first
// (hs_android_sdl_setup in android/HyperspaceHost.h).
#if defined(__ANDROID__)
    bool PAL_SDLReady();
#else
    inline bool PAL_SDLReady()
    {
        return true;
    }
#endif
