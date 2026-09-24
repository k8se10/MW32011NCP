# Native 3D renderer — current understanding (x64)

This is the **clean, current-state reference** for how a frame's worth of
game-world geometry actually reaches the GPU on the x64 build of
`iw5sp.exe`. It presents only what's actually established, organized by
architecture rather than by when it was discovered.

**For the full investigation trail** — dead ends, tooling problems, dated
rounds, the reasoning behind each finding — see
[`renderer_architecture_map.md`](renderer_architecture_map.md), the raw RE
log this document is distilled from. When the two disagree, the log is
more current; update this file in the same pass as any change there that
alters the architecture picture.

**Confidence key**: 🟢 CONFIRMED (read directly off a real decompile).
🟡 INFERRED (reasoned from shape/context, not independently proven).
🔴 OPEN (genuinely unknown).

**Scope note**: this covers the 3D scene renderer. The 2D/HUD/text draw
layer is mapped separately in
[`ui_draw_pipeline_map.md`](ui_draw_pipeline_map.md).

**Address caveat**: every address below is a coordinate against this
project's own tracked x64 binary snapshot
(`re_notes/x64_migration/binaries/iw5sp.exe`), for RE reference only. Per
the locked signature-scanning policy (`CLAUDE.md` §5/§10.3), never
hardcode one of these into shipped hook code — resolve a real signature at
runtime instead.

---

## 1. The big picture

The renderer is a classic id-Tech-lineage **frontend/backend split**, the
same shape Quake3/RTCW/CoD titles of this era are known for:

```
Main thread (per frame)                    Two dedicated worker threads
────────────────────────                   ─────────────────────────────
Top-level frame entry
  → per-player frame setup
    → camera/view/FOV math
    → fires 6 sequential "stage"          Worker threads drain a shared
      NOTIFY markers (physics,            stage-event ring buffer and do
      culling, shadow-caster              the REAL per-stage work: culling,
      gathering, ...)                     shadow-caster gathering, and
    → HUD tick                            "gen drawsurfs" (turning visible
                                           entities into draw commands)
Render-command ring buffer
  (queues typed 8-byte-aligned
  commands — frontend side only)
                                           EndScene fires on ITS OWN
                                           dedicated thread (not main),
                                           confirmed live 2026-09-24 —
                                           this is the real D3D9
                                           submission point.
```

The main thread's per-frame chain never issues a single D3D9 draw call
itself — it only queues work and fires coordination markers. The actual
scene setup (culling, shadow gathering, drawsurf generation) and the
actual D3D9 submission (`EndScene` and everything feeding it) happen on
separate, dedicated threads. This was the single biggest open question in
this whole investigation, and it's now closed: **yes, real threading
exists, confirmed by direct live observation, not inference.**

---

## 2. Per-frame CPU-side chain (main thread)

🟢 **`FUN_1401d83a0`** — the real per-frame top-level render entry.
Reached indirectly (a data reference, not a direct `CALL` — almost
certainly a function-pointer table entry in an outer frame orchestrator
not yet traced). Computes the per-view rectangle from real backbuffer
dimensions and split-screen state; the real consumer of the values
`InternalRenderScalePercent`'s own hook chain
(`Hook_RenderResCompute → FUN_1401bd1d0`) feeds. Calls three tiny helpers
(one data-linkage setter, one flag-setter, and the command-buffer
allocator — see §3) before ending in:

🟢 **`FUN_1401d7480`** — per-player frame setup. Real internal structure:
camera/view/FOV math (`FUN_1401d8f70`/`FUN_1401d9a10`/…), then the six
sequential stage-notify calls (§4), then ends in the already-fully-mapped
HUD tick (`FUN_140039f40`, see `ui_draw_pipeline_map.md`). Also reached
indirectly via a data reference, consistent with sitting in the same
function-pointer dispatch table as `FUN_1401d83a0`.

🔴 **Not yet traced**: the outer per-frame orchestrator that actually
holds this function-pointer table and calls `FUN_1401d83a0` in the first
place.

---

## 3. The render command-buffer (frontend queue)

🟢 **`FUN_1401d2930`** — a textbook ring-buffer bump allocator for a
command stream. Fixed base, wraparound-checked write cursor, 8-byte-
aligned slots; stamps a type/opcode tag (`0x80019` for the one call site
traced) into each allocated slot's first qword. Context struct at global
`0x141896b98`.

This is the `R_AddCmd`-equivalent: the frontend (main thread, or whatever
calls into this per-frame chain) queues **typed commands**, it never
issues D3D9 calls directly. The real consumer is described in §5.

🔴 **Not yet decoded**: the full enum of command type tags (`0x80019` is
one of presumably many — draw model, draw sprite, set light, end frame,
etc.), and whether this command buffer is actually the same mechanism the
worker-pool stage dispatch (§5) drains, or a structurally separate queue.

---

## 4. The stage-notify coordination layer

🟢 **`FUN_1401ea4b0(stageId)`** — a lightweight, single-shot **timestamped
marker call** (plausibly RAD Telemetry, a known CoD-era licensed
profiler), NOT a dispatcher and NOT where real work happens. Gated by a
per-stage listener-count check, tracked via a LOCK-guarded "currently
dispatching" global.

Called from `FUN_1401d7480` with stage IDs **1 through 6** back-to-back,
then later with **`0xe`**, **`0x11`**, **`0x12`** from elsewhere in the
same call chain. None of the real phase work (culling, shadow-caster
gathering, drawsurf generation) appears inline near these calls — it
happens elsewhere, on the worker threads (§5).

🟢 **Real stage-ID → name table** (`0x1404d0b50 + stageId*8`, confirmed
via direct string dump, NOT a function-pointer table despite the original
first-pass guess):

| ID | Name | ID | Name |
|---|---|---|---|
| 0x0 | `physics` | 0xa | `fx pass 2` *(no `fx pass 1` entry — real gap)* |
| 0x1 | `cell dyn brush` | 0xb | `glass` |
| 0x2 | `cell dyn model` | 0xc | `fx pass 4` |
| 0x3 | `cell scene ent` | 0xd | `fx pass 5` |
| 0x4 | `dpvs ent` *(Umbra DPVS — visibility culling)* | 0xe | `cell static` |
| 0x5 | `bound ent` | 0xf | `smodelcache` |
| 0x6 | `spot shadow ent` | 0x10 | `skin model` |
| 0x7 | `trace` | 0x11 | `add scene ent` |
| 0x8 | `trace_to_entity` | 0x12 | `gen drawsurfs` |
| 0x9 | `fx pass 0` | 0x13 | `cell glass` |

Stages 1–6 read as the real visible-scene-determination pipeline: cull
brushes/models → gather scene entities → run DPVS visibility → gather
bounds → gather shadow casters. `add scene ent` (0x11) and `gen drawsurfs`
(0x12) are the bridge into actual draw-command generation.

---

## 5. The real backend — worker threads and `gen drawsurfs`

🟢 **Fully traced end to end, every link independently confirmed via
decompile.** This resolves both "who actually does the stage work" and
"where is the render-command consumer" — they're the same mechanism.

1. **`FUN_140249e80(jobFuncPtr, slotIndex)`** — generic worker-spawn
   utility. Calls `CreateThread(..., FUN_14024a810, slotIndex,
   CREATE_SUSPENDED, ...)`, a single shared thread entry point used for
   every worker slot in the engine (real per-slot TLS setup, a per-slot
   init function, then jumps through the given job function pointer).
2. **`FUN_14024a600(jobFuncPtr, param_2)`** — the one parameterized
   "spawn slot N+2" wrapper. Its caller:
3. **`FUN_1401e9be0`** — spawns **TWO** identical worker threads (slots 2
   and 3), both running:
4. **`FUN_1401ea560`** — the real worker-thread main loop. Confirmed via
   shared global addresses with §4's own listener-count/dispatching-stage
   tracking. Picks the next pending stage ID and calls:
5. **`FUN_1401e9c40(stageId, flushAll)`** — drains a ring-buffer event
   queue for that stage (base `0x141cc2b18`, cursors at `0x141cc2b00`/
   `+0x04`), and for each queued event calls:
6. **`FUN_1401e9f70(stageId, eventRecordPtr)`** — the real per-stage work.
   A giant (~2000-line) `switch(stageId)` whose case IDs match §4's table
   exactly.

**Other worker slots, for completeness** (all confirmed NOT render-related):
slot 1/6 = generic multi-sync-event spawners (not individually traced);
slot 5 = Bink Video background-streaming/decode worker (real
`BinkControlBackgroundIO` calls, a 26MB frame-decode buffer, initially
mistaken for a render command pool); slot 7 = the real Win32
window-message-pump thread (`GetMessageA`/`TranslateMessage`/
`DispatchMessageA`).

### `gen drawsurfs` (case `0x12`)

🟢 A multi-phase state machine (a 0-3 phase counter in the event record
itself, re-queuing until phase 3). Allocates draw-surface array slots
from a large shared per-frame scratch buffer (`lRam...141896b80`,
0x18/24-byte entries) — this is array-slot bookkeeping, not a per-entity
walk.

### `add scene ent` (case `0x11`) — the real per-entity identity

🟢 **Five separate entity-category loops**, each with an identical real
shape: a plain incrementing integer index, gated by a parallel "active
this frame" byte array, calling a category-specific processing function
for each active index into a fixed-stride record array:

| Category fn | Record base | Stride |
|---|---|---|
| `FUN_1401c7aa0` | `0x141c23028` | 0x90 (144B) — largest, only one with a model-transform field check; strongest candidate for "dynamic/skinned models" |
| `FUN_1401c7b70` | `0x141c36278` | 0x58 (88B) |
| `FUN_1401c7b30` | `0x141c4de84` | 0x30 (48B) |
| `FUN_1401a7450` | (two, indirect 16-bit index via `lRam...887c30+0x380`/`+0x280`) | — |

**This is the real, stable per-entity identity** any future per-object
motion-vector work needs — a plain array index per category, keyed
against the engine's own already-maintained "active this frame" flag
(which also directly supplies the motion-vector-invalid sentinel case:
newly-active index this frame = no valid previous-frame transform).

🔴 **Not yet confirmed**: which category is actually "dynamic models"
(0x141c23028 is the strong candidate, not proven), and whether case `0x03`
(`cell scene ent`, one stage earlier) feeds these same index arrays or has
its own separate identity space.

---

## 6. Render targets

🟢 **A single generic table drives every named render target** —
`0x1404d0600`ff in `.data`: a descriptor-pair block, then 19 name-string
pointers, confirmed by direct string dump:

```
current, min_pc, $shadowmap_large, $shadowmap_small, $floatz,
$post_effect_0, $post_effect_1, $pingpong_0, $pingpong_1,
$resolved_scene, $scene, $savedscreen, $raw, $model_lighting,
$model_lighting1, $random_rotations, $ssao, $ssao_blurred, $ssao_float_z
```

Shadow maps use the **exact same creation mechanism** as every other
target (post-effect buffers, SAVED_SCREEN, etc.) — there is no special-
cased shadow-map system. One creation mechanism to understand for any
future renderer-replacement work, not many.

🟢 **A genuine, complete, never-before-documented native SSAO
implementation** exists dormant in the binary — full dvar set
(`r_ssao`/`r_ssaoStrength`/`r_ssaoPower`/`r_ssaoBlurRadius`/
`r_ssaoDownsample`/`r_ssaoDebug`), real technique names
(`ssao_calc_slow`/`fast`, `ssao_apply_fullres`/`downsampled`, plus a debug
visualization mode), full-res and downsampled quality tiers. Never touched
by this project. `r_ssaoDebug` is a real, ready-to-use tool for testing
SSAO-interaction theories — currently untestable because forcing any dvar
on x64 needs a native `SetDvarBool`/`SetDvarInt` this project hasn't found
yet (`known_issues_x64.md` issue #6).

🔴 **Not yet found**: the function that walks this table and issues the
real `CreateTexture`/`CreateRenderTarget` calls (five distinct byte-
pattern-scan techniques tried, all negative — needs a real, scoped Ghidra
analysis pass or live tracing instead of more blind scanning).

### Render-target ACTIVATION (switching which target is bound)

🟢 **`FUN_1401dfd80`** — the real per-frame render-VIEW activator (as
opposed to the creation-side table above). Real shape: an early-out if
the requested view index matches the currently-active one; a loop
scanning up to 20 cached surface-pointer slots to invalidate stale
references (two D3D9 vtable calls at `+0x1c8`/`+0x208`); then direct
`SetRenderTarget`/`SetDepthStencilSurface` calls (vtable `+0x128`/`+0x138`
in the x64 build's own vtable layout).

Live-observed firing pattern: this is what
`[x64-renderview-select-diag]` tracks — normally quiet, but fires in
rapid multi-index bursts (e.g. `1→2→6→7→8→6→8→1→2→1`) at specific,
still-unidentified moments. See §7 for why this matters.

---

## 7. x86 vs. x64 — the real comparison so far

The core question driving this comparison: the x64 recompile is suspected
of having regressed the draw-submission path itself (not content density,
not scripting load — see `known_issues_x64.md` issue #4's own synthesis).
One genuine, verified structural difference has been found; a full
explanation for the observed spikes has not.

### Render-thread confirmation (live-tested, not inferred)

🟢 **A genuine dedicated backend thread exists on x64.** `EndScene`
settles onto a persistent, distinct thread ID within the first 1-2 frames
of a session (not the main thread) and stays there for the overwhelming
majority of play. Confirmed via a live diagnostic
(`[render-thread-diag]`, comparing `GetCurrentThreadId()` against the
real main thread ID captured at `DLL_PROCESS_ATTACH`), not static
reasoning.

🟢 **The real correlated finding**: across one full session, `EndScene`'s
calling thread flipped back to the main thread exactly twice (independent
occurrences) — and both times, the very next frame spiked to 100-165ms
(vs. a normal ~16-35ms) before flipping back and normal framerate
resuming. Both switches were immediately preceded by a burst of
`FUN_1401dfd80` (§6) view-index changes.

🔴 **Not yet found**: what native condition triggers that view-index
burst, and whether the same handoff-to-main-thread pattern exists (or is
absent) on x86.

### `FUN_1401dfd80` (x64) vs. `FUN_00542cb0` (x86) — the real match

🟢 **Behaviorally verified, not positionally guessed** (a prior "x64
equivalent of `FUN_0049bf50`" label in this project's own docs was
checked and found WRONG — `FUN_0049bf50` is actually a per-surface draw
dispatcher, unrelated; corrected in the log). `FUN_00542cb0` was
independently confirmed elsewhere in this project's own research as **the
only real `SetRenderTarget` call site in the entire x86 binary**.
Decompiled and behaviorally matches x64's function exactly: same
dedup-then-`SetRenderTarget`/`SetDepthStencilSurface` shape, confirmed via
direct vtable-slot arithmetic (offset `0x94` = slot 37 = `SetRenderTarget`,
`0x9c` = slot 39 = `SetDepthStencilSurface`, both real x86 32-bit vtable
offsets).

**The real, confirmed difference**: x86's version is simpler. It has no
equivalent of x64's 20-slot cache-invalidation scan, and no equivalent of
x64's early-out dedup guard. x86 just does two straightforward
compare-and-call checks.

**Honest scope**: this is genuine, verified evidence something changed in
the draw-submission path — but a 20-iteration pointer scan plus two extra
vtable calls is microseconds of cost, nowhere near enough to explain a
100ms+ spike on its own. It's more likely a *symptom* of a larger resource-
lifetime-management change than the direct cause. 🔴 **Real next step,
not yet done**: find what actually populates/invalidates those 20 cached
slots elsewhere in the x64 binary, and whether a real invalidation fires
during the exact frames the live capture caught.

---

## 8. Open questions, ranked by how directly they bear on the regression theory

1. **What triggers the `FUN_1401dfd80` view-index burst that precedes
   both observed spike/thread-handoff events?** The single most direct
   remaining lead — trace this function's own callers, or correlate a
   future live capture against actual player action at the exact frame
   numbers.
2. **What populates/invalidates the 20-slot cache in `FUN_1401dfd80`,
   and does an actual invalidation (not just a scan) fire during a spike?**
3. **Does x86 exhibit the same main-thread handoff pattern at all?**
   Needs either a working downgrader-based live test, or tracing x86's
   own equivalent of `Hook_EndScene`'s calling thread statically.
4. **Where is the render-target table's real consumer** (the function
   issuing `CreateTexture`/`CreateRenderTarget` from the table in §6)?
5. **Material/shader binding** — how a drawn surface's shader program and
   texture samplers actually get bound per-draw-call. Untouched so far,
   though `FUN_1400a5a20` (a generic named-asset lookup, type-tag-
   confirmed against `tools/iw5oat`'s own `IW5::XAssetType` enum) is a
   real, reusable anchor for this.
6. **Which `add scene ent` category (§5) is "dynamic models"**, and
   whether `cell scene ent` (case `0x03`) shares its identity space.

---

*Distilled 2026-09-24 from `renderer_architecture_map.md`. Keep both files
in sync: log new investigation rounds there, then fold confirmed
architectural conclusions back into this document in the same pass.*
