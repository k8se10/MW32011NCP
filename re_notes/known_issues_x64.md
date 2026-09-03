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

---

## Index

- [#1](#1-critical-mw3-2011-recompiled-to-x64----mod-completely-broken-every-hardcoded-address-invalidated) — CRITICAL: MW3 (2011) recompiled to x64 — mod completely broken — **Investigating/RE underway**

---

## 1. CRITICAL: MW3 (2011) recompiled to x64 -- mod completely broken, every hardcoded address invalidated

*(Carried forward from `known_issues.md`'s former issue #111, opened 2026-09-03. Original numbering/history preserved in that file's own trimmed stub entry.)*

**Status: Investigating/RE underway. The mod does not currently work at all.**
**Emergency policy action, same day: all support for the entire existing
`-x86` release line (every version through `v0.3.5-x86`) is discontinued,
effective immediately** — not a gradual wind-down, since the live game can
no longer run a 32-bit build at all. This includes withdrawing `v0.2.2-x86`'s
Current LTS status and `v0.3.5-x86`'s LTS candidacy outright, outside this
project's normal LTS promotion/demotion process — see `LTS_POLICY.md`'s own
emergency-escalation note for the full policy record. Existing `-x86` files
stay published as historical artifacts; nothing is deleted, just unsupported
going forward. Releases now carry a `-x86`/`-x64` suffix, versioning reset to
`v0.0.1-x64` for the new architecture line once it ships.
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

**Not yet started**: no x64 hook code exists yet. Real next steps: design the
actual signature-scanning mechanism (AOB/IDA-style byte-pattern-plus-
wildcards, validated before hooking, fail loudly on a bad match); write real
per-hook x64 hook-install code to replace the ~14 `__asm` trampolines, now
that several real hook targets are confirmed (Sprint's `FUN_140014a80`, the
buttons/ADS/menu unified entry point `FUN_14007eaf0`, the pause toggle
`FUN_1400823b0`, weapnext's `FUN_1400706d0`); the shadow-map creation call
site remains the one major unresolved thread (needs indirect-call scanning
or live tracing, not more direct-reference scanning), and `cls.state`'s
exact semantics stay open but low-priority now that pause is resolved via a
separate path. See
`re_notes/x64_migration/README.md` for the complete import-table/
section-table diff, the full string-persistence data table, and every
sub-cluster's own raw Ghidra output files.
