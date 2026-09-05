# Configuration

## Overview

All tunable values — sensitivity, ADS slowdown, hold thresholds, button/stick layout, and more — live in **`mw3ncp_config.ini`**, written next to `d3d9.dll` the first time the project runs. Every option is pre-filled with its default value and a comment explaining it, so the file is self-documenting from the moment it appears — nothing needs to be configured by hand to get started.

**Config changes hot-reload while the game is running (v0.2.5+)** — save the ini and the mod picks up the change within about a second, no restart needed, with a short on-screen "MW32011NCP Config Reloaded" confirmation.

**Existing config files carry forward automatically across updates (v0.2.5+).** An internal `[Meta] ConfigVersion` marker (not a setting — don't edit it by hand) lets the mod detect an older file and migrate it: every setting you already tuned is kept, and any key that got renamed is carried over to its closest new equivalent instead of silently resetting.

## Configuration File

- **Location**: same folder as `d3d9.dll` (your MW3 install folder).
- **Format**: plain INI, sections in `[Brackets]`, `Key=Value` pairs, `;` for comments.

## Full Key Reference

| Section | Key | Default | What it does |
|---|---|---|---|
| `[Look]` | `SensitivityHorizontal` | `250` | Look-stick yaw (left/right) turn rate, degrees/second at full deflection |
| `[Look]` | `SensitivityVertical` | `250` | Look-stick pitch (up/down) turn rate, degrees/second at full deflection — split from a single `Sensitivity` key in v0.2.5 |
| `[Look]` | `AdsSlowdownStrength` | `1.75` | ADS zoom-aware look slowdown strength (`0` = off, `1` = fully proportional to zoom, higher = more aggressive) |
| `[Look]` | `AdsSlowdownBaseline` | `0.65` | Multiplies the strength curve above across every zoom level equally — lower = more slowdown at all zoom levels |
| `[Look]` | `AdsCloseRangeSlowdownStrength` | `0.35` | Extra slowdown that only meaningfully affects low-zoom weapons (pistols/iron sights) without over-slowing scoped weapons. Not yet independently live-confirmed |
| `[Look]` | `InvertLook` | `0` | OG console "Invert Look" — flips vertical look |
| `[Look]` | `AccelerationRampMs` | `33` | Milliseconds for look turn-rate to ramp from 0 to full speed after the stick leaves neutral, matching real console MW2/Black Ops behavior. `0` = instant response |
| `[Stance]` | `ProneHoldThresholdMs` | `400` | B: hold-vs-tap threshold for the stance ladder |
| `[Interact]` | `HoldThresholdMs` | `300` | X: how long Interact must be held before it fires (a quick tap reloads instead) |
| `[Survival]` | `ReadyUpHoldThresholdMs` | `740` | Y: hold-to-ready-up threshold between Survival waves |
| `[Movement]` | `AutoMantleEnabled` | `0` | **Confirmed working (v0.3.4), still off by default by design** — opt in deliberately. Automatically mantles over obstacles while sprinting and pushing the stick fully forward, gated on the game's own real ledge-availability signal |
| `[Movement]` | `AutoMantleForwardConeDegrees` | `45` | Total cone width, centered on straight-forward, the left stick must fall within for auto-mantle to fire |
| `[Movement]` | `AutoMantleMinStickMagnitude` | `0.9` | Left stick deflection `[0,1]` must be at least this close to full for auto-mantle to consider firing |
| `[Bindings]` | `ButtonLayout` | `Default` | `Default` / `Tactical` / `Lefty` / `TacticalLefty` — see [[Controller Setup]] |
| `[Bindings]` | `StickLayout` | `Default` | `Default` / `Southpaw` / `Legacy` / `LegacySouthpaw` — see [[Controller Setup]] |
| `[Bindings]` | `FlipTriggers` | `0` | Independently swaps RT↔RB and LT↔LB, combining with whichever `ButtonLayout` is active |
| `[Vibration]` | `Enabled` | `1` | Real controller rumble — fire and damage vibration are both physically confirmed working (rebuilt on safer hook targets after an earlier version crashed at startup) |
| `[Vibration]` | `FireIntensity` | `0.55` | Motor strength `[0,1]` on each real shot fired |
| `[Vibration]` | `FireDurationMs` | `90` | Milliseconds a fire pulse takes to decay to zero |
| `[Vibration]` | `DamagePerPoint` | `0.05` | Motor strength added per point of real damage taken |
| `[Vibration]` | `DamageMaxIntensity` | `1.0` | Hard cap on damage-rumble strength regardless of damage amount |
| `[Vibration]` | `DamageDurationMs` | `200` | Milliseconds a damage pulse takes to decay to zero |
| `[Gyro]` | `Enabled` | `0` | Real native gyro-aim sourced from a raw-HID DualSense (not routed through Steam Input), additive on top of right-stick look. Off by default — confirmed working on real hardware by the first live tester (2026-08-28) |
| `[Gyro]` | `Sensitivity` | `0.25` | Raw gyro-rate multiplier — not a claimed degrees/second conversion. Lowered from an initial `1.0` per direct live-tester feedback ("far too high"), a directional correction, not a fully calibrated value |
| `[Gyro]` | `InvertPitch` / `InvertYaw` | `0` | Correct the gyro axis-to-look mapping if it turns out backwards on your hardware (a best-effort guess, not verified live) |
| `[Gyro]` | `OnlyWhileAds` | `0` | **New in v0.3.5.** When on, the gyro-look delta only applies while ADS is held (a common gyro-aim convention, e.g. Splatoon/Steam Input's own default). Off preserves the original always-on gyro behavior |
| `[Video]` | `InternalRenderScalePercent` | `0` | **New in v0.3.5, PREVIEW/WIP.** Renders the actual 3D scene above or below native display resolution (`100` = native, `250` = 2.5x supersampled, etc.; `0` = feature off). Real GPU/VRAM cost, confirmed live to scale with the setting. Requires a game restart to take effect, no live hot-reload for this one setting |
| `[Video]` | `FsrSharpenEnabled` / `FsrSharpenStrength` | `0` / `0.3` | **New in v0.3.5, PREVIEW/WIP.** AMD FSR 1.0 RCAS full-screen sharpening pass, a direct port of the real reference math. Strength is `[0.0, 1.0]`. Needs `ps_3_0` hardware, refuses gracefully otherwise |
| `[Video]` | `MotionBlurEnabled` / `MotionBlurStrength` / `MotionBlurCenterFalloff` | `0` / `1.0` / `1.0` | **New in v0.3.5, EXPERIMENTAL.** Camera-only directional motion blur driven by real per-frame look data (controller stick/gyro only, not mouse). `CenterFalloff` `[0.0, 1.0]` keeps the screen center sharp while the periphery blurs more; `0.0` = uniform blur |
| `[Video]` | `ForceAnisotropicFiltering` | `0` | **New in v0.3.5.** Writes the real native `r_texFilterAnisoMax`/`r_texFilterAnisoMin` dvars to `16` (maximum) |
| `[Video]` | `ForceHighQualityShadows` / `ForceHighQualityLighting` | `0` / `0` | **New in v0.3.5.** Write real native shadow/lighting quality dvars (`sm_fastSunShadow`, `r_cacheModelLighting`/`r_cacheSModelLighting`) to their higher-quality values. Real, uncharacterized performance cost — see [[Known Issues]] |
| `[Options]` | `UseCustomOptionsScreen` | `0` | **PREVIEW/WIP.** Enables a fully custom-drawn replacement Options screen (every real vanilla tab plus real rebind capture) that draws over the real Options screen. Compiles clean but genuinely has not been played — no in-game toggle exists on purpose. See [[Known Issues]] |
| `[Experimental]` | `FireNotifyQueueKick` | `1` | Internal fix that makes Predator Missile's launch reach its native listener — leave on unless told otherwise |
| `[Experimental]` | `BindResolverHookLogging` / `HudFontIdLogging` / `HudGlyphPositionLogging` | `0` | Diagnostic logging for the controller-glyph pipeline — heavy, meant for brief targeted use only, not for normal play |
| `[Experimental]` | `GlyphPositionEditMode` | `0` | Dev tool: an in-game click-and-drag F2/F3 overlay for calibrating glyph icon positions on a specific screen |
| `[Experimental]` | `CaptureRuntimeMenuAssets` | `0` | Dev tool: dumps real menu material textures to disk while a menu is open, for the `.menu` renderer research project |
| `[Experimental]` | `FrametimeBenchmarkLogging` | `0` | Writes a real per-frame `frametime_benchmark.csv` (own hook costs broken out) — how the v0.3.3 stutter causes were actually found, not guessed |

**A note on `[AimAssist]`**: aim assist was **permanently removed from the codebase in v0.2.2**, following VAC-risk research — there is no `[AimAssist]` section in current config files at all, not a disabled one. If you're reading an old guide that mentions `[AimAssist] Enabled`, it refers to a pre-v0.2.2 build that is no longer distributed.

## Recommended Settings

The defaults above have all been individually live-tested and tuned against real hardware to feel close to real console CoD — treat them as a solid starting point, not something you necessarily need to change. If you do tune sensitivity, start with `SensitivityHorizontal`/`SensitivityVertical` alone before touching the ADS-slowdown curve.

## See Also

- [[Controller Setup]] — button/stick layout tables
- [[Known Issues]] — status of the preview/WIP features referenced above
- [[Troubleshooting]] — if a setting doesn't seem to apply
