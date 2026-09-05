# Development Notes

## Development Overview

This is a from-scratch reverse-engineering project, not a config tweak. Every feature is implemented by hooking real engine functions found via signature scanning and static analysis — there's no SDK, no documentation, and no dormant "enable controller" switch anywhere in the binary. See [[Technical Documentation]] for the architecture and [`re_notes/iw5sp.md`](https://github.com/k8se10/MW32011NCP/blob/main/re_notes/iw5sp.md) in the main repo for the complete, ongoing RE log.

The project is heavily developed with AI-assistant help — that's explicitly fine by this project's own standards, as long as every change is still verified live against the actual running game, not just confidently described. See [`CODE_STANDARDS.md`](https://github.com/k8se10/MW32011NCP/blob/main/CODE_STANDARDS.md) for the full production-readiness bar.

## Project Stages

The project uses a standard pre-alpha → alpha → beta → 1.0 progression, but "pre-alpha" here means something more specific than "barely started" — core systems (analog movement/look, real engine-state-driven Sprint and stance, the real pause menu, real menu navigation) were already confirmed working live during that stage.

| Stage | What it means here |
|---|---|
| **Pre-alpha** (`0.1.0`–`0.1.5`) | Core systems land one at a time — movement/look/combat, stance/sprint, pause menu, and menu navigation done; aim assist, vibration, killstreaks, and the controller options menu still being built out. |
| **Alpha** (`0.1.5`–`0.4.0`, current: **v0.3.5**) | The remaining major systems get built and land. **v0.2.0–v0.2.1**: Sprint fully native, 3 of 4 Survival killstreaks, menu/UI nav extended, look acceleration ramp, Hold Breath. **v0.2.2**: aim assist **permanently removed** following VAC research — first LTS-style stabilization release. **v0.2.5**: crouch/stance reliability hotfix, on-screen notifications. **v0.3.0**: controller-glyph icons, real vibration, auto-mantle attempted (shipped disabled, not working yet). **v0.3.1**: multi-language glyph fix, a from-scratch Options screen shipped as PREVIEW/WIP. **v0.3.1.h1**: hotfix (mouse-lag regression, non-16:9 glyph/scaling fixes, Options-screen crash fix). **v0.3.2**: root-caused why glyphs never actually drew for most players since v0.3.0 (a hardcoded-off master flag) — fixed; first PREVIEW/WIP native DualSense/gyro pass. **v0.3.3**: real extended-session stutter fixed (three evidence-backed causes), vibration/co-op-rumble fixes, DualSense Bluetooth preview (confirmed broken at the time). **v0.3.4**: DualSense Bluetooth stick-garbling bug fixed and confirmed live (a real signed-16-bit integer overflow); auto-mantle confirmed working; the in-game glyph editor extended to real gameplay hints; new opt-in plugin API. **v0.3.5** (largest release yet by commit volume): a new visual-enhancement suite (render scale, FSR sharpening, motion blur, anisotropic filtering) and a full stutter/threading overhaul (a recurring 2-5 second freeze fixed via five root causes across four dedicated background threads); `ForceD3D9On12` and the in-mod FPS limiter both removed entirely after being found unsafe/non-functional. **Still ahead in this stage**: playtesting the preview Options screen, closing a handful of Campaign pause-menu popup glyph gaps, Survival Body Armor/reload vibration, Predator Missile's guidance feel, Campaign scripted-sequence (QTE) controller input, USB DualSense's still-unconfirmed independent test, and remaining Campaign/Special Ops compatibility gaps. Multiplayer groundwork may start here, pending the anti-cheat question. |
| **Beta** (`0.4.0`–`1.0.0`) | Should be practically feature-complete — remaining work is closing gaps and extending reach, not building brand-new core systems. |
| **1.0 (final)** | Feature-complete against the project's full scope, stable, treated as a real release. |

See [[Changelogs]] for what's actually shipped release-by-release.

## Testing Philosophy

**Manual, live playtesting is the only thing that counts as "confirmed working."** This project is explicit about the distinction between "builds clean" and "verified live" — a change isn't done until it's actually been tested against the running game, through normal play, not just a single happy-path pass. This is why [[Known Issues]] and the main README's status tables separate "✅ fully working (live-confirmed)" from "🟡 partial" and "⬜ not implemented" so precisely — the project deliberately avoids overclaiming.

`iw5sp.exe` (Campaign/Survival) and `iw5mp.exe` (Multiplayer) are treated as **entirely separate reverse-engineering efforts** — a fix verified in one is never assumed to carry over to the other, since they're separately-built binaries.

## Contributing

Contributions are welcome. Ground rules, condensed (full detail in [`CONTRIBUTING.md`](https://github.com/k8se10/MW32011NCP/blob/main/CONTRIBUTING.md)):

- Native RE only — hook targets found via static analysis (Ghidra), no config-tweak shortcuts.
- **Hardcoded addresses, resolved once per binary version via static analysis, are this project's deliberate, permanent policy** — not a stopgap on the way to a runtime scanner. A runtime signature scanner has to search the game's own process memory every time it resolves, which is closer to what anti-cheat heuristics watch for than a fixed address found offline (no live process attached) ever is. Always document how an address was found.
- Everything gets verified live before being called done.
- `iw5sp.exe` and `iw5mp.exe` are separate efforts — don't assume parity.
- Read [`CODE_STANDARDS.md`](https://github.com/k8se10/MW32011NCP/blob/main/CODE_STANDARDS.md) before writing any code.

## Future Plans

See [[Known Issues]] for the concrete list of what's next — playtesting the preview Options screen, an independent USB DualSense confirmation, closing a handful of Campaign pause-menu popup glyph gaps, Campaign scripted-sequence (QTE) controller input, Survival Body Armor vibration, per-animation-step reload vibration, Predator Missile's guidance feel, and eventually Multiplayer groundwork once the anti-cheat question is resolved. Aim assist is not on this list — it was permanently removed in v0.2.2, not paused.
