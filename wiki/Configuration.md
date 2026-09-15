# Configuration

## Overview

All tunable values — sensitivity, ADS slowdown, hold thresholds, button/stick layout, and more — live in **`mw3ncp_config.ini`**, written next to `d3d9.dll` the first time the project runs. Every option is pre-filled with its default value and a comment explaining it, so the file is self-documenting from the moment it appears — nothing needs to be configured by hand to get started.

**Config changes hot-reload while the game is running** — save the ini and the mod picks up the change within about a second, no restart needed.

**Existing config files carry forward automatically across updates.** An internal `[Meta] ConfigVersion` marker (not a setting — don't edit it by hand) lets the mod detect an older file and migrate it: every setting you already tuned is kept, and any key that got renamed is carried over to its closest new equivalent instead of silently resetting.

> **A note on current x64 status**: the config file format and loading mechanism are shared, cross-platform code, so the keys below still exist and parse correctly. What varies is whether the FEATURE behind a given key is actually confirmed working on the current x64 rebuild yet — see the group notes below and [[Known Issues]] for the live, per-feature status.

## Configuration File

- **Location**: same folder as `d3d9.dll` (your MW3 install folder).
- **Format**: plain INI, sections in `[Brackets]`, `Key=Value` pairs, `;` for comments.

## Key Reference

**Confirmed working on the current x64 line**: `[Look]` (sensitivity, ADS slowdown, invert, acceleration ramp), `[Stance]`/`[Interact]` hold thresholds, `[Bindings]` (`ButtonLayout`/`StickLayout`/`FlipTriggers`).

| Section | Key | Default | What it does |
|---|---|---|---|
| `[Look]` | `SensitivityHorizontal` / `SensitivityVertical` | `250` / `250` | Look-stick turn rate, degrees/second at full deflection, split axes |
| `[Look]` | `AdsSlowdownStrength` | `1.75` | ADS zoom-aware look slowdown strength (`0` = off, `1` = fully proportional to zoom) |
| `[Look]` | `AdsSlowdownBaseline` | `0.65` | Multiplies the strength curve above across every zoom level equally |
| `[Look]` | `AdsCloseRangeSlowdownStrength` | `0.35` | Extra slowdown affecting only low-zoom weapons (pistols/iron sights) |
| `[Look]` | `InvertLook` | `0` | OG console "Invert Look" — flips vertical look |
| `[Look]` | `AccelerationRampMs` | `33` | Milliseconds for look turn-rate to ramp from 0 to full speed after the stick leaves neutral |
| `[Stance]` | `ProneHoldThresholdMs` | `400` | B: hold-vs-tap threshold for the stance ladder |
| `[Interact]` | `HoldThresholdMs` | `300` | X: how long Interact must be held before it fires (a quick tap reloads instead) |
| `[Bindings]` | `ButtonLayout` | `Default` | `Default` / `Tactical` / `Lefty` / `TacticalLefty` — see [[Controller Setup]] |
| `[Bindings]` | `StickLayout` | `Default` | `Default` / `Southpaw` / `Legacy` / `LegacySouthpaw` — see [[Controller Setup]] |
| `[Bindings]` | `FlipTriggers` | `0` | Independently swaps RT↔RB and LT↔LB |

**Not yet confirmed / not yet ported on the current x64 line** (keys still exist, feature behind them is unverified or inherited from the prior line's now-inactive code):

| Section | Key | Default | What it does |
|---|---|---|---|
| `[Survival]` | `ReadyUpHoldThresholdMs` | `740` | Y: hold-to-ready-up between Survival waves — not yet ported to this line |
| `[Movement]` | `AutoMantleEnabled` | `0` | Auto-mantle over obstacles while sprinting — not yet independently confirmed on this line |
| `[Vibration]` | `Enabled` | `1` | Real controller rumble on fire/damage — not yet independently confirmed on this line |
| `[Gyro]` | `Enabled` | `0` | Native DualSense raw-HID gyro-aim — the whole DualSense backend has not been ported to this line yet |
| `[Video]` | `InternalRenderScalePercent` | `0` | Render above/below native resolution — visual-enhancement suite not yet ported, see [[Known Issues]] |
| `[Video]` | `FsrSharpenEnabled` | `0` | AMD FSR 1.0 RCAS sharpening — not yet ported |
| `[Video]` | `MotionBlurEnabled` | `0` | Camera-only motion blur — not yet ported |
| `[Video]` | `ForceAnisotropicFiltering` / `ForceHighQualityShadows` / `ForceHighQualityLighting` | `0` | Native dvar overrides — not yet ported |
| `[Options]` | `UseCustomOptionsScreen` | `0` | Custom Options screen — wired into this line's input pipeline, opens via a temporary LB+RB chord while a native menu is active pending a deeper fix; see [[Known Issues]] |

**A note on `[AimAssist]`**: aim assist was **permanently removed from the codebase** on the prior line following VAC-risk research — there is no `[AimAssist]` section in current config files at all, not a disabled one, and this has carried forward unchanged.

## See Also

- [[Controller Setup]] — button/stick layout tables
- [[Known Issues]] — live, per-feature status
- [[Troubleshooting]] — if a setting doesn't seem to apply
