# Vulkan/DXVK + native DLSS (Streamline) integration — research (started 2026-09-23)

**Purpose**: deep, dedicated research supporting the locked architecture decision
recorded in `known_issues_x64.md` issue #2 — a real, native `[Video] GraphicsApi`
selector (`Legacy D3D9` default / `Vulkan` opt-in), where `Vulkan` mode runs an
in-process DXVK D3D9-to-Vulkan translation layer feeding NVIDIA's real Streamline
SDK directly, with NO ReShade and NO third-party addon framework (RenoDX,
DLSS5-Feeder, LumeniteFX) in the chain — this project's own existing native D3D9
hook already makes that middleware redundant, per direct user correction this
same session. This doc is the technical deep-dive backing that decision;
`known_issues_x64.md` issue #2 stays the short, current-status summary.

---

## 1. Real, confirmed prior art directly on THIS game

Two release files from the "MW3 Remastered" Nexus mod (mod #10,
`nexusmods.com/callofdutymodernwarfare3/mods/10`) were directly inspected this
session (full trail in `known_issues_x64.md` issue #2):

- **Base file**: a stock, unmodified ReShade `d3d9.dll` + the public
  `reshade-shaders` community repo + one preset config. Zero custom code, zero
  DLSS. The "Ray Tracing Global Illumination" claim is a real mischaracterization
  — MartysMods RTGI is a screen-space depth-buffer ray-marching SSGI
  approximation, not real ray tracing.
- **"With DLSS 5" file**: genuinely real — a full NVIDIA Streamline SDK payload
  (`sl.common.dll`, `sl.dlss.dll`, `sl.dlss_d.dll`, `sl.dlss_g.dll`,
  `sl.dlss_nr.dll`, `sl.interposer.dll`, `sl.nis.dll`), real NGX runtime DLLs
  (`nvngx_dlssnr.dll` alone is 165MB, consistent with a real NN weight payload),
  and a real RenoDX build (`renodx-dlss.addon64`). This is **direct, confirmed
  proof DLSS/Streamline can be made to function against MW3's own post-2026-09-03
  x64 binary specifically** — not an inference by analogy to a different engine,
  actual working prior art on our exact target.

RenoDX's own README directly confirms why it needs ReShade: "Using Reshade
simplifies all the hooks necessary to tap into DirectX without worrying about
patching version-specific exe files." This project already has that hook —
signature-scanned, verified, already coexisting with every other gameplay/visual
hook this project has installed since 2026-09-03. The reason RenoDX needs ReShade
does not apply to us.

## 2. NVIDIA Streamline — the real integration target

Researched directly via `gh api`/`curl` against `github.com/NVIDIA-RTX/Streamline`
(also mirrored at `github.com/NVIDIAGameWorks/Streamline`), reading
`docs/ProgrammingGuide.md` (1557 lines) in full.

### 2.1 Vulkan is a genuine first-class path, not a D3D11/12 afterthought

Confirmed directly (§2.5 "HOW TO PROVIDE CORRECT DEVICE"): a host application
using native Vulkan has two real options — let Streamline's own `vkCreateInstance`/
`vkCreateDevice` proxies manage everything, OR use native Vulkan calls and
**manually inform Streamline via `slSetVulkanInfo`**. The second option is exactly
what a DXVK-backed integration needs: DXVK creates its own real Vulkan
instance/device/queues through native Vulkan API calls; this project would call
`slSetVulkanInfo` itself once DXVK's handles are available, registering Streamline
against a host-managed Vulkan device with zero D3D11/D3D12 involvement anywhere in
the chain. This is a real, explicitly documented, officially-supported
integration shape — not a hack or an unsupported edge case.

A dedicated `ProgrammingGuideManualHooking.md` exists specifically for
integrators who don't want Streamline's own automatic interposer/hooking
behavior — flagged in the main guide as "the optimal and recommended way of
integrating SL" for exactly our situation (an application, or in our case
DXVK acting on our behalf, that already owns its own device-creation and
hook-installation sequence). **Not yet read in full — real next step.**

### 2.2 Resource tagging — the real per-frame data contract

`slSetTagForFrame` (the current API; the older `slSetTag` is deprecated) is how a
host provides Streamline with the buffers DLSS needs, tagged by a `BufferType`
enum. The relevant tags for basic DLSS Super Resolution:

- **`kBufferTypeDepth`** — "Must be suitable to use with clipToPrevClip
  transformation." Real, promising lead already on record: this project's own
  render-target table (`renderer_architecture_map.md` section 5b) already found
  `$floatz`, a real linear/float depth buffer target that exists in the native
  engine's own render-target inventory — a real candidate depth source, possibly
  without needing new native RE work to locate one from scratch.
- **`kBufferTypeMotionVectors`** — the one piece with no existing native source
  (confirmed absent from the render-target table) and no third-party shader
  being reused (LumeniteFX's LumaFlow was explicitly cut along with ReShade,
  per this session's own correction) — this needs a genuine, new, from-scratch
  motion-vector reconstruction implementation, real work not yet started.
- **`kBufferTypeHUDLessColor`** — "Color buffer with all post-processing effects
  applied but without any UI/HUD elements." A real, easy-to-miss requirement:
  DLSS must never see the HUD, or it will try to temporally reconstruct crisp
  UI text as if it were noisy scene geometry. This project's own existing
  full-screen capture/composite pipeline (already used by FSR/motion blur/SMAA)
  is already HUD-aware and already sequences its own passes relative to the
  native HUD draw — a real, existing advantage over a naive raw-backbuffer grab.
- **`kBufferTypeScalingInputColor`/`kBufferTypeScalingOutputColor`** — the
  actual low-res-in/high-res-out pair for the upscale pass itself.

Every tag has a real `ResourceLifecycle` requirement (`eOnlyValidNow` /
`eValidUntilPresent` / `eValidUntilEvaluate`) governing how long Streamline is
allowed to assume the tagged resource stays valid — real correctness
constraints to respect once implementation starts, not just a data hand-off.

### 2.3 Frame tracking and the still-unresolved jitter requirement

`slGetNewFrameToken` is called once per simulated frame; the returned token is
threaded through every subsequent SL call for that frame (including from async
render/present threads) — a real, explicit "frame identity" contract, not
implicit frame-counting. Straightforward to integrate given this project's own
existing per-frame hook structure.

**Sub-pixel camera jitter is NOT resolved by this research** — the original
`known_issues_x64.md` blocker ("no per-frame sub-pixel camera jitter, a
projection-matrix change") stands. Streamline's core programming guide doesn't
appear to supply this for the host (it's a per-feature, DLSS-specific
requirement, expected to live in `docs/ProgrammingGuideDLSS.md`, not yet read
in full — real next step). This is real, still-needed native RE work: finding
and hooking IW5's own projection-matrix construction to inject the DLSS-mandated
jitter offset each frame, on top of whatever this project's own render-scale
hook already controls for internal resolution.

### 2.4 Signing/security — real, non-optional requirements

All production SL modules ship digitally signed by NVIDIA (two signatures: a
standard Windows Store cert, plus a custom NVIDIA cert as a hardened fallback).
The guide is explicit: **self-built or "development" SL DLLs are unsigned and
must never ship** — production integrations must use NVIDIA's own prebuilt,
signed `sl.*.dll` binaries as-is. This project's own redistribution plan (if any)
needs to respect this directly — bundling the real, signed NVIDIA binaries, not
attempting to rebuild Streamline from source for a shipped release. License is
listed as "Other" on the repo (not plain MIT/zlib) — **the exact terms have not
yet been read in full, real next step before any shipping decision.**

The `EnableNvidiaSigOverride.reg` file bundled with MW3 Remastered's own DLSS
variant is consistent with this signing model — very likely a workaround for a
DIFFERENT Windows-level signature-enforcement point (not SL's own internal
`WinVerifyTrust` check, which the guide says the interposer handles
automatically), possibly related to loading the NGX runtime DLLs specifically
in a context NVIDIA's own driver doesn't recognize as an officially-integrated
title. Not yet independently confirmed — flagged as a real open question.

## 3. RenoDX's real per-game catalog — confirms the engine class is achievable, no direct MW3 precedent exists

Directly listed `src/games/` in `github.com/clshortfuse/renodx` (main branch,
not the wiki, which failed to clone cleanly this pass) — a real catalog of
250+ individually-integrated titles. **No MW3 (2011) or IW5-engine entry exists**
— this project would be genuine, first-of-its-kind native RE work for this
specific engine, not following an existing trodden path. Two real, meaningfully
comparable precedents DO exist in the catalog:

- **`callofduty_t7_blackops3`** — a related CoD-lineage engine (Treyarch's own
  fork of the shared IW/CoD engine tree), real evidence this general engine
  family has been successfully integrated before, even though Treyarch and
  Infinity Ward's engines have diverged significantly over the years.
- **`specopstheline`** — Spec Ops: The Line (2012, Yager Development, Unreal
  Engine 3), a genuinely comparable console-generation, shader-based
  (not fixed-function) title — real evidence this specific ERA/CLASS of
  engine (not just modern titles) is a real, achievable RenoDX integration
  target, reinforcing the general finding from the RTX Remix research
  (fixed-function-only is Remix's own specific limitation, not a universal
  "old games can't get DLSS" wall).

## 4. DXVK — full research landed

A dedicated research agent investigated DXVK (`github.com/doitsujin/dxvk`) in
the same depth as sections 1-3, via `gh api`/issue search. Real, decisive
findings, one of which meaningfully corrects this project's own assumed
integration architecture.

### 4.1 Native-Windows drop-in usage — real, but explicitly unofficial

The dedicated wiki page "Windows" states plainly: "The DXVK developers do not
officially support running DXVK on Windows. Many issues fall outside their
control." Real setup gotchas are documented (Microsoft's own "OpenCL, OpenGL &
Vulkan Compatibility Pack" can break DXVK; anti-cheat and overlay software —
Steam/Epic/Uplay overlays, GeForce Experience, RTSS, OBS — are known
troublemakers). Community native-Windows use for offline/retro titles is
common in practice, but no citable "this is a blessed, supported workflow"
statement exists — this project should describe it precisely as real but
community-only, not "production-proven the way Proton is" (that framing was
this project's own earlier overclaim, corrected here).

### 4.2 Shader-compile-storm stuttering — real, unresolved, and specifically hits this engine's own lineage

Not hypothetical. Real, still-open DXVK issues directly against CoD-family
titles: **#4594** ("D3D9: Call of Duty: Modern Warfare 2 (2009) / Excessive
Shader Compilation," open) — 20-60s freezes, hundreds of shader-compile log
lines, confirmed DXVK-specific (does not reproduce with WineD3D), a 400MB+
apitrace before even reaching the main menu, unresolved. **#3049** ("Call of
Duty: Modern Warfare Remastered: Shader compile conflict?", open) — real,
game-triggered shader-precompile stutter under DXVK, community workaround is
an unofficial "DXVK-ASYNC" fork never merged upstream; a DXVK dev noted D3D11
gives applications no manual shader-cache API, meaning D3D9-era precompilation
assumptions don't map cleanly onto DXVK's own pipeline-compile model. Further
same-era pattern: **#4924** (CoD: World at War crashes with
`GraphicsPipelineLibrary=Auto/True`), **#4882** (CoD: Black Ops "feels
unsmooth" since DXVK 2.5), **#4800**/**#5231** (CoD2 crash on launch, one
still open).

**Direct user connection, worth recording precisely**: this project's own
render-scale-gated stutter investigation this session (issue #4) already
concluded a genuine native engine stability limit at large render-target
sizes — and the x86 line separately had real crashes above ~250% render scale
root-caused to `ForceD3D9On12` driver corruption (issue #105, a different
mechanism, since removed). This DXVK finding is a THIRD, independent data
point in the same broader pattern: this engine family has demonstrated real
instability under multiple, genuinely different kinds of load (large
render-target memory/bandwidth; `ForceD3D9On12`'s own driver-level
corruption; now DXVK's shader-pipeline-compile volume) — three different root
mechanisms, not the same bug, but a real, recurring shape worth taking
seriously as a risk factor for `Vulkan` mode specifically, not dismissed as
unrelated. **Not confirmed to be the literal same bug as issue #4** — different
investigators, different technical layers (native D3D9 render-target sizing
vs. DXVK's own Vulkan pipeline JIT-compilation), no shared root cause
established — but the pattern itself (this engine's real stability envelope
gets hit by several independent kinds of stress) is now evidenced three times,
not once.

### 4.3 D3D9-specific `dxvk.conf` keys (real, confirmed set)

`deferSurfaceCreation`, `maxFrameLatency`, `maxFrameRate`, `presentInterval`,
`samplerAnisotropy`, `samplerLodBias`, `clampNegativeLodBias`,
`forceSampleRateShading`, `shaderModel` (0-3), `dpiAware`, `lenientClear`,
`maxAvailableMemory` (feeds `GetAvailableTextureMem`), `memoryTrackTest`,
`floatEmulation`, `deviceLocalConstantBuffers`, `supportCubeDepthFormats`,
`supportDFFormats`, `useD32forD24`, `supportX4R4G4B4`, `disableA8RT`,
`forceSamplerTypeSpecConstants`, `forceAspectRatio`, `forceRefreshRate`,
`modeCountCompatibility`, `enumerateByDisplays`, `cachedWriteOnlyBuffers`,
`seamlessCubes`, `textureMemory` (MB, 0=disable), `deviceLossOnFocusLoss`,
`countLosableResources`, `extraFrontbuffer`, `useFP16`,
`ignoreDefaultBufferLockRange`. Loaded via `DXVK_CONFIG_FILE` or the
`DXVK_CONFIG` env var.

### 4.4 Real D3D9-specific Vulkan interop API — answers the open `slSetVulkanInfo` question from section 2.1

`src/d3d9/d3d9_interop.h`/`.cpp` implement a real `D3D9VkInteropInterface`/
`D3D9VkInteropDevice` API (merged via **PR #4506**, "[d3d9] Add device import
interop API"), exposing `GetDeviceCreateInfo`, `QueryDeviceExtensions`,
`QueryDeviceQueues`, `QueryDeviceFeatures`, and `ImportDevice` — a host can
query DXVK's own required Vulkan device-creation parameters, create/import the
device itself, and hand it back. The PR author reports real, working use
(tested on AMD/Nvidia/Linux and Nvidia/Windows, MSVC and Mingw builds) running
a custom deferred renderer "atop our DX9 renderer" through this exact path —
directly relevant, working precedent for feeding DXVK's own Vulkan device to
`slSetVulkanInfo`. **Caveat**: this is a fairly recent addition (PR discussion
dated late 2024) — must verify presence in whatever DXVK version/commit this
project pins to before relying on it.

### 4.5 The export-forwarding architecture assumption was WRONG — real correction

This project's original assumption (§ earlier this session, and this doc's own
first draft) — that our proxy would forward calls to a renamed DXVK `d3d9.dll`
the same way it currently forwards to the real system `d3d9.dll` — **is
explicitly rejected by DXVK's own maintainer**. Issue **#2772** ("Using an
identical named proxy dll at the same time as dxvk"), doitsujin's own words:
**"DXVK needs to be the last DLL in the chain so I don't really see how this
would work,"** and: **"DXVK is the D3D9 implementation, we can't simply go
ahead and call someone else's Direct3DCreate9 instead of ours and expect that
to somehow still work."** A community-suggested rename+hex-patch workaround
(from WineD3D-era practice) was flagged by its own author as untested against
DXVK specifically — no verified, working rename path exists.

**The real, correct architecture, with genuine working precedent**: issue
**#1498** documents a D3D8-to-D3D9 wrapper successfully loading ON TOP of
DXVK's real `d3d9.dll` (confirmed via DXVK's own log output) — i.e. a layer
that itself calls DXVK's real `Direct3DCreate9` directly and wraps/hooks the
`IDirect3D9`/`IDirect3DDevice9` objects DXVK hands back, rather than forwarding
opaque exports through a renamed copy. Further real precedent: ReShade and
Special K (another D3D9 hooking/injection tool, same class as this project's
own MinHook-based vtable hooks) both hook the same device surface
(`EndScene`/`Present`/`Reset`) on top of DXVK in practice — real, if sometimes
version-bumpy, compatibility (issues **#3995**, **#4776**, **#5889**,
**#4145**, **#4295** — fixable interaction bugs across DXVK version bumps,
not categorical incompatibility).

**Corrected plan**: `Vulkan` mode's own `Direct3DCreate9`/`CreateDevice`
handling must explicitly `LoadLibrary` a renamed DXVK `d3d9.dll`, call ITS
`Direct3DCreate9` export directly (not blind export-table forwarding — this
project already does explicit resolve-and-call for its other real function
calls, so this is a smaller change than it first appears), and install this
project's own existing MinHook vtable hooks on the `IDirect3D9`/
`IDirect3DDevice9` objects DXVK returns — structurally identical to what this
project's hooks already do against the real system `d3d9.dll` today, just
pointed at DXVK's own implementation instead. `Legacy D3D9` mode is completely
unaffected — it keeps using the real system `d3d9.dll` exactly as today.

### 4.6 Real question raised, direct user point: ship this as a plugin DLL instead, so it "isn't the d3d9.dll"?

**Doesn't work as a detection-avoidance mechanism, for a real, hard technical
reason**: Windows loads exactly one file named `d3d9.dll` per process (from
the game's own install directory, standard DLL search order), and the game
calls THAT file's own `Direct3DCreate9` export directly at startup. Something
has to physically be that file and intercept device creation at that exact
moment, before any device exists — this project's own existing plugin system
loads plugins AFTER the main mod's own device/hooks already exist (a
deliberate, correct design for what plugins are actually for), which is
architecturally too late to ever intercept device creation itself. There is
no way to route the real DXVK integration through the existing plugin loader
— whatever handles `Vulkan` mode must be reachable from the real `d3d9.dll`'s
own `Direct3DCreate9`, full stop. The on-disk module VAC observes as
"`d3d9.dll`" is necessarily not the genuine Microsoft one either way — that's
already true of this project's own CURRENT `Legacy D3D9`-only proxy too (see
section 5's own real track-record discussion), and doesn't change based on
where the DXVK-specific logic physically lives.

**Where the underlying instinct is still genuinely right**: real isolation
value exists, just not the value originally hoped for. Rather than growing
this project's own main `d3d9.dll` to include the full DXVK/Streamline
integration directly, the main proxy's `Direct3DCreate9` handling can stay
almost exactly as it is today for the default `Legacy D3D9` path (same file,
same already-2-months-clean behavior), with one small added branch: if
`Vulkan` mode is selected, `LoadLibrary` a SEPARATE, clearly-labeled auxiliary
DLL early (NOT via the plugin loader — a direct, explicit load from within
`Direct3DCreate9` itself, before any device exists) and call ITS exported
`Direct3DCreate9` instead, with all the new, large, unverified DXVK/Streamline
integration code living entirely in that separate file. Real, legitimate
benefits this actually delivers: the already-trusted core module's own diff
stays small and easy to audit; the new, higher-risk surface is isolated,
independently removable, and impossible to accidentally activate for anyone
who hasn't explicitly opted into `Vulkan` mode; and it keeps this project's
own established "opt-in feature = separate, inspectable unit" convention
(matching how the security component already ships as a separate,
"greenlit" plugin DLL) even though the LOADING mechanism itself can't be the
literal plugin API for this specific piece.

## 5. VAC/ban risk for `Vulkan` mode — real research, a genuine correction to an assumption almost made

**A wrong argument was almost made here and is recorded so it isn't repeated**:
the intuitive case ("DXVK is safe because Steam Deck plays VAC-secured games
via Proton/DXVK constantly with no incident") does **NOT** transfer to this
project's own planned native-Windows deployment. Real, primary-source evidence
from DXVK's own maintainers, PR **#4223** ("[meta] Add more information about
anti-cheats," merged into DXVK's own README):

> "Most of the time, if a game runs without any issues via Proton, it probably
> also means it's safe to use DXVK with it... For most games, the worst
> scenario you can typically get [via Proton] is being kicked from the match
> due to anti-cheat support not being enabled. In those cases, it is very
> likely that the game is detecting and preventing **Wine**, rather than DXVK
> itself."
>
> "**On Windows, you are still using at your own risk**, since you are not
> running under Proton/Wine (which is expected to manipulate Direct3D
> libraries). Some games may simply refuse to launch when DLL replacement is
> detected. Others may allow it at first, but posteriorly kick or ban the user
> mid-match."

The Steam Deck safety signal exists BECAUSE DXVK is the ubiquitous, expected
default in that environment — an anti-cheat system tolerating Wine tolerates
DXVK as a side effect, it isn't independently vetting DXVK's own D3D9
implementation as safe. On native Windows, a replaced `d3d9.dll` is NOT the
expected state of the world, and DXVK's own maintainers explicitly decline to
extend the Steam Deck safety argument to that case. This is directly
consistent with — real, independent corroboration of — this project's own
already-established VAC research (`CLAUDE.md`'s "CORRECTED 2026-07-20" entry):
VAC is signature/heuristic-based, and an unexpected D3D-library replacement is
exactly the class of anomaly that kind of detection is built to notice.

DXVK's own doc also cites a real, concrete false-positive precedent (Apex
Legends players wrongly banned for Linux/Wine use, later reversed) — worth
recording as a real risk *category*, not just "caught cheating": detection
heuristics can misfire even absent any actual cheat behavior.

**A real, genuinely relevant counterpoint, direct user point**: this
project's own existing proxy `d3d9.dll` already does deep, invasive render-
pipeline manipulation — `CreateDevice`/`EndScene`/`Reset` vtable hooking,
signature-scanned code-cave detours into native engine functions, full-screen
capture/composite, render-target size overrides — with a real, on-point track
record: 2+ months of public use (since v0.1.0-prealpha, 2026-07-15) including
live streams and real MP sessions, **zero reported VAC bans attributable to
this mod**. This is more directly relevant than DXVK's own necessarily broad,
conservative, cross-thousands-of-titles disclaimer (written to cover far more
aggressive kernel-level anti-cheats like EAC/BattlEye, not calibrated to this
specific game's own comparatively old, likely under-maintained VAC signature
set — this project's own existing research already concludes that). This real
track record is genuine evidence that narrows the ambient worry about VAC's
practical (not theoretical-maximum) enforcement against this specific game.

**The real distinction that still stands, and why the track record doesn't
fully retire the new risk**: everything in that 2-month track record hooks
*within* the real, expected system `d3d9.dll` — the module VAC observes is
still the genuine one, same base identity, just with vtable slots redirected.
`Vulkan` mode is a categorically different change: the `d3d9.dll` itself would
no longer be the real one at all, a structurally different implementation
entirely (DXVK's own real translation layer). That's a new risk surface the
existing track record has simply never tested, since the existing technique
never touches the module's own identity. **Both facts belong in this record
together** — the real track record is a genuine reason not to treat this with
DXVK's own maximum-caution framing, but it narrows the general worry about
this game's VAC, it doesn't specifically clear the full-module-replacement
risk category `Vulkan` mode introduces.

**Real, honest mitigation plan, matching this project's own already-trusted
pattern rather than a new argument that the risk is low**:
1. **`Vulkan` mode gated SP-only initially**, mirroring this project's own
   established rollout pattern for every other major feature (Sprint,
   D-pad, menu navigation, MP itself) — VAC's confirmed-active surface is
   `iw5mp.exe`; keeping this mode out of MP entirely removes it from VAC's
   actual enforcement domain to start, not just in theory.
2. **Default OFF, explicit opt-in with a real, honest risk acknowledgment** —
   same shape as the existing MP VAC-risk gate, worded from DXVK's own direct
   disclosure above rather than a softened or reassuring paraphrase.
3. **The real, defensible distinction to state precisely**: the technique
   itself is not a cheat — no gameplay-entity memory reads, no competitive
   advantage, a rendering-only translation layer, consistent with this
   project's own hard-line "never read live gameplay memory" policy that
   already governs everything else this mod does. The open risk is
   detection/signature-matching (an unexpected DLL replacement), not that the
   technique confers any real advantage — these are different claims and
   should never be blurred together in how this is described to players.
4. **If MP support for `Vulkan` mode is ever considered**, it needs its own
   explicit, separate decision and acknowledgment step — this is not covered
   by extending the existing MP-controller-input opt-in policy automatically,
   any more than the reverse would be.

## 6. The 720p-lock connection — a real, existing advantage

Direct user point, worth recording precisely: MW3's own internal render
resolution isn't just often low, it's effectively LOCKED to a fixed low-res
tier regardless of display size (`SAVED_SCREEN` clamped at 1280x720, issue #88,
2026-08-25's own finding). This is exactly the "render low internally, present
high externally" shape DLSS's whole value proposition is built around — and
this project already has the exact mechanism a DLSS integration would need to
control it: `Hook_RenderResCompute` (the same hook `InternalRenderScalePercent`
already uses) overrides the engine's own requested internal render-target size
before the native computation runs. A DLSS integration would reuse this
directly — set the internal resolution to whatever a given DLSS quality mode
(Quality/Balanced/Performance) wants relative to the real output resolution,
rather than needing new native RE work to find and override that sizing logic
from scratch. Real, existing groundwork this project already has, not a new
blocker.

## 7. Open questions, honestly tracked

1. ~~DXVK's real license, native-Windows precedent, and Vulkan-handle interop
   mechanism~~ — **RESOLVED, section 4**: native-Windows use is real but
   unofficial; a real interop API (`D3D9VkInteropInterface`) exists for the
   Vulkan-handle question, version-pin caveat noted; DXVK's own zlib license
   confirmed earlier. **New open item from this research, not previously
   tracked**: the export-forwarding architecture must change (section 4.5) —
   real implementation work, not just a docs update, once this is built.
2. **Sub-pixel jitter injection** — still needs real native RE work against
   IW5's own projection-matrix construction; not resolved by any of this
   session's research (section 2.3).
3. **Motion-vector reconstruction** — needs a genuine, from-scratch
   implementation (ReShade-hosted LumeniteFX/LumaFlow explicitly cut); real,
   non-trivial new shader/compute work (section 2.2).
4. **Streamline's own real license terms** ("Other," not yet read in full) and
   the exact redistribution requirements for the signed `sl.*.dll`/NGX runtime
   binaries in a shipped release (section 2.4).
5. **`slSetVulkanInfo`'s own exact requirements** (which extensions/features/
   queues Streamline needs DXVK's device to have enabled) — the manual-hooking
   guide (`ProgrammingGuideManualHooking.md`) is the real next read, not yet
   done this pass.
6. **The real hook-ordering/coexistence question**: where in this project's
   own existing `CreateDevice`/`EndScene`/`Reset` hook sequence a DXVK-backed
   Vulkan device would need to be created, and whether this project's own
   existing hooks (which currently assume a real D3D9 device/swapchain) need
   restructuring for `Vulkan` mode specifically, versus staying untouched for
   the `Legacy D3D9` default path.
