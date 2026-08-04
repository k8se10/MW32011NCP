// main.cpp -- tools/ui_harness: a standalone D3D9 window for iterating on the custom
// Options replacement screen (re_notes/known_issues.md issue #66) without needing the
// actual game running. Calls the REAL overlay_hud.cpp drawing/input code by path
// (proxy_d3d9/src/overlay_hud.cpp is compiled directly into this project, not
// copied) -- what you see here is exactly what ships in the game, modulo the fake
// dvar/keybind data real_settings_mock.cpp stands in for (see its own header).
//
// Controls: D-pad/stick-equivalent via a real XInput controller if one's connected,
// otherwise arrow keys + Enter (A) + Backspace (B) + Q/E (LB/RB, tab switch) on the
// keyboard. Esc closes the harness window.
#include <windows.h>
#include <d3d9.h>
#include <Xinput.h>
#include <cstdio>

#include "mod_config.h"
#include "overlay_hud.h"
#include "controller_input.h"

#pragma comment(lib, "d3d9.lib")

extern void SetHarnessWindow(HWND hwnd); // harness_stubs.cpp

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

// Rising-edge helper -- mirrors this project's own InjectControllerMenuNav pattern
// (analog_input_hooks.cpp): held-this-frame vs. held-last-frame.
struct EdgeTracker {
    bool wasHeld = false;
    bool Tick(bool heldNow)
    {
        bool edge = heldNow && !wasHeld;
        wasHeld = heldNow;
        return edge;
    }
};

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
    HWND hwnd = CreateWindowA(wc.lpszClassName, "MW32011NCP Options Screen -- UI Harness (not the real game)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 1;
    SetHarnessWindow(hwnd);
    ShowWindow(hwnd, SW_SHOW);

    IDirect3D9* d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d9) { MessageBoxA(hwnd, "Direct3DCreate9 failed", "UI Harness", MB_OK); return 1; }

    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.hDeviceWindow = hwnd;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE; // vsync -- no reason to peg a core spinning

    IDirect3DDevice9* device = nullptr;
    HRESULT hr = d3d9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);
    if (FAILED(hr)) {
        // Common on a machine with no real GPU driver available to this session --
        // software vertex processing is slower but works everywhere.
        hr = d3d9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
    }
    if (FAILED(hr)) { MessageBoxA(hwnd, "CreateDevice failed", "UI Harness", MB_OK); return 1; }

    LoadOverlayFonts(hInstance); // same embedded Barlow Condensed font the real game uses
    LoadModConfig();             // creates/reads its OWN mw3ncp_config.ini next to this exe

    EdgeTracker upT, downT, leftT, rightT, selectT, backT, tabPrevT, tabNextT;

    MSG msg = {};
    while (g_running) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) g_running = false;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!g_running) break;

        // Real XInput controller if one's connected; keyboard fallback otherwise --
        // both map onto the SAME fixed physical buttons this project's own menu
        // navigation always uses (see analog_input_hooks.cpp's own D-pad/LB/RB
        // handling), never through the remappable ButtonLayout system.
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

        // No real pause menu here to detect "focused on the Options button" --
        // openRequestedEdge is just "A/Enter pressed while the menu isn't already
        // open", which is all CustomOptionsMenu_TickInput actually needs (see
        // overlay_hud.h's own comment on the real PAUSE_LIST_1 detection this
        // stands in for).
        bool openRequestedEdge = !CustomOptionsMenu_IsOpen() && selectEdge;

        CustomOptionsMenu_TickInput(openRequestedEdge, upEdge, downEdge, leftEdge, rightEdge,
                                      selectEdge, backEdge, tabPrevEdge, tabNextEdge);

        device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(20, 25, 35), 1.0f, 0);
        device->BeginScene();
        RunCustomOptionsMenuHarnessFrame(device);
        if (!CustomOptionsMenu_IsOpen()) {
            // Nothing else to draw when the menu's closed -- print a hint via the
            // window title instead of needing our own separate text-draw path.
            SetWindowTextA(hwnd, "MW32011NCP UI Harness -- press Enter/A to open Options (Esc to quit)");
        } else {
            SetWindowTextA(hwnd, "MW32011NCP UI Harness -- Options open (Q/E tabs, Backspace/B closes)");
        }
        device->EndScene();
        device->Present(nullptr, nullptr, nullptr, nullptr);
    }

    UnloadOverlayFonts();
    if (device) device->Release();
    if (d3d9) d3d9->Release();
    return 0;
}
