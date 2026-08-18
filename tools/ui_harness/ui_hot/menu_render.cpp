// menu_render.cpp -- see menu_render.h for the full scope/design comment.
#include "menu_render.h"
#include "overlay_hud.h" // MenuGfx_* forwarders, GetRealScreenSize
#include "menu_texture.h" // Phase 3 continuation -- real material/DDS backgrounds

#include <algorithm>
#include <cstdio>

namespace {

// Fixed logical canvas the real engine always computes itemDef/menuDef rects
// against, confirmed live this session (re_notes/iw5sp.md) -- independent of the
// player's actual real display resolution.
constexpr float kLogicalW = 1920.0f;
constexpr float kLogicalH = 1080.0f;

// Confirmed live (exact, 3/3 real examples matched with no rounding slop):
// screen = virtual * kUniformScale + margin[mode], both axes use the SAME scale.
constexpr float kUniformScale = 2.25f; // == 1080/480, i.e. always fit-to-1080-height
                                          // regardless of the true backbuffer size.

// Per-mode margins, in the fixed 1920x1080 logical space (see menu_render.h's own
// header comment). CONFIRMED (live capture, exact): horz 2/3, vert 1/3. DERIVED
// (analytically consistent with the confirmed values' own symmetric shape, not
// independently live-verified -- see this project's re_notes/iw5sp.md for the full
// account): horz 1 (mirrors horz 3's confirmed 144px edge padding onto the left
// edge), vert 2 (mirrors horz 2's confirmed "margin == half of the logical
// dimension" center-anchor shape onto the vertical axis: 1080/2). UNVERIFIED
// (modes 0/7/8/9/10 were not exercised by the live capture at all -- best-effort
// placeholders only, see this function's own comment below): mode 0 defaults to no
// margin (screen = virtual*2.25 exactly); modes 7/8/9/10 default to the same
// margin as the confirmed center mode (2), the least-wrong fallback for a
// genuinely unknown anchor (keeps unrecognized-mode items visible on screen rather
// than potentially off in a corner). If a real screen renders visibly wrong here,
// check whether it uses one of these unconfirmed modes before assuming a different
// bug.
float MarginForMode(int mode, bool horizontal)
{
    if (horizontal) {
        switch (mode) {
            case 1: return 144.0f;   // DERIVED -- left edge, mirrors mode 3's confirmed padding
            case 2: return 960.0f;   // CONFIRMED live
            case 3: return 1776.0f;  // CONFIRMED live
            case 0: return 0.0f;     // UNVERIFIED placeholder
            default: return 960.0f;  // UNVERIFIED placeholder -- modes 7/8/9/10, falls back to center
        }
    } else {
        switch (mode) {
            case 1: return 81.0f;    // CONFIRMED live
            case 2: return 540.0f;   // DERIVED -- center, mirrors horizontal mode 2's shape (1080/2)
            case 3: return 999.0f;   // CONFIRMED live
            case 0: return 0.0f;     // UNVERIFIED placeholder
            default: return 540.0f;  // UNVERIFIED placeholder -- modes 7/8/9/10, falls back to center
        }
    }
}

ParsedMenuFile g_activeMenu;
bool g_loaded = false;
MenuGameState g_gameState;

std::string ToLowerAsciiLocal(const std::string& s)
{
    std::string r = s;
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

// Builds the FunctionDefTable view menu_expr.h's evaluator needs (name -> raw Expr*)
// from the owning ParsedMenuFile's owned-pointer map -- once per DrawFrame call, not
// per item (functionDefs are file-scoped, never change mid-frame).
FunctionDefTable BuildFunctionDefTable(const ParsedMenuFile& file)
{
    FunctionDefTable table;
    for (const auto& kv : file.functionDefs) table[kv.first] = kv.second.get();
    return table;
}

// Uniform letterbox scale + centering offset from the fixed 1920x1080 logical
// canvas to whatever real window/backbuffer size the harness device currently has --
// mirrors the real engine's own separate later upscale step (see menu_render.h).
void ComputeLetterbox(void* device, float& outScale, float& outOffsetX, float& outOffsetY)
{
    int realW = 0, realH = 0;
    GetRealScreenSize(device, realW, realH);
    if (realW <= 0 || realH <= 0) { outScale = 1.0f; outOffsetX = 0.0f; outOffsetY = 0.0f; return; }
    float scaleX = static_cast<float>(realW) / kLogicalW;
    float scaleY = static_cast<float>(realH) / kLogicalH;
    outScale = std::min(scaleX, scaleY);
    outOffsetX = (static_cast<float>(realW) - kLogicalW * outScale) * 0.5f;
    outOffsetY = (static_cast<float>(realH) - kLogicalH * outScale) * 0.5f;
}

unsigned long PackArgb(const float rgba[4])
{
    auto clampByte = [](float v) -> unsigned long {
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        return static_cast<unsigned long>(v * 255.0f + 0.5f);
    };
    return (clampByte(rgba[3]) << 24) | (clampByte(rgba[0]) << 16) | (clampByte(rgba[1]) << 8) | clampByte(rgba[2]);
}

// Thin debug-outline color for items with no explicit backcolor -- Phase 1 has no
// material/asset loader (see menu_parser.h), so an item's real background art never
// renders; drawing SOME visible boundary is far more useful for glyph-position
// calibration (the actual motivating use case) than leaving unmodeled items
// invisible.
constexpr unsigned long kDebugOutlineColor = 0x60FFFF00u; // translucent yellow
constexpr float kDebugOutlineThicknessPx = 1.5f;

// Phase 2: applies `exp <field> <expr>;` overrides on top of a WORKING COPY of the
// item's static literal values -- never mutates the parsed item itself, so toggling
// MenuGameState (main.cpp's hotkeys) re-evaluates fresh next frame instead of baking
// in whatever fired first. Field/component mapping confirmed via the repo-wide `exp`
// grep documented in menu_parser.h/.cpp; "material" (maps to the real background/
// asset field) is evaluated for correctness/discoverability but has no visual effect
// yet -- Phase 1 has no material loader.
struct ItemWorkingValues
{
    float rectX, rectY, rectW, rectH;
    float forecolor[4];
    float backcolor[4];
    bool hasForecolor, hasBackcolor;
    std::string text;
    // Phase 3 continuation: real .menu files name the STATIC field "background" and
    // the DYNAMIC override field "material" for the same concept (confirmed: e.g.
    // literal `background "white"` on the static popup buttons vs. `exp material
    // tablelookup(...)` for a dynamically-resolved icon) -- both land here.
    std::string material;
};

void ApplyExpOverrides(const std::vector<MenuExpOverride>& overrides, ItemWorkingValues& v,
                        MenuGameState& gameState, const FunctionDefTable& funcs)
{
    for (const MenuExpOverride& ov : overrides) {
        Value result = EvaluateExpr(ov.expr.get(), gameState, funcs);
        std::string field = ToLowerAsciiLocal(ov.fieldName);
        if (field == "rect") {
            float num = static_cast<float>(result.AsNumber());
            if (ov.component == "x") v.rectX = num;
            else if (ov.component == "y") v.rectY = num;
            else if (ov.component == "w") v.rectW = num;
            else if (ov.component == "h") v.rectH = num;
        } else if (field == "forecolor") {
            float num = static_cast<float>(result.AsNumber());
            v.hasForecolor = true;
            if (ov.component == "r") v.forecolor[0] = num;
            else if (ov.component == "g") v.forecolor[1] = num;
            else if (ov.component == "b") v.forecolor[2] = num;
            else if (ov.component == "a") v.forecolor[3] = num;
        } else if (field == "backcolor") {
            float num = static_cast<float>(result.AsNumber());
            v.hasBackcolor = true;
            if (ov.component == "r") v.backcolor[0] = num;
            else if (ov.component == "g") v.backcolor[1] = num;
            else if (ov.component == "b") v.backcolor[2] = num;
            else if (ov.component == "a") v.backcolor[3] = num;
        } else if (field == "text") {
            v.text = result.AsString();
        } else if (field == "material") {
            v.material = result.AsString();
        }
        // every other unmodeled field name: evaluated above (via EvaluateExpr already
        // having been called), no further action -- see this function's own header
        // comment.
    }
}

void DrawItem(void* device, ParsedItemDef& item, float logicalMenuX, float logicalMenuY,
              float letterboxScale, float offsetX, float offsetY,
              MenuGameState& gameState, const FunctionDefTable& funcs)
{
    if (!item.hasRect) return;

    // Phase 2: visible gate, evaluated fresh every frame -- nullptr visibleExpr = no
    // `visible` field present = always visible (matches the real engine's own
    // default). This is THE mechanism that lets team-locked/unlock-gated real
    // Survival-armory itemDefs actually show/hide when MenuGameState changes.
    if (item.visibleExpr) {
        Value v = EvaluateExpr(item.visibleExpr.get(), gameState, funcs);
        if (!v.AsBool()) return;
    }

    ItemWorkingValues v{ item.rectX, item.rectY, item.rectW, item.rectH,
        { item.forecolor[0], item.forecolor[1], item.forecolor[2], item.forecolor[3] },
        { item.backcolor[0], item.backcolor[1], item.backcolor[2], item.backcolor[3] },
        item.hasForecolor, item.hasBackcolor, item.text, item.background };
    ApplyExpOverrides(item.expOverrides, v, gameState, funcs);

    // itemDef rects are relative to their owning menuDef's own already-transformed
    // origin in the real engine (menuDef rect establishes a local origin, itemDef
    // rects are offset from it) -- confirmed by the sampled files themselves (e.g.
    // stance.menu's itemDef sits at rect 0 0 -80 80, clearly relative to its
    // menuDef's own -84 -76 rect, not an absolute screen position). Phase 1 models
    // this as a simple additive virtual-space offset before the shared transform,
    // which matches every sampled file's shape; a menuDef-level anchor mode
    // interacting with itemDef-level anchor modes in some more complex way is
    // possible but not observed in the files read so far.
    float vx = logicalMenuX + v.rectX;
    float vy = logicalMenuY + v.rectY;

    float screenX, screenY, screenW, screenH;
    MenuRender_TransformRect(vx, vy, v.rectW, v.rectH, item.horzMode, item.vertMode,
                               screenX, screenY, screenW, screenH);

    float finalX = screenX * letterboxScale + offsetX;
    float finalY = screenY * letterboxScale + offsetY;
    float finalW = screenW * letterboxScale;
    float finalH = screenH * letterboxScale;

    // Phase 3 continuation (2026-08-17): real material/DDS background, replacing the
    // flat-color/outline placeholder wherever a real texture actually resolves --
    // direct response to live feedback that flat boxes don't serve the project's
    // actual goal (visual fidelity to the real game, not just correct positions/logic).
    // Falls back to the ORIGINAL flat-color/outline path below whenever the material
    // name is empty or fails to resolve (missing JSON/DDS/unsupported pixel format --
    // see menu_texture.h) -- never a hard failure, same degrade-gracefully standard as
    // every other unmodeled feature in this renderer.
    void* bgTexture = v.material.empty() ? nullptr : MenuTexture_LoadMaterialBackground(device, v.material);
    if (bgTexture) {
        // Tinted by forecolor when the item has one (matches this file's own
        // MenuGfx_DrawTexturedQuad contract, reusing the same diffuse-modulate blend
        // this codebase's existing glyph-icon drawing already uses elsewhere) --
        // otherwise fully-opaque white (no tint), the sensible default for a plain
        // background image with no explicit color override.
        unsigned long tint = v.hasForecolor ? PackArgb(v.forecolor) : 0xFFFFFFFFu;
        MenuGfx_DrawTexturedQuad(device, bgTexture, finalX, finalY, finalW, finalH, tint);
    } else if (v.hasBackcolor && v.backcolor[3] > 0.001f) {
        MenuGfx_DrawColorQuad(device, finalX, finalY, finalW, finalH, PackArgb(v.backcolor));
    } else if (finalW > 0.0f && finalH > 0.0f) {
        float t = kDebugOutlineThicknessPx;
        MenuGfx_DrawColorQuad(device, finalX, finalY, finalW, t, kDebugOutlineColor);
        MenuGfx_DrawColorQuad(device, finalX, finalY + finalH - t, finalW, t, kDebugOutlineColor);
        MenuGfx_DrawColorQuad(device, finalX, finalY, t, finalH, kDebugOutlineColor);
        MenuGfx_DrawColorQuad(device, finalX + finalW - t, finalY, t, finalH, kDebugOutlineColor);
    }

    if (!v.text.empty()) {
        // textScale's real semantic meaning (exact conversion to a pixel font size)
        // is NOT RE'd -- Phase 2/3 territory. This is a plain, clearly-approximate
        // heuristic (a baseline logical font height, scaled by the item's own
        // textScale and then by the same letterbox scale everything else uses) --
        // good enough to see WHERE text sits, not a claim of exact real font size.
        float logicalFontHeight = 20.0f * (item.textScale > 0.0f ? item.textScale : 1.0f);
        int fontHeightPx = static_cast<int>(logicalFontHeight * letterboxScale);
        if (fontHeightPx < 6) fontHeightPx = 6;
        unsigned long color = v.hasForecolor ? PackArgb(v.forecolor) : 0xFFFFFFFFu;
        float yCenter = finalY + finalH * 0.5f;
        MenuGfx_DrawLeftText(device, item.textTexture, item.textRenderedFor, sizeof(item.textRenderedFor),
                               item.textLastFontHeightPx, v.text.c_str(), finalX, yCenter,
                               fontHeightPx, color, 1.0f, 1.0f);
    }
}

} // namespace

void MenuRender_TransformRect(float virtualX, float virtualY, float virtualW, float virtualH,
                                int horzMode, int vertMode,
                                float& outScreenX, float& outScreenY, float& outScreenW, float& outScreenH)
{
    // Modes 4/6 (stretch-fill family, real engine rescales POSITION AND SIZE by an
    // alternate scale factor instead of the primary kUniformScale) are structurally
    // understood from static RE but were NOT exercised by the live capture at all --
    // no confirmed numeric alternate-scale value exists to use here. Falls back to
    // the same anchor+margin math every other mode uses rather than guessing a
    // second scale constant with zero live grounding; flagged here so a future pass
    // knows exactly why a mode-4/6 item's size might look wrong.
    float horzMargin = MarginForMode(horzMode, true);
    float vertMargin = MarginForMode(vertMode, false);

    outScreenX = virtualX * kUniformScale + horzMargin;
    outScreenY = virtualY * kUniformScale + vertMargin;
    outScreenW = virtualW * kUniformScale;
    outScreenH = virtualH * kUniformScale;
}

namespace {
std::string g_lastLoadedPath;
bool g_lastParseOk = false;

// Debug report (2026-08-17, Phase 1 verification aid; extended Phase 2 to also show
// EVALUATED visible/exp results) -- written next to the harness exe on every load AND
// on demand (MenuRender_RefreshDebugReport, wired to a hotkey in main.cpp) so parsed
// AND evaluated values (menu/item counts, rects, transformed screen rects, whether
// each item's `visible` currently passes, and what each `exp` override currently
// resolves to) can be checked against known real values or against a MenuGameState
// toggle's expected effect, without needing to inspect the D3D9 window directly.
void WriteDebugReport(const char* path, const ParsedMenuFile& parsed, bool parseOk, MenuGameState& gameState)
{
    FILE* f = nullptr;
    if (fopen_s(&f, "menu_parse_debug.txt", "w") != 0 || !f) return;
    FunctionDefTable funcs = BuildFunctionDefTable(parsed);
    fprintf(f, "source: %s\nparseOk: %d\nmenuCount: %zu\nfunctionDefCount: %zu\ngameState: team=%s usingMatchRulesData=%d fakeMillis=%.0f\n\n",
        path, parseOk ? 1 : 0, parsed.menus.size(), parsed.functionDefs.size(),
        gameState.teamName.c_str(), gameState.usingMatchRulesData ? 1 : 0, gameState.fakeMillisBase);
    for (size_t mi = 0; mi < parsed.menus.size(); ++mi) {
        const ParsedMenuDef& menu = parsed.menus[mi];
        bool menuVisible = true;
        if (menu.visibleExpr) menuVisible = EvaluateExpr(menu.visibleExpr.get(), gameState, funcs).AsBool();
        fprintf(f, "menu[%zu] name=\"%s\" hasRect=%d rect=(%.3f %.3f %.3f %.3f) horzMode=%d vertMode=%d "
                   "visible=%d(%s) itemCount=%zu\n",
            mi, menu.name.c_str(), menu.hasRect ? 1 : 0, menu.rectX, menu.rectY, menu.rectW, menu.rectH,
            menu.horzMode, menu.vertMode, menuVisible ? 1 : 0, menu.visibleExpr ? "evaluated" : "no visible field",
            menu.items.size());
        // Phase 2 origin already applies the menu's own exp rect x/y overrides (see
        // MenuRender_DrawFrame) -- mirror that here so the debug report's positions
        // match what actually renders, not just the static literal origin.
        float menuOriginVX = menu.rectX, menuOriginVY = menu.rectY;
        for (const MenuExpOverride& ov : menu.expOverrides) {
            Value result = EvaluateExpr(ov.expr.get(), gameState, funcs);
            std::string field = ToLowerAsciiLocal(ov.fieldName);
            if (field == "rect") {
                float num = static_cast<float>(result.AsNumber());
                if (ov.component == "x") menuOriginVX = num;
                else if (ov.component == "y") menuOriginVY = num;
            }
        }
        for (size_t ii = 0; ii < menu.items.size(); ++ii) {
            const ParsedItemDef& item = menu.items[ii];
            if (!item.hasRect) {
                fprintf(f, "  item[%zu] name=\"%s\" (no rect)\n", ii, item.name.c_str());
                continue;
            }
            bool itemVisible = true;
            if (item.visibleExpr) itemVisible = EvaluateExpr(item.visibleExpr.get(), gameState, funcs).AsBool();

            // Phase 3 fix: fold `exp` overrides into the logged rect/text too -- this
            // used to log only the pre-override STATIC values even though each
            // override's own evaluated result was printed separately below, which made
            // exp-rect-driven items (e.g. survival_armory_weapon.menu's WEAPON_POPUP_N
            // rows, positioned entirely via `exp rect y`) look mispositioned/collapsed
            // in the report despite rendering correctly (DrawItem already applied
            // overrides). Now matches exactly what DrawItem computes.
            ItemWorkingValues v{ item.rectX, item.rectY, item.rectW, item.rectH,
                { item.forecolor[0], item.forecolor[1], item.forecolor[2], item.forecolor[3] },
                { item.backcolor[0], item.backcolor[1], item.backcolor[2], item.backcolor[3] },
                item.hasForecolor, item.hasBackcolor, item.text };
            ApplyExpOverrides(item.expOverrides, v, gameState, funcs);

            float vx = menuOriginVX + v.rectX, vy = menuOriginVY + v.rectY;
            float sx, sy, sw, sh;
            MenuRender_TransformRect(vx, vy, v.rectW, v.rectH, item.horzMode, item.vertMode, sx, sy, sw, sh);
            fprintf(f, "  item[%zu] name=\"%s\" text=\"%s\" visible=%d(%s) virtualRect=(%.3f %.3f %.3f %.3f) horzMode=%d vertMode=%d "
                       "-> logicalScreenRect(1920x1080 canvas)=(%.3f %.3f %.3f %.3f)\n",
                ii, item.name.c_str(), v.text.c_str(), itemVisible ? 1 : 0, item.visibleExpr ? "evaluated" : "no visible field",
                vx, vy, v.rectW, v.rectH, item.horzMode, item.vertMode, sx, sy, sw, sh);
            for (const MenuExpOverride& ov : item.expOverrides) {
                Value result = EvaluateExpr(ov.expr.get(), gameState, funcs);
                fprintf(f, "    exp %s%s%s -> %s\n", ov.fieldName.c_str(),
                    ov.component.empty() ? "" : " ", ov.component.c_str(),
                    result.AsString().c_str());
            }
        }
        fprintf(f, "\n");
    }
    fclose(f);
}
} // namespace

bool MenuRender_LoadFile(const char* path)
{
    ParsedMenuFile parsed;
    bool ok = ParseMenuFile(path, parsed);
    g_lastLoadedPath = path ? path : "";
    g_lastParseOk = ok;
    WriteDebugReport(g_lastLoadedPath.c_str(), parsed, ok, g_gameState);
    if (!ok) {
        g_loaded = false;
        g_activeMenu.menus.clear();
        return false;
    }
    g_activeMenu = std::move(parsed);
    g_loaded = true;
    return true;
}

// Phase 2: re-dumps the debug report for the CURRENTLY loaded file against the
// CURRENT MenuGameState -- wired to a hotkey (main.cpp) so toggling e.g. team and
// re-dumping lets a `visible`/`exp` result that depends on team be checked from text
// output without needing a screenshot, per this pass' own verification requirement.
void MenuRender_RefreshDebugReport()
{
    if (g_lastLoadedPath.empty()) return;
    WriteDebugReport(g_lastLoadedPath.c_str(), g_activeMenu, g_lastParseOk, g_gameState);
}

bool MenuRender_IsLoaded()
{
    return g_loaded;
}

void MenuRender_DrawFrame(void* device)
{
    if (!g_loaded || !device) return;

    float letterboxScale = 1.0f, offsetX = 0.0f, offsetY = 0.0f;
    ComputeLetterbox(device, letterboxScale, offsetX, offsetY);

    // Built once per frame, not per item -- functionDefs are file-scoped and never
    // change mid-frame (see BuildFunctionDefTable's own comment).
    FunctionDefTable funcs = BuildFunctionDefTable(g_activeMenu);

    for (ParsedMenuDef& menu : g_activeMenu.menus) {
        // Phase 2: menuDef-level visible gate -- skip the WHOLE menu (all its items)
        // if its own `visible` expression evaluates falsy this frame.
        if (menu.visibleExpr) {
            Value v = EvaluateExpr(menu.visibleExpr.get(), g_gameState, funcs);
            if (!v.AsBool()) continue;
        }

        // The menuDef's OWN rect establishes the local origin every itemDef rect is
        // relative to (see DrawItem's own comment) -- transformed the same way, but
        // only its X/Y (not W/H, which real .menu files don't consistently use for
        // anything itemDef positioning depends on) matter for that offset. Phase 2:
        // menuDef-level `exp rect x/y <expr>;` overrides apply on top of the static
        // literal origin the same way DrawItem applies itemDef-level overrides.
        float menuOriginVX = menu.rectX, menuOriginVY = menu.rectY;
        if (!menu.expOverrides.empty()) {
            for (const MenuExpOverride& ov : menu.expOverrides) {
                Value result = EvaluateExpr(ov.expr.get(), g_gameState, funcs);
                std::string field = ToLowerAsciiLocal(ov.fieldName);
                if (field == "rect") {
                    float num = static_cast<float>(result.AsNumber());
                    if (ov.component == "x") menuOriginVX = num;
                    else if (ov.component == "y") menuOriginVY = num;
                }
            }
        }

        for (ParsedItemDef& item : menu.items) {
            DrawItem(device, item, menuOriginVX, menuOriginVY, letterboxScale, offsetX, offsetY,
                     g_gameState, funcs);
        }
    }
}

MenuGameState& MenuRender_GetGameState()
{
    return g_gameState;
}
