# MW32011NCP — Native Community Patches for MW3 (2011)

[![Support on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/officialk8)

A native, from-scratch reverse-engineering platform for **Call of Duty:
Modern Warfare 3 (2011, IW5 engine)** — not a single mod, but a patch layer
with four real, distinct pieces: real controller support (the flagship,
most mature piece), a growing suite of visual/performance enhancements,
native netcode security patches for the base game's own real vulnerabilities,
and Multiplayer support (in active development). None of it works by faking
keyboard/mouse input underneath a mapper — every piece hooks the game's own
real internal engine functions directly.

## ⚠ Alpha-stage software, no release currently available

This project hooks directly into a live game process. Expect bugs, rough
edges, and unfinished features. It is being rebuilt from scratch against
MW3's own recompiled 64-bit binaries (the game's first-ever binary update, a
hard architectural break for every tool built on the original 32-bit
executable) — no download is offered yet. **Survival's own controller
support is now Gameplay Complete** (2026-09-22) — every core control,
including Predator Missile guidance, is live-confirmed; a release is close.
Campaign has never gated release (same as on the prior 32-bit line, which
also shipped it best-effort) and ships as-is. Full live status: see this
page's Source link (GitHub) for `re_notes/known_issues_x64.md` and
`re_notes/x64_feature_parity_audit.md`.

Not affiliated with, endorsed by, or sponsored by Activision, Infinity Ward,
or any of their affiliates.

## ⚠ Unpatched MW3 (2011) netcode vulnerabilities — this mod fixes them

Real, network-reachable vulnerabilities exist in MW3's own base-game
netcode (not this mod) — re-confirmed present in the game's most recent
update, affecting Multiplayer and Spec-Ops/Survival co-op. **This mod now
closes all four tracked findings.** A vanilla, unmodified Steam install
remains exactly as vulnerable as before — a real reason to run this mod
even without interest in its controller/visual features. Exact technical
detail is withheld while these remain unpatched at the source; a full
report has been submitted to Activision through their official
security-disclosure channel. See the Source repo's `security/` component
for current per-finding status.

## What this is

MW3 (2011) shipped on PC with **zero working controller input path** — no
`xinput`/`dinput8` import anywhere in the binary, no hidden setting to
unlock. The controller side of this project is a from-scratch native
controller *and enhancement* project, not a keyboard/mouse-emulation mapper:
analog movement, look, and most buttons drive the game's own real internal
engine calls directly — the exact same fields and states a keyboard/mouse
player already uses, just fed from a controller instead.

Every other "controller support" option for this game works by faking
keyboard/mouse input underneath a mapper tool — poll, convert to a key/mouse
event, OS input queue, then the game's own input processing. This project
writes straight into the engine's real per-frame input path from inside the
game's own process, on the game's own frame tick — no OS-level input event,
no intermediate queue. That's the core advantage: input feel that matches
native console analog input, not an approximation of it.

A small number of narrow, deliberate exceptions to that rule exist
(documented, not hidden) for inputs where an exhaustive search found no
locatable native trigger — those synthesize a real keypress instead of
driving the engine directly. Everything else — including all of movement,
look, and combat — drives real engine state.

**The netcode security component** (formerly a separate project, merged in
2026-09-12) uses the same technique against a different target: it hooks
the game's own real network-message-parsing functions to fix genuine,
exploitable buffer-overflow vulnerabilities in MW3's own netcode. It ships
built into this mod by default, and also builds as a standalone DLL for
anyone who wants the protection without the rest of this mod.

## Feature status

**Confirmed working live**: analog movement and look, Fire, true
hold-to-aim ADS, Reload, Melee, Lethal, Tactical, Jump, Interact,
Crouch/Prone, Sprint, weapon switch, D-pad (actionslot and squadmate
call-in), pause menu open/close, Hold Breath, Predator Missile launch and
post-fire guidance, DPV/Goalpost mortar/Goalpost M2 turret aiming,
cutscene-skip audio, Campaign QTE button presses, Survival ready-up (full
on-screen glyph+text prompt, not just the mechanism), buy-station/use-prompt
glyphs, native controller menu/UI navigation, motion blur, internal render
scale, and the ported frame-pacing/wait-coalescing/IWD-read-cache
performance techniques (all default-on). The real fix for the long-standing
"needs a click/input at launch" bug is also shipped and live-confirmed.

**One known cosmetic bug**: the pause-menu Back glyph still flickers (Back
itself still works).

**Build-verified, awaiting live confirmation**: the plugin API, DualSense
gyro-aim (preview/WIP), FSR sharpening, and K+M safe mode (a toggle that
disables all controller/mod-side input while keeping visual features).

**Honestly not yet implemented / genuinely open**: sentry/turret-placement
and Campaign QTE prompt *text* still render native (blocked on an
unresolved native font offset — the mechanism works, only the on-screen
text isn't replaced); the Custom Options screen's real vanilla-setting tabs
(deliberately deferred — the INI config already covers everything this mod
needs); AC-130 gun-type switching (confirmed GSC/data-driven, no native
hook point to use); SMAA edge smoothing (implemented, parked off by
default — a shared internal rendering path looked worse than off even
without the SMAA math); and Back's `+scores` scoreboard (a real, cheap
gap — but a confirmed no-op even on the prior line's own final build, since
Campaign/Survival has no scoreboard UI at all; real value arrives once
Multiplayer ships).

**Multiplayer** (`iw5mp.exe`) is a separate binary under active, independent
reverse engineering — not part of the release above, expected as a close
fast-follow once Campaign/Survival reaches parity, shipping opt-in only
given Valve Anti-Cheat's confirmed presence on that binary.

## ⚠ Do not use this with Plutonium multiplayer

Plutonium's own anti-cheat is confirmed to ban DLL injection and memory
access — a 7-day ban on first offense, permanent after. This project's
entire architecture (a proxy `d3d9.dll`, function hooking) is exactly what
that system is built to catch, regardless of this being input-only rather
than a gameplay cheat. This is a real, confirmed risk, not theoretical.
Supported: retail Steam Campaign/Survival only.

## Installation

No file is currently available — see the notice at the top of this page.
Once a release ships: copy `d3d9.dll` into your MW3 install folder (the same
folder as `iw5sp.exe`), launch the game normally, and check
`proxy_d3d9.log` in that folder if anything looks wrong. Uninstalling is
just deleting `d3d9.dll` — the base game files are never modified.

## Credits & License

This project bundles and links [MinHook](https://github.com/TsudaKageyu/minhook)
(Copyright © 2009–2017 Tsuda Kageyu, BSD 2-Clause-style license) for all API
hooking, and the Hacker Disassembler Engine (HDE) 32/64 C it bundles.

Released under a custom, permissive license: free to use, modify, and fork.
The one restriction — neither this project nor any fork/derivative may ever
be sold or charged for; it must stay free for everyone. Because of that
restriction this doesn't meet the OSI's formal "open source" definition, but
the source is fully open. Does not grant any rights to Call of Duty: Modern
Warfare 3 itself — you need your own legitimate copy of the game. The
netcode security component has its own license file (same terms, one extra
responsible-disclosure clause) — see the source repo's `security/LICENSE`.

## Support this project

Built and maintained entirely in spare time, at no cost to players — no ads,
no paywalled features, nothing ever sold (the license itself forbids it). If
it's been useful to you, a [Ko-fi donation](https://ko-fi.com/officialk8) is
completely optional but genuinely appreciated.

[![Support on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/officialk8)

## Links

Full source, reverse-engineering write-ups, and the complete patch history
live on GitHub — see this page's Source/Files links. Bug reports and
contributions welcome there.
