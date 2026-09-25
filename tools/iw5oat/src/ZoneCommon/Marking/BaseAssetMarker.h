#pragma once

#include "Game/IAsset.h"
#include "Marking/AssetVisitor.h"
#include "Utils/Logging/Log.h"
#include "Utils/PointerSanity.h"
#include "Zone/ZoneTypes.h"

#include <cstdint>
#include <optional>
#include <type_traits>

#ifdef _WIN32
#include <windows.h>
#endif

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

        // 2026-09-26: real, live-reproduced crash found DOWNSTREAM of both existing
        // pointer-sanity checks above -- IsLikelyReadablePointer confirms `asset` and the
        // real, correctly-terminated name string it points to are both genuinely readable
        // (confirmed via direct diagnostic reads), but the deeper m_visitor.Visit_Dependency
        // call chain (AssetInfoCollector::Visit_Dependency -> m_zone.m_pools.GetAsset ->
        // GameGlobalAssetPools::GetAsset's own raw m_global_asset_pools[assetType] index,
        // guarded only by an assert() that's compiled out in Release) still faults --
        // STATUS_ACCESS_VIOLATION (0xc0000005) reading address 0x8, the classic signature of
        // a null-object-plus-small-field-offset dereference, live-confirmed via SEH capture
        // to be systemic (reproduces identically for multiple, unrelated Asset_t
        // instantiations -- AssetLoadedSound and AssetSoundCurve both hit it back to back on
        // the same real asset). Root cause not yet pinned down further (needs a live debugger,
        // unavailable this session) -- same "graceful degradation over hard crash" philosophy
        // already applied to the two checks above and to AssetInfoCollector's own null-name
        // guard (2026-09-17) extends naturally here: a dependency this deep in the pool-lookup
        // chain that can't be safely resolved is treated as absent, not a reason to abort the
        // whole zone load. See known_issues_x64.md issue #4's newest round for the full trail.
#ifdef _WIN32
        std::optional<XAssetInfoGeneric*> result;
        __try
        {
            result = m_visitor.Visit_Dependency(Asset_t::EnumEntry, AssetName<Asset_t>(*asset));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            con::warn("BaseAssetMarker::Mark_Dependency: real access violation resolving a "
                      "dependency (asset type {}) deep inside the pool-lookup chain -- treating "
                      "as absent instead of crashing the whole zone load.",
                      static_cast<int>(Asset_t::EnumEntry));
            asset = nullptr;
            return;
        }
#else
        const auto result = m_visitor.Visit_Dependency(Asset_t::EnumEntry, AssetName<Asset_t>(*asset));
#endif
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
