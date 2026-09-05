# Changelogs

Condensed release history. For the complete, detailed changelog (every fix, every dead end, every commit-level change), see [`PATCHNOTES.md`](https://github.com/k8se10/MW32011NCP/blob/main/PATCHNOTES.md) in the main repo — this page is a summary only.

> **⚠️ Only v0.2.2 (GitHub-only) and v0.3.2+ are currently distributed.** Everything between v0.2.5 and v0.3.1.h1 has been unpublished as of v0.3.3 (archived, not deleted). See [[Compatibility]] and the README's security notice for the full reasoning.

## v0.3.5 — Alpha (2026-08-29) — Visual-enhancement suite, stutter/threading overhaul

The largest release yet by commit volume — two major bodies of work, not just a bug-fix pass. A new **visual-enhancement suite**, all off by default: `InternalRenderScalePercent` (render above or below native resolution — real GPU cost scaling confirmed live, 220fps@100% vs. 70-80fps@300% at 2560x1440), FSR 1.0 RCAS sharpening (a direct port of AMD's real reference math), camera-only motion blur, and forced anisotropic filtering. And a full **stutter/threading-architecture overhaul**: a recurring freeze every 2-5 seconds is fixed, root-caused to five independent causes (a fixed-rate poll thread, synchronous vibration writes, an uninstrumented config-hot-reload file check, a synchronous log flush, and uncached per-frame text measurement — plus a self-inflicted poll-flood regression from the poll-thread fix itself, caught in the same pass), resolved by moving polling, vibration, config-hot-reload, and log-flushing onto four dedicated background threads. `ForceD3D9On12` and the in-mod FPS limiter are both **removed entirely** after real-world testing found them unsafe or non-functional — see [[Known Issues]]. Also ships `ForceHighQualityShadows`/`ForceHighQualityLighting` (real, confirmed native shadow/lighting quality dvars) and a real `[Gyro]` config-generation bug fix (the section never appeared in a fresh `.ini`). A pre-release Campaign/Spec Ops glyph-coverage pass fixed the Campaign Act mission list; a related Campaign pause-menu popup batch was live-tested, found wrong, and reverted pending a proper per-screen discriminator. **Investigated, not shipped**: Campaign scripted sequences (QTEs) still ignore controller button presses entirely — real GSC evidence found, likely unifies with the long-open "Dust to Dust" elevator-mantle report.

## v0.3.4 — Alpha (2026-08-25) — DualSense input-parity fix, gameplay-hint glyph editor, new plugin API

DualSense's Bluetooth stick-garbling bug is fixed, confirmed live by the reporting tester — a real signed-16-bit integer overflow at full-forward stick deflection (the one input that could ever reach that exact extreme) was found and fixed. The fix lives in code shared with the USB path, so it almost certainly applies there too, but **USB DualSense input still has not been independently confirmed working by a separate tester**. The in-game glyph position editor (F2/F3) was extended from menu items to real gameplay hints (Interact/ReadyUp/Reload/Mantle), with several real bugs found and fixed along the way. Auto-mantle while sprinting is now confirmed working (a real coupling bug found and decoupled) — still ships off by default, opt-in by design. A glyph-icon rendering bug (a faint white cutout-fringe ring) was fixed. This mod also ships a new opt-in **plugin API** — hook installation, direct process memory read/write, a text/glyph color-override extension point — off by default, with a working RGB Text example plugin. **Investigated, not shipped**: controller-glyph coverage for QTE prompts (Campaign scripted sequences, Survival's dog/hyena melee-struggle) was pursued in depth but deliberately parked after a still-unexplained sizing bug surfaced on final retest.

## v0.3.3 — Alpha (2026-08-18) — Extended-session stutter fixed; vibration and co-op rumble fixes; DualSense Bluetooth preview (broken)

Three separate, evidence-backed causes of real extended-session performance stuttering found and fixed: `proxy_d3d9.log` was never trimmed between launches (grew past 2GB across a session history), the controller-poll thread never stopped rescanning every device every ~4ms tick, and a diagnostic log's dedup logic didn't actually deduplicate (producing 400K+ lines in one session). User-confirmed "much better" after an A/B test against v0.2.2. Also fixes vibration getting stuck on across a pause/menu, and a real co-op bug where damage-rumble could trigger off a teammate's hits instead of your own (mitigated, not fully root-caused). Ships a first PREVIEW/WIP pass at DualSense Bluetooth support — **stick input is confirmed broken over Bluetooth**; USB DualSense still hasn't been independently confirmed working either. One residual stutter (looking toward enemies, likely GPU-side) remains open — see [[Known Issues]].

## v0.3.2 — Alpha (2026-08-16) — Controller-glyph icons actually work now; native DualSense/gyro preview

Root-caused the single biggest recurring bug report this project has ever received: controller-glyph icons never drew for anyone but the developer's own dev-machine build, because the overlay's own master enable flag had shipped hardcoded off in every release since v0.3.0. Fixed and confirmed live; the flag is removed from the config schema entirely so no one can stay stuck on it. Also ships a first, PREVIEW/WIP pass at native DualSense (PS5 controller) support via a new raw-HID backend that bypasses Steam Input entirely, plus a first-pass gyro-aim implementation — both off by default and, at the time of this release, not yet live-tested against real hardware.

## v0.3.1.h1 — Hotfix (2026-08-09) — Mouse-lag regression, non-16:9 scaling/glyph-visibility, Options-screen crash

Feature-free hotfix. A critical mouse-movement-correlated FPS drop, confirmed live and fixed. Controller-glyph icons never appearing at all on any non-16:9 resolution, root-caused across two separate bug classes (a font-detection gap and a position-formula bug) and fixed. Also a real crash-risk fix in the still-preview/WIP Options screen and a log-volume fix for long sessions.

## v0.3.1 — Alpha (2026-08-06) — Multi-language glyph fix + Options screen preview

Controller-glyph icons were silently broken for every non-English game language — root-caused against the game's own real localization resolver and fixed, confirmed live. Also ships a from-scratch custom Options screen replacement covering every real vanilla tab plus real keybind rebinding — real, working code, but shipped as an explicit **PREVIEW/WIP** feature (off by default, ini-only) since it hadn't been played yet.

## v0.3.0 — Alpha (2026-08-03) — Controller-glyph icons, vibration, auto-mantle (disabled)

The most significant release to date by code volume. A brand-new vibration subsystem built from zero, controller-glyph icons for real in-game interact hints and menu corner hints (the longest-running single bug investigation in this project's history), and a full crouch/UI bug batch from the first public Survival co-op stream. Auto-mantle while sprinting is implemented but was **not working** at this point and shipped disabled.

## v0.2.5 — Alpha (2026-07-31) — Hotfix: crouch/stance reliability + on-screen notifications

Hotfix release (renumbered from v0.2.3 the same day). Crouch/prone intermittently failing to fire is fully fixed, and a critical regression (couldn't fire while holding breath on a sniper) is fixed — both user-confirmed live. Also ships this project's first working per-frame render capability, powering new startup/config-hot-reload on-screen notifications, split horizontal/vertical look sensitivity, and config auto-migration across future updates.

## v0.2.2 — Alpha (2026-07-20) — Risk-mitigation release

Aim assist (rotational friction + magnetism, reading live entity/target memory) is **permanently removed**, not just disabled, following deep VAC/anti-cheat research — this project's own core input-remapping work only ever writes real input values into real input structures and never reads gameplay-entity memory; aim assist did, putting it in a materially riskier category regardless of intent. No other player-facing feature changes beyond v0.2.1. This is this project's first LTS-style stabilization release and remains the only version prior to v0.3.2 still publicly available (GitHub-only).

## v0.2.1 — Alpha (2026-07-20)

Two headline items, both confirmed live:
- **Look acceleration ramp** — controller look now ramps up over 33ms (one 30fps engine frame) after the stick leaves neutral, matching real console MW2/Black Ops behavior, instead of responding instantly.
- **Hold Breath** (L3 while ADS'd on a sniper) is now fully working as genuinely native input, after an extensive debugging pass that traced the bug down to a single stuck byte in the game's own internal state.

## v0.2.0 — Alpha (2026-07-19)

The project's first release tagged as a real milestone rather than incremental groundwork.
- **Sprint** migrated onto its real native kbutton — this made an entire custom stamina/cooldown timer layer unnecessary (removed) and resolved the Extreme Conditioning perk override for free.
- **3 of Survival's 4 real killstreaks** confirmed working (Predator Missile launch, Precision Airstrike, AI squadmate call-in).
- Real D-pad/A menu navigation extended to the main menu and title screen, not just in-game menus.
- A first vibration/rumble implementation was built, found to crash the game at startup, and disabled rather than shipped broken.
- A fully proven, implementation-ready button-glyph icon pipeline (not yet wired into rendering).

## v0.1.3 (2026-07-17)

The biggest research release so far, alongside one real shipped feature:
- **Real native D-pad/A menu navigation** confirmed working live across the main menu, pause menu, and options screens.
- Deep groundwork on a real controller-options-menu injection mechanism, an aim-assist classification fix (not yet live-verified), real vibration trigger points, and an MW3-client-compatibility survey that surfaced the Plutonium anti-cheat risk.

## v0.1.2 (2026-07-16)

Mostly a documentation-accuracy release — several already-shipped features had never been written up. One small non-functional INI comment fix.

## v0.1.1 (2026-07-16)

- Fixed a real regression where controller Sprint hooks were silently breaking vanilla keyboard Shift-to-sprint.
- Fixed ADS look-slowdown inverting look direction at high strength on deep zooms (switched from a linear blend to a power curve).

## v0.1.0-prealpha (2026-07-15)

Initial pre-alpha release. Analog movement, look, and most core buttons (Fire, ADS, Melee, Tactical/Lethal, Jump, Crouch/Prone, Interact/Reload, Sprint, weapon switch, D-pad actionslots, pause menu, Survival ready-up) confirmed working live against `iw5sp.exe`.

## See Also

- [Full PATCHNOTES.md on GitHub](https://github.com/k8se10/MW32011NCP/blob/main/PATCHNOTES.md)
- [GitHub Releases](https://github.com/k8se10/MW32011NCP/releases)
- [[Known Issues]]
