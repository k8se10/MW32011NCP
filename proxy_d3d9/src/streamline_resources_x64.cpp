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
extern VkInstance GetDxvkVkInstanceX64(); // streamline_integration_x64.cpp
extern VkDevice GetDxvkVkDeviceX64();     // streamline_integration_x64.cpp
extern bool StreamlineSetTagForFrameX64(const sl::ResourceTag* tags, uint32_t numTags); // streamline_integration_x64.cpp

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
constexpr DWORD kD3DFMT_A8R8G8B8 = 21; // matches the real scene color's own
    // confirmed VkFormat 44 (VK_FORMAT_B8G8R8A8_UNORM) via DXVK's translation.
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
// Raw D3D9 surface pointer dedup, 2026-09-24 -- same "huge fps cost" bug
// class as TagColorResourceForFrame's own fix (see that function's header
// comment): GetVulkanImageInfo flushes pending Vulkan commands, and
// SetDepthStencilSurface fires many times per real frame even though (per
// this comment block's own existing note, above) it's usually the SAME 1-2
// real images repeating. Unlike color, depth has no resolution-based
// disambiguation target to pre-filter against -- this is a plain "skip the
// flush entirely when it's literally the same D3D9 surface object as last
// time" dedup instead.
void* g_lastProcessedDepthSurfaceX64 = nullptr;
VkImage g_cachedColorImage = VK_NULL_HANDLE;
VkImageView g_cachedColorView = VK_NULL_HANDLE;

// Real DLSS output buffer (kBufferTypeScalingOutputColor) -- unlike depth
// and color input, the game has no existing resource for this at all (DLSS
// writes a NEW, real image here; nothing currently consumes it). Created
// once, lazily, as a real D3D9 RENDERTARGET texture at the real native/
// display resolution ("internal render scaled percent is what we feed into
// dlss" -- the INPUT side; the OUTPUT side is the real display resolution
// this project's own upscale target is), via this project's own already-
// established CreateTexture pattern (overlay_hud.cpp's own
// EnsureBlurTexture/CreateFullscreenCaptureTexture etc.), then resolved to
// a real Vulkan image/view through the SAME DXVK interop chain as
// depth/color input. Recreated if the real native resolution ever changes.
void* g_outputColorTexture = nullptr; // real IDirect3DTexture9*, we own this
void* g_outputColorSurface = nullptr; // real IDirect3DSurface9*, GetSurfaceLevel(0) of the above
uint32_t g_outputColorWidth = 0;
uint32_t g_outputColorHeight = 0;
VkImage g_cachedOutputColorImage = VK_NULL_HANDLE;
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

// Cached result of the last REAL (non-deduped) resolve -- reused when
// TagDepthResourceForFrame sees the identical D3D9 surface pointer again,
// to avoid a second real GetVulkanImageInfo flush for state that hasn't
// actually changed. See g_lastProcessedDepthSurfaceX64's own comment.
VkImage g_lastDepthImage = VK_NULL_HANDLE;
VkImageLayout g_lastDepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
VkImageCreateInfo g_lastDepthInfo{};

void TagDepthResourceForFrame(void* depthSurface)
{
    ++g_resolveAttemptCount;
    bool heartbeat = g_resolveAttemptCount <= 5 || (g_resolveAttemptCount % 5000) == 0;

    VkImage image = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;

    // REAL, LIVE, SEVERE BUG FIXED, 2026-09-24 -- same class as
    // TagColorResourceForFrame's own fix (see that function's header
    // comment): GetVulkanImageInfo flushes pending Vulkan commands, and
    // SetDepthStencilSurface fires many times per real frame even though
    // (per g_cachedDepthImage's own existing comment, above) it's usually
    // the SAME 1-2 real images repeating. When the identical D3D9 surface
    // pointer is seen again, reuse the cached result from the last REAL
    // resolve instead of forcing another flush.
    if (depthSurface == g_lastProcessedDepthSurfaceX64 && g_lastDepthImage != VK_NULL_HANDLE) {
        image = g_lastDepthImage;
        layout = g_lastDepthLayout;
        info = g_lastDepthInfo;
    } else {
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

        g_lastProcessedDepthSurfaceX64 = depthSurface;
        g_lastDepthImage = image;
        g_lastDepthLayout = layout;
        g_lastDepthInfo = info;
    }

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
void TagColorResourceForFrame(void* renderTarget)
{
    ++g_resolveColorAttemptCount;
    bool heartbeat = g_resolveColorAttemptCount <= 5 || (g_resolveColorAttemptCount % 5000) == 0;

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
        char buf[400];
        sprintf_s(buf, "[x64-streamline-color] attempt=%lld image=0x%llx layout=%d format=%d "
            "extent=%ux%ux%u mipLevels=%u arrayLayers=%u samples=%d usage=0x%X expected=%ux%u match=%d",
            g_resolveColorAttemptCount, reinterpret_cast<unsigned long long>(image), static_cast<int>(layout),
            static_cast<int>(info.format), info.extent.width, info.extent.height, info.extent.depth,
            info.mipLevels, info.arrayLayers, static_cast<int>(info.samples), info.usage,
            expectedWidth, expectedHeight, resolutionMatches ? 1 : 0);
        LogFromController(buf);
    }

    if (!resolutionMatches) return; // a real, different render target this frame -- not the scene color

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

    // (Re)create if this is the first attempt, or the real native resolution
    // changed since the texture was created.
    if (!g_outputColorTexture || g_outputColorWidth != static_cast<uint32_t>(nativeW) ||
        g_outputColorHeight != static_cast<uint32_t>(nativeH)) {
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

        void** deviceVtbl = *reinterpret_cast<void***>(device);
        auto createTexture = reinterpret_cast<CreateTextureFn>(deviceVtbl[kCreateTextureVtableIndex]);
        HRESULT hr = createTexture(device, static_cast<UINT>(nativeW), static_cast<UINT>(nativeH), 1,
            kD3DUSAGE_RENDERTARGET, kD3DFMT_A8R8G8B8, kD3DPOOL_DEFAULT, &g_outputColorTexture, nullptr);
        if (FAILED(hr) || !g_outputColorTexture) {
            char buf[200];
            sprintf_s(buf, "[x64-streamline-output] CreateTexture FAILED (hr=0x%08lX) for a real "
                "%dx%d DLSS output buffer.", hr, nativeW, nativeH);
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

        g_outputColorWidth = static_cast<uint32_t>(nativeW);
        g_outputColorHeight = static_cast<uint32_t>(nativeH);
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

HRESULT STDMETHODCALLTYPE Hook_SetDepthStencilSurfaceX64(void* device, void* pNewZStencil)
{
    if (pNewZStencil) TagDepthResourceForFrame(pNewZStencil);
    return g_origSetDepthStencilSurface(device, pNewZStencil);
}

HRESULT STDMETHODCALLTYPE Hook_SetRenderTargetX64(void* device, DWORD renderTargetIndex, void* pRenderTarget)
{
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

// Called once, from InstallEndSceneHook (overlay_hud.cpp) -- same one-
// device-for-this-game's-lifetime guard convention every other x64 hook
// installer in this codebase uses.
void InstallDepthStencilHookX64(void* realDevice)
{
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
