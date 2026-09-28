#pragma once

#include <cstdint>

// Standard, public BC1/BC2/BC3 (DXT1/DXT3/DXT5) block decoder/encoder --
// runtime texture-upscale-cache feature groundwork
// (re_notes/x64_migration/texture_upscale_cache_research.md). Textbook block-
// compression algorithms (Khronos/Microsoft's own published specs), not
// derived from any disassembly -- needed to get real RGBA pixels out of a
// real source .iwi's DXT-compressed mip data before feeding it to the
// upscaler, and to re-compress the upscaled result back for storage. No-STL,
// malloc-based (matching this project's own established shipped-code
// convention).
namespace TextureUpscaleDxt
{
    enum class BlockFormat
    {
        BC1, // DXT1 -- 8 bytes/block, opaque or 1-bit alpha
        BC2, // DXT3 -- 16 bytes/block, explicit 4-bit alpha
        BC3, // DXT5 -- 16 bytes/block, interpolated alpha
    };

    // Decodes a full BC1/BC2/BC3-compressed image into a newly malloc'd RGBA8
    // buffer (width*height*4 bytes, row-major, top-to-bottom -- caller must
    // free()). width/height need not be block-aligned (a real texture's
    // smallest mips can be smaller than 4x4) -- partial edge blocks are
    // decoded fully and simply clipped when copied into the output buffer.
    // Returns nullptr on invalid input (null data, zero dimensions).
    uint8_t* DecodeToRGBA(const uint8_t* blockData, uint32_t width, uint32_t height, BlockFormat format);

    // Encodes an RGBA8 buffer (width*height*4 bytes) into BC1/BC3-compressed
    // block data in a newly malloc'd buffer (caller must free()). Uses a
    // simple, real (not a stub) min/max-endpoint block compressor -- not
    // competitive with a production encoder's PSNR, but correct and fast,
    // appropriate for a cache-population background thread rather than a
    // real-time path. BC2 encoding is not implemented (this feature only
    // ever needs to round-trip formats it already decodes; BC2/DXT3's
    // explicit 4-bit alpha is rare in practice compared to BC1/BC3 -- real
    // source textures observed live so far are BC1/BC3, not BC2).
    uint8_t* EncodeFromRGBA(const uint8_t* rgba, uint32_t width, uint32_t height, BlockFormat format, uint32_t* outSize);

    // Real, standard formula for a BC1/BC2/BC3-compressed image's total byte
    // size at a given resolution -- ceil(width/4)*ceil(height/4)*bytesPerBlock.
    uint32_t CompressedSize(uint32_t width, uint32_t height, BlockFormat format);
}
