# Controller Compatibility Matrix

Per-mission tracking of controller compatibility for Campaign and Special
Ops (each mission genuinely different, tracked individually rather than as
a single aggregate verdict); Survival is tracked as one overall entry
instead, since its controller support is map-independent (see the Survival
section below for why). Started 2026-07-18 after a live playtest session
through roughly half of Campaign surfaced 7 distinct bugs across
otherwise-mostly-working missions (see `known_issues.md` issue #27) — a
single aggregate status would have hidden that unevenness for Campaign
specifically. This file is the living source of truth for "what's actually
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
| 5 | Back on the Grid | 2 | `dubai.ff` (confirmed via content: Yuri/Makarov chase, elevator ambush, restaurant collapse, Dubai skyline) | ❓ | **CORRECTED 2026-07-18**: the mortar/turret bugs previously filed here were mission-misattributed — moved to Goalpost below (`hamburg.ff` is the real zone containing both, confirmed via direct entity/asset evidence). No separately-confirmed playtest data exists for this mission's own actual content (the Dubai chase/restaurant-collapse sequence) — resetting to untested rather than inventing a false "no issues" status. |
| 6 | Mind the Gap | 2 | `london`/`sp_london` | ⚠️ | Opening helicopter/aerial sequence: ✅ fine. Tank-exit prompt (`+usereload`, not plain `+activate`) didn't register on controller (bug #7, task #28). |
| 7 | Davis Family Vacation | 2 | not yet identified | ✅ | Short camcorder-cinematic sequence, minimal gameplay by design (comparable to "No Russian"). Played and fully fine on controller (2026-07-18 confirmation — user reported all missions 2-9 were perfect except those explicitly flagged with bugs). |
| 8 | Goalpost | 2 | `hamburg.ff` (confirmed via content: mortar impact-FX table, a real player-operable `misc_turret`/M1A1 minigun mount, `smaw_nolock`/T-90 tank assets all in the same zone) | ⚠️ | **CORRECTED 2026-07-18**: this mission's real zone was found to contain the mortar/turret bugs previously misfiled under "Back on the Grid" (mortar aim works, fire doesn't — bug #5, task #26; mounted turret felt harder on controller, cause unconfirmed — bug #6, task #27) — these are apparently EARLIER set-pieces in Goalpost, not a separate mission. **Not yet reconciled against the "played and fully fine" note this row previously had** (likely that note was based on the later tank/SMAW portion specifically, without realizing the earlier mortar/turret portion is the same mission — needs a live check to confirm). Also open: SMAW lock-on against an aircraft target may have failed (bug #8, task #29) — target may be non-targetable/scripted, needs a same-target keyboard comparison before status changes. |
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

**Per-map tracking NOT warranted here, per user direction (2026-07-18)** —
unlike Campaign (genuinely different scripted content per mission) and
Special Ops (16 distinct objective-based missions), Survival's controller
support is map-independent: the same input/engine hooks apply uniformly
regardless of which MP map Survival is played on, and live testing across
maps confirms this holds in practice. Tracked as a single overall entry
instead of 16 per-map rows.

| Mode | Status | Notes |
|---|---|---|
| Survival (all maps) | ⚠️ | Works well overall across maps tested. **Two known blockers to calling Survival fully complete (2026-07-18):** (1) Predator missile killstreak launch — see `known_issues.md` issue #10 (the game-breaking stuck-prone crash is RESOLVED) and issue #29/task #7 — the raw-usercmd-bit-vs-real-kbutton hypothesis was implemented and **live-tested as WRONG** (regular gunfire unaffected, but the missile still doesn't launch), so the real `notifyonplayercommand` native trigger point is still unfound; (2) Sprint's Extreme Conditioning perk override (task #9) — perk's real name confirmed (`specialty_longersprint`) but no native `HasPerk`-equivalent query exists, genuinely parked pending a GSC-side approach. |

**Real 16 MP map zone identifiers, for reference only** (not tracked
per-map above, per the direction this section now follows): `mp_alpha`,
`mp_bootleg`, `mp_bravo`, `mp_carbon`, `mp_dome`, `mp_exchange`,
`mp_hardhat`, `mp_interchange`, `mp_lambeth`, `mp_mogadishu`, `mp_paris`,
`mp_plaza2`, `mp_radar`, `mp_seatown`, `mp_underground`, `mp_village`. Not
all of these are necessarily Survival-eligible in this retail build (real
MW3 Survival launched on a subset, later expanded via DLC) — not
independently verified, kept here only as a reference list, not a claim
that all 16 support Survival.

---

## Maintenance

Update this file directly as testing continues — don't let findings
accumulate only in `known_issues.md` issue #27 without also being reflected
here. When a mission/map's status changes, update both the status symbol
and the Notes column with a one-line pointer to the relevant
`known_issues.md` issue/task number for anything beyond ✅.
