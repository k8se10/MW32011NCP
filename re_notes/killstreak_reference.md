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

**This is the actionable list for this mod's current SP-first scope** —
mounted/support weapon systems actually encountered and controller-tested
during the 2026-07-18 Campaign playtest session (`known_issues.md` issue
#27), each Campaign-unique unless noted otherwise. Real, first-party test
data, not web research.

| Weapon system | Mission | Controller status | Notes |
|---|---|---|---|
| DPV (Diver Propulsion Vehicle) | Hunter Killer | ⚠️ Partial | Movement works, aiming doesn't (bug #1). Campaign-unique. |
| Boat | Hunter Killer | ✅ Working | Movement and aim both fully working. Campaign-unique. |
| UGV (minigun + grenade launcher) | Persona Non Grata | ✅ Working | Fully working, no fallback needed. Campaign-unique. |
| Mortar | Back on the Grid | ⚠️ Partial | Aim works, fire doesn't (bug #5, task #26). Campaign-unique. |
| Mounted Browning M2 (vehicle turret) | Back on the Grid | ⚠️ Partial (unconfirmed cause) | Aim/fire both work, but felt notably harder than expected — cause not yet diagnosed (bug #6, task #27: no aim assist vs. missing regen-rate buff). Campaign-unique. |
| Helicopter door gun (Remote Turret) | Return to Sender | ✅ Working | Fully working, no fallback needed. **Shares its real name ("Remote Turret") with an MP Support Strike Package killstreak below — same weapon-system concept, not confirmed to be the same underlying code.** |
| SMAW | Goalpost | ✅ Working (dumb-fire) / ❓ Unconfirmed (lock-on) | Free-fire against ground targets (tanks) works. Lock-on against aircraft may be broken, or the target may be non-targetable/scripted — not yet confirmed either way (bug #8, task #29). **Shares its real name with a real-world/CoD-series weapon also usable in MP** (dumb-fire only there, no lock-on in MP per public research — see below), though this project hasn't independently verified that MP fact. |
| Predator Missile (`remote_missile`) | Down the Rabbit Hole (Act 3, not yet playtested this session) — **CORRECTION 2026-07-18: "Black Tuesday" was never confirmed to use this killstreak; a research pass checked the best zone candidates and found nothing, likely a mistaken assumption from an earlier session — see `known_issues.md` issue #29 for detail** | ⚠️ Partial | Camera/view control confirmed working (shares the real UAV-control system). **Fire (2026-07-18): rewired off raw usercmd bit onto the real `+attack` kbutton (`known_issues.md` issue #29) — LIVE-TESTED. Regular gunfire unaffected (no regression), but the missile still does not launch — the raw-bit-vs-real-kbutton hypothesis is REFUTED, real fix still not found.** **Confirmed (2026-07-18): Down the Rabbit Hole (`rescue_2.ff`) uses the literal SAME compiled `1554.gscbin`/`1555.gscbin` scripts as Survival's version, byte-for-byte identical `notifyonplayercommand` calls — a Survival-side fix for Fire's `notifyonplayercommand` reachability will fix this Campaign mission too, not just Survival.** One mission-specific difference: Campaign's equip slot is `+actionslot 2`, not Survival's slot. Separately, live-reported: the post-fire missile-guidance sequence (controlling the flying missile) is where **movement breaks on controller** (`known_issues.md` issue #27 bug #9, task #25 — likely a scripted-freeze/cinematic-lock gap in the movement hook, not a Fire-wiring issue). See `known_issues.md` task #7 and `iw5sp.md`'s full GSC trace. **Also a real MP killstreak — see below.** |

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
| `remote_missile` (Predator Missile) | 2500 | 0 | Camera takeover (shares real UAV-control system); fire via `notifyonplayercommand("launch_remote_missile", "+attack")` | Yes — this is why it's currently broken (issue #29) |
| `precision_airstrike` | 2500 | 3 | **Different mechanism entirely**: real native `beginlocationselection`/`endlocationselection` placement-marker API, confirmed NOT gated by `notifyonplayercommand` at all — `confirm_location` fires purely from native code, never sent from any GSC script. **(2026-07-18) Cursor movement traced to `FUN_0057df60` mode 1, which reuses the same raw mouse-delta source normal look already feeds — plausibly already works via this mod's existing look hook, untested. Only the confirm/Fire-detection step into `confirm_location` is still unlocated.** **Worth a live test before any new code**: try `precision_airstrike` and see if the placement cursor tracks the right stick. **User confirms (2026-07-18) the real in-game mechanic matches this exactly** — it plays like tossing a smoke grenade to mark a target (aim + confirm), not a menu/UI selection — consistent with a real-time cursor driven by the look stick rather than a discrete menu-nav interaction, reinforcing the "may already work via the existing look hook" expectation. | No |
| `friendly_support_delta` (AI squadmates) | 3000 | 13 | `notifyoncommand("friendly_support_called", "+actionslot 4")`, identical spawn logic to riotshield below | Yes |
| `friendly_support_riotshield` | 5000 | 20 | Same trigger/spawn logic as delta — the two are functionally identical in GSC (only a cosmetic HUD icon differs); a prior hypothesis that a per-type code divergence explains why squadmate call-ins fail is **refuted** — see `known_issues.md` issue #14's updated trail | Yes |

`stealth_airstrike`/`carepackage_c4`/`carepackage_ammo` — **do not exist as
purchasable Survival items**, confirmed absent from the real economy CSV.
They only ever appear in one script's precache list alongside other unused
assets — dead/vestigial content, not reachable in retail Survival. Drop
these from any future tracking.

`sentry`/`sentry_gl` (turret) and `iw5_riotshield_so` are a separate
`equipment`-category buy, not `airsupport` — already working via the
existing D-pad key-synthesis fix, not re-traced this pass.

## Multiplayer killstreaks — full reference, NOT currently actionable

**This mod has not started Multiplayer (`iw5mp.exe`) work at all** (see
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
