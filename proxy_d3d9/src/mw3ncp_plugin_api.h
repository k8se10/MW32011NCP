#pragma once

// MW32011NCP Plugin API -- 2026-08-25.
//
// This header is the public, stable ABI a THIRD-PARTY (or your own, separately
// built) plugin DLL includes to talk to this mod. It is plain C, deliberately --
// no C++ types (STL containers, virtual classes) cross this boundary, since those
// aren't ABI-stable across separately compiled DLLs or different compiler
// versions. This project already treats a plain C function-pointer struct as the
// right shape for a cross-boundary API (see overlay_hud.h's own MenuGfx_* Phase 3
// forwarders, built the same way for tools/ui_harness).
//
// SHIPPING SCOPE, READ THIS FIRST: the plugin-LOADING infrastructure (this header,
// plugin_loader.h/.cpp) is part of the main mod itself and ships in every release
// of d3d9.dll. Actual PLUGIN DLLs -- anything that includes this header and
// exports MW3NCP_PluginInit -- are NOT shipped with the main mod, are not vetted
// or reviewed by this project, and this project makes NO safety/VAC-risk claim
// about what a loaded plugin does. Loading is strictly opt-in
// ([Plugins] Enabled=1 in mw3ncp_config.ini, default 0) -- see PLUGIN_API.md for
// the full design and an explicit risk statement before enabling this.
//
// WHY THIS EXISTS AS A SEPARATE SURFACE FROM THE MAIN MOD: this project's own
// aim-assist feature was permanently REMOVED (not just disabled) specifically
// because reading live gameplay-entity memory in the MAIN mod is mechanically
// identical to a soft-aimbot regardless of intent (re_notes/known_issues.md issue
// #33). That hard-line policy is about what the main mod's own default code paths
// do -- it does not, and was never intended to, forbid an explicitly separate,
// opt-in, not-shipped extension point for a player's own plugin code to do more.
// Enabling plugin loading, and whatever a specific plugin then does with the
// memory read/write access below, is the user's own deliberate choice each time,
// not something this project defaults to or ships pre-loaded.

#ifdef __cplusplus
extern "C" {
#endif

#define MW3NCP_PLUGIN_API_VERSION 1u

// Text/glyph color override -- called once per draw for every piece of text or
// controller-glyph icon this mod renders (see SetTextGlyphColorOverride below for
// exactly which draws that covers). Receives the color this mod would otherwise use
// (packed ARGB, 0xAARRGGBB, matching this mod's own internal color convention) and
// returns the color to actually use instead -- return the input unchanged to leave a
// specific draw untouched. Called from the game's own render thread, once per draw
// call -- keep this fast (no blocking, no heavy work), same rule every other hook
// callback in this mod is already held to.
typedef unsigned long (*MW3NCP_ColorOverrideFn)(unsigned long defaultColorArgb);

typedef struct MW3NCP_PluginAPI {
    unsigned int apiVersion; // = MW3NCP_PLUGIN_API_VERSION at the time this struct
                              // was populated. Check this before touching any other
                              // field -- a future host version may add fields after
                              // this one, but never reorder/remove existing ones,
                              // so an older plugin built against an earlier
                              // apiVersion stays safe to use against a newer host.

    // Hook installation -- thin wrappers around this project's own already-
    // initialized MinHook instance (the host calls MH_Initialize() once itself;
    // plugins reuse that same instance rather than each bringing their own, so two
    // plugins -- or a plugin and the host -- can't silently stomp each other's
    // hooks by running independent MinHook instances against the same process).
    // Returns 1 (true) on success, 0 (false) on failure -- never throws/crashes.
    int (*InstallHook)(void* target, void* detour, void** outOriginal);
    int (*RemoveHook)(void* target);

    // Direct process memory read/write. Since a plugin runs IN-PROCESS (loaded
    // into the same address space as the game and this mod), this is a thin,
    // SEH-guarded wrapper around a plain memory copy -- safe to call against any
    // address, including one that turns out to be unmapped/protected: returns 0
    // (false) instead of crashing the game. This is the capability class this
    // project's own main mod deliberately never uses on itself -- see this file's
    // own header comment above.
    int (*ReadMemory)(const void* addr, void* outBuffer, unsigned long size);
    int (*WriteMemory)(void* addr, const void* buffer, unsigned long size);

    // Shared logging -- writes to the SAME proxy_d3d9.log this mod's own hooks
    // already write to, so a plugin's diagnostic trail lives in one place instead
    // of a second, separate log file.
    void (*Log)(const char* msg);

    // Convenience accessors -- the game's own real HWND and this process's own
    // module base address, so a plugin doesn't need to re-derive either itself.
    void* (*GetGameWindow)(void);
    void* (*GetGameModuleBase)(void);

    // Registers (or clears, if callback is NULL) a color-override callback applied
    // to every piece of TEXT and controller-glyph ICON this mod renders anywhere --
    // gameplay hints (Interact/ReadyUp/Reload/Mantle), menu corner hints, the
    // highlighted-item glyph, the custom Options screen's own text, and every real
    // button-prompt icon. Does NOT affect solid-color UI chrome (progress bars,
    // sliders, panel backgrounds) or the custom cursor -- this is specifically the
    // "text and glyphs" surface, general mod-wide cosmetic reskinning, not a general
    // rendering hook. Only one override can be registered at a time; a second
    // plugin calling this replaces the first's callback. See the bundled "RGB Text"
    // example plugin (tools/example_plugin_rgb_text/) for a real, working use of
    // this exact call.
    void (*SetTextGlyphColorOverride)(MW3NCP_ColorOverrideFn callback);
} MW3NCP_PluginAPI;

// Every plugin DLL must export a function with EXACTLY this name and signature:
//
//   __declspec(dllexport) int MW3NCP_PluginInit(const MW3NCP_PluginAPI* api);
//
// Called once, synchronously, right after the plugin DLL is loaded (during this
// mod's own DllMain DLL_PROCESS_ATTACH -- keep this fast and non-blocking, same
// rule this project already holds every other hook callback to). Return 1 to
// accept, 0 to reject (e.g. api->apiVersion is higher than this plugin knows how
// to use) -- the host logs the result either way and does not retry.
typedef int (*MW3NCP_PluginInitFn)(const MW3NCP_PluginAPI* api);

// Optional. If a plugin DLL also exports a function with this exact name/signature,
// the host calls it once during its own DLL_PROCESS_DETACH, before tearing down:
//
//   __declspec(dllexport) void MW3NCP_PluginShutdown(void);
//
// Not required -- a plugin that skips this simply gets no shutdown notification.
typedef void (*MW3NCP_PluginShutdownFn)(void);

#ifdef __cplusplus
} // extern "C"
#endif
