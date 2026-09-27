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
#include "../third_party/streamline/include/sl_dlss.h"

extern void LogFromController(const char* msg); // defined in dllmain.cpp -- real
    // external-linkage logger every other file in this codebase uses (dllmain.cpp's
    // own Log() sits inside an anonymous namespace, internal linkage only).
extern bool ExtractEmbeddedStreamlineX64(char* outDir, size_t outDirSize); // dxvk_streamline_extract_x64.cpp

namespace {

using PFun_slInit_t = sl::Result(const sl::Preferences&, uint64_t);
using PFun_slShutdown_t = sl::Result();
using PFun_slGetFeatureRequirements_t = sl::Result(sl::Feature, sl::FeatureRequirements&);
using PFun_slSetVulkanInfo_t = sl::Result(const sl::VulkanInfo&);
using PFun_slGetNewFrameToken_t = sl::Result(sl::FrameToken*&, const uint32_t*);
using PFun_slSetTagForFrame_t = sl::Result(const sl::FrameToken&, const sl::ViewportHandle&,
    const sl::ResourceTag*, uint32_t, sl::CommandBuffer*);
using PFun_slSetConstants_t = sl::Result(const sl::Constants&, const sl::FrameToken&, const sl::ViewportHandle&);
using PFun_slEvaluateFeature_t = sl::Result(sl::Feature, const sl::FrameToken&, const sl::BaseStructure**, uint32_t, sl::CommandBuffer*);
using PFun_slGetFeatureFunction_t = sl::Result(sl::Feature, const char*, void*&);

HMODULE g_streamlineModule = nullptr;
PFun_slInit_t* g_slInit = nullptr;
PFun_slShutdown_t* g_slShutdown = nullptr;
PFun_slGetFeatureRequirements_t* g_slGetFeatureRequirements = nullptr;
PFun_slSetVulkanInfo_t* g_slSetVulkanInfo = nullptr;
PFun_slGetNewFrameToken_t* g_slGetNewFrameToken = nullptr;
PFun_slSetTagForFrame_t* g_slSetTagForFrame = nullptr;
PFun_slSetConstants_t* g_slSetConstants = nullptr;
PFun_slEvaluateFeature_t* g_slEvaluateFeature = nullptr; // 2026-09-26 -- the real DLSS
    // evaluation call, resolved alongside the other core exports (see
    // TryLoadStreamlineInterposer's own missing-exports check below).
PFun_slGetFeatureFunction_t* g_slGetFeatureFunction = nullptr; // 2026-09-26 -- resolves
    // per-feature functions (slDLSSSetOptions, etc.) by name; per its own doc comment
    // (sl_core_api.h) this must only be called AFTER slSetVulkanInfo has succeeded.
// slDLSSSetOptions itself -- resolved lazily via g_slGetFeatureFunction the first time
// StreamlineDLSSSetOptionsX64 is called (not at load time, since it needs a real device
// registered first per that function's own doc comment), cached after.
using PFun_slDLSSSetOptions_t = sl::Result(const sl::ViewportHandle&, const sl::DLSSOptions&);
PFun_slDLSSSetOptions_t* g_slDLSSSetOptions = nullptr;
using PFun_slIsFeatureLoaded_t = sl::Result(sl::Feature, bool&);
PFun_slIsFeatureLoaded_t* g_slIsFeatureLoaded = nullptr; // 2026-09-27, real
    // DLSS_NR detection -- resolved best-effort (not part of the FATAL
    // missing-exports check below, since it's diagnostic-only and this
    // SDK version is already confirmed to export it either way).
bool g_streamlineInitialized = false;
bool g_streamlineVulkanInfoSet = false;
sl::FrameToken* g_streamlineCurrentFrameToken = nullptr; // owned by Streamline itself,
    // refreshed once per real frame by StreamlineFrameTick() -- never freed by us.
long long g_streamlineFrameSequenceX64 = 0; // 2026-09-27 -- incremented only on a
    // genuinely successful slGetNewFrameToken() call (StreamlineFrameTick), exposed
    // via GetStreamlineFrameSequenceX64() so streamline_camera_x64.cpp can detect
    // "is this still the same real frame as my last slSetConstants call" without
    // needing to inspect sl::FrameToken's own opaque internals -- see that fix's own
    // comment (streamline_camera_x64.cpp) for the real bug this closes.
VkInstance g_dxvkVkInstanceX64 = VK_NULL_HANDLE; // cached in RegisterDxvkVulkanDeviceWithStreamline,
VkDevice g_dxvkVkDeviceX64 = VK_NULL_HANDLE;     // 2026-09-24 -- real handles, exposed via
VkQueue g_dxvkVkQueueX64 = VK_NULL_HANDLE;       // 2026-09-26 -- added for gpu_timing_probe_x64.cpp's
    // own real vkQueueWaitIdle-based GPU-inclusive frame timing (see that file's own header comment) --
    // the real DXVK submission queue, same one Streamline registration already resolves here, just also
    // cached now instead of only used locally.
VkPhysicalDevice g_dxvkVkPhysDeviceX64 = VK_NULL_HANDLE; // cached alongside the above, 2026-09-26 --
    // needed by RegisterDxvkVulkanDeviceWithStreamline's own vkInfo.physicalDevice field, now resolved
    // once in the shared ResolveAndCacheDxvkVulkanHandlesX64 instead of duplicated.
uint32_t g_dxvkVkQueueIndexX64 = 0;
uint32_t g_dxvkVkQueueFamilyIndexX64 = 0;
    // GetDxvkVkInstanceX64()/GetDxvkVkDeviceX64() below for streamline_resources_x64.cpp's
    // own real vkCreateImageView call (resource tagging needs a real VkImageView, which
    // ID3D9VkInteropTexture::GetVulkanImageInfo alone does not provide).

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
    g_slGetNewFrameToken = reinterpret_cast<PFun_slGetNewFrameToken_t*>(
        GetProcAddress(slModule, "slGetNewFrameToken"));
    g_slSetTagForFrame = reinterpret_cast<PFun_slSetTagForFrame_t*>(
        GetProcAddress(slModule, "slSetTagForFrame"));
    g_slSetConstants = reinterpret_cast<PFun_slSetConstants_t*>(
        GetProcAddress(slModule, "slSetConstants"));
    g_slEvaluateFeature = reinterpret_cast<PFun_slEvaluateFeature_t*>(
        GetProcAddress(slModule, "slEvaluateFeature"));
    g_slGetFeatureFunction = reinterpret_cast<PFun_slGetFeatureFunction_t*>(
        GetProcAddress(slModule, "slGetFeatureFunction"));
    g_slIsFeatureLoaded = reinterpret_cast<PFun_slIsFeatureLoaded_t*>(
        GetProcAddress(slModule, "slIsFeatureLoaded")); // best-effort, not FATAL if missing
    if (!g_slInit || !g_slShutdown || !g_slGetFeatureRequirements || !g_slSetVulkanInfo
        || !g_slGetNewFrameToken || !g_slSetTagForFrame || !g_slSetConstants
        || !g_slEvaluateFeature || !g_slGetFeatureFunction) {
        // Real worst-case measured (wc -c on the literal): 306 bytes; +MAX_PATH
        // (260) for %s = 566 -- buf[750] leaves real margin (this project's own
        // standing sprintf_s-overflow lesson). Widened to 900 (2026-09-26) after
        // adding two more export names to the same literal.
        char buf[900];
        sprintf_s(buf, "[streamline] Vendored sl.interposer.dll loaded from '%s' but is missing "
            "slInit/slShutdown/slGetFeatureRequirements/slSetVulkanInfo/slGetNewFrameToken/"
            "slSetTagForFrame/slSetConstants/slEvaluateFeature/slGetFeatureFunction exports "
            "(corrupted/wrong-version file?) -- Streamline will not be initialized this session.", slPath);
        LogFromController(buf);
        FreeLibrary(slModule);
        g_slInit = nullptr;
        g_slShutdown = nullptr;
        g_slGetFeatureRequirements = nullptr;
        g_slSetVulkanInfo = nullptr;
        g_slGetNewFrameToken = nullptr;
        g_slSetTagForFrame = nullptr;
        g_slSetConstants = nullptr;
        g_slEvaluateFeature = nullptr;
        g_slGetFeatureFunction = nullptr;
        return false;
    }

    g_streamlineModule = slModule;
    char buf[650];
    sprintf_s(buf, "[streamline] Loaded vendored sl.interposer.dll from '%s' -- slInit/slShutdown/"
        "slGetFeatureRequirements/slSetVulkanInfo/slGetNewFrameToken/slSetTagForFrame/"
        "slSetConstants/slEvaluateFeature/slGetFeatureFunction resolved.", slPath);
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
} // namespace -- closed early here, not at this file's original spot further down.
// ResolveAndCacheDxvkVulkanHandlesX64 needs real external linkage (called directly
// from d3d9_hook.cpp), and RegisterDxvkVulkanDeviceWithStreamline now calls it, so
// both must sit outside the anonymous namespace -- the exact same anonymous-
// namespace-internal-linkage trap this project has hit and documented many times
// before (see e.g. RecordRenderViewFireX64's own comment, analog_input_hooks_x64.cpp).

// Extracted 2026-09-26 from RegisterDxvkVulkanDeviceWithStreamline's own top --
// gpu_timing_probe_x64.cpp needs the real DXVK Vulkan instance/device/queue for its
// own GPU-inclusive frame-timing diagnostic (vkQueueWaitIdle-based, see that file's
// own header comment), completely independent of Streamline/DLSS. The Streamline
// registration path below is gated on g_modConfig.streamlineEnabled -- a narrow,
// separate opt-in feature -- so this resolution step must NOT live behind that gate;
// it's called unconditionally from Hook_CreateDevice whenever GraphicsApi==Vulkan,
// and RegisterDxvkVulkanDeviceWithStreamline below now just reuses the cached result
// if it already ran (idempotent: returns the cached handles on a second call without
// re-querying).
bool ResolveAndCacheDxvkVulkanHandlesX64(IUnknown* d3d9Device)
{
    if (g_dxvkVkDeviceX64 != VK_NULL_HANDLE) return true; // already resolved this session

    if (!d3d9Device) {
        LogFromController("[dxvk-vk-handles] No device pointer passed -- Vulkan handle resolution skipped.");
        return false;
    }

    ID3D9VkInteropDeviceX64* interopDevice = nullptr;
    HRESULT hr = d3d9Device->QueryInterface(IID_ID3D9VkInteropDevice_X64,
        reinterpret_cast<void**>(&interopDevice));
    if (FAILED(hr) || !interopDevice) {
        char buf[300];
        sprintf_s(buf, "[dxvk-vk-handles] QueryInterface(ID3D9VkInteropDevice) FAILED (hr=0x%08lX) -- the "
            "created device is not DXVK's, or DXVK's interop IID changed.", static_cast<unsigned long>(hr));
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

    interopDevice->Release();

    g_dxvkVkInstanceX64 = vkInstance;
    g_dxvkVkDeviceX64 = vkDevice;
    g_dxvkVkQueueX64 = vkQueue;
    g_dxvkVkPhysDeviceX64 = vkPhysDev;
    g_dxvkVkQueueIndexX64 = queueIndex;
    g_dxvkVkQueueFamilyIndexX64 = queueFamilyIndex;

    char buf[200];
    sprintf_s(buf, "[dxvk-vk-handles] Resolved real DXVK Vulkan handles (instance=%p device=%p queue=%p)",
        static_cast<void*>(vkInstance), static_cast<void*>(vkDevice), static_cast<void*>(vkQueue));
    LogFromController(buf);
    return vkInstance != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE && vkQueue != VK_NULL_HANDLE;
}

bool RegisterDxvkVulkanDeviceWithStreamline(IUnknown* d3d9Device)
{
    if (!ResolveAndCacheDxvkVulkanHandlesX64(d3d9Device)) {
        LogFromController("[streamline] Vulkan handle resolution failed -- slSetVulkanInfo not called this session.");
        return false;
    }

    VkInstance vkInstance = g_dxvkVkInstanceX64;
    VkDevice vkDevice = g_dxvkVkDeviceX64;
    VkQueue vkQueue = g_dxvkVkQueueX64;
    VkPhysicalDevice vkPhysDev = g_dxvkVkPhysDeviceX64;
    uint32_t queueIndex = g_dxvkVkQueueIndexX64;
    uint32_t queueFamilyIndex = g_dxvkVkQueueFamilyIndexX64;
    // Non-null instance/device/queue is already guaranteed here -- ResolveAndCacheDxvkVulkanHandlesX64
    // itself returns false (handled above) unless all three resolved to a real, non-null handle.

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

    // 2026-09-24: real required-buffer-tag list -- rather than guess which
    // buffers (color/motion-vectors/etc.) DLSS actually requires beyond the
    // depth tag already wired, read it directly from the SDK's own real
    // requirements report (numRequiredTags/requiredTags, sl_core_types.h).
    // This is the authoritative source, not an assumption from the public
    // docs' own general description of what DLSS "usually" needs.
    if (reqs.numRequiredTags > 0 && reqs.requiredTags) {
        char buf[400];
        int off = sprintf_s(buf, "[streamline] DLSS required buffer tags (%u): ", reqs.numRequiredTags);
        for (uint32_t i = 0; i < reqs.numRequiredTags && off < static_cast<int>(sizeof(buf)) - 12; ++i) {
            off += sprintf_s(buf + off, sizeof(buf) - off, "%u ", reqs.requiredTags[i]);
        }
        LogFromController(buf);
    } else {
        LogFromController("[streamline] DLSS reports zero required buffer tags (or a null list) -- "
            "real, worth double-checking rather than assuming depth-only is sufficient.");
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

// 2026-09-27: real, previously-missing Streamline log-callback -- the SDK's
// own Preferences struct has a logMessageCallback field ("Optional - Allows
// log message tracking including critical errors if they occur",
// sl_core_types.h) that was never wired, so any internal diagnostic
// Streamline itself logs (e.g. WHY a requested feature's plugin failed to
// load) went nowhere this project could see. Found live, 2026-09-26/27:
// slGetFeatureFunction(DLSS, "slDLSSSetOptions") failed with
// eErrorFeatureMissing (31) despite slInit() itself reporting success and
// slGetFeatureRequirements(DLSS) succeeding earlier -- meaning the DLSS
// plugin (sl.dlss.dll/nvngx_dlss.dll, both confirmed present on disk,
// correct sizes, not corrupted) never actually loaded, and this project had
// no way to see Streamline's own real reason why. Deliberately logs the
// prefix and message as two SEPARATE LogFromController calls rather than one
// sprintf_s'd buffer -- msg's real length is unbounded (an external SDK's
// own string, not authored in this codebase), and this project has hit the
// "sprintf_s with an unbounded interpolated string" crash class enough times
// (known_issues_x64.md, 2026-09-05/13/14/16) that avoiding it entirely here
// is safer than sizing a buffer defensively.
extern void PrewarmDlssOptionsX64(void* device); // streamline_evaluate_x64.cpp, 2026-09-27 --
    // see this function's own call site comment (right after
    // RegisterDxvkVulkanDeviceWithStreamline, below) for the real bug this closes.
    // Takes the real device pointer directly -- GetLastKnownRenderDevice()
    // (overlay_hud.cpp) isn't set yet at this point in the frame (only
    // Hook_EndScene sets it, and no frame has rendered yet here).

void StreamlineLogCallbackX64(sl::LogType type, const char* msg)
{
    if (!msg) return;
    const char* prefix = (type == sl::LogType::eError) ? "[streamline][sl-error]"
        : (type == sl::LogType::eWarn) ? "[streamline][sl-warn]" : "[streamline][sl-info]";
    LogFromController(prefix);
    LogFromController(msg);
}

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
    // 2026-09-27: sl::kFeatureDLSS_NR (=1004, "DLSS 5" Neural Rendering)
    // requested alongside kFeatureDLSS when [Experimental]
    // DlssNeuralRenderingEnabled=1 -- detection/registration only for now,
    // see g_modConfig.dlssNeuralRenderingEnabledX64's own comment
    // (mod_config.h) for the real blocker (NVIDIA's own DLSS_NR options
    // struct header, sl_dlss_nr.h, isn't in the public Streamline repo --
    // confirmed directly, and independently confirmed by real production
    // code, GaijinEntertainment/DagorEngine's own `#if __has_include` gate
    // on it). Requesting the feature is safe and harmless even without the
    // runtime plugin present -- slInit() already tolerates a requested
    // feature whose plugin fails to load (this project's own kFeatureDLSS
    // path already demonstrated that before the projectId fix).
    sl::Feature featuresToLoad[] = { sl::kFeatureDLSS, sl::kFeatureDLSS_NR };
    uint32_t numFeaturesToLoad = g_modConfig.dlssNeuralRenderingEnabledX64 ? 2 : 1;
    sl::Preferences pref{};
    // 2026-09-24: eUseFrameBasedResourceTagging added -- real, live-caught
    // bug. slSetTagForFrame() failed every time with eErrorInvalidIntegration
    // (result=19) until this flag was found: sl_core_api.h's own doc comment
    // on the deprecated slSetTag() function states plainly "Use the version
    // of this function that takes a sl::FrameToken instead - slSetTagForFrame
    // and set sl::PreferenceFlags::eUseFrameBasedResourceTagging" -- this
    // project already uses the FrameToken-based slSetTagForFrame exclusively
    // (never the deprecated slSetTag), so this flag was required from the
    // start and simply missing.
    pref.flags = sl::PreferenceFlags::eUseManualHooking
        | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    pref.renderAPI = sl::RenderAPI::eVulkan;
    pref.featuresToLoad = featuresToLoad;
    pref.numFeaturesToLoad = numFeaturesToLoad;
    pref.engine = sl::EngineType::eCustom;
    pref.engineVersion = "MW32011NCP";
    // 2026-09-27: REAL ROOT CAUSE of "Please provide correct application id" /
    // eErrorFeatureMissing, found by reading NVIDIA's own open-source Streamline
    // plugin source directly (github.com/NVIDIA-RTX/Streamline,
    // source/plugins/sl.common/commonEntry.cpp) after the newly-wired log
    // callback surfaced the error but not WHY it fires despite engine/
    // engineVersion already being set. The real condition (commonEntry.cpp,
    // ~line 1494): `bool hasProjectId = !engineVersion.empty() && !projectId.empty();`
    // -- NGX's "no application id needed" path requires BOTH engineVersion AND
    // projectId to be non-empty; engineVersion alone (this project's own prior
    // setup) is NOT sufficient. projectId is a self-chosen GUID (sl_core_types.h's
    // own doc comment: "Optional - GUID (like for example 'a0f57b54-1daf-4934-
    // 90ae-c4035c19df04')") -- nothing about it implies NVIDIA has to issue or
    // register it, unlike applicationId. This is the real, working path an
    // independent developer without an NVIDIA partner relationship uses --
    // confirmed directly from NVIDIA's own source, not a guess or a forum post.
    // GUID generated locally for this project (PowerShell [guid]::NewGuid()),
    // not obtained from or registered with NVIDIA -- matches the doc's own
    // example format, self-chosen per its own "Optional" framing.
    pref.projectId = "f648f6eb-4aaf-455a-8ac9-abdcebfb9dcd";
    // 2026-09-27: real log callback wired (see StreamlineLogCallbackX64's own
    // comment) -- eWarn/eError are "Always shown regardless of LogLevel" per
    // sl_core_types.h's own doc comment on LogType, so eDefault (not eVerbose)
    // is enough to surface the real reason a feature plugin fails to load
    // without risking a per-frame-chatty eVerbose log flood this project has
    // no prior data on.
    pref.logLevel = sl::LogLevel::eDefault;
    pref.logMessageCallback = &StreamlineLogCallbackX64;

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

    // REAL BUG FIX, 2026-09-27: live-reported theory, direct user diagnosis
    // ("its our dlss pipeline, it loads too late the vieport initialises the
    // goes black in game") -- slDLSSSetOptions (which triggers DLSS's own
    // real, heavy first-time GPU resource allocation) used to only ever fire
    // lazily, on the first real GAMEPLAY frame (gated behind menu/in-level/
    // clcState checks inside EvaluateStreamlineDlssX64) -- meaning that heavy
    // allocation happened cold, in the middle of an already-busy gameplay
    // frame, rather than at a quiet, idle moment. Combined with a large
    // render-scale-driven DLAA resolution, this is a real, plausible driver-
    // level TDR/VK_ERROR_DEVICE_LOST trigger (see this session's own real
    // device-loss finding, streamline_evaluate_x64.cpp's own
    // g_dlssDeviceLostX64 comment). Fixed by pre-warming slDLSSSetOptions
    // HERE, immediately after device registration succeeds -- while the
    // frame is still simple/idle (at or near the main menu, well before any
    // real gameplay content exists) -- using the real display resolution.
    // EvaluateStreamlineDlssX64's own existing lazy re-check still runs later
    // and correctly upgrades to the DLAA-effective resolution/mode once real
    // gameplay starts and a render-scale mismatch is actually detected -- but
    // that becomes a smaller, cheaper RE-configure call, not DLSS's original
    // cold, heavy first-time allocation.
    if (g_streamlineVulkanInfoSet) PrewarmDlssOptionsX64(d3d9Device);

    // 2026-09-27: real DLSS_NR ("DLSS 5" Neural Rendering) load-status
    // detection -- diagnostic only. Confirms whether the requested feature's
    // own plugin (sl.dlss_nr.dll) actually loaded, independent of whether
    // this project can yet DO anything with it (it can't -- see
    // g_modConfig.dlssNeuralRenderingEnabledX64's own comment, mod_config.h,
    // for the real remaining blocker: no verified DLSSNROptions struct
    // layout, no vendored runtime binary).
    if (g_modConfig.dlssNeuralRenderingEnabledX64) {
        if (!g_slIsFeatureLoaded) {
            LogFromController("[streamline] DlssNeuralRenderingEnabled=1, but slIsFeatureLoaded "
                "failed to resolve -- cannot confirm whether sl.dlss_nr.dll actually loaded.");
        } else {
            bool loaded = false;
            sl::Result nrResult = g_slIsFeatureLoaded(sl::kFeatureDLSS_NR, loaded);
            char buf[300];
            sprintf_s(buf, "[streamline] DLSS_NR (\"DLSS 5\" Neural Rendering) load status: "
                "result=%d loaded=%d -- detection only, no real evaluate path wired yet (real "
                "blocker: no verified DLSSNROptions struct/runtime binary -- see "
                "re_notes/x64_migration/vulkan_dlss_pipeline_research.md item 17).",
                static_cast<int>(nrResult), loaded ? 1 : 0);
            LogFromController(buf);
        }
    }

    return true;
}

// extern "C" (not plain C++ linkage) so every other Streamline-adjacent
// file (streamline_resources_x64.cpp/streamline_camera_x64.cpp/
// streamline_object_motion_x64.cpp) can forward-declare and call this
// without needing to pull in this file's own headers -- this is the single,
// most correct gate for "is it actually safe/meaningful to do real
// Streamline-adjacent work right now": it's false unless StreamlineEnabled,
// GraphicsApi==Vulkan, SP, AND slInit() itself actually succeeded all held
// (see TryInitStreamlineX64's own gate chain above) -- a stronger, more
// specific signal than re-checking the raw config flags at each call site,
// and the real fix for a genuine gap found 2026-09-26 (direct user
// instruction: "make sure theyre code gated based on api too, we dont wanna
// accidentally enable weird behavbiopuirs") -- several real per-frame
// Streamline-adjacent entry points (the SetRenderTarget/SetDepthStencilSurface
// resource-tagging hook INSTALL, the per-frame camera-matrix build, the
// per-frame DObj motion-snapshot capture) were doing real, unconditional
// work regardless of backend or even whether Streamline was enabled at all
// -- never unsafe (the one real SDK call, slSetConstants via
// StreamlineSetConstantsX64, was already correctly gated on
// g_streamlineVulkanInfoSet), but genuine wasted per-frame CPU work with
// zero purpose whenever Streamline isn't actually active, now that Vulkan
// is SP's own default. See each call site's own comment for the specific
// fix.
extern "C" bool IsStreamlineInitializedX64()
{
    return g_streamlineInitialized;
}

// Real DXVK Vulkan instance/device handles, cached once real registration
// succeeds -- streamline_resources_x64.cpp's own real vkCreateImageView call
// needs these directly (resource tagging groundwork, 2026-09-24). Returns
// VK_NULL_HANDLE if Streamline/DXVK registration hasn't happened yet;
// callers must check.
VkInstance GetDxvkVkInstanceX64()
{
    return g_dxvkVkInstanceX64;
}

VkDevice GetDxvkVkDeviceX64()
{
    return g_dxvkVkDeviceX64;
}

VkQueue GetDxvkVkQueueX64()
{
    return g_dxvkVkQueueX64;
}

// 2026-09-26 -- needed by streamline_evaluate_x64.cpp to create a real
// VkCommandPool against the same queue family Streamline's own registered
// queue belongs to (a command pool is created FOR a specific queue family;
// using the wrong one would fail command-buffer allocation).
uint32_t GetDxvkVkGraphicsQueueFamilyIndexX64()
{
    return g_dxvkVkQueueFamilyIndexX64;
}

// 2026-09-24: real frame-tracking groundwork -- the next unstarted step per
// slSetVulkanInfo()'s own success log line. slGetNewFrameToken() must be
// called once per real frame (ProgrammingGuideManualHooking.md); the
// returned FrameToken is required by every later call this integration will
// need (slSetTagForFrame, slSetConstants, slEvaluateFeature). Deliberately
// mirrors this project's own "trivial passthrough first" convention: this
// slice ONLY calls the API and logs the result, no resource tagging or
// constants yet (both need real camera/motion-vector data this project
// hasn't RE'd -- see re_notes/x64_migration/vulkan_dlss_pipeline_research.md
// item 16, Stage 1 camera-only motion vectors, still unstarted).
//
// Called from Hook_EndScene (overlay_hud.cpp), the one confirmed
// exactly-once-per-real-frame hook point this project already uses for
// every other per-frame diagnostic/feature. Gated on g_streamlineVulkanInfoSet
// (not just g_streamlineInitialized) -- calling any Streamline API before
// slSetVulkanInfo has actually registered a real device is undefined per the
// manual-hooking guide.
void StreamlineFrameTick()
{
    if (!g_streamlineVulkanInfoSet) return;

    sl::Result result = g_slGetNewFrameToken(g_streamlineCurrentFrameToken, nullptr);

    static long long s_frameTickCount = 0;
    ++s_frameTickCount;
    if (result != sl::Result::eOk) {
        static bool s_failureLogged = false;
        if (!s_failureLogged) {
            s_failureLogged = true;
            char buf[200];
            sprintf_s(buf, "[streamline] slGetNewFrameToken() FAILED (result=%d, frame=%lld) -- "
                "frame tracking is not working this session.", static_cast<int>(result), s_frameTickCount);
            LogFromController(buf);
        }
        return;
    }

    ++g_streamlineFrameSequenceX64; // only reached on a real success -- the early
        // return above on failure deliberately does NOT advance this.

    if (s_frameTickCount == 1 || (s_frameTickCount % 5000) == 0) {
        char buf[200];
        sprintf_s(buf, "[streamline] slGetNewFrameToken() succeeded (frame=%lld, token=0x%p) -- "
            "real per-frame tracking is live.", s_frameTickCount, static_cast<void*>(g_streamlineCurrentFrameToken));
        LogFromController(buf);
    }
}

// 2026-09-27: public accessor for g_streamlineFrameSequenceX64 -- see that
// global's own comment. Returns 0 if no real frame has ticked yet this
// session (a genuinely impossible sequence value for any call made after
// the first successful StreamlineFrameTick(), since it's pre-incremented
// starting from 0 -- callers should treat 0 as "not yet ticked," not as a
// real frame's sequence number).
long long GetStreamlineFrameSequenceX64()
{
    return g_streamlineFrameSequenceX64;
}

// 2026-09-24: real slSetTagForFrame wrapper -- the next unstarted step after
// frame tracking. Called from streamline_resources_x64.cpp with a real
// sl::ResourceTag array (built from the DXVK-interop-resolved depth image).
// Uses the real, already-tracked FrameToken and viewport 0 (this project
// only ever has one active render viewport). cmdBuffer is deliberately
// nullptr -- valid per slSetTagForFrame's own doc comment ("optional and can
// be null if ALL tags are null or have eValidUntilPresent life-cycle") as
// long as every tag this project supplies uses ResourceLifecycle::
// eValidUntilPresent, which streamline_resources_x64.cpp's own tags do.
// Gated the same way every other post-slSetVulkanInfo Streamline call in
// this codebase is: no-ops (returns false) until the real device has been
// registered.
bool StreamlineSetTagForFrameX64(const sl::ResourceTag* tags, uint32_t numTags)
{
    if (!g_streamlineVulkanInfoSet) return false;
    if (!g_streamlineCurrentFrameToken) return false;

    sl::ViewportHandle viewport(static_cast<uint32_t>(0));
    sl::Result result = g_slSetTagForFrame(*g_streamlineCurrentFrameToken, viewport,
        tags, numTags, nullptr);

    static long long s_tagTickCount = 0;
    ++s_tagTickCount;
    bool heartbeat = s_tagTickCount <= 5 || (s_tagTickCount % 5000) == 0;
    if (result != sl::Result::eOk) {
        static bool s_failureLogged = false;
        if (!s_failureLogged || heartbeat) {
            s_failureLogged = true;
            char buf[200];
            sprintf_s(buf, "[streamline] slSetTagForFrame() FAILED (result=%d, tick=%lld, "
                "numTags=%u)", static_cast<int>(result), s_tagTickCount, numTags);
            LogFromController(buf);
        }
        return false;
    }

    if (heartbeat) {
        char buf[150];
        sprintf_s(buf, "[streamline] slSetTagForFrame() succeeded (tick=%lld, numTags=%u)",
            s_tagTickCount, numTags);
        LogFromController(buf);
    }
    return true;
}

// 2026-09-24: real slSetConstants wrapper -- the next unstarted step after
// resource tagging (all four required tags now wired,
// streamline_resources_x64.cpp). Called from streamline_camera_x64.cpp with
// a real, built sl::Constants (camera-to-world basis + the independently-
// built standard projection matrix). Same real FrameToken/viewport-0
// convention as StreamlineSetTagForFrameX64.
bool StreamlineSetConstantsX64(const sl::Constants& constants)
{
    if (!g_streamlineVulkanInfoSet) return false;
    if (!g_streamlineCurrentFrameToken) return false;

    sl::ViewportHandle viewport(static_cast<uint32_t>(0));
    sl::Result result = g_slSetConstants(constants, *g_streamlineCurrentFrameToken, viewport);

    static long long s_constantsTickCount = 0;
    ++s_constantsTickCount;
    bool heartbeat = s_constantsTickCount <= 5 || (s_constantsTickCount % 5000) == 0;
    if (result != sl::Result::eOk) {
        static bool s_failureLogged = false;
        if (!s_failureLogged || heartbeat) {
            s_failureLogged = true;
            char buf[150];
            sprintf_s(buf, "[streamline] slSetConstants() FAILED (result=%d, tick=%lld)",
                static_cast<int>(result), s_constantsTickCount);
            LogFromController(buf);
        }
        return false;
    }

    if (heartbeat) {
        char buf[100];
        sprintf_s(buf, "[streamline] slSetConstants() succeeded (tick=%lld)", s_constantsTickCount);
        LogFromController(buf);
    }
    return true;
}

// 2026-09-26: real slDLSSSetOptions wrapper -- MANDATORY before the first
// slEvaluateFeature(kFeatureDLSS, ...) call, per sl_dlss.h's own doc comment
// ("Call this method to turn DLSS on/off, change mode etc."). Resolves the
// real per-feature function pointer lazily via slGetFeatureFunction (its own
// doc comment: "must be called AFTER device is set... slSetVulkanInfo"), so
// this can't run any earlier than slSetVulkanInfo's own success -- matches
// this file's existing g_streamlineVulkanInfoSet gate. Called once from
// streamline_evaluate_x64.cpp right after real device registration, and
// again whenever the real output resolution changes (the DLSS output-buffer
// texture is recreated on that same event, streamline_resources_x64.cpp).
bool StreamlineDLSSSetOptionsX64(uint32_t outputWidth, uint32_t outputHeight, int modeValue)
{
    if (!g_streamlineVulkanInfoSet) return false;

    if (!g_slDLSSSetOptions) {
        void* fn = nullptr;
        sl::Result resolveResult = g_slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", fn);
        if (resolveResult != sl::Result::eOk || !fn) {
            char buf[200];
            sprintf_s(buf, "[streamline] slGetFeatureFunction(DLSS, \"slDLSSSetOptions\") FAILED "
                "(result=%d) -- DLSS cannot be configured this session.", static_cast<int>(resolveResult));
            LogFromController(buf);
            return false;
        }
        g_slDLSSSetOptions = reinterpret_cast<PFun_slDLSSSetOptions_t*>(fn);
        LogFromController("[streamline] slDLSSSetOptions resolved via slGetFeatureFunction.");
    }

    // modeValue is g_modConfig.dlssModeX64, already range-checked to [0,6] at
    // config-load time (mod_config.cpp) -- matches sl::DLSSMode's own real
    // enum range (eOff..eDLAA, sl_dlss.h) exactly, so a plain cast is safe.
    sl::DLSSOptions options{};
    options.mode = static_cast<sl::DLSSMode>(modeValue);
    options.outputWidth = outputWidth;
    options.outputHeight = outputHeight;
    options.colorBuffersHDR = sl::Boolean::eFalse; // this project's own scene-color
        // target is D3DFMT_A8R8G8B8/VK_FORMAT_B8G8R8A8_UNORM (8-bit, non-HDR) --
        // see TagColorResourceForFrame's own confirmed live format.

    sl::ViewportHandle viewport(static_cast<uint32_t>(0));
    sl::Result result = g_slDLSSSetOptions(viewport, options);
    char buf[250];
    sprintf_s(buf, "[streamline] slDLSSSetOptions(mode=%d, output=%ux%u) %s (result=%d)",
        modeValue, outputWidth, outputHeight, result == sl::Result::eOk ? "succeeded" : "FAILED",
        static_cast<int>(result));
    LogFromController(buf);
    return result == sl::Result::eOk;
}

// 2026-09-26: real slEvaluateFeature wrapper -- the actual call that makes
// DLSS run. Must be called with the SAME FrameToken this frame's own
// slSetTagForFrame/slSetConstants calls used (frame/viewport must match
// per slEvaluateFeature's own doc comment) -- i.e. BEFORE StreamlineFrameTick()
// advances g_streamlineCurrentFrameToken to the next frame's token. See
// streamline_evaluate_x64.cpp's own header comment for the full per-frame
// ordering this depends on, and overlay_hud.cpp's Hook_EndScene for where
// this is actually called from.
bool StreamlineEvaluateFeatureX64(void* vkCommandBuffer)
{
    if (!g_streamlineVulkanInfoSet) return false;
    if (!g_streamlineCurrentFrameToken) return false;
    if (!vkCommandBuffer) return false;

    sl::ViewportHandle viewport(static_cast<uint32_t>(0));
    const sl::BaseStructure* inputs[] = { &viewport };

    sl::Result result = g_slEvaluateFeature(sl::kFeatureDLSS, *g_streamlineCurrentFrameToken,
        inputs, 1, reinterpret_cast<sl::CommandBuffer*>(vkCommandBuffer));

    static long long s_evaluateTickCount = 0;
    ++s_evaluateTickCount;
    bool heartbeat = s_evaluateTickCount <= 5 || (s_evaluateTickCount % 5000) == 0;
    if (result != sl::Result::eOk) {
        static bool s_failureLogged = false;
        if (!s_failureLogged || heartbeat) {
            s_failureLogged = true;
            char buf[150];
            sprintf_s(buf, "[streamline] slEvaluateFeature(DLSS) FAILED (result=%d, tick=%lld)",
                static_cast<int>(result), s_evaluateTickCount);
            LogFromController(buf);
        }
        return false;
    }

    if (heartbeat) {
        char buf[100];
        sprintf_s(buf, "[streamline] slEvaluateFeature(DLSS) succeeded (tick=%lld)", s_evaluateTickCount);
        LogFromController(buf);
    }
    return true;
}
