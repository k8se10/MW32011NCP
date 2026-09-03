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
