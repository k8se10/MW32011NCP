#include "iw5_build_identity.h"

#include "util_env.h"

#include <limits>

#ifdef _WIN32
#include "./com/com_include.h"
#endif

namespace dxvk::iw5 {

  namespace {

    constexpr uint16_t kDosSignature = 0x5a4d;
    constexpr uint32_t kNtSignature = 0x00004550;
    constexpr uint16_t kAmd64Machine = 0x8664;
    constexpr uint16_t kPe32PlusMagic = 0x020b;
    constexpr uint32_t kPeTimestamp = 0x6a743a58;
    constexpr uint32_t kImageSize = 0x044be000;
    constexpr size_t kMaxNtHeaderOffset = 1024 * 1024;

    template<typename T>
    bool readValue(const uint8_t* image, size_t imageSize, size_t offset, T& value) {
      if (offset > imageSize || sizeof(T) > imageSize - offset)
        return false;

      uint64_t decoded = 0;
      for (size_t i = 0; i < sizeof(T); i++)
        decoded |= uint64_t(image[offset + i]) << (i * 8);
      value = T(decoded);
      return true;
    }

    bool isReadableImageRange(const MEMORY_BASIC_INFORMATION& info, HMODULE module) {
      const DWORD protection = info.Protect & 0xffu;
      const bool readable = protection == PAGE_READONLY
        || protection == PAGE_READWRITE
        || protection == PAGE_WRITECOPY
        || protection == PAGE_EXECUTE_READ
        || protection == PAGE_EXECUTE_READWRITE
        || protection == PAGE_EXECUTE_WRITECOPY;

      return info.AllocationBase == module
          && info.Type == MEM_IMAGE
          && info.State == MEM_COMMIT
          && !(info.Protect & PAGE_GUARD)
          && readable;
    }

  }


  bool hasSupportedSpImageIdentity(const uint8_t* image, size_t imageSize) {
    if (image == nullptr || imageSize < 0x40)
      return false;

    uint16_t dosSignature = 0;
    uint32_t ntOffset = 0;
    if (!readValue(image, imageSize, 0, dosSignature)
     || dosSignature != kDosSignature
     || !readValue(image, imageSize, 0x3c, ntOffset)
     || ntOffset < 0x40
     || ntOffset > kMaxNtHeaderOffset)
      return false;

    uint32_t ntSignature = 0;
    uint16_t machine = 0;
    uint32_t timestamp = 0;
    uint16_t optionalHeaderSize = 0;
    if (!readValue(image, imageSize, ntOffset, ntSignature)
     || ntSignature != kNtSignature
     || !readValue(image, imageSize, size_t(ntOffset) + 4, machine)
     || !readValue(image, imageSize, size_t(ntOffset) + 8, timestamp)
     || !readValue(image, imageSize, size_t(ntOffset) + 20, optionalHeaderSize)
     || machine != kAmd64Machine
     || timestamp != kPeTimestamp
     || optionalHeaderSize < 64)
      return false;

    const size_t optionalHeaderOffset = size_t(ntOffset) + 24;
    const size_t optionalHeaderEnd = optionalHeaderOffset + optionalHeaderSize;
    if (optionalHeaderEnd < optionalHeaderOffset || optionalHeaderEnd > imageSize)
      return false;

    uint16_t optionalHeaderMagic = 0;
    uint32_t imageSizeFromHeader = 0;
    uint32_t headersSize = 0;
    if (!readValue(image, imageSize, optionalHeaderOffset, optionalHeaderMagic)
     || !readValue(image, imageSize, optionalHeaderOffset + 56, imageSizeFromHeader)
     || !readValue(image, imageSize, optionalHeaderOffset + 60, headersSize)
     || optionalHeaderMagic != kPe32PlusMagic
     || imageSizeFromHeader != kImageSize
     || headersSize < optionalHeaderEnd
     || headersSize > imageSizeFromHeader)
      return false;

    return true;
  }


  bool isSupportedSpExecutable() {
#ifdef _WIN32
    const std::string exeName = env::getExeName();
    if (!str::compareCaseInsensitive(exeName.c_str(), "iw5sp.exe"))
      return false;

    HMODULE module = ::GetModuleHandleW(nullptr);
    if (module == nullptr)
      return false;

    MEMORY_BASIC_INFORMATION info = {};
    if (!::VirtualQuery(module, &info, sizeof(info))
     || !isReadableImageRange(info, module)
     || info.BaseAddress != module)
      return false;

    const uintptr_t moduleAddress = reinterpret_cast<uintptr_t>(module);
    const uintptr_t regionAddress = reinterpret_cast<uintptr_t>(info.BaseAddress);
    if (info.RegionSize > std::numeric_limits<uintptr_t>::max() - regionAddress)
      return false;

    size_t readableSize = size_t(info.RegionSize);
    if (readableSize > kMaxNtHeaderOffset + 4096)
      readableSize = kMaxNtHeaderOffset + 4096;

    return hasSupportedSpImageIdentity(
      reinterpret_cast<const uint8_t*>(moduleAddress), readableSize);
#else
    return false;
#endif
  }

}
