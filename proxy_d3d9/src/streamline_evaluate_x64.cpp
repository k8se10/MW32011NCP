// MW32011NCP, 2026-09-26: the real slEvaluateFeature call -- the piece that
// actually makes DLSS run, closing out the integration chain built across
// prior sessions (slInit -> slSetVulkanInfo -> resource tagging -> constants
// -> HERE). Everything up to this file only prepared data; this file is the
// first thing that spends real GPU compute on DLSS itself.
//
// Real per-frame ordering this depends on (see overlay_hud.cpp's
// Hook_EndScene for the actual call site): slEvaluateFeature's own doc
// comment (sl_core_api.h) requires "frame and viewport must match whatever
// is used to set common and/or feature options and constants" -- i.e. this
// frame's own already-tagged resources/constants (all set earlier this same
// frame, using g_streamlineCurrentFrameToken) must be evaluated BEFORE
// StreamlineFrameTick() advances that token for the NEXT frame. Getting this
// order wrong (evaluating with a token that was already superseded) would
// silently evaluate against stale/mismatched tags -- a real, easy-to-miss
// correctness bug, not something that would necessarily crash or log an
// error. EvaluateStreamlineDlssX64() must therefore be called from
// Hook_EndScene BEFORE StreamlineFrameTick(), not after.
//
// Real Vulkan command-buffer plumbing: slEvaluateFeature(feature, frame,
// inputs, numInputs, cmdBuffer) needs a REAL VkCommandBuffer to record DLSS's
// own Vulkan compute-dispatch commands into (manual-hooking mode -- this
// project owns command-buffer lifecycle, DXVK doesn't do this for us the way
// it would in a DX9-native call). This file owns one small, dedicated
// command pool + a single reusable command buffer + a fence, created once
// against the real queue family Streamline itself registered
// (GetDxvkVkGraphicsQueueFamilyIndexX64) -- deliberately NOT DXVK's own
// internal command buffers/pools, which this project has no access to and
// shouldn't assume anything about.
//
// v1, intentionally coarse (matches this project's own "trivial passthrough
// first" convention, CLAUDE.md Production Ready Only): record, submit, and
// BLOCK on a fence wait for that one command buffer every single frame,
// rather than any double-buffering/overlap-with-next-frame scheme. Correct
// first; a real perf pass (if DLSS's own real GPU cost turns out to
// dominate) is a legitimate future step, not this one.

#include <windows.h>
#include <cstdio>
#include "dxvk_interop_x64.h" // real Vulkan SDK types -- must come BEFORE sl.h,
    // which uses Vulkan types without including a Vulkan header itself.
#include "../third_party/streamline/include/sl.h"
#include "mod_config.h" // g_modConfig.streamlineEnabled/dlssModeX64
#include "overlay_hud.h" // GetLastKnownRenderDevice/GetRealScreenSize

extern void LogFromController(const char* msg); // dllmain.cpp
extern "C" bool IsStreamlineInitializedX64(); // streamline_integration_x64.cpp
extern VkInstance GetDxvkVkInstanceX64(); // streamline_integration_x64.cpp
extern VkDevice GetDxvkVkDeviceX64(); // streamline_integration_x64.cpp
extern VkQueue GetDxvkVkQueueX64(); // streamline_integration_x64.cpp
extern uint32_t GetDxvkVkGraphicsQueueFamilyIndexX64(); // streamline_integration_x64.cpp
extern bool StreamlineDLSSSetOptionsX64(uint32_t outputWidth, uint32_t outputHeight, int modeValue); // streamline_integration_x64.cpp
extern bool StreamlineEvaluateFeatureX64(void* vkCommandBuffer); // streamline_integration_x64.cpp
extern bool GetStreamlineInternalRenderResolutionX64(uint32_t& outWidth, uint32_t& outHeight); // streamline_resources_x64.cpp,
    // the real render-scale-produced INPUT resolution DLSS reads from -- see this
    // file's own real-bug comment above EvaluateStreamlineDlssX64 for why this must
    // be checked against the output resolution before ever calling evaluate.

namespace {

// Real Vulkan functions this file needs, resolved dynamically -- same
// rationale as streamline_resources_x64.cpp's own top comment (no Vulkan
// import library linked in this project; every Vulkan function is resolved
// at runtime via GetProcAddress on the real loader DXVK already loaded
// in-process). This file owns its own copy of this small resolve step
// rather than sharing streamline_resources_x64.cpp's (a different, larger
// set of functions) -- matches this project's own established "each file
// owns its own copy of these fixed, never-changing lookups" convention.
using PFN_vkGetInstanceProcAddr_local = PFN_vkVoidFunction(VKAPI_PTR*)(VkInstance, const char*);
using PFN_vkGetDeviceProcAddr_local = PFN_vkVoidFunction(VKAPI_PTR*)(VkDevice, const char*);

PFN_vkCreateCommandPool g_vkCreateCommandPool = nullptr;
PFN_vkDestroyCommandPool g_vkDestroyCommandPool = nullptr;
PFN_vkAllocateCommandBuffers g_vkAllocateCommandBuffers = nullptr;
PFN_vkFreeCommandBuffers g_vkFreeCommandBuffers = nullptr;
PFN_vkResetCommandBuffer g_vkResetCommandBuffer = nullptr;
PFN_vkBeginCommandBuffer g_vkBeginCommandBuffer = nullptr;
PFN_vkEndCommandBuffer g_vkEndCommandBuffer = nullptr;
PFN_vkQueueSubmit g_vkQueueSubmit = nullptr;
PFN_vkCreateFence g_vkCreateFence = nullptr;
PFN_vkDestroyFence g_vkDestroyFence = nullptr;
PFN_vkWaitForFences g_vkWaitForFences = nullptr;
PFN_vkResetFences g_vkResetFences = nullptr;

bool g_vulkanFunctionsResolved = false;
bool g_vulkanFunctionsResolveFailed = false;

VkCommandPool g_commandPool = VK_NULL_HANDLE;
VkCommandBuffer g_commandBuffer = VK_NULL_HANDLE;
VkFence g_fence = VK_NULL_HANDLE;

// Tracks the last real (outputWidth, outputHeight, mode) StreamlineDLSSSetOptionsX64
// was actually called with -- re-calls it whenever any of the three changes (a real
// native-resolution change, or a live config hot-reload of DLSSModeX64), never on
// every single frame (slDLSSSetOptions's own doc comment: "turn DLSS on/off, change
// mode etc.", not a per-frame call).
// 2026-09-27: real design decision, direct instruction -- rather than just
// skip DLSS entirely whenever InternalRenderScalePercent produces an input
// larger than the display resolution, actually use DLSS for that case via
// DLSSMode::eDLAA (6) -- a same-resolution mode (input==output, no resize),
// whose own Min/Max dynamic-resolution range is trivially satisfied when
// output is set to match the render-scale input itself rather than the real
// display. DLAA still runs the real neural denoise/temporal-AA pass on the
// supersampled image -- this is the actual mechanism behind the "feed DLSS
// an already-supersampled image for a real quality/perf win" theory this
// session set out to test (vulkan_dlss_pipeline_research.md item 17, ROUND
// 5's note), not a workaround that avoids testing it.
// HONEST, CURRENTLY OPEN GAP: DLAA's own output at this larger resolution
// still needs a final downsample back to the real display resolution before
// it can be what the player actually sees -- that compositing step (and,
// separately, correctly sequencing it before HUD/UI draws so DLSS's
// 3D-scene-only output doesn't overwrite already-composited UI) is NOT yet
// built. This override makes slDLSSSetOptions/slEvaluateFeature succeed and
// do real GPU work at the correct resolution; it does not yet get that work
// in front of the player. See this file's own EvaluateStreamlineDlssX64
// comment and the research doc for the real remaining scope.
// REAL, DIRECT COST CAVEAT (user's own words, record precisely): "DLAA is
// EXPENSIVE" -- it is the real neural network running at the FULL
// supersampled resolution (e.g. 5120x2880 at 200% render scale), not a
// cheap fallback; this override trades one expensive thing (naive
// supersampling) for another expensive thing believed to have a better
// quality/perf ratio, not for something cheap.
// SESSION-ONLY, NEVER PERSISTED: direct instruction -- "it has to be changed
// to the compat one for that run not in config." g_modConfig.dlssModeX64
// itself is never modified by this override; only the value actually passed
// to Streamline (effective mode/resolution below) changes for as long as
// the incompatible ratio holds, reverting the instant render scale drops
// back to 100% or below. The .ini file is never rewritten by this logic.
int g_dlssEffectiveModeX64 = -1; // -1 = not yet computed this session
uint32_t g_dlssEffectiveOutputWidthX64 = 0;
uint32_t g_dlssEffectiveOutputHeightX64 = 0;
bool g_dlssAboveOutputOverrideActiveX64 = false; // edge-detected -- drives the
    // one-time-per-activation log/modal notice below, not a per-frame spam gate.

constexpr int kDlssModeDLAAX64 = 6; // sl::DLSSMode::eDLAA (sl_dlss.h) -- this
    // file deliberately doesn't include sl_dlss.h for one enum value (avoids
    // pulling its own real dependency chain into this translation unit for a
    // single constant); the real enum ordinal is fixed/stable per the vendored
    // SDK header (eOff=0..eDLAA=6) and cross-checked against it here.

uint32_t g_lastOptionsWidth = 0;
uint32_t g_lastOptionsHeight = 0;
int g_lastOptionsMode = -1;
bool g_optionsEverSucceeded = false;

bool ResolveVulkanFunctionsIfNeeded()
{
    if (g_vulkanFunctionsResolved) return true;
    if (g_vulkanFunctionsResolveFailed) return false;

    VkInstance instance = GetDxvkVkInstanceX64();
    VkDevice device = GetDxvkVkDeviceX64();
    if (instance == VK_NULL_HANDLE || device == VK_NULL_HANDLE) return false; // not registered yet, not a failure

    HMODULE vulkanModule = GetModuleHandleA("vulkan-1.dll");
    if (!vulkanModule) vulkanModule = GetModuleHandleA("winevulkan.dll");
    if (!vulkanModule) {
        LogFromController("[x64-streamline-evaluate] Could not find the real Vulkan loader module "
            "already in-process -- DLSS evaluation will not work this session.");
        g_vulkanFunctionsResolveFailed = true;
        return false;
    }

    auto getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr_local>(
        GetProcAddress(vulkanModule, "vkGetInstanceProcAddr"));
    if (!getInstanceProcAddr) {
        LogFromController("[x64-streamline-evaluate] vkGetInstanceProcAddr export not found -- "
            "DLSS evaluation will not work this session.");
        g_vulkanFunctionsResolveFailed = true;
        return false;
    }

    auto getDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr_local>(
        getInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
    if (!getDeviceProcAddr) {
        LogFromController("[x64-streamline-evaluate] vkGetDeviceProcAddr resolve FAILED -- "
            "DLSS evaluation will not work this session.");
        g_vulkanFunctionsResolveFailed = true;
        return false;
    }

    g_vkCreateCommandPool = reinterpret_cast<PFN_vkCreateCommandPool>(getDeviceProcAddr(device, "vkCreateCommandPool"));
    g_vkDestroyCommandPool = reinterpret_cast<PFN_vkDestroyCommandPool>(getDeviceProcAddr(device, "vkDestroyCommandPool"));
    g_vkAllocateCommandBuffers = reinterpret_cast<PFN_vkAllocateCommandBuffers>(getDeviceProcAddr(device, "vkAllocateCommandBuffers"));
    g_vkFreeCommandBuffers = reinterpret_cast<PFN_vkFreeCommandBuffers>(getDeviceProcAddr(device, "vkFreeCommandBuffers"));
    g_vkResetCommandBuffer = reinterpret_cast<PFN_vkResetCommandBuffer>(getDeviceProcAddr(device, "vkResetCommandBuffer"));
    g_vkBeginCommandBuffer = reinterpret_cast<PFN_vkBeginCommandBuffer>(getDeviceProcAddr(device, "vkBeginCommandBuffer"));
    g_vkEndCommandBuffer = reinterpret_cast<PFN_vkEndCommandBuffer>(getDeviceProcAddr(device, "vkEndCommandBuffer"));
    g_vkQueueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(getDeviceProcAddr(device, "vkQueueSubmit"));
    g_vkCreateFence = reinterpret_cast<PFN_vkCreateFence>(getDeviceProcAddr(device, "vkCreateFence"));
    g_vkDestroyFence = reinterpret_cast<PFN_vkDestroyFence>(getDeviceProcAddr(device, "vkDestroyFence"));
    g_vkWaitForFences = reinterpret_cast<PFN_vkWaitForFences>(getDeviceProcAddr(device, "vkWaitForFences"));
    g_vkResetFences = reinterpret_cast<PFN_vkResetFences>(getDeviceProcAddr(device, "vkResetFences"));

    if (!g_vkCreateCommandPool || !g_vkDestroyCommandPool || !g_vkAllocateCommandBuffers ||
        !g_vkFreeCommandBuffers || !g_vkResetCommandBuffer || !g_vkBeginCommandBuffer ||
        !g_vkEndCommandBuffer || !g_vkQueueSubmit || !g_vkCreateFence || !g_vkDestroyFence ||
        !g_vkWaitForFences || !g_vkResetFences) {
        LogFromController("[x64-streamline-evaluate] One or more required Vulkan command-buffer/"
            "fence functions failed to resolve -- DLSS evaluation will not work this session.");
        g_vulkanFunctionsResolveFailed = true;
        return false;
    }

    g_vulkanFunctionsResolved = true;
    LogFromController("[x64-streamline-evaluate] Real Vulkan command-buffer/fence functions resolved.");
    return true;
}

// Creates the one, small, dedicated command pool + command buffer + fence
// this file reuses every frame. Called once, lazily, the first time
// EvaluateStreamlineDlssX64 actually has everything it needs to proceed.
bool EnsureCommandResourcesX64()
{
    if (g_commandBuffer != VK_NULL_HANDLE && g_fence != VK_NULL_HANDLE) return true;

    VkDevice device = GetDxvkVkDeviceX64();
    if (device == VK_NULL_HANDLE) return false;

    if (g_commandPool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = GetDxvkVkGraphicsQueueFamilyIndexX64();
        VkResult vr = g_vkCreateCommandPool(device, &poolInfo, nullptr, &g_commandPool);
        if (vr != VK_SUCCESS) {
            char buf[150];
            sprintf_s(buf, "[x64-streamline-evaluate] vkCreateCommandPool FAILED (VkResult=%d).", static_cast<int>(vr));
            LogFromController(buf);
            g_commandPool = VK_NULL_HANDLE;
            return false;
        }
    }

    if (g_commandBuffer == VK_NULL_HANDLE) {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = g_commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        VkResult vr = g_vkAllocateCommandBuffers(device, &allocInfo, &g_commandBuffer);
        if (vr != VK_SUCCESS) {
            char buf[150];
            sprintf_s(buf, "[x64-streamline-evaluate] vkAllocateCommandBuffers FAILED (VkResult=%d).", static_cast<int>(vr));
            LogFromController(buf);
            g_commandBuffer = VK_NULL_HANDLE;
            return false;
        }
    }

    if (g_fence == VK_NULL_HANDLE) {
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkResult vr = g_vkCreateFence(device, &fenceInfo, nullptr, &g_fence);
        if (vr != VK_SUCCESS) {
            char buf[150];
            sprintf_s(buf, "[x64-streamline-evaluate] vkCreateFence FAILED (VkResult=%d).", static_cast<int>(vr));
            LogFromController(buf);
            g_fence = VK_NULL_HANDLE;
            return false;
        }
    }

    LogFromController("[x64-streamline-evaluate] Real command pool/command buffer/fence created.");
    return true;
}

} // namespace

// Called once per real frame from Hook_EndScene (overlay_hud.cpp), BEFORE
// StreamlineFrameTick() -- see this file's own top comment for why the
// ordering matters. Gated the same way every other Streamline-adjacent entry
// point in this codebase is (IsStreamlineInitializedX64), plus this file's
// own additional real prerequisite checks (Vulkan functions resolved,
// command resources created, DLSS options configured for the current real
// resolution).
void EvaluateStreamlineDlssX64()
{
    if (!IsStreamlineInitializedX64()) return;
    if (!ResolveVulkanFunctionsIfNeeded()) return;
    if (!EnsureCommandResourcesX64()) return;

    void* device = GetLastKnownRenderDevice();
    if (!device) return;
    int nativeW = 1920, nativeH = 1080;
    GetRealScreenSize(device, nativeW, nativeH);
    if (nativeW <= 0 || nativeH <= 0) return;

    uint32_t displayWidth = static_cast<uint32_t>(nativeW);
    uint32_t displayHeight = static_cast<uint32_t>(nativeH);
    int configuredMode = g_modConfig.dlssModeX64;

    // REAL FIX, 2026-09-27: live-confirmed, NGX hard-rejects any input (the
    // render-scale-produced buffer, kBufferTypeScalingInputColor) larger than
    // DLSS's own output resolution when using an upscale mode -- "Dynamic
    // scaling error... RenderSubrect (5120x2880) outside of Min (1280x720)
    // and Max (2560x1440) dynamic res" (real log capture at
    // InternalRenderScalePercent=200, 2x native). Rather than just skip DLSS
    // for that case, direct instruction: switch to DLSSMode::eDLAA (a
    // same-resolution mode, input==output, no resize) with output set to the
    // render-scale INPUT resolution itself -- DLAA's own range is then
    // trivially satisfied, and DLSS still does real, useful work (its neural
    // denoise/temporal-AA pass) on the supersampled image, which is the
    // actual mechanism behind testing whether feeding DLSS an already-
    // supersampled image is worthwhile. See this file's own top-of-file
    // comment on g_dlssEffectiveModeX64 for the full design and its current,
    // honest gaps (no final downsample-to-display compositing step exists
    // yet -- this makes the SDK calls succeed and do real work; it does not
    // yet get that work in front of the player).
    uint32_t inputWidth = 0, inputHeight = 0;
    GetStreamlineInternalRenderResolutionX64(inputWidth, inputHeight);
    bool aboveOutput = (inputWidth > displayWidth || inputHeight > displayHeight);

    int effectiveMode = configuredMode;
    uint32_t effectiveOutputWidth = displayWidth;
    uint32_t effectiveOutputHeight = displayHeight;
    if (aboveOutput) {
        effectiveMode = kDlssModeDLAAX64;
        effectiveOutputWidth = inputWidth;
        effectiveOutputHeight = inputHeight;
    }

    if (aboveOutput != g_dlssAboveOutputOverrideActiveX64) {
        g_dlssAboveOutputOverrideActiveX64 = aboveOutput;
        if (aboveOutput) {
            char buf[400];
            sprintf_s(buf, "[x64-streamline-evaluate] Render-scale input (%ux%u) exceeds the real "
                "display resolution (%ux%u) -- your configured DLSSModeX64=%d cannot run at this "
                "ratio (NGX hard-rejects any input larger than output for an upscale mode). "
                "SESSION-ONLY override to DLSSMode::eDLAA (6) active for as long as this ratio "
                "holds -- mw3ncp_config.ini's own DLSSModeX64 value is NOT changed.",
                inputWidth, inputHeight, displayWidth, displayHeight, configuredMode);
            LogFromController(buf);
            ShowOverlayMessageUntilDismissed(
                "\x03DLSS Mode Auto-Switched (This Session Only)\n\n"
                "\x01Render scale above 100% is incompatible with DLSS's upscale\n"
                "modes -- switched to DLAA for as long as this holds.\n\n"
                "DLAA is EXPENSIVE: it runs the full neural network at your\n"
                "entire supersampled resolution, not a cheap fallback.\n\n"
                "Your DLSSModeX64 config setting is unchanged.");
        } else {
            LogFromController("[x64-streamline-evaluate] Render-scale input no longer exceeds the "
                "display resolution -- DLAA override lifted, using the configured DLSSModeX64 again.");
        }
    }

    g_dlssEffectiveModeX64 = effectiveMode;
    g_dlssEffectiveOutputWidthX64 = effectiveOutputWidth;
    g_dlssEffectiveOutputHeightX64 = effectiveOutputHeight;

    if (effectiveOutputWidth != g_lastOptionsWidth || effectiveOutputHeight != g_lastOptionsHeight ||
        effectiveMode != g_lastOptionsMode) {
        bool ok = StreamlineDLSSSetOptionsX64(effectiveOutputWidth, effectiveOutputHeight, effectiveMode);
        g_lastOptionsWidth = effectiveOutputWidth;
        g_lastOptionsHeight = effectiveOutputHeight;
        g_lastOptionsMode = effectiveMode;
        g_optionsEverSucceeded = g_optionsEverSucceeded || ok;
    }
    if (!g_optionsEverSucceeded) return; // never configured successfully -- nothing to evaluate yet

    VkDevice vkDevice = GetDxvkVkDeviceX64();
    VkQueue vkQueue = GetDxvkVkQueueX64();
    if (vkDevice == VK_NULL_HANDLE || vkQueue == VK_NULL_HANDLE) return;

    g_vkResetCommandBuffer(g_commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkResult vr = g_vkBeginCommandBuffer(g_commandBuffer, &beginInfo);
    if (vr != VK_SUCCESS) {
        char buf[150];
        sprintf_s(buf, "[x64-streamline-evaluate] vkBeginCommandBuffer FAILED (VkResult=%d).", static_cast<int>(vr));
        LogFromController(buf);
        return;
    }

    bool evaluateOk = StreamlineEvaluateFeatureX64(reinterpret_cast<void*>(g_commandBuffer));

    vr = g_vkEndCommandBuffer(g_commandBuffer);
    if (vr != VK_SUCCESS) {
        char buf[150];
        sprintf_s(buf, "[x64-streamline-evaluate] vkEndCommandBuffer FAILED (VkResult=%d).", static_cast<int>(vr));
        LogFromController(buf);
        return;
    }

    if (!evaluateOk) return; // StreamlineEvaluateFeatureX64 already logged the real failure reason;
        // still submit nothing further this frame -- an empty/incomplete recording isn't worth running.

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &g_commandBuffer;

    vr = g_vkQueueSubmit(vkQueue, 1, &submitInfo, g_fence);
    if (vr != VK_SUCCESS) {
        char buf[150];
        sprintf_s(buf, "[x64-streamline-evaluate] vkQueueSubmit FAILED (VkResult=%d).", static_cast<int>(vr));
        LogFromController(buf);
        return;
    }

    // v1, intentionally coarse (this file's own top comment) -- block until
    // DLSS's own recorded work actually finishes before returning, rather
    // than any overlap-with-next-frame scheme.
    // REAL BUG FIX, 2026-09-27: this used to wait UINT64_MAX (forever) --
    // live-reported as a real hang requiring the process to be killed, no
    // crash dump generated (confirmed by the user: "was a hang and i kill
    // not a dump crash"). The real trigger was almost certainly the
    // above-output-resolution NGX rejection this file now guards against
    // entirely (a rejected slEvaluateFeature call may still leave Streamline's
    // own internal state, or this project's own recorded command buffer,
    // inconsistent for a LATER frame's submission -- the log showed several
    // fast ~4-5ms evaluate calls immediately after the first NGX failure,
    // meaning the hang didn't happen on the failing call itself, but some
    // frames later). Regardless of the deeper cause, an infinite wait on a
    // GPU fence is never acceptable -- a real timeout here is the correct
    // fix on its own merits, independent of whether the guard above turns
    // out to be the complete fix. 2 real seconds is generous for a single
    // frame's DLSS work; a genuine timeout is loud/logged, not silent.
    constexpr uint64_t kFenceTimeoutNs = 2'000'000'000ULL;
    VkResult waitResult = g_vkWaitForFences(vkDevice, 1, &g_fence, VK_TRUE, kFenceTimeoutNs);
    if (waitResult == VK_TIMEOUT) {
        static long long s_timeoutCount = 0;
        ++s_timeoutCount;
        char buf[200];
        sprintf_s(buf, "[x64-streamline-evaluate] vkWaitForFences TIMED OUT after 2s (count=%lld) -- "
            "DLSS's own recorded GPU work never signaled completion. Skipping this frame's result "
            "rather than waiting indefinitely.", s_timeoutCount);
        LogFromController(buf);
        // Do NOT reset the fence here -- it may still signal later; resetting
        // a fence the driver hasn't actually finished with is undefined per
        // the Vulkan spec. Leave it and let the NEXT frame's vkResetCommandBuffer/
        // vkBeginCommandBuffer attempt naturally re-synchronize (a real,
        // accepted limitation of this v1, single-buffered design -- a second,
        // genuinely stuck fence would need a full command-pool/fence recreate,
        // not attempted here since the guard above should prevent the real
        // trigger from recurring).
        return;
    }
    g_vkResetFences(vkDevice, 1, &g_fence);
}

// 2026-09-27: public accessor for streamline_resources_x64.cpp -- the real
// DLSS OUTPUT texture (kBufferTypeScalingOutputColor) must be sized to match
// whatever resolution Streamline was actually told is the output this frame,
// which is the render-scale INPUT resolution during the above-output/DLAA
// override, not always the real display resolution. Returns false (caller
// should fall back to the real display resolution) until this function has
// computed at least one real value, i.e. before EvaluateStreamlineDlssX64's
// own first real call this session.
bool GetDlssEffectiveOutputResolutionX64(uint32_t& outWidth, uint32_t& outHeight)
{
    if (g_dlssEffectiveOutputWidthX64 == 0 || g_dlssEffectiveOutputHeightX64 == 0) return false;
    outWidth = g_dlssEffectiveOutputWidthX64;
    outHeight = g_dlssEffectiveOutputHeightX64;
    return true;
}
