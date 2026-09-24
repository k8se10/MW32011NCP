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
// loading pattern (dllmain.cpp): LoadLibraryA, validate a real expected
// export exists before trusting the module, log loudly either way, never
// take the whole game down on a missing/corrupt vendored file. As of
// 2026-09-24 the actual binary is no longer a loose file next to this DLL --
// it's embedded as an RCDATA resource (proxy_d3d9.rc) and extracted fresh on
// every launch to %LOCALAPPDATA%\MW32011NCP\runtime_x64\streamline\ (see
// dxvk_streamline_extract_x64.cpp) -- but the source binary itself is still
// NOT committed to git (see .gitignore's own comment): a fresh clone needs
// it vendored locally under third_party/streamline/bin/x64/ before this DLL
// can even build, exactly like DXVK's own real d3d9.dll build.

#include <windows.h>
#include <cstdio>
#include "mod_config.h"
#include "game_exe_detect.h"
#include "dxvk_interop_x64.h" // Vulkan SDK types -- must come BEFORE
    // sl_helpers_vk.h below, which uses VkPhysicalDeviceVulkan12Features/etc.
    // without including any Vulkan header itself.
#include "../third_party/streamline/include/sl.h"
#include "../third_party/streamline/include/sl_helpers_vk.h"

extern void LogFromController(const char* msg); // defined in dllmain.cpp -- real
    // external-linkage logger every other file in this codebase uses (dllmain.cpp's
    // own Log() sits inside an anonymous namespace, internal linkage only).
extern bool ExtractEmbeddedStreamlineX64(char* outDir, size_t outDirSize); // dxvk_streamline_extract_x64.cpp

namespace {

using PFun_slInit_t = sl::Result(const sl::Preferences&, uint64_t);
using PFun_slShutdown_t = sl::Result();
using PFun_slGetFeatureRequirements_t = sl::Result(sl::Feature, sl::FeatureRequirements&);
using PFun_slSetVulkanInfo_t = sl::Result(const sl::VulkanInfo&);

HMODULE g_streamlineModule = nullptr;
PFun_slInit_t* g_slInit = nullptr;
PFun_slShutdown_t* g_slShutdown = nullptr;
PFun_slGetFeatureRequirements_t* g_slGetFeatureRequirements = nullptr;
PFun_slSetVulkanInfo_t* g_slSetVulkanInfo = nullptr;
bool g_streamlineInitialized = false;
bool g_streamlineVulkanInfoSet = false;

bool TryLoadStreamlineInterposer()
{
    // 2026-09-24: the four Streamline/NVIDIA binaries are now embedded inside
    // this DLL as RCDATA resources (proxy_d3d9.rc) rather than loose files a
    // player had to manually place -- extracted fresh on every launch to
    // %LOCALAPPDATA%\MW32011NCP\runtime_x64\streamline\, then loaded from
    // there. Direct instruction: "we shouldnt need to have extra dlls in the
    // game folder. it should all be inside our dll." All four land in the
    // SAME directory because sl.interposer.dll resolves its own sibling
    // plugins via its own directory's real file system -- see
    // dxvk_streamline_extract_x64.cpp's own header comment.
    char slDir[MAX_PATH];
    if (!ExtractEmbeddedStreamlineX64(slDir, sizeof(slDir))) {
        LogFromController("[streamline] StreamlineEnabled=1, but the embedded Streamline SDK "
            "binaries could not be extracted -- Streamline will not be initialized this session.");
        return false;
    }
    char slPath[MAX_PATH];
    sprintf_s(slPath, "%ssl.interposer.dll", slDir);

    HMODULE slModule = LoadLibraryA(slPath);
    if (!slModule) {
        char buf[500];
        sprintf_s(buf, "[streamline] StreamlineEnabled=1, but the extracted sl.interposer.dll at "
            "'%s' failed to load (err=%lu) -- Streamline will not be initialized this session.",
            slPath, GetLastError());
        LogFromController(buf);
        return false;
    }

    g_slInit = reinterpret_cast<PFun_slInit_t*>(GetProcAddress(slModule, "slInit"));
    g_slShutdown = reinterpret_cast<PFun_slShutdown_t*>(GetProcAddress(slModule, "slShutdown"));
    g_slGetFeatureRequirements = reinterpret_cast<PFun_slGetFeatureRequirements_t*>(
        GetProcAddress(slModule, "slGetFeatureRequirements"));
    g_slSetVulkanInfo = reinterpret_cast<PFun_slSetVulkanInfo_t*>(GetProcAddress(slModule, "slSetVulkanInfo"));
    if (!g_slInit || !g_slShutdown || !g_slGetFeatureRequirements || !g_slSetVulkanInfo) {
        // Literal text alone is 222 bytes; +MAX_PATH (260) for %s = 482 --
        // buf[600] leaves real margin (measured via wc -c on the literal,
        // this project's own standing sprintf_s-overflow lesson).
        char buf[600];
        sprintf_s(buf, "[streamline] Vendored sl.interposer.dll loaded from '%s' but is missing "
            "slInit/slShutdown/slGetFeatureRequirements/slSetVulkanInfo exports (corrupted/"
            "wrong-version file?) -- Streamline will not be initialized this session.", slPath);
        LogFromController(buf);
        FreeLibrary(slModule);
        g_slInit = nullptr;
        g_slShutdown = nullptr;
        g_slGetFeatureRequirements = nullptr;
        g_slSetVulkanInfo = nullptr;
        return false;
    }

    g_streamlineModule = slModule;
    char buf[500];
    sprintf_s(buf, "[streamline] Loaded vendored sl.interposer.dll from '%s' -- slInit/slShutdown/"
        "slGetFeatureRequirements/slSetVulkanInfo resolved.", slPath);
    LogFromController(buf);
    return true;
}

// Registers DXVK's own Vulkan device with Streamline via slSetVulkanInfo
// (manual-hooking path, ProgrammingGuideManualHooking.md). Per
// slSetVulkanInfo's own doc comment it must run IMMEDIATELY after the base
// interface exists -- the caller invokes this straight after slInit(), from
// Hook_CreateDevice, once DXVK has created its VkDevice.
//
// Queue indices: Streamline's VulkanInfo queue-index fields are "where the
// first SL queue starts after the host's queues" -- NOT the host's own
// queue index. DXVK creates exactly one queue per family it uses (every
// DxvkDeviceQueueIndex::index is 0; DxvkDeviceCapabilities::enableQueue()
// sets queueCount = index + 1, dxvk/src/dxvk/dxvk_device_info.cpp), so SL's
// queues would start at DXVK's graphics queue index + 1. But DXVK sizes its
// VkDeviceQueueCreateInfo itself and never creates any extra queues for SL
// -- so if the loaded feature reports it needs extra queues, SL would
// vkGetDeviceQueue indices that don't exist on this device (undefined
// behavior per the Vulkan spec). That case is refused here, loudly, rather
// than registered and left to fail inside the driver.
bool RegisterDxvkVulkanDeviceWithStreamline(IUnknown* d3d9Device)
{
    if (!d3d9Device) {
        LogFromController("[streamline] No device pointer passed to TryInitStreamlineX64() -- "
            "slSetVulkanInfo not called this session.");
        return false;
    }

    ID3D9VkInteropDeviceX64* interopDevice = nullptr;
    HRESULT hr = d3d9Device->QueryInterface(IID_ID3D9VkInteropDevice_X64,
        reinterpret_cast<void**>(&interopDevice));
    if (FAILED(hr) || !interopDevice) {
        char buf[300];
        sprintf_s(buf, "[streamline] QueryInterface(ID3D9VkInteropDevice) FAILED (hr=0x%08lX) -- the "
            "created device is not DXVK's, or DXVK's interop IID changed. slSetVulkanInfo not "
            "called this session.", static_cast<unsigned long>(hr));
        LogFromController(buf);
        return false;
    }

    VkInstance vkInstance = VK_NULL_HANDLE;
    VkPhysicalDevice vkPhysDev = VK_NULL_HANDLE;
    VkDevice vkDevice = VK_NULL_HANDLE;
    interopDevice->GetVulkanHandles(&vkInstance, &vkPhysDev, &vkDevice);

    VkQueue vkQueue = VK_NULL_HANDLE;
    uint32_t queueIndex = 0;
    uint32_t queueFamilyIndex = 0;
    interopDevice->GetSubmissionQueue(&vkQueue, &queueIndex, &queueFamilyIndex);

    // The Vulkan handles are owned by DXVK's device and outlive this
    // interface reference -- releasing it here does not invalidate them.
    interopDevice->Release();

    if (vkInstance == VK_NULL_HANDLE || vkPhysDev == VK_NULL_HANDLE ||
        vkDevice == VK_NULL_HANDLE || vkQueue == VK_NULL_HANDLE) {
        char buf[300];
        sprintf_s(buf, "[streamline] ID3D9VkInteropDevice returned a null handle (instance=%p "
            "physDev=%p device=%p queue=%p) -- slSetVulkanInfo not called this session.",
            static_cast<void*>(vkInstance), static_cast<void*>(vkPhysDev),
            static_cast<void*>(vkDevice), static_cast<void*>(vkQueue));
        LogFromController(buf);
        return false;
    }

    sl::FeatureRequirements reqs{};
    sl::Result reqResult = g_slGetFeatureRequirements(sl::kFeatureDLSS, reqs);
    if (reqResult != sl::Result::eOk) {
        char buf[300];
        sprintf_s(buf, "[streamline] slGetFeatureRequirements(DLSS) FAILED (result=%d) -- cannot "
            "confirm DXVK's device has the queues DLSS needs. slSetVulkanInfo not called this "
            "session.", static_cast<int>(reqResult));
        LogFromController(buf);
        return false;
    }

    {
        char buf[400];
        sprintf_s(buf, "[streamline] DLSS Vulkan requirements: graphicsQueues=%u computeQueues=%u "
            "opticalFlowQueues=%u deviceExtensions=%u instanceExtensions=%u features12=%u "
            "features13=%u.",
            reqs.vkNumGraphicsQueuesRequired, reqs.vkNumComputeQueuesRequired,
            reqs.vkNumOpticalFlowQueuesRequired, reqs.vkNumDeviceExtensions,
            reqs.vkNumInstanceExtensions, reqs.vkNumFeatures12, reqs.vkNumFeatures13);
        LogFromController(buf);
    }

    if (reqs.vkNumGraphicsQueuesRequired != 0 || reqs.vkNumComputeQueuesRequired != 0 ||
        reqs.vkNumOpticalFlowQueuesRequired != 0) {
        LogFromController("[streamline] DLSS reports it needs extra Vulkan queues, but DXVK creates "
            "its VkDevice with exactly one queue per family and none reserved for Streamline -- "
            "registering would make SL fetch queues that don't exist. slSetVulkanInfo not called "
            "this session.");
        return false;
    }

    // Graphics family supports compute too (DXVK picks it with
    // VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, DxvkDeviceCapabilities::
    // enableQueues()), so it's a valid compute family for SL as well. With
    // zero extra queues required (checked above) SL never fetches from these
    // start indices -- they're set to the correct "first slot after DXVK's
    // queue" value regardless, per the field's documented meaning.
    sl::VulkanInfo vkInfo{};
    vkInfo.instance = vkInstance;
    vkInfo.physicalDevice = vkPhysDev;
    vkInfo.device = vkDevice;
    vkInfo.graphicsQueueFamily = queueFamilyIndex;
    vkInfo.graphicsQueueIndex = queueIndex + 1;
    vkInfo.computeQueueFamily = queueFamilyIndex;
    vkInfo.computeQueueIndex = queueIndex + 1;

    sl::Result vkResult = g_slSetVulkanInfo(vkInfo);
    if (vkResult != sl::Result::eOk) {
        char buf[200];
        sprintf_s(buf, "[streamline] slSetVulkanInfo() FAILED (result=%d).", static_cast<int>(vkResult));
        LogFromController(buf);
        return false;
    }

    g_streamlineVulkanInfoSet = true;
    char buf[400];
    sprintf_s(buf, "[streamline] slSetVulkanInfo() succeeded -- DXVK's Vulkan instance/device "
        "registered with Streamline (DXVK queue: family=%u index=%u). Resource tagging and frame "
        "tracking are the next unstarted steps.", queueFamilyIndex, queueIndex);
    LogFromController(buf);
    return true;
}

} // namespace

// Opt-in entry point -- call once, right after CreateDevice returns
// (Hook_CreateDevice, d3d9_hook.cpp), passing the just-created
// IDirect3DDevice9 so DXVK's Vulkan-interop interface can be queried for
// slSetVulkanInfo. Gated independently here too (not just by the caller) so
// this function is safe to call unconditionally without the caller needing
// to duplicate the gate logic -- matches this project's own established
// "defensive, redundant gating" convention for every other
// structurally-significant feature.
bool TryInitStreamlineX64(IUnknown* d3d9Device)
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
    LogFromController("[streamline] slInit() succeeded -- Streamline SDK is now initialized.");

    // slInit() itself succeeded, so Streamline stays initialized even if
    // device registration fails -- the return value reports slInit()'s own
    // outcome; registration state is g_streamlineVulkanInfoSet.
    RegisterDxvkVulkanDeviceWithStreamline(d3d9Device);
    return true;
}

bool IsStreamlineInitializedX64()
{
    return g_streamlineInitialized;
}
