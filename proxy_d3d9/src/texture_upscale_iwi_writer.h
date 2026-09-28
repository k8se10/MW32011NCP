#pragma once

#include <cstdint>

// Standalone real-IWI-v8 (IW4/IW5) encoder -- runtime texture-upscale-cache
// feature groundwork (re_notes/x64_migration/texture_upscale_cache_research.md).
// Ported from this project's own tools/iw5oat/src/ObjImage/Image/IwiWriter8.cpp
// (the OpenAssetTools fork this project already vendors and maintains), stripped
// of that project's Texture/Texture2D/Texture3D/TextureCube object model down to
// a flat, no-STL API matching this project's own shipped-code convention
// (asset_capture.cpp's own header comment: "non-STL, same convention every
// other shipped file in this project already follows").
//
// Real, disassembly-verified layout this encoder targets (this project's own
// independent RE of iw5sp.exe's FUN_1401bae80, cross-validated byte-for-byte
// against IwiTypes.h's own community-verified iwi8::IwiHeader struct --
// texture_upscale_cache_research.md's "DEFINITIVE HEADER LAYOUT" round):
//   0x00-0x02  "IWi" tag
//   0x03       version byte (8)
//   0x04-0x07  flags (uint32_t)
//   0x08       format (int8_t, image::iwi8::IwiFormat)
//   0x09       unused (int8_t)
//   0x0A-0x0B  width  (uint16_t)
//   0x0C-0x0D  height (uint16_t)
//   0x0E-0x0F  depth  (uint16_t, 1 for a normal 2D texture)
//   0x10-0x1F  fileSizeForPicmip[4] (uint32_t x4 -- CUMULATIVE running total of
//              header+mips written so far, indexed by mip level, matching the
//              real writer's own accumulation logic exactly, not a per-mip size)
//   0x20...    mip data, SMALLEST mip first (matches the real streaming design
//              and the real writer's own iteration order)

namespace TextureUpscaleIwi
{
    // Mirrors image::iwi8::IwiFormat (IwiTypes.h) for the formats this feature
    // actually needs -- DXT1/3/5 (the three FourCCs already confirmed live via
    // the first-N CreateTexture caller trace) plus the two raw bitmap formats,
    // not the full original enum (wavelet/DXN/etc. are out of scope for this
    // feature, per texture_upscale_cache_research.md).
    enum class Format : int8_t
    {
        Invalid = 0x0,
        BitmapRGBA = 0x1,
        BitmapRGB = 0x2,
        DXT1 = 0xB,
        DXT3 = 0xC,
        DXT5 = 0xD,
    };

    // One mip level's raw, already-encoded (DXT-compressed or raw bitmap,
    // matching `format`) pixel data. The caller owns this memory; the encoder
    // only reads it.
    struct MipLevel
    {
        const uint8_t* data;
        uint32_t size;
    };

    // Encodes a real, valid IWI-v8 file into a newly malloc'd buffer (matching
    // this project's own non-STL, malloc-based shipped-code convention --
    // asset_capture.cpp's own WriteDdsFile precedent). Caller owns and must
    // free() the returned buffer. Returns nullptr (outSize left at 0) on any
    // invalid input (mipCount <= 0, mipCount > 4 -- this format's own real
    // fileSizeForPicmip table only has 4 slots, matching the real writer's own
    // `currentMipLevel < extent_v<fileSizeForPicmip>` guard -- a null mip
    // buffer, or width/height/depth that don't fit uint16_t).
    //
    // `mipsLargestFirst` must be ordered largest-mip-first (index 0 = the full
    // real resolution, matching this feature's own natural "we just upscaled
    // the base image" order) -- the encoder itself reverses this into the real
    // on-disk smallest-first order, matching IwiWriter8.cpp's own iteration.
    uint8_t* EncodeIwi8(uint16_t width, uint16_t height, uint16_t depth, Format format,
                         const MipLevel* mipsLargestFirst, int mipCount, uint32_t* outSize);
}
