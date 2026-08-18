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
using Hot_ToggleDiagramEditModeFn = void(__cdecl*)();
using Hot_ExportDiagramLayoutFn = void(__cdecl*)();
using Hot_LoadMenuFileFn = bool(__cdecl*)(const char*);
using Hot_IsMenuLoadedFn = bool(__cdecl*)();
using Hot_DrawMenuFrameFn = void(__cdecl*)(void*);
// Phase 2 (2026-08-17) -- fake MenuGameState hotkeys, see exports.cpp's own comment.
using Hot_VoidFn = void(__cdecl*)();

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
    // Harness-only diagram anchor editor (2026-08-05) -- optional, not part of
    // Valid()'s own required-export check, so an older ui_hot.dll build without
    // these two exports still loads and runs normally, just without edit mode.
    Hot_ToggleDiagramEditModeFn ToggleDiagramEditMode = nullptr;
    Hot_ExportDiagramLayoutFn ExportDiagramLayout = nullptr;
    // Real .menu renderer (2026-08-16/17, Phase 1) -- same "optional, not part of
    // Valid()'s required-export check" convention as the diagram editor above, so
    // an older ui_hot.dll build still loads fine, just without this feature.
    Hot_LoadMenuFileFn LoadMenuFile = nullptr;
    Hot_IsMenuLoadedFn IsMenuLoaded = nullptr;
    Hot_DrawMenuFrameFn DrawMenuFrame = nullptr;
    // Phase 2 -- same "optional" convention as the diagram editor/menu-loader above.
    Hot_VoidFn MenuGameState_ToggleTeam = nullptr;
    Hot_VoidFn MenuGameState_ToggleMatchRules = nullptr;
    Hot_VoidFn MenuGameState_AdvanceClock = nullptr;
    Hot_VoidFn MenuGameState_ToggleUnlockedPreset = nullptr;
    Hot_VoidFn MenuGameState_RefreshDebugReport = nullptr;
    Hot_VoidFn MenuGameState_NextItemIndex = nullptr; // Phase 3
    Hot_VoidFn MenuGameState_PrevItemIndex = nullptr;

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

// ---- Built-in source watcher (2026-08-05, live feedback: "hot reload shouldnt
// rely on any scripts it should work just like hmr") -------------------------------
//
// Previously this project's HMR loop was split across two separately-run processes:
// this exe (polls ui_hot.dll's build OUTPUT and hot-swaps it) plus watch.ps1, a
// separate PowerShell script the user had to remember to also start, which watched
// the SOURCE files and ran MSBuild. That's the actual reason "no visual changes even
// after restart" was reported -- without watch.ps1 (or a manual rebuild) actually
// running, ui_hot.dll's build output never changes, so TryHotSwap correctly has
// nothing new to load; TryHotSwap itself was never broken, the trigger for a new
// build simply didn't exist. Folded that other half in here instead: a background
// thread scans the same directories watch.ps1 did (proxy_d3d9/src, resource.h,
// this tool's own ui_hot/) every ~400ms for the newest last-write-time across all
// files in them, and runs MSBuild itself the moment that time advances -- true,
// single-process HMR, no second script to remember to start. watch.ps1 is kept
// only as a manual fallback (e.g. for watching from a terminal without the harness
// running at all); it is no longer required for normal use.
FILETIME GetNewestWriteTimeInDir(const char* dirPath)
{
    FILETIME newest = {};
    char pattern[MAX_PATH];
    sprintf_s(pattern, "%s\\*", dirPath);
    WIN32_FIND_DATAA find;
    HANDLE h = FindFirstFileA(pattern, &find);
    if (h == INVALID_HANDLE_VALUE) return newest;
    do {
        if (find.cFileName[0] == '.') continue; // skip "." / ".."
        if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue; // non-recursive -- proxy_d3d9/src and ui_hot/ are both flat
        if (CompareFileTime(&find.ftLastWriteTime, &newest) > 0) newest = find.ftLastWriteTime;
    } while (FindNextFileA(h, &find));
    FindClose(h);
    return newest;
}

FILETIME GetNewestWriteTimeOfFile(const char* filePath)
{
    FILETIME t = {};
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (GetFileAttributesExA(filePath, GetFileExInfoStandard, &attr)) t = attr.ftLastWriteTime;
    return t;
}

void GetUiHotProjectPath(char* outPath, size_t outSize)
{
    char exeDir[MAX_PATH];
    GetModuleFileNameA(nullptr, exeDir, sizeof(exeDir));
    char* lastSlash = strrchr(exeDir, '\\');
    if (lastSlash) *lastSlash = '\0';
    sprintf_s(outPath, outSize, "%s\\..\\..\\ui_hot\\ui_hot.vcxproj", exeDir);
}

DWORD WINAPI SourceWatcherThreadProc(LPVOID)
{
    char exeDir[MAX_PATH];
    GetModuleFileNameA(nullptr, exeDir, sizeof(exeDir));
    char* lastSlash = strrchr(exeDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    char proxySrcDir[MAX_PATH], resourceHeaderPath[MAX_PATH], uiHotDir[MAX_PATH], projectPath[MAX_PATH];
    sprintf_s(proxySrcDir, "%s\\..\\..\\..\\..\\proxy_d3d9\\src", exeDir);
    sprintf_s(resourceHeaderPath, "%s\\..\\..\\..\\..\\proxy_d3d9\\resource.h", exeDir);
    sprintf_s(uiHotDir, "%s\\..\\..\\ui_hot", exeDir);
    GetUiHotProjectPath(projectPath, sizeof(projectPath));

    FILETIME lastSeen = {};
    bool haveBaseline = false;

    while (g_running) {
        FILETIME newest = GetNewestWriteTimeInDir(proxySrcDir);
        FILETIME t2 = GetNewestWriteTimeOfFile(resourceHeaderPath);
        FILETIME t3 = GetNewestWriteTimeInDir(uiHotDir);
        if (CompareFileTime(&t2, &newest) > 0) newest = t2;
        if (CompareFileTime(&t3, &newest) > 0) newest = t3;

        if (!haveBaseline) {
            lastSeen = newest;
            haveBaseline = true; // don't rebuild on startup just because files already exist
        } else if (CompareFileTime(&newest, &lastSeen) > 0) {
            lastSeen = newest;
            printf("[ui_harness] source change detected -- rebuilding ui_hot.vcxproj...\n");

            char cmdLine[1024];
            sprintf_s(cmdLine, "\"C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe\" "
                                "\"%s\" /p:Configuration=%s /p:Platform=Win32 /nologo /v:quiet", projectPath, kConfigName);

            STARTUPINFOA si = { sizeof(si) };
            PROCESS_INFORMATION pi = {};
            if (CreateProcessA(nullptr, cmdLine, nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                 nullptr, nullptr, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, INFINITE);
                DWORD exitCode = 1;
                GetExitCodeProcess(pi.hProcess, &exitCode);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                if (exitCode == 0) printf("[ui_harness] rebuild OK -- will hot-swap within ~500ms\n");
                else printf("[ui_harness] rebuild FAILED (exit %lu) -- keeping last good version\n", exitCode);
            } else {
                printf("[ui_harness] failed to launch MSBuild -- check the hardcoded path in SourceWatcherThreadProc\n");
            }
        }
        Sleep(400);
    }
    return 0;
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
    m.ToggleDiagramEditMode = reinterpret_cast<Hot_ToggleDiagramEditModeFn>(GetProcAddress(dll, "Hot_ToggleDiagramEditMode"));
    m.ExportDiagramLayout = reinterpret_cast<Hot_ExportDiagramLayoutFn>(GetProcAddress(dll, "Hot_ExportDiagramLayout"));
    m.LoadMenuFile = reinterpret_cast<Hot_LoadMenuFileFn>(GetProcAddress(dll, "Hot_LoadMenuFile"));
    m.IsMenuLoaded = reinterpret_cast<Hot_IsMenuLoadedFn>(GetProcAddress(dll, "Hot_IsMenuLoaded"));
    m.DrawMenuFrame = reinterpret_cast<Hot_DrawMenuFrameFn>(GetProcAddress(dll, "Hot_DrawMenuFrame"));
    m.MenuGameState_ToggleTeam = reinterpret_cast<Hot_VoidFn>(GetProcAddress(dll, "Hot_MenuGameState_ToggleTeam"));
    m.MenuGameState_ToggleMatchRules = reinterpret_cast<Hot_VoidFn>(GetProcAddress(dll, "Hot_MenuGameState_ToggleMatchRules"));
    m.MenuGameState_AdvanceClock = reinterpret_cast<Hot_VoidFn>(GetProcAddress(dll, "Hot_MenuGameState_AdvanceClock"));
    m.MenuGameState_ToggleUnlockedPreset = reinterpret_cast<Hot_VoidFn>(GetProcAddress(dll, "Hot_MenuGameState_ToggleUnlockedPreset"));
    m.MenuGameState_RefreshDebugReport = reinterpret_cast<Hot_VoidFn>(GetProcAddress(dll, "Hot_MenuGameState_RefreshDebugReport"));
    m.MenuGameState_NextItemIndex = reinterpret_cast<Hot_VoidFn>(GetProcAddress(dll, "Hot_MenuGameState_NextItemIndex"));
    m.MenuGameState_PrevItemIndex = reinterpret_cast<Hot_VoidFn>(GetProcAddress(dll, "Hot_MenuGameState_PrevItemIndex"));

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
        // BUG FIX (2026-08-05, live-reported: "we need all assets from the main dll
        // to load"): this used to pass the HOST exe's own hInstance here, but the
        // embedded font + button-glyph-icon resources (proxy_d3d9.rc) are compiled
        // into ui_hot.dll itself, not this host -- FindResourceA against the wrong
        // module handle silently finds nothing. current.dll IS the loaded copy of
        // ui_hot.dll, the correct handle for its own embedded resources.
        current.LoadOverlayFonts(current.dll);
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

    // Real .menu renderer (2026-08-16/17, Phase 1) -- load a real screen by default
    // on startup so the tool is immediately useful without requiring a keypress
    // first; press 1-4 (see the main loop below) to switch to a different one.
    if (haveModule && current.LoadMenuFile) {
        bool ok = current.LoadMenuFile("D:\\Tools\\OpenAssetTools\\zone_dump\\ui\\scriptmenus\\survival_armory_weapon.menu");
        printf("[ui_harness] startup .menu load: %s\n", ok ? "OK" : "FAILED");
    }

    // Single-process HMR (2026-08-05) -- see SourceWatcherThreadProc's own comment.
    // Runs for the lifetime of the process; reads g_running to know when to stop.
    CreateThread(nullptr, 0, SourceWatcherThreadProc, nullptr, 0, nullptr);

    EdgeTracker upT, downT, leftT, rightT, selectT, backT, tabPrevT, tabNextT;
    EdgeTracker editToggleT, exportT; // F2/F3 -- harness-only diagram anchor editor
    DWORD lastPollMs = GetTickCount();
    constexpr DWORD kPollIntervalMs = 500;

    // Real .menu renderer (2026-08-16/17, Phase 1) -- number keys 1-4 load one of a
    // small hardcoded set of real .menu files (the ones this whole feature was built
    // for: Survival's between-round armory screens, plus one plain static screen and
    // one already-deeply-RE'd screen for cross-checking) into the harness for
    // inspection. Not a real file picker -- fine for Phase 1's own smoke-test scope,
    // see menu_render.h's own header comment. Extend this list as more screens need
    // checking; no code changes needed elsewhere to add a 5th/6th/etc. entry beyond
    // also adding its own EdgeTracker + key check below.
    const char* kMenuFileChoices[4] = {
        "D:\\Tools\\OpenAssetTools\\zone_dump\\ui\\scriptmenus\\survival_armory_weapon.menu",
        "D:\\Tools\\OpenAssetTools\\zone_dump\\ui\\waves.menu",
        "D:\\Tools\\OpenAssetTools\\zone_dump\\ui\\stance.menu",
        "D:\\Tools\\OpenAssetTools\\zone_dump\\ui\\pc_options_video_ingame.menu",
    };
    EdgeTracker menuKey1T, menuKey2T, menuKey3T, menuKey4T;

    // Phase 2 (2026-08-17) -- fake MenuGameState hotkeys, so real `visible`/`exp`
    // conditional logic in a loaded .menu (team-locked items, unlock-gated
    // attachments, etc.) can actually be exercised and visually verified instead of
    // always evaluating against one fixed default state. T/M/C/U/R chosen to avoid
    // every already-bound key above (arrows/Enter/Backspace/Q/E/F2/F3/1-4/Escape).
    EdgeTracker toggleTeamT, toggleMatchRulesT, advanceClockT, toggleUnlockedT, refreshReportT;
    EdgeTracker nextItemIndexT, prevItemIndexT; // Phase 3 -- '[' / ']', selected_item_index nav

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

        // Harness-only diagram anchor editor (2026-08-05, user-requested: "give me
        // editing functionality in the harness ... drag the sprites around to the
        // correct pos"). F2 toggles edit mode (drag handles appear on the currently
        // open Stick/Button Layout diagram); F3 exports the current GlyphStyle's
        // live-edited layout to exported_diagram_layout.txt. No controller equivalent
        // -- this is a keyboard-only dev feature, not part of the screen's real
        // navigation scheme.
        bool editToggleEdge = editToggleT.Tick((GetAsyncKeyState(VK_F2) & 0x8000) != 0);
        bool exportEdge = exportT.Tick((GetAsyncKeyState(VK_F3) & 0x8000) != 0);
        if (haveModule && editToggleEdge && current.ToggleDiagramEditMode) current.ToggleDiagramEditMode();
        if (haveModule && exportEdge && current.ExportDiagramLayout) current.ExportDiagramLayout();

        bool menuKey1Edge = menuKey1T.Tick((GetAsyncKeyState('1') & 0x8000) != 0);
        bool menuKey2Edge = menuKey2T.Tick((GetAsyncKeyState('2') & 0x8000) != 0);
        bool menuKey3Edge = menuKey3T.Tick((GetAsyncKeyState('3') & 0x8000) != 0);
        bool menuKey4Edge = menuKey4T.Tick((GetAsyncKeyState('4') & 0x8000) != 0);
        if (haveModule && current.LoadMenuFile) {
            int chosenIdx = -1;
            if (menuKey1Edge) chosenIdx = 0;
            else if (menuKey2Edge) chosenIdx = 1;
            else if (menuKey3Edge) chosenIdx = 2;
            else if (menuKey4Edge) chosenIdx = 3;
            if (chosenIdx >= 0) {
                bool ok = current.LoadMenuFile(kMenuFileChoices[chosenIdx]);
                char titleBuf[512];
                sprintf_s(titleBuf, "MW32011NCP UI Harness -- .menu %s: %s",
                    ok ? "loaded" : "FAILED to parse", kMenuFileChoices[chosenIdx]);
                SetWindowTextA(hwnd, titleBuf);
            }
        }

        // Phase 2 fake-GameState hotkeys -- each Hot_MenuGameState_* export already
        // re-writes menu_parse_debug.txt itself (see exports.cpp), so the evaluated
        // effect is checkable from text output immediately; R additionally forces a
        // refresh with no state change, useful right after a hot-reload.
        bool toggleTeamEdge = toggleTeamT.Tick((GetAsyncKeyState('T') & 0x8000) != 0);
        bool toggleMatchRulesEdge = toggleMatchRulesT.Tick((GetAsyncKeyState('M') & 0x8000) != 0);
        bool advanceClockEdge = advanceClockT.Tick((GetAsyncKeyState('C') & 0x8000) != 0);
        bool toggleUnlockedEdge = toggleUnlockedT.Tick((GetAsyncKeyState('U') & 0x8000) != 0);
        bool refreshReportEdge = refreshReportT.Tick((GetAsyncKeyState('R') & 0x8000) != 0);
        // Phase 3 -- '[' / ']' (VK_OEM_4/6): selected_item_index nav, see
        // Hot_MenuGameState_NextItemIndex's own comment for what this actually affects.
        bool nextItemIndexEdge = nextItemIndexT.Tick((GetAsyncKeyState(VK_OEM_6) & 0x8000) != 0);
        bool prevItemIndexEdge = prevItemIndexT.Tick((GetAsyncKeyState(VK_OEM_4) & 0x8000) != 0);
        if (haveModule) {
            if (toggleTeamEdge && current.MenuGameState_ToggleTeam) current.MenuGameState_ToggleTeam();
            if (toggleMatchRulesEdge && current.MenuGameState_ToggleMatchRules) current.MenuGameState_ToggleMatchRules();
            if (advanceClockEdge && current.MenuGameState_AdvanceClock) current.MenuGameState_AdvanceClock();
            if (toggleUnlockedEdge && current.MenuGameState_ToggleUnlockedPreset) current.MenuGameState_ToggleUnlockedPreset();
            if (refreshReportEdge && current.MenuGameState_RefreshDebugReport) current.MenuGameState_RefreshDebugReport();
            if (nextItemIndexEdge && current.MenuGameState_NextItemIndex) current.MenuGameState_NextItemIndex();
            if (prevItemIndexEdge && current.MenuGameState_PrevItemIndex) current.MenuGameState_PrevItemIndex();
        }

        device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(20, 25, 35), 1.0f, 0);
        device->BeginScene();

        if (haveModule) {
            bool openRequestedEdge = !current.IsOpen() && selectEdge;
            current.TickInput(openRequestedEdge, upEdge, downEdge, leftEdge, rightEdge,
                                selectEdge, backEdge, tabPrevEdge, tabNextEdge);
            current.DrawFrame(device);
            if (current.DrawMenuFrame) current.DrawMenuFrame(device);
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
