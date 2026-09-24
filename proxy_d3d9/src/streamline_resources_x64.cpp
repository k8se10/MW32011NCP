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
// ROUND 1 (same day) polled GetDepthStencilSurface from Hook_EndScene, the
// same once-per-real-frame point every other diagnostic in this feature
// uses -- and got a real, live D3DERR_NOTFOUND (0x88760866) every time.
// EndScene fires AFTER all rendering for the frame (3D scene AND any 2D/UI
// compositing), and the depth-stencil surface is very likely unbound by
// then -- UI/2D passes don't need depth testing. Wrong hook point, not a
// broken interop chain.
//
// ROUND 2, real fix: hook IDirect3DDevice9::SetDepthStencilSurface directly
// instead of polling GetDepthStencilSurface after the fact. This captures
// the real surface at the exact moment the game itself binds it for the 3D
// scene, while the pointer is guaranteed valid (D3D9 API contract: the
// caller already holds a reference to whatever it passes in).
//
// Matches this project's own "trivial passthrough first" convention: this
// slice only RESOLVES and LOGS the real Vulkan image info -- it does NOT yet
// call slSetTagForFrame (that needs the resource wrapped in a real
// sl::Resource/sl::ResourceTag and a real command-buffer handle for the
// eOnlyValidNow/eValidUntilPresent lifecycle contract, not yet built).

#include <windows.h>
#include <cstdio>
#include "dxvk_interop_x64.h"
#include "../third_party/minhook/include/MinHook.h"

extern void LogFromController(const char* msg); // dllmain.cpp

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

void LogVulkanImageForSurface(void* depthSurface)
{
    ++g_resolveAttemptCount;
    // Sparse heartbeat -- SetDepthStencilSurface can legitimately be called
    // many times per frame in some engines; this project's own established
    // "never unthrottled per-frame diagnostic" convention applies here too.
    bool heartbeat = g_resolveAttemptCount <= 5 || (g_resolveAttemptCount % 5000) == 0;
    if (!heartbeat) return;

    IUnknown* depthSurfaceUnknown = reinterpret_cast<IUnknown*>(depthSurface);
    ID3D9VkInteropTextureX64* interopTexture = nullptr;
    HRESULT hr = depthSurfaceUnknown->QueryInterface(IID_ID3D9VkInteropTexture_X64,
        reinterpret_cast<void**>(&interopTexture));
    if (FAILED(hr) || !interopTexture) {
        char buf[200];
        sprintf_s(buf, "[x64-streamline-depth] QueryInterface for ID3D9VkInteropTexture FAILED "
            "(hr=0x%08lX) -- this device isn't DXVK, or the interop interface changed.", hr);
        LogFromController(buf);
        return;
    }

    VkImage image = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    hr = interopTexture->GetVulkanImageInfo(&image, &layout, &info);
    interopTexture->Release();

    if (FAILED(hr) || image == VK_NULL_HANDLE) {
        char buf[200];
        sprintf_s(buf, "[x64-streamline-depth] GetVulkanImageInfo FAILED (hr=0x%08lX) -- no real "
            "Vulkan image behind this depth-stencil surface.", hr);
        LogFromController(buf);
        return;
    }

    char buf[350];
    sprintf_s(buf, "[x64-streamline-depth] attempt=%lld image=0x%llx layout=%d format=%d "
        "extent=%ux%ux%u mipLevels=%u arrayLayers=%u samples=%d usage=0x%X",
        g_resolveAttemptCount, reinterpret_cast<unsigned long long>(image), static_cast<int>(layout),
        static_cast<int>(info.format), info.extent.width, info.extent.height, info.extent.depth,
        info.mipLevels, info.arrayLayers, static_cast<int>(info.samples), info.usage);
    LogFromController(buf);
}

HRESULT STDMETHODCALLTYPE Hook_SetDepthStencilSurfaceX64(void* device, void* pNewZStencil)
{
    if (pNewZStencil) LogVulkanImageForSurface(pNewZStencil);
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
