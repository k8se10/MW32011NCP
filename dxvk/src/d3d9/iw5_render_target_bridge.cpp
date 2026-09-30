#include "iw5_render_target_bridge.h"

#include "../util/iw5_build_identity.h"
#include "../util/iw5_signature_scan.h"
#include "../util/log/log.h"
#include "../util/util_string.h"

#include "../third_party/minhook/include/MinHook.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

namespace dxvk::iw5 {

  namespace {

    constexpr const char* kRenderTargetSelectSignature =
      "40 56 41 54 41 57 48 83 EC 30 48 8B 71 08 4C 8B E1 4C 63 FA 44 3B BE D8 0B 00 00 "
      "0F 84 ?? ?? ?? ?? 8B 86 9C 0B 00 00 48 89 5C 24 50 48 89 6C 24 58 48 89 7C 24 60 "
      "4C 89 6C 24 28 4C 89 74 24 20 0F BA E0 1E 73 ?? 48 8B 8E 10 01 00 00 0F BA F0 1E "
      "81 A6 94 0B 00 00 FF FF FF BF 45 33 C0 89 86 9C 0B 00 00 BA C2 00 00 00 48 8B 01";

    using RenderTargetSelectFn = void (__fastcall*) (void*, int);

    thread_local int32_t g_pendingRenderTargetId = -1;
    RenderTargetSelectFn g_originalRenderTargetSelect = nullptr;
    std::mutex g_installMutex;
    bool g_hookInstalled = false;

    bool isReadableImageRange(
            const uint8_t* address,
            size_t         length,
            HMODULE        module,
            bool           requireExecutable) {
      const uintptr_t begin = reinterpret_cast<uintptr_t>(address);
      if (length > UINTPTR_MAX - begin)
        return false;
      const uintptr_t end = begin + length;

      for (uintptr_t current = begin; current < end; ) {
        MEMORY_BASIC_INFORMATION info = {};
        if (!::VirtualQuery(reinterpret_cast<const void*>(current), &info, sizeof(info)))
          return false;

        const DWORD protection = info.Protect & 0xffu;
        const bool readable =
             protection == PAGE_READONLY
          || protection == PAGE_READWRITE
          || protection == PAGE_WRITECOPY
          || protection == PAGE_EXECUTE_READ
          || protection == PAGE_EXECUTE_READWRITE
          || protection == PAGE_EXECUTE_WRITECOPY;
        const bool executable =
             protection == PAGE_EXECUTE_READ
          || protection == PAGE_EXECUTE_READWRITE
          || protection == PAGE_EXECUTE_WRITECOPY;

        if (info.AllocationBase != module
         || info.Type != MEM_IMAGE
         || info.State != MEM_COMMIT
         || (info.Protect & PAGE_GUARD)
         || !readable
         || (requireExecutable && !executable))
          return false;

        const uintptr_t regionBase = reinterpret_cast<uintptr_t>(info.BaseAddress);
        if (info.RegionSize > UINTPTR_MAX - regionBase)
          return false;
        const uintptr_t regionEnd = regionBase + info.RegionSize;
        if (regionEnd <= current)
          return false;

        current = std::min(regionEnd, end);
      }

      return true;
    }

    bool scanTextSection(HMODULE module, void*& match) {
      match = nullptr;

      const auto* base = reinterpret_cast<const uint8_t*>(module);
      if (!isReadableImageRange(base, 0x40, module, false))
        return false;

      const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
      if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE
       || dosHeader->e_lfanew < 0)
        return false;

      const size_t ntHeaderOffset = size_t(dosHeader->e_lfanew);
      if (ntHeaderOffset > 0x100000
       || ntHeaderOffset > 0x044be000 - sizeof(IMAGE_NT_HEADERS64))
        return false;

      if (!isReadableImageRange(base + ntHeaderOffset, sizeof(IMAGE_NT_HEADERS64), module, false))
        return false;

      const auto* ntHeader = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dosHeader->e_lfanew);
      if (ntHeader->Signature != IMAGE_NT_SIGNATURE
       || ntHeader->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64
       || ntHeader->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
       || ntHeader->OptionalHeader.SizeOfImage != 0x044be000
       || ntHeader->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64)
       || ntHeader->FileHeader.NumberOfSections == 0
       || ntHeader->FileHeader.NumberOfSections > 96
       || ntHeader->OptionalHeader.SizeOfHeaders > ntHeader->OptionalHeader.SizeOfImage)
        return false;

      const size_t sectionTableOffset =
        ntHeaderOffset + offsetof(IMAGE_NT_HEADERS64, OptionalHeader)
        + ntHeader->FileHeader.SizeOfOptionalHeader;
      const size_t sectionTableSize =
        size_t(ntHeader->FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);

      if (sectionTableOffset > ntHeader->OptionalHeader.SizeOfHeaders
       || sectionTableSize > ntHeader->OptionalHeader.SizeOfHeaders - sectionTableOffset)
        return false;

      if (!isReadableImageRange(base, ntHeader->OptionalHeader.SizeOfHeaders, module, false))
        return false;

      const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(base + sectionTableOffset);
      for (uint16_t i = 0; i < ntHeader->FileHeader.NumberOfSections; i++) {
        const IMAGE_SECTION_HEADER& section = sections[i];
        if (std::memcmp(section.Name, ".text", 5) != 0
         || section.Name[5] != '\0'
         || !(section.Characteristics & IMAGE_SCN_MEM_EXECUTE))
          continue;

        const size_t sectionSize = section.Misc.VirtualSize;
        if (section.VirtualAddress > ntHeader->OptionalHeader.SizeOfImage
         || sectionSize > ntHeader->OptionalHeader.SizeOfImage - section.VirtualAddress)
          return false;

        const auto* text = base + section.VirtualAddress;
        if (!isReadableImageRange(text, sectionSize, module, true))
          return false;

        size_t matchOffset = 0;
        const SignatureScanResult result = scanUniqueSignature(
          text, sectionSize, kRenderTargetSelectSignature, matchOffset);
        if (result == SignatureScanResult::UniqueMatch) {
          match = const_cast<uint8_t*>(text + matchOffset);
          return true;
        }

        if (result == SignatureScanResult::Ambiguous)
          Logger::err("IW5 render-pass bridge: render-target signature is ambiguous in .text.");
        else if (result == SignatureScanResult::InvalidPattern)
          Logger::err("IW5 render-pass bridge: invalid internal signature pattern.");
        else
          Logger::err("IW5 render-pass bridge: render-target signature was not found in .text.");
        break;
      }

      return false;
    }

    void __fastcall hookRenderTargetSelect(void* context, int renderTargetId) {
      struct ScopedPendingId {
        int32_t previous;
        ~ScopedPendingId() {
          g_pendingRenderTargetId = previous;
        }
      } restore { g_pendingRenderTargetId };

      g_pendingRenderTargetId = renderTargetId;
      g_originalRenderTargetSelect(context, renderTargetId);
    }

    int g_modulePinAnchor = 0;

  }


  int32_t getPendingRenderTargetId() {
    return g_pendingRenderTargetId;
  }


  bool initializeRenderTargetBridge() {
    std::lock_guard<std::mutex> lock(g_installMutex);
    if (g_hookInstalled)
      return true;

    if (!isSupportedSpExecutable()) {
      Logger::warn("IW5 render-pass bridge: disabled because the main executable is not the supported IW5 SP build.");
      return false;
    }

    HMODULE gameModule = ::GetModuleHandleW(nullptr);
    if (gameModule == nullptr) {
      Logger::err("IW5 render-pass bridge: failed to obtain the main executable module.");
      return false;
    }

    void* target = nullptr;
    if (!scanTextSection(gameModule, target)) {
      Logger::err("IW5 render-pass bridge: executable header or signature validation failed; hook not installed.");
      return false;
    }

    bool initializedMinHook = false;
    MH_STATUS status = MH_Initialize();
    if (status == MH_OK) {
      initializedMinHook = true;
    } else if (status != MH_ERROR_ALREADY_INITIALIZED) {
      Logger::err(str::format("IW5 render-pass bridge: MinHook initialization failed: ", MH_StatusToString(status)));
      return false;
    }

    status = MH_CreateHook(target,
      reinterpret_cast<void*>(&hookRenderTargetSelect),
      reinterpret_cast<void**>(&g_originalRenderTargetSelect));
    if (status != MH_OK) {
      Logger::err(str::format("IW5 render-pass bridge: MinHook could not create the detour: ", MH_StatusToString(status)));
      if (initializedMinHook)
        MH_Uninitialize();
      g_originalRenderTargetSelect = nullptr;
      return false;
    }

    // The detour points into this DLL and must remain mapped for the lifetime
    // of the process. Pin the module rather than risk a dangling game-code hook.
    HMODULE pinnedModule = nullptr;
    if (!::GetModuleHandleExW(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
          reinterpret_cast<LPCWSTR>(&g_modulePinAnchor),
          &pinnedModule)) {
      Logger::err("IW5 render-pass bridge: failed to pin the D3D9 module for hook lifetime.");
      MH_RemoveHook(target);
      if (initializedMinHook)
        MH_Uninitialize();
      g_originalRenderTargetSelect = nullptr;
      return false;
    }

    status = MH_EnableHook(target);
    if (status != MH_OK) {
      Logger::err(str::format("IW5 render-pass bridge: MinHook could not enable the detour: ", MH_StatusToString(status)));
      MH_RemoveHook(target);
      if (initializedMinHook)
        MH_Uninitialize();
      g_originalRenderTargetSelect = nullptr;
      ::FreeLibrary(pinnedModule);
      return false;
    }

    g_hookInstalled = true;
    Logger::info("IW5 render-pass bridge: installed for the verified SP build.");
    return true;
  }

}
