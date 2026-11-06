#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Arm64Detour
{
    void SetGameRange(uintptr_t start, size_t size);

    void SetPoolArea(void *base, size_t size);

    // A prepatched game (see Arm64Detour.cpp): trampolines in `code` (a segment of the
    // game), what they reach outside the game through `slots`, the hooked functions'
    // first instructions in `originals`. verify: the code is already there; compare it
    // and only fill the slots.
    void SetImageMode(void *code, size_t codeSize, void **slots, size_t slotCount, void *originals,
                      size_t originalsSize, bool verify);
    size_t SlotsUsed(size_t *capacity);
    const char *VerifyError();  // why verify mode failed, or ""

    size_t PoolUsed(size_t *capacity);

    enum
    {
        KeepThis = 1,
    };

    void *Hook(void *target, void *hook, int flags, char *error, size_t errorSize);

    void *MakeTrap(const char *name, void (*handler)(const char *name));

    int ReplaceUntranslatable(void *start, size_t size, char *error, size_t errorSize);

    void SetHostCode(void *(*alloc)(size_t size), int (*write)(void *dst, const void *src, size_t n));
    bool WriteCode(void *dst, const void *src, size_t n);

    bool Unprotect(void *address, size_t size, int *oldProt);
    bool Reprotect(void *address, size_t size, int oldProt);
}
