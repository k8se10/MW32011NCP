# Development Notes

## Development Overview

This is a from-scratch reverse-engineering project, not a config tweak. Every feature is implemented by hooking real engine functions found via runtime signature scanning and static analysis — there's no SDK, no documentation, and no dormant "enable controller" switch anywhere in the binary. See [[Technical Documentation]] for the architecture and [`re_notes/known_issues_x64.md`](https://github.com/k8se10/MW32011NCP/blob/main/re_notes/known_issues_x64.md) in the main repo for the complete, ongoing RE log.

The project is heavily developed with AI-assistant help — that's explicitly fine by this project's own standards, as long as every change is still verified live against the actual running game, not just confidently described. See [`CODE_STANDARDS.md`](https://github.com/k8se10/MW32011NCP/blob/main/CODE_STANDARDS.md) for the full production-readiness bar.

## Project Stages

The project uses a standard pre-alpha → alpha → beta → 1.0 progression, but "pre-alpha" here means something more specific than "barely started" — core systems were already confirmed working live during that stage on the prior 32-bit line, before MW3's own 2026-09-03 recompile to 64-bit forced a full rebuild.

| Stage | What it means here |
|---|---|
| **Pre-alpha** | Core systems land one at a time. |
| **Alpha** (current: `v0.0.1-x64`, rebuilding after the recompile) | Every core gameplay control has been re-implemented and build-verified on the new binaries, most confirmed live. The plugin API is ported; the custom Options screen is wired in with one real gap. Still ahead: the visual-enhancement suite, controller-glyph icons/hint prompts/cursor, native DualSense support, and Survival's ready-up — all of which existed on the prior line and need their own x64 port. See [[Known Issues]] for the live, current list. |
| **Beta** | Should be practically feature-complete — remaining work is closing gaps and extending reach, not building brand-new core systems. |
| **1.0 (final)** | Feature-complete against the project's full scope, stable, treated as a real release. |

See [[Changelogs]] for what's actually shipped release-by-release.

## Testing Philosophy

**Manual, live playtesting is the only thing that counts as "confirmed working."** This project is explicit about the distinction between "build-verified" and "verified live" — a change isn't done until it's actually been tested against the running game, through normal play, not just a single happy-path pass. This is why [[Known Issues]] separates confirmed-live from build-verified-but-unconfirmed from not-yet-implemented so precisely — the project deliberately avoids overclaiming.

`iw5sp.exe` (Campaign/Survival) and `iw5mp.exe` (Multiplayer) are treated as **entirely separate reverse-engineering efforts** — a fix verified in one is never assumed to carry over to the other, since they're separately-built binaries.

## Contributing

Contributions are welcome. Ground rules, condensed (full detail in [`CONTRIBUTING.md`](https://github.com/k8se10/MW32011NCP/blob/main/CONTRIBUTING.md)):

- Native RE only — hook targets found via runtime signature scanning, no config-tweak shortcuts.
- **Hook targets are resolved via runtime signature scanning, once at process startup and cached for the session** — not hardcoded. This became the current policy after a real binary update invalidated every hardcoded address the project had previously found; a hardcode-only approach cannot survive that, a resolve-once scanner does.
- Everything gets verified live before being called done.
- `iw5sp.exe` and `iw5mp.exe` are separate efforts — don't assume parity.
- Read [`CODE_STANDARDS.md`](https://github.com/k8se10/MW32011NCP/blob/main/CODE_STANDARDS.md) before writing any code.

## Future Plans

See [[Known Issues]] for the concrete, current list of what's next — the visual-enhancement suite, controller-glyph icons/hint prompts/the custom cursor, native DualSense support, Survival's ready-up, and eventually Multiplayer groundwork once the anti-cheat question is resolved. Aim assist is not on this list — it was permanently removed from the prior line, not paused, and that decision carries forward.
