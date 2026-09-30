#pragma once

#include <cstddef>
#include <cstdint>

namespace dxvk::iw5 {

  enum class SignatureScanResult {
    UniqueMatch,
    NotFound,
    Ambiguous,
    InvalidPattern,
    InvalidRange,
  };

  SignatureScanResult scanUniqueSignature(
    const uint8_t* image,
    size_t imageSize,
    const char* signature,
    size_t& matchOffset);

}
