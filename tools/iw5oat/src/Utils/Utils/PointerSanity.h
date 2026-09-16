#pragma once

#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
// MW32011NCP / iw5oat, 2026-09-17: Windows.h's wingdi.h defines ERROR as a
// plain macro (0), which silently clobbers Utils/Logging/Log.h's own
// `LogLevel::ERROR` enumerator in any translation unit that includes this
// header before Log.h -- a real build break found the first time this header
// was used (C2143/C2059 cascades inside Log.h with no obvious connection to
// this file). Undef it immediately after the include; nothing in this header
// or its callers needs the Windows ERROR macro.
#undef ERROR
#endif

namespace pointer_sanity
{
    // MW32011NCP / iw5oat, 2026-09-17: this fork's fastfile parser hits a
    // recurring bug class where a resolved-but-corrupted pointer (a forward
    // reference/alias-lookup miss, or a struct-shape mismatch elsewhere in the
    // zone) gets dereferenced before it's ever read as an asset name -- crashing
    // deep inside ucrtbase!strlen or a raw field read. A first pass of guards
    // (fastfile_format_research.md SS5.47/SS5.48) used a cheap bit-pattern
    // heuristic (`ptr >> 48 == 0` -- "looks like a real x64 canonical address")
    // but that has a real, demonstrated blind spot: a garbage value can still
    // fall inside the canonical range and pass it (e.g. 0x00000266`00000009 --
    // plausible upper bits, an obviously-too-small low DWORD) while still not
    // being a real, mapped, readable allocation.
    //
    // `IsLikelyReadablePointer` replaces the bit-pattern guess with a real
    // OS-level check via VirtualQuery: is this address actually committed
    // memory with read access, right now, in this process? This is strictly
    // stronger than any pattern heuristic and closes the whole bug class
    // rather than adding another special-cased shape to reject. Not free
    // (a real syscall per check), but every call site this guards is already
    // on a "we're about to crash anyway" path, not a hot loop.
    [[nodiscard]] inline bool IsLikelyReadablePointer(const void* ptr) noexcept
    {
        if (ptr == nullptr)
            return false;

#ifdef _WIN32
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(ptr, &mbi, sizeof(mbi)) == 0)
            return false;

        if (mbi.State != MEM_COMMIT)
            return false;

        if (mbi.Protect == PAGE_NOACCESS || (mbi.Protect & PAGE_GUARD) != 0)
            return false;

        constexpr DWORD READABLE_MASK = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        return (mbi.Protect & READABLE_MASK) != 0;
#else
        // Non-Windows builds of this tool aren't part of this fork's supported
        // matrix (the game/zone data this parses is Windows-only) -- fall back
        // to the original cheap canonical-range heuristic rather than assume
        // unconditionally safe.
        return (reinterpret_cast<uintptr_t>(ptr) >> 48) == 0 && reinterpret_cast<uintptr_t>(ptr) >= 0x10000;
#endif
    }
}
