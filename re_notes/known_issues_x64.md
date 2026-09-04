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

- [#1](#1-critical-mw3-2011-recompiled-to-x64----mod-completely-broken-every-hardcoded-address-invalidated) — CRITICAL: MW3 (2011) recompiled to x64 — mod completely broken — **Foundation live-confirmed; Sprint+Movement+Look hooks CONFIRMED WORKING LIVE**

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
Next: the remaining gameplay hooks (buttons/ADS, pause toggle, weapnext)
one at a time on the same foundation.
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
