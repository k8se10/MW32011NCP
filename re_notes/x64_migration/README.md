# x64 migration — MW3 (2011) recompiled to 64-bit, 2026-09-03

## Status: RE underway. A fresh Ghidra project against the new x64 `iw5sp.exe`
exists and is fully analyzed (`re_notes/ghidra_project_x64/iw5sp_x64_proj.gpr`,
kept completely separate from the original x86 project). No hook code has been
written against the new binaries yet. This is the single biggest event in this
project's history — read this whole file before touching anything x64-related.

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
2. Add an x64 build configuration to `proxy_d3d9.vcxproj` alongside the
   existing Win32 one (MinHook already supports x64 natively — no vendoring
   changes needed, only build config).
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
