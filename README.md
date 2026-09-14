# MW32011NCP — Native Community Patches for MW3 (2011)

A native, from-scratch reverse-engineering platform for **Call of Duty:
Modern Warfare 3 (2011, IW5 engine)** — not a single mod, but a patch layer
with four real, distinct components, all built on the same technique (a
proxy `d3d9.dll` that hooks the game's own real engine functions directly —
never a keyboard/mouse-emulation mapper, never synthesized input where a
native call exists, never a config tweak):

| Component | What it does | Status |
|---|---|---|
| **Controller support** | Real analog movement/look/every button for Campaign & Survival, matching console behavior | Flagship, most mature — see [What works](#what-works-right-now) |
| **Visual/performance enhancements** | Internal render scale, FSR 1.0 sharpening, motion blur, forced anisotropic filtering/shadow/lighting quality, stutter/threading fixes | Wired for x64, not yet live-tested |
| **Netcode security patches** | Finds and fixes real, exploitable vulnerabilities in the base game's own netcode | 3 of 4 confirmed vulnerabilities fixed — see [Security](#security-netcode-vulnerability-patches) |
| **Multiplayer (`iw5mp.exe`)** | Same controller/security methodology, ported to the separate MP binary | Active reverse-engineering, opt-in-only when it ships — see [Multiplayer](#multiplayer) |

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

## ⚠ Security notice: unpatched MW3 (2011) netcode vulnerabilities

This project's own research (the [Security](#security-netcode-vulnerability-patches)
component below) has identified real, network-reachable vulnerabilities in
MW3 (2011)'s own base-game code — **not in this mod** — affecting both
Multiplayer and Spec-Ops/Survival co-op. These were re-confirmed present and
unpatched in the game's most recent (September 2026) update, so they are not
something you can fix by updating.

**What this means practically**: a malicious peer, server, or party/lobby
host could potentially crash your game or worse. We are not publishing exact
technical detail while these remain unpatched — doing so before Activision
has a chance to fix them would put every MW3 player at risk, not just this
mod's users. A full report has been submitted to Activision through their
official security-disclosure channel.

**What you can do in the meantime**:
- Be cautious joining Multiplayer servers/lobbies you don't trust, especially
  third-party-hosted ones.
- Be cautious in Spec-Ops/Survival co-op sessions with strangers.
- This risk exists independent of whether you use this mod — it's in the
  base game's own networking code. This mod's own fixes for it are covered
  in [Security](#security-netcode-vulnerability-patches) below.

We'll update this notice with full technical detail once a fix ships and is
verified, per standard responsible-disclosure practice.

## Status

**Alpha, `v0.0.1-x64` line.** On 2026-09-03 MW3 received its first real
binary update in the game's history, recompiling both `iw5sp.exe`/`iw5mp.exe`
from 32-bit to 64-bit — a hard architectural break that invalidated every
hook this project had. The old 32-bit (`-x86`) line is fully discontinued;
its final state and documentation are preserved in
[`legacy-x86-docs/`](legacy-x86-docs/) for reference. This repository is now
rebuilding for x64 from that foundation, using the same reverse-engineering
methodology, across all four components above.

**Release gate**: no `-x64` release ships until Campaign/Survival controller
support reaches the same feature completeness the `-x86` line reached before
being discontinued — every control, the visual-enhancement suite, and the
custom Options screen all working, not just the input-remapping core.
Current estimate: a few weeks out. See
[`re_notes/known_issues_x64.md`](re_notes/known_issues_x64.md) issue #1 for
the live, detailed tracking of exactly what's done and what's left,
[`re_notes/x64_feature_parity_audit.md`](re_notes/x64_feature_parity_audit.md)
for the complete, systematic audit against every `-x86` feature (61 items
tracked), and
[`re_notes/x64_live_testing_checklist.md`](re_notes/x64_live_testing_checklist.md)
for exactly what's build-verified but still needs a real playtest before
this gate can close.

Netcode security fixes and Multiplayer support each have their own status —
see their own sections below; neither gates this release.

### What works right now

| Confirmed live (direct playtest) | Build-verified (not yet live-tested) |
|---|---|
| Analog movement, analog look | Sprint (real kbutton — mechanism changed 2026-09-12, needs re-confirming) |
| Jump, Interact | Jump auto-stand (crouch/prone → standing) |
| Fire, ADS (true hold-to-aim), Reload — **a real intermittent regression was found and fixed 2026-09-13, root cause unrelated to Fire/ADS themselves, see Known gaps; fix awaits live re-confirmation** | D-pad actionslot (all four directions) |
| Melee, Lethal, Tactical | D-pad Left's squadmate-call-in fix |
| Weapon switch (Y) | |
| Crouch/Prone (tap vs. hold) | Plugin API (loader, hook/memory access) |
| Pause menu open/close | Custom Options screen (native trigger + temporary open-chord) |
| Auto-unstick (no more "click once at launch") | Vibration/rumble (fire + damage) |
| | Survival ready-up (hold Y, synthetic F5) |
| | Hold Breath (L3 while ADS'd, sniper-class) |
| | DualSense gyro-aim (preview/WIP, same status as `-x86`, needs real hardware to test) |
| | Visual-enhancement suite (render scale, FSR, motion blur — real x64 trigger hook found 2026-09-13, see Known gaps for verification status) |
| | Native controller menu/UI navigation (main menu, pause, options, buy-stations) |
| | Menu-focus/itemDef tracking (glyph-icon dependency) |
| | Real glyph-icon substitution: Mantle, Pickup/Swap/Pickup-health, Throwback grenade, Reload/low-ammo, menu corner hints (Back/Friends/Quit/Leaderboards/Game Summary) (see Known gaps for what's still native-only) |
| | Highlighted-item A-glyph (menu list navigation) and the F2/F3 glyph-position editor |
| | Auto-Mantle (while sprinting) — ships off by default |
| | Back (scoreboard, `+scores` key-synthesis) — see Known gaps below for why this is correctly a no-op in SP |
| | ADS zoom-aware look-slowdown (`AdsSlowdownStrength`/`Baseline`/close-range taper) |
| | Custom mouse cursor overlay |

### Known gaps

These are honestly documented as not-yet-implemented, not hidden bugs. A
**complete, systematic audit against every `-x86` feature is now done** — see
[`re_notes/x64_feature_parity_audit.md`](re_notes/x64_feature_parity_audit.md)
for the full 61-item table (34 confirmed present, 21 confirmed absent, 6
partial/regressed) — **plus 7 more rows (#62-68) added 2026-09-13 from a
direct git-history audit-completeness sweep** (the original 61-row pass was
sourced from documentation, not raw commit history; 6 of the 7 turned out
already present, 1 was fixed on the spot, and 1 — the Custom Options
screen's real data layer, see below — is a genuinely large gap). The items
below are the highest-impact gaps found across both passes; see that file
for everything else (menu glyphs, killstreaks, config presets, the plugin
API, background threads, and more).
- **Motion blur's real x64 trigger hook found and wired — 2026-09-13,
  build-verified, not yet live-tested.** Live-tested absent earlier the same
  day (its safety gates and per-frame yaw/pitch delta feed were genuinely
  wired 2026-09-12, but the function's only real trigger was an x86-only
  raw-assembly engine hook that never compiled for x64, so the gate was
  armed with nothing calling it). Fresh Ghidra RE found the real x64
  equivalent hook point (`FUN_14018def0`, reached via the same call chain
  as x86's own hook, confirmed via decompile at every hop) — unlike x86 it
  needs no raw assembly, since x64 uses a standard calling convention there.
  Wired via a normal MinHook C++ detour, resolved by signature scan per the
  project's signature-scanning policy. Build-verified on both platforms;
  awaits a live playtest to confirm the effect is visible and correctly
  excludes native HUD and this mod's own overlay, the two failure modes
  its x86 counterpart's own history warns about. Full trail: parity audit
  row #45, `known_issues_x64.md` issue #1's "where is motion blur?" round.
- **Fire and/or ADS intermittently failed — root cause found and fixed
  2026-09-13, build-verified, awaiting live re-confirmation.** Originally
  reported and investigated as sniper-class-specific (a fix attempt was
  shipped around that theory, `g_notifyBindDispatch` resolving a real
  client->server reliable-command notify every bind press/release should
  send but this project's direct-kbutton-call design skipped), then
  live-tested and confirmed NOT weapon-class-specific (happened on the base
  pistol too), then reported intermittent ("on and off") rather than
  constant. The real cause: `Hook_MovementTick` (`analog_input_hooks_x64.cpp`)
  had a stray early `return` that fired whenever the left stick was
  centered — intended only to skip a no-op movement-byte write, it actually
  skipped the ENTIRE rest of the function, including Fire/ADS/Reload/
  Weapnext/Melee/Lethal/Tactical/Jump/Interact/D-pad/CrouchProne/Scoreboard/
  the gameplay-tick Pause-open poll/rumble — exactly the controls a player
  needs while standing still to aim, which is precisely when the left stick
  sits at (0,0). Confirmed as an x64-only regression by re-reading x86's own
  `InjectAllControllerInput` (`analog_input_hooks.cpp`), which calls every
  one of those as fully independent functions with no such gating. Fixed by
  scoping the early return to just the movement-byte write. The earlier
  `g_notifyBindDispatch` fix is unrelated and left in place (additive/inert
  either way). See
  [`re_notes/known_issues_x64.md`](re_notes/known_issues_x64.md)'s
  2026-09-13 "root cause found" round for the full trace.
- **The custom Options screen's real vanilla-setting tabs
  (Look/Video/Audio/Voice/Advanced Video/Movement/Actions) are silently
  non-functional on x64**, found 2026-09-13. The screen itself opens,
  navigates, and responds to mouse clicks correctly — the UI shell is
  genuinely shared, arch-neutral code — but every value shown on those 7
  tabs is a stub (dvar reads and keybind reads both return nothing on x64)
  and every edit is silently discarded (dvar and keybind writes are
  explicit no-ops on x64, added 2026-09-04 to stop a real x86-only crash
  risk, never replaced with a working x64 path). Nothing in the UI
  indicates this. Only the Controller tab and the new Custom Binds tab
  (both backed by this mod's own config, not real game settings) actually
  work. See `re_notes/x64_feature_parity_audit.md` row #64 for the full
  trace — closing this needs real x64 reverse-engineering work, not
  attempted yet. **Lower priority than it first looked**: this whole
  screen never actually finished maturing on `-x86` either (confirmed
  2026-09-14 against `legacy-x86-docs/PATCHNOTES.md` — shipped v0.3.1 as
  explicit preview/WIP, off by default, and never once mentioned again as
  graduating past that all the way through v0.3.5). x64's specific gap
  (the data layer, not the UI) is real and worth closing eventually, but
  it isn't "behind a finished x86 feature" — neither line ever finished it.
  **Deferred, 2026-09-14 (direct decision): explicitly NOT a priority for
  this release.** The INI config (`mw3ncp_config.ini`) is a reliable,
  already-working settings path for everything this mod itself controls;
  the custom Options screen's remaining value is only for real VANILLA
  game settings, a real but genuinely lower-value target. Not planned
  again until a later pass, well past the current `-x64` parity push.

- **Controller-glyph icons now draw for real on x64 for eight in-game/menu
  hint categories** (2026-09-13, three same-day follow-up passes on top of
  the text-draw hook itself): **Mantle, weapon pickup/swap/pickup-health,
  grenade throwback, Reload/low-ammo, and menu corner hints (Back, Friends,
  Quit, Leaderboards, Game Summary)** all now suppress the native hint text
  and draw this project's own icon+text instead, detected via an exact
  structural match against the real, live-resolved reference-key text (no
  font-name filtering needed for any of these — Quit/Leaderboards instead
  use a position check, matching `-x86`'s own real discriminator for this
  exact class of false-positive). Reload and the first two menu corner
  hints (Back/Friends) were initially believed unreachable from this hook
  (an earlier finding the same day claimed Reload's text "flows through a
  completely different native draw function," and menu corner hints simply
  weren't attempted), but that was a decompiler artifact — one more hop of
  RE (`FUN_1402afa60` → `FUN_1402b1090` → `FUN_14029a2b0`, the exact
  function this hook already detours) confirmed both are fully reachable
  after all. A THIRD same-day pass then closed Quit/Leaderboards/Game
  Summary plus the Special-Ops-mode-picker/Friends-list Friends-suppression
  logic, previously believed blocked on missing x64 menu-focus/itemDef
  infrastructure — that claim turned out to be stale (the same day's
  earlier menu-focus fix for the A-glyph/F2-F3 features had already closed
  the actual dependency for a different consumer); see `re_notes/x64_migration/
  drawtext_hook_x64.md` for the corrected trail.
  **Buy-station's "Hold F to use Weapon Armory," Survival's ready-up
  prompt, and turret placement still render completely native/
  unmodified** — buy-station and ready-up are blocked on x64's genuinely
  unconfirmed `Font_s` `fontName` offset (needed for `IsGameplayHintFont`-
  style filtering, since neither has a known reference-key template even on
  `-x86`; a dedicated same-day investigation tried to independently confirm
  this offset via decompile and could not — a real negative result, not a
  skipped step, see `re_notes/x64_migration/drawtext_hook_x64.md`'s "Stage
  (d)"); Sentry-Place's own reference string wasn't found anywhere in the
  x64 binary. The custom mouse cursor overlay is a separate system (a
  different pair of native globals, no shared dependency with the
  text-draw hook above) and was ported the same day (build-verified, not
  yet live-tested — see the table above).
  On-screen alignment for all eight working categories is also unverified —
  no pixel-tuning nudges were ported yet, and the Special-Ops/Friends-list
  suppression logic (a faithful port of `-x86`'s own v4 sticky-state
  algorithm, which itself took four iterations to get right) has never been
  live-exercised on x64. **A real positioning bug in this substitution was
  live-confirmed 2026-09-13** (Mantle's icon never draws at all; Interact/
  Reload's substituted text renders at the very top of the screen instead
  of near the real prompt) and root-caused the same day via a fresh
  decompile/disassembly: this hook's own x/y were pre-transform,
  draw-context-local coordinates, not the final screen-pixel position they
  were assumed to be — the real position isn't computed until a native
  scale+alignment-offset step (`FUN_14008d020`) that runs AFTER this hook's
  own interception point. Fixed by calling that real transform directly
  instead of guessing at its internal field layout; build-verified but
  **not yet live-tested** (could not deploy to the live game process this
  session — see `known_issues_x64.md`'s matching round for the full trail
  and a one-shot diagnostic log line to watch for on the next test). See
  `re_notes/x64_migration/drawtext_hook_x64.md` for the exact scope and
  reasoning behind each remaining gap. Everything else renders normally
  (including this mod's own startup/hot-reload toast messages, which ARE
  confirmed working on x64).
- **Highlighted-item A-glyph (menu list navigation) and the F2/F3 in-game
  glyph-position editor are now wired to real x64 menu-focus tracking**
  (2026-09-13) — a separate system from the gameplay-hint icon substitution
  above: this one draws an A-button icon on whichever native menu list item
  is currently highlighted, using the same manually-calibrated position
  table `-x86` already ships, and the F2/F3 editor is the tool used to
  build/extend that table. Both features only ever depended on one shared
  focus-tracking wrapper, which previously always reported "no focus" on
  x64 because its x64 branch predated the real x64 itemDef-array walk built
  2026-09-12 for the Custom Options screen's own open trigger — now routed
  to that same, already-working implementation. Build-verified, not yet
  live-tested. The A-glyph will only draw for menu groups already
  calibrated on `-x86` (the position table itself is shared, unmodified);
  any new/uncalibrated group still needs the F2/F3 editor re-run on x64 to
  confirm it lines up.
- **Auto-Mantle (while sprinting) is now fully wired on x64** (2026-09-13),
  build-verified, ships off by default matching `-x86`'s exact default. The
  native text-draw hook includes a real, language-independent structural
  match against the live localized `PLATFORM_MANTLE` template
  (`IsMantleHintCurrentlyShowingX64()`), and the `+gostand`-forcing
  injection itself is now wired on top of it: `IsSprintActiveX64()` was
  composed from three already-existing x64 tracking variables (an exact
  parity port of `-x86`'s own `IsSprintActive()` — Sprint's move to a real
  kbutton on x64 turned out not to remove the pieces this needed, just
  relocate them). Not yet live-tested. (Survival ready-up, hold Y, and Hold
  Breath, L3 while ADS'd, were both ported 2026-09-12 — see the
  build-verified column above; Hold Breath uses the same
  no-explicit-sniper-check gating as `-x86`, relying on the real native
  kbutton to limit the effect to sniper-class weapons.)
- **Back's `+scores` scoreboard** is now ported (build-verified, not yet
  live-tested), but this was never a real functionality gap — confirmed live
  on `-x86`, including direct testimony from actual Xbox 360 console play,
  that this bind is a genuine no-op in Campaign/Survival on every platform
  (no scoreboard UI exists there at all). It'll have real value once
  Multiplayer support ships, where a scoreboard genuinely exists.
- **FXAA and a forced-MSAA option** were never actually built even on the
  old `-x86` line (only ever planned) — real future work, not a regression.

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
  to real, already-registered engine dvars (`ForceAnisotropicFiltering` and
  similar) — never a separate overlay renderer.
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

No release is currently published — see the Status section above. Building
from source requires Windows, MSVC (Visual Studio Build Tools or Community
with the Windows 10 SDK), and MSBuild; see
[`CONTRIBUTING.md`](CONTRIBUTING.md) for the full build/RE-tooling setup.

## Security: netcode vulnerability patches

[`security/`](security/) — same reverse-engineering methodology and
proxy-DLL injection technique as the rest of this repo, finding and fixing
real, exploitable vulnerabilities in MW3's own netcode. It exists because
Steam's VAC doesn't cover packet-level attacks from a malicious server or
peer — a real gap for anyone playing Multiplayer or Spec-Ops/Survival co-op,
and the reason this component exists at all. **3 of 4 confirmed
vulnerabilities are fixed** (build-verified, not yet live-tested); the
fourth has a real, documented technical blocker, not a guess. See
[`security/README.md`](security/README.md) for the full findings table and
current fix status.

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

This project also embeds **[Isotherm Sans](https://github.com/k8se10/isotherm-sans)**
(UI, Condensed, and Italic styles) as a private, in-process-only font — a
modernized derivative of [Manrope](https://github.com/sharanda/manrope)
(Copyright 2018 The Manrope Project Authors), SIL Open Font License 1.1 (see
`assets/fonts/IsothermSans-OFL.txt`).

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
