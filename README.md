# MW3 Native Controller Support (Campaign & Survival)

**Status: PRE-ALPHA — actively in development (as of 2026-07-16).** Analog movement, look,
and most buttons are confirmed working live against `iw5sp.exe` (Campaign/Survival),
including a real sprint stamina/cooldown model, Start's full pause/unpause, weapon
switching, D-pad killstreak/attachment slots, and Survival's between-wave ready-up. Back
is still unassigned (deprioritized — not gameplay-defining), killstreaks need
per-killstreak work, sprint's mission/perk overrides (unlimited-sprint missions, Extreme
Conditioning) aren't accounted for yet, full menu/UI navigation isn't implemented, and
Multiplayer (`iw5mp.exe`) hasn't been started. Not feature-complete, not fully tested
end-to-end.

A from-scratch native controller mod for Call of Duty: Modern Warfare 3 (2011, IW5
engine) — analog movement, look, and buttons driven directly through the game's own
engine calls, not keyboard/mouse emulation. See `re_notes/` for the full reverse-
engineering writeup this mod is built on.

## Feature list

- **Analog movement** (left stick) — real `usercmd_t.forwardmove`/`.rightmove` bytes,
  additive on top of any keyboard input already present.
- **Analog look** (right stick) — writes the raw pitch/yaw angle-delta accumulators
  directly, bypassing the mouse pipeline entirely (own sensitivity, no mouse
  accel/filter inherited).
- **Fire** (RT), **ADS** (LT, true hold via real `kbutton_t` KeyDown/KeyUp calls, not a
  toggle), **Melee** (R3), **Tactical**/**Lethal** (LB/RB), **Jump** (A).
- **Crouch/Prone stance ladder** (B) — tap toggles crouch, hold goes prone, full 3-state
  ladder matching real Xbox 360 CoD behavior (not a raw hold of either bit).
- **Interact + Reload** (X) — real, context-sensitive `kbutton_t`, found via memdiff.
- **Sprint** (L3) — real `pm_flags` bit, forced via a Pmove-entry hook; auto-stands from
  crouch/prone first, matching console. Includes a real **stamina/cooldown model**
  (our own timer layer, since forcing the bit natively bypasses the game's own limiter
  entirely): 4 seconds continuous sprint before cutting out, then a real 2-second
  cooldown before it can resume — not just a cosmetic meter, sprint is genuinely
  blocked while on cooldown. Automatically bypassed (genuinely unlimited sprint) when
  the real `player_sprintUnlimited` dvar is live-set by specific missions.
- **Weapon switch** (Y) — real `weapnext` dispatch via the engine's own bind-index jump
  table, found by live-reading the raw-keycode dispatch table for the actual bound keys.
- **Start button** — opens **and closes** the pause menu via real engine calls (not a
  keypress emulation): the real hardcoded ESCAPE-key path for opening, and the same
  function's real "resume" case for closing, driven by a `WndProc` subclass hook so it
  keeps working even while the game's gameplay-simulation tick halts during pause.
- **D-pad** (all 4 directions) — real `+actionslot 1-4` dispatch, data-driven by
  loadout (killstreaks/attachments/NVG-style toggles, whatever's actually equipped).
- **Survival ready-up** (hold Y ~740ms between waves) — the one deliberate, documented
  exception to this mod's native-only approach (see below); switches weapons instead if
  released before the threshold.
- **Buy-station + pause interaction fix** — a real native bug (not ours) where using a
  buy station then pausing could permanently break all input (ours and real
  keyboard/mouse) until level reload; fixed by reinstating a rising-edge gate window.

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
(most testing to date has been Survival-focused):

| Killstreak | Status |
|---|---|
| Predator missile | 🟡 Partial — confirmed partially working live; needs its own per-killstreak investigation (see `re_notes/known_issues.md`) |
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
    and D-pad (+actionslot 1-4, data-driven by loadout: killstreaks/attachments/NVG)
synthetic F5 keydown/keyup via PostMessage — Survival ready-up ONLY (the sole exception
    to real-engine-calls-only input in this mod; real native trigger not yet found)
our own timer layer (GetTickCount-based, independent per hook site) — sprint stamina/
    cooldown, since forcing the real pm_flags bit bypasses the native limiter entirely;
    bypassed itself when the real player_sprintUnlimited dvar is live-set by a mission
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
- Survival ready-up (hold Y) uses a synthetic F5 keypress rather than a real engine
  call — the only such exception in the whole mod. The real native trigger was never
  found despite an extensive search (see `re_notes/known_issues.md` issue #5); this
  workaround will be replaced if/when one turns up.
- Sprint's stamina/cooldown model doesn't yet account for two real overrides: specific
  missions that live-set `player_sprintUnlimited` (checked and bypassed correctly) is
  handled, but the Extreme Conditioning perk (doubles sprint duration to 8s) is likely a
  separate mechanism (`perk_sprintMultiplier`) and isn't detected yet — see
  `re_notes/known_issues.md`.
- Full console-style aim assist (rotational friction, target magnetism) is not yet
  implemented — that requires reading live entity/aim-target data out of the game's
  process memory, planned as a later layer on top of the current stick response curves.
- Multiplayer (`iw5mp.exe`) support has not been started. It's a separately-built binary
  from `iw5sp.exe` — none of the offsets/addresses found so far carry over, and it needs
  its own full signature-scanning pass. There's also an open, unresolved question about
  anti-cheat exposure from code injection on `iw5mp.exe` that needs to be discussed
  before that work begins.
- Vanilla keyboard/mouse play is unaffected by design — the mod is strictly additive.

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
separate efforts) before opening a PR.
