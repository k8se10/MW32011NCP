# MW3 2011 Native Controller Project

Welcome to the wiki for **MW32011NCP** — a from-scratch native controller project for **Call of Duty: Modern Warfare 3 (2011, IW5 engine)**, covering **Campaign and Survival**.

MW3 (2011) shipped on PC with **zero working controller input path** — no `xinput`/`dinput8` import anywhere in the binary, no hidden setting to unlock. This project doesn't fake keyboard/mouse input under a mapper tool; it hooks the game's own real engine functions directly, so analog movement, look, and most buttons are driven through the exact same internal calls a real keyboard/mouse player uses — just fed from a controller instead. See [[Technical Documentation|Technical-Documentation]] for how that actually works.

> **Status: ALPHA — v0.3.5 (2026-08-29).** Not feature-complete, not fully tested end-to-end. Multiplayer (`iw5mp.exe`) hasn't been started at all. Read **Feature Status** below before assuming anything works — this project is deliberately honest about what's confirmed vs. not.

> **⚠️ Only v0.2.2 (GitHub-only) and v0.3.2+ are currently distributed.** Versions v0.2.5 through v0.3.1.h1 have been unpublished (archived, not deleted — full git history remains) as of v0.3.3; v0.1.0-prealpha through v0.2.1 were unpublished earlier as a VAC-risk precaution. See [[Compatibility]] and the [README security notice](https://github.com/k8se10/MW32011NCP#readme) for the full reasoning.

## What's new in v0.3.5

The largest release yet by commit volume — two major bodies of work:

- **A new visual-enhancement suite**, all off by default: `InternalRenderScalePercent` (render above or below native resolution — real GPU cost scaling confirmed live, 220fps@100% vs. 70-80fps@300% at 2560x1440), FSR 1.0 RCAS sharpening, camera-only motion blur, and forced anisotropic filtering. See the before/after screenshots on the [README](https://github.com/k8se10/MW32011NCP#readme).
- **A full stutter/threading-architecture overhaul** — a recurring freeze every 2-5 seconds is fixed, root-caused to five independent causes (plus a self-inflicted regression from the poll-thread fix itself, caught in the same pass) and resolved by moving polling, vibration, config-hot-reload, and log-flushing onto four dedicated background threads.
- `ForceD3D9On12` and the in-mod FPS limiter are both **removed entirely** after real-world testing found them unsafe or non-functional.
- New `ForceHighQualityShadows`/`ForceHighQualityLighting` toggles (real, confirmed native dvars) and a `[Gyro]` config-generation bug fix.
- **Investigated, not shipped**: Campaign scripted sequences (QTEs) still ignore controller button presses entirely — real GSC evidence found, likely unifies with the long-open "Dust to Dust" elevator-mantle report.

Also carries forward from **v0.3.4**: DualSense's Bluetooth stick-garbling bug fixed (a real signed-16-bit integer overflow at full-forward stick deflection), the in-game glyph position editor (F2/F3) extended to real gameplay hints, auto-mantle while sprinting confirmed working, and a new opt-in plugin API (hook install, memory read/write, a text/glyph color-override extension point).

## Feature Status

Everything below is organized by real confidence level: ✅ has been personally live-tested during actual play, 🟡 works with a known gap, ⬜ doesn't work yet. If something isn't listed, treat it as untested.

### ✅ Fully working (live-confirmed)
- Analog movement (left stick) and analog look (right stick), independent sensitivity, no mouse-pipeline interference
- Fire, ADS (true hold-to-aim), Melee, Tactical/Lethal, Jump
- Crouch/Prone 3-state stance ladder (tap vs. hold)
- Interact vs. Reload on the same button (hold vs. tap, like console)
- Weapon switch, D-pad actionslots (killstreaks/attachments)
- **Sprint** — real native kbutton, engine's own stamina/recovery timer and Extreme Conditioning perk both apply automatically
- **Hold Breath** (L3 while ADS'd on a sniper) — steadies aim while held, real native kbutton
- Pause menu open/close, B-to-back-out-of-menus
- Full D-pad + A menu navigation — main menu, title screen, pause menu, options (including slider value adjustment), buy-stations
- Survival ready-up, 3 of 4 Survival killstreaks (Predator Missile launch, Precision Airstrike, AI squadmate call-in)
- Button/stick layout presets, including `TacticalLefty`
- Console-accurate look acceleration ramp
- **Controller-glyph icons** (in-game interact hints, menu corner hints, custom cursor) — real button icons instead of keyboard key names, now captured via an in-game F2/F3 drag-and-export editor covering dozens of real screens. A master-flag bug kept these off for everyone but the developer's own machine from v0.3.0 through v0.3.1.h1; root-caused and fixed in v0.3.2. Non-English language positioning root-caused and fixed in v0.3.1
- **Vibration/rumble** — fire and damage rumble both physically confirmed working (an earlier version's crash-at-startup issue was fixed by rebuilding both on safer hook targets)
- **DualSense Bluetooth stick input** — the stick-garbling bug (a real signed-16-bit integer overflow at full-forward deflection) is fixed and confirmed live by the reporting tester as of v0.3.4

### 🟡 Partial (works, with a known gap)
- Predator Missile's post-fire guidance (launch works, steering the missile still feels broken/overpowered — a real, disproportionate native auto-climb constant was found in v0.3.5, not yet connected to a fix)
- Back button (implemented, live-tested with zero visible effect — real cause undiagnosed, parked as a UI gap)
- DPV, Mortar, and mounted-turret sequences in specific missions — aim or fire input has a gap
- **Real in-game controller options menu** — a full from-scratch replacement screen exists (every real vanilla tab, real rebind capture) but ships as a **PREVIEW/WIP** feature: off by default, `mw3ncp_config.ini`-only, not yet played
- **Auto-mantle while sprinting** — confirmed working (v0.3.4) after a real coupling bug was found and fixed, but still ships **off by default** by design (opt-in)
- Highlighted-item A-glyph (menu list navigation) — the original verified-only allowlist gate was removed 2026-08-16; now draws on any screen with a manually-captured position (dozens covered). A handful of Campaign pause-menu popups were captured wrong in v0.3.5 and reverted, pending a reliable per-screen discriminator
- Vibration doesn't register hits absorbed by Survival's purchasable Body Armor; in 2-player Survival co-op, damage-rumble disables itself entirely rather than risk rumbling for a teammate's hit (a real fix needs more research into "which player is me")
- **USB DualSense support** — has never been independently confirmed working by a separate tester (the Bluetooth fix lives in shared code, so it almost certainly applies, but this specific gap remains open)
- **Native gyro-aim (DualSense)** — real, additive on top of right-stick look, confirmed working on real hardware by the first live tester (v0.3.2 preview → confirmed v0.3.4/v0.3.5), with a new gyro-only-while-ADS toggle in v0.3.5

### ⬜ Not working yet
- Aim assist — **permanently removed** (v0.2.2), not just disabled, following VAC-risk research; not coming back in its old form
- Multiplayer (`iw5mp.exe`) — not started at all
- Per-animation-step reload vibration — deliberately deferred, real timing data on record, not yet built
- Dog/hyena melee-struggle prompts (Survival) and Campaign QTE prompts have no controller-glyph coverage, and Campaign scripted sequences (QTEs) don't respond to controller button presses at all yet — real GSC evidence found (v0.3.5), leading fix candidate identified but not implemented

Full detail, always current: [README.md on GitHub](https://github.com/k8se10/MW32011NCP#readme).

## Wiki Navigation

- [[Installation Guide|Installation-Guide]] — how to install/uninstall, first launch, updating
- [[Controller Setup|Controller-Setup]] — the full control map and button/stick layout presets
- [[Configuration|Configuration]] — every `mw3ncp_config.ini` setting explained
- [[Compatibility|Compatibility]] — supported clients/platforms, missions tested so far, the Plutonium warning
- [[Troubleshooting|Troubleshooting]] — common problems and what to check
- [[Known Issues|Known-Issues]] — what's broken and why, in plain language
- [[FAQs|FAQs]] — quick answers to common questions
- [[Changelogs|Changelogs]] — release history
- [[Development Notes|Development-Notes]] — project stages, contributing, testing philosophy
- [[Technical Documentation|Technical-Documentation]] — architecture, how the hooking works, the RE approach

## ⚠️ Before You Install

**Do not use this project with Plutonium multiplayer.** Plutonium's anti-cheat bans DLL injection and memory access — a 7-day ban on first offense, permanent after. This project's proxy-DLL/hooking architecture is exactly what that system is built to catch, regardless of being input-only. See [[Compatibility]] for full detail.

This project supports **retail Steam Campaign and Survival only** right now.
