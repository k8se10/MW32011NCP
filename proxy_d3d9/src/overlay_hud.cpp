// overlay_hud.cpp — see overlay_hud.h for the full rationale.
//
// REWRITTEN 2026-07-31 (same day as the first version): the original GDI-on-backbuffer
// approach (surface->GetDC, draw via TextOut, ReleaseDC) confirmed EndScene fires
// correctly, but GetDC itself failed with D3DERR_INVALIDCALL -- the classic signature
// of a multisampled (anti-aliased) backbuffer, which GDI-on-surface fundamentally
// cannot write to at all. Replaced with a real textured quad drawn through the normal
// 3D pipeline instead: normal draw calls resolve into MSAA render targets correctly
// (that's what MSAA render targets are for), unlike a raw GDI surface DC. Text is
// still rendered via GDI, but into an offscreen system-memory bitmap (never touching
// the device), then uploaded into a small D3D9 texture and drawn as an alpha-blended,
// pre-transformed (screen-space) quad -- the standard technique for D3D9 overlays that
// need to work regardless of the game's own AA/MSAA settings.
//
// Deliberately NOT including <d3d9.h> (same reasoning as d3d9_hook.cpp/dllmain.cpp
// throughout this project): every vtable slot used here is accessed via a raw index.
// These are stable, well-documented COM vtable layouts for IDirect3DDevice9/
// IDirect3DTexture9/IDirect3DSurface9, unchanged since D3D9 shipped -- hardcoding them
// is not the same risk class CLAUDE.md §5's "no hardcoded addresses" rule targets
// (that rule is about THIS GAME BINARY's own internal layout shifting between
// updates, not a fixed public COM interface Microsoft has never changed). The
// EndScene/GetBackBuffer/IUnknown::Release indices below were already empirically
// confirmed correct by the first version's live test (a wrong index would have
// crashed or returned garbage, not the exact, meaningful D3DERR_INVALIDCALL that
// came back) -- the new indices added this pass (CreateTexture, GetSurfaceLevel,
// LockRect/UnlockRect, SetTexture, SetFVF, Set/GetRenderState, SetTextureStageState,
// DrawPrimitiveUP) are the same well-documented layout but not yet live-verified.
//
// EndScene was flagged as "untried" in re_notes/known_issues.md issue #37/#47
// (Present is confirmed dead, almost certainly Steam Overlay silently taking that
// vtable slot) -- live-confirmed alive this session, unlike Present.

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <wincodec.h>
#include <shlwapi.h>
#include "../third_party/minhook/include/MinHook.h"
#include "mod_config.h"
#include "overlay_hud.h"
#include "controller_input.h"
#include "vanilla_settings_table.h"
#include "vanilla_settings_sync.h"
#include "../resource.h"

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")

extern void LogFromController(const char* msg);
extern "C" HWND GetGameWindow(); // defined in d3d9_hook.cpp
extern "C" bool GetLastMouseMoveClientPos(int& outX, int& outY); // defined in d3d9_hook.cpp
extern "C" bool IsLeftMouseButtonHeld(); // defined in d3d9_hook.cpp
extern "C" bool IsMenuActive_Exported(); // defined in analog_input_hooks.cpp -- BUG-001 follow-up
// Live-reported 2026-08-01: calling this from the WndProc/SetTimer tick (~60Hz,
// asynchronous relative to rendering) caused a visible flicker -- the menu-hint slot
// pool is consumed and reset once per RENDERED frame (EndScene), and on any frame
// where the independent 60Hz timer tick didn't happen to fire first, nothing was
// requested that frame, so it blinked off. Moved to be called from here, in
// Hook_EndScene, so it runs on the EXACT SAME per-frame cadence as the consumption
// that follows it -- same reason the native corner hints (Back/Friends, requested
// from Hook_DrawGlyphText, which always runs before this frame's own EndScene)
// never had this problem to begin with.
extern "C" void __cdecl InjectSyntheticBackHintIfNeeded(); // defined in analog_input_hooks.cpp
// Highlighted-item A-glyph investigation (2026-08-01) -- a per-frame ordinal counter
// (how many plain, no-color-span menu-item text draws have fired so far this frame)
// needs resetting once per real frame, same reasoning as every other per-frame
// accumulator in this file -- called from here so it's on the same cadence as
// everything else that resets at the top of a new frame.
extern "C" void __cdecl ResetMenuListItemOrdinalForFrame(); // defined in analog_input_hooks.cpp
// Real controller-glyph icon asset name for a raw PhysicalInput under the
// CURRENTLY configured GlyphStyle (Xbox360/XboxModern/PlayStation) -- issue #66
// follow-up (2026-08-05, live-reported: "the lack of actual button glyphs is" the
// problem with the custom Options screen). Wraps a function with internal linkage
// (analog_input_hooks.cpp's own GlyphAssetName, TU-local); this is the exported form.
extern "C" const char* GetControllerGlyphAssetName(PhysicalInput input, GlyphStyle style); // defined in analog_input_hooks.cpp
// Real per-preset stick-axis routing source (Left/Right physical stick), for the
// Stick Layout drill-down screen's controller diagram (2026-08-05 restyle) -- wraps
// RouteStickAxes, which has internal linkage (analog_input_hooks.cpp's own anonymous
// namespace).
extern "C" void GetStickLayoutAxisSources(StickLayout layout, bool& moveXFromRight, bool& moveYFromRight,
                                            bool& lookXFromRight, bool& lookYFromRight); // defined in analog_input_hooks.cpp

namespace {

// ---- Vtable indices (stable D3D9 COM layout, see file header comment) -------------
constexpr int kEndSceneVtableIndex = 42;          // IDirect3DDevice9::EndScene
constexpr int kResetVtableIndex = 16;             // IDirect3DDevice9::Reset
constexpr int kCreateTextureVtableIndex = 23;     // IDirect3DDevice9::CreateTexture
constexpr int kSetTextureVtableIndex = 65;        // IDirect3DDevice9::SetTexture
constexpr int kSetFVFVtableIndex = 89;            // IDirect3DDevice9::SetFVF
constexpr int kSetRenderStateVtableIndex = 57;    // IDirect3DDevice9::SetRenderState
constexpr int kGetRenderStateVtableIndex = 58;    // IDirect3DDevice9::GetRenderState
constexpr int kSetTextureStageStateVtableIndex = 67; // IDirect3DDevice9::SetTextureStageState
constexpr int kGetViewportVtableIndex = 48;       // IDirect3DDevice9::GetViewport
constexpr int kDrawPrimitiveUPVtableIndex = 83;   // IDirect3DDevice9::DrawPrimitiveUP
constexpr int kSetVertexShaderVtableIndex = 92;   // IDirect3DDevice9::SetVertexShader
constexpr int kGetVertexShaderVtableIndex = 93;   // IDirect3DDevice9::GetVertexShader
constexpr int kSetPixelShaderVtableIndex = 107;   // IDirect3DDevice9::SetPixelShader
constexpr int kGetPixelShaderVtableIndex = 108;   // IDirect3DDevice9::GetPixelShader
constexpr int kGetSurfaceLevelVtableIndex = 18;   // IDirect3DTexture9::GetSurfaceLevel
constexpr int kSurfaceLockRectVtableIndex = 13;   // IDirect3DSurface9::LockRect
constexpr int kSurfaceUnlockRectVtableIndex = 14; // IDirect3DSurface9::UnlockRect
constexpr int kSurfaceReleaseVtableIndex = 2;     // IUnknown::Release

// ---- D3D9 public enum/flag values (fixed COM contract, not this game's own layout) -
constexpr DWORD kD3DFMT_A8R8G8B8 = 21;
constexpr DWORD kD3DPOOL_MANAGED = 1;
constexpr DWORD kD3DFVF_XYZRHW = 0x4;
constexpr DWORD kD3DFVF_DIFFUSE = 0x40;
constexpr DWORD kD3DFVF_TEX1 = 0x100;
constexpr DWORD kFVF = kD3DFVF_XYZRHW | kD3DFVF_DIFFUSE | kD3DFVF_TEX1;
constexpr DWORD kD3DRS_ZENABLE = 7;
constexpr DWORD kD3DRS_CULLMODE = 22;
constexpr DWORD kD3DRS_LIGHTING = 137;
constexpr DWORD kD3DRS_ALPHABLENDENABLE = 27;
constexpr DWORD kD3DRS_SRCBLEND = 19;
constexpr DWORD kD3DRS_DESTBLEND = 20;
constexpr DWORD kD3DBLEND_ONE = 2;
constexpr DWORD kD3DBLEND_SRCALPHA = 5;
constexpr DWORD kD3DBLEND_INVSRCALPHA = 6;
constexpr DWORD kD3DCULL_NONE = 1;
constexpr DWORD kD3DZB_FALSE = 0;
constexpr DWORD kD3DTSS_COLOROP = 1;
constexpr DWORD kD3DTSS_COLORARG1 = 2;
constexpr DWORD kD3DTSS_COLORARG2 = 3;
constexpr DWORD kD3DTSS_ALPHAOP = 4;
constexpr DWORD kD3DTSS_ALPHAARG1 = 5;
constexpr DWORD kD3DTSS_ALPHAARG2 = 6;
constexpr DWORD kD3DTOP_DISABLE = 1;
constexpr DWORD kD3DTOP_MODULATE = 4;
constexpr DWORD kD3DTA_DIFFUSE = 0;
constexpr DWORD kD3DTA_TEXTURE = 2;
constexpr DWORD kD3DPT_TRIANGLESTRIP = 5;

typedef HRESULT(WINAPI* EndScene_t)(void* This);
typedef HRESULT(WINAPI* CreateTexture_t)(void* This, UINT Width, UINT Height, UINT Levels,
                                          DWORD Usage, DWORD Format, DWORD Pool,
                                          void** ppTexture, HANDLE* pSharedHandle);
typedef HRESULT(WINAPI* GetSurfaceLevel_t)(void* This, UINT Level, void** ppSurfaceLevel);
// Matches real D3DVIEWPORT9 layout exactly. Live-reported 2026-07-31: GetClientRect on
// the game's HWND is NOT reliable ground truth for "real screen pixels" -- this old
// engine's actual D3D9 backbuffer/render target can legitimately differ from the
// window's client area (render-resolution/supersampling settings, etc.), and our own
// quads (D3DFVF_XYZRHW, pre-transformed) draw directly into the REAL backbuffer's pixel
// space, not the window's. GetViewport reads that real space directly from the device
// itself -- the actual "edges of the screen" our own coordinate math needs to be
// proportional to, immune to any window/DPI-vs-backbuffer mismatch GetClientRect can't
// see.
struct D3DViewport9 { DWORD X, Y, Width, Height; float MinZ, MaxZ; };
typedef HRESULT(WINAPI* GetViewport_t)(void* This, D3DViewport9* pViewport);
struct LockedRect { INT Pitch; void* pBits; }; // matches real D3DLOCKED_RECT layout exactly
typedef HRESULT(WINAPI* SurfaceLockRect_t)(void* This, LockedRect* pLockedRect, const RECT* pRect, DWORD Flags);
typedef HRESULT(WINAPI* SurfaceUnlockRect_t)(void* This);
typedef ULONG(WINAPI* Release_t)(void* This);
typedef HRESULT(WINAPI* SetTexture_t)(void* This, DWORD Stage, void* pTexture);
typedef HRESULT(WINAPI* SetFVF_t)(void* This, DWORD FVF);
typedef HRESULT(WINAPI* SetRenderState_t)(void* This, DWORD State, DWORD Value);
typedef HRESULT(WINAPI* GetRenderState_t)(void* This, DWORD State, DWORD* pValue);
typedef HRESULT(WINAPI* SetTextureStageState_t)(void* This, DWORD Stage, DWORD Type, DWORD Value);
typedef HRESULT(WINAPI* DrawPrimitiveUP_t)(void* This, DWORD PrimitiveType, UINT PrimitiveCount,
                                            const void* pVertexStreamZeroData, UINT VertexStreamZeroStride);
typedef HRESULT(WINAPI* SetVertexShader_t)(void* This, void* pShader);
typedef HRESULT(WINAPI* GetVertexShader_t)(void* This, void** ppShader);
typedef HRESULT(WINAPI* SetPixelShader_t)(void* This, void* pShader);
typedef HRESULT(WINAPI* GetPixelShader_t)(void* This, void** ppShader);

struct ScreenVertex { float x, y, z, rhw; DWORD color; float u, v; };

constexpr int kTextureWidth = 512;
constexpr int kTextureHeight = 64;

EndScene_t g_origEndScene = nullptr;
DWORD g_endSceneFireCount = 0;

char g_overlayText[160] = {};
DWORD g_overlayStartMs = 0;
DWORD g_overlayDurationMs = 0;
OverlayAnimStyle g_overlayStyle = OverlayAnimStyle::Plain;
bool g_overlayActive = false;

void* g_textTexture = nullptr;        // IDirect3DTexture9*, created lazily, kept for the DLL's lifetime
char g_textureRenderedFor[160] = {};  // which message string the texture currently shows

// Set once by LoadOverlayFonts (DllMain, DLL_PROCESS_ATTACH) -- needed here too so the
// glyph-icon loader below can FindResourceA against THIS DLL's own embedded resources
// (proxy_d3d9.rc) without threading the handle through every call site separately.
HMODULE g_selfModule = nullptr;

// ---- Controller-glyph icon overlay state (issue #48, 2026-07-31) -----------------
constexpr int kMaxCachedGlyphIcons = 64; // real asset count is 47; headroom, not exact
struct GlyphIconEntry { char assetName[32]; void* texture; int width; int height; };
GlyphIconEntry g_glyphIconCache[kMaxCachedGlyphIcons] = {};
int g_glyphIconCacheCount = 0;

// A single pending request, refreshed every frame the caller (Hook_DrawGlyphText)
// still wants it shown -- NOT a queue. This project's glyph work so far only ever
// resolves one hint at a time; multi-hint support (issue #48's own open question #4)
// would need a real per-hint list instead, deliberately not built ahead of need.
char g_pendingIconAssetName[32] = {};
float g_pendingIconX = 0.0f, g_pendingIconY = 0.0f, g_pendingIconW = 0.0f, g_pendingIconH = 0.0f;
bool g_pendingIconRequestedThisFrame = false;

// TEMPORARY debug scaffolding (2026-07-31, issue #48 position-tuning round) -- draws
// a small solid-red marker at the raw (param_2, param_3) point Hook_DrawGlyphText
// receives, with NO offset/scale applied, so a live screenshot can show exactly what
// that raw point anchors relative to the real text (top-left? baseline? center?) --
// the first live icon placement was visually "way off," and guessing a second
// correction blind risks another wasted round. Remove once the real anchor
// convention is confirmed and the icon math below is corrected to match.
void* g_debugMarkerTexture = nullptr; // a single 1x1 white pixel, tinted via diffuse
constexpr int kMaxDebugMarkerSlots = 4;
float g_pendingMarkerX[kMaxDebugMarkerSlots] = {};
float g_pendingMarkerY[kMaxDebugMarkerSlots] = {};
bool g_pendingMarkerRequestedThisFrame[kMaxDebugMarkerSlots] = {};
constexpr DWORD kDebugMarkerColors[kMaxDebugMarkerSlots] = {
    0xFFFF0000, // slot 0: red
    0xFF00FF00, // slot 1: green
    0xFFFFFF00, // slot 2: yellow
    0xFF00FFFF, // slot 3: cyan
};

// ---- Custom in-game hint overlay (2026-07-31 pivot, issue #48/#49) ----------------
//
// Replaces trying to overlay a glyph icon on top of the game's OWN pixel-exact text
// rendering (issue #48's original plan -- proved fiddly to line up precisely across
// different fonts/scales) with fully replacing the hint's on-screen text: the real
// draw call is suppressed entirely (Hook_DrawGlyphText skips forwarding to the real
// trampoline for calls this feature handles) and this project draws the WHOLE hint
// itself -- prefix text, the real controller-glyph icon, suffix text -- using its own
// embedded font and its own layout math. No pixel-perfect alignment against the
// game's own font metrics needed anymore, since there's no game-drawn text left to
// align against. Only used for real in-game hints (gated by font name, see
// IsGameplayHintFont in analog_input_hooks.cpp) -- NOT applied to main-menu UI hints
// (e.g. "Friends F"), which keep rendering natively.
// ---- Gameplay hint overlay -- NAMED, independent slots (2026-08-02, BUG-004) -------
//
// Was a single shared g_pendingHint* slot for every gameplay hint (interact/pickup/
// mantle/ready-up/reload), on the assumption (documented, and true at the time) that
// only one is ever on screen at once. Live Survival co-op testing disproved that --
// see GameplayHintSlotId's own comment in overlay_hud.h for the full story. Each
// named slot keeps its own render-cache state (own textures, own measured widths)
// exactly like MenuHintSlot below, so switching which hints are simultaneously live
// doesn't thrash a shared cache.
struct GameplayHintSlot {
    char prefixText[128] = {};
    char suffixText[128] = {};
    char assetName[32] = {};
    float x = 0.0f, y = 0.0f;
    bool centerOnScreen = false;
    bool flashIcon = false;
    bool requestedThisFrame = false;
    void* prefixTexture = nullptr;
    void* suffixTexture = nullptr;
    char prefixRenderedFor[128] = {};
    char suffixRenderedFor[128] = {};
    int prefixLastFontHeight = 0;
    int suffixLastFontHeight = 0;
    int prefixMeasuredWidth = 0;
    int suffixMeasuredWidth = 0;
    // Optional separate line drawn above the main row (2026-08-02) -- see
    // RequestCustomHintOverlay's own topLineText comment (overlay_hud.h) for why this
    // exists (a real native hint turned out to be two logical lines in one draw call,
    // joined by an embedded '\n'). Empty string = no top line, nothing extra drawn.
    char topLineText[128] = {};
    void* topLineTexture = nullptr;
    char topLineRenderedFor[128] = {};
    int topLineLastFontHeight = 0;
    int topLineMeasuredWidth = 0;
};
GameplayHintSlot g_gameplayHintSlots[kGameplayHintSlotCount];

// ---- Menu-hint overlay -- MULTI-SLOT, unlike the single gameplay slot above --------
//
// Live-reported 2026-08-01: "Friends doesn't show on some screens" / "Friends stays
// on screen when it should say Back." Root cause: unlike a gameplay interact hint
// (confirmed, all session, to only ever have ONE on screen at a time), MW3's menu UI
// shows a persistent LEGEND BAR -- multiple hints (e.g. "Back" and "Friends") drawn
// SIMULTANEOUSLY, every frame, as separate Hook_DrawGlyphText calls. The gameplay
// hint's single g_pendingHint* slot above can only hold one request per frame --
// whichever menu hint's draw call happened to run last in a given frame silently won
// that one slot, and the other's native text was suppressed with nothing drawn in
// its place. This is a SEPARATE small pool of slots (own textures, own state) so
// menu hints don't fight over one slot; the gameplay path above is untouched.
// Bumped 4->6 (2026-08-03, issue #51): Survival's own map-select screen shows
// FOUR corner hints simultaneously (Leaderboards/Game Summary/Friends/Back) --
// confirmed live these alone fully exhaust the old limit of 4, silently
// dropping the manual-position list-highlight glyph's own RequestMenuHintOverlay
// call for that same frame (it simply arrived after the pool was already full).
// The glyph's OWN position was confirmed correct via diagnostic logging before
// this was found -- this was a slot-starvation bug, not a positioning bug.
// +2 headroom over the observed max (4) rather than the exact minimum, same
// margin philosophy as the original comment.
constexpr int kMaxMenuHintSlots = 6;
struct MenuHintSlot {
    char prefixText[128];
    char suffixText[128];
    char assetName[32];
    float x, y;
    void* prefixTexture;
    void* suffixTexture;
    char prefixRenderedFor[128];
    char suffixRenderedFor[128];
    int prefixLastFontHeight;
    int suffixLastFontHeight;
    int prefixMeasuredWidth;
    int suffixMeasuredWidth;
};
MenuHintSlot g_menuHintSlots[kMaxMenuHintSlots] = {};
int g_menuHintSlotCountThisFrame = 0; // how many of the slots above are live for the CURRENT frame

// In-game hint text renders noticeably larger than the toast notification's own 20px
// (live-reported 2026-07-31: default 20px read as too small next to a 34px icon,
// "text scaling is ass") -- its own constant, independent of kTextureHeight/the
// toast's own font size, so tuning one never affects the other.
constexpr int kHintFontHeightPx = 30;
constexpr float kHintIconSize = 42.0f; // sized to match kHintFontHeightPx, not the old flat 34px

// Extra pixels added on top of the measured text width before cropping a rendered
// segment's texture (see DrawOneGameplayHintSlot) -- RenderMaskLuminance always
// draws with an 8px left margin (see its own DrawTextA rect below) that
// GetTextExtentPoint32A's measurement doesn't include, and italic text's slant can
// extend a few pixels past its own nominal advance width. Without this, live testing
// 2026-07-31 showed the last character of a segment (e.g. the "s" in "Press")
// visibly clipped off. Generous rather than exact -- a little extra transparent
// canvas showing costs nothing, a clipped glyph is very visible.
constexpr int kHintTextWidthMarginPx = 20;

// Measures how wide `text` actually renders at the given font size -- needed so the
// prefix/icon/suffix pieces can be placed sequentially with no gap or overlap, since
// each is rendered into a fixed-size (kTextureWidth x kTextureHeight) canvas that's
// almost always wider than the actual text.
int MeasureTextWidthPx(const char* text, bool italic, int fontHeightPx)
{
    HDC screenDC = GetDC(nullptr);
    HFONT font = CreateFontA(fontHeightPx, 0, 0, 0, FW_DONTCARE, italic ? TRUE : FALSE, FALSE, FALSE,
                              ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, DEFAULT_PITCH, "Barlow Condensed SemiBold");
    HFONT oldFont = static_cast<HFONT>(SelectObject(screenDC, font));
    SIZE sz = {};
    GetTextExtentPoint32A(screenDC, text, static_cast<int>(strlen(text)), &sz);
    SelectObject(screenDC, oldFont);
    DeleteObject(font);
    ReleaseDC(nullptr, screenDC);
    return sz.cx;
}

// Renders `text` into a fresh kTextureWidth x kTextureHeight top-down 32bpp memory
// bitmap (a plain GDI DC/DIB, never touches D3D9), drawing it once at each (dx,dy) in
// `offsets` in white on a black background, then extracts each pixel's own luminance
// (R=G=B, since ANTIALIASED_QUALITY -- grayscale AA -- avoids ClearType color
// fringing) into `outLuminance`. Used twice by RenderTextToArgbBuffer below: once
// with a 3x3 grid of +/-1px offsets to build the combined outline+fill coverage mask,
// and once with a single (0,0) offset to build the fill-only mask -- see that
// function's own comment for how the two masks combine into a real black-outlined,
// white-filled result.
bool RenderMaskLuminance(const char* text, const POINT* offsets, int offsetCount, bool italic, BYTE* outLuminance,
                          UINT alignFlag = DT_RIGHT, int fontHeightPx = 20)
{
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = kTextureWidth;
    bmi.bmiHeader.biHeight = -kTextureHeight; // negative = top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);
    ReleaseDC(nullptr, screenDC);
    if (!memDC) return false;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        DeleteDC(memDC);
        return false;
    }

    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(memDC, dib));
    RECT full = { 0, 0, kTextureWidth, kTextureHeight };
    FillRect(memDC, &full, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    // Bundled, self-contained font (2026-07-31 follow-up) -- LoadOverlayFonts (DllMain)
    // already registered the real Barlow Condensed SemiBold .ttf/.ttf-italic embedded
    // in this DLL as a PRIVATE, in-process-only font via AddFontMemResourceEx, so this
    // no longer depends on the font being installed system-wide. Family name is
    // "Barlow Condensed SemiBold", NOT "Barlow Condensed" -- confirmed directly against
    // the actual downloaded font files (GDI+ PrivateFontCollection enumeration): Google
    // Fonts ships each static weight as its own family for legacy GDI/GDI+ compatibility
    // (only Regular/Italic exist within it, which is exactly what nItalic selects
    // between). FW_DONTCARE since the weight is already baked into which family this
    // is, not requested at lookup time. If LoadOverlayFonts ever failed (logged at
    // startup), GDI falls back to a default system font here exactly as before this
    // change -- same graceful degradation, just no longer the expected path.
    HFONT font = CreateFontA(fontHeightPx, 0, 0, 0, FW_DONTCARE, italic ? TRUE : FALSE, FALSE, FALSE,
                              ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, DEFAULT_PITCH, "Barlow Condensed SemiBold");
    HFONT oldFont = static_cast<HFONT>(SelectObject(memDC, font));
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(255, 255, 255));

    for (int i = 0; i < offsetCount; ++i) {
        RECT textRect = { 8 + offsets[i].x, offsets[i].y,
                           kTextureWidth - 8 + offsets[i].x, kTextureHeight + offsets[i].y };
        DrawTextA(memDC, text, -1, &textRect, alignFlag | DT_SINGLELINE | DT_NOCLIP | DT_VCENTER);
    }

    SelectObject(memDC, oldFont);
    DeleteObject(font);

    const DWORD* src = static_cast<const DWORD*>(bits);
    for (int i = 0; i < kTextureWidth * kTextureHeight; ++i) {
        outLuminance[i] = static_cast<BYTE>(src[i] & 0xFF); // R=G=B already (grayscale AA on black)
    }

    SelectObject(memDC, oldBmp);
    DeleteObject(dib);
    DeleteDC(memDC);
    return true;
}

// Composites a real black-outlined, white-filled result from two GDI luminance
// passes. The naive single-pass "alpha = luminance" trick this project used before
// only works for plain white-on-transparent text -- a black outline pixel would have
// luminance 0, indistinguishable from "background", making it invisible. Instead:
// render an OUTLINE mask (text drawn 9 times across a 3x3 grid of +/-1px offsets,
// covering the true glyph shape plus a 1px ring around it) and a separate FILL mask
// (text drawn once, centered, no offset). Final alpha = outline mask (the union of
// fill + outline, so both are visible); final color = white scaled by the FILL
// mask's own value (0 in outline-only regions = solid black, ramping to full white
// deep inside the glyph) -- this naturally anti-aliases the black-to-white
// transition at the fill's real edge using the fill mask's own coverage value,
// with no extra blending step needed.
bool RenderTextToArgbBuffer(const char* text, DWORD* outPixels, UINT alignFlag = DT_RIGHT, int fontHeightPx = 20)
{
    static BYTE outlineMask[kTextureWidth * kTextureHeight];
    static BYTE fillMask[kTextureWidth * kTextureHeight];

    const bool italic = g_modConfig.overlayFontItalic;

    const POINT kOutlineOffsets[9] = {
        { -1, -1 }, { 0, -1 }, { 1, -1 },
        { -1, 0 },  { 0, 0 },  { 1, 0 },
        { -1, 1 },  { 0, 1 },  { 1, 1 },
    };
    if (!RenderMaskLuminance(text, kOutlineOffsets, 9, italic, outlineMask, alignFlag, fontHeightPx)) return false;

    const POINT kFillOffset[1] = { { 0, 0 } };
    if (!RenderMaskLuminance(text, kFillOffset, 1, italic, fillMask, alignFlag, fontHeightPx)) return false;

    for (int i = 0; i < kTextureWidth * kTextureHeight; ++i) {
        DWORD alpha = outlineMask[i];
        DWORD whiteness = fillMask[i];
        outPixels[i] = (alpha << 24) | (whiteness << 16) | (whiteness << 8) | whiteness;
    }
    return true;
}

// Creates g_textTexture on first use, and re-renders it whenever the message text
// actually changes (not every frame -- GDI + Lock/Unlock is real work, only worth
// doing when there's something new to show).
void EnsureTextTexture(void* device)
{
    if (g_textTexture && strcmp(g_textureRenderedFor, g_overlayText) == 0) return;

    if (!g_textTexture) {
        void** deviceVtbl = *reinterpret_cast<void***>(device);
        auto createTexture = reinterpret_cast<CreateTexture_t>(deviceVtbl[kCreateTextureVtableIndex]);
        HRESULT hr = createTexture(device, kTextureWidth, kTextureHeight, 1, 0,
                                    kD3DFMT_A8R8G8B8, kD3DPOOL_MANAGED, &g_textTexture, nullptr);
        if (FAILED(hr) || !g_textTexture) {
            static bool loggedOnce = false;
            if (!loggedOnce) {
                loggedOnce = true;
                char buf[128];
                sprintf_s(buf, "[overlay-hud] CreateTexture failed: hr=0x%08lX", hr);
                LogFromController(buf);
            }
            g_textTexture = nullptr;
            return;
        }
    }

    void** texVtbl = *reinterpret_cast<void***>(g_textTexture);
    auto getSurfaceLevel = reinterpret_cast<GetSurfaceLevel_t>(texVtbl[kGetSurfaceLevelVtableIndex]);
    void* surface = nullptr;
    if (FAILED(getSurfaceLevel(g_textTexture, 0, &surface)) || !surface) {
        static bool loggedOnce = false;
        if (!loggedOnce) {
            loggedOnce = true;
            LogFromController("[overlay-hud] Texture GetSurfaceLevel failed");
        }
        return;
    }

    void** surfaceVtbl = *reinterpret_cast<void***>(surface);
    auto lockRect = reinterpret_cast<SurfaceLockRect_t>(surfaceVtbl[kSurfaceLockRectVtableIndex]);
    auto unlockRect = reinterpret_cast<SurfaceUnlockRect_t>(surfaceVtbl[kSurfaceUnlockRectVtableIndex]);
    auto releaseSurface = reinterpret_cast<Release_t>(surfaceVtbl[kSurfaceReleaseVtableIndex]);

    LockedRect locked = {};
    if (SUCCEEDED(lockRect(surface, &locked, nullptr, 0)) && locked.pBits) {
        static DWORD pixels[kTextureWidth * kTextureHeight];
        if (RenderTextToArgbBuffer(g_overlayText, pixels)) {
            for (int y = 0; y < kTextureHeight; ++y) {
                memcpy(static_cast<BYTE*>(locked.pBits) + y * locked.Pitch,
                       pixels + y * kTextureWidth, kTextureWidth * sizeof(DWORD));
            }
            strncpy_s(g_textureRenderedFor, g_overlayText, _TRUNCATE);
        }
        unlockRect(surface);
    } else {
        static bool loggedOnce = false;
        if (!loggedOnce) {
            loggedOnce = true;
            LogFromController("[overlay-hud] Texture surface LockRect failed");
        }
    }
    releaseSurface(surface);
}

// Generic version of EnsureTextTexture above, parameterized by which texture/cache-
// string to (re)use instead of always the toast's own g_textTexture/g_overlayText --
// used for the custom in-game hint overlay's prefix/suffix text segments, which need
// two independent textures shown side-by-side around a glyph icon. Always renders
// LEFT-aligned (DT_LEFT) since these segments are composed left-to-right, unlike the
// toast's own right-anchored text. empty string is valid input (renders nothing,
// still creates/keeps the texture) so a hint with no prefix or no suffix text doesn't
// need special-casing by the caller.
bool EnsureLeftAlignedTextTexture(void* device, void*& texture, char* renderedForBuf, size_t renderedForBufSize,
                                   const char* text, int& lastFontHeightPx, int fontHeightPx)
{
    // Live-reported 2026-07-31: resolution scaling means fontHeightPx can change
    // between calls with the SAME text (e.g. the user resizes the window) -- the
    // cache must invalidate on a scale change too, not just a text change, or a
    // stale texture rendered at the old resolution's font size would stick around.
    if (texture && lastFontHeightPx == fontHeightPx && strncmp(renderedForBuf, text, renderedForBufSize - 1) == 0) return true;

    if (!texture) {
        void** deviceVtbl = *reinterpret_cast<void***>(device);
        auto createTexture = reinterpret_cast<CreateTexture_t>(deviceVtbl[kCreateTextureVtableIndex]);
        HRESULT hr = createTexture(device, kTextureWidth, kTextureHeight, 1, 0,
                                    kD3DFMT_A8R8G8B8, kD3DPOOL_MANAGED, &texture, nullptr);
        if (FAILED(hr) || !texture) {
            texture = nullptr;
            return false;
        }
    }

    void** texVtbl = *reinterpret_cast<void***>(texture);
    auto getSurfaceLevel = reinterpret_cast<GetSurfaceLevel_t>(texVtbl[kGetSurfaceLevelVtableIndex]);
    void* surface = nullptr;
    if (FAILED(getSurfaceLevel(texture, 0, &surface)) || !surface) return false;

    void** surfaceVtbl = *reinterpret_cast<void***>(surface);
    auto lockRect = reinterpret_cast<SurfaceLockRect_t>(surfaceVtbl[kSurfaceLockRectVtableIndex]);
    auto unlockRect = reinterpret_cast<SurfaceUnlockRect_t>(surfaceVtbl[kSurfaceUnlockRectVtableIndex]);
    auto releaseSurface = reinterpret_cast<Release_t>(surfaceVtbl[kSurfaceReleaseVtableIndex]);

    bool ok = false;
    LockedRect locked = {};
    if (SUCCEEDED(lockRect(surface, &locked, nullptr, 0)) && locked.pBits) {
        static DWORD pixels[kTextureWidth * kTextureHeight];
        if (RenderTextToArgbBuffer(text, pixels, DT_LEFT, fontHeightPx)) {
            for (int y = 0; y < kTextureHeight; ++y) {
                memcpy(static_cast<BYTE*>(locked.pBits) + y * locked.Pitch,
                       pixels + y * kTextureWidth, kTextureWidth * sizeof(DWORD));
            }
            strncpy_s(renderedForBuf, renderedForBufSize, text, _TRUNCATE);
            lastFontHeightPx = fontHeightPx;
            ok = true;
        }
        unlockRect(surface);
    }
    releaseSurface(surface);
    return ok;
}

// Vertex diffuse color(s) for the base quad, per animation style -- these modulate
// against the (white-alpha) text texture via D3DTOP_MODULATE, so setting a colored
// diffuse tints the whole glyph shape directly, no texture changes needed. Returns a
// separate top/bottom color so D3D9 can interpolate a vertical gradient across the
// quad -- a single flat color (the original Gold implementation) modulated onto
// already-white text just reproduces that exact color with zero shading, reading as
// plain flat yellow rather than anything metallic (live-reported 2026-07-31: "just
// looked yellow"). A real gold/metal look needs a light-to-dark gradient; Rainbow and
// Sweep don't need one (a flat color / no base-color change respectively), so they
// return the same value for both. WaW-style homages (re_notes/known_issues.md issue
// #37's real documented hidden dev clan-tag codes): Gold = "GOLD"; Rainbow = "RAIN"
// (smooth hue cycle). Sweep's own flourish is a second, separately-drawn additive
// pass in DrawTexturedQuad, not a base-color change.
void ComputeQuadColors(DWORD elapsedMs, DWORD& outTopColor, DWORD& outBottomColor)
{
    switch (g_overlayStyle) {
        case OverlayAnimStyle::Gold:
            outTopColor = 0xFFFFEDA8;    // light warm-gold highlight
            outBottomColor = 0xFFC8860A; // richer amber/bronze shadow
            return;
        case OverlayAnimStyle::Rainbow: {
            float hue = fmodf(static_cast<float>(elapsedMs) / 2400.0f, 1.0f) * 6.0f; // ~2.4s per cycle
            int seg = static_cast<int>(hue);
            float frac = hue - static_cast<float>(seg);
            BYTE r, g, b;
            switch (seg) {
                case 0:  r = 255; g = static_cast<BYTE>(frac * 255.0f); b = 0; break;
                case 1:  r = static_cast<BYTE>((1.0f - frac) * 255.0f); g = 255; b = 0; break;
                case 2:  r = 0; g = 255; b = static_cast<BYTE>(frac * 255.0f); break;
                case 3:  r = 0; g = static_cast<BYTE>((1.0f - frac) * 255.0f); b = 255; break;
                case 4:  r = static_cast<BYTE>(frac * 255.0f); g = 0; b = 255; break;
                default: r = 255; g = 0; b = static_cast<BYTE>((1.0f - frac) * 255.0f); break;
            }
            outTopColor = outBottomColor = 0xFF000000 | (static_cast<DWORD>(r) << 16) | (static_cast<DWORD>(g) << 8) | b;
            return;
        }
        case OverlayAnimStyle::Sweep:
        case OverlayAnimStyle::Plain:
        default:
            outTopColor = outBottomColor = 0xFFFFFFFF;
            return;
    }
}

// Draws g_textTexture as an alpha-blended, pre-transformed (screen-space) quad,
// top-right anchored. Unlike GDI-on-backbuffer, a normal textured draw call resolves
// into a multisampled render target correctly -- this is the whole reason this
// version works where the GetDC-based one didn't. Saves/restores every render state
// touched (defensive, matches this project's "hook callbacks must be safe" standard)
// even though EndScene runs after the game's own draw calls for the frame are
// already done, so state pollution risk here is low but not worth leaving to chance.
void DrawTexturedQuad(void* device, DWORD elapsedMs)
{
    void** deviceVtbl = *reinterpret_cast<void***>(device);
    auto setTexture = reinterpret_cast<SetTexture_t>(deviceVtbl[kSetTextureVtableIndex]);
    auto setFVF = reinterpret_cast<SetFVF_t>(deviceVtbl[kSetFVFVtableIndex]);
    auto setRenderState = reinterpret_cast<SetRenderState_t>(deviceVtbl[kSetRenderStateVtableIndex]);
    auto getRenderState = reinterpret_cast<GetRenderState_t>(deviceVtbl[kGetRenderStateVtableIndex]);
    auto setTss = reinterpret_cast<SetTextureStageState_t>(deviceVtbl[kSetTextureStageStateVtableIndex]);
    auto drawPrimitiveUP = reinterpret_cast<DrawPrimitiveUP_t>(deviceVtbl[kDrawPrimitiveUPVtableIndex]);
    auto setVertexShader = reinterpret_cast<SetVertexShader_t>(deviceVtbl[kSetVertexShaderVtableIndex]);
    auto getVertexShader = reinterpret_cast<GetVertexShader_t>(deviceVtbl[kGetVertexShaderVtableIndex]);
    auto setPixelShader = reinterpret_cast<SetPixelShader_t>(deviceVtbl[kSetPixelShaderVtableIndex]);
    auto getPixelShader = reinterpret_cast<GetPixelShader_t>(deviceVtbl[kGetPixelShaderVtableIndex]);

    DWORD oldZEnable = 0, oldLighting = 0, oldAlphaBlend = 0, oldSrcBlend = 0, oldDestBlend = 0, oldCull = 0;
    getRenderState(device, kD3DRS_ZENABLE, &oldZEnable);
    getRenderState(device, kD3DRS_LIGHTING, &oldLighting);
    getRenderState(device, kD3DRS_ALPHABLENDENABLE, &oldAlphaBlend);
    getRenderState(device, kD3DRS_SRCBLEND, &oldSrcBlend);
    getRenderState(device, kD3DRS_DESTBLEND, &oldDestBlend);
    getRenderState(device, kD3DRS_CULLMODE, &oldCull);

    // CRITICAL fix (2026-07-31 live report): the intro logo/waveform bumper video
    // corrupted visibly where our quad drew, while normal menu/gameplay was fine.
    // Root cause: DrawPrimitiveUP with an FVF only uses the FIXED-FUNCTION pipeline
    // if NO vertex/pixel shader is currently bound on the device -- it does NOT
    // override an already-bound programmable shader. Ordinary gameplay/menu HUD
    // compositing happens to leave no shader bound by the time EndScene fires, so
    // this went unnoticed there; the intro bumper almost certainly uses a real
    // shader for its VFX, and our fixed-function vertex data was being fed through
    // THAT leftover shader, producing exactly this kind of warped/duplicated
    // corruption. Explicitly null both shaders before drawing, restore the game's
    // real ones after -- Get*Shader AddRefs its out-param, so the saved pointer
    // must be Released after being restored via Set*Shader (which re-AddRefs it
    // for the device's own internal reference) to avoid leaking a reference every
    // single frame the overlay is visible.
    void* oldVertexShader = nullptr;
    void* oldPixelShader = nullptr;
    getVertexShader(device, &oldVertexShader);
    getPixelShader(device, &oldPixelShader);
    setVertexShader(device, nullptr);
    setPixelShader(device, nullptr);

    setRenderState(device, kD3DRS_ZENABLE, kD3DZB_FALSE);
    setRenderState(device, kD3DRS_LIGHTING, FALSE);
    setRenderState(device, kD3DRS_ALPHABLENDENABLE, TRUE);
    setRenderState(device, kD3DRS_SRCBLEND, kD3DBLEND_SRCALPHA);
    setRenderState(device, kD3DRS_DESTBLEND, kD3DBLEND_INVSRCALPHA);
    setRenderState(device, kD3DRS_CULLMODE, kD3DCULL_NONE);

    setTss(device, 0, kD3DTSS_COLOROP, kD3DTOP_MODULATE);
    setTss(device, 0, kD3DTSS_COLORARG1, kD3DTA_TEXTURE);
    setTss(device, 0, kD3DTSS_COLORARG2, kD3DTA_DIFFUSE);
    setTss(device, 0, kD3DTSS_ALPHAOP, kD3DTOP_MODULATE);
    setTss(device, 0, kD3DTSS_ALPHAARG1, kD3DTA_TEXTURE);
    setTss(device, 0, kD3DTSS_ALPHAARG2, kD3DTA_DIFFUSE);
    setTss(device, 1, kD3DTSS_COLOROP, kD3DTOP_DISABLE);

    setTexture(device, 0, g_textTexture);
    setFVF(device, kFVF);

    int width = 1920;
    HWND hwnd = GetGameWindow();
    RECT clientRect;
    if (hwnd && GetClientRect(hwnd, &clientRect)) {
        width = clientRect.right - clientRect.left;
    }

    float right = static_cast<float>(width) - 12.0f;
    // Live-reported 2026-07-31 (QoL): Steam's own overlay notification indicator
    // (friend messages, achievements, etc.) occupies the very top-right corner and was
    // rendering UNDER this toast at the original 12px offset. Pushed down to clear it.
    float top = 48.0f;
    float texW = static_cast<float>(kTextureWidth);
    float texH = static_cast<float>(kTextureHeight);
    DWORD topColor, bottomColor;
    ComputeQuadColors(elapsedMs, topColor, bottomColor);

    // Standard D3D9 pretransformed-vertex half-pixel offset correction. Top/bottom
    // vertices get their own color so D3D9 interpolates a real vertical gradient
    // across the quad (Gold's light-to-dark metallic look) -- styles with no
    // gradient just pass the same value for both.
    ScreenVertex verts[4] = {
        { right - texW - 0.5f, top - 0.5f,        0.0f, 1.0f, topColor,    0.0f, 0.0f },
        { right - 0.5f,        top - 0.5f,        0.0f, 1.0f, topColor,    1.0f, 0.0f },
        { right - texW - 0.5f, top + texH - 0.5f, 0.0f, 1.0f, bottomColor, 0.0f, 1.0f },
        { right - 0.5f,        top + texH - 0.5f, 0.0f, 1.0f, bottomColor, 1.0f, 1.0f },
    };

    HRESULT hrDraw = drawPrimitiveUP(device, kD3DPT_TRIANGLESTRIP, 2, verts, sizeof(ScreenVertex));

    // WaW "CYLN" homage: a silvery highlight sweeps left-to-right through the tag,
    // repeating for the notification's whole duration. Reuses the SAME text texture
    // (no separate asset) with a narrow, scrolling U sub-range.
    //
    // FIXED (2026-07-31 live report: "looks off, usually a silver sweep across the
    // text" -- it was rendering as a hard-edged rectangular bar, not a glint masked
    // to the letters). Root cause: SRCBLEND=ONE ignores the texture's alpha channel
    // entirely, so the glint painted the full strip at full intensity everywhere,
    // including the "transparent" background between/around letters (additive
    // blending with SRCBLEND=ONE doesn't care what's already there -- alpha never
    // entered the equation at all). Fixed by using SRCBLEND=SRCALPHA instead: the
    // texture stage state below already outputs (textureAlpha * diffuseAlpha) as
    // this pass's alpha, so with an opaque (alpha=255) glint vertex color, the
    // source contribution is scaled by the TEXTURE's own real per-pixel alpha --
    // strong exactly where a letter's opaque interior is, fading out at anti-aliased
    // edges, and genuinely zero over background. DESTBLEND stays ONE (additive, only
    // ever brightens, never darkens). Color changed from pure white to a cooler,
    // slightly dimmer silver tone -- pure white additive light reads as a blown-out
    // flash, whereas a moderate cool-grey reads as an actual silver sheen once it's
    // additively brightening already-white text.
    if (g_overlayStyle == OverlayAnimStyle::Sweep) {
        constexpr float kSweepPeriodMs = 1400.0f;
        constexpr float kSweepWidthU = 0.12f;
        float t = fmodf(static_cast<float>(elapsedMs), kSweepPeriodMs) / kSweepPeriodMs;
        float uStart = t * (1.0f + kSweepWidthU) - kSweepWidthU; // slides just-off-left to just-off-right
        float uEnd = uStart + kSweepWidthU;
        float uStartClamped = uStart < 0.0f ? 0.0f : uStart;
        float uEndClamped = uEnd > 1.0f ? 1.0f : uEnd;
        if (uEndClamped > uStartClamped) {
            setRenderState(device, kD3DRS_SRCBLEND, kD3DBLEND_SRCALPHA);
            setRenderState(device, kD3DRS_DESTBLEND, kD3DBLEND_ONE);

            float xStart = right - texW + uStartClamped * texW;
            float xEnd = right - texW + uEndClamped * texW;
            const DWORD glint = 0xFFC8C8E6; // silver: alpha=200, cool light grey (200,200,230)
            ScreenVertex sweepVerts[4] = {
                { xStart - 0.5f, top - 0.5f,        0.0f, 1.0f, glint, uStartClamped, 0.0f },
                { xEnd - 0.5f,   top - 0.5f,        0.0f, 1.0f, glint, uEndClamped,   0.0f },
                { xStart - 0.5f, top + texH - 0.5f, 0.0f, 1.0f, glint, uStartClamped, 1.0f },
                { xEnd - 0.5f,   top + texH - 0.5f, 0.0f, 1.0f, glint, uEndClamped,   1.0f },
            };
            drawPrimitiveUP(device, kD3DPT_TRIANGLESTRIP, 2, sweepVerts, sizeof(ScreenVertex));

            setRenderState(device, kD3DRS_SRCBLEND, kD3DBLEND_SRCALPHA);
            setRenderState(device, kD3DRS_DESTBLEND, kD3DBLEND_INVSRCALPHA);
        }
    }

    setTexture(device, 0, nullptr);
    setRenderState(device, kD3DRS_ZENABLE, oldZEnable);
    setRenderState(device, kD3DRS_LIGHTING, oldLighting);
    setRenderState(device, kD3DRS_ALPHABLENDENABLE, oldAlphaBlend);
    setRenderState(device, kD3DRS_SRCBLEND, oldSrcBlend);
    setRenderState(device, kD3DRS_DESTBLEND, oldDestBlend);
    setRenderState(device, kD3DRS_CULLMODE, oldCull);

    // Restore the game's real shaders (Set*Shader re-AddRefs for the device's own
    // internal reference), then release OUR extra reference from the Get*Shader
    // calls above -- any COM interface's Release lives at vtable index 2
    // (IUnknown's own layout, universal across every interface), so the surface
    // Release_t typedef is reused here rather than duplicating it for shaders.
    setVertexShader(device, oldVertexShader);
    setPixelShader(device, oldPixelShader);
    if (oldVertexShader) {
        void** vtbl = *reinterpret_cast<void***>(oldVertexShader);
        reinterpret_cast<Release_t>(vtbl[kSurfaceReleaseVtableIndex])(oldVertexShader);
    }
    if (oldPixelShader) {
        void** vtbl = *reinterpret_cast<void***>(oldPixelShader);
        reinterpret_cast<Release_t>(vtbl[kSurfaceReleaseVtableIndex])(oldPixelShader);
    }

    static bool loggedDrawResult = false;
    if (!loggedDrawResult) {
        loggedDrawResult = true;
        char buf[128];
        sprintf_s(buf, "[overlay-hud] DrawPrimitiveUP hr=0x%08lX (textured quad path)", hrDraw);
        LogFromController(buf);
    }
}

void DrawOverlayMessage(void* device)
{
    if (!g_overlayActive) return;
    DWORD elapsed = GetTickCount() - g_overlayStartMs;
    if (elapsed >= g_overlayDurationMs) {
        g_overlayActive = false;
        return;
    }

    EnsureTextTexture(device);
    if (!g_textTexture) return;
    DrawTexturedQuad(device, elapsed);
}

// Decodes a PNG already sitting in memory (an embedded RCDATA resource -- see
// proxy_d3d9.rc) into a top-down 32bpp BGRA buffer via WIC, matching D3DFMT_A8R8G8B8's
// real in-memory byte order exactly (GUID_WICPixelFormat32bppBGRA -- B,G,R,A per pixel
// in a little-endian DWORD -- needs no channel-swizzling before upload). WIC is a
// built-in Windows component (wincodec.h/windowscodecs.lib) -- no new third-party
// dependency, consistent with this project's "MinHook is the only vendored library"
// stance. CoInitializeEx is safe to call even if some other component already
// initialized COM on this thread (idempotent per MSDN, returns S_FALSE not an error).
bool DecodePngFromMemory(const void* data, DWORD size, DWORD*& outPixels, UINT& outWidth, UINT& outHeight)
{
    outPixels = nullptr;
    outWidth = outHeight = 0;
    HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool needCoUninit = SUCCEEDED(coHr) && coHr != S_FALSE;

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory));
    IStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    bool ok = false;

    if (SUCCEEDED(hr)) {
        stream = SHCreateMemStream(static_cast<const BYTE*>(data), size);
        if (stream) {
            hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
        } else {
            hr = E_OUTOFMEMORY;
        }
    }
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                     nullptr, 0.0, WICBitmapPaletteTypeCustom);
    }
    UINT w = 0, h = 0;
    if (SUCCEEDED(hr)) hr = converter->GetSize(&w, &h);
    if (SUCCEEDED(hr) && w > 0 && h > 0) {
        DWORD* pixels = new DWORD[static_cast<size_t>(w) * h];
        hr = converter->CopyPixels(nullptr, w * 4, w * h * 4, reinterpret_cast<BYTE*>(pixels));
        if (SUCCEEDED(hr)) {
            outPixels = pixels;
            outWidth = w;
            outHeight = h;
            ok = true;
        } else {
            delete[] pixels;
        }
    }

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    if (needCoUninit) CoUninitialize();
    return ok;
}

// Loads one glyph icon (by its real assets/button_glyphs/ name, no extension -- e.g.
// "xbox360_x") from this DLL's own embedded resources into a real D3D9 texture, via
// the same CreateTexture/GetSurfaceLevel/LockRect upload pattern EnsureTextTexture
// above already uses for the toast-notification text texture. Returns false (leaving
// outTexture untouched) if the resource doesn't exist or WIC decoding fails -- never
// crashes or substitutes a placeholder; the caller (RequestGlyphIconOverlay's cache
// lookup) just won't draw anything for an asset name that didn't load.
bool LoadGlyphIconTexture(void* device, const char* assetName, void*& outTexture, int& outWidth, int& outHeight)
{
    if (!g_selfModule) return false;
    HRSRC res = FindResourceA(g_selfModule, assetName, RT_RCDATA);
    if (!res) {
        char buf[128];
        sprintf_s(buf, "[overlay-glyph-icon] FindResourceA failed for \"%.31s\" -- GetLastError=%lu",
            assetName, GetLastError());
        LogFromController(buf);
        return false;
    }
    HGLOBAL loaded = LoadResource(g_selfModule, res);
    void* data = loaded ? LockResource(loaded) : nullptr;
    DWORD size = SizeofResource(g_selfModule, res);
    if (!data || size == 0) return false;

    DWORD* pixels = nullptr;
    UINT w = 0, h = 0;
    if (!DecodePngFromMemory(data, size, pixels, w, h)) {
        char buf[128];
        sprintf_s(buf, "[overlay-glyph-icon] WIC decode failed for \"%.31s\"", assetName);
        LogFromController(buf);
        return false;
    }

    void** deviceVtbl = *reinterpret_cast<void***>(device);
    auto createTexture = reinterpret_cast<CreateTexture_t>(deviceVtbl[kCreateTextureVtableIndex]);
    void* texture = nullptr;
    HRESULT hr = createTexture(device, w, h, 1, 0, kD3DFMT_A8R8G8B8, kD3DPOOL_MANAGED, &texture, nullptr);
    if (FAILED(hr) || !texture) {
        char buf[128];
        sprintf_s(buf, "[overlay-glyph-icon] CreateTexture failed for \"%.31s\": hr=0x%08lX", assetName, hr);
        LogFromController(buf);
        delete[] pixels;
        return false;
    }

    void** texVtbl = *reinterpret_cast<void***>(texture);
    auto getSurfaceLevel = reinterpret_cast<GetSurfaceLevel_t>(texVtbl[kGetSurfaceLevelVtableIndex]);
    void* surface = nullptr;
    bool uploaded = false;
    if (SUCCEEDED(getSurfaceLevel(texture, 0, &surface)) && surface) {
        void** surfaceVtbl = *reinterpret_cast<void***>(surface);
        auto lockRect = reinterpret_cast<SurfaceLockRect_t>(surfaceVtbl[kSurfaceLockRectVtableIndex]);
        auto unlockRect = reinterpret_cast<SurfaceUnlockRect_t>(surfaceVtbl[kSurfaceUnlockRectVtableIndex]);
        auto releaseSurface = reinterpret_cast<Release_t>(surfaceVtbl[kSurfaceReleaseVtableIndex]);
        LockedRect locked = {};
        if (SUCCEEDED(lockRect(surface, &locked, nullptr, 0)) && locked.pBits) {
            for (UINT y = 0; y < h; ++y) {
                memcpy(static_cast<BYTE*>(locked.pBits) + y * locked.Pitch,
                       pixels + static_cast<size_t>(y) * w, static_cast<size_t>(w) * sizeof(DWORD));
            }
            unlockRect(surface);
            uploaded = true;
        }
        releaseSurface(surface);
    }
    delete[] pixels;

    if (!uploaded) {
        void** vtbl = *reinterpret_cast<void***>(texture);
        reinterpret_cast<Release_t>(vtbl[kSurfaceReleaseVtableIndex])(texture);
        return false;
    }

    outTexture = texture;
    outWidth = static_cast<int>(w);
    outHeight = static_cast<int>(h);
    return true;
}

// Cache lookup (linear scan -- kMaxCachedGlyphIcons is 64, this runs at most a few
// times per second while a hint is on screen, not a hot path). Loads and caches on
// first request for a given assetName; every later request for the same name is free.
bool GetOrLoadGlyphIconTexture(void* device, const char* assetName, void*& outTexture, int& outWidth, int& outHeight)
{
    for (int i = 0; i < g_glyphIconCacheCount; ++i) {
        if (strcmp(g_glyphIconCache[i].assetName, assetName) == 0) {
            if (!g_glyphIconCache[i].texture) return false; // cached failure, don't retry every frame
            outTexture = g_glyphIconCache[i].texture;
            outWidth = g_glyphIconCache[i].width;
            outHeight = g_glyphIconCache[i].height;
            return true;
        }
    }
    if (g_glyphIconCacheCount >= kMaxCachedGlyphIcons) return false; // shouldn't happen, real asset count is 47

    void* texture = nullptr;
    int w = 0, h = 0;
    bool loaded = LoadGlyphIconTexture(device, assetName, texture, w, h);
    GlyphIconEntry& entry = g_glyphIconCache[g_glyphIconCacheCount++];
    strncpy_s(entry.assetName, assetName, _TRUNCATE);
    entry.texture = loaded ? texture : nullptr; // cache the failure too, see the early-out above
    entry.width = w;
    entry.height = h;
    if (!loaded) return false;
    outTexture = texture;
    outWidth = w;
    outHeight = h;
    return true;
}

// Draws an arbitrary texture as a plain, white-modulated, alpha-blended screen-space
// quad at (x, y, w, h) -- the same safe render-state save/restore and shader-null/
// restore pattern DrawTexturedQuad above uses (see that function's own comment for
// why the shader handling is necessary), generalized to take a texture and rect
// instead of always drawing the fixed toast texture at its fixed top-right position.
void DrawGenericTexturedQuad(void* device, void* texture, float x, float y, float w, float h, DWORD color = 0xFFFFFFFF,
                              float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f)
{
    void** deviceVtbl = *reinterpret_cast<void***>(device);
    auto setTexture = reinterpret_cast<SetTexture_t>(deviceVtbl[kSetTextureVtableIndex]);
    auto setFVF = reinterpret_cast<SetFVF_t>(deviceVtbl[kSetFVFVtableIndex]);
    auto setRenderState = reinterpret_cast<SetRenderState_t>(deviceVtbl[kSetRenderStateVtableIndex]);
    auto getRenderState = reinterpret_cast<GetRenderState_t>(deviceVtbl[kGetRenderStateVtableIndex]);
    auto setTss = reinterpret_cast<SetTextureStageState_t>(deviceVtbl[kSetTextureStageStateVtableIndex]);
    auto drawPrimitiveUP = reinterpret_cast<DrawPrimitiveUP_t>(deviceVtbl[kDrawPrimitiveUPVtableIndex]);
    auto setVertexShader = reinterpret_cast<SetVertexShader_t>(deviceVtbl[kSetVertexShaderVtableIndex]);
    auto getVertexShader = reinterpret_cast<GetVertexShader_t>(deviceVtbl[kGetVertexShaderVtableIndex]);
    auto setPixelShader = reinterpret_cast<SetPixelShader_t>(deviceVtbl[kSetPixelShaderVtableIndex]);
    auto getPixelShader = reinterpret_cast<GetPixelShader_t>(deviceVtbl[kGetPixelShaderVtableIndex]);

    DWORD oldZEnable = 0, oldLighting = 0, oldAlphaBlend = 0, oldSrcBlend = 0, oldDestBlend = 0, oldCull = 0;
    getRenderState(device, kD3DRS_ZENABLE, &oldZEnable);
    getRenderState(device, kD3DRS_LIGHTING, &oldLighting);
    getRenderState(device, kD3DRS_ALPHABLENDENABLE, &oldAlphaBlend);
    getRenderState(device, kD3DRS_SRCBLEND, &oldSrcBlend);
    getRenderState(device, kD3DRS_DESTBLEND, &oldDestBlend);
    getRenderState(device, kD3DRS_CULLMODE, &oldCull);

    void* oldVertexShader = nullptr;
    void* oldPixelShader = nullptr;
    getVertexShader(device, &oldVertexShader);
    getPixelShader(device, &oldPixelShader);
    setVertexShader(device, nullptr);
    setPixelShader(device, nullptr);

    setRenderState(device, kD3DRS_ZENABLE, kD3DZB_FALSE);
    setRenderState(device, kD3DRS_LIGHTING, FALSE);
    setRenderState(device, kD3DRS_ALPHABLENDENABLE, TRUE);
    setRenderState(device, kD3DRS_SRCBLEND, kD3DBLEND_SRCALPHA);
    setRenderState(device, kD3DRS_DESTBLEND, kD3DBLEND_INVSRCALPHA);
    setRenderState(device, kD3DRS_CULLMODE, kD3DCULL_NONE);

    setTss(device, 0, kD3DTSS_COLOROP, kD3DTOP_MODULATE);
    setTss(device, 0, kD3DTSS_COLORARG1, kD3DTA_TEXTURE);
    setTss(device, 0, kD3DTSS_COLORARG2, kD3DTA_DIFFUSE);
    setTss(device, 0, kD3DTSS_ALPHAOP, kD3DTOP_MODULATE);
    setTss(device, 0, kD3DTSS_ALPHAARG1, kD3DTA_TEXTURE);
    setTss(device, 0, kD3DTSS_ALPHAARG2, kD3DTA_DIFFUSE);
    setTss(device, 1, kD3DTSS_COLOROP, kD3DTOP_DISABLE);

    setTexture(device, 0, texture);
    setFVF(device, kFVF);

    ScreenVertex verts[4] = {
        { x - 0.5f,     y - 0.5f,     0.0f, 1.0f, color, u0, v0 },
        { x + w - 0.5f, y - 0.5f,     0.0f, 1.0f, color, u1, v0 },
        { x - 0.5f,     y + h - 0.5f, 0.0f, 1.0f, color, u0, v1 },
        { x + w - 0.5f, y + h - 0.5f, 0.0f, 1.0f, color, u1, v1 },
    };
    drawPrimitiveUP(device, kD3DPT_TRIANGLESTRIP, 2, verts, sizeof(ScreenVertex));

    setTexture(device, 0, nullptr);
    setRenderState(device, kD3DRS_ZENABLE, oldZEnable);
    setRenderState(device, kD3DRS_LIGHTING, oldLighting);
    setRenderState(device, kD3DRS_ALPHABLENDENABLE, oldAlphaBlend);
    setRenderState(device, kD3DRS_SRCBLEND, oldSrcBlend);
    setRenderState(device, kD3DRS_DESTBLEND, oldDestBlend);
    setRenderState(device, kD3DRS_CULLMODE, oldCull);

    setVertexShader(device, oldVertexShader);
    setPixelShader(device, oldPixelShader);
    if (oldVertexShader) {
        void** vtbl = *reinterpret_cast<void***>(oldVertexShader);
        reinterpret_cast<Release_t>(vtbl[kSurfaceReleaseVtableIndex])(oldVertexShader);
    }
    if (oldPixelShader) {
        void** vtbl = *reinterpret_cast<void***>(oldPixelShader);
        reinterpret_cast<Release_t>(vtbl[kSurfaceReleaseVtableIndex])(oldPixelShader);
    }
}

// Near-duplicate of DrawGenericTexturedQuad above, with independent LEFT/RIGHT vertex
// colors instead of one uniform color -- issue #66 restyle (2026-08-05), for the real
// console's own horizontal gradient highlight bar behind the focused row (bright near
// the panel's left edge, fading out toward the right). Deliberately a separate
// function rather than a refactor of the proven, heavily-used DrawGenericTexturedQuad
// (same "don't risk a shared refactor" reasoning as this file's other near-duplicate
// draw functions, e.g. DrawOneGameplayHintSlot/DrawOneMenuHintSlot's own comment).
void DrawGradientQuad(void* device, void* texture, float x, float y, float w, float h,
                        DWORD colorLeft, DWORD colorRight)
{
    void** deviceVtbl = *reinterpret_cast<void***>(device);
    auto setTexture = reinterpret_cast<SetTexture_t>(deviceVtbl[kSetTextureVtableIndex]);
    auto setFVF = reinterpret_cast<SetFVF_t>(deviceVtbl[kSetFVFVtableIndex]);
    auto setRenderState = reinterpret_cast<SetRenderState_t>(deviceVtbl[kSetRenderStateVtableIndex]);
    auto getRenderState = reinterpret_cast<GetRenderState_t>(deviceVtbl[kGetRenderStateVtableIndex]);
    auto setTss = reinterpret_cast<SetTextureStageState_t>(deviceVtbl[kSetTextureStageStateVtableIndex]);
    auto drawPrimitiveUP = reinterpret_cast<DrawPrimitiveUP_t>(deviceVtbl[kDrawPrimitiveUPVtableIndex]);
    auto setVertexShader = reinterpret_cast<SetVertexShader_t>(deviceVtbl[kSetVertexShaderVtableIndex]);
    auto getVertexShader = reinterpret_cast<GetVertexShader_t>(deviceVtbl[kGetVertexShaderVtableIndex]);
    auto setPixelShader = reinterpret_cast<SetPixelShader_t>(deviceVtbl[kSetPixelShaderVtableIndex]);
    auto getPixelShader = reinterpret_cast<GetPixelShader_t>(deviceVtbl[kGetPixelShaderVtableIndex]);

    DWORD oldZEnable = 0, oldLighting = 0, oldAlphaBlend = 0, oldSrcBlend = 0, oldDestBlend = 0, oldCull = 0;
    getRenderState(device, kD3DRS_ZENABLE, &oldZEnable);
    getRenderState(device, kD3DRS_LIGHTING, &oldLighting);
    getRenderState(device, kD3DRS_ALPHABLENDENABLE, &oldAlphaBlend);
    getRenderState(device, kD3DRS_SRCBLEND, &oldSrcBlend);
    getRenderState(device, kD3DRS_DESTBLEND, &oldDestBlend);
    getRenderState(device, kD3DRS_CULLMODE, &oldCull);

    void* oldVertexShader = nullptr;
    void* oldPixelShader = nullptr;
    getVertexShader(device, &oldVertexShader);
    getPixelShader(device, &oldPixelShader);
    setVertexShader(device, nullptr);
    setPixelShader(device, nullptr);

    setRenderState(device, kD3DRS_ZENABLE, kD3DZB_FALSE);
    setRenderState(device, kD3DRS_LIGHTING, FALSE);
    setRenderState(device, kD3DRS_ALPHABLENDENABLE, TRUE);
    setRenderState(device, kD3DRS_SRCBLEND, kD3DBLEND_SRCALPHA);
    setRenderState(device, kD3DRS_DESTBLEND, kD3DBLEND_INVSRCALPHA);
    setRenderState(device, kD3DRS_CULLMODE, kD3DCULL_NONE);

    setTss(device, 0, kD3DTSS_COLOROP, kD3DTOP_MODULATE);
    setTss(device, 0, kD3DTSS_COLORARG1, kD3DTA_TEXTURE);
    setTss(device, 0, kD3DTSS_COLORARG2, kD3DTA_DIFFUSE);
    setTss(device, 0, kD3DTSS_ALPHAOP, kD3DTOP_MODULATE);
    setTss(device, 0, kD3DTSS_ALPHAARG1, kD3DTA_TEXTURE);
    setTss(device, 0, kD3DTSS_ALPHAARG2, kD3DTA_DIFFUSE);
    setTss(device, 1, kD3DTSS_COLOROP, kD3DTOP_DISABLE);

    setTexture(device, 0, texture);
    setFVF(device, kFVF);

    ScreenVertex verts[4] = {
        { x - 0.5f,     y - 0.5f,     0.0f, 1.0f, colorLeft,  0.0f, 0.0f },
        { x + w - 0.5f, y - 0.5f,     0.0f, 1.0f, colorRight, 1.0f, 0.0f },
        { x - 0.5f,     y + h - 0.5f, 0.0f, 1.0f, colorLeft,  0.0f, 1.0f },
        { x + w - 0.5f, y + h - 0.5f, 0.0f, 1.0f, colorRight, 1.0f, 1.0f },
    };
    drawPrimitiveUP(device, kD3DPT_TRIANGLESTRIP, 2, verts, sizeof(ScreenVertex));

    setTexture(device, 0, nullptr);
    setRenderState(device, kD3DRS_ZENABLE, oldZEnable);
    setRenderState(device, kD3DRS_LIGHTING, oldLighting);
    setRenderState(device, kD3DRS_ALPHABLENDENABLE, oldAlphaBlend);
    setRenderState(device, kD3DRS_SRCBLEND, oldSrcBlend);
    setRenderState(device, kD3DRS_DESTBLEND, oldDestBlend);
    setRenderState(device, kD3DRS_CULLMODE, oldCull);

    setVertexShader(device, oldVertexShader);
    setPixelShader(device, oldPixelShader);
    if (oldVertexShader) {
        void** vtbl = *reinterpret_cast<void***>(oldVertexShader);
        reinterpret_cast<Release_t>(vtbl[kSurfaceReleaseVtableIndex])(oldVertexShader);
    }
    if (oldPixelShader) {
        void** vtbl = *reinterpret_cast<void***>(oldPixelShader);
        reinterpret_cast<Release_t>(vtbl[kSurfaceReleaseVtableIndex])(oldPixelShader);
    }
}

// Consumes the pending icon request (if any was made since the last EndScene) and
// draws it -- clears the "requested this frame" flag unconditionally afterward so a
// hint that stops resolving stops showing its icon within one frame, with no separate
// teardown call needed from the caller's side.
void DrawGlyphIconIfRequested(void* device)
{
    if (!g_pendingIconRequestedThisFrame) return;
    g_pendingIconRequestedThisFrame = false; // consume regardless of outcome below

    void* texture = nullptr;
    int texW = 0, texH = 0;
    if (!GetOrLoadGlyphIconTexture(device, g_pendingIconAssetName, texture, texW, texH)) return;
    DrawGenericTexturedQuad(device, texture, g_pendingIconX, g_pendingIconY, g_pendingIconW, g_pendingIconH);
}

// Draws prefix-text + real controller-glyph icon + suffix-text sequentially at
// (slot.x, slot.y), all self-rendered (see the big comment above GameplayHintSlot) --
// consumed once per frame like the plain icon overlay above, must be re-requested
// every frame to keep showing. Deliberately a near-duplicate of DrawOneMenuHintSlot's
// layout math further down, same reasoning as that function's own comment: this path
// is proven/live-tested and a shared refactor isn't worth the risk right now.
void DrawOneGameplayHintSlot(void* device, GameplayHintSlot& slot, float scaleX, float scaleY)
{
    // Live-reported 2026-07-31 (second round, 1440p): still wrong even after the first
    // scale-factor pass. Per explicit direction: "let's not make res a factor except
    // for size scaling, and just do things proportional to the edges + centre of the
    // screen." Every position/size value fed into this function (g_pendingHintX/Y,
    // kIconGap, kHintIconSize, etc.) is authored against a fixed 1920x1080 reference --
    // scaleX/scaleY convert that reference fraction directly against the REAL current
    // screen size, so "698 out of 1080" always lands at the same fraction of the real
    // screen's height regardless of resolution (proportional to the edges), and every
    // SIZE constant grows/shrinks by the same fraction. GetResolutionScale itself was
    // also switched from the window's GetClientRect (unreliable -- this old engine's
    // real backbuffer isn't guaranteed to match the window's client area) to the
    // device's actual GetViewport (ground truth for the pixel space our own quads draw
    // into) -- see its own comment in overlay_hud.h for why that switch mattered.
    auto drawScaledQuad = [&](void* texture, float x, float y, float w, float h, DWORD color,
                               float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f) {
        DrawGenericTexturedQuad(device, texture, x * scaleX, y * scaleY, w * scaleX, h * scaleY, color, u0, v0, u1, v1);
    };

    if (!EnsureLeftAlignedTextTexture(device, slot.prefixTexture, slot.prefixRenderedFor,
                                       sizeof(slot.prefixRenderedFor), slot.prefixText,
                                       slot.prefixLastFontHeight, kHintFontHeightPx)) return;
    slot.prefixMeasuredWidth = MeasureTextWidthPx(slot.prefixText, g_modConfig.overlayFontItalic, kHintFontHeightPx);

    if (!EnsureLeftAlignedTextTexture(device, slot.suffixTexture, slot.suffixRenderedFor,
                                       sizeof(slot.suffixRenderedFor), slot.suffixText,
                                       slot.suffixLastFontHeight, kHintFontHeightPx)) return;
    slot.suffixMeasuredWidth = MeasureTextWidthPx(slot.suffixText, g_modConfig.overlayFontItalic, kHintFontHeightPx);

    void* iconTexture = nullptr;
    int iconTexW = 0, iconTexH = 0;
    bool haveIcon = GetOrLoadGlyphIconTexture(device, slot.assetName, iconTexture, iconTexW, iconTexH);
    // Live-reported 2026-07-31: RB/R1 (a real 94x54 wide rectangle, unlike X/A's
    // roughly-square ~70x70/70x71 source art) looked squished -- forcing every icon
    // into the same fixed square box distorted its real aspect ratio. Height stays
    // fixed at kHintIconSize (consistent visual weight against the text regardless of
    // which icon); width is derived from the source PNG's own real dimensions
    // (iconTexW/iconTexH, already read by GetOrLoadGlyphIconTexture) so wide icons
    // stay wide instead of being squeezed into a square.
    float iconDrawWidth = kHintIconSize;
    float iconDrawHeight = kHintIconSize;
    if (haveIcon && iconTexH > 0) {
        iconDrawWidth = kHintIconSize * (static_cast<float>(iconTexW) / static_cast<float>(iconTexH));
    }

    // Live-reported 2026-07-31: the space between the icon and the following text
    // read as "massive" -- pixel-measured against a real screenshot (buy-station
    // hint): icon's own visible right edge to the real text's visible left edge was
    // ~26px for a ~36px-wide icon, clearly disproportionate. Two contributing causes,
    // both fixed here: (1) RenderMaskLuminance always draws with an 8px left margin
    // (fine for the toast's own right-aligned use, where it sits on the far side from
    // the visible edge that matters) -- for these LEFT-aligned segments it added dead
    // space directly where the icon meets text. kHintTextRenderLeftMarginPx lets the
    // UV sample SKIP that known-empty leading margin instead of shifting the
    // destination quad, so the visible glyph starts right at the quad's own left edge.
    // (2) kIconGap itself was more generous (8px) than actually needed once (1) is
    // fixed -- reduced to a tighter, still-readable gap.
    constexpr float kIconGap = 3.0f;
    constexpr int kHintTextRenderLeftMarginPx = 8; // matches RenderMaskLuminance's own hardcoded left inset
    // Pixel-measured 2026-07-31 (round 6, against the real static " Model 1887" HUD
    // text via direct screenshot pixel scanning): g_pendingHintY is the caller's
    // intended VERTICAL CENTER of the line (matching the same real HUD element's own
    // measured center), not the top of the drawn box -- text quads below derive their
    // top from this center by subtracting half the canvas height, same convention the
    // icon already used.
    float iconVerticalCenter = slot.y;
    float textQuadTop = slot.y - static_cast<float>(kTextureHeight) * 0.5f;

    // Live-tested 2026-07-31: the plain measured width clipped the last character of
    // a segment (e.g. the "s" in "Press") -- RenderMaskLuminance always draws with an
    // 8px left margin GetTextExtentPoint32A's measurement doesn't include, and italic
    // slant can extend a little past its own nominal advance width. kHintTextWidthMarginPx
    // pads the DRAWN width (not the cursor advance -- see cursorX below, which still
    // uses the plain measured width so the next piece isn't pushed too far right) so
    // the crop includes the real trailing pixels.
    int prefixDrawWidth = slot.prefixMeasuredWidth > 0 ? slot.prefixMeasuredWidth + kHintTextWidthMarginPx : 0;
    int suffixDrawWidth = slot.suffixMeasuredWidth > 0 ? slot.suffixMeasuredWidth + kHintTextWidthMarginPx : 0;

    float cursorX = slot.x;
    if (slot.centerOnScreen) {
        // Live-reported 2026-07-31: several hints (pickup/swap, and now buy-station
        // too) read better horizontally CENTERED on screen than left-anchored at the
        // game's own real x. Uses the total real (unpadded) content width -- prefix +
        // gap + icon + gap + suffix -- not the padded draw widths. Content width is
        // computed in DESIGN-space pixels (same as everything else here); the real
        // screen width is real pixels, so it's converted to design-space (divided by
        // scaleX) before centering against it -- otherwise this would silently mix
        // the two spaces and mis-center on any non-1080p resolution.
        float totalContentWidth = static_cast<float>(slot.prefixMeasuredWidth) + kIconGap
            + (haveIcon ? iconDrawWidth + kIconGap : 0.0f)
            + static_cast<float>(slot.suffixMeasuredWidth);
        // Uses the SAME real-screen-size source as scaleX/scaleY above (GetViewport,
        // not a separate GetClientRect call) -- two different sources for "real screen
        // width" disagreeing with each other would silently break centering on its own.
        int screenWidthPx = 1920, screenHeightPxUnused = 1080;
        GetRealScreenSize(device, screenWidthPx, screenHeightPxUnused);
        float screenWidthDesign = static_cast<float>(screenWidthPx) / scaleX;
        cursorX = (screenWidthDesign - totalContentWidth) * 0.5f;
    }
    float lineStartX = cursorX; // captured before prefix/icon/suffix advance it -- see the optional top line below

    // Live-reported 2026-07-31: still mis-positioned at 1440p even with scaleX/scaleY
    // wired through -- logged once per distinct (assetName, design cursorX/Y, scale)
    // combination so a live repro's proxy_d3d9.log can be compared directly against a
    // pixel-measured screenshot, instead of guessing at another fix blind.
    {
        static char s_lastLoggedKey[96] = {};
        char key[96];
        sprintf_s(key, "%s|%.1f|%.1f|%.4f|%.4f", slot.assetName, cursorX, slot.y, scaleX, scaleY);
        if (strncmp(s_lastLoggedKey, key, sizeof(s_lastLoggedKey) - 1) != 0) {
            strncpy_s(s_lastLoggedKey, key, _TRUNCATE);
            char buf[224];
            sprintf_s(buf, "[overlay-hud][res-scale] hint asset=%s designCursorX=%.1f designHintY=%.1f "
                             "scaleX=%.4f scaleY=%.4f -> screenX=%.1f screenY=%.1f",
                       slot.assetName, cursorX, slot.y, scaleX, scaleY,
                       cursorX * scaleX, slot.y * scaleY);
            LogFromController(buf);
        }
    }

    if (prefixDrawWidth > 0) {
        float prefixU0 = static_cast<float>(kHintTextRenderLeftMarginPx) / static_cast<float>(kTextureWidth);
        float prefixU1 = static_cast<float>(kHintTextRenderLeftMarginPx + prefixDrawWidth) / static_cast<float>(kTextureWidth);
        drawScaledQuad(slot.prefixTexture, cursorX, textQuadTop,
            static_cast<float>(prefixDrawWidth), static_cast<float>(kTextureHeight),
            0xFFFFFFFF, prefixU0, 0.0f, prefixU1, 1.0f);
        cursorX += static_cast<float>(slot.prefixMeasuredWidth) + kIconGap;
    }

    if (haveIcon) {
        DWORD iconColor = 0xFFFFFFFF;
        if (slot.flashIcon) {
            // Console-style Reload prompt (live-reported 2026-07-31, corrected same
            // day: "it is a pulsing fade" -- not a hard on/off blink): smooth sine-
            // wave alpha pulse, surrounding text stays solid regardless.
            constexpr float kPulsePeriodMs = 1200.0f;
            constexpr BYTE kPulseMinAlpha = 60;
            float phase = fmodf(static_cast<float>(GetTickCount()), kPulsePeriodMs) / kPulsePeriodMs;
            float wave = (sinf(phase * 6.2831853f) + 1.0f) * 0.5f; // 0..1
            BYTE alpha = static_cast<BYTE>(kPulseMinAlpha + wave * (255 - kPulseMinAlpha));
            iconColor = (static_cast<DWORD>(alpha) << 24) | 0x00FFFFFF;
        }
        drawScaledQuad(iconTexture, cursorX, iconVerticalCenter - iconDrawHeight * 0.5f,
            iconDrawWidth, iconDrawHeight, iconColor);
        cursorX += iconDrawWidth + kIconGap;
    }

    if (suffixDrawWidth > 0) {
        float suffixU0 = static_cast<float>(kHintTextRenderLeftMarginPx) / static_cast<float>(kTextureWidth);
        float suffixU1 = static_cast<float>(kHintTextRenderLeftMarginPx + suffixDrawWidth) / static_cast<float>(kTextureWidth);
        drawScaledQuad(slot.suffixTexture, cursorX, textQuadTop,
            static_cast<float>(suffixDrawWidth), static_cast<float>(kTextureHeight),
            0xFFFFFFFF, suffixU0, 0.0f, suffixU1, 1.0f);
    }

    // Optional top line (2026-08-02) -- see the struct field's own comment. Drawn
    // last so a failure to build its texture never blocks the main line above, and
    // left-aligned at the SAME horizontal start the main line used (whether that
    // came from raw slot.x or the centered position computed above), directly one
    // line-height above it.
    if (slot.topLineText[0] != '\0' &&
        EnsureLeftAlignedTextTexture(device, slot.topLineTexture, slot.topLineRenderedFor,
                                       sizeof(slot.topLineRenderedFor), slot.topLineText,
                                       slot.topLineLastFontHeight, kHintFontHeightPx)) {
        slot.topLineMeasuredWidth = MeasureTextWidthPx(slot.topLineText, g_modConfig.overlayFontItalic, kHintFontHeightPx);
        int topLineDrawWidth = slot.topLineMeasuredWidth > 0 ? slot.topLineMeasuredWidth + kHintTextWidthMarginPx : 0;
        if (topLineDrawWidth > 0) {
            float topU0 = static_cast<float>(kHintTextRenderLeftMarginPx) / static_cast<float>(kTextureWidth);
            float topU1 = static_cast<float>(kHintTextRenderLeftMarginPx + topLineDrawWidth) / static_cast<float>(kTextureWidth);
            drawScaledQuad(slot.topLineTexture, lineStartX, textQuadTop - static_cast<float>(kTextureHeight),
                static_cast<float>(topLineDrawWidth), static_cast<float>(kTextureHeight),
                0xFFFFFFFF, topU0, 0.0f, topU1, 1.0f);
        }
    }
}

// Consumes and draws every gameplay hint slot requested THIS FRAME (2026-08-02,
// BUG-004), applying the ONE deliberate, named suppression this project actually
// wants: hide the Reload reminder specifically while ready-up OR a real interact
// hint (pickup/swap/buy-station/mantle) is also showing this frame, since either
// combination is redundant clutter -- this replaces the old single-slot system's
// "was an interact hint active in the last 100ms" wall-clock heuristic, itself the
// source of the report's "Reload occasionally fails to display" note, with an exact
// same-frame check that can't race. Every OTHER combination of slots coexists by
// default -- this is NOT a "pick one winner" priority scheme, it's independent-by-
// default with one explicit, named exception, per the user's own direction.
void DrawGameplayHintSlotsIfRequested(void* device)
{
    bool anyRequested = false;
    for (auto& slot : g_gameplayHintSlots) {
        if (slot.requestedThisFrame) { anyRequested = true; break; }
    }
    if (!anyRequested) return;

    float scaleX = 1.0f, scaleY = 1.0f;
    GetResolutionScale(device, scaleX, scaleY);

    bool readyUpOrInteractShowing =
        g_gameplayHintSlots[static_cast<int>(GameplayHintSlotId::ReadyUp)].requestedThisFrame ||
        g_gameplayHintSlots[static_cast<int>(GameplayHintSlotId::Interact)].requestedThisFrame;

    for (int i = 0; i < kGameplayHintSlotCount; ++i) {
        GameplayHintSlot& slot = g_gameplayHintSlots[i];
        if (!slot.requestedThisFrame) continue;
        slot.requestedThisFrame = false; // consume regardless of outcome below
        if (static_cast<GameplayHintSlotId>(i) == GameplayHintSlotId::Reload && readyUpOrInteractShowing) {
            continue; // the one named suppression rule -- see this function's own comment
        }
        DrawOneGameplayHintSlot(device, slot, scaleX, scaleY);
    }
}

// Draws ONE menu-hint slot (2026-08-01, see the big comment above g_menuHintSlots).
// Deliberately a near-duplicate of DrawCustomHintIfRequested's own per-hint layout
// math (icon aspect ratio, gap/margin handling) rather than a shared refactor -- the
// gameplay hint path above is proven and live-tested across many rounds this
// session; duplicating a page of layout code here is a smaller, lower-risk change
// than reshaping that already-working function to also serve N slots. Menu hints
// never center on screen or pulse (no Reload-style prompt exists in menu UI), so
// this omits both of those branches entirely -- always left-anchored at (x, y).
void DrawOneMenuHintSlot(void* device, MenuHintSlot& slot, float scaleX, float scaleY)
{
    auto drawScaledQuad = [&](void* texture, float x, float y, float w, float h, DWORD color,
                               float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f) {
        DrawGenericTexturedQuad(device, texture, x * scaleX, y * scaleY, w * scaleX, h * scaleY, color, u0, v0, u1, v1);
    };

    if (!EnsureLeftAlignedTextTexture(device, slot.prefixTexture, slot.prefixRenderedFor,
                                       sizeof(slot.prefixRenderedFor), slot.prefixText,
                                       slot.prefixLastFontHeight, kHintFontHeightPx)) return;
    slot.prefixMeasuredWidth = MeasureTextWidthPx(slot.prefixText, g_modConfig.overlayFontItalic, kHintFontHeightPx);

    if (!EnsureLeftAlignedTextTexture(device, slot.suffixTexture, slot.suffixRenderedFor,
                                       sizeof(slot.suffixRenderedFor), slot.suffixText,
                                       slot.suffixLastFontHeight, kHintFontHeightPx)) return;
    slot.suffixMeasuredWidth = MeasureTextWidthPx(slot.suffixText, g_modConfig.overlayFontItalic, kHintFontHeightPx);

    void* iconTexture = nullptr;
    int iconTexW = 0, iconTexH = 0;
    bool haveIcon = GetOrLoadGlyphIconTexture(device, slot.assetName, iconTexture, iconTexW, iconTexH);
    float iconDrawWidth = kHintIconSize;
    float iconDrawHeight = kHintIconSize;
    if (haveIcon && iconTexH > 0) {
        iconDrawWidth = kHintIconSize * (static_cast<float>(iconTexW) / static_cast<float>(iconTexH));
    }

    constexpr float kIconGap = 3.0f;
    constexpr int kHintTextRenderLeftMarginPx = 8;
    float iconVerticalCenter = slot.y;
    float textQuadTop = slot.y - static_cast<float>(kTextureHeight) * 0.5f;

    int prefixDrawWidth = slot.prefixMeasuredWidth > 0 ? slot.prefixMeasuredWidth + kHintTextWidthMarginPx : 0;
    int suffixDrawWidth = slot.suffixMeasuredWidth > 0 ? slot.suffixMeasuredWidth + kHintTextWidthMarginPx : 0;

    float cursorX = slot.x;
    if (prefixDrawWidth > 0) {
        float prefixU0 = static_cast<float>(kHintTextRenderLeftMarginPx) / static_cast<float>(kTextureWidth);
        float prefixU1 = static_cast<float>(kHintTextRenderLeftMarginPx + prefixDrawWidth) / static_cast<float>(kTextureWidth);
        drawScaledQuad(slot.prefixTexture, cursorX, textQuadTop,
            static_cast<float>(prefixDrawWidth), static_cast<float>(kTextureHeight),
            0xFFFFFFFF, prefixU0, 0.0f, prefixU1, 1.0f);
        cursorX += static_cast<float>(slot.prefixMeasuredWidth) + kIconGap;
    }

    if (haveIcon) {
        drawScaledQuad(iconTexture, cursorX, iconVerticalCenter - iconDrawHeight * 0.5f,
            iconDrawWidth, iconDrawHeight, 0xFFFFFFFF);
        cursorX += iconDrawWidth + kIconGap;
    }

    if (suffixDrawWidth > 0) {
        float suffixU0 = static_cast<float>(kHintTextRenderLeftMarginPx) / static_cast<float>(kTextureWidth);
        float suffixU1 = static_cast<float>(kHintTextRenderLeftMarginPx + suffixDrawWidth) / static_cast<float>(kTextureWidth);
        drawScaledQuad(slot.suffixTexture, cursorX, textQuadTop,
            static_cast<float>(suffixDrawWidth), static_cast<float>(kTextureHeight),
            0xFFFFFFFF, suffixU0, 0.0f, suffixU1, 1.0f);
    }
}

// Consumes and draws every menu-hint slot accumulated so far THIS FRAME (see the big
// comment above g_menuHintSlots for why menu hints need N slots, unlike the single
// gameplay slot), then resets the count to 0 -- same "must be re-requested every
// frame to keep showing" convention as the gameplay hint, just per-slot instead of
// a single flag, so a screen with fewer menu hints than last frame doesn't redraw
// stale ones.
void DrawMenuHintsIfRequested(void* device)
{
    int count = g_menuHintSlotCountThisFrame;
    g_menuHintSlotCountThisFrame = 0;
    if (count == 0) return;
    float scaleX = 1.0f, scaleY = 1.0f;
    GetResolutionScale(device, scaleX, scaleY);
    for (int i = 0; i < count; ++i) {
        DrawOneMenuHintSlot(device, g_menuHintSlots[i], scaleX, scaleY);
    }
}

// TEMPORARY debug scaffolding -- see the big comment above g_debugMarkerTexture.
bool EnsureDebugMarkerTexture(void* device)
{
    if (g_debugMarkerTexture) return true;
    void** deviceVtbl = *reinterpret_cast<void***>(device);
    auto createTexture = reinterpret_cast<CreateTexture_t>(deviceVtbl[kCreateTextureVtableIndex]);
    HRESULT hr = createTexture(device, 1, 1, 1, 0, kD3DFMT_A8R8G8B8, kD3DPOOL_MANAGED, &g_debugMarkerTexture, nullptr);
    if (FAILED(hr) || !g_debugMarkerTexture) return false;

    void** texVtbl = *reinterpret_cast<void***>(g_debugMarkerTexture);
    auto getSurfaceLevel = reinterpret_cast<GetSurfaceLevel_t>(texVtbl[kGetSurfaceLevelVtableIndex]);
    void* surface = nullptr;
    if (FAILED(getSurfaceLevel(g_debugMarkerTexture, 0, &surface)) || !surface) return false;
    void** surfaceVtbl = *reinterpret_cast<void***>(surface);
    auto lockRect = reinterpret_cast<SurfaceLockRect_t>(surfaceVtbl[kSurfaceLockRectVtableIndex]);
    auto unlockRect = reinterpret_cast<SurfaceUnlockRect_t>(surfaceVtbl[kSurfaceUnlockRectVtableIndex]);
    auto releaseSurface = reinterpret_cast<Release_t>(surfaceVtbl[kSurfaceReleaseVtableIndex]);
    LockedRect locked = {};
    if (SUCCEEDED(lockRect(surface, &locked, nullptr, 0)) && locked.pBits) {
        *static_cast<DWORD*>(locked.pBits) = 0xFFFFFFFF; // opaque white, tinted via diffuse at draw time
        unlockRect(surface);
    }
    releaseSurface(surface);
    return true;
}

void DrawDebugMarkerIfRequested(void* device)
{
    bool anyRequested = false;
    for (int i = 0; i < kMaxDebugMarkerSlots; ++i) if (g_pendingMarkerRequestedThisFrame[i]) anyRequested = true;
    if (!anyRequested) return;
    if (!EnsureDebugMarkerTexture(device)) return;
    // A small filled square CENTERED on each requested point, one distinct color per
    // slot (see kDebugMarkerColors) so multiple position hypotheses can be visually
    // compared against the real text in a single screenshot.
    constexpr float kMarkerSize = 8.0f;
    for (int i = 0; i < kMaxDebugMarkerSlots; ++i) {
        if (!g_pendingMarkerRequestedThisFrame[i]) continue;
        g_pendingMarkerRequestedThisFrame[i] = false;
        DrawGenericTexturedQuad(device, g_debugMarkerTexture,
            g_pendingMarkerX[i] - kMarkerSize * 0.5f, g_pendingMarkerY[i] - kMarkerSize * 0.5f,
            kMarkerSize, kMarkerSize, kDebugMarkerColors[i]);
    }
}

// ---- Custom mouse cursor (2026-08-01, user-requested) -----------------------------
//
// This project's own overlay draws happen at end-of-frame, after the real game has
// already drawn everything for the frame -- including its own native software cursor
// (this engine, like every PC CoD title, draws its own cursor sprite rather than
// using a real Windows hardware cursor). That means the native cursor was rendering
// UNDER this project's own glyph icons/hints instead of on top, wherever they
// overlapped. Fix: suppress the native cursor's own draw call (see Hook_004d48f0 in
// analog_input_hooks.cpp, a return-address-gated hook on the shared quad-draw
// primitive the native cursor happens to share with 30 other unrelated UI draw
// call sites) and redraw our own cursor art here instead, as the LAST thing drawn
// each frame, guaranteeing correct z-order over everything else this project draws.
//
// Visibility gated on the same real globals the native cursor code itself reads
// (re_notes/iw5sp.md, confirmed via decompile of FUN_00478540): DAT_01c00474
// (visibility flag), DAT_01c0ad14 (current UI/menu state -- cursor hidden for
// states 0/6/10, matching the native switch). Does NOT replicate the native code's
// own case-3 "accept invite" sub-check (a narrow, low-frequency edge case) --
// worst case there is a redundant cursor draw during that one specific flow, not a
// missing one.
//
// POSITION, by contrast, does NOT use the internal DAT_01c00468/DAT_01c0046c
// globals -- live-reported 2026-08-01: the drawn cursor landed nowhere near the
// real one (which was clearly hovering the Special Ops tile, confirmed by that
// tile being highlighted, while the drawn icon rendered up near the logo).
// Whatever coordinate space those two globals are actually in, it is NOT the same
// 1920x1080 design space this project's own glyph draws use. A follow-up attempt
// using GetCursorPos+ScreenToClient (already in real client pixels, no coordinate-
// space guessing) ALSO landed wrong -- live-reported to grow increasingly off the
// further from the top-left corner, the classic symptom of a DPI-awareness-context
// mismatch between this DLL and the host process (GetCursorPos silently returns
// virtualized/scaled coordinates for a DPI-unaware caller vs. real physical pixels
// for a DPI-aware one). Fixed by sidestepping the whole question: read the exact
// same WM_MOUSEMOVE client-coordinate values the game's own WndProc already
// receives and uses for its own hit-testing (GetLastMouseMoveClientPos,
// d3d9_hook.cpp) -- guaranteed to agree with whatever the game itself considers
// "the mouse is here," since it's literally the same message data.
// ---- Custom in-game options overlay (2026-08-04) ----------------------------------
//
// See overlay_hud.h's own comment for the overall design (extend the real OPTIONS_LIST
// menu with one purely-drawn extra row, since native menu content injection is
// confirmed unsafe for real content outside the engine's own controlled load context --
// issue #23). Everything below lives in this file because it needs the same low-level
// D3D9 text/quad drawing primitives (EnsureLeftAlignedTextTexture, MeasureTextWidthPx,
// DrawGenericTexturedQuad) already defined above in this same anonymous namespace, and
// runs off the same EndScene hook.
//
// Visual direction (explicit user request, 2026-08-04): faithful to the real console
// Options screen's structure (a flat list of settings, current value shown per row, no
// tabs) -- NOT a from-scratch redesign -- with modern polish (this project's own crisp
// embedded font, smooth highlight bar, real-resolution-aware sizing) rather than a
// literal 1:1 recreation of the console's exact 2011 art.
//
// Scope, deliberately narrow for v1: only settings this mod actually owns and can make
// DO something (Sensitivity H/V, Invert Look, Vibration Enable, Stick Layout, Button
// Layout). The real console screen also lists Game Volume/Brightness/Subtitles/Color
// Blind Assist/Horizontal+Vertical Margin -- those aren't controller-specific and this
// project doesn't implement any of them, so including them as inert rows would violate
// this project's own "no placeholder settings" standard (CLAUDE.md 5). Left out
// entirely rather than faked.

enum class OptRowKind { FloatValue, BoolToggle, StickLayoutEnum, ButtonLayoutEnum };

struct OptRow {
    const char* label;
    OptRowKind kind;
    float* floatPtr;   // FloatValue only
    float floatStep;
    float floatMin;
    float floatMax;
    bool* boolPtr;      // BoolToggle only
    const char* description = ""; // real-console-style one-line footer description
                                    // (2026-08-05 restyle), shown for whichever row
                                    // is currently focused
};

OptRow g_optRows[] = {
    { "SENSITIVITY HORIZONTAL", OptRowKind::FloatValue, &g_modConfig.lookDegreesPerSecondHorizontal, 10.0f, 50.0f, 500.0f, nullptr, "Adjust your horizontal look sensitivity." },
    { "SENSITIVITY VERTICAL",   OptRowKind::FloatValue, &g_modConfig.lookDegreesPerSecondVertical,   10.0f, 50.0f, 500.0f, nullptr, "Adjust your vertical look sensitivity." },
    { "INVERT LOOK",            OptRowKind::BoolToggle,  nullptr, 0.0f, 0.0f, 0.0f, &g_modConfig.invertLook, "Invert the vertical look axis." },
    { "VIBRATION",              OptRowKind::BoolToggle,  nullptr, 0.0f, 0.0f, 0.0f, &g_modConfig.vibrationEnabled, "Enable or disable controller vibration." },
    { "STICK LAYOUT",           OptRowKind::StickLayoutEnum,  nullptr, 0.0f, 0.0f, 0.0f, nullptr, "Choose your stick layout." },
    { "BUTTON LAYOUT",          OptRowKind::ButtonLayoutEnum, nullptr, 0.0f, 0.0f, 0.0f, nullptr, "Choose your button layout." },
};
constexpr int kOptRowCount = sizeof(g_optRows) / sizeof(g_optRows[0]);

// ---- State (2026-08-04) ----
// No more "reachable but not yet open" chip state (g_optExtraRowReachable/Selected,
// removed same day as added -- see overlay_hud.h's own header comment): the menu is
// now either open or not, invoked directly from the real pause menu's own "Options"
// button rather than a row appended to a real list.
bool g_optMenuOpen = false;
int g_optSelectedRow = 0;
// Stick/Button Layout drill-down (2026-08-05 restyle) -- selecting either row on the
// Controller tab opens a real sub-screen (own option list + controller diagram)
// instead of cycling the enum inline via Left/Right, matching the real console's own
// Stick Layout/Button Layout sub-screens. g_optSelectedRow (which row on the main
// list) stays unchanged/untouched while drilldown is open, since it's what tells the
// draw code which of the two rows (and therefore which enum/diagram) is active.
bool g_optDrilldownOpen = false;
int g_optDrilldownSelectedRow = 0;

struct TextTexCache { void* texture = nullptr; char renderedFor[128] = {}; int lastFontHeightPx = 0; };
// Sized to kUnifiedTabRowCacheSize (16, matching g_tabVanillaIndices' own capacity),
// not kOptRowCount -- shared across every tab (Controller's 6 rows and, once a tab
// switch happens, a vanilla tab's rows) since only one tab's rows are ever drawn in
// a given frame. A stale cache slot from a previously-shown tab harmlessly
// re-renders the moment its text differs from what's cached (EnsureLeftAlignedTextTexture's
// own renderedFor comparison already handles this correctly).
constexpr int kUnifiedTabRowCacheSize = 16;
TextTexCache g_optRowLabelCache[kUnifiedTabRowCacheSize];
TextTexCache g_optRowValueCache[kUnifiedTabRowCacheSize];
TextTexCache g_tabBarCache[8]; // sized above kUnifiedTabCount (3 today) for future tabs
TextTexCache g_optTitleCache;
// Real-console-style per-row description line, bottom of the panel (2026-08-05
// restyle, replaces the old full-width footer legend).
TextTexCache g_optDescCache;
// Bottom-right corner "Back" hint (2026-08-05 restyle) -- replaces the old
// full-width footer bar (LB/RB TABS / LEFT-RIGHT ADJUST / A TOGGLE / B CLOSE / OR
// CLICK); the real console screen shows only this, nothing else.
TextTexCache g_optCornerBackCache;
// Shared label-texture pool for the Stick/Button Layout drill-down diagram
// (2026-08-05) -- sized to the larger of the two diagrams' label counts (Button
// Layout: 10 ButtonMap-driven labels + 1 static D-pad label = 11), same "shared
// across whichever one is currently drawn" reasoning as g_optRowLabelCache.
TextTexCache g_diagLabelCache[16];
void* g_optWhiteTexture = nullptr; // 1x1 white texture for solid-fill background panels

bool EnsureWhiteTexture(void* device)
{
    if (g_optWhiteTexture) return true;
    void** deviceVtbl = *reinterpret_cast<void***>(device);
    auto createTexture = reinterpret_cast<CreateTexture_t>(deviceVtbl[kCreateTextureVtableIndex]);
    HRESULT hr = createTexture(device, 1, 1, 1, 0, kD3DFMT_A8R8G8B8, kD3DPOOL_MANAGED, &g_optWhiteTexture, nullptr);
    if (FAILED(hr) || !g_optWhiteTexture) { g_optWhiteTexture = nullptr; return false; }

    void** texVtbl = *reinterpret_cast<void***>(g_optWhiteTexture);
    auto getSurfaceLevel = reinterpret_cast<GetSurfaceLevel_t>(texVtbl[kGetSurfaceLevelVtableIndex]);
    void* surface = nullptr;
    if (FAILED(getSurfaceLevel(g_optWhiteTexture, 0, &surface)) || !surface) return false;
    void** surfaceVtbl = *reinterpret_cast<void***>(surface);
    auto lockRect = reinterpret_cast<SurfaceLockRect_t>(surfaceVtbl[kSurfaceLockRectVtableIndex]);
    auto unlockRect = reinterpret_cast<SurfaceUnlockRect_t>(surfaceVtbl[kSurfaceUnlockRectVtableIndex]);
    auto releaseSurface = reinterpret_cast<Release_t>(surfaceVtbl[kSurfaceReleaseVtableIndex]);
    LockedRect locked = {};
    if (SUCCEEDED(lockRect(surface, &locked, nullptr, 0)) && locked.pBits) {
        *reinterpret_cast<DWORD*>(locked.pBits) = 0xFFFFFFFFu;
        unlockRect(surface);
    }
    releaseSurface(surface);
    return true;
}

void FormatOptRowValue(const OptRow& row, char* outBuf, size_t outBufSize)
{
    switch (row.kind) {
        case OptRowKind::FloatValue:
            sprintf_s(outBuf, outBufSize, "%.0f", *row.floatPtr);
            break;
        case OptRowKind::BoolToggle:
            strcpy_s(outBuf, outBufSize, *row.boolPtr ? "ENABLED" : "DISABLED");
            break;
        case OptRowKind::StickLayoutEnum: {
            static const char* kNames[] = { "DEFAULT", "SOUTHPAW", "LEGACY", "LEGACY SOUTHPAW" };
            strcpy_s(outBuf, outBufSize, kNames[static_cast<int>(g_modConfig.stickLayout)]);
            break;
        }
        case OptRowKind::ButtonLayoutEnum: {
            static const char* kNames[] = { "DEFAULT", "TACTICAL", "LEFTY", "TACTICAL LEFTY" };
            strcpy_s(outBuf, outBufSize, kNames[static_cast<int>(g_modConfig.buttonLayout)]);
            break;
        }
    }
}

// direction: -1 (Left) or +1 (Right). Bool rows toggle regardless of direction.
void AdjustOptRow(int rowIndex, int direction)
{
    OptRow& row = g_optRows[rowIndex];
    switch (row.kind) {
        case OptRowKind::FloatValue: {
            float v = *row.floatPtr + static_cast<float>(direction) * row.floatStep;
            if (v < row.floatMin) v = row.floatMin;
            if (v > row.floatMax) v = row.floatMax;
            *row.floatPtr = v;
            break;
        }
        case OptRowKind::BoolToggle:
            *row.boolPtr = !*row.boolPtr;
            break;
        case OptRowKind::StickLayoutEnum: {
            int v = (static_cast<int>(g_modConfig.stickLayout) + direction + 4) % 4;
            g_modConfig.stickLayout = static_cast<StickLayout>(v);
            g_buttonMap = ResolveButtonMap(g_modConfig.buttonLayout, g_modConfig.flipTriggers);
            break;
        }
        case OptRowKind::ButtonLayoutEnum: {
            int v = (static_cast<int>(g_modConfig.buttonLayout) + direction + 4) % 4;
            g_modConfig.buttonLayout = static_cast<ButtonLayout>(v);
            g_buttonMap = ResolveButtonMap(g_modConfig.buttonLayout, g_modConfig.flipTriggers);
            break;
        }
    }
    SaveModConfig();
}

// ---- Unified tabs (2026-08-04, issue #66 full-scope pivot) ------------------------
//
// After the render-suppression approach was live-tested and found to prevent the
// game from launching at all (re_notes/known_issues.md issue #66), the project owner
// redirected to the original lower-risk alternative: draw fully over the top of the
// real screen and claim all input while open (both already true of this menu since
// round 1), rather than suppressing the real menu's own rendering. This section adds
// TAB navigation on top of that unchanged foundation, so the single flat 6-row
// Controller-only list becomes a tabbed screen covering the mod's own settings AND
// (phase 1) the real vanilla Look/Voice settings, via the vanilla_settings_table.h/
// vanilla_settings_sync.h layer built earlier this session.
//
// Phase 1 scope, deliberately narrow: Controller (unchanged g_optRows), Look, and
// Voice tabs only, and only their DvarFloat/DvarBool rows (fully editable, no
// ambiguity). NOT included yet, each for a real, specific reason rather than an
// oversight: Video/Audio/AdvancedVideo (need the staged-settings Apply-prompt UI,
// staged_settings.h, wired in -- not done this pass), Movement/Actions (pure
// keybinds -- need a "press a key to rebind" capture UX, not just display), Look's
// own 4 keybind rows and Voice's Push-to-Talk (same reason), DvarString/enum rows
// generally (this project doesn't yet have the real per-dvar enum choice lists, e.g.
// "Off/2x/4x" for anti-aliasing, only the raw dvar name/type -- adjusting a
// DvarString row via Left/Right with no real choice list to step through would be
// guessing, not a real control).
enum class UnifiedTab { Controller, Look, Voice };
constexpr UnifiedTab kTabOrder[] = { UnifiedTab::Controller, UnifiedTab::Look, UnifiedTab::Voice };
constexpr int kUnifiedTabCount = sizeof(kTabOrder) / sizeof(kTabOrder[0]);

const char* UnifiedTabDisplayName(UnifiedTab tab)
{
    switch (tab) {
        case UnifiedTab::Controller: return "CONTROLLER";
        case UnifiedTab::Look:       return "LOOK";
        case UnifiedTab::Voice:      return "VOICE";
    }
    return "?";
}

int g_currentTabIndex = 0; // index into kTabOrder; Controller (0) is always the
                             // opening tab, see CustomOptionsMenu_TickInput's
                             // selectEdge-into-g_optMenuOpen branch

// Cached kVanillaSettings indices belonging to the CURRENT tab (Controller doesn't
// use this -- it reads g_optRows directly). Rebuilt only when the tab changes, not
// every frame/tick. Sized generously above phase 1's actual max (Look: 4 editable
// rows) for room to grow as later phases add more tabs/rows.
int g_tabVanillaIndices[16];
int g_tabVanillaRowCount = 0;

void RebuildTabRowCache()
{
    g_tabVanillaRowCount = 0;
    UnifiedTab tab = kTabOrder[g_currentTabIndex];
    if (tab == UnifiedTab::Controller) return;
    VanillaSettingTab wantTab = (tab == UnifiedTab::Look) ? VanillaSettingTab::Look : VanillaSettingTab::Voice;
    for (int i = 0; i < kVanillaSettingCount; ++i) {
        const VanillaSettingDef& def = kVanillaSettings[i];
        if (def.tab != wantTab) continue;
        // Phase 1: only fully-editable kinds -- see the big comment above this section.
        if (def.kind != VanillaSettingKind::DvarFloat && def.kind != VanillaSettingKind::DvarBool) continue;
        if (g_tabVanillaRowCount < 16) g_tabVanillaIndices[g_tabVanillaRowCount++] = i;
    }
}

int CurrentTabRowCount()
{
    UnifiedTab tab = kTabOrder[g_currentTabIndex];
    return (tab == UnifiedTab::Controller) ? kOptRowCount : g_tabVanillaRowCount;
}

const char* CurrentTabRowLabel(int row)
{
    UnifiedTab tab = kTabOrder[g_currentTabIndex];
    if (tab == UnifiedTab::Controller) return g_optRows[row].label;
    return kVanillaSettings[g_tabVanillaIndices[row]].displayLabel;
}

void CurrentTabRowValueString(int row, char* outBuf, size_t outBufSize)
{
    UnifiedTab tab = kTabOrder[g_currentTabIndex];
    if (tab == UnifiedTab::Controller) { FormatOptRowValue(g_optRows[row], outBuf, outBufSize); return; }
    GetVanillaSettingValueString(kVanillaSettings[g_tabVanillaIndices[row]], outBuf, outBufSize);
}

bool CurrentTabRowIsBoolToggle(int row)
{
    UnifiedTab tab = kTabOrder[g_currentTabIndex];
    if (tab == UnifiedTab::Controller) return g_optRows[row].kind == OptRowKind::BoolToggle;
    return kVanillaSettings[g_tabVanillaIndices[row]].kind == VanillaSettingKind::DvarBool;
}

// Real-console-style one-line description for the currently focused row (2026-08-05
// restyle) -- drawn near the bottom of the settings panel, matching the real screen's
// own "Adjust your stick layout." convention. May be empty for rows this project
// hasn't written one for yet (VanillaSettingDef's own default-member-initializer
// leaves most non-phase-1 rows empty rather than guessed) -- caller skips drawing
// the line entirely when empty.
const char* CurrentTabRowDescription(int row)
{
    UnifiedTab tab = kTabOrder[g_currentTabIndex];
    if (tab == UnifiedTab::Controller) return g_optRows[row].description;
    return kVanillaSettings[g_tabVanillaIndices[row]].description;
}

bool CurrentTabRowIsStickLayout(int row)
{
    return kTabOrder[g_currentTabIndex] == UnifiedTab::Controller && g_optRows[row].kind == OptRowKind::StickLayoutEnum;
}

bool CurrentTabRowIsButtonLayout(int row)
{
    return kTabOrder[g_currentTabIndex] == UnifiedTab::Controller && g_optRows[row].kind == OptRowKind::ButtonLayoutEnum;
}

// direction: -1 (Left) or +1 (Right). Bool rows toggle regardless of direction.
void AdjustCurrentTabRow(int row, int direction)
{
    UnifiedTab tab = kTabOrder[g_currentTabIndex];
    if (tab == UnifiedTab::Controller) { AdjustOptRow(row, direction); return; }

    const VanillaSettingDef& def = kVanillaSettings[g_tabVanillaIndices[row]];
    char buf[64];
    GetVanillaSettingValueString(def, buf, sizeof(buf));
    if (def.kind == VanillaSettingKind::DvarFloat) {
        float v = static_cast<float>(atof(buf)) + static_cast<float>(direction) * def.floatStep;
        if (v < def.floatMin) v = def.floatMin;
        if (v > def.floatMax) v = def.floatMax;
        char newBuf[64];
        sprintf_s(newBuf, "%g", v);
        SetVanillaSettingFromString(def, newBuf);
    } else if (def.kind == VanillaSettingKind::DvarBool) {
        bool current = atoi(buf) != 0;
        SetVanillaSettingFromString(def, current ? "0" : "1");
    }
    // DvarString/Keybind rows are never reachable here -- RebuildTabRowCache already
    // filters them out of g_tabVanillaIndices for phase 1.
}

// FIXED 2026-08-04 (round 2 live feedback: "way too horizontally squished"): this
// drew the quad at only measuredWidth+12 screen pixels wide while sampling the FULL
// texture (default u0=0/u1=1, the whole kTextureWidth=512 canvas) -- since real
// rendered text only occupies a small leading slice of that 512px canvas, stretching
// the WHOLE thing into a narrow quad crushed every glyph horizontally by roughly
// (drawWidth / 512). DrawOneGameplayHintSlot/DrawOneMenuHintSlot (proven working
// since 2026-07-31) never had this bug because they always crop the UV range to the
// real rendered-text slice instead of defaulting to the full texture -- this now
// does the same: samples only [8, 8+drawWidthPx) of the 512px canvas (8 = the fixed
// left margin RenderMaskLuminance always draws with) so 1 texel of real glyph maps to
// 1 texel of screen space, same as everywhere else in this file.
void DrawOptLeftAlignedText(void* device, TextTexCache& cache, const char* text, float x, float yCenter,
                             int fontHeightPx, DWORD color, float scaleX, float scaleY)
{
    if (!EnsureLeftAlignedTextTexture(device, cache.texture, cache.renderedFor, sizeof(cache.renderedFor),
                                        text, cache.lastFontHeightPx, fontHeightPx)) return;
    int measuredWidth = MeasureTextWidthPx(text, g_modConfig.overlayFontItalic, fontHeightPx);
    if (measuredWidth <= 0) return;
    constexpr int kTextRenderLeftMarginPx = 8; // matches RenderMaskLuminance's own hardcoded left inset
    int drawWidthPx = measuredWidth + 12; // small trailing margin, same rationale
                                            // as the hint renderer's own kHintTextWidthMarginPx
    float u0 = static_cast<float>(kTextRenderLeftMarginPx) / static_cast<float>(kTextureWidth);
    float u1 = static_cast<float>(kTextRenderLeftMarginPx + drawWidthPx) / static_cast<float>(kTextureWidth);
    float top = yCenter - static_cast<float>(kTextureHeight) * 0.5f;
    DrawGenericTexturedQuad(device, cache.texture, x * scaleX, top * scaleY,
                              static_cast<float>(drawWidthPx) * scaleX, static_cast<float>(kTextureHeight) * scaleY,
                              color, u0, 0.0f, u1, 1.0f);
}

// Right-aligns `text` so it ENDS at rightEdgeX -- matching the real native vertical
// lists in this menu (OPTIONS_LIST included), which right-align every row to a shared
// column rather than left-aligning (see analog_input_hooks.cpp's own "KEY FINDING"
// comment on itemX=605). Pass 1 of this feature left-aligned the extra row STARTING
// at that same column, which put its text nowhere near where the real list's text
// actually sits -- a real, visible reason it read as un-native. Reuses
// DrawOptLeftAlignedText/MeasureTextWidthPx rather than a separate DT_RIGHT texture
// path -- text is still rendered left-aligned into its texture, just anchored in
// screen space by its own measured width.
void DrawOptRightAlignedText(void* device, TextTexCache& cache, const char* text, float rightEdgeX, float yCenter,
                               int fontHeightPx, DWORD color, float scaleX, float scaleY)
{
    int measuredWidth = MeasureTextWidthPx(text, g_modConfig.overlayFontItalic, fontHeightPx);
    float leftX = rightEdgeX - static_cast<float>(measuredWidth);
    DrawOptLeftAlignedText(device, cache, text, leftX, yCenter, fontHeightPx, color, scaleX, scaleY);
}

// ---- Stick/Button Layout drill-down controller diagram (2026-08-05 restyle) -------
//
// Real console reference screenshots (project owner-supplied) show Stick Layout and
// Button Layout as their own sub-screens: a vertical option list on the left, and a
// live controller render on the right with labeled leader lines that update per the
// highlighted preset. **2026-08-05, upgraded from a procedural circle/rect schematic
// to real controller-body reference photos** (project owner supplied one real,
// alpha-corrected product photo per supported GlyphStyle: Xbox 360, Xbox Modern,
// PlayStation -- `assets/controller_diagrams/`, embedded the same RCDATA way as the
// button glyphs). Leader lines are still pure axis-aligned elbow connectors (this
// renderer has no rotated-quad support), a deliberate simplification of the
// reference's diagonal lines. The label DATA is real, not approximated:
// GetStickLayoutAxisSources wraps this project's own live RouteStickAxes routing,
// and ResolveButtonMap is the same real function InjectControllerButtons itself
// resolves against -- both read directly rather than hand-duplicated, so this can
// never drift out of sync with actual gameplay behavior.
//
// Anchor positions are per-image FRACTIONS (0..1) of that image's own width/height,
// not absolute screen coordinates -- necessary since the three real photos have
// different aspect ratios and button layouts (PS4's DualShock puts both sticks in
// the lower half with D-pad/face-buttons above, unlike the Xbox family's
// upper-left-stick layout). Face-button fractions were derived by literally
// colorsampling each image for its real button colors (Xbox 360/Modern's solid-fill
// Y/X/B/A) or estimated visually where color-sampling wasn't reliable (PS4's thin
// line-art icons, and every controller's sticks/D-pad/shoulders, which are all the
// same dark tone as the body) -- flagged here as a first-pass estimate needing
// live/screenshot calibration, same standing convention as every other on-screen
// position in this feature's history.
struct ControllerDiagramLayout {
    const char* imageAssetName;
    float aspect; // image width / height -- used to size the drawn quad without distortion
    float lsX, lsY, rsX, rsY, dpadX, dpadY;
    float aX, aY, bX, bY, xX, xY, yX, yY;
    float lbX, lbY, rbX, rbY, ltX, ltY, rtX, rtY;
};

// Xbox 360 (600x415 source photo) -- face-button fractions from real color-sampled
// pixel centroids (A derived via diamond symmetry from the other three, which
// color-sampled cleanly); LS/RS/D-pad/shoulders are visual estimates.
constexpr ControllerDiagramLayout kDiagLayoutXbox360 = {
    "controller_body_xbox360", 600.0f / 415.0f,
    0.183f, 0.277f,  0.583f, 0.542f,  0.325f, 0.554f,
    0.840f, 0.369f,  0.877f, 0.260f,  0.718f, 0.270f,  0.755f, 0.161f,
    0.20f, 0.04f,    0.80f, 0.04f,    0.20f, 0.01f,    0.80f, 0.01f,
};
// Xbox Series/Modern (620x620 source photo) -- same derivation as above; face
// buttons color-sampled cleanly on all four (no symmetry estimate needed).
constexpr ControllerDiagramLayout kDiagLayoutXboxModern = {
    "controller_body_xboxmodern", 1.0f,
    0.242f, 0.355f,  0.629f, 0.532f,  0.315f, 0.532f,
    0.740f, 0.423f,  0.803f, 0.361f,  0.676f, 0.360f,  0.739f, 0.290f,
    0.20f, 0.14f,    0.80f, 0.14f,    0.20f, 0.09f,    0.80f, 0.09f,
};
// DualShock 4 / PS4 (447x447 source photo) -- structurally different layout from the
// Xbox family (both sticks sit in the LOWER half; D-pad and face buttons are upper).
// Field names stay generic (aX/bX/xX/yX etc, matching PhysicalInput's own naming) but
// hold each REAL PlayStation button's position at the same diamond direction as its
// Xbox counterpart (x=Square/west, y=Triangle/north, b=Circle/east, a=Cross/south --
// Sony's diamond is rotationally identical to Xbox's, just different symbols/colors,
// confirmed by literal color-sampling). No separate L2/R2 vs L1/R1 could be
// distinguished in this front-on photo -- LT/RT reuse LB/RB's own position.
constexpr ControllerDiagramLayout kDiagLayoutPS4 = {
    "controller_body_ps4", 1.0f,
    0.219f, 0.620f,  0.600f, 0.620f,  0.219f, 0.320f,
    0.772f, 0.450f,  0.839f, 0.371f,  0.694f, 0.371f,  0.772f, 0.293f,
    0.15f, 0.08f,    0.85f, 0.08f,    0.15f, 0.08f,    0.85f, 0.08f,
};

const ControllerDiagramLayout& GetDiagLayout(GlyphStyle style)
{
    switch (style) {
        case GlyphStyle::XboxModern:  return kDiagLayoutXboxModern;
        case GlyphStyle::PlayStation: return kDiagLayoutPS4;
        default:                      return kDiagLayoutXbox360;
    }
}

// Pure axis-aligned elbow leader line (horizontal segment at anchorY out to midX,
// then vertical segment from anchorY to labelY) -- this renderer has no rotated-quad
// support, so this is a deliberate simplification of the reference's diagonal lines.
void DrawDiagLeader(void* device, float anchorX, float anchorY, float midX, float labelY, float scaleX, float scaleY)
{
    constexpr float kThickness = 2.0f;
    constexpr DWORD kLineColor = 0x60FFFFFFu;
    float x0 = anchorX < midX ? anchorX : midX;
    float x1 = anchorX < midX ? midX : anchorX;
    DrawGenericTexturedQuad(device, g_optWhiteTexture, x0 * scaleX, (anchorY - kThickness * 0.5f) * scaleY,
                              (x1 - x0) * scaleX, kThickness * scaleY, kLineColor);
    float y0 = anchorY < labelY ? anchorY : labelY;
    float y1 = anchorY < labelY ? labelY : anchorY;
    DrawGenericTexturedQuad(device, g_optWhiteTexture, (midX - kThickness * 0.5f) * scaleX, y0 * scaleY,
                              kThickness * scaleX, (y1 - y0) * scaleY, kLineColor);
}

void DrawDiagLabel(void* device, int cacheIndex, const char* text, float anchorX, float anchorY,
                     float midX, float labelY, bool textOnRight, float scaleX, float scaleY)
{
    if (cacheIndex < 0 || cacheIndex >= 16) return;
    DrawDiagLeader(device, anchorX, anchorY, midX, labelY, scaleX, scaleY);
    constexpr int kLabelFontHeightPx = 20;
    constexpr DWORD kLabelColor = 0xFFE0E0E0u;
    if (textOnRight) {
        DrawOptLeftAlignedText(device, g_diagLabelCache[cacheIndex], text, midX + 12.0f, labelY, kLabelFontHeightPx, kLabelColor, scaleX, scaleY);
    } else {
        DrawOptRightAlignedText(device, g_diagLabelCache[cacheIndex], text, midX - 12.0f, labelY, kLabelFontHeightPx, kLabelColor, scaleX, scaleY);
    }
}

constexpr float kDiagBoxX = 900.0f, kDiagBoxY = 260.0f;       // top-left of the reserved diagram area, design space
constexpr float kDiagBoxMaxW = 900.0f, kDiagBoxMaxH = 560.0f; // available space the real image is fit into, preserving its own aspect ratio

// Draws the real controller-body photo for the player's current GlyphStyle, fit
// (letterboxed, not stretched) into the reserved diagram box. Returns the actual
// on-screen rect it was drawn into so the caller can convert the layout's own
// per-image fractional anchors into real screen coordinates.
bool DrawControllerBodyImage(void* device, float scaleX, float scaleY, const ControllerDiagramLayout& layout,
                               float& outX, float& outY, float& outW, float& outH)
{
    void* tex = nullptr; int texW = 0, texH = 0;
    if (!GetOrLoadGlyphIconTexture(device, layout.imageAssetName, tex, texW, texH) || texH <= 0) return false;
    float w = kDiagBoxMaxW, h = kDiagBoxMaxW / layout.aspect;
    if (h > kDiagBoxMaxH) { h = kDiagBoxMaxH; w = kDiagBoxMaxH * layout.aspect; }
    outX = kDiagBoxX + (kDiagBoxMaxW - w) * 0.5f;
    outY = kDiagBoxY + (kDiagBoxMaxH - h) * 0.5f;
    outW = w;
    outH = h;
    DrawGenericTexturedQuad(device, tex, outX * scaleX, outY * scaleY, w * scaleX, h * scaleY);
    return true;
}

// Both sticks always carry exactly 3 label lines each, for every StickLayout preset
// (a real, provable property of the routing: moveY/lookY are always on opposite
// sticks, and so are moveX/lookX, so each stick gets exactly one 2-line vertical
// group -- Forward/Back or Look Up/Down -- plus one 1-line horizontal group --
// Strafe or Rotate). This function draws whichever combination applies to ONE stick.
// isRightSide does double duty -- both "is this the physical RIGHT stick" (semantic,
// for the routing lookup) and "should the label extend rightward on screen"
// (visual) -- true for every one of this project's 3 real reference photos (RS
// always sits right-of-center, LS always left-of-center in all of them), so this is
// safe in practice, not just a coincidence of the original procedural layout.
void DrawOneStickLabels(void* device, float stickX, float stickY, bool isRightSide,
                          bool moveYFromRight, bool moveXFromRight, int& cacheIdx, float scaleX, float scaleY)
{
    bool verticalIsMove = (moveYFromRight == isRightSide);
    bool horizontalIsMove = (moveXFromRight == isRightSide);
    const char* topLabel = verticalIsMove ? "MOVE FORWARD" : "LOOK UP";
    const char* bottomLabel = verticalIsMove ? "MOVE BACK" : "LOOK DOWN";
    const char* midLabel = horizontalIsMove ? "STRAFE LEFT/RIGHT" : "ROTATE LEFT/RIGHT";
    float midX = isRightSide ? (stickX + 170.0f) : (stickX - 170.0f);
    DrawDiagLabel(device, cacheIdx++, topLabel,    stickX, stickY - 50.0f, midX, stickY - 90.0f, isRightSide, scaleX, scaleY);
    DrawDiagLabel(device, cacheIdx++, midLabel,    stickX, stickY,         midX, stickY,         isRightSide, scaleX, scaleY);
    DrawDiagLabel(device, cacheIdx++, bottomLabel, stickX, stickY + 50.0f, midX, stickY + 90.0f, isRightSide, scaleX, scaleY);
}

void DrawStickLayoutDiagram(void* device, float scaleX, float scaleY, StickLayout previewLayout)
{
    const ControllerDiagramLayout& layout = GetDiagLayout(g_modConfig.glyphStyle);
    float imgX = 0.0f, imgY = 0.0f, imgW = 0.0f, imgH = 0.0f;
    if (!DrawControllerBodyImage(device, scaleX, scaleY, layout, imgX, imgY, imgW, imgH)) return;

    float lsX = imgX + layout.lsX * imgW, lsY = imgY + layout.lsY * imgH;
    float rsX = imgX + layout.rsX * imgW, rsY = imgY + layout.rsY * imgH;

    bool moveXFromRight = false, moveYFromRight = false, lookXFromRight = false, lookYFromRight = false;
    GetStickLayoutAxisSources(previewLayout, moveXFromRight, moveYFromRight, lookXFromRight, lookYFromRight);
    int cacheIdx = 0;
    DrawOneStickLabels(device, lsX, lsY, false, moveYFromRight, moveXFromRight, cacheIdx, scaleX, scaleY);
    DrawOneStickLabels(device, rsX, rsY, true,  moveYFromRight, moveXFromRight, cacheIdx, scaleX, scaleY);
}

struct DiagAnchor { float x, y; bool onRight; };

// onRight reflects each REAL button's actual on-screen half in every one of this
// project's 3 reference photos (all four face buttons and RS sit right-of-center;
// LS/D-pad/LB/LT sit left-of-center) -- not an arbitrary per-button choice.
DiagAnchor GetDiagAnchorForInput(const ControllerDiagramLayout& layout, float imgX, float imgY, float imgW, float imgH, PhysicalInput input)
{
    switch (input) {
        case PhysicalInput::RT: return { imgX + layout.rtX * imgW, imgY + layout.rtY * imgH, true };
        case PhysicalInput::LT: return { imgX + layout.ltX * imgW, imgY + layout.ltY * imgH, false };
        case PhysicalInput::RB: return { imgX + layout.rbX * imgW, imgY + layout.rbY * imgH, true };
        case PhysicalInput::LB: return { imgX + layout.lbX * imgW, imgY + layout.lbY * imgH, false };
        case PhysicalInput::X:  return { imgX + layout.xX * imgW,  imgY + layout.xY * imgH,  true };
        case PhysicalInput::Y:  return { imgX + layout.yX * imgW,  imgY + layout.yY * imgH,  true };
        case PhysicalInput::A:  return { imgX + layout.aX * imgW,  imgY + layout.aY * imgH,  true };
        case PhysicalInput::B:  return { imgX + layout.bX * imgW,  imgY + layout.bY * imgH,  true };
        case PhysicalInput::LS: return { imgX + layout.lsX * imgW, imgY + layout.lsY * imgH, false };
        case PhysicalInput::RS: return { imgX + layout.rsX * imgW, imgY + layout.rsY * imgH, true };
        default: return { imgX + imgW * 0.5f, imgY + imgH * 0.5f, true };
    }
}

struct ButtonMapLabelEntry { PhysicalInput ButtonMap::* field; const char* label; };
constexpr ButtonMapLabelEntry kButtonMapLabels[] = {
    { &ButtonMap::fire,         "FIRE WEAPON" },
    { &ButtonMap::ads,          "AIM DOWN SIGHT" },
    { &ButtonMap::lethal,       "THROW FRAG" },
    { &ButtonMap::tactical,     "THROW TACTICAL" },
    { &ButtonMap::reloadUse,    "USE / RELOAD" },
    { &ButtonMap::weaponSwitch, "SWITCH WEAPON" },
    { &ButtonMap::jump,         "JUMP" },
    { &ButtonMap::crouchProne,  "CROUCH / PRONE" },
    { &ButtonMap::sprint,       "SPRINT / HOLD BREATH" },
    { &ButtonMap::melee,        "MELEE / ZOOM" },
};

void DrawButtonLayoutDiagram(void* device, float scaleX, float scaleY, ButtonLayout previewLayout)
{
    const ControllerDiagramLayout& layout = GetDiagLayout(g_modConfig.glyphStyle);
    float imgX = 0.0f, imgY = 0.0f, imgW = 0.0f, imgH = 0.0f;
    if (!DrawControllerBodyImage(device, scaleX, scaleY, layout, imgX, imgY, imgW, imgH)) return;

    ButtonMap bm = ResolveButtonMap(previewLayout, g_modConfig.flipTriggers);
    int cacheIdx = 0;
    for (const auto& entry : kButtonMapLabels) {
        DiagAnchor anchor = GetDiagAnchorForInput(layout, imgX, imgY, imgW, imgH, bm.*entry.field);
        float midX = anchor.onRight ? (anchor.x + 170.0f) : (anchor.x - 170.0f);
        DrawDiagLabel(device, cacheIdx++, entry.label, anchor.x, anchor.y, midX, anchor.y, anchor.onRight, scaleX, scaleY);
    }
    // D-pad isn't part of ButtonMap (its equipment/killstreak quick-select function
    // doesn't change between button layouts), so this one label is static.
    float dpadX = imgX + layout.dpadX * imgW, dpadY = imgY + layout.dpadY * imgH;
    DrawDiagLabel(device, cacheIdx++, "EQUIPMENT / KILLSTREAKS", dpadX, dpadY, dpadX - 170.0f, dpadY, false, scaleX, scaleY);
}

// Called from Hook_EndScene every frame. Draws the full custom menu when open --
// nothing to draw otherwise, invocation is now the real pause menu's own "Options"
// button (see overlay_hud.h's header comment), not an appended row that needed
// drawing even while "reachable but not yet open".
void DrawCustomOptionsMenuIfOpen(void* device)
{
    if (!g_optMenuOpen) return;
    if (!EnsureWhiteTexture(device)) return;

    float scaleX = 1.0f, scaleY = 1.0f;
    GetResolutionScale(device, scaleX, scaleY);

    // Mouse click support (2026-08-04, issue #66 follow-up: "our im game cursor to
    // be able to click entries too") -- hit-tested INLINE with layout/drawing below,
    // immediate-mode style, rather than storing rects for a separate input pass: the
    // exact same numbers that position a row/tab on screen this frame are what
    // click/hover get tested against, so there's no way for the two to drift apart
    // (and no one-frame lag either, unlike splitting layout and input into separate
    // per-frame hooks). Real screen-space mouse position, same coordinate space
    // DrawGenericTexturedQuad's own x/y*scale arguments already use.
    static bool s_lastLeftMouseHeld = false;
    bool leftMouseHeld = IsLeftMouseButtonHeld();
    bool leftClickEdge = leftMouseHeld && !s_lastLeftMouseHeld;
    s_lastLeftMouseHeld = leftMouseHeld;
    int mouseX = 0, mouseY = 0;
    bool haveMouse = GetLastMouseMoveClientPos(mouseX, mouseY);
    auto PointInRect = [&](float rectX, float rectY, float rectW, float rectH) {
        return haveMouse && mouseX >= rectX && mouseX < rectX + rectW
                          && mouseY >= rectY && mouseY < rectY + rectH;
    };

    constexpr DWORD kWhiteColor = 0xFFFFFFFFu;
    constexpr DWORD kDimTextColor = 0xFFC8C8C8u;
    constexpr DWORD kDescColor = 0xFFA0A0A0u;

    // Panel restyle (2026-08-05, real-console reference screenshots supplied by the
    // project owner). The real screen is NOT a full-screen dim with a floating
    // bordered panel (rounds 1-4's own design) -- it's a solid, edge-to-edge dark
    // panel covering roughly the left third of the screen, with the live (blurred by
    // whatever real pause postprocess the engine already applies, if any) paused game
    // visible directly past its right edge, no border decoration at all. This project
    // never suppresses the real screen underneath (draw-over-top architecture, see
    // this file's own history on why a suppression hook was tried and reverted), so
    // that view is already there for free -- no full-screen dim quad needed anymore.
    constexpr float kPanelX = 0.0f;
    constexpr float kPanelW = 672.0f; // ~35% of 1920 design width, matches the reference
    constexpr float kPanelH = 1080.0f;
    DrawGenericTexturedQuad(device, g_optWhiteTexture, kPanelX * scaleX, 0.0f,
        kPanelW * scaleX, kPanelH * scaleY, 0xFF232323u);

    constexpr int kTitleFontHeightPx = 44;
    constexpr int kTabFontHeightPx = 24;
    constexpr int kListFontHeightPx = 28;
    constexpr int kDescFontHeightPx = 20;
    constexpr float kListRowSpacingPx = 56.0f;
    constexpr float kIconReserveWidthPx = 44.0f; // room for the inline A-glyph after a focused row's label

    float titleY = 76.0f;
    float labelRightX = kPanelW - 32.0f - kIconReserveWidthPx;
    float valueX = kPanelW + 64.0f; // past the panel edge, over the live game view
    float tabY = 138.0f;
    float listTopY = 198.0f;
    float descY = kPanelH - 70.0f;

    const char* titleText = "OPTIONS";
    if (g_optDrilldownOpen) {
        titleText = CurrentTabRowIsStickLayout(g_optSelectedRow) ? "STICK LAYOUT" : "BUTTON LAYOUT";
    }
    DrawOptLeftAlignedText(device, g_optTitleCache, titleText, 56.0f, titleY, kTitleFontHeightPx, kWhiteColor, scaleX, scaleY);

    if (g_optDrilldownOpen) {
        bool isStick = CurrentTabRowIsStickLayout(g_optSelectedRow);
        static const char* kStickNames[4] = { "DEFAULT", "SOUTHPAW", "LEGACY", "LEGACY SOUTHPAW" };
        static const char* kButtonNames[4] = { "DEFAULT", "TACTICAL", "LEFTY", "TACTICAL LEFTY" };
        const char** names = isStick ? kStickNames : kButtonNames;
        constexpr int kOptionCount = 4;

        float rowY = listTopY + kListRowSpacingPx * 0.5f;
        for (int i = 0; i < kOptionCount; ++i) {
            float rowRectY = rowY - kListRowSpacingPx * 0.5f;
            bool hovered = PointInRect(kPanelX * scaleX, rowRectY * scaleY, kPanelW * scaleX, kListRowSpacingPx * scaleY);
            if (hovered) {
                g_optDrilldownSelectedRow = i;
                if (leftClickEdge) {
                    if (isStick) g_modConfig.stickLayout = static_cast<StickLayout>(i);
                    else g_modConfig.buttonLayout = static_cast<ButtonLayout>(i);
                    g_buttonMap = ResolveButtonMap(g_modConfig.buttonLayout, g_modConfig.flipTriggers);
                    SaveModConfig();
                }
            }
            bool selected = (i == g_optDrilldownSelectedRow);
            DWORD rowColor = selected ? kWhiteColor : kDimTextColor;
            if (selected) {
                DrawGradientQuad(device, g_optWhiteTexture, kPanelX * scaleX, rowRectY * scaleY,
                                   kPanelW * scaleX, kListRowSpacingPx * scaleY, 0x50FFFFFFu, 0x00FFFFFFu);
            }
            DrawOptRightAlignedText(device, g_optRowLabelCache[i], names[i], labelRightX, rowY, kListFontHeightPx, rowColor, scaleX, scaleY);
            if (selected) {
                const char* aAsset = GetControllerGlyphAssetName(PhysicalInput::A, g_modConfig.glyphStyle);
                if (aAsset && aAsset[0]) {
                    void* iconTex = nullptr; int iconW = 0, iconH = 0;
                    if (GetOrLoadGlyphIconTexture(device, aAsset, iconTex, iconW, iconH) && iconH > 0) {
                        float iconHpx = static_cast<float>(kListFontHeightPx);
                        float iconWpx = iconHpx * (static_cast<float>(iconW) / static_cast<float>(iconH));
                        DrawGenericTexturedQuad(device, iconTex, (labelRightX + 10.0f) * scaleX, (rowY - iconHpx * 0.5f) * scaleY,
                                                  iconWpx * scaleX, iconHpx * scaleY);
                    }
                }
            }
            rowY += kListRowSpacingPx;
        }

        if (isStick) {
            DrawStickLayoutDiagram(device, scaleX, scaleY, static_cast<StickLayout>(g_optDrilldownSelectedRow));
        } else {
            DrawButtonLayoutDiagram(device, scaleX, scaleY, static_cast<ButtonLayout>(g_optDrilldownSelectedRow));
        }
    } else {
        // Tab bar
        float tabX = 56.0f;
        constexpr float kTabGapPx = 44.0f;
        constexpr float kTabHitPadY = 12.0f;
        for (int t = 0; t < kUnifiedTabCount; ++t) {
            int tabWidth = MeasureTextWidthPx(UnifiedTabDisplayName(kTabOrder[t]), g_modConfig.overlayFontItalic, kTabFontHeightPx);
            bool hovered = PointInRect(tabX * scaleX, (tabY - kTabHitPadY) * scaleY,
                                         static_cast<float>(tabWidth) * scaleX, (static_cast<float>(kTabFontHeightPx) + kTabHitPadY * 2.0f) * scaleY);
            if (hovered && leftClickEdge && t != g_currentTabIndex) {
                g_currentTabIndex = t;
                RebuildTabRowCache();
                g_optSelectedRow = 0;
            }
            bool isCurrentTab = (t == g_currentTabIndex);
            DWORD tabColor = isCurrentTab ? kWhiteColor : (hovered ? 0xFFC0C0C0u : 0xFF808080u);
            DrawOptLeftAlignedText(device, g_tabBarCache[t], UnifiedTabDisplayName(kTabOrder[t]), tabX, tabY,
                                     kTabFontHeightPx, tabColor, scaleX, scaleY);
            tabX += static_cast<float>(tabWidth) + kTabGapPx;
        }
        DrawGenericTexturedQuad(device, g_optWhiteTexture, 56.0f * scaleX, (listTopY - 24.0f) * scaleY,
            (kPanelW - 112.0f) * scaleX, 2.0f * scaleY, 0x40FFFFFFu);

        int rowCount = CurrentTabRowCount();
        float rowY = listTopY + kListRowSpacingPx * 0.5f;
        for (int i = 0; i < rowCount; ++i) {
            float rowRectY = rowY - kListRowSpacingPx * 0.5f;

            // Hover moves selection (matches the real native menu's own onFocus-on-hover
            // convention, confirmed in pausedmenu.menu/all_restart_popmenu.menu) -- click
            // acts: opens the drill-down for Stick/Button Layout; toggles a bool row
            // outright; a float row is split into a left half (decrement) and right
            // half (increment), same step as Left/Right already use.
            bool hovered = PointInRect(kPanelX * scaleX, rowRectY * scaleY, kPanelW * scaleX, kListRowSpacingPx * scaleY);
            if (hovered) {
                g_optSelectedRow = i;
                if (leftClickEdge) {
                    if (CurrentTabRowIsStickLayout(i) || CurrentTabRowIsButtonLayout(i)) {
                        g_optDrilldownOpen = true;
                        g_optDrilldownSelectedRow = CurrentTabRowIsStickLayout(i)
                            ? static_cast<int>(g_modConfig.stickLayout) : static_cast<int>(g_modConfig.buttonLayout);
                    } else if (CurrentTabRowIsBoolToggle(i)) {
                        AdjustCurrentTabRow(i, +1);
                    } else {
                        float midX = (kPanelW * 0.5f) * scaleX;
                        AdjustCurrentTabRow(i, mouseX < midX ? -1 : +1);
                    }
                }
            }

            bool selected = (i == g_optSelectedRow);
            DWORD rowColor = selected ? kWhiteColor : kDimTextColor;
            if (selected) {
                // Real console's own gradient highlight bar -- bright near the panel's
                // left edge, fading toward the right (reference screenshot: the
                // highlighted row's own text reads relatively muted against the
                // brighter part of this same gradient, purely a contrast effect of the
                // bar itself, not a different text color).
                DrawGradientQuad(device, g_optWhiteTexture, kPanelX * scaleX, rowRectY * scaleY,
                                   kPanelW * scaleX, kListRowSpacingPx * scaleY, 0x50FFFFFFu, 0x00FFFFFFu);
            }
            DrawOptRightAlignedText(device, g_optRowLabelCache[i], CurrentTabRowLabel(i),
                                      labelRightX, rowY, kListFontHeightPx, rowColor, scaleX, scaleY);
            if (selected) {
                // Inline "press A" glyph -- real console convention: only the
                // currently focused row shows it, always the A button regardless of
                // what that row actually does (toggle a bool, adjust a value, or
                // drill into a Stick/Button Layout sub-screen).
                const char* aAsset = GetControllerGlyphAssetName(PhysicalInput::A, g_modConfig.glyphStyle);
                if (aAsset && aAsset[0]) {
                    void* iconTex = nullptr; int iconW = 0, iconH = 0;
                    if (GetOrLoadGlyphIconTexture(device, aAsset, iconTex, iconW, iconH) && iconH > 0) {
                        float iconHpx = static_cast<float>(kListFontHeightPx);
                        float iconWpx = iconHpx * (static_cast<float>(iconW) / static_cast<float>(iconH));
                        DrawGenericTexturedQuad(device, iconTex, (labelRightX + 10.0f) * scaleX, (rowY - iconHpx * 0.5f) * scaleY,
                                                  iconWpx * scaleX, iconHpx * scaleY);
                    }
                }
            }
            char valueBuf[64];
            CurrentTabRowValueString(i, valueBuf, sizeof(valueBuf));
            DrawOptLeftAlignedText(device, g_optRowValueCache[i], valueBuf,
                                     valueX, rowY, kListFontHeightPx, rowColor, scaleX, scaleY);
            rowY += kListRowSpacingPx;
        }

        const char* desc = CurrentTabRowDescription(g_optSelectedRow);
        if (desc && desc[0]) {
            DrawOptLeftAlignedText(device, g_optDescCache, desc, 56.0f, descY, kDescFontHeightPx, kDescColor, scaleX, scaleY);
        }
    }

    // Corner "Back" hint (bottom-right of the WHOLE SCREEN, not the panel) -- matches
    // the real console screen exactly: it shows ONLY this, nothing else (the inline
    // A-glyph above already replaces a separate "A action" legend entry, and this
    // screen has no mouse-hint concept on console either). Replaces rounds 1-5's own
    // full-width footer bar -- authentic to the reference at the cost of no longer
    // advertising LB/RB tab-switching or mouse-click support via an on-screen legend
    // (both still work, just not called out visually, same as the console itself
    // doesn't call out its own navigation).
    {
        const char* bAsset = GetControllerGlyphAssetName(PhysicalInput::B, g_modConfig.glyphStyle);
        constexpr int kCornerFontHeightPx = 24;
        constexpr float kCornerIconHeight = 26.0f;
        constexpr float kCornerMarginX = 60.0f, kCornerMarginY = 60.0f, kCornerGapPx = 10.0f;
        const char* backText = "Back";
        int textW = MeasureTextWidthPx(backText, g_modConfig.overlayFontItalic, kCornerFontHeightPx);
        float iconW = kCornerIconHeight;
        void* iconTex = nullptr; int iconTexW = 0, iconTexH = 0;
        bool haveIcon = bAsset && bAsset[0] && GetOrLoadGlyphIconTexture(device, bAsset, iconTex, iconTexW, iconTexH) && iconTexH > 0;
        if (haveIcon) iconW = kCornerIconHeight * (static_cast<float>(iconTexW) / static_cast<float>(iconTexH));
        float totalW = static_cast<float>(textW) + kCornerGapPx + iconW;
        float startX = 1920.0f - kCornerMarginX - totalW;
        float cornerY = 1080.0f - kCornerMarginY;
        DrawOptLeftAlignedText(device, g_optCornerBackCache, backText, startX, cornerY, kCornerFontHeightPx, kWhiteColor, scaleX, scaleY);
        if (haveIcon) {
            DrawGenericTexturedQuad(device, iconTex, (startX + textW + kCornerGapPx) * scaleX, (cornerY - kCornerIconHeight * 0.5f) * scaleY,
                                      iconW * scaleX, kCornerIconHeight * scaleY);
        }
    }

    // Click anywhere outside the panel closes the menu (common modal-dialog
    // convention, and the only way a mouse-only player could otherwise close this
    // screen at all -- B/Backspace is the controller/keyboard equivalent). Doesn't
    // apply while the drill-down is open -- clicking the game view there would be a
    // confusing double-purpose click (already used to preview/commit a list option).
    bool insidePanel = PointInRect(0.0f, 0.0f, kPanelW * scaleX, kPanelH * scaleY);
    if (leftClickEdge && !insidePanel && !g_optDrilldownOpen) {
        g_optMenuOpen = false;
    }
}

} // namespace

// ---- Public API (declared in overlay_hud.h) ----------------------------------------

bool CustomOptionsMenu_TickInput(bool openRequestedEdge,
                                   bool upEdge, bool downEdge, bool leftEdge, bool rightEdge,
                                   bool selectEdge, bool backEdge, bool tabPrevEdge, bool tabNextEdge)
{
    if (g_optMenuOpen && g_optDrilldownOpen) {
        // Stick/Button Layout drill-down (2026-08-05 restyle). Up/Down moves the
        // highlighted option AND commits it immediately -- same "no separate
        // confirm/apply step" convention this project already uses for these exact
        // two rows in the main list (Left/Right always applied instantly there too).
        // Back/Select both close back to the main list; committing again there is a
        // harmless no-op for controller nav (already committed by the Up/Down that
        // got here) and is what actually applies a mouse-hover-only preview that
        // never got clicked.
        bool isStick = CurrentTabRowIsStickLayout(g_optSelectedRow);
        auto commit = [&]() {
            if (isStick) g_modConfig.stickLayout = static_cast<StickLayout>(g_optDrilldownSelectedRow);
            else g_modConfig.buttonLayout = static_cast<ButtonLayout>(g_optDrilldownSelectedRow);
            g_buttonMap = ResolveButtonMap(g_modConfig.buttonLayout, g_modConfig.flipTriggers);
            SaveModConfig();
        };
        if (backEdge || selectEdge) {
            commit();
            g_optDrilldownOpen = false;
            return true;
        }
        if (upEdge) { g_optDrilldownSelectedRow = (g_optDrilldownSelectedRow - 1 + 4) % 4; commit(); }
        if (downEdge) { g_optDrilldownSelectedRow = (g_optDrilldownSelectedRow + 1) % 4; commit(); }
        return true;
    }

    if (g_optMenuOpen) {
        if (backEdge) {
            g_optMenuOpen = false;
            return true;
        }
        if (tabPrevEdge || tabNextEdge) {
            g_currentTabIndex = (g_currentTabIndex + (tabNextEdge ? 1 : -1) + kUnifiedTabCount) % kUnifiedTabCount;
            RebuildTabRowCache();
            g_optSelectedRow = 0; // switching tabs always lands on that tab's first row
        }
        int rowCount = CurrentTabRowCount();
        if (rowCount > 0) {
            if (upEdge) g_optSelectedRow = (g_optSelectedRow - 1 + rowCount) % rowCount;
            if (downEdge) g_optSelectedRow = (g_optSelectedRow + 1) % rowCount;
            if (leftEdge) AdjustCurrentTabRow(g_optSelectedRow, -1);
            if (rightEdge) AdjustCurrentTabRow(g_optSelectedRow, +1);
            if (selectEdge) {
                if (CurrentTabRowIsStickLayout(g_optSelectedRow) || CurrentTabRowIsButtonLayout(g_optSelectedRow)) {
                    g_optDrilldownOpen = true;
                    g_optDrilldownSelectedRow = CurrentTabRowIsStickLayout(g_optSelectedRow)
                        ? static_cast<int>(g_modConfig.stickLayout) : static_cast<int>(g_modConfig.buttonLayout);
                } else if (CurrentTabRowIsBoolToggle(g_optSelectedRow)) {
                    AdjustCurrentTabRow(g_optSelectedRow, +1); // A also toggles a bool row, same as Left/Right
                }
            }
        }
        return true; // claim everything while our own menu is open -- the real
                      // native menu underneath must see none of this input
    }

    // Gated on [Options] UseCustomOptionsScreen by the caller (analog_input_hooks.cpp
    // only ever passes openRequestedEdge=true when that's also enabled) -- default
    // OFF means this whole feature is invisible/inert unless explicitly enabled,
    // matching this project's standing pattern for structurally significant,
    // not-yet-verified changes.
    if (openRequestedEdge) {
        g_optMenuOpen = true;
        g_optDrilldownOpen = false;
        g_optSelectedRow = 0;
        g_currentTabIndex = 0; // always opens on the Controller tab
        RebuildTabRowCache();
        return true; // claim this press -- do NOT forward it to the real pause menu,
                      // which would otherwise run its own real "open Options" action
    }

    return false; // nothing claimed -- caller forwards this tick normally
}

void CustomOptionsMenu_ResetOnMenuClose()
{
    g_optMenuOpen = false;
    g_optDrilldownOpen = false;
    g_optSelectedRow = 0;
    g_currentTabIndex = 0;
}

bool CustomOptionsMenu_IsOpen()
{
    return g_optMenuOpen;
}

// See overlay_hud.h's own comment -- tools/ui_harness's public entry point onto the
// exact same DrawCustomOptionsMenuIfOpen the real game calls from Hook_EndScene.
void RunCustomOptionsMenuHarnessFrame(void* device)
{
    DrawCustomOptionsMenuIfOpen(device);
}

namespace {

void DrawCustomCursorIfNeeded(void* device)
{
    __try {
        constexpr uintptr_t kCursorVisibleFlagAddr = 0x01c00474;
        constexpr uintptr_t kCursorUiStateAddr = 0x01c0ad14;

        int visFlag = *reinterpret_cast<int*>(kCursorVisibleFlagAddr);
        // Live-reported 2026-08-02: with the input-method gating now working
        // correctly, a keyboard/mouse player sees the cursor persist through
        // ACTIVE gameplay (not just menus/pause) -- meaning the uiState exclusion
        // list below (0/6/10, guessed at issue #52's own original implementation)
        // never actually covered "ordinary live gameplay, no menu open" as its own
        // distinct state, and this was never solo-vs-co-op specific -- the
        // controller-priority hiding was just masking it for controller players the
        // whole time. Change-triggered logging (not gated on visFlag/uiState
        // passing, so it also logs the exact moment either one changes) to capture
        // real values during a live "cursor incorrectly showing mid-gameplay" repro
        // before guessing at another exclusion value.
        {
            static int s_lastLoggedVisFlag = -1;
            static int s_lastLoggedUiState = -1;
            if (visFlag != s_lastLoggedVisFlag || (visFlag != 0 && *reinterpret_cast<int*>(kCursorUiStateAddr) != s_lastLoggedUiState)) {
                s_lastLoggedVisFlag = visFlag;
                s_lastLoggedUiState = visFlag != 0 ? *reinterpret_cast<int*>(kCursorUiStateAddr) : s_lastLoggedUiState;
                char buf[96];
                sprintf_s(buf, "[cursor-gate-diag] visFlag=%d uiState=%d t=%lu", visFlag,
                          visFlag != 0 ? *reinterpret_cast<int*>(kCursorUiStateAddr) : -1, GetTickCount());
                LogFromController(buf);
            }
        }
        if (visFlag == 0) return;
        int uiState = *reinterpret_cast<int*>(kCursorUiStateAddr);
        if (uiState == 0 || uiState == 6 || uiState == 10) return;
        // Real fix (2026-08-02): a live capture showed uiState taking on several
        // values during ordinary active gameplay (1, 9 -- held 20+ seconds straight
        // -- and 2) that were never in the exclusion list above, and guessing more
        // magic numbers one at a time is a losing game. Require this project's own
        // already-proven-reliable "a real menu is open" signal instead (the same one
        // every corner hint/ESC-forward call already trusts) -- ordinary gameplay
        // reliably reads false here regardless of what uiState happens to be.
        if (!IsMenuActive_Exported()) return;

        // BUG-004 co-op report (2026-08-02): the native visFlag/uiState combo above
        // can consider the cursor "visible" during co-op-only states (e.g. co-op's own
        // nameplate display) even when the player is actively on a controller with no
        // real mouse/keyboard input at all -- something solo play never triggers.
        //
        // FIRST ATTEMPT (same day) compared GetLastMouseMoveTickMs() vs.
        // GetLastControllerActivityTickMs() and drew whichever was more recent --
        // live-reported to flicker constantly during actual gameplay. Root cause,
        // reasoned from the symptom rather than re-guessed: this native visFlag/
        // uiState pair is exactly the same state this project suspects drives real
        // mouse behavior natively (e.g. any native cursor-clamping/repositioning
        // while "visible"), so treating mouse-move recency as a proxy for "the user
        // touched the mouse" is contaminated by the SAME native cursor logic this
        // whole check exists to override -- a race between two signals where one
        // side can be artificially refreshed by the very state being tested was
        // never going to be stable.
        //
        // FIX: make controller activity a plain, one-sided override with a short
        // decay window, not a comparison. As long as the controller was used within
        // the last kRecentControllerActivityMs, hide the cursor outright, full stop
        // -- immune to whatever the mouse-move timestamp is doing. Only once the
        // controller has been silent for that whole window does this fall through
        // to the native flags again (covers genuine keyboard/mouse play).
        //
        // Live-reported same day, round 2: the cursor came back "too fast" after
        // using the controller -- this check alone only asked "has the controller
        // been quiet for a bit," it never actually required genuine mouse movement
        // to have happened, so a normal brief pause in stick/button input (routine
        // during real gameplay -- aiming without moving, a half-second decision
        // pause) was enough to bring it back with a stale mouse position. Fixed at
        // the source: WM_MOUSEMOVE now applies a real pixel deadzone
        // (d3d9_hook.cpp, kMouseMoveDeadzonePx) before ever updating
        // GetLastMouseMoveTickMs(), filtering out any native engine-driven
        // snap/clamp along with genuine sensor jitter -- so it's now safe to also
        // require the mouse timestamp to be newer than the controller's last real
        // touch, not just require silence.
        //
        // User-requested (2026-08-02): this exact decision is now shared with the
        // glyph-hint overlays too (analog_input_hooks.cpp's ShouldDrawGlyphOverlay)
        // -- console never shows a mouse cursor and button-prompt glyphs at once,
        // and neither should this project. Single shared function
        // (controller_input.cpp's IsControllerActiveInputMethod) so the two
        // systems can never disagree about which input method is active.
        if (IsControllerActiveInputMethod()) return;

        int mouseX = 0, mouseY = 0;
        if (!GetLastMouseMoveClientPos(mouseX, mouseY)) return;

        // Live-confirmed 2026-08-01 (two-point corner calibration: both cursors
        // overlap correctly at the top-left corner, but diverge by a consistent
        // ~1.33x -- exactly 4/3 -- toward the bottom-right): WM_MOUSEMOVE's lParam
        // is reported relative to the game WINDOW's own real client size, which is
        // NOT the same as the D3D9 device's actual render-target (viewport) size --
        // this engine renders to a smaller/different-sized backbuffer than the
        // window it's displayed in (an old stretch-blit-to-fill-the-window
        // technique). Two earlier theories were wrong: the internal DAT_01c00468/
        // 046c globals (unknown coordinate space) and a naive GetResolutionScale
        // multiply (that scale is always 1.0 when the viewport already matches
        // 1920x1080 -- it does nothing for THIS specific window-vs-viewport gap).
        // The correct fix: measure the real window client rect directly and scale
        // by (viewport size / window client size), not by any 1920x1080 assumption.
        HWND hwnd = GetGameWindow();
        RECT clientRect{};
        if (hwnd && GetClientRect(hwnd, &clientRect) &&
            (clientRect.right - clientRect.left) > 0 && (clientRect.bottom - clientRect.top) > 0) {
            int windowW = clientRect.right - clientRect.left;
            int windowH = clientRect.bottom - clientRect.top;
            int viewportW = 0, viewportH = 0;
            GetRealScreenSize(device, viewportW, viewportH);
            mouseX = static_cast<int>(mouseX * (static_cast<float>(viewportW) / static_cast<float>(windowW)));
            mouseY = static_cast<int>(mouseY * (static_cast<float>(viewportH) / static_cast<float>(windowH)));
        }
        POINT pt{ mouseX, mouseY };

        void* texture = nullptr;
        int texW = 0, texH = 0;
        if (!GetOrLoadGlyphIconTexture(device, "cursor_arrow", texture, texW, texH)) return;

        float scaleX = 1.0f, scaleY = 1.0f;
        GetResolutionScale(device, scaleX, scaleY);
        // Live-reported 2026-08-01: needed to be roughly 2.5x bigger than the first
        // attempt (32 design px). Re-cropped since then to pin the tip exactly at
        // (0,0) (see cursor_arrow.png's own generation notes) -- no longer square
        // (80x128 native), so scale by HEIGHT and derive width from the texture's
        // own real aspect ratio rather than stretching it back to square.
        constexpr float kCursorDesignHeight = 80.0f;
        float drawH = kCursorDesignHeight * scaleY;
        float drawW = texH > 0 ? drawH * (static_cast<float>(texW) / static_cast<float>(texH)) : drawH;
        // The source art's hotspot (the arrow's actual click point) sits at its own
        // top-left corner (confirmed during cropping -- the tip is right at the
        // image edge with only a few pixels of margin), so the real cursor position
        // maps directly to the quad's top-left with no centering offset needed.
        DrawGenericTexturedQuad(device, texture, static_cast<float>(pt.x), static_cast<float>(pt.y), drawW, drawH);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Never let a bad read of these fixed addresses (e.g. too early in boot,
        // before the real UI system has initialized them) crash the game.
    }
}

// ---- IDirect3DDevice9::Reset hook (2026-07-31, live-reported CRITICAL bug) --------
//
// User report: "changing display mode crashes the whole game." This project creates
// several textures of its own (the toast, hint prefix/suffix, cached glyph icons, the
// debug marker) via CreateTexture in D3DPOOL_MANAGED -- normally the D3D9 runtime
// keeps a system-memory backup of MANAGED resources and re-uploads them across a
// Reset() automatically, with no explicit handling required. This project never
// participated in that lifecycle at all before now (no Reset hook existed) -- purely
// as a defensive fix (not yet confirmed as the exact root cause, since reproducing
// and live-debugging the actual crash wasn't done first): release every texture this
// project owns immediately BEFORE the real Reset() runs, and let them lazily recreate
// on next use afterward (EnsureTextTexture/EnsureLeftAlignedTextTexture/
// GetOrLoadGlyphIconTexture/EnsureDebugMarkerTexture already all check for a null
// pointer and recreate on demand -- no new "on reset" rebuild path needed, just
// clearing the stale handles is enough). If this doesn't fully resolve the crash,
// the next step is a real live-debugger repro (x32dbg) to get the actual fault
// address rather than guessing further.
typedef HRESULT(WINAPI* Reset_t)(void* This, void* pPresentationParameters);
Reset_t g_origReset = nullptr;

void ReleaseAllCachedTextures()
{
    auto releaseIfSet = [](void*& tex) {
        if (!tex) return;
        void** vtbl = *reinterpret_cast<void***>(tex);
        reinterpret_cast<Release_t>(vtbl[kSurfaceReleaseVtableIndex])(tex);
        tex = nullptr;
    };
    releaseIfSet(g_textTexture);
    g_textureRenderedFor[0] = '\0';
    for (auto& slot : g_gameplayHintSlots) {
        releaseIfSet(slot.prefixTexture);
        slot.prefixRenderedFor[0] = '\0';
        releaseIfSet(slot.suffixTexture);
        slot.suffixRenderedFor[0] = '\0';
        releaseIfSet(slot.topLineTexture);
        slot.topLineRenderedFor[0] = '\0';
    }
    releaseIfSet(g_debugMarkerTexture);
    for (int i = 0; i < g_glyphIconCacheCount; ++i) {
        releaseIfSet(g_glyphIconCache[i].texture);
    }
    g_glyphIconCacheCount = 0;
    for (auto& slot : g_menuHintSlots) {
        releaseIfSet(slot.prefixTexture);
        slot.prefixRenderedFor[0] = '\0';
        releaseIfSet(slot.suffixTexture);
        slot.suffixRenderedFor[0] = '\0';
    }
}

HRESULT WINAPI Hook_Reset(void* device, void* pPresentationParameters)
{
    LogFromController("[overlay-hud] Reset() called -- releasing this project's own cached textures first");
    ReleaseAllCachedTextures();
    HRESULT hr = g_origReset(device, pPresentationParameters);
    char buf[96];
    sprintf_s(buf, "[overlay-hud] real Reset() returned hr=0x%08lX", hr);
    LogFromController(buf);
    return hr;
}

HRESULT WINAPI Hook_EndScene(void* device)
{
    ++g_endSceneFireCount;
    if (g_endSceneFireCount == 1) {
        LogFromController("[overlay-hud] EndScene hook fired for the first time -- confirmed alive");
    }
    // Custom Options menu drawn FIRST (2026-08-05 fix, live-reported: "all our in
    // game sprites like glyphs and cursor dont show up in the custom ui"). This used
    // to be drawn after every other overlay element below -- since its own
    // fullscreen dim layer + panel are large, near-opaque quads, drawing them LAST
    // painted them directly over every glyph icon/hint slot the earlier calls had
    // just drawn that same frame, hiding all of them. The real pause menu underneath
    // is never told to stop rendering (see overlay_hud.h's own comment on why), so
    // it keeps requesting its own real corner hints every frame regardless -- this
    // menu is meant to be a BACKGROUND those layer on top of, not the topmost thing.
    // The cursor already drew last before this fix and still does; it just wasn't
    // the only thing affected.
    DrawCustomOptionsMenuIfOpen(device);
    DrawOverlayMessage(device);
    DrawGlyphIconIfRequested(device);
    DrawGameplayHintSlotsIfRequested(device);
    InjectSyntheticBackHintIfNeeded();
    // ResetMenuListItemOrdinalForFrame() must run BEFORE DrawMenuHintsIfRequested()
    // (2026-08-03 fix): its own manual-position A-glyph block (issue #51) calls
    // RequestMenuHintOverlay() directly, populating a slot for THIS frame's draw
    // pass below -- calling it after DrawMenuHintsIfRequested() (the previous
    // order) meant every manual-position request only ever got drawn one frame
    // late, off the PREVIOUS frame's now-already-drawn slot array.
    ResetMenuListItemOrdinalForFrame();
    DrawMenuHintsIfRequested(device);
    DrawDebugMarkerIfRequested(device);
    // Always last -- see DrawCustomCursorIfNeeded's own comment for why the cursor
    // specifically needs to be the final thing drawn each frame.
    DrawCustomCursorIfNeeded(device);
    return g_origEndScene(device);
}

} // namespace

void GetRealScreenSize(void* deviceIn, int& outWidth, int& outHeight)
{
    outWidth = 1920;
    outHeight = 1080;
    bool gotViewport = false;
    if (deviceIn) {
        void** deviceVtbl = *reinterpret_cast<void***>(deviceIn);
        auto getViewport = reinterpret_cast<GetViewport_t>(deviceVtbl[kGetViewportVtableIndex]);
        D3DViewport9 vp = {};
        if (SUCCEEDED(getViewport(deviceIn, &vp)) && vp.Width > 0 && vp.Height > 0) {
            outWidth = static_cast<int>(vp.Width);
            outHeight = static_cast<int>(vp.Height);
            gotViewport = true;
        }
    }
    if (!gotViewport) {
        HWND hwnd = GetGameWindow();
        RECT clientRect;
        if (hwnd && GetClientRect(hwnd, &clientRect)) {
            outWidth = clientRect.right - clientRect.left;
            outHeight = clientRect.bottom - clientRect.top;
        }
    }

    // Live-reported 2026-07-31 (second round, 1440p): glyphs/text still landed wrong
    // even after scaling by the window's GetClientRect -- logged once per distinct
    // reading (not every frame) so a live repro's proxy_d3d9.log shows whether the real
    // viewport (ground truth for our own quads' coordinate space) ever actually
    // disagreed with the window's client rect, confirming or ruling out that theory.
    static int s_lastLoggedWidth = -1, s_lastLoggedHeight = -1;
    if (outWidth != s_lastLoggedWidth || outHeight != s_lastLoggedHeight) {
        s_lastLoggedWidth = outWidth;
        s_lastLoggedHeight = outHeight;
        char buf[160];
        sprintf_s(buf, "[overlay-hud][res-scale] real screen size=%dx%d (source=%s)",
                   outWidth, outHeight, gotViewport ? "GetViewport" : "GetClientRect-fallback");
        LogFromController(buf);
    }
}

void GetResolutionScale(void* deviceIn, float& outScaleX, float& outScaleY)
{
    int width = 1920, height = 1080;
    GetRealScreenSize(deviceIn, width, height);
    // REVERTED 2026-08-03: a same-day theory that this engine's real UI design
    // space is 720p (not 1080p) was tried here and confirmed WRONG live -- it
    // broke the cursor size and corner hints (both previously correct) and
    // still didn't fix the issue #51 manual-position list glyphs it was meant
    // to address. Back to the original, proven-correct 1920x1080 reference.
    // The manual-position glyphs' real problem is still open -- see issue #51.
    outScaleX = static_cast<float>(width) / 1920.0f;
    outScaleY = static_cast<float>(height) / 1080.0f;
}

void OnDeviceRecreated()
{
    LogFromController("[overlay-hud] device recreated (no Reset() call seen) -- releasing this project's own cached textures");
    ReleaseAllCachedTextures();
}

void InstallEndSceneHook(void* realDevice)
{
    if (!realDevice || g_origEndScene) return; // already installed -- one device for this game's lifetime
    void** deviceVtbl = *reinterpret_cast<void***>(realDevice);
    void* realEndScene = deviceVtbl[kEndSceneVtableIndex];

    MH_STATUS s = MH_CreateHook(realEndScene, reinterpret_cast<void*>(&Hook_EndScene),
                                 reinterpret_cast<void**>(&g_origEndScene));
    char buf[128];
    sprintf_s(buf, "[overlay-hud] MH_CreateHook(EndScene @ %p) = %d", realEndScene, static_cast<int>(s));
    LogFromController(buf);
    if (s == MH_OK) {
        MH_STATUS e = MH_EnableHook(realEndScene);
        sprintf_s(buf, "[overlay-hud] MH_EnableHook(EndScene) = %d", static_cast<int>(e));
        LogFromController(buf);
    }

    // See the big comment above Hook_Reset for why this exists (live-reported
    // CRITICAL crash on display-mode change).
    if (!g_origReset) {
        void* realReset = deviceVtbl[kResetVtableIndex];
        MH_STATUS rs = MH_CreateHook(realReset, reinterpret_cast<void*>(&Hook_Reset),
                                      reinterpret_cast<void**>(&g_origReset));
        sprintf_s(buf, "[overlay-hud] MH_CreateHook(Reset @ %p) = %d", realReset, static_cast<int>(rs));
        LogFromController(buf);
        if (rs == MH_OK) {
            MH_STATUS re = MH_EnableHook(realReset);
            sprintf_s(buf, "[overlay-hud] MH_EnableHook(Reset) = %d", static_cast<int>(re));
            LogFromController(buf);
        }
    }
}

namespace {
// Handles from AddFontMemResourceEx -- each represents this DLL's own private,
// in-process-only registration of one embedded .ttf. nullptr means "not loaded"
// (either LoadOverlayFonts was never called, or it failed and was logged).
HANDLE g_fontResourceRegular = nullptr;
HANDLE g_fontResourceItalic = nullptr;

bool LoadOneFontResource(HMODULE selfModule, int resourceId, HANDLE& outHandle, const char* label)
{
    HRSRC res = FindResourceA(selfModule, MAKEINTRESOURCEA(resourceId), RT_RCDATA);
    if (!res) {
        char buf[192];
        sprintf_s(buf, "[overlay-font] FindResourceA failed for %s (id %d) -- GetLastError=%lu",
            label, resourceId, GetLastError());
        LogFromController(buf);
        return false;
    }
    HGLOBAL loaded = LoadResource(selfModule, res);
    void* data = loaded ? LockResource(loaded) : nullptr;
    DWORD size = SizeofResource(selfModule, res);
    if (!data || size == 0) {
        char buf[192];
        sprintf_s(buf, "[overlay-font] LoadResource/LockResource failed for %s -- GetLastError=%lu",
            label, GetLastError());
        LogFromController(buf);
        return false;
    }
    DWORD numFontsAdded = 0;
    HANDLE fontHandle = AddFontMemResourceEx(data, size, nullptr, &numFontsAdded);
    if (!fontHandle || numFontsAdded == 0) {
        char buf[192];
        sprintf_s(buf, "[overlay-font] AddFontMemResourceEx failed for %s (%lu bytes) -- GetLastError=%lu",
            label, size, GetLastError());
        LogFromController(buf);
        return false;
    }
    outHandle = fontHandle;
    char buf[192];
    sprintf_s(buf, "[overlay-font] %s loaded as a private in-process font (%lu bytes, %lu face(s))",
        label, size, numFontsAdded);
    LogFromController(buf);
    return true;
}
} // namespace

bool LoadOverlayFonts(void* selfModuleHandle)
{
    HMODULE selfModule = static_cast<HMODULE>(selfModuleHandle);
    g_selfModule = selfModule; // also needed by the glyph-icon loader (issue #48)
    bool okRegular = LoadOneFontResource(selfModule, IDR_FONT_BARLOWCONDENSED_SEMIBOLD,
        g_fontResourceRegular, "Barlow Condensed SemiBold");
    bool okItalic = LoadOneFontResource(selfModule, IDR_FONT_BARLOWCONDENSED_SEMIBOLD_ITALIC,
        g_fontResourceItalic, "Barlow Condensed SemiBold Italic");
    return okRegular && okItalic;
}

void UnloadOverlayFonts()
{
    if (g_fontResourceRegular) {
        RemoveFontMemResourceEx(g_fontResourceRegular);
        g_fontResourceRegular = nullptr;
    }
    if (g_fontResourceItalic) {
        RemoveFontMemResourceEx(g_fontResourceItalic);
        g_fontResourceItalic = nullptr;
    }
}

void ShowOverlayMessage(const char* text, unsigned long durationMs, OverlayAnimStyle style)
{
    strncpy_s(g_overlayText, text, _TRUNCATE);
    g_overlayStartMs = GetTickCount();
    g_overlayDurationMs = durationMs;
    g_overlayStyle = style;
    g_overlayActive = true;
}

void RequestGlyphIconOverlay(float x, float y, float w, float h, const char* assetName)
{
    strncpy_s(g_pendingIconAssetName, assetName, _TRUNCATE);
    g_pendingIconX = x;
    g_pendingIconY = y;
    g_pendingIconW = w;
    g_pendingIconH = h;
    g_pendingIconRequestedThisFrame = true;
}

void RequestCustomHintOverlay(float x, float y, const char* prefixText, const char* suffixText,
                               const char* assetName, bool centerOnScreen, bool flashIcon,
                               GameplayHintSlotId slotId, const char* topLineText)
{
    GameplayHintSlot& slot = g_gameplayHintSlots[static_cast<int>(slotId)];
    strncpy_s(slot.prefixText, prefixText, _TRUNCATE);
    strncpy_s(slot.suffixText, suffixText, _TRUNCATE);
    strncpy_s(slot.assetName, assetName, _TRUNCATE);
    slot.x = x;
    slot.y = y;
    slot.centerOnScreen = centerOnScreen;
    slot.flashIcon = flashIcon;
    strncpy_s(slot.topLineText, topLineText, _TRUNCATE);
    slot.requestedThisFrame = true;
}

void AppendCustomHintSuffix(const char* extraText, GameplayHintSlotId slotId)
{
    GameplayHintSlot& slot = g_gameplayHintSlots[static_cast<int>(slotId)];
    if (!slot.requestedThisFrame) return;
    strncat_s(slot.suffixText, extraText, _TRUNCATE);
}

// Menu-hint counterpart to RequestCustomHintOverlay above -- see the big comment
// above g_menuHintSlots for why this is a small pool of slots (appended to, one per
// call THIS FRAME) rather than a single overwritten request. Silently drops the
// request if more than kMaxMenuHintSlots menu hints somehow fire in one frame --
// safe degradation (that hint just doesn't get its icon this frame) rather than a
// buffer overrun or a crash.
void RequestMenuHintOverlay(float x, float y, const char* prefixText, const char* suffixText,
                             const char* assetName)
{
    // Live-reported 2026-08-01: on a modal popup (e.g. "Choose Game Mode" over
    // Special Ops), the UNDERLYING screen's own corner hint ("Friends") kept
    // showing instead of the modal's own ("Back") -- "the game doesnt show it
    // [Friends], friends is meant to be behind the greyed out background." Root
    // cause: the underlying screen's hint call still fires every frame even while
    // a modal covers it (its owning screen doesn't know/care it's obscured) --
    // natively it gets painted over by the modal's own darkening overlay and
    // disappears, but this project's redraw happens at end-of-frame (EndScene),
    // after everything else, so it always ends up on top regardless of native
    // paint order. Fix: if a NEW request this frame lands at roughly the same
    // screen position as an EARLIER one this same frame (both hints share one
    // fixed corner-hint box, confirmed live: Friends/Back's own p2 values are only
    // 5px apart), OVERWRITE that earlier slot instead of adding a new one -- since
    // Hook_DrawGlyphText calls happen in the same order the game itself issues its
    // draw commands, "last call wins" for a shared position approximates native
    // paint order (the modal's own hint, drawn after the screen it covers,
    // naturally overwrites the obscured one). Genuinely different positions still
    // coexist as separate slots.
    constexpr float kSamePositionToleragePx = 20.0f;
    for (int i = 0; i < g_menuHintSlotCountThisFrame; ++i) {
        MenuHintSlot& existing = g_menuHintSlots[i];
        if (fabsf(existing.x - x) < kSamePositionToleragePx && fabsf(existing.y - y) < kSamePositionToleragePx) {
            strncpy_s(existing.prefixText, prefixText, _TRUNCATE);
            strncpy_s(existing.suffixText, suffixText, _TRUNCATE);
            strncpy_s(existing.assetName, assetName, _TRUNCATE);
            existing.x = x;
            existing.y = y;
            return;
        }
    }
    if (g_menuHintSlotCountThisFrame >= kMaxMenuHintSlots) return;
    MenuHintSlot& slot = g_menuHintSlots[g_menuHintSlotCountThisFrame++];
    strncpy_s(slot.prefixText, prefixText, _TRUNCATE);
    strncpy_s(slot.suffixText, suffixText, _TRUNCATE);
    strncpy_s(slot.assetName, assetName, _TRUNCATE);
    slot.x = x;
    slot.y = y;
}

void RequestDebugPositionMarker(int slot, float x, float y)
{
    if (slot < 0 || slot >= kMaxDebugMarkerSlots) return;
    g_pendingMarkerX[slot] = x;
    g_pendingMarkerY[slot] = y;
    g_pendingMarkerRequestedThisFrame[slot] = true;
}

namespace {
struct TestVariant { const char* text; OverlayAnimStyle style; };
const TestVariant kTestVariants[] = {
    { "MW32011NCP Started", OverlayAnimStyle::Plain },
    { "MW32011NCP Started - Thanks For Supporting The Project :P", OverlayAnimStyle::Plain },
    { "MW32011NCP Started", OverlayAnimStyle::Gold },
    { "MW32011NCP Started", OverlayAnimStyle::Rainbow },
    { "MW32011NCP Started", OverlayAnimStyle::Sweep },
};
constexpr int kTestVariantCount = sizeof(kTestVariants) / sizeof(kTestVariants[0]);
constexpr DWORD kTestVariantHoldMs = 4000; // long enough to actually look at each one

bool g_testCycleRunning = false;
int g_testCycleIndex = -1;
DWORD g_testCycleNextAdvanceMs = 0;
} // namespace

void TickOverlayTestCycle()
{
    if (!g_modConfig.overlayTestCycleAllVariants) {
        g_testCycleRunning = false; // config turned off mid-session -- clean restart if re-enabled
        return;
    }

    DWORD now = GetTickCount();
    if (g_testCycleRunning && now < g_testCycleNextAdvanceMs) return;

    g_testCycleRunning = true;
    g_testCycleIndex = (g_testCycleIndex + 1) % kTestVariantCount;
    const TestVariant& v = kTestVariants[g_testCycleIndex];
    // "Removing the timeout" per the user's own request: give this variant the FULL
    // cycle interval as its duration, so it's never cut short early -- the next
    // scheduled call simply overwrites it exactly when the interval elapses.
    ShowOverlayMessage(v.text, kTestVariantHoldMs, v.style);
    g_testCycleNextAdvanceMs = now + kTestVariantHoldMs;
}
