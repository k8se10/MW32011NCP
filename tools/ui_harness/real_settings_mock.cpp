// real_settings_mock.cpp -- a fake implementation of real_settings.h's API, for
// tools/ui_harness ONLY. The real real_settings.cpp (proxy_d3d9/src/) calls directly
// into the running game's own process memory via hardcoded addresses that mean
// nothing outside that process -- this harness has no game attached at all, so it
// links THIS file instead (see ui_harness.vcxproj -- real_settings.cpp is
// deliberately NOT included in this project).
//
// Purely in-memory fake state, seeded with plausible-looking (not gameplay-accurate)
// values so the Options screen has something non-empty to show. This file's only
// job is to make the UI drawable and interactively editable for layout/visual
// iteration -- it proves nothing about whether the REAL game-facing real_settings.cpp
// works; that still needs the live game, same as every other "not yet live-tested"
// caveat this project already tracks (re_notes/known_issues.md issue #66).
#include <windows.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "real_settings.h"

namespace {

struct MockDvar {
    char name[64];
    char value[64]; // stored as text regardless of real type -- simplest common format
};

// Seeded with the real defaults options_menu_full_map.md recorded, where known --
// close enough to look right in a screenshot, not a claim these match the game's
// actual current values.
MockDvar g_dvars[] = {
    { "sensitivity", "15" },
    { "ui_mousePitch", "0" },
    { "m_filter", "0" },
    { "cl_freelook", "0" },
    { "ui_r_mode", "1920x1080" },
    { "profileMenuOption_Gamma", "1.0" },
    { "profileMenuOption_volume", "0.4" },
    { "ui_outputConfig", "Stereo" },
    { "winvoice_mic_reclevel", "32000" },
    { "cl_voice", "1" },
    { "ui_r_aspectratio", "16:9" },
    { "ui_r_aasamples", "Off" },
    { "ui_r_displayRefresh", "60" },
    { "ui_r_vsync", "1" },
    { "sm_enable", "1" },
    { "r_specular", "1" },
    { "r_dof_enable", "0" },
    { "ui_r_ssao", "Low" },
    { "r_zfeather", "1" },
    { "fx_marks", "1" },
    { "ui_r_picmip_manual", "0" },
    { "ui_r_picmip", "Normal" },
    { "ui_r_picmip_bump", "Normal" },
    { "ui_r_picmip_spec", "Normal" },
};
constexpr int kDvarCount = sizeof(g_dvars) / sizeof(g_dvars[0]);

MockDvar* FindOrCreateDvar(const char* name)
{
    for (int i = 0; i < kDvarCount; ++i) {
        if (_stricmp(g_dvars[i].name, name) == 0) return &g_dvars[i];
    }
    return nullptr; // unknown dvar name -- mock has a fixed set, matching real
                      // Dvar_FindVar's own "not found" behavior for a bad name
}

struct MockKeybind {
    char command[32];
    int keynum1;
    int keynum2;
};

MockKeybind g_keybinds[] = {
    { "+lookup", 'I', -1 },
    { "+lookdown", 'K', -1 },
    { "+mlook", -1, -1 },
    { "centerview", 'C', -1 },
    { "+talk", 'T', -1 },
    { "+forward", 'W', -1 },
    { "+back", 'S', -1 },
    { "+moveleft", 'A', -1 },
    { "+moveright", 'D', -1 },
    { "+gostand", ' ', -1 },
    { "togglecrouch", 'X', -1 },
    { "toggleprone", 'Z', -1 },
    { "+breath_sprint", VK_SHIFT, -1 },
    { "+movedown", 'C', -1 },
    { "+prone", -1, -1 },
    { "+stance", -1, -1 },
    { "+sprint", VK_SHIFT, -1 },
    { "+holdbreath", -1, -1 },
    { "+left", -1, -1 },
    { "+right", -1, -1 },
    { "+strafe", VK_MENU, -1 },
    { "+attack", VK_LBUTTON, -1 },
    { "+toggleads_throw", VK_RBUTTON, -1 },
    { "+speed_throw", -1, -1 },
    { "+reload", 'R', -1 },
    { "weapnext", 'Q', -1 },
    { "+melee_zoom", 'V', -1 },
    { "+activate", 'F', -1 },
    { "+frag", 'G', -1 },
    { "+smoke", 'B', -1 },
    { "+actionslot 1", '1', -1 },
    { "+actionslot 2", '2', -1 },
    { "+actionslot 3", '3', -1 },
    { "+actionslot 4", '4', -1 },
    { "+scores", VK_TAB, -1 },
};
constexpr int kKeybindCount = sizeof(g_keybinds) / sizeof(g_keybinds[0]);

MockKeybind* FindKeybind(const char* command)
{
    for (int i = 0; i < kKeybindCount; ++i) {
        if (_stricmp(g_keybinds[i].command, command) == 0) return &g_keybinds[i];
    }
    return nullptr;
}

} // namespace

int GetDvarBool(const char* name)
{
    MockDvar* d = FindOrCreateDvar(name);
    return d ? (atoi(d->value) != 0 ? 1 : 0) : 0;
}

float GetDvarFloat(const char* name)
{
    MockDvar* d = FindOrCreateDvar(name);
    return d ? static_cast<float>(atof(d->value)) : 0.0f;
}

const char* GetDvarString(const char* name)
{
    MockDvar* d = FindOrCreateDvar(name);
    return d ? d->value : "";
}

extern "C" void SetDvarBool(const char* name, int value)
{
    if (MockDvar* d = FindOrCreateDvar(name)) sprintf_s(d->value, "%d", value ? 1 : 0);
}

extern "C" void SetDvarString(const char* name, const char* value)
{
    if (MockDvar* d = FindOrCreateDvar(name)) strncpy_s(d->value, value, _TRUNCATE);
}

extern "C" void SetDvarFloat(const char* name, float value)
{
    if (MockDvar* d = FindOrCreateDvar(name)) sprintf_s(d->value, "%g", value);
}

int GetKeybind(const char* command, int /*configIndex*/, int outKeynums[2])
{
    outKeynums[0] = -1;
    outKeynums[1] = -1;
    MockKeybind* k = FindKeybind(command);
    if (!k) return 0;
    int count = 0;
    if (k->keynum1 >= 0) outKeynums[count++] = k->keynum1;
    if (k->keynum2 >= 0) outKeynums[count++] = k->keynum2;
    return count;
}

void SetKeybind(const char* command, int /*configIndex*/, int keynum)
{
    MockKeybind* k = FindKeybind(command);
    if (!k) return;
    if (k->keynum1 < 0) k->keynum1 = keynum;
    else k->keynum2 = keynum;
}

void UnbindKeynum(int keynum, int /*configIndex*/)
{
    for (int i = 0; i < kKeybindCount; ++i) {
        if (g_keybinds[i].keynum1 == keynum) g_keybinds[i].keynum1 = -1;
        if (g_keybinds[i].keynum2 == keynum) g_keybinds[i].keynum2 = -1;
    }
}

int KeyNameToKeynum(const char* keyName)
{
    if (!keyName || !keyName[0]) return -1;
    return static_cast<unsigned char>(keyName[0]); // good enough for harness preview
}

void KeynumToDisplayName(int keynum, char* outBuf, int outBufSize)
{
    if (!outBuf || outBufSize <= 0) return;
    if (keynum < 0) {
        strncpy_s(outBuf, outBufSize, "-", _TRUNCATE);
    } else if (keynum >= 32 && keynum < 127) {
        sprintf_s(outBuf, outBufSize, "%c", static_cast<char>(keynum));
    } else {
        sprintf_s(outBuf, outBufSize, "KEY%d", keynum);
    }
}

void QueueConsoleCommand(const char* command)
{
    printf("[mock-console] %s", command);
}
