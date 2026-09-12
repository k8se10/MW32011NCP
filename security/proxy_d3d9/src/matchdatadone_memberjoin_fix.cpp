// matchdatadone_memberjoin_fix.cpp -- Findings 2 and 3, iw5mp.exe. Combined into
// ONE hook (not two separate fix modules) because both bugs share the exact same
// underlying root cause AND the same shared copy primitive -- see below.
//
// == Root cause, both findings ==
// This engine's internal message-reader system (a stateful, ring-buffer-backed
// reader struct, confirmed via decompile of FUN_140285ca0/140285910/140285be0/
// 140285970 -- each one reads a cursor field and writes it back as a side effect,
// so calling any of them a second time to "peek" desyncs the parser for every
// subsequent read on that connection) has a shared low-level copy step:
//   void FUN_140285970(ReaderStruct* reader, void* dest, int length)
// which copies `length` bytes FROM the reader's own buffered message data TO
// `dest`, with NO knowledge of `dest`'s own real capacity -- exactly like a raw
// memcpy, it trusts its caller completely.
//
// FINDING 2 (matchdatadone dispatcher, FUN_1400db980 case 3): reads a length via
// FUN_140285ca0, checks it against 0x3fc (1020) and LOGS A WARNING if it's larger
// -- but calls FUN_140285970 with the full, unclamped length regardless, into a
// 1024-byte stack buffer (local_428). Real, confirmed call-site address (this
// project's own live disassembly, not the decompile's pseudo-C):
// FUN_140285be0 @ 0x1400dbb2e -> FUN_140285ca0 @ 0x1400dbb3a -> CMP AX,R12W
// (R12W=0x3fc) @ 0x1400dbb41 -> warn CALL @ 0x1400dbb53 -> THE VULNERABLE COPY
// CALL @ 0x1400dbb67.
//
// FINDING 3 (pa_memberjoin handler, FUN_1400ec1b0): reads a length the same way,
// and calls FUN_140285970 with ZERO check at all (not even log-only) into a
// 512-byte stack buffer. This function calls the shared copy primitive TWICE --
// disassembly confirms one call at 0x1400ec364 (BEFORE the length read at
// 0x1400ec3b7, so unrelated to this bug -- almost certainly copying some other,
// fixed-size field) and the real vulnerable one at 0x1400ec3d0, immediately after
// the length read. Scoping this fix to the SECOND call specifically (not "any
// call from within this function") avoids clamping the unrelated first call,
// which this project has no evidence needs the same 512-byte limit.
//
// == Fix design ==
// Rather than trying to "peek" the length before either vulnerable function's own
// stateful read consumes it (ruled out -- see the header comment above, and
// re_notes/vulnerability_research.md's own entry on why this was the first
// approach tried and abandoned), this hooks the SHARED COPY PRIMITIVE itself,
// FUN_140285970, exactly once. Since this primitive is almost certainly called
// from many other, unrelated, legitimate places throughout this engine's netcode,
// the detour is SCOPED by EXACT return address (the address execution resumes at
// once this detour returns) -- not a whole-function range, a single specific
// address per finding, computed as a fixed offset from each dispatcher's own
// resolved base address (this project's own established anchor-plus-fixed-offset
// pattern, applied to a code address the same way MW32011NCP's own x64 port
// already applies it to reach related code from an anchor -- see that project's
// own analog_input_hooks_x64.cpp for the precedent). Every other call to this
// primitive, anywhere else in the binary -- INCLUDING pa_memberjoin's own first,
// unrelated call to it -- passes through completely untouched.
//
// The actual fix per finding is a length clamp to the REAL destination buffer
// size (1020 for matchdatadone -- the SAME threshold the original code already
// checks against but never enforces; 512 for pa_memberjoin, which has no
// existing check to build on at all) -- applied before forwarding to the real
// copy primitive via the trampoline, so the copy itself is always safe
// regardless of what either vulnerable dispatcher's own surrounding logic does.

#include "netcode_fixes.h"
#include "signature_scan.h"

#include <windows.h>
#include <cstdio>
#include <intrin.h>

namespace {

// Real, Ghidra-confirmed function bodies (via FuncBounds.java against this
// project's own x64 Ghidra project) -- used only to compute the fixed offsets
// below, not for whole-function scoping (see this file's own header comment for
// why exact-call-site scoping is used instead).
constexpr uintptr_t kMatchdatadoneEntryGhidraAddr = 0x1400db980;
constexpr uintptr_t kMemberjoinEntryGhidraAddr = 0x1400ec1b0;
constexpr uintptr_t kCopyPrimitiveGhidraAddr = 0x140285970;

// Fixed offset from each dispatcher's own resolved base to the REAL RETURN
// ADDRESS immediately following its own vulnerable CALL to the shared copy
// primitive (call address + 5, since a direct CALL rel32 is always 5 bytes) --
// confirmed via live disassembly, not derived from the decompile's pseudo-C.
constexpr ptrdiff_t kMatchdatadoneVulnCallReturnOffset = 0x1400dbb6c - kMatchdatadoneEntryGhidraAddr; // 0x1EC
constexpr ptrdiff_t kMemberjoinVulnCallReturnOffset = 0x1400ec3d5 - kMemberjoinEntryGhidraAddr;         // 0x225

// Fixed offset from matchdatadone's own base to the shared copy primitive's real
// entry point -- both addresses are within the same static module image, so this
// offset is stable across ASLR exactly like every other anchor-plus-fixed-offset
// resolution this project's own x64 work already relies on.
constexpr ptrdiff_t kCopyPrimitiveOffsetFromMatchdatadone = kCopyPrimitiveGhidraAddr - kMatchdatadoneEntryGhidraAddr; // 0x1A9FF0

// Real, fixed prefix of FUN_1400db980 -- through the second LEA (RBP-relative, safe)
// at +0x2C, ending right before the first genuine CALL (a real address reference)
// at +0x33. 51 bytes, no wildcards needed.
constexpr char kMatchdatadoneSignature[] =
    "48 89 5C 24 08 48 89 74 24 10 55 57 41 54 41 56 41 57 48 8D AC 24 70 FC FF FF "
    "48 81 EC 90 04 00 00 48 8B F2 44 8B F1 BA 00 00 02 00 48 8D 8D D0 03 00 00";

// Real, fixed prefix of FUN_1400ec1b0 -- through "MOV R13,RDX" at +0x28, ending
// right before the first genuine CALL at +0x2B. 43 bytes, no wildcards needed.
constexpr char kMemberjoinSignature[] =
    "48 89 54 24 10 55 53 56 57 41 55 48 8D AC 24 B0 FD FF FF "
    "48 81 EC 50 03 00 00 48 8B D9 49 8B F1 48 8D 4C 24 60 49 8B F8 4C 8B EA";

constexpr size_t kMatchdatadoneRealBufferSize = 1020; // local_428[1024] minus a safety byte,
    // matching the ORIGINAL code's own 0x3fc(1020) check exactly -- this fix makes that
    // existing check actually enforced, it does not invent a new threshold
constexpr size_t kMemberjoinRealBufferSize = 512; // local_248[512] in the decompile

uintptr_t g_matchdatadoneVulnReturnAddr = 0;
uintptr_t g_memberjoinVulnReturnAddr = 0;

// Real signature confirmed via decompile: FUN_140285970(ReaderStruct*, void* dest, int length)
using CopyPrimitive_t = void(__fastcall*)(void* reader, void* dest, int length);
CopyPrimitive_t g_realCopyPrimitive = nullptr;

void __fastcall Hook_CopyPrimitive(void* reader, void* dest, int length)
{
    uintptr_t returnAddr = reinterpret_cast<uintptr_t>(_ReturnAddress());

    if (returnAddr == g_matchdatadoneVulnReturnAddr) {
        if (static_cast<size_t>(length) > kMatchdatadoneRealBufferSize) {
            length = static_cast<int>(kMatchdatadoneRealBufferSize);
        }
    } else if (returnAddr == g_memberjoinVulnReturnAddr) {
        if (static_cast<size_t>(length) > kMemberjoinRealBufferSize) {
            length = static_cast<int>(kMemberjoinRealBufferSize);
        }
    }
    // Every other caller of this shared primitive -- including pa_memberjoin's
    // OWN first, unrelated call to it -- falls through here completely
    // unmodified, length passed through exactly as the caller supplied it.

    g_realCopyPrimitive(reader, dest, length);
}

} // namespace

void InstallMatchdatadoneAndMemberjoinFix(const FixHostServices& host)
{
    void* moduleBase = host.GetGameModuleBase();
    if (!moduleBase) {
        host.Log("[nsp-mp-fix] FAILED: no game module base -- cannot resolve targets");
        return;
    }

    auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
    auto ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        reinterpret_cast<uintptr_t>(moduleBase) + dosHeader->e_lfanew);
    size_t moduleSize = ntHeaders->OptionalHeader.SizeOfImage;
    uintptr_t base = reinterpret_cast<uintptr_t>(moduleBase);

    // Both dispatchers are iw5mp.exe-only -- a failed scan here is expected, not
    // an error, if this DLL is loaded into iw5sp.exe instead.
    SigScan::Result matchResult = SigScan::FindPattern(kMatchdatadoneSignature, base, moduleSize);
    SigScan::Result memberResult = SigScan::FindPattern(kMemberjoinSignature, base, moduleSize);
    if (!matchResult.found && !memberResult.found) {
        host.Log("[nsp-mp-fix] Neither signature found -- expected if this is iw5sp.exe, "
                 "not a fix failure. Skipping (Findings 2/3 only affect iw5mp.exe).");
        return;
    }

    void* copyPrimitiveAddr = nullptr;

    if (matchResult.found) {
        g_matchdatadoneVulnReturnAddr = matchResult.address + kMatchdatadoneVulnCallReturnOffset;
        copyPrimitiveAddr = reinterpret_cast<void*>(matchResult.address + kCopyPrimitiveOffsetFromMatchdatadone);
    }
    if (memberResult.found) {
        g_memberjoinVulnReturnAddr = memberResult.address + kMemberjoinVulnCallReturnOffset;
    }
    if (matchResult.found != memberResult.found) {
        char buf[320];
        sprintf_s(buf, "[nsp-mp-fix] WARNING: only one of the two dispatchers resolved "
                  "(matchdatadone=%d, memberjoin=%d) -- the other's fix will not be active "
                  "this session even though this looks like iw5mp.exe. Investigate before "
                  "trusting this build against real gameplay.",
                  matchResult.found ? 1 : 0, memberResult.found ? 1 : 0);
        host.Log(buf);
    }
    if (!copyPrimitiveAddr) {
        host.Log("[nsp-mp-fix] FAILED: matchdatadone dispatcher not resolved, cannot derive "
                 "the shared copy primitive's address via the anchor offset. Fix not installed.");
        return;
    }

    void* original = nullptr;
    if (!host.InstallHook(copyPrimitiveAddr, reinterpret_cast<void*>(&Hook_CopyPrimitive), &original)) {
        char buf[256];
        sprintf_s(buf, "[nsp-mp-fix] FAILED: InstallHook failed for shared copy primitive @ %p", copyPrimitiveAddr);
        host.Log(buf);
        return;
    }
    g_realCopyPrimitive = reinterpret_cast<CopyPrimitive_t>(original);

    char buf[420];
    sprintf_s(buf, "[nsp-mp-fix] Installed. matchdatadone vuln-call-return @ 0x%llX (clamp %zu), "
              "memberjoin vuln-call-return @ 0x%llX (clamp %zu), shared copy primitive @ %p hooked.",
              static_cast<unsigned long long>(g_matchdatadoneVulnReturnAddr), kMatchdatadoneRealBufferSize,
              static_cast<unsigned long long>(g_memberjoinVulnReturnAddr), kMemberjoinRealBufferSize,
              copyPrimitiveAddr);
    host.Log(buf);
}
