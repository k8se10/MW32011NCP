#include <tuple>

#include "vulkan_loader.h"

#include "../util/log/log.h"

#include "../util/util_env.h"
#include "../util/util_string.h"
#include "../util/util_win32_compat.h"

namespace dxvk::vk {

  // MW32011DXVK: opt-in Vulkan loader override, resolved via a real absolute
  // path in DXVK_VULKAN_LOADER_OVERRIDE, tried BEFORE the normal
  // winevulkan/vulkan-1 search below. Off by default (empty env var ->
  // behavior identical to upstream). General, host-opt-in, not IW5-specific
  // -- see this fork's own STANDALONE requirement 3 and CLAUDE.md/AGENTS.md's
  // clarification of it (an explicit opt-in general option doesn't need the
  // executable-detection gate that requirement otherwise mandates).
  //
  // Real motivation: NVIDIA Streamline's manual-hooking mode needs its own
  // sl.interposer.dll acting as the actual Vulkan loader a host's
  // vkGetInstanceProcAddr calls resolve through, so it can intercept the
  // swapchain/present entry points it needs (ProgrammingGuideManualHooking.md
  // section 2.7). sl.interposer.dll itself is a real, NVIDIA-signed,
  // ICD-compatible proxy that internally forwards to the real underlying
  // Vulkan loader on its own -- this override just tells DXVK to load THAT
  // specific file (a full path, since a host embedding/extracting it to a
  // private location, as MW32011NCP now does, can't rely on the bare-name
  // DLL search order finding it) instead of going straight to vulkan-1.dll.
  static HMODULE tryLoadOverrideLibrary(PFN_vkGetInstanceProcAddr& outProc) {
    std::string overridePath = env::getEnvVar("DXVK_VULKAN_LOADER_OVERRIDE");
    if (overridePath.empty())
      return nullptr;

    HMODULE library = LoadLibraryA(overridePath.c_str());
    if (!library) {
      Logger::err(str::format("Vulkan: DXVK_VULKAN_LOADER_OVERRIDE set to '", overridePath,
        "' but LoadLibraryA failed -- falling back to the normal Vulkan loader search."));
      return nullptr;
    }

    auto proc = GetProcAddress(library, "vkGetInstanceProcAddr");
    if (!proc) {
      Logger::err(str::format("Vulkan: DXVK_VULKAN_LOADER_OVERRIDE '", overridePath,
        "' loaded but has no vkGetInstanceProcAddr export -- falling back to the normal "
        "Vulkan loader search."));
      FreeLibrary(library);
      return nullptr;
    }

    Logger::info(str::format("Vulkan: Found vkGetInstanceProcAddr in DXVK_VULKAN_LOADER_OVERRIDE ('",
      overridePath, "') @ 0x", std::hex, reinterpret_cast<uintptr_t>(proc)));
    outProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(proc);
    return library;
  }

  static std::pair<HMODULE, PFN_vkGetInstanceProcAddr> loadVulkanLibrary() {
#ifdef _WIN32
    PFN_vkGetInstanceProcAddr overrideProc = nullptr;
    if (HMODULE overrideLibrary = tryLoadOverrideLibrary(overrideProc))
      return std::make_pair(overrideLibrary, overrideProc);
#endif

    static const std::array<const char*, 2> dllNames = {{
#ifdef _WIN32
      "winevulkan.dll",
      "vulkan-1.dll",
#else
      "libvulkan.so",
      "libvulkan.so.1",
#endif
    }};

    for (auto dllName : dllNames) {
      HMODULE library = LoadLibraryA(dllName);

      if (!library)
        continue;

      auto proc = GetProcAddress(library, "vkGetInstanceProcAddr");

      if (!proc) {
        FreeLibrary(library);
        continue;
      }

      Logger::info(str::format("Vulkan: Found vkGetInstanceProcAddr in ", dllName, " @ 0x", std::hex, reinterpret_cast<uintptr_t>(proc)));
      return std::make_pair(library, reinterpret_cast<PFN_vkGetInstanceProcAddr>(proc));
    }

    Logger::err("Vulkan: vkGetInstanceProcAddr not found");
    return { };
  }

  LibraryLoader::LibraryLoader() {
    std::tie(m_library, m_getInstanceProcAddr) = loadVulkanLibrary();
  }

  LibraryLoader::LibraryLoader(PFN_vkGetInstanceProcAddr loaderProc) {
    m_getInstanceProcAddr = loaderProc;
  }

  LibraryLoader::~LibraryLoader() {
    if (m_library)
      FreeLibrary(m_library);
  }

  PFN_vkVoidFunction LibraryLoader::sym(VkInstance instance, const char* name) const {
    return m_getInstanceProcAddr(instance, name);
  }

  PFN_vkVoidFunction LibraryLoader::sym(const char* name) const {
    return sym(nullptr, name);
  }

  
  InstanceLoader::InstanceLoader(const Rc<LibraryLoader>& library, bool owned, VkInstance instance)
  : m_library(library), m_instance(instance), m_owned(owned) { }
  
  
  PFN_vkVoidFunction InstanceLoader::sym(const char* name) const {
    return m_library->sym(m_instance, name);
  }
  
  
  DeviceLoader::DeviceLoader(const Rc<InstanceLoader>& library, bool owned, VkDevice device)
  : m_library(library)
  , m_getDeviceProcAddr(reinterpret_cast<PFN_vkGetDeviceProcAddr>(
      m_library->sym("vkGetDeviceProcAddr"))),
    m_device(device), m_owned(owned) { }
  
  
  PFN_vkVoidFunction DeviceLoader::sym(const char* name) const {
    return m_getDeviceProcAddr(m_device, name);
  }
  
  
  LibraryFn::LibraryFn() { }
  LibraryFn::LibraryFn(PFN_vkGetInstanceProcAddr loaderProc)
  : LibraryLoader(loaderProc) { }
  LibraryFn::~LibraryFn() { }
  
  
  InstanceFn::InstanceFn(const Rc<LibraryLoader>& library, bool owned, VkInstance instance)
  : InstanceLoader(library, owned, instance) { }
  InstanceFn::~InstanceFn() {
    if (m_owned)
      this->vkDestroyInstance(m_instance, nullptr);
  }
  
  
  DeviceFn::DeviceFn(const Rc<InstanceLoader>& library, bool owned, VkDevice device)
  : DeviceLoader(library, owned, device) { }
  DeviceFn::~DeviceFn() {
    if (m_owned)
      this->vkDestroyDevice(m_device, nullptr);
  }
  
}
