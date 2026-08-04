// staged_settings.cpp -- see staged_settings.h for the full rationale.
#include <cstring>
#include "staged_settings.h"
#include "vanilla_settings_table.h"
#include "vanilla_settings_sync.h"
#include "real_settings.h"

namespace {
struct PendingEntry {
    bool hasPending = false;
    char value[128] = {};
};
PendingEntry g_pending[kVanillaSettingCount];
} // namespace

void SetStagedSettingPending(int settingIndex, const char* value)
{
    if (settingIndex < 0 || settingIndex >= kVanillaSettingCount || !value) return;
    if (!kVanillaSettings[settingIndex].staged) return; // non-staged settings write immediately elsewhere, not here
    strncpy_s(g_pending[settingIndex].value, value, _TRUNCATE);
    g_pending[settingIndex].hasPending = true;
}

bool HasPendingStagedChanges()
{
    for (int i = 0; i < kVanillaSettingCount; ++i) {
        if (g_pending[i].hasPending) return true;
    }
    return false;
}

void DiscardStagedChanges()
{
    for (int i = 0; i < kVanillaSettingCount; ++i) {
        g_pending[i].hasPending = false;
    }
}

void CommitStagedSettings()
{
    bool needsVideoRestart = false;
    bool needsAudioRestart = false;

    for (int i = 0; i < kVanillaSettingCount; ++i) {
        if (!g_pending[i].hasPending) continue;
        const VanillaSettingDef& def = kVanillaSettings[i];
        SetVanillaSettingFromString(def, g_pending[i].value);
        if (def.tab == VanillaSettingTab::Audio) {
            needsAudioRestart = true;
        } else if (def.tab == VanillaSettingTab::Video || def.tab == VanillaSettingTab::AdvancedVideo) {
            needsVideoRestart = true;
        }
        g_pending[i].hasPending = false;
    }

    // Real console commands, same mechanism the real Options UI's own popup uses
    // (confirmed: all_restart_popmenu.menu's Yes action literally execs snd_restart).
    if (needsVideoRestart) QueueConsoleCommand("vid_restart\n");
    if (needsAudioRestart) QueueConsoleCommand("snd_restart\n");
}

void GetStagedOrLiveValueString(int settingIndex, char* outBuf, size_t outBufSize)
{
    if (!outBuf || outBufSize == 0) return;
    outBuf[0] = '\0';
    if (settingIndex < 0 || settingIndex >= kVanillaSettingCount) return;

    if (g_pending[settingIndex].hasPending) {
        strncpy_s(outBuf, outBufSize, g_pending[settingIndex].value, _TRUNCATE);
        return;
    }
    GetVanillaSettingValueString(kVanillaSettings[settingIndex], outBuf, outBufSize);
}
