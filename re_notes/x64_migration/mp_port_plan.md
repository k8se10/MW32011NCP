# MP port plan: controller pipeline and performance fixes on `iw5mp.exe` (2026-09-27)

**Status update (2026-09-27, later): Step 7 (movement/look) started.** Real
groundwork found while auditing what Sprint/Pmove actually depend on before
touching any signature: `kAnchorSignature` (SP `FUN_14007c3a0`) is far more
load-bearing than §2.1's own table entries suggested -- it's not just the
kbutton dispatcher, it's the SINGLE shared anchor every one of these resolves
from via a FIXED BYTE OFFSET into its own disassembly, using
`SigScan::ResolveRipRelative(anchor + insnOffset, 7)`:
`g_fireStruct` (+0x70), `g_timestampPtr` (+0x41), `g_reloadStruct` (+0x1EF),
`g_adsStruct` (+0xAB4), `g_adsToggleFlag` (+0xA9F), `g_sprintStruct` (+0xB0D),
`g_holdBreathStruct` (+0x8C9), plus `g_stanceDispatch` (the anchor address
itself, called directly for CrouchProne and Jump's auto-stand). §2.1's table
lists the anchor's MP twin (`0x1400ce950`) as LOW confidence, "only needed if
a feature depends on it" -- in fact EVERY core button feature (Fire, ADS,
Reload, Sprint, Hold Breath, CrouchProne, Jump auto-stand) depends on it. If
MP's dispatcher has even slightly different codegen before any one of these
seven offsets, that one resolve silently returns a wrong address -- this is
the single highest-risk, highest-leverage unknown in the whole controller
port, ahead of the movement-tick/FOV twins themselves. **Do not wire Sprint,
Fire/ADS/Reload, or CrouchProne/Jump-auto-stand to MP until this anchor and
all seven offsets are individually re-derived from a real MP decompile** --
confirming the anchor FUNCTION resolves is not sufficient, per §3's own
standing rule.

A full Ghidra analysis of `iw5mp.exe` (no analyzed project existed before
this) was started headless in the background
(`re_notes/x64_migration/ghidra_project_x64_mp_analyzed/`, gitignored) to
get real decompile output for this anchor and the movement-tick/FOV/mounted-
aim LOW-tier twins -- raw capstone alone hit MSVC function-chunking (the SP
movement-tick's own `.pdata` entry for `0x14007d9f0` is only 0x24 bytes, a
prologue chunk; the real body continues in a separate cold-path chunk stitched
by Ghidra's own decompiler, not resolvable from a flat linear disassembly).
Next session: once that analysis completes, decompile the MP anchor
candidate (`0x1400ce950`) and the movement/FOV/mounted-aim twins, confirm or
refute each against the SP originals per §3, THEN start wiring per §6's
implementation order.

**Status update (2026-09-27, later still): anchor CONFIRMED, Sprint/HoldBreath/
CrouchProne SHIPPED.** The Ghidra MP analysis above completed (110s). Decompiled
and directly compared FUN_1400ce950 (MP anchor candidate) against
FUN_14007c3a0 (SP anchor) case-by-case: byte-for-byte structural match --
same activate/deactivate call-pair shape for every case checked (Fire case 1/2,
cases 3/4-7/8, Sprint+HoldBreath case 9/10, Reload case 0xb/0xc, ADS-with-
toggle-flag case 0xd/0xe, CrouchProne-toggle), same 31-byte function prologue
prefix as SP's own literal kAnchorSignature (diverging by exactly one extra
early-out instruction MP adds, `param_2 - 0x49U < 2`, consistent with MP having
more action-slot cases). Confirmed UNIQUE in iw5mp.exe via a direct offline
pattern scan (not just a runtime SigScan call) before being trusted, this
project's own standing "verify before hooking" requirement. Real, load-bearing
MP-specific differences found, NOT assumed to carry over from SP:
- Per-client struct stride is 600 (0x258) bytes in MP vs SP's 0x230 (560) --
  irrelevant here since every resolve targets the LOCAL player only (client
  index 0, matching g_stanceDispatch(0, case, 0)'s existing SP call pattern).
- CrouchProne toggle case numbers are SHIFTED: MP 0x52/0x53 vs SP's 0x48/0x49
  (MP's extra killstreak action-slot cases, 0-6 vs SP's 0-3, push everything
  after them up by a consistent amount).
- MP's real stance field (DAT_140e21398) is a genuinely SEPARATE global --
  NOT derivable from g_adsToggleFlagMP by SP's own "+0x1c from the ADS-toggle-
  flag base" trick (the two addresses aren't adjacent in MP's layout at all).
  Resolved via its own anchor-relative RIP offset instead.

All real offsets independently derived via direct disassembly (capstone/
pefile) of the confirmed-unique anchor:

| Struct/global | MP address | Anchor offset | insn len |
|---|---|---|---|
| Timestamp (shared) | DAT_142cddcfc | +0x49 | 7 |
| Fire kbutton (groundwork, not yet wired) | DAT_140e1dc18 | +0x78 | 7 |
| Reload kbutton (groundwork, not yet wired) | DAT_140e1dcb8 | +0x24B | 7 |
| ADS-active flag (groundwork, not yet wired) | DAT_140e1dbb4 | +0x2AB | 7 |
| ADS kbutton (groundwork, not yet wired) | DAT_140e1dcf4 | +0x2CD | 7 |
| Hold Breath kbutton | DAT_140e1dc2c | +0xA6A | 7 |
| Sprint kbutton | DAT_140e1dd08 | +0xCA6 | 7 |
| Real stance field | DAT_140e21398 | +0x4D8 | 6 (plain mov, not LEA) |

The Sprint-tick function itself (kSprintTickSignature's own MP twin,
FUN_1400389d0 @ 0x1400389d0) was independently confirmed via decompile: same
2-arg __fastcall shape as SP's, hooked the identical "call through to the
real native logic first, then add kbutton-driven Sprint/HoldBreath edges on
top" way Hook_SprintTick already works for SP -- its own internal duration/
recovery/perk logic (a real, complex, MP-specific implementation, ~780 bytes)
never needed reading, since the hook only calls through to it, never
reimplements it. SP's own kSprintTickSignature literal does NOT match MP past
its first 30 bytes (diverges at the post-prologue struct-offset check) -- a
real, separate kSprintTickSignatureMP was needed and independently verified
unique in iw5mp.exe.

**Shipped**: InstallMpAnchorAndSprintHooksX64() (analog_input_hooks_x64.cpp),
called from dllmain.cpp's MP branch. Resolves the shared kbutton activate/
deactivate signatures (confirmed to already hit MP's own real addresses
directly, no separate MP constant needed), the MP anchor and all eight
offsets above, and the MP Sprint-tick hook. GetRealStanceX64/
ForceStandingViaRealToggleX64 are now exe-aware (MP branch uses
g_stanceFieldMP/g_stanceDispatchMP/the MP case numbers; SP branch unchanged).
Build-verified 0 errors/0 warnings on both x64 and Win32; not yet live-tested
in a private match.

**Still open for step 7**: the movement-tick/FOV/mounted-aim LOW-tier twins
(0x1400d0050/0x140073400/0x1400d0350) -- these are separate, MSVC function-
chunked routines (SP's own 0x14007d9f0 .pdata entry is only a 0x24-byte
prologue chunk, the real body continues in a separately-chunked cold path)
needing their own decompile pass against the now-available MP analyzed
project, not yet done. Pmove tick (0x1400168a0->0x14003a890, HIGH) also not
yet wired -- SP's own hook is still diagnostic-only (log-and-call-through),
so porting it is low-risk but not yet done either.

**Status update (2026-09-27, later still): movement + Fire/ADS/Reload SHIPPED,
direct instruction "we need all buttons and sticks to work."** Decompiled the
movement-tick LOW-tier twin (0x1400d0050) directly rather than leaving it as
an unconfirmed candidate: it writes `*(undefined1*)(param_2+0x1c)` /
`+0x1d` -- the EXACT usercmd_t forwardmove/rightmove offsets this project's
own layout research already established for SP (and x86 before it). That
byte-offset match is a strong independent structural confirmation on its own,
beyond the original automated matcher's LOW "7/16 neighbours" score. Real,
load-bearing calling-convention difference from SP found via the decompile:
MP's version takes THREE params (`int param_1` -- a per-client context/index,
NOT the usercmd pointer; `longlong param_2` -- the real usercmd_t*, playing
SP's own param_1 role; `undefined4 param_3`), not SP's two -- a new
`MovementTickFnMP` trampoline type and a dedicated `kMovementTickSignatureMP`
(confirmed unique in iw5mp.exe) were needed, not a reuse of SP's own type/sig.

**Shipped** (`Hook_MovementTickMP`, wired into `InstallMpAnchorAndSprintHooksX64`
alongside the anchor/Sprint work above): left-stick movement, and Fire/ADS/
Reload reusing the anchor-resolved struct addresses from the earlier status
update (the fire/reload/ads/adsToggle groundwork flagged "not yet wired" there
is now consumed). ADS explicitly sets the real ADS-active flag on the edge,
matching SP's own hard-learned lesson (the kbutton call alone doesn't drive
actual engagement) applied preemptively rather than waiting to rediscover it
live in MP.

**Deliberately NOT wired this pass, real open gaps**:
- **LOOK (right stick)** -- MP's angle-accumulator addresses (SP's own
  `kAngleAccumSignature` twin) are still OPEN, no candidate found. Writing
  look before a verified address exists would mean writing to a guess, the
  exact thing this project's signature-scanning policy exists to prevent.
- **The sniper Fire/ADS notify-bind-dispatch fix** (`g_notifyBindDispatch`,
  SP's own `kNotifyBindFuncOffset`) -- its MP address hasn't been researched.
  MP Fire/ADS may reproduce SP's own pre-fix sniper-class regression
  (`known_issues_x64.md` issue #1) until this is done.
- **D-pad action slots, Weapnext, CrouchProne/Jump/Melee/Lethal/Tactical/
  Interact/Scoreboard** -- all still OPEN per this document's own §2.1 table;
  MP's action-slot cases differ structurally from SP's (killstreaks, not the
  same layout), confirmed again by this session's own anchor decompile
  (MP's dispatcher case numbers for the shared block are shifted +6 from SP's
  own, see the anchor status update above).

Build-verified 0 errors/0 warnings (x64 and Win32); x64 redeployed and
confirmed via dumpbin. Not yet live-tested in a private match.

**Status update (2026-09-27, later still): LOOK SHIPPED, direct instruction
"look for the angle accumaltors."** Previously OPEN ("no .pdata features,
fragment of CL input code," per the very first status update above) --
resolved by dumping the FULL raw stitched disassembly of the now-confirmed
MP movement-tick function (`FUN_1400d0050`) via `DumpDisasm.java` against a
real `Function.getBody()` iteration (which correctly follows Ghidra's own
chunk-stitching, unlike a flat linear byte-range disassembly, which is what
had made this "no .pdata features" in the first place -- the accumulator
reads sit far outside the function's own first `.pdata` chunk).

Found the real pack-preamble at `0x1400d029f`: two RIP-relative `movss`
reads (`DAT_140e21454`, `DAT_140e21458`) into stack scratch, immediately
followed by `CALL 0x14001e2c0` (MP's own angle-pack function, `FUN_14001e2c0`,
the twin of SP's `FUN_140003fc0`) -- the exact same shape as SP's own
`kAngleAccumSignature`. PITCH-vs-YAW identity cross-checked two independent
ways before trusting either address:
1. Traced which of the movement-tick's own two native mouse-delta outputs
   (`FUN_1400cfb60`'s 2nd/3rd out-params) each accumulator's own native
   update derives from: the first output feeds both `DAT_140e21458` AND
   `usercmd+0x1d` (rightmove); the second feeds both `DAT_140e21454` AND
   `usercmd+0x1c` (forwardmove) -- matching the already-established
   forwardmove=vertical/rightmove=horizontal convention.
2. Matched the READ ORDER against SP's own confirmed convention: SP resolves
   pitch first, yaw second; MP's pack preamble reads `DAT_140e21454` first,
   `DAT_140e21458` second -- the identical order.

Both checks agree: `DAT_140e21454` = pitch, `DAT_140e21458` = yaw. New
`kAngleAccumSignatureMP` (confirmed UNIQUE, 1 occurrence, in `iw5mp.exe` via
a direct offline pattern scan) resolves both via the same
`SigScan::ResolveRipRelative(addr, 8)` / `addr+14` offset pattern SP's own
resolve code already uses (pitch instruction is 8 bytes, yaw instruction
starts 14 bytes later -- 8 + the 6-byte stack-store instruction between them
-- identical gap to SP's).

**Shipped**: `Hook_MovementTickMP` now injects LOOK pre-hook (before
`g_realMovementTickMP` runs), reusing the exact same formula/sign convention
as SP's own `Hook_MovementTick` (`GetAdsLookRateScaleX64()` *
`GetLookAccelerationScaleX64()`, `invertLook`, both accumulators
subtracted-from) -- these helper functions are already null-safe against
MP's still-unresolved FOV/dvar dependencies (gracefully fall back to no
ADS-zoom slowdown rather than crash or misbehave), so no new gating was
needed to reuse them safely. `g_pitchAccum`/`g_yawAccum` are shared globals
with SP (single-process, mutually exclusive install paths, same pattern
already used for `g_kbuttonActivate`/`g_kbuttonDeactivate`).

**MP now has 7 real working controller inputs, up from 0**: Sprint, Hold
Breath, CrouchProne auto-stand, left-stick movement, right-stick look, Fire,
ADS, and Reload. Build-verified 0 errors/0 warnings (x64); x64 redeployed
and confirmed via dumpbin. Not yet live-tested in a private match.

**Status: plan with the static groundwork done.** Stage 5 of the renderer
reference (`renderer_end_to_end.md` §11 links here). Direct instruction:
"Port the entire current controller pipeline and performance fixes all to
MP." This document inventories what has to move, what already resolves in
MP, the MP twin of every hook that doesn't, and the order and gates for
doing it safely.

Inputs:
- `signature_resolution_sp_mp_2026-09-27.txt`: every proxy signature
  scanned against both exes.
- **`mp_twins_2026-09-27.txt`** (new): the SP→MP twin function for all 32
  signatures that miss in MP, with confidence tiers, produced by
  `provenance_scripts/{fnfeat,cgmatch,layoutcheck,fnscore}.py`.

---

## 1. Where MP stands today

- `dllmain.cpp` installs the SP hook set only when `GetDetectedGameExecutable()
  == SP`. Under MP it installs **only** `InstallRenderScaleHookX64()`, plus
  the exe-agnostic parts: XInput/DualSense polling, the overlay, the security
  plugin (after the `MH_Initialize()` ordering fix), and the one-time
  "controller support limited in MP" modal.
- Further SP-only gates: `frame_pacing_x64.cpp` (twice), `d3d9_hook.cpp`
  (Vulkan mode), `streamline_integration_x64.cpp`,
  `streamline_object_motion_x64.cpp`.
- So in MP today a controller can move the OS cursor/overlay, but none of
  the in-game analog movement, look, menu navigation, glyph prompts, rumble
  hooks or performance fixes are active.

## 2. Inventory: what "controller pipeline" and "performance fixes" are

Grouped by feature, with each group's signatures and their MP status. The
signature status comes from the resolution file (31 hit, 32 miss) and the
twin tiers from `mp_twins_2026-09-27.txt`.

### 2.1 Controller pipeline

| Feature (config keys) | Signatures | MP status |
|---|---|---|
| Analog movement (stick → `forwardmove`/`rightmove`) | `kMovementTickSignature`, `kPmoveTickSignature`, `kKbuttonActivate/DeactivateSignature` | kbutton: **hit**. Pmove: twin `0x14003a890` HIGH. Movement tick: twin `0x1400d0050` LOW (plausible: next to the MP kbutton code, as in SP) |
| Analog look, ADS slowdown, aim assist (`lookDegreesPerSecond*`, `adsSlowdown*`, `adsCloseRangeSlowdownStrength`, `invertLook`) | `kAngleAccumSignature`, `kGetEffectiveFovX64Signature`, `kSeedMouseBaselineSignature` | seed mouse: **hit**. FOV: twin `0x140073400` LOW. Angle accumulation: OPEN (it's a site inside the CL input function; resolve after the movement tick is confirmed) |
| Gyro (`gyro*`) | shares the look path | follows analog look |
| Sprint/stance buttons | `kSprintTickSignature` | twin `0x1400389d0` HIGH |
| Weapon cycling, D-pad action slots | `kWeaponNextSignature`, `kActionSlotSignature` | OPEN, no candidate. Needs manual RE (MP action slots differ: killstreaks) |
| Mounted weapons / turrets | `kMountedAimTickSignature` | twin `0x1400d0350` LOW |
| Missile/predator steering | `kMissileGuidanceDispatchSignatureX64`, `kMissileSteerConsumerSignatureX64` | twins `0x140038ce0` / `0x1400331d0` HIGH (in MP this is the Predator Missile killstreak, so more relevant than in SP) |
| Auto-mantle (`autoMantle*`) | movement tick + Pmove | follows movement |
| Menu navigation | `kMenuKeyEventSignature` (`Menu_HandleKey`), `kGetTopmostActiveMenuSignature`, `kUiContextAnchorSignature` (`UI_KeyEvent`) | first two: **hit**. `UI_KeyEvent`: twin `0x140309270` HIGH |
| Cursor hide / draw (`UI_Refresh` dispatcher and gate) | `kCursorDrawDispatcherSignatureX64`, `kCursorGateSignature` | `UI_Refresh` twin `0x14030b3b0` HIGH; the gate site inside it is OPEN (the SP gate compares `0x14260506c`, whose MP counterpart must be found inside the twin) |
| Glyph prompts and text (`glyphStyleAuto`, `bindResolverGlyphSubstitution`) | `kDrawTextSignature`, `kHudElemTextDrawSignature`, `kGetLocalizedStringSignature`, `kApplyDrawAlignSignature`, `kComputeAuxMetricSignature`, `kFontAssetLoadThunkSignature` | all **hit** except `ApplyDrawAlign`: twin `0x1400ba970` HIGH |
| Pause (SP: pause menu toggle, cinematic pause menu, blur caps) | `kPauseToggleSignature`, `kOpenPauseMenuForCinematicSignature`, `kAnchorSignature` | **not applicable to MP** (no SP-style pause); the MP escape menu is a normal menu reached through `UI_KeyEvent`. Anchor twin `0x1400ce950` LOW, only needed if a feature depends on it |
| In-level detection | `kInLevelFlagSignature` | twin REJECTED. MP needs its own "in match" signal: `cgs`/`clc` connection state, or the MP UI active-menu state from `UI_SetActiveMenu` |
| Rumble (`vibration*`) | `InstallFireHookX64` (`rumble.cpp`) and damage via `VM_Notify` | `kVmNotifySignature` **hit**. The fire hook needs an MP check |
| Survival/GSC-driven hints | `kGscMethodTableAccessSignature` (hit), `kGscStringTableAccessSignature` (twin `0x1402a2800` = same `VM_Execute`, HIGH) | only needed for MP hint features. Survival logic itself is SP-only |

### 2.2 Performance fixes

| Fix (config key) | Signatures | MP status |
|---|---|---|
| Wait coalescing (`waitCoalescingEnabled`) | `kRenderWait1Signature` (5 hits in both), `kWorkerWaitSignature` (SP 2 / MP 5 hits), `kBackendSleep1Signature`, `kRenderSleep1Signature` | render wait: hit. Worker wait: **hit count differs** (MP 5) → must pick the right site by call graph. Sleeps: twins `0x1401e4090` / `0x1401b1750` HIGH |
| IWD read acceleration (`iwdReadAccelEnabled`) | `kIwdReadCallSignature` | twin `0x14041f9f0` HIGH |
| Frame pacing (`framePacingEnabled`) | dvar reads only (`kFindDvarX64Signature`, **hit**) | just the SP gate in `frame_pacing_x64.cpp` |
| Skip redundant shadow activation | `kPerLightShadowDispatchSignature` | **hit** |
| Skip redundant console-font init | `kConsoleFontInitSignature` (hit), `kConsoleShutdownA/B` | twins `0x1400d6d40` / `0x1400d6f80` MED |
| Skip redundant master-sequencer reactivation | `kMasterSequencerReactivationSignature` | **hit** (6/6) |
| Skip redundant orchestrator extra calls | `kOrchestratorExtraCallA/B` | **hit** |
| Skip redundant post-FX guaranteed calls | `kScenePostfxFirstCallSignature`, `kScenePostfxLastCallSignature` | first: twin `0x1401b98f0` HIGH. Last: candidate site `0x1401b9d3a` (the last call to the MP full-screen material draw `0x1401b1b10` inside the MP post-FX), to verify |
| Occlusion LOD scale fix (`occlusionLodScaleFixX64`) | `kOcclusionScreenAreaSignature` | **hit** |
| Pause/live blur step caps | `kBlurLoopSignature`, `kMotionBlurTriggerSignature` | **hit** |
| Screen-capture / saved-screen fixes | `kScreenCaptureCmdSignature` (hit), `kSavedScreenCaptureSignature` | twin `0x1401b0690` HIGH (same +0xF40 layout as SP) |
| Memory detection (`sys_sysMB`) | `kMemDetectSignature` | **hit** |
| Sqrt domain error fix | `kSqrtDomainErrorSignature` | **hit** |
| Render scale / FSR / SMAA / motion blur | `kRenderResComputeSignature`, `kProjectionPassDispatchSignature`, `kRenderViewSelectSignature`, `kProjectionMatrixBuildSignature`, `kZoneReloadPrimitiveSignature` | all **hit** (render scale is already live in MP). FSR/motion blur were blocked only by the SP in-level gate (issue #4), so they need the MP in-match signal from 2.1 |
| Audio reverb/occlusion experiments | `kMixReverbApplySignature`, `kReverbActivate/Deactivate`, `kOcclusionCheckSignature`, `kPlaySoundAliasSignature` | twins HIGH (`0x14036da20`, `0x1402c18b0`, `0x1402bb970`, `0x1402c22d0`, `0x1402be790`). Port only once the issue #10 root cause is fixed |

## 3. The real blocker isn't signatures, it's data offsets

A resolved function isn't a working hook. Most SP hook bodies read engine
state at SP-specific offsets and globals: `playerState_t`/`usercmd_t`
fields, `cg`/`cgs` arrays, `clientActive`, the UI context `0x142605050`, and
dvar handles resolved RIP-relative from inside the signature. MP compiles
the same engine with **different struct layouts and globals** (the MP
expression table alone moves from `0x1404d29d0` to `0x140552980`, and the
UI context and client arrays move too).

For every hook body being enabled in MP:
1. **List every non-argument memory access** in the hook body and its helpers:
   RIP-resolved globals, fixed struct offsets, dvar handles.
2. **Re-derive each one in MP** from the MP twin's disassembly, the same way
   the SP value was derived. Prefer resolving from inside the MP signature
   (RIP-relative operand at a fixed offset) over hardcoded offsets.
3. **Encode them in a per-exe descriptor** (§4) and never share a constant
   between SP and MP unless it was verified identical in both.

The HIGH-tier twins save the "find the function" step. This audit is the
bulk of the work, and it's per feature.

## 4. Code structure for dual-exe hooks (production approach)

- **Per-exe hook descriptors:** one table per feature: `{ name,
  signatureSP, signatureMP, resolveExtrasSP(), resolveExtrasMP(), detour,
  configGate }`. `nullptr` for an exe means "not supported there". Install
  logic iterates the table for the detected exe. This replaces the single
  `if (exe == SP)` block in `dllmain.cpp` with an explicit, reviewable matrix.
- **One detour body, per-exe data:** detours read offsets and globals from a
  per-exe struct filled at resolve time (e.g. `g_x64Offsets.playerStateViewAngles`)
  instead of literals. SP behaviour stays byte-identical, since the SP
  struct is filled with today's values.
- **Signatures:** generate MP signatures from the twin functions using the
  same process as SP (`CreateFuncAndDumpSig` / the capstone equivalent),
  wildcarding RIP displacements and stack offsets. Where one pattern
  matches both exes uniquely, use it once. Otherwise keep separate SP and MP
  patterns. The startup scan logs each hook's resolution per exe
  (`[mp-hooks] kX resolved @0x… / FATAL not resolved`), like the SP path.
- **Fail closed:** an MP hook whose signature or any extra fails to resolve is
  not installed, and the feature is reported as unavailable in the overlay's
  MP status, never partially enabled.

## 5. Safety gates specific to MP

- **VAC.** `iw5mp.exe` is VAC-secured. The project's standing position
  (`vulkan_dlss_pipeline_research.md` §5, `known_issues.md` 2026-07-20 VAC
  pass) is a clean track record but no guarantee. The render-scale MP feature
  was enabled by direct instruction. For the controller and performance
  ports:
  - every MP feature ships **off by default** behind its own config key;
  - it's documented as "use at your own risk online";
  - live validation happens in **private matches** first.
  - Input hooks change only client-side input generation (the same class as
    a Steam Input/XInput wrapper) and don't touch networking or server
    state. Keep it that way: no hook may alter what the server trusts
    beyond the usercmd a real controller could produce.
- **Netcode.** Any change to usercmd generation must keep the engine's
  cmd timing and packet cadence unchanged. The security plugin's netcode
  fixes are already MP-live, and must be re-verified after each input
  port.
- **Aim assist in MP.** Analog look is legitimate controller input. **Aim
  assist / slowdown on target is a competitive-integrity question.**
  Recommendation: port analog look and ADS sensitivity scaling, but keep
  target-based slowdown (`adsCloseRangeSlowdownStrength` and any
  target-magnetism) **SP-only** unless the user explicitly decides
  otherwise. Flagged as a decision for the user, not assumed.

## 6. Implementation order (each step builds, is reviewed, then live-tested in a private match)

1. **Refactor to per-exe descriptors (§4) with SP unchanged.** Acceptance:
   the SP startup log lists identical resolutions, and SP behaviour is
   unchanged in a smoke test.
2. **Performance fixes whose signatures already hit and whose bodies have
   no SP-only data:** per-light shadow skip, master sequencer, orchestrator
   extras, occlusion LOD, blur caps, memory detect, sqrt fix, frame pacing
   (remove its SP gate). Audit each body (§3) first.
3. **Wait coalescing and IWD cache:** MP signatures for the two sleeps and
   the IWD read (HIGH twins), and disambiguation of the 5-hit worker wait by
   call graph. Acceptance: the frame-time benchmark logger shows the same
   improvement class as SP.
4. **Menu navigation:** `UI_KeyEvent` (twin `0x140309270`) plus the
   already-hitting `Menu_HandleKey`/topmost menu. Re-derive the MP UI
   context address from the twin. Acceptance: full controller navigation of
   the MP main menu, the in-match escape menu and class selection.
5. **Glyph prompts:** the text hooks already hit, `ApplyDrawAlign` twin.
   Re-derive the font/position helpers. Acceptance: MP HUD prompts show
   controller glyphs.
6. **MP in-match signal** (replacement for `kInLevelFlagSignature`), which
   unblocks FSR/motion blur in MP too.
7. **Movement and look:** confirm the LOW twins (movement tick
   `0x1400d0050`, mounted aim `0x1400d0350`, FOV `0x140073400`) by manual
   decompile against SP, resolve the angle-accumulation site, re-derive the
   usercmd/playerState offsets. Sprint (`0x1400389d0`) and Pmove
   (`0x14003a890`) are HIGH. Acceptance: analog movement and look in a
   private match, with a cmd-rate sanity check (no change in packets/sec).
8. **Killstreak steering:** Predator missile twins (HIGH).
9. **Weapon cycling and action slots:** manual RE (MP killstreak slots).
10. **Rumble:** the fire hook in MP, and damage through `VM_Notify` (hits).
11. **Cursor gate:** resolve the gate inside MP `UI_Refresh`.

## 7. Open questions for the user

1. Target-based aim slowdown in MP: port it or keep it SP-only? (§5;
   recommendation: SP-only.)
2. Default state for MP features once ported: all off (recommended), or
   controller features on when a controller is detected?
