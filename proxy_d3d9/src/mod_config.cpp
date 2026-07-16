#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include "mod_config.h"

extern void LogFromController(const char* msg); // defined in dllmain.cpp

ModConfig g_modConfig;

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
        "; Right-stick turn rate in degrees/second at full stick deflection.\n"
        "Sensitivity=%g\n"
        "; How strongly look slows down while aiming down sights on magnified optics,\n"
        "; scaled to the weapon's actual live zoom level (read-only -- never changes\n"
        "; your real field of view). 0.0 = no slowdown at all (flat sensitivity\n"
        "; regardless of zoom); 1.0 = fully proportional to zoom (closest to real\n"
        "; console feel, confirmed live). Values in between blend toward flat.\n"
        "AdsSlowdownStrength=%g\n"
        "; OG console \"Invert Look\" -- flips vertical (up/down) look. 0 = off, 1 = on.\n"
        "InvertLook=%d\n"
        "\n"
        "[Stance]\n"
        "; Milliseconds a Crouch/Prone (B) press must be held to count as \"hold\"\n"
        "; (go prone) instead of \"tap\" (toggle crouch).\n"
        "ProneHoldThresholdMs=%lu\n"
        "\n"
        "[Interact]\n"
        "; Milliseconds Interact (X) must be held before it fires. A press released\n"
        "; before this switches weapons instead, same as a normal tap.\n"
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
        "RegenSeconds=%g\n",
        g_modConfig.lookDegreesPerSecond,
        g_modConfig.adsSlowdownStrength,
        g_modConfig.invertLook ? 1 : 0,
        g_modConfig.proneHoldThresholdMs,
        g_modConfig.interactHoldThresholdMs,
        g_modConfig.readyUpHoldThresholdMs,
        g_modConfig.sprintMaxStaminaSeconds,
        g_modConfig.sprintRegenSeconds);

    fclose(f);
}

} // namespace

void LoadModConfig()
{
    char path[MAX_PATH];
    GetConfigPath(path, sizeof(path));

    DWORD attrs = GetFileAttributesA(path);
    bool exists = (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
    if (!exists) {
        WriteDefaultConfig(path); // g_modConfig still holds its struct-initializer defaults here
        LogFromController("[config] no mw3ncp_config.ini found -- wrote a default one");
        return;
    }

    ReadFloat(path, "Look", "Sensitivity", g_modConfig.lookDegreesPerSecond);
    ReadFloat(path, "Look", "AdsSlowdownStrength", g_modConfig.adsSlowdownStrength);
    ReadBool(path, "Look", "InvertLook", g_modConfig.invertLook);
    ReadUlong(path, "Stance", "ProneHoldThresholdMs", g_modConfig.proneHoldThresholdMs);
    ReadUlong(path, "Interact", "HoldThresholdMs", g_modConfig.interactHoldThresholdMs);
    ReadUlong(path, "Survival", "ReadyUpHoldThresholdMs", g_modConfig.readyUpHoldThresholdMs);
    ReadFloat(path, "Sprint", "MaxStaminaSeconds", g_modConfig.sprintMaxStaminaSeconds);
    ReadFloat(path, "Sprint", "RegenSeconds", g_modConfig.sprintRegenSeconds);

    char buf[288];
    sprintf_s(buf,
        "[config] loaded mw3ncp_config.ini: sensitivity=%g adsSlowdownStrength=%g "
        "invertLook=%d proneHoldMs=%lu interactHoldMs=%lu readyUpHoldMs=%lu sprintMax=%g "
        "sprintRegen=%g",
        g_modConfig.lookDegreesPerSecond, g_modConfig.adsSlowdownStrength,
        g_modConfig.invertLook ? 1 : 0, g_modConfig.proneHoldThresholdMs,
        g_modConfig.interactHoldThresholdMs, g_modConfig.readyUpHoldThresholdMs,
        g_modConfig.sprintMaxStaminaSeconds, g_modConfig.sprintRegenSeconds);
    LogFromController(buf);
}
