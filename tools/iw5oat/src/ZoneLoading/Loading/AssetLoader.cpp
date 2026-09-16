#include "AssetLoader.h"
#include "Utils/Logging/Log.h"

#include <algorithm>
#include <cassert>
#include <cstdint>

namespace
{
    // MW32011NCP / iw5oat, 2026-09-17: same heuristic as AssetInfoCollector's own
    // LooksLikeCanonicalPointer (not shared cross-file, a 3-line helper isn't worth
    // the plumbing) -- see that file's own comment for the full rationale
    // (fastfile_format_research.md SS5.47, parent repo). The existing null-only
    // guard below (2026-09-15) doesn't catch a genuinely garbage NON-null pointer,
    // which is exactly the shape of the still-open LoadedSound-alias-miss crash
    // this checks for.
    [[nodiscard]] bool LooksLikeCanonicalPointer(const void* ptr)
    {
        return (reinterpret_cast<uintptr_t>(ptr) >> 48) == 0;
    }
}

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
    // 2026-09-17: extended to also catch a non-null but non-canonical (genuinely
    // garbage) pointer -- see LooksLikeCanonicalPointer's own comment above.
    if (name != nullptr && !LooksLikeCanonicalPointer(name))
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
    if (name != nullptr && !LooksLikeCanonicalPointer(name))
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
