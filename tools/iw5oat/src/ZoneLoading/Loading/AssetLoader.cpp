#include "AssetLoader.h"
#include "Utils/Logging/Log.h"
#include "Utils/PointerSanity.h"

#include <algorithm>
#include <cassert>
#include <cstdint>

AssetLoader::AssetLoader(const asset_type_t assetType, Zone& zone, ZoneInputStream& stream)
    : ContentLoaderBase(zone, stream),
      varScriptString(nullptr),
      m_asset_type(assetType)
{
}

XAssetInfoGeneric* AssetLoader::LinkAsset(const char* name,
                                          void* asset,
                                          std::vector<XAssetInfoGeneric*> dependencies,
                                          std::vector<scr_string_t> scriptStrings,
                                          std::vector<IndirectAssetReference> indirectAssetReferences) const
{
    // MW32011NCP / iw5oat, 2026-09-15: a null name is genuine, legitimate native
    // data for a "reusable"-pointer asset whose own record resolves via alias
    // lookup rather than a fresh load -- see this function's own declaration
    // (AssetLoader.h) for the full trail. std::string's own const char* constructor
    // is undefined behavior on a null pointer; substitute an empty name instead of
    // crashing.
    //
    // 2026-09-17: extended to also catch a non-null but genuinely unreadable
    // pointer -- see Utils/PointerSanity.h's own comment for why a real
    // VirtualQuery check replaced the original bit-pattern heuristic here.
    if (name != nullptr && !pointer_sanity::IsLikelyReadablePointer(name))
    {
        con::warn("AssetLoader::LinkAsset: name for asset type {} does not look like a "
                  "valid pointer ({:#x}) -- substituting an empty name instead of crashing "
                  "inside strlen (see fastfile_format_research.md SS5.47, parent repo).",
                  static_cast<int>(m_asset_type), reinterpret_cast<uintptr_t>(name));
        name = nullptr;
    }
    return m_zone.m_pools.AddAsset(
        m_asset_type, name != nullptr ? std::string(name) : std::string(), asset, std::move(dependencies), std::move(scriptStrings), std::move(indirectAssetReferences));
}

XAssetInfoGeneric* AssetLoader::GetAssetInfo(const char* name) const
{
    if (name != nullptr && !pointer_sanity::IsLikelyReadablePointer(name))
    {
        con::warn("AssetLoader::GetAssetInfo: name for asset type {} does not look like a "
                  "valid pointer ({:#x}) -- treating as absent instead of crashing inside "
                  "strlen (see fastfile_format_research.md SS5.47, parent repo).",
                  static_cast<int>(m_asset_type), reinterpret_cast<uintptr_t>(name));
        name = nullptr;
    }
    if (name == nullptr)
        return m_zone.m_pools.GetAsset(m_asset_type, std::string());

    return m_zone.m_pools.GetAsset(m_asset_type, name);
}
