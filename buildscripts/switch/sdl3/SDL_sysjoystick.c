/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2018 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

/*
  Altered for Hyperspace in ftl.nro (FTL-Hyperspace-Dino, buildscripts/switch/): replaces
  src/joystick/switch/SDL_sysjoystick.c of devkitPro's SDL 3.4 for the Switch. SDL runs
  inside an app that already reads the controllers, so this driver
  - leaves the app's controller setup alone: no padConfigureInput, no hold type, no
    controller applet; it shows the controllers the app supports (ftl.nro: player 1
    and handheld),
  - lists only connected controllers and reports them coming and going,
  - rumbles with both of HD rumble's bands, as SDL's low and high frequency motors,
  - reports the gyro and accelerometer as SDL gamepad sensors.
  Button layout and gamepad mapping are devkitPro's.
*/
#include "SDL_internal.h"

#if SDL_JOYSTICK_SWITCH

#include "../SDL_sysjoystick.h"

#include <switch.h>

// Handheld, then players 1 to 8
#define SLOT_COUNT 9

static const HidNpadIdType slot_ids[SLOT_COUNT] = {
    HidNpadIdType_Handheld, HidNpadIdType_No1, HidNpadIdType_No2, HidNpadIdType_No3, HidNpadIdType_No4,
    HidNpadIdType_No5, HidNpadIdType_No6, HidNpadIdType_No7, HidNpadIdType_No8
};

typedef struct SWITCHSlot
{
    SDL_JoystickID instance_id; // 0: nothing connected
    u32 style;                  // the style it was added with; a change re-adds it
    PadState pad;
    const HidNpadButton *pad_mapping;
    bool single_joycon;
    HidVibrationDeviceHandle vibration[2];
    int vibration_count;
    HidSixAxisSensorHandle sixaxis;
    bool sixaxis_ok;
    bool sensors_on;
    u64 sixaxis_sample;
} SWITCHSlot;

static SWITCHSlot slots[SLOT_COUNT];

static const HidNpadButton pad_mapping_default[] = {
    HidNpadButton_A, HidNpadButton_B, HidNpadButton_X, HidNpadButton_Y,
    HidNpadButton_StickL, HidNpadButton_StickR,
    HidNpadButton_L, HidNpadButton_R,
    HidNpadButton_ZL, HidNpadButton_ZR,
    HidNpadButton_Plus, HidNpadButton_Minus,
    HidNpadButton_Left, HidNpadButton_Up, HidNpadButton_Right, HidNpadButton_Down,
    HidNpadButton_StickLLeft, HidNpadButton_StickLUp, HidNpadButton_StickLRight, HidNpadButton_StickLDown,
    HidNpadButton_StickRLeft, HidNpadButton_StickRUp, HidNpadButton_StickRRight, HidNpadButton_StickRDown,
    HidNpadButton_LeftSL, HidNpadButton_LeftSR, HidNpadButton_RightSL, HidNpadButton_RightSR
};

// left single joycon mapping (start = left stick, select = minus)
static const HidNpadButton pad_mapping_left_joy[] = {
    HidNpadButton_Down, HidNpadButton_Left, HidNpadButton_Right, HidNpadButton_Up,
    BIT(31), BIT(31),
    BIT(31), BIT(31),
    HidNpadButton_LeftSL, HidNpadButton_LeftSR,
    HidNpadButton_StickL, HidNpadButton_Minus,
    HidNpadButton_StickLUp, HidNpadButton_StickLRight, HidNpadButton_StickLDown, HidNpadButton_StickLLeft,
    BIT(31), BIT(31), BIT(31), BIT(31),
    BIT(31), BIT(31), BIT(31), BIT(31),
    BIT(31), BIT(31), BIT(31), BIT(31)
};

// right single joycon mapping (start = right stick, select = plus)
static const HidNpadButton pad_mapping_right_joy[] = {
    HidNpadButton_X, HidNpadButton_A, HidNpadButton_Y, HidNpadButton_B,
    BIT(31), BIT(31),
    BIT(31), BIT(31),
    HidNpadButton_RightSL, HidNpadButton_RightSR,
    HidNpadButton_StickR, HidNpadButton_Plus,
    HidNpadButton_StickRDown, HidNpadButton_StickRLeft, HidNpadButton_StickRUp, HidNpadButton_StickRRight,
    BIT(31), BIT(31), BIT(31), BIT(31),
    BIT(31), BIT(31), BIT(31), BIT(31),
    BIT(31), BIT(31), BIT(31), BIT(31)
};

// The one style a controller is used with, from its style set
static u32 SWITCH_PickStyle(u32 styles)
{
    static const u32 order[] = {
        HidNpadStyleTag_NpadFullKey, HidNpadStyleTag_NpadHandheld, HidNpadStyleTag_NpadJoyDual,
        HidNpadStyleTag_NpadJoyLeft, HidNpadStyleTag_NpadJoyRight
    };
    for (size_t i = 0; i < SDL_arraysize(order); i++) {
        if (styles & order[i]) {
            return order[i];
        }
    }
    return 0;
}

static const char *SWITCH_StyleName(u32 style)
{
    switch (style) {
    case HidNpadStyleTag_NpadFullKey:
        return "Nintendo Switch Pro Controller";
    case HidNpadStyleTag_NpadHandheld:
        return "Nintendo Switch Handheld";
    case HidNpadStyleTag_NpadJoyDual:
        return "Nintendo Switch Joy-Con (L/R)";
    case HidNpadStyleTag_NpadJoyLeft:
        return "Nintendo Switch Joy-Con (L)";
    case HidNpadStyleTag_NpadJoyRight:
        return "Nintendo Switch Joy-Con (R)";
    default:
        return "Nintendo Switch Controller";
    }
}

static void SWITCH_SlotConnect(int i, u32 style)
{
    SWITCHSlot *s = &slots[i];
    HidNpadIdType id = slot_ids[i];
    HidSixAxisSensorHandle sixaxis[2];
    int sixaxis_count = style == HidNpadStyleTag_NpadJoyDual ? 2 : 1;

    SDL_zerop(s);
    s->style = style;
    padInitialize(&s->pad, id);
    padUpdate(&s->pad);
    s->single_joycon = style == HidNpadStyleTag_NpadJoyLeft || style == HidNpadStyleTag_NpadJoyRight;
    s->pad_mapping = style == HidNpadStyleTag_NpadJoyLeft    ? pad_mapping_left_joy
                     : style == HidNpadStyleTag_NpadJoyRight ? pad_mapping_right_joy
                                                             : pad_mapping_default;
    s->vibration_count = s->single_joycon ? 1 : 2;
    if (R_FAILED(hidInitializeVibrationDevices(s->vibration, s->vibration_count, id, (HidNpadStyleTag)style))) {
        s->vibration_count = 0;
    }
    // Joy-Cons held together: the right one's sensor, which the stick-less side holds still
    s->sixaxis_ok = R_SUCCEEDED(hidGetSixAxisSensorHandles(sixaxis, sixaxis_count, id, (HidNpadStyleTag)style));
    s->sixaxis = sixaxis[sixaxis_count - 1];
    s->instance_id = SDL_GetNextObjectID();
    SDL_PrivateJoystickAdded(s->instance_id);
}

static void SWITCH_SlotDisconnect(int i)
{
    SWITCHSlot *s = &slots[i];
    if (!s->instance_id) {
        return;
    }
    if (s->sensors_on) {
        hidStopSixAxisSensor(s->sixaxis);
    }
    SDL_JoystickID instance_id = s->instance_id;
    s->instance_id = 0;
    SDL_PrivateJoystickRemoved(instance_id);
}

static void SWITCH_JoystickDetect(void)
{
    for (int i = 0; i < SLOT_COUNT; i++) {
        u32 style = SWITCH_PickStyle(hidGetNpadStyleSet(slot_ids[i]));
        if (slots[i].instance_id && style != slots[i].style) {
            SWITCH_SlotDisconnect(i);
        }
        if (!slots[i].instance_id && style) {
            SWITCH_SlotConnect(i, style);
        }
    }
}

static bool SWITCH_JoystickInit(void)
{
    SWITCH_JoystickDetect();
    return true;
}

// device_index counts the connected slots
static int SWITCH_SlotOfDevice(int device_index)
{
    for (int i = 0; i < SLOT_COUNT; i++) {
        if (slots[i].instance_id && device_index-- == 0) {
            return i;
        }
    }
    return -1;
}

static int SWITCH_SlotOfJoystick(SDL_Joystick *joystick)
{
    for (int i = 0; i < SLOT_COUNT; i++) {
        if (slots[i].instance_id && slots[i].instance_id == joystick->instance_id) {
            return i;
        }
    }
    return -1;
}

static int SWITCH_JoystickGetCount(void)
{
    int count = 0;
    for (int i = 0; i < SLOT_COUNT; i++) {
        count += slots[i].instance_id != 0;
    }
    return count;
}

static SDL_JoystickID SWITCH_JoystickGetDeviceInstanceID(int device_index)
{
    int i = SWITCH_SlotOfDevice(device_index);
    return i < 0 ? 0 : slots[i].instance_id;
}

static bool SWITCH_JoystickIsDevicePresent(Uint16 vendor_id, Uint16 product_id, Uint16 version, const char *name)
{
    // We don't override any other drivers
    return false;
}

static const char *SWITCH_JoystickGetDeviceName(int device_index)
{
    int i = SWITCH_SlotOfDevice(device_index);
    return SWITCH_StyleName(i < 0 ? 0 : slots[i].style);
}

static const char *SWITCH_JoystickGetDevicePath(int index)
{
    return NULL;
}

static int SWITCH_JoystickGetDeviceSteamVirtualGamepadSlot(int device_index)
{
    return -1;
}

// Players 1 to 8 are SDL's players 0 to 7; handheld is player 1's
static int SWITCH_JoystickGetDevicePlayerIndex(int device_index)
{
    int i = SWITCH_SlotOfDevice(device_index);
    return i <= 0 ? 0 : i - 1;
}

static void SWITCH_JoystickSetDevicePlayerIndex(int device_index, int player_index)
{
}

static SDL_GUID SWITCH_JoystickGetDeviceGUID(int device_index)
{
    return SDL_CreateJoystickGUIDForName(SWITCH_JoystickGetDeviceName(device_index));
}

static bool SWITCH_JoystickOpen(SDL_Joystick *joystick, int device_index)
{
    int i = SWITCH_SlotOfDevice(device_index);
    if (i < 0) {
        return SDL_SetError("No such controller");
    }
    SWITCHSlot *s = &slots[i];
    joystick->nbuttons = SDL_arraysize(pad_mapping_default);
    joystick->naxes = 4;
    joystick->nhats = 0;
    joystick->instance_id = s->instance_id;

    SDL_SetBooleanProperty(SDL_GetJoystickProperties(joystick), SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN,
                           s->vibration_count > 0);
    if (s->sixaxis_ok) {
        SDL_PrivateJoystickAddSensor(joystick, SDL_SENSOR_GYRO, 200.0f);
        SDL_PrivateJoystickAddSensor(joystick, SDL_SENSOR_ACCEL, 200.0f);
    }
    return true;
}

static bool SWITCH_JoystickRumble(SDL_Joystick *joystick, Uint16 low_frequency_rumble, Uint16 high_frequency_rumble)
{
    int i = SWITCH_SlotOfJoystick(joystick);
    if (i < 0 || !slots[i].vibration_count) {
        return SDL_Unsupported();
    }
    // Each actuator has a low and a high band; both actuators play the same
    HidVibrationValue v = {
        .amp_low = low_frequency_rumble / 65535.0f, .freq_low = 160.0f,
        .amp_high = high_frequency_rumble / 65535.0f, .freq_high = 320.0f
    };
    HidVibrationValue values[2] = { v, v };
    if (R_FAILED(hidSendVibrationValues(slots[i].vibration, values, slots[i].vibration_count))) {
        return SDL_SetError("hidSendVibrationValues failed");
    }
    return true;
}

static bool SWITCH_JoystickRumbleTriggers(SDL_Joystick *joystick, Uint16 left, Uint16 right)
{
    return SDL_Unsupported();
}

static bool SWITCH_JoystickSetLED(SDL_Joystick *joystick, Uint8 red, Uint8 green, Uint8 blue)
{
    return SDL_Unsupported();
}

static bool SWITCH_JoystickSendEffect(SDL_Joystick *joystick, const void *data, int size)
{
    return SDL_Unsupported();
}

static bool SWITCH_JoystickSetSensorsEnabled(SDL_Joystick *joystick, bool enabled)
{
    int i = SWITCH_SlotOfJoystick(joystick);
    if (i < 0 || !slots[i].sixaxis_ok) {
        return SDL_Unsupported();
    }
    SWITCHSlot *s = &slots[i];
    if (enabled != s->sensors_on) {
        Result rc = enabled ? hidStartSixAxisSensor(s->sixaxis) : hidStopSixAxisSensor(s->sixaxis);
        if (R_FAILED(rc)) {
            return SDL_SetError("Couldn't %s the motion sensor", enabled ? "start" : "stop");
        }
        s->sensors_on = enabled;
    }
    return true;
}

static void SWITCH_UpdateSensors(SDL_Joystick *joystick, SWITCHSlot *s, Uint64 timestamp)
{
    HidSixAxisSensorState st;
    if (!hidGetSixAxisSensorStates(s->sixaxis, &st, 1) || st.sampling_number == s->sixaxis_sample) {
        return;
    }
    s->sixaxis_sample = st.sampling_number;
    // HID: x right, y away from the player, z up; angular velocity in turns per second,
    // acceleration in g. SDL: x right, y up, z towards the player; rad/s and m/s².
    const float turn = 2.0f * SDL_PI_F;
    float gyro[3] = { st.angular_velocity.x * turn, st.angular_velocity.z * turn, -st.angular_velocity.y * turn };
    float accel[3] = { st.acceleration.x * SDL_STANDARD_GRAVITY, st.acceleration.z * SDL_STANDARD_GRAVITY,
                       -st.acceleration.y * SDL_STANDARD_GRAVITY };
    SDL_SendJoystickSensor(timestamp, joystick, SDL_SENSOR_GYRO, timestamp, gyro, 3);
    SDL_SendJoystickSensor(timestamp, joystick, SDL_SENSOR_ACCEL, timestamp, accel, 3);
}

static void SWITCH_JoystickUpdate(SDL_Joystick *joystick)
{
    Uint64 timestamp = SDL_GetTicksNS();
    int i = SWITCH_SlotOfJoystick(joystick);
    if (i < 0) {
        return;
    }
    SWITCHSlot *s = &slots[i];

    padUpdate(&s->pad);
    if (!padIsConnected(&s->pad)) {
        return;
    }

    // single Joy-Cons: the stick is read as buttons (the mapping above)
    if (!s->single_joycon) {
        HidAnalogStickState l = padGetStickPos(&s->pad, 0), r = padGetStickPos(&s->pad, 1);
        SDL_SendJoystickAxis(timestamp, joystick, 0, (Sint16)SDL_clamp(l.x, -32768, 32767));
        SDL_SendJoystickAxis(timestamp, joystick, 1, (Sint16)SDL_clamp(-l.y, -32768, 32767));
        SDL_SendJoystickAxis(timestamp, joystick, 2, (Sint16)SDL_clamp(r.x, -32768, 32767));
        SDL_SendJoystickAxis(timestamp, joystick, 3, (Sint16)SDL_clamp(-r.y, -32768, 32767));
    }

    u64 buttons = padGetButtons(&s->pad);
    for (int b = 0; b < joystick->nbuttons; b++) {
        SDL_SendJoystickButton(timestamp, joystick, b, (buttons & s->pad_mapping[b]) != 0);
    }

    if (s->sensors_on) {
        SWITCH_UpdateSensors(joystick, s, timestamp);
    }
}

static void SWITCH_JoystickClose(SDL_Joystick *joystick)
{
    SWITCH_JoystickSetSensorsEnabled(joystick, false);
}

static void SWITCH_JoystickQuit(void)
{
    for (int i = 0; i < SLOT_COUNT; ++i) {
        SWITCH_SlotDisconnect(i);
    }
}

static bool SWITCH_JoystickGetGamepadMapping(int device_index, SDL_GamepadMapping *out)
{
    // clang-format off
    *out = (SDL_GamepadMapping){
        /*
         * This is as they are when swapped, i.e. what everything else expects.
         * This also matches the behaviour of the old SDL2 port.
         */
        .a             = {.kind = EMappingKind_Button, .target =   1},
        .b             = {.kind = EMappingKind_Button, .target =   0},
        .x             = {.kind = EMappingKind_Button, .target =   3},
        .y             = {.kind = EMappingKind_Button, .target =   2},
        .back          = {.kind = EMappingKind_Button, .target =  11},
        .guide         = {.kind = EMappingKind_None,   .target = 255},
        .start         = {.kind = EMappingKind_Button, .target =  10},
        .leftstick     = {.kind = EMappingKind_Button, .target =   4},
        .rightstick    = {.kind = EMappingKind_Button, .target =   5},
        .leftshoulder  = {.kind = EMappingKind_Button, .target =   6},
        .rightshoulder = {.kind = EMappingKind_Button, .target =   7},
        .dpup          = {.kind = EMappingKind_Button, .target =  13},
        .dpdown        = {.kind = EMappingKind_Button, .target =  15},
        .dpleft        = {.kind = EMappingKind_Button, .target =  12},
        .dpright       = {.kind = EMappingKind_Button, .target =  14},
        .misc1         = {.kind = EMappingKind_None,   .target = 255},
        .misc2         = {.kind = EMappingKind_None,   .target = 255},
        .misc3         = {.kind = EMappingKind_None,   .target = 255},
        .misc4         = {.kind = EMappingKind_None,   .target = 255},
        .misc5         = {.kind = EMappingKind_None,   .target = 255},
        .misc6         = {.kind = EMappingKind_None,   .target = 255},
        .right_paddle1 = {.kind = EMappingKind_None,   .target = 255},
        .left_paddle1  = {.kind = EMappingKind_None,   .target = 255},
        .right_paddle2 = {.kind = EMappingKind_None,   .target = 255},
        .left_paddle2  = {.kind = EMappingKind_None,   .target = 255},
        .leftx         = {.kind = EMappingKind_Axis,   .target =   0},
        .lefty         = {.kind = EMappingKind_Axis,   .target =   1},
        .rightx        = {.kind = EMappingKind_Axis,   .target =   2},
        .righty        = {.kind = EMappingKind_Axis,   .target =   3},
        .lefttrigger   = {.kind = EMappingKind_Button, .target =   8},
        .righttrigger  = {.kind = EMappingKind_Button, .target =   9},
        .touchpad      = {.kind = EMappingKind_None,   .target = 255},
    };
    // clang-format on
    return true;
}

SDL_JoystickDriver SDL_SWITCH_JoystickDriver = {
    .Init = SWITCH_JoystickInit,
    .GetCount = SWITCH_JoystickGetCount,
    .Detect = SWITCH_JoystickDetect,
    .IsDevicePresent = SWITCH_JoystickIsDevicePresent,
    .GetDeviceName = SWITCH_JoystickGetDeviceName,
    .GetDevicePath = SWITCH_JoystickGetDevicePath,
    .GetDeviceSteamVirtualGamepadSlot = SWITCH_JoystickGetDeviceSteamVirtualGamepadSlot,
    .GetDevicePlayerIndex = SWITCH_JoystickGetDevicePlayerIndex,
    .SetDevicePlayerIndex = SWITCH_JoystickSetDevicePlayerIndex,
    .GetDeviceGUID = SWITCH_JoystickGetDeviceGUID,
    .GetDeviceInstanceID = SWITCH_JoystickGetDeviceInstanceID,

    .Open = SWITCH_JoystickOpen,

    .Rumble = SWITCH_JoystickRumble,
    .RumbleTriggers = SWITCH_JoystickRumbleTriggers,

    .SetLED = SWITCH_JoystickSetLED,
    .SendEffect = SWITCH_JoystickSendEffect,
    .SetSensorsEnabled = SWITCH_JoystickSetSensorsEnabled,

    .Update = SWITCH_JoystickUpdate,
    .Close = SWITCH_JoystickClose,
    .Quit = SWITCH_JoystickQuit,

    .GetGamepadMapping = SWITCH_JoystickGetGamepadMapping,
};

#endif /* SDL_JOYSTICK_SWITCH */
