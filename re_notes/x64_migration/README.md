# x64 migration — MW3 (2011) recompiled to 64-bit, 2026-09-03

## Status: RE underway. A fresh Ghidra project against the new x64 `iw5sp.exe`
exists and is fully analyzed (`re_notes/ghidra_project_x64/iw5sp_x64_proj.gpr`,
kept completely separate from the original x86 project). No hook code has been
written against the new binaries yet. This is the single biggest event in this
project's history — read this whole file before touching anything x64-related.

## Sub-cluster RE passes (2026-09-03) — separate files, cross-linked here

Three parallel passes covering distinct hook clusters, each in its own file to
avoid concurrent-edit conflicts with this shared README. Read each for the
real evidence and honest confidence level; this is just an index.

| Cluster | File | Real confidence |
|---|---|---|
| Sprint / weapon switch | [`sprint_weapnext_x64.md`](sprint_weapnext_x64.md) | Sprint: storage globals + state machine found, high confidence; the actual Pmove-entry *write* hook still unlocated. weapnext: bind-table position found (index 66), dispatch mechanism not traced. |
| Pause menu / key handler | [`pausemenu_keyhandler_x64.md`](pausemenu_keyhandler_x64.md) | `cl_paused`'s real storage global found. **Updated after this file's own section 1g**: the real key-event handler (`FUN_1402aac50`, x86 `FUN_00541020` equivalent) and its wrapper (`FUN_14029baa0`) ARE now found, high confidence — cross-validates `pausemenu_keyhandler_x64.md`'s own `FUN_14029dfd0` candidate as a real part of the chain. Dedicated `SetMenuState`/`OpenPauseMenu`/`ForwardKeyToMenu` still not individually pinned. |
| D-pad actionslot / generic dvar API | [`actionslot_dvarhelpers_x64.md`](actionslot_dvarhelpers_x64.md) | Dvar API (`Dvar_FindVar`/get/set): high confidence, cross-validated 4 independent call sites — real simplification over x86, standard calling convention, no `__asm` needed; value offset shifted `+0xc`→`+0x10` (real x64 struct-alignment change, confirmed two ways). D-pad actionslot: bind indices computed (15/17/19/21), but whether it reuses the buttons/ADS `FUN_14007eaf0` mechanism directly is unconfirmed. |

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
