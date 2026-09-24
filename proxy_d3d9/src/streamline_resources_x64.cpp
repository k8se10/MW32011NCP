// MW32011NCP, 2026-09-24: real resource-tagging groundwork -- the next
// unstarted Streamline/DLSS step after the camera-to-world matrix and
// projection matrix (both live-confirmed this session). Stage 1
// (camera-only motion vectors, vulkan_dlss_pipeline_research.md section
// 2.5) needs, at minimum, a real depth buffer tagged via slSetTagForFrame
// (kBufferTypeDepth). This file resolves the real backing Vulkan image for
// the game's own active depth-stencil surface, via DXVK's own real
// ID3D9VkInteropTexture interop interface (dxvk_interop_x64.h) -- the same
// interop mechanism already proven working for slSetVulkanInfo's own device/
// queue handles.
//
// ROUND 1 (same day) polled GetDepthStencilSurface from Hook_EndScene --
// real, live D3DERR_NOTFOUND every time (wrong hook point, see below).
// ROUND 2 (same day, LIVE-CONFIRMED): hooked SetDepthStencilSurface
// directly instead -- real depth-stencil Vulkan images resolved correctly
// (format=VK_FORMAT_D24_UNORM_S8_UINT, usage includes DEPTH_STENCIL_
// ATTACHMENT, extent matching the real render-scale resolution).
// ROUND 3 (this pass): sl::Resource's own doc comment is explicit --
// "Resource view, description etc. are MANDATORY only when using Vulkan" --
// GetVulkanImageInfo alone gives a raw VkImage, not a VkImageView. A real
// VkImageView has to be created directly via the Vulkan API, using the
// real VkDevice already cached from slSetVulkanInfo's own registration
// (streamline_integration_x64.cpp). No Vulkan import library is linked in
// this project -- every Vulkan function used here is resolved dynamically
// via GetProcAddress on the real loader DLL already loaded in-process by
// DXVK, the same "resolve at runtime, never assume a static link" approach
// this whole codebase already uses for the game's own native functions.

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

using SetDepthStencilSurfaceFn = HRESULT(STDMETHODCALLTYPE*)(void*, void*);
SetDepthStencilSurfaceFn g_origSetDepthStencilSurface = nullptr;

long long g_resolveAttemptCount = 0;

// Real Vulkan functions, resolved dynamically -- see this file's own top
// comment for why (no Vulkan import library linked in this project).
using PFN_vkGetInstanceProcAddr_local = PFN_vkVoidFunction(VKAPI_PTR*)(VkInstance, const char*);
using PFN_vkGetDeviceProcAddr_local = PFN_vkVoidFunction(VKAPI_PTR*)(VkDevice, const char*);
PFN_vkCreateImageView g_vkCreateImageView = nullptr;
PFN_vkDestroyImageView g_vkDestroyImageView = nullptr;
bool g_vulkanFunctionsResolved = false;
bool g_vulkanFunctionsResolveFailed = false;

// Real per-image view cache -- SetDepthStencilSurface fires far more often
// than the underlying image actually changes (live-confirmed: the same 1-2
// real VkImage handles repeat across many consecutive binds), so only
// create/destroy a real VkImageView when the image genuinely changes.
VkImage g_cachedDepthImage = VK_NULL_HANDLE;
VkImageView g_cachedDepthView = VK_NULL_HANDLE;
VkFormat g_cachedDepthFormat = VK_FORMAT_UNDEFINED;

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

// Depth-only view (VK_IMAGE_ASPECT_DEPTH_BIT), not depth+stencil -- Streamline's
// kBufferTypeDepth wants the depth component specifically.
VkImageView GetOrCreateDepthViewX64(VkImage image, VkFormat format, uint32_t mipLevels, uint32_t arrayLayers)
{
    if (image == g_cachedDepthImage && g_cachedDepthView != VK_NULL_HANDLE) {
        return g_cachedDepthView;
    }

    VkDevice device = GetDxvkVkDeviceX64();
    if (device == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    if (g_cachedDepthView != VK_NULL_HANDLE) {
        g_vkDestroyImageView(device, g_cachedDepthView, nullptr);
        g_cachedDepthView = VK_NULL_HANDLE;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = arrayLayers;

    VkImageView view = VK_NULL_HANDLE;
    VkResult vr = g_vkCreateImageView(device, &viewInfo, nullptr, &view);
    if (vr != VK_SUCCESS) {
        char buf[200];
        sprintf_s(buf, "[x64-streamline-depth] vkCreateImageView FAILED (VkResult=%d) for image "
            "0x%llx.", static_cast<int>(vr), reinterpret_cast<unsigned long long>(image));
        LogFromController(buf);
        return VK_NULL_HANDLE;
    }

    g_cachedDepthImage = image;
    g_cachedDepthView = view;
    g_cachedDepthFormat = format;
    char buf[200];
    sprintf_s(buf, "[x64-streamline-depth] Created a real depth-only VkImageView (0x%llx) for "
        "image 0x%llx (format=%d).", reinterpret_cast<unsigned long long>(view),
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

    VkImageView view = GetOrCreateDepthViewX64(image, info.format, info.mipLevels, info.arrayLayers);
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

HRESULT STDMETHODCALLTYPE Hook_SetDepthStencilSurfaceX64(void* device, void* pNewZStencil)
{
    if (pNewZStencil) TagDepthResourceForFrame(pNewZStencil);
    return g_origSetDepthStencilSurface(device, pNewZStencil);
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
}
