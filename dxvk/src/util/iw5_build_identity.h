#pragma once

#include <cstddef>
#include <cstdint>

namespace dxvk::iw5 {

  /**
   * \brief Checks a bounded PE image header against the supported IW5 SP identity
   *
   * The image pointer must refer to a readable PE image buffer and imageSize
   * must describe the readable range beginning at that pointer.
   */
  bool hasSupportedSpImageIdentity(const uint8_t* image, size_t imageSize);

  /**
   * \brief Checks whether the current process is the supported IW5 SP build
   *
   * Returns false on non-Windows platforms, unreadable/invalid PE headers,
   * unexpected executable names, or a mismatched PE identity.
   */
  bool isSupportedSpExecutable();

}
