#include "texture_viewport_capture.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "mod_config.h"
#include "texture_upscale_worker.h"
#include "texture_upscale_cache.h"
#include "texture_upscale_iwi_writer.h"
#include "../third_party/minhook/include/MinHook.h"

extern void LogFromController(const char* msg); // defined in dllmain.cpp

namespace TextureViewportCapture
{
namespace
{
    // ---- "currently loading" thread_local state, set by Hook_ImageFileLoadX64 ----
    struct LoadingState { char name[256] = {}; bool active = false; bool wasHit = false; };
    thread_local LoadingState g_loadingState;

    // ---- Real D3DFMT constants this module needs -- duplicated (not shared),
    // same convention asset_capture.cpp's own copy already documents (a stable
    // public COM/API contract, not something that could drift between files). ----
    constexpr DWORD kD3DFMT_DXT1 = 0x31545844;
    constexpr DWORD kD3DFMT_DXT3 = 0x33545844;
    constexpr DWORD kD3DFMT_DXT5 = 0x35545844;

    // ---- Real name<->pointer correlation table -----------------------------
    // Fixed-size open-addressing hash table, keyed by the real D3D texture
    // pointer -- SetTexture is a genuine hot path (called many times per
    // frame), so lookup must be O(1), not a linear scan. Power-of-2 sized for
    // a cheap mask instead of a real modulo. CRITICAL_SECTION-guarded:
    // CreateTexture and SetTexture aren't guaranteed to run on the same
    // thread, and this table's own entries must never be read half-written.
    struct CorrelationEntry
    {
        void* texturePtr = nullptr; // nullptr = empty slot
        char name[256] = {};
        UINT width = 0, height = 0;
        DWORD format = 0;
        bool handled = false; // true once queued (or determined ineligible)
                               // -- SetTexture fires every frame a texture
                               // stays bound; only the FIRST real bind does
                               // any work.
    };
    constexpr int kTableSize = 8192; // real headroom -- this project's own
        // live testing has observed a few thousand distinct real textures
        // touched in a single extended session; open addressing degrades
        // gracefully (a full table just stops recording NEW correlations,
        // same "degrade gracefully" convention as this project's other
        // fixed-capacity tables) rather than corrupting anything.
    CorrelationEntry g_table[kTableSize];
    CRITICAL_SECTION g_tableLock;
    bool g_tableLockInit = false;

    void EnsureTableLockInit()
    {
        if (!g_tableLockInit) {
            InitializeCriticalSection(&g_tableLock);
            g_tableLockInit = true;
        }
    }

    size_t HashPointer(void* p)
    {
        // Real pointers are at least 16-byte aligned in practice for D3D9
        // resource allocations; shift off the low bits that would otherwise
        // never vary, spreading entries across the table instead of
        // clustering them.
        return (reinterpret_cast<uintptr_t>(p) >> 4) & (kTableSize - 1);
    }

    // Returns the slot index for `ptr` -- either its existing entry, or the
    // first empty slot on its probe sequence (for an insert). Returns -1 if
    // the table is genuinely full along this entire probe sequence (real,
    // honest degradation, see g_table's own header comment).
    int FindSlot(void* ptr)
    {
        size_t start = HashPointer(ptr);
        for (int probe = 0; probe < kTableSize; ++probe) {
            size_t idx = (start + probe) & (kTableSize - 1);
            if (g_table[idx].texturePtr == nullptr || g_table[idx].texturePtr == ptr) return static_cast<int>(idx);
        }
        return -1;
    }

    // ---- Real SetTexture hook plumbing -------------------------------------
    typedef HRESULT(WINAPI* SetTexture_t)(void* This, DWORD stage, void* texture);
    constexpr int kSetTextureVtableIndex = 65; // IDirect3DDevice9::SetTexture --
        // same standard COM layout overlay_hud.cpp/asset_capture.cpp already
        // rely on for other device-vtable indices.
    constexpr int kTextureLockRectVtableIndex = 19;
    constexpr int kTextureUnlockRectVtableIndex = 20;
    struct D3dLockedRect { INT Pitch; void* pBits; };
    typedef HRESULT(WINAPI* TextureLockRect_t)(void* This, UINT Level, D3dLockedRect* pLockedRect, const RECT* pRect, DWORD Flags);
    typedef HRESULT(WINAPI* TextureUnlockRect_t)(void* This, UINT Level);

    void* g_origSetTexture = nullptr;
    bool g_hookInstalled = false;

    bool D3dFmtToIwi(DWORD format, TextureUpscaleIwi::Format* outIwiFmt)
    {
        switch (format) {
            case kD3DFMT_DXT1: *outIwiFmt = TextureUpscaleIwi::Format::DXT1; return true;
            case kD3DFMT_DXT3: *outIwiFmt = TextureUpscaleIwi::Format::DXT3; return true;
            case kD3DFMT_DXT5: *outIwiFmt = TextureUpscaleIwi::Format::DXT5; return true;
            default: return false; // DXT2/DXT4 (rare premultiplied-alpha
                // variants) and every non-DXT format are out of scope for
                // this feature, same restriction QueueUpscaleJobFromIwiFile
                // already applies -- a routine, expected skip, not an error.
        }
    }

    // Real capture: LockRect the texture's real, current GPU-resident base
    // mip, strip any row-padding pitch down to the tight block-compressed
    // size (same technique asset_capture.cpp's own WriteDdsFile already
    // proves safe for this engine's real textures), and queue it via
    // TextureUpscaleWorker -- direct real bytes, no IWI-header round trip
    // needed since width/height/format are already known exactly from
    // CreateTexture's own real params.
    void CaptureAndQueue(void* texture, const char* name, UINT width, UINT height, DWORD format)
    {
        TextureUpscaleIwi::Format iwiFmt;
        if (!D3dFmtToIwi(format, &iwiFmt)) return;

        void** texVtbl = *reinterpret_cast<void***>(texture);
        auto lockRect = reinterpret_cast<TextureLockRect_t>(texVtbl[kTextureLockRectVtableIndex]);
        auto unlockRect = reinterpret_cast<TextureUnlockRect_t>(texVtbl[kTextureUnlockRectVtableIndex]);

        D3dLockedRect locked{};
        HRESULT hr = lockRect(texture, 0, &locked, nullptr, 0);
        if (FAILED(hr) || !locked.pBits) {
            // Real, expected case for some real pool/usage combinations
            // (e.g. D3DPOOL_DEFAULT without D3DUSAGE_DYNAMIC may not support
            // locking well after creation, unlike right at creation time --
            // asset_capture.cpp's own comment on this). Logged once per
            // distinct name, not every frame the same bound texture is
            // re-tried (this function only runs once per texture anyway,
            // since the caller marks `handled` regardless of outcome).
            char buf[300];
            sprintf_s(buf, "[texture-viewport-capture] '%.200s': LockRect failed (hr=0x%08lX), skipping", name, static_cast<unsigned long>(hr));
            LogFromController(buf);
            return;
        }

        int blockSize = (iwiFmt == TextureUpscaleIwi::Format::DXT1) ? 8 : 16;
        int blockCols = (static_cast<int>(width) + 3) / 4;
        int blockRows = (static_cast<int>(height) + 3) / 4;
        int tightRowBytes = blockCols * blockSize;
        int srcPitch = locked.Pitch;
        if (srcPitch < tightRowBytes) srcPitch = tightRowBytes; // defensive,
            // never trust an external value blindly against a memory read.

        size_t tightSize = static_cast<size_t>(tightRowBytes) * blockRows;
        uint8_t* tightBuf = static_cast<uint8_t*>(malloc(tightSize));
        if (tightBuf) {
            const uint8_t* src = static_cast<const uint8_t*>(locked.pBits);
            for (int row = 0; row < blockRows; ++row) {
                memcpy(tightBuf + static_cast<size_t>(row) * tightRowBytes,
                       src + static_cast<size_t>(row) * srcPitch, tightRowBytes);
            }
        }
        unlockRect(texture, 0);
        if (!tightBuf) return;

        if (!TextureUpscaleWorker::QueueUpscaleJob(name, static_cast<int>(iwiFmt), width, height,
                                                    tightBuf, static_cast<uint32_t>(tightSize), g_modConfig.textureRenderRes, "viewport")) {
            free(tightBuf); // already in flight, queue full, or invalid --
                // QueueUpscaleJob logs the success case itself.
        }
    }

    HRESULT WINAPI Hook_SetTexture(void* This, DWORD stage, void* texture)
    {
        // Real call happens FIRST, completely unmodified -- pure observer,
        // matching this feature's own "never alter real behavior" standard.
        HRESULT hr = reinterpret_cast<SetTexture_t>(g_origSetTexture)(This, stage, texture);
        if (!texture || g_modConfig.textureRenderRes <= 1) return hr;

        EnsureTableLockInit();
        EnterCriticalSection(&g_tableLock);
        int idx = FindSlot(texture);
        bool shouldCapture = false;
        char nameCopy[256] = {};
        UINT width = 0, height = 0;
        DWORD format = 0;
        if (idx >= 0 && g_table[idx].texturePtr == texture && !g_table[idx].handled) {
            g_table[idx].handled = true; // mark before releasing the lock --
                // guarantees exactly one real capture attempt per texture,
                // even under concurrent SetTexture calls on different stages.
            shouldCapture = true;
            strncpy_s(nameCopy, g_table[idx].name, _TRUNCATE);
            width = g_table[idx].width; height = g_table[idx].height; format = g_table[idx].format;
        }
        LeaveCriticalSection(&g_tableLock);

        if (shouldCapture && !TextureUpscaleWorker::IsNameInFlight(nameCopy)) {
            uint32_t existingSize = 0;
            uint8_t* existing = TextureUpscaleCache::TryLoadCachedUpscaledIwi(nameCopy, g_modConfig.textureRenderRes, &existingSize);
            if (existing) {
                free(existing); // already cached from a prior session/mechanism
            } else {
                CaptureAndQueue(texture, nameCopy, width, height, format);
            }
        }
        return hr;
    }
}

// Temporary, bounded thread-correlation diagnostic (2026-09-29) -- real,
// live-reported suspicion that large pools of assets never get captured at
// all. Leading hypothesis: SetCurrentlyLoadingName (called from
// Hook_ImageFileLoadX64, thread_local) and OnCreateTexture (called from
// asset_capture.cpp's Hook_CreateTexture) may run on genuinely different
// threads -- this project's own already-confirmed x64 backend-thread
// architecture (render-thread-diag: "EndScene calling thread... DIFFERENT
// from main") makes this a real, plausible root cause, not a guess. If
// confirmed, thread_local correlation is fundamentally broken for any asset
// whose CreateTexture call is deferred to a different thread than the one
// that loaded it. Remove once this is confirmed or ruled out.
int g_setNameLogCount = 0;
int g_createTextureCorrelationLogCount = 0;
long g_totalCreateTextureCalls = 0;
long g_correlatedCreateTextureCalls = 0;
DWORD g_lastCoverageLogMs = 0;

void SetCurrentlyLoadingName(const char* name, bool wasHit)
{
    if (!name) { g_loadingState.active = false; return; }
    strncpy_s(g_loadingState.name, name, _TRUNCATE);
    g_loadingState.active = true;
    g_loadingState.wasHit = wasHit;

    if (g_setNameLogCount < 30) {
        ++g_setNameLogCount;
        char buf[300];
        sprintf_s(buf, "[x64-viewport-thread-diag] SetCurrentlyLoadingName('%.200s') on thread %lu",
            name, GetCurrentThreadId());
        LogFromController(buf);
    }
}

void ClearCurrentlyLoadingName()
{
    g_loadingState.active = false;
}

void OnCreateTexture(void* texturePtr, UINT width, UINT height, DWORD format)
{
    if (!texturePtr) return;
    ++g_totalCreateTextureCalls;
    if (g_loadingState.active) ++g_correlatedCreateTextureCalls;

    DWORD nowMs = GetTickCount();
    if (nowMs - g_lastCoverageLogMs > 10000) { // periodic, real coverage-ratio
        // evidence -- answers "are we missing huge pools of assets" directly:
        // a low correlated/total ratio means most real CreateTexture calls
        // happen with no "currently loading" name active at all, i.e. outside
        // any window Hook_ImageFileLoadX64 opened for them.
        g_lastCoverageLogMs = nowMs;
        char buf[300];
        sprintf_s(buf, "[x64-viewport-thread-diag] coverage: %ld/%ld real CreateTexture calls had an active load-name correlation window (%.1f%%)",
            g_correlatedCreateTextureCalls, g_totalCreateTextureCalls,
            g_totalCreateTextureCalls > 0 ? (100.0 * g_correlatedCreateTextureCalls / g_totalCreateTextureCalls) : 0.0);
        LogFromController(buf);
    }

    if (!g_loadingState.active) return;

    if (g_createTextureCorrelationLogCount < 30) {
        ++g_createTextureCorrelationLogCount;
        char buf[300];
        sprintf_s(buf, "[x64-viewport-thread-diag] OnCreateTexture('%.200s') on thread %lu (%ux%u fmt=0x%08lX)",
            g_loadingState.name, GetCurrentThreadId(), width, height, static_cast<unsigned long>(format));
        LogFromController(buf);
    }

    EnsureTableLockInit();
    EnterCriticalSection(&g_tableLock);
    int idx = FindSlot(texturePtr);
    if (idx >= 0) {
        g_table[idx].texturePtr = texturePtr;
        strncpy_s(g_table[idx].name, g_loadingState.name, _TRUNCATE);
        g_table[idx].width = width;
        g_table[idx].height = height;
        g_table[idx].format = format;
        // A substitution hit already created the texture from the
        // upscaled/overridden result -- nothing left to capture.
        g_table[idx].handled = g_loadingState.wasHit;
    }
    LeaveCriticalSection(&g_tableLock);
}

void InstallSetTextureHook(void* realDevice)
{
    if (g_modConfig.textureRenderRes <= 1) return; // strictly opt-in, matching
        // every other part of this feature -- no hook installed at all when
        // upscaling itself is disabled.
    if (!realDevice || g_hookInstalled) return;
    g_hookInstalled = true;

    void** deviceVtbl = *reinterpret_cast<void***>(realDevice);
    void* realSetTexture = deviceVtbl[kSetTextureVtableIndex];

    MH_STATUS s = MH_CreateHook(realSetTexture, reinterpret_cast<void*>(&Hook_SetTexture), &g_origSetTexture);
    char buf[128];
    sprintf_s(buf, "[texture-viewport-capture] MH_CreateHook(SetTexture @ %p) = %d", realSetTexture, static_cast<int>(s));
    LogFromController(buf);
    if (s == MH_OK) {
        MH_STATUS e = MH_EnableHook(realSetTexture);
        sprintf_s(buf, "[texture-viewport-capture] MH_EnableHook(SetTexture) = %d", static_cast<int>(e));
        LogFromController(buf);
    }
}

}
