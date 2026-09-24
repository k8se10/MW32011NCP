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
// Matches this project's own "trivial passthrough first" convention: this
// slice only RESOLVES and LOGS the real Vulkan image info -- it does NOT yet
// call slSetTagForFrame (that needs the resource wrapped in a real
// sl::Resource/sl::ResourceTag and a real command-buffer handle for the
// eOnlyValidNow/eValidUntilPresent lifecycle contract, not yet built).

#include <windows.h>
#include <cstdio>
#include "dxvk_interop_x64.h"
#include "overlay_hud.h" // GetLastKnownRenderDevice

extern void LogFromController(const char* msg); // dllmain.cpp

namespace {

// IDirect3DDevice9::GetDepthStencilSurface -- standard, Microsoft-documented
// D3D9 COM vtable slot 40 (IUnknown's 3 slots + 37 IDirect3DDevice9-specific
// methods before it). Same "stable public interface, not this game binary's
// own internal layout" reasoning overlay_hud.cpp's own vtable indices
// already use -- confirmed consistent with this project's own already-live
// indices for neighboring methods (kGetRenderTargetVtableIndex=38,
// kEndSceneVtableIndex=42).
constexpr int kGetDepthStencilSurfaceVtableIndex = 40;

long long g_resolveAttemptCount = 0;

} // namespace

// Called from Hook_EndScene (overlay_hud.cpp), real per-frame cadence
// matching every other diagnostic in this feature. Resolves the real
// backing Vulkan image for the game's own active depth-stencil surface and
// logs it -- read-only, zero behavior change, the real live-verification
// step before this data is trusted for actual resource tagging.
void LogDepthStencilVulkanImageX64()
{
    ++g_resolveAttemptCount;
    // Sparse heartbeat, same cadence convention as the rest of this
    // feature's own diagnostics -- this is a real, if cheap, COM call chain
    // every frame would be wasteful to run unthrottled.
    bool heartbeat = g_resolveAttemptCount <= 5 || (g_resolveAttemptCount % 5000) == 0;
    if (!heartbeat) return;

    void* device = GetLastKnownRenderDevice();
    if (!device) {
        LogFromController("[x64-streamline-depth] No known render device yet -- skipping this attempt.");
        return;
    }

    void** deviceVtbl = *reinterpret_cast<void***>(device);
    using GetDepthStencilSurfaceFn = HRESULT(STDMETHODCALLTYPE*)(void*, void**);
    auto getDepthStencilSurface = reinterpret_cast<GetDepthStencilSurfaceFn>(
        deviceVtbl[kGetDepthStencilSurfaceVtableIndex]);

    void* depthSurface = nullptr;
    HRESULT hr = getDepthStencilSurface(device, &depthSurface);
    if (FAILED(hr) || !depthSurface) {
        char buf[200];
        sprintf_s(buf, "[x64-streamline-depth] GetDepthStencilSurface FAILED (hr=0x%08lX) -- "
            "no active depth-stencil surface this frame.", hr);
        LogFromController(buf);
        return;
    }

    IUnknown* depthSurfaceUnknown = reinterpret_cast<IUnknown*>(depthSurface);
    ID3D9VkInteropTextureX64* interopTexture = nullptr;
    hr = depthSurfaceUnknown->QueryInterface(IID_ID3D9VkInteropTexture_X64,
        reinterpret_cast<void**>(&interopTexture));
    if (FAILED(hr) || !interopTexture) {
        char buf[200];
        sprintf_s(buf, "[x64-streamline-depth] QueryInterface for ID3D9VkInteropTexture FAILED "
            "(hr=0x%08lX) -- this device isn't DXVK, or the interop interface changed.", hr);
        LogFromController(buf);
        depthSurfaceUnknown->Release();
        return;
    }

    VkImage image = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    hr = interopTexture->GetVulkanImageInfo(&image, &layout, &info);
    interopTexture->Release();
    depthSurfaceUnknown->Release();

    if (FAILED(hr) || image == VK_NULL_HANDLE) {
        char buf[200];
        sprintf_s(buf, "[x64-streamline-depth] GetVulkanImageInfo FAILED (hr=0x%08lX) -- no real "
            "Vulkan image behind the depth-stencil surface this frame.", hr);
        LogFromController(buf);
        return;
    }

    char buf[350];
    sprintf_s(buf, "[x64-streamline-depth] attempt=%lld image=0x%p layout=%d format=%d "
        "extent=%ux%ux%u mipLevels=%u arrayLayers=%u samples=%d usage=0x%X",
        g_resolveAttemptCount, reinterpret_cast<void*>(image), static_cast<int>(layout),
        static_cast<int>(info.format), info.extent.width, info.extent.height, info.extent.depth,
        info.mipLevels, info.arrayLayers, static_cast<int>(info.samples), info.usage);
    LogFromController(buf);
}
