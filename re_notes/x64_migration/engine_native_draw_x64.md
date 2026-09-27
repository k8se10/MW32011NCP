# Drawing our own text and glyphs IN-ENGINE on x64 (not via the D3D9 overlay) — feasibility RE (2026-09-27)

**Question (user)**: is it now possible, in the x64 build, to draw text and
controller glyphs through the engine's own renderer instead of this
project's EndScene overlay?

**Answer: yes, for both.** Every engine-side piece needed exists, is
identified, and none of it needs new engine code. Text is a direct call.
Glyphs need one piece of our own: a runtime-cloned `Material` whose image
wraps our own texture, because the PC build ships no controller-button art
(`ui_assets.md`). The engine already has an inline-icon escape in text
(`^\x01`), so a glyph can even sit INSIDE native hint text, laid out by the
engine itself.

**Method**: static only. Headless Ghidra 11.4.2 full auto-analysis of the
user-supplied `iw5sp.exe` (5,625,400 bytes, PE32+, sha256 prefix
`a97d2bbc7e495e4c`), run in a Linux cloud session, plus Capstone scans for
call sites. The binary is the exact build the mod targets: all 9 runtime
signatures in `analog_input_hooks_x64.cpp` match exactly once in `.text`. A
SteamStub `.bind` section wraps the entry point, but `.text` is not
encrypted (entropy 6.56). Raw decompiles: `engine_draw_trace/`. Addresses
are RE coordinates for this build only; shipped code resolves by signature
(CLAUDE.md §5).

Confidence: **CONFIRMED** = read directly off this pass's decompile/
disassembly. **INFERRED** = matched to the known IW-engine function it
mirrors by shape, not independently proven.

---

## 1. Text — the engine's own `UI_DrawText`

| Address | Identity | Evidence |
|---|---|---|
| `FUN_14029a2b0` | `UI_DrawText(scrPlace, text, maxChars, font, x, y, horzAlign, vertAlign, scale, const float* color, style)` | CONFIRMED shape: calls the font-scale helper, `ScrPlace_ApplyRect`, floors x/y, then the text render command. Already hooked as `Hook_DrawTextX64`. **Its `color1`/`color2` hook params are `horzAlign`/`vertAlign`; `param_10` is the real `float[4]` color.** |
| `FUN_14008d020` | `ScrPlace_ApplyRect(scrPlace, &x, &y, &w, &h, horzAlign, vertAlign)` | CONFIRMED: 11-way align switch per axis on the `ScreenPlacement` floats. |
| `FUN_14008d8f0` | `ScrPlace_GetActivePlacement()` — no args | CONFIRMED (disassembly): returns `&0x140717330` or `&0x1407173a0` depending on the mode int at `0x14071747c`. Nearly every `UI_DrawText` caller passes its return value as `scrPlace`. |
| `FUN_140080840` → `FUN_1401d2520` → `FUN_1401d1790` | `R_AddCmdDrawText(text, maxChars, font, x, y, xScale, yScale, rotation, color, style, cursorPos, cursorLetter)` | CONFIRMED: appends render command `0x11` to the frontend command buffer (`DAT_141896b98`), text copied inline. |
| `FUN_1401b7cb0` | `R_RegisterFont(name, imageTrack)` = `DB_FindXAssetHeader(0x18 /*FONT*/, name)` | CONFIRMED. `UI_Init` (`FUN_14029b640`) keeps all nine UI fonts in globals: `DAT_142604f90` bigfont, `…f98` smallfont, `…fa0` consolefont, `…fa8` boldfont, `…fb0` normalfont, `…fb8` extrabigfont, `…fc0` objectivefont, `…fc8` hudbigfont, `…fd0` hudsmallfont. |

x64 `Font_s` (CONFIRMED from `R_TextWidth`'s reads, matches `iw5oat`'s
struct with 8-byte pointers): `fontName`@0, `pixelHeight`@8,
`glyphCount`@0xC, `material`@0x10, `glowMaterial`@0x18, `glyphs`@0x20,
`Glyph` stride 0x18.

**Calling it ourselves**: `UI_DrawText(ScrPlace_GetActivePlacement(), text,
0x7fffffff, font, x, y, horz, vert, scale, color, style)`. The one
requirement: it records a FRONTEND render command, so it must run inside the
engine's own frame build (between the renderer's begin/end frame, on the
thread that builds commands), not from our D3D9 EndScene hook. Natural call
sites: our existing `Hook_DrawTextX64` (already inside the draw phase), or a
hook on the HUD tick `FUN_140039f40` / menu paint (`ui_draw_pipeline_map.md`).

## 2. Pictures — `Material_RegisterHandle` and the handle-pic draws

| Address | Identity | Evidence |
|---|---|---|
| `FUN_1401c4ba0` | `Material_RegisterHandle(name, imageTrack)` = `DB_FindXAssetHeader(5 /*MATERIAL*/, name)`; empty name → default material `DAT_141887b58` | CONFIRMED. 123 callers; `UI_Init` registers `"white"`, `"ui_cursor"`, scrollbars, etc. through it. Returns a real 64-bit `Material*`. |
| `FUN_14028c2b0` | `UI_DrawHandlePic(scrPlace, x, y, w, h, horzAlign, vertAlign, const float* color, Material*)` — negative w/h flip the UVs | CONFIRMED. The native cursor draws through it (the 2026-09-16 cursor-suppression hook targets this function). 31 callers. |
| `FUN_140078540` | `UI_DrawStretchPic(scrPlace, x, y, w, h, horz, vert, s0, t0, s1, t1, color, material)` | CONFIRMED: `ScrPlace_ApplyRect` then `FUN_140078740`. |
| `FUN_140078740` → `FUN_1401d2090` | `R_AddCmdDrawStretchPic(x, y, w, h, s0, t0, s1, t1, color, material)` → command `0x09` | CONFIRMED. Falls back to the default material if the material's technique set lacks the 2D technique, or if flag bit `0x10` at material +0x59 is set, so a cloned UI material (same technique set) passes. `FUN_1401c4a60` applies atlas-animation UVs (material +0xA/+0xB). |
| `FUN_1401d1f30` | `R_AddCmdDrawQuadPic(verts[4], color, material)` → command `0x0e` | CONFIRMED shape. |

## 3. Inline icons inside engine text — the `^\x01` escape EXISTS on x64

| Address | Identity | Evidence |
|---|---|---|
| `FUN_140185450` | backend `RB_DrawText` glyph loop | CONFIRMED: on `'^'` followed by byte `1` or `2` it reads `p[1]` (width byte), `p[3]` (name length) and `p[4…]` (material name). It looks the material up via `FUN_1401c4b80`, draws it via `FUN_140188b90`, advances the pen by `((p[1] − 16) × font->pixelHeight + 16) / 32 × xScale`, and skips `p[3] + 4` bytes. |
| `FUN_1401c4b80` | icon material lookup = `DB_FindXAssetHeader(5, name)` | CONFIRMED. **Exactly one caller** (`0x140185b21`, inside the branch above), so hooking it affects nothing but inline icons. |
| `FUN_140188b90` | emits one textured quad into the 2D tess | CONFIRMED. |
| `FUN_1401b7ce0`, `FUN_1401b7e80` | line-wrap / text-extent helpers | CONFIRMED icon-aware (same byte layout, same width formula). |
| `FUN_1401b80f0` | `R_TextWidth` | CONFIRMED **NOT** icon-aware: only handles `^0`–`^;` color codes. It would count `^`, `\x01` and the name bytes as glyphs, so centered or right-aligned text containing an icon measures too wide. Needs a small hook if icons are used in non-left-aligned text. |

INFERRED from the IW-engine convention, not proven in this decompile: `p[2]`
is the height byte; `\x02` draws the icon horizontally flipped.

Escape format: `'^', 0x01, widthByte, heightByte, nameLen, name…` (no
terminator). `widthByte = 0x30` gives an icon one font-height wide.

## 4. Custom glyph art — cloning a Material around our own texture

| Address | Identity | Evidence |
|---|---|---|
| `FUN_1401dff70` | `R_SetSampler(ctx, sampler, samplerState, GfxImage*)` | CONFIRMED: `SetTexture(stage, *(IDirect3DBaseTexture9**)image)`, i.e. the D3D texture is at `GfxImage`+0. Bit 0 of `image+0x0B` (flags) selects `D3DSAMP_SRGBTEXTURE`. |

x64 `Material` (0x80 bytes, `techniqueSet`@0x60, `textureTable`@0x68,
`constantTable`@0x70, `stateBitsTable`@0x78, `textureCount`@0x5B area) is
already native-verified in `tools/iw5oat/src/Common/Game/IW5/IW5_Assets.h`.
`MaterialTextureDef` on x64: `nameHash`(4) `nameStart` `nameEnd`
`samplerState` `semantic` then the image pointer union @8, 16 bytes.

**Construction, all from existing engine data, nothing synthesized in a shader**:
1. `base = Material_RegisterHandle("white", 3)` (or another plain 2D UI material).
2. Allocate our own 0x80-byte `Material`, copy `*base`, set a new name
   (`info.name`, offset 0) such as `"ncp_btn_a"`.
3. Allocate our own `MaterialTextureDef` array, copied from
   `base->textureTable`, so the sampler `nameHash`/`samplerState` still match the
   technique.
4. Allocate our own `GfxImage`, copied from the base def's image, and write our
   own `IDirect3DTexture9*` (created on the game's device from
   `assets/button_glyphs/`) into +0. Clear/set flags bit 0 for sRGB as the
   source art requires.
5. Point the cloned def's image pointer at our `GfxImage`, and the cloned
   material's `textureTable` at our def array.

**Use**: pass the clone to `UI_DrawHandlePic` directly, or hook
`FUN_1401c4b80` to return the clone for names with our prefix, and write
`^\x01\x30\x30\x09ncp_btn_a` into hint text.

## 5. Open items / risks before implementing

- **Device lifetime**: our `IDirect3DTexture9` must be D3DPOOL_MANAGED (or
  recreated on Reset), and must be released after the engine stops
  referencing the clone. Under DXVK the same D3D9 path applies.
- **Which text-width function `UI_TextWidth` uses** (likely `FUN_1401b80f0`)
  is not yet traced. It matters only for centered/right-aligned text with icons.
- **The `\x02` flip and the `p[2]` height byte**: inferred, confirm with a live test.
- **Thread/phase**: calls must come from inside the frontend frame build.
  `Hook_DrawTextX64` is already there, and is the lowest-risk first call site.
- **Suggested first live test** (smallest possible step): from inside
  `Hook_DrawTextX64`, once per frame, call the real `UI_DrawText` with the
  smallfont global and a fixed string. That proves in-engine text with zero
  new structs. Then a cloned `"white"` material drawn through
  `UI_DrawHandlePic` with a solid-color texture. Then the inline `^\x01` icon.
