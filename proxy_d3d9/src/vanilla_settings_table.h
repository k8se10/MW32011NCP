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
};

// ---- Look (pc_options_look_ingame.menu) -------------------------------------------
// 4 real dvars + 4 real keybinds.
inline constexpr VanillaSettingDef kVanillaSettings[] = {
    { "Look_Sensitivity",     "SENSITIVITY",        VanillaSettingTab::Look, VanillaSettingKind::DvarFloat,  "sensitivity",   false, 1.0f, 30.0f, 5.0f },
    { "Look_InvertMouse",     "INVERT MOUSE",       VanillaSettingTab::Look, VanillaSettingKind::DvarBool,   "ui_mousePitch", false, 0,0,0 },
    { "Look_SmoothMouse",     "SMOOTH MOUSE",       VanillaSettingTab::Look, VanillaSettingKind::DvarBool,   "m_filter",      false, 0,0,0 },
    { "Look_FreeLook",        "FREE LOOK",          VanillaSettingTab::Look, VanillaSettingKind::DvarBool,   "cl_freelook",   false, 0,0,0 },
    { "Look_LookUp",          "LOOK UP",            VanillaSettingTab::Look, VanillaSettingKind::Keybind,    "+lookup",       false, 0,0,0 },
    { "Look_LookDown",        "LOOK DOWN",          VanillaSettingTab::Look, VanillaSettingKind::Keybind,    "+lookdown",     false, 0,0,0 },
    { "Look_HoldMouseLook",   "HOLD MOUSE LOOK",    VanillaSettingTab::Look, VanillaSettingKind::Keybind,    "+mlook",        false, 0,0,0 },
    { "Look_CenterView",      "CENTER VIEW",        VanillaSettingTab::Look, VanillaSettingKind::Keybind,    "centerview",    false, 0,0,0 },

    // ---- Video (pc_options_video_ingame.menu) -- Resolution/Brightness are staged;
    // Color Blind Assist is profile data, excluded here (write-only via exec, see header).
    { "Video_Resolution",     "RESOLUTION",         VanillaSettingTab::Video, VanillaSettingKind::DvarString, "ui_r_mode",              true,  0,0,0 },
    { "Video_Brightness",     "BRIGHTNESS",         VanillaSettingTab::Video, VanillaSettingKind::DvarFloat,  "profileMenuOption_Gamma", true, 0.5f, 1.5f, 0.01f },

    // ---- Audio (pc_options_audio_ingame.menu) -- Subtitles is profile data, excluded.
    { "Audio_Volume",         "VOLUME",             VanillaSettingTab::Audio, VanillaSettingKind::DvarFloat,  "profileMenuOption_volume", true, 0.0f, 0.8f, 0.008f },
    { "Audio_OutputConfig",   "OUTPUT CONFIG",      VanillaSettingTab::Audio, VanillaSettingKind::DvarString, "ui_outputConfig",          true, 0,0,0 },

    // ---- Voice (pc_options_voice_ingame.menu)
    { "Voice_MicSensitivity", "MIC SENSITIVITY",    VanillaSettingTab::Voice, VanillaSettingKind::DvarFloat,  "winvoice_mic_reclevel", false, 0.0f, 65535.0f, 655.0f },
    { "Voice_Enable",         "VOICE ENABLE",       VanillaSettingTab::Voice, VanillaSettingKind::DvarBool,   "cl_voice",              false, 0,0,0 },
    { "Voice_PushToTalk",     "PUSH TO TALK",       VanillaSettingTab::Voice, VanillaSettingKind::Keybind,    "+talk",                 false, 0,0,0 },
    // The 3rd real Voice row (options_menu_full_map.md sec 2: an inverted
    // dvarFloatList Yes/No toggle, real dvar name not yet confirmed against the raw
    // .menu -- label/dvar pairing was approximate in that pass) is deliberately
    // NOT included here yet -- needs a fresh, confirmed read of the real dvar name
    // before it's safe to wire, per this project's "don't guess" standard.

    // ---- Advanced Video (pc_options_advanced_video_ingame.menu) -- ALL staged
    // (restart-required), confirmed via the real onESC/all_restart_popmenu gate.
    { "AdvVideo_AspectRatio",   "ASPECT RATIO",       VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarString, "ui_r_aspectratio",   true, 0,0,0 },
    { "AdvVideo_AntiAliasing",  "ANTI-ALIASING",      VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarString, "ui_r_aasamples",     true, 0,0,0 },
    { "AdvVideo_DisplayRefresh","DISPLAY REFRESH",    VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarString, "ui_r_displayRefresh",true, 0,0,0 },
    { "AdvVideo_VSync",         "VSYNC",              VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarBool,   "ui_r_vsync",         true, 0,0,0 },
    { "AdvVideo_ShadowMaps",    "SHADOW MAPS",        VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarBool,   "sm_enable",          true, 0,0,0 },
    { "AdvVideo_Specular",      "SPECULAR",           VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarBool,   "r_specular",         true, 0,0,0 },
    { "AdvVideo_DepthOfField",  "DEPTH OF FIELD",     VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarBool,   "r_dof_enable",       true, 0,0,0 },
    { "AdvVideo_SSAO",          "AMBIENT OCCLUSION",  VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarString, "ui_r_ssao",          true, 0,0,0 },
    { "AdvVideo_ZFeather",      "SOFT EDGES",         VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarBool,   "r_zfeather",         true, 0,0,0 },
    { "AdvVideo_BulletMarks",   "BULLET IMPACTS",     VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarBool,   "fx_marks",           true, 0,0,0 },
    { "AdvVideo_TexQualityAuto","AUTO TEXTURE QUALITY", VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarBool, "ui_r_picmip_manual", true, 0,0,0 },
    { "AdvVideo_TexQuality",    "TEXTURE QUALITY",    VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarString, "ui_r_picmip",        true, 0,0,0 },
    { "AdvVideo_TexQualityBump","BUMP MAP QUALITY",   VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarString, "ui_r_picmip_bump",   true, 0,0,0 },
    { "AdvVideo_TexQualitySpec","SPECULAR MAP QUALITY", VanillaSettingTab::AdvancedVideo, VanillaSettingKind::DvarString, "ui_r_picmip_spec", true, 0,0,0 },

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
