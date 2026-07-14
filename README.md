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

## Known limitations

- Controller menu/UI navigation (D-pad/stick item selection, button-glyph prompts, a
  real controller options screen) is not implemented yet — for now, menus (buy
  stations, pause, etc.) still need mouse/keyboard, and that continues to work
  normally alongside controller gameplay (movement/look/buttons/ADS no longer depend
  on any state the menu system also uses, so the two don't conflict).
- Full console-style aim assist and Multiplayer support are not yet implemented.
