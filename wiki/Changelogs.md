# Changelogs

Condensed release history. Full, itemized detail lives in [`PATCHNOTES.md`](https://github.com/k8se10/MW32011NCP/blob/main/PATCHNOTES.md) in the main repo.

## v0.0.1-x64 — Unreleased

The first release on the `-x64` line, rebuilding this project from scratch against MW3's recompiled 64-bit binaries. The first real playtest happened 2026-09-14: every core gameplay control except D-pad actionslot/D-pad Left is now confirmed live, along with motion blur, main-menu navigation, and glyph-icon substitution. That same playtest found and closed several real, previously-undiscovered bugs, most notably an x64-only regression that silently disabled Fire/ADS/Reload/most other controls whenever the stick was centered. On direct instruction, every Campaign killstreak-type system and outstanding Campaign issue with a real history of being broken was also investigated from GSC script logic first — several fixed (DPV/Goalpost mortar/turret aiming, Campaign QTE button presses, cutscene-skip audio), one confirmed to have never been a bug at all (SMAW lock-on). The Custom Options screen's real vanilla-setting tabs remain a known, deliberately deferred gap. **This release has not shipped** — current estimate is within the next 14 days; see [[Home]] for the release gate.

## Prior line (`-x86`, discontinued)

MW3 (2011) received its first-ever binary update on 2026-09-03, recompiling the game from 32-bit to 64-bit — a hard architectural break for every hook the prior line had found. All support for the entire `-x86` release line (every version through `v0.3.5-x86`) was discontinued the same day, and every release was subsequently archived on both GitHub and Nexus. That line's full changelog history is preserved in this project's GitHub repository under `legacy-x86-docs/PATCHNOTES.md`.

## See Also

- [[Home]] — current status
- [[Known Issues]] — live, per-feature detail
