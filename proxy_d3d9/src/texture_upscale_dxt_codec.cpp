#include "texture_upscale_dxt_codec.h"

#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace TextureUpscaleDxt
{
namespace
{
    struct Rgb { uint8_t r, g, b; };

    Rgb Unpack565(uint16_t v)
    {
        Rgb c;
        c.r = static_cast<uint8_t>(((v >> 11) & 0x1F) * 255 / 31);
        c.g = static_cast<uint8_t>(((v >> 5) & 0x3F) * 255 / 63);
        c.b = static_cast<uint8_t>((v & 0x1F) * 255 / 31);
        return c;
    }

    uint16_t Pack565(uint8_t r, uint8_t g, uint8_t b)
    {
        return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }

    void PutPixel(uint8_t* out, uint32_t width, uint32_t height, int x, int y,
                  uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= width || static_cast<uint32_t>(y) >= height) return;
        uint8_t* p = out + (static_cast<size_t>(y) * width + x) * 4;
        p[0] = r; p[1] = g; p[2] = b; p[3] = a;
    }

    // Decodes one 4x4 BC1-style color block (shared by BC1/BC2/BC3's own
    // color half) into `outColors[16]` RGB + a per-pixel alpha multiplier
    // flag (bc1Punchthrough: true if this specific pixel index is the
    // BC1 3-color-mode "transparent black" case, only relevant for real BC1).
    void DecodeColorBlock(const uint8_t* block, Rgb outColors[16], bool punchThrough[16], bool allowPunchThrough)
    {
        uint16_t c0raw, c1raw;
        memcpy(&c0raw, block, 2);
        memcpy(&c1raw, block + 2, 2);
        uint32_t indices;
        memcpy(&indices, block + 4, 4);

        Rgb c0 = Unpack565(c0raw);
        Rgb c1 = Unpack565(c1raw);
        Rgb palette[4];
        palette[0] = c0;
        palette[1] = c1;
        bool fourColorMode = !allowPunchThrough || c0raw > c1raw;
        if (fourColorMode) {
            palette[2].r = static_cast<uint8_t>((2 * c0.r + c1.r) / 3);
            palette[2].g = static_cast<uint8_t>((2 * c0.g + c1.g) / 3);
            palette[2].b = static_cast<uint8_t>((2 * c0.b + c1.b) / 3);
            palette[3].r = static_cast<uint8_t>((c0.r + 2 * c1.r) / 3);
            palette[3].g = static_cast<uint8_t>((c0.g + 2 * c1.g) / 3);
            palette[3].b = static_cast<uint8_t>((c0.b + 2 * c1.b) / 3);
        } else {
            palette[2].r = static_cast<uint8_t>((c0.r + c1.r) / 2);
            palette[2].g = static_cast<uint8_t>((c0.g + c1.g) / 2);
            palette[2].b = static_cast<uint8_t>((c0.b + c1.b) / 2);
            palette[3] = { 0, 0, 0 }; // transparent black
        }

        for (int i = 0; i < 16; ++i) {
            uint32_t idx = (indices >> (i * 2)) & 0x3;
            outColors[i] = palette[idx];
            punchThrough[i] = (!fourColorMode && idx == 3);
        }
    }
}

uint8_t* DecodeToRGBA(const uint8_t* blockData, uint32_t width, uint32_t height, BlockFormat format)
{
    if (!blockData || width == 0 || height == 0) return nullptr;

    uint8_t* out = static_cast<uint8_t*>(malloc(static_cast<size_t>(width) * height * 4));
    if (!out) return nullptr;

    uint32_t blocksX = (width + 3) / 4;
    uint32_t blocksY = (height + 3) / 4;
    uint32_t bytesPerBlock = (format == BlockFormat::BC1) ? 8 : 16;
    const uint8_t* src = blockData;

    for (uint32_t by = 0; by < blocksY; ++by) {
        for (uint32_t bx = 0; bx < blocksX; ++bx) {
            Rgb colors[16];
            bool punchThrough[16];
            uint8_t alphas[16];

            if (format == BlockFormat::BC1) {
                DecodeColorBlock(src, colors, punchThrough, /*allowPunchThrough=*/true);
                for (int i = 0; i < 16; ++i) alphas[i] = punchThrough[i] ? 0 : 255;
            } else if (format == BlockFormat::BC2) {
                // 8 bytes explicit 4-bit alpha, then 8 bytes color (always 4-color mode)
                for (int i = 0; i < 16; ++i) {
                    int byteIdx = i / 2;
                    uint8_t nibble = (i % 2 == 0) ? (src[byteIdx] & 0xF) : (src[byteIdx] >> 4);
                    alphas[i] = static_cast<uint8_t>(nibble * 17); // 4-bit -> 8-bit (0..15 -> 0..255)
                }
                DecodeColorBlock(src + 8, colors, punchThrough, /*allowPunchThrough=*/false);
            } else { // BC3
                uint8_t a0 = src[0], a1 = src[1];
                uint64_t alphaIndices = 0;
                for (int b = 0; b < 6; ++b) alphaIndices |= static_cast<uint64_t>(src[2 + b]) << (8 * b);
                uint8_t alphaPalette[8];
                alphaPalette[0] = a0; alphaPalette[1] = a1;
                if (a0 > a1) {
                    for (int i = 1; i <= 6; ++i)
                        alphaPalette[1 + i] = static_cast<uint8_t>(((7 - i) * a0 + i * a1) / 7);
                } else {
                    for (int i = 1; i <= 4; ++i)
                        alphaPalette[1 + i] = static_cast<uint8_t>(((5 - i) * a0 + i * a1) / 5);
                    alphaPalette[6] = 0;
                    alphaPalette[7] = 255;
                }
                for (int i = 0; i < 16; ++i) {
                    uint32_t idx = static_cast<uint32_t>((alphaIndices >> (i * 3)) & 0x7);
                    alphas[i] = alphaPalette[idx];
                }
                DecodeColorBlock(src + 8, colors, punchThrough, /*allowPunchThrough=*/false);
            }

            for (int py = 0; py < 4; ++py) {
                for (int px = 0; px < 4; ++px) {
                    int idx = py * 4 + px;
                    PutPixel(out, width, height, bx * 4 + px, by * 4 + py,
                             colors[idx].r, colors[idx].g, colors[idx].b, alphas[idx]);
                }
            }

            src += bytesPerBlock;
        }
    }

    return out;
}

uint32_t CompressedSize(uint32_t width, uint32_t height, BlockFormat format)
{
    uint32_t blocksX = (width + 3) / 4;
    uint32_t blocksY = (height + 3) / 4;
    uint32_t bytesPerBlock = (format == BlockFormat::BC1) ? 8 : 16;
    return blocksX * blocksY * bytesPerBlock;
}

namespace
{
    // Simple, real "range fit" BC1 color-block encoder: min/max corners of
    // the block's own real RGB bounding box become the two endpoints, every
    // pixel picks the nearest of the resulting 4-color palette. Not a
    // production-quality encoder (no cluster-fit/PCA), but correct and fast
    // -- appropriate for a background cache-population thread.
    void EncodeColorBlock(const uint8_t* rgbaBlock16, uint8_t* outBlock8)
    {
        uint8_t minR = 255, minG = 255, minB = 255, maxR = 0, maxG = 0, maxB = 0;
        for (int i = 0; i < 16; ++i) {
            uint8_t r = rgbaBlock16[i * 4 + 0], g = rgbaBlock16[i * 4 + 1], b = rgbaBlock16[i * 4 + 2];
            minR = std::min(minR, r); maxR = std::max(maxR, r);
            minG = std::min(minG, g); maxG = std::max(maxG, g);
            minB = std::min(minB, b); maxB = std::max(maxB, b);
        }
        uint16_t c0raw = Pack565(maxR, maxG, maxB);
        uint16_t c1raw = Pack565(minR, minG, minB);
        if (c0raw == c1raw) { // force 4-color mode even for a flat block
            if (c0raw < 0xFFFF) ++c0raw; else --c1raw;
        }
        if (c0raw < c1raw) std::swap(c0raw, c1raw); // guarantee 4-color mode (c0 > c1)

        Rgb palette[4];
        palette[0] = Unpack565(c0raw);
        palette[1] = Unpack565(c1raw);
        palette[2].r = static_cast<uint8_t>((2 * palette[0].r + palette[1].r) / 3);
        palette[2].g = static_cast<uint8_t>((2 * palette[0].g + palette[1].g) / 3);
        palette[2].b = static_cast<uint8_t>((2 * palette[0].b + palette[1].b) / 3);
        palette[3].r = static_cast<uint8_t>((palette[0].r + 2 * palette[1].r) / 3);
        palette[3].g = static_cast<uint8_t>((palette[0].g + 2 * palette[1].g) / 3);
        palette[3].b = static_cast<uint8_t>((palette[0].b + 2 * palette[1].b) / 3);

        uint32_t indices = 0;
        for (int i = 0; i < 16; ++i) {
            int r = rgbaBlock16[i * 4 + 0], g = rgbaBlock16[i * 4 + 1], b = rgbaBlock16[i * 4 + 2];
            int bestIdx = 0;
            int bestDist = INT32_MAX;
            for (int p = 0; p < 4; ++p) {
                int dr = r - palette[p].r, dg = g - palette[p].g, db = b - palette[p].b;
                int dist = dr * dr + dg * dg + db * db;
                if (dist < bestDist) { bestDist = dist; bestIdx = p; }
            }
            indices |= static_cast<uint32_t>(bestIdx) << (i * 2);
        }

        memcpy(outBlock8, &c0raw, 2);
        memcpy(outBlock8 + 2, &c1raw, 2);
        memcpy(outBlock8 + 4, &indices, 4);
    }

    void EncodeAlphaBlockBC3(const uint8_t* rgbaBlock16, uint8_t* outBlock8)
    {
        uint8_t minA = 255, maxA = 0;
        for (int i = 0; i < 16; ++i) {
            uint8_t a = rgbaBlock16[i * 4 + 3];
            minA = std::min(minA, a);
            maxA = std::max(maxA, a);
        }
        uint8_t a0 = maxA, a1 = minA; // a0 > a1 (or equal) -> real 8-value interpolation mode
        if (a0 == a1) { if (a0 < 255) ++a0; else --a1; }

        uint8_t alphaPalette[8];
        alphaPalette[0] = a0; alphaPalette[1] = a1;
        for (int i = 1; i <= 6; ++i)
            alphaPalette[1 + i] = static_cast<uint8_t>(((7 - i) * a0 + i * a1) / 7);

        uint64_t indices = 0;
        for (int i = 0; i < 16; ++i) {
            int a = rgbaBlock16[i * 4 + 3];
            int bestIdx = 0, bestDist = INT32_MAX;
            for (int p = 0; p < 8; ++p) {
                int d = a - alphaPalette[p];
                int dist = d * d;
                if (dist < bestDist) { bestDist = dist; bestIdx = p; }
            }
            indices |= static_cast<uint64_t>(bestIdx) << (i * 3);
        }

        outBlock8[0] = a0; outBlock8[1] = a1;
        for (int b = 0; b < 6; ++b) outBlock8[2 + b] = static_cast<uint8_t>((indices >> (8 * b)) & 0xFF);
    }
}

uint8_t* EncodeFromRGBA(const uint8_t* rgba, uint32_t width, uint32_t height, BlockFormat format, uint32_t* outSize)
{
    if (outSize) *outSize = 0;
    if (!rgba || width == 0 || height == 0 || format == BlockFormat::BC2) return nullptr; // BC2 not implemented (see header)

    uint32_t blocksX = (width + 3) / 4;
    uint32_t blocksY = (height + 3) / 4;
    uint32_t bytesPerBlock = (format == BlockFormat::BC1) ? 8 : 16;
    uint32_t totalSize = blocksX * blocksY * bytesPerBlock;

    uint8_t* out = static_cast<uint8_t*>(malloc(totalSize));
    if (!out) return nullptr;

    uint8_t rgbaBlock[16 * 4];
    uint8_t* dst = out;
    for (uint32_t by = 0; by < blocksY; ++by) {
        for (uint32_t bx = 0; bx < blocksX; ++bx) {
            // Gather the real 4x4 source block, clamping at the image edge
            // (repeats the last real pixel for any out-of-bounds sample --
            // simple, correct-enough edge handling for a cache-population
            // encoder, not a real-time path).
            for (int py = 0; py < 4; ++py) {
                for (int px = 0; px < 4; ++px) {
                    uint32_t sx = std::min(bx * 4 + px, width - 1);
                    uint32_t sy = std::min(by * 4 + py, height - 1);
                    const uint8_t* srcPixel = rgba + (static_cast<size_t>(sy) * width + sx) * 4;
                    memcpy(rgbaBlock + (py * 4 + px) * 4, srcPixel, 4);
                }
            }

            if (format == BlockFormat::BC3) {
                EncodeAlphaBlockBC3(rgbaBlock, dst);
                EncodeColorBlock(rgbaBlock, dst + 8);
            } else { // BC1
                EncodeColorBlock(rgbaBlock, dst);
            }
            dst += bytesPerBlock;
        }
    }

    if (outSize) *outSize = totalSize;
    return out;
}

}
