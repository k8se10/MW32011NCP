# Level asset-pool enumeration — map/level-load hijack research (2026-09-29)

**Status: Real proactive-preload trigger built and deployed, OPT-IN (config
default OFF). Not yet live-tested.**

Direct clarification of intent, after the read-only groundwork below shipped:
"the idea is thats the primary load mechanism and the others are used when
needed" — i.e. this table walk + proactive force-load should become how MOST
of a level's textures get queued for upscaling, with the existing reactive
paths (load-time capture, viewport capture, bulk zone-file precache) filling
in whatever this misses, not the other way around. Supporting evidence from
the user's own play sessions: loading times are already measurably longer
since some assets started being cached, consistent with the game already
tolerating extra work during its own loading window.

## Why

Direct instruction: "i think we should re the games actual map loading + asset
loading mechanism at binary so we can hijack and reimplement upscaled textures
at level load directly!" — motivated by the texture-upscale-cache feature's
existing capture paths all being *reactive* (load-time capture, viewport
capture, bulk zone-file precache — see `texture_upscale_cache_research.md`),
never a real native "here is everything this level needs" hook.

## Finding 1 — GfxImage pixel data is lazily demand-loaded, not eager at level load

Decompiled `FUN_1401bae70`/`FUN_1401bb260` (thin wrappers around the already-
hooked `FUN_1401bae80`, differing only in which read-callback they pass —
`FUN_1402b4240` vs `FUN_1402b4180`, likely loose-file-read vs zone-file-read)
and their four real callers:

- `FUN_1401b93d0` → calls `FUN_1401bae70`. Called from `FUN_140092460`
  (material/technique setup — a real per-material-bind-use call, not a level-
  load step).
- `FUN_1401b9760` → calls `FUN_1401bae70`. Same shape.
- `FUN_1401b9310` → calls `FUN_1401bb260`. Confirmed part of the native
  **device-lost recovery** path (comment found: "No way to recover image ...
  from a lost device").
- `FUN_1401b9c60` → calls `FUN_1401bb260`. Same device-lost recovery path.

**Conclusion: there is no earlier native "load everything for this level" step
to hook instead of the already-hooked `FUN_1401bae80`.** Pixel data is demand-
loaded lazily at first real material-bind use. This closes off the "find an
earlier eager hook point" angle entirely.

## Finding 2 — a real, complete, walkable asset table DOES exist

A single, shared, fixed-size flat asset-slot array holds a real pointer to
EVERY currently-registered asset of EVERY type (populated at zone/level load,
regardless of whether that asset's own pixel/geometry/etc. data has actually
been demand-loaded yet):

- **Slot array**: `ImageBase + 0xc75740` (`DAT_140c75740`), **42000 slots ×
  0x18 (24) bytes each**.
- **Hash-bucket array**: `ImageBase + 0xc5cf00` (`DAT_140c5cf00`),
  **42000 × `ushort`**, indexes into the slot array via a Bernstein-style
  string hash (`hash = hash*31 + c`, mod 42000) computed from the asset's own
  lowercase name.

Confirmed via full decompile of:
- `FUN_1400a5950` — asset **lookup** (given `assetType`, `name` → walks the
  hash chain, returns the slot pointer or null).
- `FUN_1400a54c0` — asset **creation/insert** (allocates a new slot, links it
  into the same hash chain, calls the per-type "load" dispatcher).
- `FUN_14008e3c0`/`FUN_14008e3e0`/`FUN_14008e3f0` — per-assetType jump-table
  accessors operating on a slot's own `dataPtr` field (`(&PTR_LAB_1404c25a0)
  [assetType]`-style dispatch — matches the already-known assetType→name
  table at `0x1404c2430`, `texture_upscale_cache_research.md`'s third round).

### Slot layout (offsets confirmed via `FUN_1400a5950`)

```
+0x00  int    assetType    (0xa == image, kFindOrLoadAssetImageTypeX64)
+0x08  void*  dataPtr      (the real per-type asset pointer, e.g. GfxImage*)
+0x12  ushort hashChainNext
```

Slot 0 is the real native "null asset" sentinel (hash-chain terminator,
confirmed via `FUN_1400a5950`'s own `if (uVar2 == 0) return 0;` check) — never
a real populated asset.

### Addressing — fixed RVA from module base, NOT RIP-relative

Raw disassembly of `FUN_1400a5950` (`asset_lookup_disasm.txt`) shows:

```
1400a599f  LEA RDI,[0x140000000]           ; module's own preferred image base
1400a59b6  MOVZX ECX,word ptr [RDI + RAX*2 + 0xc5cf00]   ; hash table
1400a59c6  LEA RBX,[RDI + 0xc75740]                       ; slot array
```

`LEA RDI,[0x140000000]` is a real x64 base-relocation entry (a 64-bit absolute
immediate load of the module's *preferred* base) — the OS loader fixes this up
to the module's *actual* runtime load base if it's relocated (ASLR). This
means, at runtime, `RDI == GetModuleHandleA(nullptr)` and the two globals are
reachable as `ModuleBase + 0xc5cf00` / `ModuleBase + 0xc75740` — a fixed RVA,
not `SigScan::ResolveRipRelative`'s RIP-relative case (no `[rip+disp32]`
addressing is involved here at all).

## Implementation shipped (2026-09-29, read-only groundwork only)

`analog_input_hooks_x64.cpp`:
- `kAssetPoolLookupSignature` — `FUN_1400a5950`'s own real 35-byte prologue
  (`48 89 5C 24 08 ... 0F BE 0F`), entirely deterministic (no embedded
  absolute/relative addresses within that span — confirmed via
  `DumpSigBytes.java`'s own PC-relative flags, all landing at +0x23 onward).
  Raw bytes/derivation: `asset_lookup_sigbytes.txt`.
- `ResolveAssetPoolGlobalsX64()` — resolves the signature, validates it found,
  computes `g_assetHashTableX64`/`g_assetSlotArrayX64` from
  `GetModuleHandleA(nullptr)` + the two fixed RVAs above. Logs success/failure.
  Called once at DLL init (`dllmain.cpp`, SP-only, same gating as every other
  hook in this feature).
- `LogAssetPoolImageStatsX64()` — read-only diagnostic, walks all 42000 slots,
  counts populated slots and `assetType==0xa` (image) slots, logs a sample of
  raw `dataPtr` values (**not dereferenced** — this project has no confirmed
  `GfxImage` struct layout of its own, and guessing one wasn't worth the risk
  for a stats dump). Wired to fire on every real map-change transition
  (`overlay_hud.cpp`'s existing `[x64-map-diag]` `coop_mapName` detection,
  skipping the first/cold-init transition).

Build-verified (x64 Release, 0 errors), deployed to the live install.
**Not yet live-tested** — the next play session's log will show whether the
table resolves and reports real, sane population counts.

## The actual "hijack" step — built 2026-09-29, ships opt-in

### Finding 3 — the real native force-load trigger

`FUN_1401b9760(void* imageStructPtr)` (`__fastcall`, single argument) is the
real trigger: given a pointer to the same image-load struct `FUN_1401bae80`
itself operates on (name at `+0x20`, matching the already-confirmed offset),
it checks two real streaming/device-state flags
(`[ImageBase+0xc..ca8]+0x10` / a second byte global), and if they allow it,
calls straight into `FUN_1401bae70` — the already-hooked `FUN_1401bae80`
wrapper — meaning a call to this function flows through the existing
`Hook_ImageFileLoadX64`/`Hook_ReadBytesSubstitutionX64` capture pipeline
exactly like an organic demand-load would. Confirmed via full decompile
(`FUN_1401b9760` — see the earlier chain-decomp file) and disassembly
(`image_load_trigger_sigbytes.txt`).

Real, NOT-fully-understood risk, why this ships opt-in rather than on by
default: the function's own prologue unconditionally reads the struct's real
dword at `+0x10`, zeroes it, and at the end decrements a real shared global
counter (`DAT_14073d698`, via `FUN_14008ede0`) by that old value — apparent
"in-flight load" bookkeeping this project hasn't independently verified the
full semantics of. Calling this on an image already mid-load via some other
native path could plausibly desync that counter over a long session.
x64dbg was unavailable this session (MCP server connection refused), so this
could not be live-debugged before shipping.

### Mitigations shipped

- **Only ever queued if genuinely not yet loaded**: before queuing, the
  struct's own real decoded-data pointer (offset `+0x0` — the same field
  `FUN_1401b9760` itself checks post-load, `CMP qword ptr [RBX],0x0`) is
  checked; only null (never-loaded) slots get queued. Already-loaded images
  are never re-triggered.
- **Rate-limited**: a max of 2 preload calls per frame, drained from a
  malloc-backed queue populated once per real map change — a bad interaction
  surfaces as one logged event, not a frame-time cliff.
- **SEH-guarded** on both the table-walk dereference and the trigger call
  itself.
- **Opt-in**: `[Video] ProactiveLevelTexturePreload` in `mw3ncp_config.ini`,
  default `0`. The read-only table-walk/stats diagnostic keeps running
  regardless; this flag only gates the real queue/trigger side.

### Implementation

`analog_input_hooks_x64.cpp`:
- `kImageLoadTriggerSignature` / `ResolveImageLoadTriggerX64()` — resolves
  `FUN_1401b9760` via signature match (two real RIP-relative globals
  wildcarded, matching this file's own established convention).
- `QueueLevelImagePreloadX64()` — called once per real map-change transition
  (`overlay_hud.cpp`'s existing `[x64-map-diag]` detection); walks the same
  table `LogAssetPoolImageStatsX64` already walks, queuing not-yet-loaded
  image slots.
- `PumpLevelImagePreloadX64()` — called every frame; drains up to 2 queued
  entries, calling the real trigger function on each.

Build-verified (x64 Release, 0 errors), deployed to the live install.
**Not yet live-tested** — the next real session's `[x64-level-preload]` log
lines (queue counts, per-trigger fires, any caught exceptions) are what
determines whether this is safe to flip to default-on.

## Related

- `texture_upscale_cache_research.md` — the existing three reactive capture
  paths (load-time, viewport, bulk zone-file precache) this finding is meant
  to eventually add a fourth, proactive path alongside.
- `re_notes/known_issues_x64.md` issue #12 — the still-open native
  file-handle-table crash chain, worth cross-checking before any proactive
  native-load-triggering code is ever added.
