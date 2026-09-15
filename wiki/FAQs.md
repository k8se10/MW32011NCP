# Frequently Asked Questions

## General

### What is this project?

A from-scratch native controller and enhancement project for Call of Duty: Modern Warfare 3 (2011). It hooks the game's own real internal functions to drive analog movement, look, and buttons directly — not a keyboard/mouse-emulation mapper wearing a controller icon. See [[Technical Documentation]] for how.

### Why was this created?

MW3 (2011) shipped on PC with **no controller input path at all** — confirmed via the binary's own import table, there's no hidden setting to unlock. The Xbox 360 and PS3 versions of the same game supported controllers natively; this project rebuilds that experience for PC from scratch.

### Is this official?

No. This is an independent, community reverse-engineering project, not affiliated with, endorsed by, or sponsored by Activision, Infinity Ward, or any of their affiliates.

### Is this safe to use? Will I get banned?

For **retail Steam Campaign/Survival** (`iw5sp.exe`), this is a single-player/offline experience, and no anti-cheat scanning has been identified active on that binary. **Do not use this with Plutonium multiplayer** — their anti-cheat explicitly bans DLL injection and memory access, and this project's architecture is exactly what that's built to catch (a 7-day ban on first offense, permanent after). See [[Compatibility]] for the full detail. Retail Multiplayer (`iw5mp.exe`) is a separate concern this project hasn't started work on at all — see below.

## Technical

### Why is there no download available right now?

MW3 (2011) received its first-ever binary update, recompiling the game from 32-bit to 64-bit — a hard architectural break for every hook this project had found. The project is being rebuilt from that foundation. See [[Home]] for current progress.

### Does this work with Multiplayer?

**Not yet — Multiplayer hasn't been started at all.** `iw5mp.exe` is a completely separate binary from Campaign/Survival's `iw5sp.exe`, needing its own full reverse-engineering pass from scratch. There's also an open, unresolved question about anti-cheat exposure on retail Multiplayer (VAC is confirmed active there) that needs to be worked through before that effort begins.

### Does this support all controllers?

Any XInput-compatible controller (Xbox, or DualShock/DualSense through an XInput-emulating wrapper like Steam Input) — see [[Controller Setup]] for detail. A native DualSense backend (raw HID, bypassing Steam Input, plus gyro-aim) existed on the prior 32-bit line but hasn't been ported to the current rebuild yet.

### Does this work on Steam Deck / Linux?

Multiple independent players reported it working via Proton on the prior line, including on real Steam Deck hardware — not yet independently verified on the current rebuild. See [[Compatibility]] for the honest current status.

### Why doesn't [feature X] work yet?

Check [[Known Issues]] first — it's a real, current, honest breakdown of exactly what's confirmed working, build-verified-but-unconfirmed, or not yet implemented. This is an alpha, from-scratch RE project rebuilding after a major architecture break; most gaps are documented, understood, and being actively worked on, not silent bugs.

### How does this actually work, technically?

See [[Technical Documentation]] — short version: a proxy `d3d9.dll` gets loaded by the game (standard Windows DLL search order), hooks a handful of real internal engine functions (found via runtime signature scanning), and polls XInput to feed them, all from inside the game's own process on the game's own frame tick.

## Development

### When will a release ship?

No fixed date — see [[Development Notes]] for the project's stage definitions. The release gate is reaching feature parity with the prior 32-bit line's own final state (every control, the visual-enhancement suite, the custom Options screen) before the first `-x64` release ships. Check [[Changelogs]] for progress.

### Can I contribute?

Yes — see [`CONTRIBUTING.md`](https://github.com/k8se10/MW32011NCP/blob/main/CONTRIBUTING.md) and [`CODE_STANDARDS.md`](https://github.com/k8se10/MW32011NCP/blob/main/CODE_STANDARDS.md) on GitHub for the ground rules before opening a PR.

## See Also

- [[Known Issues]]
- [[Compatibility]]
- [[Troubleshooting]]
