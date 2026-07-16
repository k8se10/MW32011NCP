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
- [ ] No hardcoded addresses — every hook target is found via signature scan at runtime
- [ ] Non-obvious findings (decompile, memdiff, live testing) are documented in `re_notes/iw5sp.md`
- [ ] Commit messages follow `[type]: [description]`
- [ ] If a new third-party library is introduced, its license is noted here and credited in `README.md`
