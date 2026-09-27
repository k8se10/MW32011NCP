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
// a prefix of the vtable (extended over time, see each slot's own comment) is
// declared here -- a COM vtable is laid
// out in declaration order, so declaring a prefix of it is ABI-correct as
// long as no later slot is ever called through this type. Any change to
// DXVK's upstream declaration order or IID must be mirrored here.
//
// x64-only, like everything Streamline-related in this project (DXVK and
// Streamline are both SP x64-only paths, see streamline_integration_x64.cpp).

#pragma once

#include <unknwn.h>
#include <cstddef> // offsetof -- D3D9VkExtImageDescX64's own layout static_asserts

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
struct D3D9VkExtImageDescX64; // forward decl -- CreateImage's own first
    // parameter type, real struct declared right after the interface below.

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

    // Vtable slots 9-11 -- never called by this project, declared only to keep
    // the vtable layout correct for CreateImage (slot 12) below. Parameter
    // types kept ABI-identical without d3d9.h (see this file's own top
    // comment): IDirect3DResource9* -> void*.
    virtual void STDMETHODCALLTYPE LockDevice() = 0;
    virtual void STDMETHODCALLTYPE UnlockDevice() = 0;
    virtual bool STDMETHODCALLTYPE WaitForResource(
        void* pResource,
        DWORD MapFlags) = 0;

    // Vtable slot 12 -- DXVK: "Creates a custom image/surface/texture."
    // EXTENDED, 2026-09-27: the ONLY way to create a real D3D9 texture whose
    // backing VkImage carries extra Vulkan usage flags (D3D9VkExtImageDesc::
    // ImageUsage, OR'd straight into VkImageCreateInfo::usage by DXVK's own
    // D3D9CommonTexture::CreatePrimaryImage). Plain IDirect3DDevice9::
    // CreateTexture never adds VK_IMAGE_USAGE_STORAGE_BIT for an ordinary
    // RENDERTARGET, but DLSS writes its output through a compute-shader
    // storage-image write -- see streamline_resources_x64.cpp's own
    // TagOutputColorResourceForFrame comment for the real bug this closes.
    // ppResult receives an IDirect3DResource9* of desc->Type (a real
    // IDirect3DTexture9* for D3DRTYPE_TEXTURE), caller owns one reference.
    virtual HRESULT STDMETHODCALLTYPE CreateImage(
        const D3D9VkExtImageDescX64* desc,
        void**                       ppResult) = 0;
};

// Mirrors DXVK's own D3D9VkExtImageDesc (d3d9_interfaces.h) field for field,
// in declaration order. D3DRESOURCETYPE/D3DFORMAT/D3DPOOL/D3DMULTISAMPLE_TYPE
// are all 4-byte C enums in the real SDK, declared DWORD here (ABI-identical)
// for the same no-d3d9.h reason as above. The static_asserts below pin the
// real layout (10 x 4-byte fields, 3 x 1-byte bools, 1 pad byte, then the
// 4-byte VkImageUsageFlags) so any accidental drift fails at compile time
// instead of passing garbage usage flags to DXVK at runtime.
struct D3D9VkExtImageDescX64 {
    DWORD             Type;               // D3DRESOURCETYPE -- SURFACE/TEXTURE/CUBETEXTURE/VOLUMETEXTURE
    UINT              Width;
    UINT              Height;
    UINT              Depth;              // must be 1 unless Type is VOLUMETEXTURE
    UINT              MipLevels;
    DWORD             Usage;              // D3DUSAGE_* flags
    DWORD             Format;             // D3DFORMAT
    DWORD             Pool;               // D3DPOOL
    DWORD             MultiSample;        // D3DMULTISAMPLE_TYPE, must be NONE unless Type is SURFACE
    DWORD             MultiSampleQuality;
    bool              Discard;            // depth stencils only
    bool              IsAttachmentOnly;   // false -> DXVK adds VK_IMAGE_USAGE_SAMPLED_BIT
    bool              IsLockable;
    VkImageUsageFlags ImageUsage;         // additional Vulkan image usage flags
};
static_assert(sizeof(D3D9VkExtImageDescX64) == 48, "D3D9VkExtImageDescX64 must match DXVK's D3D9VkExtImageDesc layout");
static_assert(offsetof(D3D9VkExtImageDescX64, Discard) == 40, "D3D9VkExtImageDescX64 must match DXVK's D3D9VkExtImageDesc layout");
static_assert(offsetof(D3D9VkExtImageDescX64, ImageUsage) == 44, "D3D9VkExtImageDescX64 must match DXVK's D3D9VkExtImageDesc layout");

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

// 2026-09-27: what one DLSS input/output image looked like at the moment it
// was tagged for this frame's evaluate (post-flush re-resolve,
// streamline_resources_x64.cpp). Used by the read-only pixel readback
// diagnostic in streamline_evaluate_x64.cpp. image == VK_NULL_HANDLE means
// "not tagged this frame".
struct DlssTaggedImageX64 {
    VkImage image = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags usage = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};
