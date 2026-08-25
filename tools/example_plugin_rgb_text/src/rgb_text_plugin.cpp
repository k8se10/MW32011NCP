// RGB Text -- MW32011NCP example/dev-test plugin (2026-08-25).
//
// A minimal, real, working plugin: reuses this mod's own already-built rainbow
// hue-cycle math (see proxy_d3d9's overlay_hud.cpp, OverlayAnimStyle::Rainbow) to
// smoothly color-cycle EVERY piece of text and controller-glyph icon this mod
// renders anywhere -- gameplay hints, menu corner hints, the highlighted-item
// glyph, the custom Options screen's own text, every real button-prompt icon.
//
// This is also this project's own live-verification vehicle for the plugin API
// itself (proxy_d3d9/src/mw3ncp_plugin_api.h/plugin_loader.cpp) -- proof the
// loader, the version check, and the SetTextGlyphColorOverride extension point
// all work end-to-end against the real running game, not just code that compiles.
//
// NOT shipped as part of the main mod's own release packaging -- see
// PLUGIN_API.md. Build this project, copy the resulting .dll into a "plugins"
// folder next to the deployed d3d9.dll, and set [Plugins] Enabled=1 in
// mw3ncp_config.ini to use it.

#include <windows.h>
#include <cmath>
#include "../../../proxy_d3d9/src/mw3ncp_plugin_api.h"

namespace {

const MW3NCP_PluginAPI* g_api = nullptr;

// Identical math to overlay_hud.cpp's own ComputeQuadColors (OverlayAnimStyle::
// Rainbow case) -- duplicated rather than shared, since a plugin is a separate DLL
// and can't reach into the host's own internal (non-exported) functions. Kept in
// sync by eye; if the host's own cycle timing/shape ever changes, this plugin's
// own copy won't automatically follow, which is fine -- this is an independent,
// separately-built plugin, not part of the host's own compiled unit.
DWORD ComputeRainbowArgb(DWORD elapsedMs)
{
    float hue = fmodf(static_cast<float>(elapsedMs) / 2400.0f, 1.0f) * 6.0f; // ~2.4s per cycle
    int seg = static_cast<int>(hue);
    float frac = hue - static_cast<float>(seg);
    BYTE r, g, b;
    switch (seg) {
        case 0:  r = 255; g = static_cast<BYTE>(frac * 255.0f); b = 0; break;
        case 1:  r = static_cast<BYTE>((1.0f - frac) * 255.0f); g = 255; b = 0; break;
        case 2:  r = 0; g = 255; b = static_cast<BYTE>(frac * 255.0f); break;
        case 3:  r = 0; g = static_cast<BYTE>((1.0f - frac) * 255.0f); b = 255; break;
        case 4:  r = static_cast<BYTE>(frac * 255.0f); g = 0; b = 255; break;
        default: r = 255; g = 0; b = static_cast<BYTE>((1.0f - frac) * 255.0f); break;
    }
    return 0xFF000000u | (static_cast<DWORD>(r) << 16) | (static_cast<DWORD>(g) << 8) | b;
}

// The color-override callback the host calls once per text/glyph draw. Ignores
// defaultColorArgb entirely (this plugin always overrides, unconditionally) --
// a more selective plugin could inspect/blend with it instead.
extern "C" unsigned long RgbTextColorOverride(unsigned long /*defaultColorArgb*/)
{
    return ComputeRainbowArgb(GetTickCount());
}

} // namespace

extern "C" __declspec(dllexport) int MW3NCP_PluginInit(const MW3NCP_PluginAPI* api)
{
    if (!api || api->apiVersion < MW3NCP_PLUGIN_API_VERSION) {
        return 0; // reject -- host is older than this plugin expects, or null
    }
    g_api = api;
    g_api->Log("[rgb-text] plugin initialized, registering text/glyph color override");
    g_api->SetTextGlyphColorOverride(&RgbTextColorOverride);
    return 1;
}

extern "C" __declspec(dllexport) void MW3NCP_PluginShutdown()
{
    if (g_api) {
        g_api->SetTextGlyphColorOverride(nullptr);
        g_api->Log("[rgb-text] plugin shutting down, override cleared");
    }
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID)
{
    return TRUE; // all real work happens in MW3NCP_PluginInit/Shutdown above
}
