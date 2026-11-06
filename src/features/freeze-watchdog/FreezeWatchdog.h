#pragma once

#if defined(__APPLE__) && !defined(HS_ANDROID)  // the desktop game on macOS, not the iPad game

namespace FreezeWatchdog {
    // Start the watchdog process. Called from launchHyperspace().
    void Start();

    // Send heartbeat signal. Called from a hook that runs every frame.
    void SendHeartbeat();

    // Signal clean shutdown. Called from CApp::OnExit hook.
    void Stop();
}

#endif // __APPLE__
