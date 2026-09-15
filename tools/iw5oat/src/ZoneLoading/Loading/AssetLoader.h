#pragma once

#include "ContentLoaderBase.h"
#include "Pool/XAssetInfo.h"
#include "Zone/ZoneTypes.h"

#include <vector>

class AssetLoader : public ContentLoaderBase
{
protected:
    AssetLoader(asset_type_t assetType, Zone& zone, ZoneInputStream& stream);

    // MW32011NCP / iw5oat, 2026-09-15: `const char* name`, not `std::string name`.
    // Every real generated call site (194 asset types) passes AssetName<T>(**pAsset)
    // directly, a raw const char* (or const char*&) -- NOT a std::string -- so the
    // old std::string-by-value parameter forced an implicit std::string(const char*)
    // construction at every call site. That constructor is undefined behavior (a
    // real, reliable MSVC crash) when the raw pointer is null, and a null asset name
    // IS genuine, legitimate native retail data for at least one confirmed real case:
    // a `reusable`-pointer asset (e.g. snd_alias_list_t, DSL: `set reusable head;`)
    // whose own top-level record resolves via ConvertOffsetToAliasLookup -- confirmed
    // via direct native decompile (FUN_14009f4b0) that the real engine's own
    // equivalent string-resolution step explicitly no-ops (`if (*field != 0) { ... }`)
    // rather than crashing when this exact field is already zero on the wire. Taking
    // a raw pointer here and safely substituting an empty name for null (below) fixes
    // this crash class for every asset type at once, in one hand-written location,
    // rather than patching 194 generated call sites. Full trail:
    // re_notes/x64_migration/fastfile_format_research.md SS5.30.
    XAssetInfoGeneric* LinkAsset(const char* name,
                                 void* asset,
                                 std::vector<XAssetInfoGeneric*> dependencies,
                                 std::vector<scr_string_t> scriptStrings,
                                 std::vector<IndirectAssetReference> indirectAssetReferences) const;

    // MW32011NCP / iw5oat, 2026-09-15: const char*, same reasoning/trail as
    // LinkAsset above -- every real call site passes AssetName<T>(**pAsset)
    // directly, and a null name is genuine legitimate data for the same
    // reusable-pointer/alias-lookup case.
    [[nodiscard]] XAssetInfoGeneric* GetAssetInfo(const char* name) const;

    scr_string_t* varScriptString;

private:
    asset_type_t m_asset_type;
};
