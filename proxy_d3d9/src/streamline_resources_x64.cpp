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

using SetDepthStencilSurfaceFn = HRESULT(STDMETHODCALLTYPE*)(void*, void*);
SetDepthStencilSurfaceFn g_origSetDepthStencilSurface = nullptr;

using SetRenderTargetFn = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, void*);
SetRenderTargetFn g_origSetRenderTarget = nullptr;

long long g_resolveAttemptCount = 0;
long long g_resolveColorAttemptCount = 0;

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
VkImage g_cachedColorImage = VK_NULL_HANDLE;
VkImageView g_cachedColorView = VK_NULL_HANDLE;

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

    VkImage image = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
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

    if (heartbeat) {
        char buf[350];
        sprintf_s(buf, "[x64-streamline-color] attempt=%lld image=0x%llx layout=%d format=%d "
            "extent=%ux%ux%u mipLevels=%u arrayLayers=%u samples=%d usage=0x%X",
            g_resolveColorAttemptCount, reinterpret_cast<unsigned long long>(image), static_cast<int>(layout),
            static_cast<int>(info.format), info.extent.width, info.extent.height, info.extent.depth,
            info.mipLevels, info.arrayLayers, static_cast<int>(info.samples), info.usage);
        LogFromController(buf);
    }

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
