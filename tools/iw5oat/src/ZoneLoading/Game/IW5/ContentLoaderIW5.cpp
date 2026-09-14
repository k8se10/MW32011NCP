#include "ContentLoaderIW5.h"

#include "Game/IW5/AssetLoaderIW5.h"
#include "Game/IW5/IW5.h"
#include "Loading/Exception/UnsupportedAssetTypeException.h"

#include <cassert>

using namespace IW5;

ContentLoader::ContentLoader(Zone& zone, ZoneInputStream& stream)
    : ContentLoaderBase(zone, stream),
      varXAssetList(nullptr),
      varXAsset(nullptr),
      varScriptStringList(nullptr)
{
}

void ContentLoader::LoadScriptStringList(const bool atStreamStart)
{
    assert(!atStreamStart);

    if (varScriptStringList->strings != nullptr)
    {
        assert(GetZonePointerType(varScriptStringList->strings) == ZonePointerType::FOLLOWING);

#ifdef ARCH_x86
        varScriptStringList->strings = m_stream.Alloc<const char*>(4);
#else
        varScriptStringList->strings = m_stream.AllocOutOfBlock<const char*>(4, varScriptStringList->count);
#endif
        varXString = varScriptStringList->strings;
        LoadXStringArray(true, varScriptStringList->count);

        if (varScriptStringList->strings && varScriptStringList->count > 0)
            m_zone.m_script_strings.InitializeForExistingZone(varScriptStringList->strings, static_cast<size_t>(varScriptStringList->count));
    }

    assert(m_zone.m_script_strings.Count() <= SCR_STRING_MAX + 1);
}

void ContentLoader::LoadXAsset(const bool atStreamStart) const
{
#define LOAD_ASSET(type_index, typeName, headerEntry)                                                                                                          \
    case type_index:                                                                                                                                           \
    {                                                                                                                                                          \
        Loader_##typeName loader(m_zone, m_stream);                                                                                                            \
        loader.Load(&varXAsset->header.headerEntry);                                                                                                           \
        break;                                                                                                                                                 \
    }
#define SKIP_ASSET(type_index)                                                                                                                                 \
    case type_index:                                                                                                                                           \
        break;

    assert(varXAsset != nullptr);

    if (atStreamStart)
        m_stream.Load<XAsset>(varXAsset);

    switch (varXAsset->type)
    {
        LOAD_ASSET(ASSET_TYPE_PHYSPRESET, PhysPreset, physPreset)
        LOAD_ASSET(ASSET_TYPE_PHYSCOLLMAP, PhysCollmap, physCollmap)
        LOAD_ASSET(ASSET_TYPE_XANIMPARTS, XAnimParts, parts)
        LOAD_ASSET(ASSET_TYPE_XMODEL_SURFS, XModelSurfs, modelSurfs)
        LOAD_ASSET(ASSET_TYPE_XMODEL, XModel, model)
        LOAD_ASSET(ASSET_TYPE_MATERIAL, Material, material)
        LOAD_ASSET(ASSET_TYPE_PIXELSHADER, MaterialPixelShader, pixelShader)
        LOAD_ASSET(ASSET_TYPE_VERTEXSHADER, MaterialVertexShader, vertexShader)
        LOAD_ASSET(ASSET_TYPE_VERTEXDECL, MaterialVertexDeclaration, vertexDecl)
        LOAD_ASSET(ASSET_TYPE_TECHNIQUE_SET, MaterialTechniqueSet, techniqueSet)
        LOAD_ASSET(ASSET_TYPE_IMAGE, GfxImage, image)
        LOAD_ASSET(ASSET_TYPE_SOUND, snd_alias_list_t, sound)
        LOAD_ASSET(ASSET_TYPE_SOUND_CURVE, SndCurve, sndCurve)
        LOAD_ASSET(ASSET_TYPE_LOADED_SOUND, LoadedSound, loadSnd)
        LOAD_ASSET(ASSET_TYPE_CLIPMAP, clipMap_t, clipMap)
        LOAD_ASSET(ASSET_TYPE_COMWORLD, ComWorld, comWorld)
        LOAD_ASSET(ASSET_TYPE_GLASSWORLD, GlassWorld, glassWorld)
        LOAD_ASSET(ASSET_TYPE_PATHDATA, PathData, pathData)
        LOAD_ASSET(ASSET_TYPE_VEHICLE_TRACK, VehicleTrack, vehicleTrack)
        LOAD_ASSET(ASSET_TYPE_MAP_ENTS, MapEnts, mapEnts)
        LOAD_ASSET(ASSET_TYPE_FXWORLD, FxWorld, fxWorld)
        LOAD_ASSET(ASSET_TYPE_GFXWORLD, GfxWorld, gfxWorld)
        LOAD_ASSET(ASSET_TYPE_LIGHT_DEF, GfxLightDef, lightDef)
        LOAD_ASSET(ASSET_TYPE_FONT, Font_s, font)
        LOAD_ASSET(ASSET_TYPE_MENULIST, MenuList, menuList)
        LOAD_ASSET(ASSET_TYPE_MENU, menuDef_t, menu)
        LOAD_ASSET(ASSET_TYPE_LOCALIZE_ENTRY, LocalizeEntry, localize)
        LOAD_ASSET(ASSET_TYPE_ATTACHMENT, WeaponAttachment, attachment)
        LOAD_ASSET(ASSET_TYPE_WEAPON, WeaponCompleteDef, weapon)
        SKIP_ASSET(ASSET_TYPE_SNDDRIVER_GLOBALS)
        LOAD_ASSET(ASSET_TYPE_FX, FxEffectDef, fx)
        LOAD_ASSET(ASSET_TYPE_IMPACT_FX, FxImpactTable, impactFx)
        LOAD_ASSET(ASSET_TYPE_SURFACE_FX, SurfaceFxTable, surfaceFx)
        LOAD_ASSET(ASSET_TYPE_RAWFILE, RawFile, rawfile)
        LOAD_ASSET(ASSET_TYPE_SCRIPTFILE, ScriptFile, scriptfile)
        LOAD_ASSET(ASSET_TYPE_STRINGTABLE, StringTable, stringTable)
        LOAD_ASSET(ASSET_TYPE_LEADERBOARD, LeaderboardDef, leaderboardDef)
        LOAD_ASSET(ASSET_TYPE_STRUCTURED_DATA_DEF, StructuredDataDefSet, structuredDataDefSet)
        LOAD_ASSET(ASSET_TYPE_TRACER, TracerDef, tracerDef)
        LOAD_ASSET(ASSET_TYPE_VEHICLE, VehicleDef, vehDef)
        LOAD_ASSET(ASSET_TYPE_ADDON_MAP_ENTS, AddonMapEnts, addonMapEnts)

    default:
    {
        throw UnsupportedAssetTypeException(varXAsset->type);
    }
    }

#undef LOAD_ASSET
}

void ContentLoader::LoadXAssetArray(const bool atStreamStart, const size_t count)
{
    assert(count == 0 || varXAsset != nullptr);

    if (atStreamStart)
    {
#ifdef ARCH_x86
        m_stream.Load<XAsset>(varXAsset, count);
#else
        // MW32011NCP / iw5oat, 2026-09-14: the pre-existing ARCH_x64 branch here used
        // an 8-byte stride with the data pointer at +4u -- x86's own record layout,
        // still wrong for the current retail x64 build this fork targets. MW3
        // (2011)'s 2026-09-03 x64 recompile widened this per-asset dispatch record
        // to a genuine 16 bytes: { int32 type; int32 pad; int64 dataPtr; } -- the
        // pointer sits at byte offset +8, with 4 bytes of alignment padding after
        // the type field, not immediately after it. Confirmed via direct decompile
        // of iw5sp.exe's own real zone-loading dispatch (FUN_14009bce0) and cross-
        // validated against this fork's own ASSET_TYPE_* enum length (the native
        // switch's real case range, 0-0x2d, matches exactly, index for index). This
        // was the direct, confirmed cause of the real-content-zone segfault this
        // fork exists to fix -- see re_notes/x64_migration/fastfile_format_research.md
        // (parent repo) SS5 for the complete trail.
        const auto fill = m_stream.LoadWithFill(16u * count);

        for (size_t index = 0; index < count; index++)
        {
            fill.Fill(varXAsset[index].type, 16u * index);
            fill.FillPtr(varXAsset[index].header.data, 16u * index + 8u);
            m_stream.AddPointerLookup(&varXAsset[index].header.data, fill.BlockBuffer(16u * index + 8u));
        }
#endif
    }

    for (size_t index = 0; index < count; index++)
    {
        LoadXAsset(false);
        varXAsset++;

#ifdef DEBUG_OFFSETS
        m_stream.DebugOffsets(index);
#endif
    }
}

void ContentLoader::Load()
{
    XAssetList assetList{};
    varXAssetList = &assetList;

#ifdef ARCH_x86
    m_stream.LoadDataRaw(&assetList, sizeof(assetList));
#else
    // MW32011NCP / iw5oat, 2026-09-14: same real bug class as LoadXAssetArray's own
    // ARCH_x64 branch above -- this used x86's own 16-byte XAssetList layout
    // (count/strings/assetCount/assets each still at their x86 offsets). Confirmed
    // via direct decompile of iw5sp.exe's own header-read call (FUN_14008eba0 ->
    // FUN_14008ef80(&header, 0x20) -- a genuine 32-byte read) and the exact byte
    // offsets the real dispatch loop consumes afterward (assetCount read at +0x10,
    // the assets pointer at +0x18, both confirmed live in the disassembly): the
    // real x64 layout is { int32 stringCount; int32 pad; int64* strings; int32
    // assetCount; int32 pad; XAsset* assets; } = 32 bytes, not 16 -- every pointer
    // field pushed 4 bytes later than this branch previously assumed. Full trail:
    // re_notes/x64_migration/fastfile_format_research.md (parent repo) SS5.
    const auto fillAccessor = m_stream.LoadWithFill(32u);
    varScriptStringList = &varXAssetList->stringList;
    fillAccessor.Fill(varScriptStringList->count, 0u);
    fillAccessor.FillPtr(varScriptStringList->strings, 8u);

    fillAccessor.Fill(varXAssetList->assetCount, 16u);
    fillAccessor.FillPtr(varXAssetList->assets, 24u);
#endif

    m_stream.PushBlock(XFILE_BLOCK_VIRTUAL);

    varScriptStringList = &assetList.stringList;
    LoadScriptStringList(false);

    if (assetList.assets != nullptr)
    {
        assert(GetZonePointerType(assetList.assets) == ZonePointerType::FOLLOWING);

#ifdef ARCH_x86
        assetList.assets = m_stream.Alloc<XAsset>(4);
#else
        assetList.assets = m_stream.AllocOutOfBlock<XAsset>(4, assetList.assetCount);
#endif
        varXAsset = assetList.assets;
        LoadXAssetArray(true, assetList.assetCount);
    }

    m_stream.PopBlock();
}
