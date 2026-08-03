#include "rumble.h"

#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "../third_party/minhook/include/MinHook.h"
#include "controller_input.h"
#include "mod_config.h"

extern void LogFromController(const char* msg); // defined in dllmain.cpp

namespace {

// ---- Byte-pattern signature scan (2026-08-03, issue #24 reimplementation) --------
//
// Per CLAUDE.md: never hardcode a raw hook-target address -- scan for it at runtime.
// This is the first such scanner in this codebase (every other existing hook still
// hardcodes its VA directly against the non-ASLR 0x00400000 default load base, a
// known pre-existing gap this one function doesn't retroactively fix elsewhere).
// Scans the main module's own memory image (this DLL is loaded into the game's own
// process, so GetModuleHandle(nullptr) IS the game .exe itself) for a literal byte
// sequence. No wildcards needed: FUN_0045e320's confirmed prologue (stack-relative
// MOV/TEST instructions, no embedded absolute addresses) is fully position-
// independent as raw bytes -- verified by dumping the actual bytes via Ghidra
// headless and diffing them against the already-confirmed disassembly, not typed
// from mnemonics by hand.
uintptr_t FindPatternInMainModule(const unsigned char* pattern, size_t patternLen)
{
    HMODULE hMod = GetModuleHandleA(nullptr);
    if (!hMod) return 0;
    auto base = reinterpret_cast<uintptr_t>(hMod);
    auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    auto ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return 0;
    size_t imageSize = ntHeaders->OptionalHeader.SizeOfImage;

    if (patternLen == 0 || imageSize < patternLen) return 0;
    const auto* data = reinterpret_cast<const unsigned char*>(base);
    for (size_t i = 0; i + patternLen <= imageSize; ++i) {
        if (memcmp(data + i, pattern, patternLen) == 0) {
            return base + i;
        }
    }
    return 0;
}

// ---- FIRE rumble: FUN_0045e320 (per-shot fire-effects handler) -------------------
//
// re_notes/known_issues.md issue #24 history: the ORIGINAL implementation hooked
// FUN_004895b0 (the generic multi-purpose notify dispatcher) directly and crashed
// the game at startup -- some OTHER real caller of that shared dispatcher (not
// identified) almost certainly passes a genuinely different real argument shape
// than this hook's fixed 3-arg signature assumed. FUN_0045e320 is the single,
// specific caller that invokes it with the real "weapon_fired" event -- confirmed
// safe to hook DIRECTLY (2026-08-03 re-verification, this session): its own
// decompiled 2-parameter signature was cross-checked against the RAW disassembly
// of its one real call site (FUN_005b68c0 @ 0x005b6991) via a fresh Ghidra headless
// pass (DumpCallSitePushCounts.java) -- exactly 2 real PUSH instructions immediately
// precede the CALL, matching FUN_0045e320's own 2-parameter signature exactly, and
// the callee itself ends in a bare RET (caller cleanup), consistent __cdecl. This is
// the SAME rigor level the original hooks skipped (trusting decompiler pseudocode
// over raw disassembly) -- not repeating that mistake here.
//
// param_2 is read by FUN_0045e320's decompiled body nowhere at all (a dead/unused
// slot from this function's own perspective) -- its real value doesn't need to be
// interpreted, only correctly forwarded to preserve the real stack layout.
typedef void(__cdecl* FireEffects_t)(int entity, unsigned int unusedParam2);
FireEffects_t g_origFireEffects = nullptr;

// First 31 bytes of FUN_0045e320's real prologue, dumped directly via Ghidra
// headless (DumpFunctionBytes.java) from the live binary -- not hand-encoded from
// assembly mnemonics, to avoid a transcription mistake in exactly the kind of
// safety-critical hook this project has already been burned by once. Ends right
// before the JZ's own 1-byte relative displacement (which IS position-dependent --
// deliberately excluded rather than wildcarded, since 31 literal bytes are already
// far more than enough to be unique in an ~8MB image).
constexpr unsigned char kFireEffectsSig[] = {
    0x81, 0xEC, 0x74, 0x04, 0x00, 0x00,             // SUB ESP,0x474
    0x55,                                            // PUSH EBP
    0x57,                                            // PUSH EDI
    0x8B, 0xBC, 0x24, 0x80, 0x04, 0x00, 0x00,       // MOV EDI,[ESP+0x480]
    0x8B, 0xAF, 0x0C, 0x01, 0x00, 0x00,             // MOV EBP,[EDI+0x10C]
    0xF7, 0x85, 0xAC, 0x00, 0x00, 0x00,             // TEST dword ptr [EBP+0xAC],...
    0x00, 0x18, 0x20, 0x00                          // ...0x00201800
};

// ---- Local-player filter --------------------------------------------------------
//
// Fire-rumble and damage-poll both need "is this entity a real player, not AI."
// Resolved via a field this project ALREADY treats as a real "does this entity have
// a client struct" gate: entity+0x10c, confirmed non-null-checked by FUN_005BC9A0
// (the real native notifyonplayercommand registration function, known_issues.md
// issue #29) as its own precondition for "is this a real player entity, not AI."
//
// HONEST CAVEAT: in solo SP/Survival (this project's only currently-supported
// configuration) there is exactly one client entity, so "has a non-null client
// struct" is equivalent to "is the local player." This is NOT scoped to specifically
// exclude a co-op partner's entity in 2-player Survival -- a second real client would
// also pass this check. Not resolved this pass, documented rather than silently
// assumed away (unchanged from the original research).
bool IsRealPlayerEntity(int entityPtr)
{
    if (!entityPtr) return false;
    return *reinterpret_cast<volatile int*>(entityPtr + 0x10c) != 0;
}

// ---- Rumble decay state, same GetTickCount()-based timer style already established
// by InjectControllerSprint's stamina/cooldown timer elsewhere in this codebase -----
DWORD g_rumbleDecayStartMs = 0;
DWORD g_rumbleDecayDurationMs = 0;
float g_rumblePeakIntensity = 0.0f;

// A stronger/longer pulse arriving while an earlier one is still decaying takes over
// (peak intensity + a fresh decay window) rather than being additive or getting cut
// short -- simple, predictable behavior for what is, honestly, a single-shared-motor-
// pair implementation (both motors driven equally; this engine's own left/right
// motor semantics -- low-frequency vs. high-frequency -- weren't differentiated per
// event type this pass, a reasonable v1 simplification, not a placeholder).
void TriggerRumble(float intensity, unsigned long durationMs)
{
    if (intensity <= 0.0f || durationMs == 0) return;
    if (intensity < g_rumblePeakIntensity) return; // a weaker pulse doesn't interrupt a stronger one already decaying
    g_rumblePeakIntensity = intensity;
    g_rumbleDecayStartMs = GetTickCount();
    g_rumbleDecayDurationMs = durationMs;
}

void __cdecl Hook_FireEffects(int entity, unsigned int unusedParam2)
{
    g_origFireEffects(entity, unusedParam2);

    if (!g_modConfig.vibrationEnabled) return;
    if (!IsRealPlayerEntity(entity)) return;
    // Mirrors FUN_0045e320's OWN internal gate for whether this specific call
    // actually represents a real weapon-fire event: `local_410 = *(byte*)(entity+0x7c)`
    // must be non-zero (confirmed via decompile -- this is the SAME byte the real
    // function itself checks before doing anything, including its own real
    // "weapon_fired" notify call). FUN_0045e320 is reached via 8 different notify-
    // dispatch case values from its one real caller, not all of which necessarily
    // reach the fire-notify branch internally -- re-deriving this same real gate
    // ourselves (a plain read, no extra native call) avoids rumbling on whichever
    // of those 8 cases DON'T represent an actual shot.
    if (*reinterpret_cast<volatile unsigned char*>(entity + 0x7c) == 0) return;

    TriggerRumble(g_modConfig.vibrationFireIntensity, g_modConfig.vibrationFireDurationMs);
}

// ---- DAMAGE rumble: per-frame health poll, NOT a hook -----------------------------
//
// re_notes/known_issues.md issue #24: the original implementation hooked
// FUN_0044cdb0 (generic notify dispatcher) directly and crashed the game. The
// documented "safer" replacement candidate, FUN_0045f770 (the real damage-
// application function, single semantic purpose), was re-verified this session via
// the SAME raw-disassembly-of-every-real-call-site rigor used for the fire hook
// above -- and FAILED it: across its 14 real call sites, the real PUSH count
// immediately preceding each CALL ranges from 6 to 11 (not a consistent count
// matching its own 13-parameter decompiled signature). This is the EXACT SAME "some
// caller passes a genuinely different real argument shape" risk class that crashed
// the game the first time, just one layer deeper than originally thought -- the
// 2026-08-03 "GO" verdict trusted the decompiler's uniform-looking signature guess
// rather than counting real per-call-site pushes, which this session's fresh Ghidra
// headless pass (DumpCallSitePushCounts.java) now does. **FUN_0045f770 is NOT hooked
// -- concluded unsafe, not attempted.**
//
// Real fix: detect "took damage" a completely different way that needs no function
// hook at all -- poll the local player's own real health field (entity+0x150,
// already an established real field in this project's own research, same
// entity-struct family as the 0x01197AD8 array) once per real gameplay frame and
// compare against the previous frame's value. A real decrease is damage; anything
// else (regen, respawn, a scripted reset) is explicitly filtered out below. This
// sidesteps the multi-caller-inconsistent-signature problem entirely since it
// never calls into or hooks any game code for the damage side at all -- pure
// read-only memory polling.
constexpr uintptr_t kEntityArrayBase = 0x01197AD8; // per-player entity array, 0x270 stride (re_notes/iw5sp.md)
constexpr uintptr_t kLocalPlayerEntity = kEntityArrayBase; // SP is always player index 0 (re_notes/iw5sp.md)
constexpr int kHealthFieldOffset = 0x150;

int g_lastKnownHealth = -1; // -1 = not yet established a baseline this "session" (see reset points below)

void PollDamageRumble()
{
    if (!g_modConfig.vibrationEnabled) return;

    if (!IsRealPlayerEntity(static_cast<int>(kLocalPlayerEntity))) {
        // No real local player right now (main menu, loading, between lives) --
        // reset the baseline so a later real reading isn't measured as a delta
        // against stale data from a previous life/level.
        g_lastKnownHealth = -1;
        return;
    }

    int health = *reinterpret_cast<volatile int*>(kLocalPlayerEntity + kHealthFieldOffset);

    // Sanity bound: real health is a small positive int in practice. Anything wildly
    // outside that range means this read landed on a transitional/garbage state
    // (mid-respawn, entity slot being reinitialized) and shouldn't be trusted or
    // used to establish a baseline.
    constexpr int kMaxPlausibleHealth = 1000;
    if (health < 0 || health > kMaxPlausibleHealth) {
        g_lastKnownHealth = -1;
        return;
    }

    if (g_lastKnownHealth < 0) {
        // First real reading since a reset (level start, respawn, checkpoint, or
        // just became a valid player again) -- establish a baseline only, don't
        // treat this as a delta yet. Without this, spawning at (say) 80/100 health
        // would otherwise register as an 80-point "hit" the instant polling resumes.
        g_lastKnownHealth = health;
        return;
    }

    int delta = g_lastKnownHealth - health;
    g_lastKnownHealth = health;

    // Explicit false-positive guards, per design requirement:
    if (delta <= 0) return; // health INCREASED or unchanged -- regen/perk/pickup, never damage
    constexpr int kMaxPlausibleSingleFrameDamage = 200; // generous vs. any real single-hit weapon damage
    if (delta > kMaxPlausibleSingleFrameDamage) return; // a drop this large in one frame reads as a checkpoint/respawn health RESET, not a real hit

    float intensity = static_cast<float>(delta) * g_modConfig.vibrationDamagePerPoint;
    if (intensity > g_modConfig.vibrationDamageMaxIntensity) {
        intensity = g_modConfig.vibrationDamageMaxIntensity;
    }
    TriggerRumble(intensity, g_modConfig.vibrationDamageDurationMs);
}

} // namespace

void Rumble_Install()
{
    char buf[220];

    uintptr_t fireAddr = FindPatternInMainModule(kFireEffectsSig, sizeof(kFireEffectsSig));
    if (fireAddr == 0) {
        LogFromController("[rumble] FIRE hook signature scan FAILED -- pattern not found, hook NOT installed (fire rumble disabled this session)");
        return;
    }
    sprintf_s(buf, "[rumble] FIRE hook signature scan OK -- found at 0x%p (FUN_0045e320 in the original static analysis)",
        reinterpret_cast<void*>(fireAddr));
    LogFromController(buf);

    MH_STATUS s1 = MH_CreateHook(reinterpret_cast<LPVOID>(fireAddr),
        reinterpret_cast<LPVOID>(&Hook_FireEffects), reinterpret_cast<LPVOID*>(&g_origFireEffects));
    sprintf_s(buf, "[rumble] MH_CreateHook(fire @ 0x%p) = %d", reinterpret_cast<void*>(fireAddr), static_cast<int>(s1));
    LogFromController(buf);
    if (s1 == MH_OK) {
        MH_STATUS e1 = MH_EnableHook(reinterpret_cast<LPVOID>(fireAddr));
        sprintf_s(buf, "[rumble] MH_EnableHook(fire) = %d", static_cast<int>(e1));
        LogFromController(buf);
    }

    // No damage hook installed -- see PollDamageRumble's own big comment. Damage
    // rumble is driven entirely by Rumble_Tick()'s per-frame poll below, not a hook.
    LogFromController("[rumble] DAMAGE rumble uses a per-frame health poll, not a hook (FUN_0045f770 confirmed unsafe to hook -- inconsistent real argument counts across its 14 real call sites, see known_issues.md issue #24)");
}

void Rumble_Tick()
{
    PollDamageRumble();

    if (!g_modConfig.vibrationEnabled || g_rumblePeakIntensity <= 0.0f) return;

    DWORD elapsed = GetTickCount() - g_rumbleDecayStartMs;
    if (elapsed >= g_rumbleDecayDurationMs) {
        g_rumblePeakIntensity = 0.0f;
        Controller_SetVibration(0.0f, 0.0f);
        return;
    }

    float remaining = 1.0f - (static_cast<float>(elapsed) / static_cast<float>(g_rumbleDecayDurationMs));
    float current = g_rumblePeakIntensity * remaining;
    Controller_SetVibration(current, current);
}
