#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "mod_config.h"

extern void LogFromController(const char* msg); // defined in dllmain.cpp

ModConfig g_modConfig;
ButtonMap g_buttonMap;

namespace {

void GetConfigPath(char* outPath, size_t outPathSize)
{
    GetModuleFileNameA(nullptr, outPath, static_cast<DWORD>(outPathSize)); // this EXE's own directory
    char* lastSlash = strrchr(outPath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    strcat_s(outPath, outPathSize, "mw3ncp_config.ini");
}

// GetPrivateProfileString has no float-returning variant -- read as string, parse
// ourselves. Falls back to defaultValue (already in outValue) on a missing/malformed
// key rather than silently zeroing it, since 0 is a meaningfully different value from
// "not set" for several of these (e.g. AdsSlowdownStrength=0 means "off").
void ReadFloat(const char* path, const char* section, const char* key, float& outValue)
{
    char buf[64];
    char defaultBuf[64];
    sprintf_s(defaultBuf, "%g", outValue);
    GetPrivateProfileStringA(section, key, defaultBuf, buf, sizeof(buf), path);
    char* end = nullptr;
    float parsed = strtof(buf, &end);
    if (end != buf) outValue = parsed;
}

void ReadUlong(const char* path, const char* section, const char* key, unsigned long& outValue)
{
    outValue = GetPrivateProfileIntA(section, key, static_cast<INT>(outValue), path);
}

void ReadBool(const char* path, const char* section, const char* key, bool& outValue)
{
    outValue = GetPrivateProfileIntA(section, key, outValue ? 1 : 0, path) != 0;
}

const char* ButtonLayoutName(ButtonLayout v)
{
    switch (v) {
        case ButtonLayout::Tactical: return "Tactical";
        case ButtonLayout::Lefty: return "Lefty";
        case ButtonLayout::TacticalLefty: return "TacticalLefty";
        default: return "Default";
    }
}

ButtonLayout ParseButtonLayout(const char* s, ButtonLayout fallback)
{
    if (_stricmp(s, "Default") == 0) return ButtonLayout::Default;
    if (_stricmp(s, "Tactical") == 0) return ButtonLayout::Tactical;
    if (_stricmp(s, "Lefty") == 0) return ButtonLayout::Lefty;
    if (_stricmp(s, "TacticalLefty") == 0) return ButtonLayout::TacticalLefty;
    return fallback;
}

const char* StickLayoutName(StickLayout v)
{
    switch (v) {
        case StickLayout::Southpaw: return "Southpaw";
        case StickLayout::Legacy: return "Legacy";
        case StickLayout::LegacySouthpaw: return "LegacySouthpaw";
        default: return "Default";
    }
}

StickLayout ParseStickLayout(const char* s, StickLayout fallback)
{
    if (_stricmp(s, "Default") == 0) return StickLayout::Default;
    if (_stricmp(s, "Southpaw") == 0) return StickLayout::Southpaw;
    if (_stricmp(s, "Legacy") == 0) return StickLayout::Legacy;
    if (_stricmp(s, "LegacySouthpaw") == 0) return StickLayout::LegacySouthpaw;
    return fallback;
}

void ReadButtonLayout(const char* path, ButtonLayout& outValue)
{
    char buf[32];
    GetPrivateProfileStringA("Bindings", "ButtonLayout", ButtonLayoutName(outValue), buf, sizeof(buf), path);
    outValue = ParseButtonLayout(buf, outValue);
}

void ReadStickLayout(const char* path, StickLayout& outValue)
{
    char buf[32];
    GetPrivateProfileStringA("Bindings", "StickLayout", StickLayoutName(outValue), buf, sizeof(buf), path);
    outValue = ParseStickLayout(buf, outValue);
}

// Writes a fresh, fully-commented default INI -- called only when no file exists yet,
// so a first run leaves something discoverable and self-documenting next to the DLL
// rather than a silent set of in-memory defaults the player has no way to find.
void WriteDefaultConfig(const char* path)
{
    FILE* f = nullptr;
    if (fopen_s(&f, path, "w") != 0 || !f) return;

    fprintf(f,
        "; MW3 Native Controller Support -- configuration\n"
        "; Changes take effect on next launch (no live reload yet).\n"
        "; A real in-game options screen (sliders) is planned -- this file is the\n"
        "; interim way to tune these values until then.\n"
        "\n"
        "[Look]\n"
        "; Look-stick turn rate in degrees/second at full stick deflection. Which\n"
        "; physical stick (and axes) actually drive look depends on the StickLayout\n"
        "; setting under [Bindings] below -- this is not always the right stick.\n"
        "Sensitivity=%g\n"
        "; How strongly look slows down while aiming down sights on magnified optics,\n"
        "; scaled to the weapon's actual live zoom level (read-only -- never changes\n"
        "; your real field of view) as effectiveFov/hipfireFov, raised to this power.\n"
        "; 0.0 = no slowdown at all (flat sensitivity regardless of zoom); 1.0 = fully\n"
        "; proportional to zoom (closest to real console feel, confirmed live); higher\n"
        "; values (2.0, 3.0, ...) apply progressively MORE slowdown than proportional,\n"
        "; useful if even 1.0 feels too fast on deep zooms -- always mathematically\n"
        "; safe (never inverts/goes negative) no matter how high you set it. Must stay\n"
        "; >= 0.0.\n"
        "AdsSlowdownStrength=%g\n"
        "; Multiplies on top of the strength curve above. Without this, low-zoom optics\n"
        "; (iron sights/red dots, where the zoom ratio stays close to 1.0) got almost no\n"
        "; slowdown at all regardless of strength. 1.0 = no extra effect (pure strength\n"
        "; curve only); lower values add real slowdown even at minimal zoom, scaling up\n"
        "; further as strength increases zoom-based slowdown on top. Must stay >= 0.0.\n"
        "AdsSlowdownBaseline=%g\n"
        "; OG console \"Invert Look\" -- flips vertical (up/down) look. 0 = off, 1 = on.\n"
        "InvertLook=%d\n"
        "\n"
        "[Stance]\n"
        "; Milliseconds a Crouch/Prone (B) press must be held to count as \"hold\"\n"
        "; instead of \"tap\", for the 3-state stance ladder: from Standing or\n"
        "; Crouched, hold goes Prone; from Prone, hold instead stands you back UP.\n"
        "; Tap: Standing<->Crouched, or Prone->Crouched.\n"
        "ProneHoldThresholdMs=%lu\n"
        "\n"
        "[Interact]\n"
        "; Milliseconds Interact (X) must be held before it fires. A press released\n"
        "; before this reloads the weapon same as console.\n"
        "HoldThresholdMs=%lu\n"
        "\n"
        "[Survival]\n"
        "; Milliseconds Y must be held between waves to ready up (Survival only). A\n"
        "; press released before this switches weapons instead, same as a normal tap.\n"
        "ReadyUpHoldThresholdMs=%lu\n"
        "\n"
        "[Sprint]\n"
        "; Seconds of continuous sprint before stamina fully depletes.\n"
        "MaxStaminaSeconds=%g\n"
        "; Seconds of NOT sprinting required to fully recover from empty.\n"
        "RegenSeconds=%g\n"
        "\n"
        "[Bindings]\n"
        "; OG console button layout presets, reconstructed from the unchanged CoD4->\n"
        "; MW2->MW3 console control scheme (NOT independently verified against real\n"
        "; hardware yet -- TacticalLefty in particular may need a correction pass).\n"
        "; One of: Default, Tactical, Lefty, TacticalLefty\n"
        "ButtonLayout=%s\n"
        "; One of: Default, Southpaw, Legacy, LegacySouthpaw\n"
        "StickLayout=%s\n"
        "; Independent toggle: swaps RT<->RB and LT<->LB (0 = off, 1 = on). Combines\n"
        "; with whichever ButtonLayout is active above.\n"
        "FlipTriggers=%d\n"
        "\n"
        "[AimAssist]\n"
        "; Our own implementation (task #16) -- the native aim-assist chain turned out\n"
        "; to be shared math bots use to aim at the player, not a player-facing feature,\n"
        "; so this is built from scratch using real entity data plus our own curves.\n"
        "; 0 = off, 1 = on.\n"
        "Enabled=%d\n"
        "; Max distance (world units) to a target for it to be considered at all.\n"
        "Range=%g\n"
        "; Half-angle (degrees) of the \"near crosshair\" cone a target must be within.\n"
        "ConeDegrees=%g\n"
        "; How much to slow the look-turn rate while the crosshair is near a valid\n"
        "; target (rotational friction). 0 = no slowdown, 1 = strongest.\n"
        "FrictionStrength=%g\n"
        "; Max degrees/second the crosshair gets pulled toward a valid target\n"
        "; (magnetism), independent of your own stick input.\n"
        "MagnetismDegreesPerSecond=%g\n"
        "\n"
        "[Vibration]\n"
        "; No native rumble exists in this build at all -- entirely our own\n"
        "; XInputSetState output, driven off real weapon-fire and damage-taken events.\n"
        "; 0 = off, 1 = on.\n"
        "Enabled=%d\n"
        "; Motor strength [0,1] on each real shot fired.\n"
        "FireIntensity=%g\n"
        "; Milliseconds a fire pulse takes to decay back to zero.\n"
        "FireDurationMs=%lu\n"
        "; Motor strength added per point of real damage the LOCAL player takes.\n"
        "DamagePerPoint=%g\n"
        "; Hard cap on damage-rumble strength regardless of how much damage lands.\n"
        "DamageMaxIntensity=%g\n"
        "; Milliseconds a damage pulse takes to decay back to zero.\n"
        "DamageDurationMs=%lu\n"
        "\n"
        "[Experimental]\n"
        "; Individually toggleable, not-yet-fully-proven behaviors -- for live\n"
        "; experimentation. Flip one off (0) if it's ever suspected of causing a\n"
        "; problem, without needing a recompile. These are not permanent settings --\n"
        "; expect entries here to eventually graduate to unconditional (and be\n"
        "; removed from this section) once confirmed correct and stable.\n"
        "; Task #7/#29: also pushes the command \"n\" onto the real client command\n"
        "; queue on Fire's down-edge, alongside the real +attack kbutton call, in an\n"
        "; attempt to reach notifyonplayercommand's delivery mechanism for\n"
        "; killstreaks like Predator Missile. 0 = off (kbutton call only, pre-\n"
        "; 2026-07-18 behavior), 1 = on.\n"
        "FireNotifyQueueKick=%d\n"
        "; Task #9, 2026-07-19: Sprint just migrated off raw pm_flags-forcing onto\n"
        "; the real +sprint kbutton. Set to 1 to skip this mod's OWN stamina/\n"
        "; cooldown timer entirely (no 4s/2s limit) while confirming the new\n"
        "; kbutton mechanism itself works in isolation. 0 = normal stamina/cooldown\n"
        "; behavior (default) -- set this back to 0 once the kbutton is confirmed.\n"
        "SprintStaminaBypassForTesting=%d\n",
        g_modConfig.lookDegreesPerSecond,
        g_modConfig.adsSlowdownStrength,
        g_modConfig.adsSlowdownBaseline,
        g_modConfig.invertLook ? 1 : 0,
        g_modConfig.proneHoldThresholdMs,
        g_modConfig.interactHoldThresholdMs,
        g_modConfig.readyUpHoldThresholdMs,
        g_modConfig.sprintMaxStaminaSeconds,
        g_modConfig.sprintRegenSeconds,
        ButtonLayoutName(g_modConfig.buttonLayout),
        StickLayoutName(g_modConfig.stickLayout),
        g_modConfig.flipTriggers ? 1 : 0,
        g_modConfig.aimAssistEnabled ? 1 : 0,
        g_modConfig.aimAssistRange,
        g_modConfig.aimAssistConeDegrees,
        g_modConfig.aimAssistFrictionStrength,
        g_modConfig.aimAssistMagnetismDegreesPerSecond,
        g_modConfig.vibrationEnabled ? 1 : 0,
        g_modConfig.vibrationFireIntensity,
        g_modConfig.vibrationFireDurationMs,
        g_modConfig.vibrationDamagePerPoint,
        g_modConfig.vibrationDamageMaxIntensity,
        g_modConfig.vibrationDamageDurationMs,
        g_modConfig.fireNotifyQueueKick ? 1 : 0,
        g_modConfig.sprintStaminaBypassForTesting ? 1 : 0);

    fclose(f);
}

} // namespace

// ---- Button layout resolution (task #15) ------------------------------------------
//
// Tables below are the user-supplied reconstruction of the unchanged CoD4->MW2->MW3
// console button layouts (see mod_config.h's enum comments for the confidence caveat).
// Default/Tactical/Lefty are each independently well-established; TacticalLefty is
// Lefty with Tactical's face-button swap (Crouch/Melee) applied on top of Lefty's own
// already-swapped stick-click assignments (Sprint/Melee) -- taken as given directly
// from the user's own final resolved table, not re-derived here.
ButtonMap ResolveButtonMap(ButtonLayout layout, bool flipTriggers)
{
    ButtonMap m; // struct defaults already match ButtonLayout::Default

    switch (layout) {
        case ButtonLayout::Default:
            break; // defaults are already correct
        case ButtonLayout::Tactical:
            m.crouchProne = PhysicalInput::RS;
            m.melee = PhysicalInput::B;
            break;
        case ButtonLayout::Lefty:
            m.fire = PhysicalInput::LT;
            m.ads = PhysicalInput::RT;
            m.lethal = PhysicalInput::LB;
            m.tactical = PhysicalInput::RB;
            m.sprint = PhysicalInput::RS;
            m.melee = PhysicalInput::LS;
            break;
        case ButtonLayout::TacticalLefty:
            m.fire = PhysicalInput::LT;
            m.ads = PhysicalInput::RT;
            m.lethal = PhysicalInput::LB;
            m.tactical = PhysicalInput::RB;
            m.crouchProne = PhysicalInput::LS;
            m.sprint = PhysicalInput::RS;
            m.melee = PhysicalInput::B;
            break;
    }

    if (flipTriggers) {
        auto flip = [](PhysicalInput p) {
            switch (p) {
                case PhysicalInput::RT: return PhysicalInput::RB;
                case PhysicalInput::RB: return PhysicalInput::RT;
                case PhysicalInput::LT: return PhysicalInput::LB;
                case PhysicalInput::LB: return PhysicalInput::LT;
                default: return p;
            }
        };
        m.fire = flip(m.fire);
        m.ads = flip(m.ads);
        m.lethal = flip(m.lethal);
        m.tactical = flip(m.tactical);
        m.reloadUse = flip(m.reloadUse);
        m.weaponSwitch = flip(m.weaponSwitch);
        m.jump = flip(m.jump);
        m.crouchProne = flip(m.crouchProne);
        m.sprint = flip(m.sprint);
        m.melee = flip(m.melee);
        m.pause = flip(m.pause);
        m.scoreboard = flip(m.scoreboard);
    }

    return m;
}

void LoadModConfig()
{
    char path[MAX_PATH];
    GetConfigPath(path, sizeof(path));

    DWORD attrs = GetFileAttributesA(path);
    bool exists = (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
    if (!exists) {
        WriteDefaultConfig(path); // g_modConfig still holds its struct-initializer defaults here
        g_buttonMap = ResolveButtonMap(g_modConfig.buttonLayout, g_modConfig.flipTriggers);
        LogFromController("[config] no mw3ncp_config.ini found -- wrote a default one");
        return;
    }

    ReadFloat(path, "Look", "Sensitivity", g_modConfig.lookDegreesPerSecond);
    ReadFloat(path, "Look", "AdsSlowdownStrength", g_modConfig.adsSlowdownStrength);
    // Live-confirmed bug (2026-07-16): the OLD linear blend formula
    // (1 - strength*(1-ratio)) went NEGATIVE for strength > 1.0 once the zoom ratio
    // dropped below (1 - 1/strength) -- e.g. strength=2.0 inverted look direction on
    // any scope zoomed in past ~50%. Not a native engine issue at all (confirmed via
    // diagnostic logging: the "risky" alt-FOV-path flag never set during the repro
    // that exposed this) -- it was the formula's shape, not the value. Fixed by
    // switching GetAdsLookRateScale to a power curve (ratio^strength) instead of a
    // linear blend -- mathematically safe for any strength >= 0, no upper bound
    // needed (see that function's own comment for why). Only guard against a
    // negative strength, which WOULD still misbehave (ratio^negative blows up as
    // ratio->0).
    if (g_modConfig.adsSlowdownStrength < 0.0f) g_modConfig.adsSlowdownStrength = 0.0f;
    ReadFloat(path, "Look", "AdsSlowdownBaseline", g_modConfig.adsSlowdownBaseline);
    // Same guard as strength above -- a negative baseline would flip the sign of the
    // whole scale factor (baseline * ratio^strength), inverting look direction.
    if (g_modConfig.adsSlowdownBaseline < 0.0f) g_modConfig.adsSlowdownBaseline = 0.0f;
    ReadBool(path, "Look", "InvertLook", g_modConfig.invertLook);
    ReadUlong(path, "Stance", "ProneHoldThresholdMs", g_modConfig.proneHoldThresholdMs);
    ReadUlong(path, "Interact", "HoldThresholdMs", g_modConfig.interactHoldThresholdMs);
    ReadUlong(path, "Survival", "ReadyUpHoldThresholdMs", g_modConfig.readyUpHoldThresholdMs);
    ReadFloat(path, "Sprint", "MaxStaminaSeconds", g_modConfig.sprintMaxStaminaSeconds);
    ReadFloat(path, "Sprint", "RegenSeconds", g_modConfig.sprintRegenSeconds);
    // FIXED 2026-07-17 (pre-release review): InjectControllerSprint divides by
    // sprintRegenSeconds every tick (dt * (sprintMaxStaminaSeconds / sprintRegenSeconds)).
    // A hand-edited RegenSeconds=0 alone is a divide-by-zero (float semantics -> inf,
    // clamped away same-tick, harmless); MaxStaminaSeconds=0 too makes it 0/0 -> NaN,
    // and g_sprintStamina goes permanently NaN (the ">= 0" clamp check is always false
    // for NaN, so it never self-corrects). Only reachable via manual config editing,
    // not normal play, but cheap to guard the same way every other config value in
    // this function already is.
    if (g_modConfig.sprintMaxStaminaSeconds < 0.1f) g_modConfig.sprintMaxStaminaSeconds = 0.1f;
    if (g_modConfig.sprintRegenSeconds < 0.1f) g_modConfig.sprintRegenSeconds = 0.1f;
    ReadButtonLayout(path, g_modConfig.buttonLayout);
    ReadStickLayout(path, g_modConfig.stickLayout);
    ReadBool(path, "Bindings", "FlipTriggers", g_modConfig.flipTriggers);
    ReadBool(path, "AimAssist", "Enabled", g_modConfig.aimAssistEnabled);
    ReadFloat(path, "AimAssist", "Range", g_modConfig.aimAssistRange);
    if (g_modConfig.aimAssistRange < 0.0f) g_modConfig.aimAssistRange = 0.0f;
    ReadFloat(path, "AimAssist", "ConeDegrees", g_modConfig.aimAssistConeDegrees);
    if (g_modConfig.aimAssistConeDegrees < 0.0f) g_modConfig.aimAssistConeDegrees = 0.0f;
    ReadFloat(path, "AimAssist", "FrictionStrength", g_modConfig.aimAssistFrictionStrength);
    if (g_modConfig.aimAssistFrictionStrength < 0.0f) g_modConfig.aimAssistFrictionStrength = 0.0f;
    ReadFloat(path, "AimAssist", "MagnetismDegreesPerSecond", g_modConfig.aimAssistMagnetismDegreesPerSecond);
    if (g_modConfig.aimAssistMagnetismDegreesPerSecond < 0.0f) g_modConfig.aimAssistMagnetismDegreesPerSecond = 0.0f;
    ReadBool(path, "Vibration", "Enabled", g_modConfig.vibrationEnabled);
    ReadFloat(path, "Vibration", "FireIntensity", g_modConfig.vibrationFireIntensity);
    if (g_modConfig.vibrationFireIntensity < 0.0f) g_modConfig.vibrationFireIntensity = 0.0f;
    ReadUlong(path, "Vibration", "FireDurationMs", g_modConfig.vibrationFireDurationMs);
    ReadFloat(path, "Vibration", "DamagePerPoint", g_modConfig.vibrationDamagePerPoint);
    if (g_modConfig.vibrationDamagePerPoint < 0.0f) g_modConfig.vibrationDamagePerPoint = 0.0f;
    ReadFloat(path, "Vibration", "DamageMaxIntensity", g_modConfig.vibrationDamageMaxIntensity);
    if (g_modConfig.vibrationDamageMaxIntensity < 0.0f) g_modConfig.vibrationDamageMaxIntensity = 0.0f;
    ReadUlong(path, "Vibration", "DamageDurationMs", g_modConfig.vibrationDamageDurationMs);
    ReadBool(path, "Experimental", "FireNotifyQueueKick", g_modConfig.fireNotifyQueueKick);
    ReadBool(path, "Experimental", "SprintStaminaBypassForTesting", g_modConfig.sprintStaminaBypassForTesting);

    g_buttonMap = ResolveButtonMap(g_modConfig.buttonLayout, g_modConfig.flipTriggers);

    char buf[900];
    sprintf_s(buf,
        "[config] loaded mw3ncp_config.ini: sensitivity=%g adsSlowdownStrength=%g "
        "adsSlowdownBaseline=%g invertLook=%d proneHoldMs=%lu interactHoldMs=%lu "
        "readyUpHoldMs=%lu sprintMax=%g "
        "sprintRegen=%g buttonLayout=%s stickLayout=%s flipTriggers=%d "
        "aimAssistEnabled=%d aimAssistRange=%g aimAssistConeDegrees=%g "
        "aimAssistFrictionStrength=%g aimAssistMagnetismDps=%g "
        "vibrationEnabled=%d vibrationFireIntensity=%g vibrationFireDurationMs=%lu "
        "vibrationDamagePerPoint=%g vibrationDamageMaxIntensity=%g vibrationDamageDurationMs=%lu "
        "fireNotifyQueueKick=%d sprintStaminaBypassForTesting=%d",
        g_modConfig.lookDegreesPerSecond, g_modConfig.adsSlowdownStrength,
        g_modConfig.adsSlowdownBaseline,
        g_modConfig.invertLook ? 1 : 0, g_modConfig.proneHoldThresholdMs,
        g_modConfig.interactHoldThresholdMs, g_modConfig.readyUpHoldThresholdMs,
        g_modConfig.sprintMaxStaminaSeconds, g_modConfig.sprintRegenSeconds,
        ButtonLayoutName(g_modConfig.buttonLayout), StickLayoutName(g_modConfig.stickLayout),
        g_modConfig.flipTriggers ? 1 : 0,
        g_modConfig.aimAssistEnabled ? 1 : 0, g_modConfig.aimAssistRange,
        g_modConfig.aimAssistConeDegrees, g_modConfig.aimAssistFrictionStrength,
        g_modConfig.aimAssistMagnetismDegreesPerSecond,
        g_modConfig.vibrationEnabled ? 1 : 0, g_modConfig.vibrationFireIntensity,
        g_modConfig.vibrationFireDurationMs, g_modConfig.vibrationDamagePerPoint,
        g_modConfig.vibrationDamageMaxIntensity, g_modConfig.vibrationDamageDurationMs,
        g_modConfig.fireNotifyQueueKick ? 1 : 0,
        g_modConfig.sprintStaminaBypassForTesting ? 1 : 0);
    LogFromController(buf);
}
