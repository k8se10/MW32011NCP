# Native text-draw hook (x86's `Hook_DrawGlyphText`) — x64 port (2026-09-13)

Continuation of the x64 migration RE work (`known_issues_x64.md` issue #1,
`re_notes/x64_migration/README.md`). Same methodology as the rest of this pass:
`RawStringScan.java` → `DecompileAt.java` → `FindCallers.java` → `DumpSigBytes.java`,
run via `analyzeHeadless.bat -process iw5sp.exe -readOnly -noanalysis` against the
existing `re_notes/ghidra_project_x64/iw5sp_x64_proj.gpr` — not re-imported, not
modified structurally.

**Standing reminder, per the locked 2026-09-03 signature-scanning policy (`CLAUDE.md`
§5/§10.3): every address below is a coordinate against THIS specific x64 binary
build, found for today's RE work — not a value to hardcode into shipped hook code.**
The actual implementation (`analog_input_hooks_x64.cpp`) resolves both targets via a
real byte-pattern signature at startup, cached for the session — see that file's own
"Native text-draw hook" section for the finished signatures.

## Why this was the blocker

x86's own `Hook_DrawGlyphText` (`analog_input_hooks.cpp`, hooking `FUN_00690c80`) is
the single call site every gameplay-hint/menu-hint/glyph-icon feature in this project
observes or replaces text through. `analog_input_hooks_x64.cpp` had zero equivalent —
confirmed via grep before this pass (zero references to `mantle`, `DrawGlyphText`, or
any text-draw hook at all) — which is why gameplay controller-glyph icons, on-screen
hint prompts, the F2/F3 glyph-position editor, and Auto-Mantle's own ledge-detection
signal were all blocked on this one missing piece (`known_issues_x64.md` issue #1's
"Scope note" round and the separate Auto-Mantle investigation round, both 2026-09-12).

## Discovery trail

**Anchor**: `RawStringScan.java` against `"PLATFORM_MANTLE"` found exactly one
reference — `1403f0568`, inside `FUN_140052220`. Same technique x86's own discovery
used (its own `FUN_00568110` was found the identical way, anchored on
`"PLATFORM_PICKUPNEWWEAPON"` — see `CLAUDE.md`'s "Key technical finding" cross-
reference in `known_issues.md`).

**`FUN_140052220`** (`decomp_140052220.txt`) is the x64 equivalent of x86's generic
HUD-element dispatcher (`FUN_00568110`'s own role) — a giant `switch(param_11)` over
hud-element-type, considerably more inlined on x64 than x86's own split-across-several-
functions design: this ONE function builds the substitution string (via
`FUN_14029f120`, the localized-string lookup) AND calls the draw chain, per case, in
one place. Case `0x50` (Mantle) and case `0x47` (Hold Breath) both follow the same
shape:

```
FUN_1402b0050(param_1, "+gostand", local_478, 0x100);      // build key-name substitution text
uVar15 = FUN_14029f120("PLATFORM_MANTLE");                  // resolve reference key -> localized template
uVar15 = FUN_14029de10(uVar15, local_478);                  // "&&1" substitution
iVar7  = FUN_14029fb10(uVar15, 0, param_14, param_15);      // measure text width
iVar9  = FUN_14029fac0(param_14);                            // measure text height
...
uVar13 = FUN_14008d8f0(param_1);                             // resolve a DC/render-context handle
FUN_14029a2b0(uVar13, uVar15, 0x7fffffff, param_14,          // <-- the draw call
              x, y, color1, color2, param_15, &DAT_1404393c0, param_18);
```

`param_14` (an opaque `undefined8` in the outer dispatcher's own signature, never
dereferenced by that function) is the live `Font_s*` — threaded through unchanged,
exactly the same "opaque handle in, opaque handle out" shape x86's own `fontArg` has.

**`FUN_14029a2b0`** (`decomp_14029a2b0.txt`) — decompiled to an 11-parameter, plain
function (no custom register tricks) with **22 real callers** (`callers_14029a2b0.txt`
in the scratch output, condensed here): pickup/mantle/hold-breath hints, the FPS
counter's own `"fps: %f"` string (`FUN_1402ac2b0`), death-quote captions
(case `0x61`), distance/waypoint markers (`FUN_140033dc0`, `FUN_140036720`),
`COOP_WAITINGFORPLAYER` (`FUN_140039f40`), and more — exactly the "universal,
used-everywhere" character x86's own comment attributes to `FUN_00690c80`. This is
the correct x64 hook point: not the smaller leaf it calls internally
(`FUN_140080840`, confirmed via decompile to be a thin, SINGLE-caller forwarder into
`FUN_1401d2520`), but the call site every one of those 22 unrelated features already
passes every drawn string through — matching x86's own hook SITE, not just chasing
the deepest possible leaf.

Signature: `?? ?? ?? ...` real bytes — `DumpSigBytes.java`'s own reference-based
heuristic flagged every RSP-relative `MOV`/`MOVSS`/`MOVAPS`/`LEA` in this function's
prologue as needing wildcarding (the exact same false positive already documented
against `kPmoveTickSignature` — RSP-relative displacements are fixed stack offsets,
never addresses that shift between builds). Hand-corrected: the only genuine
PC-relative instruction in the captured span is `CALL 0x1401b7c90` at `+0x31`, whose
4-byte `rel32` is wildcarded; every other byte (69 of 70) is kept literal. See
`impl_sig_14029a2b0.txt` for the full per-instruction dump.

**`FUN_14029f120`** (`decomp_14029f120_140080840.txt`) — the real x64
`SEH_GetString`/`GetLocalizedString` equivalent. Confirmed via full decompile to be a
byte-for-byte behavioral match to x86's own documented `FUN_00532230` contract: skips
lookup entirely for an already-literal string (leading `0x15` escape byte), otherwise
defers to a real reference→current-language table lookup (`FUN_14028a680`), and falls
back to echoing the raw key string itself into a static buffer if the lookup returns
null — never returns null itself, always safe to call with no null check on the
caller's side. This is NOT the same function as `real_settings.cpp`'s
`GetLocalizedString()` x64 stub, which deliberately just echoes the reference key
back unchanged (`#if defined(_M_X64)... return referenceKey`) since nothing on x64
had a real resolver to call before this pass — `g_getLocalizedStringX64` (resolved
separately, direct-call, no hook) is the genuine, live-resolving equivalent, making
the Mantle-hint structural match a real, language-independent comparison rather than
an English-only shortcut.

Signature (`impl_sig_14029f120.txt`): small enough that `DumpSigBytes.java`'s
heuristic correctly identified every genuine PC-relative site (two short `JNZ`/`JMP`
rel8 jumps, one `CALL` rel32, one RIP-relative `LEA`) with no false positives this
time — each wildcarded at its own displacement bytes only, 31 of 39 bytes literal.

## What was implemented

See `analog_input_hooks_x64.cpp`'s own "Native text-draw hook" section (the
authoritative, current description — this file is the RE trail, that file's own
header comment is kept in sync with actual scope). Summary:

- **Stage (a)**: `Hook_DrawTextX64` — plain passthrough + fire-count logging
  (`[x64-drawtext]`), proving signature-scan → MinHook-install → detour-fires
  end to end on this call site, zero behavior change to any of the 22 real
  callers' own drawn text.
- **Stage (b)**: Mantle-hint structural-match detection, wired on top of (a).
  `g_mantleHintLastSeenMsX64` / `IsMantleHintCurrentlyShowingX64()` mirror x86's
  `g_mantleHintDrawnThisFrame`/`g_mantleHintLastSeenMs`/`IsMantleHintCurrentlyShowing()`
  (collapsed to a single timestamp write — see that section's own comment for why
  the per-frame accumulate-then-commit indirection isn't needed for an
  observably-equivalent result). Gated identically to x86's own
  `ShouldDrawGlyphOverlay() && !IsMenuActive()` block via new exported wrappers
  (`ShouldDrawGlyphOverlay_Exported()` in `analog_input_hooks.cpp`,
  `IsMenuActiveX64_Exported()` already existing) — this is a faithful port of x86's
  real coupling between the glyph-overlay toggle and Auto-Mantle's own detection
  signal, not a new behavior introduced by this port.
- **Stage (c)**, 2026-09-13, same day: real VISUAL glyph-icon substitution,
  extending the structural-match technique to two more hint families and wiring
  `RequestCustomHintOverlay` for the first time on x64. New RE this stage (all via
  `analyzeHeadless.bat -process iw5sp.exe -readOnly -noanalysis` against the
  already-imported `iw5sp_x64_proj`, same toolkit):
  - `RawStringScan.java` against `"PLATFORM_PICKUPNEWWEAPON"`/
    `"PLATFORM_THROWBACKGRENADE"` found both inside `FUN_14004fa00` (2/1
    references); `DecompileAt.java` confirmed this function ALSO handles
    `PLATFORM_SWAPWEAPONS`/`PLATFORM_PICKUPHEALTH` (both real `"+activate"`
    binds, per `ui_assets.md`'s zone-dump research) and calls `FUN_14029a2b0`
    (this hook's own target) directly for all four, at the exact same call site
    shape as Mantle/Hold Breath in `FUN_140052220`.
  - `RawStringScan.java` against `"PLATFORM_RELOAD"` found it in a DIFFERENT
    function, `FUN_140031bc0` — `DecompileAt.java` confirmed THAT function calls
    `FUN_1402afa60`, not `FUN_14029a2b0` (the same alternate draw function
    dispatcher case `0x61`'s death-quote captions use). Reload's text can never
    be seen by this hook — a real, structural reason, not a priority choice.
  - `RawStringScan.java` against `"SENTRY_PLACE"` found **zero** references
    anywhere in this x64 binary — genuinely unresolved (different storage,
    doesn't exist in this build, or needs a different anchor), not pursued
    further.
  - `DecompileAt.java` against `FUN_14029fac0`/`FUN_14029fb10` (the width/height
    text-measure functions, called with `fontArg` directly) → `FUN_1401b7cd0`/
    `FUN_1401b80f0` (the real leaves) recovered a PARTIAL x64 `Font_s` layout:
    `pixelHeight` confirmed at `font+0x08` (`FUN_1401b7cd0`'s own
    `return *(undefined4*)(param_1+8)`), `glyphCount` confirmed at `font+0x0C`
    and the `DiagGlyph*` array confirmed at `font+0x20` (both direct
    dereferences inside `FUN_1401b80f0`'s own binary-search/direct-index glyph
    lookup). `DiagGlyph`'s own internal 24-byte stride (`letter`@+0x00,
    `dx`/advance-width@+0x04) is confirmed BYTE-IDENTICAL to x86's — expected,
    since it's raw loaded font-asset data with no pointers, architecture-
    independent by construction. `fontName`'s own offset (`font+0x00`, the one
    field `IsGameplayHintFont`-style filtering would actually need) was NOT
    independently confirmed — no leaf function dereferencing it was found this
    pass; its placement is inferred by natural x64 struct alignment (matches
    x86's exact field order widened for 8-byte pointers: the confirmed 0x10-byte
    gap between `glyphCount` and `glyphs` is exactly two pointers, `material`/
    `glowMaterial`, precisely like x86) but this is an INFERENCE, not a decompile-
    confirmed fact — flagged here so a future pass doesn't cite it as settled.
  - New function: `TryGetPickupGlyphAssetName` (`analog_input_hooks.cpp`,
    resolves via `LogicalAction::ReloadUse` — the real `"+activate"` bind's own
    default key, "F", the same one Reload's own icon already resolves through)
    — purely additive, x86's own `Hook_DrawGlyphText` never calls it, zero
    change to x86's existing generic-path pickup/swap/health behavior.
  - `ConvertRealScreenPosToDesignSpace` turned out to have internal linkage in
    the x86 file too (confirmed via a real `LNK2019` — it sits inside a SECOND
    anonymous namespace opening a few hundred lines before its own definition,
    an easy miss by eye) — duplicated locally as
    `ConvertRealScreenPosToDesignSpaceX64`, same convention as
    `TextMatchesTemplateStructurallyX64`/`FindColorHighlightSpanX64`.
  - Detection stays purely structural (exact prefix/suffix match against the
    real, live-resolved template) for all three substituted cases — Mantle,
    Pickup/Swap/PickupHealth, Throwback — so none of them needed the still-
    unconfirmed `fontName` filtering above.

## What was NOT implemented this pass (honest scope)

- x64's real `Font_s` `fontName` offset remains unconfirmed (see Stage (c) above)
  — no `IsGameplayHintFont`-style font-name filtering exists. Not needed for the
  three substituted cases (structural template match is the gate instead), but
  still blocks any future case that has no known reference-key template of its
  own — which is exactly why buy-station and ready-up (next bullet) are stuck.
- **Buy-station** (`"Hold ^3F^7 to use Weapon Armory"`) and **Survival ready-up**
  (`F5`) remain unported — genuinely blocked, not skipped for convenience: x86
  itself has no reference-key template for either (per `ui_assets.md`'s own
  zone-dump research — ready-up's text is Survival-script-driven, buy-station's
  key was never found even for x86), so x86 protects them from false positives
  via `IsGameplayHintFont`, not a structural match. Porting these safely needs
  the `fontName` gap above closed first.
- **Reload** remains unported for a structural reason, not a priority one — see
  Stage (c) above: it flows through a completely different native draw function
  (`FUN_1402afa60`), which this hook (on `FUN_14029a2b0`) can never observe.
  Would need its own separate signature/hook.
- **Sentry-Place** remains unported — its own reference string
  (`"SENTRY_PLACE"`) was not found anywhere in this x64 binary at all (see
  Stage (c) above); `TryGetSentryPlaceGlyphAssetName` exists and is ready to use
  once a real x64 reference/template is found.
- Menu-hint detection (x86's `ResolveMenuGlyphAssetNameForKeyName` block) was
  not ported — out of scope per this task's own priority ordering (in-game
  hints first).
- **No position/scale nudge tuning was ported.** x86's own pixel-perfect
  alignment (`kHintVerticalNudge`, `kMantleHintXNudge`/`YNudge`) was reached via
  several rounds of LIVE-TESTED empirical correction specific to x86's own
  position-math convention (documented in detail in
  `analog_input_hooks.cpp`'s own history around those constants). x64's own
  substitution uses the raw converted position with zero tuning — on-screen
  alignment against the real mantle-arrow sprite/pickup icon is UNVERIFIED and
  will very likely need the same class of empirical correction once actually
  seen running.
- Auto-Mantle's own `+gostand`-forcing feature is now wired on x64 by a
  concurrent session the same day (`known_issues_x64.md` issue #1, commits
  `21e2546`/`811523d`) — unrelated to this glyph-substitution work, not
  something this pass touched.

Not yet live-tested — build-verified only (x64 `/t:Rebuild` 0 errors, dumpbin-
confirmed `8664 machine (x64)` fresh timestamp, Win32 regression rebuild 0
errors/0 warnings, x64 redeployed last). See `known_issues_x64.md` issue #1 for
the live-test status this lands under.
