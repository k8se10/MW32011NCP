# x64 migration — MW3 (2011) recompiled to 64-bit, 2026-09-03

## Status: Implementing. A fresh Ghidra project against the new x64 `iw5sp.exe`
exists and is fully analyzed (`re_notes/ghidra_project_x64/iw5sp_x64_proj.gpr`,
kept completely separate from the original x86 project). A real x64 build now
compiles, LINKS, and deploys cleanly to the live game install (first time in
this project's history) with one working diagnostic hook — see
`known_issues_x64.md` issue #1's "Implementation begins" section for the full
record. No real gameplay hooks exist yet; not live-tested. This is the single
biggest event in this project's history — read this whole file before
touching anything x64-related.

**Issue tracker note (2026-09-03):** the curated, `known_issues.md`-style
record for this whole effort now lives in
[`re_notes/known_issues_x64.md`](../known_issues_x64.md) issue #1 — a
dedicated tracker for the x64 architecture line, split off the same day it
was opened once it had outgrown a single entry in the main (x86-era)
`known_issues.md`. This file stays the primary RE scoping/progress document
(sub-cluster passes, raw Ghidra output, the standing signature-scanning
caution); `known_issues_x64.md` is the shorter, curated summary a reader
would check first.

## Sub-cluster RE passes (2026-09-03) — separate files, cross-linked here

Three parallel passes covering distinct hook clusters, each in its own file to
avoid concurrent-edit conflicts with this shared README. Read each for the
real evidence and honest confidence level; this is just an index.

| Cluster | File | Real confidence |
|---|---|---|
| Sprint / weapon switch | [`sprint_weapnext_x64.md`](sprint_weapnext_x64.md) | **Both RESOLVED (static, high confidence).** Sprint: full Pmove-entry call chain traced (`FUN_140016620` frame-subdivision wrapper → `FUN_1400168a0` per-substep tick → `FUN_140014a80`, the real sprint-bit writer) — the real x64 equivalent of x86's `InjectControllerSprintPmFlags` hook target. weapnext: `FUN_14007c3a0` case `0x42` (`FUN_1400706d0` → `FUN_140074570`, a real weapon-slot-cycling function with a direction parameter) confirmed as the real dispatcher, once `FUN_14007c3a0`'s case numbers were confirmed to be direct bind-name-table indices — corrects an earlier same-day dismissal of this exact case as coincidence. |
| Pause menu / key handler | [`pausemenu_keyhandler_x64.md`](pausemenu_keyhandler_x64.md) | `cl_paused`'s real storage global found. **Updated after this file's own section 1g**: the real key-event handler (`FUN_1402aac50`, x86 `FUN_00541020` equivalent) and its wrapper (`FUN_14029baa0`) ARE now found, high confidence — cross-validates `pausemenu_keyhandler_x64.md`'s own `FUN_14029dfd0` candidate as a real part of the chain. **Same-day follow-up**: `FUN_14029baa0` fully decompiled — confirmed x64 `Cvar_Set` equivalent (`FUN_1402c5b30`) and the real resume-gameplay path (`Cvar_Set("cl_paused", 0)` once the menu stack empties). `FUN_1402ac9c0` ruled OUT as `OpenPauseMenu` (it's a bulk close/refresh pass over already-registered menu defs, gated on a menu already being open). **Second same-day follow-up: `SetMenuState`/`OpenPauseMenu` CONFIRMED.** `FUN_14029f3f0(player, mode)` is the real `SetMenuState`, a 10-destination named-screen dispatcher; mode `2` opens `"pausedmenu"` via the newly-found `FUN_1402ad950(ctx, name)` (`OpenMenuByName`) — that's the real `OpenPauseMenu`. **Third same-day follow-up, resolved in practice**: found the real live-gameplay pause TOGGLE, `FUN_1400823b0` (case `0x43` in `FUN_14007c3a0`, the confirmed x64 `FUN_00438710` equivalent) — reads the current `SetMenuState` mode and toggles between open (mode 2) and resume (mode 0). This is structurally independent of the ESC-specific `cls.state==6` branch, which stays an open, lower-priority mystery (real write sites for states 1/4/6/7 found, semantic mapping still uncertain) since Start's pause almost certainly doesn't need it. |
| D-pad actionslot / generic dvar API | [`actionslot_dvarhelpers_x64.md`](actionslot_dvarhelpers_x64.md) | Dvar API (`Dvar_FindVar`/get/set): high confidence, cross-validated 4 independent call sites — real simplification over x86, standard calling convention, no `__asm` needed; value offset shifted `+0xc`→`+0x10` (real x64 struct-alignment change, confirmed two ways). **D-pad actionslot, upgraded same day**: mapped the ENTIRE kbutton-table function cluster (4 functions total: setter, `IsKeyButtonDown` by index/by name, `ClearAllKeyButtons`) — no separate raw-dispatch table exists anywhere, confirming actionslot is "just another kbutton" at the table level (medium-high confidence). The actual "use the equipped item" consumer is still not found, plausibly GSC-VM script state rather than native C++. |

Section 1 below (this file) covers the movement/look pipeline, buttons/ADS,
and the visual-suite dvar catalog — all found directly in this session, not
delegated to a sub-pass.

## Standing caution — this was a huge update, don't assume ANYTHING carried over unverified

Direct instruction (2026-09-03): "some menu entries may now also be broken and
we have to reverify old assumption as it was a huge update." The ~14GB size of
this update means it's not safe to assume only the executable's code addresses
moved — menu/UI structure, `.menu` layout data, dvar defaults, GSC script
content, and asset paths could all have changed too, not just where a given
function lives in memory. **The string-persistence check above (same dvar
names present in both binaries) is evidence the underlying ENGINE/DATA is
still recognizable, not proof any specific menu group name, glyph position,
depth value, or piece of UI behavior this project has ever calibrated still
holds.** Every existing `kManualGlyphPositions`/`kVerifiedGlyphGroups` entry,
every menu-navigation assumption, every calibrated position — all of it needs
live re-verification against the new build once hooks exist again, the same
"having a table entry isn't evidence" standard this project already applies to
glyph work, now extended to everything, not just glyphs. Do not silently carry
forward an old assumption into new x64 code just because the old address/logic
is now confirmed to still exist — confirm the CURRENT behavior, not just the
current address.

## What happened

Sometime between 2026-08-29 (the last confirmed-working `d3d9.dll` session) and
2026-09-03, Steam pushed a real update to MW3 (2011) — `iw5sp.exe` and
`iw5mp.exe` both got new file timestamps (2026-09-03 19:31) in the live install.
This is **not** the well-documented "updating executable" DRM-rewrap quirk that
affects several old IW-engine CoD titles (that theory was checked first and
ruled out) — it's a genuine binary change.

**Confirmed directly from the PE header, two independent ways** (not hearsay,
not inferred): both executables are now `PE32+` (`IMAGE_FILE_MACHINE_AMD64`,
raw Machine field `0x8664` at the standard offset), 7 sections. The install's
own `appmanifest_42680.acf` shows no `BetaKey` set — this is the **default
branch**, not an opt-in beta. No legacy/32-bit opt-out branch found (checked via
web search and the local manifest; a definitive check would need Steam's own
Properties → Betas UI, not available from this environment).

## Why this breaks everything

This project's proxy `d3d9.dll` is a hard Win32 (x86) build — see the main
`CLAUDE.md`'s own Stack section, confirmed 2026-07-13 and never revisited since
until now. **A 32-bit DLL cannot be loaded into a 64-bit process under any
circumstance** — the OS loader rejects the architecture mismatch before
mapping the file at all. This isn't "some hardcoded addresses moved" (the
normal risk this project already knew hardcoded addresses carried across game
updates) — the injection technique itself stopped applying in one step. Every
hardcoded address this project has ever found (100+, accumulated since
2026-07-13) is against the old x86 binary and does not carry over.

**The mod has not actually run since the update** — `proxy_d3d9.log`'s own
last-write timestamp is 2026-08-29 06:08, before the update; there is exactly
one session boundary in the whole file and it's the pre-update session. Nothing
in that log reflects post-update behavior.

## The good news: what actually survived

- **The existing Ghidra project (`re_notes/ghidra_project/iw5sp_proj.gpr` +
  `.rep`, 167MB, already tracked in this repo) keeps its own internal copy of
  whatever it imported.** Every decompile, every confirmed function, the full
  ~2-month RE trail is completely intact regardless of what happened to the
  actual exe file on disk. Do not delete or re-import over this project — it's
  the reference baseline for diffing against the new binary, not a stale
  artifact to discard.
- **A backup of the ORIGINAL 2026-07-13 x86 binaries survived** (user had them
  zipped: `Call of Duty Modern Warfare 3.zip`, internal file dates confirm
  2026-07-13 21:43/21:47 — this project's actual day-one binaries) and is now
  preserved at `re_notes/x64_migration/binaries/old_x86/`. The live-update
  x64 binaries are preserved alongside at `re_notes/x64_migration/binaries/`
  (root of that folder) so neither copy depends on Steam not touching the
  install again.
- **`d3d9.dll` is still the graphics API.** Confirmed via `dumpbin /imports` on
  the new `iw5sp.exe`. The entire proxy-DLL injection technique this project
  is built on is still structurally valid — it needs an x64 build, not a
  different technique.
- **Still no `xinput*.dll`/`dinput8.dll` import anywhere in the new binary.**
  The update did not natively add controller support. The core premise this
  project was founded on (2026-07-13's own "Key technical finding") still
  holds exactly as it did on day one.
- **This looks like a genuine recompile of the same underlying engine/data,
  not a rewrite** — a same-name-string presence check (dvar names, function-
  adjacent identifiers) came back identical in both binaries for every real
  hit:

  | String | Old (x86) | New (x64) |
  |---|---|---|
  | `sm_fastSunShadow` | present | present |
  | `missileHellfireUpAccel` | present | present |
  | `com_maxfps` | present | present |
  | `monkeytoy` | present | present |
  | `cl_paused` | present | present |
  | `player_sprintUnlimited` | present | present |
  | `objectiveFont` | present | present |
  | `ai_disableSpawn` | present | present |
  | `r_texFilterAnisoMax` | present | present |
  | `coopready` | present | present |

  (A few checked strings — `SWF_COMMON_DESC_RESIZE_POPUP_NAME`,
  `usebuttonpressed`, `notifyonplayercommand`, `MENU_SP_DESC_SPECIALOPS` — came
  back absent from BOTH binaries; those live in compiled `.menu`/GSC assets
  outside the exe, not a sign of anything missing.) This consistency is the
  real evidence behind the signature-scanning decision below — if the same
  data/logic really did carry over, the same underlying byte-pattern shapes
  for each function very likely did too, even though absolute addresses and
  the calling convention (x64 has one unified convention, not x86's
  stdcall/cdecl/thiscall mix) did not.

## Real structural changes worth knowing before diving in

Import table diff (`iw5sp.exe`, via `dumpbin /imports`):
- **Gone**: `mss32.dll` (Miles Sound System), `binkw32.dll`, `NETAPI32.dll`,
  `DSOUND.dll` (implied by X3DAudio replacing it, not directly confirmed absent
  — recheck if audio hooking is ever needed).
- **New**: `steam_api64.dll` (renamed, expected), `X3DAudio1_7.dll` (modern
  XAudio2-family audio, replacing the old DirectSound/Miles path),
  `bink2w64.dll` (Bink2, replacing Bink1's `binkw32.dll`), `bcrypt.dll`,
  `WINHTTP.dll`. `ole32.dll` was already present in the old binary too (not
  new, corrected from an initial mis-read).

This is a real modernization pass against a current SDK/toolchain, not a
mechanical bit-width flip — expect some real behavioral differences beyond
pure address shifts, not just "the same code at new addresses." Verify each
hook target's actual current behavior, don't just relocate the old address's
believed meaning.

Section table diff (`iw5sp.exe`):
- Old (PE32/x86): `.text`, `.rdata`, `.data`, `.tls`, `.version`, `.rsrc`
- New (PE32+/x64): `.text`, `.rdata`, `.data`, `.pdata`, `_RDATA`, `.rsrc`, `.bind`

Notably **no `.tls` section in the new binary** — the x64dbg workflow note in
the main `CLAUDE.md` about clearing "stale TLS-callback breakpoints
(auto-loaded per-binary)" on every fresh attach may no longer apply; re-verify
against the new binary rather than assuming the old gotcha still exists.
`.pdata` (x64 SEH/unwind tables) and `.bind`/`_RDATA` are normal, expected x64
PE artifacts, not anything specific to this game.

## Locked decisions arising from this (2026-09-03, see CLAUDE.md for the full
record — this section is a pointer, not the source of truth)

1. **Project redefined: "native controller project" → "native enhancement
   project."** Direct instruction: "we have to redefine here. this is the key
   point we change from native controller project to native enhancement
   project." See `CLAUDE.md`'s Project Overview for the updated framing.
2. **Signature scanning is now the approach, reversing the 2026-08-25
   "hardcoded addresses are the permanent policy" decision.** Direct
   instruction: "also now is the time for the proper signature scanning
   approach." See `CLAUDE.md` §5/§10.3 for the full, updated policy record —
   including the honest note that this doesn't resolve the original VAC-risk
   concern the hardcode-only policy was built around, it supersedes it given
   real-world necessity (a game update just invalidated every hardcoded
   address in one step, exactly the failure mode static-only addressing can't
   survive).

## Next real steps

1. ~~Stand up a fresh, separate Ghidra project against the new x64
   `iw5sp.exe`~~ **DONE 2026-09-03**: `re_notes/ghidra_project_x64/iw5sp_x64_proj.gpr`,
   imported from the preserved copy at
   `re_notes/x64_migration/binaries/iw5sp.exe` (not the live Steam install —
   stable, won't move under us), full auto-analysis run clean (93s, no
   errors). Kept fully separate from the existing x86 project — the original
   is untouched.
1a. **Proof-of-concept validated 2026-09-03: the exact same string-scan →
    xref → decompile methodology this project has always used works cleanly
    against the new x64 binary, no changes needed.** `RawStringScan.java`
    found `cl_paused` at `1403ee240` (.rdata) with 16 real code references
    across 11 functions — same shape as the x86 project's own established
    workflow. Decompiled three candidates cleanly: `FUN_14004a870` is the x64
    equivalent of the giant dvar-registration batch function (contains the
    literal `FUN_1402c46d0("cl_paused",0,0,2,...,"Pause the game")`
    registration call, fully readable); `FUN_1402a1020`/`FUN_1402a0f50` are a
    matched pair of dispatch functions that check `cl_paused` via a dvar-get
    helper (`FUN_1402c3b10`) before walking a function-pointer table by name
    — plausible alias/command-dispatch candidates, not yet confirmed against
    a known x86 counterpart. Ghidra's decompiler output is clean and fully
    readable for this binary; nothing about the x64 recompile makes it harder
    to read than the original.
1b. **Movement/look pipeline re-located, high-confidence static match, NOT yet
    live-verified (no x64 hook code exists to test with).** Replicated the
    exact original discovery method from `re_notes/iw5sp.md`'s "Input/usercmd
    pipeline" section: `FindInputRefs.java` against the same cvar-name string
    set (`cl_yawspeed`, `m_pitch`, `m_forward`, `m_side`, `sensitivity`,
    etc.) found `FUN_140080db0` hitting all 10 of the exact same strings
    `FUN_004292f0` (the x86 `CL_InitInput`-equivalent) did. Decompiling it
    gave the new cvar storage-global table (`FindGlobalRefs.java` on those
    globals, mirroring the original technique):

    | cvar | x86 global | x64 global |
    |---|---|---|
    | `cl_yawspeed` | `DAT_00a98ac0` | `DAT_1406446d8` |
    | `cl_pitchspeed` | `DAT_00a98ad4` | `DAT_1406446e0` |
    | `cl_anglespeedkey` | `DAT_00a98d08` | `DAT_1406446e8` |
    | `m_pitch` | `DAT_00aa4084` | `DAT_1406e2510` |
    | `m_yaw` | `DAT_00aa4080` | `DAT_1406e2518` |
    | `m_forward` | `DAT_00b36200` | `DAT_1406e2520` |
    | `m_side` | `DAT_00b363a4` | `DAT_1406e2528` |
    | `m_filter` | `DAT_00b363a8` | `DAT_1406e2530` |
    | `sensitivity` | `DAT_00aa407c` | `DAT_1406e24f0` |
    | `cl_mouseAccel` | `DAT_00b36208` | `DAT_1406e2500` |
    | `cl_maxpackets` | `DAT_00b3620c` | `DAT_1406ef560` |

    Cross-referencing those globals found a tight, contiguous function
    cluster at `14007d3b0`–`14007e4e0` (~0x1130 bytes) — the same "one source
    file's functions, laid out in original order" shape the x86 cluster had
    at `0057d1xx`–`0057e3xx`. Decompiled and mapped:

    | x64 function | Role | x86 equivalent |
    |---|---|---|
    | `FUN_14007e1e0` | Zeroes a 64-byte struct (`memset(cmd,0,0x40)` shape), orchestrates the rest | `FUN_0057e480` (`CL_CreateCmd`) — **primary hook candidate** |
    | `FUN_14007d9f0` | Calls the mouse-delta reader, writes `forwardmove`/`rightmove` to `+0x1c`/`+0x1d` (same offsets as the documented x86 `usercmd_t` layout), also does angle-finalize math writing a packed `short` to `+0x38` | Looks like `FUN_0057d430` (keyboard movement) and `FUN_0057de60` (angle finalize) **fused into one function** by the x64 compiler — a real structural difference, not just an address shift |
    | `FUN_14007d3b0` | Double-buffered accumulator, sensitivity/`cl_mouseAccel` scaling, magnitude+sqrt | `FUN_0057d680` (raw mouse-delta reader) |
    | `FUN_14007de20` | Alternate-mode angle path, called conditionally | Not yet matched to a specific x86 function — candidate for the vehicle-camera path (`vehCam_*` cvars referenced nearby) |
    | `FUN_14007e4e0` | Separate double-buffered analog channel, called conditionally alongside `FUN_14007de20` | **Possible x64-side match for issue #30's long-unsolved "third analog input channel"** (DPV/mortar/turret) — worth checking directly once hooks exist, would close a genuinely old open issue as a side effect |

    **Real, important structural finding**: function boundaries do NOT map
    1:1 between the x86 and x64 builds — the x64 compiler fused at least two
    previously-separate x86 functions (`FUN_0057d430`/`FUN_0057de60`) into
    one (`FUN_14007d9f0`). Don't assume a clean one-function-per-one-function
    mapping anywhere else in the pipeline either; verify each candidate's
    actual boundaries, don't infer them from the x86 layout.
1f. **Visual-enhancement suite dvar catalog re-located, high-confidence static
    match, NOT yet live-verified.** `RawStringScan.java` on `sm_fastSunShadow`
    found a single `DATA` reference in `FUN_1401b50e0` — the x64 equivalent of
    `FUN_0043a1e0`, the giant renderer dvar-registration function this
    project's `ForceHighQualityShadows`/`ForceHighQualityLighting`/
    `ForceAnisotropicFiltering` toggles were originally built against.
    Decompiling it (725 lines, same "one function, whole dvar catalog" shape
    as the x86 original) found every dvar this session's own visual suite
    needs, registered identically, at these new storage globals:

    | dvar | x64 global |
    |---|---|
    | `sm_fastSunShadow` | `DAT_141884f50` |
    | `r_cacheSModelLighting` | `_DAT_141884ce0` |
    | `r_cacheModelLighting` | `DAT_141884ce8` |
    | `r_texFilterAnisoMax` | `DAT_141884b10` |
    | `r_texFilterAnisoMin` | `DAT_141884b20` |

    (`sm_qualitySpotShadow`, `sm_maxLights`, `r_dlightLimit` were also found
    in the same function, confirming the whole catalog carried over — not
    yet individually mapped to storage globals since nothing in this
    project's own shipped features currently needs them.) **Real write-side
    mechanism (the actual `SetDvarBool`/`SetDvarFloat`-equivalent that turns
    a resolved `+0xc`-offset global into an applied dvar write) is being
    traced separately** — see whichever of this session's parallel fork
    passes covers dvar helpers, to avoid duplicating that work here.
1c. **Broad due-diligence sweep, 2026-09-03: every one of 35 strings checked
    across every major subsystem is identically present in both binaries —
    the strongest evidence yet this is a clean recompile, not a rewrite.**
    Direct instruction: "a good lesson while were here is to see everything
    that has changed." Extended the original 10-string check to cover
    movement/menus (`weapnext`, `togglemenu`, `+actionslot`, `+scores`,
    `+gostand`, `vid_restart`), engine tick (`r_mode`, `com_timescale`,
    `fixedtime`), visual-suite dvars (`sm_qualitySpotShadow`, `sm_maxLights`,
    `r_dlightLimit`, `r_cacheSModelLighting`), and GSC QTE functions
    (`usebuttonpressed`/`meleebuttonpressed`/`attackbuttonpressed`/
    `adsbuttonpressed`, all still absent from both exes as expected — they
    live in compiled GSC, not the binary). **Real methodology correction
    along the way**: an initial pass using `grep -c` (line count) flagged
    `+actionslot` (4→1) and `vid_restart` (4→5) as differing — both were
    false alarms from `grep -c` counting matching *lines*, not matches, and
    binary files have no real line structure (a "line" boundary is just
    wherever a stray `0x0A` byte happens to land). Re-verified with
    `grep -o | wc -l` (true occurrence counting) and both came back
    identical. **Standing lesson for any future binary string-presence
    check in this project: always use `-o | wc -l`, never bare `-c`, against
    binary files.**
1d. **Bind-name table re-located, complete structural match — but the actual
    `kbutton_t` addresses (ADS/Reload/Fire/Sprint/etc.) still need live
    testing, not just this.** The x86 project's own ADS/Reload/Fire/Sprint
    kbutton addresses (`0x00A98B8C`/`0x00A98CB8` ADS, `0x00A98C68` Reload,
    `0x00A98C00` Fire, `0x00A98CCC` Sprint — see `analog_input_hooks.cpp`)
    were never found via strings at all; they were found via **live
    differential testing** (press/release a real key, diff process memory
    for the byte that changed). That technique needs a running, injectable
    build to test against, which doesn't exist yet for x64. What static
    analysis CAN do, and did: `RawStringScan.java` on `"+attack"` and
    `"+toggleads_throw"` found both as `DATA` references at `1404c1878`/
    `1404c1a48` — 0x1D0 bytes apart. Wrote a new script,
    `DumpRawQwords.java` (the x64/8-byte equivalent of the existing
    `DumpRawDwords.java` — genuinely reusable tooling for the rest of this
    migration, not a one-off), and dumped that region: it's the **complete
    32-entry bind-name table**, every single `{+name, -name}` string pair
    intact and readable (`+attack`/`-attack`, `+toggleads_throw`/
    `-toggleads_throw`, `+usereload`, `+sprint`, `+prone`, `+actionslot
    1`-`4`, `+stance`, `+gostand`, all of it), stride `0x10` (2×8-byte
    pointers/entry — proportionally identical to the x86 table's 2×4-byte
    stride). This confirms the table structure carried over exactly, but it
    only stores the bind NAME strings — the real `kbutton_t*` addresses live
    in a separate, parallel state array this table doesn't directly expose.
    **Real next step for buttons**: find the x64 equivalent of
    `FUN_00541020` (the real key-event handler/dispatcher that correlates
    this name table's row index against the kbutton state array) via the
    same string-anchor technique, which would at least locate the
    correlation logic even before live testing is possible — OR wait for an
    injectable x64 build to exist and redo the original live-diff technique
    directly.
    **Follow-up, same pass**: `+attack`'s own `PARAM` reference (from the
    earlier `RawStringScan.java` output) led to `FUN_14007eff0` —
    **the bind-name-to-table-index resolver itself**: walks the table
    starting at `PTR_DAT_1404c1870` (the slot immediately before `+attack`),
    comparing the input string against each entry via `FUN_1402ca760`
    (a `strcmp`-equivalent), returning the matching row index. This is a
    real, concrete piece of the correlation mechanism, but not the whole
    chain — the actual kbutton-state array and how a resolved index maps to
    one of its entries is still unlocated. **Real next step**: find
    `FUN_14007eff0`'s own callers (candidate for the x64
    `FUN_00541020`/key-event-handler equivalent) to trace the rest of the
    chain, or wait for live testing once an x64 build exists.
1e. **Major follow-up, same session: found the real x64 KeyDown/KeyUp setter
    itself, and it's a genuinely simpler mechanism than x86's** — high
    confidence, NOT yet live-verified. Tracing `FUN_14007eff0`'s 7 callers
    found `FUN_14007f130` ("is bind X currently held", walks
    `&DAT_140644a6c + playerIndex*0xd28` in 3-int/12-byte strides comparing
    against the resolved bind index — `0xd28` matches the same per-player
    stride constant already seen independently in the movement pipeline's
    `FUN_14007e4e0`, real cross-validation, not a coincidence) and, tracing
    further, `FUN_14007eaf0`:
    ```c
    void FUN_14007eaf0(int playerIndex, int bindIndex, int isDown)
    {
        piVar1 = &DAT_140644a64 + bindIndex*0xc + playerIndex*0xd28;
        *piVar1 = isDown;         // down-state
        piVar1[1] += isDown ? +1 : (cleared to 0 on release);  // press-count
        // + an active-kbutton counter at &DAT_140644a60 + playerIndex*0xd28
    }
    ```
    Standard x64 calling convention (RCX/RDX/R8 — no custom register tricks
    at all, unlike x86's EAX=self/ECX=bindIndex/stack=timeMs thunk this
    project's `CallKbuttonDown`/`CallKbuttonUp` had to hand-assemble). It
    takes `bindIndex` directly, not a raw `kbutton_t*` — meaning **the bind
    index for any action is computable entirely offline** from the name
    table found in 1d (base `1404c1870`, 8-byte stride,
    `index = (entryAddr - 1404c1870) / 8`): `+attack` (Fire) → index 1,
    `+toggleads_throw` (ADS) → index 59. **If this holds up, ADS/Fire/etc.
    on x64 may not need any custom-calling-convention asm thunk at all** —
    just a plain call `FUN_14007eaf0(0, 59, 1)`. This would make buttons
    genuinely EASIER to port than the movement pipeline, a real reversal
    from how much harder x64 porting looked after hitting the naked-
    trampoline wall in the build-side work. **Not yet confirmed**: whether
    `FUN_14007eaf0` is actually reachable/safe to call directly (vs. needing
    to go through some dispatcher for side effects this trace hasn't found
    yet), and the computed bind indices are static-only, not live-verified.
    **Signature-scan reminder, per the locked 2026-09-03 policy (§5/§10.3
    in `CLAUDE.md`)**: every raw address in this section (`14007eaf0`,
    `1404c1870`, `140644a64`/`140644a60`, `14007eff0`, etc.) is a real
    finding against THIS specific binary build, not a value to hardcode
    directly into shipped hook code. When this actually gets implemented,
    each of these needs a real byte-pattern signature (resolved once at
    startup, cached) built from its surrounding, binary-version-independent
    instruction shape — not the literal address recorded here. Treat every
    address in this whole document the same way: a coordinate for today's
    RE work, not tomorrow's hardcoded constant.
1g. **Pause-menu/key-handler cluster (unresolved by both this file's own
    earlier attempt and the parallel `pausemenu_keyhandler_x64.md` pass) —
    found via re-examining data already on disk, not a fresh scan.**
    `FUN_1402aac50` (already captured as a raw caller of `FUN_14007eff0` in
    `callers_14007eff0_x64.txt` from section 1d/1e's own work, just not
    recognized at the time) is the real x64 key-event handler — the x86
    `FUN_00541020` equivalent. Confirmed via the exact same anchor technique
    the original x86 discovery used: a giant keycode `switch` including
    `case 0x1b:` (ESC, hardcoded specially, forwards to whatever menu has
    `*(*param_2+0x30)` set) and `case 0xb2:` gated on the `developer` dvar
    dispatching the literal string `"screenshot\n"` — the same
    dev-command-string anchor this project's x86 RE already used once
    before to confirm a real key/command dispatcher. Traced one level up:
    **`FUN_14029baa0`** (its only relevant caller) ALSO special-cases
    `0x1b` before calling `FUN_1402aac50(&DAT_142605050, plVar3, keycode,
    isDown)` — matches x86's `FUN_0054b9f0` (the confirmed 4-arg wrapper
    around `FUN_00541020`). Traced one more level: **`FUN_14029baa0`'s own
    callers include `FUN_14007eaf0`** — the SAME buttons/ADS KeyDown/KeyUp
    setter from section 1e — plus **`FUN_14029dfd0`**, independently
    already flagged by the parallel `pausemenu_keyhandler_x64.md` pass as
    its own best (unconfirmed) candidate, now cross-validated as a real
    part of this exact chain rather than a guess. **Real conclusion**:
    `FUN_14007eaf0(playerIndex, bindIndex, isDown)` is not just a kbutton-
    state setter — it appears to be the single, unified real entry point
    for injecting ANY key/bind event on x64, menu/ESC dispatch included,
    where x86 needed several separate specialized functions
    (`FUN_0057d1c0`/`FUN_0057d200` for kbuttons, `FUN_00541020` for raw
    dispatch, `FUN_0054b9f0` as its wrapper, `FUN_004d9850` for menu-
    forwarding). If this holds up live, it's a real, welcome architectural
    simplification for the whole port, not just buttons/ADS. **Still not
    located**: the dedicated `SetMenuState`(mode 0=resume/mode 2=open)/
    `OpenPauseMenu` pair specifically, and `GetTopmostActiveMenu`/
    `ForwardKeyToMenu`'s exact x64 identities — `FUN_1402aac50`'s own ESC
    branch (`LAB_1402ab2d1` → `FUN_1402a3ca0`) is the most promising lead
    for `ForwardKeyToMenu` specifically, not yet independently confirmed.
    **Follow-up, same day: decompiled `FUN_14029baa0` itself in full** (not
    just its call shape) plus the three functions it calls in its ESC
    branch — `FUN_1402ac970`, `FUN_1402ac9c0`, `FUN_1402aaa80` (already
    named `GetTopmostActiveMenu` above, now decompiled too) — and one more
    level down, `FUN_1402c5b30`. Real findings:
    - **`FUN_1402c5b30(name, value)` is a confirmed x64 Cvar_Set equivalent**
      — looks up the dvar by name (`FUN_1402c3890`), and either sets it
      directly if it's a known numeric-ish type or re-registers it as an
      "External Dvar" via `FUN_1402c4a20` if the lookup fails. `FUN_14029baa0`
      calls it as `FUN_1402c5b30("cl_paused", 0)` — a real, confirmed,
      literal `Cvar_Set("cl_paused", "0")` call. Generically useful past this
      cluster: this is the go-to x64 target for any future dvar-write hook,
      not just pause-menu work.
    - **`FUN_14029baa0`'s full shape is now clear, and it's narrower than
      first thought**: the whole function body is gated on
      `GetTopmostActiveMenu() != 0` — i.e. it only ever runs its logic when
      a menu is ALREADY open, so this is not the "open the pause menu from
      nothing" path. Within that gate: on ESC (`0x1b`, key-down) with no
      other higher-priority active substate (`FUN_1402ac970() == 0`) and a
      specific leaf-menu field clear (`*(plVar3+0x30) == 0`), it calls
      `FUN_1402ac9c0()` instead of forwarding the key; on every other
      key/condition it forwards via the already-confirmed
      `FUN_1402aac50` (`ForwardKeyToMenu`). Afterward, if
      `GetTopmostActiveMenu()` now returns 0 (the menu stack fully closed as
      a result), it runs two cleanup calls (`FUN_14007f310`/
      `thunk_FUN_14007eeb0`, likely usercmd/input-state resets — not
      individually decompiled this pass) and then the confirmed
      `Cvar_Set("cl_paused", 0)` call above — this is the real, confirmed
      x64 **resume-gameplay path**, matching x86 `FUN_004396d0`'s `mode==0`
      case functionally, even though it's structurally one merged function
      here rather than a separate `SetMenuState(mode)` call.
    - **`FUN_1402ac9c0` is NOT `OpenPauseMenu`** — decompiled in full, it
      iterates a *different* array (`param_1+0xe`, count `param_1[0x50e]`,
      distinct from the active-menu-stack array `FUN_1402aaa80`/
      `FUN_1402ac970` walk at `param_1+0x510`/`param_1[0x530]`) of what look
      like registered menu-def entries, and for each either closes it
      (`FUN_1402acf70`) if a flag bit is clear or a data pointer is null, or
      refreshes/reformats it (`FUN_1402a3ca0`, the same "@"-indirect string
      formatter seen in `FUN_1402a1540`) otherwise. Best current read: a
      bulk "close/refresh every registered menu screen" sync pass — the real
      action ESC triggers when backing out of the topmost menu with nothing
      else blocking — not a single named-menu open call. **`OpenPauseMenu`
      (opening a menu from a fully-clean, nothing-open state) still isn't
      found** — by definition `FUN_14029baa0` can't be it, since its whole
      body is gated on a menu already being open; the real open call must be
      a separate, still-unidentified site, plausibly inside
      `FUN_14007eaf0` (Start's own handler) before it ever reaches
      `FUN_14029baa0`, not yet traced. None of this is live-tested — Start's
      open and resume-on-close both need real playtest confirmation once an
      injectable build exists.
    - **Same-day follow-up #2, this genuinely resolves `SetMenuState`/
      `OpenPauseMenu` — decompiled `FUN_14007eaf0` in full** (previously
      only known as "the single unified kbutton/key entry point," never
      read end to end) plus two functions its ESC branch calls,
      `FUN_140082e70` and `FUN_14029f3f0`. Real findings:
      - **`FUN_14029f3f0(param_1, mode)` is a confirmed, full x64
        `SetMenuState`-equivalent** — a mode-driven switch far richer than
        x86's `FUN_004396d0` (which only had a documented mode 0/mode 2
        pair). Ten real named destination screens found, each opened by a
        literal menu-name string: mode `0` = resume
        (`Cvar_Set("cl_paused",0)` + `FUN_1402ac9c0()`, matching the
        already-confirmed resume path exactly), mode `1` = the main/
        error-popmenu screen, **mode `2` = `"pausedmenu"`** — this is the
        real, confirmed **`OpenPauseMenu`**: `Cvar_Set("cl_paused",
        FUN_140083ae0())` then `FUN_1402ad950(&DAT_142605050,
        "pausedmenu")` — mode `3` = pregame/loaderror, mode `4` =
        endofgame, mode `6` = briefing, mode `7` = victoryscreen, mode
        `0xb` = coop_lobby, mode `0xc` = levels_challenge, mode `0xd` =
        main_text, mode `0xe` = main_specops.
      - **`FUN_1402ad950(ctx, name)` is a confirmed, real, generically
        useful `OpenMenuByName`/menu-activation-by-string primitive** —
        every named screen above opens through it. A genuinely new,
        reusable find past this cluster: any future work needing to open a
        specific known menu screen by name has a real, confirmed call
        shape now.
      - **`FUN_1402ac9c0` recontextualized, not re-decided**: still the
        "close/cleanup pass over registered menu screens" found last
        round, now with clear purpose — `FUN_14029f3f0` calls it as a
        pre-open cleanup step before several mode transitions (0, 3, 4, 6,
        0xb), i.e. it's the real `CloseMenu`-adjacent half of the
        open/close pair, just never itself the function that opens
        `"pausedmenu"`.
      - **`FUN_140082e70`** turned out to be a different, narrower thing
        than hoped — a connecting/loading-state ESC-cancel handler. It
        reads the exact same per-player connection-state global
        `FUN_14007eaf0` reads as `iVar3` (`DAT_1406e2558[player*100]`,
        very likely a `cls.state`-equivalent), and calls `FUN_14029f3f0`
        with mode `1` or `0` depending on that state — real code, but the
        "cancel while connecting/loading" path, not the general
        live-gameplay pause-open path.
      - **Genuinely still open, flagged honestly rather than guessed
        past**: `FUN_14007eaf0`'s own ESC branch only reaches
        `FUN_14029f3f0(x, 2)` (the confirmed pause-open call) via one
        specific case, `iVar3 == 6` — and whether connection-state `6` is
        really the live SP-gameplay/"active" state (vs., say, an MP
        briefing-specific state, given `FUN_14029f3f0`'s own mode `6` is
        separately named `"briefing"`) isn't pinned down from static
        analysis alone. The other reachable path,
        `FUN_14029baa0(param_1, 0x1b, isDown)`, is itself gated on
        `GetTopmostActiveMenu() != 0` (this section's own earlier finding)
        — whether the engine's menu-stack always carries a baseline
        HUD-as-menudef entry during live play (making that gate trivially
        true) or not is unconfirmed. Resolving the exact live-gameplay
        trigger condition needs either a real `cls.state` enum value dump
        or live testing once an injectable build exists — not a case to
        keep guessing past statically.
    - **Same-day follow-up #3: `FUN_1402aac50` and `ForwardKeyToMenu` both
      genuinely confirmed, not just "promising leads."** Decompiled
      `FUN_1402aac50` in full (previously only its callers/call shape were
      examined). It's a real, confirmed `Menu_KeyEvent`-equivalent — takes
      the active menu-def struct directly as `param_2`, early-outs entirely
      when no menu is active (`param_2 == 0`), and its `switch(param_3)`
      covers a genuine menu-context key set: Tab/select-navigation (`9`,
      `0x9b`, `0x9d`, `0xbd`, `0xcd`), Enter/confirm (`0xd`, `0xbf`, `0xca`),
      **ESC** (`0x1b`, forwards into `*(*param_2+0x30)`, matching this
      section's earlier finding), a `developer`-dvar toggle (`0xb1`) and the
      confirmed `"screenshot\n"` dev-command anchor (`0xb2`) that originally
      anchored this whole cluster, and item-select/invoke (`200`/`0xc9`).
      **`FUN_1402a3ca0` (the `LAB_1402ab2d1` sink) is reached from far more
      than just the ESC case** — the item-search loop, the "already
      hovering this item" fast path, and the `200`/`0xc9` case all funnel
      into it too — confirming it as a real, generic
      `ForwardKeyToMenu`/item-action-execute primitive, not a guess.
      **Genuinely useful negative result**: `FUN_1402aac50` has NO case
      anywhere matching weapnext's dispatch — ruling it out as weapnext's
      real dispatcher (see the parallel `sprint_weapnext_x64.md` pass,
      which had flagged this function as worth checking directly for
      exactly this). weapnext's real x64 dispatch site is still
      unlocated — this closes off one real candidate rather than leaving
      it an open guess for a future pass to re-check.
    - **Same-day follow-up #4: real `cls.state`-equivalent write sites found,
      but the semantic mapping stays genuinely open — flagged honestly
      rather than guessed past.** `DAT_1406e2558` (the connection-state
      field `FUN_14007eaf0`'s ESC branch reads as `iVar3`) has 92
      references across 54 functions project-wide — too broad to fully map
      this pass — but `FindDataWriters.java` on the base address found four
      concrete, literal-value write sites: **state `1`** (`FUN_1400794b0`/
      `FUN_140079520`, both identical bodies, gated on `state != 6` and a
      pending-count check `>1`, then a screen-text update, a conditional
      `SetMenuState(0, 0)` resume call, and the transition); **state `4`**
      (`FUN_140081c10`, gated on a flag `DAT_1406e24c4`, resets network
      fields to `"localhost"` first — strongly suggests "connecting to a
      local/loopback server"); **state `6`** (`FUN_140079130`, gated on a
      pending-job count crossing a threshold, via two parallel legacy/
      new-style code paths — the exact function whose write this session's
      earlier `FUN_14007eaf0` decompile traces `iVar3==6` back to); **state
      `7`** (`FUN_140080d50`, gated on the same `DAT_1406e24c4` flag,
      followed by disconnect-adjacent cleanup calls — plausibly a
      post-game/end state, correlating loosely with `SetMenuState` mode
      7="victoryscreen"). Also confirmed: `FUN_140082e70` (the earlier-found
      connecting-state ESC-cancel handler) fires on states `1` OR `2`
      specifically (not 6), and unconditionally resumes afterward.
      **Real, honest uncertainty, not resolved this pass**: state `6`'s
      writer (`FUN_140079130`) is gated on a *pending-job-count* check, the
      same shape as an in-progress loading/connect step — raising a genuine
      possibility that `FUN_14007eaf0`'s ESC→`SetMenuState(x,2)` branch is a
      **loading-screen-specific cancel/pause prompt**, not the general
      live-gameplay Start-button pause. If so, live Start-button pause
      during ordinary gameplay might route through `FUN_14007eaf0`'s OTHER
      dispatch path entirely — the generic bindIndex case-lookup
      (`(&DAT_140644a6c)[...]` → `FUN_14007c3a0`, already seen in this same
      function) for a `+togglemenu`/Start-bound index, not the raw ESC
      special-case at all. This is a real, structurally plausible
      alternative, not yet checked — the next concrete step for this
      thread, before more `cls.state` semantic archaeology.
    - **Same-day follow-up #5, the real breakthrough: found the actual
      Start-button pause TOGGLE, structurally independent of the
      `cls.state==6` mystery entirely.** Followed through on the alternative
      flagged above — decompiled `FUN_14007c3a0`, confirming it as the real
      x64 equivalent of x86's `FUN_00438710` (the generic case-number
      command dispatcher `FUN_14007eaf0`'s other branch reaches via the
      `(&DAT_140644a6c)[...]` case-lookup table). It's a huge, real, fully
      readable `switch(param_2)` covering dozens of distinct gameplay
      commands — mostly `FUN_14007e460`(down)/`FUN_14007e490`(up) pairs on
      per-player struct fields at `DAT_140644xxx + player*0x230`, a SECOND,
      separate per-bind state system from the kbutton table `FUN_14007eaf0`
      itself owns. Within it: **case `0x43` = `FUN_1400823b0`, confirmed as
      the real live-gameplay pause TOGGLE** — reads the current
      `SetMenuState` mode via `FUN_14029b470` (the earlier-decompiled
      `(&DAT_142615b20)[player]` reader) and calls `SetMenuState(player, 2)`
      (open) if not already paused, or `SetMenuState(player, 0)` (resume) if
      it is — a clean, complete, self-contained toggle, structurally
      unrelated to `cls.state`/`iVar3==6` at all. **Real conclusion**: live
      Start-button pause almost certainly routes through this generic
      case-dispatch path (case `0x43`), not the ESC-specific `cls.state==6`
      branch — the two are separate mechanisms, matching x86's own
      historical split (ESCAPE hardcoded specially in the key handler vs.
      Start driving pause through a different path). This resolves the
      practical "how does pause actually open during live gameplay"
      question with high confidence, independent of ever pinning down
      `cls.state`'s exact semantics. **Correction, same day**: case `0x42`
      (`FUN_1400706d0`) was initially checked here on the hope it might
      match x86's own weapnext case number and dismissed as coincidence,
      reasoning that x86/x64 case numbers aren't guaranteed to align —
      **that dismissal was wrong.** `sprint_weapnext_x64.md`'s own follow-up
      pass traced the real mechanism: `FUN_14007c3a0`'s case numbers are
      directly the bind-name-table indices (confirmed via `FUN_14007eff0`,
      the real string→index resolver, and `FUN_14007f330`/`Key_SetBinding`,
      which writes that same index into the per-keycode slot
      `FUN_14007eaf0` reads and passes straight through) — not a separate,
      independently-numbered case-ID space. Weapnext's own computed
      bind-name-table index (66 = `0x42`) landing on a real case in this
      switch isn't a coincidence, it's the direct, mechanical consequence
      of that shared indexing. **`case 0x42` = `FUN_1400706d0` IS weapnext's
      real dispatcher, now independently confirmed** by decompiling its own
      callee `FUN_140074570` — a genuine weapon-slot-cycling function (a
      15-entry array, `%0xf` wraparound, a real forward/backward direction
      parameter, ammo/holdability gates, and a real weapon-switch call).
      See `sprint_weapnext_x64.md` for the full trace — weapnext is now
      RESOLVED, static, high confidence, not just narrowed.
1h. **Render-scale/shadow-map thread: found the render-target orchestrator,
    re-confirmed (not just re-found) the x86 team's own hardest open
    question rather than cracking it.** `$shadowmap_large` needed the
    literal `$`-prefixed string (bare `RESOLVED_SCENE`/`SAVED_SCREEN`
    substrings returned zero raw matches for reasons not tracked down —
    a real, unexplained tooling gap, not evidence those targets don't
    exist) — found at `1404135a0`, one data reference at `1404d0690`
    (the x64 render-target name-table base, equivalent to x86's
    `0093a2f0`). That table has exactly one code reference,
    `FUN_1401b8c80` — the x64 equivalent of `FUN_004b60a0` (the shared
    slot-descriptor creator `InternalRenderScalePercent` already sits on
    top of). Its 5 real callers were decompiled in full: they create
    `SAVED_SCREEN` (index 9), `FLOAT_Z` (index 2), `SSAO`/`SSAO_BLURRED`/
    `SSAO_FLOAT_Z` (indices 0xe/0xf/0x10), and three unlabeled indices
    (0xb/0xc/0xd, model-lighting-related). **None of the 5 callers pass
    index 0 or 1 (shadowmap large/small)** — the exact same negative
    result the original x86 investigation reached after 9+ rounds
    including a full Ghidra analysis pass. This is now a SECOND
    independent binary generation confirming the same gap, which raises
    real confidence this isn't a tooling/coverage miss specific to either
    binary — the actual creation call site is very plausibly reached via
    an indirect call (a function pointer, not a direct `CALL`), which
    static direct-xref tooling structurally cannot find either way. Not
    pursued further this pass; a genuinely different technique (indirect-
    call pattern scanning, or live tracing once an injectable build
    exists) is the real next step, not more direct-reference scanning.
2. ~~Add an x64 build configuration to `proxy_d3d9.vcxproj`~~ **DONE
   2026-09-03, but revealed the real scope is bigger than this line
   originally implied.** Debug/Release x64 configs added, `IntDir` fixed to
   include `$(Platform)` (was about to silently collide Win32/x64 object
   files in the same folder), `hde32.c`/`hde64.c` correctly split per
   platform. MinHook's `trampoline.c`/`hook.c`/`buffer.c` do already branch
   on `_M_X64` cleanly, confirmed by getting a clean compile — that part of
   the original claim held up. **What it didn't anticipate**: two real,
   separate x86-only inline-assembly problems, found only by actually
   attempting the build:
   - `dllmain.cpp`'s D3D9 export-forwarding stubs (`__declspec(naked)` +
     inline `__asm { jmp dword ptr [...] }`, for every real d3d9.dll export
     other than `Direct3DCreate9`) — neither `__declspec(naked)` nor inline
     `__asm` exist on MSVC's x64 target at all (hard compiler rejection, not
     a warning). **Fixed**: real MASM (`forward_stubs_x64.asm`, assembled by
     `ml64.exe`, wired into the `.vcxproj` via the `masm.props`/`masm.targets`
     build customization) — functionally identical tail-jump thunks, just
     expressed as real x64 assembly instead of inline C `__asm`. This part
     is genuinely done and confirmed assembling clean.
   - **The much bigger one**: `analog_input_hooks.cpp` alone has 12 more
     `__asm` blocks (`real_settings.cpp` has 2 more), and unlike the export
     stubs, most of these are the actual **hook-install trampolines
     themselves** — `__declspec(naked)` functions like `Hook_0057de60`
     (literally the movement/look pipeline hook this doc's own section 1b
     re-located the x64 target for earlier) and `Hook_693ff0`/
     `Hook_0061f6f0`/etc., plus two raw custom-calling-convention call
     thunks (`CallKbuttonDown`/`CallKbuttonUp` for ADS). **None of this
     compiles for x64 as-is, and none of it can be mechanically translated
     the way the export stubs were** — each one needs a real per-hook x64
     redesign, and most of their underlying target addresses aren't even
     confirmed for x64 yet (only the movement/look cluster from section 1b
     is). **Direct decision (2026-09-03): stop here rather than rush a
     stub-everything pass** — continue finding real x64 hook-target
     addresses via Ghidra first; don't write more hook-install/asm code
     until a given hook's real target is actually confirmed. The build
     infrastructure itself (x64 configs, MASM export forwarding, MinHook)
     is genuinely done and stays; `analog_input_hooks.cpp`/
     `real_settings.cpp` are excluded from further x64 build attempts until
     their real targets are found.
3. x64dbg (not x32dbg) is already installed
   (`D:\Tools\x64dbg\release\x64\x64dbg.exe`) — no new tooling needed for live
   debugging.
4. Design the actual signature-scanning mechanism: a byte-pattern-plus-
   wildcards scanner (the classic AOB/IDA-style signature format), resolved
   once at startup (not a continuous/repeated re-scan loop — keep the runtime
   surface as narrow as this approach reasonably allows), with the same
   "validate before hooking, fail loudly rather than hook garbage" standard
   §5 already requires for hardcoded addresses.
5. Re-derive each hook target's real x64 signature by diffing the function's
   already-decompiled x86 body (from the preserved Ghidra project) against the
   new binary — start with the highest-value/most-central hooks
   (`FUN_0057de60`'s equivalent, the per-frame usercmd pipeline) rather than
   working alphabetically through every known address.
