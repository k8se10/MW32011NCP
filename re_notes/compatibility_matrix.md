# Controller Compatibility Matrix

Per-mission/per-map tracking of controller compatibility, tracked individually
rather than as a single "Campaign works" / "Spec Ops works" verdict. Started
2026-07-18 after a live playtest session through roughly half of Campaign
surfaced 7 distinct bugs across otherwise-mostly-working missions (see
`known_issues.md` issue #27) — a single aggregate status would have hidden
that unevenness. This file is the living source of truth for "what's actually
been tested and how it went"; `known_issues.md` stays the place for the
technical RE trail behind each individual bug.

**Mission lists below are sourced from public mission-list/wiki research
(cross-checked across multiple consistent sources), not yet independently
verified against this project's own GSC/zone asset extraction.** The
game's real internal zone names (confirmed directly from
`zone/english/*.ff` on this install) are noted where a mapping is already
solid from this session's own work; the rest are not yet cross-referenced
to a specific zone file — a real, open follow-up task, not a blocker for
using this matrix.

## Compatibility levels

| Symbol | Meaning |
|---|---|
| ✅ | **Fully compatible** — tested live, fully playable on controller, no keyboard/mouse fallback needed anywhere in the mission |
| ⚠️ | **Partial** — tested live, playable overall but requires fallback at specific, identified point(s). Cross-referenced to the exact `known_issues.md` bug / task number |
| ❌ | **Broken** — tested live, largely or entirely unplayable on controller |
| ❓ | **Not yet tested** — no live data yet |

---

## Campaign

| # | Mission | Act | Zone (if confirmed) | Status | Notes |
|---|---|---|---|---|---|
| 1 | Black Tuesday | 1 | `intro`/`sp_intro` (likely — not independently confirmed) | ❓ | Prologue/flashback. Not yet tested this session. |
| 2 | Hunter Killer | 1 | `ny_harbor`/`sp_ny_harbor` (likely — not independently confirmed) | ⚠️ | DPV sequence: movement works, aiming doesn't (bug #1). Boat sequence in the same mission: ✅ fully working. |
| 3 | Persona Non Grata | 1 | `ny_manhattan`/`sp_ny_manhattan` (likely — not independently confirmed) | ✅ | UGV (minigun + grenade launcher) worked perfectly. |
| 4 | Turbulence | 1 | `hijack`/`sp_hijack` (fairly confident — matches Spec Ops zone `so_milehigh_hijack`) | ⚠️ | Movement hook bypasses the scripted player-freeze during the plane-breaking-apart sequence (bug #4, task #25). Potentially systemic beyond just this mission — see task #25. |
| 5 | Back on the Grid | 2 | `warlord`/`sp_warlord`? (unconfirmed — needs re-check, "warlord" may instead map to Return to Sender given Waraabe is "the Warlord") | ⚠️ | Two issues: mortar aim works, fire doesn't (bug #5, task #26); mounted turret sequence felt harder on controller, cause unconfirmed (bug #6, task #27 — deep-investigation flagged). |
| 6 | Mind the Gap | 2 | `london`/`sp_london` | ⚠️ | Opening helicopter/aerial sequence: ✅ fine. Tank-exit prompt (`+usereload`, not plain `+activate`) didn't register on controller (bug #7, task #28). |
| 7 | Davis Family Vacation | 2 | not yet identified | ✅ | Short camcorder-cinematic sequence, minimal gameplay by design (comparable to "No Russian"). Played and fully fine on controller (2026-07-18 confirmation — user reported all missions 2-9 were perfect except those explicitly flagged with bugs). |
| 8 | Goalpost | 2 | not yet identified | ✅ | Played and fully fine on controller (2026-07-18 confirmation — see note on #7). **Open question, not yet confirmed either way (bug #8, task #29)**: SMAW may have failed to lock onto an aircraft target — but the target may also be a non-targetable scripted entity, which would make this a non-issue rather than a controller-specific bug. Needs a same-target keyboard comparison before status changes. |
| 9 | Return to Sender | 2 | `warlord`? (see note on #5 — needs disambiguation) | ✅ | Helicopter door-gun/Remote Turret strafing runs worked fully. |
| 10 | Bag and Drag | 2 | not yet identified | ❓ | Playtest session paused here — not yet tested, this is where testing will resume. |
| 11 | Iron Lady | 2 | not yet identified | ❓ | Not yet tested. |
| 12 | Eye of the Storm | 2 | not yet identified | ❓ | Not yet tested. |
| 13 | Blood Brothers | 2 | `berlin`/`sp_berlin`? (unconfirmed) | ❓ | Not yet tested. |
| 14 | Stronghold | 3 | not yet identified | ❓ | Not yet tested. |
| 15 | Scorched Earth | 3 | not yet identified | ❓ | Not yet tested. |
| 16 | Down the Rabbit Hole | 3 | not yet identified | ❓ | Not yet tested. |
| 17 | Dust to Dust (final mission) | 3 | not yet identified | ❓ | Not yet tested. |

**Cross-cutting issue, not mission-specific**: Crouch intermittently fails to
fire (~2% of attempts), recovers after pause/unpause (bug #2, no task yet —
needs live diagnostic logging first). Observed during this session's
playthrough but not tied to any one mission; assume present everywhere
until diagnosed otherwise.

**Also cross-cutting, feature gap not a regression**: Hold Breath on sniper
ADS was never implemented, and its absence causes L3 to wrongly force-stand
the player while crouched + ADS with a sniper (bug #3, task #24) — relevant
to any mission with sniper gameplay, not just where first noticed.

## Special Ops

**16 real internal zone identifiers confirmed directly from this install's
`zone/english/*.ff`** (authoritative — these are not web-sourced):
`so_assassin_payback`, `so_assault_rescue_2`, `so_deltacamp`,
`so_heliswitch_berlin`, `so_ied_berlin`, `so_jeep_paris_b`,
`so_killspree_paris_a`, `so_littlebird_payback`, `so_milehigh_hijack`,
`so_nyse_ny_manhattan`, `so_rescue_hijack`, `so_stealth_prague`,
`so_stealth_warlord`, `so_timetrial_london`, `so_trainer2_so_deltacamp`,
`so_zodiac2_ny_harbor`.

**Real in-game display names for these 16 not yet confidently mapped** —
web research this session found a partial, explicitly-uncertain list (tier
structure suggested but not confirmed complete). Using the real zone names
as row identifiers below until display names are independently confirmed,
rather than presenting an unverified name mapping as fact.

| Zone identifier | Status | Notes |
|---|---|---|
| `so_assassin_payback` | ❓ | Not yet tested. |
| `so_assault_rescue_2` | ❓ | Not yet tested. |
| `so_deltacamp` | ❓ | Not yet tested. |
| `so_heliswitch_berlin` | ❓ | Not yet tested. |
| `so_ied_berlin` | ❓ | Not yet tested. |
| `so_jeep_paris_b` | ❓ | Not yet tested. |
| `so_killspree_paris_a` | ❓ | Not yet tested. |
| `so_littlebird_payback` | ❓ | Not yet tested. |
| `so_milehigh_hijack` | ❓ | Not yet tested. Likely display name "Mile High Jack" per partial web research — not confirmed. |
| `so_nyse_ny_manhattan` | ❓ | Not yet tested. |
| `so_rescue_hijack` | ❓ | Not yet tested. |
| `so_stealth_prague` | ❓ | Not yet tested. |
| `so_stealth_warlord` | ❓ | Not yet tested. |
| `so_timetrial_london` | ❓ | Not yet tested. |
| `so_trainer2_so_deltacamp` | ❓ | Not yet tested. |
| `so_zodiac2_ny_harbor` | ❓ | Not yet tested. |

**Open follow-up**: independently confirm real display names for all 16
(via GSC/localization asset extraction rather than web research, matching
this project's own "trust native RE over dumps" standard) before this
table is considered fully authoritative for anything beyond test tracking.

## Survival

**16 real MP map zone identifiers confirmed directly from this install**
(Survival mode reuses multiplayer maps in MW3): `mp_alpha`, `mp_bootleg`,
`mp_bravo`, `mp_carbon`, `mp_dome`, `mp_exchange`, `mp_hardhat`,
`mp_interchange`, `mp_lambeth`, `mp_mogadishu`, `mp_paris`, `mp_plaza2`,
`mp_radar`, `mp_seatown`, `mp_underground`, `mp_village`.

**Not yet confirmed which of these actually support Survival mode in this
specific retail build** — real MW3 Survival launched on a subset of maps,
later expanded via DLC; this project hasn't independently verified which
subset applies here. Listing all 16 as candidates, not asserting all are
Survival-eligible.

| Map | Status | Notes |
|---|---|---|
| `mp_alpha` | ❓ | Not yet tested. Survival-eligibility not confirmed. |
| `mp_bootleg` | ❓ | Not yet tested. Survival-eligibility not confirmed. |
| `mp_bravo` | ❓ | Not yet tested. Survival-eligibility not confirmed. |
| `mp_carbon` | ❓ | Not yet tested. Survival-eligibility not confirmed. |
| `mp_dome` | ❓ | Not yet tested. Survival-eligibility not confirmed. |
| `mp_exchange` | ❓ | Not yet tested. Survival-eligibility not confirmed. |
| `mp_hardhat` | ❓ | Not yet tested. Survival-eligibility not confirmed. |
| `mp_interchange` | ❓ | Not yet tested. Survival-eligibility not confirmed. |
| `mp_lambeth` | ❓ | Not yet tested. Survival-eligibility not confirmed. |
| `mp_mogadishu` | ❓ | Not yet tested. Survival-eligibility not confirmed. |
| `mp_paris` | ❓ | Not yet tested. Survival-eligibility not confirmed. |
| `mp_plaza2` | ❓ | Not yet tested. Survival-eligibility not confirmed. |
| `mp_radar` | ❓ | Not yet tested. Survival-eligibility not confirmed. |
| `mp_seatown` | ❓ | Not yet tested. Survival-eligibility not confirmed. |
| `mp_underground` | ❓ | Not yet tested. Survival-eligibility not confirmed. |
| `mp_village` | ❓ | Not yet tested. Survival-eligibility not confirmed. |

**Note**: this project's existing Survival work (ready-up, sprint
stamina/cooldown, D-pad killstreak call-ins, etc. — see `known_issues.md`
issues #5/#6/#13/#14) was developed and verified live, but not tracked
per-map until now. Those confirmed-working mechanics almost certainly
transfer across maps (they're map-independent input/engine hooks, not
map-specific content), but this table tracks actual per-map playtest
coverage, which is a different, currently-empty question.

---

## Maintenance

Update this file directly as testing continues — don't let findings
accumulate only in `known_issues.md` issue #27 without also being reflected
here. When a mission/map's status changes, update both the status symbol
and the Notes column with a one-line pointer to the relevant
`known_issues.md` issue/task number for anything beyond ✅.
