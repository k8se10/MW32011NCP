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
    if (!mipsLargestFirst || mipCount <= 0 || mipCount > kMaxPicmipEntries) return nullptr;
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

    // Real writer's own accumulation logic (IwiWriter8.cpp DumpImage), ported
    // verbatim: iterate SMALLEST mip first, running total starts at
    // sizeof(version)+sizeof(header), each entry in fileSizeForPicmip is the
    // CUMULATIVE size after that mip is included -- not that mip's own size
    // alone. mipsLargestFirst[0] is this feature's own largest/base mip
    // (index `mipCount-1` in the real on-disk smallest-first order), so the
    // real per-level index used for fileSizeForPicmip here is
    // `mipCount-1-i` where `i` walks mipsLargestFirst backwards.
    uint32_t runningSize = static_cast<uint32_t>(sizeof(versionHeader) + sizeof(header));
    for (int realMipLevel = 0; realMipLevel < mipCount; ++realMipLevel) {
        // realMipLevel 0 = smallest (last in mipsLargestFirst), ascending to
        // mipCount-1 = largest/base (mipsLargestFirst[0]) -- matches the real
        // writer's own descending-currentMipLevel loop exactly, just indexed
        // from the opposite end since our own input order is reversed from
        // the real on-disk order.
        const MipLevel& mip = mipsLargestFirst[mipCount - 1 - realMipLevel];
        runningSize += mip.size;
        if (realMipLevel < kMaxPicmipEntries) header.fileSizeForPicmip[realMipLevel] = runningSize;
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
