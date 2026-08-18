// dll_stubs.cpp -- satisfies the small set of extern symbols overlay_hud.cpp/
// mod_config.cpp reference that normally come from the real game hooks
// (d3d9_hook.cpp/analog_input_hooks.cpp/dllmain.cpp), none of which exist in this
// standalone harness. Lives in ui_hot.dll (not the host exe) since overlay_hud.cpp/
// mod_config.cpp are compiled into this DLL, not the host, under the hot-reload
// architecture (tools/ui_harness/README.md).
//
// Every stub here is either never actually called by the Options-menu code path
// this harness exercises (DrawCustomOptionsMenuIfOpen), or has an obviously-safe
// harness-local answer -- see each one's own comment.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include "mod_config.h" // for PhysicalInput/GlyphStyle -- ui_hot.vcxproj already adds
                          // proxy_d3d9/src to its include path (overlay_hud.cpp/
                          // mod_config.cpp are compiled from there directly)

namespace {
HWND g_harnessWindow = nullptr;
}

// Exported (exports.cpp) so the host can tell this DLL which real window it's
// drawing into, once per load -- window handle itself doesn't change across a
// hot-reload (the host keeps the same window; only this DLL gets swapped).
void SetHarnessWindow(HWND hwnd)
{
    g_harnessWindow = hwnd;
}

// Real definition (dllmain.cpp) logs to proxy_d3d9.log + OutputDebugString. Harness
// equivalent: stdout + the debugger's Output window.
void LogFromController(const char* msg)
{
    printf("[ui_hot] %s\n", msg);
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}

// Real definition (d3d9_hook.cpp) returns the game's own hooked window -- only used
// by overlay_hud.cpp as a GetClientRect fallback when GetViewport fails, and by
// cursor/hint-slot drawing this harness never calls.
extern "C" HWND GetGameWindow()
{
    return g_harnessWindow;
}

// Real definition (d3d9_hook.cpp) captures WM_MOUSEMOVE specifically, because the
// real game's GetCursorPos-vs-window-coordinate-space can disagree under DPI
// virtualization (see that function's own comment). This harness owns its window
// outright with no such concern, so real-time GetCursorPos+ScreenToClient is exactly
// as accurate and needs no message-capture plumbing of its own -- this IS the real
// mouse position the Options screen's new click support (2026-08-04) hit-tests
// against, not a stand-in.
extern "C" bool GetLastMouseMoveClientPos(int& outX, int& outY)
{
    if (!g_harnessWindow) { outX = 0; outY = 0; return false; }
    POINT p;
    if (!GetCursorPos(&p)) { outX = 0; outY = 0; return false; }
    ScreenToClient(g_harnessWindow, &p);
    outX = p.x;
    outY = p.y;
    return true;
}

// Real definition (d3d9_hook.cpp) tracks real WM_LBUTTONDOWN/UP. Same reasoning as
// GetLastMouseMoveClientPos above -- this harness can just poll the real live key
// state directly, no message-capture plumbing needed for its own self-owned window.
extern "C" bool IsLeftMouseButtonHeld()
{
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}

// Real definition (asset_capture.cpp, proxy_d3d9/src -- 2026-08-17, runtime material/
// texture capture for THIS harness's own .menu renderer) hooks the real game's
// CreateTexture to dump material textures to disk. Genuinely inert here on purpose:
// this harness is the CONSUMER of those captured files (see menu_texture.cpp's own
// runtime-capture-folder check), never the producer -- there's no real game device
// creating real material textures inside this standalone harness process to capture
// from, so InstallEndSceneHook calling this is correctly a no-op, not a missing
// feature.
void AssetCapture_InstallHookIfEnabled(void*) {}

// Real definitions (d3d9_hook.cpp) hook into the real game's own WndProc subclass to
// capture the next real key/mouse-button press for keybind rebinding (issue #66 task
// #29, 2026-08-06). This harness has no such subclass (it owns its window outright,
// no real game WndProc to hook), so it polls GetAsyncKeyState directly instead --
// same "give a real, working harness-local answer instead of a no-op stub" pattern
// already used for IsLeftMouseButtonHeld above, not a fake/inert stand-in. Covers the
// same VK set as d3d9_hook.cpp's own VkCodeToKeyName (duplicated, not shared, same
// convention as GetControllerGlyphAssetName above -- keep in sync if that set grows).
namespace {
bool g_harnessCaptureActive = false;
constexpr int kHarnessCaptureVks[] = {
    '0','1','2','3','4','5','6','7','8','9',
    'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8, VK_F9, VK_F10, VK_F11, VK_F12,
    VK_SPACE, VK_TAB, VK_RETURN, VK_ESCAPE, VK_BACK, VK_DELETE, VK_INSERT,
    VK_HOME, VK_END, VK_PRIOR, VK_NEXT, VK_CAPITAL, VK_CONTROL, VK_SHIFT, VK_MENU,
    VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_OEM_3, VK_LBUTTON, VK_RBUTTON, VK_MBUTTON,
};
const char* HarnessVkToKeyName(int vk)
{
    if (vk >= '0' && vk <= '9') { static char s[2] = {}; s[0] = static_cast<char>(vk); return s; }
    if (vk >= 'A' && vk <= 'Z') { static char s[2] = {}; s[0] = static_cast<char>(vk); return s; }
    if (vk >= VK_F1 && vk <= VK_F12) { static char s[4]; sprintf_s(s, "F%d", vk - VK_F1 + 1); return s; }
    switch (vk) {
        case VK_SPACE: return "SPACE";
        case VK_TAB: return "TAB";
        case VK_RETURN: return "ENTER";
        case VK_ESCAPE: return "ESCAPE";
        case VK_BACK: return "BACKSPACE";
        case VK_DELETE: return "DEL";
        case VK_INSERT: return "INS";
        case VK_HOME: return "HOME";
        case VK_END: return "END";
        case VK_PRIOR: return "PGUP";
        case VK_NEXT: return "PGDN";
        case VK_CAPITAL: return "CAPSLOCK";
        case VK_CONTROL: return "CTRL";
        case VK_SHIFT: return "SHIFT";
        case VK_MENU: return "ALT";
        case VK_UP: return "UPARROW";
        case VK_DOWN: return "DOWNARROW";
        case VK_LEFT: return "LEFTARROW";
        case VK_RIGHT: return "RIGHTARROW";
        case VK_OEM_3: return "~";
        case VK_LBUTTON: return "MOUSE1";
        case VK_RBUTTON: return "MOUSE2";
        case VK_MBUTTON: return "MOUSE3";
        default: return nullptr;
    }
}
} // namespace

extern "C" void StartKeybindCapture()
{
    g_harnessCaptureActive = true;
}

extern "C" void CancelKeybindCapture()
{
    g_harnessCaptureActive = false;
}

extern "C" bool PollCapturedKeyName(char* outBuf, int outBufSize)
{
    if (!g_harnessCaptureActive) return false;
    for (int vk : kHarnessCaptureVks) {
        if (GetAsyncKeyState(vk) & 0x8000) {
            const char* name = HarnessVkToKeyName(vk);
            if (name) {
                strncpy_s(outBuf, outBufSize, name, _TRUNCATE);
                g_harnessCaptureActive = false;
                return true;
            }
        }
    }
    return false;
}

// Real definition (analog_input_hooks.cpp) reads a real per-player menu-active gate
// bit -- not read anywhere in the Options-menu draw/tick path itself (that path
// tracks its OWN g_optMenuOpen state instead).
extern "C" bool IsMenuActive_Exported()
{
    return true;
}

// Real definitions (analog_input_hooks.cpp) are per-frame housekeeping for glyph-hint
// systems Hook_EndScene drives that this harness doesn't call at all (only
// DrawCustomOptionsMenuIfOpen, via RunCustomOptionsMenuHarnessFrame).
extern "C" void __cdecl InjectSyntheticBackHintIfNeeded() {}
extern "C" void __cdecl ResetMenuListItemOrdinalForFrame() {}

// Real definition (controller_input.cpp, which lives in the HOST exe, not this DLL,
// since input polling is "platform" code that never needs hot-reloading) -- only
// used by DrawCustomCursorIfNeeded, never called by DrawCustomOptionsMenuIfOpen.
bool IsControllerActiveInputMethod()
{
    return false;
}

// Real definition (analog_input_hooks.cpp) wraps that file's own internal-linkage
// RouteStickAxes, which this harness doesn't compile at all -- mirrors the SAME real
// per-preset routing table directly (2026-08-05, Stick Layout drill-down diagram),
// same "duplicated, not shared, harness-local answer" pattern as
// GetControllerGlyphAssetName above. Keep in sync with analog_input_hooks.cpp's own
// RouteStickAxes if that routing table ever changes.
extern "C" void GetStickLayoutAxisSources(StickLayout layout, bool& moveXFromRight, bool& moveYFromRight,
                                            bool& lookXFromRight, bool& lookYFromRight)
{
    switch (layout) {
        case StickLayout::Southpaw:
            moveXFromRight = true;  moveYFromRight = true;
            lookXFromRight = false; lookYFromRight = false;
            break;
        case StickLayout::Legacy:
            moveXFromRight = true;  moveYFromRight = false;
            lookXFromRight = false; lookYFromRight = true;
            break;
        case StickLayout::LegacySouthpaw:
            moveXFromRight = false; moveYFromRight = true;
            lookXFromRight = true;  lookYFromRight = false;
            break;
        default: // Default
            moveXFromRight = false; moveYFromRight = false;
            lookXFromRight = true;  lookYFromRight = true;
            break;
    }
}

// Real definition (analog_input_hooks.cpp) has internal linkage there and is exposed
// to overlay_hud.cpp via a small extern "C" wrapper of the same name -- this harness
// mirrors that real switch table directly (2026-08-05, issue #66 follow-up: "the lack
// of actual button glyphs is" the problem this harness exists to let us iterate on
// without a full game launch, so this needs a REAL answer, not an empty stub like the
// other harmless no-ops above). Deliberately duplicated rather than shared across a
// header, same as this whole file's standing "harness-local answer" pattern -- keep
// in sync with analog_input_hooks.cpp's own GlyphAssetName if that table ever changes
// (asset names come from assets/button_glyphs/, already extracted/committed).
extern "C" const char* GetControllerGlyphAssetName(PhysicalInput input, GlyphStyle style)
{
    switch (style) {
        case GlyphStyle::Xbox360:
            switch (input) {
                case PhysicalInput::RT: return "xbox360_rt";
                case PhysicalInput::LT: return "xbox360_lt";
                case PhysicalInput::RB: return "xbox360_rb";
                case PhysicalInput::LB: return "xbox360_lb";
                case PhysicalInput::X: return "xbox360_x";
                case PhysicalInput::Y: return "xbox360_y";
                case PhysicalInput::A: return "xbox360_a";
                case PhysicalInput::B: return "xbox360_b";
                case PhysicalInput::Start: return "xbox360_start";
                case PhysicalInput::Back: return "xbox360_back";
                default: return "";
            }
        case GlyphStyle::XboxModern:
            switch (input) {
                case PhysicalInput::RT: return "xboxmodern_rt";
                case PhysicalInput::LT: return "xboxmodern_lt";
                case PhysicalInput::RB: return "xboxmodern_rb";
                case PhysicalInput::LB: return "xboxmodern_lb";
                case PhysicalInput::X: return "xboxmodern_x";
                case PhysicalInput::Y: return "xboxmodern_y";
                case PhysicalInput::A: return "xboxmodern_a";
                case PhysicalInput::B: return "xboxmodern_b";
                case PhysicalInput::Start: return "xboxmodern_menu";
                case PhysicalInput::Back: return "xboxmodern_view";
                case PhysicalInput::LS: return "xboxmodern_ls";
                case PhysicalInput::RS: return "xboxmodern_rs";
                default: return "";
            }
        case GlyphStyle::PlayStation:
            switch (input) {
                case PhysicalInput::RT: return "ps_r2";
                case PhysicalInput::LT: return "ps_l2";
                case PhysicalInput::RB: return "ps_r1";
                case PhysicalInput::LB: return "ps_l1";
                case PhysicalInput::X: return "ps_square";
                case PhysicalInput::Y: return "ps_triangle";
                case PhysicalInput::A: return "ps_cross";
                case PhysicalInput::B: return "ps_circle";
                case PhysicalInput::Start: return "ps_options";
                case PhysicalInput::Back: return "ps_create";
                case PhysicalInput::LS: return "ps_l3";
                case PhysicalInput::RS: return "ps_r3";
                default: return "";
            }
    }
    return "";
}
