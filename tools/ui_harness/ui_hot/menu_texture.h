#pragma once

// menu_texture -- Phase 3 continuation (2026-08-17), real .menu material backgrounds.
// User feedback after seeing Phase 1/2's flat-color/outline rendering: "this isnt
// rendering like it does in game, thats kinda the WHOLE POINT" -- the actual long-term
// goal (user's own words, earlier this session) is pixel-accurate real .menu rendering,
// not just correct positions. This resolves an itemDef's `background "materialName"`
// field to a REAL D3D9 texture instead of the flat-color fallback.
//
// Real asset chain, confirmed this session:
//   D:\Tools\OpenAssetTools\zone_dump\ui\materials\<name>.json  (310 files)
//     -> "textures"[0]."image" names a file under
//   D:\Tools\OpenAssetTools\zone_dump\ui\images\<image>.dds     (301 files)
// e.g. materials/background_image.json's textures[0].image = "background_image" ->
// images/background_image.dds.
//
// DDS formats actually found in this asset set (both handled, nothing else is):
//   - Uncompressed DDPF_RGB, 24bpp (D3DFMT_R8G8B8) or 32bpp with/without an alpha mask
//     (D3DFMT_X8R8G8B8 / D3DFMT_A8R8G8B8) -- only the STANDARD RGB(A) channel-mask
//     layout is recognized; anything else is treated as unsupported (logged once,
//     nullptr returned) rather than guessing at a byte-swizzle.
//   - DDPF_FOURCC DXT1/DXT3/DXT5 -- D3D9 supports these natively via CreateTexture, so
//     the compressed block data is uploaded as-is (no decompression needed); the raw
//     4-byte FourCC read directly from the file IS the real D3DFMT_DXT1/3/5 numeric
//     constant (D3D9's own FourCC-format convention), so no string-to-enum mapping is
//     needed either.
//
// Lives under tools/ui_harness/ui_hot/ like the rest of this pass -- STL-permitted,
// never compiled into the real shipped proxy_d3d9.dll. Actual D3D9 texture
// creation/upload happens in overlay_hud.cpp's new MenuGfx_CreateTextureFromRawFormat
// (this file only parses the JSON/DDS bytes and hands over already-formatted rows) --
// keeps all raw D3D9 vtable interop inside overlay_hud.cpp, matching this project's own
// established boundary (see that function's own header comment).

#include <string>

// Resolves a real itemDef `background` material name to a loaded, cached D3D9
// texture. Returns nullptr (cached, so a given material name is only ever attempted
// once per process) if the material JSON, its referenced DDS, or the DDS's own pixel
// format can't be resolved -- callers (menu_render.cpp) fall back to the existing
// flat-color/outline rendering, never crash. `device` must be the same live D3D9
// device MenuGfx_* draw calls use.
void* MenuTexture_LoadMaterialBackground(void* device, const std::string& materialName);
