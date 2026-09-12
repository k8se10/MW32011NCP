# MW32011NCP — Native Controller & Enhancement Project for MW3 (2011)

A from-scratch, native reverse-engineering project that brings real controller
support — plus a growing suite of visual and quality-of-life enhancements — to
**Call of Duty: Modern Warfare 3 (2011, IW5 engine)**, covering Campaign and
Survival. It is not a keyboard/mouse-emulation mapper: analog movement, look,
and every button hook the game's own real engine functions directly, the same
internal calls a keyboard/mouse player already uses, just fed from a
controller instead.

MW3 (2011) shipped on PC with **zero working controller input path** — no
`xinput`/`dinput8` import anywhere in either binary, no hidden setting to
unlock. Every control this project supports was found and wired up through
static reverse engineering (Ghidra) plus live verification against the
running game, not assumed.

## ⚠ Security notice: unpatched MW3 (2011) netcode vulnerabilities

Independent security research (this project's sibling repo,
[MW32011NSP](https://github.com/k8se10/MW32011NSP)) has identified real,
network-reachable vulnerabilities in MW3 (2011)'s own base-game code —
**not in this mod** — affecting both Multiplayer and Spec-Ops/Survival
co-op. These were re-confirmed present and unpatched in the game's most
recent (September 2026) update, so they are not something you can fix by
updating.

**What this means practically**: a malicious peer, server, or party/lobby
host could potentially crash your game or worse. We are not publishing
exact technical detail while these remain unpatched — doing so before
Activision has a chance to fix them would put every MW3 player at risk, not
just this mod's users. A full report has been submitted to Activision
through their official security-disclosure channel.

**What you can do in the meantime**:
- Be cautious joining Multiplayer servers/lobbies you don't trust, especially
  third-party-hosted ones.
- Be cautious in Spec-Ops/Survival co-op sessions with strangers.
- This risk exists independent of whether you use this mod — it's in the
  base game's own networking code.

We'll update this notice with full technical detail once a fix ships and is
verified, per standard responsible-disclosure practice. See
[MW32011NSP](https://github.com/k8se10/MW32011NSP) for the project working
on fixes for these directly (a proxy-DLL patch, the same technique this mod
uses for input).

## Status

**Alpha, `v0.0.1-x64` line.** On 2026-09-03 MW3 received its first real
binary update in the game's history, recompiling both `iw5sp.exe`/`iw5mp.exe`
from 32-bit to 64-bit — a hard architectural break that invalidated every
hook this project had. The old 32-bit (`-x86`) line is fully discontinued;
its final state and documentation are preserved in
[`legacy-x86-docs/`](legacy-x86-docs/) for reference. This repository is now
rebuilding for x64 from that foundation, using the same reverse-engineering
methodology.

**Release gate**: no `-x64` release ships until this reaches the same
feature completeness the `-x86` line reached before being discontinued —
every control, the visual-enhancement suite, and the custom Options screen
all working, not just the input-remapping core. Current estimate: a few
weeks out. See [`re_notes/known_issues_x64.md`](re_notes/known_issues_x64.md)
issue #1 for the live, detailed tracking of exactly what's done and what's
left.

**Multiplayer (`iw5mp.exe`) does not gate this release**, but work on it
starts now, alongside Campaign/Survival's remaining gaps, rather than
after — the `-x86` line's own Multiplayer effort never actually shipped
(it stalled at reverse-engineering only) and this project doesn't want to
repeat that. Expect it as a close fast-follow release once the first
`-x64` build ships, not bundled into it.

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
| | Visual-enhancement suite (render scale, FSR, motion blur) |
| | Native controller menu/UI navigation (main menu, pause, options, buy-stations) |
| | Menu-focus/itemDef tracking (glyph-icon dependency) |

### Known gaps

These are honestly documented as not-yet-implemented, not hidden bugs. A
**complete, systematic audit against every `-x86` feature is now done** — see
[`re_notes/x64_feature_parity_audit.md`](re_notes/x64_feature_parity_audit.md)
for the full 61-item table (34 confirmed present, 21 confirmed absent, 6
partial/regressed). The items below are the highest-impact gaps it found;
see that file for everything else (menu glyphs, killstreaks, config
presets, the plugin API, background threads, and more).

- **Controller-glyph icons, on-screen hint prompts, the highlighted-item
  A-glyph, the F2/F3 glyph-position editor, and the custom cursor** still
  don't draw on x64. The underlying menu-focus/item-position tracking they
  depend on has now been ported — the remaining gap is a separate, not-yet-
  ported piece (the native text-draw hook itself). Everything else renders
  normally (including this mod's own startup/hot-reload toast messages,
  which ARE confirmed working on x64).
- **DualSense gyro-aim isn't wired into x64's look input.** Basic DualSense
  stick input (movement/look) works fine on x64 already; gyro-aim
  specifically doesn't. This was still a preview/WIP feature even on the
  `-x86` line, so this is a lower-priority gap than the others above.
- **Hold Breath, Survival ready-up (hold Y), Auto-Mantle, and Back's
  `+scores` scoreboard** are all absent on x64 — none of the four have any
  wiring in the current x64 input pipeline yet.
- **FXAA and a forced-MSAA option** were never actually built even on the
  old `-x86` line (only ever planned) — real future work, not a regression.

Full detail, investigation trails, and current status on every item:
[`re_notes/known_issues_x64.md`](re_notes/known_issues_x64.md) and
[`re_notes/x64_feature_parity_audit.md`](re_notes/x64_feature_parity_audit.md).

## How it works

- **Injection**: a proxy `d3d9.dll` sits beside the game's real one, forwards
  every real export through untouched, and gets code execution with no
  external injector needed.
- **Input**: this project links XInput itself (the game never did) and hooks
  the engine's own per-frame usercmd-build function to feed movement/look/
  buttons in natively — not synthesized keypresses.
- **Hook targets are found via runtime signature scanning** — a wildcarded
  byte-pattern scan against the game's own main module, resolved once at
  process startup and cached for the session, not a continuous re-scan loop.
- **No OS-level input emulation**, with a small number of explicitly scoped,
  documented exceptions where no native trigger could be found after an
  exhaustive search — see [`CONTRIBUTING.md`](CONTRIBUTING.md).
- **Both target binaries** (`iw5sp.exe` for Campaign/Survival, `iw5mp.exe`
  for Multiplayer) are treated as independent reverse-engineering efforts —
  a fix or signature found in one is never assumed to carry over to the
  other. Multiplayer's x64 reverse engineering is in progress now, as a
  fast-follow to this release rather than blocking it (see the Status
  section above).

Full technical trail (every function found, every dead end ruled out):
[`re_notes/known_issues_x64.md`](re_notes/known_issues_x64.md) and
[`re_notes/x64_migration/README.md`](re_notes/x64_migration/README.md).

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

**NCP now stands for Native Community Patches** — this project's identity
expanded 2026-09-12 to cover not just controller input and visual/QoL
enhancements, but native netcode security patching too, absorbed from what
was previously a separate sibling repo (`MW32011NSP`). Controller support
remains the flagship, first-shipped patch; it isn't the whole of what this
project is anymore. Full record of the redefinition and the merge itself:
`CLAUDE.md`'s 2026-09-12 Version Timeline entry.

[`security/`](security/) is this project's own netcode security component
— same reverse-engineering methodology and proxy-DLL injection technique as
the rest of this repo, finding and fixing real, exploitable vulnerabilities
in MW3's own netcode, independent of anything the controller/enhancement
side does. It exists because Steam's VAC doesn't cover packet-level attacks
from a malicious server or peer, a real gap for anyone playing Multiplayer
or Spec-Ops/Survival co-op. See [`security/README.md`](security/README.md)
for its own full technical detail (findings table, current fix status).

This mod ships `security/`'s fixes **built in by default**: a small,
explicit "greenlit" allowlist in this project's own plugin loader
auto-loads the security-fix plugin (built from `security/tools/
ncp_plugin_netcode_fixes/`) without requiring the normal third-party-plugin
opt-in (`[Plugins] Enabled=1`) — see [`PLUGIN_API.md`](PLUGIN_API.md) for
the full design. `security/` also still builds its own standalone DLL
(`security/proxy_d3d9/`) for anyone who wants the netcode fixes without the
rest of this mod. See the security notice above for current vulnerability
status.

The former `github.com/k8se10/MW32011NSP` repo's full commit history
(including the original vendor security-disclosure record) carried over
intact into this repo's own history via a `git subtree` merge, not a fresh
copy. That repo itself is being archived as a historical, read-only
pointer to this new location — nothing there is lost, just superseded.

## Contributing

Contributions are welcome — see [`CONTRIBUTING.md`](CONTRIBUTING.md) for the
ground rules and [`CODE_STANDARDS.md`](CODE_STANDARDS.md) for the
production-ready bar every change is held to. See
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
project.
