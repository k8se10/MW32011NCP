# Compatibility

> **⚠️ No release is currently distributed.** See [[Home]] for current status.

## Client Compatibility

This project is built and verified only against **retail Steam MW3**, current (post-recompile) version. The table below reflects real, current status — most rows are "not yet investigated," not "confirmed working."

| Client | SP/MP | Status |
|---|---|---|
| **Retail Steam (Windows)** | Both | ✅ Actively supported — the current, only verified target |
| Retail Steam via Proton (Steam Deck/Linux) | SP/Survival | Multiple independent player reports of it working on the prior 32-bit line, including on real Steam Deck hardware — **not yet independently tested on the current x64 line**. |
| Plutonium — Multiplayer | MP | ⚠️ **Not recommended — see warning below** |
| Plutonium — Singleplayer | SP | Uses a different binary than retail; not yet investigated |
| AlterWare IW5-Mod | SP + Spec Ops | Not yet investigated |
| DeckOps (MW3) | MP via Plutonium | Not yet investigated — inherits the Plutonium MP warning below |

> ### ⚠️ Do Not Use With Plutonium Multiplayer
>
> Plutonium's own anti-cheat is confirmed to ban **DLL injection and memory access** — a 7-day ban on first offense, permanent after. This project's entire architecture (a proxy `d3d9.dll`, function hooking) is exactly what that system is built to catch, regardless of the project being input-only rather than a gameplay cheat. **This is a real, confirmed risk, not a theoretical one.**

## Mode / Mission Compatibility

> **Reflects testing on the prior 32-bit line — not yet re-verified against the current x64 rebuild.** Kept here as a reference for what's historically been exercised; treat every row as needing fresh confirmation until [[Known Issues]] says otherwise.

Tracked per-mission because support has historically turned out to be genuinely uneven — some missions/set-pieces work perfectly, others need a keyboard/mouse fallback at a specific point. A single "Campaign works" claim would hide that.

| Mode | Tested so far (prior line) | Fully compatible | Partial (fallback needed at a specific point) | Not yet tested |
|---|---|---|---|---|
| Campaign (17 missions) | 7 | 3 | 4 | 10 |
| Special Ops (16 missions) | 0 | — | — | 16 |
| Survival | Tracked as one entry | Worked well overall | 1 known issue (see [[Known Issues]]) | — |

Missions confirmed **fully working** on the prior line: Persona Non Grata, Davis Family Vacation, Return to Sender.

Missions with a **specific, known fallback point** on the prior line (not whole-mission failures): Hunter Killer (a DPV underwater segment's aiming), Turbulence (a scripted sequence where the player should be frozen but isn't), Goalpost (a mortar/turret sequence), Mind the Gap (a vehicle-exit prompt not wired up).

Everything else in Campaign, all of Special Ops, and AC-130 sequences were simply **untested** on the prior line — not known-broken, just never exercised.

## Controllers

Any XInput-compatible controller — see [[Controller Setup]] for detail.

## Operating Systems

Windows only for a native install. See the Proton/Steam Deck row above for the community-reported (unofficial) alternative.

## See Also

- [[Known Issues]]
- [[Installation Guide]]
