# Changelog

Condensed from `PATCHNOTES.md` in the source repo — see there for the full,
itemized detail behind each entry.

## v0.0.1-x64 — Alpha (2026-09-22)

The first release on the `-x64` line, rebuilding this project from scratch
against MW3's recompiled 64-bit binaries. **Survival's own controller
support is now Gameplay Complete (2026-09-22)** — every core gameplay
control is confirmed working live: analog movement/look, Jump, Interact,
Fire, true hold-to-aim ADS, Reload, Melee, Lethal, Tactical, weapon switch,
Crouch/Prone, Sprint, D-pad, pause menu open/close, Hold Breath, Predator
Missile launch and post-fire guidance (the last core control that had never
worked on either architecture), DPV/mortar/turret aiming, cutscene-skip
audio, and Campaign QTE button presses.

A full feature-parity audit against the prior 32-bit line's final build
found and closed several real gaps, most notably Sprint silently running an
old, deprecated mechanism and vibration never having been wired in at all.
Also now confirmed live: Survival ready-up (a full on-screen glyph+text
prompt, not just the mechanism), buy-station/use-prompt glyphs, native
D-pad+A/B menu/UI navigation, vibration/rumble, the visual-enhancement
suite's headline features (render scale, motion blur), and the ported
frame-pacing/wait-coalescing/IWD-read-cache performance techniques (all
default-on). DualSense gyro-aim and FSR sharpening remain build-verified
only.

**This release also folds in netcode security patching**, previously a
separate project, now merged in as this repo's own `security/` component —
real fixes for all four tracked, genuine, exploitable vulnerabilities in
the base game's own netcode, shipping built into this mod by default.

One known cosmetic bug remains: the pause-menu Back glyph still flickers
(Back itself still works). Not yet implemented / genuinely open: sentry/
turret-placement and Campaign QTE prompt *text* (the mechanism works, only
the on-screen text still renders native); the Custom Options screen's real
vanilla-setting tabs (deliberately deferred); AC-130 gun-type switching
(confirmed GSC/data-driven, no native hook point); SMAA (implemented,
parked off by default); and Back's scoreboard button (a real gap, but a
confirmed no-op in Campaign/Survival even on the prior line's own final
build — no scoreboard UI exists there).

Campaign has never gated this release (same as on the prior `-x86` line,
which also shipped it best-effort/partially untested); it ships as-is,
verified as it's touched.

The prior `-x86` line's full changelog history is preserved in this project's
GitHub repository under `legacy-x86-docs/nexus/changelog.bbcode.txt`.
