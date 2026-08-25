#pragma once

// Plugin loader (2026-08-25) -- see mw3ncp_plugin_api.h for the full design/risk
// statement and PLUGIN_API.md for the player/plugin-author-facing documentation.

// Scans <dll_dir>\plugins\*.dll and loads any DLL exporting MW3NCP_PluginInit, if
// and only if g_modConfig.pluginsEnabled is true -- a complete no-op (no directory
// scan, no LoadLibrary calls) otherwise. Call once, from DllMain's
// DLL_PROCESS_ATTACH, after InstallAnalogInputHooks() so the host's own MinHook
// instance is already initialized before any plugin might install its own hook.
void LoadPlugins();

// Calls MW3NCP_PluginShutdown() (if exported) on every loaded plugin. Call once,
// from DllMain's DLL_PROCESS_DETACH, before this mod's own teardown.
void UnloadPlugins();
