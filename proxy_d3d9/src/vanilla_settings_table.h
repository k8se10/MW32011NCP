// vanilla_settings_table.h -- the full catalog of every real vanilla Options setting
// this mod is replacing/mirroring (re_notes/known_issues.md issue #66, full-scope
// expansion 2026-08-04: "we're replacing the entire options flow ... i want them all
// synced and read via the ini"). Pure data -- one entry per real setting, sourced
// directly from the real pc_options_*_ingame.menu files (re_notes/options_menu_full_map.md
// secs 1/2), not approximated. Drives BOTH the ini mirror (mod_config.cpp) and the
// eventual replacement UI (task #11) from a single source of truth, rather than a
// hand-written struct field + parse/serialize pair per setting -- with 62 real
// settings across 7 tabs, that would be unmaintainable duplication.
//
// Deliberately excludes: Margin/safe-area (options_menu_full_map.md sec 3 -- confirmed
// no such PC in-game setting exists at all) and Subtitles/Color Blind Assist's READ
// side (sec 3/8 -- real getprofiledata dispatch not traceable statically; these two
// are WRITE-ONLY for now via the exec profile_toggle... commands already available,
// tracked separately as task #17).
#pragma once

enum class VanillaSettingTab {
    Look,
    Video,
    Audio,
    Voice,
    AdvancedVideo,
    Movement,
    Actions,
};

enum class VanillaSettingKind {
    DvarFloat,   // real float dvar, SetDvarFloat/GetDvarFloat(not yet written)/real_settings.h
    DvarBool,    // real bool/int dvar, SetDvarBool/GetDvarInt
    DvarString,  // real string/enum-as-string dvar, SetDvarString/GetDvarString
    Keybind,     // real keybind command, SetKeybind/GetKeybind, real_settings.h
};

struct VanillaSettingDef {
    const char* iniKey;      // stable key used in mw3ncp_config.ini -- NEVER rename
                              // once shipped without a config-migration entry
                              // (mod_config.cpp already has precedent for this)
    const char* displayLabel; // human-readable row label for the replacement UI,
                               // same ALL-CAPS convention as this project's existing
                               // g_optRows labels (overlay_hud.cpp)
    VanillaSettingTab tab;
    VanillaSettingKind kind;
    const char* realName;    // dvar name (DvarFloat/Bool/String) or keybind command
                              // string (Keybind), e.g. "sensitivity" or "+sprint"
    bool staged;             // true = DVAR_LATCHED-family setting gated behind the
                              // real apply-settings popup (all_restart_popmenu.menu) --
                              // needs vid_restart/snd_restart after writing, see task #20.
                              // Always false for Keybind (binds apply immediately, no
                              // restart concept exists for them).
    float floatMin, floatMax, floatStep; // DvarFloat only, from the real .menu's own
                                           // slider range/step; zero for other kinds.
    const char* description = ""; // real-console-style one-line footer description
                                    // (2026-08-05 restyle). Default member initializer
                                    // means existing positional entries below don't all
                                    // need updating -- only populated for rows this
                                    // project's UI actually surfaces today (phase 1:
                                    // DvarFloat/DvarBool on Look/Voice); left empty for
                                    // the rest rather than guessed, same "no placeholder
                                    // settings" standard as everything else in this file.

    // Full-scope expansion (2026-08-06, issue #66: "EVERY SINGLE OPTION FROM NATIVE
    // AND OUR MOD"). Several real Advanced Video/Audio settings are DvarFloat/
    // DvarString dvars whose real value set is a small, fixed, non-linear list
    // (confirmed straight from the real .menu file's own `dvarFloatList`/`dvarStrList`
    // block, e.g. Anti-Aliasing's real values are 1/2/4, not a linear 1/2/3/4 step) --
    // a plain floatMin/Max/Step range would produce INVALID intermediate values for
    // these. When set, AdjustCurrentTabRow (overlay_hud.cpp) cycles through this list
    // instead of the linear step -- get/set mechanics are otherwise identical DvarFloat/
    // DvarString, so this doesn't need its own VanillaSettingKind. Left null/0 (the
    // default) for every setting with either a simple linear range (floatMin/Max/Step
    // instead) or a real value set that's runtime/display-dependent and NOT statically
    // enumerable (Resolution/DisplayRefresh use a real `dvarEnumList` queried from the
    // actual display's capabilities at menu-open time -- deliberately left read-only
    // rather than guessed).
    const float* floatEnumValues = nullptr;
    int floatEnumCount = 0;
    const char* const* stringEnumValues = nullptr;
    int stringEnumCount = 0;
    // Live-reported (2026-08-06): "stuff like AA and texture quality just say numbers
    // thats not user friendly" -- a float-enum row used to display GetStagedOrLiveValueString's
    // raw numeric value ("1"/"2"/"4" for Anti-Aliasing). Parallel array to
    // floatEnumValues (same index, same count) giving the real display label instead
    // (the exact real `dvarFloatList` label text, e.g. "@MENU_2X" -> "2X") --
    // CurrentTabRowValueString (overlay_hud.cpp) uses this instead of the raw number
    // whenever it's set. Null for any float-enum row that predates this (none do
    // today) or where no label was written -- falls back to the raw number rather
    // than crashing.
    const char* const* floatEnumLabels = nullptr;
};

// Real discrete value lists (2026-08-06), confirmed directly from each dvar's own
// `dvarFloatList`/`dvarStrList` block in the real .menu file (zone_dump/ui/
// pc_options_*_ingame.menu) -- exact real values, not display labels or guesses.
inline constexpr float kEnumValuesAntiAliasing[] = { 1.0f, 2.0f, 4.0f };           // Off/2X/4X
inline constexpr float kEnumValuesSSAO[] = { 0.0f, 1.0f, 2.0f };                    // Off/Low/High
inline constexpr float kEnumValuesTexQualityTier[] = { 3.0f, 2.0f, 1.0f, 0.0f };    // Low/Normal/High/Extra
// Display labels for the float-enum lists above (2026-08-06, live-reported: "stuff
// like AA and texture quality just say numbers thats not user friendly") -- exact
// real `dvarFloatList` label text ("@MENU_2X" -> "2X" etc, confirmed same .menu
// blocks as the values themselves), same index/order as their matching values array.
inline constexpr const char* kEnumLabelsAntiAliasing[] = { "OFF", "2X", "4X" };
inline constexpr const char* kEnumLabelsSSAO[] = { "OFF", "LOW", "HIGH" };
inline constexpr const char* kEnumLabelsTexQualityTier[] = { "LOW", "NORMAL", "HIGH", "EXTRA" };
inline constexpr const char* kEnumValuesOutputConfig[] = {
    "Windows default", "Mono", "Stereo", "4 speakers", "5.1 speakers",
};
inline constexpr const char* kEnumValuesAspectRatio[] = { "auto", "standard", "wide 16:10", "wide 16:9" };

// ---- Look (pc_options_look_ingame.menu) -------------------------------------------
// 4 real dvars + 4 real keybinds.
inline constexpr VanillaSettingDef kVanillaSettings[] = {
    { "Look_Sensitivity",     "SENSITIVITY",        VanillaSettingTab::Look, VanillaSettingKind::DvarFloat,  "sensitivity",   false, 1.0f, 30.0f, 5.0f, "Adjust your mouse look sensitivity." },
    { "Look_InvertMouse",     "INVERT MOUSE",       VanillaSettingTab::Look, VanillaSettingKind::DvarBool,   "ui_mousePitch", false, 0,0,0, "Invert the vertical mouse look axis." },
    { "Look_SmoothMouse",     "SMOOTH MOUSE",       VanillaSettingTab::Look, VanillaSettingKind::DvarBool,   "m_filter",      false, 0,0,0, "Smooth out mouse look movement." },
    { "Look_FreeLook",        "FREE LOOK",          VanillaSettingTab::Look, VanillaSettingKind::DvarBool,   "cl_freelook",   false, 0,0,0, "Enable free look without holding a key." },
    { "Look_LookUp",          "LOOK UP",            VanillaSettingTab::Look, VanillaSettingKind::Keybind,    "+lookup",       false, 0,0,0 },
    { "Look_LookDown",        "LOOK DOWN",          VanillaSettingTab::Look, VanillaSettingKind::Keybind,    "+lookdown",     false, 0,0,0 },
    { "Look_HoldMouseLook",   "HOLD MOUSE LOOK",    VanillaSettingTab::Look, VanillaSettingKind::Keybind,    "+mlook",        false, 0,0,0 },
    { "Look_CenterView",      "CENTER VIEW",        VanillaSettingTab::Look, VanillaSettingKind::Keybind,    "centerview",    false, 0,0,0 },

    // ---- Video (pc_options_video_ingame.menu) -- Resolution/Brightness are staged;
    // Color Blind Assist is profile data, excluded here (write-only via exec, see header).
    // Resolution uses a real `dvarEnumList "r_mode"` -- populated at runtime from the
    // actual display's supported modes, not a static list this file can enumerate.
    // Left DvarString/read-only rather than guessed -- see VanillaSettingDef's comment.
    { "Video_Resolution",     "RESOLUTION",         VanillaSettingTab::Video, VanillaSettingKind::DvarString, "ui_r_mode",              true,  0,0,0 },
    { "Video_Brightness",     "BRIGHTNESS",         VanillaSettingTab::Video, VanillaSettingKind::DvarFloat,  "profileMenuOption_Gamma", true, 0.5f, 1.5f, 0.01f },

    // ---- Audio (pc_options_audio_ingame.menu) -- Subtitles is profile data, excluded.
    { "Audio_Volume",         "VOLUME",             VanillaSettingTab::Audio, VanillaSettingKind::DvarFloat,  "profileMenuOption_volume", true, 0.0f, 0.8f, 0.008f },
    { "Audio_OutputConfig",   "OUTPUT CONFIG",      VanillaSettingTab::Audio, VanillaSettingKind::DvarString, "ui_outputConfig",          true, 0,0,0, "", nullptr, 0, kEnumValuesOutputConfig, 5 },

    // ---- Voice (pc_options_voice_ingame.menu)
    { "Voice_MicSensitivity", "MIC SENSITIVITY",    VanillaSettingTab::Voice, VanillaSettingKind::DvarFloat,  "winvoice_mic_reclevel", false, 0.0f, 65535.0f, 655.0f, "Adjust your microphone's recording sensitivity." },
    { "Voice_Enable",         "VOICE ENABLE",       VanillaSettingTab::Voice, VanillaSettingKind::DvarBool,   "cl_voice",              false, 0,0,0, "Enable or disable voice chat." },
    { "Voice_PushToTalk",     "PUSH TO TALK",       VanillaSettingTab::Voice, VanillaSettingKind::Keybind,    "+talk",                 false, 0,0,0 },
    // The 3rd real Voice row (options_menu_full_map.md sec 2: an inverted
    // dvarFloatList Yes/No toggle, real dvar name not yet confirmed against the raw
    // .menu -- label/dvar pairing was approximate in that pass) is deliberately
    // NOT included here yet -- needs a fresh, confirmed read of the real dvar name
    // before it's safe to wire, per this project's "don't guess" standard.

    // ---- Advanced Video (pc_options_advanced_video_ingame.menu) -- ALL staged
    // (restart-required), confirmed via the real onESC/all_restart_popmenu gate.
    { "AdvVideo_AspectRatio",   "ASPECT RATIO",       VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarString, "ui_r_aspectratio",   true, 0,0,0, "", nullptr, 0, kEnumValuesAspectRatio, 4 },
    // Real dvarFloatList (1/2/4), NOT a string dvar despite the earlier approximate
    // pass marking it DvarString -- corrected 2026-08-06 against the real .menu file.
    { "AdvVideo_AntiAliasing",  "ANTI-ALIASING",      VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarFloat,  "ui_r_aasamples",     true, 0,0,0, "", kEnumValuesAntiAliasing, 3, nullptr, 0, kEnumLabelsAntiAliasing },
    // Resolution/DisplayRefresh both use a real `dvarEnumList` -- a list POPULATED AT
    // RUNTIME from the actual display's supported modes, not a static value set this
    // file can safely enumerate. Left DvarString/read-only (no floatEnumValues/
    // stringEnumValues) rather than guessed -- see VanillaSettingDef's own comment.
    { "AdvVideo_DisplayRefresh","DISPLAY REFRESH",    VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarString, "ui_r_displayRefresh",true, 0,0,0 },
    { "AdvVideo_VSync",         "VSYNC",              VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarBool,   "ui_r_vsync",         true, 0,0,0 },
    { "AdvVideo_ShadowMaps",    "SHADOW MAPS",        VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarBool,   "sm_enable",          true, 0,0,0 },
    { "AdvVideo_Specular",      "SPECULAR",           VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarBool,   "r_specular",         true, 0,0,0 },
    { "AdvVideo_DepthOfField",  "DEPTH OF FIELD",     VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarBool,   "r_dof_enable",       true, 0,0,0 },
    // Real dvarFloatList (0/1/2), NOT a string dvar -- corrected 2026-08-06.
    { "AdvVideo_SSAO",          "AMBIENT OCCLUSION",  VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarFloat,  "ui_r_ssao",          true, 0,0,0, "", kEnumValuesSSAO, 3, nullptr, 0, kEnumLabelsSSAO },
    { "AdvVideo_ZFeather",      "SOFT EDGES",         VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarBool,   "r_zfeather",         true, 0,0,0 },
    { "AdvVideo_BulletMarks",   "BULLET IMPACTS",     VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarBool,   "fx_marks",           true, 0,0,0 },
    { "AdvVideo_TexQualityAuto","AUTO TEXTURE QUALITY", VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarBool, "ui_r_picmip_manual", true, 0,0,0 },
    // Real dvarFloatList (Low=3/Normal=2/High=1/Extra=0 -- value DECREASES as quality
    // increases), NOT string dvars -- corrected 2026-08-06 against the real .menu file.
    { "AdvVideo_TexQuality",    "TEXTURE QUALITY",    VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarFloat,  "ui_r_picmip",        true, 0,0,0, "", kEnumValuesTexQualityTier, 4, nullptr, 0, kEnumLabelsTexQualityTier },
    { "AdvVideo_TexQualityBump","BUMP MAP QUALITY",   VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarFloat,  "ui_r_picmip_bump",   true, 0,0,0, "", kEnumValuesTexQualityTier, 4, nullptr, 0, kEnumLabelsTexQualityTier },
    { "AdvVideo_TexQualitySpec","SPECULAR MAP QUALITY", VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarFloat, "ui_r_picmip_spec", true, 0,0,0, "", kEnumValuesTexQualityTier, 4, nullptr, 0, kEnumLabelsTexQualityTier },

    // ---- Movement (pc_options_movement_ingame.menu) -- 16 real keybinds, complete
    // list confirmed directly from the raw .menu (not approximated).
    { "Movement_Forward",      "MOVE FORWARD",   VanillaSettingTab::Movement, VanillaSettingKind::Keybind, "+forward",       false, 0,0,0 },
    { "Movement_Back",         "MOVE BACK",      VanillaSettingTab::Movement, VanillaSettingKind::Keybind, "+back",          false, 0,0,0 },
    { "Movement_MoveLeft",     "MOVE LEFT",      VanillaSettingTab::Movement, VanillaSettingKind::Keybind, "+moveleft",      false, 0,0,0 },
    { "Movement_MoveRight",    "MOVE RIGHT",     VanillaSettingTab::Movement, VanillaSettingKind::Keybind, "+moveright",     false, 0,0,0 },
    { "Movement_GoStand",      "STAND/MANTLE",   VanillaSettingTab::Movement, VanillaSettingKind::Keybind, "+gostand",       false, 0,0,0 },
    { "Movement_ToggleCrouch", "TOGGLE CROUCH",  VanillaSettingTab::Movement, VanillaSettingKind::Keybind, "togglecrouch",   false, 0,0,0 },
    { "Movement_ToggleProne",  "TOGGLE PRONE",   VanillaSettingTab::Movement, VanillaSettingKind::Keybind, "toggleprone",    false, 0,0,0 },
    { "Movement_HoldSprint",   "HOLD SPRINT",    VanillaSettingTab::Movement, VanillaSettingKind::Keybind, "+breath_sprint", false, 0,0,0 },
    { "Movement_MoveDown",     "MOVE DOWN",      VanillaSettingTab::Movement, VanillaSettingKind::Keybind, "+movedown",      false, 0,0,0 },
    { "Movement_Prone",        "GO PRONE",       VanillaSettingTab::Movement, VanillaSettingKind::Keybind, "+prone",         false, 0,0,0 },
    { "Movement_Stance",       "CHANGE STANCE",  VanillaSettingTab::Movement, VanillaSettingKind::Keybind, "+stance",        false, 0,0,0 },
    { "Movement_Sprint",       "SPRINT",         VanillaSettingTab::Movement, VanillaSettingKind::Keybind, "+sprint",        false, 0,0,0 },
    { "Movement_HoldBreath",   "HOLD BREATH",    VanillaSettingTab::Movement, VanillaSettingKind::Keybind, "+holdbreath",    false, 0,0,0 },
    { "Movement_Left",         "LEAN LEFT",      VanillaSettingTab::Movement, VanillaSettingKind::Keybind, "+left",          false, 0,0,0 },
    { "Movement_Right",        "LEAN RIGHT",     VanillaSettingTab::Movement, VanillaSettingKind::Keybind, "+right",         false, 0,0,0 },
    { "Movement_Strafe",       "STRAFE MODE",    VanillaSettingTab::Movement, VanillaSettingKind::Keybind, "+strafe",        false, 0,0,0 },

    // ---- Actions (pc_options_actions_ingame.menu) -- 14 real keybinds, complete
    // list confirmed directly from the raw .menu (not approximated).
    { "Actions_Attack",        "ATTACK",          VanillaSettingTab::Actions, VanillaSettingKind::Keybind, "+attack",           false, 0,0,0 },
    { "Actions_ADSThrow",      "ADS/COOK TOGGLE", VanillaSettingTab::Actions, VanillaSettingKind::Keybind, "+toggleads_throw",  false, 0,0,0 },
    { "Actions_SpeedThrow",    "QUICK THROW",     VanillaSettingTab::Actions, VanillaSettingKind::Keybind, "+speed_throw",      false, 0,0,0 },
    { "Actions_Reload",        "RELOAD",          VanillaSettingTab::Actions, VanillaSettingKind::Keybind, "+reload",           false, 0,0,0 },
    { "Actions_WeaponNext",    "NEXT WEAPON",     VanillaSettingTab::Actions, VanillaSettingKind::Keybind, "weapnext",          false, 0,0,0 },
    { "Actions_MeleeZoom",     "MELEE/ZOOM",      VanillaSettingTab::Actions, VanillaSettingKind::Keybind, "+melee_zoom",       false, 0,0,0 },
    { "Actions_Activate",      "USE/INTERACT",    VanillaSettingTab::Actions, VanillaSettingKind::Keybind, "+activate",         false, 0,0,0 },
    { "Actions_Frag",          "THROW FRAG",      VanillaSettingTab::Actions, VanillaSettingKind::Keybind, "+frag",             false, 0,0,0 },
    { "Actions_Smoke",         "THROW SMOKE",     VanillaSettingTab::Actions, VanillaSettingKind::Keybind, "+smoke",            false, 0,0,0 },
    { "Actions_ActionSlot1",   "ACTION SLOT 1",   VanillaSettingTab::Actions, VanillaSettingKind::Keybind, "+actionslot 1",     false, 0,0,0 },
    { "Actions_ActionSlot2",   "ACTION SLOT 2",   VanillaSettingTab::Actions, VanillaSettingKind::Keybind, "+actionslot 2",     false, 0,0,0 },
    { "Actions_ActionSlot3",   "ACTION SLOT 3",   VanillaSettingTab::Actions, VanillaSettingKind::Keybind, "+actionslot 3",     false, 0,0,0 },
    { "Actions_ActionSlot4",   "ACTION SLOT 4",   VanillaSettingTab::Actions, VanillaSettingKind::Keybind, "+actionslot 4",     false, 0,0,0 },
    { "Actions_Scores",        "SCOREBOARD",      VanillaSettingTab::Actions, VanillaSettingKind::Keybind, "+scores",           false, 0,0,0 },
};

inline constexpr int kVanillaSettingCount = sizeof(kVanillaSettings) / sizeof(kVanillaSettings[0]);
