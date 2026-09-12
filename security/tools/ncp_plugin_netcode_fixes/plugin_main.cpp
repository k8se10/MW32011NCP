// plugin_main.cpp -- the MW32011NCP-plugin variant of NSP's own netcode security
// fixes. Builds to "mw32011nsp_security.dll" -- this exact filename is what NCP's
// own "greenlit" trusted-plugin allowlist (plugin_loader.cpp) looks for, so it can
// load automatically without requiring [Plugins] Enabled=1 the way an ordinary
// third-party plugin does. See PLUGIN_API.md (in the MW32011NCP repo) for the full
// design and its real caveat (filename matching isn't cryptographic).
//
// This file is a thin adapter, nothing more: it exports the two functions NCP's
// plugin loader requires (MW3NCP_PluginInit/MW3NCP_PluginShutdown), and populates
// the SAME FixHostServices struct the standalone proxy_d3d9 build populates (see
// ../../proxy_d3d9/src/netcode_fixes.h) directly from the MW3NCP_PluginAPI* the
// host hands us -- the shapes already match closely by design, so there's no
// translation logic to get wrong here. Every actual fix (signature scanning, hook
// detours, bounds-check logic) lives in the SAME .cpp files the standalone build
// compiles (referenced directly from ../../proxy_d3d9/src/ in this project's own
// .vcxproj, not copied) -- one implementation, two build targets, exactly as
// planned.

#include "mw3ncp_plugin_api.h"
#include "../../proxy_d3d9/src/netcode_fixes.h"

namespace {

const MW3NCP_PluginAPI* g_hostApi = nullptr;

// Adapts MW3NCP_PluginAPI::InstallHook's own signature (int(*)(void*,void*,void**))
// to FixHostServices::InstallHook -- they're already identical, this indirection
// exists only so FixHostServices stores a plain function pointer, not a bound
// method on g_hostApi, keeping the fix modules themselves host-agnostic.
int InstallHookAdapter(void* target, void* detour, void** outOriginal)
{
    return g_hostApi->InstallHook(target, detour, outOriginal);
}

void* GetGameModuleBaseAdapter()
{
    return g_hostApi->GetGameModuleBase();
}

} // namespace

// Genuine GLOBAL (not anonymous-namespace) function, deliberately -- signature_scan.cpp
// (compiled directly into this project from ../../proxy_d3d9/src/, the exact same
// translation unit the standalone build uses) declares `extern void Log(const char*);`
// expecting this exact free-function symbol to exist wherever it's linked, matching
// the standalone build's own dllmain.cpp. Without this, the plugin target fails to
// link with an unresolved external symbol -- this is that same symbol, just forwarding
// to the host's own logging instead of a local log file.
void Log(const char* msg)
{
    if (g_hostApi) g_hostApi->Log(msg);
}

extern "C" __declspec(dllexport) int MW3NCP_PluginInit(const MW3NCP_PluginAPI* api)
{
    if (!api || api->apiVersion < MW3NCP_PLUGIN_API_VERSION) return 0;
    g_hostApi = api;

    api->Log("[nsp-plugin] MW32011NSP security-fix plugin initializing...");

    FixHostServices host;
    host.InstallHook = &InstallHookAdapter;
    host.GetGameModuleBase = &GetGameModuleBaseAdapter;
    host.Log = &Log;
    InstallNetcodeFixes(host);

    return 1;
}

extern "C" __declspec(dllexport) void MW3NCP_PluginShutdown(void)
{
    if (g_hostApi) g_hostApi->Log("[nsp-plugin] MW32011NSP security-fix plugin shutting down.");
    g_hostApi = nullptr;
}
