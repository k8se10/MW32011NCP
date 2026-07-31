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
#include <cstring>
#include "../third_party/minhook/include/MinHook.h"
#include "mod_config.h"
#include "overlay_hud.h"

extern void LogFromController(const char* msg);
extern "C" HWND GetGameWindow(); // defined in d3d9_hook.cpp

namespace {

// ---- Vtable indices (stable D3D9 COM layout, see file header comment) -------------
constexpr int kEndSceneVtableIndex = 42;          // IDirect3DDevice9::EndScene
constexpr int kCreateTextureVtableIndex = 23;     // IDirect3DDevice9::CreateTexture
constexpr int kSetTextureVtableIndex = 65;        // IDirect3DDevice9::SetTexture
constexpr int kSetFVFVtableIndex = 89;            // IDirect3DDevice9::SetFVF
constexpr int kSetRenderStateVtableIndex = 57;    // IDirect3DDevice9::SetRenderState
constexpr int kGetRenderStateVtableIndex = 58;    // IDirect3DDevice9::GetRenderState
constexpr int kSetTextureStageStateVtableIndex = 67; // IDirect3DDevice9::SetTextureStageState
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

// Renders `text` into a fresh kTextureWidth x kTextureHeight top-down 32bpp memory
// bitmap (a plain GDI DC/DIB, never touches D3D9), drawing it once at each (dx,dy) in
// `offsets` in white on a black background, then extracts each pixel's own luminance
// (R=G=B, since ANTIALIASED_QUALITY -- grayscale AA -- avoids ClearType color
// fringing) into `outLuminance`. Used twice by RenderTextToArgbBuffer below: once
// with a 3x3 grid of +/-1px offsets to build the combined outline+fill coverage mask,
// and once with a single (0,0) offset to build the fill-only mask -- see that
// function's own comment for how the two masks combine into a real black-outlined,
// white-filled result.
bool RenderMaskLuminance(const char* text, const POINT* offsets, int offsetCount, bool italic, BYTE* outLuminance)
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

    // "Barlow Condensed" requested by face name only -- this project doesn't vendor/
    // privately-load the actual .ttf, so if it isn't installed system-wide, GDI
    // silently substitutes a default font instead of failing (documented limitation,
    // see mod_config.h's [Overlay] comment). GDI fakes an oblique slant for a
    // TrueType font with no dedicated italic style file, so the italic flag still
    // does something even without a real italic Barlow Condensed weight installed.
    HFONT font = CreateFontA(20, 0, 0, 0, FW_SEMIBOLD, italic ? TRUE : FALSE, FALSE, FALSE,
                              ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, DEFAULT_PITCH, "Barlow Condensed");
    HFONT oldFont = static_cast<HFONT>(SelectObject(memDC, font));
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(255, 255, 255));

    for (int i = 0; i < offsetCount; ++i) {
        RECT textRect = { 8 + offsets[i].x, offsets[i].y,
                           kTextureWidth - 8 + offsets[i].x, kTextureHeight + offsets[i].y };
        DrawTextA(memDC, text, -1, &textRect, DT_RIGHT | DT_SINGLELINE | DT_NOCLIP | DT_VCENTER);
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
bool RenderTextToArgbBuffer(const char* text, DWORD* outPixels)
{
    static BYTE outlineMask[kTextureWidth * kTextureHeight];
    static BYTE fillMask[kTextureWidth * kTextureHeight];

    const bool italic = g_modConfig.overlayFontItalic;

    const POINT kOutlineOffsets[9] = {
        { -1, -1 }, { 0, -1 }, { 1, -1 },
        { -1, 0 },  { 0, 0 },  { 1, 0 },
        { -1, 1 },  { 0, 1 },  { 1, 1 },
    };
    if (!RenderMaskLuminance(text, kOutlineOffsets, 9, italic, outlineMask)) return false;

    const POINT kFillOffset[1] = { { 0, 0 } };
    if (!RenderMaskLuminance(text, kFillOffset, 1, italic, fillMask)) return false;

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
    float top = 12.0f;
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

HRESULT WINAPI Hook_EndScene(void* device)
{
    ++g_endSceneFireCount;
    if (g_endSceneFireCount == 1) {
        LogFromController("[overlay-hud] EndScene hook fired for the first time -- confirmed alive");
    }
    DrawOverlayMessage(device);
    return g_origEndScene(device);
}

} // namespace

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
}

void ShowOverlayMessage(const char* text, unsigned long durationMs, OverlayAnimStyle style)
{
    strncpy_s(g_overlayText, text, _TRUNCATE);
    g_overlayStartMs = GetTickCount();
    g_overlayDurationMs = durationMs;
    g_overlayStyle = style;
    g_overlayActive = true;
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
