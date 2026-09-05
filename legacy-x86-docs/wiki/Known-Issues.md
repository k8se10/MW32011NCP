# Known Issues

This is a plain-language, player-facing summary, current as of **v0.3.5 (2026-08-29)**. For the full internal reverse-engineering trail behind each item (function addresses, disassembly notes, live-test evidence), see [`re_notes/known_issues.md`](https://github.com/k8se10/MW32011NCP/blob/main/re_notes/known_issues.md) in the main repo — issue numbers below reference entries there.

## Partial — Works, With a Known Gap

| Feature | The gap | Cite |
|---|---|---|
| Predator Missile guidance | Launch works fine; steering the missile after firing still feels broken/overpowered. A real, disproportionate native auto-climb constant (1000x the player's own steering rate) was found via static RE, not yet connected to a fix | #29/#30 |
| Back button | Implemented, builds clean, live-tested — zero visible effect (no scoreboard/objectives overlay appears). Real cause undiagnosed, parked as a UI gap | #3/#7/#28 |
| DPV (Hunter Killer) | Movement works, aiming doesn't | #30 |
| Mortar (Goalpost) | Aim works, fire input not wired up | #30 |
| Mounted M2 turret (Goalpost) | Works, but feels too hard — cause not yet diagnosed, likely the same missing-aim-channel cause as DPV | #30 |
| SMAW lock-on vs. aircraft | Unconfirmed whether this is even a real bug | — |
| Real in-game controller options menu | A full replacement screen exists (every real vanilla tab, real rebind capture) but ships as a **PREVIEW/WIP** feature — off by default, not yet played, missing a window-mode setting, resolution scaling unverified | #66 |
| Highlighted-item A-glyph (menu navigation) | The original verified-only allowlist gate was removed 2026-08-16 — glyphs now draw on any screen with a manually-captured position (dozens covered via an in-game F2/F3 editor). A handful of Campaign pause-menu popups were captured wrong in v0.3.5 and reverted, pending a reliable per-screen discriminator | #51/#109 |
| Vibration/rumble | Fire and damage rumble both physically confirmed working. Doesn't register hits absorbed by Survival's purchasable Body Armor (a separate value not yet located in memory); in 2-player Survival co-op, damage-rumble disables itself entirely rather than risk triggering off a teammate's hits | #24/#63/#79 |
| Auto-mantle while sprinting | **Confirmed working (v0.3.4)** after a real coupling bug was found and fixed — still ships **off by default** (`AutoMantleEnabled=0`), opt-in by design, not because it's broken | #62 |
| DualSense (USB) | Implemented, but **has never been independently confirmed working against real hardware** — don't assume it works just because it's shipped. Bluetooth's own stick-garbling fix (v0.3.4) lives in shared code, so it almost certainly applies here too, but that's not the same as an independent confirmation | #76 |
| Campaign scripted sequences (QTEs) | Controller button presses don't register at all during scripted sequences (e.g. "Dust to Dust"'s elevator mantle) — real GSC evidence found (the native detection reads a genuine button-press event, not steady-state input), leading fix candidate identified (the same synthetic-keypress technique already used for Survival's ready-up), not yet implemented | #108 |
| Native shadow/lighting quality | `ForceHighQualityShadows`/`ForceHighQualityLighting` (v0.3.5, off by default) write real, confirmed native dvars for sun-shadow and model-lighting quality. Shadow-map *resolution* specifically was investigated in depth (9+ RE rounds including a full Ghidra analysis pass) but the actual creation call site wasn't found | #107 |

## Not Working / Not Implemented Yet

| Feature | Why | Cite |
|---|---|---|
| **Aim assist** | **Permanently removed** (v0.2.2), not just disabled — a from-scratch implementation was built and its math confirmed correct, but reading gameplay-entity memory to adjust aim was judged too close to a soft-aimbot from an anti-cheat perspective and cut entirely rather than shipped. Not coming back in its old form | #15/#16/#33 |
| Dog/hyena melee-struggle prompts (Survival) and Campaign QTE prompts | No controller-glyph coverage — real icon assets confirmed but a still-unexplained sizing bug surfaced on final retest and this was deliberately parked rather than ship a regression | #78 |
| Per-animation-step reload vibration | Deliberately deferred, committed final-scope item — real per-weapon timing data already found, still needs a genuine "reload is happening" trigger | #63 |
| Vehicle-exit prompts (e.g. tank exit in "Mind the Gap") | Not wired up yet | — |
| Survival debug menu / dev console | The game's real developer console is confirmed permanently disabled in this build — a custom replacement hasn't been built | — |
| Multiplayer (`iw5mp.exe`) | **Not started at all.** Separate binary, needs its own full reverse-engineering pass, and there's an open question about anti-cheat exposure that needs resolving first | — |
| `[Video] ForceD3D9On12` | **Removed entirely** (not just disabled, v0.3.5) — real-world evidence showed enabling it even once leaves the system in a bad driver-level state that outlives the setting being turned back off, causing crashes in later sessions even with it disabled | #92/#105 |
| In-mod FPS limiter | **Removed for good** (v0.3.5, second design) — a rebuilt version writing the game's own `com_maxfps` dvar was live-tested and confirmed to only cap menu framerate, not gameplay. Use an external limiter (RivaTuner Statistics Server) instead | #99 |

## Recently Resolved (v0.3.4/v0.3.5)

| Feature | What was fixed | Cite |
|---|---|---|
| **DualSense over Bluetooth** | Stick input used to be confirmed garbled/unusable on real hardware. Root cause found and fixed in v0.3.4: a real signed-16-bit integer overflow at full-forward stick deflection (the one input that could ever reach that exact extreme) — confirmed live by the reporting tester | #77 |
| Recurring freeze every 2-5 seconds | Fixed in v0.3.5 — root-caused to five independent causes (a fixed-rate poll thread, synchronous vibration writes, an uninstrumented config-hot-reload file check, a synchronous log flush, uncached per-frame text measurement), resolved by moving each onto its own dedicated background thread | #87 |
| `[Gyro]` section missing from a fresh config | Fixed in v0.3.5 — the section simply never appeared in `WriteDefaultConfig`'s template, even though the feature itself worked if the keys were hand-typed in | #76 |

## An Open Investigation

**A residual performance dip above 1080p resolution** remains after v0.3.5's own stutter/threading overhaul (which resolved the far larger, recurring 2-5 second freeze). Live-tested and confirmed clean at both 1080p and 4:3, but a genuine stutter appears at 1440p and above — not explained by this project's own resolution-scaling code (this project's one known scaling bug class needs a non-uniform aspect ratio, and 1440p is exactly 16:9 like 1080p). Leading theory is genuine GPU-side rendering cost this engine doesn't handle well above 1080p, not yet confirmed via GPU profiling. See #79/#87.

## A Note on Keyboard/Mouse

Keyboard/mouse is meant to remain fully functional and unaffected by this project. It's treated as a secondary-priority input path during testing (controller gets the most thorough verification), but it should never *break* — a real regression has happened before and was fixed (#10); if you notice one, please report it, this is taken seriously.

## Why Some Things Take a While

This project reverse-engineers the game from scratch — there's no documentation, no SDK, and no dormant "enable controller" switch to flip. Every working feature was found by decompiling the actual game binary, and every fix is verified against a real, live playtest before being called done. See [[Development Notes]] and [[Technical Documentation]] for more on the approach.

## See Also

- [[Compatibility]] — mission-by-mission and client-by-client compatibility
- [[Troubleshooting]] — if you're hitting something not listed here
- [Full internal RE trail on GitHub](https://github.com/k8se10/MW32011NCP/blob/main/re_notes/known_issues.md)
