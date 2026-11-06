#include "SdlTest.h"

#ifdef HS_TEST_SDL
/**
 * Reports which SDL3 subsystems come up inside the Hyperspace binary, drawn over the game.
 *
 */

#include "Global.h"
#include "helpers/PALSDL.h"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

static std::string g_report;
static SDL_Gamepad *g_pad = nullptr;
static SDL_AudioStream *g_beep = nullptr;
static std::string g_rumble;

static bool InitSubsystem(const char *name, SDL_InitFlags flag)
{
    bool ok = SDL_InitSubSystem(flag);
    g_report += std::string(name) + (ok ? ": ok" : std::string(": FAILED - ") + SDL_GetError()) + "\n";
    return ok;
}

static void PlayBeep()
{
    std::vector<float> tone(4800);
    for (size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.2f * SDL_sinf(2.0f * SDL_PI_F * 440.0f * i / 48000.0f);

    SDL_PutAudioStreamData(g_beep, tone.data(), (int)(tone.size() * sizeof(float)));
}

// Opens the first gamepad, also one connected later, and notes what it can do
static void OpenPad()
{
    int padCount = 0;
    SDL_JoystickID *padIds = SDL_GetGamepads(&padCount);
    if (padCount > 0) g_pad = SDL_OpenGamepad(padIds[0]);
    SDL_free(padIds);
    if (!g_pad) return;

    SDL_PropertiesID props = SDL_GetGamepadProperties(g_pad);
    bool gyro = SDL_GamepadHasSensor(g_pad, SDL_SENSOR_GYRO);
    bool accel = SDL_GamepadHasSensor(g_pad, SDL_SENSOR_ACCEL);
    if (gyro) SDL_SetGamepadSensorEnabled(g_pad, SDL_SENSOR_GYRO, true);
    std::string found = std::string("Gamepad: ") + SDL_GetGamepadName(g_pad) + "\n"
                      + "Rumble: " + (SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false) ? "yes" : "no")
                      + "  Gyro: " + (gyro ? "yes" : "no") + "  Accelerometer: " + (accel ? "yes" : "no") + "\n";
    if (accel) SDL_SetGamepadSensorEnabled(g_pad, SDL_SENSOR_ACCEL, true);
    g_report += found;
    hs_log_file("SdlTest: " + found);
}

void SdlTest::Init()
{
    if (!PAL_SDLReady())
    {
        g_report = "SDL: not set up by the app\n";
        hs_log_file("SdlTest:\n" + g_report);
        return;
    }
    if (InitSubsystem("Audio", SDL_INIT_AUDIO))
    {
        g_report += std::string("Audio driver: ") + SDL_GetCurrentAudioDriver() + "\n";

        int deviceCount = 0;
        SDL_AudioDeviceID *devices = SDL_GetAudioPlaybackDevices(&deviceCount);
        g_report += "Audio devices: " + std::to_string(deviceCount) + "\n";
        SDL_free(devices);

        SDL_AudioSpec spec = { SDL_AUDIO_F32, 1, 48000 };
        g_beep = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (g_beep) SDL_ResumeAudioStreamDevice(g_beep);
    }

    if (InitSubsystem("Gamepad", SDL_INIT_GAMEPAD))
    {
        OpenPad();
        if (!g_pad) g_report += "Gamepad: none connected\n";
    }

    hs_log_file("SdlTest:\n" + g_report);
    g_report += "Press the X (A) button on the gamepad to hear a beep and feel a rumble\n";
}

void SdlTest::Render()
{
    if (!PAL_SDLReady()) return;

    // FTL owns the event loop, so nothing refreshes gamepad state for us
    SDL_UpdateGamepads();

    if (g_pad && !SDL_GamepadConnected(g_pad))
    {
        SDL_CloseGamepad(g_pad);
        g_pad = nullptr;
        g_report += "Gamepad: disconnected\n";
    }
    if (!g_pad && SDL_WasInit(SDL_INIT_GAMEPAD)) OpenPad();

    std::string live = "Gamepad state: ";
    if (g_pad)
    {
        live += "LX " + std::to_string(SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTX))
              + "  LY " + std::to_string(SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTY));

        float v[3];
        if (SDL_GamepadSensorEnabled(g_pad, SDL_SENSOR_GYRO) && SDL_GetGamepadSensorData(g_pad, SDL_SENSOR_GYRO, v, 3))
            live += "\nGyro (rad/s) " + std::to_string(v[0]) + " " + std::to_string(v[1]) + " " + std::to_string(v[2]);
        // Lying still and flat, the accelerometer shows about 9.8 on the second (up) axis
        if (SDL_GamepadSensorEnabled(g_pad, SDL_SENSOR_ACCEL) && SDL_GetGamepadSensorData(g_pad, SDL_SENSOR_ACCEL, v, 3))
            live += "\nAccelerometer (m/s2) " + std::to_string(v[0]) + " " + std::to_string(v[1]) + " " + std::to_string(v[2]);

        static bool wasDown = false;
        bool isDown = SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_SOUTH);
        if (isDown && !wasDown)
        {
            if (g_beep) PlayBeep();
            g_rumble = SDL_RumbleGamepad(g_pad, 0xFFFF, 0xFFFF, 300) ? "Rumble: sent" : std::string("Rumble: FAILED - ") + SDL_GetError();
            hs_log_file("SdlTest: button pressed, " + g_rumble + "\n");
        }
        wasDown = isDown;
        if (!g_rumble.empty()) live += "\n" + g_rumble;
    }
    else
    {
        live += "no gamepad";
    }

    CSurface::GL_SetColor(GL_Color(0.4f, 1.0f, 0.6f, 1.0f));

    float y = 40.0f;
    size_t start = 0;
    while (start < g_report.size())
    {
        size_t end = g_report.find('\n', start);
        freetype::easy_print(10, 40.0f, y, g_report.substr(start, end - start));
        y += 18.0f;
        start = end + 1;
    }
    y += 18.0f;
    start = 0;
    while (start < live.size())
    {
        size_t end = live.find('\n', start);
        if (end == std::string::npos) end = live.size();
        freetype::easy_print(10, 40.0f, y, live.substr(start, end - start));
        y += 18.0f;
        start = end + 1;
    }

    CSurface::GL_SetColor(GL_Color(1.0f, 1.0f, 1.0f, 1.0f));
}

#endif // HS_TEST_SDL
