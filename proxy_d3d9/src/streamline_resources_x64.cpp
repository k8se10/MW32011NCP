// MW32011NCP, 2026-09-24: real resource-tagging groundwork for DLSS. This
// file resolves the real backing Vulkan images for the game's own D3D9
// surfaces (depth-stencil, and now the primary color render target), via
// DXVK's own real ID3D9VkInteropTexture interop interface
// (dxvk_interop_x64.h) -- the same interop mechanism already proven working
// for slSetVulkanInfo's own device/queue handles.
//
// DEPTH (kBufferTypeDepth) -- LIVE-CONFIRMED working end to end:
//   ROUND 1 polled GetDepthStencilSurface from Hook_EndScene -- real, live
//   D3DERR_NOTFOUND every time (wrong hook point: EndScene fires after the
//   depth-stencil surface is typically unbound for 2D/UI passes).
//   ROUND 2: hooked SetDepthStencilSurface directly instead -- real
//   depth-stencil Vulkan images resolved correctly (VK_FORMAT_D24_UNORM_
//   S8_UINT, DEPTH_STENCIL_ATTACHMENT usage, extent matching the real
//   render-scale resolution).
//   ROUND 3: sl::Resource's own doc comment is explicit -- "Resource view,
//   description etc. are MANDATORY only when using Vulkan" -- a real
//   VkImageView has to be created directly via the Vulkan API (no import
//   library linked in this project; every Vulkan function used here is
//   resolved dynamically via GetProcAddress on the real loader DLL already
//   loaded in-process by DXVK).
//   ROUND 4: slSetTagForFrame() itself failed every call with
//   eErrorInvalidIntegration until sl::PreferenceFlags::
//   eUseFrameBasedResourceTagging was added to slInit()'s own Preferences
//   (streamline_integration_x64.cpp) -- required per the SDK's own doc
//   comment on the deprecated slSetTag(). LIVE-CONFIRMED succeeding after
//   that fix.
//   ROUND 5: slGetFeatureRequirements(DLSS)'s own real requiredTags list
//   (logged for the first time) confirmed the full real requirement set:
//   kBufferTypeDepth(0), kBufferTypeMotionVectors(1),
//   kBufferTypeScalingInputColor(3), kBufferTypeScalingOutputColor(4) --
//   the authoritative source, not a guess from the public docs' general
//   description.
//
// COLOR INPUT (kBufferTypeScalingInputColor) -- this pass: the same proven
// pattern, hooking SetRenderTarget(0, ...) instead of SetDepthStencilSurface,
// COLOR aspect instead of DEPTH.
//
// STILL NOT DONE: kBufferTypeMotionVectors (Stage 1 camera-only needs SOME
// real tagged buffer even though cameraMotionIncluded=eFalse -- the required
// list proves a tag is mandatory, not optional to omit) and
// kBufferTypeScalingOutputColor (DLSS writes into this -- the game has no
// existing resource for it; a real texture has to be created at the
// upscaled target resolution, tying into this project's own capture/
// composite pipeline design, not yet done).

#include <windows.h>
#include <cstdio>
#include "dxvk_interop_x64.h" // real Vulkan SDK types -- must come BEFORE sl.h,
    // which uses Vulkan types without including a Vulkan header itself.
#include "../third_party/streamline/include/sl.h"
#include "../third_party/minhook/include/MinHook.h"
#include "mod_config.h" // g_modConfig.internalRenderScalePercent
#include "overlay_hud.h" // GetLastKnownRenderDevice/GetRealScreenSize

extern void LogFromController(const char* msg); // dllmain.cpp
extern "C" bool IsStreamlineInitializedX64(); // streamline_integration_x64.cpp,
    // 2026-09-26 -- see its own comment for the full rationale. MSVC (unlike
    // clang) requires a linkage-specification declaration at true global
    // scope, not inside a function body -- a real C2598 caught before this
    // shipped.
extern VkInstance GetDxvkVkInstanceX64(); // streamline_integration_x64.cpp
extern VkDevice GetDxvkVkDeviceX64();     // streamline_integration_x64.cpp
extern "C" bool TryGetEngineRenderTargetSelectX64(int* outId); // analog_input_hooks_x64.cpp, 2026-09-27 (ROUND 22)
extern bool StreamlineSetTagForFrameX64(const sl::ResourceTag* tags, uint32_t numTags); // streamline_integration_x64.cpp
extern bool GetDlssEffectiveOutputResolutionX64(uint32_t& outWidth, uint32_t& outHeight); // streamline_evaluate_x64.cpp,
    // 2026-09-27 -- see that file's own g_dlssEffectiveModeX64 comment. The real DLSS
    // output resolution during the above-output/DLAA override is the render-scale
    // INPUT resolution, not the display's -- this project's own owned output texture
    // (TagOutputColorResourceForFrame, below) must match it exactly.

namespace {

// IDirect3DDevice9::SetDepthStencilSurface -- standard, Microsoft-documented
// D3D9 COM vtable slot 39 (IUnknown's 3 slots + 36 IDirect3DDevice9-specific
// methods before it, one before GetDepthStencilSurface's own confirmed
// slot 40). Same "stable public interface" reasoning overlay_hud.cpp's own
// vtable indices already use.
constexpr int kSetDepthStencilSurfaceVtableIndex = 39;

// IDirect3DDevice9::SetRenderTarget -- standard COM vtable slot 37 (one
// before SetDepthStencilSurface's own confirmed slot 39, i.e. 38 is
// GetRenderTarget -- matches this project's own already-live
// kGetRenderTargetVtableIndex=38 in overlay_hud.cpp exactly). Real
// signature: HRESULT SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9*).
constexpr int kSetRenderTargetVtableIndex = 37;

// IDirect3DDevice9::CreateTexture (slot 23) / IDirect3DTexture9::GetSurfaceLevel
// (slot 18) -- same stable public COM constants overlay_hud.cpp's own
// kCreateTextureVtableIndex/kGetSurfaceLevelVtableIndex already use; redeclared
// here (internal linkage, not exported) rather than shared, matching this
// project's own existing convention of each file owning its own copy of these
// fixed, never-changing interface constants.
constexpr int kCreateTextureVtableIndex = 23;
constexpr int kGetSurfaceLevelVtableIndex = 18;
constexpr DWORD kD3DUSAGE_RENDERTARGET = 0x00000001;
constexpr DWORD kD3DPOOL_DEFAULT = 0;
constexpr DWORD kD3DFMT_A8B8G8R8 = 32; // VK_FORMAT_R8G8B8A8_UNORM via DXVK's
    // translation -- the DLSS output buffer's format, see TagOutputColorResourceForFrame's
    // own 2026-09-27 comment for why not A8R8G8B8 (21, B8G8R8A8_UNORM) any more.
constexpr DWORD kD3DRTYPE_TEXTURE = 3; // D3DRESOURCETYPE -- D3D9VkExtImageDescX64::Type
constexpr DWORD kD3DFMT_G16R16F = 115; // standard D3D9 2-channel 16-bit float --
    // the real, standard format for a motion-vector buffer (X/Y offset per pixel).
constexpr int kColorFillVtableIndex = 35; // IDirect3DDevice9::ColorFill -- same
    // standard D3D9 vtable layout already confirmed live elsewhere in this
    // codebase (overlay_hud.cpp's own kEndSceneVtableIndex=42/kClearVtableIndex=43,
    // and this file's own kCreateTextureVtableIndex=23/kSetRenderTargetVtableIndex=37/
    // kSetDepthStencilSurfaceVtableIndex=39 -- all consistent with one real,
    // single vtable ordering, not independently guessed here).

typedef HRESULT(WINAPI* CreateTextureFn)(void* This, UINT Width, UINT Height, UINT Levels,
    DWORD Usage, DWORD Format, DWORD Pool, void** ppTexture, HANDLE* pSharedHandle);
typedef HRESULT(WINAPI* GetSurfaceLevelFn)(void* This, UINT Level, void** ppSurfaceLevel);
typedef HRESULT(WINAPI* ColorFillFn)(void* This, void* pSurface, const RECT* pRect, DWORD color);
    // DWORD, not D3DCOLOR (a plain DWORD typedef in the real SDK) -- this file
    // deliberately never includes d3d9.h (see dxvk_interop_x64.h's own comment
    // on why, MIDL/type collisions with DXVK's bundled headers), so its
    // D3DCOLOR typedef isn't available here; DWORD is ABI-identical.

// IDirect3DSurface9::GetDesc, standard COM vtable slot 12 -- same real
// constant and struct layout overlay_hud.cpp's own kSurfaceGetDescVtableIndex/
// SurfaceDesc already use, redeclared here per this project's own "each file
// owns its own copy of these fixed constants" convention. Used, 2026-09-24,
// as a cheap PURE-D3D9 pre-filter before ever calling the real Vulkan interop
// path below -- see TagColorResourceForFrame's own header comment for the
// real "huge fps cost" bug this closes.
constexpr int kSurfaceGetDescVtableIndexX64 = 12;

// IDirect3DDevice9::StretchRect (slot 34) -- same standard vtable ordering as the
// constants above (streamline_evaluate_x64.cpp's own composite uses the same slot).
// 2026-09-27 (ROUND 23): used to resolve the engine's multisampled scene target.
constexpr int kStretchRectVtableIndexRes = 34;
constexpr DWORD kD3DTEXF_NONE = 0;
typedef HRESULT(WINAPI* StretchRectFnRes)(void* This, void* pSourceSurface, const RECT* pSourceRect,
    void* pDestSurface, const RECT* pDestRect, DWORD Filter);
struct SurfaceDescX64 { DWORD Format, Type, Usage, Pool, MultiSampleType, MultiSampleQuality, Width, Height; };
typedef HRESULT(WINAPI* SurfaceGetDescFnX64)(void* This, SurfaceDescX64* pDesc);

using SetDepthStencilSurfaceFn = HRESULT(STDMETHODCALLTYPE*)(void*, void*);
SetDepthStencilSurfaceFn g_origSetDepthStencilSurface = nullptr;

using SetRenderTargetFn = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, void*);
SetRenderTargetFn g_origSetRenderTarget = nullptr;

long long g_resolveAttemptCount = 0;
long long g_resolveColorAttemptCount = 0;

// Real, live-caught disambiguation need: SetRenderTarget(0, ...) fires for
// MANY real render targets per frame (shadow maps, post-process ping-pong
// buffers, UI compositing), not just the final 3D scene color -- live-
// confirmed (two distinct resolutions tagged indiscriminately on the first
// attempt at this: the real 2560x1440 display-resolution target, and the
// real 5120x2880 internal-render-scale target, at InternalRenderScalePercent
// =200%). Direct instruction: "internal render scaled percent is what we
// feed into dlss" -- so the real target is specifically the one at the
// real internal render-scale resolution, computed the exact same way
// Hook_RenderResCompute (analog_input_hooks_x64.cpp) already does:
// target = native * pct / 100. No new RE needed, this is the same real,
// already-confirmed formula.
bool ComputeExpectedInternalResolutionX64(uint32_t& outWidth, uint32_t& outHeight)
{
    int nativeW = 1920, nativeH = 1080;
    GetRealScreenSize(GetLastKnownRenderDevice(), nativeW, nativeH);
    if (nativeW <= 0 || nativeH <= 0) return false;

    int pct = g_modConfig.internalRenderScalePercent;
    if (pct <= 0) {
        // Render scale off -- Hook_RenderResCompute's own real gate
        // (`if (pct > 0)`) never fires either, so the engine renders the
        // scene at native resolution directly; the expected scene-color
        // target IS the native/display resolution.
        outWidth = static_cast<uint32_t>(nativeW);
        outHeight = static_cast<uint32_t>(nativeH);
        return true;
    }

    outWidth = static_cast<uint32_t>(static_cast<int64_t>(nativeW) * pct / 100);
    outHeight = static_cast<uint32_t>(static_cast<int64_t>(nativeH) * pct / 100);
    return true;
}

// Real Vulkan functions, resolved dynamically -- see this file's own top
// comment for why (no Vulkan import library linked in this project).
using PFN_vkGetInstanceProcAddr_local = PFN_vkVoidFunction(VKAPI_PTR*)(VkInstance, const char*);
using PFN_vkGetDeviceProcAddr_local = PFN_vkVoidFunction(VKAPI_PTR*)(VkDevice, const char*);
PFN_vkCreateImageView g_vkCreateImageView = nullptr;
PFN_vkDestroyImageView g_vkDestroyImageView = nullptr;
bool g_vulkanFunctionsResolved = false;
bool g_vulkanFunctionsResolveFailed = false;

// Real per-image view caches -- SetDepthStencilSurface/SetRenderTarget fire
// far more often than the underlying image actually changes (live-confirmed
// for depth: the same 1-2 real VkImage handles repeat across many
// consecutive binds), so only create/destroy a real VkImageView when the
// image genuinely changes. Separate caches for depth vs. color since
// they're two independent resources.
VkImage g_cachedDepthImage = VK_NULL_HANDLE;
VkImageView g_cachedDepthView = VK_NULL_HANDLE;
// This frame's real scene depth/color D3D9 surfaces -- the last depth-stencil
// surface the game bound, and the last render target 0 that passed
// TagColorResourceForFrame's resolution match, both recorded by the hooks
// below. 2026-09-27 (ROUND 19): RetagStreamlineInputsAfterFlushX64 re-resolves
// them to their CURRENT VkImage right before evaluate -- see its own comment
// for the real relocation bug this closes. Raw, non-owning pointers, valid
// only for the frame they were recorded in (the game never releases a bound
// scene target mid-frame): cleared every frame by
// ClearStreamlineFrameSurfacesX64 (called from ResetDlssCompositeFrameGuardX64,
// Hook_EndScene). Deliberately NOT AddRef'd -- holding a reference to a
// D3DPOOL_DEFAULT game resource past its owner's Release would make a later
// IDirect3DDevice9::Reset fail.
//
// REMOVED, 2026-09-27 (ROUND 19): g_lastProcessedDepthSurfaceX64 and its
// g_lastDepthImage/Layout/Info cache, which skipped GetVulkanImageInfo when
// the same D3D9 depth surface was bound again. It assumed a surface's VkImage
// never changes -- false: DXVK's memory defragmentation (on by default on
// NVIDIA, dxvk_memory.cpp enableDefrag/pickDefragChunk) relocates images to
// new VkImage handles, so the cache tagged a stale, eventually-destroyed image
// forever after the first relocation. Its stated reason ("GetVulkanImageInfo
// flushes pending Vulkan commands") does not hold for the DXVK this mod ships:
// D3D9VkInteropTexture::GetVulkanImageInfo (dxvk/src/d3d9/d3d9_interop.cpp)
// only reads the image's handle/create-info, no flush, no CS-thread sync.
void* g_frameSceneDepthSurfaceX64 = nullptr;
void* g_frameSceneColorSurfaceX64 = nullptr;
// 2026-09-27 (ROUND 23): the engine's scene target (R_RENDERTARGET_SCENE, id 2)
// is 4x MSAA in real configs (live log: engineRtId=2 samples=4), and the engine
// resolves it with StretchRect into RESOLVED_SCENE/RESOLVED_POST_SUN -- which are
// never BOUND as render targets, so the SetRenderTarget hook never sees a
// single-sampled scene colour at all. When the last accepted scene-colour bind
// of the frame is multisampled it is recorded here instead (raw, non-owning,
// same per-frame lifetime rules as g_frameSceneColorSurfaceX64), and
// ResolveMsaaSceneColorForDlssX64 resolves it into the proxy-owned
// single-sampled texture below right before evaluate.
void* g_frameSceneColorMsaaSurfaceX64 = nullptr;
void* g_msaaResolveTexture = nullptr; // IDirect3DTexture9*, owned
void* g_msaaResolveSurface = nullptr; // IDirect3DSurface9*, level 0 of the above, owned
uint32_t g_msaaResolveWidth = 0, g_msaaResolveHeight = 0;
DWORD g_msaaResolveFormat = 0;
VkImage g_cachedColorImage = VK_NULL_HANDLE;
VkImageView g_cachedColorView = VK_NULL_HANDLE;

// Real DLSS output buffer (kBufferTypeScalingOutputColor) -- unlike depth
// and color input, the game has no existing resource for this at all (DLSS
// writes a NEW, real image here; nothing currently consumes it). Created
// once, lazily, as a real D3D9 RENDERTARGET texture at the real native/
// display resolution ("internal render scaled percent is what we feed into
// dlss" -- the INPUT side; the OUTPUT side is the real display resolution
// this project's own upscale target is), via DXVK's own interop CreateImage
// with VK_IMAGE_USAGE_STORAGE_BIT (2026-09-27 -- DLSS writes this through a
// storage image; see TagOutputColorResourceForFrame's own comment), then
// resolved to a real Vulkan image/view through the SAME DXVK interop chain
// as depth/color input. Recreated if the real native resolution ever changes.
void* g_outputColorTexture = nullptr; // real IDirect3DTexture9*, we own this
void* g_outputColorSurface = nullptr; // real IDirect3DSurface9*, GetSurfaceLevel(0) of the above
uint32_t g_outputColorWidth = 0;
uint32_t g_outputColorHeight = 0;
VkImage g_cachedOutputColorImage = VK_NULL_HANDLE;
// 2026-09-27: snapshots of the scene-color input and DLSS output exactly as
// tagged by the post-flush re-resolve (see DlssTaggedImageX64), for the
// readback diagnostic. Reset each frame by ClearStreamlineFrameSurfacesX64.
DlssTaggedImageX64 g_taggedInputColorX64;
DlssTaggedImageX64 g_taggedOutputColorX64;
VkImageView g_cachedOutputColorView = VK_NULL_HANDLE;

// Real motion-vectors buffer (kBufferTypeMotionVectors) -- required per the
// real DLSS requirement list even for Stage 1 camera-only reconstruction
// (Constants::cameraMotionIncluded=eFalse tells Streamline to compute
// camera-induced motion itself, but the tag itself is still mandatory --
// vulkan_dlss_pipeline_research.md item 17's own round 3 finding). We own
// this resource entirely, same as the output buffer, but sized to match
// the color INPUT resolution (the internal render-scale resolution), not
// the output/native resolution -- motion vectors are spatially aligned
// with the color being upscaled FROM, not the upscaled result.
void* g_motionVectorsTexture = nullptr;
void* g_motionVectorsSurface = nullptr;
uint32_t g_motionVectorsWidth = 0;
uint32_t g_motionVectorsHeight = 0;
VkImage g_cachedMotionVectorsImage = VK_NULL_HANDLE;
VkImageView g_cachedMotionVectorsView = VK_NULL_HANDLE;

bool ResolveVulkanFunctionsIfNeeded()
{
    if (g_vulkanFunctionsResolved) return true;
    if (g_vulkanFunctionsResolveFailed) return false;

    VkInstance instance = GetDxvkVkInstanceX64();
    VkDevice device = GetDxvkVkDeviceX64();
    if (instance == VK_NULL_HANDLE || device == VK_NULL_HANDLE) return false; // not registered yet, not a failure

    // The real Vulkan loader is already loaded in-process by DXVK itself
    // (winevulkan.dll or vulkan-1.dll, vulkan_loader.cpp's own real search
    // order) -- GetModuleHandleA finds the already-loaded module, never
    // LoadLibraryA's a new one.
    HMODULE vulkanModule = GetModuleHandleA("vulkan-1.dll");
    if (!vulkanModule) vulkanModule = GetModuleHandleA("winevulkan.dll");
    if (!vulkanModule) {
        LogFromController("[x64-streamline-depth] Could not find the real Vulkan loader module "
            "already in-process (neither vulkan-1.dll nor winevulkan.dll) -- resource tagging "
            "will not work this session.");
        g_vulkanFunctionsResolveFailed = true;
        return false;
    }

    auto getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr_local>(
        GetProcAddress(vulkanModule, "vkGetInstanceProcAddr"));
    if (!getInstanceProcAddr) {
        LogFromController("[x64-streamline-depth] vkGetInstanceProcAddr export not found in the "
            "real Vulkan loader module -- resource tagging will not work this session.");
        g_vulkanFunctionsResolveFailed = true;
        return false;
    }

    auto getDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr_local>(
        getInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
    if (!getDeviceProcAddr) {
        LogFromController("[x64-streamline-depth] vkGetDeviceProcAddr resolve FAILED -- resource "
            "tagging will not work this session.");
        g_vulkanFunctionsResolveFailed = true;
        return false;
    }

    g_vkCreateImageView = reinterpret_cast<PFN_vkCreateImageView>(
        getDeviceProcAddr(device, "vkCreateImageView"));
    g_vkDestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(
        getDeviceProcAddr(device, "vkDestroyImageView"));
    if (!g_vkCreateImageView || !g_vkDestroyImageView) {
        LogFromController("[x64-streamline-depth] vkCreateImageView/vkDestroyImageView resolve "
            "FAILED -- resource tagging will not work this session.");
        g_vulkanFunctionsResolveFailed = true;
        return false;
    }

    g_vulkanFunctionsResolved = true;
    LogFromController("[x64-streamline-depth] Real Vulkan functions resolved (vkCreateImageView/"
        "vkDestroyImageView) -- resource tagging can proceed.");
    return true;
}

// Generic per-image view cache/create -- shared by both depth (DEPTH_BIT)
// and color (COLOR_BIT) resolution below. cachedImage/cachedView are the
// caller's own cache slots (passed by reference so depth and color stay
// fully independent); logPrefix/label are just for readable log lines.
VkImageView GetOrCreateViewX64(VkImage image, VkFormat format, uint32_t mipLevels, uint32_t arrayLayers,
    VkImageAspectFlags aspect, VkImage& cachedImage, VkImageView& cachedView,
    const char* logPrefix, const char* label)
{
    if (image == cachedImage && cachedView != VK_NULL_HANDLE) {
        return cachedView;
    }

    VkDevice device = GetDxvkVkDeviceX64();
    if (device == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    if (cachedView != VK_NULL_HANDLE) {
        g_vkDestroyImageView(device, cachedView, nullptr);
        cachedView = VK_NULL_HANDLE;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = arrayLayers;

    VkImageView view = VK_NULL_HANDLE;
    VkResult vr = g_vkCreateImageView(device, &viewInfo, nullptr, &view);
    if (vr != VK_SUCCESS) {
        char buf[220];
        sprintf_s(buf, "%s vkCreateImageView FAILED (VkResult=%d) for %s image 0x%llx.",
            logPrefix, static_cast<int>(vr), label, reinterpret_cast<unsigned long long>(image));
        LogFromController(buf);
        return VK_NULL_HANDLE;
    }

    cachedImage = image;
    cachedView = view;
    char buf[250];
    sprintf_s(buf, "%s Created a real %s VkImageView (0x%llx) for image 0x%llx (format=%d).",
        logPrefix, label, reinterpret_cast<unsigned long long>(view),
        reinterpret_cast<unsigned long long>(image), static_cast<int>(format));
    LogFromController(buf);
    return view;
}

void TagDepthResourceForFrame(void* depthSurface)
{
    ++g_resolveAttemptCount;
    bool heartbeat = g_resolveAttemptCount <= 5 || (g_resolveAttemptCount % 5000) == 0;

    VkImage image = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;

    IUnknown* depthSurfaceUnknown = reinterpret_cast<IUnknown*>(depthSurface);
    ID3D9VkInteropTextureX64* interopTexture = nullptr;
    HRESULT hr = depthSurfaceUnknown->QueryInterface(IID_ID3D9VkInteropTexture_X64,
        reinterpret_cast<void**>(&interopTexture));
    if (FAILED(hr) || !interopTexture) {
        if (heartbeat) {
            char buf[200];
            sprintf_s(buf, "[x64-streamline-depth] QueryInterface for ID3D9VkInteropTexture FAILED "
                "(hr=0x%08lX) -- this device isn't DXVK, or the interop interface changed.", hr);
            LogFromController(buf);
        }
        return;
    }

    hr = interopTexture->GetVulkanImageInfo(&image, &layout, &info);
    interopTexture->Release();

    if (FAILED(hr) || image == VK_NULL_HANDLE) {
        if (heartbeat) {
            char buf[200];
            sprintf_s(buf, "[x64-streamline-depth] GetVulkanImageInfo FAILED (hr=0x%08lX) -- no "
                "real Vulkan image behind this depth-stencil surface.", hr);
            LogFromController(buf);
        }
        return;
    }

    // 2026-09-27 (ROUND 23): DLSS needs single-sampled depth. The engine binds
    // a 4x-MSAA depth-stencil with its multisampled scene target and a shared
    // 1x depth-stencil (created at the same requested scene size,
    // FUN_1401d44f0) with its single-sampled targets; keep the latest 1x one.
    if (info.samples != VK_SAMPLE_COUNT_1_BIT) {
        if (heartbeat) {
            char buf[160];
            sprintf_s(buf, "[x64-streamline-depth] multisampled depth (samples=%d) not recorded as DLSS "
                "depth -- keeping the frame's single-sampled depth.", static_cast<int>(info.samples));
            LogFromController(buf);
        }
        return;
    }
    g_frameSceneDepthSurfaceX64 = depthSurface;

    if (heartbeat) {
        char buf[350];
        sprintf_s(buf, "[x64-streamline-depth] attempt=%lld image=0x%llx layout=%d format=%d "
            "extent=%ux%ux%u mipLevels=%u arrayLayers=%u samples=%d usage=0x%X",
            g_resolveAttemptCount, reinterpret_cast<unsigned long long>(image), static_cast<int>(layout),
            static_cast<int>(info.format), info.extent.width, info.extent.height, info.extent.depth,
            info.mipLevels, info.arrayLayers, static_cast<int>(info.samples), info.usage);
        LogFromController(buf);
    }

    if (!ResolveVulkanFunctionsIfNeeded()) return;

    VkImageView view = GetOrCreateViewX64(image, info.format, info.mipLevels, info.arrayLayers,
        VK_IMAGE_ASPECT_DEPTH_BIT, g_cachedDepthImage, g_cachedDepthView,
        "[x64-streamline-depth]", "depth-only");
    if (view == VK_NULL_HANDLE) return;

    // Real sl::Resource -- native=VkImage, view=the real depth-only VkImageView
    // just created/reused, state=the real layout GetVulkanImageInfo reported.
    // Width/height/format/mips/arrays/usage all real, read straight from the
    // real VkImageCreateInfo DXVK itself returned.
    sl::Resource depthResource(sl::ResourceType::eTex2d, reinterpret_cast<void*>(image),
        /*mem*/ nullptr, reinterpret_cast<void*>(view), static_cast<uint32_t>(layout));
    depthResource.width = info.extent.width;
    depthResource.height = info.extent.height;
    depthResource.nativeFormat = static_cast<uint32_t>(info.format);
    depthResource.mipLevels = info.mipLevels;
    depthResource.arrayLayers = info.arrayLayers;
    depthResource.usage = info.usage;

    // eValidUntilPresent -- this project never modifies/destroys this image
    // itself (DXVK owns it), and it stays bound at least for the rest of
    // this frame's real 3D scene rendering, so it's valid to tag as
    // "unchanged until Present" and pass a null command buffer per
    // slSetTagForFrame's own documented allowance.
    sl::ResourceTag depthTag(&depthResource, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent);
    StreamlineSetTagForFrameX64(&depthTag, 1);
}

// Real primary color render target -- kBufferTypeScalingInputColor (the
// real, low-res-relative-to-output color buffer DLSS reads and upscales
// from). Same interop chain as depth, COLOR aspect instead of DEPTH,
// SetRenderTarget(0, ...) instead of SetDepthStencilSurface.
// 2026-09-27 (ROUND 22): engine render targets that can hold the finished
// scene colour. IDs from the binary's own R_RENDERTARGET_* name table
// (renderer_end_to_end.md §8.3). Everything else -- SAVED_SCREEN 0, FLOAT_Z 5
// (R32F depth), PINGPONG_0/1 6/7 and POST_EFFECT_0/1 8/9 (glow/bloom/blur
// intermediates), shadow maps 10/11, SSAO 12-14 -- is never a valid DLSS
// scene-colour input.
bool IsEngineSceneColorTargetIdX64(int id)
{
    return id == 1 /* FRAME_BUFFER */ || id == 2 /* SCENE */ ||
        id == 3 /* RESOLVED_POST_SUN */ || id == 4 /* RESOLVED_SCENE */;
}

void TagColorResourceForFrame(void* renderTarget)
{
    ++g_resolveColorAttemptCount;
    bool heartbeat = g_resolveColorAttemptCount <= 5 || (g_resolveColorAttemptCount % 5000) == 0;

    // REAL FIX, 2026-09-27 (ROUND 22) -- the above-100% black viewport. The
    // size filter below used to be the ONLY filter, and the LAST size-matching
    // SetRenderTarget(0) of the frame won. The engine creates FLOAT_Z (5),
    // PINGPONG_0/1 (6/7) and POST_EFFECT_0/1 (8/9) at the same requested scene
    // resolution as SCENE (FUN_1401d44f0, from 0x141888670 -- exactly what
    // InternalRenderScalePercent rewrites), and post-FX binds the ping-pong/
    // post-effect targets AFTER the scene for its glow/bloom/film-blur passes.
    // So whenever those passes ran, DLSS was fed a mostly-black blur/bloom
    // intermediate as its "scene colour", DLAA processed it faithfully, and the
    // composite painted it over the frame. At exactly 100% the native
    // FRAME_BUFFER (1) also matches the size and post-FX re-binds it after
    // every pass, so it always won there -- which is why 100% looked fine.
    // Now the engine's own target ID (published by Hook_RenderViewSelectDiag
    // around FUN_1401dfd80, the engine's only SetRenderTarget caller) decides:
    // only scene-colour targets are accepted, and binds from outside the engine
    // (this proxy's own passes) are ignored. Falls back to the legacy
    // size-only rule if that hook isn't live this session.
    int engineRtId = -1;
    if (TryGetEngineRenderTargetSelectX64(&engineRtId) && !IsEngineSceneColorTargetIdX64(engineRtId)) {
        static long long s_idRejectCount = 0;
        ++s_idRejectCount;
        if (s_idRejectCount <= 10 || (s_idRejectCount % 5000) == 0) {
            char buf[200];
            sprintf_s(buf, "[x64-streamline-color] engine target id %d is not a scene-colour target -- "
                "not tagged as DLSS input (reject #%lld).", engineRtId, s_idRejectCount);
            LogFromController(buf);
        }
        return;
    }

    // REAL, LIVE, SEVERE BUG FIXED, 2026-09-24: this function used to call
    // QueryInterface+GetVulkanImageInfo UNCONDITIONALLY, before the
    // resolution-match filter below even ran -- and DXVK's own doc comment on
    // GetVulkanImageInfo says it "flushes outstanding commands" to report the
    // post-flush layout. SetRenderTarget(0, ...) fires many times per real
    // frame (shadow maps, post-process, UI -- ~9+ times/frame per this
    // project's own [x64-renderview-select-diag] live data), so this was
    // forcing a real GPU command-buffer flush that many times EVERY frame,
    // for render targets we almost always immediately discarded anyway --
    // live-reported as a sustained ~120fps -> ~12fps regression, invisible to
    // this project's own CPU-side hook timers (frame_benchmark.cpp's
    // ourOwnTotalMs, and this file's own per-call QPC wrapper in
    // overlay_hud.cpp) since the real cost is a forced GPU pipeline stall,
    // not CPU-side hook-body work. Fixed by checking the resolution FIRST via
    // a cheap, pure-D3D9 IDirect3DSurface9::GetDesc call (no Vulkan interop,
    // no flush) -- the expensive QueryInterface/GetVulkanImageInfo path below
    // now only ever runs for the one render target per frame that actually
    // matches the real internal render-scale resolution.
    void** rtVtblEarly = *reinterpret_cast<void***>(renderTarget);
    auto getDescEarly = reinterpret_cast<SurfaceGetDescFnX64>(rtVtblEarly[kSurfaceGetDescVtableIndexX64]);
    SurfaceDescX64 earlyDesc{};
    uint32_t earlyExpectedWidth = 0, earlyExpectedHeight = 0;
    bool haveEarlyExpected = ComputeExpectedInternalResolutionX64(earlyExpectedWidth, earlyExpectedHeight);
    if (SUCCEEDED(getDescEarly(renderTarget, &earlyDesc)) && haveEarlyExpected &&
        (earlyDesc.Width != earlyExpectedWidth || earlyDesc.Height != earlyExpectedHeight)) {
        if (heartbeat) {
            char buf[220];
            sprintf_s(buf, "[x64-streamline-color] attempt=%lld pre-filter reject: "
                "%ux%u != expected %ux%u (no Vulkan interop call made)",
                g_resolveColorAttemptCount, earlyDesc.Width, earlyDesc.Height,
                earlyExpectedWidth, earlyExpectedHeight);
            LogFromController(buf);
        }
        return;
    }

    IUnknown* renderTargetUnknown = reinterpret_cast<IUnknown*>(renderTarget);
    ID3D9VkInteropTextureX64* interopTexture = nullptr;
    HRESULT hr = renderTargetUnknown->QueryInterface(IID_ID3D9VkInteropTexture_X64,
        reinterpret_cast<void**>(&interopTexture));
    if (FAILED(hr) || !interopTexture) {
        if (heartbeat) {
            char buf[200];
            sprintf_s(buf, "[x64-streamline-color] QueryInterface for ID3D9VkInteropTexture FAILED "
                "(hr=0x%08lX).", hr);
            LogFromController(buf);
        }
        return;
    }

    VkImage image = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    hr = interopTexture->GetVulkanImageInfo(&image, &layout, &info);
    interopTexture->Release();

    if (FAILED(hr) || image == VK_NULL_HANDLE) {
        if (heartbeat) {
            char buf[200];
            sprintf_s(buf, "[x64-streamline-color] GetVulkanImageInfo FAILED (hr=0x%08lX) -- no "
                "real Vulkan image behind this render target.", hr);
            LogFromController(buf);
        }
        return;
    }

    // Real disambiguation filter -- SetRenderTarget(0, ...) fires for many
    // real render targets per frame (shadow maps, post-process, UI), not
    // just the final 3D scene color. Only the target at the real internal
    // render-scale resolution ("internal render scaled percent is what we
    // feed into dlss", direct instruction) is the one DLSS actually wants.
    uint32_t expectedWidth = 0, expectedHeight = 0;
    bool haveExpected = ComputeExpectedInternalResolutionX64(expectedWidth, expectedHeight);
    bool resolutionMatches = haveExpected &&
        info.extent.width == expectedWidth && info.extent.height == expectedHeight;

    if (heartbeat) {
        char buf[512]; // widened for engineRtId, 2026-09-27 (ROUND 22) -- worst case ~375
        sprintf_s(buf, "[x64-streamline-color] attempt=%lld image=0x%llx layout=%d format=%d "
            "extent=%ux%ux%u mipLevels=%u arrayLayers=%u samples=%d usage=0x%X expected=%ux%u match=%d "
            "engineRtId=%d",
            g_resolveColorAttemptCount, reinterpret_cast<unsigned long long>(image), static_cast<int>(layout),
            static_cast<int>(info.format), info.extent.width, info.extent.height, info.extent.depth,
            info.mipLevels, info.arrayLayers, static_cast<int>(info.samples), info.usage,
            expectedWidth, expectedHeight, resolutionMatches ? 1 : 0, engineRtId);
        LogFromController(buf);
    }

    if (!resolutionMatches) return; // a real, different render target this frame -- not the scene color
    if (info.samples != VK_SAMPLE_COUNT_1_BIT) {
        // 2026-09-27 (ROUND 23): DLSS cannot consume a multisampled image, and
        // the engine's own resolved copies are never bound as render targets
        // (see g_frameSceneColorMsaaSurfaceX64). Record it as this frame's
        // scene colour SOURCE; ResolveMsaaSceneColorForDlssX64 resolves it into
        // a proxy-owned single-sampled texture right before evaluate, and that
        // texture is what gets tagged.
        g_frameSceneColorMsaaSurfaceX64 = renderTarget;
        g_frameSceneColorSurfaceX64 = nullptr;
        if (heartbeat) {
            char buf[256];
            sprintf_s(buf, "[x64-streamline-color] engine target id %d is multisampled (samples=%d) -- "
                "recorded as the scene-colour source; it will be resolved before DLSS evaluates.",
                engineRtId, static_cast<int>(info.samples));
            LogFromController(buf);
        }
        return;
    }

    g_frameSceneColorSurfaceX64 = renderTarget;
    g_frameSceneColorMsaaSurfaceX64 = nullptr; // a later single-sampled scene bind supersedes it

    if (!ResolveVulkanFunctionsIfNeeded()) return;

    VkImageView view = GetOrCreateViewX64(image, info.format, info.mipLevels, info.arrayLayers,
        VK_IMAGE_ASPECT_COLOR_BIT, g_cachedColorImage, g_cachedColorView,
        "[x64-streamline-color]", "color");
    if (view == VK_NULL_HANDLE) return;

    sl::Resource colorResource(sl::ResourceType::eTex2d, reinterpret_cast<void*>(image),
        /*mem*/ nullptr, reinterpret_cast<void*>(view), static_cast<uint32_t>(layout));
    colorResource.width = info.extent.width;
    colorResource.height = info.extent.height;
    colorResource.nativeFormat = static_cast<uint32_t>(info.format);
    colorResource.mipLevels = info.mipLevels;
    colorResource.arrayLayers = info.arrayLayers;
    colorResource.usage = info.usage;

    sl::ResourceTag colorTag(&colorResource, sl::kBufferTypeScalingInputColor,
        sl::ResourceLifecycle::eValidUntilPresent);
    StreamlineSetTagForFrameX64(&colorTag, 1);
}

// Real DLSS output buffer (kBufferTypeScalingOutputColor) -- see this file's
// own top comment and the g_outputColorTexture block's comment above for the
// full design ("internal render scaled percent is what we feed into dlss" --
// the input side; this is the real output side, the real native/display
// resolution). Lazily creates a real D3D9 texture the first time it's
// needed, recreating it if the real native resolution ever changes (e.g. a
// real display-mode change mid-session). Called once per real frame,
// unconditionally -- unlike depth/color input, this isn't hooked off a
// real game call, since the game never touches this resource at all; WE
// own its entire lifecycle.
void TagOutputColorResourceForFrame()
{
    static long long s_attemptCount = 0;
    ++s_attemptCount;
    bool heartbeat = s_attemptCount <= 5 || (s_attemptCount % 5000) == 0;

    void* device = GetLastKnownRenderDevice();
    if (!device) return;

    int nativeW = 1920, nativeH = 1080;
    GetRealScreenSize(device, nativeW, nativeH);
    if (nativeW <= 0 || nativeH <= 0) return;

    // 2026-09-27: the real DLSS output resolution is NOT always the display
    // resolution -- during the above-output/DLAA override (streamline_evaluate_x64.cpp's
    // own g_dlssEffectiveModeX64/g_dlssEffectiveOutputWidthX64 comment), Streamline is
    // told the output IS the render-scale input resolution (DLAA, no resize), so this
    // texture must match THAT size, not the display's. Falls back to the real display
    // resolution when the override hasn't computed a value yet (this session's very
    // first tick, before EvaluateStreamlineDlssX64 has run even once) or isn't active.
    uint32_t targetWidth = static_cast<uint32_t>(nativeW);
    uint32_t targetHeight = static_cast<uint32_t>(nativeH);
    GetDlssEffectiveOutputResolutionX64(targetWidth, targetHeight); // no-op (keeps the
        // display-resolution fallback above) if it returns false -- see its own comment.

    // (Re)create if this is the first attempt, or the real target resolution
    // changed since the texture was created.
    if (!g_outputColorTexture || g_outputColorWidth != targetWidth ||
        g_outputColorHeight != targetHeight) {
        if (g_outputColorSurface) {
            reinterpret_cast<IUnknown*>(g_outputColorSurface)->Release();
            g_outputColorSurface = nullptr;
        }
        if (g_outputColorTexture) {
            reinterpret_cast<IUnknown*>(g_outputColorTexture)->Release();
            g_outputColorTexture = nullptr;
        }
        // A stale cached view/image would now point at a released image --
        // clear them too so GetOrCreateViewX64 is forced to create fresh
        // ones against the new texture rather than reuse a dangling handle.
        g_cachedOutputColorImage = VK_NULL_HANDLE;
        g_cachedOutputColorView = VK_NULL_HANDLE;

        // REAL FIX, 2026-09-27 -- the DLSS evaluate GPU hang/device loss
        // on level load (vulkan_dlss_pipeline_research.md item 17, ROUND 18).
        // This texture used to be created via plain IDirect3DDevice9::
        // CreateTexture(RENDERTARGET, A8R8G8B8). Read against DXVK's own
        // source (d3d9_common_texture.cpp CreatePrimaryImage/OptimizeLayout):
        // that yields a VkImage with usage TRANSFER_SRC|TRANSFER_DST|SAMPLED|
        // COLOR_ATTACHMENT (0x17) -- NO VK_IMAGE_USAGE_STORAGE_BIT -- held in
        // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL. DLSS writes its output
        // with a compute-shader storage-image write, which the Vulkan spec
        // forbids on an image without STORAGE usage (undefined behavior at
        // the GPU level -- a real, sufficient explanation for the first
        // evaluate's fence never signaling and the device then being lost,
        // independent of any cold-start timing). DXVK's own interop
        // CreateImage is the real, supported way to add Vulkan usage flags
        // to a D3D9 texture: ImageUsage is OR'd straight into the VkImage's
        // usage, and DXVK's OptimizeLayout then keeps a STORAGE image in
        // VK_IMAGE_LAYOUT_GENERAL -- the layout storage writes require.
        // Format is D3DFMT_A8B8G8R8 (VK_FORMAT_R8G8B8A8_UNORM, whose
        // STORAGE_IMAGE support is mandatory per the Vulkan spec) rather
        // than A8R8G8B8 (B8G8R8A8_UNORM, whose storage support is optional);
        // the composite StretchRect into the A8R8G8B8 scene target takes
        // DXVK's own blit path for the format change (d3d9_device.cpp
        // StretchRect, !AreFormatsSimilar -> vkCmdBlitImage), a standard,
        // supported conversion.
        IUnknown* deviceUnknown = reinterpret_cast<IUnknown*>(device);
        ID3D9VkInteropDeviceX64* interopDevice = nullptr;
        HRESULT hr = deviceUnknown->QueryInterface(IID_ID3D9VkInteropDevice_X64,
            reinterpret_cast<void**>(&interopDevice));
        if (FAILED(hr) || !interopDevice) {
            char buf[200];
            sprintf_s(buf, "[x64-streamline-output] QueryInterface for ID3D9VkInteropDevice FAILED "
                "(hr=0x%08lX) -- cannot create a storage-capable DLSS output buffer.", hr);
            LogFromController(buf);
            return;
        }

        D3D9VkExtImageDescX64 desc{};
        desc.Type = kD3DRTYPE_TEXTURE;
        desc.Width = targetWidth;
        desc.Height = targetHeight;
        desc.Depth = 1;
        desc.MipLevels = 1;
        desc.Usage = kD3DUSAGE_RENDERTARGET; // keeps it a valid StretchRect source/RT
        desc.Format = kD3DFMT_A8B8G8R8;
        desc.Pool = kD3DPOOL_DEFAULT;
        desc.MultiSample = 0; // D3DMULTISAMPLE_NONE -- textures can't be multisampled
        desc.MultiSampleQuality = 0;
        desc.Discard = false;
        desc.IsAttachmentOnly = false; // DXVK adds SAMPLED
        desc.IsLockable = false;
        desc.ImageUsage = VK_IMAGE_USAGE_STORAGE_BIT;

        hr = interopDevice->CreateImage(&desc, &g_outputColorTexture);
        interopDevice->Release();
        if (FAILED(hr) || !g_outputColorTexture) {
            char buf[250];
            sprintf_s(buf, "[x64-streamline-output] DXVK CreateImage FAILED (hr=0x%08lX) for a real "
                "%ux%u storage-capable DLSS output buffer -- DLSS will not evaluate.",
                hr, targetWidth, targetHeight);
            LogFromController(buf);
            g_outputColorTexture = nullptr;
            return;
        }

        void** texVtbl = *reinterpret_cast<void***>(g_outputColorTexture);
        auto getSurfaceLevel = reinterpret_cast<GetSurfaceLevelFn>(texVtbl[kGetSurfaceLevelVtableIndex]);
        hr = getSurfaceLevel(g_outputColorTexture, 0, &g_outputColorSurface);
        if (FAILED(hr) || !g_outputColorSurface) {
            char buf[200];
            sprintf_s(buf, "[x64-streamline-output] GetSurfaceLevel FAILED (hr=0x%08lX) on the "
                "real DLSS output texture.", hr);
            LogFromController(buf);
            reinterpret_cast<IUnknown*>(g_outputColorTexture)->Release();
            g_outputColorTexture = nullptr;
            g_outputColorSurface = nullptr;
            return;
        }

        g_outputColorWidth = targetWidth;
        g_outputColorHeight = targetHeight;
        char buf[200];
        sprintf_s(buf, "[x64-streamline-output] Created a real %ux%u DLSS output texture+surface.",
            g_outputColorWidth, g_outputColorHeight);
        LogFromController(buf);
    }

    if (!g_outputColorSurface) return;

    IUnknown* surfaceUnknown = reinterpret_cast<IUnknown*>(g_outputColorSurface);
    ID3D9VkInteropTextureX64* interopTexture = nullptr;
    HRESULT hr = surfaceUnknown->QueryInterface(IID_ID3D9VkInteropTexture_X64,
        reinterpret_cast<void**>(&interopTexture));
    if (FAILED(hr) || !interopTexture) {
        if (heartbeat) {
            char buf[200];
            sprintf_s(buf, "[x64-streamline-output] QueryInterface for ID3D9VkInteropTexture "
                "FAILED (hr=0x%08lX) on our own DLSS output texture.", hr);
            LogFromController(buf);
        }
        return;
    }

    VkImage image = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    hr = interopTexture->GetVulkanImageInfo(&image, &layout, &info);
    interopTexture->Release();

    if (FAILED(hr) || image == VK_NULL_HANDLE) {
        if (heartbeat) {
            char buf[200];
            sprintf_s(buf, "[x64-streamline-output] GetVulkanImageInfo FAILED (hr=0x%08lX) on our "
                "own DLSS output texture.", hr);
            LogFromController(buf);
        }
        return;
    }

    if (heartbeat) {
        char buf[350];
        sprintf_s(buf, "[x64-streamline-output] attempt=%lld image=0x%llx layout=%d format=%d "
            "extent=%ux%ux%u mipLevels=%u arrayLayers=%u usage=0x%X",
            s_attemptCount, reinterpret_cast<unsigned long long>(image), static_cast<int>(layout),
            static_cast<int>(info.format), info.extent.width, info.extent.height, info.extent.depth,
            info.mipLevels, info.arrayLayers, info.usage);
        LogFromController(buf);
    }

    if (!ResolveVulkanFunctionsIfNeeded()) return;

    VkImageView view = GetOrCreateViewX64(image, info.format, info.mipLevels, info.arrayLayers,
        VK_IMAGE_ASPECT_COLOR_BIT, g_cachedOutputColorImage, g_cachedOutputColorView,
        "[x64-streamline-output]", "output-color");
    if (view == VK_NULL_HANDLE) return;

    sl::Resource outputResource(sl::ResourceType::eTex2d, reinterpret_cast<void*>(image),
        /*mem*/ nullptr, reinterpret_cast<void*>(view), static_cast<uint32_t>(layout));
    outputResource.width = info.extent.width;
    outputResource.height = info.extent.height;
    outputResource.nativeFormat = static_cast<uint32_t>(info.format);
    outputResource.mipLevels = info.mipLevels;
    outputResource.arrayLayers = info.arrayLayers;
    outputResource.usage = info.usage;

    // eValidUntilPresent -- we own this texture for its entire lifetime
    // (only released on resolution change or DLL unload), so it's always
    // valid for at least the rest of the current frame.
    sl::ResourceTag outputTag(&outputResource, sl::kBufferTypeScalingOutputColor,
        sl::ResourceLifecycle::eValidUntilPresent);
    StreamlineSetTagForFrameX64(&outputTag, 1);
}

// Real motion-vectors buffer (kBufferTypeMotionVectors) -- see the
// g_motionVectorsTexture block's own comment above for the full design.
// Same lazy-create/resize-on-change pattern as the output color buffer,
// but sized to ComputeExpectedInternalResolutionX64() (the color INPUT
// resolution) rather than the native/output resolution.
//
// CLOSED, 2026-09-24: this texture's real initial content used to be
// D3D9-undefined (CreateTexture with no initial data). Now that
// slSetConstants is being wired (streamline_camera_x64.cpp) and will
// actually set Constants::motionVectorsInvalidValue, undefined content is a
// real correctness problem, not a harmless gap -- Streamline needs to be
// able to tell "no per-object motion here" from "real motion", and it can
// only do that if every never-written texel reliably holds one known
// sentinel value. Fixed with a real ColorFill(surface, nullptr, 0) right
// after (re)creation, below -- zeros every channel once, at (re)create time
// only, not per-frame. motionVectorsInvalidValue is set to 0.0f to match.
void TagMotionVectorsResourceForFrame()
{
    static long long s_attemptCount = 0;
    ++s_attemptCount;
    bool heartbeat = s_attemptCount <= 5 || (s_attemptCount % 5000) == 0;

    void* device = GetLastKnownRenderDevice();
    if (!device) return;

    uint32_t expectedWidth = 0, expectedHeight = 0;
    if (!ComputeExpectedInternalResolutionX64(expectedWidth, expectedHeight)) return;

    if (!g_motionVectorsTexture || g_motionVectorsWidth != expectedWidth ||
        g_motionVectorsHeight != expectedHeight) {
        if (g_motionVectorsSurface) {
            reinterpret_cast<IUnknown*>(g_motionVectorsSurface)->Release();
            g_motionVectorsSurface = nullptr;
        }
        if (g_motionVectorsTexture) {
            reinterpret_cast<IUnknown*>(g_motionVectorsTexture)->Release();
            g_motionVectorsTexture = nullptr;
        }
        g_cachedMotionVectorsImage = VK_NULL_HANDLE;
        g_cachedMotionVectorsView = VK_NULL_HANDLE;

        void** deviceVtbl = *reinterpret_cast<void***>(device);
        auto createTexture = reinterpret_cast<CreateTextureFn>(deviceVtbl[kCreateTextureVtableIndex]);
        HRESULT hr = createTexture(device, expectedWidth, expectedHeight, 1,
            kD3DUSAGE_RENDERTARGET, kD3DFMT_G16R16F, kD3DPOOL_DEFAULT, &g_motionVectorsTexture, nullptr);
        if (FAILED(hr) || !g_motionVectorsTexture) {
            char buf[220];
            sprintf_s(buf, "[x64-streamline-mvec] CreateTexture FAILED (hr=0x%08lX) for a real "
                "%ux%u G16R16F motion-vectors buffer.", hr, expectedWidth, expectedHeight);
            LogFromController(buf);
            g_motionVectorsTexture = nullptr;
            return;
        }

        void** texVtbl = *reinterpret_cast<void***>(g_motionVectorsTexture);
        auto getSurfaceLevel = reinterpret_cast<GetSurfaceLevelFn>(texVtbl[kGetSurfaceLevelVtableIndex]);
        hr = getSurfaceLevel(g_motionVectorsTexture, 0, &g_motionVectorsSurface);
        if (FAILED(hr) || !g_motionVectorsSurface) {
            char buf[200];
            sprintf_s(buf, "[x64-streamline-mvec] GetSurfaceLevel FAILED (hr=0x%08lX) on the real "
                "motion-vectors texture.", hr);
            LogFromController(buf);
            reinterpret_cast<IUnknown*>(g_motionVectorsTexture)->Release();
            g_motionVectorsTexture = nullptr;
            g_motionVectorsSurface = nullptr;
            return;
        }

        g_motionVectorsWidth = expectedWidth;
        g_motionVectorsHeight = expectedHeight;

        auto colorFill = reinterpret_cast<ColorFillFn>(deviceVtbl[kColorFillVtableIndex]);
        HRESULT fillHr = colorFill(device, g_motionVectorsSurface, nullptr, 0);
        char buf[220];
        sprintf_s(buf, "[x64-streamline-mvec] Created a real %ux%u G16R16F motion-vectors "
            "texture+surface, ColorFill zero-init hr=0x%08lX.", g_motionVectorsWidth,
            g_motionVectorsHeight, fillHr);
        LogFromController(buf);
    }

    if (!g_motionVectorsSurface) return;

    IUnknown* surfaceUnknown = reinterpret_cast<IUnknown*>(g_motionVectorsSurface);
    ID3D9VkInteropTextureX64* interopTexture = nullptr;
    HRESULT hr = surfaceUnknown->QueryInterface(IID_ID3D9VkInteropTexture_X64,
        reinterpret_cast<void**>(&interopTexture));
    if (FAILED(hr) || !interopTexture) {
        if (heartbeat) {
            char buf[200];
            sprintf_s(buf, "[x64-streamline-mvec] QueryInterface for ID3D9VkInteropTexture FAILED "
                "(hr=0x%08lX) on our own motion-vectors texture.", hr);
            LogFromController(buf);
        }
        return;
    }

    VkImage image = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    hr = interopTexture->GetVulkanImageInfo(&image, &layout, &info);
    interopTexture->Release();

    if (FAILED(hr) || image == VK_NULL_HANDLE) {
        if (heartbeat) {
            char buf[200];
            sprintf_s(buf, "[x64-streamline-mvec] GetVulkanImageInfo FAILED (hr=0x%08lX) on our "
                "own motion-vectors texture.", hr);
            LogFromController(buf);
        }
        return;
    }

    if (heartbeat) {
        char buf[350];
        sprintf_s(buf, "[x64-streamline-mvec] attempt=%lld image=0x%llx layout=%d format=%d "
            "extent=%ux%ux%u mipLevels=%u arrayLayers=%u usage=0x%X",
            s_attemptCount, reinterpret_cast<unsigned long long>(image), static_cast<int>(layout),
            static_cast<int>(info.format), info.extent.width, info.extent.height, info.extent.depth,
            info.mipLevels, info.arrayLayers, info.usage);
        LogFromController(buf);
    }

    if (!ResolveVulkanFunctionsIfNeeded()) return;

    VkImageView view = GetOrCreateViewX64(image, info.format, info.mipLevels, info.arrayLayers,
        VK_IMAGE_ASPECT_COLOR_BIT, g_cachedMotionVectorsImage, g_cachedMotionVectorsView,
        "[x64-streamline-mvec]", "motion-vectors");
    if (view == VK_NULL_HANDLE) return;

    sl::Resource mvecResource(sl::ResourceType::eTex2d, reinterpret_cast<void*>(image),
        /*mem*/ nullptr, reinterpret_cast<void*>(view), static_cast<uint32_t>(layout));
    mvecResource.width = info.extent.width;
    mvecResource.height = info.extent.height;
    mvecResource.nativeFormat = static_cast<uint32_t>(info.format);
    mvecResource.mipLevels = info.mipLevels;
    mvecResource.arrayLayers = info.arrayLayers;
    mvecResource.usage = info.usage;

    sl::ResourceTag mvecTag(&mvecResource, sl::kBufferTypeMotionVectors,
        sl::ResourceLifecycle::eValidUntilPresent);
    StreamlineSetTagForFrameX64(&mvecTag, 1);
}

// 2026-09-27 (ROUND 19): resolves a D3D9 surface to its CURRENT backing
// VkImage and (re-)tags it for this frame -- the shared step behind
// RetagStreamlineInputsAfterFlushX64 below. Same QueryInterface/
// GetVulkanImageInfo/GetOrCreateViewX64/slSetTagForFrame chain every
// per-buffer tag function above already uses; returns false (and tags
// nothing) on any failure so the caller can skip evaluate instead of letting
// DLSS read a stale tag. Tagging the same buffer type more than once in a
// frame is already the live-proven norm here (the depth hook tags on every
// SetDepthStencilSurface fire) -- the last tag before evaluate wins.
bool ResolveAndTagCurrentImageX64(void* surface, sl::BufferType bufferType, VkImageAspectFlags aspect,
    VkImage& cachedImage, VkImageView& cachedView, const char* logPrefix, const char* label)
{
    static long long s_failureCount = 0;
    static long long s_imageChangeCount = 0;

    IUnknown* surfaceUnknown = reinterpret_cast<IUnknown*>(surface);
    ID3D9VkInteropTextureX64* interopTexture = nullptr;
    HRESULT hr = surfaceUnknown->QueryInterface(IID_ID3D9VkInteropTexture_X64,
        reinterpret_cast<void**>(&interopTexture));
    VkImage image = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    if (SUCCEEDED(hr) && interopTexture) {
        hr = interopTexture->GetVulkanImageInfo(&image, &layout, &info);
        interopTexture->Release();
    }
    if (FAILED(hr) || image == VK_NULL_HANDLE) {
        ++s_failureCount;
        if (s_failureCount <= 10 || (s_failureCount % 1000) == 0) {
            char buf[250];
            sprintf_s(buf, "%s post-flush re-resolve FAILED for %s (hr=0x%08lX, count=%lld) -- "
                "skipping DLSS evaluate this frame rather than use a stale tag.",
                logPrefix, label, hr, s_failureCount);
            LogFromController(buf);
        }
        return false;
    }

    // Live evidence for the relocation fix: logs whenever the image this
    // buffer resolves to differs from the one its view cache last held. For
    // color/output/motion vectors that means DXVK relocated it (or the game
    // recreated the target); depth also changes when the game alternates its
    // real 4x-MSAA and resolved 1x depth surfaces, so depth lines alone are
    // not proof of relocation.
    if (cachedImage != VK_NULL_HANDLE && image != cachedImage) {
        ++s_imageChangeCount;
        if (s_imageChangeCount <= 20 || (s_imageChangeCount % 1000) == 0) {
            char buf[250];
            sprintf_s(buf, "%s post-flush re-resolve: %s image changed 0x%llx -> 0x%llx (count=%lld).",
                logPrefix, label, reinterpret_cast<unsigned long long>(cachedImage),
                reinterpret_cast<unsigned long long>(image), s_imageChangeCount);
            LogFromController(buf);
        }
    }

    VkImageView view = GetOrCreateViewX64(image, info.format, info.mipLevels, info.arrayLayers,
        aspect, cachedImage, cachedView, logPrefix, label);
    if (view == VK_NULL_HANDLE) return false;

    sl::Resource resource(sl::ResourceType::eTex2d, reinterpret_cast<void*>(image),
        /*mem*/ nullptr, reinterpret_cast<void*>(view), static_cast<uint32_t>(layout));
    resource.width = info.extent.width;
    resource.height = info.extent.height;
    resource.nativeFormat = static_cast<uint32_t>(info.format);
    resource.mipLevels = info.mipLevels;
    resource.arrayLayers = info.arrayLayers;
    resource.usage = info.usage;

    if (bufferType == sl::kBufferTypeScalingInputColor || bufferType == sl::kBufferTypeScalingOutputColor) {
        DlssTaggedImageX64& snap = (bufferType == sl::kBufferTypeScalingInputColor)
            ? g_taggedInputColorX64 : g_taggedOutputColorX64;
        snap.image = image;
        snap.layout = layout;
        snap.format = info.format;
        snap.usage = info.usage;
        snap.width = info.extent.width;
        snap.height = info.extent.height;
    }

    sl::ResourceTag tag(&resource, bufferType, sl::ResourceLifecycle::eValidUntilPresent);
    return StreamlineSetTagForFrameX64(&tag, 1);
}

HRESULT STDMETHODCALLTYPE Hook_SetDepthStencilSurfaceX64(void* device, void* pNewZStencil)
{
    // Real bug fix (2026-09-29) -- see SetLastKnownRenderDevice's own header
    // comment (overlay_hud.h) for the real crash this closes: this hook can
    // fire on a freshly-recreated device before that device's first
    // EndScene ever runs, and TagDepthResourceForFrame's own downstream
    // resolution lookups (via GetRealScreenSize) would otherwise use the
    // OLD, already-destroyed device that g_lastKnownRenderDevice still
    // pointed at.
    SetLastKnownRenderDevice(device);
    if (pNewZStencil) TagDepthResourceForFrame(pNewZStencil);
    return g_origSetDepthStencilSurface(device, pNewZStencil);
}

HRESULT STDMETHODCALLTYPE Hook_SetRenderTargetX64(void* device, DWORD renderTargetIndex, void* pRenderTarget)
{
    // Real, live-confirmed crash fix (2026-09-29): see
    // SetLastKnownRenderDevice's own header comment (overlay_hud.h) --
    // TagColorResourceForFrame's own resolution lookups (via
    // ComputeExpectedInternalResolutionX64 -> GetRealScreenSize) used
    // GetLastKnownRenderDevice(), which was only ever refreshed in
    // Hook_EndScene. A real WER crash dump confirmed this hook firing on a
    // freshly-recreated device (a display-mode switch, fullscreen <->
    // windowed-borderless) BEFORE that device's own first EndScene call --
    // GetLastKnownRenderDevice() still returned the OLD, already-destroyed
    // device at that exact moment, and calling GetViewport on it crashed.
    // This hook already receives the real, live, current device as its own
    // first argument -- refreshing the global here closes the gap.
    SetLastKnownRenderDevice(device);
    // Only render target 0 (the primary color buffer) is relevant for DLSS
    // scaling input -- other indices are MRT slots (normals, velocity
    // buffers the game's own renderer might use internally, etc.), not the
    // final composited scene color.
    if (renderTargetIndex == 0 && pRenderTarget) TagColorResourceForFrame(pRenderTarget);
    return g_origSetRenderTarget(device, renderTargetIndex, pRenderTarget);
}

} // namespace

// Called once per real frame from Hook_EndScene (overlay_hud.cpp) -- unlike
// depth/color input, this isn't hooked off a real game call (the game never
// touches this resource), so it needs its own explicit per-frame call site
// rather than a MinHook detour.
void TagStreamlineOutputColorX64()
{
    TagOutputColorResourceForFrame();
}

// Same real per-frame call-site rationale as TagStreamlineOutputColorX64
// above -- this project owns the motion-vectors resource too.
void TagStreamlineMotionVectorsX64()
{
    TagMotionVectorsResourceForFrame();
}

// Public wrapper around the anonymous-namespace ComputeExpectedInternalResolutionX64
// above (internal linkage -- not directly callable from another translation
// unit, per this project's own already-documented linkage lesson,
// CLAUDE.md's "Checking is far cheaper than digging" note). Added 2026-09-24
// so streamline_camera_x64.cpp can size Constants::mvecScale against the
// same real resolution the motion-vectors buffer itself actually uses,
// without duplicating the formula a second time.
bool GetStreamlineInternalRenderResolutionX64(uint32_t& outWidth, uint32_t& outHeight)
{
    return ComputeExpectedInternalResolutionX64(outWidth, outHeight);
}

// 2026-09-27: public accessor for the real DLSS output surface (g_outputColorSurface,
// internal linkage above) -- streamline_evaluate_x64.cpp's own final composite step
// (StretchRect this surface onto the current render target, before HUD/UI drawing)
// needs the real surface pointer directly, not just its resolution. Returns nullptr
// if the output texture hasn't been created yet this session (no successful evaluate
// has happened, or TagStreamlineOutputColorX64 hasn't run yet this frame) -- callers
// must check.
void* GetDlssOutputSurfaceX64()
{
    return g_outputColorSurface;
}

// 2026-09-27: size of the DLSS output texture as actually created, so the
// evaluate can refuse to run when it disagrees with the options' output size.
// Returns false if no output texture exists yet.
bool GetDlssOutputTextureSizeX64(uint32_t& outWidth, uint32_t& outHeight)
{
    if (!g_outputColorSurface) return false;
    outWidth = g_outputColorWidth;
    outHeight = g_outputColorHeight;
    return true;
}

// 2026-09-27 (ROUND 19): REAL FIX for DLSS running for a while and then
// hanging/losing the device, sooner on heavier levels. DXVK relocates images
// to new VkImage handles in the background -- memory defragmentation, on by
// default on NVIDIA and engaged under exactly the memory pressure heavy
// levels (plus DLSS's own allocations) create (dxvk_memory.cpp
// enableDefrag/pickDefragChunk). Relocations are recorded at the end of every
// DXVK command list (DxvkContext::endRecording -> relocateQueuedResources),
// INCLUDING the one FlushRenderingCommands() closes right before our own
// evaluate. Every tag set earlier in the frame (the SetRenderTarget/
// SetDepthStencilSurface hooks, and our own output/motion-vector tags) can
// therefore name a VkImage DXVK has just retired; its memory is freed once
// DXVK's copy completes, and DLSS then reads/writes freed GPU memory -- a
// page fault, i.e. the hang/device loss. Called by EvaluateStreamlineDlssX64
// AFTER FlushRenderingCommands (the CS thread is idle, so nothing can be
// relocated again before our submit) and BEFORE LockSubmissionQueue (no D3D9
// or interop call is made while the queue is locked): re-resolves all four
// required DLSS inputs to their current VkImage and re-tags them for this
// frame. Returns false -- caller skips evaluate this frame -- if this frame's
// scene color/depth were never recorded or any re-resolve fails.
bool RetagStreamlineInputsAfterFlushX64()
{
    if (!g_frameSceneDepthSurfaceX64 || !g_frameSceneColorSurfaceX64 ||
        !g_outputColorSurface || !g_motionVectorsSurface) {
        // 2026-09-27 (ROUND 22): visible, rate-limited -- with the colour input
        // now selected by engine target ID, "no scene colour recorded" is the
        // expected outcome if the engine never binds an accepted scene-colour
        // target at the internal resolution; that must show up in the log,
        // not silently skip DLSS.
        static long long s_missingCount = 0;
        ++s_missingCount;
        if (s_missingCount <= 10 || (s_missingCount % 1000) == 0) {
            char buf[240];
            sprintf_s(buf, "[x64-streamline-evaluate] Skipping evaluate: missing this frame's%s%s%s%s "
                "(count=%lld).", g_frameSceneDepthSurfaceX64 ? "" : " scene-depth",
                g_frameSceneColorSurfaceX64 ? "" : " scene-colour",
                g_outputColorSurface ? "" : " output", g_motionVectorsSurface ? "" : " motion-vectors",
                s_missingCount);
            LogFromController(buf);
        }
        return false;
    }
    if (!ResolveVulkanFunctionsIfNeeded()) return false;

    return ResolveAndTagCurrentImageX64(g_frameSceneDepthSurfaceX64, sl::kBufferTypeDepth,
               VK_IMAGE_ASPECT_DEPTH_BIT, g_cachedDepthImage, g_cachedDepthView,
               "[x64-streamline-depth]", "depth-only") &&
        ResolveAndTagCurrentImageX64(g_frameSceneColorSurfaceX64, sl::kBufferTypeScalingInputColor,
               VK_IMAGE_ASPECT_COLOR_BIT, g_cachedColorImage, g_cachedColorView,
               "[x64-streamline-color]", "color") &&
        ResolveAndTagCurrentImageX64(g_outputColorSurface, sl::kBufferTypeScalingOutputColor,
               VK_IMAGE_ASPECT_COLOR_BIT, g_cachedOutputColorImage, g_cachedOutputColorView,
               "[x64-streamline-output]", "output-color") &&
        ResolveAndTagCurrentImageX64(g_motionVectorsSurface, sl::kBufferTypeMotionVectors,
               VK_IMAGE_ASPECT_COLOR_BIT, g_cachedMotionVectorsImage, g_cachedMotionVectorsView,
               "[x64-streamline-mvec]", "motion-vectors");
}

// 2026-09-27 (ROUND 19): per-real-frame reset for the raw, non-owning scene
// surface pointers above -- called from ResetDlssCompositeFrameGuardX64
// (streamline_evaluate_x64.cpp), itself called once per real frame from
// Hook_EndScene, so a pointer recorded in one frame is never used in the next.
void ClearStreamlineFrameSurfacesX64()
{
    g_frameSceneDepthSurfaceX64 = nullptr;
    g_frameSceneColorSurfaceX64 = nullptr;
    g_frameSceneColorMsaaSurfaceX64 = nullptr;
    g_taggedInputColorX64 = DlssTaggedImageX64{};
    g_taggedOutputColorX64 = DlssTaggedImageX64{};
}


// 2026-09-27 (ROUND 23): if this frame's scene colour was multisampled (see
// g_frameSceneColorMsaaSurfaceX64), resolve it into a proxy-owned single-
// sampled render-target texture of the same size and format with the standard
// D3D9 MSAA resolve (StretchRect, same size, D3DTEXF_NONE -- DXVK turns this
// into a real vkCmdResolveImage), and make that texture this frame's DLSS
// scene-colour input. Must run on the render thread, BEFORE
// EvaluateStreamlineDlssX64 (whose post-flush re-tag then resolves and tags
// the resolved texture's VkImage). No-op when the scene colour was already
// single-sampled. Returns false only if a resolve was needed and failed
// (logged, rate-limited) -- the evaluate then skips this frame.
bool ResolveMsaaSceneColorForDlssX64(void* device)
{
    if (!g_frameSceneColorMsaaSurfaceX64) return true; // nothing to resolve
    if (!device) return false;

    static long long s_failCount = 0;
    auto logFail = [](const char* what, HRESULT hr) {
        ++s_failCount;
        if (s_failCount <= 10 || (s_failCount % 1000) == 0) {
            char buf[220];
            sprintf_s(buf, "[x64-streamline-resolve] %s FAILED (hr=0x%08lX, count=%lld) -- DLSS skipped "
                "this frame.", what, static_cast<unsigned long>(hr), s_failCount);
            LogFromController(buf);
        }
    };

    void* source = g_frameSceneColorMsaaSurfaceX64;
    void** srcVtbl = *reinterpret_cast<void***>(source);
    auto getDesc = reinterpret_cast<SurfaceGetDescFnX64>(srcVtbl[kSurfaceGetDescVtableIndexX64]);
    SurfaceDescX64 desc{};
    HRESULT hr = getDesc(source, &desc);
    if (FAILED(hr)) { logFail("GetDesc on the multisampled scene target", hr); return false; }

    if (!g_msaaResolveTexture || g_msaaResolveWidth != desc.Width || g_msaaResolveHeight != desc.Height ||
        g_msaaResolveFormat != desc.Format) {
        if (g_msaaResolveSurface) {
            reinterpret_cast<IUnknown*>(g_msaaResolveSurface)->Release();
            g_msaaResolveSurface = nullptr;
        }
        if (g_msaaResolveTexture) {
            reinterpret_cast<IUnknown*>(g_msaaResolveTexture)->Release();
            g_msaaResolveTexture = nullptr;
        }
        void** devVtbl = *reinterpret_cast<void***>(device);
        auto createTexture = reinterpret_cast<CreateTextureFn>(devVtbl[kCreateTextureVtableIndex]);
        hr = createTexture(device, desc.Width, desc.Height, 1, kD3DUSAGE_RENDERTARGET, desc.Format,
            kD3DPOOL_DEFAULT, &g_msaaResolveTexture, nullptr);
        if (FAILED(hr) || !g_msaaResolveTexture) {
            g_msaaResolveTexture = nullptr;
            logFail("CreateTexture for the single-sampled resolve target", hr);
            return false;
        }
        void** texVtbl = *reinterpret_cast<void***>(g_msaaResolveTexture);
        auto getSurfaceLevel = reinterpret_cast<GetSurfaceLevelFn>(texVtbl[kGetSurfaceLevelVtableIndex]);
        hr = getSurfaceLevel(g_msaaResolveTexture, 0, &g_msaaResolveSurface);
        if (FAILED(hr) || !g_msaaResolveSurface) {
            reinterpret_cast<IUnknown*>(g_msaaResolveTexture)->Release();
            g_msaaResolveTexture = nullptr;
            g_msaaResolveSurface = nullptr;
            logFail("GetSurfaceLevel on the resolve target", hr);
            return false;
        }
        g_msaaResolveWidth = desc.Width;
        g_msaaResolveHeight = desc.Height;
        g_msaaResolveFormat = desc.Format;
        char buf[220];
        sprintf_s(buf, "[x64-streamline-resolve] Created a %ux%u single-sampled resolve target (D3DFORMAT %lu) "
            "for the %u-sample scene colour.", desc.Width, desc.Height, static_cast<unsigned long>(desc.Format),
            static_cast<unsigned>(desc.MultiSampleType));
        LogFromController(buf);
    }

    void** devVtbl = *reinterpret_cast<void***>(device);
    auto stretchRect = reinterpret_cast<StretchRectFnRes>(devVtbl[kStretchRectVtableIndexRes]);
    hr = stretchRect(device, source, nullptr, g_msaaResolveSurface, nullptr, kD3DTEXF_NONE);
    if (FAILED(hr)) { logFail("StretchRect MSAA resolve", hr); return false; }

    static long long s_resolveCount = 0;
    ++s_resolveCount;
    if (s_resolveCount <= 5 || (s_resolveCount % 5000) == 0) {
        char buf[200];
        sprintf_s(buf, "[x64-streamline-resolve] Resolved the %u-sample %ux%u scene colour for DLSS "
            "(count=%lld).", static_cast<unsigned>(desc.MultiSampleType), desc.Width, desc.Height,
            s_resolveCount);
        LogFromController(buf);
    }
    g_frameSceneColorSurfaceX64 = g_msaaResolveSurface; // this frame's DLSS scene-colour input
    return true;
}

// 2026-09-27: the scene-color input and DLSS output as tagged for this
// frame's evaluate (post-flush). Only meaningful after
// RetagStreamlineInputsAfterFlushX64 succeeded this frame.
void GetDlssTaggedImagesX64(DlssTaggedImageX64& inputColor, DlssTaggedImageX64& outputColor)
{
    inputColor = g_taggedInputColorX64;
    outputColor = g_taggedOutputColorX64;
}

// Called once, from InstallEndSceneHook (overlay_hud.cpp) -- same one-
// device-for-this-game's-lifetime guard convention every other x64 hook
// installer in this codebase uses.
void InstallDepthStencilHookX64(void* realDevice)
{
    // Real backend gate, 2026-09-26 (direct instruction, following the
    // GraphicsApi default flip to Vulkan) -- previously unconditional,
    // installing these MinHook detours (and thus firing TagColorResourceForFrame/
    // TagDepthResourceForFrame on every real SetRenderTarget/
    // SetDepthStencilSurface call, 9+ times/frame) regardless of whether
    // Streamline was even enabled. Never unsafe (the resource-tagging work
    // itself only ever reaches a real Vulkan/DXVK call after its own
    // internal resolution succeeds, which requires DXVK to be active) but
    // genuine wasted per-call overhead under LegacyD3D9 or with
    // StreamlineEnabled=0 for zero purpose. TryInitStreamlineX64 (d3d9_hook.cpp)
    // always runs before this -- see IsStreamlineInitializedX64's own
    // comment (streamline_integration_x64.cpp) for the full ordering
    // guarantee this relies on.
    if (!IsStreamlineInitializedX64()) return;
    if (!realDevice || g_origSetDepthStencilSurface) return;

    void** deviceVtbl = *reinterpret_cast<void***>(realDevice);
    void* realSetDepthStencilSurface = deviceVtbl[kSetDepthStencilSurfaceVtableIndex];

    MH_STATUS s = MH_CreateHook(realSetDepthStencilSurface,
        reinterpret_cast<void*>(&Hook_SetDepthStencilSurfaceX64),
        reinterpret_cast<void**>(&g_origSetDepthStencilSurface));
    if (s != MH_OK) {
        char buf[160];
        sprintf_s(buf, "[x64-streamline-depth] FATAL: MH_CreateHook failed for "
            "SetDepthStencilSurface (status=%d)", static_cast<int>(s));
        LogFromController(buf);
        return;
    }
    s = MH_EnableHook(realSetDepthStencilSurface);
    if (s != MH_OK) {
        char buf[160];
        sprintf_s(buf, "[x64-streamline-depth] FATAL: MH_EnableHook failed for "
            "SetDepthStencilSurface (status=%d)", static_cast<int>(s));
        LogFromController(buf);
        return;
    }
    LogFromController("[x64-streamline-depth] SetDepthStencilSurface hook installed -- "
        "watching for the real depth-stencil surface the game itself binds.");

    // Real color-input resolution (kBufferTypeScalingInputColor), same
    // pattern, same call site -- installed alongside the depth hook rather
    // than requiring a separate call from overlay_hud.cpp.
    void* realSetRenderTarget = deviceVtbl[kSetRenderTargetVtableIndex];
    s = MH_CreateHook(realSetRenderTarget, reinterpret_cast<void*>(&Hook_SetRenderTargetX64),
        reinterpret_cast<void**>(&g_origSetRenderTarget));
    if (s != MH_OK) {
        char buf[160];
        sprintf_s(buf, "[x64-streamline-color] FATAL: MH_CreateHook failed for "
            "SetRenderTarget (status=%d)", static_cast<int>(s));
        LogFromController(buf);
        return;
    }
    s = MH_EnableHook(realSetRenderTarget);
    if (s != MH_OK) {
        char buf[160];
        sprintf_s(buf, "[x64-streamline-color] FATAL: MH_EnableHook failed for "
            "SetRenderTarget (status=%d)", static_cast<int>(s));
        LogFromController(buf);
        return;
    }
    LogFromController("[x64-streamline-color] SetRenderTarget hook installed -- "
        "watching for the real primary color render target the game itself binds.");
}
