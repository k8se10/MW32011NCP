#include "AssetInfoCollector.h"

#include "Utils/Logging/Log.h"
#include "Utils/PointerSanity.h"

#include <algorithm>
#include <cassert>
#include <cstdint>

AssetInfoCollector::AssetInfoCollector(Zone& zone)
    : m_zone(zone)
{
}

std::vector<XAssetInfoGeneric*> AssetInfoCollector::GetDependencies() const
{
    std::vector<XAssetInfoGeneric*> dependencies;
    if (!m_dependencies.empty())
    {
        dependencies.reserve(m_dependencies.size());
        for (auto dependency : m_dependencies)
            dependencies.push_back(dependency);
    }

    return dependencies;
}

std::vector<scr_string_t> AssetInfoCollector::GetUsedScriptStrings() const
{
    std::vector<scr_string_t> usedScriptStrings;
    if (!m_used_script_strings.empty())
    {
        usedScriptStrings.reserve(m_used_script_strings.size());
        for (auto scrString : m_used_script_strings)
            usedScriptStrings.push_back(scrString);

        std::ranges::sort(usedScriptStrings);
    }

    return usedScriptStrings;
}

std::vector<IndirectAssetReference> AssetInfoCollector::GetIndirectAssetReferences() const
{
    std::vector<IndirectAssetReference> assetReferences;
    if (!m_indirect_asset_references.empty())
    {
        assetReferences.reserve(m_indirect_asset_references.size());
        for (const auto& assetReference : m_indirect_asset_references)
            assetReferences.emplace_back(assetReference);
    }

    return assetReferences;
}

std::optional<XAssetInfoGeneric*> AssetInfoCollector::Visit_Dependency(const asset_type_t assetType, const char* assetName)
{
    // MW32011NCP / iw5oat, 2026-09-17: a null assetName is genuine, legitimate data for a
    // dependency reference this fork's own graceful degradation (fastfile_format_research.md
    // SS5.29/5.41, parent repo) left unresolved -- exactly the same "null is a normal, valid,
    // absent-reference outcome" case AssetLoader::GetAssetInfo/LinkAsset already guard against
    // (2026-09-15), just missed here. m_pools.GetAsset takes `const std::string&`, so passing
    // a null const char* through unchecked implicitly constructs std::string(nullptr) --
    // undefined behavior that crashes inside ucrtbase!strlen on this MSVC STL. There is no
    // dependency to record if its own name never resolved.
    // 2026-09-17: extended to also catch a non-null but genuinely unreadable
    // pointer -- see Utils/PointerSanity.h's own comment for why a real
    // VirtualQuery check replaced the original bit-pattern heuristic here.
    if (assetName != nullptr && !pointer_sanity::IsLikelyReadablePointer(assetName))
    {
        con::warn("AssetInfoCollector::Visit_Dependency: name for asset type {} does not look like a "
                  "valid pointer ({:#x}) -- treating as absent instead of crashing inside strlen "
                  "(see fastfile_format_research.md SS5.47, parent repo).",
                  static_cast<int>(assetType),
                  reinterpret_cast<uintptr_t>(assetName));
        assetName = nullptr;
    }

    if (assetName == nullptr)
        return std::nullopt;

    auto* assetInfo = m_zone.m_pools.GetAsset(assetType, assetName);
    if (assetInfo == nullptr)
        return std::nullopt;

    const auto existingEntry = m_dependencies.find(assetInfo);
    if (existingEntry != m_dependencies.end())
        return std::nullopt;

    m_dependencies.emplace(assetInfo);

    return std::nullopt;
}

std::optional<scr_string_t> AssetInfoCollector::Visit_ScriptString(scr_string_t scriptString)
{
    assert(scriptString < m_zone.m_script_strings.Count());

    if (scriptString >= m_zone.m_script_strings.Count())
        return std::nullopt;

    m_used_script_strings.emplace(scriptString);

    return std::nullopt;
}

void AssetInfoCollector::Visit_IndirectAssetRef(asset_type_t assetType, const char* assetName)
{
    // Same guard as Visit_Dependency above -- IndirectAssetReference's own constructor
    // takes `std::string` by value, same implicit-construction-from-nullptr/garbage-pointer risk.
    if (assetName != nullptr && !pointer_sanity::IsLikelyReadablePointer(assetName))
    {
        con::warn("AssetInfoCollector::Visit_IndirectAssetRef: name for asset type {} does not look like a "
                  "valid pointer ({:#x}) -- treating as absent instead of crashing inside strlen "
                  "(see fastfile_format_research.md SS5.47, parent repo).",
                  static_cast<int>(assetType),
                  reinterpret_cast<uintptr_t>(assetName));
        assetName = nullptr;
    }

    if (assetName == nullptr)
        return;

    m_indirect_asset_references.emplace(assetType, assetName);
}
