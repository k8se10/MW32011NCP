#pragma once

// menu_parser -- Phase 1 (2026-08-16) of the real .menu file renderer for
// tools/ui_harness. Parses the REAL MW3 .menu text format (as extracted by
// OpenAssetTools' Unlinker to D:\Tools\OpenAssetTools\zone_dump\ui\) -- brace-
// delimited menuDef/itemDef/functionDef blocks, keyword-value fields, an embedded
// C-like expression sub-language for dynamic fields (`exp <field> <expr>;`,
// functionDef's `value <expr>;`, `visible <expr-or-literal>`), and an imperative
// statement sub-language inside onOpen/onClose/onESC blocks.
//
// PHASE 1 SCOPE (deliberately, per explicit user-approved scope): STATIC layout
// only. functionDef blocks are tokenized-and-discarded (never evaluated).
// onOpen/onClose/onESC and any other brace-delimited field are consumed as a
// balanced-brace block and discarded (never executed). `exp <field> <expr>;`
// dynamic overrides are tokenized-and-discarded -- the field keeps whatever
// literal value it otherwise has (or a default). NONE of this is a parse
// failure -- a file full of functionDefs/onOpen scripts still parses cleanly,
// just without any of that logic taking effect. Expression EVALUATION (Phase 2)
// and table lookups (Phase 3) are explicitly out of scope here.
//
// This file (and menu_parser.cpp/menu_render.h/menu_render.cpp) lives entirely
// under tools/ui_harness/ui_hot/ -- NEVER compiled into the real shipped
// proxy_d3d9.dll (see ui_hot.vcxproj's own ItemGroup) -- so, unlike
// overlay_hud.cpp/.h (shared with the shipped mod, must stay STL-free/
// exception-free), this code is free to use the STL where it simplifies things.

#include <string>
#include <vector>
#include <map>
#include <cstdint>

#include "menu_expr.h"

// Phase 2 (2026-08-17): a single `exp <field> <expr>;` / menuDef-level `exp rect <axis>
// <expr>;` dynamic override. `component` is only meaningful for "rect"/"forecolor"/
// "backcolor"/"glowcolor" fields (one of "x"/"y"/"w"/"h" or "r"/"g"/"b"/"a", lowercased)
// -- confirmed via a repo-wide grep of every real `exp` line's field-path shape (see
// menu_parser.cpp's ParseExpOverride for the full account); every other field name
// (text/material/elementheight/textaligny/etc.) has an empty component and the
// expression's result applies to the whole field directly.
struct MenuExpOverride
{
    std::string fieldName;
    std::string component;
    ExprPtr expr;
};

// One parsed itemDef. Deliberately only models the fields Phase 1's renderer
// actually draws (rect/type/text/font/color/background-name) -- every other
// real field (decoration, ownerdraw, textstyle, group, action scripts, etc.) is
// tokenized correctly (so the parser never desyncs) but not stored, since
// nothing reads it yet. Add fields here as later phases need them.
struct ParsedItemDef
{
    std::string name;
    std::string type;         // itemDef "type" is a numeric code in real files (e.g. "8"); kept as
                               // the raw literal string here (Phase 1 doesn't interpret item types).
    std::string text;         // literal "text" field if present (may be a locstring key like "@SOMETHING",
                               // never evaluated in Phase 1 -- drawn as-is).
    std::string background;   // material/asset name, if present. Phase 1 has no material loader --
                               // rendered as a flat color quad instead (see menu_render.cpp).

    bool hasRect = false;
    float rectX = 0.0f, rectY = 0.0f, rectW = 0.0f, rectH = 0.0f;
    int horzMode = 0, vertMode = 0; // the real .menu file's trailing 2 rect numbers --
                                     // see re_notes/iw5sp.md's confirmed 11-value enum.

    bool hasForecolor = false;
    float forecolor[4] = { 1.0f, 1.0f, 1.0f, 1.0f }; // r g b a, 0..1

    bool hasBackcolor = false;
    float backcolor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    float textScale = 1.0f;   // real files use this as a font-size multiplier-ish value;
                               // Phase 1 uses it as a direct-ish pixel scale, see menu_render.cpp.

    // ---- Runtime render-cache state (Phase 1 renderer, owned per item) --------------
    // Matches MenuGfx_DrawLeftText's own caller-owned-state contract (overlay_hud.h) --
    // one persistent texture/renderedFor/lastFontHeightPx triplet per item, so its text
    // texture is only re-rendered when the text actually changes, not every frame.
    void* textTexture = nullptr;
    char textRenderedFor[256] = {};
    int textLastFontHeightPx = 0;

    // Phase 2: dynamic overrides and visibility, both evaluated fresh each frame by the
    // renderer (see menu_render.cpp) against the harness's current MenuGameState --
    // correctness over speed, these expression trees are small and this isn't a hot loop
    // by real-game standards. nullptr visibleExpr = no `visible` field present = always
    // visible (matches the real engine's own default).
    std::vector<MenuExpOverride> expOverrides;
    ExprPtr visibleExpr;
};

// One parsed menuDef. A single .menu file can (and often does) define more than
// one menuDef block (e.g. a main screen plus its popup variants) -- see
// ParsedMenuFile below.
struct ParsedMenuDef
{
    std::string name;

    bool hasRect = false;
    float rectX = 0.0f, rectY = 0.0f, rectW = 0.0f, rectH = 0.0f;
    int horzMode = 0, vertMode = 0;

    // Ordered, MUTABLE -- deliberately NOT baked into an immutable parse-then-render
    // single pass. Forward-looking requirement (long-term goal: splice this project's
    // own custom itemDefs into a parsed REAL menuDef before rendering, so the custom
    // Options-replacement screen can eventually become "real .menu + our own extra
    // itemDefs" instead of a from-scratch visual approximation) -- a caller can
    // `menu.items.push_back(customItemDef)` any time before calling the renderer, with
    // no parser changes needed. Do not refactor this into a fixed-size array or a
    // parse-time-only representation.
    std::vector<ParsedItemDef> items;

    // Phase 2: same shape/meaning as ParsedItemDef's own fields -- confirmed real usage
    // of `exp rect <axis> <expr>;` directly inside a menuDef body, not just itemDefs
    // (see menu_parser.cpp's ParseMenuDefBody), plus a menuDef-level `visible` gate.
    std::vector<MenuExpOverride> expOverrides;
    ExprPtr visibleExpr;
};

// A whole parsed .menu file -- may contain multiple menuDef blocks (see above).
struct ParsedMenuFile
{
    std::vector<ParsedMenuDef> menus;

    // Phase 2: `functionDef { name "FUNC_N" value <expr>; }` blocks, now actually parsed
    // and stored (Phase 1 parsed-and-discarded these) -- name -> owned expression AST.
    // Real files chain these (FUNC_15 calling FUNC_12()/FUNC_7()/etc.), so the WHOLE
    // table must exist before any expression referencing a FUNC_N is evaluated -- see
    // menu_render.cpp, which builds a FunctionDefTable view (menu_expr.h) from this map
    // once per loaded file, not per item.
    std::map<std::string, ExprPtr> functionDefs;
};

// Parses a real .menu file from disk (already-extracted OpenAssetTools text format,
// e.g. D:\Tools\OpenAssetTools\zone_dump\ui\scriptmenus\survival_armory_weapon.menu).
// Returns false (outFile left empty) on read failure or a genuinely malformed file
// (unbalanced braces at EOF) -- never throws, matches this project's own established
// "missing/bad input degrades gracefully" standard even though this specific file is
// STL-permitted (see this header's own top comment).
bool ParseMenuFile(const char* path, ParsedMenuFile& outFile);
