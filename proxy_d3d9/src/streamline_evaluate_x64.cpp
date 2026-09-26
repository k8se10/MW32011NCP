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

    uint32_t outputWidth = static_cast<uint32_t>(nativeW);
    uint32_t outputHeight = static_cast<uint32_t>(nativeH);
    int mode = g_modConfig.dlssModeX64;

    if (outputWidth != g_lastOptionsWidth || outputHeight != g_lastOptionsHeight || mode != g_lastOptionsMode) {
        bool ok = StreamlineDLSSSetOptionsX64(outputWidth, outputHeight, mode);
        g_lastOptionsWidth = outputWidth;
        g_lastOptionsHeight = outputHeight;
        g_lastOptionsMode = mode;
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
    g_vkWaitForFences(vkDevice, 1, &g_fence, VK_TRUE, UINT64_MAX);
    g_vkResetFences(vkDevice, 1, &g_fence);
}
