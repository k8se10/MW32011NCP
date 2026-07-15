# MW3 Native Controller Support (Campaign & Survival)

**Status: ALPHA — actively in development (as of 2026-07-15).** Analog movement, look,
and most buttons are confirmed working live against `iw5sp.exe` (Campaign/Survival).
Start now natively opens the pause menu (see the control map below for the current
partial-functionality caveat). A couple of buttons are still unassigned, full menu/UI
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
| Y | Weapon switch (`weapnext`) | ⬜ Not yet implemented |
| Start | Opens pause menu (real native call, not a keypress emulation) | 🟡 Partial — opens the menu; closing/unpausing via controller doesn't work yet (use keyboard ESC or mouse to resume for now) |
| Back | *(unassigned)* | ⬜ Not yet implemented |
| D-pad (all 4 directions) | *(unassigned)* | ⬜ Not yet implemented |
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

- **Y (`weapnext`):** not a held kbutton — it's a one-shot console-style command. The
  engine's real `Cbuf_AddText`/`Cmd_ExecuteString` pair was found and confirmed working
  (see `re_notes/known_issues.md`), but a live dump of every registered command proved
  `weapnext` isn't invoked through that generic dispatcher at all — core gameplay verbs
  like weapon switching go through the same bind-index/`kbutton_t` mechanism already
  used for ADS and Reload instead. Needs the same live-verified bind-index hunt those
  got, not a text-command guess.
- **Start, closing the menu:** opening the pause menu is solved (a real hardcoded
  ESCAPE-key path in the engine's key-event handler, not a text command — see
  `re_notes/known_issues.md`). Closing it needs the mod's per-frame injection to keep
  running while the game is paused, which the current hook doesn't do (it lives in the
  gameplay-simulation pipeline, which pausing halts by design). A real
  `IDirect3DDevice9::Present` hook now exists for this purpose, but its detour doesn't
  fire live yet for reasons not yet root-caused — deferred until the real controller
  UI/menu-navigation effort below, which will need to solve this same class of problem
  anyway.
- **Back:** unassigned — likely `+scores`, a held (not one-shot) kbutton that needs its
  own real address found the same way ADS/Reload were.
- **D-pad:** the underlying `+actionslot 1-4` bits are known but largely unconfirmed
  individually against real gameplay effects — not yet assigned.
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
    │  hooks IDirect3D9::CreateDevice (vtable) -> hooks the real device's Present
    ▼
XInput poll (linked by us, game has none)  → deadzone + response curve
    ▼
TWO separate per-frame injection points, because they run at different times:
    │  FUN_0057de60 (gameplay-simulation tick, halts while paused)
    │      — movement, look, buttons, ADS, Sprint, Reload all inject here
    │  IDirect3DDevice9::Present (rendering tick, keeps running even while paused)
    │      — intended for menu-related input (Start); currently not reliably firing
    │        live, so Start's pause-menu handling also still runs from the gameplay
    │        tick as a fallback (see re_notes/known_issues.md)
    ▼
real KeyDown/KeyUp kbutton calls — ADS, Reload (not raw usercmd bits)
real Cbuf_AddText/Cmd_ExecuteString pair — confirmed working, but not the mechanism
    for weapnext/togglemenu (see re_notes/known_issues.md)
real hardcoded ESCAPE-key path in the key-event handler — Start's pause-menu open
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
- Start opens the pause menu natively but can't close/unpause it yet — use keyboard
  ESC or mouse to resume for now. Root cause and what's already been tried are
  documented in `re_notes/known_issues.md`.
- Full console-style aim assist (rotational friction, target magnetism) is not yet
  implemented — that requires reading live entity/aim-target data out of the game's
  process memory, planned as a later layer on top of the current stick response curves.
- Multiplayer (`iw5mp.exe`) support has not been started. It's a separately-built binary
  from `iw5sp.exe` — none of the offsets/addresses found so far carry over, and it needs
  its own full signature-scanning pass. There's also an open, unresolved question about
  anti-cheat exposure from code injection on `iw5mp.exe` that needs to be discussed
  before that work begins.
- Vanilla keyboard/mouse play is unaffected by design — the mod is strictly additive.
