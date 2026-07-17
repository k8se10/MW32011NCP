# MW3 Native Controller Support (Campaign & Survival)

**Status: PRE-ALPHA — actively in development (v0.1.3, 2026-07-17).** Analog movement, look,
and most buttons are confirmed working live against `iw5sp.exe` (Campaign/Survival),
including a real sprint stamina/cooldown model, Start's full pause/unpause, weapon
switching, D-pad killstreak/attachment slots, Survival's between-wave ready-up, and now
real D-pad/A menu navigation (main menu, pause menu, options screens). Back
is still unassigned (deprioritized — not gameplay-defining), killstreaks need
per-killstreak work, sprint's mission/perk overrides (unlimited-sprint missions, Extreme
Conditioning) aren't accounted for yet, slider-type settings can be navigated to but not
adjusted by controller yet, button-glyph UI prompts aren't implemented, and
Multiplayer (`iw5mp.exe`) hasn't been started. Not feature-complete, not fully tested
end-to-end.

A from-scratch native controller mod for Call of Duty: Modern Warfare 3 (2011, IW5
engine) — analog movement, look, and buttons driven directly through the game's own
engine calls, not keyboard/mouse emulation. See `re_notes/` for the full reverse-
engineering writeup this mod is built on, and `PATCHNOTES.md` for what changed in
each release.

## Project stages

This project uses the standard pre-alpha → alpha → beta → 1.0 progression, but
"pre-alpha" here means something more specific than "barely started" — a lot of
this mod's core systems (analog movement/look, real engine-state-driven Sprint
and Crouch/Prone, real pause menu, real D-pad/A menu navigation) are already
confirmed working live, on par with the functional bar many shipped mods or
public betas ship at. The label reflects how much of the mod's *planned* scope
is still open, not how rough what already works is.

| Stage | Version range | What it means here |
|---|---|---|
| **Pre-alpha** *(current)* | `0.1.0` – `0.1.5` | Core systems land one at a time — movement/look/combat, stance/sprint, pause menu, and now menu navigation are done; aim assist, vibration, killstreaks, and the controller options menu are still being built out. Expect real gaps, not just polish issues. |
| **Alpha** | `0.1.5` – `0.4.0` | The remaining major systems get built and land: full menu/UI navigation (slider adjustment, button glyphs, a real in-game options screen), aim assist taken from "math confirmed, classification unverified" to actually working, killstreaks fully scoped, vibration, and Extreme Conditioning's real override. Multiplayer groundwork may start here, pending the anti-cheat question being resolved first. |
| **Beta** | `0.4.0` – `1.0.0` | Should be practically feature-complete — remaining work is closing gaps, fixing what live testing surfaces, and extending reach (other MW3 clients, Multiplayer if the anti-cheat question resolves favorably) rather than building brand-new core systems from scratch. |
| **1.0 (final)** | `1.0.0`+ | Feature-complete against this project's full scope, stable, and treated as a real release rather than an actively-shifting work in progress. |

## Feature list

### Movement & look
- **Analog movement** (move-stick, left by default) — real `usercmd_t.forwardmove`/
  `.rightmove` bytes, additive on top of any keyboard input already present.
- **Analog look** (look-stick, right by default) — writes the raw pitch/yaw
  angle-delta accumulators directly, bypassing the mouse pipeline entirely (own
  sensitivity, no mouse accel/filter inherited). Sensitivity, invert-Y, and an
  ADS-zoom-aware slowdown curve are all configurable — see **Configuration** below.
- **ADS look-slowdown** — look rate scales down while aiming, proportional to the
  weapon's actual live zoom level (`effectiveFov/hipfireFov`, read-only — your real
  field of view is never touched), so magnified optics don't feel absurdly twitchy.
  A separate baseline multiplier applies some slowdown even on low-zoom optics (iron
  sights/red dots), where the zoom ratio alone stays too close to 1.0 to produce a
  noticeable effect on its own. Both configurable, mathematically safe at any value
  (a power curve, not a linear blend — the linear version could invert look direction
  at high strength on deep zooms; fixed in v0.1.1).
- **Aim assist (rotational friction + magnetism) — EXPERIMENTAL, DISABLED BY DEFAULT,
  NOT FUNCTIONAL YET.** A from-scratch implementation (the game's native aim-assist
  system turned out to be shared math bots use to aim *at* the player, not a
  player-facing feature — MW3 PC genuinely has no mouse aim-assist). The underlying
  math (angle error, friction curve, magnetism) is confirmed correct via live
  diagnostic logging, but the target-validity filter is not: it currently uses a
  movement heuristic that oscillates between multiple simultaneously-moving things
  (a real enemy, a settling ragdoll, a thrown grenade), producing genuinely broken
  targeting in practice, not just an unpolished feel. A real fix (native type/health-
  based classification, no movement heuristic needed) is believed found via static
  analysis but not yet live-verified — see `re_notes/known_issues.md` issue #15.
  **Ships with `Enabled=0` and must stay that way for any public/release build**
  until that's confirmed live. Do not enable this for anyone other than active
  development/testing.

### Combat & interaction
- **Fire** (RT), **Tactical**/**Lethal** (LB/RB), **Jump** (A).
- **ADS** (LT) — true hold-to-aim via the real `+toggleads_throw` `kbutton_t`
  KeyDown/KeyUp calls, not a toggle or a raw bit.
- **Melee** (R3) — real melee kbutton, confirmed "100% knife" live.
- **Reload** (X) — real, context-sensitive `kbutton_t`, found via memdiff; fires
  instantly on press, unaffected by Interact's hold requirement below.
- **Interact** (X, same physical button as Reload) — **requires a hold** (300ms by
  default, configurable), not an instant tap: a quick tap reloads the weapon instead,
  same as console. Reload is a separate real kbutton on the same physical button that
  fires on every press regardless of hold duration — so a quick tap doesn't fire
  Interact, but does trigger a real reload.
- **Weapon switch** (Y) — real `weapnext` dispatch via the engine's own bind-index
  jump table, found by live-reading the raw-keycode dispatch table for the actual
  bound keys.
- **D-pad** (all 4 directions) — real `+actionslot 1-4` dispatch, data-driven by
  loadout (killstreaks/attachments/NVG-style toggles, whatever's actually equipped).
  Up/Right/Down call the real dispatch function directly; Left is the second of this
  mod's two deliberate, documented exceptions to native-only input (see below) — it
  synthesizes the real bound key instead, since Survival's AI-squadmate call-in on
  that slot needed it (turret call-ins on the same slot are unaffected).

### Stance & Sprint (real engine state, not our own tracked copy)
- **Crouch/Prone stance ladder** (B) — a real 3-state ladder driving the game's own
  native togglecrouch/toggleprone toggle directly (not a raw bit force), so it can
  never desync from the engine's own state:

  | Current stance | B tapped | B held |
  |---|---|---|
  | Standing | → Crouched | → Prone |
  | Crouched | → Standing | → Prone |
  | Prone | → Crouched | → Standing |

  "Hold" fires the instant the press crosses the threshold (no need to release
  first); "tap" only fires on release, and only if the hold threshold was never
  reached during that press. Threshold is configurable (400ms default).
- **Sprint** (L3) — real `pm_flags` bit, forced via a Pmove-entry hook; auto-stands
  from crouch/prone first if needed, matching console. Includes a real
  **stamina/cooldown state machine** (our own timer layer, since forcing the bit
  natively bypasses the game's own limiter entirely):

  | State | Behavior | Transitions to |
  |---|---|---|
  | Ready | Full stamina, sprint available | Sprinting (on L3 held) |
  | Sprinting | Stamina drains continuously | Winded (stamina hits 0), Regenerating (L3 released early) |
  | Winded | Sprint fully blocked for a fixed cooldown, independent of stamina float | Ready (cooldown timer expires, full refill) |
  | Regenerating | Stamina refills while not sprinting | Ready (full) or Sprinting (L3 held again) |

  4 seconds of continuous sprint to fully deplete, a real 2-second cooldown once
  winded (both configurable) — not just a cosmetic meter, sprint is genuinely
  blocked while on cooldown, decoupled from the stamina float itself (an earlier
  version had a regen-flicker bug where continuous regen cleared the cooldown lock
  almost instantly; fixed with a dedicated cooldown timer). Automatically bypassed
  (genuinely unlimited sprint) when the real `player_sprintUnlimited` dvar is
  live-set by specific missions. Real keyboard Shift-to-sprint is left completely
  untouched by these hooks, regardless of whether a controller is connected or idle
  (see Known Limitations for the k+m note, and Sprint's real kbutton search).

### Menu & pause
- **Start button** — opens **and closes** the pause menu via real engine calls (not a
  keypress emulation): the real hardcoded ESCAPE-key path for opening, and the same
  function's real "resume" case for closing, driven by a `WndProc` subclass hook so it
  keeps working even while the game's gameplay-simulation tick halts during pause.
- **B — back out of menus** — while a menu is open (main menu, pause menu, etc.), B
  forwards a real ESC keypress to it (the same real mechanism the engine's own key
  handler uses for ESC generically), backing out one level or closing it, on top of
  its normal crouch/prone role during gameplay.
- **Survival ready-up** (hold Y ~740ms between waves) — one of two deliberate, documented
  exceptions to this mod's native-only approach (see below); switches weapons instead if
  released before the threshold.
- **Buy-station + pause interaction fix** — a real native bug (not ours) where using a
  buy station then pausing could permanently break all input (ours and real
  keyboard/mouse) until level reload; fixed by reinstating a rising-edge gate window.

### Configuration & customization

All of the tunable values above — plus button/stick layout — live in
**`mw3ncp_config.ini`**, written next to the DLL the first time the mod runs (with
every option pre-filled with its default value and a comment explaining it, so the
file is self-documenting from the moment it appears — nothing to configure by hand
to get started). Changes take effect on next launch; there's no live-reload yet, and
no in-game options screen — this file is the interim way to tune the mod until
native controller UI navigation exists.

| Section | Key | Default | What it does |
|---|---|---|---|
| `[Look]` | `Sensitivity` | `250` | Look-stick turn rate, degrees/second at full deflection (not always the right stick — depends on `StickLayout` below) |
| `[Look]` | `AdsSlowdownStrength` | `1.75` | ADS zoom-aware look slowdown strength (`0` = off, `1` = fully proportional to zoom, higher = more aggressive than proportional; `1.75` confirmed live to feel closer to real console controller CoD than exactly `1.0`) |
| `[Look]` | `AdsSlowdownBaseline` | `0.65` | Multiplies on top of the strength curve above — without it, low-zoom optics (iron sights/red dots) got almost no slowdown at all, since the zoom ratio alone stays too close to `1.0` to produce a real effect regardless of strength. `1.0` = no extra effect; lower = more slowdown even at minimal zoom |
| `[Look]` | `InvertLook` | `0` | OG console "Invert Look" — flips vertical look |
| `[Stance]` | `ProneHoldThresholdMs` | `400` | B: hold-vs-tap threshold for the stance ladder |
| `[Interact]` | `HoldThresholdMs` | `300` | X: how long Interact must be held before it fires (a quick tap reloads instead, same as console) |
| `[Survival]` | `ReadyUpHoldThresholdMs` | `740` | Y: hold-to-ready-up threshold between Survival waves |
| `[Sprint]` | `MaxStaminaSeconds` | `4` | Seconds of continuous sprint before stamina depletes |
| `[Sprint]` | `RegenSeconds` | `2` | Seconds not sprinting to fully recover from empty |
| `[Bindings]` | `ButtonLayout` | `Default` | `Default` / `Tactical` / `Lefty` / `TacticalLefty` — see table below |
| `[Bindings]` | `StickLayout` | `Default` | `Default` / `Southpaw` / `Legacy` / `LegacySouthpaw` — see table below |
| `[Bindings]` | `FlipTriggers` | `0` | Independently swaps RT↔RB and LT↔LB, combining with whichever `ButtonLayout` is active |
| `[AimAssist]` | `Enabled` | `0` | Our own from-scratch aim assist (see below) — **EXPERIMENTAL, NOT FUNCTIONAL** (broken target classification), must stay `0` for public builds |
| `[AimAssist]` | `Range` | `1200` | Max world-unit distance to a target for it to be considered at all |
| `[AimAssist]` | `ConeDegrees` | `6` | Half-angle of the "near crosshair" cone a target must be within |
| `[AimAssist]` | `FrictionStrength` | `0.6` | How much to slow the look-turn rate while the crosshair is near a valid target (`0` = no slowdown, `1` = strongest) |
| `[AimAssist]` | `MagnetismDegreesPerSecond` | `40` | Max degrees/second the crosshair gets pulled toward a valid target, independent of stick input |

**Button layout presets** (reconstructed from the unchanged CoD4→MW2→MW3 console
control scheme; ~90-95% confidence, not independently verified against real
hardware — `TacticalLefty` in particular may need a correction pass):

| Action | Default | Tactical | Lefty | TacticalLefty |
|---|---|---|---|---|
| Fire | RT | RT | LT | LT |
| ADS | LT | LT | RT | RT |
| Lethal | RB | RB | LB | LB |
| Tactical | LB | LB | RB | RB |
| Crouch/Prone | B | RS | B | LS |
| Sprint | LS | LS | RS | RS |
| Melee | RS | B | LS | B |

**Stick layout presets:**

| Layout | Left stick | Right stick |
|---|---|---|
| Default | Move | Look |
| Southpaw | Look | Move |
| Legacy | Forward/back + turn (horizontal) | Look up/down + strafe (horizontal) |
| LegacySouthpaw | Look up/down + strafe (horizontal) | Forward/back + turn (horizontal) |

## Why native, not an emulator

Every other "controller support" option for this game works by faking keyboard/mouse
input (synthetic key taps, injected mouse deltas) underneath a mapper tool. That adds a
real, measurable translation layer between the stick and the game: poll → convert to a
key/mouse event → OS input queue → the game's own keyboard/mouse-delta processing.

This mod instead writes straight into the engine's real per-frame input path —
`usercmd_t.forwardmove/rightmove`/`.buttons`, the raw pitch/yaw angle accumulators, and
(where the engine requires it) the real internal `kbutton_t` down/up state and
`pm_flags` bits the game's own movement code reads — from inside the game's own
process, on the game's own frame tick. There is no OS-level input event, no
intermediate queue, and no keyboard/mouse pipeline to pass through at all. That's a full
layer of translation and buffering removed, which is the mod's core advantage: input
feel and latency that matches (not approximates) native console analog input, not a
keyboard/mouse emulation layer with a controller icon on it.

**One narrow, explicit exception:** Survival's between-wave ready-up (hold Y) synthesizes
a real F5 keypress via `PostMessage` rather than driving an engine call directly. This is
a deliberate, user-approved workaround for one specific, non-gameplay-critical UI prompt
after an extensive multi-session search found no locatable native trigger — see
`re_notes/known_issues.md` issue #5 for the full trail and rationale. It's the only place
in the whole mod that does this; every other button, including all of movement/look/
combat, drives the engine's real internal state directly, as described above.

## Current control map (`iw5sp.exe`, Xbox-layout controller)

| Input | Action | Status |
|---|---|---|
| Left stick | Move (analog forward/back/strafe) | ✅ Confirmed |
| Right stick | Look (independent sensitivity, no mouse-accel/filter inherited) | ✅ Confirmed |
| Right trigger (RT) | Fire | ✅ Confirmed |
| Left trigger (LT) | Aim Down Sights (true hold-to-aim, real kbutton) | ✅ Confirmed |
| Left stick click (L3) | Sprint (real `pm_flags` bit; auto-stands from crouch/prone; real 4s/2s stamina-cooldown model) | ✅ Confirmed |
| A | Jump | ✅ Confirmed |
| B | Crouch/Prone — tap toggles crouch, hold goes prone, full 3-state ladder (see below) | ✅ Confirmed |
| X | Interact **and** Reload (real kbutton, context-sensitive like console) | ✅ Confirmed |
| Right stick click (R3) | Melee | ✅ Confirmed |
| Left bumper (LB) | Tactical (smoke) | ✅ Confirmed |
| Right bumper (RB) | Lethal (frag) | ✅ Confirmed |
| Y | Weapon switch (`weapnext`); hold ~740ms in Survival to ready up between waves | ✅ Confirmed |
| Start | Opens **and closes** the pause menu (real native calls, not a keypress emulation) | ✅ Confirmed |
| Back | *(unassigned)* | ⬜ Not yet implemented — a first attempt regressed live (see `re_notes/known_issues.md`), reverted, deprioritized (nice-to-have, not gameplay-defining) |
| D-pad (Up/Right/Down/Left) | `+actionslot 1-4` — killstreaks/attachments (e.g. noob tube), data-driven by loadout | ✅ Confirmed* (user tested at least half the directions live; all four use the identical confirmed mechanism, so high confidence on the untested ones too) |
| Killstreaks (collectively) | Calling in / controlling killstreaks (Predator missile, etc.) — see the dedicated table below | 🟡 Partial — essential to Campaign, which is otherwise mostly untested so far |
| Menu/UI navigation | Buy stations, pause menu, options, etc. | ⬜ Not yet implemented — mouse/keyboard still required |

**B's stance ladder**, matching real Xbox 360 CoD behavior (not a raw hold of either
bit):

| From | Tap | Hold |
|---|---|---|
| Standing | → Crouched | → Prone |
| Crouched | → Standing | → Prone |
| Prone | → Crouched | → Standing |

"Hold" fires the instant the press crosses the threshold; "tap" only fires on release,
and only if the hold threshold was never reached.

**Killstreaks** — essential to Campaign, which is otherwise mostly untested so far
(most testing to date has been Survival-focused). Real roster confirmed via the
game's own buy-station data (`sp/survival_armories.csv`):

| Killstreak | Status |
|---|---|
| Predator missile (`remote_missile`) | 🟡 Partial — confirmed partially working live; needs its own per-killstreak investigation (see `re_notes/known_issues.md`) |
| Precision airstrike (`precision_airstrike`) | ⬜ Not yet tested |
| AI squadmate call-in (`friendly_support_delta`/`friendly_support_riotshield`) | 🟡 Partial — likely the same feature behind D-pad Left's known squadmate call-in bug (see `re_notes/known_issues.md` issue #14) |
| All others | ⬜ Not yet tested |

## What's blocking the remaining buttons

- **Back:** a first attempt wired `0x00A98B14` in as `+scores`'s kbutton, based on an
  unvalidated assumption (a bind-name-table index treated as if it were a
  `FUN_00438710` switch case number). Confirmed WRONG live — it made the player walk
  backward (it's almost certainly the real `+back` movement kbutton). Reverted
  immediately; Back is a no-op again. Needs the same live-keycode-table technique that
  correctly solved weapnext (see `re_notes/known_issues.md`) applied to TAB instead.
  Deprioritized — scoreboard isn't gameplay-defining, unlike D-pad/killstreaks.
- **Killstreaks:** user's first live test (Predator missile) showed partial
  functionality — needs its own per-killstreak investigation once D-pad/scoreboard
  settle, likely a distinct mechanism per killstreak type.
- **Menu/UI navigation (task #22) — real D-pad/A navigation now implemented and
  live-confirmed** (main menu, pause menu, options screens including two-pane
  category/settings drill-in-drill-out) — see the D-pad/A section above. Button-
  glyph prompt swapping (task #6's other half) is still unstarted, and slider-type
  settings (sensitivity, etc.) can be navigated to but not yet adjusted by
  controller — see `re_notes/known_issues.md` issue #22.

## Architecture

```
iw5sp.exe (unmodified game logic)
    │  loads d3d9.dll from its own directory first (standard Windows DLL search order)
    ▼
our proxy d3d9.dll                         ← real injection point, ships beside the exe
    │  forwards all real d3d9 exports to the genuine system d3d9.dll
    │  hooks IDirect3D9::CreateDevice (vtable) -> subclasses the real device's window
    ▼
XInput poll (linked by us, game has none)  → deadzone + response curve
    ▼
TWO separate per-frame injection points, because they run at different times:
    │  FUN_0057de60 (gameplay-simulation tick, halts while paused)
    │      — movement, look, buttons, ADS, Sprint, Reload, weapon switch inject here
    │  WndProc subclass + a SetTimer-driven ~60Hz WM_TIMER (keeps running even while
    │  paused, since it's a plain Win32 window hook, not a D3D9 vtable)
    │      — Start's pause-menu open/close inject here (a real Present hook was tried
    │        first but confirmed dead — see re_notes/known_issues.md)
    ▼
real KeyDown/KeyUp kbutton calls — ADS, Reload (not raw usercmd bits)
real Cbuf_AddText/Cmd_ExecuteString pair — confirmed working, but not the mechanism
    for weapnext/togglemenu (see re_notes/known_issues.md)
real hardcoded ESCAPE-key path + FUN_004396d0's open/close cases — Start's pause menu
real FUN_00541020 raw-keycode dispatch table + FUN_00438710 jump table — weapon switch
    and D-pad (+actionslot 1-4, data-driven by loadout: killstreaks/attachments/NVG)
synthetic keydown/keyup via PostMessage — Survival ready-up (F5) and D-pad Left's
    AI-squadmate call-in ('4') ONLY, the two deliberate exceptions to real-engine-
    calls-only input in this mod; real native triggers not yet found for either
our own timer layer (GetTickCount-based, independent per hook site) — sprint stamina/
    cooldown, since forcing the real pm_flags bit bypasses the native limiter entirely;
    bypassed itself when the real player_sprintUnlimited dvar is live-set by a mission
    ▼
real ForwardKeyToMenu (FUN_004d9850) call, generic keycode forward to whatever menu
    is active — D-pad Up/Down/Left/Right + A now drive real menu item navigation and
    select/drill-in-drill-out, keycodes read directly out of the decompiled
    FUN_004dfd30 dispatcher rather than assumed (task #22, see known_issues.md)
```

Every hook target is found via byte-pattern/signature scanning or live memory-diffing
at runtime — never a hardcoded address assumed stable across game updates or even
between two launches of the same build (several of this mod's real kbutton/flag
addresses live in dynamically-allocated per-tick structures, not fixed static memory).
See `re_notes/iw5sp.md` for the complete reverse-engineering log: every function found,
every dead end ruled out, and why.

## Controller compatibility by mission/mode

Started tracking per-mission/per-mode live playtest status after a first
Campaign playtest session (2026-07-18) found controller support was solid
overall but genuinely uneven mission-to-mission — a single "Campaign works"
verdict would have hidden that. Full detail, including exact fallback
points and open questions, lives in `re_notes/compatibility_matrix.md`;
this is a condensed summary.

| Mode | Tested | Fully compatible | Partial (fallback needed) | Not yet tested |
|---|---|---|---|---|
| Campaign (17 missions) | 8 | 4 | 4 | 9 |
| Special Ops (16 missions) | 0 | — | — | 16 |
| Survival | tracked as one entry (map-independent) | Works well overall | 1 known issue (Predator missile killstreak, see below) | — |

Campaign missions confirmed fully compatible so far: Persona Non Grata,
Davis Family Vacation, Goalpost, Return to Sender. Partial (specific
fallback points only, not whole-mission failures): Hunter Killer (DPV
aiming), Turbulence (a scripted-freeze sequence bypassed by our movement
hook), Back on the Grid (mortar fire input, plus an unconfirmed
mounted-turret difficulty question flagged for deep investigation), Mind
the Gap (a vehicle-exit prompt gated on a bind this mod doesn't drive yet).
See `re_notes/compatibility_matrix.md` for the full per-mission breakdown
and `re_notes/known_issues.md` issue #27 for the underlying bug detail
behind each partial entry.

## Killstreak support

Full detail, including MP's full killstreak list for future reference, is
in `re_notes/killstreak_reference.md` — this is a condensed summary of the
Campaign-relevant, controller-tested subset only.

| Weapon system | Mission | Status |
|---|---|---|
| Boat | Hunter Killer | ✅ Working |
| UGV (minigun + grenade launcher) | Persona Non Grata | ✅ Working |
| Helicopter door gun | Return to Sender | ✅ Working |
| SMAW (dumb-fire) | Goalpost | ✅ Working |
| DPV (Diver Propulsion Vehicle) | Hunter Killer | ⚠️ Aim broken, movement works |
| Mortar | Back on the Grid | ⚠️ Fire input not wired up |
| Mounted Browning M2 turret | Back on the Grid | ⚠️ Works, but difficulty discrepancy under investigation |
| SMAW (lock-on vs. aircraft) | Goalpost | ❓ Unconfirmed — may not even be a real bug |
| Predator Missile | Black Tuesday / Down the Rabbit Hole | ⚠️ Camera/view works, Fire launch uncertain (task #7) |
| AC-130 | Iron Lady / Fire Mission (Spec Ops) | ❓ Not yet playtested |

Multiplayer's own killstreak system (3 strike packages, ~20+ rewards) is
untouched — MP support hasn't started at all, see "Known limitations"
below.

## Known limitations

See `re_notes/known_issues.md` for the full, actively-tracked list.

- Controller menu/UI navigation (D-pad item selection + A-select, including options
  screens' category/settings drill-in-drill-out) is implemented and live-confirmed
  (task #22) — see the D-pad/A section above. Still open: adjusting a slider-type
  setting's actual value by controller (navigating to one works, changing it
  doesn't yet), and button-glyph prompts (no controller icons in hint text yet) —
  keyboard/mouse remains fully functional alongside controller for both.
- Survival ready-up (hold Y) uses a synthetic F5 keypress rather than a real engine
  call — the only such exception in the whole mod. The real native trigger was never
  found despite an extensive search (see `re_notes/known_issues.md` issue #5); this
  workaround will be replaced if/when one turns up.
- Sprint's stamina/cooldown model doesn't yet account for two real overrides: specific
  missions that live-set `player_sprintUnlimited` (checked and bypassed correctly) is
  handled, but the Extreme Conditioning perk (doubles sprint duration to 8s, real
  internal name `specialty_longersprint`) is likely a separate mechanism
  (`perk_sprintMultiplier`) and isn't detected yet — see `re_notes/known_issues.md`.
- Aim assist (rotational friction, target magnetism) is implemented but currently
  **non-functional and disabled by default** (`Enabled=0`) — the underlying math is
  confirmed correct, and real entity classification (telling an AI actor apart from
  props/debris) is believed found via static analysis, but not yet live-verified, so
  the fallback movement heuristic remains in place and still produces broken
  targeting. See `re_notes/known_issues.md` issue #15.
- **A real, native controller-options menu (task #23) is in active development, not
  yet shippable.** A working mechanism to inject custom menu content into the game's
  own real menu system was built and confirmed live for simple content, but real
  menu content (backgrounds, sliders, etc.) hit a genuine architectural limit —
  loading a material live triggers unsafe GPU-resource creation outside the engine's
  controlled loading context. A structurally-sound fix (loading through the engine's
  own real level-load transition instead) is believed viable but not yet
  implemented. No player-facing effect from this work exists in this build. See
  `re_notes/known_issues.md` issue #23.
- Multiplayer (`iw5mp.exe`) support has not been started. It's a separately-built binary
  from `iw5sp.exe` — none of the offsets/addresses found so far carry over, and it needs
  its own full signature-scanning pass. There's also an open, unresolved question about
  anti-cheat exposure from code injection on `iw5mp.exe` that needs to be discussed
  before that work begins.
- **Keyboard/mouse play is intended to be strictly additive and unaffected, but is no
  longer treated as a fully-verified, first-class input path.** A real regression was
  found and fixed this session (our own controller-support hooks silently broke native
  keyboard sprint entirely — see `re_notes/known_issues.md` issue #10) — the kind of bug
  that's easy to introduce with this project's hooking style and easy to miss unless
  someone happens to test keyboard specifically. Controller is the actively-verified,
  primary input method going forward; if you're mainly a keyboard/mouse player, keep a
  keyboard within reach and expect the occasional oddity while this mod is installed.
  **This is not a suggestion to avoid the keyboard, though** — it's still required,
  not optional, for adjusting slider-type settings, Back, and most killstreak
  call-ins, none of which have a controller-native implementation yet (menu
  navigation itself now does, as of task #22). A keyboard needs to stay reachable
  during any session either way. See `re_notes/known_issues.md` issue #11 for the
  full reasoning.

---

## Client compatibility

This mod is built and verified only against **retail Steam MW3**. Long-term goal is
to support other MW3 client variants too, but none of the following are implemented
or tested yet — this table is research-stage only, not a compatibility claim. Full
detail in `re_notes/known_issues.md` issue #25.

| Client | SP/MP | Binary vs. retail | `d3d9.dll` injection viable? | Status |
|---|---|---|---|---|
| Retail Steam | Both | — (baseline) | Yes (confirmed, current target) | Actively supported |
| Plutonium — MP | MP | `iw5mp.exe` byte-identical to retail | Believed yes (same binary) | **Not recommended — see warning below** |
| Plutonium — SP | SP | `iw5sp.exe` is a different binary (~175KB smaller, differences start almost immediately, not just a few patches) | Unknown, would need independent address re-verification | Not yet investigated |
| AlterWare IW5-Mod | SP + Spec Ops | Separate `iw5-mod.exe` executable, not `iw5sp.exe` | Unknown, binary not yet acquired for analysis | Not yet investigated — most promising target given this mod's SP-first scope, no known anti-cheat concern found |
| DeckOps (MW3) | MP (via Plutonium) | Same as Plutonium MP | Unknown — Proton/Wine's D3D9 translation layer untested | Not yet investigated — inherits the Plutonium MP warning below, plus unverified Proton behavior |

> **⚠️ Do not use this mod with Plutonium multiplayer.** Plutonium's anti-cheat is
> confirmed (from its own documentation) to ban DLL injection and memory access —
> a 7-day ban on first offense, permanent after. This mod's entire architecture
> (a proxy `d3d9.dll`, function hooking, and memory-read-based aim assist) is
> exactly what that system is built to catch, regardless of the mod being
> input-only rather than a gameplay cheat. This is a real, confirmed risk, not a
> theoretical one — see `re_notes/known_issues.md` issue #25 for the evidence.

---

## Credits

This mod vendors and links the following third-party library:

- **[MinHook](https://github.com/TsudaKageyu/minhook)** (`proxy_d3d9/third_party/minhook/`) — Copyright (C) 2009-2017 Tsuda Kageyu. BSD 2-Clause-style license (see `proxy_d3d9/third_party/minhook/LICENSE.txt`). Used for all API hooking (vtable and inline detours) in the proxy DLL.
- **Hacker Disassembler Engine (HDE) 32/64 C**, bundled with MinHook — Copyright (c) 2008-2009, Vyacheslav Patkov. Same style of license (see the same `LICENSE.txt`).

Full license text for both is reproduced verbatim in `proxy_d3d9/third_party/minhook/LICENSE.txt`.

## License

This project's own source is released under a custom, permissive license — see
[`LICENSE`](LICENSE). The source is fully open: free to use, modify, and fork.
The one restriction is that neither this project nor any fork/derivative of it
may ever be sold or charged for — it must stay free for everyone. **Because of
that restriction, this license does not meet the OSI's formal "open source"
definition** (which requires no limits on commercial use) — it's an open,
freely-forkable, source-available license with one deliberate carve-out, not
an OSI-approved one. It does not grant any rights to Call of Duty: Modern
Warfare 3 itself; you need your own legitimate copy of the game to use this
mod.

## Contributing

Contributions are welcome — see [`CONTRIBUTING.md`](CONTRIBUTING.md) for the
ground rules (native RE only, no hardcoded addresses, verify live, SP/MP are
separate efforts) and [`CODE_STANDARDS.md`](CODE_STANDARDS.md) for the
production-ready bar every change is held to (no placeholder hooks, no
half-finished work presented as done — applies identically to AI-assisted
code) before opening a PR.
