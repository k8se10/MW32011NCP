# Frequently Asked Questions

## General

### What is this project?

A from-scratch native controller project for Call of Duty: Modern Warfare 3 (2011). It hooks the game's own real internal functions to drive analog movement, look, and buttons directly — not a keyboard/mouse-emulation mapper wearing a controller icon. See [[Technical Documentation]] for how.

### Why was this created?

MW3 (2011) shipped on PC with **no controller input path at all** — confirmed via the binary's own import table, there's no hidden setting to unlock. The Xbox 360 and PS3 versions of the same game supported controllers natively; this project rebuilds that experience for PC from scratch.

### Is this official?

No. This is an independent, community reverse-engineering project, not affiliated with, endorsed by, or sponsored by Activision, Infinity Ward, or any of their affiliates.

### Is this safe to use? Will I get banned?

For **retail Steam Campaign/Survival** (`iw5sp.exe`), this is a single-player/offline experience, and no anti-cheat scanning has been identified active on that binary. **Do not use this with Plutonium multiplayer** — their anti-cheat explicitly bans DLL injection and memory access, and this project's architecture is exactly what that's built to catch (a 7-day ban on first offense, permanent after). See [[Compatibility]] for the full detail. Retail Multiplayer (`iw5mp.exe`) is a separate concern this project hasn't started work on at all — see below.

## Technical

### Does this work with Multiplayer?

**Not yet — Multiplayer hasn't been started at all.** `iw5mp.exe` is a completely separate binary from Campaign/Survival's `iw5sp.exe`, needing its own full reverse-engineering pass from scratch. There's also an open, unresolved question about anti-cheat exposure on retail Multiplayer (VAC is confirmed active there) that needs to be worked through before that effort begins.

### Does this support all controllers?

Any XInput-compatible controller (Xbox, or DualShock/DualSense through an XInput-emulating wrapper like Steam Input) — see [[Controller Setup]] for detail. As of v0.3.2, there's also a native DualSense backend that talks to the controller directly over raw HID, bypassing Steam Input, plus real gyro-aim. Its Bluetooth path's stick-garbling bug is fixed and confirmed live as of v0.3.4 (a real signed-16-bit integer overflow at full-forward deflection); its USB path has still never been independently confirmed working by a separate tester, though the shared fix almost certainly applies there too.

### Does this work on Steam Deck / Linux?

Multiple independent players have reported it working via Proton, including on real Steam Deck hardware — but this hasn't been independently verified by the project itself yet. See [[Compatibility]] for the honest current status.

### Why doesn't [feature X] work yet?

Check [[Known Issues]] first — it's a real, current, honest breakdown of exactly what's confirmed working, partial, or not implemented. This is an alpha, from-scratch RE project; most gaps are documented, understood, and being actively worked on, not silent bugs.

### How does this actually work, technically?

See [[Technical Documentation]] — short version: a proxy `d3d9.dll` gets loaded by the game (standard Windows DLL search order), hooks a handful of real internal engine functions, and polls XInput to feed them, all from inside the game's own process on the game's own frame tick.

## Development

### When will Beta release?

No fixed date — see [[Development Notes]] for the project's stage definitions. Beta means practically feature-complete; the project is currently in Alpha. Vibration, button glyphs, DualSense (both connection types), and a new visual-enhancement suite are all live-confirmed working now, and aim assist has been permanently removed rather than being a pending item — what's still ahead is mainly playtesting the preview Options screen, fixing Survival Body Armor/reload vibration, Predator Missile's guidance feel, Campaign scripted-sequence (QTE) controller input, and closing the remaining Campaign/Special Ops and A-glyph gaps. Check [[Changelogs]] for release-by-release progress.

### Can I contribute?

Yes — see [`CONTRIBUTING.md`](https://github.com/k8se10/MW32011NCP/blob/main/CONTRIBUTING.md) and [`CODE_STANDARDS.md`](https://github.com/k8se10/MW32011NCP/blob/main/CODE_STANDARDS.md) on GitHub for the ground rules before opening a PR.

## See Also

- [[Known Issues]]
- [[Compatibility]]
- [[Troubleshooting]]
