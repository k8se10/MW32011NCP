# Known Issues

This is a plain-language, player-facing summary of the current `v0.0.1-x64` line's status, current as of the 2026-09-14 playtest. For the full internal reverse-engineering trail behind each item, see [`re_notes/known_issues_x64.md`](https://github.com/k8se10/MW32011NCP/blob/main/re_notes/known_issues_x64.md) in the main repo, issue #1.

## Confirmed working, live (2026-09-14 playtest)

Analog movement/look, Sprint (real kbutton), Fire, true hold-to-aim ADS, Reload, weapon switch, Melee, Lethal, Tactical, Jump (and auto-stand), Interact, the full Crouch/Prone stance ladder, pause menu open/close, native controller main-menu navigation, motion blur, internal render scale, Survival ready-up's mechanism, Hold Breath, Predator Missile launch, and Mantle glyph-icon substitution.

**A real x64-only regression was found and fixed the same day**: an early-return meant only to skip a no-op movement write instead silently disabled Fire/ADS/Reload/most other controls whenever the left stick was centered — exactly the moment a player stops to aim. This was the actual cause behind earlier "Fire/ADS randomly fails" reports, not weapon class.

## Build-verified, awaiting independent live confirmation

| Feature | Status |
|---|---|
| D-pad actionslot (all four directions) | Deployed, not yet independently exercised in a live test |
| D-pad Left squadmate-call-in | Synthesizes a real keypress instead of calling the native action-slot function directly — a leading fix, not yet independently confirmed |
| Plugin API | Loader and hook/memory-access surface ported; the bundled RGB Text example plugin has its own x64 build config |
| Custom Options screen (Controller/Custom-Binds tabs) | These two tabs work today (backed by this mod's own config); the other 7 tabs are a separate, deliberately deferred gap — see below |
| FSR sharpening, forced anisotropic filtering/shadow/lighting quality | Game runs without crashing with these enabled; not yet confirmed to produce their real visible effect |
| DualSense gyro-aim | Preview/WIP, needs real hardware to test |
| Remaining glyph-icon categories (Pickup/Swap/Pickup-health, Throwback, Reload/low-ammo, five menu corner hints) | Same underlying mechanism as the already-confirmed Mantle substitution |
| Custom mouse cursor overlay | A real gap (not showing at the true main menu) was found and fixed 2026-09-14 |
| DPV/Goalpost mortar/Goalpost M2 turret aiming | Never worked on either architecture before; a real shared root cause was found and fixed 2026-09-14 |
| Cutscene-skip audio, Campaign QTE/scripted-sequence button presses | Both fixed 2026-09-14 via real root-causing; awaiting live re-confirmation |

## Not yet implemented / genuinely open

| Feature | Why |
|---|---|
| Buy-station and Survival ready-up's own on-screen hint TEXT | The mechanism works (you can ready up / buy); the prompt itself still renders native/unmodified. Blocked on x64's genuinely unconfirmed native font-name offset — investigated directly, a real negative result, not a skipped step |
| Custom Options screen's real vanilla-setting data layer (7 of 9 tabs) | The UI shell opens/navigates/responds to clicks correctly (shared, architecture-neutral code), but every value shown is a stub and every edit is silently discarded. **Deliberately deferred** — the INI config (`mw3ncp_config.ini`) already covers everything this mod itself controls; this only matters for real vanilla game settings |
| Predator Missile's post-fire guidance (steering in flight) | Has never worked on **either** architecture — not a parity gap. Mapped further than ever this session; real (not conclusive) evidence it may already be partially fixed as a side effect of the ordinary look pipeline, but this couldn't be proven statically. A safe diagnostic hook ships instead of a guess |
| AC-130 gun-type switching (105mm/40mm/25mm) | Investigated in depth — confirmed entirely GSC/data-driven with no native dispatch case to hook; correctly left unfixed rather than guessed at against an already-working feature |
| FXAA, forced MSAA | Checked directly — neither was ever actually built even on the prior `-x86` line, only ever planned |
| Multiplayer (`iw5mp.exe`) | Separate binary, active static reverse engineering underway (not started from zero) — will ship opt-in only once it reaches live/injection work, given Valve Anti-Cheat's confirmed presence on that binary |

## Netcode security

This project's netcode-security research has identified real, network-reachable vulnerabilities in MW3 (2011)'s own base-game code — not in this mod — affecting Multiplayer and Spec-Ops/Survival co-op. These were re-confirmed present and unpatched in the game's most recent update, so updating the game does not fix them. Be cautious joining servers/lobbies or co-op sessions with people you don't trust; this risk exists independent of whether you use this mod. See the main repo's `security/README.md` for current fix status.

## A Note on Keyboard/Mouse

Keyboard/mouse is meant to remain fully functional and unaffected by this project. It's treated as a secondary-priority input path during testing (controller gets the most thorough verification), but it should never *break* — if you notice a regression, please report it, this is taken seriously.

## Why Some Things Take a While

This project reverse-engineers the game from scratch — there's no documentation, no SDK, and no dormant "enable controller" switch to flip. The game's own 2026-09-03 recompile from 32-bit to 64-bit invalidated every hook this project had found, so the current line is being rebuilt from that foundation. Every working feature is found by decompiling the actual game binary, and every fix is verified against a real, live playtest before being called done. See [[Development Notes]] and [[Technical Documentation]] for more on the approach.

## See Also

- [[Compatibility]] — mission-by-mission and client-by-client compatibility
- [[Troubleshooting]] — if you're hitting something not listed here
- [Full internal RE trail on GitHub](https://github.com/k8se10/MW32011NCP/blob/main/re_notes/known_issues_x64.md)
