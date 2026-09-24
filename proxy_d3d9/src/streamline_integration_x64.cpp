// MW32011NCP, 2026-09-24: real, first-slice NVIDIA Streamline SDK integration
// -- loads the real, vendored sl.interposer.dll and calls the real slInit(),
// confirming the SDK's own loading/initialization pipeline works end to end
// against this project's build. Matches this project's own "trivial
// passthrough first" convention (CLAUDE.md/AGENTS.md, Production Ready
// Only) -- no actual DLSS feature (Super Resolution, DLAA, etc.) is wired
// yet. See re_notes/x64_migration/vulkan_dlss_pipeline_research.md section
// 2 for the full real remaining integration steps (slSetVulkanInfo against
// DXVK's own real Vulkan device, resource tagging, the jitter/motion-vector
// contract -- jitter itself is already solved, see item 2/analog_input_hooks_x64.cpp).
//
// Deliberately mirrors TryLoadVendoredDxvk()'s own real, already-proven-safe
// loading pattern (dllmain.cpp): resolve a path relative to THIS DLL's own
// deployed location, LoadLibraryA, validate a real expected export exists
// before trusting the module, log loudly either way, never take the whole
// game down on a missing/corrupt vendored file. sl.interposer.dll is NOT
// committed to git (see .gitignore's own comment) -- a fresh clone genuinely
// won't have it until someone vendors it locally, exactly like DXVK's own
// real d3d9.dll build before that one landed.

#include <windows.h>
#include <cstdio>
#include "mod_config.h"
#include "game_exe_detect.h"
#include "../third_party/streamline/include/sl.h"

extern void LogFromController(const char* msg); // defined in dllmain.cpp -- real
    // external-linkage logger every other file in this codebase uses (dllmain.cpp's
    // own Log() sits inside an anonymous namespace, internal linkage only).

namespace {

using PFun_slInit_t = sl::Result(const sl::Preferences&, uint64_t);
using PFun_slShutdown_t = sl::Result();

HMODULE g_streamlineModule = nullptr;
PFun_slInit_t* g_slInit = nullptr;
PFun_slShutdown_t* g_slShutdown = nullptr;
bool g_streamlineInitialized = false;

bool TryLoadStreamlineInterposer()
{
    HMODULE selfModule = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCSTR>(&TryLoadStreamlineInterposer), &selfModule);
    char dllDir[MAX_PATH];
    GetModuleFileNameA(selfModule, dllDir, MAX_PATH);
    char* lastSlash = strrchr(dllDir, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0'; else dllDir[0] = '\0';
    char slPath[MAX_PATH];
    sprintf_s(slPath, "%sstreamline\\sl.interposer.dll", dllDir);

    HMODULE slModule = LoadLibraryA(slPath);
    if (!slModule) {
        char buf[500];
        sprintf_s(buf, "[streamline] StreamlineEnabled=1, but no vendored sl.interposer.dll was found "
            "at '%s' (err=%lu) -- Streamline will not be initialized this session. This SDK is not "
            "bundled with this mod yet.", slPath, GetLastError());
        LogFromController(buf);
        return false;
    }

    g_slInit = reinterpret_cast<PFun_slInit_t*>(GetProcAddress(slModule, "slInit"));
    g_slShutdown = reinterpret_cast<PFun_slShutdown_t*>(GetProcAddress(slModule, "slShutdown"));
    if (!g_slInit || !g_slShutdown) {
        // Literal text alone is 181 bytes; +MAX_PATH (260) for %s exceeds a
        // buf[400] -- widened with real margin (checked via wc -c on the
        // literal, this project's own standing lesson from earlier today).
        char buf[500];
        sprintf_s(buf, "[streamline] Vendored sl.interposer.dll loaded from '%s' but is missing "
            "slInit/slShutdown exports (corrupted/wrong-version file?) -- Streamline will not be "
            "initialized this session.", slPath);
        LogFromController(buf);
        FreeLibrary(slModule);
        g_slInit = nullptr;
        g_slShutdown = nullptr;
        return false;
    }

    g_streamlineModule = slModule;
    char buf[400];
    sprintf_s(buf, "[streamline] Loaded vendored sl.interposer.dll from '%s' -- slInit/slShutdown "
        "resolved.", slPath);
    LogFromController(buf);
    return true;
}

} // namespace

// Real, opt-in entry point -- call once, early, right after DXVK itself has
// loaded successfully (TryLoadVendoredDxvk(), dllmain.cpp). Gated
// independently here too (not just by the caller) so this function is safe
// to call unconditionally without the caller needing to duplicate the gate
// logic -- matches this project's own established "defensive, redundant
// gating" convention for every other structurally-significant feature.
bool TryInitStreamlineX64()
{
    if (!g_modConfig.streamlineEnabled) return false;
    if (g_modConfig.graphicsApi != GraphicsApi::Vulkan) return false;
    if (GetDetectedGameExecutable() != GameExecutable::SP) {
        LogFromController("[streamline] StreamlineEnabled=1, but Streamline is SP-only for now (same gate as "
            "GraphicsApi=Vulkan itself) -- not initializing under this binary.");
        return false;
    }

    if (!TryLoadStreamlineInterposer()) return false;

    // Real sl::Preferences -- eUseManualHooking since this project already owns
    // its own device-creation and hook-installation sequence (DXVK creates the
    // real Vulkan device internally; this project calls slSetVulkanInfo
    // manually once it exists, per ProgrammingGuideManualHooking.md's own
    // documented path -- see vulkan_dlss_pipeline_research.md section 2.7).
    // Requesting DLSS (Super Resolution) as the one feature to load for now,
    // matching the real, confirmed-universal (RTX 20-series+) baseline this
    // project's own hardware-tier research settled on.
    sl::Feature featuresToLoad[] = { sl::kFeatureDLSS };
    sl::Preferences pref{};
    pref.flags = sl::PreferenceFlags::eUseManualHooking;
    pref.renderAPI = sl::RenderAPI::eVulkan;
    pref.featuresToLoad = featuresToLoad;
    pref.numFeaturesToLoad = 1;
    pref.engine = sl::EngineType::eCustom;
    pref.engineVersion = "MW32011NCP";
    pref.logLevel = sl::LogLevel::eDefault;

    sl::Result result = g_slInit(pref, sl::kSDKVersion);
    if (result != sl::Result::eOk) {
        char buf[300];
        sprintf_s(buf, "[streamline] slInit() FAILED (result=%d) -- Streamline will not be usable "
            "this session.", static_cast<int>(result));
        LogFromController(buf);
        return false;
    }

    g_streamlineInitialized = true;
    LogFromController("[streamline] slInit() succeeded -- Streamline SDK is now initialized. No actual DLSS "
        "feature is wired yet (slSetVulkanInfo/resource tagging/frame tracking are all still real, "
        "unstarted next steps) -- this confirms the SDK loading pipeline works end to end.");
    return true;
}

bool IsStreamlineInitializedX64()
{
    return g_streamlineInitialized;
}
