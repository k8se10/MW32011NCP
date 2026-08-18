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

// Fraction of a pulse's total duration spent HOLDING the full commanded peak before
// decaying, rather than ramping down from t=0 (2026-08-03, issue #63 round 2 -- still
// "extremely weak" after the first strength/duration bump). Real ERM vibration motors
// have genuine physical spin-up lag (~50-100ms) before reaching a speed a human can
// feel; a pure linear decay from t=0 spends a short pulse's ENTIRE duration commanding
// a strength the motor is still ramping toward, so it may barely become perceptible
// right as it's told to stop. Holding at peak first, then decaying only for the tail,
// gives the physical motor real time at the commanded strength.
constexpr float kRumbleSustainFraction = 0.6f;

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

    // Rate-limited cadence diagnostic (issue #63 round 2) -- logs the first 30 real
    // fire-rumble triggers each session so a live retest can show how often this
    // actually fires (e.g. whether a full-auto weapon retriggers faster than the
    // pulse's own duration, which would read as one sustained buzz rather than
    // distinct pulses) without flooding the log for a whole play session.
    static int s_fireRumbleLogCount = 0;
    if (s_fireRumbleLogCount < 30) {
        ++s_fireRumbleLogCount;
        char buf[128];
        sprintf_s(buf, "[rumble-diag] fire trigger #%d, intensity=%.2f durationMs=%lu",
            s_fireRumbleLogCount, g_modConfig.vibrationFireIntensity, g_modConfig.vibrationFireDurationMs);
        LogFromController(buf);
    }

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
constexpr uintptr_t kEntityStride = 0x270;
constexpr uintptr_t kLocalPlayerEntity = kEntityArrayBase; // SP is always player index 0 (re_notes/iw5sp.md)
constexpr uintptr_t kSecondPlayerEntitySlot = kEntityArrayBase + kEntityStride; // index 1 --
    // 2-player Survival's co-op partner, if present. See kLocalPlayerEntity's own
    // comment and IsRealPlayerEntity's "HONEST CAVEAT" above -- neither is actually
    // scoped to "the specific human at THIS keyboard," just "index 0" / "any real
    // client." That gap was flagged as a known, unconfirmed risk when this was
    // written; live-reported 2026-08-18 as a real, reproducible bug: "in coop it
    // triggers when the other tm8 is shot" -- damage-rumble reads index 0's health
    // unconditionally, so a client player (not the host, i.e. not index 0) gets
    // rumble driven by their TEAMMATE's health instead of their own.
constexpr int kHealthFieldOffset = 0x150;

int g_lastKnownHealth = -1; // -1 = not yet established a baseline this "session" (see reset points below)

// MITIGATION, not a fix (2026-08-18) -- finding the real "which array index is
// THIS client" mechanism needs genuine RE (a local-clientnum global or equivalent,
// not yet located -- see re_notes/iw5sp.md's own "PARKED, not abandoned" entity-
// array cross-link research for the closest existing lead) which wasn't done this
// pass, per this project's own standing rule: never hardcode/guess a value that
// hasn't actually been confirmed. Guessing "always use index 1 instead of 0" would
// just move the exact same bug onto whichever player IS at index 0 instead of
// fixing it. Until the real mechanism is found, detect when a second real player
// entity exists (2-player co-op) and disable damage-rumble entirely in that case --
// wrongly rumbling for a teammate's hits is worse than not rumbling at all, and
// fire-rumble (TriggerFireRumble, hooked directly off THIS client's own weapon-fire
// call rather than reading a hardcoded array slot) is unaffected by any of this,
// so co-op players still get rumble on their own shots, just not on damage taken.
bool SecondRealPlayerEntityPresent()
{
    return IsRealPlayerEntity(static_cast<int>(kSecondPlayerEntitySlot));
}

void PollDamageRumble()
{
    if (!g_modConfig.vibrationEnabled) return;

    if (SecondRealPlayerEntityPresent()) {
        // 2-player co-op detected -- see this file's own MITIGATION comment above.
        // Reset the baseline (same as "no real local player right now" below) so
        // health polling starts clean again if this becomes solo later (partner
        // disconnects) rather than measuring a delta against stale co-op-era data.
        g_lastKnownHealth = -1;
        return;
    }

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

// ---- ARMOR candidate scan, OFF by default (issue #63 follow-up, 2026-08-03) --------
//
// User-reported: PollDamageRumble above never fires while Survival's purchasable
// Body Armor is absorbing a hit, since armor is tracked separately from real health
// and this project has no prior research locating that separate field. Rather than
// guess an offset (this project's own standing rule -- never hardcode a value that
// hasn't actually been confirmed), this scans a window of the same per-player entity
// struct PollDamageRumble already reads (kLocalPlayerEntity, the same 0x270-stride
// struct the confirmed health field at +0x150 lives in) for a value that behaves like
// armor should: STABLE for at least two consecutive frames, then a single-frame drop
// of a plausible hit-sized amount. The stability requirement is the key filter -- it's
// what tells a real "absorbed a hit" event apart from an ordinary countdown timer
// (ammo reload clocks, cooldowns, animation timers), which are never stable
// beforehand since they tick down every single frame regardless of player action.
//
// Scoped to [0x00, 0x270) in 4-byte steps -- the same struct stride already confirmed
// for the entity array itself, so armor (if it lives in the per-player entity struct
// at all, rather than in GSC-VM-only script storage this project can't read this way)
// should fall somewhere in this window.
constexpr int kArmorScanWindowBytes = 0x270;
constexpr int kArmorScanSlotCount = kArmorScanWindowBytes / 4;
int g_armorScanPrevValue[kArmorScanSlotCount];
int g_armorScanPrevPrevValue[kArmorScanSlotCount];
bool g_armorScanPrimed = false; // needs 2 real frames of history before the stability check means anything
int g_armorScanLogLinesEmitted = 0;
constexpr int kArmorScanMaxLogLines = 300; // hard cap so a noisy candidate can't flood the log all session
int g_armorScanPerSlotCount[kArmorScanSlotCount]; // per-offset cap (see below) -- zero-initialized (global array)
constexpr int kArmorScanMaxLogsPerSlot = 3; // round 1 (100-line global cap) got entirely eaten by ONE noisy
    // offset (entity+0x58, confirmed by its own log shape -- constant, mostly drop=1, occasional resets
    // upward -- to be current ammo in the clip, not armor) before any other candidate got a chance to
    // appear at all. Capping PER OFFSET instead guarantees a spread of distinct candidates even if
    // another noisy field exists elsewhere in the window.
constexpr int kArmorScanExcludedOffset = 0x58; // confirmed ammo count (round 1 capture, 2026-08-03) -- skip
    // logging it entirely rather than waste any of the per-slot budget re-confirming what's already known.

void PollArmorFieldScanDiag()
{
    if (!g_modConfig.armorFieldScanLogging) return;
    if (g_armorScanLogLinesEmitted >= kArmorScanMaxLogLines) return;

    if (SecondRealPlayerEntityPresent()) {
        // Same co-op ambiguity as PollDamageRumble's own MITIGATION comment above --
        // this diagnostic exists to locate the real armor field by watching for a
        // stable-then-drops pattern, and candidate data from the WRONG player's
        // entity would actively mislead that search, not just be a no-op like
        // damage-rumble's own case. Skip entirely rather than log misleading
        // candidates.
        g_armorScanPrimed = false;
        return;
    }

    if (!IsRealPlayerEntity(static_cast<int>(kLocalPlayerEntity))) {
        g_armorScanPrimed = false; // no real player right now -- history is stale once one exists again
        return;
    }

    constexpr int kMinPlausibleDrop = 1;
    constexpr int kMaxPlausibleDrop = 250; // generous vs. any real single-hit weapon damage, same order as PollDamageRumble's own cap

    for (int slot = 0; slot < kArmorScanSlotCount; ++slot) {
        int offset = slot * 4;
        int current = *reinterpret_cast<volatile int*>(kLocalPlayerEntity + offset);

        if (g_armorScanPrimed && offset != kArmorScanExcludedOffset
            && g_armorScanPerSlotCount[slot] < kArmorScanMaxLogsPerSlot) {
            int prev = g_armorScanPrevValue[slot];
            int prevPrev = g_armorScanPrevPrevValue[slot];
            if (prev == prevPrev) { // stable for the 2 frames before this one
                int drop = prev - current;
                if (drop >= kMinPlausibleDrop && drop <= kMaxPlausibleDrop
                    && current >= 0 && current <= 1000
                    && g_armorScanLogLinesEmitted < kArmorScanMaxLogLines) {
                    char buf[128];
                    sprintf_s(buf, "[armor-scan-diag] entity+0x%X: %d -> %d (drop=%d)",
                        offset, prev, current, drop);
                    LogFromController(buf);
                    ++g_armorScanLogLinesEmitted;
                    ++g_armorScanPerSlotCount[slot];
                }
            }
        }

        g_armorScanPrevPrevValue[slot] = g_armorScanPrevValue[slot];
        g_armorScanPrevValue[slot] = current;
    }
    g_armorScanPrimed = true;
}

// Shared by Rumble_Tick (gameplay tick) and Rumble_TickExpiryWatchdog (menu
// tick -- see rumble.h's own comment on why this needed splitting out). Enforces
// the CURRENT event's own already-scheduled expiry (g_rumbleDecayStartMs +
// g_rumbleDecayDurationMs) and pushes the resulting motor state -- never reads
// or resets anything event-specific itself, so calling it from two different
// ticks is safe/idempotent the same way this project's other dual-tick calls
// (InjectControllerPauseMenu, InjectControllerMenuBack) already are.
void UpdateRumbleOutput()
{
    if (!g_modConfig.vibrationEnabled || g_rumblePeakIntensity <= 0.0f) return;

    DWORD elapsed = GetTickCount() - g_rumbleDecayStartMs;
    if (elapsed >= g_rumbleDecayDurationMs) {
        g_rumblePeakIntensity = 0.0f;
        Controller_SetVibration(0.0f, 0.0f);
        return;
    }

    // Sustain-then-release envelope (see kRumbleSustainFraction's own comment) --
    // hold the full commanded peak for the first portion of the pulse, only decay
    // the tail, instead of ramping down for the pulse's entire duration.
    DWORD sustainMs = static_cast<DWORD>(static_cast<float>(g_rumbleDecayDurationMs) * kRumbleSustainFraction);
    float current;
    if (elapsed < sustainMs) {
        current = g_rumblePeakIntensity;
    } else {
        DWORD decayElapsed = elapsed - sustainMs;
        DWORD decayDurationMs = g_rumbleDecayDurationMs - sustainMs;
        float remaining = decayDurationMs > 0
            ? 1.0f - (static_cast<float>(decayElapsed) / static_cast<float>(decayDurationMs))
            : 0.0f;
        current = g_rumblePeakIntensity * remaining;
    }
    Controller_SetVibration(current, current);
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
    PollArmorFieldScanDiag(); // OFF by default -- see its own comment (issue #63 follow-up)
    UpdateRumbleOutput();
}

// See rumble.h's own comment. Deliberately just UpdateRumbleOutput() -- no
// polling here, since this runs on the menu tick where the gameplay entity
// reads PollDamageRumble/PollArmorFieldScanDiag rely on aren't meaningful.
void Rumble_TickExpiryWatchdog()
{
    UpdateRumbleOutput();
}
