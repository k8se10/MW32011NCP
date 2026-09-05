#pragma once

// Plugin loader (2026-08-25) -- see mw3ncp_plugin_api.h for the full design/risk
// statement and PLUGIN_API.md for the player/plugin-author-facing documentation.

// Scans <dll_dir>\plugins\*.dll and loads any DLL exporting MW3NCP_PluginInit.
// Ordinary (third-party) plugins load only if g_modConfig.pluginsEnabled is true.
// A small, explicit allowlist of first-party "greenlit" plugin filenames (see
// plugin_loader.cpp's own kTrustedPluginFilenames) load UNCONDITIONALLY,
// regardless of that flag -- see PLUGIN_API.md's "Greenlit (trusted) plugins"
// section for the design and its real caveat. This means the plugins\ folder is
// always scanned at least once now, even with PluginsEnabled=0 -- not a complete
// no-op the way it used to be. Call once, from DllMain's DLL_PROCESS_ATTACH,
// after InstallAnalogInputHooks() so the host's own MinHook instance is already
// initialized before any plugin might install its own hook.
void LoadPlugins();

// Calls MW3NCP_PluginShutdown() (if exported) on every loaded plugin. Call once,
// from DllMain's DLL_PROCESS_DETACH, before this mod's own teardown.
void UnloadPlugins();
