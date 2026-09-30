#include "iw5_signature_scan.h"

#include <vector>

namespace dxvk::iw5 {

  namespace {

    bool parseSignature(const char* text, std::vector<int16_t>& pattern) {
      if (text == nullptr)
        return false;

      pattern.clear();
      while (*text != '\0') {
        while (*text == ' ')
          text++;

        if (*text == '\0')
          break;

        const char highChar = text[0];
        const char lowChar = text[1];
        if (lowChar == '\0' || (text[2] != '\0' && text[2] != ' '))
          return false;

        if (highChar == '?' && lowChar == '?') {
          pattern.push_back(-1);
        } else {
          auto hexValue = [] (char value) -> int32_t {
            if (value >= '0' && value <= '9')
              return value - '0';
            if (value >= 'A' && value <= 'F')
              return value - 'A' + 10;
            if (value >= 'a' && value <= 'f')
              return value - 'a' + 10;
            return -1;
          };

          const int32_t high = hexValue(highChar);
          const int32_t low = hexValue(lowChar);
          if (high < 0 || low < 0)
            return false;
          pattern.push_back(int16_t((high << 4) | low));
        }

        text += 2;
        if (*text == ' ')
          text++;
      }

      return !pattern.empty();
    }

  }


  SignatureScanResult scanUniqueSignature(
    const uint8_t* image,
    size_t imageSize,
    const char* signature,
    size_t& matchOffset) {
    matchOffset = 0;
    std::vector<int16_t> pattern;
    if (!parseSignature(signature, pattern))
      return SignatureScanResult::InvalidPattern;
    if (image == nullptr || imageSize < pattern.size())
      return SignatureScanResult::InvalidRange;

    size_t matches = 0;
    for (size_t offset = 0; offset <= imageSize - pattern.size(); offset++) {
      bool equal = true;
      for (size_t i = 0; i < pattern.size(); i++) {
        if (pattern[i] >= 0 && image[offset + i] != uint8_t(pattern[i])) {
          equal = false;
          break;
        }
      }

      if (equal) {
        matchOffset = offset;
        if (++matches > 1)
          return SignatureScanResult::Ambiguous;
      }
    }

    return matches == 1
      ? SignatureScanResult::UniqueMatch
      : SignatureScanResult::NotFound;
  }

}
