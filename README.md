<div align="center">

# 🎮 MW32011NCP

### Native Community Patches for MW3 (2011)

[![Release](https://img.shields.io/github/v/release/k8se10/MW32011NCP?include_prereleases&sort=semver&label=release)](https://github.com/k8se10/MW32011NCP/releases)
[![Build](https://github.com/k8se10/MW32011NCP/actions/workflows/build.yml/badge.svg)](https://github.com/k8se10/MW32011NCP/actions/workflows/build.yml)
[![Last commit](https://img.shields.io/github/last-commit/k8se10/MW32011NCP)](https://github.com/k8se10/MW32011NCP/commits/main)
[![License](https://img.shields.io/badge/license-custom%20(free%2C%20no%20resale)-blue)](LICENSE)
[![Support on Ko-fi](https://img.shields.io/badge/support-ko--fi-FF5E5B?logo=ko-fi&logoColor=white)](https://ko-fi.com/officialk8)

</div>

A native, from-scratch reverse-engineering platform for **Call of Duty:
Modern Warfare 3 (2011, IW5 engine)** — not a single mod, but a patch layer
with four real, distinct components, all built on the same technique (a
proxy `d3d9.dll` that hooks the game's own real engine functions directly —
never a keyboard/mouse-emulation mapper, never synthesized input where a
native call exists, never a config tweak):

| Component | What it does | Release-gating? | Status |
|---|---|---|---|
| **Controller support** | Real analog movement/look/every button for Campaign & Survival, matching console behavior | 🔴 Yes — Survival gates the release; Campaign ships best-effort and has never gated it (same as on `-x86`) | Survival: Gameplay Complete (2026-09-22) — see [What works](#what-works-right-now) |
| **Visual/performance enhancements** | Internal render scale (SP and MP), FSR 1.0 sharpening, motion blur (any input device), stutter/threading fixes, frame pacing/wait coalescing/IWD read cache | 🔴 Yes — the gate | Render scale, motion blur, and the three ported perf techniques (frame pacing/wait coalescing/IWD cache) live-confirmed; FSR build-verified only; SMAA parked, FXAA build-verified — see [What works](#what-works-right-now). **The forced anisotropic filtering/shadow/lighting quality toggles are currently non-functional on x64** (a silent no-op, not a crash) — see [Known gaps](#known-gaps) |
| **Netcode security patches** | Finds and fixes real, exploitable vulnerabilities in the base game's own netcode | ⚪ No — not held to the SP controller-support gate below (same repo, absorbed 2026-09-12 — not a separate project) | **Complete, end to end — all 4 tracked vulnerabilities resolved** (3 fixed, 1 confirmed already safe), closing genuine RCE-class holes present since before this project existed, with no known official Activision fix. 2 of 3 active fixes independently confirmed firing against real MP traffic; the third (SP/Spec-Ops P2P) is build-verified and resolves correctly live, just not yet observed against a real P2P session — see [Security](#security-netcode-vulnerability-patches) |
| **Multiplayer (`iw5mp.exe`)** | Same controller/security methodology, ported to the separate MP binary | ⚪ No — allowed to lag SP by 2-4 releases until beta | Active reverse-engineering, opt-in-only when it ships — see [Multiplayer](#multiplayer) |

Plus a cross-cutting [plugin API](PLUGIN_API.md) that lets anyone extend
this mod, or build an independent MW3 sub-mod, without touching this
repo's own source — the security component itself ships as one such
plugin, "greenlit" to load by default.

**This is the same project as before, redefined, not replaced.** Through
2026-09-03 this repo was "MW32011NCP — Native Controller (and Enhancement)
Project." On 2026-09-12 it was redefined again to **Native Community
Patches** — name and repo unchanged, scope formalized to match what it had
already organically become, with the sibling `MW32011NSP` security project
folding in as this repo's own `security/` component the same day. Full
record: `CLAUDE.md`'s Version Timeline, 2026-09-03 and 2026-09-12 entries.
Controller support remains the flagship, first-shipped patch — it just
isn't the whole of what this project does anymore.

MW3 (2011) shipped on PC with **zero working controller input path** — no
`xinput`/`dinput8` import anywhere in either binary, no hidden setting to
unlock — which is where this project started. Every control, every visual
toggle, and every security fix here was found and wired up through static
reverse engineering (Ghidra) plus live verification against the running
game, not assumed.

**Why the x64 rebuild matters beyond controller support.** The 2026-09-03
recompile broke every hook this project had — a real crisis at the time —
but it also forced a from-scratch reverse-engineering pass across the
entire binary, not just the controller-input code paths this project
started with. That's the direct reason all four netcode vulnerabilities
above are now genuinely fixed: this project re-derived every one of them
against the CURRENT binary rather than reusing years-old, possibly-stale
research, and shipped real, working patches for every one still open — RCE-
class holes that had sat unpatched through this game's entire prior
history, with no known official Activision fix for most of them. The
x64 rebuild isn't just "the same mod, ported" — it's the reason this
project now closes real security gaps for the native install that existed
long before this project did, on top of everything controller-support-
related.

## ⚠ Security notice: unpatched MW3 (2011) netcode vulnerabilities — this mod fixes them

This project's own research (the [Security](#security-netcode-vulnerability-patches)
component below) identified real, network-reachable vulnerabilities in MW3
(2011)'s own base-game code — **not in this mod** — affecting both
Multiplayer and Spec-Ops/Survival co-op. These were re-confirmed present and
unpatched in the game's most recent (September 2026) update, so they are not
something you can fix by updating the game itself.

**This mod now closes all four of them.** Installing it patches the running
process in memory with real, working fixes — see
[Security](#security-netcode-vulnerability-patches) below for the current
per-finding status. **A vanilla, unmodified Steam install remains exactly as
vulnerable as before** — this is a real, meaningful reason to run this mod
even if you have no interest in its controller/visual features.

**What this means for a vanilla install**: a malicious peer, server, or
party/lobby host could potentially crash the game or worse. We are not
publishing exact technical detail while these remain unpatched at the
source — doing so before Activision has a chance to fix them would put
every MW3 player at risk, not just non-mod users. A full report has been
submitted to Activision through their official security-disclosure channel.

**If you're not using this mod**:
- Be cautious joining Multiplayer servers/lobbies you don't trust, especially
  third-party-hosted ones.
- Be cautious in Spec-Ops/Survival co-op sessions with strangers.

We'll update this notice with full technical detail once Activision ships
and verifies its own fix, per standard responsible-disclosure practice.

## Status

> **`v0.0.2-x64` is out now.** Survival is the recommended way to
> use the mod (Gameplay Complete — every core control, including Predator Missile guidance, is live-confirmed);
> Campaign ships best-effort, and Multiplayer has no controller/menu-navigation support yet — but its first real
> visual-enhancement feature (internal render scale) now works there. The netcode security fixes protect every mode,
> Multiplayer included. Still early software — expect hidden bugs and unfinished or unported features. Known gaps
> are listed below and in [`re_notes/known_issues_x64.md`](re_notes/known_issues_x64.md).

**Alpha, `v0.0.2-x64` line.** On 2026-09-03 MW3 received its first real
binary update in the game's history, recompiling both `iw5sp.exe`/`iw5mp.exe`
from 32-bit to 64-bit — a hard architectural break that invalidated every
hook this project had. The old 32-bit (`-x86`) line is fully discontinued;
its final state and documentation are preserved in
[`legacy-x86-docs/`](legacy-x86-docs/) for reference. This release rebuilds
the project for x64 from that foundation, using the same reverse-engineering
methodology, across all four components above — **almost three weeks after
the recompile broke every prior release**, now rebuilt more resilient than
before, with real fixes and features beyond what the `-x86` line ever shipped.

**Release scope**: Survival controller support reaches the same feature
completeness the `-x86` line reached before being discontinued — every
control and the visual-enhancement suite's headline features working, not
just the input-remapping core. (The custom Options screen's own vanilla-
setting tabs are a deliberately deferred exception — see Known gaps below.)
**Campaign has never been a release gate** — same as on the `-x86` line,
where several Campaign-specific items (e.g. the AC-130 sequence) shipped
honestly marked untested rather than blocking release; Campaign controller
support ships best-effort, verified as it's touched, not held to Survival's
own live-test bar. **Survival's own controller-support scope reached
Gameplay Complete on 2026-09-22** — every core control, Predator Missile
guidance included, is live-confirmed working; further killstreak/mounted-
weapon feel and sensitivity refinement is deferred to a dedicated future
bulk pass. One known cosmetic bug ships with this release: the pause-menu
Back glyph flickers (Back itself still works). See
[`re_notes/known_issues_x64.md`](re_notes/known_issues_x64.md) issue #1 for
the live, detailed tracking of exactly what's done and what's left, and
[`re_notes/x64_feature_parity_audit.md`](re_notes/x64_feature_parity_audit.md)
for the complete, systematic audit against every `-x86` feature.

Netcode security fixes and Multiplayer support each have their own status —
see their own sections below; neither gated this release.

### What works right now

**2026-09-22: Survival's own controller-support scope is Gameplay Complete.**
Since the first real playtest (2026-09-14) found every core control except
D-pad working, a further week of live-tested fixes closed the rest: D-pad,
Predator Missile's post-fire guidance (the last core control that had never
worked on either architecture), the Survival ready-up prompt (now a full
glyph+text replacement, not just the underlying mechanism), buy-station/
use-prompt glyphs, AI-suppression's F4 toggle, several critical launch-
crashing buffer overflows, and the real root cause of the "needs a click/
input at launch" bug family. Campaign has never been a release gate (same
as on `-x86`, where it also shipped best-effort/partially untested) and
ships as-is, verified as it's touched.

#### Graphics API compatibility

`[Video] GraphicsApi` selects between `LegacyD3D9` (this project's original, always-supported native D3D9 pipeline) and `Vulkan` (DXVK-in-process translation, **default since 2026-09-26**) — SP-only, `iw5mp.exe` always uses `LegacyD3D9` regardless of this setting (real MP-specific VAC-risk research is needed before that gate is ever reconsidered; this is unrelated to, and unaffected by, SP's own default change). Almost every `[Video]` feature is implemented at the plain D3D9 API level and works identically under either backend by construction, since DXVK implements that same surface — the table below lists the real exceptions. **If a feature isn't explicitly listed here, assume it works under both backends** (this is the default/common case, not something each one needs its own row to confirm).

| Feature | `LegacyD3D9` | `Vulkan` | Note |
|---|---|---|---|
| `StreamlineEnabled` / NVIDIA DLSS | ❌ Not supported | ✅ | Genuinely Vulkan-only by design — NVIDIA's Streamline SDK has no D3D9 support at all (confirmed directly from NVIDIA's own SDK), needs DXVK's own Vulkan-interop interface underneath it |
| `ForceAnisotropicFiltering` / `ForceHighQualityShadows` / `ForceHighQualityLighting` | ❌ Silent no-op | ❌ Silent no-op | **Not a backend difference** — these three are currently non-functional on x64 entirely, on either backend (see [known_issues_x64.md issue #6](re_notes/known_issues_x64.md)); the real x64 dvar-setter these rely on isn't wired up yet |
| `SkipRedundantShadowActivation` (default on, 2026-09-26) | ⚠ Expected to work, not independently confirmed | ✅ Live-confirmed | A native engine-level fix (skips a redundant per-light render-view reactivation), independent of which D3D9 implementation is underneath it in principle — but every live test so far (30→40fps intensive mission, 17→27fps pause menu, 28-30→41fps Survival) was run under `Vulkan`; `LegacyD3D9` hasn't been separately tested yet |
| `ProjectionJitterEnabled` | ⚠ Real no-op | ⚠ Real no-op (groundwork only) | Pure DLSS/temporal-accumulation groundwork — writes real jitter into the projection matrix under either backend, but nothing currently consumes it (no TAA/DLSS resolve pass exists yet), so it's a harmless, purposeless shimmer either way until that lands |

#### ✅ Confirmed live

| Feature | Note |
|---|---|
| Analog movement, analog look | |
| Fire, ADS (true hold-to-aim), Reload | A real x64-only regression (an early-return meant to skip a no-op write instead silently disabled most controls whenever the stick was centered) was found and fixed 2026-09-13 |
| Melee, Lethal, Tactical, Jump, Interact, Jump auto-stand | |
| Weapon switch (Y) | |
| Crouch/Prone (tap vs. hold), Sprint (real kbutton) | |
| Pause menu open/close | Multiple real bugs found and fixed since 2026-09-14 (Start wrongly blocked after unpause, B firing a second unwanted ESC, gameplay controls no-op'ing correctly while a menu is up and for 300-400ms after it closes) — this whole interaction area has had a full week of live-test hardening |
| D-pad actionslot (all four directions), D-pad Left's squadmate-call-in | |
| Survival ready-up | Full prompt replacement (real glyph + "Hold ... to ready up: NN" text via the client hudelem draw hook), not just the underlying mechanism — found and shipped 2026-09-19/21, correcting this file's own earlier "mechanism only" framing |
| Hold Breath (L3 while ADS'd, sniper-class) | |
| Predator Missile launch and post-fire guidance (Survival buy-station) | Guidance fixed 2026-09-22 (issue #30) — never worked on either architecture before this. Filed as "good enough," not fully polished; further killstreak/mounted-weapon feel refinement is deferred to a future bulk pass |
| DPV (Hunter Killer)/Goalpost mortar/Goalpost M2 turret aiming | Never worked on either architecture before, real shared root cause found and fixed 2026-09-14 |
| AC-130 zoom-aware look sensitivity | |
| Cutscene-skip audio (controller Start) | Fixed on both `-x86` and `-x64` 2026-09-14 |
| Campaign QTE/scripted-sequence button presses (e.g. the "Dust to Dust" elevator/chopper jump) | Real synthetic-keypress fix, same technique already proven for Survival ready-up |
| Motion blur | Real x64 trigger hook found 2026-09-13; a stepped-ghosting artifact was later fixed 2026-09-21 (8-tap → 24-tap bilinear) |
| Internal render scale (SP and MP) | ⚠ Safe above 100% only up to a point that is NOT one fixed number — see the warning under [Known gaps](#known-gaps) |
| Native controller menu/UI navigation | Main menu, pause, buy-stations |
| Buy-station / use-prompt glyph substitution | Structural template match ("Hold/Press ^N key ^7"), shipped 2026-09-21 |
| Glyph-icon substitution: Mantle, Pickup/Swap/Pickup-health, Throwback, Reload/low-ammo, five menu corner hints | Mod-wide 100ms hold + 50ms fade added 2026-09-22 to stop flicker; drawn only on the visible back-buffer pass (an earlier bug re-rasterised text at a different scale on offscreen blur passes) |
| "Needs a click/input at launch," real root cause | Genuinely a family of three separate native bugs (stuck kbuttons from a stale internal table, an unseeded mouse-delta baseline, and a missing message-queue-routed input event), each found via full decompile and fixed 2026-09-16/17, replacing the old 2026-09-04 pause/unpause automation workaround entirely. User-confirmed "seamless," including a bonus fix for a long-standing mission-restart version of the same bug |
| `[Video] FramePacingEnabled`, `WaitCoalescingEnabled`, `IwdReadAccelEnabled` | Three techniques ported from `legoliamneeson/MW3_Standalone_D3D9_Project`, credited. Confirmed live 2026-09-21; default on |
| F4 `ai_disableSpawn` debug toggle | Fixed 2026-09-21 (was a silent no-op — the dvar setter drops writes off the main thread; now queued to the gameplay tick) |
| Highlighted-item A-glyph (menu list navigation) and the F2/F3 glyph-position editor | |
| Custom mouse cursor overlay | A real gap (not showing at the true main menu specifically) was found and fixed 2026-09-14 |
| Auto-Mantle (while sprinting) — ships off by default | |
| Back (scoreboard, `+scores` key-synthesis) — correctly confirmed as a no-op in SP | |
| ADS zoom-aware look-slowdown (`AdsSlowdownStrength`/`Baseline`/close-range taper) | |

#### 🟠 One known remaining visual bug

The pause-menu **Back glyph still flickers**. It's drawn from a stored
position every frame (rather than the flickering native draw) specifically
to fix this, and buy-station Back uses a separate native-template-match path
— both went through several iterations 2026-09-21/22 — but a residual
flicker is still present as of the latest build. Purely cosmetic (Back still
works), tracked for a follow-up pass.

#### 🟡 Build-verified, awaiting live confirmation

| Feature | Note |
|---|---|
| Plugin API | Loader, hook/memory access |
| DualSense gyro-aim | Preview/WIP, same status as `-x86`, needs real hardware to test |
| FSR sharpening | Runs without crashing; not yet confirmed to produce its real visible effect the way `-x86` was |
| `[General] DisableControllerInput` ("K+M safe mode") | Hot-reloadable toggle disabling all controller/mod-side input injection while keeping every visual-enhancement feature working, shipped 2026-09-16 |
| `[Video] FxaaEnabled` | Edge-blur-only AA pass, shipped 2026-09-21, off by default — real but limited (blurs jagged edges, can't smooth geometry without softening the frame) |
| First-launch welcome modal, "possibly outdated" nag, launch toast, dev-build watermark | Informational/cosmetic, shipped 2026-09-22 |

*(The Custom Options screen's vanilla-setting tabs and AC-130 gun-type switching are genuinely-open gaps, not "verified and awaiting a live test" — see [Known gaps](#known-gaps).)*

### Known gaps

These are honestly documented as not-yet-implemented, not hidden bugs. A
**complete, systematic audit against every `-x86` feature is done** — see
[`re_notes/x64_feature_parity_audit.md`](re_notes/x64_feature_parity_audit.md)
for the full 68-item table (documentation-sourced plus a follow-up
git-history sweep) — this section lists only what's still genuinely open;
anything resolved since a prior pass has been moved up into
[What works right now](#what-works-right-now) rather than left here.

Sorted by priority — highest-impact/most-blocking first. Click a row for the
full investigation trail; nothing below has been trimmed, just moved out of
the main flow.

| # | Gap | Priority | Current status |
|---|---|---|---|
| 1 | ⚠ **Internal render scale's safe ceiling is NOT one fixed percentage** | 🔴 High — real, current, user-facing | Confirmed content-dependent: the exact same 200% that's clean throughout SP causes CONSTANT stutter under MP. See full detail below before raising this setting |
| 2 | Pause-menu Back glyph flicker | 🟠 Medium | Cosmetic only — Back still works. Drawn from a stored position specifically to fix this, plus a separate native-template-match path for buy-station Back; several iterations landed 2026-09-21/22 but a residual flicker remains |
| 3 | Sentry/turret-placement and Campaign QTE prompt **text** | 🟠 Medium | Mechanism works — the on-screen prompt itself still renders native. Buy-station and Survival ready-up's own prompt text are both now fully replaced (see [What works](#what-works-right-now)); this is what's left. Blocked on a genuinely unresolved native offset |
| 4 | OpenAssetTools `Unlinker` crash (dev tooling) | 🟠 Medium | 5 zones fully clean, many more no longer crash after the `SpeakerMap` fix; a `LoadedSound` alias-miss bug remains open — affects project velocity, not players |
| 5 | AC-130 gun-type switching (105/40/25mm) | 🟡 Low | Confirmed GSC/data-driven with no native hook point; correctly left unfixed rather than guessed at |
| 6 | Custom Options screen's vanilla-setting data layer (7/9 tabs) | 🟡 Low — deliberately deferred | INI config already covers everything this mod itself needs |
| 7 | Back's `+scores` scoreboard | 🟢 Low | Real gap, but a confirmed no-op in SP/Survival on every platform; matters once MP ships |
| 8 | SMAA edge smoothing | 🟢 Low — parked 2026-09-21 | Implemented but off by default and not viable yet: even a plain capture-and-redraw with no SMAA math looked worse than off and cost far more frame time, so the shared capture/redraw path is suspect independent of the SMAA shaders. See `re_notes/known_issues_x64.md` issue #2 for the staged AA/renderer roadmap |

<details open>
<summary><b>1. Internal render scale's safe ceiling is NOT one fixed percentage</b> — full detail</summary>

`InternalRenderScalePercent` (the visual-enhancement suite's supersampling/
downsampling override) defaults to 100% and never clamps or restricts what
you set it to — it's uncapped by design (see issue #88). A real, severe,
sustained stutter tied to pushing this well above 100% was investigated in
full depth 2026-09-23 and root-caused to a genuine **native engine
stability limit at large render-target sizes** — not a bug in this mod's
own code (five separate real fix/rule-out attempts against this project's
own code were all eliminated: a screen-capture cost, a hardcoded 3GB
memory-detection cap, an I/O-coalescing feature, a HUD-layer CPU copy loop,
and a shader-sampler setup chain). This is very plausibly why the original
PC port locked its own internal render resolution to a fixed reference
size in the first place, long before this mod existed.

**The critical thing to understand: there is no single "safe" percentage.**
In SP, 200% is completely clean — no stutter at all, confirmed across
damage/pause/ADS/level-transition repros. The exact same 200% setting,
tested the same day in Multiplayer, causes **constant, sustained lag** —
not gated to any specific trigger event, just always there. The most
likely explanation: MP's generally denser per-frame scenes (more players,
more concurrently-rendered character/weapon models, more active
netcode/prediction state) sit closer to this same underlying engine
boundary BEFORE any render-scale multiplier is even applied, so the same
percentage that's fully safe in one context can be well past the edge in
another — a busier map or mode, a more chaotic moment in a match, a
crowded Survival wave, or (unverified but plausible) even a specific
level's own geometry/lighting complexity could all shift where that edge
actually sits. **This mod has no way to detect any of that automatically.**

A real, tested, on-screen warning already fires once per session above
200% linear (the one boundary this project has actually confirmed, on one
reference GPU, in SP) — but per the finding above, staying under 200%
does NOT guarantee you're safe in every mode, and a more powerful GPU may
tolerate meaningfully higher than 200% in some contexts and meaningfully
less in others. **Practical guidance until this is better understood**:
treat any increase above 100% as something to test deliberately in the
specific mode/map you actually play, not a "set once and forget" value —
if you notice stutter after raising this setting, especially in
Multiplayer or a busy Survival wave, lower it back toward 100% rather
than assuming the on-screen 200% warning is the only threshold that
matters. See `re_notes/known_issues_x64.md` issue #4 for the complete
investigation trail, including the exact live-test evidence behind this
finding.

</details>

<details>
<summary><b>2. Pause-menu Back glyph flicker</b> — full detail</summary>

This went through heavy iteration 2026-09-21/22: hardcoding the pause Back
glyph's position and drawing it from that stored value every frame (instead
of the flickering native draw) was the fix that actually stuck for the
pause menu specifically; buy-station Back went the other way — after a
hardcoded-position attempt was tried and reverted, it settled back on the
native-driven, template-text-matched approach ("Back matched by template
text at any row, drawn at the native position"), matching `-x86`'s own
original design. A mod-wide 100ms hold + 50ms fade for every hint glyph
(gameplay and menu) was also added 2026-09-22 specifically to smooth over
gaps in native-draw detection. Despite all of this, a residual flicker on
the pause-menu Back glyph specifically is still present as of the latest
build — purely cosmetic, Back still functions correctly either way.

</details>

<details>
<summary><b>3. Sentry/turret-placement and Campaign QTE prompt text</b> — full detail</summary>

Survival's ready-up prompt and the buy-station use-prompt are both now
fully replaced with real glyph + text (2026-09-19/21, via the client
hudelem draw hook `FUN_140046a30` and a structural "Hold/Press ^N key ^7"
template match respectively). What's left — sentry/turret placement and
Campaign QTE prompts — is blocked on the same genuinely unconfirmed
`Font_s`/`fontName` offset that blocked ready-up until its own hudelem
route was found (neither has a known reference-key template even on
`-x86`; a dedicated investigation tried to independently confirm this
offset via decompile and could not — a real negative result, not a skipped
step, see `re_notes/x64_migration/drawtext_hook_x64.md`'s "Stage (d)").
Sentry-Place's own reference string wasn't found anywhere in the x64 binary
either.

Separately: on-screen alignment for the glyph-icon categories (Mantle,
Pickup/Swap/Pickup-health, Throwback, Reload/low-ammo, five menu corner
hints, buy-station, pause-menu Back) is unverified beyond the ones
explicitly live-tested — no pixel-tuning nudges were ported for the rest
yet, and the Special-Ops/Friends-list suppression logic (a faithful port of
`-x86`'s own v4 sticky-state algorithm, which itself took four iterations
to get right) has never been fully live-exercised on x64.

</details>

<details>
<summary><b>4. OpenAssetTools Unlinker crash (dev tooling)</b> — full detail</summary>

A real, project-wide GSC-extraction tooling blocker was found 2026-09-14:
`Unlinker` (both the vendored version and the current latest release)
reproducibly crashed loading any real retail zone file from this install,
almost certainly because the 2026-09-03 x64 recompile changed the zone
container format. Blocked fresh GSC decompilation for any future
investigation until resolved.

**Real, substantial progress since**: the dominant failure classes (a
Material/shader-chain offset-pointer-width bug, a `snd_alias_list_t`/
`LoadedSound` struct-layout bug, and — this week's own major breakthrough
— a completely wrong wire shape for `SpeakerMap`/`MSSChannelMap`/
`MSSSpeakerLevels`, found via native x64 decompile after 40+ investigation
rounds) are all fixed. **5 zones now load completely cleanly** (`sp_intro.ff`,
`sp_prague.ff`, `so_trainer2_so_deltacamp.ff`, `sp_dubai.ff`,
`sp_ny_harbor.ff`), and many previously-crashing zones (`hamburg.ff`,
`common.ff`, `code_post_gfx.ff`, `common_survival.ff`) now progress
substantially further without crashing. **Currently open**: a
`LoadedSound` alias-miss during Sound-asset dependency sharing — two
independent live-tested attempts to degrade the failing resolution both
caused a real, different downstream segfault; the crash has been narrowed
(via a new self-dump + offline-analysis technique, since live debugger
attach is currently confirmed unsafe on this machine) to a corrupted
`std::string` discovered during a hash-table walk, whose actual insertion
point isn't yet identified. See
[`tools/iw5oat/README.md`](tools/iw5oat/README.md)'s own "Current status"
section and `re_notes/x64_migration/fastfile_format_research.md` §5.13-§5.47
for the complete trail.

</details>

<details>
<summary><b>5. AC-130 gun-type switching</b> — full detail</summary>

Investigated in depth 2026-09-14 — a whole-binary native string sweep
confirmed this is entirely GSC/data-driven with zero native dispatch case
to hook, and a public MP-only GSC reference couldn't be confirmed to match
Campaign/Spec-Ops's own actual mechanism. Correctly left unfixed rather
than guessed at against an already-working, live-confirmed feature this
session had no way to verify a change against. See
`re_notes/known_issues.md` issue #40.

</details>

<details>
<summary><b>6. Custom Options screen's vanilla-setting data layer</b> — full detail</summary>

The custom Options screen's real vanilla-setting tabs
(Look/Video/Audio/Voice/Advanced Video/Movement/Actions) are silently
non-functional on x64, found 2026-09-13. The screen itself opens,
navigates, and responds to mouse clicks correctly — the UI shell is
genuinely shared, arch-neutral code — but every value shown on those 7 tabs
is a stub (dvar reads and keybind reads both return nothing on x64) and
every edit is silently discarded (dvar and keybind writes are explicit
no-ops on x64, added 2026-09-04 to stop a real x86-only crash risk, never
replaced with a working x64 path). Nothing in the UI indicates this. Only
the Controller tab and the Custom Binds tab (both backed by this mod's own
config, not real game settings) actually work. See
`re_notes/x64_feature_parity_audit.md` row #64 for the full trace — closing
this needs real x64 reverse-engineering work, not attempted yet.

**Lower priority than it first looked**: this whole screen never actually
finished maturing on `-x86` either (confirmed 2026-09-14 against
`legacy-x86-docs/PATCHNOTES.md` — shipped v0.3.1 as explicit preview/WIP,
off by default, and never once mentioned again as graduating past that all
the way through v0.3.5). x64's specific gap (the data layer, not the UI) is
real and worth closing eventually, but it isn't "behind a finished x86
feature" — neither line ever finished it. **Deferred, 2026-09-14 (direct
decision): explicitly NOT a priority for this release.** The INI config
(`mw3ncp_config.ini`) is a reliable, already-working settings path for
everything this mod itself controls; the custom Options screen's remaining
value is only for real VANILLA game settings, a real but genuinely
lower-value target. Not planned again until a later pass, well past the
current `-x64` parity push.

</details>

<details>
<summary><b>7. Back's `+scores` scoreboard</b> — full detail</summary>

Ported (build-verified, not yet live-tested), but this was never a real
functionality gap — confirmed live on `-x86`, including direct testimony
from actual Xbox 360 console play, that this bind is a genuine no-op in
Campaign/Survival on every platform (no scoreboard UI exists there at all).
It'll have real value once Multiplayer support ships, where a scoreboard
genuinely exists.

</details>

<details>
<summary><b>8. SMAA edge smoothing</b> — full detail</summary>

MW3 (2011) ships no working AA (the render target reports zero MSAA
samples). SMAA 1x (three passes — edge detect, blend weights via area/search
lookup textures, neighborhood blend — from the MIT-licensed iryoku reference,
credited in `LICENSE`/`README.md`) was implemented and shipped 2026-09-21,
off by default. A real bug (the blend-weight pass reading the area texture
from the wrong channels — HLSL selects `.ra`, the upload used `.rg`) was
found and fixed the same day, but even after that fix, `SmaaDebugView=3` (a
plain capture-and-redraw with no SMAA math at all) still looked worse than
off and cost far more frame time than expected — meaning the shared
capture/redraw path the full-screen passes share (also used by motion blur
and FSR) is itself suspect, independent of the SMAA shaders. **Parked**
until that's understood; not viable to ship as default-on yet. A simpler
FXAA-style pass (`[Video] FxaaEnabled`) shipped the same day as an
edge-blur-only fallback, off by default. Full staged roadmap (temporal
upscalers, path tracing, a DX12/Vulkan translation layer) in
`re_notes/known_issues_x64.md` issue #2.

</details>

Full detail, investigation trails, and current status on every item:
[`re_notes/known_issues_x64.md`](re_notes/known_issues_x64.md) and
[`re_notes/x64_feature_parity_audit.md`](re_notes/x64_feature_parity_audit.md).

## How it works

The same core technique underlies all four components:

- **Injection**: a proxy `d3d9.dll` sits beside the game's real one, forwards
  every real export through untouched, and gets code execution with no
  external injector needed.
- **Controller input**: this project links XInput itself (the game never
  did) and hooks the engine's own per-frame usercmd-build function to feed
  movement/look/buttons in natively — not synthesized keypresses.
- **Visual enhancements**: hook the engine's own render pipeline (`EndScene`/
  `Reset`) and post-process ahead of/after the native draw, or write directly
  to real, already-registered engine dvars — never a separate overlay
  renderer. (The dvar-write path specifically is currently broken on x64 —
  see [Known gaps](#known-gaps) issue #6 in `known_issues_x64.md`; render
  scale/motion blur/FSR use the D3D9-hook path instead and are unaffected.)
- **Netcode security fixes**: hook the game's own real network-message-
  parsing functions directly, validating/clamping attacker-controlled data
  to a known-safe size *before* the vulnerable original code runs — see
  [`security/`](security/) for the full technique.
- **Hook targets are found via runtime signature scanning** across every
  component — a wildcarded byte-pattern scan against the game's own main
  module, resolved once at process startup and cached for the session, not
  a continuous re-scan loop.
- **No OS-level input emulation**, with a small number of explicitly scoped,
  documented exceptions where no native trigger could be found after an
  exhaustive search — see [`CONTRIBUTING.md`](CONTRIBUTING.md).
- **`iw5sp.exe` (Campaign/Survival) and `iw5mp.exe` (Multiplayer) are
  treated as independent reverse-engineering efforts** — a fix or signature
  found in one is never assumed to carry over to the other. See
  [Multiplayer](#multiplayer) below for that binary's own status.

Full technical trail (every function found, every dead end ruled out):
[`re_notes/known_issues_x64.md`](re_notes/known_issues_x64.md) and
[`re_notes/x64_migration/README.md`](re_notes/x64_migration/README.md).

## Repository structure — nested components

This isn't one flat codebase. Two real, separate git histories are merged
into specific subdirectories here, each via a history-preserving
`git subtree` (not a fresh copy, not a submodule) — each one's own original
commit history is fully intact and browsable in place, and each is under
its **own** license, distinct from this repo's own root one.

| Path | What it is | Why it's here | Status | License |
|---|---|---|---|---|
| [`security/`](security/) | This project's own netcode-security-patch component — originally the separate `MW32011NSP` repo, absorbed 2026-09-12 | Finds and fixes real, exploitable vulnerabilities in MW3's own base-game netcode — Steam's VAC doesn't cover packet-level attacks from a malicious server/peer, a real gap this closes. Ships built into the mod by default (see [Security](#security-netcode-vulnerability-patches) below) | 4 of 4 confirmed vulnerabilities resolved (3 fixed, 1 already safe) | This repo's own permissive license, plus one extra responsible-disclosure clause — see [`security/LICENSE`](security/LICENSE) |
| [`tools/iw5oat/`](tools/iw5oat/) | An IW5-only fork of [OpenAssetTools](https://github.com/Laupetin/OpenAssetTools), a third-party CoD modding-tool suite, plus this project's own **[`x64_offset_fixes/`](tools/iw5oat/x64_offset_fixes/)** post-processing scripts | The 2026-09-03 x64 recompile broke upstream's own zone/fastfile loading for the current retail build — a real, confirmed bug ([full root-cause writeup](re_notes/x64_migration/fastfile_format_research.md)), with no existing x64 support anywhere in that codebase to build on. This fork exists to build the x64 support this project actually needs for GSC extraction (the standing GSC-first RE methodology), faster than waiting on an upstream/community fix; `x64_offset_fixes/` is the repeatable tooling that patches the generated per-asset loader code's own x86-only offsets after every `ZoneCodeGenerator` run. **Developer/research tooling only — never shipped to players**, not part of the mod's own `d3d9.dll` | **5 zones fully clean** (0 warnings/0 errors: `sp_intro.ff`/`sp_prague.ff`/`so_trainer2_so_deltacamp.ff`/`sp_dubai.ff`/`sp_ny_harbor.ff`), many more now progress substantially further without crashing after this week's own major `SpeakerMap` wire-shape fix. Currently blocked by a `LoadedSound` alias-miss bug during Sound-asset dependency sharing — see [`tools/iw5oat/README.md`](tools/iw5oat/README.md) for the current, detailed status | **GNU GPLv3** — NOT this repo's own license; see [`tools/iw5oat/LICENSE`](tools/iw5oat/LICENSE) and this repo's own [`LICENSE`](LICENSE)'s "Third-party components" section for exactly how the two coexist |

Each has its own README with the full story — [`security/README.md`](security/README.md)
and [`tools/iw5oat/README.md`](tools/iw5oat/README.md).

## Multiplayer

`iw5mp.exe` is a completely separate binary from `iw5sp.exe`, reverse-
engineered independently — no address or signature from Campaign/Survival
work carries over. Real, active static reverse engineering is underway now
(started 2026-09-05), specifically so this doesn't repeat the `-x86` line's
own history: Multiplayer work there never shipped at all, stalling at
static research and never being picked back up.

**Confirmed so far**: the full key-event → gameplay-bind dispatch chain,
with real addresses extracted for all ~89 dispatch cases (only a handful
confirmed by name so far — every other case number is treated as an
unconfirmed candidate, never assumed, per this project's own hard-learned
policy on trusting dispatcher case numbers); the movement/look pipeline,
cross-validated against Campaign/Survival's own confirmed `usercmd_t` byte
offsets — an exact match across two separately-compiled binaries; high-
confidence, still-unconfirmed candidates for ADS and Hold Breath; and a
real category match for the killstreak-slot system (MW3's own
`assaultStreaks` table). Full live tracking:
[`re_notes/iw5mp_x64.md`](re_notes/iw5mp_x64.md).

**Scope, decided explicitly**: Multiplayer support may proceed using this
project's existing input-remapping methodology (engine-function hooks +
XInput, writing to real input structures — never reading live gameplay-
entity memory) once it reaches live/injection work, but **ships opt-in only**
(default off, with a one-time VAC-risk acknowledgment before it can be
enabled) — VAC is confirmed active on `iw5mp.exe`, a real, non-zero,
unresolved-to-zero risk. Static-only research (no live process attach) is
what's authorized and in progress today; see `CLAUDE.md`'s Version Timeline
for the full decision record. Multiplayer does not gate the Campaign/
Survival release above — expect it as a close fast-follow once that ships,
not bundled into it.

**Parity standard (2026-09-15, until beta)**: since Multiplayer never gates
a release, it's explicitly allowed to lag Campaign/Survival by **2-4
releases** through the `v0.4.0-x64` beta milestone — MP doesn't need to
ship every SP-era feature/fix in lockstep, just stay within that bounded
window. This is deliberate, not neglect: real, tracked static RE progress
continues in parallel (`re_notes/iw5mp_x64.md`) specifically so MP doesn't
repeat the `-x86` line's own history of stalling out entirely. This
standard is explicitly conditional — if Campaign/Survival itself reaches
full completion (in controller-support and original-scope terms) before
`v0.4.0-x64` ships, it'll be revisited rather than mechanically applied to
a finished target.

**Note — visual enhancements are a separate track from the above.**
Everything in this section is specifically about controller input/menu
navigation, gated by the VAC-risk opt-in policy. `InternalRenderScalePercent`
(the visual-enhancement suite's render-resolution override) is a real
exception already shipped in `v0.0.2-x64`: its hook signature was
independently verified against `iw5mp.exe` and doesn't touch gameplay input
or read any entity memory, so it works under Multiplayer today, on by
default, with no VAC-risk acknowledgment needed. Motion blur and FSR remain
SP-only for now — their own required safety-gate signature doesn't resolve
under `iw5mp.exe` yet. **The same render-scale caution above applies here
too, more so**: a live test found the identical 200% that's clean in SP
causes constant stutter in MP — see the [Known gaps](#known-gaps) warning.

## Compatibility

Built and verified only against retail Steam MW3. See the
[wiki Compatibility page](../../wiki/Compatibility) for the full
per-client/per-mission breakdown.

**⚠ Do not use this with Plutonium multiplayer.** Plutonium's own anti-cheat
is confirmed to ban DLL injection and memory access — a 7-day ban on first
offense, permanent after. This project's entire architecture (a proxy
`d3d9.dll`, function hooking) is exactly what that system is built to catch,
regardless of this being input-only rather than a gameplay cheat. This is a
real, confirmed risk, not theoretical. Supported: retail Steam
Campaign/Survival only.

### Support policy

Steam retail only, `-x86` permanently discontinued (not just unsupported —
it will not be rebuilt or re-released under any name or framing), and
cracked/pirated copies are never supported on either architecture. Full
statement, kept current in one place: [`LTS_POLICY.md`](LTS_POLICY.md#support-scope).

## Installation

1. Requires a legitimate copy of Call of Duty: Modern Warfare 3 (2011) on
   Steam.
2. Drop the built `d3d9.dll` (and its companion files) into the game's
   install directory, alongside `iw5sp.exe`.
3. Launch the game normally — the mod loads automatically, no separate
   injector needed.
4. Configuration lives in `mw3ncp_config.ini`, generated on first launch next
   to the DLL. See the [wiki Configuration page](../../wiki/Configuration)
   for every available key.

Download the latest `v0.0.2-x64` release from this repo's
[Releases page](../../releases/latest) or [Nexus Mods](https://www.nexusmods.com/callofdutymodernwarfare3/mods/29)
— both ship the same zip (`d3d9.dll` + `LICENSE` + `PATCHNOTES.md` + a short
`README.txt`). Building from source requires Windows, MSVC (Visual Studio
Build Tools or Community with the Windows 10 SDK), and MSBuild; see
[`CONTRIBUTING.md`](CONTRIBUTING.md) for the full build/RE-tooling setup.

## Security: netcode vulnerability patches

[`security/`](security/) — same reverse-engineering methodology and
proxy-DLL injection technique as the rest of this repo, finding and fixing
real, exploitable vulnerabilities in MW3's own netcode. It exists because
Steam's VAC doesn't cover packet-level attacks from a malicious server or
peer — a real gap for anyone playing Multiplayer or Spec-Ops/Survival co-op,
and the reason this component exists at all. **Complete, end to end — all 4
confirmed vulnerabilities are now resolved** (3 fixed, 1 confirmed already
safe). 2 of the 3 active fixes are independently confirmed firing against
real MP traffic; the third (SP/Spec-Ops P2P) is build-verified and resolves
correctly live, just not yet observed against a real P2P session — none
have been tested against an actual malicious packet, since that can't
safely be simulated. See [`security/README.md`](security/README.md) for the
full findings table and current fix status.

**A patch to the native install, not a separate client.** This ships as a
proxy DLL sitting next to the exact same official `iw5sp.exe`/`iw5mp.exe`
Steam already installs — same executable, same Steam launch, same native
matchmaking/P2P infrastructure, same players. Nothing about switching to a
different platform or backend (contrast third-party clients like
Plutonium, which steer players onto an entirely separate server
infrastructure instead of patching the vulnerable code directly). **One
real limitation worth being explicit about**: this only protects players
who actually have this mod installed. It patches the running process in
memory, not Activision's own shipped game files — a vanilla, unmodified
Steam install of MW3 is still exactly as vulnerable as before. Full
technical detail on all four findings has been submitted to Activision
through their official disclosure channel; only they can actually fix the
real binary for every player, modded or not.

**Originally a separate sibling repo (`MW32011NSP`), absorbed into this
one 2026-09-12** as part of the redefinition described at the top of this
file — its full commit history, including the original vendor
security-disclosure record submitted to Activision, carried over intact via
a `git subtree` merge, not a fresh copy. The former `github.com/k8se10/
MW32011NSP` repo is now archived as a read-only pointer to this location;
nothing there was lost, just superseded. Full decision record: `CLAUDE.md`'s
2026-09-12 Version Timeline entry.

This mod ships `security/`'s fixes **built in by default**: a small,
explicit "greenlit" allowlist in this project's own plugin loader
auto-loads the security-fix plugin (built from
[`security/tools/ncp_plugin_netcode_fixes/`](security/tools/ncp_plugin_netcode_fixes/))
without requiring the normal third-party-plugin opt-in
(`[Plugins] Enabled=1`) — see [`PLUGIN_API.md`](PLUGIN_API.md) for the full
design. `security/` also still builds its own standalone DLL
([`security/proxy_d3d9/`](security/proxy_d3d9/)) for anyone who wants the
netcode fixes without the rest of this mod.

## Contributing

Contributions are welcome — see [`CONTRIBUTING.md`](CONTRIBUTING.md) for the
ground rules and [`CODE_STANDARDS.md`](CODE_STANDARDS.md) for the
production-ready bar every change is held to. Contributing to the security
component specifically? It has its own
[`security/CONTRIBUTING.md`](security/CONTRIBUTING.md) and
[`security/CODE_STANDARDS.md`](security/CODE_STANDARDS.md), extending the
root versions with responsible-disclosure requirements. See
[`PLUGIN_API.md`](PLUGIN_API.md) if you want to extend the mod (or write a
sub-mod for the game itself) without touching this repo's own source.

## Credits

This project vendors and links the following third-party library:

- **[MinHook](https://github.com/TsudaKageyu/minhook)**
  (`proxy_d3d9/third_party/minhook/`) — Copyright (C) 2009-2017 Tsuda
  Kageyu. BSD 2-Clause-style license (see
  `proxy_d3d9/third_party/minhook/LICENSE.txt`). Used for all API hooking
  (vtable and inline detours) in the proxy DLL.
- **Hacker Disassembler Engine (HDE) 32/64 C**, bundled with MinHook —
  Copyright (c) 2008-2009, Vyacheslav Patkov. Same style of license (see the
  same `LICENSE.txt`).
- **[DXVK](https://github.com/doitsujin/dxvk)** (`proxy_d3d9/third_party/dxvk/`,
  v3.1.1) — Copyright (c) 2017 Philip Rebohle and contributors, zlib/libpng
  license (see `proxy_d3d9/third_party/dxvk/LICENSE.txt`). A real, independently-
  maintained D3D9-to-Vulkan translation layer, powering the default-on, SP-only
  `[Video] GraphicsApi=Vulkan` mode (2026-09-26, following a real live-confirmed
  track record — LegacyD3D9 remains fully supported and selectable) this
  project uses to unlock native NVIDIA
  Streamline/DLSS integration — see `re_notes/x64_migration/vulkan_dlss_pipeline_research.md`
  for the full research and architecture behind this feature. Native-Windows use
  of DXVK is real but explicitly unofficial per the upstream project's own docs;
  this mod loads it directly (not layered under ReShade or any other framework)
  the same way this project already hooks the real system `d3d9.dll`.
- **[NVIDIA Streamline](https://github.com/NVIDIAGameWorks/Streamline)**
  (`proxy_d3d9/third_party/streamline/`) — Copyright (c) 2023 NVIDIA
  Corporation, MIT license (see
  `proxy_d3d9/third_party/streamline/license.txt`). Only the public SDK
  headers are committed here (not the source build system, sample code, or
  platform binaries). NVIDIA's compiled, signed runtime binaries (`sl.*.dll`,
  `nvngx_*.dll`) are not part of this project and are not covered by its
  license: they are governed by NVIDIA's own RTX SDKs License, redistributed
  unmodified in object-code form only, never committed to this repository,
  and licensed for use only on NVIDIA GPUs — see `LICENSE`'s "Third-party
  components" section. DLSS is an NVIDIA-GPU-only feature within the
  vendor-neutral `Vulkan` mode. See
  `re_notes/x64_migration/vulkan_dlss_pipeline_research.md` section 2.4 for
  the full licensing research and section 2 for the integration plan.
- **[RenderDoc](https://github.com/baldurk/renderdoc)** —
  `renderdoc_app.h` only (`proxy_d3d9/third_party/renderdoc/`), Copyright (c)
  2015-2026 Baldur Karlsson, MIT license. This is the official in-application
  API header for RenderDoc's real GPU frame-capture tool, letting the mod
  itself trigger a capture (F11, dev/RE tooling — see
  `re_notes/known_issues_x64.md` issue #4) without any external process
  attach. RenderDoc's own runtime (`renderdoc.dll`) is not vendored or
  bundled — it must be installed separately (free, renderdoc.org) for F11 to
  do anything; the mod degrades cleanly and logs clearly if it isn't found.

The x64 line's `[Video] FramePacingEnabled` frame-pacing limiter
(`proxy_d3d9/src/frame_pacing_x64.cpp`), `[Video] WaitCoalescingEnabled`
wait-coalescing/archive-priority-boost feature
(`proxy_d3d9/src/wait_coalescing_x64.cpp`), and `[Video] IwdReadAccelEnabled`
persistent `.iwd` archive read cache
(`proxy_d3d9/src/iwd_read_cache_x64.cpp`) port their algorithms from
**[legoliamneeson/MW3_Standalone_D3D9_Project](https://github.com/legoliamneeson/MW3_Standalone_D3D9_Project)**
(Copyright legoliamneeson) — that project's own README states no
redistribution license is asserted for the ported source; credited here per
its own attribution note.

The x64 line's `[Video] Smaa*` anti-aliasing passes are based on **SMAA** by
Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro and Diego
Gutierrez (**[iryoku/smaa](https://github.com/iryoku/smaa)**, Copyright 2013,
MIT license) — reference shader, area and search lookup textures used with the
authors' license notice preserved.

This project also embeds **[Isotherm Sans](https://github.com/k8se10/isotherm-sans)**
(UI, Condensed, and Italic styles) as a private, in-process-only font — a
modernized derivative of [Manrope](https://github.com/sharanda/manrope)
(Copyright 2018 The Manrope Project Authors), SIL Open Font License 1.1 (see
`assets/fonts/IsothermSans-OFL.txt`).

Two further, larger components are absorbed as full nested repositories
(their own git history and license, each merged via a history-preserving
`git subtree`, not vendored as a plain library) — **[OpenAssetTools](https://github.com/Laupetin/OpenAssetTools)**
(`tools/iw5oat/`, GPLv3) and this project's own former `MW32011NSP` sibling
project (`security/`, this repo's own license). See the
[Repository structure](#repository-structure--nested-components) section
above for the full story on each, and `LICENSE`'s own "Third-party
components" section for the complete, authoritative license list.

## Support this project

This is built and maintained entirely in spare time, at no cost to players —
no ads, no paywalled features, nothing ever sold (the license itself forbids
it, see below). If it's been useful to you, a donation is completely
optional but genuinely appreciated — it goes straight back into the time
this reverse-engineering work takes.

[![Support on Ko-fi](https://img.shields.io/badge/support-ko--fi-FF5E5B?logo=ko-fi&logoColor=white)](https://ko-fi.com/officialk8)

## License

This project's own source is released under a custom, permissive license —
see [`LICENSE`](LICENSE). The source is fully open: free to use, modify, and
fork. The one restriction is that neither this project nor any
fork/derivative of it may ever be sold or charged for — it must stay free
for everyone. Because of that restriction, this license does not meet the
OSI's formal "open source" definition, which requires no limits on
commercial use. It does not grant any rights to Call of Duty: Modern
Warfare 3 itself — you need your own legitimate copy of the game to use this
project. `security/` has its own `LICENSE`, identical in substance with one
extra component-specific clause — see [`LICENSE`](LICENSE)'s own scope note
for exactly how the two relate.
