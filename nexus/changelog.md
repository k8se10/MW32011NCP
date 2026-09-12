# Changelog

Condensed from `PATCHNOTES.md` in the source repo — see there for the full,
itemized detail behind each entry.

## v0.0.1-x64 — Unreleased

The first release on the `-x64` line, rebuilding this project from scratch
against MW3's recompiled 64-bit binaries. Every core gameplay control is
implemented and build-verified, most confirmed live: analog movement/look,
Jump, Interact, Fire, true hold-to-aim ADS, Reload, Melee, Lethal, Tactical,
weapon switch, the full Crouch/Prone stance ladder, pause menu open/close,
and auto-unstick.

A full feature-parity audit against the prior 32-bit line's final build
found and closed several real gaps, most notably Sprint silently running an
old, deprecated mechanism and vibration never having been wired in at all.
Also newly build-verified this pass: Survival ready-up, Hold Breath, native
D-pad+A/B menu/UI navigation, vibration/rumble, the visual-enhancement suite
(render scale, FSR sharpening, motion blur), and DualSense gyro-aim
(preview/WIP). None of these are live-tested yet.

**This release also folds in netcode security patching**, previously a
separate project, now merged in as this repo's own `security/` component —
real fixes for genuine, exploitable vulnerabilities in the base game's own
netcode, shipping built into this mod by default.

Not yet implemented on this line: gameplay controller-glyph icons, on-screen
hint prompts, and the custom cursor (blocked on one remaining render hook);
Auto-Mantle (same blocker); Back's scoreboard button (a real gap, but a
confirmed no-op in Campaign/Survival even on the prior line's own final
build — no scoreboard UI exists there).

**This release has not shipped.**

The prior `-x86` line's full changelog history is preserved in this project's
GitHub repository under `legacy-x86-docs/nexus/changelog.bbcode.txt`.
