// p2p_fix.cpp -- Finding 1: iw5sp.exe Steamworks P2P receive-path overflow.
//
// Root cause (full trail in re_notes/vulnerability_research.md and
// re_notes/INTERNAL_vulnerability_research.md's own x64-recompile-verification
// section): FUN_1402893f0 (x64 -- x86 was FUN_00511a20) polls the real, documented
// Steamworks SDK interface ISteamNetworking in a loop. It calls the real
// IsP2PPacketAvailable (vtable slot at interface+0x8 in this x64 build) to learn
// the REMOTE PEER's own self-reported pending-packet size, then passes that value
// DIRECTLY AND WITHOUT ANY BOUNDS CHECK as the destination-buffer-size (cubDest)
// argument to the real ReadP2PPacket (vtable slot at interface+0x10), which copies
// into a fixed 1200-byte stack buffer (local_4c8[1200] in the decompile). A
// malicious P2P peer in an active Spec-Ops/Survival co-op session can report an
// inflated size to overflow that buffer.
//
// FIX DESIGN, chosen specifically to avoid reimplementing FUN_1402893f0's own
// substantial post-read message-parsing logic (calls into several other functions
// this project doesn't have full RE detail on): hook ReadP2PPacket itself, at the
// real Steamworks vtable level (the standard, correct way to intercept a COM-style
// virtual method -- this project's own sibling MW32011NCP already uses the same
// vtable-hook technique for IDirect3DDevice9::EndScene/CreateDevice). ReadP2PPacket
// is presumably called from other, legitimate, unrelated places in the game's own
// netcode too -- a BLANKET clamp on every call would risk truncating some other
// caller's genuinely larger, valid packet. So the detour is SCOPED: it inspects its
// own return address (the address execution resumes at once ReadP2PPacket returns,
// i.e. the instruction immediately after the CALL) and only clamps cubDest when
// that return address falls within FUN_1402893f0's own real function body (verified
// via Ghidra: [0x1402893f0, 0x1402895b2), 0x1C2 bytes) -- every other caller
// anywhere else in the binary passes through completely untouched.
//
// The fix itself is a one-line correction, not a rewrite: pass min(cubDest, 1200)
// to the real ReadP2PPacket instead of the attacker-reported value. This dequeues
// the pending packet correctly (Steamworks' own documented ReadP2PPacket behavior
// for a too-small destination handles a legitimate size mismatch safely -- this is
// completely standard, correct API usage, not something usable maliciously) rather
// than either crashing (the original bug) or silently leaving the oversized packet
// stuck at the head of the peer's queue forever (which a naive "just skip it"
// fix would do, itself a real DoS risk against the local player's own P2P session).

#include "netcode_fixes.h"
#include "signature_scan.h"

#include <windows.h>
#include <cstdio>
#include <intrin.h>
#include <memory>

namespace {

constexpr size_t kP2PFuncBodySize = 0x1C2; // real, Ghidra-confirmed function body size

// Real, fixed 52-byte prefix of FUN_1402893f0 -- confirmed via disassembly to be
// entirely free of PC-relative/absolute address references (every instruction in
// this range is either a fixed opcode or an RSP/RBP-relative stack displacement,
// neither of which needs wildcarding -- see signature_scan.h's own header comment
// for why a stack-relative displacement is not an "address" in the sense that
// matters here). No wildcards needed at all for this signature.
constexpr char kP2PFuncSignature[] =
    "40 55 56 48 8D AC 24 88 FB FF FF 48 81 EC 78 05 00 00 81 A5 A4 04 00 00 FF FF 0F FF "
    "33 F6 40 88 B5 A7 04 00 00 81 A5 A4 04 00 00 00 00 F0 FF 89 B5 A0 04 00 00";

constexpr size_t kRealDestBufferSize = 1200; // local_4c8[1200] in the decompile -- the
    // REAL, confirmed destination buffer size FUN_1402893f0's own stack frame reserves

uintptr_t g_p2pFuncStart = 0;

// Real Steamworks SDK signature (publicly documented, not game-specific):
//   bool ReadP2PPacket(void *pubDest, uint32 cubDest, uint32 *pcubMsgSize,
//                       CSteamID *psteamIDRemote, int nChannel = 0);
// Confirmed via this project's own decompile of the vulnerable call site that this
// x64 build's calling convention matches a plain __fastcall (this*, dest, cubDest,
// pcubMsgSize, psteamIDRemote, nChannel) -- standard x64 Steamworks ABI.
using ReadP2PPacket_t = bool(__fastcall*)(void* thisPtr, void* pubDest, uint32_t cubDest,
                                            uint32_t* pcubMsgSize, void* psteamIDRemote, int nChannel);
ReadP2PPacket_t g_realReadP2PPacket = nullptr;

bool __fastcall Hook_ReadP2PPacket(void* thisPtr, void* pubDest, uint32_t cubDest,
                                     uint32_t* pcubMsgSize, void* psteamIDRemote, int nChannel)
{
    // _ReturnAddress() -- the address execution resumes at once this detour
    // returns, i.e. the instruction immediately following the CALL that reached
    // us. Scoping on this (rather than clamping every ReadP2PPacket call
    // globally) is what keeps this fix from affecting any other, unrelated,
    // legitimate caller of the same real Steamworks interface method.
    uintptr_t returnAddr = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (g_p2pFuncStart != 0 &&
        returnAddr >= g_p2pFuncStart && returnAddr < g_p2pFuncStart + kP2PFuncBodySize) {
        if (cubDest > kRealDestBufferSize) {
            cubDest = static_cast<uint32_t>(kRealDestBufferSize);
        }
    }
    return g_realReadP2PPacket(thisPtr, pubDest, cubDest, pcubMsgSize, psteamIDRemote, nChannel);
}

// Tri-state outcome for the Steamworks-resolution-and-hook-install step, kept
// separate from the (one-time, not retry-worthy) signature scan above. Real,
// live-observed 2026-09-05: on the very first live execution of this whole
// pipeline (via NCP's greenlit plugin), SteamNetworking() came back null --
// steam_api64.dll was loaded and exported the symbol, but Steamworks itself
// wasn't initialized yet at DLL_PROCESS_ATTACH time. This project's own
// header comment for InstallNetcodeFixes() previously claimed "no need to
// wait... DLL_PROCESS_ATTACH is fine" -- true for the signature scan (the
// module is always fully mapped by then) but WRONG for anything depending on
// the game's own Steamworks init, which runs later in the game's own
// startup, well after this DLL's DllMain has already returned. See
// TryInstallP2PFixRetryLoop below for the fix.
enum class SteamResolveOutcome { Success, Retry, HardFail };

SteamResolveOutcome TryResolveSteamAndInstallHook(const FixHostServices& host)
{
    // Resolve the REAL Steamworks SDK export directly -- SteamNetworking() is a
    // genuine, publicly-documented export of steam_api64.dll, confirmed via this
    // project's own Ghidra symbol lookup to be a real EXTERNAL (not internal game
    // code) symbol. Calling it ourselves, exactly the way the game's own code
    // does, is standard, correct Steamworks SDK usage -- not reading or writing
    // any gameplay-entity memory, just the same public API surface any legitimate
    // Steamworks-integrated tool already uses.
    HMODULE steamApi = GetModuleHandleA("steam_api64.dll");
    if (!steamApi) {
        // Transient -- the game may not have loaded steam_api64.dll yet this
        // early in process startup. Worth retrying, not a hard failure.
        return SteamResolveOutcome::Retry;
    }
    using SteamNetworking_t = void*(__fastcall*)();
    auto steamNetworking = reinterpret_cast<SteamNetworking_t>(GetProcAddress(steamApi, "SteamNetworking"));
    if (!steamNetworking) {
        // The DLL is loaded but genuinely lacks the export -- a version
        // mismatch or real problem, not something a retry will fix.
        host.Log("[nsp-p2p-fix] FAILED: steam_api64.dll missing SteamNetworking export");
        return SteamResolveOutcome::HardFail;
    }
    void* iface = steamNetworking();
    if (!iface) {
        // Real, live-confirmed transient case (see this function's own header
        // comment) -- steam_api64.dll is loaded and exports the symbol, but
        // Steamworks itself hasn't finished initializing inside the game yet.
        return SteamResolveOutcome::Retry;
    }

    // Real vtable layout confirmed via this project's own decompile of the
    // vulnerable call site: IsP2PPacketAvailable at interface+0x8, ReadP2PPacket
    // at interface+0x10 (both real Valve Steamworks SDK slots, matching what the
    // game's own code already dispatches through -- see this file's own header
    // comment for the exact decompiled call).
    void** vtable = *reinterpret_cast<void***>(iface);
    void* readP2PPacketAddr = vtable[2]; // +0x10 / sizeof(void*) = index 2

    void* original = nullptr;
    if (!host.InstallHook(readP2PPacketAddr, reinterpret_cast<void*>(&Hook_ReadP2PPacket), &original)) {
        char buf[256];
        sprintf_s(buf, "[nsp-p2p-fix] FAILED: InstallHook failed for ReadP2PPacket @ %p", readP2PPacketAddr);
        host.Log(buf);
        return SteamResolveOutcome::HardFail;
    }
    g_realReadP2PPacket = reinterpret_cast<ReadP2PPacket_t>(original);

    char buf[256];
    sprintf_s(buf, "[nsp-p2p-fix] Installed. FUN_1402893f0 @ 0x%llX (body size 0x%zX), "
              "ReadP2PPacket @ %p hooked, scoped clamp to %zu bytes.",
              static_cast<unsigned long long>(g_p2pFuncStart), kP2PFuncBodySize,
              readP2PPacketAddr, kRealDestBufferSize);
    host.Log(buf);
    return SteamResolveOutcome::Success;
}

// Retry state + thread proc kept in a small heap-allocated block passed via
// lpParameter -- FixHostServices is plain function pointers (no captured
// state), safe to copy and outlive InstallP2PFix's own stack frame this way.
// Matches this codebase's existing event-driven/periodic-retry background-
// thread convention (NCP's own issue #87 architecture: a dedicated thread per
// distinct "wait for something to become ready" job, plain Sleep()-paced
// where there's no natural wake event to use instead).
struct RetryThreadArgs {
    FixHostServices host;
};

constexpr int kMaxRetryAttempts = 30;   // ~30s total at 1s apart -- generous
constexpr DWORD kRetryIntervalMs = 1000; // for real Steamworks init timing,
                                          // still bounded so a genuinely
                                          // broken/missing Steamworks doesn't
                                          // retry forever.

DWORD WINAPI P2PFixRetryThreadProc(LPVOID param)
{
    std::unique_ptr<RetryThreadArgs> args(static_cast<RetryThreadArgs*>(param));
    for (int attempt = 1; attempt <= kMaxRetryAttempts; ++attempt) {
        Sleep(kRetryIntervalMs);
        SteamResolveOutcome outcome = TryResolveSteamAndInstallHook(args->host);
        if (outcome == SteamResolveOutcome::Success) {
            char buf[192];
            sprintf_s(buf, "[nsp-p2p-fix] Steamworks became ready on retry attempt %d/%d.",
                      attempt, kMaxRetryAttempts);
            args->host.Log(buf);
            return 0;
        }
        if (outcome == SteamResolveOutcome::HardFail) {
            return 0; // TryResolveSteamAndInstallHook already logged the reason
        }
        // Retry: keep looping.
    }
    char buf[192];
    sprintf_s(buf, "[nsp-p2p-fix] FAILED: Steamworks never became ready after %d attempts "
              "(~%d sec) -- giving up. Finding 1's fix is NOT active this session.",
              kMaxRetryAttempts, kMaxRetryAttempts * static_cast<int>(kRetryIntervalMs / 1000));
    args->host.Log(buf);
    return 0;
}

} // namespace

void InstallP2PFix(const FixHostServices& host)
{
    void* moduleBase = host.GetGameModuleBase();
    if (!moduleBase) {
        host.Log("[nsp-p2p-fix] FAILED: no game module base -- cannot resolve FUN_1402893f0");
        return;
    }

    // Only iw5sp.exe (Campaign/Survival/Spec-Ops) has this vulnerable function --
    // a failed scan on iw5mp.exe is EXPECTED, not an error, since this DLL may be
    // loaded into either binary. Log at info level, not FATAL.
    auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
    auto ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        reinterpret_cast<uintptr_t>(moduleBase) + dosHeader->e_lfanew);
    size_t moduleSize = ntHeaders->OptionalHeader.SizeOfImage;

    SigScan::Result funcResult = SigScan::FindPattern(
        kP2PFuncSignature, reinterpret_cast<uintptr_t>(moduleBase), moduleSize);
    if (!funcResult.found) {
        host.Log("[nsp-p2p-fix] Signature not found -- expected if this is iw5mp.exe, "
                 "not a fix failure. Skipping (Finding 1 only affects iw5sp.exe).");
        return;
    }
    g_p2pFuncStart = funcResult.address;

    // Fast path: try once synchronously first (covers the case where
    // Steamworks is already up by the time this runs -- no need to pay for a
    // thread + a full second's delay in the common case once this project's
    // own load-ordering settles). Only fall back to the retry thread if this
    // first attempt reports a genuinely transient "not ready yet" condition.
    SteamResolveOutcome outcome = TryResolveSteamAndInstallHook(host);
    if (outcome == SteamResolveOutcome::Success || outcome == SteamResolveOutcome::HardFail) {
        return; // Success already logged its own confirmation; HardFail already logged why.
    }

    host.Log("[nsp-p2p-fix] Steamworks not ready yet (steam_api64.dll not loaded, or "
             "SteamNetworking() returned null) -- starting a background retry (1/sec, "
             "up to 30sec) instead of giving up. Real, live-observed timing issue "
             "(2026-09-05): DLL_PROCESS_ATTACH runs before the game's own Steamworks init.");
    auto* args = new RetryThreadArgs{host};
    HANDLE thread = CreateThread(nullptr, 0, &P2PFixRetryThreadProc, args, 0, nullptr);
    if (!thread) {
        host.Log("[nsp-p2p-fix] FAILED: CreateThread for the Steamworks retry loop itself failed");
        delete args;
        return;
    }
    CloseHandle(thread); // detached -- the thread proc owns `args` and frees it (unique_ptr) on exit
}
