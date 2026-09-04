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

- [#1](#1-critical-mw3-2011-recompiled-to-x64----mod-completely-broken-every-hardcoded-address-invalidated) — CRITICAL: MW3 (2011) recompiled to x64 — mod completely broken — **Every control implemented; two open bugs found live (sniper Fire/ADS, D-pad "diff keys"), investigation paused for a docs/ETA pass — release ETA 2-4 weeks, gated on x86 parity**

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
  sometimes diff keys used."** Partially explained by ALREADY-KNOWN,
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
