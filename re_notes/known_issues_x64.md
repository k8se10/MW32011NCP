# Known Issues — x64 migration line

This is a **separate, dedicated issue tracker for the x64 architecture line**,
split off from the main `re_notes/known_issues.md` on 2026-09-03. Rationale
(direct instruction): the x86→x64 recompile (see issue #1 below) produced a
single `known_issues.md` entry (formerly "#111") that grew into a
multi-hundred-line, actively-growing document on its own within one day —
clogging the main tracker's scannability for every other, unrelated x86-era
issue sitting above it. Splitting keeps the historical x86 tracker stable and
gives the x64 line room to grow the same way `known_issues.md` did for x86,
without one megaissue dominating either file.

**Numbering restarts at #1 here** — this is a fresh tracker for a fresh
architecture line, not a renumbering of the old file. Issue #1 below carries
forward the full technical content of the old file's issue #111 (MW3 (2011)
recompiled to x64) so that history and context aren't lost — `known_issues.md`
itself now carries only a short pointer stub for #111, per the same
"structure the history, don't delete it" convention `CODE_STANDARDS.md`
already establishes for that file.

**Same conventions as `known_issues.md`** apply here unchanged (see
`CLAUDE.md` §5 "Documentation Standards"): every entry opens with a
`**Status:**` line using the same fixed vocabulary (Open / Investigating /
Partially Resolved / Resolved / Deferred / Roadmap Idea), history is
structured as dated rounds after the current status rather than interleaved,
cross-references use the `issue #N` form, and a top-of-file index appears
once the issue count outgrows a single screen (not yet needed here).

**Cross-reference**: `re_notes/x64_migration/README.md` is the primary
scoping/progress document for the RE side of this work (sub-cluster passes,
raw Ghidra output files, the standing signature-scanning caution) — this file
is the `known_issues.md`-style curated issue record, same relationship the
two files already had for #111 before the split.

**Live-testing tracking, added 2026-09-12**: this file records what's
build-verified and (once tested) what's confirmed live, but doesn't track
"what still needs a real playtest" as a standalone, workable checklist —
that's [`re_notes/x64_live_testing_checklist.md`](x64_live_testing_checklist.md),
a living document seeded with every build-verified-but-untested item across
this whole session (grows as new fixes land, items move to its own
"Completed" section once actually confirmed). Direct methodology, same day:
"rapid push for parity then mass testing through everything done."

---

## Index

- [#1](#1-critical-mw3-2011-recompiled-to-x64----mod-completely-broken-every-hardcoded-address-invalidated) — CRITICAL: MW3 (2011) recompiled to x64 — mod completely broken — **D-pad Left synthetic-key exception AND a sniper Fire/ADS fix attempt both shipped (build-verified, neither live-tested yet); Plugin API ported (build-verified, needed no host code changes); visual-enhancement-suite x64 port attempted TWICE, still blocked on two addresses that resist exhaustive static RE (render-scale, clcState/in-level-flag) — likely needs live tracing, not more static analysis; FXAA/MSAA found to not even exist on x86, out of scope for parity; overlay-render bug fully audited — cursor's own unguarded x86 address landmine found and fixed, rest of the render path confirmed clean; Custom Options screen wired into x64's input pipeline (build-verified, temporary manual open-chord substitute pending a real focus-detection RE pass -- **RE pass now done 2026-09-12: real x64 menu-focus/itemDef-array offsets re-derived and cross-confirmed, TryGetRealFocusedGroupAndIndexX64 wired in as the real Options-screen open trigger alongside the chord (kept as a fallback), build-verified, not yet live-tested**); a "greenlit" trusted-plugin allowlist added to plugin_loader.cpp so the sibling MW32011NSP project's own security-fix plugin can ship built in by default (build-verified, end-to-end test pending that plugin's own existence); Sprint (L3) migrated from raw pm_flags-forcing to the real +sprint kbutton, matching x86's final design, plus the rising-edge stand-from-crouch/prone behavior (build-verified, not yet live-tested); native D-pad+A/B controller menu navigation ported (InjectControllerMenuNavX64/InjectControllerMenuBackX64, driven by a newly-resolved ForwardKeyToMenu equivalent, FUN_1402aac50) — main menu, pause menu, options drill-down, buy-station/armory lists, and B-back all now controller-navigable in principle, plus two real menu-active-gating conflicts found and fixed along the way (D-pad actionslot, CrouchProne/B dual-purpose) — build-verified, not yet live-tested; Auto-Mantle (while sprinting) investigated and found genuinely BLOCKED, not implemented — its real ledge-availability gate depends entirely on the native hint text-draw hook (x86's Hook_DrawGlyphText), which has no x64 equivalent yet (same separate, larger RE task blocking gameplay-hint glyph overlays generally), and a considered alternative (reading the engine's own raw mantle condition-flag memory directly) was deliberately rejected as a diverging, policy-adjacent workaround rather than a real port; Survival ready-up (hold Y) ported — same synthetic-F5-via-PostMessageA exception x86 already ships, direct port of SendSyntheticF5/InjectControllerWeaponNext's hold-vs-tap split, one honest scoped difference from x86 (the IsInSurvivalMode() mode gate is omitted, since x64's own Dvar_FindVar equivalent is a still-unresolved RE target — fires unconditionally on the hold edge instead, relying on the same "safe by construction" reasoning x86's own design already documents) — build-verified, not yet live-tested; Hold Breath (L3 while ADS'd) ported (parity audit item #23, closes the last confirmed-ABSENT control) — same no-explicit-sniper-check gating as x86 (`sprintHeld && adsHeldNow`), real kbutton struct resolved via the same anchor+offset technique (two independent angles: decompile + independently re-dumped raw LEA bytes), structurally a genuinely separate dedicated kbutton_t on x64 (not the internal-field alias x86's own address is), a real pre-existing early-return bug in Hook_SprintTick that would have silently starved Hold Breath's own edge check on steady ticks was caught and fixed in the same pass — build-verified, not yet live-tested; DualSense gyro-aim ported, same day, directly by the coordinator (not a fork) after the background session pool was paused for a low-token-budget check — turned out to be the identical "mechanism exists, never wired into the x64 tick" shape vibration/rumble already was: `Controller_GetGyroRate` (`controller_input.cpp`) has no arch guard at all, wired into `Hook_MovementTick`'s existing LOOK PRE-hook block, applied additively onto the yaw/pitch accumulators and the motion-blur delta feed after the stick-look branch (confirmed matching x86's own `=` then `+=` pattern in `InjectControllerLookAngles` before writing it), stays PREVIEW/WIP exactly as x86 does — build-verified (x64 rebuild, dumpbin-confirmed, Win32 regression clean, x64 redeployed last), not yet live-tested (needs real DualSense hardware). Back's `+scores` scoreboard port shipped 2026-09-13 (`SendSyntheticScoreboardKeyX64`, same hold-through-passthrough `PostMessageA(VK_TAB)` mechanism as x86's `InjectControllerScoreboard()`, a pure wiring port — x86's own function already had no arch guard, per the parity audit's row 30 finding — build-verified both platforms, correctly expected to remain a visible no-op in SP, see `known_issues.md` issue #28, real value only once Multiplayer ships). The gameplay glyph-icon native text-draw hook (the large remaining RE task, also blocked Auto-Mantle) shipped 2026-09-13, in two commits: a passthrough milestone hooking `FUN_14029a2b0` (the x64 equivalent of x86's `Hook_DrawGlyphText` target, found via the same `RawStringScan.java`-anchor technique x86's own discovery used, confirmed via 22 real callers), then Mantle-hint structural-match detection wired on top (`IsMantleHintCurrentlyShowingX64()`, resolving the real x64 `SEH_GetString` equivalent `FUN_14029f120` for a live, language-independent template match — NOT `real_settings.cpp`'s x64 `GetLocalizedString()` stub). This unblocks Auto-Mantle's own detection DEPENDENCY specifically; every visual glyph-icon SUBSTITUTION (Interact hints, Reload, Throwback, Sentry-Place, menu hints, x64's real `Font_s` struct layout) remains unimplemented — build-verified both platforms, x64 redeployed last, not yet live-tested. Full trail: `re_notes/x64_migration/drawtext_hook_x64.md`. **Auto-Mantle's own `+gostand`-forcing feature shipped later the same day (2026-09-13)**: `IsSprintActiveX64()` composed from three already-existing x64 tracking vars (`g_sprintKbuttonActiveX64`/`GetRealStanceX64()`/`g_adsHeldX64`, exact parity port of x86's own `IsSprintActive()`, no new RE needed) wired together with `IsMantleHintCurrentlyShowingX64()` and the same 750ms cooldown/stick-forward-cone check x86 uses, into `Hook_MovementTick`'s existing Jump raw-usercmd-bit block (same `kJumpUsercmdBit`/0x400) — ships off by default, build-verified both platforms, x64 redeployed last, not yet live-tested. See this issue's own "UPDATE 2026-09-13 (later same day)" round under the Auto-Mantle section for the full trace. **A separate, concurrent 2026-09-13 session then shipped real visual glyph-icon SUBSTITUTION** (not just detection) for Mantle, Pickup/Swap/PickupHealth, and Throwback grenade — `RequestCustomHintOverlay` now actually draws this project's own icon+text and suppresses the native draw for those three hint families, all detected via a purely structural template match (no font-name filtering needed); buy-station, Survival ready-up, Sentry-Place, and Reload remain genuinely unported (the first three lack any known reference-key template even on x86 or in this binary at all; Reload is confirmed to flow through a completely different native draw function this hook can't observe) — see this issue's own newest "UPDATE 2026-09-13 (a separate, concurrent session, same day)" round under the Auto-Mantle section, and `re_notes/x64_migration/drawtext_hook_x64.md`'s "Stage (c)," for the full trail — release ETA 2-4 weeks, gated on x86 parity**

---

## 1. CRITICAL: MW3 (2011) recompiled to x64 -- mod completely broken, every hardcoded address invalidated

*(Carried forward from `known_issues.md`'s former issue #111, opened 2026-09-03. Original numbering/history preserved in that file's own trimmed stub entry.)*

**Status: Foundation confirmed working, end to end, live; Sprint and
Movement — the first two real gameplay hooks — now CONFIRMED WORKING LIVE
(2026-09-04).** A real x64 build compiles, LINKS, deploys, LAUNCHES
cleanly, and its diagnostic hook FIRES DURING REAL GAMEPLAY — two real
startup crashes were found and fixed first (see "First/Second live crash,
found and fixed" below); the third launch reached a clean main-menu session
with no crash; a later session reached live Pmove-ticking gameplay and
`proxy_d3d9.log` shows the diagnostic hook firing 5 times in a row with a
clean call-through each time. **The entire signature-scan → MinHook →
detour pipeline is live-confirmed on this x64 binary, not just
build-verified.** On top of that proven foundation, Sprint and Movement
were wired in, hit one real deployment bug along the way (a shared-`OutDir`
platform-switch redeploy silently leaving an x86 DLL loaded — found, fixed,
documented below), and are now direct-user-confirmed working together in
real gameplay: "movement and sprint work." See "Sprint + Movement hooks
implemented" below for the full record. **Look (right stick) is now also
implemented AND CONFIRMED WORKING LIVE** (direct user report: "works") — a
direct-write port of x86's own current accumulator-write design, folded
into the same Movement hook (MinHook only allows one detour per target),
including a real new `SigScan::ResolveRipRelative` capability to resolve
the angle-accumulator DATA globals without a hardcoded offset. Sprint,
Movement, and Look are now all live-confirmed working together on x64.
**Buttons/ADS/Reload, Pause toggle, and Weapnext are now ALSO implemented**
(direct instruction: "do all in one pass") — all three via direct calls
into confirmed, self-contained real engine functions (not MinHook detours),
polled from the same per-tick orchestration point Look uses. **This
immediately exposed a real, separate, pre-existing bug**: the game crashed
on launch before even its splash video (`0xc0000409` STATUS_STACK_BUFFER_
OVERRUN, root-caused via Windows Event Viewer + `dumpbin /disasm` to a
`sprintf_s` overflow in `signature_scan.cpp`'s OWN logging code — the new
`kWeaponNextSignature` string is 242 characters, longer than the fixed
`char buf[256]` its log line formats into, a latent bug in place since that
file was first written, never triggered before because every earlier
signature was short enough) — found and fixed (buffers bumped to 1024
bytes). **A real live playtest after that fix found two more real bugs**,
both fixed the same day: Pause could open but not close (its poll only ran
from the gameplay tick, which halts entirely while paused — fixed by also
polling from the always-on menu tick, matching x86's own real fix for this
identical bug class years earlier); and Fire/ADS/Reload did nothing at all
(a genuine misread of `FUN_14007eaf0`'s own parameter semantics — fixed by
routing through `FUN_14007c3a0`, the real case-number dispatcher, the same
way Pause/Weapnext already do). **A THIRD live playtest after that found
two more real bugs, both fixed via the same root cause**: Fire fired once
then stopped, and ADS came out as a toggle instead of hold — both traced
to `FUN_14007c3a0`'s own down/up cases tail-calling `FUN_14007e460`/
`FUN_14007e490`, real dual-source kbutton handlers whose second argument
is a source identifier, not an isDown boolean — fixed by calling those two
functions directly with a consistent synthetic id, which also eliminates
ADS's unwanted toggle side effect as a free consequence (that toggle only
existed inside `FUN_14007c3a0`'s own case dispatch, now bypassed
entirely). **A THIRD playtest confirmed Fire and Reload both working live
("fire reliable", "reload works"), and Pause fully confirmed
open+close ("pause unpause works")** — but found ADS broken in a NEW way
(did nothing at all): live evidence that `DAT_1406e26e0` (the flag
`FUN_14007c3a0`'s own case dispatch toggles) genuinely IS the real "is
aiming down sights" state, not safely bypassable as assumed. Fixed by
resolving that flag too and FORCING it directly to the desired absolute
value on the edge, on top of the existing kbutton call. Full trail in
"Real crash found and fixed," "Real live playtest," "Second live
playtest," and "Third live playtest" below. **Sprint, Movement, Look,
Pause (open+close), Fire, Reload, ADS, and Weapnext are now ALL CONFIRMED
WORKING LIVE** ("ads fixed now") — every gameplay hook this issue's own
"next steps" list ever named is now live-confirmed. **A separate, older-
class symptom resurfaced the same session**: "we have the original issue
where we need to click in to get input (that internal focus mechanism)" --
the same SYMPTOM CLASS as x86's own real, already-fixed "needs an initial
click at launch" bug, but confirmed NOT actually fixed by that same
mechanism on x64 (the x86 fix's own log line already fires every x64
session, the symptom persists regardless) -- a genuinely new, open x64
investigation, not a regression of a closed issue. An experimental real-
OS-focus-call addition (`SendRealFocusNudgeX64`, x64-only) has been added
and deployed but is **NOT YET LIVE-TESTED** -- see "x64 focus-gate
symptom" below.
**Emergency policy action, same day: all support for the entire existing
`-x86` release line (every version through `v0.3.5-x86`) is discontinued,
effective immediately** — not a gradual wind-down, since the live game can
no longer run a 32-bit build at all. This includes withdrawing `v0.2.2-x86`'s
Current LTS status and `v0.3.5-x86`'s LTS candidacy outright, outside this
project's normal LTS promotion/demotion process — see `LTS_POLICY.md`'s own
emergency-escalation note for the full policy record. **Further escalated
2026-09-04**: every `-x86` release archived/unpublished on both Nexus and
GitHub (all 15 GitHub Releases converted to Draft) — "no release works and
as such i refuse to serve it." Nothing deleted on either platform, just no
longer downloadable through either one's normal listing. Releases now carry
a `-x86`/`-x64` suffix, versioning reset to `v0.0.1-x64` for the new
architecture line once it ships.
Full technical record and reconnaissance in `re_notes/x64_migration/README.md`
(plus its own linked sub-passes for Sprint/weapnext, pause-menu/key-handler,
and D-pad actionslot/dvar-API) -- this entry is the known_issues-standard
summary/index pointer.

**Progress snapshot, same day**: real x64 build infrastructure exists
(`.vcxproj` x64 configs, MinHook confirmed working, D3D9 export forwarding
rebuilt as real MASM since `__declspec(naked)`+inline `__asm` don't exist on
x64 at all) -- but the ~14 hand-written asm hook-install trampolines
(`Hook_0057de60` and friends) need real per-hook x64 redesign before a
functional build is possible, deliberately not rushed. Static RE has found,
with varying confidence (none live-verified yet, no x64 build exists to test
against): the movement/look pipeline (high confidence), the bind-name table
and a simpler-than-x86 buttons/ADS KeyDown/KeyUp mechanism (high confidence),
the visual-suite dvar catalog (high confidence, all 5 storage globals),
**Sprint's full Pmove-entry hook chain (RESOLVED, static, high confidence)**,
the generic dvar read/write API (high confidence, real x64 simplification --
no custom calling convention needed at all), and **D-pad actionslot (upgraded
to medium-high confidence)** -- the entire kbutton-table function cluster is
mapped (setter, `IsKeyButtonDown` by index/by name, `ClearAllKeyButtons`) and
no separate raw-dispatch table exists, though the actual "use item" consumer
is still not found, plausibly GSC-VM state.

**Key-event/menu-dispatch cluster, same day, multiple follow-up rounds**:
`FUN_1402aac50` (the x86 `FUN_00541020` equivalent, confirmed via the exact
same `"screenshot"`-dev-command anchor technique the original x86 discovery
used) is confirmed as a real `Menu_KeyEvent`-equivalent -- early-outs
entirely when no menu is active, covers Tab/Enter/ESC/developer-toggle/
screenshot/item-select. Its wrapper `FUN_14029baa0` traces back to
`FUN_14007eaf0`, the SAME buttons/ADS KeyDown/KeyUp setter -- meaning that
one function appears to be the single unified real entry point for injecting
any key/bind event on x64 (menu/ESC dispatch included), a real architectural
simplification over x86's several separate specialized functions.

`FUN_1402c5b30` is confirmed as the real x64 `Cvar_Set` equivalent (4
independent real call sites). The real resume-gameplay path is confirmed:
once the active-menu stack empties, `FUN_14029baa0` runs two cleanup calls
then `Cvar_Set("cl_paused", 0)`, matching x86 `FUN_004396d0`'s `mode==0` case
functionally. `FUN_1402ac9c0` was ruled OUT as `OpenPauseMenu` -- it's a bulk
close/refresh pass over a distinct registered-menu-defs array.

**`SetMenuState`/`OpenPauseMenu` CONFIRMED**: `FUN_14029f3f0(player, mode)`
is a real, full `SetMenuState` equivalent -- ten named destination screens,
each opened via a new confirmed primitive, `FUN_1402ad950(ctx, name)`
(`OpenMenuByName`): mode 0=resume, mode 1=main/error-popmenu, **mode
2="pausedmenu" -- the real `OpenPauseMenu`**, mode 3=pregame/loaderror, mode
4=endofgame, mode 6=briefing, mode 7=victoryscreen, mode 0xb=coop_lobby,
mode 0xc=levels_challenge, mode 0xd=main_text, mode 0xe=main_specops.
`FUN_140082e70` turned out to be a connecting/loading-state ESC-cancel
handler, not the general pause-open path.

**`ForwardKeyToMenu` CONFIRMED** (`FUN_1402a3ca0`, the `LAB_1402ab2d1` sink
in `FUN_1402aac50`) -- reached from many switch cases, not just ESC,
confirming it as a real, generic item-action-execute primitive.

**Genuinely still open, this cluster**: `FUN_14007eaf0`'s ESC branch only
reaches mode-2 `SetMenuState` via one specific connection-state value
(`iVar3==6`), and whether that's really the live SP-gameplay/active state
(vs. an MP-briefing-specific one, given mode 6's own name is "briefing")
isn't pinned down statically -- needs a real `cls.state` enum value dump or
live testing. **Follow-up**: found real write sites for states `1`, `4`,
`6`, `7` on the underlying field (`DAT_1406e2558`, 92 refs/54 functions
project-wide, too broad to fully map) -- state 6's writer is gated on a
pending-job-count check, the shape of an in-progress loading/connect step,
raising a genuine possibility that this whole ESC branch is a
loading-screen-specific cancel/pause prompt rather than the general
live-gameplay Start-button pause.

**RESOLVED (practically) -- the real Start-button pause toggle found,
independent of the `cls.state` mystery entirely.** Followed through on the
alternative flagged above: decompiled `FUN_14007c3a0`, confirmed as the
real x64 equivalent of x86's `FUN_00438710` (the generic case-number
command dispatcher). Within its large, fully-mapped `switch` (dozens of
real gameplay commands, mostly KeyDown/KeyUp pairs on a second, separate
per-bind state system from the kbutton table): **case `0x43` =
`FUN_1400823b0`, confirmed as the real live-gameplay pause TOGGLE** -- reads
the current `SetMenuState` mode and calls `SetMenuState(player, 2)` (open)
if not already paused, or `SetMenuState(player, 0)` (resume) if it is. This
resolves the practical question with high confidence, independent of ever
pinning down `cls.state`'s exact semantics -- live Start-button pause almost
certainly routes through this generic case-dispatch path, not the
ESC-specific `cls.state==6` branch (the two are separate mechanisms,
matching x86's own historical ESC-vs-Start split). Case `0x42` was checked
against the hope it might match x86's own weapnext case number (also
`0x42`) and initially dismissed as coincidence -- **corrected below (see the
weapnext thread): it's not a coincidence, `FUN_14007c3a0`'s case numbers are
directly the bind-name-table indices, and case `0x42` really is weapnext's
real dispatcher.** Full detail in `re_notes/x64_migration/README.md`
section 1g. Not live-tested.

**Sprint thread -- RESOLVED, static, high confidence**: `FUN_140014a80` is
the real Pmove-entry sprint-bit WRITER (`*(uint*)(lVar3+0xc) |= 0x4000`, the
confirmed `pm_flags`-equivalent), gated on a real held-input check
(`param_1+0xc & 2`) plus the already-confirmed `player_sprintUnlimited`
bypass and a chain of real state exclusions. Traced two levels up:
`FUN_1400168a0` (Pmove per-substep tick, calls `FUN_140014a80` from every
movement-type branch) is called from `FUN_140016620` (the outer Pmove
frame-subdivision wrapper, sub-steps capped at 66ms -- the exact classic id
Tech/Quake3-lineage `Pmove()` constant, strong independent corroboration
this chain really is Pmove). This is the real, confirmed x64 equivalent of
x86's `InjectControllerSprintPmFlags`/`ReassertSprintPmFlags` hook target --
two real options once live testing is possible: hook `FUN_140014a80`
directly, or set the held-input bit at `param_1+0xc` before the chain runs
and let the existing native logic do the rest. Full detail in
`re_notes/x64_migration/sprint_weapnext_x64.md`.

**weapnext thread -- RESOLVED, static, high confidence.** Bind-table position
found (index 66, no `kbutton_t` counterpart, one-shot command). The full
top-level input-event architecture was mapped first: `FUN_14007eaf0`'s own
caller is `FUN_14023ccb0`, a real, confirmed `Com_EventLoop`/
`Sys_SendKeyEvents`-equivalent pulling typed events from `FUN_1402ee660`
(confirmed `Sys_GetEvent` -- a 256-slot ring buffer with a genuine
`PeekMessageA`/`GetMessageA`/`DispatchMessageA` Win32 message-pump fallback)
and dispatching by a 4-way type selector -- 3 of 4 types were ruled out
(char-typed, console-command-text, `Com_Error`), leaving only the kbutton
path (`FUN_14007eaf0`) structurally possible.

Within that path, `FUN_14007eaf0` reaches `FUN_14007c3a0` (the real x64
equivalent of x86's `FUN_00438710`, confirmed while investigating the
pause-menu cluster) via a per-keycode "bound index" field
(`DAT_140644a6c`, literally `DAT_140644a64+8` -- kbutton_t's own third `int`
field: `{down, count, boundIndex}`). Tracing `FUN_14007eff0` (the real
string->bind-name-table-index resolver every `Key_*` helper calls) and
`FUN_14007f330` (`Key_SetBinding`, which writes that same resolved index
into the per-keycode slot) confirmed `FUN_14007c3a0`'s case numbers ARE
bind-name-table indices directly -- a real, unified x64 architecture, not a
separate case-ID layer. **This corrects an earlier same-day dismissal**:
case `0x42` in `FUN_14007c3a0` was initially checked against weapnext's own
computed index (66 = `0x42`) and dismissed as coincidence -- wrong, since
the match is a direct mechanical consequence of the shared indexing, not
luck. `case 0x42` = `FUN_1400706d0`, confirmed as weapnext's real dispatcher
by decompiling its own callee `FUN_140074570`: a genuine weapon-slot-cycling
function (15-entry array, `%0xf` wraparound, a real forward/backward
direction parameter, ammo/holdability gates, a real weapon-switch call).
`FUN_1400706d0` = the real x64 equivalent of x86's `FUN_004a5f70`;
`FUN_140074570` = the real equivalent of x86's `FUN_0057a670`. Full trace in
`re_notes/x64_migration/sprint_weapnext_x64.md`. Not live-tested.

**D-pad actionslot -- upgraded to medium-high confidence**: the ENTIRE
kbutton-table function cluster is mapped via `FindGlobalRefs.java` -- only 4
functions touch the table project-wide: the confirmed setter
(`FUN_14007eaf0`), `IsKeyButtonDown` by index (`FUN_14007f1b0`), the same by
name (`FUN_14007eab0`), and `ClearAllKeyButtons` (`FUN_14007eeb0`). No
separate raw-dispatch table exists anywhere, confirming actionslot is "just
another kbutton" at the table level, unlike x86's dedicated, separately-
discovered `ActionSlotDown`/`ActionSlotUp` pair. The actual "use the
equipped item" consumer is still not found -- plausibly lives in GSC-VM
script state (the same category this project's plugin-API policy already
reserves for live game-state reads) rather than anywhere in this native C++
layer.

**Render-scale/shadow-map thread**: found the x64 render-target orchestrator
(`FUN_1401b8c80`, equivalent to `FUN_004b60a0`) and confirmed its 5 real
callers create `SAVED_SCREEN`/`FLOAT_Z`/`SSAO`/`SSAO_BLURRED`/
`SSAO_FLOAT_Z` -- but, matching the x86 investigation's own 9+-round
conclusion exactly, none of them pass the shadowmap indices (0/1) either.
Now confirmed independently in TWO binary generations, raising real
confidence the actual creation site is reached via an indirect call
(a function pointer), which direct-xref static tooling structurally can't
find -- a genuinely different technique is the real next step, not more
scanning.

**Symptom**: MW3 (2011) received a genuine Steam update between 2026-08-29 and
2026-09-03 -- the first real binary update in this project's entire history.
Both `iw5sp.exe` and `iw5mp.exe` were recompiled from **x86 (32-bit) to x64
(64-bit)** -- confirmed directly from the PE header's `Machine` field
(`0x8664`, `IMAGE_FILE_MACHINE_AMD64`) via two independent tools (raw hex
inspection and the Unix `file` command), not inferred or taken on hearsay.
This is on the default Steam branch (no `BetaKey` in the local
`appmanifest_42680.acf`), not an opt-in beta -- no legacy 32-bit branch was
found to exist.

**Root cause of the break**: this project's `proxy_d3d9.dll` is a hard Win32
(x86) build (a foundational architecture decision confirmed 2026-07-13 and
never revisited until now). A 32-bit DLL cannot be loaded into a 64-bit
process under any circumstance -- the OS loader rejects the architecture
mismatch before mapping the file at all. This is not "some hardcoded
addresses shifted" (the ordinary risk hardcoded addresses already carried
across game updates) -- the entire injection technique stopped applying in
one step. Confirmed the mod has not actually run since the update:
`proxy_d3d9.log`'s own last-write timestamp is 2026-08-29 06:08, before the
update, with exactly one session boundary in the whole file.

**What survived**: the existing 167MB Ghidra project
(`re_notes/ghidra_project/iw5sp_proj.gpr`+`.rep`) keeps its own internal copy
of the original x86 binary regardless of what happened to the exe on disk --
no RE work is lost. A backup of the true original 2026-07-13 x86 binaries
(recovered from a user-side zip, internal file dates confirm 2026-07-13) and
the new x64 binaries are both now preserved at
`re_notes/x64_migration/binaries/`. `d3d9.dll` is still the real graphics
API (confirmed via `dumpbin /imports`) -- the injection technique itself is
still structurally valid, it just needs an x64 build. No `xinput`/`dinput8`
import was added -- the project's founding 2026-07-13 premise (this game has
zero native controller input path) still holds exactly as it did on day one.
A 10-string persistence check (dvar names, function-adjacent identifiers)
came back identical in both binaries for every real hit -- strong evidence
this is a genuine recompile of the same underlying engine/data, not a
rewrite, meaning the existing decompiled *understanding* of what each
function does very likely still transfers even though every address needs
re-finding.

**Two locked decisions made the same day** (see `CLAUDE.md`/`AGENTS.md` for
the full record, both files mirror this): (1) the project is redefined --
from "native controller project" first to "native enhancement project,"
then refined same day to "native controller (and enhancement) project" once
the user asked to keep "controller" in the name -- formalizing what it had
already organically become (visual-enhancement suite, stutter/threading
work, plugin API) rather than narrowing scope; (2) the hardcoded-address
policy (locked 2026-08-25) is reversed again, back to signature scanning --
resolved once at process startup and cached, not a continuous re-scan loop,
given the demonstrated failure mode a hardcode-only approach just hit. This
does not resolve the original VAC-risk reasoning behind the 2026-08-25
policy; it's superseded by explicit instruction given real-world necessity,
not a rebuttal of that reasoning.

**Also same day**: a new release-naming convention was adopted (`-x86`/
`-x64` suffix, versioning reset to `v0.0.1-x64` for this new architecture
line) and, later the same day, escalated to an emergency full discontinuation
of all `-x86` support -- see the Status line above and `LTS_POLICY.md` for
the complete policy record.

**Implementation begins, 2026-09-03 (direct instruction: "now we start
implementing and then getting this closer to parity though i acknowledge
this is probably multi week work"). First real, working x64 build.**

- **`signature_scan.h`/`.cpp` (new, platform-agnostic)**: the real runtime
  AOB (Array-of-Bytes) byte-pattern scanner the locked 2026-09-03 policy
  requires (`CLAUDE.md` SS5/SS10.3) -- every x64 hook target is resolved
  through this, once at startup, cached, never a repeated re-scan loop.
  Parses a `"48 83 3D ?? ?? ?? ?? 00"`-style pattern string, scans the
  game's own main module (resolved via a real PE-header walk, not
  `psapi.h`), and fails loudly (per SS5's own standard) on zero matches OR
  more matches than expected -- an ambiguous signature is refused, not
  silently guessed.
- **New Ghidra script, `DumpSigBytes.java`**: dumps a function's real raw
  instruction bytes plus Ghidra's own PC-relative/reference analysis per
  instruction, as a starting point for building an actual signature. **Real
  tooling lesson found while using it**: its reference-based heuristic
  produces false positives on RSP/RBP-relative operands (a `LEA
  RBP,[RSP-0x80]` or `MOVAPS [RSP+0x120],XMM10` is NOT an address that
  shifts between builds, just a small fixed stack displacement) -- Ghidra
  attaches a reference to these too, but they don't need wildcarding.
  Always hand-review the suggested mask; only true RIP-relative/absolute
  operands (a real global reference, or a CALL/JMP rel32) actually need it.
- **`analog_input_hooks_x64.cpp` (new)**: the designated home for every
  real x64 hook going forward, parallel to (not merged into) the existing
  x86 file. First deliverable: a single, deliberately zero-behavior-change
  diagnostic hook on `FUN_1400168a0` (the confirmed Pmove per-substep
  tick) -- signature-scans it, installs a MinHook detour that logs a
  rate-limited fire count (first 5 calls, then every 5000) and calls
  straight through to the real function unmodified. Matches this project's
  own established "trivial passthrough first, to isolate plumbing bugs
  from real-effect bugs" convention (the visual-suite Phase A precedent).
  Proves signature-scan -> MinHook-install -> detour-fires works end to
  end on this exact binary before any real gameplay hook goes in on top.
- **A real, substantial bug found and fixed while wiring this up**: the
  existing x64 build infrastructure (MASM export-forwarding stubs, built
  earlier this same day) had never actually reached the link stage before
  today, since the naked-asm compile errors blocked it first. Once those
  were fixed, linking exposed a genuine, previously-undiscovered bug: the
  `g_real_D3DPERF_BeginEvent`-class globals `forward_stubs_x64.asm`
  references via plain `EXTERN name:QWORD` were declared inside dllmain.cpp's
  own anonymous namespace, giving them C++-mangled internal linkage a
  separately-assembled MASM translation unit can never match (LNK2019 on
  all 15). Fixed by moving them outside the namespace with real `extern "C"`
  linkage -- correct and necessary for x64, harmless for x86 (unchanged
  behavior there, only ever referenced from the same file either way).
- **Porting `analog_input_hooks.cpp` itself for x64 without touching x86**:
  the file's ~11000 lines interleave genuinely cross-platform utility
  functions (glyph editor exports, menu-active queries, controller-activity
  tracking -- dozens of symbols other translation units depend on) with
  x86-only hook-callback logic throughout, not separable into one
  contiguous block. An initial attempt to exclude the WHOLE file from the
  x64 build was too blunt and broke those real cross-file dependencies
  (confirmed via a real link failure, ~29 unresolved externals) --
  corrected by individually guarding each of the ~12 real
  `__declspec(naked)`/inline-`__asm` sites (8 naked hook trampolines, 4
  plain helper functions: `CallKbuttonDown`/`Up`, `GetDvarInt`/`Float`) plus
  the whole `InstallAnalogInputHooks()` function body (every one of its
  `MH_CreateHook` calls targets an x86 hardcoded address, meaningless on
  x64 regardless of `__asm` use) with `#if !defined(_M_X64) &&
  !defined(_WIN64)`, same pattern `dllmain.cpp`'s own `FORWARD_STUB` macro
  already used. Everything else in the file -- the real majority of its
  content -- now compiles for x64 unmodified, restoring every symbol other
  files needed.
- **Real result, both platforms verified building clean**: `x64` Release
  now compiles AND links a complete, real `d3d9.dll`
  (`PE32+ ... x86-64`, confirmed via `file`), deployed to the live game
  install directory for the first time in this project's history. `Win32`
  Release was re-verified to still build clean afterward -- no regression
  from any of the above. **Not yet live-tested** -- no x64dbg/live-attach
  session confirmed the diagnostic hook actually fires in the running
  game; the next real step is launching MW3 with this build and checking
  `proxy_d3d9.log` for `"[x64-diag] Pmove tick hook fired"`.
- **Explicit scope note**: this is genuinely the first slice of a
  multi-week effort, not a finished port. No real gameplay behavior
  (movement/look/buttons/ADS/Sprint/etc.) is hooked yet -- only the
  no-op diagnostic. Real per-hook work (starting from the now-confirmed
  targets: Sprint's `FUN_140014a80`, the unified buttons/ADS/menu entry
  point `FUN_14007eaf0`, the pause toggle `FUN_1400823b0`, weapnext's
  `FUN_1400706d0`) is the next phase, once the diagnostic hook's live fire
  is confirmed.

**First live crash, found and fixed, 2026-09-04.** Direct user report: "crashes
on startup." First real launch of the x64 build against the live game.

- **Diagnosis**: `proxy_d3d9.log` showed the pipeline got surprisingly far --
  `Direct3DCreate9`, `CreateDevice` (real backbuffer 2560x1440), `WndProc`
  subclass, `EndScene` hook confirmed firing, glyph-icon prewarm, one
  successful `DrawPrimitiveUP` call -- then stopped mid-frame with no crash
  annotation. Windows Event Viewer (`Get-WinEvent -FilterHashtable
  @{LogName='Application'; ProviderName='Application Error'}`, this
  project's own established crash-diagnosis technique) showed two identical
  Application Error entries: `iw5sp.exe` faulting in `d3d9.dll` (this
  project's own DLL) at the exact same offset both times, exception
  `0xc0000005` (access violation) -- a real, deterministic, reproducible bug
  in this project's own code, not a flaky game issue.
- **Root cause, found via `dumpbin /disasm` + the build's own `.pdb`**: the
  fault RVA (`0x4F30`, resolved against the build's real image base via
  `dumpbin /headers`) disassembled to `mov eax, dword ptr [0B36210h]` -- a
  raw, hardcoded x86-only absolute memory address
  (`kMenuActiveGateAddr`/`IsMenuActive()`, the "is a menu currently open"
  gate bit), read unconditionally. That address was only ever valid in the
  OLD 32-bit process's address space; in the x64 process it points at
  unmapped memory. This function has NO `__asm`/naked code at all, so the
  earlier same-day `__asm`-only guard pass (see "Implementation begins"
  above) never caught it -- it compiled cleanly and crashed at runtime the
  first time it actually ran. Reached via its exported wrapper,
  `IsMenuActive_Exported()`, called every frame by `overlay_hud.cpp`'s
  glyph-draw gate logic -- genuinely x64-reachable, unlike most of this
  file's other x86-only helpers.
- **Real scope, once actually audited**: this was not an isolated bug. A
  full, systematic sweep of `analog_input_hooks.cpp` found the same class of
  landmine in roughly 30 more places -- direct raw-address dereferences
  (`*reinterpret_cast<...*>(0x00......)`) and calls through global
  x86-address function pointers (`WeaponNext`, `SetMenuState`,
  `CbufAddText`, `ForwardKeyToMenu`, `ActionSlotDown`/`Up`, `LoadZones`,
  `OpenMenuByName`, `ToggleStance`, `GetDvarString`, etc.), scattered
  throughout functions the earlier `__asm`-only guard pass never touched
  because none of them use inline assembly. Found via two complementary
  methods: (1) grepping for every raw-address dereference/function-pointer-
  call pattern and checking each containing function's guard status against
  a script-generated preprocessor-depth map, and (2) the far more reliable
  method once the first crash was fixed -- iteratively rebuilding for x64
  and fixing each real `error C2065`/`C3861` (undeclared identifier) the
  compiler reported, which exhaustively finds every symbol reachable from
  genuinely unguarded code (a compile error can't be missed the way a manual
  grep audit can).
- **Fix approach, case by case**: for every function confirmed to have NO
  real external caller outside `analog_input_hooks.cpp` (checked via a
  cross-file grep for each function name, filtering out comment-only
  mentions), the whole function was guarded `#if !defined(_M_X64) &&
  !defined(_WIN64)` -- dozens of functions this pass (`GetRealStance`,
  `IsSprintActive`, `InjectControllerButtons`, `InjectControllerSprint`,
  `InjectControllerLookAngles`, `InjectControllerMenuBack`,
  `InjectControllerMenuNav`, `InjectControllerDpad`,
  `IsInSurvivalMode`, `GetTopmostActiveMenu`, `GetMenuStackDepth`,
  `TryGetStableFocusedGroupAndIndex`, `GetRawTopOfStackMenu`, several debug/
  test-only functions, and more). **Three functions were confirmed
  genuinely externally-reachable and given real x64-safe bodies instead of
  a wholesale guard** (matching the already-established `GetDvarInt`/
  `GetDvarFloat` pattern from the earlier same-day pass): `IsMenuActive()`
  now returns `false` on x64 (the crash fix itself), `GetMenuStackDepth()`
  returns `-1` (an honest "unknown" sentinel, only reached via a debug-log
  argument), `TryGetStableFocusedGroupAndIndex()` returns `false` (its own
  real "no stable focus" contract, callers already handle it).
  `InjectMenuInputTick()` (confirmed called every `WndProc` message from
  `d3d9_hook.cpp` -- the always-running menu/pause input tick) got surgical
  internal guarding instead: its genuinely safe, already-cross-platform
  calls (`Controller_RequestPoll()`, `CheckConfigHotReload()`,
  `TickOverlayTestCycle()`) stay unconditional; only its x86-only middle
  section (menu navigation, font-patch debug tests) is excluded on x64.
  `ResetMenuListItemOrdinalForFrame()` (confirmed called every frame from
  `overlay_hud.cpp`'s `Hook_EndScene`) got the same surgical treatment: its
  real glyph-positioning logic stays active, only its F4 AI-suppression
  debug toggle (a real gameplay feature, not a diagnostic -- deliberately
  left fully disabled rather than given a fake stub, since there's no
  honest safe behavior for "toggle AI spawn" other than "do nothing until
  ported") is excluded.
- **Verification**: both x64 and Win32 configurations rebuilt clean from
  scratch (`/t:Rebuild`) after the fix, zero errors. Confirmed via
  `dumpbin`-derived raw byte-pattern search across the compiled x64
  `d3d9.dll` that the specific crashing instruction's byte encoding (and
  the byte encodings of several other previously-dangerous raw addresses)
  no longer appears anywhere in the binary. **Not yet re-confirmed with an
  actual live launch** -- this is real, methodical build-time verification,
  not a live-tested "it works now" claim.
- **Standing lesson for any future x64 porting pass in this file**: the
  `__asm`/`__declspec(naked)` sites are NOT the only x64 hazard in this
  codebase -- any function reading raw process memory at a literal x86
  address, or calling through a global function pointer initialized from
  one, is equally dangerous and produces ZERO compile-time warning on x64
  (a `reinterpret_cast<T>(0x00XXXXXX)` is completely valid C++ regardless
  of target architecture; it just point at garbage on a different memory
  layout). The reliable way to find these is NOT a one-time grep audit
  (easy to miss transitive call chains) but the iterative "rebuild → fix
  every real compile error → repeat until clean" loop, since a genuinely
  unreachable x86-only helper never surfaces as a compile error once its
  own callers are correctly excluded, while a genuinely x64-reachable one
  always will. **This lesson understated the real scope, corrected below**:
  the compile-error loop only catches raw-address DEREFERENCES and calls
  through a NAMED global function pointer that the compiler can see is
  undeclared once its x86-only home is guarded out -- it does NOT catch a
  call through a function pointer that's still validly DECLARED (unguarded)
  but crashes when actually INVOKED. That gap is exactly what caused the
  second crash below, in a file (`overlay_hud.cpp`) with zero `__asm` that
  the first crash's fix never even looked at.

**Second live crash, found and fixed, same day (2026-09-04).** Direct user
report: "still crashes," after the first fix was rebuilt and redeployed.

- **Confirmed genuinely different from the first crash, not a redeploy
  failure**: Windows Event Viewer showed a fresh Application Error entry with
  a different `d3d9.dll` module timestamp (confirming the rebuilt DLL really
  was loaded) and a different fault offset (`0x137BB`, not the first crash's
  `0x4F30`) -- a real, distinct second bug, not the same one recurring.
- **Root cause**: the same `dumpbin /disasm` + image-base technique resolved
  the new fault to `mov eax, dword ptr [0A98ACCh]` -- another raw x86-only
  address (`kInLevelFlagAddr`/`kInLevelFlagAddrForFsrGate`, a per-frame
  "in level" gate used by the visual-enhancement suite's FSR/render-scale
  full-screen post-process pass). **Critically, this was in
  `overlay_hud.cpp`, not `analog_input_hooks.cpp`** -- a file the first
  crash's fix never touched, because it has zero `__asm`/naked code and was
  assumed safe on that basis alone. It isn't: `RunFullScreenPostProcessIfEnabled()`
  is called unconditionally every frame from the confirmed-active
  `Hook_EndScene` dispatcher, and reads several raw addresses
  (`0x00A98ACC`, `0x00B36218`) once past its config gate -- exactly the same
  bug class as the first crash, just in a file the compile-error-loop method
  never flagged (the function pointers/addresses here are all validly
  declared constants; nothing was ever undeclared, so nothing ever failed to
  compile).
- **Broader audit triggered by this discovery**: searched every source file
  for the same raw-address patterns (`reinterpret_cast<...Fn>(0x00...)`,
  `reinterpret_cast<volatile T*>(0x00...)`), not just
  `analog_input_hooks.cpp`. Found matches in four more files:
  `overlay_hud.cpp` (confirmed two live-reachable functions:
  `RunFullScreenPostProcessIfEnabled` -- the actual second crash --  and
  `PollDamageDiagLoggingIfEnabled`, both called unconditionally from
  `Hook_EndScene`; a third, `RunPreOverlayMotionBlurPassIfEnabled`,
  confirmed currently unreachable but guarded defensively anyway),
  `real_settings.cpp` (confirmed reachable via `overlay_hud.cpp`'s custom
  Options-screen/glyph-editor call sites -- `SetDvarBool`/`String`/`Float`,
  `SetKeybind`/`UnbindKeynum`/`KeyNameToKeynum`/`KeynumToDisplayName`/
  `QueueConsoleCommand`/`GetLocalizedString` all guarded; its `GetDvarBool`/
  `Float`/`String` getters were ALREADY safe, since their own `__asm`
  internals were already correctly wrapped in `#ifdef _M_IX86` from before
  this session -- a real example of the right pattern already being used
  elsewhere in this codebase), `rumble.cpp` (confirmed currently
  UNREACHABLE -- its dangerous functions' only real callers,
  `Rumble_Tick()`/`Rumble_Install()`, are themselves only called from
  already-guarded `analog_input_hooks.cpp` code -- audited and left
  as-is, not defensively guarded, to avoid touching working code without a
  live reason), `asset_capture.cpp`/`options_render_suppress.cpp` (false
  positives -- color bitmasks and an already-fully-disabled feature
  respectively, not addresses).
- **Fix pattern used for `overlay_hud.cpp`/`real_settings.cpp`**: a simple,
  robust early-return at the very top of each confirmed- or plausibly-
  reachable function (`#if defined(_M_X64) || defined(_WIN64) return;
  #endif`), rather than trying to guard individual internal lines --
  deliberately chosen after the first crash's more surgical per-line
  approach required several iterations to get exactly right (an off-by-line
  `#endif` placement error was caught and fixed mid-pass, see commit
  history). A whole-function early return is easier to verify correct by
  inspection and safer against missing an internal landmine than
  cherry-picking which specific lines are dangerous.
- **Real, honest scope note carried forward**: the visual-enhancement suite
  (FSR/RCAS, render-scale, motion blur) and the custom Options screen/glyph
  editor are now confirmed fully inert on x64 (early-return no-ops) rather
  than crash-prone -- consistent with "no real gameplay/feature hooks yet,"
  not a regression from what x64 already had (it never worked on x64 at
  all before today).
- **Verification**: both platforms rebuilt clean from scratch again. Same
  `dumpbin`-derived byte-pattern check confirms `0x00A98ACC`/`0x00B36218`'s
  encodings no longer appear in the compiled x64 binary either. **Still not
  live-tested past this point** -- given two real crashes found in a row
  during actual live launches (not caught by static audit alone until
  each one crashed first), a third, not-yet-found landmine of the same
  class remains a real, honestly-acknowledged possibility, not
  hand-waved away. The next real step is another live launch.

**First clean, non-crashing live launch, same day (2026-09-04).** Direct
user report: "no crash this time." `proxy_d3d9.log` for this run goes well
past where both prior crashes hit -- through the signature scan, MinHook
diagnostic-hook install, `Direct3DCreate9`/`CreateDevice`, the WndProc
subclass, `EndScene` firing, glyph-icon prewarm, the `ForceAnisotropicFiltering`/
`ForceHighQualityShadows`/`ForceHighQualityLighting` dvar writes (now
confirmed genuinely inert on x64 per the `real_settings.cpp` fix above --
each still logs its own "wrote X" line unconditionally even though the
underlying x64 write is currently a no-op, a real but harmless log-wording
mismatch worth fixing later, not a functional bug), XInput loading, and all
the way to a clean `DLL_PROCESS_DETACH`/`proxy_d3d9 detach` -- a genuine
normal exit, not a crash. This is the first x64 session in this project's
history to reach a clean shutdown.

**One honest gap, not yet closed [now CLOSED, see below]**: `"[x64-diag]
Pmove tick hook fired"` never appears in this log. The diagnostic hook's
install (signature scan + `MH_CreateHook`/`MH_EnableHook`) is confirmed
successful, but this specific session's log shows only main-menu-level
activity before exit -- Pmove (`FUN_1400168a0`) only runs once a level is
actually loaded (Campaign mission or Survival match), which this run
apparently never reached. The real, final confirmation that the whole
signature-scan -> MinHook pipeline fires correctly during live gameplay
(not just installs cleanly at menu time) is still pending an actual
in-level test.

**RESOLVED, same day: the diagnostic hook fired live, during real gameplay.**
Direct user follow-up ("check log") on a later session -- `proxy_d3d9.log`
shows `[x64-diag] Pmove tick hook fired (count=1)` through `(count=5)`
immediately after the menu-time init sequence (XInput load), meaning the
game actually reached live Pmove-ticking gameplay this time and the
installed detour fired and returned control to the real function correctly,
five times in a row, no crash. **This is the real, final confirmation this
whole implementation phase was building toward**: the entire pipeline --
real signature scan against the live x64 binary, successful `MH_CreateHook`/
`MH_EnableHook`, the detour firing during actual gameplay, and a clean
call-through back to the original function with zero behavior change --
is now confirmed working end to end on x64, not just build-verified. This
is the first real, live-confirmed x64 hook in this project's history.

**Still not started**: real per-hook gameplay code (see above); the shadow-map
creation call site remains the one major unresolved static-RE thread (needs
indirect-call scanning or live tracing, not more direct-reference scanning),
and `cls.state`'s exact semantics stay open but low-priority now that pause
is resolved via a separate path. See
`re_notes/x64_migration/README.md` for the complete import-table/
section-table diff, the full string-persistence data table, and every
sub-cluster's own raw Ghidra output files.

**Sprint + Movement hooks implemented, same day (2026-09-04) -- CONFIRMED
WORKING LIVE** (direct user report, after the deployment bug below was
found and fixed: "movement and sprint work"). First two real gameplay hooks on top of the
now-confirmed-working diagnostic foundation, in `analog_input_hooks_x64.cpp`.
Movement was added specifically because Sprint alone produces no observable
effect without movement to multiply -- can't meaningfully test one without
the other, so both went in together this round.

- **Sprint** (`FUN_140014a80`, signature in `re_notes/x64_migration/
  impl_sig_140014a80.txt`): the real x64 Pmove-entry pm_flags writer, called
  from within `FUN_1400168a0` (the already-hooked diagnostic function) on
  every movement-type branch. `Hook_SprintTick` calls through to native logic
  FIRST, untouched, then forces the pm_flags sprint bit (`lVar3+0xc |= 0x4000`,
  where `lVar3 = *param1`) directly afterward -- matching x86's own
  `InjectControllerSprintPmFlags` design, not the alternative of feeding a
  synthetic input bit in before the call (rejected: that shared bitfield's
  bit 0x2 is read by more than one function per this session's own RE, too
  risky to touch pre-call with only partial semantics understood). Uses the
  same bit-ownership tracking pattern as x86's own hard-won fix for the exact
  same regression class (`g_sprintBitForcedByUs` -- only ever clears a bit
  this hook itself set, never touches a bit native/keyboard logic set) --
  see CLAUDE.md's "Sprint's real kbutton" section for the original x86
  regression this pattern exists to prevent from recurring.
- **Movement** (`FUN_14007d9f0`, signature in `re_notes/x64_migration/
  impl_sig_14007d9f0.txt`, full decompile in `impl_movement_14007d9f0.txt`):
  a genuine **structural fusion**, by the x64 compiler, of x86's separate
  `FUN_0057d430` (keyboard movement writer) and `FUN_0057de60` (angle-
  finalize) into ONE function -- confirmed via full decompile. `param_1`
  (RCX) is directly the `usercmd_t*` (not a wrapper context struct like the
  Pmove functions use), with `forwardmove`@+0x1c/`rightmove`@+0x1d as signed
  bytes -- IDENTICAL offsets to x86's own documented layout, strong evidence
  the underlying struct never changed across the recompile. `Hook_MovementTick`
  calls through first, then mirrors x86's own `InjectControllerMovement`
  exactly: reads both sticks, routes via `RouteStickAxes` per
  `g_modConfig.stickLayout`, adds `moveY*127.0f` to forwardmove and
  `moveX*127.0f` to rightmove (additive on top of whatever native/keyboard
  already wrote, no inversion -- x86's own real-hardware playtest already
  confirmed movement needs none, only look was ever reported inverted),
  clamped to int8 range.
- **Two cross-file linkage fixes needed to reuse x86's own logic rather than
  duplicating it** (both the same class of bug as `IsPhysicalHeld_Exported`,
  added just before this round -- see that entry above): `RouteStickAxes()`
  in `analog_input_hooks.cpp` is ALSO anonymous-namespace-scoped (confirmed
  via the same brace-depth trace, not a heuristic guess), so a matching
  `RouteStickAxes_Exported()` thin `extern "C"` wrapper was added right after
  it, same pattern. `ClampToSByte()` is anonymous-namespace-scoped too (its
  own separate small namespace) but at 3 lines wasn't worth cross-file
  plumbing for -- duplicated locally in `analog_input_hooks_x64.cpp` as
  `ClampToSByteX64` instead, a deliberate case-by-case call (export what's
  genuinely reused/nontrivial, duplicate what's trivial), not a blanket rule
  either way.
- **Verification**: both hooks build clean (0 errors) on x64; Win32 rebuilt
  immediately after and also builds clean (0 warnings introduced, confirming
  no regression to the still-working x86 build). A first "redeploy x64 last"
  step silently left an x86 DLL deployed (see the deployment-bug entry right
  below) -- once that was caught and fixed with a real `/t:Rebuild`, the user
  live-tested both hooks together and confirmed **"movement and sprint
  work."** This is the first live-confirmed real gameplay hook pair on the
  x64 line, beyond the zero-behavior-change Pmove diagnostic above.

**Real, self-caught deployment bug, same round: the "rebuild x64 last to
redeploy" step above did NOT actually redeploy x64 -- silently left an x86
DLL loaded into the x64 game process.** User relaunched the game, tried
controller, "nothing happened," then reported `proxy_d3d9.log` hadn't been
touched at all even after a full relaunch -- the real tell, since a genuine
attach always opens/writes the log first thing. Root cause, confirmed via
`dumpbin /headers`: the "final x64 rebuild" MSBuild invocation reported
`Link: All outputs are up-to-date` and skipped relinking entirely, because
its own incremental-build state only tracks whether ITS OWN inputs (the x64
object files) changed since ITS OWN last link -- it has no way to know the
shared `OutDir` target file was overwritten by the INTERVENING Win32 build's
own link step in between. The deployed `d3d9.dll` was still genuinely x86
(confirmed `14C machine (x86)` via `dumpbin /headers`) -- a 32-bit DLL can
never load into a 64-bit process at all (`LoadLibrary` fails outright at the
OS loader level), so the proxy never attached, `DllMain` never ran, and the
log file was never opened, exactly matching what the user saw. **Fixed** by
forcing a genuine `/t:Rebuild` (not incremental `/t:Build`) for the final
x64 pass -- confirmed via `dumpbin /headers` afterward (`8664 machine
(x64)`), not just trusted from MSBuild's own text output. **Standing lesson
for any future "switch platform back to X to redeploy" step on this shared-
OutDir project**: MSBuild's own "up-to-date" verdict is per-platform-config
local state, not aware of a sibling config's build clobbering the same
output file in between -- always verify the ACTUAL deployed binary's real
architecture with `dumpbin /headers` after a platform-switch-back redeploy,
never trust the build log's silence on relinking. Direct instance of
CLAUDE.md's own "checking is far cheaper than digging" principle -- this
should have been checked before telling the user to test, not found the
expensive way after a wasted live-test round.

**Look (right stick) implemented, same day (2026-09-04) -- CONFIRMED WORKING
LIVE** (direct user report: "works"). Third real gameplay hook on the x64
line, added directly onto the now-live-confirmed Sprint+Movement
foundation.

- **Real target found via full decompile**: `re_notes/x64_migration/
  decomp_14007d3b0.txt` confirmed `FUN_14007d3b0` (the function
  `FUN_14007d9f0` calls at its own top) as the x64 equivalent of x86's
  `FUN_0057d680` (raw mouse-delta reader) -- but per x86's OWN documented
  history (see `analog_input_hooks.cpp`'s `InjectControllerLookAngles`
  comment, "Superseded 2026-07-14"), hooking the raw-delta source was
  explicitly abandoned there in favor of writing straight to the real
  pitch/yaw angle-ACCUMULATOR globals, bypassing the mouse-cvar pipeline
  entirely -- a direct user correction that look-via-mouse-delta-injection
  was still mouse emulation under the hood, not true native input. This x64
  implementation follows the CURRENT x86 design, not the superseded one.
- **Accumulator globals found via full decompile of `FUN_14007d9f0`**
  (`re_notes/x64_migration/impl_movement_14007d9f0.txt`, the same function
  already hooked for Movement): `DAT_1406e2738` (pitch, accumulated via `+=`)
  / `DAT_1406e273c` (yaw, accumulated via `-=`) are read, packed into the
  real `usercmd_t.angles` short (`param_1+0x38`) and byte (`+0x3a`) fields
  via a call to `FUN_140003fc0`, and have their leftover fractional
  remainder written back -- ALL unconditionally, on every single call to
  this function, regardless of whether there was any real mouse delta that
  tick (that guard only gates whether NATIVE delta gets accumulated on top
  of whatever's already there, not whether the pack step runs -- confirmed
  via the decompile's own control flow, `LAB_14007dd3a` is reached
  unconditionally). This means a value written to these accumulators BEFORE
  the native call gets picked up and packed in the SAME tick, and correctly
  STACKS with simultaneous real mouse input rather than either clobbering
  the other.
- **A real new SigScan capability needed and added**: every previous x64
  signature resolved a CODE target (a hook's own entry point) directly from
  the match address. The accumulators are DATA, referenced only via
  RIP-relative operands *inside* `FUN_14007d9f0`'s body -- resolving their
  real addresses via a hardcoded RVA-from-function-base offset would
  reintroduce exactly the fragility the signature-scanning policy exists to
  avoid (CLAUDE.md SS5/SS10.3). Added `SigScan::ResolveRipRelative(insnAddress,
  insnLength)` (`signature_scan.h`) -- reads the disp32 always encoded as an
  instruction's LAST 4 bytes for this addressing mode and computes
  `target = insnAddress + insnLength + disp32`, the standard x64 RIP-relative
  formula. A single 33-byte, 5-instruction signature
  (`kAngleAccumSignature`, `re_notes/x64_migration/
  full_sigbytes_14007d9f0.txt` offsets +0x394-+0x3B4 -- two different
  RIP-relative float reads to two different globals, into two different
  stack slots, immediately followed by a CALL, distinctive enough to be
  unique in the whole binary) resolves BOTH accumulators from one scan: the
  match address is the pitch read, the yaw read starts exactly 14 bytes in.
- **Design**: PRE-hook, not post-hook (the opposite of Movement/Sprint) --
  since the native call itself both consumes and packs the accumulators in
  one pass, the controller's contribution has to already be sitting there
  before `g_realMovementTick` runs. Reads both sticks, routes via
  `RouteStickAxes_Exported` (the same wrapper Movement already uses),
  computes `yawDelta`/`pitchDelta` from `g_modConfig.lookDegreesPerSecond
  Horizontal/Vertical` * a look-acceleration-ramp scale (`GetLookAcceleration
  ScaleX64`, a direct, unmodified port of x86's own `GetLookAccelerationScale`
  -- pure `g_modConfig`/`GetTickCount()` math, no hardcoded x86 addresses, so
  it needed no RE at all) * `dt`, and subtracts both from their respective
  accumulators -- sign convention copied directly from x86's own
  confirmed-correct `InjectControllerLookAngles`, not re-derived.
- **Deliberately deferred, not overlooked**: x86's ADS-FOV look-slowdown
  (`GetAdsLookRateScale`) needs an x64 equivalent of the hardcoded
  `GetEffectiveFov`/`Dvar_FindVar` addresses it depends on -- genuinely
  unresolved RE targets, not yet found, and a clean separate follow-up.
  Gyro-aim is already PREVIEW/WIP and never live-tested even on x86 itself --
  lowest priority. Motion-blur's per-frame yaw/pitch delta globals
  (`g_motionBlurYawDeltaDeg`/`g_motionBlurPitchDeltaDeg` on x86) were also
  left out of this pass -- the whole x64 visual-enhancement suite is still
  confirmed fully inert (see above), so there's no consumer to feed yet.
- **Verification**: signature_scan.h's new `ResolveRipRelative` helper and
  the full Look hook build clean (0 errors) on x64; Win32 rebuilt immediately
  after and also builds clean (0 warnings introduced, confirming no
  regression); x64 rebuilt again with a forced `/t:Rebuild` and confirmed
  genuinely deployed via `dumpbin /headers` (`8664 machine (x64)`), applying
  the deployment-bug lesson immediately above rather than repeating it. User
  live-tested and confirmed: "works."

**Buttons/ADS/Reload, Pause toggle, and Weapnext implemented, same day
(2026-09-04, direct instruction "do all in one pass") -- BUILD-VERIFIED
ONLY, NOT YET LIVE-TESTED.** The remaining three items from this issue's
own "next steps" list, all landed together per the user's explicit request.
All three are polled from inside the already-hooked `Hook_MovementTick`
(the same per-tick orchestration point Look was folded into) rather than
via any new `MH_CreateHook` -- none of these call sites are hooks at all,
they're direct calls into confirmed, self-contained real engine functions,
resolved via signature scan for their address only.

- **Buttons/ADS/Reload** (`FUN_14007eaf0`, `re_notes/x64_migration/
  README.md` sections 1e/1g, decompiled in full this round --
  `re_notes/x64_migration/decomp_buttons_pause_weapnext.txt`): the confirmed
  unified x64 kbutton-state setter, `void FUN_14007eaf0(int playerIndex, int
  bindIndex, int isDown)` -- standard fastcall, no custom register
  convention (a real x64 architectural simplification over x86's own
  hand-assembled `CallKbuttonDown`/`CallKbuttonUp` thunks, which needed a
  third stack-passed timestamp argument neither x64 function needs).
  `bindIndex` is a row index into the 32-entry bind-name table
  (`re_notes/x64_migration/kbutton_table_x64.txt`, base `1404c1870`, 8-byte
  stride) -- computed the same offline `index = (entryAddr - base) / 8`
  technique this project's own RE notes already established for weapnext:
  Fire (`+attack`) = 1, Reload (`+usereload` -- NOT the separate `+reload`
  at index 53, a genuinely distinct bind this table also has) = 11, ADS
  (`+toggleads_throw`) = 59. Held-style, edge-triggered exactly like x86's
  own `InjectControllerAds`/`InjectControllerReload`/`InjectControllerFire`
  (call on state CHANGE only, matching a real keypress/release, not every
  tick).
- **Pause toggle** (`FUN_1400823b0`, confirmed via the README's own
  "Same-day follow-up #5" as the real, self-contained live-gameplay pause
  TOGGLE): reads the current `SetMenuState` mode and calls
  `SetMenuState(player, 2)` (open) or `SetMenuState(player, 0)` (resume)
  accordingly -- a complete toggle in one call, `void
  FUN_1400823b0(int playerIndex)`. Called ONLY on the press edge (never on
  release -- calling a toggle twice per press would immediately undo
  itself).
- **Weapnext** (`FUN_1400706d0`, confirmed RESOLVED in
  `re_notes/x64_migration/sprint_weapnext_x64.md` after this project's own
  self-corrected coincidence-vs-mechanism investigation): the real x64
  weapnext dispatcher, `void FUN_1400706d0(int playerIndex, int direction)`
  -- internally gates on real weapon-busy/reload-state exclusion checks
  before calling `FUN_140074570` (the actual weapon-slot-cycling function),
  safe to call directly on the press edge the same way a real bound key's
  press already does. `direction=1` matches the confirmed forward-cycle
  convention.
- **A real, honest residual caution flagged in-code and here, not glossed
  over**: `FUN_14007eaf0`'s full body (beyond the state-write this project
  relies on) also contains menu-forwarding/ESC-dispatch logic this pass
  didn't exhaustively re-trace for every input value -- this project's own
  prior RE notes already flag "not yet confirmed whether `FUN_14007eaf0` is
  actually reachable/safe to call directly... the computed bind indices are
  static-only, not live-verified." This IS the same function real native
  keyboard Fire/ADS/Reload presses already route through today in vanilla,
  unmodified play (real evidence favoring safety), but unlike Sprint/
  Movement/Look/Pause/Weapnext (each independently confirmed
  self-contained), Buttons/ADS/Reload carries more residual uncertainty --
  test this one carefully during the upcoming playtest, not blindly assumed
  safe from the RE trail alone.
- **Signatures** (`re_notes/x64_migration/impl_sig_14007eaf0.txt`,
  `impl_sig_1400823b0.txt`, `impl_sig_1400706d0.txt`): each derived via
  `DumpSigBytes.java` against the real disassembly, RSP-relative stack
  spills kept literal per this file's own established false-positive
  lesson, every genuine RIP-relative/rel32 reference wildcarded.
- **Verification**: all three build clean (0 errors) on x64; Win32 rebuilt
  immediately after and also builds clean (0 warnings introduced, no
  regression); x64 rebuilt again with a forced `/t:Rebuild` and confirmed
  genuinely deployed via `dumpbin /headers` (`8664 machine (x64)`), same
  double-check discipline as every round since the deployment-bug lesson.
  **Not yet live-tested** -- this completes every item this issue's own
  "next steps" list named (Sprint, Movement, Look, Buttons/ADS/Reload,
  Pause, Weapnext all now implemented); next is a real playtest of the
  full set together.

**Real crash found and fixed, same day, immediately after the round above:
"no launch just exits no splash" -- a genuine stack-buffer-overflow bug in
`signature_scan.cpp`'s own logging code, exposed (not caused) by this
round's longer signature strings.** Direct user report after trying to
launch with the Buttons/Pause/Weapnext build deployed: the game process
started and immediately exited, before even the intro splash video --
`proxy_d3d9.log` showed only the `"---- proxy_d3d9 attach ----"` line, no
crash diagnostic despite this project's own `FlushLogOnCrash` vectored
exception handler.

- **Root-caused via real evidence, not guessed**: Windows Event Viewer
  (`Get-WinEvent`, Application log, Event ID 1000) showed the actual crash
  record -- `Exception code: 0xc0000409` (STATUS_STACK_BUFFER_OVERRUN) at
  fault offset `0x2d304` inside `d3d9.dll` itself (this project's own
  module, not the game's), matching this exact build's timestamp.
  `0xc0000409` is raised via `int 29h`/`__fastfail`, which bypasses SEH/VEH
  entirely -- explaining directly why `FlushLogOnCrash` never fired and the
  log stopped dead after the attach line, without needing to guess.
- **Pinpointed the exact instruction, not inferred**: `dumpbin /disasm` on
  the deployed `d3d9.dll` (with its matching PDB alongside it, so symbol
  names resolved automatically) confirmed the fault address is the `int
  29h` inside the CRT's own `_invoke_watson`, reached from
  `__report_gsfailure` -- the standard MSVC `/GS` stack-cookie-check-failure
  handler. `_invoke_watson` is also the exact function `sprintf_s`'s
  DEFAULT invalid-parameter handler calls when a formatted string would
  exceed its destination buffer -- the same failure path, not a coincidence.
- **Real cause, confirmed by direct measurement**: `signature_scan.cpp`'s
  `FindPattern()` logs its own result via `sprintf_s` into fixed `char
  buf[256]`/`buf[320]` buffers that embed the FULL pattern string via
  `"%s"` -- sized adequately for every signature this project had used
  before today. This round's new `kWeaponNextSignature`
  (`analog_input_hooks_x64.cpp`) is **242 characters long** on its own; the
  success-path log line alone (`"[sigscan] OK: pattern \"%s\" resolved to
  0x%llX (%d match%s)"`) comes out to roughly 309 characters with that
  pattern substituted in -- a real ~53-byte overflow of the old `buf[256]`.
  `sprintf_s` is a "safe" function (it detects an overflow rather than
  writing past the buffer), but its default failure response IS
  `_invoke_watson` -> `int 29h` -- exactly the crash observed. This is a
  genuinely pre-existing latent bug in the logging code (present since
  `signature_scan.cpp` was first written), never triggered before because
  every earlier signature happened to be short enough to fit -- this
  round's longer, more complex function signatures are what finally
  exposed it, not new code that introduced the bug itself.
- **Fixed**: every `char buf[...]` in `signature_scan.cpp`'s `FindPattern()`
  bumped to `1024` bytes -- generous headroom for any signature this
  project is realistically likely to need, not a tight fit to today's
  specific longest pattern. Documented in-code at the top of `FindPattern()`
  so a future session hitting a similar overflow with an even longer
  pattern understands the real mechanism immediately rather than
  re-deriving it.
- **Verification**: rebuilt x64 clean (0 errors); Win32 rebuilt immediately
  after, also clean (0 regression); x64 rebuilt again with a forced
  `/t:Rebuild` and confirmed genuinely deployed via `dumpbin /headers`
  (`8664 machine (x64)`). **Not yet re-tested live** -- this fix is
  build-verified only; the actual launch-then-exit repro needs to be
  re-attempted to confirm the game now reaches its splash screen again,
  before any of Sprint/Movement/Look/Buttons/Pause/Weapnext can be
  meaningfully playtested.
- **Real lesson for this project's own signature-scanning tooling going
  forward**: `DumpSigBytes.java`'s own suggested masked signatures can
  legitimately run to 150-250+ characters for functions with many
  RIP-relative references needing wildcarding (exactly this round's
  Buttons/Pause/Weapnext functions) -- any future logging code that embeds
  a full pattern string via `%s` needs headroom for that, not an assumption
  that signatures stay short. `CLAUDE.md`'s own "grep the whole file for
  existing values first" lesson (issue #46, Hold Breath/Fire bind-index
  collision) is the same class of lesson here in a different shape: a fixed
  buffer sized for today's inputs silently becomes wrong once a genuinely
  different-shaped input (a much longer string) shows up later.

**Real live playtest, same day: Sprint/Movement/Look/Pause-open/Weapnext all
confirmed working; two real bugs found in Buttons/ADS/Reload and Pause-
close, both root-caused and fixed.** Direct user report: "Okay, pause
works, no buttons other than weapnext work" (later, same session: "obvs
cant unpause when paused which was a early x86 known issue"). Both are
real, understood bugs, not mysteries -- fixed the same round.

- **Bug 1: Pause could open but never close.** Root cause: `PollPauseToggleX64`
  was only ever called from `Hook_MovementTick`, which rides `FUN_14007d9f0`
  -- part of the per-frame GAMEPLAY SIMULATION pipeline, which halts
  entirely while genuinely paused (confirmed architecture, not a guess --
  the same reason x86's own `InjectAllControllerInput` stops firing while
  paused, `CLAUDE.md`'s "One-shot commands and the real pause-menu path"
  section). Once Start's first press opened pause, the ONLY code path
  polling for the second press to close it also stopped running -- a
  structurally identical bug to a real, already-documented x86 one from
  this project's own early history (2026-07-15's "REAL FIX" for the same
  symptom, `analog_input_hooks.cpp`'s own `InjectMenuInputTick` comment).
  **Fixed** the exact same way x86 already fixed it: `PollPauseToggleX64`
  is now ALSO called from `InjectMenuInputTick` (the WndProc-subclass +
  SetTimer-driven tick that keeps running unconditionally even during
  pause, already confirmed firing on x64 via `Controller_RequestPoll`'s own
  unconditional call there) -- exported from `analog_input_hooks_x64.cpp`
  via `extern "C"` (same established anonymous-namespace-with-real-linkage
  pattern this file already uses elsewhere), declared and called from
  `analog_input_hooks.cpp` under an `#if defined(_M_X64)` guard.
  Redundantly still also called from `Hook_MovementTick` itself (handles
  OPENING pause during live gameplay; the menu-tick call is what now
  handles CLOSING it) -- same "safe/idempotent from either call site"
  design x86's own `InjectControllerPauseMenu` already established.
- **Bug 2: Fire/ADS/Reload silently did nothing.** Root cause: a genuine
  misread of `FUN_14007eaf0`'s own semantics, corrected by re-reading its
  full decompile (`re_notes/x64_migration/decomp_buttons_pause_weapnext.txt`)
  more rigorously rather than re-testing the same call with different
  values. The original implementation called
  `FUN_14007eaf0(player, bindIndex, isDown)` directly, treating `bindIndex`
  (computed offline from the bind-name table: Fire=1, Reload=11, ADS=59) as
  if it were that function's own dispatch key. It isn't -- `FUN_14007eaf0`'s
  second parameter is really a RAW KEYCODE SLOT: the function's own raw
  `*piVar1 = isDown` write at the top lands harmlessly into whatever
  per-keycode struct that slot number happens to be, but its REAL dispatch
  logic further down looks up `DAT_140644a6c[player*0x34a + param_2*3]` --
  the "which bind is THIS KEYCODE currently bound to" field, populated only
  by `Key_SetBinding` for genuine raw keycodes with a real key binding,
  never for an arbitrary bind-name-table index passed in directly. Calling
  it with 1/11/59 as if those were keycodes hit an unpopulated slot, the
  function's own `== 0` early-out fired every time, and `FUN_14007c3a0`
  (the REAL case-number dispatcher) was never reached -- exactly matching
  the observed symptom (Pause/Weapnext both bypass `FUN_14007eaf0` entirely,
  calling their own terminal functions directly, which is why only those
  two worked).
  **Fixed**: decompiled `FUN_14007c3a0` in full
  (`re_notes/x64_migration/decomp_14007c3a0_full.txt`, confirmed x64
  equivalent of x86's `FUN_00438710`) and confirmed its case numbers ARE
  bind-name-table indices directly (already independently established for
  weapnext=case 0x42/pause=case 0x43 -- `re_notes/x64_migration/README.md`
  section 1g). Fire = cases 1(down)/2(up), Reload(+usereload) =
  0xb(down)/0xc(up), ADS(+toggleads_throw) = 0x3b(down)/0x3c(up) -- each
  pair confirmed via the decompile calling `FUN_14007e460`(down)/
  `FUN_14007e490`(up) on the correct per-bind struct base for that action.
  Now calls `FUN_14007c3a0(player, caseNumber, isDown)` directly on the
  edge, exactly the same "direct call into the real dispatcher, bypassing
  the raw-key-event layer entirely" pattern already proven working for
  Pause/Weapnext -- just entered one level higher in the real call chain
  than the original (wrong) attempt. Real signature via `DumpSigBytes.java`
  (`re_notes/x64_migration/impl_sig_14007c3a0.txt`).
- **Verification**: both fixes build clean (0 errors) on x64; Win32
  rebuilt immediately after, also clean (0 regression); x64 rebuilt again
  with a forced `/t:Rebuild`, confirmed genuinely deployed via `dumpbin
  /headers` (`8664 machine (x64)`). **Not yet re-tested live** -- both
  fixes are build-verified only until run against the actual game again.

**Second live playtest, same day: two more real bugs found in the fixes
above, both root-caused and fixed by decompiling ONE more function pair
in full.** Direct user report: "okay weird, fire worked initially once,
then stopped working also our ads is the wrong behaviour we need the hold
to ads like we did for x86." Both trace to the SAME root cause.

- **Real root cause, confirmed via full decompile of `FUN_14007e460`/
  `FUN_14007e490`** (`re_notes/x64_migration/decomp_14007e460_e490.txt` --
  these are the actual down/up handlers `FUN_14007c3a0`'s cases tail-call
  into, previously only known by call shape, never read end to end): this
  is a real **dual-source kbutton_t** (classic id-Tech/Quake lineage --
  tracks up to TWO simultaneous key sources bound to the same action, e.g.
  mouse1 AND spacebar both bound to Fire, so releasing one doesn't cancel
  the other's hold). The handlers' own second argument is a SOURCE
  IDENTIFIER (confirmed via `FUN_14007eaf0`'s own real call sites: on a
  genuine native keypress it forwards the RAW KEYCODE itself as this
  argument, not a 0/1 flag), not an isDown boolean. The previous fix passed
  `isDown ? 1 : 0` as this argument -- on release, "0" happened to
  accidentally match the kbutton's unused SECOND slot (which defaults to
  0), not the FIRST slot the press actually wrote "1" into, so the release
  call hit its own early-out before ever clearing the first slot --
  permanently wedging the kbutton "active." The next press was then
  silently ignored too (the handler's own new-source check saw "1" already
  present) -- exactly matching "fire worked once, then stopped."
- **Fixed**: call `FUN_14007e460`/`FUN_14007e490` DIRECTLY (their own real,
  independently-resolved signatures -- no longer routed through
  `FUN_14007c3a0` at all) with a single, FIXED, consistent non-zero
  synthetic source id (`kSyntheticSourceId = 0x1000`, reused safely across
  Fire/Reload/ADS since each has its own independent struct) -- identical
  on both the down and up call for a given press, guaranteeing the release
  always finds and clears the exact slot the press wrote.
- **This also fixes the ADS toggle-vs-hold complaint as a direct
  consequence, not a separate patch**: `FUN_14007c3a0`'s case 0x3b/0x3c
  wrapped the SAME `FUN_14007e460`/`e490` call with an extra, unconditional
  toggle of a completely separate flag (`DAT_1406e26e0`, confirmed via real
  disassembly at `0x14007ce3f`-`0x14007ce50`: `flag = (flag==0)`,
  unconditional on every DOWN press, never touched on release) -- that
  flag toggle is exactly what made ADS look like "press to toggle on,
  stays on after releasing." Calling the kbutton handlers directly
  bypasses `FUN_14007c3a0`'s case dispatch entirely, so that toggle
  mutation never runs -- ADS now drives purely off the same real
  kbutton-held mechanism Fire/Reload use, matching x86's own proven
  hold-to-ADS design exactly (`InjectControllerAds`: `CallKbuttonDown` on
  press, `CallKbuttonUp` on release, nothing else).
- **Real per-bind struct/timestamp addresses**, all DATA not code, resolved
  via `SigScan::ResolveRipRelative` -- but anchored off `FUN_14007c3a0`'s
  own already-reliable resolved address plus a FIXED byte offset to each
  real instruction, rather than four more standalone multi-instruction
  signatures. Every offset independently verified via `DumpRawBytes.java`
  against the live binary (`re_notes/x64_migration/
  rawbytes_c3a0_targets.txt`), not estimated from the decompile's own
  pseudo-C. This is the same sanctioned "resolve an entry point, apply a
  byte offset" pattern `signature_scan.h`'s own `ResolveAs<FnT>` already
  documents for the function-pointer case, extended here to a data
  reference at a verified fixed offset within the same already-scanned
  function.
- **Verification**: build clean (0 errors) on x64; Win32 rebuilt
  immediately after, also clean (0 regression); x64 rebuilt again with a
  forced `/t:Rebuild`, confirmed genuinely deployed via `dumpbin /headers`
  (`8664 machine (x64)`). **Not yet re-tested live.**

**Third live playtest, same day: Fire and Reload both CONFIRMED WORKING
LIVE ("pause unpause works and reload works" / fire "reliable"), ADS
found broken in a NEW way, root-caused and fixed.** Direct user report:
"ads now does not work at all but shoot is reliable." Real, useful signal
rather than a setback -- the resolved-address log line
(`[x64-buttons] Fire/ADS/Reload active: fireStruct=0x140644818
reloadStruct=0x1406448A4 adsStruct=0x1406448E0
timestampPtr=0x141EFB764`) confirmed every address matches this session's
own independently hand-verified values exactly, proving the whole
resolution mechanism (anchor + fixed-offset RIP-relative reads) is sound
-- Fire and Reload's own success on the IDENTICAL mechanism rules out a
plumbing bug for ADS specifically.

- **Real cause**: the sixth round's fix (calling `FUN_14007e460`/
  `FUN_14007e490` directly, bypassing `FUN_14007c3a0`'s case 0x3b/0x3c
  entirely) was reasoned to be safe because x86's own proven ADS design is
  pure kbutton-hold, with no toggle involved. **Live-tested: wrong for this
  specific x64 bind.** With the kbutton call alone, ADS did nothing at all
  (worse than the toggle symptom from the previous round) -- direct,
  unambiguous evidence that `DAT_1406e26e0` (the flag `FUN_14007c3a0`'s
  case 0x3b/0x3c toggles as a side effect) genuinely IS the real "is aiming
  down sights" state this engine's aim/FOV/camera code reads, not a
  secondary/cosmetic effect safe to bypass as originally assumed.
- **Fixed**: resolve `DAT_1406e26e0`'s real address too (same
  anchor-plus-fixed-offset technique, offset independently re-verified via
  `DumpRawBytes.java` for the case 0x3b `LEA R8,[DAT_1406e26e0]`
  instruction) and FORCE it directly to the desired absolute value on the
  edge -- `1` while ADS is held, `0` while it isn't -- rather than relying
  on the native code's own toggle-on-press-only semantics (which is what
  broke hold behavior in the fifth round in the first place). Still also
  calls `FUN_14007e460`/`FUN_14007e490` on the ADS kbutton struct
  (whatever secondary state that drives, e.g. slowdown/animation, likely
  still wanted) -- the flag force is additive on top, not a replacement.
- **Verification**: build clean (0 errors) on x64; Win32 rebuilt
  immediately after, also clean (0 regression); x64 rebuilt again with a
  forced `/t:Rebuild`, confirmed genuinely deployed via `dumpbin /headers`
  (`8664 machine (x64)`). **Not yet re-tested live** -- Fire/Reload/Pause
  are all now confirmed working; ADS's flag-force fix specifically needs
  the next playtest.

**ADS CONFIRMED WORKING LIVE** (direct user report: "ads fixed now").
**Same session, a real, older-class symptom resurfaced: "we have the
original issue where we need to click in to get input (that internal
focus mechanism)."** This is the exact same SYMPTOM CLASS as x86's own
"needs an initial click at launch" bug (`known_issues.md` issues
#1/#27/#42, fixed 2026-07-31 via `SendSyntheticActivationClick()` --
synthesizing `WM_ACTIVATE`/`WM_SETFOCUS`/click messages directly into the
game's real `WndProc` via `CallWindowProcA`) -- confirmed via
`proxy_d3d9.log` that this exact x86 fix's own log line
(`[focus-gate-fix] synthesized...`) already fires unconditionally on
every x64 session too (the function itself has no platform guard). **The
symptom being back despite the fix already running means x86's own
mechanism isn't sufficient for whatever x64's real equivalent gate
actually needs** -- per this project's own standing "x86/x64 are
separately-built binaries, don't assume a mechanism carries over
unverified" principle (`CLAUDE.md` §10.8), this is NOT assumed to be the
same crouch-specific guard-byte pair x86's fix targeted (crouch input
isn't even wired on x64 yet as of this session) -- a genuinely open, new
x64 investigation.

- **Real, testable theory**: `SendSyntheticActivationClick`'s own design
  deliberately never touches real OS-level focus state (`GetForegroundWindow`/
  `GetActiveWindow`/`GetFocus`) -- its own original comment explicitly
  chose "no `SetForegroundWindow`, no stealing focus" since x86's own gate
  apparently only cared about the MESSAGE arriving, not real OS window
  state. If x64's own equivalent gate reads actual OS focus state instead,
  the synthetic-message-only approach would never satisfy it regardless of
  how many times it fires.
- **Fixed, as an explicit EXPERIMENT (same honesty standard as the
  original x86 fix -- not a confirmed root-cause fix until live-tested)**:
  added `SendRealFocusNudgeX64()` (`d3d9_hook.cpp`), a real
  `SetForegroundWindow`/`SetActiveWindow`/`SetFocus` call sequence, x64-only
  (guarded `#if defined(_M_X64) || defined(_WIN64)`), called immediately
  after the existing synthetic click in `InstallWndProcHook`. Deliberately
  NOT enabled on x86 -- x86 is already confirmed working without it
  (live-tested, "never clicked the window once"), and `SetForegroundWindow`
  specifically can genuinely steal focus from an unrelated window if
  misused, exactly the risk x86's own fix chose to avoid; no reason to
  introduce that risk where it isn't needed.
- **Verification**: build clean (0 errors) on x64; Win32 rebuilt
  immediately after, confirmed the new code compiles out entirely for
  Win32 (no regression risk, the x64-only guard works as intended); x64
  rebuilt again with a forced `/t:Rebuild`, confirmed genuinely deployed
  via `dumpbin /headers` (`8664 machine (x64)`). **Not yet live-tested** --
  genuinely experimental, watch for `[focus-gate-fix-x64]` in
  `proxy_d3d9.log` and confirm live whether input now works without a
  manual click before treating this as resolved.

**Real correction, same day: the OS-focus experiment above was WRONG --
direct user correction: "and not windows focus deffo internal they juist
moved it."** Confirmed via both experiments' own log lines firing every
session with the symptom still present -- neither synthesizing WndProc
messages nor real `SetForegroundWindow`/`SetActiveWindow`/`SetFocus` calls
touch whatever x64's real gate actually is. Pivoted to genuine static RE
instead of a third blind Win32-level guess.

- **Real find, via full decompile of `FUN_14007d9f0` itself** (the
  function this project's own Movement/Look hook already sits on, full
  decompile already on disk from earlier this session): its very first
  real branch, immediately after the mouse-delta-accumulator call, is
  `if ((DAT_1406e4774 & 0x800) != 0) return;` -- when this ONE bit is set,
  the ENTIRE function (movement, look-angle packing, everything) is a
  complete no-op, for BOTH controller injection and real native
  keyboard/mouse input alike (the bit lives inside the native function
  itself; this project's own hook just calls through to it either way).
- **Independent corroboration, not just one data point**: a SECOND,
  structurally distinct function, `FUN_14007d5f0`
  (`re_notes/x64_migration/decomp_14007d5f0.txt` -- a genuine usercmd
  movement writer in its own right, touching the same forwardmove/
  rightmove/+0x1e/+0x1f usercmd fields), gates its ENTIRE body behind the
  exact same bit (`&DAT_1406e4774 + player*0xce5c`, the same per-player
  field `FUN_14007d9f0` reads at offset 0 for SP's player 0). Two
  independent functions gating all their real work behind the identical
  single bit is strong, convergent evidence this is genuinely a broad
  "movement/input processing suppressed" gate, not a narrow crouch-specific
  lock like x86's own stance-guard bytes were.
- **Static analysis could NOT find a writer** to this flag --
  `FindDataWriters.java` found only TEST/read references, zero direct
  writes, the SAME limitation x86's own original investigation hit for its
  own guard bytes (`known_issues.md` issue #42: "an exhaustive whole-binary
  scan... found only 4 reader functions and ZERO writers"), likely because
  the real writer uses register-relative addressing Ghidra's reference
  tracker doesn't resolve back to this literal address.
- **Fixed via the same empirical philosophy that already fixed x86's
  original issue AND this session's own ADS toggle-flag bug**: rather than
  continue a static hunt for an unconfirmed writer, resolve the flag's real
  address and FORCE it clear directly, every Movement tick, before calling
  through to native logic -- bypassing whatever real event naturally clears
  it, the same "force the desired state directly" pattern already proven
  live for Sprint's pm_flags bit and ADS's is-aiming flag. Resolved via the
  ALREADY-scanned `kMovementTickSignature` match (no separate scan needed)
  plus a fixed, directly-verified byte offset (`+0x54`,
  `re_notes/x64_migration/full_sigbytes_14007d9f0.txt`) to the real `TEST
  dword ptr [rip+disp32], 0x800` instruction. This is the FIRST instruction
  shape this project has hit where the disp32 isn't the instruction's last
  4 bytes (a 10-byte `TEST reg/mem, imm32` form: opcode + disp32 + a
  trailing 4-byte immediate) -- added `SigScan::ResolveRipRelativeAt()`
  (`signature_scan.h`), a more general two-explicit-address form, alongside
  the existing `ResolveRipRelative()` rather than changing that one's
  contract and risking the already-working Look/ADS-flag resolutions.
- **Real, honest uncertainty, not glossed over**: it's not confirmed what
  ELSE this bit's legitimate SET state might represent (a deliberate "not
  yet controllable" window early in a level load being the most likely
  guess) -- but forcing it clear only from inside `Hook_MovementTick`,
  which itself only ever runs during an active Pmove simulation tick in the
  first place (never during a menu/pause/loading screen, per this session's
  own established "Pmove tick halts during pause" finding), should keep
  this narrowly scoped to exactly the "stuck after launch" window it's
  meant to fix, without touching whatever legitimately needs this bit set
  during a genuine non-gameplay state.
- **Verification**: build clean (0 errors) on x64; Win32 rebuilt
  immediately after, also clean (0 regression, the new
  `ResolveRipRelativeAt` helper is additive, doesn't touch the existing
  overload's behavior); x64 rebuilt again with a forced `/t:Rebuild`,
  confirmed genuinely deployed via `dumpbin /headers` (`8664 machine
  (x64)`). **Not yet live-tested** -- watch for `[x64-inputgate] Resolved
  @ 0x...` in `proxy_d3d9.log` (confirms the flag's real address was found)
  and confirm live whether input now works without a manual click.

**Fourth live-test report, same day: "nope still the same" -- the
`DAT_1406e4774` force-clear did NOT resolve it.** Direct user follow-up,
crucially with a specific, precise historical pointer: **"it was the exact
same issue we had way back before we ever released ncp as 0.1."** This
identifies `known_issues.md` issue #1 ("Buy-station + pause menu completely
breaks movement," 2026-07-15, x86) as the real precedent, not x86's later
crouch-specific issue #42 (already tried twice this session and also
failed). Issue #1's real root cause: this project's OWN early code
(`InjectAllControllerInput`) unconditionally forced a "menu active" gate
bit (`0x10` at x86 address `0x00B36210`, paired with a "game state" field
at `+8`, `0x00B36218`) to 0 every frame -- permanently suppressing a
transition the buy-station's own closing sequence needed to briefly see,
desyncing the game's own menu-depth tracking. **Real, honest caution**:
this is a DIFFERENT bug shape than a simple "click needed" gate -- the
actual x86 bug was THIS PROJECT'S OWN CODE breaking a native transition by
forcing a bit permanently, not a native engine defect needing an external
nudge. Two things are true at once here: this project isn't currently
forcing any x64 equivalent of this exact bit (so it's not repeating issue
#1's own specific mistake) -- but the user's broader point (this general
BUG CLASS, "an internal gate needs a genuine transition, forcing/ignoring
it wrong breaks things") is the right lens for continued investigation,
more so than treating it as identical to issue #42's shape.
- **x64's real structural equivalent of the 0x00B36210/0x00B36218 pair,
  confirmed, not guessed**: `DAT_1406e2550` (a per-player gate struct,
  0x190-byte stride) paired with `DAT_1406e2558` (`+8`, matching x86's
  exact relative offset) as a "state" field --
  `re_notes/x64_migration/decomp_buttons_pause_weapnext.txt`'s own
  `FUN_14007eaf0` decompile reads both together
  (`(&DAT_1406e2550)[player*400] & 0x10`-style "menu active" checks
  alongside `(&DAT_1406e2558)[player*100]` as the "state" value), and this
  project's OWN already-confirmed-working Pause hook (`FUN_1400823b0`)
  independently reads the exact same `DAT_1406e2550` bit `0x10` directly.
  Since Pause (open AND close) is already confirmed working live off this
  bit, it's evidently being read/tracked CORRECTLY by native logic already
  -- this project isn't the one desyncing it, unlike x86's own issue #1.
- **Deliberately NOT forced/written this round** -- given issue #1's own
  lesson (permanently forcing this CLASS of bit is exactly what broke
  things on x86), guessing a blind force here risks repeating that same
  mistake rather than fixing anything. Instead, resolved `DAT_1406e2550`
  purely for DIAGNOSTIC logging (via the already-scanned
  `kPauseToggleSignature` match plus a fixed, directly-verified `+0x70`
  byte offset) and added a rate-limited (~1s) heartbeat log inside
  `Hook_MovementTick` dumping both `DAT_1406e4774` and `DAT_1406e2550`'s
  live values -- the same "log first, then find the real fix" methodology
  that solved x86's own issue #42 (which added a full guard-byte heartbeat
  BEFORE the actual fix was found, not instead of trying a fix). The
  existing `DAT_1406e4774` bit-0x800 force-clear from the previous round is
  left in place (didn't help, but nothing reported as newly broken by it
  either) -- not removed pending real data on whether it's relevant at all.
- **Verification**: build clean (0 errors) on x64; Win32 rebuilt
  immediately after, also clean (0 regression); x64 rebuilt again with a
  forced `/t:Rebuild`, confirmed genuinely deployed via `dumpbin /headers`
  (`8664 machine (x64)`). **This round is diagnostic-only, not a fix
  attempt** -- the next playtest needs to capture `proxy_d3d9.log`'s
  `[x64-diag-gate] heartbeat` lines across a real "stuck, then unstuck by a
  manual click" cycle, so the actual bit transition (or lack of one) can be
  seen directly instead of guessed at.

**Real diagnostic data captured, real structural gap found, same day.**
Direct user report with the actual heartbeat log attached, plus a precise
clarification: **"check it i played a bit after but yeah always on entry
of level as we had in x86."**

- **What the heartbeat data actually showed**: `menuActiveGateFlag
  (DAT_1406e2550)` stayed frozen at `0x00000000` for the ENTIRE captured
  session, never once changing -- no correlation with anything, ruling it
  out as a live diagnostic signal for this symptom (consistent with it
  simply never being in a menu-open moment during the capture, not
  evidence it's broken -- Pause already reads this same bit successfully).
  `inputGateFlag (DAT_1406e4774)` DID vary actively during real gameplay
  (`0x20`, `0x4000`, `0x50`, `0x70`, etc.) but NEVER showed bit `0x800` set
  in any heartbeat -- confirming the previous round's force-clear IS
  running correctly -- yet the symptom persisted regardless, meaning that
  bit genuinely isn't the (or isn't the ONLY) blocker.
- **The real, structural gap, found from the user's clarification, not
  from the log data**: "always on entry of level" means this happens on
  EVERY level load, not just the very first launch. But
  `SendSyntheticActivationClick` and `SendRealFocusNudgeX64` (this
  session's first two fix attempts) BOTH only ever fire ONCE, from
  `InstallWndProcHook` -- which itself only runs once per game SESSION
  (`CreateDevice`'s own hwnd doesn't change across an ordinary level load,
  so the one-shot-fire logic never re-triggers). **Neither experiment
  could ever have worked, regardless of which underlying theory was
  closer to correct** -- they simply never got a chance to run again for
  the second, third, Nth level. This is a real bug in the FIX'S OWN
  design/trigger timing, independent of which native mechanism is
  actually being satisfied.
- **Fixed**: added `SendPeriodicActivationNudgeX64()` (`d3d9_hook.cpp`,
  x64-only), re-fired every ~2 seconds for the WHOLE session (not gated to
  "just once at startup") via the already-existing ~60Hz WM_TIMER tick
  already driving `InjectMenuInputTick`/`PollPauseToggleX64`. Rate-limited
  to match x86's own original "3-second window" scale for this exact bug
  class (`known_issues.md` issue #1's own fix shape: a windowed
  re-assertion tied to level transitions, not a one-shot event).
  Deliberately does NOT include the real click
  (`WM_LBUTTONDOWN`/`WM_LBUTTONUP`) `SendSyntheticActivationClick` sends --
  that function's own "(1,1) coordinate" safety reasoning only holds for a
  ONE-TIME fire before any real UI/gameplay exists yet; repeating a real
  click every 2 seconds throughout an entire play session is a genuinely
  different risk (could misfire a real gameplay action if the game's own
  WndProc treats `WM_LBUTTONDOWN` as Fire/interact for keyboard-and-mouse
  players). The new periodic function only re-asserts
  `WM_ACTIVATE`/`WM_SETFOCUS` (through the engine's own WndProc, same as
  the one-shot version) plus the real OS-level `SetForegroundWindow`/
  `SetActiveWindow`/`SetFocus` calls -- both already proven side-effect-free
  for a single fire, safe to repeat indefinitely. `SendSyntheticActivationClick`
  itself stays one-shot-only, completely unchanged.
- **Real, self-caught linkage bug found and fixed the same round**: the
  new function's forward declaration was initially placed BEFORE the
  anonymous namespace `HookWndProc` and the function itself both live
  inside, rather than inside it -- an immediate `LNK2019` (mangled symbol
  mismatch, the exact class of bug `CLAUDE.md`'s own "checking is far
  cheaper than digging" lesson already documents), caught by the real
  build error and fixed by moving the declaration inside the namespace,
  not by guessing at linkage.
- **Verification**: build clean (0 errors) on x64; Win32 rebuilt
  immediately after, also clean (0 regression, the x64-only guard compiles
  out entirely for Win32); x64 rebuilt again with a forced `/t:Rebuild`,
  confirmed genuinely deployed via `dumpbin /headers` (`8664 machine
  (x64)`). **Not yet live-tested** -- this is the first fix attempt that
  actually addresses the "recurs on every level" shape of the symptom
  rather than a one-shot launch-time nudge; the diagnostic heartbeat
  logging from the previous round is left in place, so the next playtest
  will show both whether the symptom is gone AND what the candidate gate
  values were doing throughout, in case this doesn't fully resolve it
  either.

**Fifth fix attempt, same day: the periodic activation-nudge fix above did
NOT resolve it either.** Direct, decisive user report: **"still no
difference it still requires the classic pause unpause workaround."** This
finally disproves the entire WndProc-message/OS-focus THEORY line this
session pursued across three attempts (synthetic click, real focus, then
periodic re-fire of both) -- none of them are the mechanism, regardless of
timing/frequency. But the report also hands over the actual, confirmed fix:
genuinely opening the pause menu and closing it again is what unsticks
input, every time. This is a direct answer, not a new theory to test.

- **Real reframe**: rather than keep guessing which internal flag a
  WndProc-level event might satisfy, this automates the user's OWN
  confirmed manual fix -- using this project's ALREADY-confirmed-working
  Pause toggle (`g_pauseToggle`/`FUN_1400823b0`) to open pause, wait one
  real beat, then close it again, exactly replicating the manual
  workaround programmatically instead of trying to reverse-engineer WHY it
  works.
- **Real "has a level just (re)loaded" signal, no dedicated level-load RE
  needed**: `g_lastPmoveTickMs` (updated on every real `Hook_PmoveTick`
  fire) tracks whether the Pmove/gameplay-simulation pipeline is currently
  live. When it transitions from "not ticking recently" (a menu/loading
  screen) to "ticking steadily for the last half-second" (a level is
  genuinely active), that's a reusable proxy for "a level just started,"
  reused directly from infrastructure this project already had rather than
  new RE work.
- **New function `AutoUnstickPauseCycleX64()`** (`analog_input_hooks_x64.cpp`),
  a small tick-based state machine (Idle / JustOpenedPause) -- NOT a
  blocking sleep (per `CLAUDE.md` §5's own hook-safety rule against
  blocking calls in hook callbacks): opens pause on the first tick a fresh
  level is detected as live, waits ~250ms of real elapsed ticks, then
  closes it again, once per level (armed again only once Pmove goes quiet
  for ~2s, i.e. back at a menu/loading screen). **Must run from the
  always-on menu tick** (`InjectMenuInputTick`, alongside
  `PollPauseToggleX64`), not the gameplay tick -- its own OPEN step pauses
  the game, which halts the gameplay tick entirely, so a gameplay-tick-based
  caller could never reach its own CLOSE step.
- **Real, honest scope note**: the earlier WndProc-message experiments
  (`SendSyntheticActivationClick`, `SendPeriodicActivationNudgeX64`) are
  left in place, not removed -- confirmed not the mechanism, but harmless,
  and pulling them out is a cleanup task for once this is fully confirmed
  resolved, not before.
- **Verification**: build clean (0 errors) on x64 (after a locked-DLL link
  failure from the still-running game process, resolved once the user
  closed it -- not a code issue); Win32 rebuilt immediately after, also
  clean (0 regression); x64 rebuilt again with a forced `/t:Rebuild`,
  confirmed genuinely deployed via `dumpbin /headers` (`8664 machine
  (x64)`). **Not yet live-tested** -- this is the first attempt that
  automates the user's own DIRECTLY CONFIRMED fix rather than a new theory,
  the strongest candidate so far.

**Real live-test feedback, same day, two separate findings from one
report: "weird, sprint fires when gated and when standing still so its
reading it but not allowing the other input, also the workaround fires
too early to work."**

- **Finding 1, real diagnostic signal, not yet acted on**: Sprint's own
  `pm_flags` bit-force (`Hook_SprintTick`, `FUN_140014a80` inside the
  Pmove tick chain) still fires correctly even while the game is in the
  "gated" state -- proving this project's OWN hooks genuinely run and
  write correctly regardless of whatever's blocking things; the block is
  downstream of hook execution, not upstream. Since Sprint lives in a
  structurally SEPARATE function from Movement/Look (`FUN_140014a80` vs.
  `FUN_14007d9f0`), this is consistent with -- though not proof of --
  different input paths being gated independently rather than one single
  global switch. Not yet chased further; recorded for whenever the
  pause/unpause automation below is confirmed insufficient on its own.
- **Finding 2, real, concrete, and directly actionable**: the auto-unstick
  cycle's own timing was simply too aggressive to actually replicate what
  a real manual pause/unpause does. It fired the moment Pmove was
  confirmed ticking steadily (500ms) and closed pause again after only
  250ms -- both far shorter than how a real player naturally performs the
  manual workaround (well after actually settling into a level, with a
  real, unhurried gap between the two presses). **Fixed**: added a genuine
  settle delay (4 seconds after Pmove is first confirmed live, via a new
  `WaitingToSettle` state tracking `g_levelActiveSinceMs` -- the timestamp
  Pmove FIRST went live this streak, not merely "ticked recently") before
  opening pause at all, and widened the open-to-close gap from 250ms to
  1000ms -- both now matching x86's own original "3-second window" scale
  for this exact bug class (`known_issues.md` issue #1) rather than this
  session's own first-guess short values.
- **Verification**: build clean (0 errors) on x64; Win32 rebuilt
  immediately after, also clean (0 regression); x64 rebuilt again with a
  forced `/t:Rebuild`, confirmed genuinely deployed via `dumpbin /headers`
  (`8664 machine (x64)`). **Not yet live-tested** -- same mechanism as the
  previous round (automating the user's own confirmed manual fix), just
  with real timing corrections from direct live-test feedback rather than
  a new theory.

**Real-time tuning, same day: "wait needs to be halved."** Direct,
concrete feedback after the above -- `kLevelSettleDelayMs` halved from
4000ms to 2000ms (the pre-open settle wait specifically; the 1000ms
open-to-close gap left unchanged pending any separate feedback on it).
Build-verified on both platforms, x64 redeployed and confirmed via
`dumpbin`. Not yet re-tested live.

**Second real-time tuning pass, same day: "still a touch slow maybe
1.75s and also make it close basically instantly, it should basically
look flawless user end."** `kLevelSettleDelayMs` refined further, `2000ms
-> 1750ms`. `kAutoUnstickCloseDelayMs` cut drastically, `1000ms -> 50ms`
-- kept deliberately non-zero (not fired in the same tick as the open
call) so the open and close remain two genuinely separate real engine
ticks rather than risking native logic treating them as one
indistinguishable event, but 50ms (~3 WM_TIMER ticks at this project's
own ~16ms/60Hz cadence) is well under normal human flash-perception
threshold -- as close to "instant" as this tick-based, non-blocking
design (`CLAUDE.md` SS5's hook-safety rule against blocking calls) can
get. Build-verified on both platforms, x64 redeployed and confirmed via
`dumpbin`. Not yet re-tested live.

**Third real-time tuning pass, same day: "could still be earlier try
1.25s."** `kLevelSettleDelayMs` refined further, `1750ms -> 1250ms`.
Build-verified, x64 redeployed and confirmed via `dumpbin`.

**Remaining x64 controls implemented in the same pass, direct
instruction: "also make sure we add all remaining controls that are
missing in this pass."** Re-read x86's own `InjectControllerButtons`/
`InjectControllerDpad` in full before writing any code -- a real, useful
finding: x86 does NOT drive Melee/Tactical/Lethal/Jump/Interact through
kbutton down/up calls at all (unlike Fire/ADS/Reload) -- it ORs raw bits
directly into `usercmd_t.buttons` (a `uint` at `+0x04`, confirmed in
`re_notes/iw5sp.md`'s own struct-layout table) every tick. Since x64's
`usercmd_t` is already confirmed identical at `+0x1c`/`+0x1d`
(forwardmove/rightmove, this session's own Movement work), the same
`+0x04` buttons field is trusted to carry over too -- mirrors x86's own
proven raw-bit mechanism directly rather than inventing a new one.

- **Melee** (`0x4`), **Lethal/frag** (`0x4000`), **Tactical/smoke**
  (`0x8000`) -- raw bits, additively OR'd every tick while held, real
  values copied directly from x86's own confirmed constants.
- **Jump** (`0x400`, `+gostand`) -- same raw bit, suppressed while a menu
  is active (reusing `g_menuActiveGateFlag`, resolved earlier this
  session for the "needs a click" diagnostic heartbeat, now ALSO used for
  its real, originally-intended purpose: this project's confirmed x64
  `IsMenuActive()` equivalent). x86's own "auto-stand from crouch/prone on
  Jump's rising edge" enhancement is deliberately NOT ported yet -- it
  depends on the same real stance-toggle mechanism CrouchProne itself
  needs (see below); Jump's own core bit-force works standalone without
  it, a minor feature gap, not a functional bug.
- **Interact** (`0x8`) -- hold-to-interact (`g_modConfig.interactHoldThresholdMs`,
  already a cross-platform config value), dual-purpose with Reload on the
  SAME physical button (X), matching x86's exact design (both the kbutton-
  based Reload call and this raw bit fire off the same physical press).
- **D-pad actionslot** -- the one exception to the raw-bit approach: x86
  itself ALSO calls a real function here (`ActionSlotDown`/`Up`), and
  x64's own confirmed equivalent, `FUN_14006dee0(playerIndex, slotIndex)`
  (decompiled in full this round --
  `re_notes/x64_migration/decomp_actionslot_stance.txt`), matches that
  shape: a genuine "use this actionslot item now" one-shot action
  (internally dispatches to weapon-switch/killstreak-use logic based on
  the slot's own equipped-item type), not a hold-based kbutton -- called
  once on the press edge only, same pattern as Weapnext, no "up" call
  needed. Real signature via `DumpSigBytes.java`
  (`re_notes/x64_migration/impl_sig_14006dee0.txt`).
- **CrouchProne (B) deliberately NOT included this round.** x64's own real
  stance-lock gate, `FUN_14007e430`, is now fully confirmed via real
  disassembly (`re_notes/x64_migration/disasm_14007e430.txt`) as a genuine
  `IsStanceLocked(player)`-equivalent (`XOR AL,AL` when both guard bytes
  are clear, `MOV AL,1` when either is set), structurally matching x86's
  own `FUN_0057d190` closely. But `FUN_14007c3a0`'s own case `0x17`/`0x18`
  (`+stance` down/up) toggle logic has real, unresolved ambiguity in its
  "restore previous posture" semantics on release (checks whether the
  SAVED old posture equals exactly `1`, not a simple restore-to-saved-
  value) that this pass's decompile alone doesn't cleanly resolve. Given
  this project's own documented history of genuinely nasty stuck-crouch/
  stuck-prone regressions (`CLAUDE.md`'s "Crouch 'needs an initial click
  at launch'" section, and the live x86 incident that motivated
  `ToggleStance`'s own real-toggle redesign in the first place), shipping
  this blind risks a real softlock -- honestly deferred rather than
  guessed.
- **Verification**: build clean (0 errors) on x64; Win32 rebuilt
  immediately after, also clean (0 regression); x64 rebuilt again with a
  forced `/t:Rebuild`, confirmed genuinely deployed via `dumpbin /headers`
  (`8664 machine (x64)`). **Not yet live-tested.**

**Live-test result, same day: "everything works but dpad is unconfirmed
and also as known crouch isnt donw that needs doing next."** Direct
confirmation -- the auto-unstick pause/unpause cycle (all three tuning
rounds, settled at 1.25s settle / ~50ms close) and every newly-added
control (Melee, Lethal, Tactical, Jump, Interact) are all CONFIRMED
WORKING LIVE. D-pad actionslot is confirmed BUILT but not yet
independently exercised in this test pass -- not a known bug, just
untested; worth a dedicated check next time. CrouchProne (B), already
flagged above as deliberately deferred this round, is the explicitly
named next task.

**CrouchProne (B) implemented, same day -- direct instruction: "as known
crouch isnt donw that needs doing next."** Rather than trying to replicate
`FUN_14007c3a0`'s own case `0x17`/`0x18` internal state machine (the real,
unresolved "restore previous posture" ambiguity flagged as the reason this
was deferred in the previous round), the SAFE design chosen instead:
forward B's own real press/release edges DIRECTLY to
`FUN_14007c3a0(0, 0x17, 1)` (down) / `FUN_14007c3a0(0, 0x18, 0)` (up) --
confirmed safe specifically because neither case body actually reads its
own `param_3` argument at all (only the shared timestamp global,
`DAT_141efb764`), so bypassing the ambiguous internal "what was the prior
posture" logic entirely and just re-firing the same real dispatcher B
would already reach through the normal input path carries none of that
ambiguity's risk. This sidesteps the stuck-crouch/stuck-prone regression
class this project has hit before (see the previous round's own deferral
reasoning) without needing to fully resolve `case 0x17`/`0x18`'s internal
semantics.

- No new signature scan needed -- `g_stanceDispatch` reuses the SAME
  already-resolved `FUN_14007c3a0` entry point (`kAnchorSignature`) every
  other struct/flag this session anchors off of already uses.
- Edge-tracked exactly like every other held-button control this session
  (`g_crouchProneHeldX64`, polled in `Hook_MovementTick` right after the
  D-pad block) -- press fires the down case once, release fires the up
  case once, no per-tick re-fire.
- **Verification**: build clean (0 errors) on x64; Win32 rebuilt
  immediately after, also clean (0 regression); x64 rebuilt again with a
  forced `/t:Rebuild`, confirmed genuinely deployed via `dumpbin /headers`
  (`8664 machine (x64)`, fresh `LastWriteTime`). **Not yet live-tested** --
  build-verified only, same honesty bar as every other round in this
  issue; CrouchProne needs a direct live confirmation before it can be
  marked working.

**CrouchProne (B) CONFIRMED WORKING LIVE, same day: "yep works fine."**
Direct confirmation of the previous round's implementation -- no further
changes needed.

**Jump auto-stand implemented, same day -- direct instruction: "you need
to implement the press a to stand up thing we did for x86 too."** Ports
x86's own real "auto-stand from crouch/prone on Jump's rising edge"
enhancement (`ForceStandingViaRealToggle`), the gap explicitly named and
deferred in both of the two prior rounds pending CrouchProne's own
mechanism existing to build on.

- **x86 precedent, re-read in full before writing any x64 code** (direct
  user correction this session: "you need to be comparing at every stage
  to the original so you can see the things like this youre constantly
  overlooking"): `ForceStandingViaRealToggle()` reads the real current
  stance directly from a fixed `+0x1C` offset in the player struct
  (`kRealStanceFieldAddr`), then calls the real `ToggleStance(playerIndex,
  mode)` with `mode` set to the CURRENT value -- since `ToggleStance`'s own
  logic is a genuine toggle (`current == mode ? 0 : mode`), passing
  `mode=current` always resolves to 0 (standing) regardless of whether
  current was 1 (crouch) or 2 (prone).
- **x64 has no standalone `ToggleStance(mode)` function to call the same
  way** -- crouch/prone are driven through `FUN_14007c3a0`'s own FIXED case
  numbers instead. Re-reading the full case list in
  `decomp_14007c3a0_full.txt` (not just the two cases CrouchProne itself
  already used) found case `0x48` = `togglecrouch` (toggles stance 0<->1)
  and case `0x49` = `toggleprone` (toggles stance 0<->2) -- `0x48` is
  independently corroborated as the SAME case number x86's own
  `togglecrouch` dispatch uses (`re_notes/iw5sp.md`'s "Found togglecrouch's
  REAL dispatch" note), a direct, comparable cross-check against the
  original rather than a fresh guess. Replicating x86's exact "call toggle
  with mode=current" trick means picking the MATCHING case for whatever
  the current stance actually is: current==1 -> case `0x48` (its own
  `current != 1` check is false, forces 0); current==2 -> case `0x49` (its
  own `current != 2` check is false, forces 0). Same result as x86's
  dynamic-mode call, expressed through x64's fixed-case dispatch instead.
- **The real stance field itself, found by re-reading
  `disasm_14007c3a0_full.txt` alongside the decompile rather than trusting
  the decompiler's separate `DAT_` names at face value**: the decompile's
  `DAT_1406e26fc` is genuinely `DAT_1406e26e0 + 0x1c` -- every one of case
  `0x49`/`0x4a`/`0x4b`'s real instructions is `[RAX + R8*0x1 + 0x1c]` off
  the SAME `LEA R8,[0x1406e26e0]` base `kAdsToggleFlagInsnOffset` already
  resolves for the ADS toggle flag (confirmed via `case 0x3b`'s own
  `LEA R8,[0x1406e26e0]` at `0x14007ce3f`, the same instruction that
  anchor's own comment already cites). **No new signature scan needed** --
  just a fixed `+0x1c` byte offset on top of the ALREADY-resolved
  `g_adsToggleFlag` pointer. This also mirrors x86's own design one level
  deeper, not just at the case-number level: x86's `kRealStanceFieldAddr`
  is itself a fixed `+0x1C` offset from its own per-player struct base --
  the exact same offset, `0x1C`, carried over identically to x64's
  analogous struct. A real, direct architectural parallel found BY
  comparing to the original, not assumed.
- Reads (`GetRealStanceX64()`) are direct/read-only, same as x86's own
  `GetRealStance()`; writes always go through the real case dispatch
  (`g_stanceDispatch`, the same pointer CrouchProne already resolves --
  no new anchor needed there either), which re-checks the same
  stance-lock guard bytes `FUN_14007e430` itself checks, never a raw
  memory write.
- Wired into the existing Jump block in `Hook_MovementTick`: on Jump's
  rising edge only (before updating `g_jumpHeldX64`), calls
  `ForceStandingViaRealToggleX64()`, which reads current stance and fires
  the matching case.
- **Verification**: build clean (0 errors) on x64; Win32 rebuilt
  immediately after, also clean (0 regression); x64 rebuilt again with a
  forced `/t:Rebuild`, confirmed genuinely deployed via `dumpbin /headers`
  (`8664 machine (x64)`, fresh `LastWriteTime`). **Not yet live-tested.**

**Current status, this session:** every control this issue's own history
has ever named -- Sprint, Movement, Look, Pause (open+close), Fire,
Reload, ADS, Weapnext, Melee, Lethal, Tactical, Jump (including auto-
stand), Interact, D-pad actionslot, CrouchProne, and the auto-unstick
pause/unpause cycle -- is now IMPLEMENTED and BUILD-VERIFIED. Directly
live-confirmed by the user: Sprint, Movement, Look, Pause, Fire, Reload,
ADS, Weapnext, Melee, Lethal, Tactical, Jump (core bit-force only, not
auto-stand specifically), Interact, CrouchProne, and the auto-unstick
cycle. **Awaiting live confirmation**: D-pad actionslot and Jump
auto-stand (both built, deployed, not independently exercised yet).

**Two new bugs found live, same day, both OPEN -- investigation started
then explicitly paused mid-session ("were going to hold here rn you need
to update the main docs with an eta") to prioritize a docs/ETA update
first. Recorded here as-is, not yet root-caused, so a future session
picks up the actual investigation state honestly instead of from
scratch.**

- **Bug: Fire and ADS both fail on sniper-class weapons specifically**
  (direct report: "cant shoot and ads? on sniper why" -- other weapon
  classes already confirmed working, so this is weapon-class-specific,
  not a general Fire/ADS regression). **Leading investigation angle,
  NOT YET CONFIRMED**: this project's own x64 Fire/ADS design calls
  `g_kbuttonActivate`/`g_kbuttonDeactivate` (`FUN_14007e460`/`e490`)
  DIRECTLY on the Fire/ADS kbutton structs, bypassing
  `FUN_14007c3a0`'s own case dispatch entirely for these two binds
  (unlike Weapnext/CrouchProne/Jump-autostand, which all go through
  `g_stanceDispatch`/the case dispatcher itself). `FUN_14007c3a0`'s own
  decompile (`decomp_14007c3a0_full.txt`, line ~18) shows every case
  runs `FUN_14007fc00(param_1, param_2)` BEFORE its own real handler,
  whenever `param_2 != 0` -- a per-case pre-call this project's direct
  kbutton calls never invoke. Not yet decompiled/confirmed what
  `FUN_14007fc00` actually does, or whether it's specifically relevant
  to bolt-action/scoped-weapon fire gating (a real, precedented bug
  class on x86 -- see `known_issues.md` issue #46, "can't fire while
  holding breath on a sniper," though that was a bind-index collision,
  a different mechanism, not directly transferable). **Also worth
  checking**: x64 has NOT yet implemented Hold Breath at all (grep
  confirms no `HoldBreath`/`breath` reference in
  `analog_input_hooks_x64.cpp`), so issue #46's own specific fix does
  not apply here -- this needs its own fresh root-cause, not a reapplied
  old fix. Next step when resumed: decompile `FUN_14007fc00`, and/or
  live-diagnostic capture of what state differs between a working
  weapon class and a sniper at the moment Fire/ADS should engage.
- **Report: D-pad actionslot "weirdly not just number but also have
  sometimes diff keys used."** UPDATE, same day: the D-pad Left gap
  described below has now been PORTED (build-verified, not yet
  live-tested) -- see "D-pad Left synthetic-key exception ported" below
  this section for the fix. Original writeup kept for context. Partially
  explained by ALREADY-KNOWN,
  EXPECTED x86 behavior re-confirmed by re-reading
  `analog_input_hooks.cpp`'s own D-pad section: the real per-slot action
  is genuinely DATA-DRIVEN by loadout (`FUN_00410ad0`'s x86 equivalent
  reads a per-slot "what's assigned here" type and dispatches to
  weapon-switch, killstreak/equipment select, or an NVG-style toggle
  depending on what's equipped) -- matching the user's own original
  expectation that D-pad maps to killstreaks/attachments which vary by
  loadout, not a bug on its own. **But a real, NOT-YET-PORTED gap found
  while re-reading that same x86 section**: x86 needed one explicit,
  narrowly-scoped exception for D-pad Left specifically (`+actionslot4`)
  -- a synthesized real `WM_KEYDOWN`/`WM_KEYUP` for `'4'` instead of the
  direct native call, because the pure native call path failed 100% of
  the time for Survival's AI-squadmate call-in (a GSC script watching
  for a real key event, not reachable via the native call alone; turret
  call-ins on the same slot worked fine via the native call). x64's
  current D-pad implementation calls `g_actionSlot(0, slot)` uniformly
  for all four directions with NO equivalent exception for slot 3 (Left)
  -- if x64 has the same GSC-script gap x86 did (likely, same engine
  lineage, not yet confirmed), Survival squadmate call-ins specifically
  would be expected to fail the same way x86's did before that fix,
  which may be part of what's behind "sometimes diff keys used." Not yet
  confirmed live which specific report the user is describing. Next step
  when resumed: clarify with the user whether this is the loadout-driven
  behavior (expected) or a specific D-pad Left/squadmate-callin failure
  (a real, portable fix), and if the latter, port x86's own
  `SendSyntheticActionSlot4Key` synthesis to x64 the same way.

**Release ETA set, same day: "i wont release until were at the same
level x86 was at" (2-4 weeks, user's own estimate).** The first `-x64`
release ships only once it reaches feature parity with `v0.3.5-x86`'s
own final state -- every control (including the two open bugs above,
resolved), the full visual-enhancement suite, stutter/threading fixes,
and the plugin API, not just the input-remapping core this pass has
focused on. Reflected in `README.md`'s top banner and `CLAUDE.md`/
`AGENTS.md`'s Version Timeline.

**D-pad Left synthetic-key exception ported, same day (investigation
resumed) -- BUILD-VERIFIED, NOT YET LIVE-TESTED.** Re-read x86's own
`SendSyntheticActionSlot4Key` and its D-pad call site
(`analog_input_hooks.cpp`, `ActionSlotDown`/`ActionSlotUp` section) in
full again before writing anything, per this project's own standing
compare-to-x86-original discipline. Confirmed x64 already has every
prerequisite x86's synthesis relies on with zero new plumbing needed:
`GetGameWindow()` (`d3d9_hook.cpp`) is plain `extern "C"`, not
arch-guarded, and the WndProc subclass behind it already installs for
x64 exactly as it does for x86 -- no blocking gap found.

Ported directly: a new `SendSyntheticActionSlot4KeyX64(bool down)`
(`analog_input_hooks_x64.cpp`) synthesizes `WM_KEYDOWN`/`WM_KEYUP` for
`'4'` via `PostMessageA`, identical mechanism and rationale to x86's own.
The D-pad dispatch loop in `Hook_MovementTick` now special-cases slot 3
(D-pad Left): press and release edges both route through the synthetic
key instead of `g_actionSlot(0, 3)`, mirroring x86's exact down/up
pairing (a real keypress has both edges, so the synthetic one does too)
-- deliberately does NOT also call `g_actionSlot` for this slot, same
"don't double-dispatch" reasoning x86's own comment gives (the
synthesized key's own real dispatch already reaches `FUN_14006dee0`,
x64's confirmed action-slot handler, on its own). The other three D-pad
directions are completely unchanged, still driven by the direct native
one-shot call.

This is the LEADING FIX for the "sometimes diff keys used" report, not a
confirmed root-cause resolution -- x64's own GSC-side behavior for the
Survival squadmate call-in specifically has not been independently
re-verified to have the same gap x86 did, only inferred from the
identical engine lineage and identical native-call shape
(`FUN_14006dee0` confirmed structurally equivalent to x86's
`ActionSlotDown`, see `kActionSlotSignature`'s own comment in
`analog_input_hooks_x64.cpp`). **Build-verified**: x64 `/t:Rebuild` (0
errors) -> Win32 regression build (0 errors, no new warnings) -> x64
`/t:Rebuild` again (forced) -> `dumpbin /headers` confirmed `8664
machine (x64)` with a fresh `LastWriteTime`. **Not yet live-tested** --
next step when the user next plays Survival is to confirm the AI-
squadmate call-in specifically now works via D-pad Left, and that turret
call-ins (D-pad Left's other loadout option, and the other three D-pad
directions generally) show no regression.

Sniper Fire/ADS (the other open bug above) is out of scope for this
round -- being investigated in parallel elsewhere this same session; not
touched here.

**Visual-enhancement suite x64 port, first attempt (same day) -- scoped
to `InternalRenderScalePercent` (x86 issue #88) and FSR 1.0 RCAS
sharpening (x86 issue #94), both BLOCKED on unresolved x64 addresses,
nothing unsafe shipped.** Read both x86 implementations in full first,
per this project's own standing rule (compare to the x86 original at
every stage). Real findings, both negative but concrete:

- **The full-screen shader/pipeline infrastructure itself is ALREADY
  x64-clean today**, confirmed by inspection rather than assumed:
  `overlay_hud.cpp` (home of `EnsureRcasShader`/`DrawFullScreenPass`/
  `RcasShaderSetupCallback`/the whole Phase A/B pipeline) has zero
  `_M_IX86`-only exclusions and compiles unconditionally into the x64
  build already -- every call it makes (`CreatePixelShader`/
  `SetPixelShader`/`SetPixelShaderConstantF`/`DrawPrimitiveUP`) is
  COM-vtable-index-based, not a hardcoded address, so none of it needed
  porting in the first place. The ONLY x64-specific code in this whole
  path is a single early-return stub already added during the
  2026-09-04 crash-fix pass (`RunFullScreenPostProcessIfEnabled`,
  `overlay_hud.cpp`), which explicitly documents "future work, not
  attempted this pass" -- this round is that attempted follow-up.
- **`InternalRenderScalePercent` needs x64's own equivalent of
  `FUN_00679010`** (the this-in-ECX resolution-compute function whose
  `+0x1c`/`+0x20` fields feed the real, unclamped scene render-target
  size) -- NOT the same function as `FUN_1401b8c80`
  (`re_notes/x64_migration/README.md`'s own "Render-scale/shadow-map
  thread" entry, x64 equivalent of `FUN_004b60a0`, the DOWNSTREAM
  render-target slot-descriptor creator). `FUN_00679010`'s x64
  equivalent has not been located by any pass to date. Not attempted
  further this round -- flagging the exact gap rather than guessing at
  an address.
- **FSR RCAS needs x64 equivalents of x86's `clcState` (0x00B36218)
  and the in-level time-delta flag (0x00A98ACC)** -- both load-bearing
  safety gates, not optional polish: x86's own issue #103/#104 history
  proved a menu-gate-only version of this exact pass crashes during
  loading screens and hangs/crashes on quit-to-menu, so shipping it on
  x64 without equivalent gating would very likely reproduce the same
  crash class. **One of the three x86 gates is already resolved for
  x64**: the menu-active check (`IsMenuActive_Exported`'s x86
  equivalent) has a real, confirmed x64 counterpart already wired up
  and in live use elsewhere this session -- `g_menuActiveGateFlag`
  (`analog_input_hooks_x64.cpp`, signature-scan-resolved pointer to
  `DAT_1406e2550`, bit `0x10` = menu active), directly reusable here.
  **`clcState` and the in-level flag are NOT yet resolved for x64.**
  Attempted via `RawStringScan.java` against the exact native error
  string `"SCR_DrawScreenField: bad clcState"` -- confirmed the string
  itself still exists in the x64 binary verbatim (`STRING @ 1403f6069`,
  consistent with this project's earlier ~10-identifier persistence
  check suggesting a genuine recompile of the same engine/data), but
  came back with **zero direct code references** under `-noanalysis` --
  the same class of indirect-reference tooling gap
  `x64_migration/README.md` already documents for a different target
  (`$shadowmap_large`), not evidence the check doesn't exist. Also
  traced the adjacent `SetMenuState`-equivalent dispatcher
  (`dvar_decomp_clpaused_users.txt`'s own decompile, `DAT_142615b20`
  per-player menu-state array, `FUN_14007f3b0(param_1, 0x10)` as the
  real bit-0x10 setter) far enough to independently confirm it's the
  SAME mechanism already backing `g_menuActiveGateFlag` above -- useful
  corroboration, but it's UI menu-state, not connection/`clcState`, so
  it doesn't close this gap on its own.
- **Decision: did not wire either feature's real enable path into the
  live per-frame call.** The existing x64 early-return stub in
  `RunFullScreenPostProcessIfEnabled` was left exactly as-is rather than
  removed or partially relaxed -- per `CLAUDE.md` §7 (verified live,
  no placeholder hooks) and §5 (fail loudly rather than hook garbage),
  shipping a full-screen capture-and-redraw pass with a known-incomplete
  safety-gate set, that cannot be live-tested by this pass, is a real
  crash risk to the actual game process, not an acceptable "probably
  fine" gap. Nothing built this round changes runtime behavior on
  either platform.
- **Next step when resumed**: find `FUN_00679010`'s x64 equivalent
  (likely via its one real caller chain, `FUN_00679db0`/its own
  device-creation-time caller, mirroring how the x86 original was
  found) for render-scale; find `clcState`/the in-level flag's x64
  addresses via either a fresh Ghidra pass with real analysis enabled
  (not `-noanalysis`, to pick up indirect references the current
  tooling misses) or a live-diagnostic capture once other x64 work
  reaches a live-testable state.
**Plugin API ported to x64, same day -- build-verified, not yet live-tested.**
Read x86's plugin API in full first, per this project's own standing
comparison discipline. The loading infrastructure itself
(`mw3ncp_plugin_api.h`, `plugin_loader.h`/`.cpp`, the `[Plugins] Enabled`
path in `mod_config.h`/`.cpp`) turned out to need ZERO code changes:
`mw3ncp_plugin_api.h` is plain C with no architecture-specific types by
design (deliberately kept ABI-stable across compilers/DLLs), and
`plugin_loader.cpp` was already compiled unconditionally for both
platforms in `proxy_d3d9.vcxproj` (no `Condition="'$(Platform)'=='x64'"`
exclusion, unlike `analog_input_hooks_x64.cpp`) -- its dependencies
(`InstallHook`/`RemoveHook` via the host's own MinHook instance,
SEH-guarded `ReadMemory`/`WriteMemory`, `LogFromController`,
`GetGameWindow()`, `GetGameModuleBase()`, `SetPluginTextGlyphColorOverride()`)
all live in files (`d3d9_hook.cpp`, `overlay_hud.cpp`, `dllmain.cpp`,
`mod_config.cpp`) that were already confirmed cross-platform during the
2026-09-03/04 migration audit. Specifically confirmed live-real, not
assumed: `GetGameWindow()` returns `g_gameHwnd`, which is set by the same
`SetWindowLongPtrA`/`GWLP_WNDPROC` subclass call this session's own x64
auto-unstick feature (`SendPeriodicActivationNudgeX64`) already proved
working live against the real running game -- so the plugin API's one
real prerequisite (a working game window handle) was already satisfied
before this port even started.

Verified via a forced x64 `/t:Rebuild` (0 errors, `plugin_loader.cpp`
compiles clean, links clean; `dumpbin /headers` confirms `8664 machine
(x64)`) and a Win32 regression rebuild (0 errors, no regression) --
both builds redirected to a local, non-deployed output folder for
verification only, since the project's `OutDir` is an absolute path
into the live shared game install and this round of work happened in an
isolated worktree alongside other concurrent x64 work; the primary
session deploys for real once this is merged.

The one real, necessary code change: the bundled RGB Text example
plugin's own project (`tools/example_plugin_rgb_text/example_plugin_rgb_text.vcxproj`)
was Win32-only (a leftover from when `iw5sp.exe` itself was x86) -- a
plugin DLL must be the SAME bitness as the process it loads into, so
the old Win32-only build could never have loaded into today's x64 game
at all, regardless of whether the host-side loader worked. Added a
`Debug|x64` project configuration (Win32 kept for anyone still running
an archived `-x86` build) and split `OutDir`/`IntDir` by `$(Platform)`
so the two builds don't clobber each other's output. Verified via a
clean x64 rebuild (0 errors) plus `dumpbin /headers /exports` confirming
`8664 machine (x64)` and both required exports (`MW3NCP_PluginInit`,
`MW3NCP_PluginShutdown`) present with correct names; Win32 rebuilt clean
too (no regression on the existing config).

**Not yet live-tested** -- no build here was deployed to the live game
install (see above), so "the plugin loader actually finds/loads/inits a
real plugin DLL against the real running x64 game" is still an open
confirmation, same as this session's other x64 features awaiting live
play. Next step when resumed: deploy a real x64 build, drop the built
`rgb_text_plugin.dll` (x64 config) into a `plugins` folder next to it,
set `[Plugins] Enabled=1` in `mw3ncp_config.ini`, and confirm live that
every piece of text/glyph this mod draws actually rainbow-cycles --
this is also this project's own live-verification vehicle for the API
itself, per the plugin's own header comment.

**Sniper Fire/ADS -- first real fix attempt, same day, root-cause investigation
resumed in parallel with the three forks above -- BUILD-VERIFIED, NOT YET
LIVE-TESTED.** Picked back up exactly where the docs/ETA pause left off:
decompiled `FUN_14007fc00` (the per-case "pre-call" every case in
`FUN_14007c3a0`/`g_stanceDispatch` makes when `param_2 != 0`, flagged as an
unconfirmed hypothesis before the pause) via a headless Ghidra
`DecompileAt.java` run against `iw5sp_x64_proj`.

**What `FUN_14007fc00` actually is, now confirmed, not guessed:** a real
client->server RELIABLE COMMAND send, matching this engine family's classic
Quake3-descended reliable-command channel exactly. Gated on a real
connection-state check (`FUN_14026afa0`: `DAT_142533370 == 2`) and a
demo/override-flag check (`FUN_140265a20`), it formats the case number into a
short string (`"n %i"` -- confirmed via `ReadStringAt.java`/`DumpRawBytes.java`,
a genuinely standalone literal, MSVC tail-merged in the same string pool as
`"cubemapShot"` and others) and calls `FUN_14007fb30`, which writes it into a
128-entry ring buffer (`(seq+1) & 0x7f`, per-client stride `0x3618`) with the
exact `"EXE_ERR_CLIENT_CMD_OVERFLOW"` overflow message this engine's own
reliable-command mechanism uses. In plain terms: every real bind press/release
(including a real keyboard `+attack`/`+ads`) tells the game's own (local,
loopback) server "bind N fired," entirely separately from raw `usercmd_t`
button state.

**Case numbers for Fire/ADS confirmed by address match, not position** --
directly applying the standing compare-to-x86-original discipline (x86's own
issue #3: never trust a bind-index as a case number without independent
confirmation). Case 1/2 in `FUN_14007c3a0` call the real kbutton
activate/deactivate pair on `&DAT_140644818` -- the EXACT address
`kFireStructInsnOffset` already resolves to `g_fireStruct`. Case 0xd/0xe call
the same pair on `&DAT_1406448e0` -- the EXACT address `kAdsStructInsnOffset`
already resolves to `g_adsStruct` (case 0xd/0xe also clear the ads-toggle-flag
byte first, matching this project's own `g_adsToggleFlag` handling exactly).
Confirmed: **case 1 = "+attack" down, case 2 = "-attack" up, case 0xd =
"+ads" down, case 0xe = "-ads" up.**

**Why this matters -- the same gap x86's own research already flagged and
never confirmed.** `re_notes/iw5sp.md`'s Predator Missile section (2026-07-17)
already raised this exact hypothesis for x86, months before x64 existed:
"this project's Fire (RT) is raw `usercmd_t` button bits, not a synthesized
`+attack` bind/command execution. If `notifyonplayercommand` only fires on
real bind/command dispatch (not raw usercmd bits), that directly explains
[a GSC-side gate never firing]... Worth a native-side check... before
assuming this is the whole story" -- flagged, never independently confirmed,
on either binary, until now. `notifyonplayercommand`/`notifyoncommand` are
already confirmed (same file) as the general native<->GSC bridge for
player-triggered actions. x64's own Fire/ADS implementation (like x86's) calls
the real kbutton activate/deactivate handlers DIRECTLY, bypassing
`FUN_14007c3a0` entirely -- so this reliable-command notify never fires for
controller Fire/ADS, unlike a real keyboard press. **Leading hypothesis, not
yet confirmed**: a sniper-class weapon's bolt-action/scope state machine is
plausibly the one weapon class whose GSC/native logic needs this notify
(a "+attack"/"+ads" bind literally happened) where most other weapon classes
evidently don't.

**Fix, deliberately minimal and additive.** Resolved `FUN_14007fc00`'s real
address as a fixed function-to-function byte offset from the already-resolved
`g_stanceDispatch` anchor (`0x14007fc00 - 0x14007c3a0 = 0x3860`) -- the same
anchor-plus-fixed-offset pattern this file already uses everywhere for
struct/field offsets, here applied to a CODE offset instead of a data offset
for the first time (holds for the identical reason: both addresses are fixed
positions within the same static PE image, differing only by the one ASLR
base that cancels out in the subtraction). Calls the resolved function
(`g_notifyBindDispatch`) with the confirmed case number on every Fire/ADS
down/up edge, alongside (never instead of) the existing direct kbutton calls
-- if the hypothesis is wrong, the extra notify is inert; the proven-working
kbutton logic is completely untouched.

**Build-verified**: x64 `/t:Rebuild` (0 errors) -> `dumpbin /headers`
confirmed `8664 machine (x64)` with a fresh `LastWriteTime` -> Win32
regression rebuild (0 errors, `analog_input_hooks_x64.cpp` correctly excluded
from the Win32 compile, no regression). **NOT YET LIVE-TESTED -- this is a
fix ATTEMPT, not a confirmed root-cause resolution.** Next step when the user
next plays with a sniper-class weapon: confirm Fire/ADS now work, and that
non-sniper weapon classes show no regression. If the symptom persists, the
next RE angle is tracing `FUN_14007fb30`'s ring-buffer consumer server-side
(who reads `"n %i"` off the reliable-command queue and what it does with it)
rather than assuming the notify alone was sufficient.

**New live bug report, same day: "no visual rendered elements show on screen
... including our own mw32011ncp started messages" -- one real cause CONFIRMED
and fixed, scope of the rest still open.** User confirmed via clarifying
question: the game itself (menus/HUD/gameplay) renders completely normally --
only this mod's OWN drawn elements (toast notifications, glyph icons, hint
prompts, custom cursor) fail to appear.

Investigation, `proxy_d3d9.log` first (a session from earlier the same day):
`EndScene` hook confirmed firing ("confirmed alive"), and the startup toast's
own `DrawPrimitiveUP` call logged `hr=0x00000000` (success) at least once.
`RunFullScreenPostProcessIfEnabled` (the visual-enhancement pass) is a
confirmed no-op stub on x64 right now, so it can't be painting over anything.
`Hook_CreateDevice` doesn't touch `D3DPRESENT_PARAMETERS`/MSAA on either
platform. `EnsureTextTexture`'s `CreateTexture`/`GetSurfaceLevel`/`LockRect`
chain has proper HRESULT checks and logs on failure -- none of those failure
lines appear in the log, so the text-texture pipeline isn't silently erroring
either.

**One real, confirmed cause found and fixed: `DrawCustomCursorIfNeeded`
(overlay_hud.cpp) read two RAW, UNGUARDED x86-only hardcoded addresses**
(`kCursorVisibleFlagAddr = 0x01c00474`, `kCursorUiStateAddr = 0x01c0ad14`) --
meaningless against x64's real module base (`0x140000000`, confirmed via this
file's own env-diag log line) and almost certainly unmapped memory in the x64
process. This is the exact same landmine class the 2026-09-04 crash audit
already found and fixed ~30 instances of in this file -- but this ONE slipped
through that audit specifically because the whole function is wrapped in
`__try`/`__except`: on x86 the addresses are real and safe, but on x64
dereferencing them is almost certainly an access violation that SEH silently
swallows instead of crashing, so it failed completely invisibly (no crash, no
log line) rather than surfacing the way `IsMenuActive`'s own unguarded read
did (that one crashed outright, which is WHY it got caught during the
2026-09-04 audit and this one didn't). **Fixed** with the same `#if defined
(_M_X64) || defined(_WIN64)` early-return guard every other x64-deferred
function in this file already uses -- honest "not yet ported" behavior, not a
real cursor-visibility fix (finding this engine's actual x64 cursor-state
equivalents is real future RE work, not attempted this pass). Build-verified
on both platforms.

**Not yet fully explained**: the cursor fix accounts for the cursor
specifically, but the toast/glyph/hint symptoms need their own investigation
-- a secondary finding worth flagging: `[manual-glyph-diag]` log lines show
`ShouldDrawGlyphOverlay()` (analog_input_hooks.cpp) genuinely returning true
at points during the session (`overlayOn=1`), yet `realGroup=""` `realIndex=-1`
`siblingCount=-1` in EVERY logged line regardless -- meaning even when the
gate allows drawing, the underlying glyph-position-resolution mechanism never
found a single valid focused-item position throughout the whole session. That
could be its own separate x64 bug (position resolution failing to find real
data) rather than a draw-call visibility problem, and hasn't been isolated
from the toast's own apparent one-time success. Full audit of
`overlay_hud.cpp`'s other draw functions (`DrawGlyphIconIfRequested`,
`DrawGameplayHintSlotsIfRequested`, `DrawMenuHintsIfRequested`) for similar
unguarded address reads, plus tracing why glyph-position-resolution comes back
empty, is the next step -- handed to a dedicated fork this same session.

**Visual-enhancement suite x64 port, second attempt (same day, direct
follow-up instruction: "implement as much as we can... as long as it works
and doesnt regress") -- STILL BLOCKED, nothing shipped, one real scope
correction found.** Picked up exactly where the first attempt (`bad3d26`)
left off, with a much more exhaustive push on the same two gaps before
concluding they're genuinely static-RE-hard, not just under-tried.

**Scope correction, found before writing any code**: re-checked whether x86
actually ships FXAA or "better MSAA" as real, shipped features before
treating them as port targets. **Neither exists on x86.** `grep`-ing the
entire x86 `analog_input_hooks.cpp`/`overlay_hud.cpp` for `Fxaa`/`FXAA`
turns up only forward-looking comments ("a future FXAA/motion-blur pass
would set its own [pixel shader constants] the same way") -- FXAA was
planned (`twinkly-tickling-gem.md` Phase C) but never built. Likewise every
`MultiSampleType`/`MultiSampleQuality` mention is an incidental
`D3DSURFACE_DESC` struct-field reference or an issue #93 diagnostic note,
not a real `[Graphics] ForcedMsaaSamples`-style feature. **Building these
for x64 first would not be "reaching x86 parity" -- it would be new work
beyond parity**, against a plan-only spec x86 itself never validated live.
Correctly out of scope for this task; not attempted. If genuinely wanted,
these need their own fresh scoping/implementation pass on x86 first (or a
direct decision to build x64-first, which is a real option but a different
task than "port to reach parity").

**`InternalRenderScalePercent` (`FUN_00679010`'s x64 equivalent) -- still
NOT located, not re-attempted this round.** The first attempt's own
diagnosis (x86's ONE real caller of `FUN_00679010` is reached via what is
very plausibly an indirect/function-pointer call, the same static-xref-proof
shape that already stumped the ORIGINAL x86 investigation for 9+ rounds
including a full Ghidra pass) is a structural finding, not a tooling gap a
different script fixes -- confirmed by finding the SAME shape independently
on x64's render-target-name-table caller chain (`FUN_1401b8c80`). Spent this
round's effort on the more tractable-looking `clcState` gap instead (below);
if `clcState`/in-level-flag get resolved in a future pass, `FUN_00679010`
would still need its own dedicated live-tracing session (an actual
injectable build logging its own caller's return address at runtime is the
real next step, not more static xref scanning -- consistent with the first
attempt's own conclusion).

**`clcState`/in-level-flag x64 equivalents -- still NOT located, despite a
genuinely more thorough attempt than the first pass, using three independent
static techniques in sequence:**
1. Re-ran `RawStringScan.java` against `"SCR_DrawScreenField: bad clcState"`
   AFTER running a real, full Ghidra auto-analysis pass on a working copy of
   the project (the original attempt used `-noanalysis` throughout this
   session for speed -- confirmed here that headless `-process` WITHOUT
   `-noanalysis` really does run the complete default analyzer pipeline,
   including `Data Reference`/`Scalar Operand References`, and completes in
   under a minute on this already-substantially-analyzed project, not the
   many-minutes worst case assumed). **Still 0 references** to the string.
2. Wrote a new script, `FindLeaRefsToAddr.java` (committed, genuinely
   reusable for future indirect-reference hunts), that scans raw
   executable-section bytes directly for `LEA reg,[RIP+disp32]` instruction
   encodings whose COMPUTED target equals the string's address -- bypassing
   Ghidra's instruction/reference database entirely, so it doesn't matter
   whether Ghidra's own disassembler ever visited or recognized the
   instruction. **0 hits.**
3. Broadened the same script to scan EVERY initialized memory block (not
   just executable ones) for both the LEA-rip pattern AND a raw 8-byte
   absolute-pointer match (covering a `MOV r64, imm64` load or a plain
   pointer-table entry). **Still 0 hits, anywhere in the loaded image.**

Three independent techniques, one exhaustive raw-byte pass across the whole
loaded image, zero references found. This rules out "wrong analyzer" or
"missing xref" as the explanation -- either this exact error string's print
call site was compiled out of this specific release build entirely (leaving
an orphaned literal in `.rdata`, which the compiler/linker can do even for
reachable error paths depending on how the string pool is organized), or the
real reference is constructed through an addressing pattern this scan
doesn't cover (e.g. built from two separate 32-bit halves at runtime,
vanishingly unlikely for a compiler-emitted string load, but not
disprovable without a live trace). Either way: **this is not the same class
of gap a smarter static script fixes** -- `x64_migration/README.md`'s own
"second independent binary generation confirming the same [indirect-call]
gap" framing for the render-scale problem applies here too, now backed by a
much more exhaustive attempt than the first pass made.

**Decision, same standard as the first attempt: nothing shipped, no gates
relaxed.** Confirmed a weaker substitute (menu-active gate alone, which x64
already has via `g_menuActiveGateFlag`) is NOT an acceptable stand-in --
that's the EXACT configuration x86's own issue #103/#104 already proved
crashes on loading screens and quit-to-menu. Shipping FSR RCAS or motion
blur gated on menu-active alone would knowingly reproduce an
already-documented crash class, which is explicitly out of bounds even
under this round's "move fast" instruction (extended-playtest bugs are
acceptable; re-shipping a KNOWN crash is not). The x64 early-return stubs in
`RunFullScreenPostProcessIfEnabled`/`RunPreOverlayMotionBlurPassIfEnabled`
are left exactly as they were.

**Next step when resumed**: this specific pair of addresses (and
`FUN_00679010`'s equivalent) most likely needs LIVE tracing rather than more
static analysis -- e.g. a diagnostic build that logs candidate global
addresses' values across a real play session spanning menu/loading/gameplay
transitions, then correlating by hand (the same methodology x86's own
issue #99/#100 ultimately had to fall back on after its own static/heuristic
attempts at clcState's real mapping were reverted for being wrong). Not
attempted here since this pass has no live-testable build to run it against.

**Follow-up audit complete, same day -- no further code changes needed beyond
the cursor fix above.** Full audit of every function `Hook_EndScene` calls
(`overlay_hud.cpp`): `DrawCustomOptionsMenuIfOpen`, `DrawOverlayMessage`,
`DrawGlyphIconIfRequested`, `DrawGameplayHintSlotsIfRequested`,
`DrawMenuHintsIfRequested`, `DrawDebugMarkerIfRequested`,
`DrawGlyphEditHandlesIfRequested`, `ApplyForcedAnisotropicFilteringIfEnabled`,
`ApplyForcedHighQualityShadowsIfEnabled`, `ApplyForcedHighQualityLightingIfEnabled`
-- every body checked line-by-line for raw hex constants in the x86 process-
space range (`0x00400000`-`0x02000000`) outside an `#if defined(_M_X64)` guard.
**Zero additional landmines found** -- every hex literal in these functions is
a color/flag constant (`0x00FFFFFF`, `0x90000000u`, etc.), not an address. The
cursor was the ONLY real `__try`-wrapped SEH-hidden landmine in this file's
own draw path (confirmed via a full `__try` census: `overlay_hud.cpp` has
exactly one, the cursor's, now fixed).

**Glyph-position-empty finding, resolved -- NOT a bug.**
`TryGetStableFocusedGroupAndIndex` (`analog_input_hooks.cpp`) is already an
explicit, deliberate x64 stub: `#if !defined(_M_X64) && !defined(_WIN64)` guards
the real x86-only implementation (depends on `GetMenuStackDepth()`/
`TryGetRealFocusedGroupAndIndex()`, neither ported), `#else` returns `false`
unconditionally with its own comment already documenting this exact case:
"x64: not yet ported... returning false is this function's own real contract
for that case already, every caller already handles it." The observed
`realGroup="" realIndex=-1` in every `[manual-glyph-diag]` log line is this
stub working exactly as designed and documented, not a regression.

**Broader finding: the entire glyph/hint-icon system is currently unwired for
x64, not landmine-broken.** `analog_input_hooks_x64.cpp` makes zero calls to
any glyph/hint-request function (`RequestGlyphIcon`,
`RequestMenuHintOverlay`, or equivalent) -- confirmed via a whole-file grep.
`DrawGlyphIconIfRequested`'s own gate (`g_pendingIconRequestedThisFrame`) and
`DrawGameplayHintSlotsIfRequested`'s equivalent per-slot flags are plain,
harmless booleans that simply never get set to true on x64, since nothing in
the x64 input-hook file requests one. This isn't a hidden failure -- it's
real, honest, not-yet-ported scope (matching this project's own convention
elsewhere), and traces back to the same root cause as the stub above: the
menu-focus/itemDef-position-reading infrastructure this whole system is built
on was never ported to x64. `InjectSyntheticBackHintIfNeeded`'s own chain
(`IsInsideSpecOpsNestedModal`) is similarly and harmlessly inert on x64 for
the same reason -- it reads `g_focusedItemName`, a global only ever populated
by an `#if !defined(_M_X64)`-guarded naked-asm hook, so it correctly,
non-crashingly stays empty and the function correctly no-ops.

**Net conclusion on the "no visual elements" report**: the startup toast
(`DrawOverlayMessage`/`ShowStartupMessage`) IS wired for x64 and the log
already shows it drawing successfully once (`hr=0x00000000`) -- the user may
simply have missed its one 15-second window at launch, not a code bug.
Everything glyph/hint/cursor-related genuinely doesn't draw on x64 today,
but for an honest, already-scoped reason (menu-focus-tracking infrastructure
not yet ported), not a hidden regression -- the cursor was the one place that
gap manifested as a SILENT landmine instead of an honest no-op, now fixed to
match the honest-no-op pattern everything else in this class already follows.
**Real x64 port of menu-focus/itemDef-position tracking** (to make glyphs/
hints/the in-game glyph editor actually draw on x64, not just fail safely) is
the concrete next step toward closing this specific parity gap -- a genuine,
scoped RE task (native x64 equivalents of `GetMenuStackDepth`/
`TryGetRealFocusedGroupAndIndex`/`g_focusedItemName`'s populating hook), not
attempted this pass.

**Custom Options screen wired into x64's input pipeline, same day --
BUILD-VERIFIED, NOT YET LIVE-TESTED, one real gap honestly flagged rather than
guessed around.** Read x86's own `CustomOptionsMenu_TickInput`/
`DrawCustomOptionsMenuIfOpen` (`overlay_hud.cpp`) in full first, per this
project's standing compare-to-x86-original rule. Confirmed by inspection: the
whole draw/navigate-once-open half of this feature is ALREADY genuinely
cross-platform -- zero hardcoded addresses anywhere in either function -- so
none of that needed porting.

**The real gap**: x86's own real TRIGGER for opening the screen
(`InjectControllerMenuNav`, `analog_input_hooks.cpp`) never runs on x64 at
all -- x64's per-frame input pipeline never called `CustomOptionsMenu_TickInput`,
so `g_optMenuOpen` could never become true and the screen was completely
unreachable in-game, independent of the "no visual elements" bug documented
above. **Fixed**: a new `PollCustomOptionsMenuX64()` (`analog_input_hooks_x64.cpp`),
called from the same always-on `InjectMenuInputTick` tick `PollPauseToggleX64`/
`AutoUnstickPauseCycleX64` already use (must run there, not the gameplay tick,
since the gameplay tick halts entirely while a menu/pause is active -- the only
time this screen is ever reachable). Computes the same D-pad/A/B/LB/RB edges
`CustomOptionsMenu_TickInput` expects, using this file's own already-resolved
primitives (`IsPhysicalHeld_Exported`, `kXI_DPAD_*_X64`, `g_menuActiveGateFlag`
bit `0x10` for "is a real menu active" -- all already confirmed working on x64
this session, none re-derived).

**Gap ABOVE -- RESOLVED 2026-09-12, real x64 offsets re-derived and wired
in.** x86's real open-trigger detects a real NATIVE menu-item focus via
`TryGetRealFocusedGroupAndIndex` (`analog_input_hooks.cpp`) -- a raw
itemDef-array walk hardcoding a **4-byte pointer stride** (`arr + i * 4`) and
item-struct field offsets (`+0x48` flags, `+0x0` name pointer, `+0xa8`/`+0xac`
array count/pointer), all genuine 32-bit-pointer-width assumptions. On x64
(8-byte pointers) this reads misaligned garbage, which the function's own
`LooksSane()` checks correctly reject -- it always returns false. This
independently confirmed the exact same symptom the concurrent
overlay-visibility audit fork found via `[manual-glyph-diag]` log evidence
(`realGroup="" realIndex=-1` every frame, regardless of
`ShouldDrawGlyphOverlay()`'s own value) -- same underlying mechanism, found
from two different angles the same day.

**2026-09-12 -- dedicated Ghidra decompile pass completed** (a genuinely
different x64 struct layout, not just doubling the x86 stride, exactly as
flagged above as the required next step). Full trail:
`re_notes/x64_migration/decomp_gettopmostmenu_x64.txt`,
`decomp_menuctx_helpers_x64.txt`, `decomp_itemnav_x64.txt`,
`decomp_itemhelpers2_x64.txt`, `decomp_itemfocus_x64.txt`,
`impl_sig_14029baa0.txt`, `impl_sig_1402aaa80.txt`. Every offset
independently cross-checked against MULTIPLE real consumers before being
trusted, per this project's own issue #3 lesson:
- Real x64 UI-context global (x86's `kMenuStackCtx` equivalent):
  `DAT_142605050` -- a fixed, single-instance data address confirmed via its
  own literal `LEA RCX,[0x142605050]` in `FUN_14029baa0` (the real x64
  `Menu_KeyEvent` caller/resume-path) and reused unmodified across 6 other
  menu-stack helpers decompiled this pass.
- `ctx + 0x14C0` = real open-menu-stack depth (x86: `kMenuStackCtx + 0xA7C`),
  confirmed via TWO independent functions reading the identical offset
  (`FUN_1402aaa80`, `FUN_1402ad530`).
- `ctx + 0x1440` = real open-menu stack array base, 8 bytes/entry, confirmed
  via the same two functions.
- `FUN_1402aaa80(ctx)` = the real x64 `GetTopmostActiveMenu()` equivalent
  (x86's `FUN_00547980`) -- confirmed via its OWN call site in
  `FUN_14029baa0` (`plVar3 = FUN_1402aaa80(&DAT_142605050);` feeding directly
  into `FUN_1402aac50`, the confirmed x64 `Menu_KeyEvent` -- the exact
  "resolve the topmost menu, then route input into it" shape x86's own
  `ForwardKeyToMenu` uses) AND structurally via its own disassembly (walks
  the stack top-down, returns the first entry whose per-player flags at
  `entry+0x58+player*4` have bits 0x4 and 0x2 both set).
- The returned menu's item count/array live at `menu+0xB8` (`0x17*8`, x86's
  `menu+0xa8`) and `menu+0xC0` (`0x18*8`, x86's `menu+0xac`), confirmed via
  THREE independent consumers agreeing on both offsets (`FUN_1402aac50`,
  `FUN_1402ac5d0`, `FUN_1402ac6f0`).
- Each itemDef's per-player focus flags live at `item+0x50+player*4` (x86's
  `item+0x48`, no player index there), confirmed via FIVE independent
  consumers testing the identical `(flags & 4) != 0 && ((flags >> 1) & 1) !=
  0` pair (`FUN_1402aac50`, `FUN_1402ad560`, `FUN_1402b21b0`,
  `FUN_1402b1de0`, `FUN_1402a7f00`).
- Each itemDef's name pointer lives at `item+0x0`, UNCHANGED from x86 (the
  first field of a struct can never shift regardless of pointer width) --
  confirmed via TWO independent consumers directly string-comparing
  `*itemPtr` against known literal item-name strings (`FUN_1402ac6f0`,
  `FUN_1402a4580`), not merely assumed from struct-layout convention.

Implemented as `TryGetRealFocusedGroupAndIndexX64` (`analog_input_hooks_x64.cpp`,
new "Menu-focus / itemDef-array tracking, x64 port" section) -- structurally
identical logic to x86's own function, only the offsets/stride differ, exactly
per this section's own earlier note that this would need re-deriving, not
recalculating. `GetMenuStackDepthX64`/`GetTopmostActiveMenuX64` ported
alongside it as support functions. Resolution follows this file's own
established signature-scan-once-at-startup convention: `DAT_142605050`'s
address is resolved via `SigScan::ResolveRipRelative` off a real RIP-relative
`LEA` inside a signature-matched `FUN_14029baa0` prologue;
`FUN_1402aaa80`'s entry point is resolved via its own dedicated signature for
a direct call (no hook installed, same pattern as `g_weaponNext`/
`g_pauseToggle`/`g_actionSlot`). Both resolves are logged
(`[x64-menufocus]`), independent of each other and of every other resolve in
`InstallAnalogInputHooksX64()` -- a failure here only disables the real
Options-screen trigger and the diagnostic below, the LB+RB chord (already
resolved via `g_menuActiveGateFlag`) stays available regardless.

**Real Options-screen trigger now wired**, mirroring x86's own
`InjectControllerMenuNav` exactly: real native menu-item focus landing on the
pause/campaign/specops menu's own real "Options" button (`PAUSE_LIST`/1,
`CAMPAIGN_BUTTON_LIST`/3, `SPECOPS_BUTTON_LIST`/5 -- the same group
names/indices x86 uses, expected to carry over unchanged since the x86->x64
migration was a code recompile against the same game data, not a content
update). Wired into `PollCustomOptionsMenuX64`, OR'd together with the
existing LB+RB chord into a single `openRequestedEdge` -- **the chord is KEPT
as a fallback, not replaced outright**, until the real trigger is
live-tested; both funnel into the same `CustomOptionsMenu_TickInput` call so
there's no double-open risk from having both wired at once. A deduped
`[x64-menufocus-diag]` log line (haveFocus/group/index/siblingCount, logged
only on change) and a one-shot `[x64-optmenu-realtrigger]` fire confirmation
give direct live visibility into whether the port is resolving real, changing
focus state during actual play.

**Scope note, honestly flagged**: this closes the menu-focus/itemDef-tracking
dependency specifically. Full gameplay controller-glyph icon overlays (the
in-hint "Press [A]" replacement, which needs the caller to already know the
native text draw's own screen-space position/scale) remain blocked on a
SEPARATE, not-yet-ported piece -- the native text-draw hook (x86's
`Hook_DrawGlyphText`) has no x64 equivalent yet. That is a different,
larger RE task (hooking the actual text-draw call site, matching fonts, the
glyph allowlist) not attempted this pass -- only the menu-focus/itemDef
detection this section covers is resolved. `g_focusedItemName`'s x86-only
naked-asm populating hook (x86's `FUN_00616230`, a `getfocuseditemname()`
VM-opcode hook) was also NOT ported this pass -- a genuinely separate RE
thread (finding the x64 GSC-VM opcode dispatch and its own case for this
specific builtin), not a quick addition; `TryGetRealFocusedGroupAndIndexX64`
is the PRIMARY signal now and does not depend on it, so this is a secondary,
still-open gap, not a blocker.

**UPDATE 2026-09-13**: the separate, larger RE task named above (the native
text-draw hook, x86's `Hook_DrawGlyphText`) has now shipped -- see this
issue's own later "UPDATE 2026-09-13" round under the Auto-Mantle section
below, and the dedicated trail in `re_notes/x64_migration/drawtext_hook_x64.md`.
That port covers hook installation and Mantle-hint DETECTION only -- no
visual glyph-icon substitution (the actual "Press [A]" replacement this note
describes) is wired yet, so this scope note's own core conclusion (full
gameplay glyph icon overlays remain blocked) still stands, just no longer on
a completely unstarted piece -- the hook now exists for a future pass to
build substitution on top of.

**Build-verified**: x64 `/t:Rebuild` (0 errors) -> `dumpbin /headers` confirmed
`8664 machine (x64)` with a fresh `LastWriteTime`. **Win32 regression rebuild
blocked this pass by an unrelated, pre-existing/concurrent issue** (a
mismatched `#if`/`#endif` in `rumble.cpp`, a file this work never touched,
left uncommitted by a different concurrent session) -- confirmed via the
project's own `proxy_d3d9.vcxproj` that `analog_input_hooks_x64.cpp` is
entirely `ExcludedFromBuild` for any platform other than x64
(`Condition="'$(Platform)'=='x64'"`), so this change cannot be the cause and
cannot regress the Win32 configuration; a clean Win32 rebuild should be
re-attempted once that unrelated file's own edit is resolved. **NOT YET
LIVE-TESTED** -- next step when the user next enables `UseCustomOptionsScreen`
and tests: confirm the real trigger (approach the pause/campaign/specops
menu's own Options button with a real controller and press A) opens the
screen without needing the LB+RB chord at all, watch `proxy_d3d9.log` for
`[x64-menufocus]`/`[x64-menufocus-diag]`/`[x64-optmenu-realtrigger]` lines
confirming live resolution, and re-confirm the panel/blur/list draw
correctly, D-pad/A/B navigate and select rows, and closing returns cleanly to
the native menu with no regression to normal D-pad/A/B gameplay input once
closed. Once confirmed reliable, the LB+RB chord can be removed as no longer
needed.

**"Greenlit" trusted-plugin allowlist added to `plugin_loader.cpp`, same
day.** Direct instruction: the sibling `MW32011NSP` project's own netcode
security-fix plugin should ship built into this mod by default, not gated
behind the normal third-party-plugin opt-in acknowledgment
(`[Plugins] Enabled=1`). Added a small, explicit allowlist
(`kTrustedPluginFilenames`, currently one entry:
`mw32011nsp_security.dll`) that `LoadPlugins()` checks and loads
UNCONDITIONALLY -- the general opt-in scan for every other `.dll` in the
`plugins\` folder is completely unchanged, still fully gated on
`PluginsEnabled`. A single directory-scan pass now handles both paths (a
file is either the one trusted filename or an ordinary plugin, never
both), so there's no double-load risk to guard against separately. Real,
explicitly documented caveat (`PLUGIN_API.md`'s new "Greenlit (trusted)
plugins" section): filename matching is not cryptographic -- this is a
default-behavior UX convenience over the same physical-access trust
boundary the rest of the plugin system already rests on, not a new
security guarantee.

**Build-verified**: x64 `/t:Rebuild` (0 errors) -> `dumpbin /headers`
confirmed `8664 machine (x64)` with a fresh `LastWriteTime` -> Win32
regression rebuild (0 errors, no regression). **Cannot be end-to-end
tested yet** -- `mw32011nsp_security.dll` doesn't exist yet (NSP's own
implementation is a separate, parallel effort); the loader logs a clear
line either way ("matches the greenlit/trusted allowlist" or the
no-plugins-folder-found case) so this is independently verifiable the
moment that DLL exists, without needing to touch this code again.

**Corrected gap list, 2026-09-12 (direct instruction: "ALL 0.3.5 stuff
needs to be present at the same level or better") -- this issue's own
"Known gaps" summary (also mirrored in README.md) was INCOMPLETE, not
just imprecise.** A real, systematic audit against
`legacy-x86-docs/README.md`'s own "Status at a glance" and "Feature
completeness matrix" (the actual authoritative record of what v0.3.5-x86
had, not this file's own summary of it) found real gaps that were never
listed here at all:

- **Vibration/rumble: 100% unported to x64, not attempted.**
  `Rumble_Install()` (`rumble.cpp`) is only ever called from
  `InstallAnalogInputHooks()` (`analog_input_hooks.cpp`), which is
  entirely wrapped in `#if !defined(_M_X64) && !defined(_WIN64)` -- on
  x64 this call site simply never executes. Not a signature-scan failure
  (which would at least log something), not a landmine (nothing crashes)
  -- it's silent, total absence, confirmed via direct code trace, not
  inferred. x86 scored this 1.5/2 in the completeness matrix (fire
  rumble live-confirmed, damage rumble live-confirmed via health-poll,
  known gaps only around Body Armor hits and 2-player co-op) -- a real,
  previously-shipped, fully-working feature currently regressed to
  nothing on x64. Real next step: port `rumble.cpp`'s two hook/poll
  mechanisms (fire-effects hook, per-frame health-poll for damage) the
  same way every other x64 hook this session has been ported --
  signature-scan resolution replacing the x86 hardcoded byte pattern,
  following this file's own established `analog_input_hooks_x64.cpp`
  conventions.
- **DualSense gyro-aim: not wired into the x64 look pipeline.** Zero
  references to gyro/DualSense-specific state anywhere in
  `analog_input_hooks_x64.cpp`. Lower priority than vibration -- this
  was still a genuine preview/WIP feature even on x86 at v0.3.5 (Bluetooth
  fixed and confirmed, issue #77; USB never independently confirmed by a
  second tester, issue #76) -- but per today's direct instruction, "at
  the same level" means x64 should still reach at least that same WIP
  state, not silently regress to fully absent. Basic DualSense STICK
  input (movement/look) is NOT part of this gap -- `Controller_GetLeftStick`/
  `Controller_GetRightStick` (`controller_input.cpp`, no `_M_IX86`/`_M_X64`
  guards anywhere in that file) already abstract over XInput and DualSense
  transparently and are already in active x64 use
  (`analog_input_hooks_x64.cpp`'s own movement/look hook calls them
  directly) -- only the GYRO-specific additive rotation data is unwired.
- **Confirmed NOT gaps, checked directly rather than assumed** (so this
  correction doesn't overcorrect into re-litigating things that are
  actually fine): `ForceAnisotropicFiltering`/`ForceHighQualityShadows`/
  `ForceHighQualityLighting` (`overlay_hud.cpp`, no arch guards, real
  native `SetDvarBool` calls -- already confirmed firing on x64 via this
  session's own crash-investigation log evidence, `[aniso-force]`/
  `[shadow-quality-force]`/`[lighting-quality-force]` lines). The full
  issue #87 four-thread background architecture (poll, vibration-output,
  config-hot-reload, log-flush -- `controller_input.cpp`, `mod_config.cpp`,
  `asset_capture.cpp`) is present and thread-creation call sites carry no
  arch guards either -- the THREAD infrastructure is intact; the
  vibration gap above is specifically that nothing currently feeds the
  vibration-output thread real trigger events on x64, not that the
  thread itself is missing.
- **Full re-audit against every other v0.3.5-x86 feature (menu nav, D-pad,
  killstreaks, the F2/F3 glyph-position editor, plugin API, etc.) is not
  yet complete** -- this pass focused on the areas most likely to hide a
  silent gap (anything historically implemented via hardcoded x86
  addresses, per this project's own hardcode-era history). Do not treat
  the absence of a new entry here as proof something else is fine; treat
  it as not yet re-checked.

**Full re-audit now complete, 2026-09-12 -- see
`re_notes/x64_feature_parity_audit.md` for the full 61-item table** (34
confirmed present, 21 confirmed absent, 6 partial/regressed), superseding
the "not yet complete" note directly above. Cross-referencing rather than
duplicating that file's full table here -- two findings from it are
important enough to call out explicitly in this tracker too, since they
were NOT covered by this entry's own original gap list above and are, by
player impact, more significant than the vibration/gyro gaps already
documented here:

- **Native D-pad+A menu/UI navigation is 100% absent on x64 -- not just the
  custom Options screen's own open-trigger (already covered above under
  "Custom Options screen wired into x64's input pipeline"), but the entire
  underlying mechanism.** `InjectControllerMenuNav()` (main menu, pause
  menu, options two-pane drill, buy-station/armory lists, slider value
  adjustment) and `InjectControllerMenuBack()` (B's real ESC-forward) are
  BOTH wrapped in `#if !defined(_M_X64) && !defined(_WIN64)` in their
  entirety (`analog_input_hooks.cpp`) and neither is called from x64's
  `InjectMenuInputTick` -- confirmed directly: that function's x64-only
  `#if` block calls only `PollPauseToggleX64`/`AutoUnstickPauseCycleX64`/
  `PollCustomOptionsMenuX64`, and the x86-only block containing both menu-nav
  functions is excluded entirely for x64 builds. Practical consequence: a
  controller player on x64 today cannot navigate ANY native menu at all
  (main menu, pause menu, buy stations, sliders) -- keyboard/mouse is
  required for every menu interaction. This is the single highest-impact
  gap the audit found, ahead of vibration/gyro in practical effect on
  ordinary play, since it blocks basic controller-only menu use entirely
  rather than degrading one specific feature.
- **Sprint (L3) uses `-x86`'s ORIGINAL, deprecated pre-kbutton design, not
  its final shipped one.** `Hook_SprintTick` (`analog_input_hooks_x64.cpp`)
  forces the `pm_flags`-equivalent bit (`FUN_140014a80`'s own field)
  directly -- structurally identical to `-x86`'s first Sprint
  implementation, which was deliberately replaced (2026-07-19, see
  `CLAUDE.md`'s "Sprint's real kbutton" section) once the real `+sprint`
  kbutton was found, specifically because raw bit-forcing gave infinite
  sprint with no native duration/recovery timer and no automatic Extreme
  Conditioning perk override. A real x64 `+sprint`-style kbutton was never
  searched for this session -- the RE effort found and hooked the
  `pm_flags` WRITER itself (a real, working, but earlier-generation
  design) and stopped there. Not yet live-confirmed whether x64 Sprint
  is actually unlimited in practice, but that's the predicted behavior
  given `-x86`'s own documented history with the identical mechanism.

**Real crash on first live deploy of the above (2026-09-05, "game failed to
launch") -- ROOT-CAUSED AND FIXED, same session.** Once NSP's own
`mw32011nsp_security.dll` actually existed and was deployed as a greenlit
plugin (see NSP's own repo for that work) and a freshly-rebuilt `d3d9.dll`
was deployed alongside it, the game failed to launch, deterministically,
across 3 repeated attempts.

**New diagnostic technique for this project: live crash-dump analysis via
WinDbg/`cdb`, not just Event Viewer.** Windows had already been silently
writing full crash dumps to `%LOCALAPPDATA%\CrashDumps\iw5sp.exe.<pid>.dmp`
for every crash this session (a pre-existing `LocalDumps` registry
configuration, not something set up for this investigation specifically);
opening the newest one with `mcp-windbg`'s `open_cdb_dump`, pointed at the
exact just-built `d3d9.pdb` (plus `mw32011nsp_security.pdb` from NSP's own
build output) gave a fully symbolized stack trace and `!analyze -v`
verdict in one step -- a real step up from the Event-Viewer-plus-manual-
`dumpbin`-offset-correlation technique this file's own earlier rounds used
(see "First/Second live crash, found and fixed" above), which only gives
a module+offset, not a symbolized frame. Worth reaching for by name next
time a live crash needs root-causing: check
`%LOCALAPPDATA%\CrashDumps\<exe>.<pid>.dmp` before falling back to Event
Viewer alone.

**Real root cause, and a real methodology lesson on reading the OS
exception code literally.** The reported exception was, again, `0xC0000409`
(`STATUS_STACK_BUFFER_OVERRUN`) -- but `!analyze -v`'s own
`FailFast.Name: INVALID_ARG` / `Subcode: 0x5 FAST_FAIL_INVALID_ARG` line
proved this was NOT a literal stack-cookie/buffer-smash violation (that
would show a different FailFast name) -- Windows reports EVERY
`__fastfail`/`RaiseFailFastException` call under this same generic OS
exception code regardless of the specific underlying CRT-level trigger, so
the exception code name alone is not diagnostic; the fail-fast *subcode* is.
The symbolized stack (`d3d9!InstallAnalogInputHooksX64+0x46c` ->
`sprintf_s<160>` -> `vsprintf_s` -> `__stdio_common_vsprintf_s` ->
`_invalid_parameter_internal` -> `_invoke_watson`) pointed at one exact
line: the brand-new sniper Fire/ADS fix's own diagnostic log call (added
earlier this same session, see "A fix attempt for sniper-class Fire/ADS"
above) formats a 16-hex-digit `%p` plus a `%llX` into `char buf[160]` --
worst-case output is 169 chars + null = 170 bytes, exceeding the buffer by
10. This UCRT's `sprintf_s` fails fast rather than silently truncating
when the formatted output doesn't fit -- **the exact same bug class as
this issue's own earlier "First/Second live crash" round** (a
`kWeaponNextSignature` string overflowing a fixed log buffer in
`signature_scan.cpp`), recurring here in a different file because the new
code was, again, never actually executed against the real game until this
exact live test -- build-verification and byte-signature-matching alone
never exercise a log line's own string-formatting path.

Swept every other `sprintf_s` call site in `analog_input_hooks_x64.cpp`
(16 total, all logging calls added across this project's x64 work) plus
NSP's own fix-module logging (`p2p_fix.cpp`, `matchdatadone_memberjoin_
fix.cpp`, `signature_scan.cpp`) for the same undersized-buffer class before
declaring this done -- every other buffer has comfortable margin against
its own worst-case output; only the one call site was actually broken.
Fixed by widening `buf[160]` to `buf[256]`.

**Build-verified**: x64 `/t:Rebuild` (0 errors) -> `dumpbin /headers`
confirmed `8664 machine (x64)` with a fresh `LastWriteTime`, redeployed to
the live game install. **NOT YET LIVE-RETESTED** -- next step: user
relaunches; if this was the only bug, the game should now reach a normal
session with the greenlit `mw32011nsp_security.dll` loading and its own
`[plugin-loader]`/fix-install log lines appearing in `proxy_d3d9.log`
alongside every other hook's own confirmation line. If a further crash
occurs, repeat the same `CrashDumps` + `cdb` technique above rather than
falling back to Event-Viewer-only triage.

---

**Sprint (L3) migrated to the real `+sprint` kbutton (2026-09-12) -- fixes
the raw `pm_flags`-forcing regression recorded above ("Sprint's real x64
kbutton was never searched for") and identified again independently the
same day by the full feature-parity audit (`re_notes/
x64_feature_parity_audit.md`, finding #2 / row #22).**

The x64 `+sprint`-style kbutton this file's own earlier round said was
"never searched for" has now been found and wired in. Resolved via the
SAME anchor+offset technique already proven for Fire/Reload/ADS
(`kFireStructInsnOffset` etc.) -- confirmed via TWO independent angles,
matching this project's own issue #3 standard (never trust a case/lead
without independent confirmation):

1. Decompiled `FUN_14007c3a0` (`decomp_14007c3a0_full.txt`) case `0x3d`/
   `0x3e` (61/62 decimal -- x86's own exact "+sprint"/"-sprint" case
   numbers) calls `FUN_14007e460`/`FUN_14007e490` on
   `&DAT_1406448f4 + lVar4*0x230` -- the same per-bind-struct pattern
   already confirmed for Fire/Reload/ADS, and the same case-number-carries-
   over-from-x86 pattern already independently confirmed for every other
   bind in this dispatcher (Fire=1/2, Reload=0xb/0xc, ADS=0x3b/0x3c,
   togglecrouch=0x48, etc.).
2. Independent cross-check, the SAME technique x86's own original
   discovery used: case 9 ("+breath_sprint" down, the real default SHIFT
   bind) in the same decompile fires `FUN_14007e460` on `&DAT_14064482c`
   (Hold Breath's alias) AND on `&DAT_1406448f4` back-to-back -- i.e. the
   real default Sprint/Hold-Breath key already drives this exact same
   struct today, mirroring x86's own "case 9 disassembles to two
   back-to-back kbutton calls, one of which is the Sprint kbutton"
   cross-confirmation exactly.

A prior session's RE scratch pass (`re_notes/x64_migration/
rawbytes_sprint_struct.txt`) had already dumped the raw bytes at the two
real `LEA reg,[rip+disp32]` instructions for case 0x3d/0x3e (`0x14007cead`/
`0x14007ced7`) but was cut off by a rate limit before writing any code.
Independently decoded by hand this session rather than trusted blindly:
both `48 8D 05 <disp32>` instructions resolve to `0x1406448f4`, matching
the decompile's `DAT_1406448f4` name exactly -- confirms that scratch lead
was correct.

**Implementation**: `Hook_SprintTick` now calls `g_kbuttonActivate`/
`g_kbuttonDeactivate` (the same real `FUN_14007e460`/`FUN_14007e490`
resolved for Fire/ADS/Reload) on a newly-resolved `g_sprintStruct`
(`kSprintStructInsnOffset = 0xB0D` from the `FUN_14007c3a0` anchor), using
the same synthetic-source-id pattern (own local
`kSprintSyntheticSourceId = 0x1000`, same value/rationale as the later
`kSyntheticSourceId`, kept separate to avoid a `constexpr` forward-
declaration problem -- `Hook_SprintTick` is defined earlier in the file
than Fire/ADS/Reload's own struct-resolve cluster, so `extern` forward
declarations were added for `g_kbuttonActivate`/`g_kbuttonDeactivate`/
`g_sprintStruct`/`g_timestampPtr` instead of relocating the function). The
raw `pm_flags`-forcing code and its `g_sprintBitForcedByUs` bit-ownership
tracking were removed entirely -- no longer needed, since driving the real
kbutton hands `pm_flags` back to native engine ownership, the same handoff
x86 made in 2026-07-19.

Gating excludes ADS (matches x86's `!g_adsHeld` exclusion), computed
locally inside `Hook_SprintTick` from the controller state it already
reads, rather than reaching for the separate `g_adsHeldX64` global
(`Hook_MovementTick`'s own tracking variable, defined much later in the
same anonymous namespace -- same physical-input source either way, this
just avoids a second forward-declaration dependency). x64 has no Hold
Breath kbutton yet (parity audit item #23, confirmed ABSENT, a separately
tracked gap, not this fix's scope), so there's no second consumer of the
bind to stay mutually exclusive with -- this narrows to a plain ADS
exclusion.

**Also ports the rising-edge "stand up from crouch/prone" behavior** from
x86's `InjectControllerSprint` (real console sprint stands the player back
up before running) -- per direct coordinator instruction after the initial
plan flagged this as borderline-in-scope: confirmed as genuine x86
behavior (not optional polish) by directly re-reading
`analog_input_hooks.cpp`, and the standing directive for this whole parity
pass is "all 0.3.5 stuff needs to be present at the same level or better."
Reuses `ForceStandingViaRealToggleX64()` as-is (already built and wired
for Jump's own auto-stand, this same issue's earlier "Jump auto-stand"
round) rather than reimplementing -- same real native toggle-case dispatch
(0x48/0x49 stance cases), just called from a second trigger site (Sprint's
own rising edge while crouched/prone and not ADS'd).

Hold Breath, Auto-Mantle-while-sprinting, and the "needs a fresh feature"
class of gap stay explicitly out of scope (parity audit items #23/#24,
unchanged by this fix). Extreme Conditioning (item #25) is resolved "for
free" as a direct consequence of the real kbutton now driving Sprint --
same as x86, no separate code needed.

**Build-verified**: x64 `/t:Rebuild` (0 errors) -> `dumpbin /headers`
confirmed `8664 machine (x64)` with a fresh timestamp; Win32 `/t:Rebuild`
(0 errors, 0 warnings) confirmed no regression; x64 rebuilt a THIRD time,
last, so the deployed DLL is the correct architecture (shared `OutDir`).
**NOT YET LIVE-TESTED** -- next step: a live playtest confirming Sprint
still engages/disengages correctly, the native duration/recovery timer
now applies (should no longer be unlimited), and the rising-edge stand-up
behavior fires correctly from both crouch and prone without regressing
ADS/Hold-Breath-adjacent behavior.

---

**Native D-pad+A/B controller menu navigation ported to x64, 2026-09-12 --
the single highest-impact gap the same-day full feature-parity audit found
(parity audit rows #29/#33): a controller player on x64 could not navigate
ANY native menu at all (main menu, pause menu, options screen, buy-station/
armory lists) and had to use keyboard/mouse for every menu interaction.**

**Read first, per this project's own compare-to-x86-original rule**: x86's
`InjectControllerMenuNav()`/`InjectControllerMenuBack()`
(`analog_input_hooks.cpp` ~2725-3047) -- both entirely wrapped in
`#if !defined(_M_X64) && !defined(_WIN64)` and never compiled for x64 at
all before this fix, let alone called.

**Confirmed real x64 target: `FUN_1402aac50` is the combined equivalent of
x86's `ForwardKeyToMenu` (`0x004d9850`) + the function its non-ESC branch
calls (`FUN_004dfd30`).** Its full decompile was ALREADY on disk from the
2026-09-12 menu-focus/itemDef port
(`re_notes/x64_migration/keyhandler_1402aac50_full.txt`) but had never been
connected to a signature or wired up -- this fix's main RE contribution was
recognizing what was already found, not discovering it from zero. Confirmed
via its own internal `switch(keyCode)`, which matches x86's `FUN_004dfd30`
switch case-for-case:
- `{9, 0x9b, 0x9d, 0xbd, 0xcd}` -> `FUN_1402ac5d0` (next-item) -- matches
  x86's Group A -> `FUN_006253d0`.
- `{0x9a, 0x9c, 0xb7, 0xce}` -> `FUN_1402ac6f0` (prev-item) -- matches
  x86's Group B -> `FUN_00625290`.
- `{0xd, 0xbf, 0xca}` -> select/activate -- matches x86's Enter case
  (`0xd`).
- `0x1b` -> ESC/back handling -- matches x86's ESC case.

Real call site (`FUN_14029baa0`, `re_notes/x64_migration/
keyhandler_callers_1402aac50.txt`, this project's own confirmed x64
key-event-resume path):
```c
plVar3 = (longlong *)FUN_1402aaa80(&DAT_142605050);   // = GetTopmostActiveMenuX64()
FUN_1402aac50(&DAT_142605050, plVar3, param_2 /*keyCode*/, param_3 /*isDown*/);
```
i.e. `ctx`/`menu` are exactly this file's own already-resolved
`g_uiMenuContextX64`/`GetTopmostActiveMenuX64()` (from the earlier
menu-focus/itemDef port) -- no new context-resolution work was needed, just
one additional signature.

**Signature derivation**: `DumpSigBytes.java` run against `1402aac50`
(`re_notes/x64_migration/impl_sig_1402aac50.txt`), hand-refined the same
way every other signature in this file was -- the RSP-relative stack-spill
prologue (`MOV qword ptr [RSP+0x18],RBX`) kept literal, per this file's own
established `DumpSigBytes.java` false-positive lesson (its reference-based
heuristic over-flags RSP-relative operands); every genuine RIP-relative
disp32 (`CMP`/`MOV`) and `CALL`/`JMP`/`Jcc` rel32/rel8 wildcarded.

**Implementation** (`analog_input_hooks_x64.cpp`):
- `ForwardKeyToMenuX64(keyCode, isDown)` -- resolves the topmost active menu
  fresh on every call (matching the real call site's own shape) and calls
  `g_menuKeyEventX64` (the resolved `FUN_1402aac50`).
- `InjectControllerMenuNavX64()` -- direct port of x86's
  `InjectControllerMenuNav()`. Two intentional differences from a literal
  line-for-line port:
  1. LB/RB tab-prev/tab-next needed NO new code -- `PollCustomOptionsMenuX64`
     (from the 2026-09-05 Custom Options screen work) already owns them for
     the custom overlay's own tab bar.
  2. x86 has ONE function that calls `CustomOptionsMenu_TickInput` itself
     and branches on its return ("claimed this tick" or not). x64 already
     has that call living in the separately-scheduled
     `PollCustomOptionsMenuX64`, so `InjectControllerMenuNavX64` instead
     reads `CustomOptionsMenu_IsOpen()` (a plain state read) and skips every
     `ForwardKeyToMenuX64`/synthetic-key call for the tick when it's true,
     while still updating every held-state edge tracker unconditionally
     (mirrors x86's own "claimed this tick" branch). This REQUIRES
     `PollCustomOptionsMenuX64()` to run before `InjectControllerMenuNavX64()`
     in the same tick, now wired that way in `InjectMenuInputTick`
     (`analog_input_hooks.cpp`).
  Y/X/Back-button synthetic sends (Friends/Game Summary/Leaderboards)
  reimplemented locally (`SendSyntheticFX64`/`GX64`/`F1X64`) rather than
  cross-file-exposing x86's versions, which sit in an anonymous namespace
  in `analog_input_hooks.cpp` with internal linkage only -- same local-
  reimplementation pattern this file already used for
  `SendSyntheticActionSlot4KeyX64`.
- `InjectControllerMenuBackX64()` -- direct port of x86's
  `InjectControllerMenuBack()`, forwarding real ESC (`0x1b`) to
  `ForwardKeyToMenuX64` on B's edge changes while a menu is active and the
  custom Options overlay isn't open.

**Two real conflicts found and fixed during the port** (neither in the
original task description -- found by diffing current x64 code against
x86's `InjectControllerDpad`/`InjectControllerButtons`):
1. **D-pad actionslot dispatch (`Hook_MovementTick`) had no menu-active
   gate at all on x64** -- unlike x86's `InjectControllerDpad`, which
   suppresses `ActionSlotDown/Up`/`SendSyntheticActionSlot4Key` while a
   menu is active, symmetrically on both press AND release edges. Without
   this fix, native D-pad menu-nav (new) and the raw actionslot dispatch
   (existing, gameplay tick -- which keeps running while a non-pause menu,
   e.g. a Survival buy station, is open) would double-fire on the same
   physical D-pad press. Fixed, matching x86's symmetric gate exactly.
2. **CrouchProne (B) dispatch (`Hook_MovementTick`) had no menu-active gate
   either.** B is dual-purpose on x64 too (crouch/prone vs. menu-back), and
   x86 solves the conflict via a shared `g_currentBPressTouchedMenu` bool.
   Added `g_currentBPressTouchedMenuX64`, x64's own equivalent, maintained
   by `InjectControllerMenuBackX64` and read by the CrouchProne dispatch
   before firing `g_stanceDispatch` -- without this, B backing out of a
   menu would ALSO toggle real native stance underneath it, a genuine
   stuck-crouch/prone regression risk (see CLAUDE.md's "Crouch 'needs an
   initial click at launch'" history).

**Constraint check, per direct coordinator instruction**: whether the
Custom Options screen's temporary LB+RB open-chord workaround (see this
issue's earlier "Custom Options screen wired into x64's input pipeline"
round) could now be simplified/removed. **Left in place, deliberately not
touched** -- the chord is a fallback for the real focus-based open trigger
(`onAnyRealOptionsButtonX64 && selectEdge`, already wired 2026-09-12,
independent RE pass), which depends on `TryGetRealFocusedGroupAndIndexX64`
(focus-tracking) and button-edge detection, NOT on `ForwardKeyToMenu` --
adding `ForwardKeyToMenuX64` doesn't make that trigger any more reliable,
so this fix provides no real basis to remove the chord. Per the prior
agent's own comment, it should stay until the real trigger is
independently live-confirmed.

**Build-verified**: x64 `/t:Rebuild` (0 errors) -> `dumpbin /headers`
confirmed `8664 machine (x64)` with a fresh timestamp; Win32 `/t:Rebuild`
(0 errors, `analog_input_hooks_x64.cpp` correctly excluded from that
build) confirmed no regression; x64 rebuilt a THIRD time, last, so the
deployed DLL is the correct architecture (shared `OutDir`).
**NOT YET LIVE-TESTED** -- next step: a live playtest confirming each of
main menu, pause menu, options drill-down, buy-station/armory lists, and
B-back actually work as expected, plus that the two conflict fixes above
(D-pad actionslot suppression, CrouchProne/B dual-purpose handling) don't
themselves regress ordinary gameplay D-pad/crouch-prone use outside any
menu context.

**Process note**: this fix pass ran concurrently, in the same shared
working directory, with the Sprint kbutton fix above -- both landed in
this file's `analog_input_hooks_x64.cpp` at the same time. Commits were
kept separated by hand (`git apply --cached` against a hand-extracted
single-hunk patch, rather than a blanket `git add`) so each agent's own
work stayed attributed to its own commit wherever the interleaving allowed
it to be cleanly separated -- worth noting as a real, reusable technique
for any future session that finds itself in the same shared-working-
directory situation this project's own concurrent-agent model can produce.

---

**Auto-Mantle (while sprinting) -- INVESTIGATED, BLOCKED, not implemented
this pass (2026-09-12).** Cross-reference: `re_notes/x64_feature_parity_audit.md`
row #24 ("Zero references to `AutoMantle`/`auto.?mantle` anywhere in the x64
file. Not gated off -- simply never implemented for x64"). This round traced
the real dependency chain rather than porting the feature on assumption, per
this task's own explicit instruction to investigate before committing to an
approach.

**x86's real detection chain, read in full first** (`analog_input_hooks.cpp`):
the condition gating `out |= 0x400u; // +gostand` (line ~1478) is
`g_modConfig.autoMantleEnabled && IsSprintActive() && IsMantleHintCurrentlyShowing() &&`
cooldown-elapsed. `IsMantleHintCurrentlyShowing()` (line 3763) is a pure
grace-window timestamp check against `g_mantleHintLastSeenMs`, which is only
ever advanced from `g_mantleHintDrawnThisFrame` (set at line 8178,
`if (isMantleHint) g_mantleHintDrawnThisFrame = true;`) -- and `isMantleHint`
itself (line 8163) is computed INSIDE `Hook_DrawGlyphText`'s own body, via a
structural template match (`RenderedTextMatchesSubstitutionTemplate(param_1,
"PLATFORM_MANTLE")`) against the literal text string the native engine is
handing to that hooked draw call THIS frame. In other words: Auto-Mantle's
entire ledge-availability signal is not a native engine flag this project
reads directly -- it is inferred by hooking the real native hint TEXT-DRAW
call and pattern-matching what string is being rendered. This is deliberate,
not incidental: issue #62's own history (see `CLAUDE.md`'s "Auto-mantle"
timeline entry and this file's cross-references) shows the design was
explicitly built and fixed around "the engine itself has ALREADY decided a
ledge is mantleable" being observable ONLY through what it chooses to draw,
not through a separately-read condition byte.

**Concrete finding: this exact dependency is confirmed still unported on
x64.** `Hook_DrawGlyphText`'s x64 equivalent does not exist -- already
documented earlier in this same issue (`known_issues_x64.md` issue #1,
"Scope note, honestly flagged" round, 2026-09-12): "Full gameplay
controller-glyph icon overlays... remain blocked on a SEPARATE,
not-yet-ported piece -- the native text-draw hook (x86's
`Hook_DrawGlyphText`) has no x64 equivalent yet." Independently
re-confirmed this round via direct grep of `analog_input_hooks_x64.cpp`:
zero references to `mantle`, `AutoMantle`, `DrawGlyphText`, or any
text-draw hook at all. `g_mantleHintDrawnThisFrame`/`g_mantleHintLastSeenMs`/
`IsMantleHintCurrentlyShowing()` have no x64 counterpart because there is no
x64 hook that could ever set them -- there is currently no code path on x64
that observes native hint text being drawn, mantle-related or otherwise.
**Auto-Mantle is therefore blocked on the same not-yet-attempted RE task as
the gameplay-hint glyph overlay generally (row #34 in
`x64_feature_parity_audit.md`), not a separate, smaller gap of its own.**

**Alternative path considered and deliberately rejected**: `re_notes/iw5sp.md`'s
"Mantle -- found, concretely" section (2026-07-xx SP research) separately
documents real native condition flags the ENGINE itself checks to decide
whether `+gostand` means "mantle" vs. "stand" (`DAT_00a760ec`/`DAT_00a7610c`/
`DAT_00a86390`/`DAT_00a86ae0`, all `+0xc`-offset checks). Reading these
directly on x64 (once re-signature-scanned) would technically produce SOME
ledge-availability signal without needing the text-draw hook at all. **Not
pursued, for three concrete reasons, not just caution**: (1) it would
directly read raw engine condition-flag memory rather than observe the
already-rendered native hint text -- a materially different, riskier signal
class than the one x86 deliberately chose, and closer to the class of
live-state read this project's own standing policy reserves away from the
main mod (see `CLAUDE.md`'s permanently-removed aim-assist reasoning and the
Plugin API's "even SP that poses a risk, possible deferrence to plugin"
precedent, issue #85/#89) -- worth a fresh, explicit discussion before use,
not a default fallback; (2) it would be a parallel native-state check that
duplicates but diverges from x86's actual shipped hint-detection logic --
exactly the workaround this task's own hard constraint #1 explicitly rules
out; (3) those four addresses were never independently re-derived for x64 in
this pass, so this would be fresh, unverified signature-scan work on top of
an already-rejected approach, not a shortcut.

**No code changes made to `analog_input_hooks_x64.cpp` for Auto-Mantle this
pass** -- shipping a stub or a diverging parallel check would violate this
task's own explicit honesty-over-completion instruction. `x64_feature_parity_audit.md`
row #24 stays accurate as written ("simply never implemented for x64");
this round adds the WHY (a real, traced blocking dependency, not an
oversight) and the concrete prerequisite (`Hook_DrawGlyphText`'s x64 port)
that would unblock it. **Status: Deferred**, pending that separate,
larger RE task -- not a small remaining step, and not to be force-shipped
via a diverging native-flag read without a fresh explicit decision.

**UPDATE 2026-09-13 -- the blocking prerequisite (`Hook_DrawGlyphText`'s x64
port) has now shipped, in two commits on `analog_input_hooks_x64.cpp`: a
passthrough milestone hooking `FUN_14029a2b0` (the x64 equivalent of x86's
`Hook_DrawGlyphText` target, `FUN_00690c80` -- see
`re_notes/x64_migration/drawtext_hook_x64.md` for the full discovery trail),
then Mantle-hint structural-match detection wired on top: `IsMantleHintCurrentlyShowingX64()`
and `g_mantleHintLastSeenMsX64` (`analog_input_hooks_x64.cpp`), the x64
equivalents of x86's `IsMantleHintCurrentlyShowing()`/`g_mantleHintDrawnThisFrame`/
`g_mantleHintLastSeenMs` (collapsed to a single timestamp write -- see that
code's own comment for why this is observably equivalent without needing a
separate per-frame commit step). Detection is a real, language-independent
structural match against the LIVE localized `PLATFORM_MANTLE` template
(resolved via `FUN_14029f120`, the real x64 `SEH_GetString` equivalent,
confirmed byte-for-byte behaviorally matching x86's own documented
`FUN_00532230` contract -- NOT `real_settings.cpp`'s x64 `GetLocalizedString()`
stub, which deliberately just echoes the reference key back and would never
match real rendered text), gated identically to x86's own
`ShouldDrawGlyphOverlay() && !IsMenuActive()` block (via new exported
wrappers) so this carries the same real coupling to the glyph-overlay
toggle x86 has, not a new behavior introduced by the port.

**This resolves Auto-Mantle's detection DEPENDENCY specifically -- it does
NOT implement the Auto-Mantle FEATURE itself.** The actual `+gostand`-forcing
injection (x86's `out |= 0x400u`, gated on `autoMantleEnabled &&
IsSprintActive() && IsMantleHintCurrentlyShowing() &&` cooldown) has not been
wired on x64. A genuine additional gap, not previously called out: x64 has
no direct `IsSprintActive()`-equivalent read to build that gate from at all --
Sprint was migrated to a real kbutton on x64 2026-09-12 (see
`sprint_weapnext_x64.md`), so there is no native `pm_flags` bit this project
reads to know whether sprint is currently active, only its own tracked
kbutton-activation state (`g_sprintKbuttonActiveX64`), which is not
necessarily identical (the native duration timer could deactivate sprint
without this project observing the transition). Wiring the actual feature is
therefore a further, separate task, not a trivial follow-on now that
detection exists. **Status stays Deferred** for the feature itself; the
detection dependency this entry originally tracked as the blocker is now
Resolved. Also NOT covered by this update: x64's real `Font_s` struct layout
(no font-name filtering gate applied to the Mantle match), and every visual
glyph-icon substitution case besides Mantle detection (no icon is drawn for
Mantle either -- this is detection-only, native hint text renders completely
unmodified). Build-verified (x64 `/t:Rebuild`, dumpbin-confirmed, Win32
regression clean, x64 redeployed last), not yet live-tested.

**UPDATE 2026-09-13 (later same day) -- the actual `+gostand`-forcing feature
is now wired. Status: Resolved (build-verified, live-test pending).**
The "no direct `IsSprintActive()`-equivalent read" gap flagged in the update
directly above turned out to be a smaller gap than it first looked: x86's
`IsSprintActive()` (`analog_input_hooks.cpp`, line ~1939) was re-read in full
and confirmed to be a plain logical state check --
`g_sprintHeld && GetRealStance() == 0 && !g_adsHeld` -- not a native memory
read requiring fresh RE. x64 already had all three equivalent pieces, each
existing for an unrelated prior reason:

- `g_sprintKbuttonActiveX64` (`Hook_SprintTick`, the 2026-09-12 Sprint
  kbutton-migration work) -- x64's own equivalent of x86's `g_sprintHeld`,
  already computed fresh every Sprint-hook tick as `sprintHeld && !adsHeldNow`.
- `GetRealStanceX64()` -- already used elsewhere this session (the same
  Sprint hook's own rising-edge stand-up call).
- `g_adsHeldX64` (`Hook_MovementTick`) -- already used for Hold Breath and
  gyro-only-while-ADS gating.

`IsSprintActiveX64()` composes these three exactly as x86 does:
`g_sprintKbuttonActiveX64 && GetRealStanceX64() == 0 && !g_adsHeldX64`. Same
honest caveat x86's own design already carries, not a new one introduced
here: this reads logical input intent, not the native sprint duration/
recovery timer's own internal state -- x86's `IsSprintActive()` has never
accounted for that either (see its own comment block), so this is an exact
parity port, not a regression relative to x86.

Wired into `Hook_MovementTick`'s existing Melee/Lethal/Tactical/Jump/Interact
raw-usercmd-bits block, right after the Jump section (same physical bit,
`kJumpUsercmdBit`/0x400, that section already ORs in): `g_modConfig.autoMantleEnabled
&& IsSprintActiveX64() && IsMantleHintCurrentlyShowingX64() &&` a 750ms
cooldown (`kAutoMantleCooldownMsX64`, matching x86's `kAutoMantleCooldownMs`
exactly), then the same forward-stick-cone check x86 uses
(`g_modConfig.autoMantleMinStickMagnitude`/`autoMantleForwardConeDegrees`,
`atan2f`/`fabsf`/`sqrtf` against `moveX`/`moveY`). No new native RE was
needed for the trigger itself either -- x86's Auto-Mantle already just OR's
the identical Jump bit once its own ledge-hint gate passes (the engine's own
`+gostand` dispatch decides mantle-vs-stand-vs-nothing internally), and
x64's `usercmd_t.buttons` field at `+0x04` was already confirmed identical
to x86's for Jump/Melee/Lethal/Tactical/Interact. `moveX`/`moveY` are reused
directly from `Hook_MovementTick`'s own earlier `RouteStickAxes_Exported()`
call in the same tick rather than a second stick read (x86's
`InjectControllerButtons` is a separate function from
`InjectControllerMovement` so it has to re-read the stick itself;
`Hook_MovementTick` is both). Same rate-limited `[automantle-diag]`-style
logging as x86 (tagged `[automantle-diag-x64]` here) for live verification.

Ships off by default, matching x86's exact default
(`g_modConfig.autoMantleEnabled = false`, `mod_config.h` -- unchanged by this
work). Build-verified: x64 `/t:Rebuild` 0 errors (dumpbin confirms
`8664 machine (x64)`, fresh timestamp), Win32 regression rebuild 0
errors/0 warnings, x64 rebuilt and redeployed last. **Not yet live-tested**
-- needs a real controller sprinting at an actual mantleable ledge, watching
for `[automantle-diag-x64]`/`[x64-drawtext] Mantle-hint structural match
confirmed` in `proxy_d3d9.log`, and confirming the same "jumps always when
trying to sprint" regression x86 hit in its own history (2026-08-03) does
not resurface here. Not covered by this update, same as the prior one: x64's
real `Font_s` struct layout and every visual glyph-icon substitution case
(no icon is drawn for Mantle, native hint text still renders unmodified).

**UPDATE 2026-09-13 (a separate, concurrent session, same day) -- real visual
glyph-icon SUBSTITUTION now ships for three hint families.** Unrelated to the
Auto-Mantle trigger work in the update directly above (that session worked in
`Hook_MovementTick`; this one worked in `Hook_DrawTextX64` only) -- see
`re_notes/x64_migration/drawtext_hook_x64.md`'s own "Stage (c)" for the full
RE trail. Summary:

- `RequestCustomHintOverlay` is now actually called from x64 for the first
  time, suppressing the native draw and drawing this project's own icon+text
  instead, for: **Mantle** (previously detection-only, now also visual),
  **Pickup/Swap/PickupHealth** (`PLATFORM_PICKUPNEWWEAPON`/`SWAPWEAPONS`/
  `PICKUPHEALTH`, all confirmed via fresh decompile to flow through the same
  `FUN_14029a2b0` draw call, inside `FUN_14004fa00`), and **Throwback grenade**
  (`PLATFORM_THROWBACKGRENADE`, same function). New function
  `TryGetPickupGlyphAssetName` (`analog_input_hooks.cpp`, resolves via
  `LogicalAction::ReloadUse`) added for the pickup family; Throwback reuses
  the existing `TryGetThrowbackGlyphAssetName` unchanged.
- Detection stays purely structural (exact prefix/suffix match against the
  real, live-resolved reference-key template) for all three -- no font-name
  filtering was needed.
- **Buy-station and Survival ready-up remain unported** -- genuinely blocked,
  not skipped: neither has a known reference-key template even on x86 (x86
  protects them via `IsGameplayHintFont` instead), and x64's own `Font_s`
  `fontName` offset is still unconfirmed (only `pixelHeight`@+0x08,
  `glyphCount`@+0x0C, and the `DiagGlyph*` array@+0x20 were confirmed this
  pass, via `FUN_1401b7cd0`/`FUN_1401b80f0`'s own direct dereferences;
  `fontName`@+0x00 is an alignment INFERENCE, not decompile-confirmed).
- **Reload is structurally unreachable from this hook** -- confirmed via
  decompile that x64's own Reload/low-ammo function (`FUN_140031bc0`) calls a
  completely different draw function, `FUN_1402afa60`, not `FUN_14029a2b0`.
  Would need its own separate hook.
- **Sentry-Place's own reference string** (`"SENTRY_PLACE"`) was searched for
  across the entire x64 binary and found ZERO times -- genuinely unresolved.
- No position/scale nudge tuning was ported (x86's own pixel-perfect alignment
  constants came from several rounds of live testing specific to x86's own
  math) -- on-screen alignment is unverified.

Build-verified: x64 `/t:Rebuild` 0 errors (dumpbin-confirmed `8664 machine
(x64)`, fresh timestamp), Win32 regression rebuild 0 errors/0 warnings, x64
rebuilt and redeployed last. **Not yet live-tested** -- needs a real
controller triggering each hint, watching for `[x64-drawtext] First real
glyph-icon SUBSTITUTION fired` in `proxy_d3d9.log` and visually confirming
the icon draws in a reasonable position (see the honest position-tuning
caveat above).

---

**Survival ready-up (hold Y) -- PORTED, build-verified, not yet live-tested
(2026-09-12).** Cross-reference: `re_notes/x64_feature_parity_audit.md`'s
own "zero wiring in x64's input pipeline" finding for this control.
Direct port of x86's own `SendSyntheticF5`/`InjectControllerWeaponNext`
(`analog_input_hooks.cpp`), re-read in full first per this project's own
standing rule.

**Not a native-kbutton case** -- same explicitly-authorized, narrowly-scoped
exception to the "no OS-level input emulation" rule x86 already ships
(user-approved 2026-07-15, see `CLAUDE.md`'s "Survival ready-up (hold Y)"
section). x86's own real trigger for F5/"skip" was never found despite an
exhaustive search across multiple techniques (real `+gostand` kbutton: wrong
system; `togglecrouch`/`FUN_0057d2c0` mode variants: inert, or a genuine
unrelated prone-toggle that got a player stuck prone live; GSC
`notifyonplayercommand`/`VM_Notify`: real primitives but need live GSC-VM
stack manipulation from an async hook, too risky). That search is NOT
re-run here per this task's own explicit scope -- the game data/GSC scripts
are unchanged by the x64 recompile, so the same "no native call" conclusion
is assumed to carry over.

**Prerequisites confirmed already in place before writing any new code**:
`GetGameWindow()` (`d3d9_hook.cpp`) is a plain `extern "C"` function, not
architecture-guarded there at all, and already in active x64 use --
`SendSyntheticActionSlot4KeyX64` (D-pad Left's squadmate-call-in fix, same
file) already calls it for an identical `PostMessageA`-based synthetic-key
technique, used directly as this port's template. `g_weaponNext`
(`FUN_1400706d0`, resolved via `kWeaponNextSignature`) was already confirmed
working live from this session's earlier work, so the "release before
threshold fires weapon-switch instead" fallback reuses it directly with no
new signature-scan needed.

**Implementation**: `SendSyntheticF5X64()` added (same file, right after
`SendSyntheticActionSlot4KeyX64`, matching its structure exactly) --
`PostMessageA(hwnd, WM_KEYDOWN, VK_F5, 1)` then
`PostMessageA(hwnd, WM_KEYUP, VK_F5, 0xC0000001)`, same lParam values as
x86's own `SendSyntheticF5`. The existing Weapnext dispatch block (inside
`Hook_MovementTick`, where Fire/ADS/Reload/Weapnext already live) was
extended with the same hold-vs-tap state machine x86's
`InjectControllerWeaponNext` uses: `g_yPressStartMsX64`/
`g_yReadyUpFiredX64` track press-start time and a per-hold debounce; a hold
past `g_modConfig.readyUpHoldThresholdMs` (740ms default, `[Survival]
ReadyUpHoldThresholdMs`, already architecture-neutral config -- no new
config plumbing needed) fires `SendSyntheticF5X64()` once; a release before
the threshold fires `g_weaponNext(0, 1)` instead (the pre-existing
weapon-switch call), exactly mirroring x86's own deferred-to-release design
(firing weapnext unconditionally on the press edge would also switch
weapons on every ready-up hold attempt, since Survival's between-wave break
is live gameplay with usable weapons, not a frozen wait).

**One honest, deliberate difference from x86**: x86 additionally gates the
synthetic F5 behind `IsInSurvivalMode()`, a `mapname` dvar read via x86's
raw `Dvar_FindVar`-equivalent (`FUN_0062abe0` @ `0x0062abe0`). x64's own
equivalent of that raw dvar-lookup function is a genuinely unresolved RE
target, already documented earlier in this same issue
(`GetLookAccelerationScaleX64`'s own comment: "the hardcoded
GetEffectiveFov/Dvar_FindVar addresses... genuinely unresolved RE targets,
not yet found") -- `real_settings.cpp`'s `FindDvar()`/`GetDvarString()` are
x86-only (their `__asm` body is `#ifdef _M_IX86`-guarded, a safe no-op
returning `nullptr` on x64, not a crash, but not a real lookup either).
Rather than block this port on a separate RE task outside its own scope
(this task's own hard constraint #1 explicitly said not to go hunting for a
"real" native trigger -- extended here to also not chase down an unrelated
dvar-lookup prerequisite mid-port), `SendSyntheticF5X64()` fires
unconditionally on the hold-threshold edge. This relies on the SAME "safe
by construction" reasoning x86's own design comment already documents as
sufficient even without the mode gate: IW5 has no DirectInput import at all
(this project's own founding finding), so keyboard input is real
`WM_KEYDOWN`/`WM_KEYUP` messages, and a misplaced synthetic F5 outside
Survival's ready-up wait is simply ignored by the game, the same as a real,
misplaced press would be. Worth revisiting if x64's `Dvar_FindVar`
equivalent is ever resolved for other work, but not a blocker for this
control specifically.

**Build verification**: x64 `-t:Rebuild` (0 errors, 10 pre-existing
`C4312` warnings in x86-only code compiled into the x64 TU, unrelated to
this change), `dumpbin -headers` confirmed `8664 machine (x64)` with a
fresh timestamp. Win32 `-t:Rebuild` (0 errors, 0 warnings) confirmed no
regression. x64 rebuilt a THIRD time, last, so the deployed DLL (shared
`OutDir`) is the correct architecture.

**NOT YET LIVE-TESTED** -- next step: a live Survival playtest confirming
(1) a ~740ms Y hold between waves actually readies up (same as `-x86`'s
confirmed-live behavior), (2) a quick tap or a hold that falls short of the
threshold still switches weapons, and (3) the missing `IsInSurvivalMode()`
gate doesn't cause any observable side effect outside Survival (expected:
none, per the "safe by construction" reasoning above, but unconfirmed
against real hardware on this binary specifically).

**Status: Build-verified, not yet live-tested.**

---

**Hold Breath (L3 while ADS'd) ported to x64 (2026-09-12) -- closes parity
audit item #23 (row #23, previously ABSENT: "Zero references anywhere in
`analog_input_hooks_x64.cpp`").**

Read x86's own implementation in full first (`analog_input_hooks.cpp`'s
"Hold Breath (L3 while ADS'd)" section) per this project's own standing
"compare to x86 original at every stage" rule. Real mechanism confirmed:
x86 gates purely on `g_sprintHeld && g_adsHeld` -- no explicit sniper-class
check in ITS OWN code either; the real native kbutton is what limits the
sway-reduction/accuracy effect to sniper-class weapons, not anything this
project's own injection code does. x64 mirrors this exactly: no weapon-class
logic added, gating is `sprintHeld && adsHeldNow` inside `Hook_SprintTick`.

**Struct resolved via the SAME anchor+offset technique already proven for
Fire/Reload/ADS/Sprint, confirmed via TWO independent angles (this
project's own issue #3 standard):**

1. Decompiled `FUN_14007c3a0` case 9 ("+breath_sprint" down, the real
   default SHIFT bind -- `re_notes/x64_migration/decomp_14007c3a0_full.txt`)
   fires `FUN_14007e460` on `&DAT_14064482c + lVar4*0x230` FIRST, then on
   `&DAT_1406448f4 + lVar4*0x230` (= `g_sprintStruct`, already resolved by
   the earlier Sprint port) SECOND -- the real default Sprint/Hold-Breath
   key already drives both structs back-to-back today, mirroring x86's own
   original discovery of this exact same two-call shape on that binary.
2. Independently re-derived the raw bytes at both real
   `LEA reg,[rip+disp32]` instructions this decompile reference resolves to
   (`0x14007cc69` and `0x14007cc93`, dumped fresh via `DumpRawBytes.java`
   against the live `iw5sp_x64_proj` Ghidra project --
   `re_notes/x64_migration/rawbytes_holdbreath_struct.txt` -- not read off
   the decompile alone): both are genuine 7-byte `48 8D 05 <disp32>` LEA
   instructions whose RIP-relative target computes to `0x14064482C`
   bit-for-bit, matching the decompile's `DAT_14064482c` name exactly.

A third, structural cross-check: `0x14064482c` is exactly `0x14` bytes past
`g_fireStruct`'s own target (`DAT_140644818`) -- the very NEXT `kbutton_t`
in the contiguous per-player array (this engine's x64 `kbutton_t` is 0x14/20
bytes: down0/down1/timestamp/downtime as int32 + a 1-byte active flag, per
`FUN_14007e460`/`e490`'s own decompile), NOT an internal field of Fire's own
struct the way x86's `0xA98C04` alias is (x86's own `kbutton_t` stride is
smaller, so ITS Hold Breath address lands INSIDE Fire's struct at its
down[1] field -- a real x86-specific aliasing quirk, `known_issues.md` issue
#6). x64's Hold Breath struct is a genuinely separate, dedicated `kbutton_t`
-- there is no structural reason to expect x86's own "active flag never
self-clears on this alias" bug (issue #24, the reason x86 needed a debounce
+ force-clear workaround) to recur here, and x64's own Sprint migration
(same session, immediately above) already proved this exact call pattern
(`g_kbuttonActivate`/`g_kbuttonDeactivate`, no debounce, no active-flag
force-clear) works cleanly with no such workaround needed -- so none was
added for Hold Breath either. **Flagged for live confirmation, not assumed
risk-free from static analysis alone.**

Also referenced by decompile lines 201-206: a SEPARATE case, 0x31/0x32 --
a standalone (likely unbound-by-default) `+breath_hold`-class bind that
drives this exact same struct alone, with no paired Sprint call --
independent corroboration this is a real, dedicated Hold Breath kbutton,
not an incidental byproduct of case 9's own dual-call shape.

**A real bug was caught and fixed while writing this, not shipped**:
`Hook_SprintTick`'s own pre-existing early `return` (`if (active ==
g_sprintKbuttonActiveX64) return;`) would have silently skipped Hold
Breath's own edge check on every tick where Sprint's state happened to be
steady -- i.e. most ticks while ADS-holding-still, exactly the case that
matters most for this feature. Restructured Sprint and Hold Breath into two
independent per-tick state machines in the same function (matching x86's
own `InjectControllerSprint` shape, which already handles both this way)
instead of a shared early exit.

**Implementation**: `Hook_SprintTick` (`analog_input_hooks_x64.cpp`) now
also computes `holdBreathActive = sprintHeld && adsHeldNow` and edge-
triggers `g_kbuttonActivate`/`g_kbuttonDeactivate` on the newly-resolved
`g_holdBreathStruct` (`kHoldBreathStructInsnOffset = 0x8C9` from the
`FUN_14007c3a0` anchor), using its own `kHoldBreathSyntheticSourceId`
(`0x1000`, same value/rationale as every other bind's synthetic source id --
distinct struct pointers mean no cross-bind collision risk regardless of
source-id reuse) and its own `g_holdBreathKbuttonActiveX64` edge-tracking
bool, kept separate from `g_sprintKbuttonActiveX64`.

**Build-verified**: x64 `/t:Rebuild` (0 errors, same pre-existing C4312
warnings as every other round, unrelated to this change), `dumpbin
/headers` confirmed `8664 machine (x64)` with a fresh timestamp. Win32
`/t:Rebuild` (0 errors) confirmed no regression -- `analog_input_hooks_x64.cpp`
correctly does not compile into the Win32 build at all. x64 rebuilt a THIRD
time, last, so the deployed DLL (shared `OutDir`) is the correct
architecture.

**NOT YET LIVE-TESTED** -- next step: a live playtest with a sniper-class
weapon, ADS'd, confirming (1) holding the Sprint bind while ADS'd produces
the real sway-reduction/steadier-aim effect and accuracy degrades once
breath runs out (same as `-x86`'s confirmed-live behavior), (2) the kbutton
correctly releases on letting go of the bind or breaking ADS (watch
specifically for any sign of x86's own "active flag latches, never clears"
symptom recurring here despite the structural reasoning above that it
shouldn't), and (3) ordinary hip-fire Sprint (not ADS'd) is unaffected.

---

**Back's `+scores` scoreboard key-synthesis ported to x64 (2026-09-13) --
closes parity audit row 30, a small, cheap, well-understood wiring port, NOT
new RE work.**

Read x86's own `InjectControllerScoreboard()` in full first
(`analog_input_hooks.cpp`) per this project's own standing "compare to x86
original at every stage" rule. Confirmed already by this session's own audit
(`re_notes/x64_feature_parity_audit.md` row 30): x86's own function has NO
architecture guard and would compile fine on x64 as-is (pure synthetic-key
logic via `GetGameWindow()`/`PostMessageA`, no hardcoded x86 addresses) -- it
was simply never CALLED from anywhere in `analog_input_hooks_x64.cpp`. No new
signature scanning or decompiling was needed for this port.

**IMPORTANT FRAMING, confirmed by direct Xbox 360 console testimony
(2026-08-04, `known_issues.md` issue #28): this is a genuine no-op in
Campaign/Survival on every platform -- there is no scoreboard UI in SP at
all.** This is NOT an undiagnosed bug. The point of this port is
completeness/consistency (the function already existed and cost nothing to
wire in) and real future value once Multiplayer ships with its own actual
scoreboard -- NOT to make a visible feature appear in SP. If the port is
correct, pressing Back in Campaign/Survival continues to do nothing visible,
exactly matching confirmed real console behavior -- that is success, not
failure.

**Implementation**: `SendSyntheticScoreboardKeyX64(bool down)`
(`analog_input_hooks_x64.cpp`) -- same `PostMessageA(hwnd, WM_KEYDOWN/WM_KEYUP,
VK_TAB, ...)` mechanism as x86's `InjectControllerScoreboard()`, re-expressed
as a bool-down/up function matching this file's own
`SendSyntheticActionSlot4KeyX64` shape. Wired into `Hook_MovementTick`'s
existing edge-tracking block (same `if (Controller_GetRawButtonsAndTriggers(...))`
scope as Fire/ADS/Reload/D-pad/CrouchProne), gated on `g_buttonMap.scoreboard`
via `IsPhysicalHeld_Exported`, tracked by a new `g_scoreboardHeldX64` bool.
Hold-through-passthrough, not tap/toggle -- Back down -> TAB down, Back up ->
TAB up, mirrors x86 exactly. No menu-active gate needed (x86's own function
has none either, and Back has no other current meaning on x64 to conflict
with).

**Build-verified**: x64 `/t:Rebuild` (0 errors, same pre-existing C4312
warnings as every other round, unrelated to this change), `dumpbin /headers`
confirmed `8664 machine (x64)` with a fresh timestamp. Win32 `/t:Rebuild`
(0 errors) confirmed no regression. x64 rebuilt a THIRD time, last, so the
deployed DLL (shared `OutDir`) is the correct architecture.

**NOT YET LIVE-TESTED** -- next step, per `re_notes/x64_live_testing_checklist.md`:
confirm Back produces NO visible change in Campaign/Survival (the expected,
correct outcome, not "confirm it works"). Real value only confirmable once
Multiplayer ships.

**Status: Build-verified, not yet live-tested.**

**Status: Build-verified, not yet live-tested.**
