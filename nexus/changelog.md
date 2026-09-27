# Changelog

Condensed from `PATCHNOTES.md` in the source repo — see there for the full,
itemized detail behind each entry.

## v0.0.3-x64 — Alpha (2026-09-27)

The biggest release in this project's history.

**Native Vulkan rendering and NVIDIA DLSS/DLAA.** Campaign/Survival now
render through Vulkan by default, using this project's own bundled DXVK
fork (built into `d3d9.dll`, nothing extra to install; set
`GraphicsApi=LegacyD3D9` for the old path, and Multiplayer always uses
D3D9). On top of it, DLSS/DLAA via NVIDIA Streamline is working end to end
as an opt-in for RTX GPUs (`StreamlineEnabled=1`), including above 100%
render scale, where it switches to DLAA automatically for that session.
Known gaps: bloom and depth of field aren't applied to the DLSS image above
100% render scale yet, and moving objects don't have their own motion
vectors yet.

**Major performance fixes, all on by default.** The biggest is "the 67
bug": a blur/downsample loop whose cost grew with render scale, and the
cause of the long-standing "the pause menu runs worse than gameplay"
problem. Fixing it took Dome at 250% render scale from 23 fps to a 76 fps
average, and the pause menu now runs faster than gameplay. A lost
shadow-quality check, three redundant per-frame render-view setups and a
repeated console-font reload (all introduced by Activision's own 64-bit
recompile) are skipped too, each with a measured gain.

**World audio no longer sounds like it's in a small room.** The reverb
bug where everything except your own gun echoed like an enclosed space is
fixed and on by default (`ReverbWetScale=0.5`; `1.0` restores the old mix).

**Multiplayer:** controller menu navigation (D-pad + A/B, B back) now
works in MP's menus, and the performance fixes above also run there.
In-game controller movement/aiming in MP is not supported yet.

Also: the forced anisotropic filtering/shadow/lighting options and the
Custom Options screen's vanilla settings finally have a real write path on
x64 (not yet live-tested); the pause-menu Back glyph flicker has a fix
(not yet live-tested); motion blur works again for controller as well as
mouse; and new built-in diagnostics (F9/F10/F11) make performance reports
far easier to investigate.

**A one-week development break starts on release day (2026-09-27 to
2026-10-04)** so this release can settle under real play. v0.0.3-x64 is
the LTS candidate for this line: if it holds up for 4 weeks with no major
regression, it becomes the long-term-support release (see `LTS_POLICY.md`
on GitHub). Please keep reporting bugs during the break.

## v0.0.2-x64 — Alpha (2026-09-23)

**Internal render scale now works in Multiplayer** — the first
visual-enhancement feature to ship there, on by default, no VAC-risk
opt-in needed (it never touches gameplay input or entity memory).
⚠ **Important**: this release also confirms the render-scale stutter
investigated below is content-dependent, not one fixed safe percentage
— the exact same 200% that's completely clean throughout Campaign/Survival
causes constant lag in Multiplayer. See the mod's own README/PATCHNOTES for
the full detail; the practical takeaway is to test any increase above 100%
deliberately in the mode you actually play, rather than assuming a number
that's safe in one mode is safe everywhere.

Fixed a critical Multiplayer launch crash (a hardcoded address, valid only
in Campaign/Survival's own binary, that was being reached for the first
time under Multiplayer) and a severe, sustained render-scale-gated stutter
was investigated in real depth — root-caused to a genuine native engine
stability limit at large render-target sizes, not a bug in this mod, with
a real on-screen warning now firing at the tested 200% boundary. Two
genuine native engine bugs were also found and fixed along the way (a
hardcoded 3GB system-memory detection cap, and a mod-side crash from this
session's own new diagnostics).

**Motion blur is no longer controller-only** — it now reacts to real
per-tick look movement from any input device, including mouse/keyboard.

Real reverse-engineering groundwork: a first map of the native 3D
renderer's own architecture (prep for a future renderer replacement),
including the complete render-target table (shadow maps included, closing
a gap open since 2026-08-29) and a genuine, previously-undocumented native
SSAO implementation sitting dormant in the game's own binary. Also caught
and corrected a real documentation bug: the forced anisotropic
filtering/shadow/lighting-quality toggles have been silent no-ops on x64
since the port — not crashing, just doing nothing — despite being
previously listed as working.

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
