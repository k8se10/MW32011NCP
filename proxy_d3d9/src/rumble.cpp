#include "rumble.h"

#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "../third_party/minhook/include/MinHook.h"
#include "controller_input.h"
#include "mod_config.h"
#include "signature_scan.h" // x64 port only (2026-09-12) -- signature.cpp compiles for both
                             // platforms already (per proxy_d3d9.vcxproj's own comment), the
                             // x86 half of this file below still uses its own local
                             // FindPatternInMainModule() and never calls into this.

extern void LogFromController(const char* msg); // defined in dllmain.cpp

namespace {

// x86-only from here through IsRealPlayerEntity below (2026-09-12, x64 port) -- every
// one of these reads a HARDCODED x86 address/offset (0x10c, the local FindPatternInMainModule's
// own non-ASLR-base assumption baked into its comment, etc.) that is NOT valid on x64 --
// entity struct offsets shifted for real (confirmed via Ghidra: the "has client struct"
// pointer moved 0x10c -> 0x110, and it's now an 8-byte pointer not a 4-byte int) and this
// hand-rolled scanner duplicates what signature_scan.cpp already does generically. Left
// unguarded before this pass purely by omission -- Rumble_Install()/Rumble_Tick() were
// simply never CALLED from x64 code at all (the actual root cause of the "vibration 100%
// unported" gap, known_issues_x64.md's "Corrected gap list, 2026-09-12" entry), so this
// compiling-but-dead code never actually ran against a live x64 process. Guarding it now
// anyway, matching this project's own established landmine-prevention convention
// (analog_input_hooks.cpp/overlay_hud.cpp's own 2026-09-04 audits) -- an x86-address read
// left reachable on x64 is exactly the bug class that already caused two real startup
// crashes elsewhere in this codebase, and Rumble_Tick() is about to start actually being
// called every x64 gameplay frame.
#if !defined(_M_X64) && !defined(_WIN64)

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

#endif // !_M_X64 && !_WIN64 (x86-only block above)

// ---- Rumble decay state, same GetTickCount()-based timer style already established
// by InjectControllerSprint's stamina/cooldown timer elsewhere in this codebase -----
// SHARED across both platforms -- pure timer/math, no raw memory addresses.
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

// x86-only from here through PollArmorFieldScanDiag below (2026-09-12, x64 port) --
// same rationale as the earlier x86-only guard above: hardcoded x86 entity-struct
// addresses/offsets (kEntityArrayBase, +0x150 health, IsRealPlayerEntity's +0x10c),
// none valid on x64. See this file's own x64 block below (after PollArmorFieldScanDiag)
// for the x64 equivalents.
#if !defined(_M_X64) && !defined(_WIN64)

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

#endif // !_M_X64 && !_WIN64 (x86-only block above)

// ==================== x64 PORT (2026-09-12) =========================================
//
// Ports both real x86 mechanisms above -- the FIRE hook and the per-frame DAMAGE health
// poll -- to x64. Root cause of the gap this closes: Rumble_Install()/Rumble_Tick() were
// simply never CALLED from any x64 code path at all (InstallAnalogInputHooks(), the only
// x86 caller of Rumble_Install(), is entirely wrapped in
// `#if !defined(_M_X64) && !defined(_WIN64)` -- see known_issues_x64.md's "Corrected gap
// list, 2026-09-12" entry for the full context). This block resolves BOTH x64 targets via
// this project's own locked signature-scanning policy (CLAUDE.md SS5/SS10.3, the same
// `signature_scan.h`/analog_input_hooks_x64.cpp conventions every other x64 hook already
// follows) -- never a hardcoded address, resolved once at startup and cached.
//
// RE trail (full raw Ghidra output preserved under re_notes/x64_migration/rumble_scratch/
// for this pass): started from the SAME anchor the x86 research used originally --
// the interned GSC notify-event-name table (this binary's own x64 equivalent of
// FUN_00470d00, found at FUN_1401795b0, contains the literal `"weapon_fired"`/`"damage"`
// string args to its own interning calls). `FindGlobalRefs` on the resulting handle
// globals (DAT_1413b839e = weapon_fired, DAT_1413b82aa = damage) found the real x64
// consumer functions directly, the same "hash string -> handle -> xref its reads"
// technique re_notes/iw5sp.md's own "Vibration/rumble trigger points" section used for x86.
#if defined(_M_X64) || defined(_WIN64)

// ---- FIRE rumble, x64: FUN_14016bf50 -----------------------------------------------
//
// Confirmed as the real x64 equivalent of x86's FUN_0045e320, independently, three
// separate ways (not just "it reads the weapon_fired handle," which alone would be
// weak evidence -- TWO functions read that handle, this is the one that survives every
// further check):
//   1. Its own gate matches x86's BYTE FOR BYTE: `TEST dword ptr [client+0xac],0x201800`
//      -- the exact same 0x00201800 mask x86's own kFireEffectsSig ends on, read off a
//      "client struct" pointer at entity+0x110 (x86: entity+0x10c -- shifted by the
//      recompile, same role, now an 8-byte pointer not a 4-byte int). Then reads
//      entity+0x7c as its own internal fire-gate byte -- the SAME OFFSET as x86 (0x7c),
//      unusual enough to not be a coincidence given the mask already matched exactly.
//   2. Its one real caller, FUN_14011f320 (the x64 equivalent of x86's FUN_005b68c0),
//      reaches this exact call site from 8 distinct notify-dispatch case values
//      (0x24/0x25/0x29/0x2a/0x34/0x35/0x36/0x37) -- matching x86's own documented "8
//      different notify-dispatch case values from its one real caller" precisely.
//   3. The call site itself (re_notes/x64_migration/rumble_scratch/disasm_14011f320.txt)
//      is a single physical CALL reached via a jump table, `MOV EDX,[R15]` /
//      `MOV RCX,RDI` / `CALL FUN_14016bf50` -- standard x64 fastcall, 2 args, matching
//      the confirmed 2-parameter decompiled signature exactly (RCX=entity, EDX=param2,
//      param2 unused by the callee's own body, same as x86's unusedParam2).
//
// Signature: 42 literal bytes of FUN_14016bf50's real prologue (dumped via
// DumpSigBytes.java from the live x64 binary, not hand-encoded), through and including
// the TEST's own 0x00201800 immediate -- no wildcards needed (the one apparent
// PC-relative flag DumpSigBytes.java raised in this range, the `LEA RBP,[RSP-0x448]` at
// +0x05, is RSP-relative, a fixed stack displacement not an address -- the exact same
// false-positive class this project's kPmoveTickSignature comment already documents;
// left literal, matching that established precedent). Cut short right before the
// function's first conditional jump (`JZ`), the same "stop before a relative jump
// displacement, don't embed or wildcard it" choice x86's own kFireEffectsSig comment
// documents ("Ends right before the JZ's own 1-byte relative displacement"). Confirmed
// unique in the whole module via a direct pattern-count check
// (re_notes/x64_migration/rumble_scratch/patcheck_fire_x64.txt -- exactly 1 match, at
// FUN_14016bf50 itself).
constexpr const char* kFireEffectsSigX64 =
    "40 55 53 56 57 48 8D AC 24 B8 FB FF FF 48 81 EC 48 05 00 00 "
    "48 8B B1 10 01 00 00 8B DA 48 8B F9 F7 86 AC 00 00 00 00 18 20 00";

using FireEffectsX64Fn = void(__fastcall*)(void* entity, unsigned int unusedParam2);
FireEffectsX64Fn g_origFireEffectsX64 = nullptr;

// ---- Entity array, x64: DAT_140f57cf0, stride 0x2a0 ---------------------------------
//
// x86's own local-player entity lives at a fixed array base (0x01197AD8, stride 0x270,
// "SP is always player index 0" -- re_notes/iw5sp.md). Found the x64 equivalent by
// tracing FUN_14016bf50's own entity-pointer argument up its real call chain:
// FUN_14011f320 (fire dispatcher) <- FUN_14011f8e0 (per-player "think"/Pmove-orchestration
// function -- confirmed by its own call to FUN_140016620, the ALREADY-confirmed x64
// Pmove frame-subdivision wrapper Sprint's own hook chain runs through) <- FUN_14011f850,
// which computes the entity pointer directly: `FUN_14011f8e0(&DAT_140f57cf0 +
// playerIndex*0x2a0)` (re_notes/x64_migration/rumble_scratch/callers_14011f8e0_x64.txt).
// This is exactly x86's own "&entityArrayBase + playerIndex*stride" shape. A SECOND,
// independent function reading the weapon_fired handle (FUN_1402e4210, NOT the one
// hooked above) does the identical `&DAT_140f57cf0 + entityNumber*0x2a0` computation
// from a raw entity NUMBER rather than a pointer -- corroborating this is genuinely the
// global entity-number-indexed array, not a per-caller-local coincidence. A THIRD
// independent confirmation: FUN_14012c280 (one of the real x64 "damage" handle
// consumers, the x64 equivalent of x86's alternate scripted/melee damage paths) reads
// `param[0x110]`/`param[0x7c]`/`param[0x130]` directly off ITS OWN entity-pointer
// parameters -- the same field offsets confirmed above, on pointers from the same family.
//
// Per CLAUDE.md SS5/SS10.3, this address is NOT hardcoded directly -- resolved via a
// RIP-relative LEA inside FUN_14011f850 (`LEA RAX,[DAT_140f57cf0]`), anchored on a
// signature covering that whole tiny wrapper function's real prologue (through the LEA's
// own opcode+ModRM, disp32 wildcarded) -- confirmed unique in the module
// (re_notes/x64_migration/rumble_scratch/patcheck_entityarray_x64.txt, 1 match). The
// embedded `A0 02 00 00` mid-signature is the literal `IMUL RBX,RAX,0x2a0` immediate --
// the real per-player stride, baked directly into the same bytes that anchor the
// address resolve, not a separately-guessed constant.
constexpr const char* kEntityArrayAnchorSigX64 =
    "40 53 48 83 EC 20 48 63 C1 48 69 D8 A0 02 00 00 48 8D 05 ?? ?? ?? ??";
constexpr ptrdiff_t kEntityArrayLeaInsnOffset = 0x10; // byte offset of the LEA within the signature match
constexpr size_t kEntityArrayLeaInsnLength = 7;       // 48 8D 05 + disp32
constexpr uintptr_t kEntityStrideX64 = 0x2a0;         // confirmed via the IMUL immediate above

uint8_t* g_entityArrayBaseX64 = nullptr; // resolved once at startup, cached (SS10.3) -- index 0 = local player (SP)

// entity+0x110: the same "client struct" pointer field FUN_14016bf50's own fire-gate
// TEST reads (see kFireEffectsSigX64's own comment) -- x86's IsRealPlayerEntity
// equivalent, now an 8-byte pointer (x86 was a 4-byte int at +0x10c). Same HONEST
// CAVEAT as x86's own IsRealPlayerEntity: "has a non-null client struct" is equivalent
// to "is the local player" only in solo SP/Survival (this project's only currently-
// supported x64 configuration) -- not scoped to exclude a co-op partner specifically.
constexpr int kClientPtrOffsetX64 = 0x110;
constexpr int kFireGateByteOffsetX64 = 0x7c; // same offset as x86, independently confirmed (see kFireEffectsSigX64 comment)

// entity+0x16c: x86's health field was +0x150 -- CONFIRMED SHIFTED for x64, not assumed
// to carry over (per CLAUDE.md's "don't assume any x86 offset/address carries over"
// standard). Found via the x64 equivalent of x86's own confirmation method: FUN_1402d19b0
// (the real x64 "damage" handle consumer, matching x86 FUN_0045f770's own 13-parameter
// signature exactly) does `*(int*)(param_1+0x16c) -= amount` immediately before its own
// call to the "damage" notify dispatcher (DAT_1413b82aa) and, on health reaching <= 0, a
// "death" notify (DAT_1413b82ae) -- the same real shape x86's FUN_0045f770 has. Like x86's
// own FUN_0045f770, FUN_1402d19b0 is NOT hooked -- only used to identify this offset; its
// many real call sites carry the same "inconsistent argument shape across callers" risk
// x86's own research already ruled out hooking for (re_notes/known_issues.md issue #24).
constexpr int kHealthFieldOffsetX64 = 0x16c;

bool IsRealPlayerEntityX64(uint8_t* entity)
{
    if (!entity) return false;
    return *reinterpret_cast<uint64_t volatile*>(entity + kClientPtrOffsetX64) != 0;
}

uint8_t* LocalPlayerEntityX64()
{
    return g_entityArrayBaseX64; // index 0 -- SP is always player index 0, same as x86
}

uint8_t* SecondPlayerEntityX64()
{
    if (!g_entityArrayBaseX64) return nullptr;
    return g_entityArrayBaseX64 + kEntityStrideX64; // index 1 -- 2-player Survival co-op partner, if present
}

// Same MITIGATION as x86's own SecondRealPlayerEntityPresent -- see that function's own
// comment (in the x86 block above) for the full "in coop it triggers when the other tm8
// is shot" history. Not re-solved for x64 either (same real, unconfirmed "which array
// index is THIS client" mechanism gap x86 has); mirrored here so x64 damage-rumble
// doesn't ship the same known co-op bug x86 already has a name for.
bool SecondRealPlayerEntityPresentX64()
{
    return IsRealPlayerEntityX64(SecondPlayerEntityX64());
}

void __fastcall Hook_FireEffectsX64(void* entityVoid, unsigned int unusedParam2)
{
    g_origFireEffectsX64(entityVoid, unusedParam2);

    if (!g_modConfig.vibrationEnabled) return;
    uint8_t* entity = reinterpret_cast<uint8_t*>(entityVoid);
    if (!IsRealPlayerEntityX64(entity)) return;
    // Mirrors FUN_14016bf50's OWN internal fire-gate byte (see kFireEffectsSigX64's own
    // comment) -- same rationale as x86's Hook_FireEffects: re-derive the real gate
    // ourselves rather than rumbling on whichever of the 8 real dispatch cases this
    // function is reachable from DON'T represent an actual shot.
    if (*reinterpret_cast<volatile unsigned char*>(entity + kFireGateByteOffsetX64) == 0) return;

    static int s_fireRumbleLogCountX64 = 0;
    if (s_fireRumbleLogCountX64 < 30) {
        ++s_fireRumbleLogCountX64;
        char buf[160];
        sprintf_s(buf, "[rumble-x64-diag] fire trigger #%d, intensity=%.2f durationMs=%lu",
            s_fireRumbleLogCountX64, g_modConfig.vibrationFireIntensity, g_modConfig.vibrationFireDurationMs);
        LogFromController(buf);
    }

    TriggerRumble(g_modConfig.vibrationFireIntensity, g_modConfig.vibrationFireDurationMs);
}

int g_lastKnownHealthX64 = -1; // -1 = not yet established a baseline this "session" (same convention as x86)

void PollDamageRumbleX64()
{
    if (!g_modConfig.vibrationEnabled) return;
    if (!g_entityArrayBaseX64) return; // signature scan failed at startup -- nothing to poll

    if (SecondRealPlayerEntityPresentX64()) {
        g_lastKnownHealthX64 = -1;
        return;
    }

    uint8_t* local = LocalPlayerEntityX64();
    if (!IsRealPlayerEntityX64(local)) {
        g_lastKnownHealthX64 = -1;
        return;
    }

    int health = *reinterpret_cast<volatile int*>(local + kHealthFieldOffsetX64);

    constexpr int kMaxPlausibleHealth = 1000; // same sanity bound as x86's own PollDamageRumble
    if (health < 0 || health > kMaxPlausibleHealth) {
        g_lastKnownHealthX64 = -1;
        return;
    }

    if (g_lastKnownHealthX64 < 0) {
        g_lastKnownHealthX64 = health;
        return;
    }

    int delta = g_lastKnownHealthX64 - health;
    g_lastKnownHealthX64 = health;

    if (delta <= 0) return; // health INCREASED or unchanged -- regen/perk/pickup, never damage
    constexpr int kMaxPlausibleSingleFrameDamage = 200; // same bound as x86 -- filters checkpoint/respawn resets
    if (delta > kMaxPlausibleSingleFrameDamage) return;

    float intensity = static_cast<float>(delta) * g_modConfig.vibrationDamagePerPoint;
    if (intensity > g_modConfig.vibrationDamageMaxIntensity) {
        intensity = g_modConfig.vibrationDamageMaxIntensity;
    }
    TriggerRumble(intensity, g_modConfig.vibrationDamageDurationMs);
}

// Armor-field scan diagnostic (x86's own PollArmorFieldScanDiag) is deliberately NOT
// ported this pass -- it's an off-by-default exploratory RE tool (issue #63 follow-up),
// not one of the two real shipped mechanisms this task scopes (fire hook + damage poll).
// g_modConfig.armorFieldScanLogging simply has no effect on x64 -- honest gap, not a
// silent one; a future pass can port it the same way if the armor field is ever needed.

void InstallFireHookX64()
{
    char buf[256];

    SigScan::Result r = SigScan::FindPatternInMainModule(kFireEffectsSigX64);
    if (!r.found) {
        LogFromController("[rumble-x64] FIRE hook signature scan FAILED -- pattern not found, hook NOT installed "
            "(fire rumble disabled this session)");
        return;
    }
    sprintf_s(buf, "[rumble-x64] FIRE hook signature scan OK -- found at 0x%p (FUN_14016bf50 in the original static analysis)",
        reinterpret_cast<void*>(r.address));
    LogFromController(buf);

    MH_STATUS s1 = MH_CreateHook(reinterpret_cast<LPVOID>(r.address),
        reinterpret_cast<LPVOID>(&Hook_FireEffectsX64), reinterpret_cast<LPVOID*>(&g_origFireEffectsX64));
    sprintf_s(buf, "[rumble-x64] MH_CreateHook(fire @ 0x%p) = %d", reinterpret_cast<void*>(r.address), static_cast<int>(s1));
    LogFromController(buf);
    if (s1 == MH_OK) {
        MH_STATUS e1 = MH_EnableHook(reinterpret_cast<LPVOID>(r.address));
        sprintf_s(buf, "[rumble-x64] MH_EnableHook(fire) = %d", static_cast<int>(e1));
        LogFromController(buf);
    }
}

void ResolveEntityArrayX64()
{
    char buf[256];

    SigScan::Result r = SigScan::FindPatternInMainModule(kEntityArrayAnchorSigX64);
    if (!r.found) {
        LogFromController("[rumble-x64] Entity-array anchor signature scan FAILED -- damage rumble disabled this "
            "session (fire rumble unaffected, resolved independently above)");
        return;
    }

    uintptr_t leaInsnAddr = r.address + kEntityArrayLeaInsnOffset;
    g_entityArrayBaseX64 = reinterpret_cast<uint8_t*>(SigScan::ResolveRipRelative(leaInsnAddr, kEntityArrayLeaInsnLength));
    if (!g_entityArrayBaseX64) {
        LogFromController("[rumble-x64] Entity-array RIP-relative resolution FAILED -- damage rumble disabled this session");
        return;
    }
    sprintf_s(buf, "[rumble-x64] Entity array resolved @ 0x%p (stride 0x%llX) -- damage rumble active "
        "(per-frame health poll, no hook installed).",
        reinterpret_cast<void*>(g_entityArrayBaseX64), static_cast<unsigned long long>(kEntityStrideX64));
    LogFromController(buf);
}

#endif // _M_X64 || _WIN64 (x64 port block above)

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
#if defined(_M_X64) || defined(_WIN64)
    // 2026-09-12 x64 port -- see this file's own "x64 PORT" block above for the full
    // RE trail. Two independent resolves, each fails loudly and disables only its own
    // mechanism on failure (never falls through to a garbage address, per CLAUDE.md SS5).
    InstallFireHookX64();
    ResolveEntityArrayX64();
#else
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
#endif
}

void Rumble_Tick()
{
#if defined(_M_X64) || defined(_WIN64)
    PollDamageRumbleX64();
    // Armor-scan diagnostic not ported to x64 this pass -- see its own comment in the
    // x64 PORT block above (out of scope: an off-by-default exploratory RE tool, not
    // one of the two real shipped mechanisms).
#else
    PollDamageRumble();
    PollArmorFieldScanDiag(); // OFF by default -- see its own comment (issue #63 follow-up)
#endif
    UpdateRumbleOutput();
}

// See rumble.h's own comment. Deliberately just UpdateRumbleOutput() -- no
// polling here, since this runs on the menu tick where the gameplay entity
// reads PollDamageRumble/PollArmorFieldScanDiag rely on aren't meaningful.
void Rumble_TickExpiryWatchdog()
{
    UpdateRumbleOutput();
}
