#pragma once

#include "Game/IAsset.h"
#include "Marking/AssetVisitor.h"
#include "Utils/PointerSanity.h"
#include "Zone/ZoneTypes.h"

#include <cstdint>
#include <type_traits>

class BaseAssetMarker
{
protected:
    explicit BaseAssetMarker(AssetVisitor& visitor);

    template<AssetDefinition Asset_t> void Mark_Dependency(std::add_lvalue_reference_t<std::add_pointer_t<typename Asset_t::Type>> asset)
    {
        // MW32011NCP / iw5oat, 2026-09-17: `asset` here is the dependency's own resolved
        // pointer (out of ZoneInputStream's forward-reference/alias-lookup machinery) --
        // AssetName<Asset_t>(*asset) immediately dereferences it to read a name field off
        // the pointee. This is the single generic template every asset type's dependency
        // marking funnels through, so a corrupted/unresolved pointer surfacing here (same
        // bug class already guarded in AssetLoader.cpp/AssetInfoCollector.cpp,
        // fastfile_format_research.md SS5.47/5.48) crashes deep inside whichever asset
        // type happened to be marked first, not at a single obvious call site -- this is
        // the actual root fix point, not another per-asset patch. Uses the real
        // VirtualQuery-backed check (Utils/PointerSanity.h), not a bit-pattern guess --
        // a cheap canonical-range heuristic tried here first had a real, demonstrated
        // blind spot (a garbage value that still falls in the canonical range).
        if (asset != nullptr && !pointer_sanity::IsLikelyReadablePointer(asset))
        {
            asset = nullptr;
            return;
        }

        if (asset == nullptr)
            return;

        const auto result = m_visitor.Visit_Dependency(Asset_t::EnumEntry, AssetName<Asset_t>(*asset));
        if (result.has_value())
            asset = static_cast<std::add_pointer_t<typename Asset_t::Type>>((*result)->m_ptr);
    }

    void Mark_ScriptString(scr_string_t& scriptString) const;
    void MarkArray_ScriptString(scr_string_t* scriptStringArray, size_t count) const;

    void Mark_IndirectAssetRef(asset_type_t assetType, const char* assetName) const;
    void MarkArray_IndirectAssetRef(asset_type_t assetType, const char** assetNames, size_t count) const;

    AssetVisitor& m_visitor;
};

template<AssetDefinition> struct AssetMarkerWrapper
{
    // using WrapperClass = WrapperClass;
};

template<typename AssetType> using AssetMarker = AssetMarkerWrapper<AssetType>::WrapperClass;

#define DEFINE_MARKER_CLASS_FOR_ASSET(asset, markerClass)                                                                                                      \
    template<> struct AssetMarkerWrapper<asset>                                                                                                                \
    {                                                                                                                                                          \
        using WrapperClass = markerClass;                                                                                                                      \
    };
