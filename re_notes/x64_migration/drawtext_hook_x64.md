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

## What was NOT implemented this pass (honest scope)

- x64's real `Font_s` struct layout (the x86 `DiagFont` equivalent) was not
  independently re-derived — no `IsGameplayHintFont`-style font-name filtering is
  applied. The structural template match alone is the only gate on false positives
  for the Mantle case.
- No visual glyph-icon substitution — `RequestCustomHintOverlay` is never called from
  this hook, native hint text renders completely unmodified for every case observed,
  Mantle included.
- Interact hints, Reload's bare-word detection, Throwback-grenade/Sentry-Place
  detection, and menu-hint detection (x86's other
  `RenderedTextMatchesSubstitutionTemplate*`/`RenderedTextMatchesReferenceKey*`
  callers) were not ported — Mantle only, per this task's own explicit priority
  ordering ("at least... the Mantle hint specifically since Auto-Mantle depends on
  it").
- Auto-Mantle's own `+gostand`-forcing feature (the actual `out |= 0x400u` injection,
  gated on `autoMantleEnabled && IsSprintActive() && IsMantleHintCurrentlyShowing() &&`
  cooldown on x86) was NOT wired on x64 this pass — only the detection DEPENDENCY
  (`IsMantleHintCurrentlyShowingX64()`) now exists for a future pass to consume. x64
  has no direct `IsSprintActive()`-equivalent read either (Sprint is now kbutton-
  driven on x64, per the 2026-09-12 migration — see `sprint_weapnext_x64.md` — with
  no native pm_flags read exposed), so wiring the feature itself is a further,
  separate task, not a trivial follow-on.

Not yet live-tested — build-verified only (see `known_issues_x64.md` issue #1 for the
live-test status this lands under).
