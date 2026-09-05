#include "plugin_loader.h"

#include <windows.h>
#include <cstdio>
#include "mw3ncp_plugin_api.h"
#include "mod_config.h"
#include "../third_party/minhook/include/MinHook.h"

extern void LogFromController(const char* msg); // dllmain.cpp
extern "C" HWND GetGameWindow(); // d3d9_hook.cpp
extern "C" void SetPluginTextGlyphColorOverride(MW3NCP_ColorOverrideFn callback); // overlay_hud.cpp

namespace {

// Same fixed-array convention this codebase already uses throughout (e.g.
// kMaxCachedGlyphIcons, kMaxMenuHintSlots) rather than an STL container -- no
// plugin count anywhere near this many is expected; generous headroom, not a
// tuned exact count.
constexpr int kMaxPlugins = 32;
HMODULE g_loadedPlugins[kMaxPlugins] = {};
MW3NCP_PluginShutdownFn g_pluginShutdownFns[kMaxPlugins] = {};
int g_loadedPluginCount = 0;

// ---- "Greenlit" trusted-plugin allowlist (2026-09-05) ----------------------------
//
// A small, explicit allowlist of first-party, pre-approved plugin filenames --
// these load UNCONDITIONALLY, regardless of [Plugins] Enabled, unlike arbitrary
// third-party plugins (which still require the normal opt-in). Direct instruction:
// the sibling MW32011NSP project's own security-fix plugin should ship built into
// this mod by default, not gated behind the same risk-acknowledgment an unknown
// third-party plugin gets -- see PLUGIN_API.md's own "Greenlit (trusted) plugins"
// section for the full design and the real caveat this does NOT cover (filename
// matching is not cryptographic; see that section before assuming this is a
// security boundary rather than a UX default).
//
// Matched case-insensitively against just the filename (not the full path) via
// _stricmp, since FindFirstFileA/FindNextFileA already scope the scan to the
// plugins\ folder -- no path-traversal surface here, same as the general scan
// below.
constexpr const char* kTrustedPluginFilenames[] = {
    "mw32011nsp_security.dll",
};
constexpr int kTrustedPluginCount = sizeof(kTrustedPluginFilenames) / sizeof(kTrustedPluginFilenames[0]);

bool IsTrustedPluginFilename(const char* filename)
{
    for (int i = 0; i < kTrustedPluginCount; ++i) {
        if (_stricmp(filename, kTrustedPluginFilenames[i]) == 0) return true;
    }
    return false;
}

// ---- MW3NCP_PluginAPI implementations --------------------------------------------

int Plugin_InstallHook(void* target, void* detour, void** outOriginal)
{
    if (!target || !detour) return 0;
    MH_STATUS s = MH_CreateHook(target, detour, outOriginal);
    if (s != MH_OK) {
        char buf[128];
        sprintf_s(buf, "[plugin-api] InstallHook(%p) failed: MH_CreateHook = %d", target, static_cast<int>(s));
        LogFromController(buf);
        return 0;
    }
    s = MH_EnableHook(target);
    if (s != MH_OK) {
        char buf[128];
        sprintf_s(buf, "[plugin-api] InstallHook(%p) failed: MH_EnableHook = %d", target, static_cast<int>(s));
        LogFromController(buf);
        MH_RemoveHook(target);
        return 0;
    }
    return 1;
}

int Plugin_RemoveHook(void* target)
{
    if (!target) return 0;
    MH_DisableHook(target);
    return MH_RemoveHook(target) == MH_OK ? 1 : 0;
}

// SEH-guarded raw memory copy -- matches this project's own "wrap injected code
// paths defensively" standard (CODE_STANDARDS.md, Error Handling & Logging). Safe
// against any address, including an unmapped/protected one: returns 0 instead of
// crashing the host process.
int Plugin_ReadMemory(const void* addr, void* outBuffer, unsigned long size)
{
    if (!addr || !outBuffer || size == 0) return 0;
    __try {
        memcpy(outBuffer, addr, size);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int Plugin_WriteMemory(void* addr, const void* buffer, unsigned long size)
{
    if (!addr || !buffer || size == 0) return 0;
    __try {
        memcpy(addr, buffer, size);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void Plugin_Log(const char* msg)
{
    if (!msg) return;
    LogFromController(msg);
}

void* Plugin_GetGameWindow()
{
    return static_cast<void*>(GetGameWindow());
}

void* Plugin_GetGameModuleBase()
{
    return reinterpret_cast<void*>(GetModuleHandleA(nullptr));
}

void Plugin_SetTextGlyphColorOverride(MW3NCP_ColorOverrideFn callback)
{
    SetPluginTextGlyphColorOverride(callback);
}

// Same MW3NCP_PluginAPI struct handed to every plugin -- stateless (all fields are
// plain function pointers to the free functions above), so one shared instance is
// safe to hand out repeatedly.
MW3NCP_PluginAPI g_pluginApi = {
    MW3NCP_PLUGIN_API_VERSION,
    &Plugin_InstallHook,
    &Plugin_RemoveHook,
    &Plugin_ReadMemory,
    &Plugin_WriteMemory,
    &Plugin_Log,
    &Plugin_GetGameWindow,
    &Plugin_GetGameModuleBase,
    &Plugin_SetTextGlyphColorOverride,
};

// Loads and initializes one plugin DLL by its full path. Logs every step (found,
// loaded, exported-function-found, init result) -- silent failure on a plugin that
// doesn't load or gets rejected is not acceptable, same standard this project
// already holds every other hook/signature-resolution path to.
void TryLoadOnePlugin(const char* fullPath)
{
    char buf[512];
    HMODULE h = LoadLibraryA(fullPath);
    if (!h) {
        sprintf_s(buf, "[plugin-loader] LoadLibraryA failed for \"%s\" (err=%lu)", fullPath, GetLastError());
        LogFromController(buf);
        return;
    }

    auto initFn = reinterpret_cast<MW3NCP_PluginInitFn>(GetProcAddress(h, "MW3NCP_PluginInit"));
    if (!initFn) {
        sprintf_s(buf, "[plugin-loader] \"%s\" loaded but exports no MW3NCP_PluginInit -- not a plugin, unloading", fullPath);
        LogFromController(buf);
        FreeLibrary(h);
        return;
    }

    sprintf_s(buf, "[plugin-loader] \"%s\" loaded, calling MW3NCP_PluginInit (host apiVersion=%u)", fullPath, MW3NCP_PLUGIN_API_VERSION);
    LogFromController(buf);

    int accepted = initFn(&g_pluginApi);
    if (!accepted) {
        sprintf_s(buf, "[plugin-loader] \"%s\" rejected the host API (MW3NCP_PluginInit returned 0) -- unloading", fullPath);
        LogFromController(buf);
        FreeLibrary(h);
        return;
    }

    if (g_loadedPluginCount >= kMaxPlugins) {
        sprintf_s(buf, "[plugin-loader] \"%s\" initialized, but kMaxPlugins (%d) already reached -- won't receive a shutdown call", fullPath, kMaxPlugins);
        LogFromController(buf);
        return; // leave it loaded/running; just can't track it for shutdown
    }

    g_loadedPlugins[g_loadedPluginCount] = h;
    g_pluginShutdownFns[g_loadedPluginCount] = reinterpret_cast<MW3NCP_PluginShutdownFn>(GetProcAddress(h, "MW3NCP_PluginShutdown"));
    ++g_loadedPluginCount;

    sprintf_s(buf, "[plugin-loader] \"%s\" initialized successfully", fullPath);
    LogFromController(buf);
}

} // namespace

void LoadPlugins()
{
    // Unlike before 2026-09-05, this can no longer return early on
    // !pluginsEnabled -- the plugins\ folder must always be scanned at least once
    // to find any "greenlit" trusted-filename plugin (see kTrustedPluginFilenames
    // above), which loads regardless of this flag. The general opt-in scan below
    // is still fully gated on pluginsEnabled, unchanged from before.
    char dllDir[MAX_PATH];
    GetModuleFileNameA(nullptr, dllDir, MAX_PATH); // full path of the .exe that loaded us
    char* lastSlash = strrchr(dllDir, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';

    char searchPattern[MAX_PATH];
    sprintf_s(searchPattern, "%splugins\\*.dll", dllDir);

    WIN32_FIND_DATAA findData = {};
    HANDLE findHandle = FindFirstFileA(searchPattern, &findData);
    if (findHandle == INVALID_HANDLE_VALUE) {
        LogFromController(g_modConfig.pluginsEnabled
            ? "[plugin-loader] PluginsEnabled=1 but no \"plugins\" folder (or no .dll files in it) -- nothing to load"
            : "[plugin-loader] no \"plugins\" folder (or no .dll files in it) -- no greenlit/trusted plugin found either");
        return;
    }

    do {
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        // A single pass handles both paths -- a file is either the one, specific
        // trusted filename (loaded unconditionally, "greenlit") or an ordinary
        // plugin (loaded only if the player opted in), never both, so there's no
        // double-load/double-init risk to guard against separately.
        bool isTrusted = IsTrustedPluginFilename(findData.cFileName);
        if (!isTrusted && !g_modConfig.pluginsEnabled) continue;

        char fullPath[MAX_PATH];
        sprintf_s(fullPath, "%splugins\\%s", dllDir, findData.cFileName);

        if (isTrusted) {
            char buf[512];
            sprintf_s(buf, "[plugin-loader] \"%s\" matches the greenlit/trusted allowlist -- loading regardless of PluginsEnabled (currently %d)",
                findData.cFileName, g_modConfig.pluginsEnabled ? 1 : 0);
            LogFromController(buf);
        }
        TryLoadOnePlugin(fullPath);
    } while (FindNextFileA(findHandle, &findData));

    FindClose(findHandle);

    char buf[128];
    sprintf_s(buf, "[plugin-loader] done -- %d plugin(s) loaded and initialized", g_loadedPluginCount);
    LogFromController(buf);
}

void UnloadPlugins()
{
    for (int i = 0; i < g_loadedPluginCount; ++i) {
        if (g_pluginShutdownFns[i]) g_pluginShutdownFns[i]();
    }
    // A plugin that registered a text/glyph color override is about to have its own
    // code unmapped (this runs from DLL_PROCESS_DETACH, right before the process
    // exits) -- clear the override so nothing could ever call through a dangling
    // pointer into unloaded plugin code. In practice DLL_PROCESS_DETACH order means
    // no more frames render after this point anyway, but this costs nothing and
    // removes the theoretical risk entirely.
    SetPluginTextGlyphColorOverride(nullptr);
}
