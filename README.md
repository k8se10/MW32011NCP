# MW3 Native Controller Support (Campaign & Survival)

**Status: ALPHA — partially working, in active development (as of 2026-07-14).**
Core movement/look/buttons/ADS are functional against `iw5sp.exe` (Campaign/Survival)
but several mechanics are still incomplete or have known bugs (see below and
`re_notes/iw5sp.md`'s open-items list). Not feature-complete, not fully tested, and
not yet started for `iw5mp.exe` (Multiplayer).

A from-scratch native controller mod for Call of Duty: Modern Warfare 3 (2011, IW5
engine) — analog movement, look, and buttons driven directly through the game's own
engine calls, not keyboard/mouse emulation. See `re_notes/` for the full reverse-
engineering writeup.

## Why native, not an emulator

Every other "controller support" option for this game works by faking keyboard/mouse
input (synthetic key taps, injected mouse deltas) underneath a mapper tool. That adds
a real, measurable translation layer between the stick and the game: poll → convert to
a key/mouse event → OS input queue → the game's own keyboard/mouse-delta processing.

This mod instead writes straight into the engine's real per-frame input path —
`usercmd_t.forwardmove/rightmove`/`.buttons` and the raw pitch/yaw angle accumulators —
from inside the game's own process, on the game's own frame tick. There is no OS-level
input event, no intermediate queue, and no keyboard/mouse pipeline to pass through at
all. That's a full layer of translation and buffering removed, which is the mod's core
advantage: input feel and latency that matches (not approximates) native console analog
input, not a keyboard/mouse emulation layer with a controller icon on it.

## Known limitations

- Controller menu/UI navigation (D-pad/stick item selection, button-glyph prompts, a
  real controller options screen) is not implemented yet — for now, menus (buy
  stations, pause, etc.) still need mouse/keyboard, and that continues to work
  normally alongside controller gameplay (movement/look/buttons/ADS no longer depend
  on any state the menu system also uses, so the two don't conflict).
- Full console-style aim assist and Multiplayer support are not yet implemented.
