# Controller Setup

## Supported Controllers

This project uses **XInput** as its primary backend, so any XInput-compatible controller works out of the box — most commonly Xbox controllers (360/One/Series). DualShock/DualSense controllers also work through an XInput-compatible driver/wrapper (Steam Input configured to emulate an Xbox controller, DS4Windows, etc). **Native DualSense support (raw-HID, bypassing Steam Input, with gyro-aim) existed on the prior 32-bit line but has not yet been ported to the current 64-bit rebuild** — see [[Known Issues]] for current status.

No plug-and-play detection UI exists — just plug in your controller before or during play.

## Current Control Map (Xbox layout)

Status reflects direct playtest confirmation on the current x64 rebuild — see [[Known Issues]] for the full, live-updated list.

| Input | Action | Status |
|---|---|---|
| Left stick | Move (analog forward/back/strafe) | ✅ Confirmed live |
| Right stick | Look — independent sensitivity, own acceleration ramp | ✅ Confirmed live |
| Right trigger (RT) | Fire | ✅ Confirmed live — a fix attempt for sniper-class weapons specifically is shipped, not yet independently confirmed |
| Left trigger (LT) | Aim Down Sights (true hold-to-aim) | ✅ Confirmed live — same sniper-class caveat as Fire |
| Left stick click (L3) | Sprint | ✅ Confirmed live. Hold Breath (steadying aim on a sniper) has not been ported to this line yet |
| A | Jump | ✅ Confirmed live. Jump auto-stand (standing up first if crouched/prone) is build-verified, not yet independently confirmed |
| B | Crouch/Prone (tap = crouch, hold = prone, full 3-state ladder below) | ✅ Confirmed live |
| X | Interact **and** Reload — a quick tap reloads, a hold interacts, same as console | ✅ Confirmed live |
| Right stick click (R3) | Melee | ✅ Confirmed live |
| Left bumper (LB) | Tactical (smoke) | ✅ Confirmed live |
| Right bumper (RB) | Lethal (frag) | ✅ Confirmed live |
| Y | Weapon switch | ✅ Confirmed live. Survival's hold-to-ready-up has not been ported to this line yet |
| Start | Opens **and** closes the pause menu | ✅ Confirmed live |
| D-pad | Killstreaks/attachments/loadout-dependent actionslots | 🟡 Build-verified and deployed, awaiting independent live confirmation. D-pad Left carries a leading fix for a "sometimes different keys used" report |
| All buttons (glyph icons) | Real controller-glyph icons in in-game hints and menu corner hints | ⬜ Not yet implemented on this line — see [[Known Issues]] |

**Stance ladder (B):**

| Current stance | Tap | Hold |
|---|---|---|
| Standing | → Crouched | → Prone |
| Crouched | → Standing | → Prone |
| Prone | → Crouched | → Standing |

"Hold" fires the instant the press crosses the threshold; "tap" only fires on release, and only if the hold threshold was never reached.

## Button Layout Presets

Reconstructed from the unchanged CoD4→MW2→MW3 console control scheme. Set via `ButtonLayout` in `mw3ncp_config.ini` — see [[Configuration]]. This is config-application logic sitting on top of the confirmed control map above, not itself architecture-specific.

| Action | Default | Tactical | Lefty | TacticalLefty |
|---|---|---|---|---|
| Fire | RT | RT | LT | LT |
| ADS | LT | LT | RT | RT |
| Lethal | RB | RB | LB | LB |
| Tactical | LB | LB | RB | RB |
| Crouch/Prone | B | RS | B | LS |
| Sprint | LS | LS | RS | RS |
| Melee | RS | B | LS | B |

A separate `FlipTriggers` option independently swaps RT↔RB and LT↔LB, on top of whichever layout is active.

## Stick Layout Presets

Set via `StickLayout` in `mw3ncp_config.ini`.

| Layout | Left stick | Right stick |
|---|---|---|
| Default | Move | Look |
| Southpaw | Look | Move |
| Legacy | Forward/back + turn (horizontal) | Look up/down + strafe (horizontal) |
| LegacySouthpaw | Look up/down + strafe (horizontal) | Forward/back + turn (horizontal) |

## Recommended Settings

The shipped defaults (`SensitivityHorizontal`/`SensitivityVertical=250`, `AdsSlowdownStrength=1.75`, `AccelerationRampMs=33`) were individually live-tested and tuned against real hardware on the prior line to feel close to real console CoD — a good starting point rather than something to necessarily change. See [[Configuration]] for what each value does if you want to adjust to taste.

## Input Behaviour

- **Movement/Look**: driven directly through the game's real per-frame input path, not mouse/keyboard emulation — see [[Technical Documentation]] for why that matters for feel/latency.
- **Buttons**: mapped to the game's real internal button-state calls, same mechanism a real keyboard press uses.
- **Triggers**: read as analog (0–255) for deadzone purposes, but Fire/ADS themselves are simple held/not-held digital binds, matching how the base game itself treats them.
