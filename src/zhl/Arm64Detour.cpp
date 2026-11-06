#if defined(__aarch64__)

#include "Arm64Detour.h"
#include "../helpers/PALPlatform.h"
#if defined(HS_ANDROID) && !defined(__APPLE__)  // macOS asks the kernel instead (MapsProtection)
    #include "android/MachO.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifndef HS_IPAD_SWITCH
    #include <sys/mman.h>
#else
    #define PROT_READ 1
    #define PROT_WRITE 2
    #define PROT_EXEC 4
#endif
#include <unistd.h>
#include <mutex>
#include <vector>
#include <algorithm>

#ifdef __APPLE__
    // macOS (iOS-FTL-for-macOS): the game's code is mapped from its signed file and
    // mprotect() refuses to make it writable; Mach calls give a private copy instead
    #include <mach/mach.h>
    #include <libkern/OSCacheControl.h>
#elif !defined(MAP_FIXED_NOREPLACE)
    #define MAP_FIXED_NOREPLACE 0x100000
#endif

namespace
{
    uintptr_t g_gameStart = 0;
    size_t g_gameSize = 0;

    void FlushCode(void *p, size_t n)
    {
#ifdef __APPLE__
        sys_icache_invalidate(p, n);  // iOS doesn't export __clear_cache
#else
        __builtin___clear_cache((char *)p, (char *)p + n);
#endif
    }

    size_t PageSize()
    {
        static size_t s = (size_t)sysconf(_SC_PAGESIZE);
        return s;
    }

    struct Pool
    {
        uint8_t *base = nullptr;
        size_t size = 0, used = 0;
    };

    Pool g_pool;
    std::mutex g_lock;

    void *(*g_hostAlloc)(size_t) = nullptr;
    int (*g_hostWrite)(void *, const void *, size_t) = nullptr;

    // Image mode (a prepatched game, for iOS where code can't be changed at runtime):
    // the trampolines are in a segment of the game image, and the code reaches
    // everything outside the image (Hyperspace's functions) through a table of
    // pointers in another one. So the code depends on nothing but the image and is
    // the same at any address: it's recorded once (on a Mac, where it can be
    // written) and put in the game's file, and where it can't be written it's only
    // compared (verify mode).
    bool g_imageMode, g_verify;
    void **g_slots;
    size_t g_slotCount, g_slotsUsed;
    // Each hooked function's first instruction before it was hooked: Hook() needs
    // it, and in verify mode the function already starts with the hook's branch.
    struct Original { uint32_t offset, ins; };  // offset from the game's start
    Original *g_originals;
    size_t g_originalCount, g_originalsUsed;
    char g_verifyError[160];

    bool InImage(uint64_t a) { return a >= g_gameStart && a < g_gameStart + g_gameSize; }

    bool NearEnough(uintptr_t from, uintptr_t to)
    {
        intptr_t d = (intptr_t)to - (intptr_t)from;
        return d > -(intptr_t)(120u << 20) && d < (intptr_t)(120u << 20);
    }

    bool PoolInReach(uintptr_t pool, size_t size, uintptr_t lo, uintptr_t hi)
    {
        return NearEnough(lo, pool) && NearEnough(lo, pool + size) && NearEnough(hi, pool) && NearEnough(hi, pool + size);
    }

    // The process's mappings, sorted: [start, end)
    std::vector<std::pair<uintptr_t, uintptr_t>> MappedRanges()
    {
        std::vector<std::pair<uintptr_t, uintptr_t>> used;
#ifdef __APPLE__
        vm_address_t a = 0;
        vm_size_t len = 0;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t obj;
        while (vm_region_64(mach_task_self(), &a, &len, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count,
                              &obj) == KERN_SUCCESS)
        {
            used.emplace_back((uintptr_t)a, (uintptr_t)(a + len));
            a += len;
            count = VM_REGION_BASIC_INFO_COUNT_64;
        }
#elif !defined(HS_IPAD_SWITCH)
        FILE *f = fopen("/proc/self/maps", "r");
        if (!f) return used;
        char line[512];
        while (fgets(line, sizeof(line), f))
        {
            unsigned long a, b;
            if (sscanf(line, "%lx-%lx", &a, &b) == 2) used.emplace_back(a, b);
        }
        fclose(f);
#endif
        std::sort(used.begin(), used.end());
        return used;
    }

    // Read + write memory at exactly `want`, or nullptr if something is there
    void *MapAt(uintptr_t want, size_t size)
    {
#ifdef __APPLE__
        vm_address_t a = want;
        if (vm_allocate(mach_task_self(), &a, size, VM_FLAGS_FIXED) != KERN_SUCCESS) return nullptr;
        return (void *)a;
#elif !defined(HS_IPAD_SWITCH)
        void *p = mmap((void *)want, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        return p == MAP_FAILED ? nullptr : p;
#else
        return nullptr;
#endif
    }

    void FreeMap(void *p, size_t size)
    {
#ifdef __APPLE__
        vm_deallocate(mach_task_self(), (vm_address_t)p, size);
#elif !defined(HS_IPAD_SWITCH)
        munmap(p, size);
#endif
    }

    bool TakePool(Pool &pool, void *p, size_t size)
    {
#ifndef HS_IPAD_SWITCH
        if (!g_hostAlloc) mprotect(p, size, PROT_READ | PROT_EXEC);
#endif
        pool.base = (uint8_t *)p;
        pool.size = size;
        pool.used = 0;
        return true;
    }

    bool MakePoolInGap(Pool &pool, uintptr_t lo, uintptr_t hi, size_t size)
    {
#ifdef HS_IPAD_SWITCH
        return false;
#endif
        std::vector<std::pair<uintptr_t, uintptr_t>> used = MappedRanges();
        if (used.empty()) return false;

        const uintptr_t align = 1u << 16;
        const uintptr_t reach = 120u << 20;
        uintptr_t windowLo = hi > reach ? hi - reach + 1 : 0;
        uintptr_t windowHi = lo + reach - 1;
        std::vector<uintptr_t> candidates;
        uintptr_t prevEnd = 0;
        for (size_t i = 0; i <= used.size(); i++)
        {
            uintptr_t gapLo = std::max(prevEnd, windowLo);
            uintptr_t gapHi = std::min(i < used.size() ? used[i].first : ~(uintptr_t)0, windowHi);
            if (i < used.size()) prevEnd = std::max(prevEnd, used[i].second);
            gapLo = (gapLo + align - 1) & ~(align - 1);
            if (gapHi <= gapLo || gapHi - gapLo < size) continue;
            uintptr_t at = gapLo >= hi ? gapLo : ((gapHi - size) & ~(align - 1));
            if (at >= gapLo) candidates.push_back(at);
        }
        uintptr_t mid = lo + (hi - lo) / 2;
        std::sort(candidates.begin(), candidates.end(), [mid](uintptr_t x, uintptr_t y)
        {
            uintptr_t dx = x > mid ? x - mid : mid - x, dy = y > mid ? y - mid : mid - y;
            return dx < dy;
        });
        for (uintptr_t want : candidates)
        {
            void *p = MapAt(want, size);
            if (!p) continue;
            if (!PoolInReach((uintptr_t)p, size, lo, hi))
            {
                FreeMap(p, size);
                continue;
            }
            return TakePool(pool, p, size);
        }
        return false;
    }

    bool MakePoolNear(Pool &pool, uintptr_t lo, uintptr_t hi, size_t size)
    {
        if (g_hostAlloc)
        {
            void *p = g_hostAlloc(size);
            return p && PoolInReach((uintptr_t)p, size, lo, hi) && TakePool(pool, p, size);
        }
#ifdef HS_IPAD_SWITCH
        return false;
#endif
        if (MakePoolInGap(pool, lo, hi, size)) return true;

        const uintptr_t align = 1u << 16;
        for (uintptr_t step = 0; step < (64u << 20); step += (1u << 20))
        {
            for (int dir = 0; dir < 2; dir++)
            {
                uintptr_t want = dir == 0 ? ((hi + align - 1) & ~(align - 1)) + step
                                          : (lo & ~(align - 1)) - size - step;
                void *p = MapAt(want, size);
                if (!p) continue;
                if (!PoolInReach((uintptr_t)p, size, lo, hi))
                {
                    FreeMap(p, size);
                    continue;
                }
                return TakePool(pool, p, size);
            }
        }
        return false;
    }

    bool MakePool(uintptr_t near)
    {
        uintptr_t lo = g_gameStart ? g_gameStart : near;
        uintptr_t hi = g_gameStart ? g_gameStart + g_gameSize : near;
        for (size_t size : {(size_t)1u << 20, (size_t)512u << 10, (size_t)256u << 10})
        {
            if (MakePoolNear(g_pool, lo, hi, size)) return true;
        }
        return false;
    }

    bool SetProt(void *addr, size_t size, int prot)
    {
#ifdef HS_IPAD_SWITCH
        return false;
#else
        uintptr_t start = (uintptr_t)addr & ~(uintptr_t)(PageSize() - 1);
        uintptr_t end = ((uintptr_t)addr + size + PageSize() - 1) & ~(uintptr_t)(PageSize() - 1);
#ifdef __APPLE__
        // VM_PROT_COPY: a private copy, writable even where the file mapping isn't
        // (PROT_* and VM_PROT_* have the same values). Pages are never writable and
        // executable at once on macOS: code is written while it isn't executable, which
        // is safe because hooks go in while only the loading thread runs.
        vm_prot_t vm = prot & PROT_WRITE ? (prot & ~PROT_EXEC) | VM_PROT_COPY : prot;
        return vm_protect(mach_task_self(), start, end - start, false, vm) == KERN_SUCCESS;
#else
        return mprotect((void *)start, end - start, prot) == 0;
#endif
#endif
    }

    int MapsProtection(uintptr_t a)
    {
#ifdef __APPLE__
        vm_address_t at = a;
        vm_size_t len = 0;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t obj;
        if (vm_region_64(mach_task_self(), &at, &len, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &obj) !=
                KERN_SUCCESS || at > a)
            return PROT_READ;
        return info.protection & (PROT_READ | PROT_WRITE | PROT_EXEC);
#endif
        FILE *f = fopen("/proc/self/maps", "r");
        if (!f) return PROT_READ;
        char line[512];
        int prot = PROT_READ;
        while (fgets(line, sizeof(line), f))
        {
            unsigned long lo, hi;
            char perms[5];
            if (sscanf(line, "%lx-%lx %4s", &lo, &hi, perms) != 3) continue;
            if (a < lo || a >= hi) continue;
            prot = (perms[0] == 'r' ? PROT_READ : 0) | (perms[1] == 'w' ? PROT_WRITE : 0) | (perms[2] == 'x' ? PROT_EXEC : 0);
            break;
        }
        fclose(f);
        return prot;
    }

    int CurrentProt(void *addr)
    {
        uintptr_t a = (uintptr_t)addr;
        if (g_pool.base && a >= (uintptr_t)g_pool.base && a < (uintptr_t)g_pool.base + g_pool.size)
            return PROT_READ | PROT_EXEC;
#if defined(HS_ANDROID) && !defined(__APPLE__)
        if (g_gameStart && a >= g_gameStart && a < g_gameStart + g_gameSize)
        {
            int vm = MachO::SegmentProtection((const void *)g_gameStart, a - g_gameStart);
            if (vm >= 0)
            {
                return (vm & MachO::VM_PROT_READ ? PROT_READ : 0) | (vm & MachO::VM_PROT_WRITE ? PROT_WRITE : 0) |
                       (vm & MachO::VM_PROT_EXECUTE ? PROT_EXEC : 0);
            }
        }
#endif
        return MapsProtection(a);
    }

    bool RawWriteCode(void *dst, const void *src, size_t n, int prot)
    {
        if (g_verify)
        {
            if (memcmp(dst, src, n) == 0) return true;
            if (!g_verifyError[0])
                snprintf(g_verifyError, sizeof(g_verifyError),
                         "the game's prepatched code at %#lx doesn't match this Hyperspace; patch the game with it again",
                         (unsigned long)((uintptr_t)dst - g_gameStart + 0x100000000ul));
            return false;
        }
        if (g_hostWrite) return g_hostWrite(dst, src, n) == 0;
        if (!SetProt(dst, n, prot | PROT_READ | PROT_WRITE)) return false;
        memcpy(dst, src, n);
        bool ok = SetProt(dst, n, prot);
        FlushCode(dst, n);
        return ok;
    }

    bool RawWriteCode(void *dst, const void *src, size_t n)
    {
        return RawWriteCode(dst, src, n, CurrentProt(dst));
    }

    // Image mode: a pointer-table entry holding `value`, or nullptr if the table is full
    void **NewSlot(uint64_t value)
    {
        if (g_slotsUsed >= g_slotCount) return nullptr;
        void **slot = &g_slots[g_slotsUsed++];
        *slot = (void *)(uintptr_t)value;  // data: written in verify mode too
        return slot;
    }

    struct Emitter
    {
        uint32_t buf[64];
        int n = 0;
        uint64_t lits[16];
        int nlits = 0;
        int litFixups[16];
        // Image mode: instructions that address something in the image, completed once
        // the code's own address is known (Finish)
        enum { FixB, FixAdrp, FixAddLo12, FixLdrLo12 };
        struct Fix { int at, kind; uint64_t target; } fixes[24];
        int nfixes = 0;
        bool failed = false;  // the pointer table is full

        void Emit(uint32_t ins) { buf[n++] = ins; }

        void EmitFixed(uint32_t ins, int kind, uint64_t target)
        {
            fixes[nfixes++] = Fix{n, kind, target};
            Emit(ins);
        }

        // reg = address (image mode: PC-relative inside the image, else from the pointer table)
        void LoadAddress(int reg, uint64_t value)
        {
            if (!g_imageMode)
            {
                LoadLiteral(reg, value);
                return;
            }
            if (InImage(value))
            {
                EmitFixed(0x90000000u | (uint32_t)reg, FixAdrp, value);                             // adrp
                EmitFixed(0x91000000u | ((uint32_t)reg << 5) | (uint32_t)reg, FixAddLo12, value);  // add
                return;
            }
            void **slot = NewSlot(value);
            if (!slot) failed = true;
            uint64_t at = (uint64_t)(uintptr_t)slot;
            EmitFixed(0x90000000u | (uint32_t)reg, FixAdrp, at);                                    // adrp
            EmitFixed(0xf9400000u | ((uint32_t)reg << 5) | (uint32_t)reg, FixLdrLo12, at);         // ldr
        }

        // Instructions JumpTo / CallTo emit
        int JumpLength(uint64_t to) const { return g_imageMode && InImage(to) ? 1 : g_imageMode ? 3 : 2; }

        void JumpTo(uint64_t to)
        {
            if (g_imageMode && InImage(to)) EmitFixed(0x14000000u, FixB, to);  // b
            else if (g_imageMode) { LoadAddress(16, to); Emit(0xd61f0200u); }  // br x16
            else JumpAbs(to);
        }

        void CallTo(uint64_t to)
        {
            if (g_imageMode && InImage(to)) EmitFixed(0x94000000u, FixB, to);  // bl
            else { LoadAddress(16, to); Emit(0xd63f0200u); }                    // blr x16
        }

        void LoadLiteral(int reg, uint64_t value)
        {
            litFixups[nlits] = n;
            lits[nlits++] = value;
            Emit(0x58000000u | (uint32_t)reg);
        }

        void JumpAbs(uint64_t to)
        {
            LoadLiteral(16, to);
            Emit(0xd61f0200u);  // br x16
        }

        size_t Size() const
        {
            return (((size_t)n * 4 + 7) & ~(size_t)7) + (size_t)nlits * 8;
        }

        size_t Finish(uint8_t *out, uint64_t pc)
        {
            for (int i = 0; i < nfixes; i++)
            {
                const Fix &f = fixes[i];
                uint64_t at = pc + (uint64_t)f.at * 4;
                if (f.kind == FixB)
                    buf[f.at] |= (uint32_t)((((int64_t)f.target - (int64_t)at) >> 2) & 0x3ffffff);
                else if (f.kind == FixAdrp)
                {
                    int64_t pages = (int64_t)(f.target >> 12) - (int64_t)(at >> 12);
                    buf[f.at] |= (uint32_t)(pages & 3) << 29 | (uint32_t)((pages >> 2) & 0x7ffff) << 5;
                }
                else if (f.kind == FixAddLo12)
                    buf[f.at] |= (uint32_t)(f.target & 0xfff) << 10;
                else
                    buf[f.at] |= (uint32_t)((f.target & 0xfff) >> 3) << 10;
            }
            size_t code = (size_t)n * 4;
            size_t litOff = (code + 7) & ~(size_t)7;
            for (int i = 0; i < nlits; i++)
            {
                int at = litFixups[i];
                int64_t delta = (int64_t)(litOff + (size_t)i * 8) - (int64_t)at * 4;
                buf[at] |= (uint32_t)(((delta >> 2) & 0x7ffff) << 5);
            }
            memcpy(out, buf, code);
            if (litOff > code) memcpy(out + code, "\x1f\x20\x03\xd5", 4);  // nop
            memcpy(out + litOff, lits, (size_t)nlits * 8);
            return litOff + (size_t)nlits * 8;
        }
    };

    int64_t SignExtend(uint64_t v, int bits)
    {
        uint64_t m = 1ull << (bits - 1);
        return (int64_t)((v ^ m) - m);
    }

    bool Relocate(Emitter &e, uint32_t ins, uint64_t pc, char *error, size_t errorSize)
    {
        if ((ins & 0x7c000000u) == 0x14000000u)  // B / BL
        {
            uint64_t to = pc + (uint64_t)(SignExtend(ins & 0x3ffffff, 26) * 4);
            if (ins & 0x80000000u)  // BL
                e.CallTo(to);
            else
                e.JumpTo(to);
            return true;
        }
        bool cond = (ins & 0xff000010u) == 0x54000000u;   // B.cond
        bool cbz = (ins & 0x7e000000u) == 0x34000000u;    // CBZ / CBNZ
        bool tbz = (ins & 0x7e000000u) == 0x36000000u;    // TBZ / TBNZ
        if (cond || cbz || tbz)
        {
            uint64_t to = tbz ? pc + (uint64_t)(SignExtend((ins >> 5) & 0x3fff, 14) * 4)
                              : pc + (uint64_t)(SignExtend((ins >> 5) & 0x7ffff, 19) * 4);
            uint32_t keep = tbz ? (ins & ~(0x3fffu << 5)) | (2u << 5) : (ins & ~(0x7ffffu << 5)) | (2u << 5);
            e.Emit(keep);                                                  // taken: to the jump
            e.Emit(0x14000000u | (uint32_t)(1 + e.JumpLength(to)));  // not taken: past it
            e.JumpTo(to);
            return true;
        }
        if ((ins & 0x1f000000u) == 0x10000000u)  // ADR / ADRP
        {
            int rd = ins & 0x1f;
            uint64_t imm = ((ins >> 29) & 3) | (((uint64_t)(ins >> 5) & 0x7ffff) << 2);
            uint64_t value;
            if (ins & 0x80000000u)
                value = (pc & ~0xfffull) + (uint64_t)(SignExtend(imm, 21) << 12);
            else
                value = pc + (uint64_t)SignExtend(imm, 21);
            e.LoadAddress(rd, value);
            return true;
        }
        if ((ins & 0x3b000000u) == 0x18000000u)  // LDR (literal)
        {
            uint64_t addr = pc + (uint64_t)(SignExtend((ins >> 5) & 0x7ffff, 19) * 4);
            int rt = ins & 0x1f;
            uint32_t opc = ins >> 30;
            bool simd = (ins >> 26) & 1;
            e.LoadAddress(17, addr);
            if (!simd)
            {
                if (opc == 0) e.Emit(0xb9400000u | (17u << 5) | (uint32_t)rt);       // ldr wt, [x17]
                else if (opc == 1) e.Emit(0xf9400000u | (17u << 5) | (uint32_t)rt);  // ldr xt, [x17]
                else if (opc == 2) e.Emit(0xb9800000u | (17u << 5) | (uint32_t)rt);  // ldrsw xt, [x17]
                else e.Emit(0xd503201fu);                                            // nop
            }
            else
            {
                if (opc == 0) e.Emit(0xbd400000u | (17u << 5) | (uint32_t)rt);       // ldr st, [x17]
                else if (opc == 1) e.Emit(0xfd400000u | (17u << 5) | (uint32_t)rt);  // ldr dt, [x17]
                else if (opc == 2) e.Emit(0x3dc00000u | (17u << 5) | (uint32_t)rt);  // ldr qt, [x17]
                else
                {
                    snprintf(error, errorSize, "unsupported literal load %08x", ins);
                    return false;
                }
            }
            return true;
        }
        e.Emit(ins);
        return true;
    }

    uint8_t *PoolAlloc(size_t n, Pool &pool = g_pool)
    {
        n = (n + 15) & ~(size_t)15;
        if (pool.used + n > pool.size) return nullptr;
        uint8_t *p = pool.base + pool.used;
        pool.used += n;
        return p;
    }

    uint8_t *PoolWrite(Emitter &e, Pool &pool = g_pool)
    {
        alignas(8) uint8_t tmp[sizeof(e.buf) + sizeof(e.lits) + 8];
        uint8_t *p = PoolAlloc(e.Size(), pool);
        if (!p) return nullptr;
        if (e.failed) return nullptr;
        size_t n = e.Finish(tmp, (uint64_t)(uintptr_t)p);
        return RawWriteCode(p, tmp, n) ? p : nullptr;
    }

    uint32_t BranchTo(uintptr_t from, uintptr_t to)
    {
        return 0x14000000u | (uint32_t)((((int64_t)to - (int64_t)from) >> 2) & 0x3ffffff);
    }
}

namespace Arm64Detour
{
    void SetHostCode(void *(*alloc)(size_t size), int (*write)(void *dst, const void *src, size_t n))
    {
        g_hostAlloc = alloc;
        g_hostWrite = write;
    }

    bool WriteCode(void *dst, const void *src, size_t n)
    {
        std::lock_guard<std::mutex> lock(g_lock);
        return RawWriteCode(dst, src, n);
    }

    void SetGameRange(uintptr_t start, size_t size)
    {
        g_gameStart = start;
        g_gameSize = size;
    }

    void SetImageMode(void *code, size_t codeSize, void **slots, size_t slotCount, void *originals,
                      size_t originalsSize, bool verify)
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_imageMode = true;
        g_verify = verify;
        g_pool.base = (uint8_t *)code;
        g_pool.size = codeSize;
        g_pool.used = 0;
        g_slots = slots;
        g_slotCount = slotCount;
        g_slotsUsed = 0;
        g_originals = (Original *)originals;
        g_originalCount = originalsSize / sizeof(Original);
        g_originalsUsed = 0;
    }

    size_t SlotsUsed(size_t *capacity)
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (capacity) *capacity = g_slotCount;
        return g_slotsUsed;
    }

    const char *VerifyError() { return g_verifyError; }

    void SetPoolArea(void *base, size_t size)
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!base || g_pool.base) return;
        if (PoolInReach((uintptr_t)base, size, g_gameStart, g_gameStart + g_gameSize)) TakePool(g_pool, base, size);
    }

    size_t PoolUsed(size_t *capacity)
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (capacity) *capacity = g_pool.size;
        return g_pool.used;
    }

    void *Hook(void *target, void *hook, int flags, char *error, size_t errorSize)
    {
        std::lock_guard<std::mutex> lock(g_lock);
        uintptr_t t = (uintptr_t)target;
        if (!g_pool.base && !MakePool(t))
        {
            snprintf(error, errorSize, "no memory for hook trampolines near the game");
            return nullptr;
        }
        if (!NearEnough(t, (uintptr_t)g_pool.base) || !NearEnough(t, (uintptr_t)g_pool.base + g_pool.size))
        {
            snprintf(error, errorSize, "function is out of branch range of the trampolines");
            return nullptr;
        }

        uint32_t first;
        memcpy(&first, target, 4);
        bool finalBranch = true;  // this hook's branch is the one the function keeps
        if (g_imageMode)
        {
            // Hooks go in in the same order every time; the table keeps what each one replaced.
            if (g_originalsUsed >= g_originalCount)
            {
                snprintf(error, errorSize, "the prepatch table is full");
                return nullptr;
            }
            Original o = {(uint32_t)(t - g_gameStart), first};
            if (g_verify)
            {
                if (g_originals[g_originalsUsed].offset != o.offset)
                {
                    snprintf(error, errorSize, "the game is prepatched for another Hyperspace; patch it with this one again");
                    return nullptr;
                }
                first = g_originals[g_originalsUsed].ins;
                // A function hooked again later ends up with the later hook's branch
                for (size_t i = g_originalsUsed + 1; i < g_originalCount && g_originals[i].offset; i++)
                    if (g_originals[i].offset == o.offset) finalBranch = false;
                g_originalsUsed++;
            }
            else
            {
                if (!RawWriteCode(&g_originals[g_originalsUsed], &o, sizeof(o)))
                {
                    snprintf(error, errorSize, "could not write the prepatch table");
                    return nullptr;
                }
                g_originalsUsed++;
            }
        }

        Emitter tr;
        if (!Relocate(tr, first, t, error, errorSize)) return nullptr;
        tr.JumpTo(t + 4);

        Emitter ve;
        if (flags & KeepThis)
        {
            ve.Emit(0xa9be7bfdu);  // stp x29, x30, [sp, #-32]!
            ve.Emit(0x910003fdu);  // mov x29, sp
            ve.Emit(0xf9000be0u);  // str x0, [sp, #16]
            ve.CallTo((uint64_t)(uintptr_t)hook);
            ve.Emit(0xf9400be0u);  // ldr x0, [sp, #16]
            ve.Emit(0xa8c27bfdu);  // ldp x29, x30, [sp], #32
            ve.Emit(0xd65f03c0u);  // ret
        }
        else
        {
            ve.JumpTo((uint64_t)(uintptr_t)hook);
        }

        uint8_t *tramp = PoolWrite(tr);
        uint8_t *veneer = tramp ? PoolWrite(ve) : nullptr;
        if (!tramp || !veneer)
        {
            snprintf(error, errorSize, "%s", g_verifyError[0] ? g_verifyError
                                         : tr.failed || ve.failed ? "the prepatch's pointer table is full"
                                         : "out of hook trampoline space, or it can't be written");
            return nullptr;
        }
        int64_t d = ((int64_t)(uintptr_t)veneer - (int64_t)t) >> 2;
        uint32_t b = 0x14000000u | (uint32_t)(d & 0x3ffffff);
        if ((!g_verify || finalBranch) && !RawWriteCode(target, &b, 4))
        {
            snprintf(error, errorSize, "%s", g_verifyError[0] ? g_verifyError : "could not make game code writable");
            return nullptr;
        }
        return tramp;
    }

    void *MakeTrap(const char *name, void (*handler)(const char *name))
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g_pool.base && !MakePool(g_gameStart)) return nullptr;
        Emitter e;
        e.LoadAddress(0, (uint64_t)(uintptr_t)name);
        e.JumpTo((uint64_t)(uintptr_t)handler);
        return PoolWrite(e);
    }

    int ReplaceUntranslatable(void *start, size_t size, char *error, size_t errorSize)
    {
        std::lock_guard<std::mutex> lock(g_lock);
        static Pool own;
        uintptr_t lo = (uintptr_t)start, hi = lo + size;
        if (!own.base && !MakePoolNear(own, lo, hi, 1u << 16))
        {
            snprintf(error, errorSize, "no memory near the code to rewrite");
            return -1;
        }
        int count = 0;
        for (uintptr_t a = lo; a + 4 <= hi; a += 4)
        {
            uint32_t ins;
            memcpy(&ins, (void *)a, 4);
            // FCVTZS / FCVTZU (vector, integer), scalar
            if ((ins & 0xdfbffc00u) != 0x5ea1b800u) continue;
            bool isUnsigned = (ins >> 29) & 1, isDouble = (ins >> 22) & 1;
            uint32_t rn = (ins >> 5) & 31, rd = ins & 31;
            Emitter e;
            e.Emit(0xd10043ffu);  // sub sp, sp, #16
            e.Emit(0xf90003f0u);  // str x16, [sp]
            // fcvtz{s,u} w16, sN / x16, dN
            e.Emit((isDouble ? 0x9e780000u : 0x1e380000u) | (isUnsigned ? 0x10000u : 0) | (rn << 5) | 16u);
            e.Emit((isDouble ? 0x9e670000u : 0x1e270000u) | (16u << 5) | rd);  // fmov sD, w16 / dD, x16
            e.Emit(0xf94003f0u);  // ldr x16, [sp]
            e.Emit(0x910043ffu);  // add sp, sp, #16
            uint8_t *stub = PoolAlloc(e.Size() + 4, own);
            if (!stub)
            {
                snprintf(error, errorSize, "out of space for rewritten instructions");
                return -1;
            }
            e.Emit(BranchTo((uintptr_t)stub + (uintptr_t)e.n * 4, a + 4));
            alignas(8) uint8_t tmp[sizeof(e.buf) + 8];
            size_t n = e.Finish(tmp, (uint64_t)(uintptr_t)stub);
            uint32_t b = BranchTo(a, (uintptr_t)stub);
            if (!RawWriteCode(stub, tmp, n, PROT_READ | PROT_EXEC) || !RawWriteCode((void *)a, &b, 4, PROT_READ | PROT_EXEC))
            {
                snprintf(error, errorSize, "could not write code");
                return -1;
            }
            count++;
        }
        return count;
    }

    bool Unprotect(void *address, size_t size, int *oldProt)
    {
        if (g_hostWrite) { *oldProt = 0; return true; }
        *oldProt = CurrentProt(address);
        // Code is only compared in verify mode (WriteCode), never made writable
        if (g_verify && (*oldProt & PROT_EXEC)) return true;
        return SetProt(address, size, *oldProt | PROT_READ | PROT_WRITE);
    }

    bool Reprotect(void *address, size_t size, int oldProt)
    {
        if (g_hostWrite) return true;
        if (g_verify && (oldProt & PROT_EXEC)) return true;
        bool ok = SetProt(address, size, oldProt);
        if (oldProt & PROT_EXEC) FlushCode(address, size);
        return ok;
    }
}

#endif // __aarch64__
