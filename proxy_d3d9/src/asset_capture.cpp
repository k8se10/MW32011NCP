#include "asset_capture.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>

#include "mod_config.h"
#include "frame_benchmark.h"
#include "../third_party/minhook/include/MinHook.h"

extern void LogFromController(const char* msg); // defined in dllmain.cpp

namespace {

// ---- Real material-name stack (set from analog_input_hooks.cpp's FindOrLoadAsset
// hook) -------------------------------------------------------------------------
// Fixed-depth, non-STL (this file ships in the real proxy_d3d9.dll, same no-STL
// convention every other shipped file in this project already follows) -- 8 deep
// is generous headroom over anything a material-load cascade has been observed to
// nest (material -> techniqueSet -> image is the deepest real chain documented in
// re_notes/iw5sp.md).
constexpr int kMaxCaptureStackDepth = 8;
char g_captureNameStack[kMaxCaptureStackDepth][64];
int g_captureDepth = 0;

// ---- Dedup set: don't rewrite the same material's texture every time it's
// re-requested (a material can be FindOrLoadAsset'd repeatedly across a session,
// e.g. every time a menu re-opens) ------------------------------------------------
constexpr int kMaxCapturedNames = 512;
char g_capturedNames[kMaxCapturedNames][64];
int g_capturedCount = 0;

bool AlreadyCaptured(const char* name)
{
    for (int i = 0; i < g_capturedCount; ++i) {
        if (_stricmp(g_capturedNames[i], name) == 0) return true;
    }
    return false;
}

void MarkCaptured(const char* name)
{
    if (g_capturedCount >= kMaxCapturedNames) return; // degrade gracefully -- stop
        // capturing NEW names once the fixed table is full rather than overflow;
        // 512 names is far beyond the 310 real materials this project's own zone
        // dump extraction found, so hitting this cap in practice would itself be
        // a notable/interesting finding, not an expected steady state.
    strncpy_s(g_capturedNames[g_capturedCount], name, _TRUNCATE);
    ++g_capturedCount;
}

// ---- Output directory: <gameDir>\runtime_asset_capture\materials\ --------------
bool g_outputDirReady = false;
char g_outputDir[MAX_PATH];

bool EnsureOutputDir()
{
    if (g_outputDirReady) return true;
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH); // full path of the .exe that loaded us
    char* lastSlash = strrchr(path, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    strcat_s(path, "runtime_asset_capture");
    CreateDirectoryA(path, nullptr); // ignore failure -- ERROR_ALREADY_EXISTS is the
        // common/expected case across sessions, and any other failure surfaces
        // naturally as every subsequent file write also failing, logged once below.
    strcat_s(path, "\\materials");
    CreateDirectoryA(path, nullptr);
    strcpy_s(g_outputDir, path);
    g_outputDirReady = true;
    return true;
}

// ---- Real DDS_HEADER layout -- MUST stay binary-identical to
// tools/ui_harness/ui_hot/menu_texture.cpp's own DdsHeader (that's the reader this
// writer exists to feed), including its "uncompressed rows are tightly packed,
// ignore dwPitchOrLinearSize" assumption -- see this file's own DDS-writing code
// below, which strips any LockRect row-padding to match. -----------------------
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
constexpr uint32_t kDdsCapsTexture = 0x1000;
constexpr uint32_t kDdsdCaps = 0x1, kDdsdHeight = 0x2, kDdsdWidth = 0x4,
                    kDdsdPitch = 0x8, kDdsdPixelFormat = 0x1000, kDdsdLinearSize = 0x80000;

// D3DFORMAT numeric values actually needed here (same "raw constant, no SDK header"
// convention overlay_hud.cpp/menu_texture.cpp already use) -- DXT1/3/5's real D3D9
// enum values ARE their own FourCC bytes (MAKEFOURCC('D','X','T','1') etc, an
// established D3D9 SDK convention, not something this project invented), so no
// translation table is needed -- the Format this hook observes IS the DDS FourCC.
constexpr DWORD kD3DFMT_A8R8G8B8 = 21;
constexpr DWORD kD3DFMT_X8R8G8B8 = 22;
constexpr DWORD kD3DFMT_DXT1 = 0x31545844; // 'DXT1'
constexpr DWORD kD3DFMT_DXT2 = 0x32545844;
constexpr DWORD kD3DFMT_DXT3 = 0x33545844;
constexpr DWORD kD3DFMT_DXT4 = 0x34545844;
constexpr DWORD kD3DFMT_DXT5 = 0x35545844;

bool IsDxtFormat(DWORD format)
{
    return format == kD3DFMT_DXT1 || format == kD3DFMT_DXT2 || format == kD3DFMT_DXT3 ||
           format == kD3DFMT_DXT4 || format == kD3DFMT_DXT5;
}

// ---- Background disk-write worker (2026-08-17 fix) -----------------------------
// Live-reported regression, same session this capture mode shipped: with
// CaptureRuntimeMenuAssets=1, the buy station prompt wouldn't open unless the
// player paused first, plus general jitter -- both symptoms matching this
// project's own well-established "something is stalling the main thread" pattern
// (see the earlier proxy_d3d9.log-flush lesson, issue #67's lineage). Root cause:
// the code below used to do fopen/fwrite/fclose SYNCHRONOUSLY inside
// Hook_CreateTexture, on whatever thread called the real CreateTexture -- for a
// menu's own texture creation, that IS the main game thread, so a real (slow,
// latency-unpredictable) disk write blocked the game's own menu-open code path
// at the exact moment it was creating that menu's textures. Fixed the same way
// this project already fixed the log-flush problem: keep the FAST, in-memory
// work (LockRect, building the complete DDS byte buffer) synchronous, and hand
// only the actual file write off to a dedicated background thread. No STL here
// (this file ships in the real proxy_d3d9.dll) -- a small fixed-capacity ring
// buffer of pending writes, guarded by a critical section, matches
// controller_input.cpp's own established cross-thread pattern.
struct PendingWrite { char path[MAX_PATH]; unsigned char* data; size_t size; };
constexpr int kMaxPendingWrites = 64;
PendingWrite g_pendingWrites[kMaxPendingWrites];
int g_pendingWriteHead = 0, g_pendingWriteTail = 0, g_pendingWriteCount = 0;
CRITICAL_SECTION g_pendingWriteLock;
bool g_pendingWriteLockInit = false;
HANDLE g_writeThreadHandle = nullptr;

DWORD WINAPI WriteThreadProc(LPVOID)
{
    for (;;) {
        PendingWrite job{};
        bool have = false;
        EnterCriticalSection(&g_pendingWriteLock);
        if (g_pendingWriteCount > 0) {
            job = g_pendingWrites[g_pendingWriteHead];
            g_pendingWriteHead = (g_pendingWriteHead + 1) % kMaxPendingWrites;
            --g_pendingWriteCount;
            have = true;
        }
        LeaveCriticalSection(&g_pendingWriteLock);
        if (have) {
            // The slow part -- now safely off the main/game thread entirely.
            FILE* f = nullptr;
            if (fopen_s(&f, job.path, "wb") == 0 && f) {
                fwrite(job.data, 1, job.size, f);
                fclose(f);
            }
            free(job.data);
        } else {
            Sleep(10); // idle poll -- capture events are rare (once per NEW
                       // material name, deduped), not a hot loop worth a
                       // condition variable for.
        }
    }
    return 0;
}

void EnsureWriteThreadStarted()
{
    if (g_writeThreadHandle) return;
    if (!g_pendingWriteLockInit) {
        InitializeCriticalSection(&g_pendingWriteLock);
        g_pendingWriteLockInit = true;
    }
    g_writeThreadHandle = CreateThread(nullptr, 0, WriteThreadProc, nullptr, 0, nullptr);
}

// Takes ownership of `data` (malloc'd) on success -- the write thread frees it
// once actually written. On failure (queue full), caller must free it instead.
bool QueueDiskWrite(const char* path, unsigned char* data, size_t size)
{
    EnsureWriteThreadStarted();
    EnterCriticalSection(&g_pendingWriteLock);
    if (g_pendingWriteCount >= kMaxPendingWrites) {
        LeaveCriticalSection(&g_pendingWriteLock);
        return false;
    }
    PendingWrite& job = g_pendingWrites[g_pendingWriteTail];
    strncpy_s(job.path, path, _TRUNCATE);
    job.data = data;
    job.size = size;
    g_pendingWriteTail = (g_pendingWriteTail + 1) % kMaxPendingWrites;
    ++g_pendingWriteCount;
    LeaveCriticalSection(&g_pendingWriteLock);
    return true;
}

// Builds one captured texture's complete DDS byte buffer in memory (fast, no
// I/O -- safe to call on the main thread) and hands it to the write thread.
// `locked` is whatever IDirect3DTexture9::LockRect actually handed back (real
// pitch, possibly padded beyond the tight per-row byte count) -- this strips
// that padding for uncompressed formats to match menu_texture.cpp's own
// "tightly packed rows" reader assumption; DXT-compressed data from D3D9 is
// already block-tight (no per-row padding exists for block-compressed
// formats), so that path is a straight copy.
struct LockedRectLite { void* pBits; int pitch; };

bool WriteDdsFile(const char* materialName, UINT width, UINT height, DWORD format, const LockedRectLite& locked)
{
    bool isDxt = IsDxtFormat(format);
    if (!isDxt && format != kD3DFMT_A8R8G8B8 && format != kD3DFMT_X8R8G8B8) {
        // Real texture, but not a format menu_texture.cpp's own loader (the ONLY
        // consumer of this output) understands -- skip rather than write a file
        // the harness can't read anyway. Logged once by the caller, not here.
        return false;
    }

    DdsHeader header{};
    header.size = 124;
    header.height = height;
    header.width = width;
    header.depth = 0;
    header.mipMapCount = 0;
    header.pixelFormat.size = 32;
    header.caps = kDdsCapsTexture;

    const unsigned char* srcRow = static_cast<const unsigned char*>(locked.pBits);
    int tightRowBytes = 0;
    int rows = static_cast<int>(height);

    if (isDxt) {
        int blockSize = (format == kD3DFMT_DXT1) ? 8 : 16;
        int blockCols = (static_cast<int>(width) + 3) / 4;
        int blockRows = (static_cast<int>(height) + 3) / 4;
        tightRowBytes = blockCols * blockSize;
        rows = blockRows;
        header.flags = kDdsdCaps | kDdsdHeight | kDdsdWidth | kDdsdPixelFormat | kDdsdLinearSize;
        header.pitchOrLinearSize = static_cast<uint32_t>(tightRowBytes) * static_cast<uint32_t>(blockRows);
        header.pixelFormat.flags = kDdpfFourCC;
        header.pixelFormat.fourCC = format;
    } else {
        int bytesPerPixel = 4;
        tightRowBytes = static_cast<int>(width) * bytesPerPixel;
        header.flags = kDdsdCaps | kDdsdHeight | kDdsdWidth | kDdsdPixelFormat | kDdsdPitch;
        header.pitchOrLinearSize = static_cast<uint32_t>(tightRowBytes);
        header.pixelFormat.flags = kDdpfRGB;
        header.pixelFormat.rgbBitCount = 32;
        header.pixelFormat.rMask = 0x00FF0000;
        header.pixelFormat.gMask = 0x0000FF00;
        header.pixelFormat.bMask = 0x000000FF;
        if (format == kD3DFMT_A8R8G8B8) {
            header.pixelFormat.flags |= kDdpfAlphaPixels;
            header.pixelFormat.aMask = 0xFF000000;
        }
    }

    int srcPitch = locked.pitch;
    if (srcPitch < tightRowBytes) srcPitch = tightRowBytes; // defensive; real D3D9
        // pitch is never smaller than the tight row size, but never trust an
        // external value blindly when it's about to drive a memory read below.

    size_t pixelBytes = static_cast<size_t>(tightRowBytes) * static_cast<size_t>(rows);
    size_t totalSize = 4 /* "DDS " magic */ + sizeof(header) + pixelBytes;
    unsigned char* buffer = static_cast<unsigned char*>(malloc(totalSize));
    if (!buffer) return false; // out of memory -- degrade gracefully, no capture
                                // this time rather than crashing the real game.

    memcpy(buffer, "DDS ", 4);
    memcpy(buffer + 4, &header, sizeof(header));
    unsigned char* dst = buffer + 4 + sizeof(header);
    for (int y = 0; y < rows; ++y) {
        memcpy(dst + static_cast<size_t>(y) * tightRowBytes,
               srcRow + static_cast<size_t>(y) * static_cast<size_t>(srcPitch),
               tightRowBytes);
    }

    char path[MAX_PATH];
    sprintf_s(path, "%s\\%s.dds", g_outputDir, materialName);
    if (!QueueDiskWrite(path, buffer, totalSize)) {
        free(buffer); // queue full (64 pending -- would mean 64 brand-new
                       // materials in one frame, never observed) -- drop this
                       // one capture rather than block waiting for room.
        return false;
    }
    return true;
}

// ---- CreateTexture device-vtable hook ------------------------------------------
constexpr int kCreateTextureVtableIndex = 23;  // IDirect3DDevice9::CreateTexture --
    // same standard COM layout overlay_hud.cpp already confirmed/uses for this
    // exact index; duplicated here (not shared) since that file's own copy is
    // anonymous-namespace/not exported, and this is a stable public COM contract,
    // not something that could drift between two definitions.
constexpr int kTextureLockRectVtableIndex = 19;   // IDirect3DTexture9::LockRect
constexpr int kTextureUnlockRectVtableIndex = 20; // IDirect3DTexture9::UnlockRect
constexpr int kTextureReleaseVtableIndex = 2;     // IUnknown::Release

typedef HRESULT(WINAPI* CreateTexture_t)(void* This, UINT Width, UINT Height, UINT Levels,
                                          DWORD Usage, DWORD Format, DWORD Pool,
                                          void** ppTexture, HANDLE* pSharedHandle);
struct D3dLockedRect { INT Pitch; void* pBits; };
typedef HRESULT(WINAPI* TextureLockRect_t)(void* This, UINT Level, D3dLockedRect* pLockedRect,
                                            const RECT* pRect, DWORD Flags);
typedef HRESULT(WINAPI* TextureUnlockRect_t)(void* This, UINT Level);

void* g_origCreateTexture = nullptr;
bool g_hookInstalled = false;

HRESULT WINAPI Hook_CreateTexture(void* This, UINT Width, UINT Height, UINT Levels,
                                   DWORD Usage, DWORD Format, DWORD Pool,
                                   void** ppTexture, HANDLE* pSharedHandle)
{
    // Real call happens FIRST, completely unmodified -- this hook is a pure
    // observer, matching this project's "read-only, never alter real behavior"
    // standard for every diagnostic hook (e.g. Hook_004dfd30 in
    // analog_input_hooks.cpp). The real return value/texture pointer is always
    // what's handed back to the caller regardless of anything below.
    //
    // 2026-08-17 stutter investigation, second pass -- this hook intercepts EVERY
    // real CreateTexture call on the device, not just the real engine's own
    // material loads: overlay_hud.cpp's own glyph-icon/hint-text texture creation
    // goes through this SAME real device vtable slot. Live data from the first
    // pass showed a real, tightly regular alternating stall pattern that doesn't
    // match one-time asset streaming -- if THIS mod's own glyph/hint code is
    // recreating a texture unnecessarily on some repeating cadence (a
    // cache-invalidation bug would fit "became noticeable post-glyphs-update,
    // post-0.2.5" exactly), that real cost needs to be visible, unconditionally,
    // regardless of whether captureRuntimeMenuAssets is even on -- unlike the
    // rest of this hook (gated below), this timing always runs when
    // FrametimeBenchmarkLogging is on, tracking every single texture creation.
    LARGE_INTEGER realCallStart{}, realCallEnd{}, realCallFreq{};
    QueryPerformanceFrequency(&realCallFreq);
    QueryPerformanceCounter(&realCallStart);
    HRESULT hr = reinterpret_cast<CreateTexture_t>(g_origCreateTexture)(
        This, Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle);
    QueryPerformanceCounter(&realCallEnd);
    FrameBenchmark_AddRealCreateTextureCall(
        (static_cast<double>(realCallEnd.QuadPart - realCallStart.QuadPart) * 1000.0) /
        static_cast<double>(realCallFreq.QuadPart),
        Width, Height, Format);

    // Timer below starts AFTER the real call above, deliberately: that call's own
    // duration is now accounted separately (above), not overhead THIS project's
    // OWN capture logic added. Everything from here down (the capture-depth/dedup
    // checks, LockRect, DDS-buffer build) IS this hook's own added cost, and every
    // return path below accounts its own elapsed time before leaving, so
    // frametime_benchmark.csv's assetCaptureMs column reflects that separately.
    LARGE_INTEGER benchFreq{}, benchStart{};
    QueryPerformanceFrequency(&benchFreq);
    QueryPerformanceCounter(&benchStart);
    auto AccountAndReturn = [&](HRESULT ret) -> HRESULT {
        LARGE_INTEGER benchEnd{};
        QueryPerformanceCounter(&benchEnd);
        FrameBenchmark_AddAssetCaptureMs(
            (static_cast<double>(benchEnd.QuadPart - benchStart.QuadPart) * 1000.0) /
            static_cast<double>(benchFreq.QuadPart));
        return ret;
    };

    if (FAILED(hr) || !ppTexture || !*ppTexture) return AccountAndReturn(hr);
    if (g_captureDepth <= 0) return AccountAndReturn(hr); // not inside a tracked
        // material load -- see this file's own header comment on why a wrong
        // name association would be worse than none; only capture when we're
        // CONFIDENT which material this texture belongs to.

    const char* materialName = g_captureNameStack[g_captureDepth - 1];
    if (materialName[0] == '\0' || AlreadyCaptured(materialName)) return AccountAndReturn(hr);

    // Read the real pixel data back out via the texture's own LockRect -- D3D9
    // allows locking a texture immediately after creation regardless of Pool
    // (this project's own overlay_hud.cpp already does the equivalent for its own
    // created textures via a surface-level LockRect; this is the same idea one
    // level up, directly on the IDirect3DTexture9 interface).
    void* texture = *ppTexture;
    void** texVtbl = *reinterpret_cast<void***>(texture);
    auto lockRect = reinterpret_cast<TextureLockRect_t>(texVtbl[kTextureLockRectVtableIndex]);
    auto unlockRect = reinterpret_cast<TextureUnlockRect_t>(texVtbl[kTextureUnlockRectVtableIndex]);

    D3dLockedRect locked{};
    if (SUCCEEDED(lockRect(texture, 0, &locked, nullptr, 0)) && locked.pBits) {
        EnsureOutputDir();
        LockedRectLite lite{ locked.pBits, locked.Pitch };
        bool wrote = WriteDdsFile(materialName, Width, Height, Format, lite);
        unlockRect(texture, 0);
        if (wrote) {
            MarkCaptured(materialName);
            char buf[192];
            sprintf_s(buf, "[asset-capture] queued '%s' (%ux%u, format=%lu) -> runtime_asset_capture\\materials\\%s.dds",
                materialName, Width, Height, Format, materialName);
            LogFromController(buf);
        } else {
            // Logged once per distinct (name,format) so an unsupported-format run
            // doesn't spam every frame the same material gets re-requested --
            // matches this project's own issue #67 log-volume lesson.
            static char lastUnsupportedName[64] = {};
            if (_stricmp(lastUnsupportedName, materialName) != 0) {
                strncpy_s(lastUnsupportedName, materialName, _TRUNCATE);
                char buf[192];
                sprintf_s(buf, "[asset-capture] '%s': unsupported texture format %lu, not captured", materialName, Format);
                LogFromController(buf);
            }
        }
    }

    return AccountAndReturn(hr);
}

} // namespace

bool AssetCapture_PushMaterialName(const char* name)
{
    if (!g_modConfig.captureRuntimeMenuAssets) return false;
    if (!name || g_captureDepth >= kMaxCaptureStackDepth) return false; // degrade
        // gracefully -- deeper-than-expected nesting just stops tracking new
        // names rather than corrupting the stack; the shallower entries already
        // pushed are untouched and still pop correctly. Caller MUST NOT call
        // PopMaterialName when this returns false -- see this function's own
        // declaration comment in asset_capture.h.
    strncpy_s(g_captureNameStack[g_captureDepth], name, _TRUNCATE);
    ++g_captureDepth;
    return true;
}

void AssetCapture_PopMaterialName()
{
    if (!g_modConfig.captureRuntimeMenuAssets) return;
    if (g_captureDepth > 0) --g_captureDepth;
}

void AssetCapture_InstallHookIfEnabled(void* realDevice)
{
    // 2026-08-17 stutter investigation: also installs when FrametimeBenchmarkLogging
    // is on by itself (captureRuntimeMenuAssets off) -- Hook_CreateTexture now times
    // EVERY real texture creation unconditionally for the benchmark CSV (see its own
    // header comment), not just material-capture-tagged ones, so the benchmark needs
    // this hook installed even when the actual capture-to-disk feature isn't in use.
    if (!g_modConfig.captureRuntimeMenuAssets && !g_modConfig.frametimeBenchmarkLogging) return;
    if (!realDevice || g_hookInstalled) return; // one real device for this game's
        // lifetime, same convention as overlay_hud.cpp's InstallEndSceneHook.
    g_hookInstalled = true;

    void** deviceVtbl = *reinterpret_cast<void***>(realDevice);
    void* realCreateTexture = deviceVtbl[kCreateTextureVtableIndex];

    MH_STATUS s = MH_CreateHook(realCreateTexture, reinterpret_cast<void*>(&Hook_CreateTexture),
                                 &g_origCreateTexture);
    char buf[128];
    sprintf_s(buf, "[asset-capture] MH_CreateHook(CreateTexture @ %p) = %d", realCreateTexture, static_cast<int>(s));
    LogFromController(buf);
    if (s == MH_OK) {
        MH_STATUS e = MH_EnableHook(realCreateTexture);
        sprintf_s(buf, "[asset-capture] MH_EnableHook(CreateTexture) = %d", static_cast<int>(e));
        LogFromController(buf);
    }
}
