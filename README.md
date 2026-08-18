# MW3 Native Controller Support (Campaign & Survival)

> **▶️ v0.3.3 (2026-08-18) — real, extended-session performance stuttering fixed.**
> Three separate, evidence-backed causes found and fixed: `proxy_d3d9.log` was
> opened in append mode and never trimmed between launches (grew past 2GB
> across every session since install); the background controller-poll thread
> never stopped rescanning every XInput slot plus DualSense on every ~4ms
> tick, even once the active device was known; and a diagnostic log's dedup
> logic didn't actually deduplicate, producing hundreds of thousands of lines
> in a single combat session. A/B tested directly against v0.2.2 to confirm
> the regression was real, not imagined — user-confirmed "much better"
> afterward. One residual dip (looking toward enemies) remains open, most
> likely genuine GPU-side rendering cost rather than anything this mod
> controls — see `re_notes/known_issues.md` issue #79.
>
> **Also fixed**: vibration could get stuck on indefinitely across a pause or
> menu; a real co-op bug where damage-taken rumble could trigger off a
> teammate's hits instead of your own (mitigated pending further research into
> the real "which player is me" mechanism — see issue #79's own entry).
>
> **Ships a first, PREVIEW/WIP pass at DualSense Bluetooth support — stick
> input does not work correctly over Bluetooth, confirmed live against real
> hardware; do not rely on it for gameplay.** USB DualSense support (added
> v0.3.2) has still not been independently confirmed working either — see
> issue #76.
>
> See `PATCHNOTES.md` for the full v0.3.3 changelog, including v0.3.2's
> controller-glyph root-cause fix and native DualSense/gyro preview.

> **⚠️ SUPPORTED VERSIONS — only v0.2.2 (GitHub only) and v0.3.2+ are currently
> distributed.**
> Versions v0.2.1 and earlier shipped with an aim-assist feature's code compiled
> into the DLL — disabled by default, but present in the binary. v0.2.2
> **permanently removed that code entirely** following VAC-risk research (see
> Known Limitations below for the full reasoning). No ban was ever reported or
> observed under earlier builds, and whether the code's mere presence raised
> VAC exposure was never confirmed either way — as a precaution (2026-07-31),
> **releases v0.1.0-prealpha through v0.2.1 have been unpublished from both
> GitHub Releases and Nexus Mods.**
>
> **v0.2.2 is kept available as a stable, LTS-style anchor release — GitHub
> only** (archived on Nexus). Everything between it and the current release
> (v0.2.5, v0.3.0, v0.3.1, v0.3.1.h1) has been unpublished from GitHub
> Releases as of v0.3.3 — that stretch was rapid, in-progress alpha churn, not
> a stable point worth keeping downloadable alongside the current release.
> **v0.2.2 and v0.3.2 onward are the only versions this project currently
> distributes or supports.** Nothing has been deleted: the underlying git
> tags, commits, and full source history for every version remain available
> in this repository; only the built, downloadable release packages are
> hidden.

> **A note on antivirus scans:** a small number of scanners have flagged
> released DLLs with generic heuristic labels — v0.3.0 got 1/67 on VirusTotal
> (VBA32, `dbadur`); v0.3.1 got a report of MaxSecure flagging it as
> `Trojan.Malware.300983.susgen`. Every major vendor (Microsoft, Kaspersky,
> ESET, Malwarebytes, CrowdStrike, BitDefender, and more) has shown clean on
> every release so far. Both labels are generic "suspicious-generic" heuristic
> buckets, not a named-family match — consistent with a heuristic false
> positive on the DLL's real, intentional behavior (hooking functions inside
> the game process via MinHook and runtime byte-pattern scanning), the same
> general shape plenty of legitimate tools (ReShade, RivaTuner Statistics
> Server) trigger on some scanners. Full source is in this repository if you
> want to verify for yourself. See `known_issues.md` #64.

**Status: ALPHA — v0.3.3 (2026-08-18), extended-session stutter fix + vibration/
co-op rumble fixes + DualSense Bluetooth preview.** Changes below since v0.3.2,
see `PATCHNOTES.md` for the full itemized list:

- **Real, extended-session performance stuttering fixed** — three separate causes (log file never trimmed between launches, controller-poll thread never stopping its rescans, a diagnostic log's broken dedup) found and fixed via direct evidence, not guessed. User-confirmed "much better." One residual dip (looking toward enemies, likely GPU-side) remains open — see `re_notes/known_issues.md` issue #79.
- **Vibration could get stuck on indefinitely across a pause or menu** — fixed.
- **Co-op: damage-rumble could trigger off a teammate's hits, not your own** — mitigated (disables itself when a second real player entity is detected); the real underlying fix needs further research.
- **PREVIEW/WIP, KNOWN BROKEN over Bluetooth: DualSense Bluetooth support** — stick input doesn't work correctly over Bluetooth, confirmed live. USB DualSense support (v0.3.2) still hasn't been independently confirmed working either.
- Menu-glyph calibration work covering every screen reachable from the main menu, plus a new in-game click-and-drag glyph position editor (dev tool).

> ⚠ **Known gap: a few menu glyph icons can land in a slightly wrong POSITION
> under non-English languages** ⚠
> Icon SELECTION is correct in every language (fixed in v0.3.1) — only fine
> on-screen positioning on a handful of screens (calibrated against English
> text length) isn't yet.

Everything else carries over from v0.3.2 (controller-glyph root-cause fix, native
DualSense/gyro USB preview) and v0.3.0 (controller-glyph icons, real controller
vibration). **Auto-mantle while sprinting is now confirmed working** (issue #62,
root-caused and fixed) but still ships off by default (`AutoMantleEnabled=0`) —
opt in deliberately if you want it. Vibration for damage absorbed by Survival's
purchasable Body Armor remains unimplemented (a separate value from real health
this project hasn't located in memory yet). See Known Limitations below for both.

No other player-facing behavior changed from v0.2.2, itself a risk-mitigation release: aim assist
(rotational friction + magnetism, reading live entity/target memory) was **permanently removed**,
not just disabled, following VAC risk research — see Known Limitations. Builds on v0.2.1's two
live-confirmed items (console-accurate look acceleration ramp, Hold Breath fully working as native
input). Core movement, look, combat, stance, and Sprint remain confirmed working live against
`iw5sp.exe` (Campaign/Survival), real D-pad/A menu navigation covers every UI surface actually
exercised (main menu, title screen, pause menu, options, buy-stations, sliders), and 3 of Survival's 4
real killstreaks are confirmed working end-to-end. See **Status at a glance** immediately below for an
explicit, no-guessing breakdown of exactly what's fully working, partial, or not implemented at all —
read that before assuming any specific feature works. Not feature-complete, not fully tested
end-to-end, and Multiplayer (`iw5mp.exe`) hasn't been started at all.

A from-scratch native controller project for Call of Duty: Modern Warfare 3 (2011, IW5
engine) — analog movement, look, and buttons driven directly through the game's own
engine calls, not keyboard/mouse emulation. See `re_notes/` for the full reverse-
engineering writeup this project is built on, and `PATCHNOTES.md` for what changed in
each release.

## Status at a glance

The single most direct answer to "does X work?" — organized by real confidence
level, not by feature category. If something isn't listed here, assume it's
**untested**, not confirmed either way. Everything in ✅ has been personally
live-tested by the developer during actual play, not just built-and-assumed.

### ✅ Fully working (live-confirmed)

| Feature | Confirmed |
|---|---|
| Analog movement (left stick) | Real `usercmd_t` bytes |
| Analog look (right stick) + ADS zoom-aware slowdown | Real angle accumulators |
| Fire (RT), Tactical/Lethal (LB/RB), Jump (A) | |
| ADS hold-to-aim (LT) | Real kbutton, not a toggle |
| Melee (R3) | Real kbutton |
| Reload (X) | Real kbutton |
| Interact hold-vs-tap (X) | 300ms hold, tap reloads instead |
| Weapon switch (Y) | Real `weapnext` dispatch |
| D-pad Up/Right/Down — killstreak/attachment slots | Real `+actionslot` dispatch |
| D-pad Left — AI squadmate call-in (Survival) | Key-synthesis exception, confirmed |
| Crouch/Prone 3-state stance ladder (B) | Real native toggle, no desync. Two bugs (intermittent ~2% silent failures; a separate "needs an initial click at launch" gap) fixed and user-confirmed live in v0.2.5 — `known_issues.md` #1/#27/#42 |
| **Sprint (L3)** | **Real kbutton (2026-07-19) — native duration/recovery timer AND Extreme Conditioning's perk override both apply automatically, no custom code needed** |
| Start — pause menu open **and** close | Real engine calls, not a keypress |
| B — back out of open menus | Real ESC-forward |
| Survival ready-up (hold Y) | Key-synthesis exception, confirmed |
| Buy-station + pause interaction bug fix | |
| Menu/UI navigation (D-pad + A) | Main menu, title screen, pause menu, options two-pane drill, buy-station/armory lists, **slider VALUE adjustment** |
| Predator Missile — **launch only** | Fixed 2026-07-19 via a bind-index command-queue fix |
| Precision Airstrike (Survival) | Smoke-grenade-throw mechanic, uses Fire as-is |
| Boat (Hunter Killer), UGV (Persona Non Grata), Helicopter door gun (Return to Sender), SMAW dumb-fire (Goalpost) | Campaign weapon systems |
| Button/stick layout presets, **including `TacticalLefty`** | **`TacticalLefty` confirmed correct against real hardware, 2026-07-19** — previously this preset's own remap was the one open accuracy question in this system |
| **Hold Breath (L3 while ADS'd on a sniper)** | **Real kbutton (2026-07-20)**, genuinely native, no key-synthesis needed — steadies aim while held, accuracy drops once breath runs out. A critical regression (couldn't fire while breath was held, issue #46) was fixed and user-confirmed live 2026-07-31 |
| Look acceleration ramp | Console-accurate turn-rate ramp (33ms = one 30fps engine frame), on by default |
| Button-glyph controller icons (in-game interact hints, menu corner hints, custom cursor) | Confirmed live (2026-08-01) — in-game interact hints (pickup/swap, buy-station, mantle, Reload, grenade throwback, Survival ready-up), menu UI corner hints (Back/Friends), and a custom mouse cursor overlay replacing the native software cursor. See `known_issues.md` #48/#50/#52. **v0.3.2 (2026-08-16)**: the master enable flag had shipped hardcoded off in every release from v0.3.0 through v0.3.1.h1, so glyphs never drew on a fresh install for anyone but the developer's own dev-machine build — root-caused and fixed, confirmed live via direct reproduction; see `known_issues.md` issue #74 |

### 🟡 Partial (works, but with a specific, known gap)

| Feature | What's missing |
|---|---|
| Predator Missile — **post-fire guidance/aim** | Launch works; controlling the flying missile is still broken. Real reader chain found, diagnostic deployed, needs one more live data pull to finish |
| Back button (`+scores`) | Live-tested (2026-07-17) with zero visible effect (no scoreboard/objectives overlay appears). Real cause undiagnosed; parked as a UI gap to revisit, not a "just needs a test" item — see `re_notes/known_issues.md` issues #3/#7/#28 |
| L3 no longer force-stands while ADS'd | Implemented, builds clean — not yet separately live-confirmed |
| DPV (Hunter Killer) | Movement works, aiming doesn't |
| Mortar (Goalpost) | Aim works, fire input not wired |
| Mounted M2 turret (Goalpost) | Works, but feels too hard — cause not yet diagnosed |
| SMAW lock-on vs. aircraft (Goalpost) | Unconfirmed whether this is even a real bug |
| Real in-game controller options menu | **Superseded, v0.3.1**: a full replacement screen exists (every real vanilla setting, real rebind capture) but ships as a preview/WIP feature — off by default, `mw3ncp_config.ini`-only, not yet played. See Feature list and `known_issues.md` #66 |
| Highlighted-item A-glyph (menu list navigation) | **Shown only on an explicit, live-verified allowlist** (main menu, Campaign hub, and the Leave Lobby/Choose Content Pack popups), a release-safety policy adopted 2026-08-03 after an audit found several previously-assumed-working screens either unconfirmed or actively broken. Every other menu screen shows no glyph at all, rather than a possibly-wrong one, until individually re-verified. See `re_notes/known_issues.md` issue #51's coverage table for the full per-screen breakdown |
| Vibration/rumble | Fire rumble via a re-verified-safe hook; damage rumble via a per-frame health poll instead of a hook, since the candidate damage-hook target turned out unsafe on closer inspection. `FireIntensity` is maxed at its software ceiling (`1.0`) — if still weak on your controller, that's very likely the controller's own hardware, not a remaining config fix. **Known gaps**: doesn't register hits absorbed by Survival's Body Armor (a separate value from real health, not yet located in memory); in 2-player Survival co-op, damage-rumble disables itself entirely rather than risk triggering off a teammate's hits, pending real research into identifying which player is you (v0.3.3, issue #79). See `re_notes/known_issues.md` issues #24/#63/#79 |
| **Auto-mantle while sprinting** | **Confirmed working (v0.3.3, issue #62)** after two earlier fix rounds that couldn't be live-confirmed — a real coupling bug (the mantle-fire gate accidentally required an unrelated icon lookup to succeed first) was found and decoupled. **Still ships OFF by default** (`AutoMantleEnabled=0`), opt-in by design, not because it doesn't work |

### ⬜ Not working / not implemented at all

| Feature | Why |
|---|---|
| Per-animation-step reload vibration | **Committed final-scope target, deliberately deferred, not started.** Real per-weapon reload-timing data already found (`iReloadTime`/`iReloadStartTime`/`iReloadEndTime`/etc. in `WeaponCompleteDef`) to eventually sync pulses to each weapon's own real animation phases; still needs a genuine "reload is actually happening" trigger (a real ammo/reload-state poll, not the Reload button press — that fires on every Interact tap too). See `re_notes/known_issues.md` issue #63 |
| Vehicle-exit prompt (`+usereload`, Mind the Gap) | Not wired |
| Survival debug menu / dev console | The real console is confirmed permanently dead — a custom debug menu hasn't been built |
| WaW-style animated dev clan tag *presence data above players* | Still not implemented — genuinely complicated by a dead networked Elite-session dependency, see `known_issues.md` #37. (The per-frame render hook blocker this previously depended on is resolved as of v0.2.5 — see the on-screen notifications feature below, which reuses the same Gold/Rainbow/Sweep *visual style* for a different, unrelated purpose, not this feature itself.) |
| Multiplayer (`iw5mp.exe`) | **Not started at all** — separate binary, needs its own full RE pass, anti-cheat exposure still unresolved |

### ❓ Untested (not known broken, just never exercised)

Special Ops (all 16 missions), AC-130 (Iron Lady/Fire Mission), 9 of 17 Campaign
missions — see **Controller compatibility by mission/mode** below for the exact list.

### 🔭 Roadmap (planned, partial, and parked features)

A consolidated forward-looking view across the whole project — pulled from every
`re_notes/*.md` file, `CLAUDE.md`, and `PATCHNOTES.md`, not just the tables above.
Organized by how close each item is to done, not by feature category. Deliberately
excludes aim assist (permanently removed 2026-07-20 by explicit decision, not a
roadmap item — see `re_notes/known_issues.md` issues #15/#33) and anything already
fully live-confirmed working with no known gap (that's the ✅ table above).

#### Tier 1 — Partial, close to done

| Item | Status | Cite |
|---|---|---|
| AC-130 gun-type switching (Iron Lady) | Confirmed working on controller (flight/camera/fire) except switching cannon type (105mm/40mm/25mm); real native trigger not yet found | `known_issues.md` #40 |
| AC-130 camera zoom sensitivity | Look sensitivity not scaled to gunship camera zoom level, feels overly sensitive when zoomed in; roadmap only, not yet investigated | `known_issues.md` #40 |
| Predator Missile post-fire guidance | Launch works; flying-missile control still broken. Real reader chain found, diagnostic deployed, needs one more live data pull | `known_issues.md` #29/#30 |
| Back button (`+scores`) | Live-tested, zero visible effect (no scoreboard/objectives overlay). Real cause undiagnosed, explicitly parked | `known_issues.md` #3/#7/#28 |
| DPV aiming (Hunter Killer) | Movement works, aim doesn't; candidate unifying cause (3rd analog channel `cmd+0x3e`/`0x3f`) found, not yet applied here | `known_issues.md` #30 |
| Mortar fire (Goalpost) | Aim works, fire not wired; confirmed NOT fixed by the Fire-kbutton rewrite; mortar's own fire-control script not yet located | `known_issues.md` #30, PATCHNOTES v0.2.0 |
| Mounted M2 turret difficulty (Goalpost) | Works, feels too hard; regen-buff hypothesis refuted; likely the same missing-aim-channel cause as DPV | `known_issues.md` #30 |
| SMAW lock-on vs. aircraft | Unconfirmed whether even a real bug | README (Partial table above) |
| Real in-game controller options menu (task #23/#66) | **Superseded, v0.3.1**: the original plan (inject content into the real menu system) hit a real GPU-resource-loading limit and was abandoned; built instead as a fully custom-drawn replacement screen (own rendering, own input handling) that draws over the real Options screen. Covers every real vanilla tab plus rebind capture — shipped as a preview/WIP feature, off by default, not yet played. Remaining gaps: a missing window-mode setting, resolution-scaling unverified, some diagram positional calibration incomplete | `known_issues.md` #66 |
| Highlighted-item A-glyph, remaining screens | Shown on an explicit verified-only allowlist after several supposedly-working screens turned out broken; most screens remain to be individually re-verified and added back | README (Partial table above), `known_issues.md` #51 |
| Per-animation-step reload vibration | Deliberately deferred final-scope item; real per-weapon timing data already found, needs a genuine reload-detection trigger | README (Not-working table above), `known_issues.md` #63 |

#### Tier 2 — Planned / researched, not implemented

| Item | Status | Cite |
|---|---|---|
| God-mode / debug-cheat toggles | Real, disassembly-confirmed entity health-immunity bit (`entity+0x13c` bit `0x1`); simpler `so_nofail` "can't fail mission" dvar switch; ammo-refill/wave-skip/killstreak-spawn native pieces not yet reached; a `noclip` GSC notify-event lead not fully traced | `known_issues.md` task #20 |
| Survival wave-skip / difficulty-tuning tools | Full wave-loop + CSV data-table structure reverse-engineered and logged as reference material for a future debug menu; not implemented | `survival_wave_scaling.md` |
| WaW-style animated dev clan tag *presence data above players* | Real investigation done, twice (2026-07-18 and 2026-07-21). MW3's own clan-tag system is networked Elite-session presence data, a bad fit for offline SP/Survival; `self.playername` (local, native-sourced) is too narrow to build on. Recommended path is a fully project-owned overlay (own config + own timer, same precedent as the Sprint stamina timer). The project-wide render-hook blocker this was gated on is now resolved (v0.2.5's `EndScene` hook, see below) — this specific feature (per-player floating tags) is still not implemented, just no longer blocked on the render side | README (Not-working table above), `ui_assets.md`, `known_issues.md` #37 |
| ~~First-launch welcome/MOTD message~~ | **Superseded by v0.2.5's on-screen notification feature** (a startup message + config-hot-reload message, with Gold/Rainbow/Sweep visual variants inspired by WaW's dev-clan-tag aesthetic) — same end-user goal (a native, non-console welcome message), delivered via the new overlay-quad render hook instead of the originally-planned MOTD-ticker repurpose. Shipped, not just planned. | `known_issues.md` #47 |

#### Tier 3 — Researched and parked

- **Real developer console** — decisively confirmed dead code; a custom debug menu (see Tier 2) is the path instead, not unlocking the real one. *(`known_issues.md` task #20)*
- **Vehicle input path** — no dedicated path found; evidence suggests vehicles reuse the same `usercmd_t` fields already hooked (movement/look might already work — untested hypothesis). *(`known_issues.md` #26)*
- **Survival environmental hazards (barricades)** — inconclusive, not confirmed or ruled out. *(`survival_mode_overview.md`)*
- **Survival loadout/killstreak persistence** across death/downed state — open question. *(`survival_mode_overview.md`)*
- **Survival past-wave-21 behavior** (loop/clamp/extrapolate) — unknown. *(`survival_wave_scaling.md`)*
- **Wave-end bonus tier → named-difficulty mapping** — unconfirmed. *(`survival_mode_overview.md`)*
- **`_id_061C`'s defining script** (owns real per-wave enemy formulas) — referenced constantly, never located by filename. *(`survival_mode_overview.md`)*

#### Tier 4 — Not started at all

| Item | Status | Cite |
|---|---|---|
| **Local splitscreen co-op** | User-suggested, to bring back more of the console experience — MW3's Xbox 360/PS3 builds shipped real local splitscreen for Special Ops co-op. Not investigated: no research yet into whether the PC build retains a dormant second-viewport/second-local-client path (distinct from reading a second controller's *input*, already solved). One existing lead: `iw5mp.exe`'s leftover `splitscreenactivegamepadcount`/`attachedcontrollercount`/`@PLATFORM_USECONTROLLER1` strings (this project's very first investigation, `CLAUDE.md`'s "Key technical finding"). A second, independent lead: `survival_mode_overview.md` confirms Survival's real structure is built for **2-player co-op specifically** (`ui_eog_player1_bestscore`/`_player2_bestscore`, `surHUD_performance`/`_p2` HUD fields), with no evidence of >2-player support anywhere — consistent with reviving a real, still-present 2-player path rather than building one from scratch, though this remains unconfirmed until the actual render/simulation code is traced. Likely one of the largest single undertakings in this project's history — a second local client/render pipeline, not a hook on the existing single-player path everything else here builds on. | `known_issues.md` #36 |
| **Multiplayer (`iw5mp.exe`)** | Not started, separate binary, anti-cheat exposure unresolved | README, `known_issues.md` §Multiplayer feasibility |
| Other MW3 client compatibility (Plutonium SP, AlterWare IW5-Mod, DeckOps) | Research-stage only; Steam Deck/Proton is community-reported, not independently verified by this project | README (Client Compatibility table) |
| Vehicle-exit prompt (`+usereload`, Mind the Gap) | Not wired at all | README (Not-working table above) |

## Scorecard (last updated 2026-08-06)

Two different questions, kept deliberately separate — a feature can be highly
functional but low-completeness (a few things built, all working great) or the
reverse (broad coverage, but shaky):

| | Score | Answers |
|---|---|---|
| **Raw functionality** | **~76/100** | Of what's actually implemented and tested, how well does it work? See the methodology note below the matrix. |
| **Feature completeness (SP/Survival scope)** | **~92/100** | Of the full planned SP/Survival roadmap, how much exists at all? From the matrix below. |

**Multiplayer is excluded from both numbers** — it's a separate, equally-large phase
that hasn't started at all; folding a from-scratch second-binary effort into one
blended score would misrepresent both numbers.

### Feature completeness matrix

Deliberately broken into atomic done/not-done sub-items rather than one line per
major system, specifically so that large remaining systems (killstreaks, the real
options menu, vibration) are represented by multiple not-done rows instead of a
single lightly-weighted "partial" line — a flat list with one row
each for "stick layout presets" and "the real options menu" would understate how
much work the latter actually has left, since it's a much larger undertaking. Also
deliberately NOT computed from the live task-tracking list (an ever-expanding
scratchpad — every bug found adds another entry, so its completed/total ratio gets
worse the more thoroughly this project tests itself) — sourced instead from this
file's own Feature List, `known_issues.md`, and the full commit history, the same
kind of durable record git/`PATCHNOTES.md` provides.

| Category | Done | Total | Notes |
|---|---|---|---|
| Foundation/infrastructure | 7 | 7 | Proxy DLL injection, engine hook installation, controller detection/raw input, XInput deadzone+curve, config file system (incl. a new `[Experimental]` section for individually-toggleable in-flight hypotheses), diagnostic logging, dev/safety tooling |
| Core gameplay input | 17 | 17 | Movement, look, ADS+slowdown, core combat buttons, reload/interact, weapon switch, D-pad actionslots, stance ladder (incl. L3 no longer force-standing while ADS'd), sprint (real kbutton, engine's own native duration/recovery timer — LIVE-CONFIRMED 2026-07-19, no custom timer needed), pause menu, B menu-back, ready-up, buy-station+pause fix, keyboard/mouse non-interference |
| Menu/UI navigation | 5 | 5 | Main/pause menu nav — **now confirmed live including the title screen itself**, not just in-game menus — options two-pane drill, buy-station/armory nav, slider adjustment, button/stick layout presets |
| Back/scoreboard | 1 | 2 | Implementation done (builds clean); live-tested, confirmed no visible effect — real cause undiagnosed, parked as a UI gap |
| Button-glyph UI prompts | 5 | 6 | The highlighted-item A-glyph sub-item scores partial, not full credit: an audit (2026-08-03) found several screens claimed working had never been re-confirmed, and one (with its own calibrated position-table entry) was confirmed outright broken. It's now gated behind an explicit, live-verified allowlist (main menu, Campaign hub, two popups) rather than assumed complete; in-game interact hints, menu corner hints, and the custom cursor overlay remain genuinely done. See `known_issues.md` #48/#50/#51/#52 |
| Killstreak support | 4 | 4 | All 4 real Survival killstreaks have a confirmed real mechanism: Predator Missile launch (fixed via a from-bytecode-to-native-delivery trace) and camera/view both confirmed working; Precision Airstrike confirmed working (a smoke-grenade-throw mechanic, not a menu system); AI squadmate call-in confirmed working. Predator Missile's post-fire guidance AIM is a separate, still-open bug (not a killstreak-activation problem) — tracked under the mounted-aim-channel work below |
| Real in-game options menu | 3.5 | 4 | The original injection plan was abandoned (v0.3.1) for a fully custom-drawn replacement screen, now covering every real vanilla tab plus real rebind capture and a custom-bindings drill-down; not full credit since it ships as an unplayed preview feature (off by default) with a few known gaps (window mode setting, resolution scaling) still open — see `known_issues.md` #66 |
| Vibration/rumble | 1.5 | 2 | Rebuilt after an earlier version's startup-crash disable: fire rumble via a re-verified-safe hook, damage rumble via a per-frame health poll (the originally-recommended damage-hook target turned out unsafe on closer inspection, so it's deliberately not hooked). Physically confirmed working, maxed out in software. **Known gap: doesn't register hits absorbed by Survival's Body Armor** (not counted against this row — a genuinely separate, unlocated field). Per-animation-step reload vibration is a real, deliberately-deferred final-scope item (also not counted) — see `known_issues.md` #24/#63 |
| Auto-mantle (sprint) | 0 | 1 | Implemented and live-tested twice (a jump-spam regression fixed, then a total-failure regression addressed), still not confirmed actually firing near a real ledge. Shipped OFF by default; scores 0 rather than partial credit since it has never been observed working — see `known_issues.md` #62 |
| Extreme Conditioning override | 1 | 1 | Resolved for free (2026-07-19) — Sprint's real-kbutton migration means the native perk system applies its own duration override automatically, same as for keyboard players; no separate detection/override code was needed once the kbutton was found |
| **Total** | **45** | **49** | **45/49 ≈ 92/100** — every row current as of 2026-08-06 (the real in-game options menu row was raised from 2→3.5 that day, reflecting the custom replacement screen's full vanilla-tab + rebind-capture coverage) |

### Raw functionality methodology

Average of three real, currently-tracked data sets (untested items excluded from
this score entirely, since "untested" is a completeness question, not a
functionality one): ✅ = 1.0, 🟡/partial = 0.5.

- Current control map (this file, above): 19 confirmed / 3 partial / 2 not-started
  → 20.5/24 ≈ 85% (auto-mantle counts as not-started, not partial — implemented
  and live-tested twice, still not confirmed working)
- Campaign mission compatibility (`re_notes/compatibility_matrix.md`), tested
  missions only: **3 full / 4 partial out of 7 tested** → 5/7 ≈ 71% (the
  mortar/turret bugs belong to Goalpost, not "Back on the Grid," which is
  untested rather than fully-compatible)
- Killstreak-type weapon systems (this file's own "Killstreak support" table,
  Campaign-relevant subset), tested items only (AC-130 excluded, untested):
  **3 full (Boat, UGV, Helicopter door gun) / 5 partial (DPV, Mortar, Mounted
  turret, SMAW, Predator Missile — each has a real open caveat, from unwired
  fire input to unconfirmed lock-on to a still-broken post-fire aim) out of
  8 tested** → 5.5/8 ≈ 69%
- Average: (85 + 71 + 69) / 3 ≈ **75/100**

Recompute periodically as sub-items close out and more of Campaign/Special Ops gets
playtested — these are rough, transparently-weighted estimates for at-a-glance
tracking, not a precise scientific measurement.

## Project stages

This project uses the standard pre-alpha → alpha → beta → 1.0 progression, but
"pre-alpha" here means something more specific than "barely started" — a lot of
this project's core systems (analog movement/look, real engine-state-driven Sprint
and Crouch/Prone, real pause menu, real D-pad/A menu navigation) are already
confirmed working live, on par with the functional bar many shipped mods or
public betas ship at. The label reflects how much of the project's *planned* scope
is still open, not how rough what already works is.

| Stage | Version range | What it means here |
|---|---|---|
| **Pre-alpha** | `0.1.0` – `0.1.5` | Core systems land one at a time — movement/look/combat, stance/sprint, pause menu, and menu navigation done; aim assist, vibration, killstreaks, and the controller options menu still being built out. |
| **Alpha** *(current, v0.3.1.h1)* | `0.1.5` – `0.4.0` | The remaining major systems get built and land. **v0.2.0**: Sprint fully native (no custom timer, Extreme Conditioning resolved for free), 3 of 4 Survival killstreaks confirmed working, menu/UI navigation extended to the main menu/title screen and slider values. **v0.2.1**: console-accurate look acceleration ramp, and Hold Breath (L3 while ADS'd) fully working as genuinely native input after an extensive debugging pass. **v0.2.2**: risk-mitigation release — aim assist permanently removed (not just disabled) following VAC research; see the security notice at the top of this file. Also this project's first LTS-style stabilization release — no follow-up for about a week and a half. **v0.2.5**: crouch/stance reliability hotfix and the on-screen notification system. **v0.3.0**: controller-glyph icons (in-game hints, menu corner hints, custom cursor overlay), the highlighted-item A-glyph (shown on a verified-only allowlist, adopted after an audit found several previously-assumed-working screens weren't), real controller vibration (physically confirmed, strength maxed out in software), and the crouch/UI bug batch from the first public Survival co-op stream. **Two things attempted but NOT working in v0.3.0, shipped honestly flagged rather than silently absent**: auto-mantle while sprinting (implemented, live-tested twice, still doesn't fire reliably — shipped disabled) and vibration for Survival Body Armor hits (a separate, unlocated value). **v0.3.1**: fixed controller-glyph icons never appearing for non-English game languages (root-caused against the game's own real localization resolver, confirmed live); also ships a from-scratch replacement Options screen covering every real vanilla setting plus real rebind capture, as an explicitly labeled preview/WIP feature — off by default, `mw3ncp_config.ini`-only, not yet played. **Intended as a second LTS-style stabilization release, same treatment as v0.2.2** — no further release planned for up to ~2 weeks barring a critical issue. **v0.3.1.h1**: feature-free hotfix — a critical mouse-lag regression (confirmed live), non-16:9 glyph-visibility and scaling fixes (two separate confirmed bug classes), and a real Options-screen crash-risk fix; see `PATCHNOTES.md` for the full itemized list. **Still ahead in this stage**: playtesting the preview Options screen (and closing its known gaps — window mode setting, resolution scaling), re-verifying the remaining A-glyph screens, actually fixing auto-mantle and armor vibration, per-animation-step reload vibration, Predator Missile's guidance aim, and remaining Campaign/Special Ops compatibility gaps. Multiplayer groundwork may start here, pending the anti-cheat question being resolved first. |
| **Beta** | `0.4.0` – `1.0.0` | Should be practically feature-complete — remaining work is closing gaps, fixing what live testing surfaces, and extending reach (other MW3 clients, Multiplayer if the anti-cheat question resolves favorably) rather than building brand-new core systems from scratch. |
| **1.0 (final)** | `1.0.0`+ | Feature-complete against this project's full scope, stable, and treated as a real release rather than an actively-shifting work in progress. |

## Feature list

### Movement & look
- **Analog movement** (move-stick, left by default) — real `usercmd_t.forwardmove`/
  `.rightmove` bytes, additive on top of any keyboard input already present.
- **Analog look** (look-stick, right by default) — writes the raw pitch/yaw
  angle-delta accumulators directly, bypassing the mouse pipeline entirely (own
  sensitivity, no mouse accel/filter inherited). Sensitivity, invert-Y, and an
  ADS-zoom-aware slowdown curve are all configurable — see **Configuration** below.
- **ADS look-slowdown** — look rate scales down while aiming, proportional to the
  weapon's actual live zoom level (`effectiveFov/hipfireFov`, read-only — your real
  field of view is never touched), so magnified optics don't feel absurdly twitchy.
  A separate baseline multiplier applies some slowdown even on low-zoom optics (iron
  sights/red dots), where the zoom ratio alone stays too close to 1.0 to produce a
  noticeable effect on its own. Both configurable, mathematically safe at any value
  (a power curve, not a linear blend — the linear version could invert look direction
  at high strength on deep zooms; fixed in v0.1.1).
- **Aim assist — PERMANENTLY REMOVED (2026-07-20).** A from-scratch implementation
  (rotational friction + magnetism, reading live entity/target data out of process
  memory) was built and its math confirmed correct via live diagnostic logging, but
  it was never shipped functional (broken target classification) and has now been
  cut entirely rather than fixed. Reading gameplay-entity memory to adjust aim is
  mechanically identical to a soft-aimbot regardless of intent — this project's own
  VAC research found the closest real precedent for a proxy-DLL project that
  manipulates gameplay state beyond pure input remapping (ENB, versus ReShade's
  clean, visual-only track record) has actual documented ban history. Removed as a
  deliberate risk-reduction decision, not left disabled-by-default — see
  `re_notes/known_issues.md` issue #15/#16 and #33 for the full reasoning.

### Combat & interaction
- **Fire** (RT), **Tactical**/**Lethal** (LB/RB), **Jump** (A).
- **ADS** (LT) — true hold-to-aim via the real `+toggleads_throw` `kbutton_t`
  KeyDown/KeyUp calls, not a toggle or a raw bit.
- **Melee** (R3) — real melee kbutton, confirmed "100% knife" live.
- **Reload** (X) — real, context-sensitive `kbutton_t`, found via memdiff; fires
  instantly on press, unaffected by Interact's hold requirement below.
- **Interact** (X, same physical button as Reload) — **requires a hold** (300ms by
  default, configurable), not an instant tap: a quick tap reloads the weapon instead,
  same as console. Reload is a separate real kbutton on the same physical button that
  fires on every press regardless of hold duration — so a quick tap doesn't fire
  Interact, but does trigger a real reload.
- **Weapon switch** (Y) — real `weapnext` dispatch via the engine's own bind-index
  jump table, found by live-reading the raw-keycode dispatch table for the actual
  bound keys.
- **D-pad** (all 4 directions) — real `+actionslot 1-4` dispatch, data-driven by
  loadout (killstreaks/attachments/NVG-style toggles, whatever's actually equipped).
  Up/Right/Down call the real dispatch function directly; Left is the second of this
  project's three deliberate, documented exceptions to native-only input (see below) — it
  synthesizes the real bound key instead, since Survival's AI-squadmate call-in on
  that slot needed it (turret call-ins on the same slot are unaffected).
- **Killstreaks (Survival)** — all 4 of Survival's real buy-station killstreaks
  (`sp/survival_armories.csv`: Predator Missile, Precision Airstrike, and the two AI
  squadmate/riot-shield support call-ins) now have a confirmed real mechanism.
  Predator Missile's launch needed a genuine fix, found by tracing the full
  `notifyonplayercommand` GSC bytecode-to-native-delivery chain: the queued client
  command needs an explicit bind-table index argument (`"n 1"`, not bare `"n"`) to
  actually reach the registered GSC listener. Precision Airstrike is a
  smoke-grenade-throw mechanic (not a HUD/cursor system like MP) and uses the
  existing Fire button as-is. Predator Missile's post-fire missile-guidance camera/
  aim is a separate, still-open issue — see Known Limitations.
- **Vibration/rumble.** An earlier version's two native trigger hooks (generic,
  variable-argument native dispatchers) crashed the game at startup, so both
  halves were rebuilt on safer mechanisms. Fire rumble hooks a
  single-call-site-safe, re-verified target found via a runtime byte-pattern
  scan. Damage rumble is not a hook at all — the candidate replacement target for it has an
  inconsistent calling convention across its 14 callers, the same risk class
  that caused the original crash, so damage is instead detected via a
  per-frame poll of the local player's own real health field. A separate bug —
  this project loading a legacy XInput DLL whose own vibration output is a
  documented no-op on real Windows installs — made vibration silently do
  nothing even with the hooks working correctly; also fixed. Uses a
  sustain/decay envelope tuned for real ERM motor spin-up lag, with
  `FireIntensity` maxed at its software ceiling (`1.0`) — physically confirmed
  working. **Known gap: doesn't register hits absorbed by Survival's
  purchasable Body Armor** (a separate value from real health this project
  hasn't located in memory yet). **Per-animation-step reload vibration** (a
  real pulse per animation beat, matching CoD's historical console behavior)
  is a committed final-scope item, deliberately deferred, not yet started —
  real per-weapon reload-timing data is already on record to build it from.
  See `re_notes/known_issues.md` issues #24/#63.

### Stance & Sprint (real engine state, not our own tracked copy)
- **Crouch/Prone stance ladder** (B) — a real 3-state ladder driving the game's own
  native togglecrouch/toggleprone toggle directly (not a raw bit force), so it can
  never desync from the engine's own state:

  | Current stance | B tapped | B held |
  |---|---|---|
  | Standing | → Crouched | → Prone |
  | Crouched | → Standing | → Prone |
  | Prone | → Crouched | → Standing |

  "Hold" fires the instant the press crosses the threshold (no need to release
  first); "tap" only fires on release, and only if the hold threshold was never
  reached during that press. Threshold is configurable (400ms default).
- **Sprint** (L3) — real `+sprint` kbutton_t, found 2026-07-19 and driven directly via
  the same `CallKbuttonDown`/`CallKbuttonUp` mechanism as ADS/Reload/Fire; auto-stands
  from crouch/prone first if needed, matching console. **No custom stamina/cooldown
  timer needed at all**: an earlier version forced the raw `pm_flags` sprint bit
  directly, which bypassed the engine's own native duration/recovery timer entirely
  (confirmed to give unlimited sprint, unlike real keyboard play) and required this
  project to maintain its own hand-rolled 4s-sprint/2s-cooldown timer layer as a
  workaround. Driving the real kbutton instead means the engine's own native timer
  now engages automatically, **LIVE-CONFIRMED working** — including the Extreme
  Conditioning perk's real duration override applying for free, with zero detection
  code needed on this project's side. Real keyboard Shift-to-sprint is left completely
  untouched by these hooks, regardless of whether a controller is connected or idle.
- **Hold Breath** (L3 while ADS'd on a sniper) — real console/keyboard shares this
  exact physical bind with Sprint (`+breath_sprint`); while aiming, L3 drives Hold
  Breath's own kbutton instead of Sprint's, steadying aim while held, with accuracy
  dropping noticeably once breath runs out. **LIVE-CONFIRMED working (2026-07-20)**
  after an extensive debugging pass: the real kbutton (`0xA98C04`, actually Fire's
  own `down[1]` struct slot) has a single byte (`active`, `+0x10`) that doesn't
  self-clear on `KeyUp` — found via a live memory readback after two failed direct
  attempts and a since-removed key-synthesis detour. Fixed by force-clearing that
  byte after every release, driven via the same real `CallKbuttonDown`/
  `CallKbuttonUp` mechanism as Sprint/ADS/Reload — genuinely native input, no
  key-synthesis needed. See `re_notes/known_issues.md` issue #6/#24 for the full
  trail.
- **Auto-mantle — confirmed working (v0.3.3), still off by default by design**
  (`[Movement] AutoMantleEnabled=0` in `mw3ncp_config.ini`, opt-in). While sprinting
  and pushing the stick fully forward (within a configurable ~45° cone), it
  automatically drives the same real native mantle command Jump already uses,
  vaulting over obstacles without a separate Jump press. It's gated on the game's
  own real "is a ledge actually mantleable right now" signal (reusing this
  project's existing detection of the native mantle button-prompt hint) — an
  earlier version without that gate made the player jump continuously any time
  they sprinted forward, since the mantle command and Jump are literally the same
  input. Two earlier fix rounds (the jump-spam gate above, then a timing-gap fix
  between the render hook and gameplay-tick hook) couldn't be live-confirmed
  working; the actual root cause was a real coupling bug — the mantle-fire gate
  accidentally also required this project's own icon lookup to succeed, an
  unrelated concern — found and decoupled in v0.3.3. Direct confirmation after the
  fix: "it works now." See `re_notes/known_issues.md` issue #62.

### Menu & pause
- **Start button** — opens **and closes** the pause menu via real engine calls (not a
  keypress emulation): the real hardcoded ESCAPE-key path for opening, and the same
  function's real "resume" case for closing, driven by a `WndProc` subclass hook so it
  keeps working even while the game's gameplay-simulation tick halts during pause.
- **B — back out of menus** — while a menu is open (main menu, pause menu, etc.), B
  forwards a real ESC keypress to it (the same real mechanism the engine's own key
  handler uses for ESC generically), backing out one level or closing it, on top of
  its normal crouch/prone role during gameplay.
- **Survival ready-up** (hold Y ~740ms between waves) — one of three deliberate, documented
  exceptions to this project's native-only approach (see below); switches weapons instead if
  released before the threshold.
- **Buy-station + pause interaction fix** — a real native bug (not ours) where using a
  buy station then pausing could permanently break all input (ours and real
  keyboard/mouse) until level reload; fixed by reinstating a rising-edge gate window.

### Configuration & customization

All of the tunable values above — plus button/stick layout — live in
**`mw3ncp_config.ini`**, written next to the DLL the first time the project runs (with
every option pre-filled with its default value and a comment explaining it, so the
file is self-documenting from the moment it appears — nothing to configure by hand
to get started). This file remains the primary way to tune the project — a
from-scratch in-game Options screen exists as of v0.3.1 but ships as an unplayed
preview/WIP feature (`[Options] UseCustomOptionsScreen`, off by default, no
in-game toggle), not the recommended way to configure things yet. See the
Feature list below and `re_notes/known_issues.md` issue #66.

**Config changes hot-reload while the game is running.** Save the ini and the mod
picks up the change within about a second — no restart needed — with a short
on-screen confirmation ("MW32011NCP Config Reloaded") in the top-right corner.
You'll also see a "MW32011NCP Started" message for 15 seconds on launch
(rarely, a small thank-you variant instead) — both use the on-screen text
mechanism from issue #47.

**Existing config files carry forward automatically across updates (v0.2.5+).**
An internal `[Meta] ConfigVersion` marker (not a setting — don't edit it by hand)
lets the mod detect an older file and migrate it: every setting you already had
tuned is kept, and any key that got renamed or restructured (e.g. v0.2.5 splitting
`Sensitivity` into `SensitivityHorizontal`/`SensitivityVertical`) is carried over
to its closest equivalent under the new key(s) instead of silently resetting to
the new default. The file is rewritten once, in the current format, right after a
migration runs — nothing to do on your end, and nothing is lost.

| Section | Key | Default | What it does |
|---|---|---|---|
| `[Look]` | `SensitivityHorizontal` | `250` | Look-stick yaw (left/right) turn rate, degrees/second at full deflection (not always the right stick — depends on `StickLayout` below) |
| `[Look]` | `SensitivityVertical` | `250` | Look-stick pitch (up/down) turn rate, degrees/second at full deflection — separate from horizontal since 2026-07-31 |
| `[Look]` | `AdsSlowdownStrength` | `1.75` | ADS zoom-aware look slowdown strength (`0` = off, `1` = fully proportional to zoom, higher = more aggressive than proportional; `1.75` confirmed live to feel closer to real console controller CoD than exactly `1.0`) |
| `[Look]` | `AdsSlowdownBaseline` | `0.65` | Multiplies the strength curve above across EVERY zoom level equally. `1.0` = no extra effect; lower = more slowdown, but at all zoom levels, not just low ones. See `AdsCloseRangeSlowdownStrength` below for low-zoom-only tuning instead |
| `[Look]` | `AdsCloseRangeSlowdownStrength` | `0.35` | Extra slowdown that only meaningfully affects low-zoom weapons (`ratio` close to `1.0`, e.g. pistols/iron sights) and decays to negligible at any real optic's zoom level — fixes "pistols barely feel slowed" without over-slowing 3x+ scopes the way lowering `AdsSlowdownBaseline` would. `0` = off; must stay in `[0, 1]`. Not yet independently live-confirmed (issue #44) |
| `[Look]` | `InvertLook` | `0` | OG console "Invert Look" — flips vertical look |
| `[Look]` | `AccelerationRampMs` | `33` | Milliseconds for look turn-rate to ramp from 0 to full speed after the stick leaves neutral, matching real console MW2/Black Ops behavior. Live-tested against many values — 33ms (one 30fps engine frame) confirmed correct, not the ~0.2s figure external research suggested. `0` = instant response (old behavior) |
| `[Stance]` | `ProneHoldThresholdMs` | `400` | B: hold-vs-tap threshold for the stance ladder |
| `[Interact]` | `HoldThresholdMs` | `300` | X: how long Interact must be held before it fires (a quick tap reloads instead, same as console) |
| `[Survival]` | `ReadyUpHoldThresholdMs` | `740` | Y: hold-to-ready-up threshold between Survival waves |
| `[Movement]` | `AutoMantleEnabled` | `0` | **Confirmed working (v0.3.3), still off by default by design — opt in deliberately.** Automatically mantles over obstacles while sprinting and pushing the stick fully forward, gated on the game's own real ledge-availability signal (not just stance+stick — an earlier version without this misfired into constant jumping on flat ground). See `re_notes/known_issues.md` issue #62 |
| `[Movement]` | `AutoMantleForwardConeDegrees` | `45` | Total cone width (not half-angle), centered on straight-forward, the left stick must fall within for auto-mantle to consider firing |
| `[Movement]` | `AutoMantleMinStickMagnitude` | `0.9` | Left stick deflection `[0,1]` must be at least this close to full for auto-mantle to consider firing |
| `[Bindings]` | `ButtonLayout` | `Default` | `Default` / `Tactical` / `Lefty` / `TacticalLefty` — see table below |
| `[Bindings]` | `StickLayout` | `Default` | `Default` / `Southpaw` / `Legacy` / `LegacySouthpaw` — see table below |
| `[Bindings]` | `FlipTriggers` | `0` | Independently swaps RT↔RB and LT↔LB, combining with whichever `ButtonLayout` is active |
| `[Vibration]` | `Enabled` | `1` | See Known Limitations — fire rumble via a real hook, damage rumble via a per-frame health poll |
| `[Vibration]` | `FireIntensity` | `0.55` | Motor strength `[0,1]` on each real shot fired, tuned up from an initial `0.25` after live feedback that it felt too weak once vibration became physically real |
| `[Vibration]` | `FireDurationMs` | `90` | Milliseconds a fire pulse takes to decay to zero, tuned up from an initial `60` to give a real motor enough time to spin up |
| `[Vibration]` | `DamagePerPoint` | `0.05` | Motor strength added per point of real damage the local player takes, tuned up from an initial `0.03` alongside `FireIntensity` above |
| `[Vibration]` | `DamageMaxIntensity` | `1.0` | Hard cap on damage-rumble strength regardless of damage amount |
| `[Vibration]` | `DamageDurationMs` | `200` | Milliseconds a damage pulse takes to decay to zero |
| `[Experimental]` | `FireNotifyQueueKick` | `1` | Also pushes `"n 1"` onto the real client command queue on Fire's down-edge (alongside the real `+attack` kbutton call) — the confirmed fix that makes Predator Missile's launch reach its native `notifyonplayercommand` listener. Toggle to `0` to fall back to kbutton-only Fire (pre-2026-07-18 behavior) if this is ever suspected of a regression |

**Button layout presets** (reconstructed from the unchanged CoD4→MW2→MW3 console
control scheme. `TacticalLefty` — previously this table's one open accuracy
question — **confirmed correct against real hardware, 2026-07-19**):

| Action | Default | Tactical | Lefty | TacticalLefty |
|---|---|---|---|---|
| Fire | RT | RT | LT | LT |
| ADS | LT | LT | RT | RT |
| Lethal | RB | RB | LB | LB |
| Tactical | LB | LB | RB | RB |
| Crouch/Prone | B | RS | B | LS |
| Sprint | LS | LS | RS | RS |
| Melee | RS | B | LS | B |

**Stick layout presets:**

| Layout | Left stick | Right stick |
|---|---|---|
| Default | Move | Look |
| Southpaw | Look | Move |
| Legacy | Forward/back + turn (horizontal) | Look up/down + strafe (horizontal) |
| LegacySouthpaw | Look up/down + strafe (horizontal) | Forward/back + turn (horizontal) |

## Why native, not an emulator

Every other "controller support" option for this game works by faking keyboard/mouse
input (synthetic key taps, injected mouse deltas) underneath a mapper tool. That adds a
real, measurable translation layer between the stick and the game: poll → convert to a
key/mouse event → OS input queue → the game's own keyboard/mouse-delta processing.

This project instead writes straight into the engine's real per-frame input path —
`usercmd_t.forwardmove/rightmove`/`.buttons`, the raw pitch/yaw angle accumulators, and
(where the engine requires it) the real internal `kbutton_t` down/up state and
`pm_flags` bits the game's own movement code reads — from inside the game's own
process, on the game's own frame tick. There is no OS-level input event, no
intermediate queue, and no keyboard/mouse pipeline to pass through at all. That's a full
layer of translation and buffering removed, which is the project's core advantage: input
feel and latency that matches (not approximates) native console analog input, not a
keyboard/mouse emulation layer with a controller icon on it.

**Three narrow, explicit exceptions**, each a deliberate, user-approved workaround for one
specific input where an extensive search found no locatable native trigger — everything
else in the project, including all of movement/look/combat, drives the engine's real internal
state directly, as described above:

1. **Survival's between-wave ready-up** (hold Y) synthesizes a real F5 keypress via
   `PostMessage`. See `re_notes/known_issues.md` issue #5.
2. **D-pad Left's AI-squadmate call-in** synthesizes the real bound key (`'4'`) instead
   of calling the dispatch function directly, since the direct call failed 100% of the
   time for squadmate call-ins specifically (turret call-ins on the same slot are
   unaffected). See `re_notes/known_issues.md` issues #13/#14.
3. **Back's real `+scores`** (scoreboard/objectives) synthesizes a real TAB keypress,
   since `+scores` turned out not to be a per-frame usercmd kbutton at all — it's a plain
   keyboard bind read by the UI layer. Implemented, builds clean, not yet separately
   live-confirmed. See `re_notes/known_issues.md` issue #28.

## Current control map (`iw5sp.exe`, Xbox-layout controller)

| Input | Action | Status |
|---|---|---|
| Left stick | Move (analog forward/back/strafe) | ✅ Confirmed |
| Right stick | Look (independent sensitivity, no mouse-accel/filter inherited); turn-rate ramps up over 33ms (one 30fps engine frame) after leaving neutral, matching real console MW2/Black Ops behavior | ✅ Confirmed |
| Right trigger (RT) | Fire | ✅ Confirmed |
| Left trigger (LT) | Aim Down Sights (true hold-to-aim, real kbutton) | ✅ Confirmed |
| Left stick click (L3) | Sprint (real `+sprint` kbutton; auto-stands from crouch/prone; native duration/recovery timer + Extreme Conditioning apply automatically, no custom timer needed). While ADS'd on a sniper, drives Hold Breath instead (real kbutton + a force-clear fix for a struct byte that didn't self-clear on release) — steadies aim while held, accuracy drops once breath runs out | ✅ Confirmed live (Sprint 2026-07-19, Hold Breath 2026-07-20); issue #46 fire-while-holding-breath regression fixed 2026-07-31 |
| A | Jump | ✅ Confirmed |
| B | Crouch/Prone — tap toggles crouch, hold goes prone, full 3-state ladder (see below) | ✅ Confirmed |
| X | Interact **and** Reload (real kbutton, context-sensitive like console) | ✅ Confirmed |
| Right stick click (R3) | Melee | ✅ Confirmed |
| Left bumper (LB) | Tactical (smoke) | ✅ Confirmed |
| Right bumper (RB) | Lethal (frag) | ✅ Confirmed |
| Y | Weapon switch (`weapnext`); hold ~740ms in Survival to ready up between waves | ✅ Confirmed |
| Start | Opens **and closes** the pause menu (real native calls, not a keypress emulation) | ✅ Confirmed |
| Back | Real `+scores` (scoreboard/objectives) via a synthetic TAB keypress — third key-synthesis exception, same technique as ready-up/squadmate call-in | 🟡 Implemented, builds clean — not yet separately live-confirmed (see `re_notes/known_issues.md` issue #28) |
| D-pad (Up/Right/Down/Left) | `+actionslot 1-4` — killstreaks/attachments (e.g. noob tube), data-driven by loadout | ✅ Confirmed* (user tested at least half the directions live; all four use the identical confirmed mechanism, so high confidence on the untested ones too) |
| Killstreaks (collectively, Survival) | Calling in / controlling Survival's 4 real killstreaks — see the dedicated table below | 🟡 3 of 4 fully confirmed (Predator Missile launch, Precision Airstrike, squadmate call-in); Predator Missile's post-fire guidance aim still broken — see the dedicated killstreak table below and `re_notes/killstreak_reference.md` for per-item status |
| D-pad + A menu navigation | Item navigation (main menu, pause menu, options screens' two-pane category/settings drill-in-drill-out) | ✅ Confirmed live (task #22), including the title screen itself |
| D-pad + A, buy-station/armory (Survival) | Item navigation on the armory's `itemDef` list | ✅ Confirmed live (2026-07-18) |
| Slider-type settings (e.g. sensitivity) | Adjusting the actual VALUE of a slider, not just navigating to it | ✅ Confirmed live (2026-07-18) — Left/Right adjusts the value directly, via the same generic menu-forwarding mechanism as everything else in this section |
| Button-glyph UI prompts | Real controller-glyph icons in in-game interact hints and menu UI corner hints; custom mouse cursor overlay | ✅ Confirmed live via a custom-hint-redraw overlay — see `re_notes/known_issues.md` issues #48/#50/#52. **v0.3.2**: fixed a project-side master-flag bug that had kept glyphs from drawing for everyone since v0.3.0 — see issue #74 |
| Highlighted-item A-glyph (menu list navigation) | Real controller-glyph icon on whichever menu-list item is currently focused | 🟡 Shown only on a live-verified allowlist (main menu, Campaign hub, two popups); every other screen intentionally shows nothing rather than a possibly-wrong glyph, pending individual re-verification (see `re_notes/known_issues.md` issue #51) |
| Vibration/rumble | Controller rumble on weapon fire/taking damage | 🟡 Fire rumble via a re-verified hook, damage rumble via a per-frame health poll (not a hook, since the recommended hook target turned out unsafe). Physically confirmed working, strength maxed out in software. Known gap: doesn't register Survival Body Armor hits (see `re_notes/known_issues.md` issues #24/#63) |
| Auto-mantle (sprint) | Automatically mantle over obstacles while sprinting + pushing the stick fully forward | 🟡 **Confirmed working (v0.3.3)**, still off by default (`AutoMantleEnabled=0`) — opt in deliberately (see `re_notes/known_issues.md` issue #62) |

**B's stance ladder**, matching real Xbox 360 CoD behavior (not a raw hold of either
bit):

| From | Tap | Hold |
|---|---|---|
| Standing | → Crouched | → Prone |
| Crouched | → Standing | → Prone |
| Prone | → Crouched | → Standing |

"Hold" fires the instant the press crosses the threshold; "tap" only fires on release,
and only if the hold threshold was never reached.

**Killstreaks — Survival's buy-station roster specifically** (distinct from the
Campaign-mission killstreak-type weapon systems covered in the "Killstreak
support" section above — see `re_notes/killstreak_reference.md` for that side).
Real roster confirmed via the game's own buy-station data
(`sp/survival_armories.csv`):

| Killstreak | Status |
|---|---|
| Predator missile (`remote_missile`) | 🟡 Launch confirmed FIXED and working live (needed an explicit bind-index argument on the queued client command — see `re_notes/known_issues.md` issue #29); post-fire missile-guidance camera/aim still broken, real native mechanism found (`controlslinkto`) but the actual fix not yet implemented |
| Precision airstrike (`precision_airstrike`) | ✅ Confirmed fully working live — a smoke-grenade-throw mechanic (not a HUD/cursor system like MP), uses the existing Fire button as-is |
| AI squadmate call-in (`friendly_support_delta`/`friendly_support_riotshield`) | ✅ Fixed — this is D-pad Left's squadmate call-in, which failed 100% until a narrowly-scoped key-synthesis exception resolved it (see `re_notes/known_issues.md` issue #14) |

This is now the complete real Survival killstreak roster, confirmed via the game's
own buy-station data (`sp/survival_armories.csv`) — an earlier, larger 6-item list
assumed in prior notes included dead precache-only content that Survival's buy
stations never actually offer.

## What's blocking the remaining buttons

- **Back:** an early attempt wired `0x00A98B14` in as `+scores`'s kbutton, based on
  an unvalidated assumption (a bind-name-table index treated as if it were a
  `FUN_00438710` switch case number). Live-tested wrong — it made the player walk
  backward (almost certainly the real `+back` movement kbutton) — and was reverted.
  `+scores` turned out not to be a per-frame usercmd kbutton at all: it's a plain
  keyboard bind read by the UI layer, the same category of problem Survival
  ready-up and D-pad Left's squadmate call-in already needed key synthesis to
  solve. Implemented as the third such exception (synthetic TAB keypress) —
  builds clean, not yet separately live-confirmed. See
  `re_notes/known_issues.md` issue #28.
- **Killstreaks:** resolved for 3 of Survival's 4 real killstreaks via a
  from-bytecode-to-native-delivery reverse-engineering pass through the
  `notifyonplayercommand` GSC command-queue chain, which found the real reason
  Predator Missile's launch wasn't reaching its native listener (the queued
  client command needs an explicit bind-table index argument). Precision Airstrike
  and the AI squadmate call-in are both confirmed working live. **Still open:**
  Predator Missile's post-fire missile-guidance camera/aim — the real native
  mechanism (`controlslinkto` → a per-client control-mode bit) is now confirmed via
  disassembly and a live diagnostic hook is deployed, but the actual input-redirect
  fix isn't implemented yet. See the dedicated killstreak table above and
  `re_notes/killstreak_reference.md`.
- **Menu/UI navigation (task #22) — real D-pad/A navigation implemented and
  live-confirmed** across the main menu, pause menu, options screens (two-pane
  category/settings drill-in-drill-out), buy-station/armory item lists, AND slider
  value adjustment (Left/Right adjusts the value directly, not just navigation to it —
  corrected 2026-07-18, see `re_notes/known_issues.md` issue #22's correction note).
  Button-glyph prompt swapping (task #6's other half, real controller icons in hint
  text) is no longer unstarted — shipped and confirmed live in full, 2026-08-01,
  including the Special Ops modal corner-hint bug (genuinely resolved, not just the
  earlier inert mitigation), a highlighted-item A-glyph, and a custom mouse cursor
  overlay. See `re_notes/known_issues.md` issues #48/#50/#51/#52.

## Architecture

```
iw5sp.exe (unmodified game logic)
    │  loads d3d9.dll from its own directory first (standard Windows DLL search order)
    ▼
our proxy d3d9.dll                         ← real injection point, ships beside the exe
    │  forwards all real d3d9 exports to the genuine system d3d9.dll
    │  hooks IDirect3D9::CreateDevice (vtable) -> subclasses the real device's window
    ▼
XInput poll (linked by us, game has none)  → deadzone + response curve
    ▼
TWO separate per-frame injection points, because they run at different times:
    │  FUN_0057de60 (gameplay-simulation tick, halts while paused)
    │      — movement, look, buttons, ADS, Sprint, Hold Breath, Reload, weapon switch
    │        inject here
    │  WndProc subclass + a SetTimer-driven ~60Hz WM_TIMER (keeps running even while
    │  paused, since it's a plain Win32 window hook, not a D3D9 vtable)
    │      — Start's pause-menu open/close inject here (a real Present hook was tried
    │        first but confirmed dead — see re_notes/known_issues.md)
    ▼
real KeyDown/KeyUp kbutton calls — ADS, Reload, Sprint, Hold Breath (not raw usercmd
    bits); Sprint's real native duration/recovery timer and Extreme Conditioning's
    perk override both apply automatically once the real kbutton is driven, no custom
    timer layer needed (an earlier hand-rolled stamina/cooldown timer, built to work
    around a since-abandoned raw pm_flags-forcing approach, was removed 2026-07-19)
real Cbuf_AddText/Cmd_ExecuteString pair — confirmed working, but not the mechanism
    for weapnext/togglemenu (see re_notes/known_issues.md)
real hardcoded ESCAPE-key path + FUN_004396d0's open/close cases — Start's pause menu
real FUN_00541020 raw-keycode dispatch table + FUN_00438710 jump table — weapon switch
    and D-pad (+actionslot 1-4, data-driven by loadout: killstreaks/attachments/NVG)
synthetic keydown/keyup via PostMessage — Survival ready-up (F5), D-pad Left's
    AI-squadmate call-in ('4'), and Back's real +scores scoreboard (TAB) ONLY, the
    three deliberate exceptions to real-engine-calls-only input in this project; real
    native triggers not yet found for ready-up/squadmate call-in, and +scores turned
    out not to be a native kbutton at all (a plain keyboard bind read by the UI layer).
    Hold Breath went through this same detour for one debugging session (2026-07-20)
    before a live memory readback found the real fix and returned it to a genuine
    kbutton call above — see re_notes/known_issues.md issue #6/#24.
    ▼
real ForwardKeyToMenu (FUN_004d9850) call, generic keycode forward to whatever menu
    is active — D-pad Up/Down/Left/Right + A now drive real menu item navigation and
    select/drill-in-drill-out, keycodes read directly out of the decompiled
    FUN_004dfd30 dispatcher rather than assumed (task #22, see known_issues.md)
```

Every hook target is found via byte-pattern/signature scanning or live memory-diffing
at runtime — never a hardcoded address assumed stable across game updates or even
between two launches of the same build (several of this project's real kbutton/flag
addresses live in dynamically-allocated per-tick structures, not fixed static memory).
See `re_notes/iw5sp.md` for the complete reverse-engineering log: every function found,
every dead end ruled out, and why.

## Controller compatibility by mission/mode

Started tracking per-mission/per-mode live playtest status after a first
Campaign playtest session (2026-07-18) found controller support was solid
overall but genuinely uneven mission-to-mission — a single "Campaign works"
verdict would have hidden that. Full detail, including exact fallback
points and open questions, lives in `re_notes/compatibility_matrix.md`;
this is a condensed summary.

| Mode | Tested | Fully compatible | Partial (fallback needed) | Not yet tested |
|---|---|---|---|---|
| Campaign (17 missions) | 7 | 3 | 4 | 10 |
| Special Ops (16 missions) | 0 | — | — | 16 |
| Survival | tracked as one entry (map-independent) | Works well overall | 1 known issue (Predator missile post-fire aim, see below) | — |

Campaign missions confirmed fully compatible so far: Persona Non Grata,
Davis Family Vacation, Return to Sender. Partial (specific fallback points
only, not whole-mission failures): Hunter Killer (DPV aiming), Turbulence (a
scripted-freeze sequence bypassed by our movement hook), **Goalpost** (mortar
fire input not yet wired, plus an unconfirmed mounted-turret difficulty
question flagged for deep investigation — corrected 2026-07-19: these two
bugs were previously misfiled under "Back on the Grid" in earlier notes; a
dedicated zone-identification pass confirmed the actual mission/zone is
Goalpost/`hamburg.ff`, not Back on the Grid/`dubai.ff`, which is untested),
Mind the Gap (a vehicle-exit prompt gated on a bind this project doesn't drive
yet). See `re_notes/compatibility_matrix.md` for the full per-mission
breakdown and `re_notes/known_issues.md` issues #26/#27 for the underlying
bug detail behind each partial entry.

## Killstreak support

Full detail, including MP's full killstreak list for future reference, is
in `re_notes/killstreak_reference.md` — this is a condensed summary of the
Campaign-relevant, controller-tested subset only.

| Weapon system | Mission | Status |
|---|---|---|
| Boat | Hunter Killer | ✅ Working |
| UGV (minigun + grenade launcher) | Persona Non Grata | ✅ Working |
| Helicopter door gun | Return to Sender | ✅ Working |
| DPV (Diver Propulsion Vehicle) | Hunter Killer | ⚠️ Aim broken, movement works |
| Mortar | Goalpost *(corrected 2026-07-19 — previously misfiled under "Back on the Grid")* | ⚠️ Fire input not wired up |
| Mounted Browning M2 turret | Goalpost *(corrected 2026-07-19 — previously misfiled under "Back on the Grid")* | ⚠️ Works, but difficulty discrepancy under investigation |
| SMAW (dumb-fire and lock-on vs. aircraft) | Goalpost | ✅ Dumb-fire working; lock-on vs. aircraft ❓ unconfirmed — may not even be a real bug |
| Predator Missile | Survival buy-station killstreak | ✅ Launch fixed and confirmed working live; post-fire missile-guidance camera/aim still broken — see the Survival killstreak table above and `re_notes/known_issues.md` issue #29 |
| AC-130 | Iron Lady / Fire Mission (Spec Ops) | ❓ Not yet playtested |

Multiplayer's own killstreak system (3 strike packages, ~20+ rewards) is
untouched — MP support hasn't started at all, see "Known limitations"
below.

## Known limitations

See `re_notes/known_issues.md` for the full, actively-tracked list.

- Controller menu/UI navigation (D-pad item selection + A-select, including options
  screens' category/settings drill-in-drill-out, buy-station/armory lists, and
  slider VALUE adjustment) is implemented and live-confirmed (task #22), now
  including the main menu and title screen themselves — see the D-pad/A section
  above. Button-glyph prompts (real controller icons in hint text) are now shipped
  for both in-game hints and menu UI corner hints — see below — keyboard/mouse
  remains fully functional alongside controller either way.
- **Button-glyph UI prompts are shipped and confirmed live** (2026-08-01)
  for in-game interact hints (pickup/swap, buy-station, mantle, Reload, grenade
  throwback, Survival ready-up) and menu UI corner hints (Back/Friends) — a
  custom-hint-redraw overlay that suppresses the game's own native hint text and
  draws prefix text + a real controller-glyph icon + suffix text itself, in this
  project's own embedded font. This replaced an earlier, different plan (an
  in-font glyph-codepoint substitution via a boot-time zone splice) that was
  fully researched but abandoned in favor of the overlay technique actually
  shipped. The Special Ops modal corner-hint bug (previously an open gap here)
  is genuinely resolved — the earlier shipped mitigation had never actually
  activated in any logged session, and the real fix uses a safe native
  `getfocuseditemname()` signal instead. Also shipped in the same pass: a
  custom mouse cursor overlay, so the native software cursor no longer renders
  underneath these icons. See `re_notes/known_issues.md` issues #48/#50/#52.
- **The highlighted-item A-glyph (menu list navigation) is not shipped in full.**
  An audit found several screens previously assumed working had never been
  independently re-confirmed, and one (with its own calibrated position-table
  entry) was confirmed outright broken — a table entry alone was never actual
  proof a screen worked. Rather than risk showing a wrong glyph, the visible
  icon is gated behind an explicit, live-verified allowlist: currently the main
  menu, the Campaign hub, and the Leave Lobby/Choose Content Pack popups. Every
  other menu screen shows no glyph at all until individually re-verified and
  added to that allowlist — see `re_notes/known_issues.md` issue #51's
  per-screen coverage table for exactly what's confirmed, broken, or still
  unverified.
- **Vibration/rumble.** An earlier version's two native trigger hooks crashed
  the game at startup, so both halves were rebuilt on different mechanisms.
  Fire rumble hooks a single-call-site-safe, re-verified target found via a
  runtime byte-pattern scan. Damage rumble is deliberately not a hook — the
  candidate replacement target for it has an inconsistent calling convention
  across its 14 real callers, the same risk class that caused the original
  crash, so damage is instead detected by polling the local player's own real
  health field once per frame. A separate bug — this project loading a legacy
  XInput DLL whose own vibration output is a documented no-op on real Windows
  installs — made vibration silently do nothing even with both mechanisms
  working correctly; also fixed. Uses a sustain/decay envelope tuned for real
  ERM motor spin-up lag, with `FireIntensity` maxed at its software ceiling
  (`1.0`) — physically confirmed working. **Known gap: doesn't register hits
  absorbed by Survival's purchasable Body Armor** (a separate value from real
  health this project hasn't located in memory yet — an opt-in diagnostic scan
  exists for the next investigation pass). **Per-animation-step reload
  vibration** (matching CoD's historical console behavior — a distinct pulse
  per real animation beat, not just a generic reload cue) is a committed
  final-scope target, deliberately deferred; real per-weapon reload-timing
  data is already on record (`re_notes/iw5sp.md`) to build it from once a
  genuine "reload is actually happening" trigger is found. See
  `re_notes/known_issues.md` issues #24/#63.
- **Auto-mantle while sprinting — confirmed working (v0.3.3), still shipped
  disabled by design** (`[Movement] AutoMantleEnabled=0` in `mw3ncp_config.ini`,
  opt-in). Drives the same real native mantle command Jump already uses whenever
  the player is sprinting and pushing the stick fully forward within a
  configurable cone, gated on the game's own real mantle button-prompt hint being
  visible — an earlier version without that gate made the player jump
  continuously on flat ground, since the mantle command and Jump are literally
  the same input. Two earlier fix rounds (that gate, then a render/gameplay-tick
  timing-gap fix) couldn't be live-confirmed working; the real root cause was a
  coupling bug (the mantle-fire gate accidentally also required this project's
  own icon lookup to succeed) found and decoupled in v0.3.3. See
  `re_notes/known_issues.md` issue #62.
- Survival ready-up (hold Y) uses a synthetic F5 keypress rather than a real engine
  call — the real native trigger was never found despite an extensive search (see
  `re_notes/known_issues.md` issue #5); this workaround will be replaced if/when one
  turns up. **This is no longer the only such exception** — three more have since
  been added, each for the same reason (no real native call found despite a real
  search): D-pad Left's squadmate call-in (issue #14), Back's `+scores` (issue #28),
  and Y opening the Friends list from a menu (issue #50). All four follow the same
  standing convention: a real `WM_KEYDOWN`/`WM_KEYUP` posted directly at the game's
  own window, safe because this game has no DirectInput import at all — keyboard
  input is genuine window messages either way, so this is indistinguishable from an
  actual keypress. See `CONTRIBUTING.md`'s own list of these exceptions.
- **Aim assist was permanently removed (2026-07-20), not just disabled.** A
  from-scratch implementation (rotational friction, target magnetism) had its math
  confirmed correct but was never shipped functional, and has now been cut entirely
  as a deliberate risk-reduction decision: reading gameplay-entity memory to adjust
  aim is mechanically identical to a soft-aimbot regardless of intent, and this
  project's own VAC research found the closest real precedent for a proxy-DLL that
  manipulates gameplay state beyond input remapping (ENB) has actual documented ban
  history. See `re_notes/known_issues.md` issues #15/#16 and #33.
- **The custom in-game Options screen (task #23/#66) ships as a preview/WIP
  feature, off by default, not yet played.** The original plan — inject custom
  content into the game's own real menu system — hit a genuine architectural
  limit (loading real menu content live triggers unsafe GPU-resource creation
  outside the engine's controlled loading context) and was abandoned. Built
  instead as a fully custom-drawn replacement screen that draws entirely over
  the real Options screen and claims its own input — covers every real
  vanilla tab (Look/Video/Audio/Voice/Advanced Video/Movement/Actions) plus
  real keybind rebind capture and a custom controller-bindings drill-down.
  Compiles clean and follows every rendering/input pattern already
  live-confirmed elsewhere in this project, but genuinely has not been played
  — enable at your own risk via `[Options] UseCustomOptionsScreen=1` in
  `mw3ncp_config.ini` (no in-game toggle exists on purpose). Known open gaps:
  a real "window mode" (fullscreen/windowed/borderless) setting is still
  missing from the catalog, and correct scaling at non-1920x1080 resolutions
  is unverified. See `re_notes/known_issues.md` issue #66 for the full,
  itemized status.
- Multiplayer (`iw5mp.exe`) support has not been started. It's a separately-built binary
  from `iw5sp.exe` — none of the offsets/addresses found so far carry over, and it needs
  its own full signature-scanning pass. There's also an open, unresolved question about
  anti-cheat exposure from code injection on `iw5mp.exe` that needs to be discussed
  before that work begins.
- **Keyboard/mouse play is intended to be strictly additive and unaffected, but is no
  longer treated as a fully-verified, first-class input path.** A real regression was
  found and fixed (our own controller-support hooks silently broke native
  keyboard sprint entirely — see `re_notes/known_issues.md` issue #10) — the kind of bug
  that's easy to introduce with this project's hooking style and easy to miss unless
  someone happens to test keyboard specifically. Controller is the actively-verified,
  primary input method going forward; if you're mainly a keyboard/mouse player, keep a
  keyboard within reach and expect the occasional oddity while this project is installed.
  **This is not a suggestion to avoid the keyboard, though** — it's still required,
  not optional, for most killstreak call-ins and menu prompts this project hasn't
  mapped a controller button to, and as a fallback for Back until it's separately
  live-confirmed (menu navigation
  itself, including sliders and buy-station/armory lists, is fully controller-native
  as of task #22). A keyboard needs to stay reachable during any session either way.
  See `re_notes/known_issues.md` issue #11 for the full reasoning.

---

## Client compatibility

This project is built and verified only against **retail Steam MW3**. Long-term goal is
to support other MW3 client variants too, but none of the following are implemented
or tested yet — this table is research-stage only, not a compatibility claim. Full
detail in `re_notes/known_issues.md` issue #25.

| Client | SP/MP | Binary vs. retail | `d3d9.dll` injection viable? | Status |
|---|---|---|---|---|
| Retail Steam (Windows) | Both | — (baseline) | Yes (confirmed, current target) | Actively supported |
| Retail Steam via Proton (Steam Deck / Linux) | SP/Survival (unspecified) | Same retail binary, run under Proton | **Multiple user reports of it working** (2026-07-19, Reddit — including a direct "funciona de maravilla" ["works wonderfully"] report after a live test on real Deck hardware) — still not independently tested by this project itself | Community-confirmed but project-untested — plausible on its face (the whole architecture is a proxy `d3d9.dll` + standard Win32 calls, exactly the surface Proton translates), and now corroborated by more than one independent player. Treat as "works for players," not "verified/supported by this project," until reproduced here. |
| Plutonium — MP | MP | `iw5mp.exe` byte-identical to retail | Believed yes (same binary) | **Not recommended — see warning below** |
| Plutonium — SP | SP | `iw5sp.exe` is a different binary (2,320-byte size delta, ~175K individual differing byte positions across the file — corrected 2026-07-18, previously misstated as "~175KB smaller") | Unknown, would need independent address re-verification | Not yet investigated |
| AlterWare IW5-Mod | SP + Spec Ops | Separate `iw5-mod.exe` executable, not `iw5sp.exe` | Unknown, binary not yet acquired for analysis | Not yet investigated — most promising target given this project's SP-first scope, no known anti-cheat concern found |
| DeckOps (MW3) | MP (via Plutonium) | Same as Plutonium MP | Unknown — Proton/Wine's D3D9 translation layer untested | Not yet investigated — inherits the Plutonium MP warning below, plus unverified Proton behavior |

> **⚠️ Do not use this project with Plutonium multiplayer.** Plutonium's anti-cheat is
> confirmed (from its own documentation) to ban DLL injection and memory access —
> a 7-day ban on first offense, permanent after. This project's entire architecture
> (a proxy `d3d9.dll` and function hooking) is exactly what that system is built to
> catch, regardless of the project being input-only rather than a gameplay cheat.
> This is a real, confirmed risk, not a theoretical one — see
> `re_notes/known_issues.md` issue #25 for the evidence.

---

## Credits

This project vendors and links the following third-party library:

- **[MinHook](https://github.com/TsudaKageyu/minhook)** (`proxy_d3d9/third_party/minhook/`) — Copyright (C) 2009-2017 Tsuda Kageyu. BSD 2-Clause-style license (see `proxy_d3d9/third_party/minhook/LICENSE.txt`). Used for all API hooking (vtable and inline detours) in the proxy DLL.
- **Hacker Disassembler Engine (HDE) 32/64 C**, bundled with MinHook — Copyright (c) 2008-2009, Vyacheslav Patkov. Same style of license (see the same `LICENSE.txt`).

Full license text for both is reproduced verbatim in `proxy_d3d9/third_party/minhook/LICENSE.txt`.

This project also embeds the following font, bundled directly in the proxy DLL (`proxy_d3d9/proxy_d3d9.rc`) as a private, in-process-only font so it never depends on being installed on the user's system:

- **[Barlow Condensed](https://github.com/jpt/barlow)** SemiBold (Regular and Italic) — Copyright 2017 The Barlow Project Authors. SIL Open Font License, Version 1.1 (see `assets/fonts/BarlowCondensed-OFL.txt`). Used for the top-right on-screen notification text.

## License

This project's own source is released under a custom, permissive license — see
[`LICENSE`](LICENSE). The source is fully open: free to use, modify, and fork.
The one restriction is that neither this project nor any fork/derivative of it
may ever be sold or charged for — it must stay free for everyone. **Because of
that restriction, this license does not meet the OSI's formal "open source"
definition** (which requires no limits on commercial use) — it's an open,
freely-forkable, source-available license with one deliberate carve-out, not
an OSI-approved one. It does not grant any rights to Call of Duty: Modern
Warfare 3 itself; you need your own legitimate copy of the game to use this
project.

## Contributing

Contributions are welcome — see [`CONTRIBUTING.md`](CONTRIBUTING.md) for the
ground rules (native RE only, no hardcoded addresses, verify live, SP/MP are
separate efforts) and [`CODE_STANDARDS.md`](CODE_STANDARDS.md) for the
production-ready bar every change is held to (no placeholder hooks, no
half-finished work presented as done — applies identically to AI-assisted
code) before opening a PR.
