// vram_diag.cpp -- see vram_diag.h for the full rationale.
#include <windows.h>
#include <dxgi1_4.h>
#include <cstdio>

extern void LogFromController(const char* msg); // defined in dllmain.cpp

namespace {

bool g_dxgiInitAttempted = false;
IDXGIAdapter3* g_dxgiAdapter3 = nullptr; // real interface, real memory-info source below.
                                          // Never released -- matches this project's own
                                          // established "install once, never uninstall"
                                          // background-resource convention (e.g. the
                                          // background threads in dllmain.cpp).
DWORD g_lastRealVramLogMs = 0;

// Lazily creates and caches the DXGI adapter this process' default (adapter 0) GPU maps
// to. Deliberately does NOT try to match the exact adapter the live D3D9 device is bound
// to via LUID -- IDirect3D9 has no direct LUID accessor on plain (non-Ex) interfaces, and
// adapter 0 (the default/primary GPU) is correct for the overwhelming majority of real
// systems (anything with a single GPU, which covers virtually all real playtests this
// project has ever run against) -- a "trivial passthrough first" simplification, not a
// silent correctness gap: multi-GPU systems where the game runs on a NON-default adapter
// are the one real case this would misreport, flagged here rather than hidden.
bool EnsureDxgiAdapter()
{
    if (g_dxgiInitAttempted) return g_dxgiAdapter3 != nullptr;
    g_dxgiInitAttempted = true;

    IDXGIFactory1* factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    if (FAILED(hr) || !factory) {
        LogFromController("[vram-diag-real] CreateDXGIFactory1 FAILED -- real VRAM diagnostic unavailable this session");
        return false;
    }

    IDXGIAdapter1* adapter1 = nullptr;
    hr = factory->EnumAdapters1(0, &adapter1);
    factory->Release();
    if (FAILED(hr) || !adapter1) {
        LogFromController("[vram-diag-real] EnumAdapters1(0) FAILED -- real VRAM diagnostic unavailable this session");
        return false;
    }

    hr = adapter1->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&g_dxgiAdapter3));
    adapter1->Release();
    if (FAILED(hr) || !g_dxgiAdapter3) {
        // Real, expected failure mode on pre-Windows-10 systems -- IDXGIAdapter3 (and
        // QueryVideoMemoryInfo) is a Windows 10+ interface. Not expected on this
        // project's own dev/test machine, but a real system this could see live use on.
        LogFromController("[vram-diag-real] IDXGIAdapter3 unavailable (needs Windows 10+) -- real VRAM diagnostic unavailable this session");
        return false;
    }

    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(g_dxgiAdapter3->GetDesc1(&desc))) {
        char nameBuf[256];
        // DXGI_ADAPTER_DESC1.Description is WCHAR[128] -- narrow it for this project's
        // own plain-char log format, same as every other diagnostic line in this codebase.
        int written = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nameBuf, sizeof(nameBuf), nullptr, nullptr);
        if (written <= 0) { nameBuf[0] = '\0'; }
        char buf[400];
        sprintf_s(buf, "[vram-diag-real] DXGI adapter resolved: \"%s\" dedicatedVideoMemoryMB=%.1f dedicatedSystemMemoryMB=%.1f sharedSystemMemoryMB=%.1f",
                   nameBuf,
                   static_cast<double>(desc.DedicatedVideoMemory) / (1024.0 * 1024.0),
                   static_cast<double>(desc.DedicatedSystemMemory) / (1024.0 * 1024.0),
                   static_cast<double>(desc.SharedSystemMemory) / (1024.0 * 1024.0));
        LogFromController(buf);
    }

    return true;
}

} // namespace

void LogRealVramDiagIfDue()
{
    DWORD nowMs = GetTickCount();
    if (nowMs - g_lastRealVramLogMs < 1000) return;
    g_lastRealVramLogMs = nowMs;

    if (!EnsureDxgiAdapter()) return; // failure already logged once, inside EnsureDxgiAdapter

    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    HRESULT hr = g_dxgiAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);
    if (FAILED(hr)) return; // transient failure -- next ~1s tick will retry, no need to spam

    char buf[300];
    sprintf_s(buf,
        "[vram-diag-real] LOCAL segment: budgetMB=%.1f currentUsageMB=%.1f availableForReservationMB=%.1f currentReservationMB=%.1f",
        static_cast<double>(info.Budget) / (1024.0 * 1024.0),
        static_cast<double>(info.CurrentUsage) / (1024.0 * 1024.0),
        static_cast<double>(info.AvailableForReservation) / (1024.0 * 1024.0),
        static_cast<double>(info.CurrentReservation) / (1024.0 * 1024.0));
    LogFromController(buf);
}
