## What does this change?

<!-- Brief summary of the change and why. -->

## Live testing

<!--
Required for anything touching movement, look/aim, buttons, sprint, menu
navigation, or any other gameplay-facing behavior — a hook that "should work"
but hasn't been run against the actual game isn't done. See CONTRIBUTING.md.
-->

- **Binary tested:** iw5sp.exe / iw5mp.exe
- **Mode tested:** Campaign / Survival / Multiplayer
- **What I did:**
- **What I observed:**
- **Vanilla keyboard/mouse re-tested?** yes / no / N/A (only required if this change touches a shared code path)

## Checklist

- [ ] I opened an issue first for anything that wasn't a small, obvious fix (new hook target, `iw5mp.exe` work, new input-emulation exception)
- [ ] This meets [`CODE_STANDARDS.md`](../CODE_STANDARDS.md) — production-ready, live-verified (or honestly labeled as build-verified only), no placeholder/half-finished work (applies identically to AI-assisted code)
- [ ] Hook target resolved via runtime signature scanning (wildcarded byte pattern, resolved once at startup, cached) — not hardcoded, per `CODE_STANDARDS.md`
- [ ] Non-obvious findings (decompile, memdiff, live testing) are documented in `re_notes/known_issues_x64.md`
- [ ] Commit messages follow `[type]: [description]`
- [ ] If a new third-party library is introduced, its license is noted here and credited in `README.md`
