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
| Fire, ADS (true hold-to-aim), Reload | D-pad actionslot (all four directions) |
| Melee, Lethal, Tactical | D-pad Left's squadmate-call-in fix |
| Weapon switch (Y) | A fix attempt for sniper-class Fire/ADS |
| Crouch/Prone (tap vs. hold) | Plugin API (loader, hook/memory access) |
| Pause menu open/close | Custom Options screen (native trigger + temporary open-chord) |
| Auto-unstick (no more "click once at launch") | Vibration/rumble (fire + damage) |
| | Survival ready-up (hold Y, synthetic F5) |
| | Hold Breath (L3 while ADS'd, sniper-class) |
| | DualSense gyro-aim (preview/WIP, same status as `-x86`, needs real hardware to test) |
| | Visual-enhancement suite (render scale, FSR, motion blur) |
| | Native controller menu/UI navigation (main menu, pause, options, buy-stations) |
| | Menu-focus/itemDef tracking (glyph-icon dependency) |
| | Real glyph-icon substitution: Mantle, Pickup/Swap/Pickup-health, Throwback grenade (see Known gaps for what's still native-only) |
| | Highlighted-item A-glyph (menu list navigation) and the F2/F3 glyph-position editor |
| | Auto-Mantle (while sprinting) — ships off by default |
| | Back (scoreboard, `+scores` key-synthesis) — see Known gaps below for why this is correctly a no-op in SP |
| | ADS zoom-aware look-slowdown (`AdsSlowdownStrength`/`Baseline`/close-range taper) |

### Known gaps

These are honestly documented as not-yet-implemented, not hidden bugs. A
**complete, systematic audit against every `-x86` feature is now done** — see
[`re_notes/x64_feature_parity_audit.md`](re_notes/x64_feature_parity_audit.md)
for the full 61-item table (34 confirmed present, 21 confirmed absent, 6
partial/regressed). The items below are the highest-impact gaps it found;
see that file for everything else (menu glyphs, killstreaks, config
presets, the plugin API, background threads, and more).

- **Controller-glyph icons now draw for real on x64 for three in-game hints**
  (2026-09-13, same day as the text-draw hook itself shipped): **Mantle,
  weapon pickup/swap/pickup-health, and grenade throwback** all now suppress
  the native hint text and draw this project's own icon+text instead,
  detected via an exact structural match against the real, live-resolved
  reference-key text (no font-name filtering needed for these three).
  **Buy-station's "Hold F to use Weapon Armory," Survival's ready-up
  prompt, Reload, turret placement, and menu corner hints (Back/Friends)
  still render completely native/unmodified** — buy-station
  and ready-up are blocked on x64's still-unconfirmed `Font_s` `fontName`
  offset (needed for `IsGameplayHintFont`-style filtering, since neither has
  a known reference-key template even on `-x86`); Reload is confirmed to
  flow through a different native draw function this hook can't observe at
  all; Sentry-Place's own reference string wasn't found anywhere in the x64
  binary; custom cursor and menu corner hints weren't attempted this pass.
  On-screen alignment for the three working cases is also unverified — no
  pixel-tuning nudges were ported yet. See
  `re_notes/x64_migration/drawtext_hook_x64.md` for the exact scope and
  reasoning behind each gap. Everything else renders normally (including
  this mod's own startup/hot-reload toast messages, which ARE confirmed
  working on x64).
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
