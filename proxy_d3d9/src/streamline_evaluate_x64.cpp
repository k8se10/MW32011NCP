// MW32011NCP, 2026-09-26: the real slEvaluateFeature call -- the piece that
// actually makes DLSS run, closing out the integration chain built across
// prior sessions (slInit -> slSetVulkanInfo -> resource tagging -> constants
// -> HERE). Everything up to this file only prepared data; this file is the
// first thing that spends real GPU compute on DLSS itself.
//
// Real per-frame ordering this depends on: slEvaluateFeature's own doc
// comment (sl_core_api.h) requires "frame and viewport must match whatever
// is used to set common and/or feature options and constants" -- i.e. this
// frame's own already-tagged resources/constants (all set earlier this same
// frame, using g_streamlineCurrentFrameToken) must be evaluated BEFORE
// StreamlineFrameTick() advances that token for the NEXT frame. Getting this
// order wrong (evaluating with a token that was already superseded) would
// silently evaluate against stale/mismatched tags -- a real, easy-to-miss
// correctness bug, not something that would necessarily crash or log an
// error.
//
// REVISED, 2026-09-27 -- moved from Hook_EndScene to the SAME earlier
// per-viewport boundary motion blur/FSR already use (FUN_14018def0 on x64,
// "the real boundary before the engine's per-viewport 2D/HUD command-
// dispatch loop" per analog_input_hooks.cpp's own x86 discovery comment),
// via TriggerMotionBlurFromEngineHook -> RunDlssEvaluateAndCompositeX64
// (this file). Real reason: DLSS's own color-input tag is deliberately the
// 3D-scene-only render target (TagColorResourceForFrame's own resolution-
// match disambiguation), so its output only ever represents the enhanced 3D
// scene, with no HUD/UI drawn on it. Evaluating from Hook_EndScene (which
// fires AFTER the frame's own 2D/UI compositing is already done, per this
// project's own DrawFullScreenPass comment) would mean any attempt to
// composite DLSS's output back over the frame at that point overwrites the
// HUD entirely, not enhances the 3D scene under it. Evaluating (and
// compositing) at this earlier boundary instead means the composite lands
// BEFORE HUD/UI draws, so the game's own subsequent HUD dispatch draws
// correctly on top of DLSS's now-enhanced backbuffer -- this closes the
// "still explicitly open" compositing gap flagged in every prior round of
// this item. StreamlineFrameTick() (advancing the token for the NEXT frame)
// stays at Hook_EndScene, still last in the frame -- see that call site's
// own comment.
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
#include <cstring> // memcpy -- readback diagnostic decoding
#include <cstdint> // UINT32_MAX -- readback diagnostic memory-type search
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
extern bool StreamlineAllocateResourcesX64(sl::Feature feature); // streamline_integration_x64.cpp, 2026-09-27
extern bool StreamlineEvaluateFeatureX64(void* vkCommandBuffer); // streamline_integration_x64.cpp
extern bool GetStreamlineInternalRenderResolutionX64(uint32_t& outWidth, uint32_t& outHeight); // streamline_resources_x64.cpp,
    // the real render-scale-produced INPUT resolution DLSS reads from -- see this
    // file's own real-bug comment above EvaluateStreamlineDlssX64 for why this must
    // be checked against the output resolution before ever calling evaluate.
extern void TagStreamlineOutputColorX64(); // streamline_resources_x64.cpp -- must run
    // before evaluate, now that both are called from the same earlier per-frame point.
extern void TagStreamlineMotionVectorsX64(); // streamline_resources_x64.cpp, same reason.
extern bool GetDlssOutputTextureSizeX64(uint32_t& outWidth, uint32_t& outHeight); // streamline_resources_x64.cpp, 2026-09-27
extern bool ResolveMsaaSceneColorForDlssX64(void* device); // streamline_resources_x64.cpp, 2026-09-27 (ROUND 23)
extern void UpdateStreamlineCameraFromEngineViewX64(void* engineState); // streamline_camera_x64.cpp, 2026-09-27 (ROUND 24)
extern "C" void* GetEngineMainCmdBufStateX64(); // analog_input_hooks_x64.cpp, 2026-09-27 (ROUND 24)
extern void GetDlssTaggedImagesX64(DlssTaggedImageX64& inputColor, DlssTaggedImageX64& outputColor); // streamline_resources_x64.cpp, 2026-09-27
extern VkPhysicalDevice GetDxvkVkPhysicalDeviceX64(); // streamline_integration_x64.cpp, 2026-09-27
extern void* GetDlssOutputSurfaceX64(); // streamline_resources_x64.cpp, 2026-09-27 -- the
    // real IDirect3DSurface9* this file's own final composite step reads from.
extern "C" bool IsMenuActiveX64_Exported(); // analog_input_hooks_x64.cpp -- same real
    // menu-active gate motion blur/FSR already require (issue #96/#97/#103/#104's own
    // "do NOT ship with fewer than x86's own three gates" standing rule) -- a stale
    // DLSS output from the last real gameplay frame must never get composited over a
    // menu/loading screen.
extern "C" bool TryGetInLevelFlagX64(int* outValue); // analog_input_hooks_x64.cpp
extern "C" bool TryGetClcStateX64(int* outValue); // analog_input_hooks_x64.cpp
extern bool IsStreamlineCameraStableX64(); // streamline_camera_x64.cpp, 2026-09-27 -- real
    // post-reset stabilization-window gate (kStreamlineStabilizationTicksX64 real tracked
    // frames since the last teleport/first-frame reset). Added after live evidence the
    // evaluate gameplay-gate above can open on a frame that is still mid-camera-transition
    // (map-dependent: Dome hung faster, mid-transition; Underground reached first-person
    // first) -- the single-frame teleport-reset flag alone doesn't cover that race.
extern bool RetagStreamlineInputsAfterFlushX64(); // streamline_resources_x64.cpp,
    // 2026-09-27 ROUND 19 -- re-resolves all four DLSS buffers after DXVK's flush.
extern void ClearStreamlineFrameSurfacesX64(); // streamline_resources_x64.cpp, same round.
extern bool HasStreamlineCameraDataX64(); // streamline_camera_x64.cpp -- true once the
    // real per-frame camera path has validly set sl::Constants at least once. 2026-09-27
    // ROUND 17 correction: an earlier version of the menu warm-up below tried to build its
    // OWN degenerate identity Constants, on the wrong assumption that no real camera exists
    // at any menu screen -- live-disproven the same day: the "menu" this fired against still
    // had a real, live camera (this game's pause menu keeps rendering the loaded level behind
    // it), so the real per-frame path had ALREADY called slSetConstants for that tick before
    // the warm-up's own separate call ran, and Streamline correctly rejected the duplicate
    // ("Setting different 'common' constants multiple times within the same frame is NOT
    // allowed!") -- the warm-up aborted before ever reaching evaluate, never actually testing
    // anything. Fixed by dropping the separate identity-Constants path entirely and reusing
    // whatever the real per-frame path already set this tick instead -- see this accessor's
    // use in RunDlssMainMenuWarmupOnceX64 below. (A plain `extern bool g_have...` for the
    // underlying global does NOT link -- it has internal linkage inside an anonymous
    // namespace in that file; this real accessor is the correct fix, same pattern
    // IsStreamlineCameraStableX64 already uses.)

namespace {

// REAL BUG FIX, 2026-09-27: live-reported device loss + hang (AppHangB1,
// confirmed via Windows Error Reporting -- no crash dump, since a hang
// produces none). The actual log evidence: `vkQueueSubmit FAILED
// (VkResult=-4)` -- VK_ERROR_DEVICE_LOST, a real GPU/driver-level fault
// (most likely triggered by DLAA processing a full render-scale-resolution
// buffer, e.g. 5120x2880 at 200% render scale -- a genuinely unusual,
// heavy real-time neural-network workload, not a common/well-tested
// Streamline/NGX usage pattern). Once a Vulkan device is lost, every
// subsequent call on it is undefined -- this project's own code used to
// just retry every single frame regardless, hammering an already-dead
// device forever, which is exactly the shape of thing that turns one real
// GPU fault into an unrecoverable hang. This latch permanently disables
// further DLSS evaluate/composite attempts for the rest of the session the
// instant device loss is detected -- there is no real recovery path here
// (that would need full device/swapchain recreation, which this project
// does not attempt), so the honest, safe response is to stop touching this
// device entirely, not keep trying.
bool g_dlssDeviceLostX64 = false;

// REAL FIX, 2026-09-27: real, decisive root cause of the VK_ERROR_DEVICE_LOST
// fault found via live testing (see g_dlssDeviceLostX64's own comment) --
// this file's own raw vkQueueSubmit calls were submitting directly to the
// SAME VkQueue DXVK uses internally, with ZERO synchronization against
// DXVK's own internal submission thread. Concurrent vkQueueSubmit calls on
// one VkQueue from different threads with no external synchronization is
// undefined behavior per the Vulkan spec itself. DXVK's own real interop
// interface (dxvk_interop_x64.h, ID3D9VkInteropDeviceX64, extended this
// session to include the real FlushRenderingCommands/LockSubmissionQueue/
// ReleaseSubmissionQueue methods, per DXVK's own doc comments word for
// word) exists specifically for this. This RAII guard QueryInterfaces the
// real interop device, calls FlushRenderingCommands() (DXVK's own doc:
// "Must be called before submitting Vulkan commands to the rendering queue
// if those commands use the backing resource of a D3D9 object" -- every
// resource this pipeline touches is one) then LockSubmissionQueue() on
// construction, and ReleaseSubmissionQueue()+Release() on destruction --
// guaranteeing the lock is always released on every return path (including
// the several early-return failure paths already in EvaluateStreamlineDlssX64),
// without needing to touch each one individually. DXVK's own doc comment on
// LockSubmissionQueue is explicit: "While the submission queue is locked,
// no D3D9 methods must be called from the locking thread, or otherwise a
// deadlock might occur" -- this project's own composite StretchRect call
// (a real D3D9 method) must therefore only ever run AFTER this guard's
// destructor has already fired, i.e. after EvaluateStreamlineDlssX64 itself
// has returned, which is exactly how RunDlssEvaluateAndCompositeX64 already
// calls it (evaluate first, composite only after evaluate returns).
class ScopedDxvkSubmissionLockX64 {
public:
    explicit ScopedDxvkSubmissionLockX64(void* d3d9Device)
    {
        if (!d3d9Device) return;
        HRESULT hr = reinterpret_cast<IUnknown*>(d3d9Device)->QueryInterface(
            IID_ID3D9VkInteropDevice_X64, reinterpret_cast<void**>(&m_device));
        if (FAILED(hr) || !m_device) {
            m_device = nullptr;
            return;
        }
        m_device->FlushRenderingCommands();
    }
    ~ScopedDxvkSubmissionLockX64()
    {
        if (m_locked) m_device->ReleaseSubmissionQueue();
        if (m_device) m_device->Release();
    }
    ScopedDxvkSubmissionLockX64(const ScopedDxvkSubmissionLockX64&) = delete;
    ScopedDxvkSubmissionLockX64& operator=(const ScopedDxvkSubmissionLockX64&) = delete;
    // REVISED, 2026-09-27 (ROUND 19): flush (constructor) and lock (Lock())
    // are now two steps, so RetagStreamlineInputsAfterFlushX64 can run
    // between them -- after the flush, so it sees every relocation that flush
    // recorded; before the lock, since no D3D9/interop call may run on this
    // thread while the queue is locked (DXVK's own LockSubmissionQueue doc).
    bool IsValid() const { return m_device != nullptr; } // flushed, not yet locked
    void Lock()
    {
        if (!m_device || m_locked) return;
        m_device->LockSubmissionQueue();
        m_locked = true;
    }

private:
    ID3D9VkInteropDeviceX64* m_device = nullptr;
    bool m_locked = false;
};

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
PFN_vkCmdPipelineBarrier g_vkCmdPipelineBarrier = nullptr; // 2026-09-27, ROUND 19

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
    g_vkCmdPipelineBarrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(getDeviceProcAddr(device, "vkCmdPipelineBarrier"));

    if (!g_vkCreateCommandPool || !g_vkDestroyCommandPool || !g_vkAllocateCommandBuffers ||
        !g_vkFreeCommandBuffers || !g_vkResetCommandBuffer || !g_vkBeginCommandBuffer ||
        !g_vkEndCommandBuffer || !g_vkQueueSubmit || !g_vkCreateFence || !g_vkDestroyFence ||
        !g_vkWaitForFences || !g_vkResetFences || !g_vkCmdPipelineBarrier) {
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
// REVISED, 2026-09-27: now returns bool -- true only on a genuine, complete,
// fence-signaled success this call, false on every early-out (including the
// real 2s timeout below). RunDlssEvaluateAndCompositeX64 (this file) needs
// this to know whether it's safe to composite g_outputColorTexture's content
// this frame, or whether it holds stale/no data.
// ---------------------------------------------------------------------------
// 2026-09-27: read-only DLSS pixel readback diagnostic.
//
// Live report: at InternalRenderScalePercent > 100 (the DLAA override) the
// gameplay viewport is black while every evaluate/composite call logs
// success; pausing (which skips the composite) shows the real scene. Three
// different faults produce exactly that picture, and logging API results
// cannot tell them apart:
//   1. the surface tagged as DLSS's scene-color input is black or the wrong
//      target (above 100% every scene-sized target shares the tagged size,
//      so the "last bound target of that size" choice is ambiguous);
//   2. DLSS/DLAA writes black into its output;
//   3. the output is fine and the composite StretchRect is what fails.
// This copies a 4x4 block from the centre of the tagged input and output
// images into a host-visible buffer, inside the SAME command buffer as the
// evaluate (after its trailing barrier), and logs both after the fence wait.
// It changes nothing that is drawn: images are only transitioned to
// TRANSFER_SRC_OPTIMAL when their tagged layout can't be copied from
// directly, and are always transitioned straight back. Runs for the first 3
// evaluates after any change of DLSS mode/output size, then every 600th.
// Any setup failure disables only the diagnostic, never DLSS.
// ---------------------------------------------------------------------------
namespace {

PFN_vkCreateBuffer g_rbCreateBuffer = nullptr;
PFN_vkGetBufferMemoryRequirements g_rbGetBufferMemoryRequirements = nullptr;
PFN_vkAllocateMemory g_rbAllocateMemory = nullptr;
PFN_vkBindBufferMemory g_rbBindBufferMemory = nullptr;
PFN_vkMapMemory g_rbMapMemory = nullptr;
PFN_vkCmdCopyImageToBuffer g_rbCmdCopyImageToBuffer = nullptr;

VkBuffer g_rbBuffer = VK_NULL_HANDLE;
VkDeviceMemory g_rbMemory = VK_NULL_HANDLE;
void* g_rbMapped = nullptr;
bool g_rbSetupFailed = false;

constexpr VkDeviceSize kRbBufferSize = 4096;
constexpr VkDeviceSize kRbInputOffset = 0;
constexpr VkDeviceSize kRbOutputOffset = 1024;
constexpr uint32_t kRbBlock = 4; // 4x4 texels

long long g_rbEvaluateCount = 0;
int g_rbForcedRemaining = 3;
int g_rbLastMode = -1;
uint32_t g_rbLastWidth = 0, g_rbLastHeight = 0;

// Per-frame record of what was copied, consumed after the fence wait.
struct RbCopyRecord {
    bool valid = false;
    DlssTaggedImageX64 img;
    uint32_t texelSize = 0;
    VkDeviceSize offset = 0;
};
RbCopyRecord g_rbInput, g_rbOutput;
bool g_rbPendingThisFrame = false;

uint32_t RbTexelSize(VkFormat f)
{
    switch (f) {
    case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM: case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
        return 4;
    case VK_FORMAT_R16G16B16A16_SFLOAT: case VK_FORMAT_R16G16B16A16_UNORM:
        return 8;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return 16;
    default:
        return 0;
    }
}

float RbHalfToFloat(uint16_t h)
{
    uint32_t sign = (h >> 15) & 1u, exp = (h >> 10) & 0x1Fu, mant = h & 0x3FFu;
    float v;
    if (exp == 0) v = static_cast<float>(mant) * (1.0f / 16777216.0f); // 2^-24
    else if (exp == 31) v = 65504.0f; // inf/nan: clamp for logging
    else {
        uint32_t bits = ((exp + 112u) << 23) | (mant << 13);
        memcpy(&v, &bits, sizeof(v));
    }
    return sign ? -v : v;
}

// Average RGBA of the copied block, in 0..1 (or HDR range for float formats).
// Returns false for formats this decoder doesn't handle (raw hex is logged instead).
bool RbDecodeAverage(const uint8_t* data, VkFormat f, uint32_t count, float rgba[4])
{
    rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0.0f;
    for (uint32_t i = 0; i < count; ++i) {
        float r = 0, g = 0, b = 0, a = 0;
        switch (f) {
        case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SRGB: {
            const uint8_t* p = data + i * 4;
            r = p[0] / 255.0f; g = p[1] / 255.0f; b = p[2] / 255.0f; a = p[3] / 255.0f; break; }
        case VK_FORMAT_B8G8R8A8_UNORM: case VK_FORMAT_B8G8R8A8_SRGB: {
            const uint8_t* p = data + i * 4;
            b = p[0] / 255.0f; g = p[1] / 255.0f; r = p[2] / 255.0f; a = p[3] / 255.0f; break; }
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32: {
            uint32_t v; memcpy(&v, data + i * 4, 4);
            r = (v & 0x3FF) / 1023.0f; g = ((v >> 10) & 0x3FF) / 1023.0f; b = ((v >> 20) & 0x3FF) / 1023.0f;
            a = (v >> 30) / 3.0f; break; }
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32: {
            uint32_t v; memcpy(&v, data + i * 4, 4);
            b = (v & 0x3FF) / 1023.0f; g = ((v >> 10) & 0x3FF) / 1023.0f; r = ((v >> 20) & 0x3FF) / 1023.0f;
            a = (v >> 30) / 3.0f; break; }
        case VK_FORMAT_R16G16B16A16_SFLOAT: {
            uint16_t h[4]; memcpy(h, data + i * 8, 8);
            r = RbHalfToFloat(h[0]); g = RbHalfToFloat(h[1]); b = RbHalfToFloat(h[2]); a = RbHalfToFloat(h[3]); break; }
        case VK_FORMAT_R16G16B16A16_UNORM: {
            uint16_t h[4]; memcpy(h, data + i * 8, 8);
            r = h[0] / 65535.0f; g = h[1] / 65535.0f; b = h[2] / 65535.0f; a = h[3] / 65535.0f; break; }
        case VK_FORMAT_R32G32B32A32_SFLOAT: {
            float v[4]; memcpy(v, data + i * 16, 16);
            r = v[0]; g = v[1]; b = v[2]; a = v[3]; break; }
        default:
            return false;
        }
        rgba[0] += r; rgba[1] += g; rgba[2] += b; rgba[3] += a;
    }
    for (int c = 0; c < 4; ++c) {
        rgba[c] /= static_cast<float>(count);
        // Clamp for printing: a garbage float would otherwise print dozens of
        // digits through %.3f and overflow the caller's sprintf_s buffer
        // (fatal -- this project's standing sprintf_s lesson). NaN -> -1.
        if (!(rgba[c] == rgba[c])) rgba[c] = -1.0f;
        else if (rgba[c] > 9999.0f) rgba[c] = 9999.0f;
        else if (rgba[c] < -9999.0f) rgba[c] = -9999.0f;
    }
    return true;
}

bool EnsureReadbackResourcesX64()
{
    if (g_rbMapped) return true;
    if (g_rbSetupFailed) return false;

    VkInstance instance = GetDxvkVkInstanceX64();
    VkDevice device = GetDxvkVkDeviceX64();
    VkPhysicalDevice phys = GetDxvkVkPhysicalDeviceX64();
    if (instance == VK_NULL_HANDLE || device == VK_NULL_HANDLE || phys == VK_NULL_HANDLE) return false;

    HMODULE vulkanModule = GetModuleHandleA("vulkan-1.dll");
    if (!vulkanModule) vulkanModule = GetModuleHandleA("winevulkan.dll");
    auto getInstanceProcAddr = vulkanModule ? reinterpret_cast<PFN_vkGetInstanceProcAddr_local>(
        GetProcAddress(vulkanModule, "vkGetInstanceProcAddr")) : nullptr;
    auto getDeviceProcAddr = getInstanceProcAddr ? reinterpret_cast<PFN_vkGetDeviceProcAddr_local>(
        getInstanceProcAddr(instance, "vkGetDeviceProcAddr")) : nullptr;
    auto getMemProps = getInstanceProcAddr ? reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
        getInstanceProcAddr(instance, "vkGetPhysicalDeviceMemoryProperties")) : nullptr;
    if (!getDeviceProcAddr || !getMemProps) {
        LogFromController("[x64-streamline-readback] Vulkan loader entry points unavailable -- readback "
            "diagnostic disabled (DLSS unaffected).");
        g_rbSetupFailed = true;
        return false;
    }
    g_rbCreateBuffer = reinterpret_cast<PFN_vkCreateBuffer>(getDeviceProcAddr(device, "vkCreateBuffer"));
    g_rbGetBufferMemoryRequirements = reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(
        getDeviceProcAddr(device, "vkGetBufferMemoryRequirements"));
    g_rbAllocateMemory = reinterpret_cast<PFN_vkAllocateMemory>(getDeviceProcAddr(device, "vkAllocateMemory"));
    g_rbBindBufferMemory = reinterpret_cast<PFN_vkBindBufferMemory>(getDeviceProcAddr(device, "vkBindBufferMemory"));
    g_rbMapMemory = reinterpret_cast<PFN_vkMapMemory>(getDeviceProcAddr(device, "vkMapMemory"));
    g_rbCmdCopyImageToBuffer = reinterpret_cast<PFN_vkCmdCopyImageToBuffer>(
        getDeviceProcAddr(device, "vkCmdCopyImageToBuffer"));
    if (!g_rbCreateBuffer || !g_rbGetBufferMemoryRequirements || !g_rbAllocateMemory ||
        !g_rbBindBufferMemory || !g_rbMapMemory || !g_rbCmdCopyImageToBuffer) {
        LogFromController("[x64-streamline-readback] Buffer/memory functions failed to resolve -- readback "
            "diagnostic disabled (DLSS unaffected).");
        g_rbSetupFailed = true;
        return false;
    }

    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = kRbBufferSize;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (g_rbCreateBuffer(device, &bi, nullptr, &g_rbBuffer) != VK_SUCCESS) {
        LogFromController("[x64-streamline-readback] vkCreateBuffer failed -- readback diagnostic disabled.");
        g_rbBuffer = VK_NULL_HANDLE;
        g_rbSetupFailed = true;
        return false;
    }
    VkMemoryRequirements req{};
    g_rbGetBufferMemoryRequirements(device, g_rbBuffer, &req);
    VkPhysicalDeviceMemoryProperties mp{};
    getMemProps(phys, &mp);
    const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t typeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((req.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) {
            typeIndex = i;
            break;
        }
    }
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = typeIndex;
    if (typeIndex == UINT32_MAX || g_rbAllocateMemory(device, &ai, nullptr, &g_rbMemory) != VK_SUCCESS ||
        g_rbBindBufferMemory(device, g_rbBuffer, g_rbMemory, 0) != VK_SUCCESS ||
        g_rbMapMemory(device, g_rbMemory, 0, VK_WHOLE_SIZE, 0, &g_rbMapped) != VK_SUCCESS || !g_rbMapped) {
        // The buffer/memory are deliberately leaked on this one-time failure
        // path (a few KB, once per session): destroying them would need two
        // more resolved entry points for a path that should never run.
        LogFromController("[x64-streamline-readback] host-visible memory setup failed -- readback "
            "diagnostic disabled (DLSS unaffected).");
        g_rbMapped = nullptr;
        g_rbSetupFailed = true;
        return false;
    }
    LogFromController("[x64-streamline-readback] Readback buffer ready (4 KB host-visible).");
    return true;
}

// Decides whether this evaluate gets a readback.
bool ShouldReadbackThisEvaluateX64(int mode, uint32_t outW, uint32_t outH)
{
    ++g_rbEvaluateCount;
    if (mode != g_rbLastMode || outW != g_rbLastWidth || outH != g_rbLastHeight) {
        g_rbLastMode = mode;
        g_rbLastWidth = outW;
        g_rbLastHeight = outH;
        g_rbForcedRemaining = 3;
    }
    if (g_rbForcedRemaining > 0) { --g_rbForcedRemaining; return true; }
    return (g_rbEvaluateCount % 600) == 0;
}

// Records the centre-block copy of one image into the readback buffer.
void RecordCenterCopyX64(VkCommandBuffer cmd, const DlssTaggedImageX64& img, VkDeviceSize offset,
    RbCopyRecord& rec, const char* label)
{
    rec = RbCopyRecord{};
    if (img.image == VK_NULL_HANDLE || img.width < kRbBlock || img.height < kRbBlock) return;
    uint32_t texel = RbTexelSize(img.format);
    if (texel == 0 || !(img.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
        img.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        char buf[240];
        sprintf_s(buf, "[x64-streamline-readback] %s not copyable (format=%d usage=0x%X layout=%d) -- skipped.",
            label, static_cast<int>(img.format), img.usage, static_cast<int>(img.layout));
        LogFromController(buf);
        return;
    }
    const bool directCopy = (img.layout == VK_IMAGE_LAYOUT_GENERAL ||
        img.layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    const VkImageLayout copyLayout = directCopy ? img.layout : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img.image;
    b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (!directCopy) {
        b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b.oldLayout = img.layout;
        b.newLayout = copyLayout;
        g_vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
    }

    VkBufferImageCopy region{};
    region.bufferOffset = offset;
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageOffset = { static_cast<int32_t>(img.width / 2 - kRbBlock / 2),
                           static_cast<int32_t>(img.height / 2 - kRbBlock / 2), 0 };
    region.imageExtent = { kRbBlock, kRbBlock, 1 };
    g_rbCmdCopyImageToBuffer(cmd, img.image, copyLayout, g_rbBuffer, 1, &region);

    if (!directCopy) {
        b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        b.oldLayout = copyLayout;
        b.newLayout = img.layout;
        g_vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
    }

    rec.valid = true;
    rec.img = img;
    rec.texelSize = texel;
    rec.offset = offset;
}

void LogCopyResultX64(const RbCopyRecord& rec, const char* label, char* out, size_t outSize)
{
    if (!rec.valid) { sprintf_s(out, outSize, "%s=<not copied>", label); return; }
    const uint8_t* data = static_cast<const uint8_t*>(g_rbMapped) + rec.offset;
    uint32_t first = 0;
    memcpy(&first, data, 4);
    float rgba[4];
    if (RbDecodeAverage(data, rec.img.format, kRbBlock * kRbBlock, rgba)) {
        sprintf_s(out, outSize, "%s %ux%u fmt=%d layout=%d avgRGBA=(%.3f,%.3f,%.3f,%.3f) first=0x%08X",
            label, rec.img.width, rec.img.height, static_cast<int>(rec.img.format),
            static_cast<int>(rec.img.layout), rgba[0], rgba[1], rgba[2], rgba[3], first);
    } else {
        sprintf_s(out, outSize, "%s %ux%u fmt=%d layout=%d first=0x%08X (format not decoded)",
            label, rec.img.width, rec.img.height, static_cast<int>(rec.img.format),
            static_cast<int>(rec.img.layout), first);
    }
}

// Records both copies (if this evaluate is sampled). Call after the evaluate's
// trailing barrier, before vkEndCommandBuffer.
void RecordDlssReadbackIfDueX64(VkCommandBuffer cmd, int mode, uint32_t outW, uint32_t outH)
{
    g_rbPendingThisFrame = false;
    if (!ShouldReadbackThisEvaluateX64(mode, outW, outH)) return;
    if (!EnsureReadbackResourcesX64()) return;
    DlssTaggedImageX64 input, output;
    GetDlssTaggedImagesX64(input, output);
    RecordCenterCopyX64(cmd, input, kRbInputOffset, g_rbInput, "input-color");
    RecordCenterCopyX64(cmd, output, kRbOutputOffset, g_rbOutput, "dlss-output");
    if (!g_rbInput.valid && !g_rbOutput.valid) return;
    VkMemoryBarrier hb{};
    hb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    // HOST: makes the copied texels visible to the CPU after the fence wait.
    // ALL_COMMANDS: execution dependency so nothing submitted after this
    // (DXVK's next write to either image) can overtake our transfer reads --
    // needed on the direct-copy path, which has no transition-back barrier.
    g_vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 1, &hb, 0, nullptr, 0, nullptr);
    g_rbPendingThisFrame = true;
}

// Logs the sampled pixels. Call only after the evaluate's fence has signaled.
void LogDlssReadbackIfPendingX64(int mode)
{
    if (!g_rbPendingThisFrame) return;
    g_rbPendingThisFrame = false;
    char in[200], outp[200], buf[520];
    LogCopyResultX64(g_rbInput, "input", in, sizeof(in));
    LogCopyResultX64(g_rbOutput, "output", outp, sizeof(outp));
    sprintf_s(buf, "[x64-streamline-readback] evaluate #%lld mode=%d | %s | %s",
        g_rbEvaluateCount, mode, in, outp);
    LogFromController(buf);
}

} // namespace

// 2026-09-27: computes this frame's effective DLSS mode and output size and
// publishes them in g_dlssEffectiveModeX64 / g_dlssEffectiveOutputWidthX64 /
// g_dlssEffectiveOutputHeightX64. Split out of EvaluateStreamlineDlssX64 so
// RunDlssEvaluateAndCompositeX64 can run it BEFORE TagStreamlineOutputColorX64:
// the output texture is sized from these globals, and computing them only
// inside the evaluate (after the tag) left the texture one frame behind the
// options whenever the effective size changed. Live-confirmed consequence at
// InternalRenderScalePercent > 100: NGX "Output subrect base and size exceed
// the dimensions of the output Color image" on the first DLAA frame (options
// already 5120x2880, texture still 2560x1440). Idempotent; the edge-detected
// log line and overlay notice fire once per transition.
static bool UpdateDlssEffectiveOutputX64(void* device)
{
    if (!device) return false;
    int nativeW = 1920, nativeH = 1080;
    GetRealScreenSize(device, nativeW, nativeH);
    if (nativeW <= 0 || nativeH <= 0) return false;

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
    return true;
}

bool EvaluateStreamlineDlssX64()
{
    if (g_dlssDeviceLostX64) return false; // see this flag's own comment -- permanent for this session
    if (!IsStreamlineInitializedX64()) return false;
    if (!ResolveVulkanFunctionsIfNeeded()) return false;
    if (!EnsureCommandResourcesX64()) return false;

    void* device = GetLastKnownRenderDevice();
    if (!UpdateDlssEffectiveOutputX64(device)) return false;
    int effectiveMode = g_dlssEffectiveModeX64;
    uint32_t effectiveOutputWidth = g_dlssEffectiveOutputWidthX64;
    uint32_t effectiveOutputHeight = g_dlssEffectiveOutputHeightX64;

    if (effectiveOutputWidth != g_lastOptionsWidth || effectiveOutputHeight != g_lastOptionsHeight ||
        effectiveMode != g_lastOptionsMode) {
        bool ok = StreamlineDLSSSetOptionsX64(effectiveOutputWidth, effectiveOutputHeight, effectiveMode);
        g_lastOptionsWidth = effectiveOutputWidth;
        g_lastOptionsHeight = effectiveOutputHeight;
        g_lastOptionsMode = effectiveMode;
        g_optionsEverSucceeded = g_optionsEverSucceeded || ok;
    }
    if (!g_optionsEverSucceeded) return false; // never configured successfully -- nothing to evaluate yet

    // 2026-09-27: never evaluate while the output texture and the options
    // disagree on size -- NGX rejects the frame ("Output subrect base and
    // size exceed the dimensions of the output Color image"). With the
    // effective size now computed before the output tag this should not
    // happen, but a failed texture (re)creation would otherwise reproduce it
    // every frame.
    uint32_t outputTextureWidth = 0, outputTextureHeight = 0;
    if (!GetDlssOutputTextureSizeX64(outputTextureWidth, outputTextureHeight) ||
        outputTextureWidth != effectiveOutputWidth || outputTextureHeight != effectiveOutputHeight) {
        static long long s_sizeMismatchCount = 0;
        ++s_sizeMismatchCount;
        if (s_sizeMismatchCount <= 10 || (s_sizeMismatchCount % 1000) == 0) {
            char buf[260];
            sprintf_s(buf, "[x64-streamline-evaluate] Skipping evaluate: DLSS output texture is %ux%u but "
                "the options expect %ux%u (count=%lld).", outputTextureWidth, outputTextureHeight,
                effectiveOutputWidth, effectiveOutputHeight, s_sizeMismatchCount);
            LogFromController(buf);
        }
        return false;
    }

    VkDevice vkDevice = GetDxvkVkDeviceX64();
    VkQueue vkQueue = GetDxvkVkQueueX64();
    if (vkDevice == VK_NULL_HANDLE || vkQueue == VK_NULL_HANDLE) return false;

    // REAL FIX, 2026-09-27 -- see ScopedDxvkSubmissionLockX64's own top
    // comment for the full real root-cause record. Must be acquired before
    // the command-buffer reset/record/submit/wait sequence below (all of it
    // is "Vulkan commands submitted to the rendering queue" in DXVK's own
    // terms) and must stay held for the ENTIRE sequence, including the fence
    // wait -- releasing early would reopen the exact race this exists to
    // close. The guard's destructor releases it automatically on every
    // return path below.
    ScopedDxvkSubmissionLockX64 dxvkLock(device);
    if (!dxvkLock.IsValid()) {
        static bool s_lockFailureLogged = false;
        if (!s_lockFailureLogged) {
            s_lockFailureLogged = true;
            LogFromController("[x64-streamline-evaluate] Could not acquire DXVK's submission queue lock "
                "(QueryInterface(ID3D9VkInteropDevice) failed) -- skipping DLSS evaluation this frame "
                "for safety rather than submitting unsynchronized.");
        }
        return false;
    }

    // REAL FIX, 2026-09-27 (ROUND 19) -- see RetagStreamlineInputsAfterFlushX64's
    // own comment (streamline_resources_x64.cpp): the flush the guard above just
    // did can relocate any of DLSS's four input/output images to a new VkImage,
    // so every tag set earlier this frame may now name a retired image. Re-resolve
    // and re-tag all four here, then lock.
    if (!RetagStreamlineInputsAfterFlushX64()) return false; // failures already
        // logged (rate-limited) by the re-resolve itself; a frame whose scene
        // color/depth weren't recorded is skipped silently.
    dxvkLock.Lock();

    g_vkResetCommandBuffer(g_commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkResult vr = g_vkBeginCommandBuffer(g_commandBuffer, &beginInfo);
    if (vr != VK_SUCCESS) {
        char buf[150];
        sprintf_s(buf, "[x64-streamline-evaluate] vkBeginCommandBuffer FAILED (VkResult=%d).", static_cast<int>(vr));
        LogFromController(buf);
        return false;
    }

    // 2026-09-27 (ROUND 19): full memory barriers around DLSS's own recorded
    // work -- the cross-API hand-off neither side performs for us. DXVK's
    // barriers only cover access DXVK itself knows about, and a plain fence
    // wait between two submissions is an execution dependency, not a memory
    // one. The leading barrier's first scope (ALL_COMMANDS, in submission
    // order) covers every DXVK command list submitted before ours on this
    // queue -- the scene color/depth writes and any relocation copies the
    // guard's flush just recorded -- making them visible to DLSS's reads. The
    // trailing one makes DLSS's output writes visible to whatever DXVK
    // submits next (the composite StretchRect's blit). Global memory barriers
    // only, no layout changes: Streamline performs its own transitions from/
    // back to each tag's reported layout.
    VkMemoryBarrier handoffBarrier{};
    handoffBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    handoffBarrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    handoffBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    g_vkCmdPipelineBarrier(g_commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &handoffBarrier, 0, nullptr, 0, nullptr);

    bool evaluateOk = StreamlineEvaluateFeatureX64(reinterpret_cast<void*>(g_commandBuffer));

    g_vkCmdPipelineBarrier(g_commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &handoffBarrier, 0, nullptr, 0, nullptr);

    if (evaluateOk) RecordDlssReadbackIfDueX64(g_commandBuffer, effectiveMode, effectiveOutputWidth,
        effectiveOutputHeight); // read-only diagnostic, see its own block comment

    vr = g_vkEndCommandBuffer(g_commandBuffer);
    if (vr != VK_SUCCESS) {
        char buf[150];
        sprintf_s(buf, "[x64-streamline-evaluate] vkEndCommandBuffer FAILED (VkResult=%d).", static_cast<int>(vr));
        LogFromController(buf);
        return false;
    }

    if (!evaluateOk) return false; // StreamlineEvaluateFeatureX64 already logged the real failure reason;
        // still submit nothing further this frame -- an empty/incomplete recording isn't worth running.

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &g_commandBuffer;

    vr = g_vkQueueSubmit(vkQueue, 1, &submitInfo, g_fence);
    if (vr != VK_SUCCESS) {
        char buf[200];
        sprintf_s(buf, "[x64-streamline-evaluate] vkQueueSubmit FAILED (VkResult=%d)%s.", static_cast<int>(vr),
            (vr == VK_ERROR_DEVICE_LOST) ? " -- VK_ERROR_DEVICE_LOST, disabling DLSS for the rest of "
                "this session (see g_dlssDeviceLostX64's own comment)" : "");
        LogFromController(buf);
        if (vr == VK_ERROR_DEVICE_LOST) g_dlssDeviceLostX64 = true;
        return false;
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
    if (waitResult == VK_ERROR_DEVICE_LOST) {
        LogFromController("[x64-streamline-evaluate] vkWaitForFences reported VK_ERROR_DEVICE_LOST -- "
            "disabling DLSS for the rest of this session (see g_dlssDeviceLostX64's own comment).");
        g_dlssDeviceLostX64 = true;
        return false;
    }
    if (waitResult == VK_TIMEOUT) {
        static long long s_timeoutCount = 0;
        ++s_timeoutCount;
        char buf[400]; // widened from 200, ROUND 18 -- the revised message below is 270 bytes
            // worst case (count at INT64_MIN), per this project's own sprintf_s-overflow lesson.
        sprintf_s(buf, "[x64-streamline-evaluate] vkWaitForFences TIMED OUT after 2s (count=%lld) -- "
            "DLSS's own recorded GPU work never signaled completion. Disabling DLSS for the rest of "
            "this session (the command buffer is still pending on the GPU and cannot be safely "
            "reused).", s_timeoutCount);
        LogFromController(buf);
        // Do NOT reset the fence here -- it may still signal later; resetting
        // a fence the driver hasn't actually finished with is undefined per
        // the Vulkan spec.
        // REVISED, 2026-09-27 (ROUND 18): this used to fall through and let
        // the NEXT frame call vkResetCommandBuffer/vkBeginCommandBuffer on the
        // same g_commandBuffer -- but a timed-out submission leaves it in the
        // PENDING state, and resetting or re-recording a pending command
        // buffer is itself invalid usage per the Vulkan spec
        // (VUID-vkResetCommandBuffer-commandBuffer-00045), stacking a second
        // fault on top of whatever made the GPU stall. Same session-long
        // latch as device loss: stop touching DLSS entirely rather than
        // compound it.
        g_dlssDeviceLostX64 = true;
        return false;
    }
    g_vkResetFences(vkDevice, 1, &g_fence);
    LogDlssReadbackIfPendingX64(effectiveMode);
    return true; // genuine, complete, fence-signaled success -- safe to composite this frame.
}

// 2026-09-27: called once, from streamline_integration_x64.cpp's own
// TryInitStreamlineX64 immediately after device registration succeeds --
// see that call site's own comment for the real bug this closes (DLSS's
// first, heaviest resource allocation used to happen cold, on the first
// real GAMEPLAY frame instead of here, at a quiet/idle moment). Calls
// StreamlineDLSSSetOptionsX64 directly with the real display resolution
// and the CONFIGURED mode (not the DLAA-effective one -- at this early
// point nothing has rendered yet, so GetStreamlineInternalRenderResolutionX64
// would have nothing real to report; EvaluateStreamlineDlssX64's own
// existing lazy re-check still runs later and correctly upgrades to the
// DLAA-effective resolution/mode once real gameplay starts and a mismatch
// is actually detected -- that becomes a smaller, cheaper re-configure
// call, not this original cold allocation). Deliberately does not touch
// g_dlssEffectiveModeX64/g_dlssEffectiveOutputWidthX64/HeightX64 -- those
// describe the DLAA-override decision specifically, not this generic
// pre-warm, and streamline_resources_x64.cpp's own output-buffer sizing
// already falls back to the real display resolution until those are set.
void PrewarmDlssOptionsX64(void* device)
{
    // REAL BUG, caught before shipping: this used to call
    // GetLastKnownRenderDevice() instead of taking the device directly --
    // that global (overlay_hud.cpp) is only ever set from inside
    // Hook_EndScene, which hasn't fired even once yet at the point this
    // function runs (called from Hook_CreateDevice's own call chain,
    // before any frame exists) -- it would have silently no-op'd on
    // !device, defeating the entire point of this fix. The caller
    // (TryInitStreamlineX64, streamline_integration_x64.cpp) already has
    // the real device pointer from CreateDevice's own parameters -- use
    // that directly instead.
    if (!device) return;
    int nativeW = 1920, nativeH = 1080;
    GetRealScreenSize(device, nativeW, nativeH);
    if (nativeW <= 0 || nativeH <= 0) return;

    uint32_t displayWidth = static_cast<uint32_t>(nativeW);
    uint32_t displayHeight = static_cast<uint32_t>(nativeH);
    int mode = g_modConfig.dlssModeX64;

    bool ok = StreamlineDLSSSetOptionsX64(displayWidth, displayHeight, mode);
    g_lastOptionsWidth = displayWidth;
    g_lastOptionsHeight = displayHeight;
    g_lastOptionsMode = mode;
    g_optionsEverSucceeded = g_optionsEverSucceeded || ok;

    // REAL BUG, caught live via crash dump analysis, 2026-09-27: this literal's real
    // worst case (uint32_t/int printed at their maximum negative/digit-count width) is
    // 238 bytes -- buf[200] was too small, and this UCRT fails fast (0xc0000409/
    // FAST_FAIL_INVALID_ARG) rather than truncating, crashing BOTH iw5sp.exe/iw5mp.exe
    // on every launch since this call runs unconditionally from Hook_CreateDevice, before
    // anything else in the mod gets a chance to run. The exact same recurring bug class
    // this project has hit and fixed many times before (known_issues_x64.md, 2026-09-05/
    // 13/14/16/26) -- widened generously (400, not just "big enough for today").
    char buf[400];
    sprintf_s(buf, "[x64-streamline-evaluate] Pre-warmed slDLSSSetOptions at device-registration time "
        "(display=%ux%u mode=%d, ok=%d) -- real DLSS resource allocation now happens at an idle moment, "
        "not on the first real gameplay frame.", displayWidth, displayHeight, mode, ok ? 1 : 0);
    LogFromController(buf);

    // REAL FIX, 2026-09-27 -- see StreamlineAllocateResourcesX64's own top
    // comment (streamline_integration_x64.cpp) for the real GPU-hang finding
    // this closes. Only meaningful once slDLSSSetOptions itself has actually
    // succeeded (the feature must be configured before its resources can be
    // meaningfully allocated).
    if (ok) {
        bool allocOk = StreamlineAllocateResourcesX64(sl::kFeatureDLSS);
        char abuf[200];
        sprintf_s(abuf, "[x64-streamline-evaluate] Pre-warmed slAllocateResources(DLSS) at "
            "device-registration time, ok=%d.", allocOk ? 1 : 0);
        LogFromController(abuf);
    }
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

// IDirect3DDevice9::GetRenderTarget (slot 38) / StretchRect (slot 34) -- the
// same real, stable COM vtable constants overlay_hud.cpp's own
// kGetRenderTargetVtableIndex/kStretchRectVtableIndex already use (that
// file's DrawFullScreenPass uses the identical pattern for its own capture
// step), redeclared here per this project's own "each file owns its own
// copy of these fixed constants" convention rather than shared across files.
namespace {
constexpr int kGetRenderTargetVtableIndexX64 = 38;
constexpr int kStretchRectVtableIndexX64 = 34;
typedef HRESULT(WINAPI* GetRenderTargetFnX64)(void* This, DWORD RenderTargetIndex, void** ppRenderTarget);
typedef HRESULT(WINAPI* StretchRectFnX64)(void* This, void* pSourceSurface, const RECT* pSourceRect,
    void* pDestSurface, const RECT* pDestRect, DWORD Filter);
constexpr DWORD kD3DTEXF_LINEAR_X64 = 2;

bool g_dlssCompositeRanThisFrameX64 = false; // per-real-frame guard, same shape as
    // overlay_hud.cpp's own g_motionBlurRanThisFrame -- FUN_14018def0 (this
    // function's own real trigger, via TriggerMotionBlurFromEngineHook) can fire
    // more than once per real frame (splitscreen/PIP exclusion-zone carving,
    // per that hook's own long-documented history) -- never evaluate/composite
    // DLSS more than once for the same real frame.
} // namespace

// 2026-09-27: real final-composite entry point -- the piece that actually
// gets DLSS's enhanced output in front of the player, closing the "still
// explicitly open" compositing gap every prior round of this item flagged.
// Called from TriggerMotionBlurFromEngineHook (overlay_hud.cpp), the SAME
// real per-viewport boundary motion blur/FSR already use (FUN_14018def0 on
// x64) -- see this file's own top-of-file comment for why this specific
// timing (before HUD/UI drawing, not Hook_EndScene/after) is required.
//
// Real gates, matching motion blur's own three ("do NOT ship with fewer than
// x86's own three gates" -- issue #96/#97/#103/#104's own standing rule):
// menu-active, in-level, clcState. Without these, a stale DLSS output from
// the last real gameplay frame could get composited over a menu or loading
// screen -- DLSS's own color-input tag only ever updates while the 3D scene
// target is actually bound, so g_outputColorTexture can easily hold content
// from several real frames ago by the time a menu opens.
void RunDlssEvaluateAndCompositeX64(void* device)
{
    if (!IsStreamlineInitializedX64()) return;
    if (!device) return;

    if (g_dlssCompositeRanThisFrameX64) return;

    if (IsMenuActiveX64_Exported()) return;
    int inLevel = 0;
    if (!TryGetInLevelFlagX64(&inLevel) || inLevel <= 0) return;
    int clcState = 0;
    if (!TryGetClcStateX64(&clcState) || clcState == 0) return;
    // 2026-09-27 (ROUND 24): this frame's camera constants, from the engine's
    // real view/projection (post-FX has just bound this view's GfxViewParms).
    // Must run before the stabilization gate, which counts these updates.
    UpdateStreamlineCameraFromEngineViewX64(GetEngineMainCmdBufStateX64());
    if (!IsStreamlineCameraStableX64()) return; // real stabilization-window gate --
        // see this file's own extern declaration comment above for the exact race
        // this closes (mid-transition evaluate attempts on a just-reset camera history).

    g_dlssCompositeRanThisFrameX64 = true;

    // Tag this frame's output/motion-vectors buffers (streamline_resources_x64.cpp)
    // BEFORE evaluating -- both must be tagged with the current frame's token
    // before slEvaluateFeature reads them. Color/depth are already tagged earlier
    // in the frame via their own SetRenderTarget/SetDepthStencilSurface hooks,
    // which fire naturally during the 3D scene's own rendering, well before this
    // boundary -- no change needed there.
    // Effective mode/size first, so the output texture below is created at the
    // size this frame's options will use (see UpdateDlssEffectiveOutputX64).
    if (!UpdateDlssEffectiveOutputX64(device)) return;
    TagStreamlineOutputColorX64();
    TagStreamlineMotionVectorsX64();
    // 2026-09-27 (ROUND 23): the engine's scene target is multisampled in real
    // configs; resolve it into a single-sampled texture DLSS can read. No-op
    // when the frame's scene colour was already single-sampled.
    if (!ResolveMsaaSceneColorForDlssX64(device)) return;

    bool evaluated = EvaluateStreamlineDlssX64();
    if (!evaluated) return; // already logged its own real failure reason -- nothing to composite

    void* outputSurface = GetDlssOutputSurfaceX64();
    if (!outputSurface) return;

    void** deviceVtbl = *reinterpret_cast<void***>(device);
    auto getRenderTarget = reinterpret_cast<GetRenderTargetFnX64>(deviceVtbl[kGetRenderTargetVtableIndexX64]);
    auto stretchRect = reinterpret_cast<StretchRectFnX64>(deviceVtbl[kStretchRectVtableIndexX64]);

    void* currentTarget = nullptr;
    // GetRenderTarget(0) here is the real 3D-scene target the engine has bound
    // AT THIS EXACT BOUNDARY -- confirmed by precedent, not assumed: motion
    // blur's own full-screen pass already writes into whatever this call
    // returns at this same trigger point, live-confirmed correct (no HUD
    // bleed, no corruption) since 2026-09-13. StretchRect resizes automatically
    // if source/dest dimensions differ (the DLAA-override case, where
    // outputSurface is at the render-scale resolution, larger than this
    // target) -- a real, standard D3D9 capability, not something built here.
    if (FAILED(getRenderTarget(device, 0, &currentTarget)) || !currentTarget) return;

    HRESULT hr = stretchRect(device, outputSurface, nullptr, currentTarget, nullptr, kD3DTEXF_LINEAR_X64);
    reinterpret_cast<IUnknown*>(currentTarget)->Release();

    static long long s_compositeCount = 0;
    ++s_compositeCount;
    bool heartbeat = s_compositeCount <= 5 || (s_compositeCount % 5000) == 0;
    if (FAILED(hr)) {
        static bool s_failureLogged = false;
        if (!s_failureLogged || heartbeat) {
            s_failureLogged = true;
            char buf[200];
            sprintf_s(buf, "[x64-streamline-composite] StretchRect FAILED (hr=0x%08lX, count=%lld) -- "
                "DLSS's own enhanced output was NOT composited this frame.", static_cast<unsigned long>(hr),
                s_compositeCount);
            LogFromController(buf);
        }
        return;
    }

    if (heartbeat) {
        char buf[200]; // widened from 150 during this session's own sprintf_s sweep --
            // real worst case (count at INT64_MIN) is 148 bytes, a 1-byte margin at 150
            // was too tight per this project's own standing lesson.
        sprintf_s(buf, "[x64-streamline-composite] StretchRect succeeded (count=%lld) -- DLSS's "
            "enhanced output composited before this frame's HUD/UI draws.", s_compositeCount);
        LogFromController(buf);
    }
}

// Reset hook for the per-real-frame guard above -- called from Hook_EndScene
// (overlay_hud.cpp), same real "exactly once per real frame, always after
// every FUN_14018def0 call this frame already happened" reasoning
// g_motionBlurRanThisFrame's own reset already documents.
void ResetDlssCompositeFrameGuardX64()
{
    g_dlssCompositeRanThisFrameX64 = false;
    ClearStreamlineFrameSurfacesX64(); // 2026-09-27 (ROUND 19) -- same once-per-real-frame
        // point; this frame's recorded scene color/depth surfaces must never carry over.
}

// 2026-09-27, ROUND 17 (corrected same day): real, one-shot menu-active DLSS
// warm-up -- direct user-requested experiment ("Option #1": force the first
// real evaluate to fire before any gameplay gate applies), given live evidence
// the hang is tied to the very FIRST real evaluate call ever made, and that
// the crash timing is genuinely map-dependent (Dome hung mid-transition,
// faster than before; Underground reached first-person first) -- consistent
// with a cold GPU-side first-time cost (NGX shader/kernel compilation being
// the leading hypothesis) colliding with whatever timing pressure a specific
// map's own transition puts on the same frame. This is exploratory, not a
// confirmed fix, and is honestly logged as such.
//
// CORRECTED same day: the first version of this function built its own
// degenerate identity sl::Constants, on the wrong assumption that no real
// camera exists at any "menu active" screen. Live-disproven the first time
// this fired: the specific menu active during that test still had a real,
// live camera (this game's pause menu keeps the loaded level rendering
// behind it) -- the real per-frame camera path (Hook_ProjectionMatrixBuild)
// had ALREADY called slSetConstants for that tick, and Streamline correctly
// rejected this function's own separate, duplicate call ("Setting different
// 'common' constants multiple times within the same frame is NOT allowed!"),
// aborting before ever reaching evaluate -- the experiment never actually
// ran. Fixed by dropping the separate Constants path entirely: this function
// now just requires g_haveStreamlinePrevFrameX64 (the real per-frame path has
// validly set Constants at least once this session) and calls
// EvaluateStreamlineDlssX64() directly, reusing whatever Constants the real
// path already set -- no duplicate call, no synthesized camera data.
//
// Deliberately narrower than RunDlssEvaluateAndCompositeX64 above:
//   - Never composites anything to screen while menu-active -- calls
//     EvaluateStreamlineDlssX64() directly and discards the result either way.
//   - Fires at most once per process lifetime, regardless of outcome -- a
//     hang here is exactly as costly to the player as a hang during real
//     gameplay, so this must never retry.
//   - Reuses EvaluateStreamlineDlssX64()'s own existing real safety net
//     unchanged: ScopedDxvkSubmissionLockX64, the 2s fence timeout, and the
//     g_dlssDeviceLostX64 latch this file's own Round 13/14 fixes already
//     added -- if THIS call is what hangs, it hangs under the exact same
//     bounded, already-proven-safest-available conditions as the real
//     in-gameplay path, not a weaker ad hoc one.
//
// Called from TriggerMotionBlurFromEngineHook (overlay_hud.cpp) -- the same
// real per-viewport boundary the gameplay composite path uses, which also
// fires while a menu is active (confirmed reachable: this boundary is a
// generic per-viewport HUD/2D dispatch point, not gated to in-level content).
void RunDlssMainMenuWarmupOnceX64(void* device)
{
    static bool s_warmupAttempted = false;
    if (s_warmupAttempted) return;
    if (!IsStreamlineInitializedX64()) return;
    if (!device) return;
    if (!IsMenuActiveX64_Exported()) return; // only ever while some menu is active --
        // mutually exclusive with the real gameplay path (which requires inLevel > 0
        // and refuses to run while a menu is active).
    // 2026-09-27 (ROUND 24): constants are no longer set every frame from the
    // projection hook, so set this frame's here (no-op if already set this frame).
    UpdateStreamlineCameraFromEngineViewX64(GetEngineMainCmdBufStateX64());
    if (!HasStreamlineCameraDataX64()) return; // real per-frame Constants haven't been
        // set yet this session (no camera has rendered at all) -- wait for a menu state
        // that has one (e.g. the pause menu over an already-loaded level), rather than
        // trying to evaluate against Constants that were never validly set.

    s_warmupAttempted = true; // one-shot regardless of outcome -- see this function's
        // own header comment for why a hang here must never be retried.

    LogFromController("[dlss-menu-warmup] attempting one-shot menu-active DLSS evaluate "
        "warm-up (Option #1 experiment, corrected) -- forcing any real first-time GPU-side "
        "cost (NGX kernel/shader compilation) to happen here instead of during a real "
        "gameplay map transition. Reusing this tick's real Constants, already set by the "
        "normal per-frame camera path.");

    // Self-owned output/motion-vectors resources -- safe to create/tag regardless of
    // whether any real 3D scene is currently rendering (see streamline_resources_x64.cpp's
    // own TagStreamlineOutputColorX64/TagStreamlineMotionVectorsX64 -- this project
    // creates and owns both textures itself, sized off the real display resolution).
    // Effective mode/size first, so the output texture below is created at the
    // size this frame's options will use (see UpdateDlssEffectiveOutputX64).
    if (!UpdateDlssEffectiveOutputX64(device)) return;
    TagStreamlineOutputColorX64();
    TagStreamlineMotionVectorsX64();

    bool evaluated = EvaluateStreamlineDlssX64();
    char buf[256];
    sprintf_s(buf, "[dlss-menu-warmup] one-shot main-menu evaluate attempt finished, "
        "evaluated=%d (see prior EvaluateStreamlineDlssX64 log lines for the real detail -- "
        "this result is NEVER composited to screen). Real gameplay evaluate path is unaffected "
        "either way.", evaluated ? 1 : 0);
    LogFromController(buf);
}
