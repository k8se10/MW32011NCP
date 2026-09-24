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

    // Later slots (TransitionTextureLayout, FlushRenderingCommands, ...)
    // intentionally not declared -- never call anything beyond slot 4
    // through this type without first declaring it here in DXVK's order.
};
