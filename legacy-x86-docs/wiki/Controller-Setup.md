# Controller Setup

## Supported Controllers

This project uses **XInput** as its primary backend, so any XInput-compatible controller works out of the box — most commonly Xbox controllers (360/One/Series). DualShock/DualSense controllers also work through an XInput-compatible driver/wrapper (Steam Input configured to emulate an Xbox controller, DS4Windows, etc).

**PREVIEW/WIP: native DualSense support (v0.3.2+).** A separate raw-HID backend now talks to a real DualSense directly, bypassing Steam Input entirely, and adds real gyro-aim (`[Gyro]` in `mw3ncp_config.ini`, off by default, confirmed working on real hardware). This exists specifically because Steam Input's own translation layer was found unreliable for this project's glyph/input detection. **Current status**: **Bluetooth DualSense stick input is fixed and confirmed live by the reporting tester (v0.3.4)** — the root cause was a real signed-16-bit integer overflow at full-forward stick deflection. **USB DualSense has still never been independently confirmed working against real hardware** by a separate tester — the fix lives in code shared with the USB path, so it almost certainly applies there too, but that's not the same as a confirmation. See [[Known Issues]] before relying on USB specifically for gameplay.

No plug-and-play detection UI exists — just plug in your controller before or during play. **The active controller/backend is locked once at boot (v0.3.3+)** for performance reasons — reconnecting the same device mid-session works fine, but switching to a different controller slot or between XInput/DualSense needs a relaunch to be picked up.

## Current Control Map (Xbox layout)

| Input | Action | Status |
|---|---|---|
| Left stick | Move (analog forward/back/strafe) | ✅ |
| Right stick | Look — independent sensitivity, own acceleration ramp | ✅ |
| Right trigger (RT) | Fire | ✅ |
| Left trigger (LT) | Aim Down Sights (true hold-to-aim) | ✅ |
| Left stick click (L3) | Sprint. While ADS'd on a sniper, drives **Hold Breath** instead — steadies aim while held, accuracy drops once breath runs out | ✅ |
| A | Jump | ✅ |
| B | Crouch/Prone (tap = crouch, hold = prone, full 3-state ladder below); also backs out of any open menu | ✅ |
| X | Interact **and** Reload — a quick tap reloads, a hold (300ms default) interacts, same as console | ✅ |
| Right stick click (R3) | Melee | ✅ |
| Left bumper (LB) | Tactical (smoke) | ✅ |
| Right bumper (RB) | Lethal (frag) | ✅ |
| Y | Weapon switch; hold ~740ms in Survival to ready up between waves | ✅ |
| Start | Opens **and** closes the pause menu | ✅ |
| Back | Scoreboard/objectives | 🟡 Implemented, live-tested — zero visible effect, real cause undiagnosed |
| D-pad | Killstreaks/attachments/loadout-dependent actionslots; also full menu item navigation with A to select | ✅ |
| All buttons (glyph icons) | Real controller-glyph icons in in-game hints and menu corner hints, instead of keyboard key names | ✅ Confirmed live (see [[Known Issues]] for the remaining highlighted-item-glyph screen gaps) |
| Fire / taking damage (vibration) | Controller rumble on weapon fire and taking damage | ✅ Confirmed live — see [[Configuration]]'s `[Vibration]` section |

**Stance ladder (B):**

| Current stance | Tap | Hold |
|---|---|---|
| Standing | → Crouched | → Prone |
| Crouched | → Standing | → Prone |
| Prone | → Crouched | → Standing |

"Hold" fires the instant the press crosses the threshold; "tap" only fires on release, and only if the hold threshold was never reached.

## Button Layout Presets

Reconstructed from the unchanged CoD4→MW2→MW3 console control scheme, and confirmed correct against real hardware (including `TacticalLefty`). Set via `ButtonLayout` in `mw3ncp_config.ini` — see [[Configuration]].

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

The shipped defaults (`SensitivityHorizontal`/`SensitivityVertical=250`, `AdsSlowdownStrength=1.75`, `AccelerationRampMs=33`) have all been individually live-tested and tuned against real hardware to feel close to real console CoD — a good starting point rather than something to necessarily change. See [[Configuration]] for what each value does if you want to adjust to taste.

## Input Behaviour

- **Movement/Look**: driven directly through the game's real per-frame input path, not mouse/keyboard emulation — see [[Technical Documentation]] for why that matters for feel/latency.
- **Buttons**: mapped to the game's real internal button-state calls (`kbutton_t` down/up), same mechanism a real keyboard press uses.
- **Triggers**: read as analog (0–255) for deadzone purposes, but Fire/ADS themselves are simple held/not-held digital binds, matching how the base game itself treats them.
