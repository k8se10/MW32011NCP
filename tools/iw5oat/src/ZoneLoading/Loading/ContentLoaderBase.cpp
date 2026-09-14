#include "ContentLoaderBase.h"

#include <cassert>
#include <cstdint>
#include <limits>

ContentLoaderBase::ContentLoaderBase(Zone& zone, ZoneInputStream& stream)
    : varXString(nullptr),
      m_zone(zone),
      m_memory(zone.Memory()),
      m_stream(stream),

      // -1
      m_zone_ptr_following(
          reinterpret_cast<const void*>(std::numeric_limits<std::uintptr_t>::max() >> ((sizeof(std::uintptr_t) * 8u) - stream.GetPointerBitCount()))),

      // -2
      m_zone_ptr_insert(
          reinterpret_cast<const void*>((std::numeric_limits<std::uintptr_t>::max() >> ((sizeof(std::uintptr_t) * 8u) - stream.GetPointerBitCount())) - 1u))
{
}

void ContentLoaderBase::LoadXString(const bool atStreamStart) const
{
    assert(varXString != nullptr);

    if (atStreamStart)
        m_stream.Load<const char*>(varXString);

    if (*varXString != nullptr)
    {
        if (GetZonePointerType(*varXString) == ZonePointerType::FOLLOWING)
        {
            *varXString = m_stream.Alloc<const char>(1);
            m_stream.LoadNullTerminated(const_cast<char*>(*varXString));
        }
        else
        {
            *varXString = m_stream.ConvertOffsetToPointerNative<const char>(*varXString);
        }
    }
}

void ContentLoaderBase::LoadXStringArray(const bool atStreamStart, const size_t count)
{
    assert(varXString != nullptr);

#ifdef ARCH_x86
    if (atStreamStart)
        m_stream.Load<const char*>(varXString, count);
#else
    if (atStreamStart)
    {
        // MW32011NCP / iw5oat, 2026-09-14: this ARCH_x64 branch is shared across
        // every game (ContentLoaderBase, not a per-game file) and still hardcoded
        // a 4u (x86 pointer) stride/offset for a plain array of string pointers --
        // confirmed live as the actual cause of the "invalid block 15" error seen
        // extracting real x64 zones (LoadScriptStringList's own string array is
        // read via this exact function). Fixed to use the stream's own already-
        // configured real pointer width (m_pointer_byte_count, driven by the
        // pointerBitCount each ZoneLoaderFactory passes in -- 64u for this fork's
        // IW5 x64 target, see ZoneLoaderFactoryIW5.cpp) instead of an x86-only
        // constant, so this stays correct for whatever pointer width the active
        // game/arch actually uses. Full trail: re_notes/x64_migration/
        // fastfile_format_research.md (parent repo) SS5.6.
        const auto pointerByteCount = m_stream.GetPointerBitCount() / 8u;
        const auto fill = m_stream.LoadWithFill(pointerByteCount * count);

        for (size_t index = 0; index < count; index++)
        {
            fill.FillPtr(varXString[index], pointerByteCount * index);
            m_stream.AddPointerLookup(&varXString[index], fill.BlockBuffer(pointerByteCount * index));
        }
    }
#endif

    for (size_t index = 0; index < count; index++)
    {
        LoadXString(false);
        varXString++;
    }
}

ZonePointerType ContentLoaderBase::GetZonePointerType(const void* zonePtr) const
{
    if (zonePtr == m_zone_ptr_following)
        return ZonePointerType::FOLLOWING;
    if (zonePtr == m_zone_ptr_insert)
        return ZonePointerType::INSERT;

    return ZonePointerType::OFFSET;
}
