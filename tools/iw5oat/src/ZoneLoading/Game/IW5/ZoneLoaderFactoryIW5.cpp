#include "ZoneLoaderFactoryIW5.h"

#include "ContentLoaderIW5.h"
#include "Game/GameLanguage.h"
#include "Game/IW5/GameIW5.h"
#include "Game/IW5/IW5.h"
#include "Game/IW5/ZoneConstantsIW5.h"
#include "Loading/Processor/ProcessorAuthedBlocks.h"
#include "Loading/Processor/ProcessorCaptureData.h"
#include "Loading/Processor/ProcessorInflate.h"
#include "Loading/Steps/StepAddProcessor.h"
#include "Loading/Steps/StepAllocXBlocks.h"
#include "Loading/Steps/StepLoadHash.h"
#include "Loading/Steps/StepLoadSignature.h"
#include "Loading/Steps/StepLoadZoneContent.h"
#include "Loading/Steps/StepLoadZoneSizes.h"
#include "Loading/Steps/StepRemoveProcessor.h"
#include "Loading/Steps/StepSkipBytes.h"
#include "Loading/Steps/StepVerifyFileName.h"
#include "Loading/Steps/StepVerifyHash.h"
#include "Loading/Steps/StepVerifyMagic.h"
#include "Loading/Steps/StepVerifySignature.h"
#include "Utils/ClassUtils.h"
#include "Utils/Logging/Log.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <type_traits>

using namespace IW5;

namespace
{
    void SetupBlock(ZoneLoader& zoneLoader)
    {
#define XBLOCK_DEF(name, type) std::make_unique<XBlock>(STR(name), name, type)

        zoneLoader.AddXBlock(XBLOCK_DEF(XFILE_BLOCK_TEMP, XBlockType::BLOCK_TYPE_TEMP));
        zoneLoader.AddXBlock(XBLOCK_DEF(XFILE_BLOCK_PHYSICAL, XBlockType::BLOCK_TYPE_NORMAL));
        zoneLoader.AddXBlock(XBLOCK_DEF(XFILE_BLOCK_RUNTIME, XBlockType::BLOCK_TYPE_RUNTIME));
        zoneLoader.AddXBlock(XBLOCK_DEF(XFILE_BLOCK_VIRTUAL, XBlockType::BLOCK_TYPE_NORMAL));
        zoneLoader.AddXBlock(XBLOCK_DEF(XFILE_BLOCK_LARGE, XBlockType::BLOCK_TYPE_NORMAL));
        zoneLoader.AddXBlock(XBLOCK_DEF(XFILE_BLOCK_CALLBACK, XBlockType::BLOCK_TYPE_NORMAL));
        zoneLoader.AddXBlock(XBLOCK_DEF(XFILE_BLOCK_VERTEX, XBlockType::BLOCK_TYPE_NORMAL));
        zoneLoader.AddXBlock(XBLOCK_DEF(XFILE_BLOCK_INDEX, XBlockType::BLOCK_TYPE_NORMAL));
        zoneLoader.AddXBlock(XBLOCK_DEF(XFILE_BLOCK_SCRIPT, XBlockType::BLOCK_TYPE_NORMAL));

#undef XBLOCK_DEF
    }

    std::unique_ptr<cryptography::IPublicKeyAlgorithm> SetupRsa(const bool isOfficial)
    {
        if (isOfficial)
        {
            auto rsa = cryptography::CreateRsa(cryptography::HashingAlgorithm::RSA_HASH_SHA256, cryptography::RsaPaddingMode::RSA_PADDING_PSS);

            if (!rsa->SetKey(ZoneConstants::RSA_PUBLIC_KEY_INFINITY_WARD, sizeof(ZoneConstants::RSA_PUBLIC_KEY_INFINITY_WARD)))
            {
                con::error("Invalid public key for signature checking");
                return nullptr;
            }

            return std::move(rsa);
        }
        else
        {
            assert(false);

            // TODO: Load custom RSA key here
            return nullptr;
        }
    }

    void AddAuthHeaderSteps(const ZoneLoaderInspectionResult& inspectResult, ZoneLoader& zoneLoader, const std::string& fileName)
    {
        // Unsigned zones do not have an auth header
        if (!inspectResult.m_is_signed)
            return;

        // If file is signed setup a RSA instance.
        auto rsa = SetupRsa(inspectResult.m_is_official);

        zoneLoader.AddLoadingStep(step::CreateStepVerifyMagic(ZoneConstants::MAGIC_AUTH_HEADER));
        zoneLoader.AddLoadingStep(step::CreateStepSkipBytes(4)); // Skip reserved

        auto subHeaderHash = step::CreateStepLoadHash(sizeof(DB_AuthHash::bytes), 1);
        auto* subHeaderHashPtr = subHeaderHash.get();
        zoneLoader.AddLoadingStep(std::move(subHeaderHash));

        auto subHeaderHashSignature = step::CreateStepLoadSignature(sizeof(DB_AuthSignature::bytes));
        auto* subHeaderHashSignaturePtr = subHeaderHashSignature.get();
        zoneLoader.AddLoadingStep(std::move(subHeaderHashSignature));

        zoneLoader.AddLoadingStep(step::CreateStepVerifySignature(std::move(rsa), subHeaderHashSignaturePtr, subHeaderHashPtr));

        auto subHeaderCapture = processor::CreateProcessorCaptureData(sizeof(DB_AuthSubHeader));
        auto* subHeaderCapturePtr = subHeaderCapture.get();
        zoneLoader.AddLoadingStep(step::CreateStepAddProcessor(std::move(subHeaderCapture)));

        zoneLoader.AddLoadingStep(step::CreateStepVerifyFileName(fileName, sizeof(DB_AuthSubHeader::fastfileName)));
        zoneLoader.AddLoadingStep(step::CreateStepSkipBytes(4)); // Skip reserved

        auto masterBlockHashes =
            step::CreateStepLoadHash(sizeof(DB_AuthHash::bytes), static_cast<unsigned>(std::extent_v<decltype(DB_AuthSubHeader::masterBlockHashes)>));
        auto* masterBlockHashesPtr = masterBlockHashes.get();
        zoneLoader.AddLoadingStep(std::move(masterBlockHashes));

        zoneLoader.AddLoadingStep(step::CreateStepVerifyHash(cryptography::CreateSha256(), 0, subHeaderHashPtr, subHeaderCapturePtr));
        zoneLoader.AddLoadingStep(step::CreateStepRemoveProcessor(subHeaderCapturePtr));

        // Skip the rest of the first chunk
        zoneLoader.AddLoadingStep(step::CreateStepSkipBytes(ZoneConstants::AUTHED_CHUNK_SIZE - sizeof(DB_AuthHeader)));

        zoneLoader.AddLoadingStep(step::CreateStepAddProcessor(
            processor::CreateProcessorAuthedBlocks(ZoneConstants::AUTHED_CHUNK_COUNT_PER_GROUP,
                                                   ZoneConstants::AUTHED_CHUNK_SIZE,
                                                   static_cast<unsigned>(std::extent_v<decltype(DB_AuthSubHeader::masterBlockHashes)>),
                                                   cryptography::CreateSha256(),
                                                   masterBlockHashesPtr)));
    }
} // namespace

std::optional<ZoneLoaderInspectionResult> ZoneLoaderFactory::InspectZoneHeader(ZoneDataPeeking& filePeek) const
{
    const auto& header = filePeek.PeekStruct<ZoneHeader>();
    if (header.m_version != ZoneConstants::ZONE_VERSION)
        return std::nullopt;

    if (!memcmp(header.m_magic, ZoneConstants::MAGIC_SIGNED_INFINITY_WARD, std::char_traits<char>::length(ZoneConstants::MAGIC_SIGNED_INFINITY_WARD)))
    {
        return ZoneLoaderInspectionResult{
            .m_game_id = GameId::IW5,
            .m_endianness = GameEndianness::LE,
            // MW32011NCP / iw5oat, 2026-09-14: was ARCH_32, matching every IW5 zone
            // before 2026-09-03. Updated for accuracy alongside the real fix
            // (pointerBitCount below, in CreateLoaderForHeader) -- this field itself
            // has no consumers anywhere in the codebase as of this fork point
            // (confirmed by grep before changing it), so this change is inert on its
            // own; kept accurate rather than left silently wrong in case future code
            // starts relying on it.
            .m_word_size = GameWordSize::ARCH_64,
            .m_platform = GamePlatform::PC,
            .m_is_official = true,
            .m_is_signed = true,
            .m_is_encrypted = false,
        };
    }

    if (!memcmp(header.m_magic, ZoneConstants::MAGIC_UNSIGNED, std::char_traits<char>::length(ZoneConstants::MAGIC_UNSIGNED)))
    {
        return ZoneLoaderInspectionResult{
            .m_game_id = GameId::IW5,
            .m_endianness = GameEndianness::LE,
            // MW32011NCP / iw5oat, 2026-09-14: was ARCH_32, matching every IW5 zone
            // before 2026-09-03. Updated for accuracy alongside the real fix
            // (pointerBitCount below, in CreateLoaderForHeader) -- this field itself
            // has no consumers anywhere in the codebase as of this fork point
            // (confirmed by grep before changing it), so this change is inert on its
            // own; kept accurate rather than left silently wrong in case future code
            // starts relying on it.
            .m_word_size = GameWordSize::ARCH_64,
            .m_platform = GamePlatform::PC,
            .m_is_official = false,
            .m_is_signed = false,
            .m_is_encrypted = false,
        };
    }

    return std::nullopt;
}

std::unique_ptr<ZoneLoader> ZoneLoaderFactory::CreateLoaderForHeader(ZoneDataPeeking& filePeek,
                                                                     const std::string& fileName,
                                                                     std::optional<std::unique_ptr<ProgressCallback>> progressCallback) const
{
    const auto inspectResult = InspectZoneHeader(filePeek);
    if (!inspectResult)
        return nullptr;

    // Create new zone
    auto zone = std::make_unique<Zone>(fileName, 0, GameId::IW5, inspectResult->m_platform);
    auto* zonePtr = zone.get();
    zone->m_language = GameLanguage::LANGUAGE_NONE;

    // File is supported. Now setup all required steps for loading this file.
    auto zoneLoader = std::make_unique<ZoneLoader>(std::move(zone));

    SetupBlock(*zoneLoader);

    // Skip the initial header that we peeked at before
    zoneLoader->AddLoadingStep(step::CreateStepSkipBytes(sizeof(ZoneHeader)));

    // Skip unknown 1 byte field that the game ignores as well
    zoneLoader->AddLoadingStep(step::CreateStepSkipBytes(1));

    // Skip timestamp
    zoneLoader->AddLoadingStep(step::CreateStepSkipBytes(8));

    // Add steps for loading the auth header which also contain the signature of the zone if it is signed.
    AddAuthHeaderSteps(*inspectResult, *zoneLoader, fileName);

    zoneLoader->AddLoadingStep(step::CreateStepAddProcessor(processor::CreateProcessorInflate(ZoneConstants::AUTHED_CHUNK_SIZE)));

    // Start of the XFile struct
    zoneLoader->AddLoadingStep(step::CreateStepLoadZoneSizes());
    zoneLoader->AddLoadingStep(step::CreateStepAllocXBlocks());

    // MW32011NCP / iw5oat, 2026-09-14: pointerBitCount was hardcoded to 32u here,
    // matching every IW5 zone that existed before MW3 (2011)'s 2026-09-03 x64
    // recompile. This fork's own scope is deliberately IW5 x64 only (see the root
    // README) -- the current retail build's own zone content genuinely uses 64-bit
    // pointer fields (confirmed via direct decompile of iw5sp.exe's own zone-
    // loading code, not assumed; full trail: re_notes/x64_migration/
    // fastfile_format_research.md, parent repo, SS5), so this must be 64u for this
    // fork to correctly parse anything from the build it actually targets.
    // KNOWN LIMITATION, not a bug: this value can't yet auto-detect which format a
    // given zone actually is (the outer FastFile header is byte-identical either
    // way -- magic, version, and the 44-byte block-size header are all unchanged
    // between the two formats, confirmed by direct hex comparison, SS1/SS5) --
    // loading a genuinely pre-2026-09-03 x86-format zone through THIS ARCH_x64
    // code path would misread it the same way the old hardcoded 32u misread the
    // new format. Not a regression for this fork's own stated scope (IW5 x64
    // only), but real, worth fixing properly (a genuine per-zone format
    // auto-detect) before this is presented as general-purpose rather than
    // NCP-specific tooling.
    constexpr unsigned IW5_X64_POINTER_BIT_COUNT = 64u;

    // Start of the zone content
    zoneLoader->AddLoadingStep(step::CreateStepLoadZoneContent(
        [zonePtr](ZoneInputStream& stream)
        {
            return std::make_unique<ContentLoader>(*zonePtr, stream);
        },
        IW5_X64_POINTER_BIT_COUNT,
        ZoneConstants::OFFSET_BLOCK_BIT_COUNT,
        ZoneConstants::INSERT_BLOCK,
        zonePtr->Memory(),
        std::move(progressCallback)));

    return zoneLoader;
}
