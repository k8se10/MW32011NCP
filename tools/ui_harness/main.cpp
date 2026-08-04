// main.cpp -- tools/ui_harness: a standalone D3D9 window for iterating on the custom
// Options replacement screen (re_notes/known_issues.md issue #66) without needing the
// actual game running, with true HMR-style hot-reload: the UI code (overlay_hud.cpp
// and friends) lives in a separate DLL, ui_hot.dll, that this host loads dynamically.
// Rebuild ui_hot.vcxproj (or run watch.ps1 to do it automatically on file save) and
// this window updates within about a second, with no visible relaunch.
//
// Hot-swap mechanism (the standard "copy-then-load" pattern for native DLL hot-reload
// -- Windows won't let a rebuild overwrite a DLL this process has LoadLibrary'd, so
// this process never loads ui_hot.dll's own real build output directly): every
// kPollIntervalMs, check that file's last-write-time; if it changed, copy it to a
// freshly-numbered scratch file next to this exe and LoadLibrary THAT instead, so
// MSBuild always has exclusive write access to its own real output path regardless
// of what this process currently has loaded. The old module is freed and its scratch
// copy deleted (best-effort) right after the new one loads successfully.
//
// Menu-open/tab state and cached D3D9 textures live as globals INSIDE ui_hot.dll, so
// they reset to nothing on every swap -- same trade-off as web dev's fast-refresh
// sometimes losing component state, and worth it for the iteration speed. The small
// number of textures (toast/panel/tab-bar caches) the OLD module's globals held a
// reference to are never explicitly released before that module unloads -- a real,
// accepted leak of a few small D3D9 resources per reload, fine for a dev-only tool
// run for a session at a time, not worth the extra complexity to avoid.
#include <windows.h>
#include <d3d9.h>
#include <Xinput.h>
#include <cstdio>
#include <cstring>

#include "controller_input.h"

#pragma comment(lib, "d3d9.lib")

namespace {

bool g_running = true;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_DESTROY:
            g_running = false;
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) DestroyWindow(hwnd);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

struct EdgeTracker {
    bool wasHeld = false;
    bool Tick(bool heldNow)
    {
        bool edge = heldNow && !wasHeld;
        wasHeld = heldNow;
        return edge;
    }
};

// ---- Hot-swap plumbing -------------------------------------------------------

using Hot_SetWindowFn = void(__cdecl*)(void*);
using Hot_LoadOverlayFontsFn = bool(__cdecl*)(void*);
using Hot_UnloadOverlayFontsFn = void(__cdecl*)();
using Hot_LoadModConfigFn = void(__cdecl*)();
using Hot_TickInputFn = bool(__cdecl*)(bool, bool, bool, bool, bool, bool, bool, bool, bool);
using Hot_IsOpenFn = bool(__cdecl*)();
using Hot_ResetOnMenuCloseFn = void(__cdecl*)();
using Hot_DrawFrameFn = void(__cdecl*)(void*);

struct HotModule {
    HMODULE dll = nullptr;
    char loadedCopyPath[MAX_PATH] = {};
    Hot_SetWindowFn SetWindow = nullptr;
    Hot_LoadOverlayFontsFn LoadOverlayFonts = nullptr;
    Hot_UnloadOverlayFontsFn UnloadOverlayFonts = nullptr;
    Hot_LoadModConfigFn LoadModConfig = nullptr;
    Hot_TickInputFn TickInput = nullptr;
    Hot_IsOpenFn IsOpen = nullptr;
    Hot_ResetOnMenuCloseFn ResetOnMenuClose = nullptr;
    Hot_DrawFrameFn DrawFrame = nullptr;

    bool Valid() const { return dll && TickInput && DrawFrame; }
};

#ifdef _DEBUG
constexpr const char* kConfigName = "Debug";
#else
constexpr const char* kConfigName = "Release";
#endif

void GetSourceDllPath(char* outPath, size_t outSize)
{
    char exeDir[MAX_PATH];
    GetModuleFileNameA(nullptr, exeDir, sizeof(exeDir));
    char* lastSlash = strrchr(exeDir, '\\');
    if (lastSlash) *lastSlash = '\0';
    sprintf_s(outPath, outSize, "%s\\..\\..\\ui_hot\\bin\\%s\\ui_hot.dll", exeDir, kConfigName);
}

void GetScratchCopyPath(char* outPath, size_t outSize, int counter)
{
    char exeDir[MAX_PATH];
    GetModuleFileNameA(nullptr, exeDir, sizeof(exeDir));
    char* lastSlash = strrchr(exeDir, '\\');
    if (lastSlash) *lastSlash = '\0';
    sprintf_s(outPath, outSize, "%s\\ui_hot_live_%d.dll", exeDir, counter);
}

// Loads `copyPath` and resolves every export this host needs. Returns false (and
// leaves *out untouched) if the DLL doesn't load or is missing an export -- a stale
// or partially-written build should never take down the whole harness.
bool TryBindModule(const char* copyPath, HotModule& out)
{
    HMODULE dll = LoadLibraryA(copyPath);
    if (!dll) return false;

    HotModule m;
    m.dll = dll;
    strncpy_s(m.loadedCopyPath, copyPath, _TRUNCATE);
    m.SetWindow = reinterpret_cast<Hot_SetWindowFn>(GetProcAddress(dll, "Hot_SetWindow"));
    m.LoadOverlayFonts = reinterpret_cast<Hot_LoadOverlayFontsFn>(GetProcAddress(dll, "Hot_LoadOverlayFonts"));
    m.UnloadOverlayFonts = reinterpret_cast<Hot_UnloadOverlayFontsFn>(GetProcAddress(dll, "Hot_UnloadOverlayFonts"));
    m.LoadModConfig = reinterpret_cast<Hot_LoadModConfigFn>(GetProcAddress(dll, "Hot_LoadModConfig"));
    m.TickInput = reinterpret_cast<Hot_TickInputFn>(GetProcAddress(dll, "Hot_TickInput"));
    m.IsOpen = reinterpret_cast<Hot_IsOpenFn>(GetProcAddress(dll, "Hot_IsOpen"));
    m.ResetOnMenuClose = reinterpret_cast<Hot_ResetOnMenuCloseFn>(GetProcAddress(dll, "Hot_ResetOnMenuClose"));
    m.DrawFrame = reinterpret_cast<Hot_DrawFrameFn>(GetProcAddress(dll, "Hot_DrawFrame"));

    if (!m.Valid()) {
        FreeLibrary(dll);
        return false;
    }
    out = m;
    return true;
}

} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "MW32011NCP_UIHarness";
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    RegisterClassA(&wc);

    constexpr int kWindowW = 1920;
    constexpr int kWindowH = 1080;
    RECT windowRect = { 0, 0, kWindowW, kWindowH };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowA(wc.lpszClassName, "MW32011NCP Options Screen -- UI Harness (hot-reload)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOW);

    IDirect3D9* d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d9) { MessageBoxA(hwnd, "Direct3DCreate9 failed", "UI Harness", MB_OK); return 1; }

    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.hDeviceWindow = hwnd;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    IDirect3DDevice9* device = nullptr;
    HRESULT hr = d3d9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);
    if (FAILED(hr)) {
        hr = d3d9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
    }
    if (FAILED(hr)) { MessageBoxA(hwnd, "CreateDevice failed", "UI Harness", MB_OK); return 1; }

    char sourceDllPath[MAX_PATH];
    GetSourceDllPath(sourceDllPath, sizeof(sourceDllPath));

    HotModule current;
    int copyCounter = 0;
    FILETIME lastLoadedWriteTime = {};
    bool haveModule = false;

    auto TryHotSwap = [&]() {
        WIN32_FILE_ATTRIBUTE_DATA attr;
        if (!GetFileAttributesExA(sourceDllPath, GetFileExInfoStandard, &attr)) return; // not built yet
        if (haveModule && CompareFileTime(&attr.ftLastWriteTime, &lastLoadedWriteTime) == 0) return; // unchanged

        // Give MSBuild's linker a moment to finish flushing before we copy -- a
        // changed timestamp can appear slightly before the file is fully written.
        Sleep(150);

        char scratchPath[MAX_PATH];
        GetScratchCopyPath(scratchPath, sizeof(scratchPath), ++copyCounter);
        if (!CopyFileA(sourceDllPath, scratchPath, FALSE)) {
            printf("[ui_harness] ui_hot.dll changed but copy failed (likely still being written) -- will retry\n");
            --copyCounter;
            return;
        }

        HotModule fresh;
        if (!TryBindModule(scratchPath, fresh)) {
            printf("[ui_harness] ui_hot.dll rebuilt but failed to load/bind -- keeping previous version\n");
            DeleteFileA(scratchPath);
            return;
        }

        // New module is good -- tear down the old one (if any) and swap in.
        char oldCopyPath[MAX_PATH] = {};
        if (haveModule) {
            strncpy_s(oldCopyPath, current.loadedCopyPath, _TRUNCATE);
            if (current.UnloadOverlayFonts) current.UnloadOverlayFonts();
            FreeLibrary(current.dll);
        }

        current = fresh;
        haveModule = true;
        lastLoadedWriteTime = attr.ftLastWriteTime;

        current.SetWindow(hwnd);
        current.LoadOverlayFonts(hInstance);
        current.LoadModConfig();
        // Menu/tab state reset to zero automatically (fresh DLL globals) -- explicit
        // call kept here for clarity/documentation, not strictly required.
        current.ResetOnMenuClose();

        if (oldCopyPath[0]) DeleteFileA(oldCopyPath); // best-effort; fine if it fails
        printf("[ui_harness] hot-reloaded ui_hot.dll (build #%d)\n", copyCounter);
        SetWindowTextA(hwnd, "MW32011NCP UI Harness -- hot-reloaded! Press Enter/A to open Options");
    };

    TryHotSwap();
    if (!haveModule) {
        MessageBoxA(hwnd, "ui_hot.dll hasn't been built yet.\nBuild tools\\ui_harness\\ui_hot\\ui_hot.vcxproj first.",
            "UI Harness", MB_OK);
    }

    EdgeTracker upT, downT, leftT, rightT, selectT, backT, tabPrevT, tabNextT;
    DWORD lastPollMs = GetTickCount();
    constexpr DWORD kPollIntervalMs = 500;

    MSG msg = {};
    while (g_running) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) g_running = false;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!g_running) break;

        DWORD nowMs = GetTickCount();
        if (nowMs - lastPollMs >= kPollIntervalMs) {
            lastPollMs = nowMs;
            TryHotSwap();
        }

        unsigned short buttons = 0;
        unsigned char lt = 0, rt = 0;
        bool haveController = Controller_GetRawButtonsAndTriggers(buttons, lt, rt);

        bool upHeld = (haveController && (buttons & XINPUT_GAMEPAD_DPAD_UP)) || (GetAsyncKeyState(VK_UP) & 0x8000);
        bool downHeld = (haveController && (buttons & XINPUT_GAMEPAD_DPAD_DOWN)) || (GetAsyncKeyState(VK_DOWN) & 0x8000);
        bool leftHeld = (haveController && (buttons & XINPUT_GAMEPAD_DPAD_LEFT)) || (GetAsyncKeyState(VK_LEFT) & 0x8000);
        bool rightHeld = (haveController && (buttons & XINPUT_GAMEPAD_DPAD_RIGHT)) || (GetAsyncKeyState(VK_RIGHT) & 0x8000);
        bool selectHeld = (haveController && (buttons & XINPUT_GAMEPAD_A)) || (GetAsyncKeyState(VK_RETURN) & 0x8000);
        bool backHeld = (haveController && (buttons & XINPUT_GAMEPAD_B)) || (GetAsyncKeyState(VK_BACK) & 0x8000);
        bool tabPrevHeld = (haveController && (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER)) || (GetAsyncKeyState('Q') & 0x8000);
        bool tabNextHeld = (haveController && (buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER)) || (GetAsyncKeyState('E') & 0x8000);

        bool upEdge = upT.Tick(upHeld);
        bool downEdge = downT.Tick(downHeld);
        bool leftEdge = leftT.Tick(leftHeld);
        bool rightEdge = rightT.Tick(rightHeld);
        bool selectEdge = selectT.Tick(selectHeld);
        bool backEdge = backT.Tick(backHeld);
        bool tabPrevEdge = tabPrevT.Tick(tabPrevHeld);
        bool tabNextEdge = tabNextT.Tick(tabNextHeld);

        device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(20, 25, 35), 1.0f, 0);
        device->BeginScene();

        if (haveModule) {
            bool openRequestedEdge = !current.IsOpen() && selectEdge;
            current.TickInput(openRequestedEdge, upEdge, downEdge, leftEdge, rightEdge,
                                selectEdge, backEdge, tabPrevEdge, tabNextEdge);
            current.DrawFrame(device);
        }

        device->EndScene();
        device->Present(nullptr, nullptr, nullptr, nullptr);
    }

    if (haveModule) {
        if (current.UnloadOverlayFonts) current.UnloadOverlayFonts();
        FreeLibrary(current.dll);
        DeleteFileA(current.loadedCopyPath);
    }
    if (device) device->Release();
    if (d3d9) d3d9->Release();
    return 0;
}
