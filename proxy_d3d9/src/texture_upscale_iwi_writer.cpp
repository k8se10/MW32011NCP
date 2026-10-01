#include "texture_upscale_iwi_writer.h"

#include <cstdlib>
#include <cstring>

namespace TextureUpscaleIwi
{
namespace
{
#pragma pack(push, 1)
    struct RawIwiVersionHeader
    {
        char tag[3];
        char version;
    };

    // Field order/types match image::iwi8::IwiHeader (tools/iw5oat/src/ObjImage/
    // Image/IwiTypes.h) exactly -- natural MSVC alignment already produces the
    // real, disassembly-confirmed byte offsets with zero implicit padding
    // (verified via the static_assert below), so #pragma pack(1) here is a
    // belt-and-suspenders guarantee, not a correction of anything.
    struct RawIwiHeader
    {
        uint32_t flags;
        int8_t format;
        int8_t unused;
        uint16_t dimensions[3];
        uint32_t fileSizeForPicmip[4];
    };
#pragma pack(pop)

    static_assert(sizeof(RawIwiVersionHeader) == 4, "IWI version header must be exactly 4 bytes");
    static_assert(sizeof(RawIwiHeader) == 28, "IWI header must be exactly 28 bytes (32 total with the version header)");
    constexpr int kMaxPicmipEntries = 4; // matches RawIwiHeader::fileSizeForPicmip's real, fixed size
}

uint8_t* EncodeIwi8(uint16_t width, uint16_t height, uint16_t depth, Format format,
                     const MipLevel* mipsLargestFirst, int mipCount, uint32_t* outSize)
{
    if (outSize) *outSize = 0;
    // Real fix (2026-10-01): mipCount is NOT capped at kMaxPicmipEntries (4).
    // That cap was a real, live-confirmed bug -- it made this encoder only
    // ever able to produce a TRUNCATED mip chain (at most 4 levels), which a
    // caller's own "generate the full real pyramid" change then collided
    // with: the native engine independently computes how many real mip
    // levels a texture of a given width/height SHOULD have (standard
    // complete-mipmap-chain convention, matching 0 passed to D3D9's own
    // CreateTexture) and tries to LockRect every one of them regardless of
    // how many we actually created -- a texture capped at 4 real levels but
    // expected to have far more (e.g. 13 for a 4096x4096 texture) crashed/
    // corrupted with D3DERR_INVALIDCALL on the out-of-range Level indices.
    // The REST of this function was already correct for mipCount > 4 (see
    // the fileSizeForPicmip accumulation loop below, which already guards
    // `arrIdx < kMaxPicmipEntries` per-entry, matching the real official
    // IwiWriter8.cpp's own `currentMipLevel < extent_v<fileSizeForPicmip>`
    // guard -- it only ever recorded a checkpoint for the top/largest 4
    // levels, same real semantics either way) -- this was the one actual
    // blocker. kMaxRealMipLevels below is a generous, non-format-mandated
    // array-sizing bound (not a real native limit, unlike kMaxPicmipEntries
    // itself), comfortably covering any texture size this pipeline could
    // ever produce.
    constexpr int kMaxRealMipLevels = 24;
    if (!mipsLargestFirst || mipCount <= 0 || mipCount > kMaxRealMipLevels) return nullptr;
    if (width == 0 || height == 0 || depth == 0) return nullptr;

    uint32_t totalMipBytes = 0;
    for (int i = 0; i < mipCount; ++i) {
        if (!mipsLargestFirst[i].data || mipsLargestFirst[i].size == 0) return nullptr;
        totalMipBytes += mipsLargestFirst[i].size;
    }

    const uint32_t totalSize = static_cast<uint32_t>(sizeof(RawIwiVersionHeader)) +
                                static_cast<uint32_t>(sizeof(RawIwiHeader)) + totalMipBytes;
    uint8_t* buffer = static_cast<uint8_t*>(malloc(totalSize));
    if (!buffer) return nullptr;

    RawIwiVersionHeader versionHeader{};
    versionHeader.tag[0] = 'I';
    versionHeader.tag[1] = 'W';
    versionHeader.tag[2] = 'i';
    versionHeader.version = 8;
    memcpy(buffer, &versionHeader, sizeof(versionHeader));

    RawIwiHeader header{};
    header.flags = 0;
    header.format = static_cast<int8_t>(format);
    header.unused = 0;
    header.dimensions[0] = width;
    header.dimensions[1] = height;
    header.dimensions[2] = depth;
    if (mipCount <= 1) header.flags |= 0x2; // IMG_FLAG_NOMIPMAPS (iwi8::IwiFlags), matches
                                             // IwiWriter8.cpp's own !HasMipMaps() case

    // Real writer's own accumulation logic (IwiWriter8.cpp DumpImage) --
    // REAL BUG FOUND AND FIXED, 2026-09-28: a live test hit the real engine's
    // own "Image file corrupt." error (FUN_1401bae80's `if (local_58[0] !=
    // lVar3)` check, `lVar3` = the real total file size). `fileSizeForPicmip`
    // is indexed by REAL semantic mip level (0 = largest/base), and each
    // entry is the CUMULATIVE size of that level and every SMALLER level
    // below it -- so `fileSizeForPicmip[0]` must equal the TOTAL file size
    // (base level needs everything), not "after the first mip processed."
    // The earlier version of this loop mapped its own `realMipLevel` (0 =
    // smallest) directly onto the array index -- exactly backwards. Fixed by
    // accumulating from the smallest array slot (mipCount-1, the real
    // smallest mip) up to index 0 (the real base/largest mip, matching
    // `mipsLargestFirst[0]`'s own documented meaning) -- array index and real
    // mip level are the SAME number for this struct, no reversal needed once
    // the accumulation direction itself is correct.
    uint32_t runningSize = static_cast<uint32_t>(sizeof(versionHeader) + sizeof(header));
    for (int arrIdx = mipCount - 1; arrIdx >= 0; --arrIdx) {
        runningSize += mipsLargestFirst[arrIdx].size;
        if (arrIdx < kMaxPicmipEntries) header.fileSizeForPicmip[arrIdx] = runningSize;
    }

    memcpy(buffer + sizeof(versionHeader), &header, sizeof(header));

    // Real mip data, smallest-first (matches the real on-disk order and the
    // real reader, FUN_1401bae80, which streams starting from the smallest
    // stored mip).
    uint8_t* cursor = buffer + sizeof(versionHeader) + sizeof(header);
    for (int realMipLevel = 0; realMipLevel < mipCount; ++realMipLevel) {
        const MipLevel& mip = mipsLargestFirst[mipCount - 1 - realMipLevel];
        memcpy(cursor, mip.data, mip.size);
        cursor += mip.size;
    }

    if (outSize) *outSize = totalSize;
    return buffer;
}

}
