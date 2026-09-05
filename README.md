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

### What works right now

| Confirmed live (direct playtest) | Build-verified (not yet live-tested) |
|---|---|
| Analog movement, analog look | Jump auto-stand (crouch/prone → standing) |
| Sprint, Jump, Interact | D-pad actionslot (all four directions) |
| Fire, ADS (true hold-to-aim), Reload | D-pad Left's squadmate-call-in fix |
| Melee, Lethal, Tactical | A fix attempt for sniper-class Fire/ADS |
| Weapon switch (Y) | Plugin API (loader, hook/memory access) |
| Crouch/Prone (tap vs. hold) | Custom Options screen (temporary open-chord) |
| Pause menu open/close | |
| Auto-unstick (no more "click once at launch") | |

### Known gaps

These are honestly documented as not-yet-implemented, not hidden bugs:

- **Controller-glyph icons, on-screen hint prompts, and the custom cursor**
  don't draw on x64 yet — the underlying menu-focus/item-position tracking
  they depend on hasn't been ported. Everything else renders normally.
- **The visual-enhancement suite** (internal render scale, FSR sharpening,
  motion blur) isn't on x64 yet — blocked on two engine addresses that have
  resisted signature-scan-based discovery so far; next step is live tracing
  rather than more static analysis.
- **FXAA and a forced-MSAA option** were never actually built even on the
  old `-x86` line (only ever planned) — real future work, not a regression.

Full detail, investigation trails, and current status on every item:
[`re_notes/known_issues_x64.md`](re_notes/known_issues_x64.md).

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
  Multiplayer work has not started.

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
