#include "AssetLoader.h"

#include <algorithm>
#include <cassert>

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
    return m_zone.m_pools.AddAsset(
        m_asset_type, name != nullptr ? std::string(name) : std::string(), asset, std::move(dependencies), std::move(scriptStrings), std::move(indirectAssetReferences));
}

XAssetInfoGeneric* AssetLoader::GetAssetInfo(const char* name) const
{
    if (name == nullptr)
        return m_zone.m_pools.GetAsset(m_asset_type, std::string());

    return m_zone.m_pools.GetAsset(m_asset_type, name);
}
