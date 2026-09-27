# The "~3.8-4GB ceiling" on x64: what it really is, and every real memory limit in the binary (2026-09-27)

Follow-up to `known_issues_x64.md` issue #4 (the "RAM and VRAM both sit flat
at ~3.8GB in Task Manager, even with every setting cranked" observation).
It extends that issue's CPU-pool work (10MB Hunk, 300MB zone reserve,
`sys_sysMB` 3072 cap) with a complete static sweep of every allocation path,
decompiled with angr (per-function, `.pdata`-bounded) against the current
x64 `iw5sp.exe` / `iw5mp.exe`. Addresses are VAs at the fixed image base
`0x140000000` (no `DYNAMIC_BASE`, see `binary_provenance_and_antidebug.md`).

## Bottom line

**There is no single 4GB allocator ceiling in either binary.** The flat
~3.8GB is the game's **fixed footprint for a fully loaded level**, not a
wall it runs into. Specifically:

- **VRAM** ≈ every loaded texture + render targets. IW5 loads a level's
  whole texture set at load time. There's no streaming and no higher mip
  tier to unlock, and "Extra" texture quality is already `picmip 0`, the
  shipped resolution. So no setting makes it load *more* texture data. The
  only VRAM that grows with settings is render targets
  (`InternalRenderScalePercent`, SMAA/FSR/DLSS buffers, all
  `D3DPOOL_DEFAULT`). The earlier DXGI capture already showed that
  (budget 10.26GB, usage 1.0-2.6GB, on an RTX 2080 Ti).
- **RAM** closely tracks texture VRAM because **every static texture is
  created in `D3DPOOL_MANAGED`** (see §1). A managed texture keeps a full
  system-memory copy:
  - on native D3D9, the runtime keeps it;
  - on the Vulkan path, DXVK keeps a persistent host-mapped backing
    buffer. `DXVK_USE_UNMAPPABLE_MEMORY` (the LRU eviction behind
    `d3d9.textureMemory`) is only compiled for 32-bit builds
    (`dxvk/src/util/util_unmap.h`), so on x64 the copy is never evicted.

  On top of that sit the zone pool (≤300MB), the small fixed pools, the
  image (≈64MB SP / 132MB MP `.data`), the CRT heap (audio, script, etc.)
  and driver/DXVK overhead.
- The earlier `ResourceUsageLogging` capture already showed
  **`privateBytesMB` peaking at 4857.7MB**, so the process does go past
  4GB when content needs it. It just doesn't need to in most levels.

In short, "cranking settings" can't raise memory use, because nothing
settings-driven loads more assets. The game only uses more memory if
there's more content: HD texture packs, bigger or custom zones, heavier
mods. **That's exactly where the real 32-bit-era limits below start to
matter.** Those limits are what "removing the ceiling" actually means.

## 1. Why RAM follows VRAM: textures are `D3DPOOL_MANAGED`

`Create2DTexture` (`FUN_1401b8e40`, the generic primitive every image
load goes through):

```c
pool  = 2;                                   // placeholder
usage = (flags >> 25) & 1 ? (fmt-75 <= 5 && ... ? 2 : 1) : (flags >> 15) & 0x200;
if (!((flags >> 26) & 1)) pool = !usage;     // usage==0 -> pool 1 = D3DPOOL_MANAGED
device->CreateTexture(w, h, levels, usage, fmt, pool, &tex, NULL);   // vtable +0xB8
```

Ordinary (usage 0) textures → `D3DPOOL_MANAGED`. Render targets and
dynamic textures → `D3DPOOL_DEFAULT` (VRAM only). Error path:
`"Create2DTexture( %s, %i, %i, %i, %i ) failed: %08x = %s"` → `Com_Error`.

## 2. Every real memory limit found (SP address / MP address)

| # | Limit | SP | MP | Value | On overflow | Blocks what |
|---|---|---|---|---|---|---|
| 1 | **Fastfile zone pool** (all XFile blocks of every loaded `.ff`) | init `FUN_1402c9130`, alloc `FUN_1402c8e30` | init `FUN_14032a2c0`, alloc `FUN_140329fc0` | `VirtualAlloc(0x12C00000, MEM_RESERVE)`: **298MB** main (`0x12A00000`) + **2MB** pool-3 at `base+0x12A00000` | `FUN_1402eeda0(7)` → `WIN_OUT_OF_MEM_TITLE/BODY` message box → `exit(-1)` | Bigger/extra zones, HD content packed in fastfiles |
| 2 | **Loose-image scratch** (`.iwi` from IWD/disk) | `FUN_1401ba830` (callers `FUN_1401ba910`, `FUN_1401baf37`, `FUN_1401bb02f`) | `FUN_1401e0470` | **26MB** (`0x1A00000`) reserved+committed once; stack-like bump, released after each load, so the limit is **per image** | `Com_Error(1, "Needed to allocate at least %.1f MB to load images")` → drop to menu | Any single loose texture >26MB (e.g. 8K DXT5 with mips ≈85MB, 4K RGBA8 = 64MB) |
| 3 | **VRAM detection** | `FUN_1401e7e10` (+ `FUN_1401e7fa0`, `FUN_1401e80e0`), cached max in `FUN_1401e7d90` | `FUN_14020cfe0` | `min(DirectDraw7::GetAvailableVidMem(DDSCAPS_LOCALVIDMEM)` (DWORD, rounded up to pow2, −16MB)`, IDirect3DDevice9::GetAvailableTextureMem()>>20` (UINT)`)` → **≤ ~4080MB** | none | Only consumer: auto texture quality (`FUN_1401b9980`/`FUN_1401b9d50`). Harmless on ≥4GB GPUs, but it's the literal "4GB" in the renderer |
| 4 | `sys_sysMB` detection | `FUN_1402ea710` | not re-traced this pass | clamped to **3072** | none | Auto-config tiers. **Already fixed** by `Hook_MemDetectFix` |
| 5 | Hunk | `FUN_1402be3d0` | `FUN_14031ca30` | **10MB** (`0xA00000`) | `Hunk_*` errors → fatal | Script/string/anim-script scratch only |
| 6 | Small reserves | `FUN_1402be1a0` 1.25MB; many 2KB-512KB temp buffers via `FUN_1402bd750` | `FUN_14031c800` 2MB | fixed | per-site | Not memory-relevant |
| 7 | **Fixed-count render arrays** (static `.data`) | strings `0x140422300…0x140422df0` | same set | `MAX_DRAWSURFS(16384)`, `MAX_SCENE_SURFS_SIZE(131072)`, `R_MAX_SKINNED_CACHE_VERTICES(1024*144)`, `FX_ELEM_LIMIT(2048)`, `GFX_MARK_SURF_LIMIT(1536)`, `MAX_ADDED_DLIGHTS(32)`, … | `"… exceeded - not drawing …"`: geometry **silently dropped** | Scenes pushed past vanilla density, e.g. by extended LOD distances |

### Texture-quality auto-select (`FUN_1401b9980` / `FUN_1401b9d50`)

This is where limits 3 and 4 are actually consumed:

- `r_picmip_manual` set → uses `r_picmip` (clamped ≤2), `r_picmip_bump`,
  `r_picmip_spec` as-is.
- Otherwise: detected VRAM MB ≥ threshold (`cfg+0x274`, copied from the
  renderer's hardware-config struct by `FUN_1401bc4f0`) → picmip 0, else 1.
- `ullTotalVirtual < 3000MB` → floor 1 on bump/spec (never true on x64).
- `sys_sysMB ≤ 1024` → floor **2** (quarter-res). `≤ 2048` on Win10+
  (`≤ 1400` on older OS, `FUN_1402ea210` returns the OS generation) →
  floor **1**. With `Hook_MemDetectFix` active, the floor never applies.
- If `g_141884c20` (spec-map enable) is off, `picmip_spec = 3`.

With manual "Extra" settings, none of this reduces quality today.

### Zone pool details (limit 1)

```c
// FUN_1402c9130 (SP) -- the ONLY writer of the pool globals
base = VirtualAlloc(NULL, 0x12C00000, MEM_RESERVE, PAGE_READWRITE);
g_zoneMain  = { .base = base,              .cap(+0x224) = 0x12A00000 };   // 0x142717ce0
g_zonePool3 = { .base = base + 0x12A00000, .cap(+0x224) = 0x00200000 };   // 0x142718110
```

- The block→pool map (`0x1404c29f8`) is `{0,1,0,0,0,0,1,1,2}` and the pool
  params (`0x1404c2a20`, {align, flags, kind}) are `{4096,4,1}`,
  `{4096,1028,1}`, `{4096,4,3}`. Kind `3` → the 2MB pool; everything else
  → the main pool. Low end: main thread. High end: per-loader-thread
  slots (`132*4` bytes apart), growing down.
- Overflow checks are **signed 32-bit** (`sub eax,[rsi+0x224]; test eax,eax;
  jle`), with 32-bit offsets added to the 64-bit base. **So the pool can be
  raised to at most just under 2GB (e.g. `0x7F000000`) without rewriting
  the allocator.**
- Other users: `FUN_1402c9020` (free, size-agnostic) and `FUN_1402c7993`
  (uses the pool address only as an end-of-buffer marker for a different
  array). Nothing else hardcodes the size, so replacing the init function
  resizes the pool cleanly.

## 3. How to remove each limit (production approach, none implemented yet)

All of these fit the proxy's existing model: signature-scanned, resolved
once at startup, MinHook, gated by an `mw3ncp_config.ini` key. None
of them trip the DRM, which doesn't checksum code (`NoModuleVerification`).

1. **Zone pool → up to ~2GB.** Hook the init (`FUN_1402c9130` /
   `FUN_14032a2c0`) with a full replacement: reserve
   `mainMB + 2MB`, write the two pool structs exactly as the original
   does (memset the same 540/0x200 byte ranges, set base/cap), and keep
   the 2MB pool at `base + main`. The init runs from `Com_Init`, long
   after the proxy's `DllMain`, so installing it at startup is in time.
   Config: `ZonePoolMB` (default 298 = vanilla, clamp 298..2032). Only
   reserved address space grows; pages are still committed on demand.
2. **Loose-image scratch → e.g. 256MB.** Replace `FUN_1401ba830` /
   `FUN_1401e0470` with the same bump logic and a configurable cap
   (`ImageScratchMB`, default 26). Note the original commits the whole
   buffer up front, so this *does* cost RAM. A reserve-then-commit-on-demand
   version costs nothing until a big image is loaded.
3. **VRAM detection → real 64-bit value.** Post-hook `FUN_1401e7e10` /
   `FUN_14020cfe0` to return
   `DXGI_ADAPTER_DESC.DedicatedVideoMemory >> 20` (this project already
   has `vram_diag.cpp`). This mainly makes auto quality correct and
   removes the last literal 4GB from the renderer.
4. **Hunk** only if a script-heavy mod ever hits `Hunk_Alloc* failed`.
   Same replace-the-init pattern (`FUN_1402be3d0`).
5. **Fixed-count render arrays** need the static arrays moved to heap
   memory and every reference repointed (dozens of sites each). It's a
   large project, only worth it if a live `"exceeded - not drawing"`
   counter shows these actually firing. Do that first.
6. **Diagnostics first (recommended before any of the above).** Add a
   rate-limited logger that reads the zone pool structs live
   (used-low, lowest high-slot, cap) plus hit counters on the
   `"exceeded"` warning sites. Then it's clear whether any real level
   comes close to limits 1, 2 or 7 today.

### Reducing RAM (the opposite lever)

The biggest RAM consumer is the managed-texture duplication. Removing it
means creating static textures in `D3DPOOL_DEFAULT` and uploading through
a `D3DPOOL_SYSTEMMEM` staging texture + `UpdateTexture`, instead of
`LockRect` on a managed texture. That changes the engine's upload path
and device-reset behaviour (DEFAULT resources must be recreated on
reset), so it's a separate, riskier project, noted here only for
completeness.

## Reproduce

Decompiles came from angr 9.2 (`pip install angr` in a venv) with a
`.pdata`-bounded per-function CFG; xrefs and argument recovery came from
`provenance_scripts/xref.py` and a capstone call-site scanner.
