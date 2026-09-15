// matchdatadone_memberjoin_fix.cpp -- Findings 2, 3, AND 4, all iw5mp.exe.
// Originally covered Findings 2+3 only ("matchdatadone" dispatcher and
// "pa_memberjoin" handler); Finding 4 (fragment-reassembly OOB write) added
// 2026-09-15 once a safe fix design was worked out, per netcode_fixes.h's own
// prior "not implemented this pass" note. All three share the exact same
// underlying shared copy primitive (FUN_140285970), which MinHook can only ever
// have ONE hook installed on -- they MUST live in one file with one hook,
// scoped per finding by exact return address, not three separate hooks on the
// same target (the second/third InstallHook call on an already-hooked address
// would simply fail). Finding 4 additionally needs a second, distinct hook (see
// "Finding 4" section below) since its vulnerable destination can't be
// validated from the copy primitive's own arguments alone.
//
// == Root cause, Findings 2 and 3 ==
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
// == Fix design, Findings 2 and 3 ==
// Rather than trying to "peek" the length before either vulnerable function's own
// stateful read consumes it (ruled out -- see the header comment above, and
// re_notes/vulnerability_research.md's own entry on why this was the first
// approach tried and abandoned), this hooks the SHARED COPY PRIMITIVE itself,
// FUN_140285970, exactly once. Since this primitive is almost certainly called
// from many other, unrelated, legitimate places throughout this engine's netcode,
// the detour is SCOPED by EXACT return address (the address execution resumes at
// once this detour returns) -- not a whole-function range, a single specific
// address per finding, computed as a fixed offset from a resolved anchor address
// (this project's own established anchor-plus-fixed-offset pattern, applied to a
// code address the same way MW32011NCP's own x64 port already applies it to reach
// related code from an anchor -- see that project's own analog_input_hooks_x64.cpp
// for the precedent). Every other call to this primitive, anywhere else in the
// binary -- INCLUDING pa_memberjoin's own first, unrelated call to it -- passes
// through completely untouched.
//
// The actual fix per finding is a length clamp to the REAL destination buffer
// size (1020 for matchdatadone -- the SAME threshold the original code already
// checks against but never enforces; 512 for pa_memberjoin, which has no
// existing check to build on at all) -- applied before forwarding to the real
// copy primitive via the trampoline, so the copy itself is always safe
// regardless of what either vulnerable dispatcher's own surrounding logic does.
//
// == Finding 4 (fragment-reassembly OOB write), added 2026-09-15 ==
// Real root cause (re_notes/vulnerability_research.md's own entry, and this
// project's own re_notes/INTERNAL_vulnerability_research.md for exact addresses):
// FUN_1400ad330 processes reliable message fragments in a loop. At entry it
// resolves this CONNECTION's own fixed-size (0x2ffc = 12284 byte) reassembly
// buffer via FUN_14027a4c0 (an index/lookup into a fixed global table, NOT a
// generic memory utility -- see below for why this matters), caching the result
// in a local (`lVar7` in the decompile). Per fragment, it reads an
// attacker-controlled OFFSET (0-65535, via the shared FUN_140285ca0 the same as
// findings 2/3) and LENGTH, checks `offset+length` against the buffer's real
// 0x2ffc size and LOGS A WARNING if it's exceeded -- but then calls the SAME
// shared copy primitive, FUN_140285970, with `dest = lVar7 + offset` regardless
// of whether either check passed. Same bug shape as findings 2/3 (a real check
// exists, but is log-only, never enforced), different failure mode: here the
// destination is `buffer_base + attacker_offset`, not a fixed stack buffer, so
// a simple length clamp (findings 2/3's fix) isn't enough on its own -- the
// primitive's own hook never sees `buffer_base` separately from the
// already-combined `dest` pointer, so it can't tell a small-but-past-the-end
// offset from a legitimate one without knowing the buffer's real base and size.
//
// CORRECTION to this project's own prior documentation: the earlier internal
// research doc described this function's "magic-byte-range/version-compat
// checks" section as having "no early-return... not otherwise understood." A
// fresh, fully-analyzed decompile (2026-09-15) shows this is not quite right --
// there IS a real early return in that section (confirmed via the referenced
// string "PLATFORM_DISCONNECTED_FROM_SERVER", i.e. this path disconnects the
// player as a protocol violation) -- but it is CONDITIONAL on an outer gate
// (two runtime flags this project has not identified the meaning of) being
// active; when that gate is false, execution reaches the vulnerable copy with
// zero validation regardless. This doesn't change the fix design below (the
// return-address-scoped hook fires identically regardless of which path
// reaches the vulnerable call), but is worth recording since it means the bug
// is not unconditionally reachable on every fragment the way the original
// summary implied -- narrower, not eliminated.
//
// == Fix design, Finding 4 ==
// This is real Plan (b) from the prior research pass's own two considered
// options (Plan (a), replacing FUN_1400ad330 wholesale, was rejected then and
// remains rejected now -- its "magic-byte-range/version-compat checks" section
// is still not fully understood, and faithfully reimplementing unknown behavior
// risks silently breaking something rather than just fixing the overflow).
//
// A SECOND hook, on FUN_14027a4c0 (the buffer-lookup function, NOT the generic
// memmove-style primitive FUN_1403f5380 that FUN_140285970 internally calls --
// deliberately NOT hooking that one: it's a fully generic byte-copy routine
// called from an enormous number of unrelated places throughout the entire
// binary for entirely unrelated purposes, e.g. rendering/audio/physics, and
// hooking it would be a much larger, hotter, less surgical footprint than any
// other hook this project has installed), scoped the same way to the exact
// return address of FUN_1400ad330's own specific call to it. Its real return
// value (the resolved buffer base pointer, or null if this connection has none)
// is captured into g_fragmentReassemblyBufferBase before being passed through
// unmodified via the trampoline.
//
// The (already-hooked) shared copy primitive then gets a THIRD scoped case: when
// the return address matches Finding 4's own vulnerable call site, validate
// `dest` (the combined buffer_base+offset the caller already computed) against
// the real captured buffer's `[base, base+0x2ffc)` range, clamping `length` down
// to whatever actually fits, or refusing the copy entirely (length 0) if `dest`
// is already at or past the end of the real buffer, or if no valid buffer has
// been captured for this connection yet at all (treated as unsafe-by-default,
// not "assume anything").
//
// Known limitation, same risk tolerance as findings 2/3's existing globals: both
// new globals (buffer base + its own return-address gate) are simple, unlocked
// globals, not connection-indexed. This assumes FUN_1400ad330's own reachable
// call path is single-threaded/sequential per this project's own reachability
// analysis (a per-connection ring-buffer loop with no per-client array indexing
// found anywhere in the function) -- consistent with, not independently proven
// beyond, what findings 2/3's own fix already assumes for this exact code area.

#include "netcode_fixes.h"
#include "signature_scan.h"

#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <intrin.h>

namespace {

// Real, Ghidra-confirmed function bodies (via FuncBounds.java against this
// project's own x64 Ghidra project) -- used only to compute the fixed offsets
// below, not for whole-function scoping (see this file's own header comment for
// why exact-call-site scoping is used instead).
constexpr uintptr_t kMatchdatadoneEntryGhidraAddr = 0x1400db980;
constexpr uintptr_t kMemberjoinEntryGhidraAddr = 0x1400ec1b0;
constexpr uintptr_t kFragmentReassemblyEntryGhidraAddr = 0x1400ad330; // FUN_1400ad330
constexpr uintptr_t kCopyPrimitiveGhidraAddr = 0x140285970;
constexpr uintptr_t kBufferResolveGhidraAddr = 0x14027a4c0; // FUN_14027a4c0

// Fixed offset from each dispatcher's own resolved base to the REAL RETURN
// ADDRESS immediately following its own vulnerable CALL to the shared copy
// primitive (call address + 5, since a direct CALL rel32 is always 5 bytes) --
// confirmed via live disassembly, not derived from the decompile's pseudo-C.
constexpr ptrdiff_t kMatchdatadoneVulnCallReturnOffset = 0x1400dbb6c - kMatchdatadoneEntryGhidraAddr; // 0x1EC
constexpr ptrdiff_t kMemberjoinVulnCallReturnOffset = 0x1400ec3d5 - kMemberjoinEntryGhidraAddr;         // 0x225
// Finding 4's own vulnerable copy call (CALL 0x140285970 @ 0x1400ad4cc, return
// address 0x1400ad4d1) and its buffer-resolve call (CALL 0x14027a4c0 @
// 0x1400ad37b, return address 0x1400ad380) -- both confirmed via live
// disassembly (DumpDisasm.java) against a freshly re-analyzed x64 Ghidra
// project, 2026-09-15.
constexpr ptrdiff_t kFragmentReassemblyVulnCallReturnOffset = 0x1400ad4d1 - kFragmentReassemblyEntryGhidraAddr; // 0x1A1
constexpr ptrdiff_t kFragmentReassemblyResolveCallReturnOffset = 0x1400ad380 - kFragmentReassemblyEntryGhidraAddr; // 0x50

// Fixed offset from each anchor to the shared copy primitive's real entry point
// -- both addresses are within the same static module image, so this offset is
// stable across ASLR exactly like every other anchor-plus-fixed-offset
// resolution this project's own x64 work already relies on. Kept from BOTH
// anchors (not just matchdatadone's) so a partial signature match (e.g. only
// Finding 4's own anchor resolves) can still derive the shared primitive's
// address without depending on Findings 2/3 also resolving.
constexpr ptrdiff_t kCopyPrimitiveOffsetFromMatchdatadone = kCopyPrimitiveGhidraAddr - kMatchdatadoneEntryGhidraAddr; // 0x1A9FF0
constexpr ptrdiff_t kCopyPrimitiveOffsetFromFragmentReassembly = kCopyPrimitiveGhidraAddr - kFragmentReassemblyEntryGhidraAddr; // 0x1D8640
// Fixed offset from Finding 4's own anchor to the buffer-resolve function.
constexpr ptrdiff_t kBufferResolveOffsetFromFragmentReassembly = kBufferResolveGhidraAddr - kFragmentReassemblyEntryGhidraAddr; // 0x1CD190

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

// Real, fixed prefix of FUN_1400ad330 -- PUSH RDI/PUSH R13/SUB RSP,0x1e8/
// MOV RDI,RDX/MOV R13D,ECX, ending right before the first genuine CALL at
// +0x11. Only 17 bytes (shorter than the other two signatures in this file,
// since this function's first CALL comes earlier) -- independently verified
// unique across the whole binary via a purpose-built scanner
// (re_notes/ghidra_scripts/CountByteMatches.java) before shipping, exactly 1
// match, at the expected address.
constexpr char kFragmentReassemblySignature[] =
    "40 57 41 55 48 81 EC E8 01 00 00 48 8B FA 44 8B E9";

constexpr size_t kMatchdatadoneRealBufferSize = 1020; // local_428[1024] minus a safety byte,
    // matching the ORIGINAL code's own 0x3fc(1020) check exactly -- this fix makes that
    // existing check actually enforced, it does not invent a new threshold
constexpr size_t kMemberjoinRealBufferSize = 512; // local_248[512] in the decompile
constexpr uintptr_t kFragmentReassemblyRealBufferSize = 0x2ffc; // 12284 -- the SAME constant
    // the original code itself uses for its own (log-only) bounds check, confirmed via
    // decompile: `if ((0x2ffc < uVar11) || (*param_2 != 0)) { FUN_14026fab0(...warn...); }`

uintptr_t g_matchdatadoneVulnReturnAddr = 0;
uintptr_t g_memberjoinVulnReturnAddr = 0;
uintptr_t g_fragmentReassemblyVulnReturnAddr = 0;
uintptr_t g_fragmentReassemblyResolveReturnAddr = 0;

// Captured by Hook_BufferResolve, consumed by Hook_CopyPrimitive's Finding-4
// case. nullptr (the default) means "no valid buffer captured yet for this
// connection" -- Hook_CopyPrimitive treats that as unsafe-by-default and
// refuses the copy entirely, never assumes a buffer exists.
void* g_fragmentReassemblyBufferBase = nullptr;

// Real signature confirmed via decompile: FUN_140285970(ReaderStruct*, void* dest, int length)
using CopyPrimitive_t = void(__fastcall*)(void* reader, void* dest, int length);
CopyPrimitive_t g_realCopyPrimitive = nullptr;

// Real signature confirmed via decompile: FUN_14027a4c0(int connectionIndex) -> void*
// (the connection's own fixed-size reassembly buffer, or nullptr if it has none).
using BufferResolve_t = void*(__fastcall*)(int connectionIndex);
BufferResolve_t g_realBufferResolve = nullptr;

void* __fastcall Hook_BufferResolve(int connectionIndex)
{
    void* result = g_realBufferResolve(connectionIndex);
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) == g_fragmentReassemblyResolveReturnAddr) {
        g_fragmentReassemblyBufferBase = result;
    }
    return result;
}

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
    } else if (returnAddr == g_fragmentReassemblyVulnReturnAddr) {
        // dest = buffer_base + attacker_offset, already combined by the caller
        // (FUN_1400ad330) before this call -- validated against the REAL
        // captured buffer range, not trusted.
        auto destAddr = reinterpret_cast<uintptr_t>(dest);
        auto base = reinterpret_cast<uintptr_t>(g_fragmentReassemblyBufferBase);
        if (base == 0 || destAddr < base) {
            // No valid buffer captured for this connection, or dest computed
            // to somewhere before the buffer even starts (shouldn't happen --
            // the attacker offset is read as an unsigned 0-65535 value -- but
            // treated as unsafe rather than assumed impossible). Refuse.
            length = 0;
        } else {
            uintptr_t bufferEnd = base + kFragmentReassemblyRealBufferSize;
            if (destAddr >= bufferEnd) {
                // Offset already past the end of the real buffer -- nothing
                // can be safely written here at all. Refuse entirely rather
                // than attempting a negative-length clamp.
                length = 0;
            } else {
                uintptr_t remaining = bufferEnd - destAddr;
                if (static_cast<uintptr_t>(static_cast<size_t>(length)) > remaining) {
                    length = static_cast<int>(remaining);
                }
            }
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

    // All three dispatchers are iw5mp.exe-only -- a failed scan here is
    // expected, not an error, if this DLL is loaded into iw5sp.exe instead.
    SigScan::Result matchResult = SigScan::FindPattern(kMatchdatadoneSignature, base, moduleSize);
    SigScan::Result memberResult = SigScan::FindPattern(kMemberjoinSignature, base, moduleSize);
    SigScan::Result fragResult = SigScan::FindPattern(kFragmentReassemblySignature, base, moduleSize);
    if (!matchResult.found && !memberResult.found && !fragResult.found) {
        host.Log("[nsp-mp-fix] No signatures found -- expected if this is iw5sp.exe, "
                 "not a fix failure. Skipping (Findings 2/3/4 only affect iw5mp.exe).");
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
    void* bufferResolveAddr = nullptr;
    if (fragResult.found) {
        g_fragmentReassemblyVulnReturnAddr = fragResult.address + kFragmentReassemblyVulnCallReturnOffset;
        g_fragmentReassemblyResolveReturnAddr = fragResult.address + kFragmentReassemblyResolveCallReturnOffset;
        bufferResolveAddr = reinterpret_cast<void*>(fragResult.address + kBufferResolveOffsetFromFragmentReassembly);
        if (!copyPrimitiveAddr) {
            copyPrimitiveAddr = reinterpret_cast<void*>(fragResult.address + kCopyPrimitiveOffsetFromFragmentReassembly);
        }
    }
    if (!(matchResult.found && memberResult.found && fragResult.found)) {
        char buf[420];
        sprintf_s(buf, "[nsp-mp-fix] WARNING: not all three dispatchers resolved "
                  "(matchdatadone=%d, memberjoin=%d, fragment-reassembly=%d) -- any "
                  "unresolved finding's fix will not be active this session even though "
                  "this looks like iw5mp.exe. Investigate before trusting this build "
                  "against real gameplay.",
                  matchResult.found ? 1 : 0, memberResult.found ? 1 : 0, fragResult.found ? 1 : 0);
        host.Log(buf);
    }
    if (!copyPrimitiveAddr) {
        host.Log("[nsp-mp-fix] FAILED: no anchor resolved, cannot derive the shared copy "
                 "primitive's address. No fixes in this file installed.");
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

    if (bufferResolveAddr) {
        void* resolveOriginal = nullptr;
        if (!host.InstallHook(bufferResolveAddr, reinterpret_cast<void*>(&Hook_BufferResolve), &resolveOriginal)) {
            char buf[300];
            sprintf_s(buf, "[nsp-mp-fix] FAILED: InstallHook failed for Finding 4's buffer-resolve "
                      "function @ %p -- Finding 4's own fix will NOT be active this session "
                      "(no buffer base can ever be captured, so its copy-primitive case will "
                      "always refuse rather than silently trust an uncaptured buffer).",
                      bufferResolveAddr);
            host.Log(buf);
        } else {
            g_realBufferResolve = reinterpret_cast<BufferResolve_t>(resolveOriginal);
        }
    }

    char buf[600];
    sprintf_s(buf, "[nsp-mp-fix] Installed. matchdatadone vuln-call-return @ 0x%llX (clamp %zu), "
              "memberjoin vuln-call-return @ 0x%llX (clamp %zu), fragment-reassembly vuln-call-return "
              "@ 0x%llX (validated against captured buffer, real size %zu), buffer-resolve hook %s, "
              "shared copy primitive @ %p hooked.",
              static_cast<unsigned long long>(g_matchdatadoneVulnReturnAddr), kMatchdatadoneRealBufferSize,
              static_cast<unsigned long long>(g_memberjoinVulnReturnAddr), kMemberjoinRealBufferSize,
              static_cast<unsigned long long>(g_fragmentReassemblyVulnReturnAddr),
              static_cast<size_t>(kFragmentReassemblyRealBufferSize),
              g_realBufferResolve ? "active" : "NOT active",
              copyPrimitiveAddr);
    host.Log(buf);
}
