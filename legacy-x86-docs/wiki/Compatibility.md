# Compatibility

> **⚠️ Only v0.2.2 (GitHub-only) and v0.3.2+ are currently distributed.** See [[Changelogs]] and the [README security notice](https://github.com/k8se10/MW32011NCP#readme) for the full versioning/archival policy.

## Client Compatibility

This project is built and verified only against **retail Steam MW3**. The table below reflects real, current status — most rows are "not yet investigated," not "confirmed working."

| Client | SP/MP | Status |
|---|---|---|
| **Retail Steam (Windows)** | Both | ✅ Actively supported — the current, only verified target |
| Retail Steam via Proton (Steam Deck/Linux) | SP/Survival | Multiple independent player reports of it working, including on real Steam Deck hardware — **not yet independently tested by this project itself**. Plausible given the architecture (a proxy DLL + standard Win32 calls, exactly what Proton translates), but treat as "works for some players," not "officially supported," until verified here. |
| Plutonium — Multiplayer | MP | ⚠️ **Not recommended — see warning below** |
| Plutonium — Singleplayer | SP | Uses a different binary than retail; not yet investigated |
| AlterWare IW5-Mod | SP + Spec Ops | Not yet investigated |
| DeckOps (MW3) | MP via Plutonium | Not yet investigated — inherits the Plutonium MP warning below |

> ### ⚠️ Do Not Use With Plutonium Multiplayer
>
> Plutonium's own anti-cheat is confirmed to ban **DLL injection and memory access** — a 7-day ban on first offense, permanent after. This project's entire architecture (a proxy `d3d9.dll`, function hooking, and memory reads such as the per-frame health poll used for damage rumble) is exactly what that system is built to catch, regardless of the project being input-only rather than a gameplay cheat. **This is a real, confirmed risk, not a theoretical one.** (Aim assist, this project's one feature that read *gameplay-entity* memory specifically, was permanently removed in v0.2.2 following this exact research — see [[Known Issues]].)

## Mode / Mission Compatibility

Tracked per-mission because support has turned out to be genuinely uneven — some missions/set-pieces work perfectly, others need a keyboard/mouse fallback at a specific point. A single "Campaign works" claim would hide that.

| Mode | Tested so far | Fully compatible | Partial (fallback needed at a specific point) | Not yet tested |
|---|---|---|---|---|
| Campaign (17 missions) | 7 | 3 | 4 | 10 |
| Special Ops (16 missions) | 0 | — | — | 16 |
| Survival | Tracked as one entry | Works well overall | 1 known issue (see [[Known Issues]]) | — |

Missions confirmed **fully working**: Persona Non Grata, Davis Family Vacation, Return to Sender.

Missions with a **specific, known fallback point** (not whole-mission failures): Hunter Killer (a DPV underwater segment's aiming), Turbulence (a scripted sequence where the player should be frozen but isn't — a known, understood bug), Goalpost (a mortar/turret sequence and a mounted-turret difficulty question — currently being re-investigated, see [[Known Issues]]), Mind the Gap (a vehicle-exit prompt not yet wired up).

Everything else in Campaign, all of Special Ops, and AC-130 sequences are simply **untested** — not known-broken, just never exercised yet during development.

## Controllers

Any XInput-compatible controller — see [[Controller Setup]] for detail.

## Operating Systems

Windows only for a native install. See the Proton/Steam Deck row above for the community-reported (unofficial) alternative.

## See Also

- [[Known Issues]]
- [[Installation Guide]]
