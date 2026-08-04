// vanilla_settings_sync.cpp -- see vanilla_settings_sync.h for the full rationale.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "vanilla_settings_sync.h"
#include "vanilla_settings_table.h"
#include "real_settings.h"

void GetVanillaSettingValueString(const VanillaSettingDef& def, char* outBuf, size_t outBufSize)
{
    if (!outBuf || outBufSize == 0) return;
    outBuf[0] = '\0';

    switch (def.kind) {
        case VanillaSettingKind::DvarFloat: {
            float v = GetDvarFloat(def.realName);
            snprintf(outBuf, outBufSize, "%g", v);
            break;
        }
        case VanillaSettingKind::DvarBool: {
            int v = GetDvarBool(def.realName);
            snprintf(outBuf, outBufSize, "%d", v);
            break;
        }
        case VanillaSettingKind::DvarString: {
            const char* v = GetDvarString(def.realName);
            snprintf(outBuf, outBufSize, "%s", v ? v : "");
            break;
        }
        case VanillaSettingKind::Keybind: {
            int keynums[2] = { -1, -1 };
            GetKeybind(def.realName, 0, keynums);
            snprintf(outBuf, outBufSize, "%d,%d", keynums[0], keynums[1]);
            break;
        }
    }
}

void SetVanillaSettingFromString(const VanillaSettingDef& def, const char* value)
{
    if (!value) return;

    switch (def.kind) {
        case VanillaSettingKind::DvarFloat: {
            float v = static_cast<float>(atof(value));
            if (v < def.floatMin) v = def.floatMin;
            if (v > def.floatMax) v = def.floatMax;
            SetDvarFloat(def.realName, v);
            break;
        }
        case VanillaSettingKind::DvarBool: {
            SetDvarBool(def.realName, atoi(value) != 0 ? 1 : 0);
            break;
        }
        case VanillaSettingKind::DvarString: {
            SetDvarString(def.realName, value);
            break;
        }
        case VanillaSettingKind::Keybind: {
            int keynum1 = -1, keynum2 = -1;
            sscanf_s(value, "%d,%d", &keynum1, &keynum2);
            if (keynum1 >= 0) SetKeybind(def.realName, 0, keynum1);
            if (keynum2 >= 0) SetKeybind(def.realName, 0, keynum2);
            break;
        }
    }
}
