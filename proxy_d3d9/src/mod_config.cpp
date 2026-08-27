#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "mod_config.h"
#include "overlay_hud.h"
#include "vanilla_settings_table.h"
#include "vanilla_settings_sync.h"
#include "frame_benchmark.h" // 2026-08-27 -- times ConfigHotReloadThreadProc's own
    // real per-check cost (issue #96 follow-up, background-thread visibility gap)

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

// ---- Config migration (task #14 follow-up, 2026-07-31) ----------------------------
//
// mw3ncp_config.ini has no live-reload, but it DOES persist across mod updates -- an
// existing file predating a key rename (e.g. v0.2.2's single [Look] Sensitivity,
// split into SensitivityHorizontal/SensitivityVertical in v0.2.5) must not silently
// fall back to the new keys' hardcoded defaults and lose a player's tuned value.
// ConfigVersion is an internal schema marker (not a user-facing setting) tracking
// which migrations a given file has already had applied. Missing entirely ==
// version 0, i.e. any file from before this system existed. Bumped several times the
// same day (all still pre-release): v1->v2 when issue #44's AdsSlowdownBaseline
// reset-if-untouched rule was added after some files had already been migrated to
// v1 by the Sensitivity-split migration alone (their ConfigVersion=1 would
// otherwise skip the new rule, since it's not < 1); v2->v3 when live testing found
// the 0.65->0.45 baseline retune itself was the wrong fix (see mod_config.h's own
// comment on adsSlowdownBaseline) and got reverted back to 0.65 in favor of the
// new, decoupled adsCloseRangeSlowdownStrength; v3->v4 when the new [Overlay]
// section (FontItalic, TestCycleAllVariants) was added -- WriteDefaultConfig() only
// runs when `configVersion < kCurrentConfigVersion` (see the bottom of
// LoadModConfig()), so a file already sitting AT the current version never gets
// rewritten just because new keys were added to the template; without this bump,
// anyone already on v3 would never see the new keys appear in their real ini file
// at all, even though the compiled defaults would still apply correctly in memory --
// confirmed live: exactly this happened, config hot-reload alone doesn't add new
// keys to an already-current-version file; v4->v5 when the new [Experimental]
// HudGlyphPositionLogging key (issue #48) was added; v5->v6 when GlyphIconOverlay
// (issue #48's actual icon-drawing toggle) was added -- same reasoning applies every
// time going forward: any new config key needs a version bump or it silently never
// appears for already-current-version users. v6->v7 (2026-08-03) when the new
// [Movement] section (AutoMantleEnabled/AutoMantleForwardConeDegrees/
// AutoMantleMinStickMagnitude) was added.
//
// POLICY WIDENED 2026-08-03 (explicit user direction: "make sure the config always
// updates outdated comment text"): a version bump is now required not just for new
// keys, but for ANY correction to an existing key's comment text too (e.g. a stale
// explanation, a corrected default-value citation, a fixed typo) -- WriteDefaultConfig()
// only re-runs when `configVersion < kCurrentConfigVersion`, so a comment-only fix
// with no version bump would silently never reach anyone already on the current
// version, identical to the "new key never appears" gap this same mechanism already
// exists to prevent. v7->v8 (2026-08-03) when the [Vibration] section's comments were
// corrected (the "no native rumble exists" framing was stale post-issue-#24
// reimplementation) and its default values were retuned (FireIntensity 0.25->0.55,
// FireDurationMs 60->90, DamagePerPoint 0.03->0.05 -- see mod_config.h's own comment).
// v8->v9 (2026-08-03, same day, round 2): still user-reported "extremely weak" after
// the above -- retuned again (FireIntensity 0.55->0.85, FireDurationMs 90->150,
// DamagePerPoint 0.05->0.08, DamageDurationMs 200->250) alongside the new sustain/
// decay envelope in rumble.cpp, and added the new [Experimental] ArmorFieldScanLogging
// key (issue #63 follow-up, Survival armor doesn't register on the damage-rumble poll).
// v9->v10 (2026-08-03, same day, round 3, v0.3.0 packaging pass): still reported
// "better now but kinda weak" -- FireIntensity raised to its ceiling (1.0),
// FireDurationMs 150->180, DamagePerPoint 0.08->0.12, DamageDurationMs 250->280, and
// the sustain fraction itself raised (rumble.cpp, 0.6->0.7). Comment text also
// corrected throughout [Vibration] to describe round 3 instead of round 2.
// v10->v11 (2026-08-03, same day, issue #65): SensitivityVertical default corrected
// 250->75 -- real MW3 console vertical sensitivity is ~30% of horizontal per direct
// user testimony from actual console play (corroborated by the real console Options
// menu having only one Sensitivity slider at all, no independent vertical control),
// not the ~80% ratio the 2026-07-31 split's own default wrongly assumed.
// v11->v12 (2026-08-03, same day): 75 (the initial ~30% feel-estimate) live-tested and
// reported "way too slow" -- corrected to 145 (~58%, "closer to about 55-60%").
// v12->v13 (2026-08-04, issue #66 full-scope pivot): new [Options] section
// (UseCustomOptionsScreen) added -- the full custom-options-replacement toggle.
// v13->v14 (2026-08-16, issue #74 root-cause fix follow-up): [Experimental]
// GlyphIconOverlay REMOVED entirely -- it was the master on/off switch for the whole
// controller-glyph overlay, shipped defaulted OFF since issue #48 and never flipped on
// for release, which was the real cause of every "no glyphs" community report. Simply
// changing the in-code default to true isn't enough on its own: every existing user's
// already-written ini has an explicit `GlyphIconOverlay=0` on disk (SaveModConfig wrote
// it out on every prior run), which would keep overriding the new default right back to
// broken. The bump here forces WriteDefaultConfig() to rewrite every upgrader's ini and
// drop the stale key, so it physically cannot block the overlay anymore.
// v14->v15 (2026-08-16, issue #51 follow-up): new [Experimental] GlyphPositionEditMode
// key added -- the in-game click-and-drag menu-glyph calibration tool's master gate.
// v15->v16 (2026-08-17, tools/ui_harness .menu-renderer project): new [Experimental]
// CaptureRuntimeMenuAssets key added -- dumps real material textures to disk while
// playing, for the harness's .menu renderer to use in place of the incomplete static
// zone_dump extraction. See mod_config.h's own field comment for the full rationale.
// v16->v17 (2026-08-17, same-day stutter investigation): new [Experimental]
// FrametimeBenchmarkLogging key added -- real per-frame CSV timing diagnostic.
// v17->v18 (2026-08-24, font swap): new [Overlay] FontFamily key added -- default
// bundled font switched from Barlow Condensed SemiBold to Isotherm Sans (Barlow is
// no longer bundled at all, see resource.h), and the family name is now player-
// overridable to any system-installed font, not just a fixed choice between two
// bundled ones.
// v18->v19 (2026-08-24, same day, two-font follow-up): new [Overlay]
// FontFamilyCondensed key added -- Isotherm Sans is now bundled as TWO selectable
// variants (UI, the general default; Condensed, used only by specific hint call
// sites), each independently player-overridable. FontFamily's own default flipped
// from "Isotherm Sans" to "Isotherm Sans UI" in the same pass -- see resource.h and
// mod_config.h's own field comments for the full history.
// v19->v20 (2026-08-25, glyph-style auto-detect): new [Bindings] GlyphStyleAuto key
// added -- detects DualSense/Xbox360/XboxModern and keeps GlyphStyle in sync
// automatically. Its OWN load logic (LoadModConfig, right after ReadGlyphStyle) is
// NOT a plain ReadBool -- see that call site's own comment for why a file with an
// already-non-default GlyphStyle (a pre-existing hand-edit) defaults this to false
// on migration instead of the usual compiled-default true, so a deliberate manual
// choice made before this feature existed can't be silently overridden.
// v20->v21 (2026-08-25, plugin API): new [Plugins] Enabled key added -- STRICTLY
// opt-in, default off. See PLUGIN_API.md and mod_config.h's own pluginsEnabled
// field comment for the full design/risk statement.
// v21->v22 (2026-08-25, issue #88): new [Video] InternalRenderScalePercent key
// added -- STRICTLY opt-in, 0/disabled by default. Fixes the engine's real
// stuck-at-a-legacy-r_mode-default softness on displays above 1080p (direct user
// framing: "above 1080p it looks bad like 2005 bad") by setting r_mode to a
// percentage of the real desktop native resolution + a real vid_restart. See
// mod_config.h's own internalRenderScalePercent field comment for the full design.
// v22->v23 (2026-08-26, issue #92): new [Video] ForceD3D9On12 key added -- STRICTLY
// opt-in, off by default. Forces this DLL's own Direct3DCreate9 export to call the
// real system d3d9.dll's Direct3DCreate9On12 entry point instead of the ordinary
// one -- a real, Microsoft-documented alternate export, not a third-party DLL swap.
// See mod_config.h's own forceD3D9On12 field comment for the full design.
constexpr unsigned long kCurrentConfigVersion = 27; // v23->v24: FsrSharpenEnabled/FsrSharpenStrength (Phase B)
                                                     // v24->v25: MotionBlurEnabled/MotionBlurStrength (Phase E),
                                                     // FsrSharpenStrength default 0.5->0.3 (live feedback: "needs more softness")
                                                     // v25->v26: ForceAnisotropicFiltering
                                                     // v26->v27: MotionBlurCenterFalloff

// Reads a legacy key's raw value, returning true only if the key genuinely existed
// (unlike ReadFloat, which can't distinguish "absent" from "present but unparsable" --
// migration needs to know "was there anything here to carry over at all").
bool TryReadLegacyFloat(const char* path, const char* section, const char* key, float& outValue)
{
    char buf[64];
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), path);
    if (buf[0] == '\0') return false;
    char* end = nullptr;
    float parsed = strtof(buf, &end);
    if (end == buf) return false;
    outValue = parsed;
    return true;
}

// Reads a float key the same as ReadFloat, EXCEPT: a plain compiled-default change
// (e.g. AdsSlowdownBaseline 0.65 -> 0.45, issue #44) can't reach an existing config
// the way a key rename can -- the key never disappeared, so the file's own explicit
// value always wins over a new compiled default regardless of whether the player
// ever deliberately touched it. User-decided policy (2026-07-31): only adopt the
// new default for a file that's still sitting on the EXACT old default untouched;
// anything else is treated as a deliberate customization and always respected,
// unconditionally, same as ReadFloat. Gated by fromVersion so this reset rule only
// ever applies once, to files that predate it -- a file already migrated (or a
// fresh install already on the new default) never re-triggers it even if the player
// later happens to dial the value back to exactly the old number.
void ReadFloatWithDefaultRetune(const char* path, const char* section, const char* key,
                                 unsigned long configVersion, unsigned long fromVersion,
                                 float oldDefault, float& outValue)
{
    char buf[64];
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), path);
    if (buf[0] == '\0') return; // key absent -- outValue already holds the current compiled default
    char* end = nullptr;
    float onDisk = strtof(buf, &end);
    if (end == buf) return; // unparsable -- leave outValue alone, same as ReadFloat's own behavior
    if (configVersion < fromVersion && fabsf(onDisk - oldDefault) < 0.0001f) {
        return; // stale, never-customized old default -- adopt the new compiled default instead
    }
    outValue = onDisk; // explicit real value (post-retune file, or a genuine customization) -- respect it
}

const char* ButtonLayoutName(ButtonLayout v)
{
    switch (v) {
        case ButtonLayout::Tactical: return "Tactical";
        case ButtonLayout::Lefty: return "Lefty";
        case ButtonLayout::TacticalLefty: return "TacticalLefty";
        case ButtonLayout::Custom: return "Custom";
        default: return "Default";
    }
}

ButtonLayout ParseButtonLayout(const char* s, ButtonLayout fallback)
{
    if (_stricmp(s, "Default") == 0) return ButtonLayout::Default;
    if (_stricmp(s, "Tactical") == 0) return ButtonLayout::Tactical;
    if (_stricmp(s, "Lefty") == 0) return ButtonLayout::Lefty;
    if (_stricmp(s, "TacticalLefty") == 0) return ButtonLayout::TacticalLefty;
    if (_stricmp(s, "Custom") == 0) return ButtonLayout::Custom;
    return fallback;
}

// PhysicalInput <-> short name (2026-08-06, issue #66's Binds tab) -- used for both
// this ini section's own [CustomBinds] keys and the Binds tab's row-value display
// (overlay_hud.cpp's own PhysicalInputShortName is a separate, UI-facing copy of the
// same names -- kept apart same as this project's other cross-module name tables,
// not shared, since one is a stable on-disk format and the other is free to reflow
// for display).
const char* PhysicalInputName(PhysicalInput v)
{
    switch (v) {
        case PhysicalInput::RT: return "RT";
        case PhysicalInput::LT: return "LT";
        case PhysicalInput::RB: return "RB";
        case PhysicalInput::LB: return "LB";
        case PhysicalInput::X:  return "X";
        case PhysicalInput::Y:  return "Y";
        case PhysicalInput::A:  return "A";
        case PhysicalInput::B:  return "B";
        case PhysicalInput::LS: return "LS";
        case PhysicalInput::RS: return "RS";
        case PhysicalInput::Start: return "Start";
        case PhysicalInput::Back:  return "Back";
    }
    return "A";
}

PhysicalInput ParsePhysicalInput(const char* s, PhysicalInput fallback)
{
    if (_stricmp(s, "RT") == 0) return PhysicalInput::RT;
    if (_stricmp(s, "LT") == 0) return PhysicalInput::LT;
    if (_stricmp(s, "RB") == 0) return PhysicalInput::RB;
    if (_stricmp(s, "LB") == 0) return PhysicalInput::LB;
    if (_stricmp(s, "X") == 0) return PhysicalInput::X;
    if (_stricmp(s, "Y") == 0) return PhysicalInput::Y;
    if (_stricmp(s, "A") == 0) return PhysicalInput::A;
    if (_stricmp(s, "B") == 0) return PhysicalInput::B;
    if (_stricmp(s, "LS") == 0) return PhysicalInput::LS;
    if (_stricmp(s, "RS") == 0) return PhysicalInput::RS;
    if (_stricmp(s, "Start") == 0) return PhysicalInput::Start;
    if (_stricmp(s, "Back") == 0) return PhysicalInput::Back;
    return fallback;
}

void ReadCustomButtonMap(const char* path, ButtonMap& outValue)
{
    char buf[32];
#define READ_CUSTOM_BIND(field) \
    GetPrivateProfileStringA("CustomBinds", #field, PhysicalInputName(outValue.field), buf, sizeof(buf), path); \
    outValue.field = ParsePhysicalInput(buf, outValue.field);
    READ_CUSTOM_BIND(fire)
    READ_CUSTOM_BIND(ads)
    READ_CUSTOM_BIND(lethal)
    READ_CUSTOM_BIND(tactical)
    READ_CUSTOM_BIND(reloadUse)
    READ_CUSTOM_BIND(weaponSwitch)
    READ_CUSTOM_BIND(jump)
    READ_CUSTOM_BIND(crouchProne)
    READ_CUSTOM_BIND(sprint)
    READ_CUSTOM_BIND(melee)
    READ_CUSTOM_BIND(pause)
    READ_CUSTOM_BIND(scoreboard)
#undef READ_CUSTOM_BIND
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

const char* GlyphStyleName(GlyphStyle v)
{
    switch (v) {
        case GlyphStyle::XboxModern: return "XboxModern";
        case GlyphStyle::PlayStation: return "PlayStation";
        default: return "Xbox360";
    }
}

GlyphStyle ParseGlyphStyle(const char* s, GlyphStyle fallback)
{
    if (_stricmp(s, "Xbox360") == 0) return GlyphStyle::Xbox360;
    if (_stricmp(s, "XboxModern") == 0) return GlyphStyle::XboxModern;
    if (_stricmp(s, "PlayStation") == 0) return GlyphStyle::PlayStation;
    return fallback;
}

void ReadGlyphStyle(const char* path, GlyphStyle& outValue)
{
    char buf[32];
    GetPrivateProfileStringA("Bindings", "GlyphStyle", GlyphStyleName(outValue), buf, sizeof(buf), path);
    outValue = ParseGlyphStyle(buf, outValue);
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
        "; Save this file while the game is running and it hot-reloads within about a\n"
        "; second (v0.2.5+) -- no need to relaunch to see most changes take effect.\n"
        "; A real in-game options screen (sliders) is planned -- this file is the\n"
        "; interim way to tune these values until then.\n"
        "\n"
        "[Meta]\n"
        "; Internal schema marker, not a setting -- do not edit by hand. Lets this mod\n"
        "; carry your tuned values forward automatically across updates that rename or\n"
        "; restructure config keys (e.g. v0.2.5 splitting Sensitivity into\n"
        "; SensitivityHorizontal/SensitivityVertical), instead of silently reverting to\n"
        "; the new keys' defaults.\n"
        "ConfigVersion=%lu\n"
        "\n"
        "[Look]\n"
        "; Look-stick turn rate in degrees/second at full stick deflection, split into\n"
        "; horizontal (yaw, left/right) and vertical (pitch, up/down) axes -- separated\n"
        "; 2026-07-31 per user request, as a deliberate PC-side enhancement (real MW3\n"
        "; console has only ONE Sensitivity slider driving both axes through a fixed\n"
        "; internal ratio, not independently tunable). Which physical stick (and axes)\n"
        "; actually drive look depends on the StickLayout setting under [Bindings]\n"
        "; below -- this is not always the right stick.\n"
        "SensitivityHorizontal=%g\n"
        "; Corrected twice 2026-08-03 (issue #65): 250 (~80%% of Horizontal, wrongly assumed\n"
        "; to match console) -> 75 (an initial ~30%% feel-estimate, live-tested and found\n"
        "; way too slow) -> 145 (~58%%, corrected estimate: \"closer to about 55-60%%\").\n"
        "SensitivityVertical=%g\n"
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
        "; NOTE: this affects EVERY zoom level by the same relative percentage --\n"
        "; lowering it to fix low-zoom weapons also makes high-zoom scopes proportionally\n"
        "; slower, which live testing found perceptible (\"too harsh\"). For low-zoom-only\n"
        "; tuning, use AdsCloseRangeSlowdownStrength below instead.\n"
        "AdsSlowdownBaseline=%g\n"
        "; Extra slowdown that only matters for low-zoom weapons (pistols/iron sights,\n"
        "; where the zoom ratio stays close to 1.0) and decays away rapidly as zoom\n"
        "; increases, so it does NOT compound with the AdsSlowdownBaseline/Strength\n"
        "; curve's existing high-zoom feel the way lowering AdsSlowdownBaseline itself\n"
        "; does. 0 = off (no extra low-zoom slowdown); must stay in [0, 1] -- 1.0 would\n"
        "; fully zero out look while ADS'd with zero zoom, almost certainly too extreme.\n"
        "AdsCloseRangeSlowdownStrength=%g\n"
        "; OG console \"Invert Look\" -- flips vertical (up/down) look. 0 = off, 1 = on.\n"
        "InvertLook=%d\n"
        "; Milliseconds for look turn-rate to ramp from 0 to full speed after the stick\n"
        "; leaves neutral, matching real console MW2/Black Ops (confirmed via external\n"
        "; research to apply a linear ramp rather than instant full-rate response) --\n"
        "; this project's own look had none at all until 2026-07-19. Live playtesting\n"
        "; (2026-07-20) confirmed the ramp is tied to this old engine's locked 30fps\n"
        "; tick (33.33ms/frame), not an arbitrary wall-clock duration -- 33 = one\n"
        "; engine frame, the confirmed-correct default. 0 = off (instant response).\n"
        "AccelerationRampMs=%lu\n"
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
        "[Movement]\n"
        "; Auto-mantle: STRICTLY OFF BY DEFAULT, explicit opt-in only. When enabled,\n"
        "; automatically drives the real native mantle command (the same +gostand\n"
        "; command Jump already uses) whenever you're sprinting AND pushing the left\n"
        "; stick within the cone below of straight-forward, at or near full\n"
        "; deflection -- no separate Jump press needed near a real mantleable ledge.\n"
        "; A no-op on flat ground (the engine's own real condition flags decide\n"
        "; whether anything actually happens). 0 = off, 1 = on.\n"
        "AutoMantleEnabled=%d\n"
        "; Total cone width (not half-angle), in degrees, centered on straight-forward,\n"
        "; the left stick must fall within to count as \"pushing forward enough\".\n"
        "AutoMantleForwardConeDegrees=%g\n"
        "; Left stick deflection (0..1) must be at least this close to full to count\n"
        "; as \"full analog stick forward\", not just \"roughly forward\".\n"
        "AutoMantleMinStickMagnitude=%g\n"
        "\n"
        "; [Sprint] section removed 2026-07-19 (task #9): Sprint now drives the real\n"
        "; +sprint kbutton directly, so the engine's own native sprint duration/\n"
        "; recovery timer (and Extreme Conditioning's real override) apply\n"
        "; automatically -- LIVE-CONFIRMED. No config needed for this anymore.\n"
        "\n"
        "[Bindings]\n"
        "; OG console button layout presets, reconstructed from the unchanged CoD4->\n"
        "; MW2->MW3 console control scheme. Confirmed correct against real hardware,\n"
        "; 2026-07-19 -- including TacticalLefty.\n"
        "; One of: Default, Tactical, Lefty, TacticalLefty\n"
        "ButtonLayout=%s\n"
        "; One of: Default, Southpaw, Legacy, LegacySouthpaw\n"
        "StickLayout=%s\n"
        "; Independent toggle: swaps RT<->RB and LT<->LB (0 = off, 1 = on). Combines\n"
        "; with whichever ButtonLayout is active above.\n"
        "FlipTriggers=%d\n"
        "; Controller-glyph icon style (task #6) -- selects which real button-prompt art\n"
        "; is drawn (Xbox 360, Xbox One/Series, or PlayStation glyphs). Only takes\n"
        "; effect when GlyphStyleAuto below is 0 -- see that key's own comment.\n"
        "; One of: Xbox360, XboxModern, PlayStation\n"
        "GlyphStyle=%s\n"
        "; Auto-detects which controller is actually connected (DualSense -> PlayStation,\n"
        "; a Microsoft-vendored pad -> Xbox360/XboxModern by a best-effort VID/PID table)\n"
        "; and sets GlyphStyle above accordingly, once, the moment a controller is first\n"
        "; detected each session (matches this mod's existing 'restart to switch\n"
        "; controllers' rule -- it doesn't re-poll on a timer). Set to 0 to pick GlyphStyle\n"
        "; above manually instead (1 = on, 0 = off).\n"
        "GlyphStyleAuto=%d\n"
        "\n"
        "[CustomBinds]\n"
        "; Per-action override used when ButtonLayout=Custom above (issue #66's Binds\n"
        "; tab, in-game Options -> CUSTOM BINDS) -- lets you assign any physical\n"
        "; button to any action instead of picking from the 4 fixed presets. Ignored\n"
        "; entirely unless ButtonLayout=Custom. Values: RT, LT, RB, LB, X, Y, A, B,\n"
        "; LS, RS, Start, Back.\n"
        "Fire=%s\n"
        "Ads=%s\n"
        "Lethal=%s\n"
        "Tactical=%s\n"
        "ReloadUse=%s\n"
        "WeaponSwitch=%s\n"
        "Jump=%s\n"
        "CrouchProne=%s\n"
        "Sprint=%s\n"
        "Melee=%s\n"
        "Pause=%s\n"
        "Scoreboard=%s\n"
        "\n"
        "[Options]\n"
        "; Issue #66: replaces the ENTIRE real Options screen with a fully custom-drawn\n"
        "; one covering every real vanilla setting (Look/Video/Audio/Voice/Advanced\n"
        "; Video/Movement/Actions), not just this mod's own controller settings. STRICTLY\n"
        "; OPT-IN, default off -- a structurally significant, not-yet-verified change.\n"
        "; See VanillaLook/VanillaVideo/etc. sections below for the settings BACKUP this\n"
        "; also enables regardless of whether the replacement screen itself is on.\n"
        "; 0 = off (real native Options screen, unmodified), 1 = on.\n"
        "UseCustomOptionsScreen=%d\n"
        "\n"
        "[Video]\n"
        "; Issue #88: fixes a real engine behavior where the actual 3D scene renders\n"
        "; smaller than your real display, which looks visibly soft/dated (\"2005 bad\")\n"
        "; above 1080p. Set to 100 to render the scene at your real native resolution;\n"
        "; values above 100 genuinely supersample above native (real GPU/VRAM cost,\n"
        "; quadratic with the percentage); values below 100 downscale for performance.\n"
        "; 0 = disabled. Applied by overriding the engine's own requested render-\n"
        "; resolution input right before its one-time startup computation runs -- no\n"
        "; r_mode writes, no vid_restart, no live device recreation -- but since the\n"
        "; real render targets are only ever created once, a change requires RESTARTING\n"
        "; THE GAME, not just a hot-reload. Confirmed live (220fps at 100%% vs. 70-80fps\n"
        "; at 300%% on a real 2560x1440 display -- real GPU cost genuinely scales).\n"
        "InternalRenderScalePercent=%d\n"
        "; Issue #92: forces this DLL's own Direct3DCreate9 export to call the real\n"
        "; system d3d9.dll's Direct3DCreate9On12 entry point instead of the ordinary\n"
        "; one -- a real, Microsoft-documented alternate export from the SAME real DLL,\n"
        "; not a third-party renderer swap (D3D9On12 maps D3D9 onto D3D12, a genuine\n"
        "; Windows OS component).\n"
        "; *** DO NOT USE -- CONFIRMED UNSTABLE, 2026-08-26. *** Produces a real,\n"
        "; reproducible black screen (device alive, menu input keeps working, nothing\n"
        "; renders) under conditions this project could not fully root-cause after an\n"
        "; extensive live investigation -- ruled out: memory/address-space exhaustion,\n"
        "; GPU driver TDR (none logged), shader cache corruption (both real caches\n"
        "; deleted, still recurs), a GPU driver reset (Win+Ctrl+Shift+B, still recurs),\n"
        "; this project's own overlay drawing (fully bypassed, still recurs), this\n"
        "; project's own CreateTexture/Reset hooks (confirmed clean passthroughs). Real,\n"
        "; live-confirmed visual quality improvement at 100%% scale before it started\n"
        "; failing, but not safe to use. See known_issues.md issue #92 for the full\n"
        "; trail, including a real, open microsoft/D3D9On12 GitHub issue for black\n"
        "; screens on other games/GPUs. Left here (default 0/off) for future\n"
        "; investigation, not because it currently works. Try raising anisotropic\n"
        "; filtering (r_texFilterAnisoMax/Min in players2/config.cfg) on the normal\n"
        "; native driver instead for a similar sharpness improvement, safely.\n"
        "ForceD3D9On12=%d\n"
        "; --- Recommended companion settings for everything below (FSR/motion blur/\n"
        "; render-scale) -- confirmed live 2026-08-27, known_issues.md issue #99: this\n"
        "; engine's own camera-look pacing gets visibly worse under vsync (a real,\n"
        "; confirmed root cause, not this mod's own code -- the camera already updates\n"
        "; on every real rendered frame, so vsync-introduced frame-pacing irregularity\n"
        "; shows up directly as camera stutter). Also visible at high uncapped\n"
        "; framerates, since a fixed-rate engine like this one gets no extra real game-\n"
        "; state between frames beyond a point -- just more repeats of the same state.\n"
        "; Recommended: disable vsync (in-game and driver-level) and cap your framerate\n"
        "; with an external limiter instead, e.g. RivaTuner Statistics Server (RTSS),\n"
        "; set to whatever framerate feels smoothest for your own hardware/display --\n"
        "; not a specific mandated number, just a real limiter instead of vsync.\n"
        "; Phase B, visual-suite plan: FSR 1.0 RCAS (Robust Contrast Adaptive\n"
        "; Sharpening), a real full-screen sharpen pass built on the Phase A capture/\n"
        "; composite pipeline -- a direct port of AMD's real FidelityFX-FSR reference\n"
        "; math (MIT license, see re_notes/shaders/fsr_rcas.hlsl for the full port and\n"
        "; citation). PREVIEW/WIP -- builds and runs, not yet live-tuned for actual\n"
        "; visual quality. 0 = off, 1 = on.\n"
        "FsrSharpenEnabled=%d\n"
        "; 0.0-1.0, RCAS's own real sharpen-lobe scale. 0.0 = negligible effect, 1.0 =\n"
        "; RCAS's real maximum sharpening. Only takes effect while FsrSharpenEnabled=1.\n"
        "; Live-tested 2026-08-26: 0.5 (the original default) was reported \"needs more\n"
        "; softness\" -- lowered to 0.3.\n"
        "FsrSharpenStrength=%.2f\n"
        "; Phase E, visual-suite plan: camera-only (view-angle-delta-based) directional\n"
        "; motion blur, built on the same pipeline -- composes with FsrSharpenEnabled\n"
        "; above when both are on (runs in sequence, after RCAS). Driven by real per-\n"
        "; frame degrees-of-rotation from controller stick/gyro look only, NOT mouse\n"
        "; look. PREVIEW/WIP -- builds and runs, genuinely untuned; expect to need\n"
        "; real live-tuning via MotionBlurStrength below. 0 = off, 1 = on.\n"
        "MotionBlurEnabled=%d\n"
        "; Multiplies the real per-frame yaw/pitch delta before converting to blur\n"
        "; extent -- 1.0 = the shipped default scale, 0.0 = no blur regardless of\n"
        "; camera motion. A hard safety clamp in the shader-setup code caps how far a\n"
        "; single fast turn or frame-time hitch can smear, so raising this very high\n"
        "; is safe (just increasingly less effective) rather than ever looking broken.\n"
        "MotionBlurStrength=%.2f\n"
        "; 0.0-1.0: 0.0 = uniform blur everywhere (the original behavior). 1.0 = a real\n"
        "; center-to-edge radial falloff -- blur fades to ~0 exactly at screen center,\n"
        "; reaching full MotionBlurStrength only at the farthest on-screen point (a\n"
        "; corner). Keeps the real focal point sharp while peripheral vision smears\n"
        "; more. Default 1.0 -- this IS the requested behavior, not an opt-in extra.\n"
        "MotionBlurCenterFalloff=%.2f\n"
        "; Writes the real native r_texFilterAnisoMax/r_texFilterAnisoMin dvars to 16\n"
        "; (maximum) via this project's own real dvar-write mechanism -- the same\n"
        "; sharpness improvement this session's own ForceD3D9On12 investigation (issue\n"
        "; #92) already confirmed live and safe as a hand-edited players2/config.cfg\n"
        "; value; this just makes it a mod-config toggle that survives a native\n"
        "; \"Restore Defaults\" or a fresh profile instead. 0 = off, 1 = on.\n"
        "ForceAnisotropicFiltering=%d\n"
        "\n"
        "[Plugins]\n"
        "; Loads plugin DLLs from a \"plugins\" subfolder next to this DLL at startup.\n"
        "; Plugins get hook-installation and DIRECT PROCESS MEMORY READ/WRITE access --\n"
        "; capability this project's own main mod deliberately never uses on itself (see\n"
        "; re_notes/known_issues.md issue #33 and the permanently-removed aim-assist\n"
        "; feature). A plugin is YOUR OWN code, or someone else's -- not vetted, reviewed,\n"
        "; or shipped by this project. See PLUGIN_API.md for the full design and an\n"
        "; explicit risk statement before enabling this. STRICTLY OPT-IN. 0 = off (no\n"
        "; plugins directory scan happens at all), 1 = on.\n"
        "Enabled=%d\n"
        "\n"
        "[Vibration]\n"
        "; Real XInputSetState output, driven off real weapon-fire and damage-taken\n"
        "; events (fixed 2026-08-03 -- was silently a no-op before, see\n"
        "; known_issues.md issue #24: xinput9_1_0.dll's own SetState doesn't produce\n"
        "; real vibration on most Windows installs; now tries xinput1_4/xinput1_3\n"
        "; first). 0 = off, 1 = on.\n"
        "Enabled=%d\n"
        "; Motor strength [0,1] on each real shot fired. Bumped three times on\n"
        "; 2026-08-03: 0.25->0.55->0.85->1.0 (its own ceiling -- XInputSetState's real\n"
        "; motor-speed fields are already commanded at their maximum, 65535, here).\n"
        "; If STILL weak at this value on the same physical controller, there is no\n"
        "; higher software value to send -- the remaining variable is very likely the\n"
        "; controller's own hardware (third-party/Bluetooth pads commonly have\n"
        "; materially weaker real motor response than a genuine Xbox controller).\n"
        "FireIntensity=%g\n"
        "; Milliseconds a fire pulse takes to decay back to zero. Bumped 60->90->150->180\n"
        "; across three 2026-08-03 rounds -- real vibration motors have physical\n"
        "; spin-up lag (~50-100ms) before reaching a speed you can feel; a pulse now\n"
        "; HOLDS at full strength for its first ~70%% before decaying (rumble.cpp's\n"
        "; kRumbleSustainFraction) instead of decaying the whole time.\n"
        "FireDurationMs=%lu\n"
        "; Motor strength added per point of real damage the LOCAL player takes.\n"
        "; Bumped 0.03->0.05->0.08->0.12 across the same three \"too weak\" rounds as\n"
        "; FireIntensity above. NOTE: does not yet register hits absorbed by\n"
        "; Survival's purchasable Body Armor (a separate value from real health this\n"
        "; project hasn't located yet -- see known_issues.md issue #63).\n"
        "DamagePerPoint=%g\n"
        "; Hard cap on damage-rumble strength regardless of how much damage lands.\n"
        "DamageMaxIntensity=%g\n"
        "; Milliseconds a damage pulse takes to decay back to zero. Bumped 200->250->280\n"
        "; (2026-08-03) alongside the sustain/decay envelope change above.\n"
        "DamageDurationMs=%lu\n"
        "\n"
        "[Overlay]\n"
        "; Font used for MOST on-screen text -- notification toasts, most HUD hints, and\n"
        "; the custom Options screen. Default \"Isotherm Sans UI\" is bundled directly in\n"
        "; this DLL (2026-07-31 follow-up, swapped from Barlow Condensed SemiBold, then\n"
        "; split into two selectable variants, both 2026-08-24) -- no system font install\n"
        "; required. Set this to any OTHER font's real name to use a system-installed\n"
        "; font instead -- if the name doesn't resolve, GDI silently falls back to a\n"
        "; default system font, same as a missing font always has.\n"
        "FontFamily=%s\n"
        "; Font used ONLY by the specific hint call sites that explicitly request the\n"
        "; Condensed role (throwback prompt, sentry gun placement) -- everything else\n"
        "; uses FontFamily above regardless of this key. Empty (the default) means use\n"
        "; the bundled \"Isotherm Sans\" (Condensed style). Same system-font override\n"
        "; mechanism as FontFamily, independent of it -- set both to get a fully custom\n"
        "; look, or just this one to keep the default look everywhere else.\n"
        "FontFamilyCondensed=%s\n"
        "; 1 = italic, 0 = upright (default 0 -- italic was a Barlow Condensed-era\n"
        "; styling choice, not a property of the bundled fonts; both Isotherm Sans\n"
        "; variants default to upright). Uses the active variant's real Italic style if\n"
        "; it has one, GDI's own synthesized oblique otherwise.\n"
        "FontItalic=%d\n"
        "; STRICTLY A TESTING TOGGLE, default off. When on, continuously cycles\n"
        "; through every known message/animation-style variant every few seconds\n"
        "; instead of the normal one-shot startup roll, so every variant can actually\n"
        "; be seen on demand rather than waiting on a 1-in-20 RNG roll. Never enable\n"
        "; this for normal play.\n"
        "TestCycleAllVariants=%d\n"
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
        "; Task #6/#35: the bind-resolver text hook (FUN_0061f6f0) is always installed\n"
        "; and always forwards to the real game logic completely unmodified (log-only\n"
        "; first pass -- no glyph substitution happens yet). This only controls whether\n"
        "; it logs what it observes to proxy_d3d9.log. 0 = off, 1 = on.\n"
        "BindResolverHookLogging=%d\n"
        "; Task #6/#35: overwrite resolved hint text with a controller-glyph\n"
        "; codepoint when a mapping exists for the current GlyphStyle and a\n"
        "; controller is connected. DEFAULT OFF ON PURPOSE -- no font asset the\n"
        "; running game can load yet actually renders these codepoints (see\n"
        "; re_notes/known_issues.md issue #23), so enabling this today replaces\n"
        "; readable text with missing-glyph boxes. Leave at 0 until that's resolved.\n"
        "BindResolverGlyphSubstitution=%d\n"
        "; Task #6/#34: read-only diagnostic hook on the real glyph-draw call, logs the\n"
        "; real font name in use for on-screen HUD/menu text whenever it changes. Always\n"
        "; forwards unmodified regardless of this toggle. 0 = off, 1 = on.\n"
        "HudFontIdLogging=%d\n"
        "; Issue #48: read-only diagnostic, same hook site as HudFontIdLogging above but\n"
        "; dedup'd by drawn text changing and logs the full raw position/scale/color\n"
        "; parameter set instead of just the font name. Investigation-only toggle for\n"
        "; the overlay-quad glyph-icon pivot -- turn on, reproduce a real interact-hint\n"
        "; prompt, check proxy_d3d9.log for \"[hud-glyph-pos]\" lines, then turn back off.\n"
        "; Always forwards unmodified regardless of this toggle. 0 = off, 1 = on.\n"
        "HudGlyphPositionLogging=%d\n"
        "; Issue #67 log-slimming pass (2026-08-08): [list-item-diag], the ordinal-\n"
        "; based menu-item-position fallback's own diagnostic -- used to log\n"
        "; unconditionally on EVERY menu list item drawn, a real contributor to\n"
        "; proxy_d3d9.log growing to ~22GB in one session. Still occasionally useful\n"
        "; for calibrating a new menu screen without a manual position table entry\n"
        "; yet (issue #51), just not worth paying for on every frame of every session.\n"
        "; DEFAULT OFF -- turn on, reproduce the specific screen needing calibration,\n"
        "; check proxy_d3d9.log for \"[list-item-diag]\" lines, then turn back off.\n"
        "; Always forwards unmodified regardless of this toggle. 0 = off, 1 = on.\n"
        "ListItemPositionLogging=%d\n"
        "; Issue #63 follow-up: Survival's purchasable Body Armor absorbs hits\n"
        "; separately from real health, so the damage-rumble health poll never\n"
        "; fires while armored -- the real armor field's memory location isn't\n"
        "; known yet (a round-1 capture, 2026-08-03, confirmed entity+0x58 is\n"
        "; current ammo, not armor, and excluded it; the real field is still\n"
        "; unidentified). DEFAULT OFF. When on, scans the local player's entity\n"
        "; struct every tick for a value that was stable then dropped by a\n"
        "; plausible hit amount, logging candidates (max 3 per offset) as\n"
        "; \"[armor-scan-diag]\". Turn on, take a few hits while armored in\n"
        "; Survival, then check proxy_d3d9.log. 0 = off, 1 = on.\n"
        "ArmorFieldScanLogging=%d\n"
        "; 2026-08-08: community-reported (Nexus, v0.3.1) -- some players see NO\n"
        "; controller-glyph icons at all, even on English with default settings, and it's\n"
        "; not reproducible on the developer's own machine -- likely a per-environment\n"
        "; issue (e.g. the controller sitting in a non-zero XInput slot, which this\n"
        "; project's XInput reads don't currently scan for). DEFAULT OFF -- a one-off\n"
        "; diagnostic toggle. When on, the glyph/hint overlay draws regardless of whether\n"
        "; a controller is currently detected as the active input method -- if icons then\n"
        "; show correctly, report that back, it narrows this down a lot. 0 = off, 1 = on.\n"
        "ForceGlyphOverlay=%d\n"
        "; 2026-08-16, issue #51 follow-up -- the in-game click-and-drag menu-glyph\n"
        "; calibration tool. DEFAULT OFF, only ever turn on for an active calibration\n"
        "; session. This is the master gate only -- once on, press F2 in-game to actually\n"
        "; activate live dragging: the currently-focused real menu-list item's glyph\n"
        "; position becomes draggable with the mouse, seeded from the existing calibrated\n"
        "; position if one exists. Press F3 to export every group touched this session to\n"
        "; exported_glyph_positions.txt, next to this DLL. 0 = off, 1 = on.\n"
        "GlyphPositionEditMode=%d\n"
        "; 2026-08-17, tools/ui_harness .menu-renderer project -- dumps real material\n"
        "; textures the game actually creates while playing to\n"
        "; <gameDir>\\runtime_asset_capture\\materials\\<materialName>.dds, for the harness's\n"
        "; own .menu renderer to use instead of the incomplete static zone_dump extraction.\n"
        "; DEFAULT OFF -- dev-only diagnostic, not for normal play. Turn on, play a session\n"
        "; touching the menus you want captured, turn back off. 0 = off, 1 = on.\n"
        "CaptureRuntimeMenuAssets=%d\n"
        "; 2026-08-17: live-reported stutter investigation -- writes\n"
        "; frametime_benchmark.csv (next to this DLL) with real per-frame timing, one row\n"
        "; per frame, including a breakdown of this project's own hook durations, so a\n"
        "; felt stutter can be checked against real data instead of guessed. DEFAULT OFF --\n"
        "; per-frame disk I/O has its own real cost. Turn on, play until the stutter is\n"
        "; felt, turn back off, share frametime_benchmark.csv. 0 = off, 1 = on.\n"
        "FrametimeBenchmarkLogging=%d\n"
        "; Issue #92, 2026-08-26: logs real process memory (working set, private bytes,\n"
        "; pagefile usage) and system memory (load %%, available physical/virtual) once a\n"
        "; second, to check whether a crash at high InternalRenderScalePercent under\n"
        "; ForceD3D9On12 is real address-space/memory exhaustion. DEFAULT OFF. Turn on,\n"
        "; reproduce, check proxy_d3d9.log for \"[resource-diag]\" lines. 0 = off, 1 = on.\n"
        "ResourceUsageLogging=%d\n"
        "; Phase A, visual-suite plan, 2026-08-26: validates the new full-screen\n"
        "; capture/composite pipeline with a trivial no-op shader, before any real effect\n"
        "; (sharpening/FXAA/motion blur) is built on it. Should look IDENTICAL to off --\n"
        "; a temporary plumbing-validation toggle, not a real feature. DEFAULT OFF.\n"
        "; 0 = off, 1 = on.\n"
        "FullScreenPassthroughTest=%d\n",
        kCurrentConfigVersion,
        g_modConfig.lookDegreesPerSecondHorizontal,
        g_modConfig.lookDegreesPerSecondVertical,
        g_modConfig.adsSlowdownStrength,
        g_modConfig.adsSlowdownBaseline,
        g_modConfig.adsCloseRangeSlowdownStrength,
        g_modConfig.invertLook ? 1 : 0,
        g_modConfig.lookAccelerationRampMs,
        g_modConfig.proneHoldThresholdMs,
        g_modConfig.interactHoldThresholdMs,
        g_modConfig.readyUpHoldThresholdMs,
        g_modConfig.autoMantleEnabled ? 1 : 0,
        g_modConfig.autoMantleForwardConeDegrees,
        g_modConfig.autoMantleMinStickMagnitude,
        ButtonLayoutName(g_modConfig.buttonLayout),
        StickLayoutName(g_modConfig.stickLayout),
        g_modConfig.flipTriggers ? 1 : 0,
        GlyphStyleName(g_modConfig.glyphStyle),
        g_modConfig.glyphStyleAuto ? 1 : 0,
        PhysicalInputName(g_modConfig.customButtonMap.fire),
        PhysicalInputName(g_modConfig.customButtonMap.ads),
        PhysicalInputName(g_modConfig.customButtonMap.lethal),
        PhysicalInputName(g_modConfig.customButtonMap.tactical),
        PhysicalInputName(g_modConfig.customButtonMap.reloadUse),
        PhysicalInputName(g_modConfig.customButtonMap.weaponSwitch),
        PhysicalInputName(g_modConfig.customButtonMap.jump),
        PhysicalInputName(g_modConfig.customButtonMap.crouchProne),
        PhysicalInputName(g_modConfig.customButtonMap.sprint),
        PhysicalInputName(g_modConfig.customButtonMap.melee),
        PhysicalInputName(g_modConfig.customButtonMap.pause),
        PhysicalInputName(g_modConfig.customButtonMap.scoreboard),
        g_modConfig.useCustomOptionsScreen ? 1 : 0,
        g_modConfig.internalRenderScalePercent,
        g_modConfig.forceD3D9On12 ? 1 : 0,
        g_modConfig.fsrSharpenEnabled ? 1 : 0,
        g_modConfig.fsrSharpenStrength,
        g_modConfig.motionBlurEnabled ? 1 : 0,
        g_modConfig.motionBlurStrength,
        g_modConfig.motionBlurCenterFalloff,
        g_modConfig.forceAnisotropicFiltering ? 1 : 0,
        g_modConfig.pluginsEnabled ? 1 : 0,
        g_modConfig.vibrationEnabled ? 1 : 0,
        g_modConfig.vibrationFireIntensity,
        g_modConfig.vibrationFireDurationMs,
        g_modConfig.vibrationDamagePerPoint,
        g_modConfig.vibrationDamageMaxIntensity,
        g_modConfig.vibrationDamageDurationMs,
        g_modConfig.overlayFontFamily,
        g_modConfig.overlayFontFamilyCondensed,
        g_modConfig.overlayFontItalic ? 1 : 0,
        g_modConfig.overlayTestCycleAllVariants ? 1 : 0,
        g_modConfig.fireNotifyQueueKick ? 1 : 0,
        g_modConfig.bindResolverHookLogging ? 1 : 0,
        g_modConfig.bindResolverGlyphSubstitution ? 1 : 0,
        g_modConfig.hudFontIdLogging ? 1 : 0,
        g_modConfig.hudGlyphPositionLogging ? 1 : 0,
        g_modConfig.listItemPositionLogging ? 1 : 0,
        g_modConfig.armorFieldScanLogging ? 1 : 0,
        g_modConfig.forceGlyphOverlay ? 1 : 0,
        g_modConfig.glyphPositionEditMode ? 1 : 0,
        g_modConfig.captureRuntimeMenuAssets ? 1 : 0,
        g_modConfig.frametimeBenchmarkLogging ? 1 : 0,
        g_modConfig.resourceUsageLogging ? 1 : 0,
        g_modConfig.fullScreenPassthroughTest ? 1 : 0);

    fclose(f);
}

} // namespace

// ---- Button layout resolution (task #15) ------------------------------------------
//
// Tables below are the user-supplied reconstruction of the unchanged CoD4->MW2->MW3
// console button layouts (see mod_config.h's enum comments -- all four presets,
// including TacticalLefty, are confirmed correct against real hardware as of
// 2026-07-19). TacticalLefty is Lefty with Tactical's face-button swap (Crouch/
// Melee) applied on top of Lefty's own already-swapped stick-click assignments
// (Sprint/Melee) -- taken as given directly from the user's own final resolved
// table, not re-derived here.
ButtonMap ResolveButtonMap(ButtonLayout layout, bool flipTriggers)
{
    // Custom (2026-08-06, issue #66's Binds tab): the player's own exact per-action
    // assignment, already fully resolved -- returned as-is, deliberately bypassing
    // flipTriggers below (flipping a layout the player explicitly hand-configured
    // would silently undo their own choice, unlike the 4 real presets where flip is
    // a documented modifier on top of a known-fixed starting layout).
    if (layout == ButtonLayout::Custom) return g_modConfig.customButtonMap;

    ButtonMap m; // struct defaults already match ButtonLayout::Default

    switch (layout) {
        case ButtonLayout::Default:
        case ButtonLayout::Custom: // unreachable (handled above) -- listed to silence -Wswitch
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

    unsigned long configVersion = static_cast<unsigned long>(GetPrivateProfileIntA("Meta", "ConfigVersion", 0, path));
    bool migratedLegacyValue = false;

    // v0.2.5 (task #14 follow-up): [Look] Sensitivity split into
    // SensitivityHorizontal/SensitivityVertical. Carry an existing single value over
    // to BOTH new axes -- the only sane "equivalent" reading of a pre-split value,
    // since the split only matters once a player actually wants to diverge them --
    // rather than silently reverting a tuned value back to the new keys' 250.0f
    // struct-initializer default. Pre-seeds the two fields BEFORE the normal
    // ReadFloat calls below run, so ReadFloat's own "use current value as fallback if
    // the key is missing" behavior picks up the migrated value instead of the
    // compile-time default on an old file that has Sensitivity but not yet either new
    // key (an explicit SensitivityHorizontal/Vertical in the file, e.g. if a user
    // hand-edited one in already, still wins over this, same as any other key).
    if (configVersion < 1) {
        float legacySensitivity;
        if (TryReadLegacyFloat(path, "Look", "Sensitivity", legacySensitivity)) {
            g_modConfig.lookDegreesPerSecondHorizontal = legacySensitivity;
            g_modConfig.lookDegreesPerSecondVertical = legacySensitivity;
            migratedLegacyValue = true;
        }
    }

    ReadFloat(path, "Look", "SensitivityHorizontal", g_modConfig.lookDegreesPerSecondHorizontal);
    // Corrected 2026-08-03 (issue #65): the 250 default was an ~80% ratio of Horizontal,
    // wrongly assumed to match console -- real MW3 console vertical sensitivity is ~30% of
    // horizontal per direct user testimony from actual console play, corroborated by the
    // real console Options menu having only ONE Sensitivity slider at all (no independent
    // vertical control there). Retuned to 75 (30% of Horizontal's 250). Gated at
    // fromVersion=11 so a file already customized away from 250 is never silently overwritten.
    ReadFloatWithDefaultRetune(path, "Look", "SensitivityVertical", configVersion, 11, 250.0f,
                               g_modConfig.lookDegreesPerSecondVertical);
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
    // issue #44 (2026-07-31): a same-day round trip. The default first dropped
    // 0.65 -> 0.45 to make pistols/iron sights more slowed, then got reverted back
    // to 0.65 once live testing showed that also made high-zoom scopes "too harsh"
    // (see mod_config.h's own comment on adsSlowdownBaseline for the full math). A
    // plain default change can't reach an existing file the way a key rename can
    // (the key never disappeared), so this corrects a file still sitting on the
    // now-wrong, short-lived 0.45 back to the restored 0.65 -- see
    // ReadFloatWithDefaultRetune's own comment for the full policy. Gated at
    // fromVersion=3 since this correction was added after some files may already
    // have been bumped past version 2 while 0.45 was still the compiled default.
    ReadFloatWithDefaultRetune(path, "Look", "AdsSlowdownBaseline", configVersion, 3, 0.45f,
                               g_modConfig.adsSlowdownBaseline);
    // Same guard as strength above -- a negative baseline would flip the sign of the
    // whole scale factor (baseline * ratio^strength), inverting look direction.
    if (g_modConfig.adsSlowdownBaseline < 0.0f) g_modConfig.adsSlowdownBaseline = 0.0f;
    // Issue #44's real, decoupled fix (2026-07-31) -- see mod_config.h's own comment.
    // Clamped to [0, 1]: negative would ADD speed at low zoom (nonsensical), and
    // above 1.0 the (1 - strength*ratio^power) term could go negative, which would
    // invert look direction the same class of bug as the old AdsSlowdownStrength
    // linear-blend issue this project already fixed once before (see that comment).
    ReadFloat(path, "Look", "AdsCloseRangeSlowdownStrength", g_modConfig.adsCloseRangeSlowdownStrength);
    if (g_modConfig.adsCloseRangeSlowdownStrength < 0.0f) g_modConfig.adsCloseRangeSlowdownStrength = 0.0f;
    if (g_modConfig.adsCloseRangeSlowdownStrength > 1.0f) g_modConfig.adsCloseRangeSlowdownStrength = 1.0f;
    ReadBool(path, "Look", "InvertLook", g_modConfig.invertLook);
    ReadUlong(path, "Look", "AccelerationRampMs", g_modConfig.lookAccelerationRampMs);
    ReadUlong(path, "Stance", "ProneHoldThresholdMs", g_modConfig.proneHoldThresholdMs);
    ReadUlong(path, "Interact", "HoldThresholdMs", g_modConfig.interactHoldThresholdMs);
    ReadUlong(path, "Survival", "ReadyUpHoldThresholdMs", g_modConfig.readyUpHoldThresholdMs);
    ReadBool(path, "Movement", "AutoMantleEnabled", g_modConfig.autoMantleEnabled);
    ReadFloat(path, "Movement", "AutoMantleForwardConeDegrees", g_modConfig.autoMantleForwardConeDegrees);
    if (g_modConfig.autoMantleForwardConeDegrees < 1.0f) g_modConfig.autoMantleForwardConeDegrees = 1.0f;
    if (g_modConfig.autoMantleForwardConeDegrees > 180.0f) g_modConfig.autoMantleForwardConeDegrees = 180.0f;
    ReadFloat(path, "Movement", "AutoMantleMinStickMagnitude", g_modConfig.autoMantleMinStickMagnitude);
    if (g_modConfig.autoMantleMinStickMagnitude < 0.0f) g_modConfig.autoMantleMinStickMagnitude = 0.0f;
    if (g_modConfig.autoMantleMinStickMagnitude > 1.0f) g_modConfig.autoMantleMinStickMagnitude = 1.0f;
    // [Sprint] MaxStaminaSeconds/RegenSeconds removed 2026-07-19 (task #9): the real
    // +sprint kbutton migration made this mod's own custom stamina/cooldown timer
    // (and its divide-by-zero guard that used to live here) dead code -- the engine's
    // own native timer now applies automatically. See mod_config.h's [Sprint] comment.
    ReadButtonLayout(path, g_modConfig.buttonLayout);
    ReadCustomButtonMap(path, g_modConfig.customButtonMap);
    ReadStickLayout(path, g_modConfig.stickLayout);
    ReadBool(path, "Bindings", "FlipTriggers", g_modConfig.flipTriggers);
    ReadGlyphStyle(path, g_modConfig.glyphStyle);
    // GlyphStyleAuto (2026-08-25) -- a brand-new key, so a plain ReadBool (default =
    // whatever g_modConfig.glyphStyleAuto was already compiled to, true) would be
    // correct for the common case (fresh install, or an upgrader who's never touched
    // GlyphStyle and is still sitting on its Xbox360 compiled default) but WRONG for
    // the one real edge case this project's own "explicit value always wins" policy
    // (see FontItalic's migration precedent above) exists to protect: a player who
    // hand-edited GlyphStyle in their ini to XboxModern/PlayStation before this
    // feature existed. There's no separate "was this deliberately set" flag for
    // GlyphStyle to check (unlike FontItalic's version-gated exact-old-default
    // check), so the on-disk GlyphStyle value itself is read FIRST (immediately
    // above) and used as that signal instead: if the key is genuinely absent (a file
    // that predates this feature) AND the loaded GlyphStyle isn't the historical
    // Xbox360 default, treat that as a deliberate manual choice and default
    // glyphStyleAuto to false rather than silently overriding it the next poll tick.
    {
        char buf[8];
        GetPrivateProfileStringA("Bindings", "GlyphStyleAuto", "", buf, sizeof(buf), path);
        if (buf[0] != '\0') {
            g_modConfig.glyphStyleAuto = (atoi(buf) != 0);
        } else if (g_modConfig.glyphStyle != GlyphStyle::Xbox360) {
            g_modConfig.glyphStyleAuto = false;
        } // else: key absent, GlyphStyle at its historical default -- keep the compiled true
    }
    ReadBool(path, "Options", "UseCustomOptionsScreen", g_modConfig.useCustomOptionsScreen);
    ReadBool(path, "Plugins", "Enabled", g_modConfig.pluginsEnabled);
    ReadBool(path, "Vibration", "Enabled", g_modConfig.vibrationEnabled);
    ReadFloat(path, "Vibration", "FireIntensity", g_modConfig.vibrationFireIntensity);
    if (g_modConfig.vibrationFireIntensity < 0.0f) g_modConfig.vibrationFireIntensity = 0.0f;
    ReadUlong(path, "Vibration", "FireDurationMs", g_modConfig.vibrationFireDurationMs);
    ReadFloat(path, "Vibration", "DamagePerPoint", g_modConfig.vibrationDamagePerPoint);
    if (g_modConfig.vibrationDamagePerPoint < 0.0f) g_modConfig.vibrationDamagePerPoint = 0.0f;
    ReadFloat(path, "Vibration", "DamageMaxIntensity", g_modConfig.vibrationDamageMaxIntensity);
    if (g_modConfig.vibrationDamageMaxIntensity < 0.0f) g_modConfig.vibrationDamageMaxIntensity = 0.0f;
    ReadUlong(path, "Vibration", "DamageDurationMs", g_modConfig.vibrationDamageDurationMs);
    {
        // Not read via GetPrivateProfileStringA directly into g_modConfig.overlayFontFamily
        // (as both lpDefault and lpReturnedString) -- overlapping in/out buffers on this API
        // are an unnecessary risk to take for no real benefit; a local temp buffer avoids it.
        char buf[sizeof(g_modConfig.overlayFontFamily)];
        GetPrivateProfileStringA("Overlay", "FontFamily", g_modConfig.overlayFontFamily,
            buf, sizeof(buf), path);
        strncpy_s(g_modConfig.overlayFontFamily, buf, _TRUNCATE);
    }
    {
        char buf[sizeof(g_modConfig.overlayFontFamilyCondensed)];
        GetPrivateProfileStringA("Overlay", "FontFamilyCondensed", g_modConfig.overlayFontFamilyCondensed,
            buf, sizeof(buf), path);
        strncpy_s(g_modConfig.overlayFontFamilyCondensed, buf, _TRUNCATE);
    }
    ReadBool(path, "Overlay", "FontItalic", g_modConfig.overlayFontItalic);
    ReadBool(path, "Overlay", "TestCycleAllVariants", g_modConfig.overlayTestCycleAllVariants);
    ReadBool(path, "Experimental", "FireNotifyQueueKick", g_modConfig.fireNotifyQueueKick);
    ReadBool(path, "Experimental", "BindResolverHookLogging", g_modConfig.bindResolverHookLogging);
    ReadBool(path, "Experimental", "BindResolverGlyphSubstitution", g_modConfig.bindResolverGlyphSubstitution);
    ReadBool(path, "Experimental", "HudFontIdLogging", g_modConfig.hudFontIdLogging);
    ReadBool(path, "Experimental", "HudGlyphPositionLogging", g_modConfig.hudGlyphPositionLogging);
    ReadBool(path, "Experimental", "ListItemPositionLogging", g_modConfig.listItemPositionLogging);
    ReadBool(path, "Experimental", "ArmorFieldScanLogging", g_modConfig.armorFieldScanLogging);
    ReadBool(path, "Experimental", "ForceGlyphOverlay", g_modConfig.forceGlyphOverlay);
    ReadBool(path, "Experimental", "GlyphPositionEditMode", g_modConfig.glyphPositionEditMode);
    ReadBool(path, "Experimental", "CaptureRuntimeMenuAssets", g_modConfig.captureRuntimeMenuAssets);
    ReadBool(path, "Experimental", "FrametimeBenchmarkLogging", g_modConfig.frametimeBenchmarkLogging);
    ReadBool(path, "Experimental", "ResourceUsageLogging", g_modConfig.resourceUsageLogging);
    ReadBool(path, "Experimental", "FullScreenPassthroughTest", g_modConfig.fullScreenPassthroughTest);
    ReadBool(path, "Gyro", "Enabled", g_modConfig.gyroEnabled);
    ReadFloat(path, "Gyro", "Sensitivity", g_modConfig.gyroSensitivity);
    if (g_modConfig.gyroSensitivity < 0.0f) g_modConfig.gyroSensitivity = 0.0f;
    ReadBool(path, "Gyro", "InvertPitch", g_modConfig.gyroInvertPitch);
    ReadBool(path, "Gyro", "InvertYaw", g_modConfig.gyroInvertYaw);
    {
        int v = GetPrivateProfileIntA("Video", "InternalRenderScalePercent", g_modConfig.internalRenderScalePercent, path);
        g_modConfig.internalRenderScalePercent = v;
    }
    ReadBool(path, "Video", "ForceD3D9On12", g_modConfig.forceD3D9On12);
    ReadBool(path, "Video", "FsrSharpenEnabled", g_modConfig.fsrSharpenEnabled);
    ReadFloat(path, "Video", "FsrSharpenStrength", g_modConfig.fsrSharpenStrength);
    if (g_modConfig.fsrSharpenStrength < 0.0f) g_modConfig.fsrSharpenStrength = 0.0f;
    if (g_modConfig.fsrSharpenStrength > 1.0f) g_modConfig.fsrSharpenStrength = 1.0f;
    ReadBool(path, "Video", "MotionBlurEnabled", g_modConfig.motionBlurEnabled);
    ReadFloat(path, "Video", "MotionBlurStrength", g_modConfig.motionBlurStrength);
    if (g_modConfig.motionBlurStrength < 0.0f) g_modConfig.motionBlurStrength = 0.0f;
    ReadFloat(path, "Video", "MotionBlurCenterFalloff", g_modConfig.motionBlurCenterFalloff);
    if (g_modConfig.motionBlurCenterFalloff < 0.0f) g_modConfig.motionBlurCenterFalloff = 0.0f;
    if (g_modConfig.motionBlurCenterFalloff > 1.0f) g_modConfig.motionBlurCenterFalloff = 1.0f;
    ReadBool(path, "Video", "ForceAnisotropicFiltering", g_modConfig.forceAnisotropicFiltering);

    g_buttonMap = ResolveButtonMap(g_modConfig.buttonLayout, g_modConfig.flipTriggers);

    char buf[1024];
    sprintf_s(buf,
        "[config] loaded mw3ncp_config.ini: sensitivityH=%g sensitivityV=%g adsSlowdownStrength=%g "
        "adsSlowdownBaseline=%g adsCloseRangeSlowdownStrength=%g invertLook=%d lookAccelRampMs=%lu proneHoldMs=%lu interactHoldMs=%lu "
        "readyUpHoldMs=%lu "
        "buttonLayout=%s stickLayout=%s flipTriggers=%d glyphStyle=%s glyphStyleAuto=%d "
        "useCustomOptionsScreen=%d internalRenderScalePercent=%d forceD3D9On12=%d pluginsEnabled=%d "
        "vibrationEnabled=%d vibrationFireIntensity=%g vibrationFireDurationMs=%lu "
        "vibrationDamagePerPoint=%g vibrationDamageMaxIntensity=%g vibrationDamageDurationMs=%lu "
        "overlayFontFamily=%s overlayFontFamilyCondensed=%s overlayFontItalic=%d overlayTestCycleAllVariants=%d "
        "fireNotifyQueueKick=%d bindResolverHookLogging=%d bindResolverGlyphSubstitution=%d "
        "hudFontIdLogging=%d hudGlyphPositionLogging=%d listItemPositionLogging=%d "
        "armorFieldScanLogging=%d forceGlyphOverlay=%d glyphPositionEditMode=%d "
        "captureRuntimeMenuAssets=%d frametimeBenchmarkLogging=%d",
        g_modConfig.lookDegreesPerSecondHorizontal, g_modConfig.lookDegreesPerSecondVertical,
        g_modConfig.adsSlowdownStrength,
        g_modConfig.adsSlowdownBaseline,
        g_modConfig.adsCloseRangeSlowdownStrength,
        g_modConfig.invertLook ? 1 : 0, g_modConfig.lookAccelerationRampMs,
        g_modConfig.proneHoldThresholdMs,
        g_modConfig.interactHoldThresholdMs, g_modConfig.readyUpHoldThresholdMs,
        ButtonLayoutName(g_modConfig.buttonLayout), StickLayoutName(g_modConfig.stickLayout),
        g_modConfig.flipTriggers ? 1 : 0, GlyphStyleName(g_modConfig.glyphStyle),
        g_modConfig.glyphStyleAuto ? 1 : 0,
        g_modConfig.useCustomOptionsScreen ? 1 : 0,
        g_modConfig.internalRenderScalePercent,
        g_modConfig.forceD3D9On12 ? 1 : 0,
        g_modConfig.pluginsEnabled ? 1 : 0,
        g_modConfig.vibrationEnabled ? 1 : 0, g_modConfig.vibrationFireIntensity,
        g_modConfig.vibrationFireDurationMs, g_modConfig.vibrationDamagePerPoint,
        g_modConfig.vibrationDamageMaxIntensity, g_modConfig.vibrationDamageDurationMs,
        g_modConfig.overlayFontFamily,
        g_modConfig.overlayFontFamilyCondensed,
        g_modConfig.overlayFontItalic ? 1 : 0,
        g_modConfig.overlayTestCycleAllVariants ? 1 : 0,
        g_modConfig.fireNotifyQueueKick ? 1 : 0,
        g_modConfig.bindResolverHookLogging ? 1 : 0,
        g_modConfig.bindResolverGlyphSubstitution ? 1 : 0,
        g_modConfig.hudFontIdLogging ? 1 : 0,
        g_modConfig.hudGlyphPositionLogging ? 1 : 0,
        g_modConfig.listItemPositionLogging ? 1 : 0,
        g_modConfig.armorFieldScanLogging ? 1 : 0,
        g_modConfig.forceGlyphOverlay ? 1 : 0,
        g_modConfig.glyphPositionEditMode ? 1 : 0,
        g_modConfig.captureRuntimeMenuAssets ? 1 : 0,
        g_modConfig.frametimeBenchmarkLogging ? 1 : 0);
    LogFromController(buf);

    // Rewrite the file once, now that g_modConfig holds every existing setting PLUS
    // any migrated legacy value, so it's expressed in the current schema (current key
    // names, bumped ConfigVersion) and this migration doesn't need to re-run on every
    // future launch. Reuses WriteDefaultConfig as-is -- it already sources every
    // value it prints from g_modConfig, so this naturally preserves everything the
    // player had tuned, not just the migrated key.
    if (configVersion < kCurrentConfigVersion) {
        WriteDefaultConfig(path);
        LogFromController(migratedLegacyValue
            ? "[config] migrated mw3ncp_config.ini to the current schema -- carried "
              "legacy [Look] Sensitivity over into SensitivityHorizontal/SensitivityVertical"
            : "[config] upgraded mw3ncp_config.ini's schema marker (no legacy values "
              "needed carrying over)");
    }
}

// ---- Config hot-reload QoL feature (2026-07-31, user request) ---------------------
//
// mw3ncp_config.ini was load-once-at-startup-only until now (the default-config
// header comment above previously said as much -- "no live reload yet" -- updated
// the same day this feature landed). Polls the file's real last-write-time (not a
// content hash -- cheap, and a real edit always bumps this).
//
// MOVED OFF THE MAIN THREAD (2026-08-25, live-reported: recurring freezes every
// 2-5 seconds, worse on XInput than DualSense; Steam Input already ruled out --
// confirmed already disabled). This function used to run the ENTIRE check --
// including the actual `GetFileAttributesExA` syscall -- directly on the main
// thread, once per second forever, via `InjectMenuInputTick` (the always-running
// WndProc/SetTimer tick). `GetFileAttributesExA` is normally a cheap cached NTFS
// stat, but is a well-documented real-world stutter source when antivirus
// real-time protection intercepts the file-system call -- an IRREGULAR stall (only
// some calls get intercepted, not all), which matches the reported "random," not
// perfectly periodic character better than a deterministic engine-side cause would.
// This was completely invisible to `FrametimeBenchmarkLogging` (no column for it
// existed) and to Afterburner (a synchronous main-thread stall inside a single
// frame doesn't reliably show as a Present-to-Present gap), which is exactly why
// neither tool caught it despite two real investigation passes (this one and the
// earlier v0.3.3 stutter investigation, issue #79). Split into two halves: the
// potentially-slow stat call now runs on its own dedicated background thread, once
// a second, forever; the actual reload (LoadModConfig/InvalidateTextTextureCachesOnConfigChange/
// ShowOverlayMessage) stays on the main thread, called from CheckConfigHotReload
// exactly as before -- those touch plain, unsynchronized globals that are only ever
// safe to read/write from the main thread (see overlay_hud.cpp's own toast-state
// comment, and controller_input.cpp's near-identical "why not call ShowOverlayMessage
// from the poll thread" rationale) -- so ONLY the file-stat cost moves, not the
// state-mutating reload logic itself.
namespace {
constexpr DWORD kHotReloadCheckIntervalMs = 1000;
FILETIME g_lastConfigWriteTime = {};
bool g_haveLastConfigWriteTime = false;
CRITICAL_SECTION g_hotReloadLock;
bool g_hotReloadLockInit = false;
bool g_configFileChangedPending = false; // set by the background thread, consumed by the main thread
HANDLE g_hotReloadThreadHandle = nullptr;

DWORD WINAPI ConfigHotReloadThreadProc(LPVOID)
{
    char path[MAX_PATH];
    GetConfigPath(path, sizeof(path));

    for (;;) {
        // Timed (2026-08-27, issue #96 follow-up) -- this thread's own real
        // GetFileAttributesExA cost (the exact call this whole thread exists to
        // move off the main thread, per this section's own header comment) was
        // completely invisible to frametime_benchmark.csv before now. A cheap
        // no-op when FrametimeBenchmarkLogging is off (guarded inside
        // FrameBenchmark_AddHotReloadThreadMs itself).
        LARGE_INTEGER freq{}, start{}, end{};
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
        WIN32_FILE_ATTRIBUTE_DATA attrData;
        BOOL gotAttrs = GetFileAttributesExA(path, GetFileExInfoStandard, &attrData);
        QueryPerformanceCounter(&end);
        FrameBenchmark_AddHotReloadThreadMs(
            (static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0) / static_cast<double>(freq.QuadPart));
        if (gotAttrs) {
            EnterCriticalSection(&g_hotReloadLock);
            if (!g_haveLastConfigWriteTime) {
                // First check since DLL load -- LoadModConfig() already read this
                // exact file at startup, so just record its current write-time as
                // the baseline rather than treating "first observation" as a change.
                g_lastConfigWriteTime = attrData.ftLastWriteTime;
                g_haveLastConfigWriteTime = true;
            } else if (CompareFileTime(&attrData.ftLastWriteTime, &g_lastConfigWriteTime) != 0) {
                g_configFileChangedPending = true;
            }
            LeaveCriticalSection(&g_hotReloadLock);
        }
        Sleep(kHotReloadCheckIntervalMs);
    }
    return 0; // unreachable -- lives for the whole process, matching this project's
              // existing "install once, never uninstall" background-thread pattern
              // (controller_input.cpp's own poll thread)
}

void EnsureHotReloadThreadStarted()
{
    static bool started = false;
    if (started) return;
    started = true;
    InitializeCriticalSection(&g_hotReloadLock);
    g_hotReloadLockInit = true;
    g_hotReloadThreadHandle = CreateThread(nullptr, 0, ConfigHotReloadThreadProc, nullptr, 0, nullptr);
    if (!g_hotReloadThreadHandle) {
        LogFromController("[config] CreateThread FAILED for the hot-reload watcher thread -- config hot-reload disabled this session");
    }
}
} // namespace

extern "C" void CheckConfigHotReload()
{
    EnsureHotReloadThreadStarted();
    if (!g_hotReloadLockInit) return; // thread failed to start -- nothing to consume

    EnterCriticalSection(&g_hotReloadLock);
    bool changed = g_configFileChangedPending;
    g_configFileChangedPending = false;
    LeaveCriticalSection(&g_hotReloadLock);
    if (!changed) return;

    LogFromController("[config] mw3ncp_config.ini changed on disk -- hot-reloading");
    LoadModConfig();
    // NOTE: InternalRenderScalePercent (issue #88) is a startup-only, restart-required
    // setting -- Hook_FUN_00679010 (analog_input_hooks.cpp) fires exactly once at real
    // device-creation time, so a hot-reload here does NOT re-apply it; see mod_config.h's
    // own field comment.
    // Live-reported 2026-08-24, "its just the word hold not the full text" -- see
    // InvalidateTextTextureCachesOnConfigChange's own comment (overlay_hud.h) for the
    // full bug: without this, any text whose exact string+height hadn't otherwise
    // changed since it was first rendered stayed stuck on whatever FontFamily/
    // FontFamilyCondensed/FontItalic was active THEN, not the newly-reloaded config.
    InvalidateTextTextureCachesOnConfigChange();
    ShowOverlayMessage("MW32011NCP Config Reloaded", 15000);

    // Re-read the write-time AFTER LoadModConfig() rather than trusting the
    // pre-reload snapshot: LoadModConfig() can itself rewrite the file (a pending
    // schema migration, or just re-persisting current values), which would
    // otherwise look like ANOTHER external change to the background thread's own
    // next check and loop this reload (and its on-screen message) forever. Updates
    // the SAME shared baseline the background thread reads, under the same lock.
    char path[MAX_PATH];
    GetConfigPath(path, sizeof(path));
    WIN32_FILE_ATTRIBUTE_DATA attrData;
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &attrData)) {
        EnterCriticalSection(&g_hotReloadLock);
        g_lastConfigWriteTime = attrData.ftLastWriteTime;
        LeaveCriticalSection(&g_hotReloadLock);
    }
}

// Persists the CURRENT in-memory g_modConfig to disk, in the current schema --
// despite the name, WriteDefaultConfig() always serializes whatever is presently in
// g_modConfig (not hardcoded compiled defaults; see its own call sites in
// LoadModConfig() above, which set g_modConfig from either a migrated file or a
// fresh-install default BEFORE calling it) -- exactly the "save" semantics the new
// in-game custom options overlay (2026-08-04) needs when the player adjusts a value
// live. Exposed here (outside the anonymous namespace) so custom_options_menu.cpp can
// call it without duplicating GetConfigPath()'s own path-resolution logic.
extern "C" void SaveModConfig()
{
    char path[MAX_PATH];
    GetConfigPath(path, sizeof(path));
    WriteDefaultConfig(path);

    // Proactively update the hot-reload watcher's own last-known write time to this
    // save's timestamp -- without this, CheckConfigHotReload's very next poll (within
    // 1 second) would see the file we just wrote as an EXTERNAL change and reload +
    // show the "Config Reloaded" toast, which would fire on every single value
    // adjustment in the new overlay -- confusing, not an external change to react to.
    WIN32_FILE_ATTRIBUTE_DATA attrData{};
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &attrData)) {
        g_lastConfigWriteTime = attrData.ftLastWriteTime;
        g_haveLastConfigWriteTime = true;
    }
}

// ---- Vanilla settings mirror (issue #66, 2026-08-04 full-scope pivot) -------------
//
// See mod_config.h's own comment on these two functions for the overall rationale
// (real dvars/keybinds stay the source of truth for gameplay; the ini mirrors them
// as a backup). One ini section per tab (VanillaLook/VanillaVideo/etc., matching
// VanillaSettingTab's own names) rather than one flat section, so a player opening
// the file to actually read their backup sees it organized the same way the real
// Options screen groups things, not 40 unsorted keys.
namespace {
const char* VanillaTabSectionName(VanillaSettingTab tab)
{
    switch (tab) {
        case VanillaSettingTab::Look:          return "VanillaLook";
        case VanillaSettingTab::Video:         return "VanillaVideo";
        case VanillaSettingTab::Audio:         return "VanillaAudio";
        case VanillaSettingTab::Voice:         return "VanillaVoice";
        case VanillaSettingTab::AdvancedVideo: return "VanillaAdvancedVideo";
        case VanillaSettingTab::Movement:      return "VanillaMovement";
        case VanillaSettingTab::Actions:       return "VanillaActions";
    }
    return "VanillaUnknown";
}
} // namespace

void SyncVanillaSettingsToIni()
{
    char path[MAX_PATH];
    GetConfigPath(path, sizeof(path));

    char valueBuf[128];
    for (int i = 0; i < kVanillaSettingCount; ++i) {
        const VanillaSettingDef& def = kVanillaSettings[i];
        GetVanillaSettingValueString(def, valueBuf, sizeof(valueBuf));
        WritePrivateProfileStringA(VanillaTabSectionName(def.tab), def.iniKey, valueBuf, path);
    }
}

void RestoreVanillaSettingsFromIni()
{
    char path[MAX_PATH];
    GetConfigPath(path, sizeof(path));

    char valueBuf[128];
    for (int i = 0; i < kVanillaSettingCount; ++i) {
        const VanillaSettingDef& def = kVanillaSettings[i];
        GetPrivateProfileStringA(VanillaTabSectionName(def.tab), def.iniKey, "", valueBuf, sizeof(valueBuf), path);
        if (valueBuf[0] == '\0') continue; // never synced yet -- nothing to restore, leave the real setting alone
        SetVanillaSettingFromString(def, valueBuf);
    }
    LogFromController("[config] restored vanilla settings from mw3ncp_config.ini backup");
}

// Issue #88: the config-driven internal render resolution control used to live
// here as ApplyInternalRenderScaleIfConfigured() -- a `r_mode`-write + `vid_restart`
// approach that crashed the game twice, live, and was abandoned (issue #91 later
// found the SAME crash happens via the real vanilla Resolution menu too, meaning
// `vid_restart` itself is unstable in this environment regardless of trigger). A
// same-day follow-up then hooked FUN_00463820, which turned out to be the WRONG
// target (only resized a secondary screenshot buffer, zero real GPU workload
// change -- caught live via a "no FPS change at 300%" report). SUPERSEDED, final,
// 2026-08-26 by Hook_FUN_00679010 (analog_input_hooks.cpp), which overrides the
// engine's own requested render-resolution input before its one-time,
// startup-only computation runs -- that value flows unclamped into the REAL scene
// render target and post-processing chain. Zero r_mode writes, zero vid_restart,
// zero device/window recreation. See known_issues.md issue #88's final 2026-08-26
// sections for the full RE trail.
