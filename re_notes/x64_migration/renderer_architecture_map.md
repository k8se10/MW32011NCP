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
remastered mod which does this via reshade." **Independently re-confirmed
by directly inspecting the shipped release archive** (`MW3 Remastered 10
1.5 2026-09-13T22-03Z YUAs0NSPB.zip`, 135 files, 59.5MB): the entire
package is a stock, unmodified ReShade `d3d9.dll` binary, the public
`reshade-shaders` community repository unmodified (standard textures —
blue noise, LUTs, dirt/bloom textures — accounting for nearly all of the
59.5MB), one `ReShadePreset.ini` config enabling a stock effect chain
(MartysMods Launchpad/MXAO/RTGI/SMAA, qUINT Debanding/Lightroom/DOF, Bloom,
AmbientLight, FakeHDR, DPX, Technicolor2, Vibrance, AdaptiveSharpen), and
one custom splash-screen bitmap for branding. Zero custom native code,
zero engine-specific integration of any kind, zero DLSS/DXVK/motion-vector
component (confirmed via a direct file-list search — no matches for any of
those). Even "RTGI" (MartysMods' shader, the closest thing to a "ray
tracing" claim in the enabled effect list) is a well-documented
screen-space depth-buffer ray-marching SSGI approximation, the same real
category as MXAO/SMAA — a genuine, respected ReShade effect, but not
remotely a renderer replacement. This is the single, complete, confirmed
technical basis for the "not much more than a reshade preset" comparison
this whole effort started from — not an impression, a fully verified fact. This project's own existing
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

- ~~**The actual render-command consumer/backend** (section 4's open
  question #2) — nothing in this pass's trace reaches it.~~ **RESOLVED this
  pass, 2026-09-23 — see the new section 7 below.**
- ~~**Whoever actually performs the named phases in section 5's table**~~
  **RESOLVED this pass, see section 7.** Kept below for the full
  investigation trail. **Whoever actually performs the named phases in
  section 5's table**
  (`cell dyn brush/model`, `dpvs ent` culling, `spot shadow ent` gathering,
  `gen drawsurfs`) — confirmed NOT to be inline in `FUN_1401d7480` itself,
  almost certainly a separate thread. **The prior "inconclusive" CreateThread
  investigation was resolved this pass (2026-09-23)** — confirmed a genuine
  tooling artifact, not evidence of anything: Ghidra's own reference manager
  only sees a caller once the call site has been disassembled, which
  `-noanalysis` mode skips. Fixed via approach (1) already flagged below (an
  IAT-slot AOB scan), but the actual IAT slot address first had to be
  computed correctly via a direct PE Import Directory parse (Python, not
  `dumpbin` line-counting by eye — a manual count off `dumpbin /imports`'
  printed order was tried first and got the wrong slot, `GetThreadPriority`'s
  not `CreateThread`'s; the lesson already written into `CLAUDE.md`'s own
  "checking is far cheaper than digging" section applies directly here —
  trust a real parse over a manual count). Real findings:
  - **A genuine generic worker-thread-pool spawning mechanism exists and is
    now mapped**: `FUN_140249e80(jobFuncPtr, workerSlotIndex)` calls
    `CreateThread(..., FUN_14024a810, workerSlotIndex, CREATE_SUSPENDED, ...)`
    — a single shared thread ENTRY point (`FUN_14024a810`) used for every
    worker slot, which does real per-slot TLS setup, calls a real per-slot
    init function (`FUN_1402ca370`, itself calling `FUN_14024a3d0` three
    times against three separate per-slot memory-arena tables — a real,
    reusable per-worker-slot state pattern), then jumps through the SAME
    job-function-pointer `FUN_140249e80` was given, passed via a small
    per-slot struct at `0x142005850 + slotIndex*8`. Each slot has its own
    tiny "spawn slot N" wrapper function (`FUN_14024a420` confirmed = slot
    5's wrapper) called via a tail-call `JMP` (not a plain `CALL` — a real,
    concrete reason the direct-E8-call scan alone wouldn't have found its
    own caller either, needed a `LEA`/tail-call-`JMP` scan too) from a real
    named subsystem-init function.
  - **Slot 5 identified, and it's a real, concrete NEGATIVE result, not the
    render worker**: its registrant (`FUN_1401a1be0`, VA `0x1401a1be0`)
    allocates a real, large (0x1900000 = ~26MB) buffer before spawning —
    initially a promising signal for a render command-buffer pool, but the
    actual job function (`FUN_1401a2960`) is unmistakably the **Bink Video
    background-streaming/decode worker** — real, direct calls to
    `BinkControlBackgroundIO`/`BinkClose`, cutscene-playlist-string parsing
    (`FUN_1402caa00` copying a 0x100-byte path buffer, `':'`-delimited
    parsing via `func_0x000140390ae4`). The 26MB allocation is Bink's own
    frame-decode buffer pool, not a render command buffer. A real, useful
    negative — rules out slot 5 specifically, doesn't touch slots 0-4.
  - **FOUND, same pass, immediately after slot 5**: of the remaining wrapper
    functions, three resolved to real, concrete, NON-render subsystems —
    slot 6 (`FUN_14024a450`) and slot 1 (`FUN_14024a4f0`) are generic
    multi-sync-event worker spawners, not yet individually traced further;
    slot 7 (`FUN_14024a590`, the only wrapper that raises thread priority)
    is the **real Win32 window-message-pump thread**
    (`FUN_14030fce0` → `GetMessageA`/`TranslateMessage`/`DispatchMessageA`,
    genuinely useful to know for this project's own `WndProc`-subclass input
    architecture, but not the render worker). **The real render/scene-setup
    worker was found via the fourth, parameterized wrapper**
    (`FUN_14024a600(jobFuncPtr, param_2)`, a generic "spawn slot N+2"
    variant supporting a variable slot count) — its own caller
    (`FUN_1401e9be0`) spawns **TWO** identical worker threads (slots 2 and 3,
    via a `for i in 0..2` loop) both running the SAME job function,
    `FUN_1401ea560` — see the new section 7 below for the full, now-complete
    trace from there down to the real, confirmed `gen drawsurfs`
    implementation. The investigation originally framed as "one of the
    remaining four, not yet confirmed" is now fully resolved, not a
    remaining open item.
  - **Tooling note for future sessions**: the repo-tracked
    `re_notes/ghidra_project_x64/iw5sp_x64_proj.gpr` project failed to
    resolve `-process iw5sp.exe` this session (`"Requested project program
    file(s) not found: iw5sp.exe"`, despite the project opening
    successfully and a prior session's own log showing this exact
    project/process combination working) — real cause not identified
    (a `WARN Using deprecated Mangled filesystem` line appeared this
    session that isn't present in that prior working log, a possible lead,
    not confirmed). **Worked around, not fixed**: this pass instead did a
    fresh one-shot `-import` into a new scratch project
    (`ghidra_scratch_proj`, in this session's own scratchpad directory, not
    committed) built directly from the real installed `iw5sp.exe`, which
    imported and scripted cleanly. The existing tracked project may need a
    fresh re-import in a future session if this recurs — flagged, not
    resolved, since the existing project's own accumulated function/label
    history (many prior sessions' work) would be lost if it's actually
    corrupted rather than just this one session's transient issue.
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
  **A real, live, cheap test shipped 2026-09-24, not yet run**: rather than
  another blind static pass, `g_mainThreadId` (captured in `DllMain`'s
  `DLL_PROCESS_ATTACH`, the real process main thread for an implicitly-
  linked DLL) is now compared against `GetCurrentThreadId()` inside
  `Hook_EndScene` every time it changes, logged as
  `[render-thread-diag] EndScene calling thread changed -> N (main thread =
  M, SAME/DIFFERENT)`. Direct motivation: the user's own real synthesis
  this session ("something changed on x64 from x86 draw pipeline which
  fundamentally broke the perf of the engine... not our work but the
  game") ties together every finding in `known_issues_x64.md` issue #4
  (pause-menu lag with simulation halted, the SP heli-sequence dip, MP's
  constant lag even in a private match) under one real, testable
  mechanism -- if `EndScene` genuinely fires on a dedicated backend
  thread, a regression there (lost parallelism, added synchronization,
  whatever it turns out to be) would explain all three without needing a
  content- or scripting-density explanation for any of them. If `EndScene`
  turns out to always run on `g_mainThreadId`, that's real, direct proof
  D3D9 submission is single-threaded on this x64 build, and the real next
  question becomes whether x86 had a genuine backend thread that x64 lost
  (a real, separately-answerable question via the existing x86 Ghidra
  project/binary snapshot this project already keeps). Build-verified
  (x64 Release, 0 errors, `dumpbin`-confirmed genuine x64 output),
  deployed live; a linkage bug (the new global landed inside this file's
  own anonymous namespace, the same class of bug `CLAUDE.md`'s "checking
  is far cheaper than digging" section already documents once) was caught
  at the LINK stage and fixed via a real external-linkage accessor
  function (`GetMainThreadId()`, matching the already-proven
  `IsDxvkActive()` pattern in the same file) before this shipped.

  **LIVE-TESTED, real and significant result, 2026-09-24.** `EndScene`
  genuinely does NOT run on the main thread for the vast majority of a
  session -- it settles onto a real, distinct, persistent second thread
  (TID `17452` this session, vs. main thread `19460`) within the first few
  frames after the hook first fires (the very first 1-2 `EndScene` calls
  land on the main thread before this settles, startup transient, not
  meaningful). **This is real, direct, live confirmation that D3D9
  submission on this x64 build IS backed by a genuine dedicated thread**,
  closing section 4's open question #2 in the affirmative.

  **Real, stronger finding on top of that**: across the whole session,
  `EndScene`'s calling thread flipped back to the main thread exactly
  twice after the startup settle (frame #2982, frame #4532) -- and BOTH
  times, the very next logged frame spiked to 100-165ms (vs. a normal
  ~16-35ms), before flipping back to the dedicated thread and normal
  framerate resuming. Two independent occurrences of the identical
  pattern is real, not noise. **One candidate correlate was checked and
  RULED OUT**: the `[x64-readyup]` glyph-gate diagnostic's `overlayOn=1`
  flag appears at both switch points, but a full history check shows it's
  ambient state (true almost immediately after startup once a controller
  connects, stays true the rest of the session) -- not a real transition,
  not the cause. **A real, not-yet-confirmed candidate found instead**:
  both switches are immediately preceded by a burst of
  `[x64-renderview-select-diag] view index changed` lines cycling rapidly
  through several distinct indices (this session: `1→2→6→7→8→6→8→1→2→1`)
  right before the thread flip and the frame spike -- a real, non-ambient
  signal (`x64-renderview-select-diag` doesn't fire on a fixed interval,
  only on genuine index changes per its own design, see issue #4's own
  entry on this diagnostic) worth chasing as the real trigger next, rather
  than another guess. Real, working hypothesis: whatever native event
  causes a burst of render-view reselection (plausibly a menu-adjacent or
  scene-transition event, not yet identified specifically) also forces
  that frame's `EndScene` back onto the main thread instead of the normal
  dedicated backend thread -- and THAT handoff itself is the expensive
  part, not the view reselection alone. Real next step: correlate the next
  live capture against what the player was actually doing at those exact
  frame numbers (this session's own capture wasn't annotated with player
  action), and/or trace `x64-renderview-select-diag`'s own call site to
  find what native condition drives a multi-index burst like this.
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

## 7. The real render/scene-setup worker thread, fully traced end to end — `gen drawsurfs` FOUND (2026-09-23)

**Direct instruction to keep pushing this exact question** (bundled with the
concurrent jitter/motion-vector research the same day — finding a stable
per-entity draw identity, needed for real per-object motion vectors, was the
original motivation for restarting this specific investigation). Full chain,
every link independently confirmed via decompile against the real x64
binary, not inferred:

1. **`FUN_140249e80(jobFuncPtr, slotIndex)`** — the generic worker-spawn
   utility (section 6). Calls `CreateThread(..., FUN_14024a810, slotIndex,
   CREATE_SUSPENDED, ...)`.
2. **`FUN_14024a600(jobFuncPtr, param_2)`** — the one PARAMETERIZED "spawn
   slot N+2" wrapper (distinct from the five fixed-slot wrappers already
   resolved — slots 1/5/6/7 all confirmed non-render). Its own caller:
3. **`FUN_1401e9be0`** — loops `param_2 = 0, 1` (real, confirmed: `while
   (uVar2 < 2)`), spawning **two** identical worker threads, slots 2 and 3,
   both running the same job function:
4. **`FUN_1401ea560`** — the real worker-thread main loop. Confirmed
   definitively (not just plausibly) to be the stage-dispatch worker: its
   own global addresses (`0x141cc2b10`, `0x141cc3898`, `0x141cc389c`) are
   the EXACT same addresses already independently documented in section 5's
   own (superseded-but-accurate) original pseudocode for the per-stage
   listener-count gate and the min-running-stage tracker. This loop picks
   the next pending stage ID (scanning the per-stage counter table) and
   calls:
5. **`FUN_1401e9c40(stageId, flushAll)`** — a real, generic ring-buffer
   event-queue drain: pops queued event records for the given stage
   (`0x141cc2b18` = ring buffer base, `iVar3` = per-stage record stride,
   `0x141cc2b00`/`+0x04` = read/write cursors) and, for each one, calls:
6. **`FUN_1401e9f70(stageId, eventRecordPtr)`** — **the real per-stage work
   implementation**, confirmed via decompile to be a giant (~2000-line)
   `switch(stageId)` whose case IDs match section 5's own stage-ID table
   exactly (case `0x10` = `skin model`, `0x11` = `add scene ent`, `0x12` =
   `gen drawsurfs`, `0x13` = `cell glass`, and lower cases for `physics`
   through `spot shadow ent`). **`gen drawsurfs`'s real case (`0x12`) is a
   multi-phase state machine** (a 0-3 phase counter stored in the event
   record itself, re-queuing itself via `FUN_1401e95b0(0x12, param_2)`
   until phase 3 — i.e. `gen drawsurfs` runs across multiple dispatch
   cycles, not in one shot), allocating draw-surface array slots from a
   large shared per-frame scratch buffer (`lRam0000000141896b80`, offsets
   up to `+0x110400`, entries at `(count + 0x4a80)*0x18` — a real,
   concrete, reusable fact: **draw-surface records in this buffer are 0x18
   (24) bytes each**) via helper functions (`FUN_1401a3c70` seen twice,
   `FUN_1401cfab0`, `FUN_1401bff30`/`FUN_1401bf210`).

**This resolves BOTH of section 6's "completely unmapped" items at once**:
the render-command consumer/backend (section 4's open question #2) and
whoever performs the named stage phases (section 5's open question) are the
SAME mechanism — a 2-worker job pool draining a shared stage-event ring
buffer, with `FUN_1401e9f70`'s giant switch statement as the actual backend
that turns culled/visible entities into real draw commands.

**Real, honest assessment against the original motivation (stable per-entity
identity for real, engine-sourced motion vectors, `vulkan_dlss_pipeline_research.md`
§2.6)**: case `0x12`'s own code, read this pass, does NOT itself show an
obvious per-entity iteration with a visible stable handle — it reads as
draw-surface-ARRAY-SLOT bookkeeping (how many slots were allocated, from
which shared buffer, this dispatch cycle), not a per-entity walk. The real
per-entity iteration is more likely to live in an EARLIER stage this pass
did not read in depth — case `0x03` (`cell scene ent`) or case `0x11` (`add
scene ent`) are the much stronger candidates by name alone, since "add
scene ent" is exactly the kind of stage that would walk a visible-entity
list and hand each one a stable slot/handle that `gen drawsurfs` then
consumes downstream. **Real, concrete, not-yet-done next step**: read case
`0x11` (found at file line ~1368 in this pass's own raw decompile dump,
`re_notes/x64_migration/stage_listener_invoker2.txt`) and case `0x03` with
the same scrutiny `0x12` just got, specifically looking for a per-entity
loop and what identity (index, pointer, handle) it assigns or reads per
entity — that identity, if stable frame-to-frame, is the real answer the
motion-vector work needs.

**Full raw decompile trail**: `re_notes/x64_migration/worker_pool_init.txt`,
`worker_wrappers_other4.txt`, `worker_slot_registrants2.txt`,
`worker_2pool_jobfunc.txt`, `stage_listener_invoker.txt`,
`stage_listener_invoker2.txt` (the ~2000-line `FUN_1401e9f70` dump — case
`0x12` starts at line 1578).

**Follow-up, same pass: case `0x11` (`add scene ent`) read — the per-entity
identity question is ANSWERED.** Case `0x11`'s real code (line 1368 of the
same raw dump) is a real, confirmed per-entity iteration — not one loop, but
**five separate entity-category loops**, each with the identical real shape:
a plain integer index (`uVar56`, incrementing per iteration, bounded by a
per-category count stored in the event record), gating each iteration on a
parallel byte array (`*pcVar52 == '\x01'`, an "active/visible this frame"
flag keyed by that same index), and — only for indices flagged active —
calling a real, category-specific processing function
(`FUN_1401c7aa0`/`FUN_1401c7b70`/`FUN_1401c7b30`/`FUN_1401a7450`, one per
category) that reads/writes a fixed-stride record array at that exact index
(confirmed strides: `0x90`/144 bytes at `0x141c23028`, `0x58`/88 bytes at
`0x141c36278`, `0x30`/48 bytes at `0x141c4de84`, plus two smaller
indirect-16-bit-index categories reading through
`lRam...887c30+0x380`/`+0x280`). **This IS the real, stable per-entity
identity this project's own motion-vector work (`vulkan_dlss_pipeline_research.md`
§2.6) needs**: a plain integer array index, per entity category, gated by
the engine's own already-maintained "active this frame" boolean array — not
something this project needs to invent or infer, the engine already tracks
exactly this. **Real, concrete implication for the motion-vector cache
design**: key the current/previous-frame transform cache by
`(entityCategory, arrayIndex)`, reading the SAME "active this frame" byte
array this stage already consults to know when an index's history is
invalid (a newly-active index this frame — i.e. was `\0` last frame, `\x01`
now — has no valid previous transform, exactly the `motionVectorsInvalidValue`
case Streamline's own contract already has a sentinel for). **Not yet
determined**: which of these five categories corresponds to which real
engine concept (dynamic/skinned models vs. FX vs. sound emitters vs.
something else) — the category at `0x141c23028` (stride 0x90, the largest
of the five, called via `FUN_1401c7aa0`, and the only one whose record
includes an explicit model-transform-adjacent field check, `puVar55 & 0x100000`)
is the strongest candidate for "dynamic models," worth confirming before
committing to it as the motion-vector work's real target. Not yet read:
case `0x03` (`cell scene ent`), which may feed these same index arrays one
stage earlier rather than being a separate identity scheme — a real,
cheap, worthwhile cross-check before starting the capture-cache
implementation, to confirm these two stages share one identity space
rather than each entity type having category-local, stage-local numbering.

---

## 8. First real x86-vs-x64 structural comparison, 2026-09-24 — `FUN_1401dfd80` vs. its documented x86 equivalent `FUN_0049bf50`

Decompiled `FUN_1401dfd80` (section 6's own render-view activator, found
while tracing the `[render-thread-diag]`/`[x64-renderview-select-diag]`
correlation in `known_issues_x64.md` issue #4) against its already-
documented x86 equivalent `FUN_0049bf50` (`known_issues.md` line 11032),
using the real, preserved pre-recompile binary
(`re_notes/x64_migration/binaries/old_x86/iw5sp.exe`) — the first genuine
side-by-side native-code comparison this whole draw-pipeline-regression
investigation has done, rather than reasoning from live symptoms alone.

A real structural difference exists, but it is NOT a clean, one-sided
"x64 added overhead" result:

- **x64**: real, inline D3D9 work directly in this function — a dedup
  guard (early-returns entirely if the requested view index matches the
  currently-active one; x86 shows no equivalent at this level), then a
  loop scanning up to 20 cached surface-pointer slots to invalidate stale
  references, then direct `SetRenderTarget`-/`SetDepthStencilSurface`-
  shaped D3D9 vtable calls, all inline in this one function.
- **x86**: dispatches through ~13 separate named helper functions
  (`func_0x00443cc0`, `func_0x004d8360`, `func_0x004e5b30`,
  `func_0x004a2a30`, `func_0x004c2710`, `func_0x0044d610`,
  `func_0x004c70a0`, `func_0x0052ae90`, `func_0x0048a350`,
  `func_0x00404860`, `func_0x004d6d70`, `func_0x004b15c0`,
  `func_0x00550d40`) with NO D3D9 vtable calls visible at this level at
  all — the real API work is one layer deeper in its own callees, most
  likely `func_0x0044d610` (shaped like a `SetRenderTarget` wrapper by
  its call position), not yet decompiled.

**Honest, unresolved question**: does x86's own callee chain contain an
equivalent cache-invalidation scan that just isn't visible at this
decompilation depth (meaning the two are functionally equivalent, just
structured differently — likely ordinary compiler/toolchain inlining
differences between the two builds, not a real regression), or is the
20-slot invalidation loop genuinely NEW work added on x64 specifically?
x64 also has a real, cheap early-out x86 doesn't show at this level,
which argues against a simple "x64 does strictly more work" reading —
if anything this one function looks MORE optimized on x64 in that
specific respect. **Real next step to actually settle this**: decompile
x86's own `func_0x0044d610` and its sibling callees to check whether the
same invalidation logic exists there before drawing any conclusion about
this specific function being the regression's real source. Not yet done.

**CORRECTION, same day, follow-up: the `FUN_0049bf50` "x64 equivalent"
label itself is wrong.** Decompiling the rest of `FUN_0049bf50`'s own
callees (`func_0x0044d610` above, plus `FUN_004c70a0`/`FUN_0052ae90`/
`FUN_0048a350`/`FUN_004d6d70`/`FUN_004b15c0`/`FUN_00550d40`) reveals
x86's `FUN_0049bf50` is genuinely a **per-surface DRAW dispatcher**, not
a render-target switcher at all: bind material (`func_0x0044d610`), bind
texture (`FUN_004c70a0`, its own real dedup+`SetTexture`-shaped vtable
call at offset `0x15c`), toggle a few dedicated render-state flags
(`FUN_0052ae90`/`FUN_004d6d70`/`FUN_004b15c0`, three near-identical
tiny wrappers each gated on a different flag byte), set a viewport/
scissor value (`FUN_0048a350`), then issue exactly ONE real
`DrawIndexedPrimitive` call (`FUN_00550d40`, vtable offset `0x148` =
slot 82 in the real IDirect3DDevice9 layout, confirmed by direct
vtable-slot arithmetic, not a guess). This is a completely different
responsibility from x64's `FUN_1401dfd80` (a genuine
`SetRenderTarget`/`SetDepthStencilSurface` framebuffer switcher). **The
"x64 equivalent of x86's own documented `FUN_0049bf50`" label carried in
`known_issues_x64.md` since an earlier session was never independently
re-verified at the decompile level and does not hold up** -- both
functions get reached from broadly similar contexts (a per-view/per-pass
setup chain) and were paired on that positional/contextual similarity
alone, not confirmed behavioral equivalence. **Real, standing
methodological lesson**: verify a claimed x86≈x64 function pairing by
actually decompiling both sides before building further comparative
analysis on it, the same "checking is cheaper than digging" principle
`CLAUDE.md` already documents, applied here to a cross-architecture
claim rather than a single-binary one.

**Net effect**: the real x86 equivalent of x64's `FUN_1401dfd80` (if a
directly comparable render-target-switching function exists in the x86
build at all) is still genuinely unidentified via `FUN_0049bf50`'s own
callees -- but see the real, independently-verified match found below via
a fresh anchor.

**REAL MATCH FOUND, same day, via a different anchor already on record**:
`known_issues.md` line 11146 already documents a completed x86-side
vtable-dispatch scan concluding **`FUN_00542cb0` is the ONLY real
`SetRenderTarget` call site in the entire x86 binary** -- a genuine,
pre-existing anchor this session had not yet cross-referenced. Decompiled
it directly (`re_notes/x64_migration/binaries/old_x86/iw5sp.exe`):

```c
void FUN_00542cb0(int param_1,int param_2)
{
  piVar1 = *(int **)(param_1 + 0xc0);
  iVar2 = *(int *)(param_2 * 0x14 + 0x24bf484);
  param_2 = param_2 * 0x14;
  if (*(int *)(*(int *)(param_1 + 0xb44) * 0x14 + 0x24bf484) != iVar2) {
    (**(code **)(*piVar1 + 0x94))(piVar1,0,iVar2);      // SetRenderTarget
    ... // reset a handful of tracking fields
  }
  if (*(int *)(*(int *)(param_1 + 0xb44) * 0x14 + 0x24bf488) != *(int *)(param_2 + 0x24bf488)) {
    (**(code **)(*piVar1 + 0x9c))(piVar1,*(int *)(param_2 + 0x24bf488)); // SetDepthStencilSurface
  }
}
```

**This is a genuinely, behaviorally verified match, not another
positional guess**: indexes a table (`0x14`=20-byte stride, the x86
equivalent of x64's `0x20`=32-byte stride -- the size difference is
consistent with 32-bit vs. 64-bit pointer fields, not evidence of a
different table shape), compares the CURRENTLY-active index's stored
target/depth-stencil pointers against the REQUESTED index's, and
conditionally calls the exact same two real D3D9 methods x64's function
calls (`SetRenderTarget` at vtable `+0x94` = slot 37, `SetDepthStencilSurface`
at vtable `+0x9c` = slot 39 -- both confirmed by direct vtable-slot
arithmetic against the real IDirect3DDevice9 layout).

**The real, confirmed difference**: x86's version is SIMPLER than x64's.
It has NO equivalent of x64's 20-slot cache-invalidation scan (the loop
checking up to 20 cached surface pointers for a match and invalidating
stale references via two additional vtable calls at `+0x1c8`/`+0x208`)
and no equivalent of x64's early-out dedup-on-unchanged-index guard.
x86 just does the two straightforward compare-and-call checks, nothing
else. **This is real, concrete, independently-verified evidence that x64
does genuinely MORE work than x86 for the identical operation** -- the
first solid confirmation in this whole investigation that something
concrete changed in the draw-submission path itself, not just an
inference from live symptoms.

**Honest scope of what this does and doesn't prove**: a 20-iteration
pointer-comparison loop plus two extra vtable calls is not, on its own,
a plausible explanation for a 100-165ms frame spike -- that's a
microsecond-scale cost at most, even run a handful of times per frame.
This extra x64 logic is more likely a SYMPTOM of a real, larger design
change (e.g. x64 needing to actively track/invalidate stale render-target
references because something about resource lifetime management changed
in the recompile) than the direct cause of the spike itself. **Real next
step**: find what actually populates/invalidates those 20 cached slots
elsewhere in the x64 binary, and whether an entry actually gets
invalidated (not just scanned) during the exact frames the
`[render-thread-diag]`/`[x64-renderview-select-diag]` burst was captured
-- that's the real mechanism worth chasing now, not the loop's raw
iteration cost.

---

*Status: early, first-pass mapping. Real architectural shape established
(command-buffer frontend/backend split, a stage-based notify/coordination
layer distinct from the actual draw submission) — the actual draw-call
path itself, shadow pass, and material/shader binding remain unmapped.
Section 7 (2026-09-23) resolves the render-command consumer/backend and the
named-stage-worker questions completely; the per-entity-identity question
for `gen drawsurfs`/`add scene ent` specifically is the new, narrower open
item to continue from.*
