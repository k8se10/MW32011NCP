# Native 3D renderer architecture — x64 map (started 2026-09-23)

**Purpose, stated directly by the user**: not tied to one specific bug fix —
a full map of the native 3D renderer's real architecture, explicitly framed
as groundwork for a future full renderer replacement (potentially including
ray tracing), not just another visual-enhancement toggle. Direct motivation,
worth recording verbatim: "the reason im doing a full renderer replacement
and rt is becuase the mod mw3 remastered claims to do this all, but on
review of the files looks like not much more than a reshade preset" — i.e.
a competing community mod claims a comprehensive renderer overhaul but
inspection suggests it's a post-process-only (ReShade-preset-tier) effort,
not a real replacement. **Named and confirmed, same day**: the competing mod
is "MW3 Remastered" on Nexus Mods
(`nexusmods.com/callofdutymodernwarfare3/mods/10`) — direct user
confirmation, having already reviewed its files: "this is the current mw3
remastered mod which does this via reshade." This project's own existing
visual-enhancement suite (render scale, motion blur, FSR, forced-quality
dvars) is itself still fundamentally a post-process/dvar-tweak layer on top
of the native D3D9 pipeline, not a replacement — this new effort is
explicitly aiming higher than that, and higher than what MW3 Remastered
appears to actually ship.

This document is the master reference for the native renderer's real
architecture, to be filled in incrementally across sessions. Unlike
`ui_draw_pipeline_map.md` (the 2D/HUD/text draw layer, already mapped in
full), this covers the 3D scene renderer itself: how a frame's worth of
game-world geometry actually reaches the GPU.

**Real scope correction, same day, from actual research (full trail in
`known_issues_x64.md` issue #2)**: investigating a real, working reference
implementation (a GTA IV DLAA/DLSS project) found that full path-tracing/
geometry-and-material-replacement renderer overhaul — the maximal reading of
"renderer replacement" this doc opened with — is genuinely, structurally
blocked for this engine, not just hard: RTX Remix (NVIDIA's real runtime,
the thing that would do it) is explicitly built only for D3D8/9 games with
FIXED-FUNCTION pipelines, and IW5 is a fully shader-based renderer (this
doc's own section 5b already confirms real material/technique/shader-asset
systems throughout, not fixed-function texture-stage-state rendering) — the
same mismatch GTA IV's own RAGE engine has, which is exactly why that
project never attempts it either. The achievable, real slice — DLAA/DLSS
upscaling and reconstruction via a ReShade add-on layer (`LumeniteFX` for
motion vectors, `DLSS5-Feeder` for the actual neural-rendering feed), no
cross-process bridge needed since MW3 is already native 64-bit unlike GTA
IV — is itself, honestly, still fundamentally a sophisticated ReShade-layer
approach, the same category of thing this doc's own opening framing
contrasted against ("not much more than a reshade preset"). The real
distinction that survives: a genuine DLSS/DLAA neural-rendering pipeline
with real optical-flow motion vectors is a categorically more capable
ReShade layer than a filter/color-grade preset, but it's still a
post-process addition on top of the native D3D9 pipeline mapped in this
document, not a replacement of it — worth being precise about that
distinction going forward rather than let "renderer replacement" imply more
than what's actually achievable here.

**Methodology**: same as every other x64 RE pass in this directory —
`analyzeHeadless.bat -import ... -readOnly -noanalysis` against
`re_notes/x64_migration/binaries/iw5sp.exe` (the tracked `.rep` project
under `ghidra_project_x64/` is confirmed still broken/empty, same
"no files enumerate" failure noted throughout this directory — a fresh
scratch import is the only working path every time). Per the locked
2026-09-03 signature-scanning policy (`CLAUDE.md` §5/§10.3): every address
below is a coordinate against THIS specific x64 binary build, for RE
reference only — never hardcode one of these into shipped hook code.

**Confidence key**: **CONFIRMED** = read directly off a real decompile/
disassembly this pass. **INFERRED** = a reasoned guess from shape/context,
not independently proven.

---

## 1. The real per-frame top-level entry (CONFIRMED)

`FUN_1401d83a0` is the genuine per-frame top-level render entry point —
found by following the same investigation thread `ui_draw_pipeline_map.md`
left open ("per-frame entry, not traced further this pass").

- Computes the same viewport-scale constants
  (`iRam...888670`/`674`/`uRam...888678`/`67c`) the SAVED_SCREEN
  quality-tier/render-scale system already uses — this function is where
  the per-view rectangle (x/y/w/h at struct offsets `+0x148..+0x174`) is
  actually derived from the real backbuffer dimensions, split-screen state
  (`param_2[0x13c0]` != 0 branch), and those same scale constants. This is
  the real anchor point `InternalRenderScalePercent`'s own hook
  (`Hook_RenderResCompute` → `FUN_1401bd1d0`) feeds into indirectly — not
  the same function, but the consumer of the values it computes.
- Calls three small helper functions before reaching the UI/HUD chain —
  see section 2, all three turned out to be tiny (not full render-stage
  entries as first suspected from their unresolved-symbol appearance).
- Ends by calling `FUN_1401d7480` (see section 3) — the already-known,
  now more-fully-mapped per-player frame-setup function.
- **Its own caller is a single DATA reference** (`0x14445093c`), not a
  direct `CALL` — i.e. this function is invoked indirectly, almost
  certainly through a function-pointer table entry from deeper in the
  engine's own frame-render orchestrator (itself not yet traced — real
  next step, see section 6).

## 2. Three small per-frame helpers (CONFIRMED, all trivial — not render stages)

Initially suspected (from their `func_0x...` unresolved-symbol rendering,
a `-noanalysis` artifact, not evidence of anything unusual) to be the
actual 3D scene/shadow-pass entries. Raw disassembly shows all three are
tiny:

- `FUN_1401d2b70` (7 instructions) — a pure data-linkage setter: writes a
  computed pointer (`base + playerIndex*0x14c0`) into a per-view struct
  field at `+0x330`. Not a render call.
- `FUN_14008d6c0` (2 instructions) — sets a single global flag
  (`DAT_14071747c = 1`). Not a render call.
- `FUN_1401d2930` (real finding, see section 4) — a render-COMMAND-BUFFER
  slot allocator. Small, but architecturally the most important of the
  three.

## 3. `FUN_1401d7480` — per-player frame setup, real internals now decompiled (CONFIRMED)

Already known narratively from `ui_draw_pipeline_map.md` ("per-player
frame setup: view/damage/killcam state, a numbered UI-event dispatch...
THEN drives the [HUD tick]"). Full decompile this pass shows real internal
structure worth recording:

- Calls a chain of `FUN_1401d8f70`/`FUN_1401d9a10`/`FUN_1401d8910`/
  `FUN_1401d3660`/`FUN_1401d9b40`/`FUN_1401d7940`/`FUN_1401d5c10` — view/
  camera/FOV-adjacent setup (`FUN_1401d8f70` computes FOV-scale-derived
  aspect constants written into `param_1+0x9ec`/`+0x9e8`/`+0x9e4`, real
  floating-point camera math; `FUN_1401d9a10` is a large struct-copy/
  view-parameter-marshal function, real fog/DOF/vignette-adjacent dvar
  reads visible in its own decompile — see the raw trail for detail).
- **The real render-stage NOTIFY dispatcher, `FUN_1401ea4b0(N)`, called
  repeatedly with sequential stage IDs 1 through 6**, then again later
  with `0xe`, `0x11`, `0x12` — see section 5, this is the single most
  important finding of this pass architecturally.
- `FUN_1401a81e0` — NOT a render call despite its position in the middle
  of this chain; real decompile shows it's audio-occlusion/portal-flag
  bookkeeping (walks two byte-array "flag" tables keyed by an index,
  computes distance-based falloff via `FUN_1401c6ed0`/`FUN_1401a8e20`,
  ends by firing stage-`0x11` notify itself) — a real, useful negative
  result: this function's NAME/POSITION suggested camera/view work, but
  its actual content is unrelated. A caution for future passes in this
  same chain: position in the call sequence is not a reliable signal for
  what a function actually does here, only decompiling is.
- `FUN_1401d6b20` — real conditional logic gating a `FUN_1401ea4b0(0xe)`
  stage-notify plus two more calls (`FUN_14019c310`/`FUN_14019cbc0`)
  behind `param_2[9] != 0` (an already-known "menu active"-adjacent flag
  shape, not yet confirmed identical to `g_menuActiveGateFlag`), then
  (when `param_4 == 1`) does real player-position/fog-trigger-volume
  distance-check work (`FUN_140190b60`) — this looks like real fog-volume/
  trigger-zone evaluation, a genuine gameplay-adjacent system, not core
  rendering.
- Ends with `FUN_140039f40` — the already-fully-mapped HUD tick from
  `ui_draw_pipeline_map.md`.

**Its own caller is also a single DATA reference** (`0x144450894`), same
indirect-call shape as section 1 — consistent with both functions being
entries in the same per-frame function-pointer dispatch table.

## 4. The render command-buffer allocator — the key architectural finding (CONFIRMED shape, command semantics NOT yet decoded)

`FUN_1401d2930` (raw disassembly, 19 instructions):

```
RDX = ptr to a command-stream context struct (global @ 0x141896b98)
EAX = (some counter @ 0x141896b8c) + 0xffffe000   ; i.e. counter - 0x2000, a wraparound-style bound check
R8  = struct[+0x8]   (current write position, as an int)
ECX = struct[+0xc] - R8 + EAX                       ; remaining-space check
if (ECX < 8) { struct[+0x10] = 0; return; }          ; buffer full -- returns a null slot pointer, caller must handle
RCX = struct[base] + R8                              ; real slot address = base + current write position
struct[+0x8]  = R8 + 8                               ; advance the write cursor by 8 bytes
struct[+0x10] = RCX                                  ; publish the allocated slot pointer (this function's own "return value" via output field)
*(qword*)RCX  = 0x80019                              ; STAMP the command header/type tag into the slot
return
```

This is a textbook ring-buffer bump allocator for a command STREAM (fixed
base, wraparound-checked write cursor, 8-byte-aligned slots), and the
`0x80019` constant written into every allocated slot's first qword is
almost certainly a command TYPE/opcode tag — the classic id-Tech
`R_AddCmd`/`RB_ExecuteRenderCommands` pattern (Quake3 lineage, this
project's own already-documented engine ancestry per `CLAUDE.md`'s
"Engine" field) where the game-logic ("frontend") thread queues typed
render commands into a ring buffer, and a separate consumer ("backend",
classically a different thread in this engine family) drains the buffer
and issues the real D3D9 API calls.

**This is the single most important architectural finding for the stated
goal ("prep for a future renderer replacement")**: it means the actual
D3D9 draw-call submission this project would need to intercept or replace
is NOT reachable from the CPU-side per-frame logic chain traced in
sections 1-3 at all — those functions only ever QUEUE typed commands.
The real render-command CONSUMER (wherever `0x80019`-tagged and sibling
command types actually get turned into `IDirect3DDevice9::DrawIndexedPrimitive`/
`SetTexture`/`SetRenderState` calls) is a structurally separate function
(or thread) not yet located.

**Open questions, the real next-step targets**:
1. What is the full enum of command type tags (of which `0x80019` is one)?
   Likely findable by searching for other literal stamps of the same shape
   (`*(qword*)slotPtr = 0x8....`) written by sibling allocator-callers
   elsewhere in the binary — this command-buffer allocator function itself
   is presumably called from many places, each writing a different literal
   tag for a different command type (draw model, draw sprite, set light,
   end frame, etc.).
2. Where is the consumer — the function that reads slots back out of this
   same ring buffer (`struct[base]` walked from some read cursor) and
   dispatches on the tag to real D3D9 calls? This is very likely reachable
   from (or IS) the code this project's own `Hook_EndScene`/`Hook_Reset`
   D3D9-vtable hooks already sit adjacent to, or from a genuinely separate
   render-backend thread this project has never traced (worth checking:
   does this engine actually run its render backend on its own thread on
   PC, the way idTech3/RTCW/Quake did, or was that collapsed to
   single-threaded on this port? A real, answerable question via a thread-
   enumeration pass or by tracing whatever calls
   `IDirect3DDevice9::BeginScene`/the vtable this project's own hooks
   already resolve).
3. Confirm whether `0x141896b98` (this command-stream context struct) is
   the SAME context `FUN_1401d2b70` (section 2) links a per-view pointer
   into at `+0x330` — if so, that's real evidence tying the per-view setup
   directly to a specific command-stream instance (e.g. one stream per
   split-screen viewport), not a single global stream.

## 5. The render-stage notify dispatcher — `FUN_1401ea4b0(N)` (CONFIRMED shape AND stage names -- corrected from this doc's own first-pass framing)

**Correction to this section's own original framing** (written earlier the
same pass): the table at `0x1404d0b50` was initially assumed to be a
function-pointer dispatch table (INFERRED, explicitly flagged as
unconfirmed at the time). Dumping and reading the actual bytes at each
entry shows they're real, human-readable ASCII strings, not code
addresses — this is a **named profiler/telemetry checkpoint table**, not
a render-stage dispatcher. Full table, indices 0-19 (`0x1404d0b50 +
index*8`):

| idx | hex | name |
|---|---|---|
| 0 | 0x0 | `physics` |
| 1 | 0x1 | `cell dyn brush` |
| 2 | 0x2 | `cell dyn model` |
| 3 | 0x3 | `cell scene ent` |
| 4 | 0x4 | `dpvs ent` |
| 5 | 0x5 | `bound ent` |
| 6 | 0x6 | `spot shadow ent` |
| 7 | 0x7 | `trace` |
| 8 | 0x8 | `trace_to_entity` |
| 9 | 0x9 | `fx pass 0` |
| 10 | 0xa | `fx pass 2` (note: no `fx pass 1` in this table -- real gap, not a read error) |
| 11 | 0xb | `glass` |
| 12 | 0xc | `fx pass 4` |
| 13 | 0xd | `fx pass 5` |
| 14 | 0xe | `cell static` |
| 15 | 0xf | `smodelcache` |
| 16 | 0x10 | `skin model` |
| 17 | 0x11 | `add scene ent` |
| 18 | 0x12 | `gen drawsurfs` |
| 19 | 0x13 | `cell glass` |

**This is genuinely valuable, independent of the dispatch-table
misreading**: `FUN_1401d7480`'s own real call sequence (`notify(1)` through
`notify(6)`, back to back) now reads as real, named scene-setup phases —
`cell dyn brush` → `cell dyn model` → `cell scene ent` → `dpvs ent`
(**DPVS** = almost certainly Umbra's "Dynamic Potentially Visible Set"
middleware, well-known visibility-culling tech used across this engine
generation) → `bound ent` → `spot shadow ent` — i.e. **this is the real
visible-scene-determination / shadow-caster-gathering pipeline**, the
exact "what's actually visible and what casts shadows this frame" phase
that has to run before any draw command exists. `add scene ent` (0x11)
and, most importantly, **`gen drawsurfs` (0x12)** — called right after
`add scene ent` and right before the HUD tick at the very end of
`FUN_1401d7480` — is almost certainly the direct bridge into section 4's
render command buffer: "generate draw surfaces" is the classic id-Tech
term for converting the culled/visible entity list into the actual
per-surface draw commands the backend consumes.

**Revised understanding of `FUN_1401ea4b0` itself**: its body
(`func_0x0001401e94f0(0xffffffff, &UNK_1404234e8, table[stageId])`
immediately followed by `FUN_1401ea200(&UNK_1401ea540, 1)`) is now read as
a lightweight, single-shot TIMESTAMPED MARKER call (not a begin/end zone
pair, and not a dispatch to real work) — consistent with this engine
family's known use of RAD Game Tools' Telemetry profiler internally (CoD
titles of this era are publicly known Telemetry licensees). **Critically,
none of the real phase work (culling, shadow-caster gathering, drawsurf
generation) appears as a separate function call anywhere near these
marker calls in `FUN_1401d7480`'s own traced body** — the six `notify()`
calls run back-to-back with no other substantial work between most of
them. This is real, concrete evidence (not just inference) that **the
actual scene-setup/culling/drawsurf work happens elsewhere, almost
certainly on a separate thread**, and these markers are cross-thread
synchronization/telemetry checkpoints the main/game thread uses to record
or wait on that other thread's progress through each named phase — this
sharpens, rather than contradicts, section 4's already-confirmed frontend/
backend command-buffer split. Finding that separate thread (or the
function(s) that actually perform each named phase, wherever they run) is
now the single most direct path to the real draw-call submission code —
more direct than continuing to trace this specific call chain further.

### Original (now-corrected) framing, kept for the record only

```c
void FUN_1401ea4b0(int stageId) {
    if (*(int*)(stageId*0x80 + 0x141cc2b0c) <= 0) return;   // per-stage "listener count" gate
    if (stageId < g_someRunningMinStageId) g_someRunningMinStageId = stageId;  // (LOCK/UNLOCK around this -- genuinely thread-synchronized)
    g_currentDispatchingStage = stageId;
    if (g_someFlag_141cc389c != 0) func_0x00014024a400();   // conditional side call, not yet traced
    func_0x0001401e94f0(0xffffffff, &UNK_1404234e8, table[stageId]);  // table = *(qword*)(stageId*8 + 0x1404d0b50) -- a real per-stage function-pointer table
    FUN_1401ea200(&UNK_1401ea540, 1);
    g_currentDispatchingStage = 0xffffffff;
}
```

Called, in order, with stage IDs **1, 2, 3, 4, 5, 6** (from `FUN_1401d7480`,
back to back, right after the camera/view setup chain and right before the
local-player-only `FUN_1402506a0` call and the HUD tick), plus **`0xe`**
(conditionally, from `FUN_1401d6b20`, gated on the same menu-active-shaped
flag) and **`0x11`**/**`0x12`** (from `FUN_1401a81e0`'s audio-occlusion
work and directly in `FUN_1401d7480` itself).

This has the real shape of a generic event/listener-notification system
(a per-stage listener-count gate, a LOCK-guarded "currently dispatching"
tracker, a function-pointer table indexed by stage ID) rather than the
render stages calling straight into draw code — i.e. **stages 1-6 are
very likely broadcast NOTIFICATIONS other subsystems subscribe to** ("the
frontend has reached stage N"), not the actual per-stage rendering work
itself. This is architecturally consistent with section 4's finding: the
real rendering happens via the separate command-buffer/backend mechanism,
and this notify system is a coordination/synchronization layer around it
(plausibly: stage 1 = "damage/kill-cam state finalized," stage 2 = "camera
finalized," etc., each letting other subsystems react before the frame's
commands are actually issued) — INFERRED, not confirmed; the real per-
stage listener table (`table[stageId]` at `0x1404d0b50 + stageId*8`) has
not yet been walked to see what actually gets called for each ID.

**Real next step**: dump the 7 (at least) real function pointers at
`0x1404d0b50` through `+0x30` (stages 1-6) plus whatever lives at
`0x1404d0b50 + 0xe*8`/`+0x11*8`/`+0x12*8`, and decompile each — this
directly answers "what does each numbered render stage actually do,"
the single most load-bearing open question for mapping the renderer
further.

## 5b. The real render-target descriptor table — FOUND, shadow maps and everything else (CONFIRMED)

Direct instruction to pivot from the notify-table chase to this: chasing
`$shadowmap_large` (the known x86-era string, per `known_issues.md`'s own
"R_RENDERTARGET_SHADOWMAP_LARGE/_SMALL" lead) turned up the complete,
real render-target inventory this engine creates. Two reference-based
tools (`RawStringScan.java`, then a `CreateThread`-style symbol xref
search) both came back with zero results — the same `-noanalysis`
reference-resolution gap section 6 already flagged for the backend-thread
search. Rather than retry a third reference-based tool, built two new
byte-pattern scanners (no full analysis pass needed): `FindLeaRefsTo.java`
(raw-scans every executable byte for `LEA reg,[rip+disp32]` targeting a
given address) and `FindQwordValueOccurrences.java` (raw-scans every
readable data block for 8-byte-aligned occurrences of a given VALUE, for
cases where an address is stored as DATA rather than referenced by code).
`FindLeaRefsTo` against the shadowmap string came back empty too (real,
consistent with it being a data-table entry, not directly LEA'd from
code) — `FindQwordValueOccurrences` found it in exactly one place:
`0x1404d0690`, in `.data`, right next to (0x4c0 bytes before) the
already-mapped profiler-checkpoint table from section 5.

Dumping the surrounding qwords reveals the real table layout:

```
0x1404d0600 - 0x1404d067f  (0x80 bytes, 16 qwords / 32 dwords)
    packed 32-bit descriptor pairs, one dword-pair per render target
    (dimension/format bitfields, not yet fully decoded -- see below)

0x1404d0680 - 0x1404d0710  (19 qwords, one per render target)
    real name-string pointers, ALL 19 read and confirmed:
    "current", " min_pc", "$shadowmap_large", "$shadowmap_small",
    "$floatz", "$post_effect_0", "$post_effect_1", "$pingpong_0",
    "$pingpong_1", "$resolved_scene", "$scene", "$savedscreen", "$raw",
    "$model_lighting", "$model_lighting1", "$random_rotations", "$ssao",
    "$ssao_blurred", "$ssao_float_z"

0x1404d0718 - 0x1404d0730  (4 qwords, code addresses: 0x1401be850,
    0x1401be860, 0x1401be8a0, 0x1401c1210)
    **CORRECTED -- NOT part of this table, a coincidentally-adjacent,
    unrelated table.** Originally guessed to be per-target creation
    callbacks; decompiled all four and found three of them (the first
    three) are small dvar/flag-gated accessors that tail-jump into a
    shared function, `FUN_1401be940`, which turned out to be a genuine
    **6-plane frustum/visibility test** (iterates 6 planes, a dot-product
    inside/outside check per plane, returns whether a point survives
    all 6 -- a completely standard frustum-culling primitive, nothing to
    do with render-target creation). The fourth, `FUN_1401c1210`, is a
    per-index device-capability bitmask comparison, also unrelated. This
    is real, useful RE in its own right (a newly-identified, reusable
    frustum-test primitive) but it means this specific address range is
    NOT the render-target table's own creation-callback array -- just
    four qwords that happen to sit in the same general `.data` region.
    The real per-target creation mechanism (if a callback-pointer array
    exists for it at all) has not been located.

0x1404d0740 onward: more qword entries (0x140420de8, +0x20e08, ...),
    evenly spaced by 0x20 (32) bytes -- a fourth, not-yet-identified
    sub-table, possibly per-target extended descriptors or dvar-name
    pointers for per-target overrides. Not yet traced.
```

**This directly closes the "shadow-map rendering... not yet located at
all" gap this doc originally opened in section 6** — `$shadowmap_large`/
`$shadowmap_small` are real, confirmed, first-class entries in the same
generic render-target table `$post_effect_0`/`$post_effect_1`/
`$savedscreen`/`$random_rotations` (all already known from this project's
own existing visual-suite work) live in — i.e. **there is no separate,
special-cased "shadow map system" to find; shadow maps are created by
the exact same generic table-driven render-target creation mechanism as
every other named target this project has already hooked into once
(`InternalRenderScalePercent`'s own `Hook_RenderResCompute`/
`FUN_1401bd1d0` chain)**. This is a genuinely major simplification for
any future renderer-replacement work: one creation mechanism to
understand/replace, not many.

**Also newly confirmed by name, real additional render-system surface
area not previously documented anywhere in this project**: `$floatz`
(a linear/float depth buffer — SSAO's typical depth source), `$pingpong_0`/
`$pingpong_1` (ping-pong buffers for iterative blur passes — likely what
FSR/motion blur's own multi-pass work already uses internally, or would
reuse), `$resolved_scene` (the MSAA-resolved color target, separate from
`$scene` itself), `$model_lighting`/`$model_lighting1` (two buffers,
consistent with a deferred or semi-deferred lighting accumulation
scheme), and **`$ssao`/`$ssao_blurred`/`$ssao_float_z`** — real, confirmed
evidence this engine has a native SSAO implementation, never previously
documented anywhere in this project's own RE history (the existing
visual-enhancement suite has no SSAO-related feature or dvar at all).

**Follow-up the same day, direct user connection ("native ssao makes
sense to as why our aa stuff made everything look worse")**: dumped
every string in the binary (`DumpAllStrings.java`) and found this is a
COMPLETE, real, fully-named native SSAO feature, not just three orphaned
render targets — a full dvar set and shader-technique table:

- `r_supportsSSAO` — "True if the videocard supports features needed for SSAO"
- `r_ssao` — "Screen Space Ambient Occlusion mode" (the real master toggle/mode dvar)
- `r_ssaoStrength` — "Strength of Screen Space Ambient Occlusion effect"
- `r_ssaoPower` — "Power curve applied to SSAO factor"
- `r_ssaoBlurRadius` — "Apply gaussian blur with this radius to calculated SSAO values"
- `r_ssaoDownsample` — "Perform SSAO calculation on downsampled depth buffer"
- `r_ssaoDebug` — **"Render calculated or applied Screen Space Ambient Occlusion values"** — a real debug VISUALIZATION mode, directly useful for testing the SMAA-interaction theory below without needing to guess
- Real technique names: `ssao_calc_slow`/`ssao_calc_fast`, `ssao_apply_fullres`/`ssao_apply_downsampled`, `ssao_debug_apply_fullres`/`ssao_debug_calc_fullres`/`ssao_debug_apply_downsampled`/`ssao_debug_calc_downsampled`, `ssao_zdownsample`
- Render-target registration strings: `R_RENDERTARGET_SSAO`/`R_RENDERTARGET_SSAO_BLURRED`/`R_RENDERTARGET_SSAO_FLOAT_Z` (the real registration-time names behind the `$ssao`/`$ssao_blurred`/`$ssao_float_z` table entries above)

This is a genuine, dormant, fully-built native feature this project has
never touched — not a stub, not a leftover fragment, a complete
calc/blur/apply pipeline with its own quality tiers (full-res vs.
downsampled) and debug tooling already built by the original developers.
**Directly relevant to `known_issues_x64.md` issue #2** (SMAA parked
after even a no-op capture-and-redraw looked worse than off) — logged
there as a real, testable lead: forcing `r_ssaoDebug` on would show
whether the existing full-screen capture/redraw path's own "looks worse"
symptom correlates with SSAO specifically. **Currently untestable**:
forcing this (or any) dvar on x64 needs the real x64 `SetDvarBool`/
`SetDvarInt` native, which `known_issues_x64.md` issue #6 already
identified as missing entirely — this SSAO dvar set is a second,
independent, concrete reason to prioritize finding it (unlocks a real,
substantial, genuinely native visual-enhancement feature, not just the
three smaller forced-quality toggles issue #6 already named).

**Not yet found: the function that walks this table and issues the real
`CreateTexture`/`CreateRenderTarget` calls.** Real, multi-technique
effort spent on this specific sub-question this pass, all genuinely
exhausted for now:
1. `FindLeaRefsTo` (RIP-relative `LEA`) against the descriptor-pair table
   start `0x1404d0600` — zero matches.
2. Same tool against the name-pointer array start `0x1404d0680` — zero
   matches.
3. Same tool against what was believed to be the callback-pointer array
   start `0x1404d0718` — zero matches (and per the correction just above,
   this specific address turned out not to even belong to this table).
4. `FindMovImm64RefsTo` (absolute 64-bit immediate load, a real
   alternative addressing form to RIP-relative LEA) against
   `0x1404d0600` — zero matches.
5. Decompiling the four qwords at `0x1404d0718` directly (sidestepping
   the need to find a REFERENCE to the table at all) — real result, but
   a dead end for this specific question: a frustum-culling primitive,
   unrelated.

Five genuine, distinct technique attempts, all negative or off-target,
against this one specific sub-question — this is the kind of extended,
same-angle run `CLAUDE.md` §10.9 ("Fresh Perspective") flags as the
signal to stop guessing at more byte-pattern variants and either bring
in a different technique class entirely (a real, even if narrowly
scoped, Ghidra analysis pass — this project's `-noanalysis` convention
is a deliberate speed tradeoff, not an absolute rule, and a pass scoped
to just this table's containing `.data` region plus its nearby `.text`
would very likely resolve this in one shot where five rounds of
blind byte-pattern guessing haven't) or ask before continuing to sink
more effort into this one narrow angle. Genuinely valuable, unaffected
findings banked regardless (the full render-target name table itself,
the frustum-test primitive, the material/light-def asset-type
correction) — this is a dead end for ONE specific follow-up question,
not for the renderer-mapping effort as a whole.

## 6. What's still completely unmapped

- **The actual render-command consumer/backend** (section 4's open
  question #2) — nothing in this pass's trace reaches it.
- **Whoever actually performs the named phases in section 5's table**
  (`cell dyn brush/model`, `dpvs ent` culling, `spot shadow ent` gathering,
  `gen drawsurfs`) — confirmed NOT to be inline in `FUN_1401d7480` itself,
  almost certainly a separate thread. **Attempted this pass, inconclusive**:
  `FindCallersByName.java` against both `CreateThread` (a real KERNEL32
  import, confirmed present in the DLL import table) and
  `_beginthreadex`/`_beginthread` (not present as named external symbols at
  all — expected, since this binary statically links its CRT, no
  `MSVCRT.DLL`/`UCRTBASE.DLL` import exists) — `CreateThread` resolved as a
  real external symbol but came back with **zero callers**. This is a real
  negative result, but very likely a tooling artifact of `-noanalysis`
  mode rather than genuine evidence the game creates no threads via it:
  Ghidra's reference manager only sees a caller if the call site itself
  has already been disassembled into a real `Instruction`/`Reference`, and
  `-noanalysis` deliberately skips that broad a sweep for speed (this
  project's own established, deliberate tradeoff — see this directory's
  other RE docs for the same "-noanalysis, targeted scripts only"
  convention). **Two real paths forward, neither attempted yet**: (1) an
  IAT-slot-address AOB scan for indirect `CALL [rip+disp32]` patterns
  targeting `CreateThread`'s specific import-table slot (avoids needing a
  full analysis pass, reuses this project's own existing pattern-scan
  tooling class); (2) a live thread-enumeration diagnostic
  (`CreateToolhelp32Snapshot`/`Thread32First`, or simply logging each
  thread's start address via `NtQueryInformationThread`) — genuinely new
  live instrumentation, would need explicit agreement first per this
  project's own standing "no live diags without agreement" convention.
- ~~**Shadow-map rendering** specifically — not yet located at all.~~
  **RESOLVED this pass, see section 5b**: `$shadowmap_large`/
  `$shadowmap_small` are confirmed real entries in the generic
  render-target table alongside every other named target — shadow maps
  use the same creation mechanism as `$post_effect_0`/`$savedscreen`/etc,
  not a separate system. The actual table-CONSUMER function (which issues
  the real `CreateTexture` calls) is still not located — see section 5b's
  own "not yet found" paragraph for what was tried and why it's a
  separate, real open item from the shadow-map-specifically question this
  bullet originally asked.
- **Material/shader binding** — how a drawn surface's shader program and
  texture samplers actually get bound per-draw-call. Untouched this pass,
  though `FUN_1400a5a20` (section 5b's sibling investigation, see
  correction below) is confirmed to be this engine's generic named-asset
  lookup/registration function (type tag = `IW5::XAssetType`, cross-
  referenced directly against this project's own `tools/iw5oat` fork's
  `IW5.h` enum — type `5` = `ASSET_TYPE_MATERIAL`, type `0x16` = 22 =
  `ASSET_TYPE_LIGHT_DEF`) — a real, reusable anchor for future material-
  system RE, even though the two specific call sites that led here
  (`FUN_1401c48f0`/`FUN_1401c0550`, loading a fixed 72-entry built-in-
  material table and one built-in light-def) turned out to be generic
  engine-init material/light bootstrapping, NOT render-target creation as
  originally hoped when this thread was first chased.
- **Whether the render backend runs on its own thread on PC** (section 4's
  open question #2) — genuinely unknown, a real, checkable fact this
  project has never verified either way for this specific x64 binary.
- **The already-known pieces this doc should eventually cross-reference,
  not re-derive**: `InternalRenderScalePercent`'s own hook chain
  (`Hook_RenderResCompute` → `FUN_1401bd1d0`, `analog_input_hooks_x64.cpp`),
  the SAVED_SCREEN capture/quality-tier system (`known_issues_x64.md`
  issue #4's own trail), the POST_EFFECT_0/1 sampler setup
  (`FUN_140189c90`/`FUN_140189ed0`, also issue #4), motion blur's real x64
  trigger chain (`FUN_140194130`/`FUN_14018e720`/`FUN_14018def0`,
  `x64_feature_parity_audit.md` row #45), and the full UI/HUD draw
  pipeline (`ui_draw_pipeline_map.md`) — all of these sit somewhere
  relative to the command-buffer/stage-notify architecture mapped in
  sections 4-5 above, but their exact relationship (which numbered stage,
  if any, they correspond to; whether they're part of the command stream
  or a separate direct-call post-process layer bolted on afterward) has
  not been cross-checked yet.

---

*Status: early, first-pass mapping. Real architectural shape established
(command-buffer frontend/backend split, a stage-based notify/coordination
layer distinct from the actual draw submission) — the actual draw-call
path itself, shadow pass, and material/shader binding remain unmapped.
Continue from section 6's open items.*
