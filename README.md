# MW3 Native Controller Support (Campaign & Survival)

A from-scratch native controller mod for Call of Duty: Modern Warfare 3 (2011, IW5
engine) — analog movement, look, and buttons driven directly through the game's own
engine calls, not keyboard/mouse emulation. See `re_notes/` for the full reverse-
engineering writeup.

## Known limitations

- **Mouse/keyboard-driven menus are not supported while the controller mod is active**
  (e.g. Survival's buy stations). The game shares one internal gate between "residual
  loading-screen cursor block" and "a real interactive menu wants the cursor" — no
  reliable way was found to tell those apart, so controller mode always keeps that gate
  clear, which prevents mouse-driven menus from opening. This is a known trade-off, not
  a bug being tracked for a fix. It goes away once native controller menu navigation
  (in progress) is implemented — at that point the game's own menu system will manage
  this gate correctly instead of the mod overriding it.
- Full console-style aim assist, controller UI glyphs, and Multiplayer support are not
  yet implemented.
