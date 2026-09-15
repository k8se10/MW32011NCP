#include "game_exe_detect.h"

#include <windows.h>
#include <cassert>
#include <cstring>

namespace {

GameExecutable g_detected = GameExecutable::Unknown;
bool g_detectedOnce = false;

// Case-insensitive basename compare -- the real file on disk could in principle be
// renamed by a user, but the DLL search-order deployment (and every install doc
// this project ships) always assumes the real, unrenamed "iw5sp.exe"/"iw5mp.exe",
// so this is the same assumption the rest of the project already makes, not a new
// one introduced here.
bool BaseNameEquals(const char* path, const char* name) {
    const char* base = path;
    for (const char* p = path; *p; ++p) {
        if (*p == '\\' || *p == '/') base = p + 1;
    }
    return _stricmp(base, name) == 0;
}

} // namespace

GameExecutable DetectGameExecutable() {
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        g_detected = GameExecutable::Unknown;
    } else if (BaseNameEquals(path, "iw5sp.exe")) {
        g_detected = GameExecutable::SP;
    } else if (BaseNameEquals(path, "iw5mp.exe")) {
        g_detected = GameExecutable::MP;
    } else {
        g_detected = GameExecutable::Unknown;
    }
    g_detectedOnce = true;
    return g_detected;
}

GameExecutable GetDetectedGameExecutable() {
    assert(g_detectedOnce && "GetDetectedGameExecutable() called before DetectGameExecutable() ever ran");
    return g_detected;
}

const char* GameExecutableName(GameExecutable exe) {
    switch (exe) {
        case GameExecutable::SP: return "iw5sp.exe";
        case GameExecutable::MP: return "iw5mp.exe";
        default: return "unknown";
    }
}
