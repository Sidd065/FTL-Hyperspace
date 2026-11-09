#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HS_HOST_VERSION 1

typedef struct hs_host_info
{
    uint32_t version;
    uint32_t size;

    const void *mach_header;
    intptr_t slide;
    size_t image_size;

    const char *log_dir;
    const char *data_dir;

    void (*log)(int level, const char *message);

    int (*find_macho_unwind)(uintptr_t pc, uintptr_t *mh, uintptr_t *text_len, uintptr_t *compact_unwind,
                             size_t *compact_unwind_len, uintptr_t *eh_frame, size_t *eh_frame_len);

    /* Optional: room near the game for the hook trampolines (the Android app) */
    void *hook_area;
    size_t hook_area_size;

    /* Optional: executable memory near the game, and code writes (the Switch) */
    void *(*alloc_code)(size_t size);
    int (*write_code)(void *dst, const void *src, size_t n);

    /* Optional: shows an error and returns once the player closed it */
    void (*show_error)(const char *title, const char *message);

    /* Optional: a prepatched game, for iOS where code can't be changed at runtime
       (see Arm64Detour.cpp). Segments the host added to the game: the trampolines go
       in prepatch_code, the pointers they jump through in prepatch_slots, the hooked
       functions' first instructions in prepatch_originals. Mode 1 records (writes the
       code, on a Mac); mode 2 verifies (the code is in the game's file already). */
    void *prepatch_code;
    size_t prepatch_code_size;
    void **prepatch_slots;
    size_t prepatch_slot_count;
    void *prepatch_originals;
    size_t prepatch_originals_size;
    int prepatch_mode;
} hs_host_info;

enum { HS_PREPATCH_NONE = 0, HS_PREPATCH_RECORD = 1, HS_PREPATCH_VERIFY = 2 };

#define HS_HOST_INFO_SIZE_V1 offsetof(hs_host_info, hook_area)

int hs_android_init(const hs_host_info *info, char *error, size_t error_size);
typedef int (*hs_android_init_fn)(const hs_host_info *info, char *error, size_t error_size);

enum { HS_RIGHT_BUTTON_DOWN = 0, HS_RIGHT_BUTTON_MOVE = 1, HS_RIGHT_BUTTON_UP = 2 };
int hs_android_wants_right_click(void);
int hs_android_wants_right_click_at(int x, int y);
void hs_android_right_button(int action, int x, int y);
typedef int (*hs_android_wants_right_click_fn)(void);
typedef int (*hs_android_wants_right_click_at_fn)(int x, int y);
typedef void (*hs_android_right_button_fn)(int action, int x, int y);

// Android only: SDL3, which Hyperspace's features use for audio and controllers, needs
// its Java side. The app ships SDL's Java classes (org.libsdl.app, of the SDL version
// Hyperspace is built with), forwards controller input to SDLControllerManager, and calls
// this once before hs_android_init, from a thread Java started (only there does JNI find
// the app's classes), with its JavaVM * and the activity (a jobject). Without it,
// Hyperspace's SDL features stay off. Returns 0, or 1 with the reason in error.
int hs_android_sdl_setup(void *java_vm, void *activity, char *error, size_t error_size);
typedef int (*hs_android_sdl_setup_fn)(void *java_vm, void *activity, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif
