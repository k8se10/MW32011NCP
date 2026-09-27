# MP port plan: controller pipeline and performance fixes on `iw5mp.exe` (2026-09-27)

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
