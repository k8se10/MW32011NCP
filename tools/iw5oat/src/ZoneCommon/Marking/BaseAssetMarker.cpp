#include "BaseAssetMarker.h"
#include "Utils/Logging/Log.h"
#include "Utils/PointerSanity.h"

#include <algorithm>
#include <cassert>
#include <cstdint>

BaseAssetMarker::BaseAssetMarker(AssetVisitor& visitor)
    : m_visitor(visitor)
{
}

void BaseAssetMarker::Mark_ScriptString(scr_string_t& scriptString) const
{
    const auto result = m_visitor.Visit_ScriptString(scriptString);
    if (result.has_value())
        scriptString = *result;
}

void BaseAssetMarker::MarkArray_ScriptString(scr_string_t* scriptStringArray, const size_t count) const
{
    assert(scriptStringArray != nullptr);

    // MW32011NCP / iw5oat, 2026-09-17: real, page-heap-confirmed out-of-bounds
    // read found here -- `scriptStringArray` is expected to point into a
    // trailing inline array inside the owning WeaponFullDef/etc. (per
    // IW5_Assets.h's own struct layout, the counts genuinely do match), but
    // for a specific WeaponDef instance the resolved pointer's real backing
    // allocation was smaller than `count` elements -- a genuine pointer-
    // resolution bug upstream (ZoneInputStream), not a struct-shape mismatch.
    // VirtualQuery only proves page-level readability, not the real element
    // count, so this must be checked per-element rather than once up front
    // (fastfile_format_research.md SS5.47/5.48, parent repo).
    for (size_t index = 0; index < count; index++)
    {
        if (!pointer_sanity::IsLikelyReadablePointer(&scriptStringArray[index]))
        {
            con::warn("BaseAssetMarker::MarkArray_ScriptString: element {} of {} does not look like a "
                      "valid, readable address ({:#x}) -- stopping this array instead of crashing "
                      "(see fastfile_format_research.md SS5.47, parent repo).",
                      index,
                      count,
                      reinterpret_cast<uintptr_t>(&scriptStringArray[index]));
            break;
        }

        Mark_ScriptString(scriptStringArray[index]);
    }
}

void BaseAssetMarker::Mark_IndirectAssetRef(const asset_type_t assetType, const char* assetName) const
{
    // MW32011NCP / iw5oat, 2026-09-17: this is the one, central function every
    // MarkArray_IndirectAssetRef/Mark_X-generated call site across every asset
    // type funnels an indirect-asset-name pointer through -- the highest-leverage
    // fix point in this whole class of bug (fastfile_format_research.md
    // SS5.47/5.48, parent repo). This had NO validity guard at all before now;
    // `!assetName[0]` already dereferences it unconditionally.
    if (!assetName)
        return;

    if (!pointer_sanity::IsLikelyReadablePointer(assetName))
    {
        con::warn("BaseAssetMarker::Mark_IndirectAssetRef: name for asset type {} does not look like a "
                  "valid pointer ({:#x}) -- treating as absent instead of crashing "
                  "(see fastfile_format_research.md SS5.47, parent repo).",
                  static_cast<int>(assetType),
                  reinterpret_cast<uintptr_t>(assetName));
        return;
    }

    if (!assetName[0])
        return;

    m_visitor.Visit_IndirectAssetRef(assetType, assetName);
}

void BaseAssetMarker::MarkArray_IndirectAssetRef(const asset_type_t assetType, const char** assetNames, const size_t count) const
{
    assert(assetNames != nullptr);

    for (size_t index = 0; index < count; index++)
        Mark_IndirectAssetRef(assetType, assetNames[index]);
}
