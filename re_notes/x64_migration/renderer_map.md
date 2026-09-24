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
exists on x64, confirmed by direct live observation, not inference.**

**The confirmed explanation for the draw-pipeline regression** (§7,
resolved 2026-09-24): x86 had real dedicated D3D9-adjacent threading
(device-lost polling, `BeginScene`) but its actual frame *submission*
(`EndScene`) ran unconditionally on the main thread, every frame, as part
of `Com_Frame()`. x64 moved submission itself onto a dedicated thread —
so x64 didn't lose parallelism, it gained more of it. The real cost is
the rare fallback path where x64 has to hand work back to the main
thread, something x86's architecture never needed to do at all — and
that hand-off is exactly what's been caught live correlating with both
observed 100-165ms spikes. This is now confirmed on both sides (x86 via
real decompile/caller-chain analysis, x64 via live diagnostic), not a
standing theory.

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

🟢 **Confirmed**: case `0x03` (`cell scene ent`, one stage earlier) shares
the exact same array/index space as the `FUN_1401c7aa0` category — verified
via direct offset arithmetic (`0x141c23028 - 0x141c22fb8 = 0x70`, matching
the `0x90` stride's own field layout). A motion-vector cache can key off
this one array starting from case `0x03`'s own processing.

🟡 **Reframed, not settled**: `FUN_1401c7aa0`/`c7b70`/`c7b30` all tail-call
into one shared function (`FUN_1401c7660`) that turns out to be a real
spatial-cell/position-slot **registration** system (position caching, LOD
dedup, a reverb-zone proximity probe, cell-table registration) — not a
draw-dispatch path as originally assumed. `FUN_1401c7aa0` (full 6-float
transform, largest record) is still the best-supported candidate for
"dynamic models" on parameter-shape grounds, but that's now a weaker,
unconfirmed claim. `FUN_1401a7450` is NOT an entity category at all — it's
the shared reverb/nearest-point probe those three call internally.

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

🟢 **Consumer FOUND, 2026-09-24** — `FUN_1401d40eb` (likely a mid-function
cut, real entry point slightly earlier in the same contiguous code, but
the code region is unambiguous). Found via a new, reusable technique
after five prior reference-scan attempts against the table's own address
all failed (the table is never referenced directly by name/address —
creation functions are handed a record pointer by their caller instead):
a raw scan for `CALL [reg+disp32]` against `CreateRenderTarget`'s real
x64 vtable offset (`0xE0`, slot 28) found exactly one hit in the whole
binary. Real, confirmed cross-references tying this into everything else
already mapped: sizes from the exact same `InternalRenderScalePercent`
globals (`_DAT_141888670`/`674` unclamped, `_DAT_141888678`/`67c`
tier-clamped) already hooked elsewhere; reads a creation parameter from
`_DAT_1404d0750` (confirming the "fourth sub-table" below really is real
per-target creation data, not dead data); writes its created surface
pointer into the exact same `0x141bb6e00`-range block the activation
function (below) reads its own cache from — the creation and activation
systems share one underlying surface-pointer array. 🔴 Not yet found:
this function's own caller (the real per-target dispatch loop, presumably
iterating all 19 table entries).

### Render-target ACTIVATION (switching which target is bound)

🟢 **`FUN_1401dfd80`** — the real per-frame render-VIEW activator (as
opposed to the creation-side table above). Real shape: an early-out if
the requested view index matches the currently-active one; a loop
scanning up to 20 cached surface-pointer slots (see below); then direct
`SetRenderTarget`/`SetDepthStencilSurface` calls (vtable `+0x128`/`+0x138`
in the x64 build's own vtable layout).

🟢 **The 20-slot "cache" is texture-sampler-stage tracking, not a generic
resource cache — confirmed via vtable-slot arithmetic.** The table spans
exactly `struct+0x70` through `struct+0x10F`, ending precisely where the
real D3D9 device pointer begins at `+0x110` (a confirmed struct layout).
The invalidation call at `+0x208` is `SetTexture` (slot 65). This is
standard D3D9 render-to-texture hazard avoidance: before binding a
surface as a render target, any sampler stage currently using that same
surface as a shader INPUT must first be unbound, since D3D9 disallows a
resource being simultaneously a render target and a bound input. x86's
equivalent (`FUN_00542cb0`, §7) has no version of this — which may mean
x64's pipeline genuinely does more render-to-texture round-tripping than
x86's did (real, necessary bookkeeping for a different pipeline shape),
not wasted overhead bolted onto an unchanged one. 🔴 Still open: who
writes non-zero values into the table, and whether a real invalidation
(not just the scan) actually fires during ordinary play — both need a
real analysis pass or a new targeted live diagnostic, not static
guessing.

Live-observed firing pattern: this is what
`[x64-renderview-select-diag]` tracks — normally quiet, but fires in
rapid multi-index bursts (e.g. `1→2→6→7→8→6→8→1→2→1`) at specific,
still-unidentified moments. 🟡 Best-supported (not confirmed) reading:
`FUN_1401dfd80` is reached from at least two structurally distinct
contexts (a generic per-technique/material dispatch table, and a second
table matching the render-target descriptor's own indexing stride) —
consistent with it being a commonly-invoked shared utility called many
times per frame, meaning the burst itself may be an unremarkable
multi-pass sequence rather than a single rare trigger. Finding its real
CALL-instruction consumers hit this project's own known `-noanalysis`
reference-manager blind spot (both known references are DATA references,
not calls) — needs a real analysis pass or live tracing. See §7 for why
this matters.

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

🟢 **Real, major finding, 2026-09-24: x86 also had genuine dedicated
D3D9-adjacent threading — the regression is likely NOT "x64 lost
parallelism."** Of the x86 build's own already-documented
`setjmp3`-guarded infinite-loop threads, one (`FUN_0040de80`) is confirmed
to be a real device thread: a fixed ~30Hz `Sleep`-paced loop calling
`TestCooperativeLevel` (confirmed via the real `D3DERR_DEVICELOST`/
`D3DERR_DEVICENOTRESET` constants) and `BeginScene` on device-lost
recovery.

🟢 **CONFIRMED, 2026-09-24, via a real full Ghidra analysis pass (not
live tracing — x86 no longer runs against the current retail game).**
x86 had a genuine *partial* split, now proven rather than inferred: real
per-frame `EndScene` submission ran directly on the main thread, as part
of `Com_Frame()` (`FUN_0044c7b0`, already independently confirmed
elsewhere in this project's own research as the real per-frame tick,
called in a tight `while(true)` loop directly from the real
WinMain-equivalent). The chain: `FUN_00534380` (WinMain, main thread) →
`FUN_0044c7b0` (`Com_Frame`, main thread, every tick) → `FUN_004b6510` →
the real `EndScene` vtable call. Device management (`TestCooperativeLevel`/
`BeginScene` on device loss) genuinely was on its own dedicated thread —
but normal-case frame *submission* was not; it was main-thread by
construction, unconditionally, every frame.

x64 (confirmed live) moved that same per-frame `EndScene` submission onto
a dedicated backend thread. **This is now the strongest, most directly
evidenced conclusion in the whole investigation**: x64 gained real
parallelism for the common case, but the real regression cost is the rare
fallback path where x64 has to hand frame submission back to the main
thread — something x86's architecture never needed to do at all. The
thread HANDOFF itself, not lost threading, is the real regression
mechanism. (Three other real `EndScene`-calling functions were found and
ruled out as the normal path — all are device-reset/recovery-specific,
called from the background device-health thread or other rebuild
utilities, not the steady-state per-frame path.)

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

1. ~~Does x86's real `EndScene` call site sit on the main thread or a
   dedicated one?~~ **RESOLVED, 2026-09-24 — see §7.** Confirmed via a
   real, saved Ghidra analysis pass (no live tracing possible — x86 no
   longer runs against the current retail game): x86's real per-frame
   `EndScene` is called directly from `Com_Frame()` (`FUN_0044c7b0`),
   the already-independently-confirmed main-thread per-frame tick
   function, every single frame — no cross-thread hand-off ever needed
   for the normal case. This directly confirms the "thread hand-off is
   the real regression, not lost parallelism" theory.
2. **What triggers the `FUN_1401dfd80` view-index burst that precedes
   both observed spike/thread-handoff events, and is it genuinely rare or
   an unremarkable multi-pass sequence?** Needs a real Ghidra analysis
   pass (not `-noanalysis`) or live tracing to find the actual
   CALL-instruction consumers — reference-manager searching hit its known
   blind spot.
3. **Who writes into `FUN_1401dfd80`'s 20-slot texture-unbind table, and
   does a real invalidation (not just the scan) fire during a spike?**
   Needs a real analysis pass or a new, narrowly-targeted live diagnostic.
4. **`FUN_1401d40eb`'s own caller** (the render-target table's real
   per-entry dispatch loop) — not yet found, real next step now that the
   creation function itself is located.
5. **Material/shader binding** — how a drawn surface's shader program and
   texture samplers actually get bound per-draw-call. Untouched so far,
   though `FUN_1400a5a20` (a generic named-asset lookup, type-tag-
   confirmed against `tools/iw5oat`'s own `IW5::XAssetType` enum) is a
   real, reusable anchor for this.
6. **Which `add scene ent` category (§5) is "dynamic models"** —
   `FUN_1401c7aa0` remains the best-supported candidate but is unconfirmed
   after the real spatial-registration-system reframing found this pass.

---

*Distilled 2026-09-24 from `renderer_architecture_map.md`. Keep both files
in sync: log new investigation rounds there, then fold confirmed
architectural conclusions back into this document in the same pass.*
