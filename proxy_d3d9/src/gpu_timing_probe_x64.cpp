// gpu_timing_probe_x64.cpp -- added 2026-09-26, direct user instruction: "we need a
// custom gpu dump tool that tells us all info we need statically." Built after this
// project's own extensive session-long chase of the pause-vs-live/sunny-map fps
// regression hit a real, consistent wall: every CPU-side static-RE lead traced (SSAO,
// the main-loop mechanism, the master render sequencer, FUN_1401939f0's post-effect
// chain, cl_paused's own direct references, the UI jump table, GSC script scheduling)
// turned out to be either a flat/unconditional cost or genuinely unrelated -- and
// separately, the two "measure it directly" tools already tried this session (a real
// CPU thread sampler, self_sampling_profiler_x64.cpp; and RenderDoc frame captures
// converted to chrome.json) both structurally CANNOT see real GPU execution time:
// the CPU sampler can't distinguish "genuinely idle" from "correctly blocked waiting
// on the GPU," and RenderDoc's own chrome.json export only contains CPU-side API-call
// recording timestamps, never real hardware GPU duration.
//
// THIS TOOL closes that gap directly, without any external tool or capture file: it
// forces a real Vulkan queue synchronization (vkQueueWaitIdle) at chosen points in
// this project's own already-hooked per-frame render path, using the real DXVK
// Vulkan device/queue this project already resolves via DXVK's own public
// ID3D9VkInteropDevice interop interface (see streamline_integration_x64.cpp's
// ResolveAndCacheDxvkVulkanHandlesX64 -- extracted from the Streamline init path
// specifically so this tool doesn't need StreamlineEnabled=1 to work). Because
// vkQueueWaitIdle blocks the CALLING (CPU) thread until every previously submitted
// unit of GPU work on that queue has ACTUALLY FINISHED EXECUTING on the hardware,
// the real wall-clock time between two consecutive marks is genuinely GPU-inclusive
// -- not just how long it took to record/submit commands, which is all the CPU-side
// tools above could ever show.
//
// REAL, DELIBERATE TRADEOFF, stated plainly: forcing a hardware sync point removes
// all CPU/GPU pipelining between marks -- the GPU sits idle waiting for the CPU to
// finish recording the next batch of work, and the CPU sits idle waiting for the GPU
// to finish the last batch, instead of the two overlapping as they normally do. This
// WILL cost real, substantial fps while active, the same category of overhead
// RenderDoc's own instrumentation already demonstrated this session ("renderdoc
// itself added huge overhead"). This is a diagnostic tool, not something any player
// would ever run with by default -- hence `[Experimental] GpuSyncTimingLogging`,
// off by default (see mod_config.h's own comment on that field for the full
// rationale), Vulkan/DXVK-only (LegacyD3D9 has no Vulkan queue to sync against at
// all -- the tool is simply inert, not broken, under that backend).
//
// WHAT IT MEASURES: currently two marks bracket the real per-frame EndScene call
// (see the two GpuSyncMarkX64 call sites in overlay_hud.cpp's Hook_EndScene) --
// "frame-start" (before the real native EndScene work runs) and "frame-end" (after
// it returns, before Present). The logged interval between consecutive "frame-end"
// marks is therefore real, GPU-inclusive, wall-clock frame time -- directly
// comparable between a live-gameplay session and a paused-menu session by reading
// the log, no capture file, no external tool, no debugger. Real, honest scope note:
// this is intentionally coarse (whole-frame) for v1, not yet broken down by internal
// render phase (per-light shadows vs SSAO vs post-effect etc.) -- inserting
// additional marks at internal native-function boundaries (which this project's own
// signature-scanning infrastructure could reach, e.g. the already-hooked shadow-
// activation call site) is a real, concrete, cheap extension if whole-frame timing
// alone doesn't isolate the differential.

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <vulkan/vulkan_core.h>

extern void LogFromController(const char* msg); // dllmain.cpp
extern VkInstance GetDxvkVkInstanceX64();        // streamline_integration_x64.cpp
extern VkDevice GetDxvkVkDeviceX64();            // streamline_integration_x64.cpp
extern VkQueue GetDxvkVkQueueX64();              // streamline_integration_x64.cpp
extern "C" float GetDvarFloatX64_Exported(const char* name); // analog_input_hooks_x64.cpp -- real
    // cl_paused read, same accessor overlay_hud.cpp's own [x64-renderview-rate] line already uses,
    // so every logged interval here is reliably labeled too (see that line's own 2026-09-26 fix --
    // this project had no trustworthy pause-state signal before that).

namespace {

using PFN_vkGetInstanceProcAddr_local2 = PFN_vkVoidFunction(VKAPI_PTR*)(VkInstance, const char*);
using PFN_vkGetDeviceProcAddr_local2 = PFN_vkVoidFunction(VKAPI_PTR*)(VkDevice, const char*);

PFN_vkQueueWaitIdle g_vkQueueWaitIdle = nullptr;
bool g_resolveAttempted = false;
bool g_resolveFailed = false;

// Same resolution pattern already proven in streamline_resources_x64.cpp -- the real
// Vulkan loader (vulkan-1.dll or winevulkan.dll) is already loaded in-process by DXVK
// itself, so this only ever does a GetModuleHandleA (never LoadLibraryA a new module).
bool ResolveVulkanWaitIdleIfNeeded()
{
    if (g_vkQueueWaitIdle) return true;
    if (g_resolveFailed) return false;
    g_resolveAttempted = true;

    VkInstance instance = GetDxvkVkInstanceX64();
    VkDevice device = GetDxvkVkDeviceX64();
    if (instance == VK_NULL_HANDLE || device == VK_NULL_HANDLE) return false; // not resolved yet this session -- not a failure, just early

    HMODULE vulkanModule = GetModuleHandleA("vulkan-1.dll");
    if (!vulkanModule) vulkanModule = GetModuleHandleA("winevulkan.dll");
    if (!vulkanModule) {
        LogFromController("[gpu-sync-mark] Could not find the real Vulkan loader module already "
            "in-process -- GpuSyncTimingLogging will not work this session.");
        g_resolveFailed = true;
        return false;
    }

    // REAL BUG, FOUND 2026-09-26 (live log showed "vkGetDeviceProcAddr resolve FAILED" --
    // the tool silently never fired all session despite the config being on): the original
    // code here called vkGetInstanceProcAddr(NULL, "vkGetDeviceProcAddr"), assuming a NULL
    // instance is valid for that query. Per the real Vulkan spec, a NULL instance is ONLY
    // valid for a small fixed set of global functions (vkEnumerateInstanceExtensionProperties/
    // LayerProperties/Version, vkCreateInstance) -- vkGetDeviceProcAddr is NOT one of them,
    // and DXVK's own ICD correctly returns nullptr for it rather than tolerating the misuse.
    // Fixed by passing the REAL, already-resolved VkInstance (GetDxvkVkInstanceX64(),
    // cached the same way GetDxvkVkDeviceX64()/GetDxvkVkQueueX64() already are) instead of NULL.
    auto getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr_local2>(
        GetProcAddress(vulkanModule, "vkGetInstanceProcAddr"));
    if (!getInstanceProcAddr) {
        LogFromController("[gpu-sync-mark] vkGetInstanceProcAddr export not found -- "
            "GpuSyncTimingLogging will not work this session.");
        g_resolveFailed = true;
        return false;
    }
    auto getDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr_local2>(
        getInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
    if (!getDeviceProcAddr) {
        LogFromController("[gpu-sync-mark] vkGetDeviceProcAddr resolve FAILED -- "
            "GpuSyncTimingLogging will not work this session.");
        g_resolveFailed = true;
        return false;
    }

    g_vkQueueWaitIdle = reinterpret_cast<PFN_vkQueueWaitIdle>(getDeviceProcAddr(device, "vkQueueWaitIdle"));
    if (!g_vkQueueWaitIdle) {
        LogFromController("[gpu-sync-mark] vkQueueWaitIdle resolve FAILED -- "
            "GpuSyncTimingLogging will not work this session.");
        g_resolveFailed = true;
        return false;
    }

    LogFromController("[gpu-sync-mark] Real vkQueueWaitIdle resolved -- GpuSyncTimingLogging active. "
        "REAL OVERHEAD WARNING: this forces a hardware GPU sync every frame, destroying normal "
        "CPU/GPU pipelining -- expect a real, substantial fps drop while this is enabled.");
    return true;
}

LARGE_INTEGER g_perfFreq{};
LARGE_INTEGER g_lastMarkTime{};
char g_lastMarkLabel[32] = "none";
bool g_haveLastMark = false;
long long g_markCount = 0;

} // namespace

void GpuSyncMarkX64(const char* label)
{
    if (!ResolveVulkanWaitIdleIfNeeded()) return;

    VkQueue queue = GetDxvkVkQueueX64();
    if (queue == VK_NULL_HANDLE) return;

    if (g_perfFreq.QuadPart == 0) QueryPerformanceFrequency(&g_perfFreq);

    // The real, deliberate sync point -- blocks THIS thread until every unit of GPU
    // work already submitted on this queue has genuinely finished executing.
    g_vkQueueWaitIdle(queue);

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);

    ++g_markCount;
    if (g_haveLastMark) {
        double elapsedMs = (static_cast<double>(now.QuadPart - g_lastMarkTime.QuadPart) * 1000.0) /
            static_cast<double>(g_perfFreq.QuadPart);
        if (g_markCount <= 20 || (g_markCount % 100) == 0) {
            int clPaused = static_cast<int>(GetDvarFloatX64_Exported("cl_paused"));
            char buf[220];
            sprintf_s(buf, "[gpu-sync-mark] %s->%s elapsedMs=%.3f clPaused=%d (GPU-inclusive, real hw sync)",
                g_lastMarkLabel, label, elapsedMs, clPaused);
            LogFromController(buf);
        }
    }

    strncpy_s(g_lastMarkLabel, label, sizeof(g_lastMarkLabel) - 1);
    g_lastMarkTime = now;
    g_haveLastMark = true;
}
