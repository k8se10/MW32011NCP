# Patch Notes

All notable changes to the project, per release. See
`re_notes/known_issues_x64.md` for the full, actively-tracked issue list and
reverse-engineering trail behind each entry. The prior `-x86` line's own
patch history is preserved in
[`legacy-x86-docs/PATCHNOTES.md`](legacy-x86-docs/PATCHNOTES.md).

---

## v0.0.1-x64 — Unreleased

**Summary:** The first release on the `-x64` line, rebuilding this project
from scratch against MW3's recompiled 64-bit binaries. Every core gameplay
control is implemented and build-verified, most confirmed live; the visual-
enhancement suite, vibration/rumble, native controller menu navigation, and
the menu-focus/itemDef tracking glyph icons depend on are all now ported and
build-verified (none live-tested yet). A full feature-parity audit against
the `-x86` line (`re_notes/x64_feature_parity_audit.md`) found and closed
several real gaps this file's own prior summary had missed, most notably
Sprint silently running on x86's own deprecated pre-kbutton design and
vibration never having been wired to x64 at all. **This release has not
shipped** — see `README.md` for the current release gate (parity with the
`-x86` line's final state) and `re_notes/known_issues_x64.md` issue #1 for
live, detailed status on every item below.

### What's New
1. **Every core gameplay control implemented.** Movement, look, Sprint,
   Fire, ADS (true hold-to-aim), Reload, weapon switch, Melee, Lethal,
   Tactical, Jump, Interact, Crouch/Prone (tap vs. hold), pause menu
   open/close, and D-pad actionslot all hook the game's real engine
   functions directly, resolved via runtime signature scanning. Most are
   confirmed working live by direct playtest; see the table in `README.md`
   for exactly which.
2. **Jump auto-stand.** Jumping while crouched or prone now stands the
   player up first, matching console behavior — forces the real
   `togglecrouch`/`toggleprone` case matching whatever the current stance
   actually is.
3. **Auto-unstick.** An automated pause/unpause cycle on level start fixes
   the "needs a click at launch" input-gate issue automatically.
4. **Plugin API ported.** The loading infrastructure needed no code changes
   at all — it was already architecture-neutral. The bundled RGB Text
   example plugin gained its own x64 build configuration.
5. **Custom Options screen wired into the input pipeline.** The screen's own
   draw/navigate code was already cross-platform; a new poll function drives
   it from the same always-on tick Pause's own toggle uses. Its real, native
   open trigger (focus landing on the actual in-game "Options" menu item) is
   now also ported and wired in alongside the original temporary LB+RB
   chord, which stays as a fallback until the real trigger is live-confirmed
   — see item 11 below.
6. **Addressing architecture: runtime signature scanning.** Every hook
   target is resolved via a wildcarded byte-pattern scan against the game's
   own main module, once at process startup and cached for the session —
   see `CODE_STANDARDS.md` for the full policy and rationale.
7. **"Greenlit" trusted-plugin allowlist.** A small, explicit allowlist of
   first-party plugin filenames now load automatically, without requiring
   `[Plugins] Enabled=1` — this project's own `security/` component's
   netcode security-fix plugin ships built in this way by default. Every
   other, arbitrary third-party plugin still needs the normal opt-in — see
   `PLUGIN_API.md` for the full design and its real caveat (filename
   matching isn't cryptographic).
8. **Native D-pad+A/B controller menu navigation.** Previously 100% absent
   on x64 — a controller player could not navigate any native menu (main
   menu, pause menu, options screen, buy-station/armory lists) and needed
   keyboard/mouse for every menu interaction. Now drives the same real
   engine call the game's own ESC key uses to forward input to whatever
   menu is active, resolved via signature scan. Also fixes two real
   conflicts found during the port: D-pad's menu navigation could have
   double-fired against the existing raw D-pad actionslot dispatch, and B's
   menu-back could have toggled real crouch/prone underneath an open menu
   — both now correctly suppress the gameplay-side action while a menu is
   active. Not yet live-tested. See `re_notes/known_issues_x64.md` issue #1
   for the full RE trail.
9. **Vibration/rumble ported to x64.** Previously 100% absent — the code
   that installs it was only ever called from an x86-only-guarded path, so
   it silently never ran, despite not appearing anywhere in this project's
   own prior gap list. Both real mechanisms ported: fire rumble (a real
   engine hook, its x64 target confirmed three independent ways) and damage
   rumble (a per-frame health poll against the real x64 entity array,
   confirmed via three independent consumers computing the same array
   layout). Not yet live-tested.
10. **The visual-enhancement suite ported to x64** — internal render scale,
    FSR 1.0 RCAS sharpening, and camera motion blur. Resolves a target
    (`InternalRenderScalePercent`'s own resolution-compute function) two
    prior static-RE passes couldn't find. All three of x86's own proven-
    necessary safety gates (menu-active, `clcState`, in-level) are wired for
    both FSR and motion blur — deliberately not shipped on a weaker gate
    than x86's own documented crash history (issues #103/#104) proved
    necessary. Not yet live-tested.
11. **Menu-focus/itemDef-array tracking ported to x64.** The underlying
    mechanism controller-glyph icons, the custom cursor, and the custom
    Options screen's real (non-chord) open trigger all depend on — every
    struct offset independently re-derived and cross-confirmed for x64's
    different (64-bit-aligned) layout, not assumed from the x86 original.
    The real Options-screen trigger is now wired (see item 5). **Scope
    note**: this resolves the focus-DETECTION half only — actually drawing
    gameplay glyph icons still needs a separate, not-yet-ported native
    text-draw hook (a different, larger RE task) — see Investigated, Not Yet
    Resolved below.
12. **Survival ready-up (hold Y) ported to x64.** Previously 100% absent —
    the same narrowly-scoped, already-approved exception `-x86` ships (no
    native dispatch was ever found for F5/"skip" after an exhaustive search,
    see `CLAUDE.md`): holding Y for `[Survival] ReadyUpHoldThresholdMs`
    (740ms default) synthesizes a real `WM_KEYDOWN`/`WM_KEYUP` F5 via
    `PostMessageA` at the game's own window; releasing early instead fires
    the normal weapon-switch, same hold-vs-tap split as `-x86`. One honest,
    deliberate difference from `-x86`: the extra `IsInSurvivalMode()` gate
    isn't wired in, since x64's own `Dvar_FindVar` equivalent needed to read
    the `mapname` dvar is a separate, still-unresolved RE target — the
    synthetic F5 fires unconditionally on the hold edge instead, relying on
    the same "a misplaced F5 outside its one context is simply ignored"
    reasoning `-x86`'s own design already documents as sufficient even
    without that gate. Build-verified, not yet live-tested. See
    `re_notes/known_issues_x64.md` issue #1 for the full trail.
13. **Hold Breath (L3 while ADS'd, sniper-class) ported to x64.** Previously
    100% absent (parity audit item #23) — L3 only ever drove raw Sprint,
    with no ADS-aware branch to a separate kbutton. Now edge-triggers the
    real kbutton activate/deactivate handlers on a newly-resolved,
    dedicated struct, matching `-x86`'s exact gating (`sprintHeld && adsHeld`,
    no explicit sniper-class check in either platform's own code — the real
    native kbutton is what limits the sway-reduction/accuracy effect to
    sniper weapons). Also fixes a real bug found while implementing this: a
    pre-existing early `return` in the same per-tick function would have
    silently skipped Hold Breath's own edge check on every tick where
    Sprint's own state was steady — restructured so Sprint and Hold Breath
    update as two independent state machines, matching `-x86`'s own design
    shape. Build-verified, not yet live-tested. See
    `re_notes/known_issues_x64.md` issue #1 for the full trail.
14. **DualSense gyro-aim ported to x64, staying PREVIEW/WIP.** Turned out to
    be the same "real mechanism exists, was just never called from the x64
    tick" shape as the vibration/rumble gap this session already closed —
    `dualsense_input.cpp`/`controller_input.cpp`'s gyro-read path has no
    architecture guard at all, it simply wasn't wired into the x64 look
    pipeline. Now applied additively on top of stick-based look (the same
    `+=` pattern `-x86`'s own `InjectControllerLookAngles` uses for its gyro
    contribution), gated by the existing gyro-only-while-ADS toggle. Axis
    mapping and invert-sign handling copied verbatim from `-x86`'s own
    still-unverified-on-real-hardware mapping. Build-verified, not yet
    live-tested (needs real DualSense hardware to exercise). See
    `re_notes/known_issues_x64.md` issue #1 for the full trail.

### Fixed
1. **Crash on launch with the sniper Fire/ADS fix's own log line.** The
   diagnostic message that fix attempt logs on resolving its target
   formatted a 16-hex-digit pointer into a buffer 10 bytes too small,
   which this UCRT fails fast on rather than truncating (surfaced as
   `0xC0000409`, misleadingly labeled `STATUS_STACK_BUFFER_OVERRUN` by
   Windows even though the real cause was a CRT argument-validation
   fail-fast, not a stack-cookie violation). Root-caused via a full
   crash-dump analysis (WinDbg/`cdb` against `%LOCALAPPDATA%\CrashDumps`,
   symbolized against the exact built PDB) rather than Event Viewer alone
   — see `re_notes/known_issues_x64.md` issue #1 for the full trail and
   the reusable diagnostic technique.
2. **D-pad Left's squadmate-call-in exception ported.** D-pad Left now
   synthesizes a real keypress instead of calling the native action-slot
   function directly, matching how a real keyboard press reaches the game —
   a leading fix for a live "sometimes different keys used" report, not yet
   independently confirmed.
3. **A fix attempt for sniper-class Fire/ADS.** Real RE work found that
   every other bind press/release sends a client-side notification the
   game's own scripting layer can react to, which controller Fire/ADS never
   sent; now sends it alongside the existing input logic. Not yet confirmed
   live.
4. **The on-screen cursor was silently non-functional.** It read raw,
   unguarded addresses left over from the 32-bit binary, which safely but
   silently failed against the 64-bit process instead of crashing — fixed
   by gating it off honestly pending a real x64 port of the underlying
   mechanism.
5. **Sprint (L3) was using x86's original, deliberately-abandoned mechanism**
   (forcing the raw `pm_flags`-equivalent bit directly), not the real
   `+sprint` kbutton x86 ultimately shipped — meaning the native sprint
   duration/recovery timer and the Extreme Conditioning perk override never
   applied. Found by a full feature-parity audit against the `-x86` line;
   fixed by driving the same real kbutton activate/deactivate calls already
   used for Fire/ADS/Reload. Also ports the "stand up from crouch/prone on
   Sprint's rising edge" behavior, reusing the same real toggle Jump's own
   auto-stand already calls. See `re_notes/known_issues_x64.md` issue #1 for
   the full RE trail.

### Documentation
1. **`re_notes/known_issues_x64.md` established** as the dedicated x64 issue
   tracker.
2. **Full documentation reset.** Every contributor/user-facing doc in the
   repo, the Nexus mod-page copy, and the GitHub wiki were archived to
   `legacy-x86-docs/` and rewritten fresh to describe the current x64-based
   project rather than the discontinued 32-bit line.
3. **Security notice added for unpatched base-game netcode vulnerabilities.**
   MW32011NSP's research confirmed three RCE-class stack overflows in
   `iw5sp.exe`/`iw5mp.exe` netcode survive unchanged into the current x64
   build. Reported to Activision through their official disclosure channel;
   a general risk notice (no exploit-enabling detail) now sits at the top
   of `README.md` pending a fix.
4. **NCP redefined as Native Community Patches; `MW32011NSP` absorbed as a
   nested component.** NCP's own identity expanded 2026-09-12 from "Native
   Controller Project" — name/repo unchanged, meaning redefined, same
   pattern as the earlier 2026-09-03 redefinition — to cover netcode
   security patching alongside controller input and the visual-enhancement
   suite. The former sibling `MW32011NSP` repo's full commit history
   (24 commits, including its original vendor security-disclosure record)
   carried over intact via a `git subtree` merge into this repo's own
   `security/` directory — not a fresh copy. Mechanically unchanged: the
   same three fixes, the same standalone DLL, the same "greenlit" plugin
   loaded the same way — see `security/PATCHNOTES.md` for that component's
   own detailed history, and `CLAUDE.md`'s 2026-09-12 Version Timeline
   entry for the full decision record.

### Groundwork
1. **`signature_scan.h`/`.cpp`** — the runtime AOB byte-pattern scanner this
   entire architecture is built on: parses wildcarded hex patterns, walks
   the game's own PE headers, fails loudly on a zero or ambiguous match.
2. New Ghidra tooling for x64 RE work, including raw-byte reference scanners
   for tracking down indirect references static analysis alone misses.

### Investigated, Not Yet Resolved
1. **Gameplay controller-glyph icon overlays** (in-hint "Press [A]"-style
   replacements, on-screen hint prompts, the custom cursor) still don't
   draw on x64. The dependency this was originally blocked on (menu-focus/
   itemDef tracking) is now resolved — see item 11 above — but the actual
   native text-draw hook glyphs need to intercept (x86's `Hook_DrawGlyphText`)
   has no x64 equivalent yet. A different, larger, not-yet-attempted RE
   task (the hook itself, font/asset matching, the glyph allowlist), not a
   quick follow-up to item 11.
2. **FXAA and a forced-MSAA option** don't exist on either line — checked
   directly, and neither was ever actually built even on the old `-x86`
   line, only ever planned. Real future work, not a regression.
