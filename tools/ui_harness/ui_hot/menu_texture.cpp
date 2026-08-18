// menu_texture.cpp -- see menu_texture.h for the full scope/asset-chain comment.
#include "menu_texture.h"
#include "overlay_hud.h" // MenuGfx_CreateTextureFromRawFormat

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <map>
#include <vector>
#include <set>

namespace {

std::string ToLowerAscii(const std::string& s)
{
    std::string r = s;
    for (char& c : r) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    return r;
}

void LogOnce(const std::string& key, const char* fmt, const char* arg)
{
    static std::set<std::string> logged;
    if (logged.count(key)) return;
    logged.insert(key);
    fprintf(stderr, fmt, arg);
}

std::vector<unsigned char> ReadWholeFile(const std::string& path, bool& ok)
{
    ok = false;
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return {};
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return {}; }
    std::vector<unsigned char> data(static_cast<size_t>(size));
    size_t readBytes = fread(data.data(), 1, data.size(), f);
    fclose(f);
    if (readBytes != data.size()) return {};
    ok = true;
    return data;
}

// Minimal, narrowly-scoped JSON scan -- NOT a general parser. Only extracts
// materials/<name>.json's "textures"[0]."image" string value, per this file's own
// header comment on the real, fixed OpenAssetTools material schema. Falls back to
// "not found" (empty string) on anything unexpected rather than guessing.
std::string ExtractFirstTextureImageName(const std::string& jsonText)
{
    size_t texturesPos = jsonText.find("\"textures\"");
    if (texturesPos == std::string::npos) return {};
    size_t imagePos = jsonText.find("\"image\"", texturesPos);
    if (imagePos == std::string::npos) return {};
    size_t colonPos = jsonText.find(':', imagePos);
    if (colonPos == std::string::npos) return {};
    size_t firstQuote = jsonText.find('"', colonPos);
    if (firstQuote == std::string::npos) return {};
    size_t secondQuote = jsonText.find('"', firstQuote + 1);
    if (secondQuote == std::string::npos) return {};
    return jsonText.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}

// Real DDS_HEADER layout (Microsoft's own documented struct, standard across every
// DDS file regardless of content) -- field offsets confirmed directly against this
// project's own sampled real files via a hex dump before writing this (see
// re_notes/iw5sp.md's Phase 3-continuation section), not assumed from memory alone.
#pragma pack(push, 1)
struct DdsPixelFormat
{
    uint32_t size, flags, fourCC, rgbBitCount, rMask, gMask, bMask, aMask;
};
struct DdsHeader
{
    uint32_t size, flags, height, width, pitchOrLinearSize, depth, mipMapCount;
    uint32_t reserved1[11];
    DdsPixelFormat pixelFormat;
    uint32_t caps, caps2, caps3, caps4, reserved2;
};
#pragma pack(pop)

constexpr uint32_t kDdpfFourCC = 0x4;
constexpr uint32_t kDdpfRGB = 0x40;
constexpr uint32_t kDdpfAlphaPixels = 0x1;

// D3DFORMAT numeric values (avoiding a d3d9.h dependency in this STL-permitted file,
// same "raw constant, no SDK header" convention overlay_hud.cpp itself already uses
// for kD3DFMT_A8R8G8B8 etc.) -- confirmed against the public D3DFORMAT enum values.
constexpr uint32_t kD3DFMT_R8G8B8 = 20;
constexpr uint32_t kD3DFMT_A8R8G8B8 = 21;
constexpr uint32_t kD3DFMT_X8R8G8B8 = 22;

void* LoadDdsAsTexture(void* device, const std::vector<unsigned char>& fileBytes, const std::string& logName)
{
    if (fileBytes.size() < 128 || memcmp(fileBytes.data(), "DDS ", 4) != 0) {
        LogOnce("dds_bad_magic_" + logName, "[menu_texture] '%s': not a valid DDS file (bad magic)\n", logName.c_str());
        return nullptr;
    }
    DdsHeader header{};
    memcpy(&header, fileBytes.data() + 4, sizeof(DdsHeader));
    int width = static_cast<int>(header.width);
    int height = static_cast<int>(header.height);
    if (width <= 0 || height <= 0) return nullptr;
    const unsigned char* pixelData = fileBytes.data() + 4 + sizeof(DdsHeader);
    size_t availableBytes = fileBytes.size() - (4 + sizeof(DdsHeader));

    if (header.pixelFormat.flags & kDdpfFourCC) {
        // D3D9 supports DXT1/3/5 natively -- the file's own raw 4-byte FourCC value IS
        // the real D3DFMT_DXT1/3/5 numeric constant (D3D9's FourCC-format convention),
        // no decompression or string-to-enum mapping needed, see this file's header
        // comment. Only DXT1/3/5 are recognized (the only ones found in this asset
        // set) -- anything else (e.g. a DX10-extended-header format) is unsupported.
        uint32_t fourCC = header.pixelFormat.fourCC;
        bool isDxt1 = memcmp(&fourCC, "DXT1", 4) == 0;
        bool isDxt3 = memcmp(&fourCC, "DXT3", 4) == 0;
        bool isDxt5 = memcmp(&fourCC, "DXT5", 4) == 0;
        if (!isDxt1 && !isDxt3 && !isDxt5) {
            LogOnce("dds_unsupported_fourcc_" + logName,
                "[menu_texture] '%s': unsupported compressed DDS FourCC (only DXT1/3/5 handled)\n", logName.c_str());
            return nullptr;
        }
        int blockSize = isDxt1 ? 8 : 16;
        int blockCols = (width + 3) / 4;
        int blockRows = (height + 3) / 4;
        int rowBytes = blockCols * blockSize;
        size_t neededBytes = static_cast<size_t>(rowBytes) * static_cast<size_t>(blockRows);
        if (neededBytes > availableBytes) {
            LogOnce("dds_truncated_" + logName, "[menu_texture] '%s': DDS file shorter than its own header claims\n", logName.c_str());
            return nullptr;
        }
        return MenuGfx_CreateTextureFromRawFormat(device, width, height, fourCC, pixelData, rowBytes, blockRows);
    }

    if (header.pixelFormat.flags & kDdpfRGB) {
        bool hasAlpha = (header.pixelFormat.flags & kDdpfAlphaPixels) != 0;
        uint32_t r = header.pixelFormat.rMask, g = header.pixelFormat.gMask, b = header.pixelFormat.bMask;
        uint32_t a = header.pixelFormat.aMask;
        uint32_t d3dFormat = 0;
        int bytesPerPixel = 0;
        // Only the standard channel-mask layout is recognized (see this file's header
        // comment) -- anything else falls through to "unsupported" below rather than
        // guessing at a byte-swizzle.
        if (header.pixelFormat.rgbBitCount == 32 && r == 0x00FF0000 && g == 0x0000FF00 && b == 0x000000FF) {
            d3dFormat = (hasAlpha && a == 0xFF000000) ? kD3DFMT_A8R8G8B8 : kD3DFMT_X8R8G8B8;
            bytesPerPixel = 4;
        } else if (header.pixelFormat.rgbBitCount == 24 && r == 0x00FF0000 && g == 0x0000FF00 && b == 0x000000FF) {
            d3dFormat = kD3DFMT_R8G8B8;
            bytesPerPixel = 3;
        }
        if (bytesPerPixel == 0) {
            LogOnce("dds_unsupported_rgb_" + logName,
                "[menu_texture] '%s': unsupported uncompressed DDS channel layout (only standard RGB/RGBA masks handled)\n",
                logName.c_str());
            return nullptr;
        }
        int rowBytes = width * bytesPerPixel;
        size_t neededBytes = static_cast<size_t>(rowBytes) * static_cast<size_t>(height);
        if (neededBytes > availableBytes) {
            LogOnce("dds_truncated_" + logName, "[menu_texture] '%s': DDS file shorter than its own header claims\n", logName.c_str());
            return nullptr;
        }
        return MenuGfx_CreateTextureFromRawFormat(device, width, height, d3dFormat, pixelData, rowBytes, height);
    }

    LogOnce("dds_unsupported_pf_" + logName, "[menu_texture] '%s': DDS pixel format is neither RGB nor FourCC\n", logName.c_str());
    return nullptr;
}

std::map<std::string, void*>& TextureCache()
{
    static std::map<std::string, void*> cache;
    return cache;
}

} // namespace

void* MenuTexture_LoadMaterialBackground(void* device, const std::string& materialName)
{
    if (materialName.empty()) return nullptr;
    std::string key = ToLowerAscii(materialName);
    auto& cache = TextureCache();
    auto it = cache.find(key);
    if (it != cache.end()) return it->second; // nullptr entry = confirmed-unresolvable, cached

    // 2026-08-17: real runtime-captured texture takes priority over the static
    // zone_dump extraction -- see proxy_d3d9/src/asset_capture.h's own header
    // comment. The static extraction is real but incomplete (310/301 files), and
    // some real material names (e.g. "white") are almost certainly procedural and
    // can never be extracted statically at all, no matter how thorough a
    // re-extraction is -- a live capture from the real game while it's actually
    // creating these textures is the only way to get those. Captured files are
    // named directly after the MATERIAL name (not routed through a materials.json
    // -> images\*.dds indirection like the static path below), since
    // asset_capture.cpp already resolved that itself before writing the file.
    // Two candidate locations, tried in order: next to this harness's own exe
    // (portable -- works if the capture folder is copied here), then the known
    // real game install path on this development machine (works out of the box
    // with zero copy step, matching this project's own existing precedent for
    // hardcoding a known-local absolute path in dev-only tooling, e.g. this same
    // file's MSBuild.exe path in main.cpp's SourceWatcherThreadProc).
    const char* kRuntimeCaptureCandidates[] = {
        "runtime_asset_capture\\materials\\",
        "D:\\SteamLibrary\\steamapps\\common\\Call of Duty Modern Warfare 3\\runtime_asset_capture\\materials\\",
    };
    for (const char* candidateDir : kRuntimeCaptureCandidates) {
        std::string capturedPath = std::string(candidateDir) + materialName + ".dds";
        bool capturedOk = false;
        std::vector<unsigned char> capturedBytes = ReadWholeFile(capturedPath, capturedOk);
        if (capturedOk) {
            void* texture = LoadDdsAsTexture(device, capturedBytes, materialName + " (runtime-captured)");
            if (texture) {
                LogOnce("runtime_capture_hit_" + key, "[menu_texture] '%s': using runtime-captured texture\n", materialName.c_str());
                cache[key] = texture;
                return texture;
            }
        }
    }

    std::string materialPath = "D:\\Tools\\OpenAssetTools\\zone_dump\\ui\\materials\\" + materialName + ".json";
    bool ok = false;
    std::vector<unsigned char> jsonBytes = ReadWholeFile(materialPath, ok);
    if (!ok) {
        LogOnce("mat_missing_" + key, "[menu_texture] material JSON not found: '%s'\n", materialName.c_str());
        cache[key] = nullptr;
        return nullptr;
    }
    std::string jsonText(reinterpret_cast<const char*>(jsonBytes.data()), jsonBytes.size());
    std::string imageName = ExtractFirstTextureImageName(jsonText);
    if (imageName.empty()) {
        LogOnce("mat_no_image_" + key, "[menu_texture] material '%s' has no resolvable textures[0].image\n", materialName.c_str());
        cache[key] = nullptr;
        return nullptr;
    }

    std::string ddsPath = "D:\\Tools\\OpenAssetTools\\zone_dump\\ui\\images\\" + imageName + ".dds";
    bool ddsOk = false;
    std::vector<unsigned char> ddsBytes = ReadWholeFile(ddsPath, ddsOk);
    if (!ddsOk) {
        LogOnce("dds_missing_" + key, "[menu_texture] DDS image not found: '%s'\n", imageName.c_str());
        cache[key] = nullptr;
        return nullptr;
    }

    void* texture = LoadDdsAsTexture(device, ddsBytes, materialName);
    cache[key] = texture; // caches failure (nullptr) too -- one attempt per material name, ever
    return texture;
}
