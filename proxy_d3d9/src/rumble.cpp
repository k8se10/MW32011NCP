#include "rumble.h"

#include <windows.h>
#include <cstdio>
#include <cstdint>
#include "../third_party/minhook/include/MinHook.h"
#include "controller_input.h"
#include "mod_config.h"

extern void LogFromController(const char* msg); // defined in dllmain.cpp

namespace {

// ---- Real native notify-dispatch choke points (task #17, 2026-07-17 research pass,
// re_notes/known_issues.md issue #24 / re_notes/iw5sp.md "Vibration/rumble trigger
// points"). Both confirmed plain __cdecl via raw disassembly this session (flat stack
// args, bare RET -- no register-passed convention, unlike the kbutton-family calls
// elsewhere in this codebase).

// FUN_004895b0(entity, eventHandle, paramCount) -- general native notify dispatcher.
// FUN_0045e320 (the per-shot fire-effects handler) calls this with the real
// "weapon_fired" event handle once per real shot.
typedef void(__cdecl* NotifySimple_t)(int entity, unsigned short eventHandle, unsigned int paramCount);
constexpr uintptr_t kNotifySimpleAddr = 0x004895b0;
NotifySimple_t g_origNotifySimple = nullptr;

// The real "weapon_fired" event handle lives at this address as a runtime-resolved
// 2-byte value (the GSC notify-event interning table hashes each name into a 2-byte
// handle at startup, per FUN_00470d00/FUN_005048b0 -- see iw5sp.md). Read live, not
// hardcoded as a literal value, since the hash is computed at runtime.
constexpr uintptr_t kWeaponFiredHandleAddr = 0x015c61a6;

// FUN_0044cdb0(eventHandle, entity, p3, p4, p5, damageAmount, p7, p8, p9, p10, p11, p12)
// -- the richer native notify dispatcher. FUN_0045f770 (the real damage-application
// function) calls this with the real "damage" event handle whenever any damageable
// entity takes damage; damageAmount (this function's own 6th parameter) is the literal
// amount just applied, confirmed via the real call site's argument order.
typedef void(__cdecl* NotifyRich_t)(unsigned int eventHandle, int entity, unsigned int p3,
    void* p4, void* p5, int damageAmount, int p7, unsigned int p8, int p9, int p10,
    unsigned int p11, char p12);
constexpr uintptr_t kNotifyRichAddr = 0x0044cdb0;
NotifyRich_t g_origNotifyRich = nullptr;
constexpr uintptr_t kDamageHandleAddr = 0x015c60b2;

// Both handles are genuinely 2-byte values (confirmed for the weapon-fired case via
// FUN_004895b0's own disassembly: MOVZX reads a word, not a dword). Comparing a wider
// int against a zero-extended unsigned short is safe by construction either way -- if
// the upper bits of whatever this hook receives are ever non-zero for some reason
// this project hasn't seen, the comparison simply never matches (hook silently
// doesn't fire), not a crash or a wrong match.
unsigned short ReadEventHandle(uintptr_t addr)
{
    return *reinterpret_cast<volatile unsigned short*>(addr);
}

// ---- Local-player filter --------------------------------------------------------
//
// Both weapon-fired and damage notifies fire for ANY entity (AI included), not just
// the local player -- a real implementation needs a local-player filter (flagged as
// unresolved research in the original 2026-07-17 pass). Resolved this session via a
// field this project ALREADY treats as a real "does this entity have a client
// struct" gate: entity+0x10c, confirmed non-null-checked by FUN_005BC9A0 (the real
// native notifyonplayercommand registration function, known_issues.md issue #29) as
// its own precondition for "is this a real player entity, not AI."
//
// HONEST CAVEAT: in solo SP/Survival (this project's only currently-supported
// configuration) there is exactly one client entity, so "has a non-null client
// struct" is equivalent to "is the local player." This is NOT scoped to specifically
// exclude a co-op partner's entity in 2-player Survival -- a second real client would
// also pass this check, meaning damage/fire rumble could fire for a co-op partner's
// events too. Not resolved this pass; flagged here rather than silently assumed away.
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

void __cdecl Hook_NotifySimple(int entity, unsigned short eventHandle, unsigned int paramCount)
{
    g_origNotifySimple(entity, eventHandle, paramCount);

    if (!g_modConfig.vibrationEnabled) return;
    if (eventHandle != ReadEventHandle(kWeaponFiredHandleAddr)) return;
    if (!IsRealPlayerEntity(entity)) return;

    TriggerRumble(g_modConfig.vibrationFireIntensity, g_modConfig.vibrationFireDurationMs);
}

void __cdecl Hook_NotifyRich(unsigned int eventHandle, int entity, unsigned int p3, void* p4,
    void* p5, int damageAmount, int p7, unsigned int p8, int p9, int p10, unsigned int p11,
    char p12)
{
    g_origNotifyRich(eventHandle, entity, p3, p4, p5, damageAmount, p7, p8, p9, p10, p11, p12);

    if (!g_modConfig.vibrationEnabled) return;
    if (eventHandle != ReadEventHandle(kDamageHandleAddr)) return;
    if (!IsRealPlayerEntity(entity)) return;
    if (damageAmount <= 0) return;

    float intensity = static_cast<float>(damageAmount) * g_modConfig.vibrationDamagePerPoint;
    if (intensity > g_modConfig.vibrationDamageMaxIntensity) {
        intensity = g_modConfig.vibrationDamageMaxIntensity;
    }
    TriggerRumble(intensity, g_modConfig.vibrationDamageDurationMs);
}

} // namespace

void Rumble_Install()
{
    char buf[160];

    MH_STATUS s1 = MH_CreateHook(reinterpret_cast<LPVOID>(kNotifySimpleAddr),
        reinterpret_cast<LPVOID>(&Hook_NotifySimple), reinterpret_cast<LPVOID*>(&g_origNotifySimple));
    sprintf_s(buf, "[rumble] MH_CreateHook(FUN_004895b0 @ 0x%p) = %d",
        reinterpret_cast<void*>(kNotifySimpleAddr), static_cast<int>(s1));
    LogFromController(buf);
    if (s1 == MH_OK) {
        MH_STATUS e1 = MH_EnableHook(reinterpret_cast<LPVOID>(kNotifySimpleAddr));
        sprintf_s(buf, "[rumble] MH_EnableHook(FUN_004895b0) = %d", static_cast<int>(e1));
        LogFromController(buf);
    }

    MH_STATUS s2 = MH_CreateHook(reinterpret_cast<LPVOID>(kNotifyRichAddr),
        reinterpret_cast<LPVOID>(&Hook_NotifyRich), reinterpret_cast<LPVOID*>(&g_origNotifyRich));
    sprintf_s(buf, "[rumble] MH_CreateHook(FUN_0044cdb0 @ 0x%p) = %d",
        reinterpret_cast<void*>(kNotifyRichAddr), static_cast<int>(s2));
    LogFromController(buf);
    if (s2 == MH_OK) {
        MH_STATUS e2 = MH_EnableHook(reinterpret_cast<LPVOID>(kNotifyRichAddr));
        sprintf_s(buf, "[rumble] MH_EnableHook(FUN_0044cdb0) = %d", static_cast<int>(e2));
        LogFromController(buf);
    }
}

void Rumble_Tick()
{
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
