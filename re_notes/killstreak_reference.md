# Killstreak Reference

Two different kinds of content in this file, kept clearly separated:

1. **Campaign killstreak-type weapon systems (below)** — real,
   first-party controller-support status from this project's own live
   playtesting (`known_issues.md` issue #27) and its own GSC trace work
   (task #7, `iw5sp.md`). This IS an actionable status list — cross-check
   `known_issues.md`/`compatibility_matrix.md` for the underlying bug
   detail behind anything marked ⚠️/❓.
2. **Multiplayer killstreaks (further down)** — a canonical reference
   list sourced from public web research, for forward-planning only. MP
   work hasn't started at all, so nothing in that section reflects any
   tested or implemented status — see that section's own header for the
   sourcing caveat.

Don't conflate the two — a weapon system appearing in both sections (e.g.
Predator Missile, SMAW) means it exists in both Campaign and MP contexts,
not that this project has verified anything about the MP version.

## Campaign killstreak-type weapon systems — live-tested this session, real controller-support status

**This is the actionable list for this project's current SP-first scope** —
mounted/support weapon systems actually encountered and controller-tested
during the 2026-07-18 Campaign playtest session (`known_issues.md` issue
#27), each Campaign-unique unless noted otherwise. Real, first-party test
data, not web research.

| Weapon system | Mission | Controller status | Notes |
|---|---|---|---|
| DPV (Diver Propulsion Vehicle) | Hunter Killer | ⚠️ Partial | Movement works, aiming doesn't (bug #1). Campaign-unique. |
| Boat | Hunter Killer | ✅ Working | Movement and aim both fully working. Campaign-unique. |
| UGV (minigun + grenade launcher) | Persona Non Grata | ✅ Working | Fully working, no fallback needed. Campaign-unique. |
| Mortar | Goalpost *(corrected 2026-07-19 — a dedicated zone-identification pass found this is actually Goalpost/`hamburg.ff`; previously misfiled under "Back on the Grid"/`dubai.ff`, which is untested — see `compatibility_matrix.md`)* | ⚠️ Partial | Aim works, fire doesn't (bug #5, task #26). Campaign-unique. |
| Mounted Browning M2 (vehicle turret) | Goalpost *(corrected 2026-07-19 — same mission mis-attribution as Mortar above)* | ⚠️ Partial (unconfirmed cause) | Aim/fire both work, but felt notably harder than expected — cause not yet diagnosed (bug #6, task #27: no aim assist vs. missing regen-rate buff). Campaign-unique. |
| Helicopter door gun (Remote Turret) | Return to Sender | ✅ Working | Fully working, no fallback needed. **Shares its real name ("Remote Turret") with an MP Support Strike Package killstreak below — same weapon-system concept, not confirmed to be the same underlying code.** |
| SMAW | Goalpost | ✅ Working (dumb-fire) / ❓ Unconfirmed (lock-on) | Free-fire against ground targets (tanks) works. Lock-on against aircraft may be broken, or the target may be non-targetable/scripted — not yet confirmed either way (bug #8, task #29). **Shares its real name with a real-world/CoD-series weapon also usable in MP** (dumb-fire only there, no lock-on in MP per public research — see below), though this project hasn't independently verified that MP fact. |
| Predator Missile (`remote_missile`) | Down the Rabbit Hole (Act 3, not yet separately playtested in Campaign — Survival confirmed, see below) — **CORRECTION 2026-07-18: "Black Tuesday" was never confirmed to use this killstreak; a research pass checked the best zone candidates and found nothing, likely a mistaken assumption from an earlier session — see `known_issues.md` issue #29 for detail** | ⚠️ Partial | Camera/view control confirmed working (shares the real UAV-control system). **Fire — FIXED and LIVE-CONFIRMED 2026-07-18/19 (see the Survival roster table below for the full fix detail): the real `+attack` kbutton call alone was necessary but not sufficient — delivery to `notifyonplayercommand`'s native listener additionally needed an explicit bind-table index argument on the queued client command (`"n 1"`, not bare `"n"`). Since Down the Rabbit Hole (`rescue_2.ff`) runs the literal same compiled `1554.gscbin`/`1555.gscbin` scripts as Survival, this fix applies here too** — not yet SEPARATELY live-tested in this specific Campaign mission, but high confidence given the byte-identical scripts. One mission-specific difference: Campaign's equip slot is `+actionslot 2`, not Survival's slot. Separately, live-reported and still OPEN: the post-fire missile-guidance sequence (controlling the flying missile) is where **movement/aim breaks on controller**. **Updated 2026-07-19**: a re-investigation via the killstreak's own GSC (confirmed the guidance-phase script does zero per-frame input reads — steering is 100% native) plus a whole-binary static scan found the real per-frame reader chain (`FUN_004554d0` → `FUN_006423d0`, reading `pml+0xc/+0x10/+0x14`) — this REFUTES the earlier `cmd+0x3e`/`0x3f` theory as the mechanism for this specific bug (that theory's `+0x1094` flag is a different address from the `clientStruct+0xc` bit `controlslinkto` actually sets). A new diagnostic (`Hook_MissileGuidanceDispatch`) is deployed logging both sides pending one more live data pull, but the actual fix isn't implemented yet (`known_issues.md` issue #30). See `known_issues.md` issues #7/#29/#30 and `iw5sp.md`'s full GSC trace. **Also a real MP killstreak — see below.** |

Turret and AI-squadmate call-ins (D-pad Left in Survival) are a related
but structurally separate system from any of the above — see
`known_issues.md` issues #13/#14 for that trace, not this file.

## Campaign killstreaks not yet playtested this session

- **AC-130** — appears in Campaign mission **"Iron Lady"** (Act 2, not yet
  reached this session) and the Special Ops mission **"Fire Mission"**.
  No controller-support status yet, since this mission hasn't been
  playtested. (Sourced from earlier web research this session, kept here
  for completeness — not re-verified against live testing.)

## Survival buy-station killstreak roster — real, CSV-verified (2026-07-18)

**Corrects an earlier assumption.** A prior session's "killstreak-crate
table" lead (found in one script's precache list, not the actual economy
data) wrongly implied 6 purchasable killstreak items. Re-extracting
`sp/survival_armories.csv` directly (the real buy-station economy table)
shows **only 4 real, purchasable Survival killstreaks exist**:

| Item | Cost | Wave gate | Real input mechanism | `notifyonplayercommand`-gated? |
|---|---|---|---|---|
| `remote_missile` (Predator Missile) | 2500 | 0 | Camera takeover (shares real UAV-control system); fire via `notifyonplayercommand("launch_remote_missile", "+attack")` | Yes — **FIXED and LIVE-CONFIRMED 2026-07-18/19 (superseding the "currently broken" status this row previously had).** The real `+attack` kbutton call (issue #29) was necessary but not sufficient on its own — a dedicated Ghidra trace of the full delivery chain (`FUN_0044bb50`→`FUN_0053b1f0`→`Cmd_Argv(1)`→`FUN_00738683`/`atol()`) found the queued client command needs an explicit decimal bind-table index as a second token: `"n 1"` (index 1 = `+attack` in the real 81-entry bind-name table at `0x00929fa0`), not bare `"n"`, which left `Cmd_Argv(1)` empty and never matched. Pushed via `FUN_00428a70` (confirmed plain `__cdecl`, safe to call directly) on Fire's down-edge, gated behind the new `[Experimental] FireNotifyQueueKick` config toggle (default on). **Launch itself confirmed working live** — see `known_issues.md` issue #29 for the full bytecode-to-delivery trace. Post-fire missile-guidance aim is a SEPARATE, still-open bug — see issue #30. |
| `precision_airstrike` | 2500 | 3 | ✅ **CONFIRMED FULLY WORKING on controller in Survival (2026-07-18, live-confirmed by the user).** Real mechanism, per the user's own direct correction: it's a smoke-grenade-style THROWN marker, not a HUD/cursor-based placement system like MP's version — aim with the look stick, throw with Fire, same as a normal grenade throw. This explains why it "just works": both aiming (the look-stick hook) and throwing (the real `+attack` kbutton, or the underlying grenade-throw input) are inputs this project already drives correctly, with no separate confirm/placement-UI step needed at all. Corrects the earlier `beginlocationselection`/`confirm_location`-based theory below, which was closer to MP's cursor-based version, not Survival's actual grenade-throw mechanic. | No |
| `friendly_support_delta` (AI squadmates) | 3000 | 13 | ✅ **CONFIRMED WORKING** via D-pad Left's key-synthesis fix (see `known_issues.md` issues #13/#14) — `notifyoncommand("friendly_support_called", "+actionslot 4")`, identical spawn logic to riotshield below | Yes |
| `friendly_support_riotshield` | 5000 | 20 | ✅ **CONFIRMED WORKING**, same mechanism as delta above — the two are functionally identical in GSC (only a cosmetic HUD icon differs); a prior hypothesis that a per-type code divergence explains why squadmate call-ins fail is **refuted** — see `known_issues.md` issue #14's updated trail | Yes |

`stealth_airstrike`/`carepackage_c4`/`carepackage_ammo` — **do not exist as
purchasable Survival items**, confirmed absent from the real economy CSV.
They only ever appear in one script's precache list alongside other unused
assets — dead/vestigial content, not reachable in retail Survival. Drop
these from any future tracking.

`sentry`/`sentry_gl` (turret) and `iw5_riotshield_so` are a separate
`equipment`-category buy, not `airsupport` — already working via the
existing D-pad key-synthesis fix, not re-traced this pass.

## Multiplayer killstreaks — full reference, NOT currently actionable

**This project has not started Multiplayer (`iw5mp.exe`) work at all** (see
README's "Known limitations") — the list below is pure forward-planning
reference, not a to-do list to start working through yet. MW3 MP
introduced a 3-package killstreak system (a genuine departure from earlier
CoD titles' single-list model):

### Assault Strike Package (killstreak resets on death — offensive)
| Kills | Reward |
|---|---|
| 4 | UAV |
| 4 | Care Package |
| 5 | I.M.S. (Intelligent Munitions System) |
| 5 | Predator Missile |
| 5 | Sentry Gun |
| 6 | Precision Airstrike |
| 7 | Attack Helicopter |
| 9 | Strafe Run (5 Attack Helicopters) / Little Bird Guard / Reaper *(exact ordering/naming inconsistent across sources at this tier — needs cross-checking)* |
| 10 | Assault Drone |
| 12 | AC-130 |
| 12 | Pave Low |
| 15 | Juggernaut |
| 17 | Osprey Gunner |

### Support Strike Package (killstreak persists through death — defensive/utility)
| Kills | Reward |
|---|---|
| 4 | UAV |
| 5 | Counter-UAV |
| 5 | Ballistic Vests |
| 8 | SAM Turret |
| 10 | Recon Drone |
| 12 | Advanced UAV |
| 12 | Remote Turret |
| 14 | Stealth Bomber |
| 18 | Escort Airdrop |

### Specialist Strike Package
No killstreak rewards at all — instead grants additional perks at 2/4/6
kills (up to 3 extra perk slots stacked onto the player's loadout).

### Universal (any package)
| Kills | Reward |
|---|---|
| 25 (not counting Strike Package kills) | MOAB (Mother of All Bombs) |

Sources: [The Complete Guide To MW3's Killstreak Rewards - Game Informer](https://gameinformer.com/b/features/archive/2011/11/05/the-complete-guide-to-modern-warfare-3-39-s-killstreaks.aspx), [Support Strike Package research via web search, multiple sources cross-checked (Habbox Forum archive, Altered Gamer, GameRant)]

## Open follow-up

- Locate this install's own real killstreak-definition asset (very likely a
  CSV/GDT table similar to `sp/survival_armories.csv` already found for the
  buy-station system) to independently confirm exact kill thresholds and
  resolve the tier-9 Assault Package naming inconsistency noted above.
- ~~Cross-reference this list against whatever Survival mode's own
  buy-station killstreak set actually offers~~ **RESOLVED 2026-07-18** — see
  the new "Survival buy-station killstreak roster" section above. Survival's
  real roster is a 4-item subset (`remote_missile`, `precision_airstrike`,
  `friendly_support_delta`, `friendly_support_riotshield`), all sharing
  names with real MP killstreaks in the table below.
