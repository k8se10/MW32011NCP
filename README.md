# MW3 Native Controller Support (Campaign & Survival)

**Status: ALPHA — actively in development (as of 2026-07-15).** Analog movement, look,
and most buttons are confirmed working live against `iw5sp.exe` (Campaign/Survival).
Start now natively opens **and closes** the pause menu, and Y (weapon switch) works.
Back and D-pad are still unassigned, killstreaks need per-killstreak work, full menu/UI
navigation isn't implemented, and Multiplayer (`iw5mp.exe`) hasn't been started. Not
feature-complete, not fully tested end-to-end.

A from-scratch native controller mod for Call of Duty: Modern Warfare 3 (2011, IW5
engine) — analog movement, look, and buttons driven directly through the game's own
engine calls, not keyboard/mouse emulation. See `re_notes/` for the full reverse-
engineering writeup this mod is built on.

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

## Current control map (`iw5sp.exe`, Xbox-layout controller)

| Input | Action | Status |
|---|---|---|
| Left stick | Move (analog forward/back/strafe) | ✅ Confirmed |
| Right stick | Look (independent sensitivity, no mouse-accel/filter inherited) | ✅ Confirmed |
| Right trigger (RT) | Fire | ✅ Confirmed |
| Left trigger (LT) | Aim Down Sights (true hold-to-aim, real kbutton) | ✅ Confirmed |
| Left stick click (L3) | Sprint (real `pm_flags` bit; auto-stands from crouch/prone) | ✅ Confirmed |
| A | Jump | ✅ Confirmed |
| B | Crouch/Prone — tap toggles crouch, hold goes prone, full 3-state ladder (see below) | ✅ Confirmed |
| X | Interact **and** Reload (real kbutton, context-sensitive like console) | ✅ Confirmed |
| Right stick click (R3) | Melee | ✅ Confirmed |
| Left bumper (LB) | Tactical (smoke) | ✅ Confirmed |
| Right bumper (RB) | Lethal (frag) | ✅ Confirmed |
| Y | Weapon switch (`weapnext`) | ✅ Confirmed |
| Start | Opens **and closes** the pause menu (real native calls, not a keypress emulation) | ✅ Confirmed |
| Back | *(unassigned)* | ⬜ Not yet implemented — a first attempt regressed live (see `re_notes/known_issues.md`), reverted, deprioritized (nice-to-have, not gameplay-defining) |
| D-pad (all 4 directions) | Killstreaks/attachments (e.g. noob tube) — normally numbered keys on PC | ⬜ In progress |
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

## What's blocking the remaining buttons

- **Back:** a first attempt wired `0x00A98B14` in as `+scores`'s kbutton, based on an
  unvalidated assumption (a bind-name-table index treated as if it were a
  `FUN_00438710` switch case number). Confirmed WRONG live — it made the player walk
  backward (it's almost certainly the real `+back` movement kbutton). Reverted
  immediately; Back is a no-op again. Needs the same live-keycode-table technique that
  correctly solved weapnext (see `re_notes/known_issues.md`) applied to TAB instead.
  Deprioritized — scoreboard isn't gameplay-defining, unlike D-pad/killstreaks.
- **D-pad:** intended for killstreaks and attachment toggles (e.g. a grenade-launcher
  underbarrel), which map to `+actionslot 1-4` and normally sit on numbered keys on PC.
  The old table-order-guessed bit identities for these are flagged unreliable, and two
  of them (`0x100`/`0x200`) are already claimed by the confirmed-working B-button
  crouch/prone system — so those guesses are doubly suspect. In progress: live-reading
  the real keycode dispatch table for the actual bound keys instead.
- **Killstreaks:** user's first live test (Predator missile) showed partial
  functionality — needs its own per-killstreak investigation once D-pad/scoreboard
  settle, likely a distinct mechanism per killstreak type.
- **Full menu/UI navigation:** a genuinely separate system from in-game movement/look
  (menus read keyboard/mouse binds, not `usercmd`). Deliberately saved for last per the
  project's locked scope order — needs its own hook/input-synthesis path plus
  button-glyph prompt swapping.

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
    ▼
separate hook/path still needed for FULL menu & UI navigation (not implemented yet)
```

Every hook target is found via byte-pattern/signature scanning or live memory-diffing
at runtime — never a hardcoded address assumed stable across game updates or even
between two launches of the same build (several of this mod's real kbutton/flag
addresses live in dynamically-allocated per-tick structures, not fixed static memory).
See `re_notes/iw5sp.md` for the complete reverse-engineering log: every function found,
every dead end ruled out, and why.

## Known limitations

See `re_notes/known_issues.md` for the full, actively-tracked list.

- Controller menu/UI navigation (D-pad/stick item selection, button-glyph prompts, a
  real controller options screen) is not implemented yet — for now, menus (buy
  stations, pause, etc.) still need mouse/keyboard, and that continues to work
  normally alongside controller gameplay.
- Full console-style aim assist (rotational friction, target magnetism) is not yet
  implemented — that requires reading live entity/aim-target data out of the game's
  process memory, planned as a later layer on top of the current stick response curves.
- Multiplayer (`iw5mp.exe`) support has not been started. It's a separately-built binary
  from `iw5sp.exe` — none of the offsets/addresses found so far carry over, and it needs
  its own full signature-scanning pass. There's also an open, unresolved question about
  anti-cheat exposure from code injection on `iw5mp.exe` that needs to be discussed
  before that work begins.
- Vanilla keyboard/mouse play is unaffected by design — the mod is strictly additive.
