// MW32011NCP, 2026-09-24: minimal DXVK Vulkan-interop interface declaration
// -- just enough of ID3D9VkInteropDevice (DXVK's public interop API,
// dxvk/src/d3d9/d3d9_interfaces.h in this repo's own vendored DXVK subtree)
// to QueryInterface a DXVK-created IDirect3DDevice9 and pull the Vulkan
// instance/physical-device/device/queue handles Streamline's
// slSetVulkanInfo needs.
//
// Why a local re-declaration instead of including d3d9_interfaces.h
// directly: that header drags in DXVK's own d3d9.h/MIDL_INTERFACE setup
// (built against DXVK's bundled mingw-directx-headers), which collides with
// the Windows SDK d3d9.h this proxy already builds against. Only the IID and
// the first two vtable slots (after IUnknown's three) are declared here --
// those are the only methods this project calls, and a COM vtable is laid
// out in declaration order, so declaring a prefix of it is ABI-correct as
// long as no later slot is ever called through this type. Any change to
// DXVK's upstream declaration order or IID must be mirrored here.
//
// x64-only, like everything Streamline-related in this project (DXVK and
// Streamline are both SP x64-only paths, see streamline_integration_x64.cpp).

#pragma once

#include <unknwn.h>

// Vulkan-Headers submodule inside the vendored DXVK subtree
// (dxvk/include/vulkan, pinned by DXVK itself). Angle-bracket include to
// match DXVK's own convention -- vulkan_core.h pulls in <vk_video/...> via
// angle-bracket search, so dxvk/include/vulkan/include must be an include
// DIRECTORY (proxy_d3d9.vcxproj's AdditionalIncludeDirectories), not a
// relative single-file include.
#include <vulkan/vulkan_core.h>

// {2EAA4B89-0107-4BDB-87F7-0F541C493CE0} -- copied verbatim from DXVK's
// __CRT_UUID_DECL(ID3D9VkInteropDevice, ...) in d3d9_interfaces.h.
static const IID IID_ID3D9VkInteropDevice_X64 =
    { 0x2eaa4b89, 0x0107, 0x4bdb, { 0x87, 0xf7, 0x0f, 0x54, 0x1c, 0x49, 0x3c, 0xe0 } };

struct ID3D9VkInteropTextureX64; // forward decl -- TransitionTextureLayout's own
    // first parameter type, real struct declared further down this file.

// EXTENDED, 2026-09-27: real, live-confirmed root cause of a genuine
// VK_ERROR_DEVICE_LOST fault (this project's own live testing) -- this
// project's raw vkQueueSubmit calls (streamline_evaluate_x64.cpp) were
// submitting directly to the SAME VkQueue DXVK uses internally, with ZERO
// synchronization against DXVK's own internal submission thread.
// Concurrent vkQueueSubmit calls on one VkQueue from different threads
// with no external synchronization is undefined behavior per the Vulkan
// spec itself -- DXVK's own real doc comments on FlushRenderingCommands/
// LockSubmissionQueue/ReleaseSubmissionQueue below say exactly this is
// the correct, required pattern for exactly this situation ("Must be
// called before submitting Vulkan commands to the rendering queue if
// those commands use the backing resource of a D3D9 object" /
// "immediately before submitting Vulkan commands... in order to prevent
// DXVK from using the queue"). Extending the vtable declaration up
// through these three real methods (TransitionTextureLayout must also be
// declared, even though never called, to keep the vtable layout correct
// -- a COM vtable is laid out in declaration order, so every slot before
// the one you actually need must exist).
struct ID3D9VkInteropDeviceX64 : public IUnknown {
    // Vtable slot 3 -- DXVK: "Queries Vulkan handles used by DXVK".
    virtual void STDMETHODCALLTYPE GetVulkanHandles(
        VkInstance*       pInstance,
        VkPhysicalDevice* pPhysDev,
        VkDevice*         pDevice) = 0;

    // Vtable slot 4 -- DXVK: "Queries the rendering queue used by DXVK".
    virtual void STDMETHODCALLTYPE GetSubmissionQueue(
        VkQueue*  pQueue,
        uint32_t* pQueueIndex,
        uint32_t* pQueueFamilyIndex) = 0;

    // Vtable slot 5 -- DXVK: "Executes an explicit image layout transition...
    // Synchronization is left up to the caller." Never called by this
    // project -- declared only to keep the vtable layout correct for the
    // real slots below that ARE called.
    virtual void STDMETHODCALLTYPE TransitionTextureLayout(
        ID3D9VkInteropTextureX64*      pTexture,
        const VkImageSubresourceRange* pSubresources,
        VkImageLayout                  OldLayout,
        VkImageLayout                  NewLayout) = 0;

    // Vtable slot 6 -- DXVK: "Must be called before submitting Vulkan
    // commands to the rendering queue if those commands use the backing
    // resource of a D3D9 object." Every resource this project's own
    // evaluate/composite pipeline touches (color/depth/output/motion-vector
    // buffers) IS a D3D9 object's backing resource -- this must be called
    // before our own vkQueueSubmit.
    virtual void STDMETHODCALLTYPE FlushRenderingCommands() = 0;

    // Vtable slot 7 -- DXVK: "Should be called immediately before submitting
    // Vulkan commands to the rendering queue in order to prevent DXVK from
    // using the queue. While the submission queue is locked, no D3D9 methods
    // must be called from the locking thread, or otherwise a deadlock might
    // occur." -- this last point matters: our own composite StretchRect (a
    // real D3D9 method) must only ever run AFTER ReleaseSubmissionQueue.
    virtual void STDMETHODCALLTYPE LockSubmissionQueue() = 0;

    // Vtable slot 8 -- DXVK: "Should be called immediately after submitting
    // Vulkan commands to the rendering queue in order to allow DXVK to
    // submit new commands."
    virtual void STDMETHODCALLTYPE ReleaseSubmissionQueue() = 0;

    // Later slots (LockDevice, UnlockDevice, WaitForResource, CreateImage)
    // intentionally not declared -- never call anything beyond slot 8
    // through this type without first declaring it here in DXVK's order.
};

// {D56344F5-8D35-46FD-806D-94C351B472C1} -- copied verbatim from DXVK's
// __CRT_UUID_DECL(ID3D9VkInteropTexture, ...) in d3d9_interfaces.h. A real,
// separate interface from ID3D9VkInteropDevice above -- QueryInterface this
// one from a real IDirect3DSurface9/IDirect3DTexture9 (e.g. the active
// depth-stencil surface) to get its backing Vulkan image, needed for
// Streamline resource tagging (slSetTagForFrame), 2026-09-24.
static const IID IID_ID3D9VkInteropTexture_X64 =
    { 0xd56344f5, 0x8d35, 0x46fd, { 0x80, 0x6d, 0x94, 0xc3, 0x51, 0xb4, 0x72, 0xc1 } };

struct ID3D9VkInteropTextureX64 : public IUnknown {
    // Vtable slot 3 -- DXVK: "Retrieves both the image handle as well as the
    // image's properties. Any of the given pointers may be nullptr." pLayout
    // receives the layout the image will be in after flushing outstanding
    // commands -- DXVK's own doc comment on this method.
    virtual HRESULT STDMETHODCALLTYPE GetVulkanImageInfo(
        VkImage*           pHandle,
        VkImageLayout*      pLayout,
        VkImageCreateInfo*  pInfo) = 0;
};
