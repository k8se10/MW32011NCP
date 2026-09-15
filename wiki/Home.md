# MW32011NCP — Native Community Patches for MW3 (2011)

Welcome to the wiki for **MW32011NCP** — a native, from-scratch reverse-engineering platform for **Call of Duty: Modern Warfare 3 (2011, IW5 engine)**, covering **Campaign and Survival** (Multiplayer is under active, separate development — see below). Not a single mod: a patch layer with four real, distinct components — real controller support (the flagship, most mature piece), a growing suite of visual/performance enhancements, native netcode security patches for the base game's own real vulnerabilities, and Multiplayer support.

MW3 (2011) shipped on PC with **zero working controller input path** — no `xinput`/`dinput8` import anywhere in the binary, no hidden setting to unlock. This project doesn't fake keyboard/mouse input under a mapper tool; it hooks the game's own real engine functions directly, so analog movement, look, and most buttons are driven through the exact same internal calls a real keyboard/mouse player uses — just fed from a controller instead. See [[Technical Documentation]] for how that actually works.

**This is the same project as before, redefined, not replaced.** Through 2026-09-03 this project was "MW32011NCP — Native Controller (and Enhancement) Project." On 2026-09-12 it was redefined again to **Native Community Patches** — name and repo unchanged, scope formalized to match what it had already organically become, with the sibling `MW32011NSP` netcode-security project folding in as this repo's own `security/` component the same day. Controller support remains the flagship, first-shipped patch — it just isn't the whole of what this project does anymore.

> **Status: ALPHA, `v0.0.1-x64` line.** On 2026-09-03 MW3 received its first real binary update in the game's history, recompiling the game from 32-bit to 64-bit — a hard architectural break that invalidated every hook the prior line had. The old 32-bit line is fully discontinued; this wiki, like the rest of the project, now describes the 64-bit rebuild. **No release is currently available — current estimate: within the next 14 days**, following the first real playtest of this build.

## Current progress

**2026-09-14: the first real playtest of this build happened.** Every core control except D-pad actionslot/D-pad Left is now confirmed live, along with motion blur, main-menu navigation, and glyph-icon substitution. Several real, previously-undiscovered bugs were found and fixed the same day — most notably an x64-only regression where a movement-tick early-return meant to skip a no-op write instead silently disabled Fire/ADS/Reload/most other controls whenever the stick was centered (exactly when a player stops to aim).

- **Every core gameplay control implemented, most confirmed live**: analog movement/look, Sprint (real kbutton), Fire, true hold-to-aim ADS, Reload, weapon switch, Melee, Lethal, Tactical, Jump, Interact, the full Crouch/Prone stance ladder, pause menu open/close, and native D-pad+A menu/UI navigation (main menu confirmed; pause/options/buy-stations not yet separately exercised).
- **The visual-enhancement suite is live**: internal render scale and motion blur are both live-confirmed; FSR sharpening, forced anisotropic filtering, and forced shadow/lighting quality are build-verified.
- **The netcode security component ships built-in by default** — see [[Known Issues]] and the main repo's `security/README.md` for the vulnerability-fix status.
- **Glyph-icon substitution now covers nine hint categories** (Mantle, Pickup/Swap/Pickup-health, Throwback, Reload/low-ammo, five menu corner hints) — Mantle confirmed visible live; the rest build-verified.

**Not yet on this line**: buy-station/ready-up/turret-placement hint text (blocked on an unresolved native font-name offset), the Custom Options screen's real vanilla-setting tabs (deliberately deferred — the INI config already covers everything this mod needs), and Multiplayer (`iw5mp.exe`, active static reverse engineering, not yet live/injection work). See [[Known Issues]] for the complete, live-updated status.

**Release gate**: no `-x64` release ships until Campaign/Survival controller support reaches the same feature completeness the `-x86` line reached before being discontinued — every control and the visual-enhancement suite working, not just the input-remapping core.

## Feature Status

Everything below is organized by real confidence level. If something isn't listed, treat it as untested.

### ✅ Confirmed live (direct playtest, 2026-09-14)
- Analog movement, analog look, Sprint, Fire, true hold-to-aim ADS, Reload
- Melee, Lethal, Tactical, Jump, Interact, weapon switch (Y)
- Crouch/Prone (tap vs. hold), pause menu open/close, auto-unstick
- Survival ready-up (mechanism; the prompt itself still renders native), Hold Breath, Predator Missile launch
- Motion blur, internal render scale, native controller main-menu navigation
- Mantle glyph-icon substitution, highlighted-item A-glyph, Auto-Mantle, Back's `+scores`, ADS zoom-aware look-slowdown

### 🟡 Build-verified, awaiting independent live confirmation
- D-pad actionslot (all four directions), D-pad Left's squadmate-call-in fix
- Plugin API (loader, hook/memory access), Custom Options screen's Controller/Custom-Binds tabs
- Predator Missile's post-fire guidance (launch is live; guidance remains genuinely open)
- AC-130 zoom-aware look sensitivity (fixed, not yet live-tested); gun-type switching (investigated, genuinely open)
- DualSense gyro-aim (preview/WIP), FSR sharpening, remaining glyph-icon categories, custom mouse cursor overlay
- DPV/Goalpost mortar/Goalpost M2 turret aiming (real shared fix, not yet live-tested)
- Cutscene-skip audio fix, Campaign QTE/scripted-sequence button presses (both fixed 2026-09-14, not yet live-tested)

### ⬜ Not yet implemented / genuinely open
- Buy-station and Survival ready-up's own on-screen hint TEXT (mechanism works; text still renders native)
- Custom Options screen's real vanilla-setting data layer (7 of 9 tabs; deliberately deferred, not a release blocker)
- Multiplayer (`iw5mp.exe`) — active static RE, no live/injection work yet, will ship opt-in only
- FXAA / forced MSAA (never built even on the old `-x86` line)

## Wiki Pages

- [[Installation Guide]] — how to install (once a release exists)
- [[Controller Setup]] — the full control map
- [[Configuration]] — every tunable setting
- [[Compatibility]] — client and mission-by-mission compatibility
- [[Known Issues]] — the honest, current breakdown of what works
- [[Troubleshooting]] — common problems and fixes
- [[FAQs]] — frequently asked questions
- [[Technical Documentation]] — how the project actually works under the hood
- [[Development Notes]] — project stages, testing philosophy, contributing
- [[Changelogs]] — condensed release history

## Links

- [GitHub Repository](https://github.com/k8se10/MW32011NCP)
- [Report an Issue](https://github.com/k8se10/MW32011NCP/issues)
- [Support on Ko-fi](https://ko-fi.com/officialk8)
