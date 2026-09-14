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
vibration never having been wired to x64 at all. The native text-draw hook
that blocked gameplay glyph icons and Auto-Mantle's own detection dependency
is now ported too, with Mantle-hint detection wired on top, and Auto-Mantle's
actual `+gostand`-forcing feature now ships on top of that (off by default).
Real visual glyph substitution now works for nine hint families (Mantle,
pickup/swap/pickup-health, grenade throwback, Reload, and every menu corner
hint — Back/Friends/Quit/Leaderboards/Game Summary, the last three plus
Friends-suppression logic added in a later follow-up) — see items 16-19 and
22 under What's New. `Dvar_FindVar`/`GetEffectiveFov`'s x64 equivalents are
now resolved too, closing the ADS zoom-aware look-slowdown and Survival
ready-up's `IsInSurvivalMode()` gate in the same pass (item 20). **This
release has not
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
    the normal weapon-switch, same hold-vs-tap split as `-x86`. The extra
    `IsInSurvivalMode()` gate `-x86` also uses — originally omitted, since
    x64's own `Dvar_FindVar` equivalent needed to read the `mapname` dvar
    was a separate, unresolved RE target — is now wired in too (see item 20
    below for the resolution). Build-verified, not yet live-tested. See
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
15. **Back's `+scores` scoreboard key-synthesis ported to x64.** A small,
    cheap wiring fix (parity audit row 30), not new RE work — `-x86`'s own
    `InjectControllerScoreboard()` had no architecture guard at all and
    would compile fine on x64 as-is, it was simply never called from
    anywhere in the x64 input pipeline. Now wired in with the same
    hold-through-passthrough `PostMessageA(VK_TAB)` mechanism, gated on the
    Back button's real physical mapping. **This is intentionally, correctly
    a no-op in Campaign/Survival** — confirmed by direct Xbox 360 console
    testimony that no scoreboard UI exists in SP at all, on any platform
    (`known_issues.md` issue #28) — real value arrives once Multiplayer
    ships with its own actual scoreboard. Build-verified, not yet
    live-tested. See `re_notes/known_issues_x64.md` issue #1 for the full
    trail.
16. **Native text-draw hook ported to x64, with Mantle-hint detection wired
    on top.** This was the single remaining blocker for gameplay controller-
    glyph icons, on-screen hint prompts, the F2/F3 glyph-position editor,
    and Auto-Mantle's own ledge-detection signal (`known_issues_x64.md`
    issue #1's 2026-09-12 "Scope note" round and the separate Auto-Mantle
    investigation the same day). Finds and hooks `FUN_14029a2b0` — the x64
    equivalent of x86's `Hook_DrawGlyphText` target (`FUN_00690c80`),
    confirmed via 22 real callers spanning every kind of HUD/hint text drawn
    on this engine — via the same `RawStringScan.java` anchor technique x86's
    own discovery used. On top of the plain passthrough hook, wires a real,
    language-independent structural match against the LIVE localized
    `PLATFORM_MANTLE` template (resolved via `FUN_14029f120`, the real x64
    `SEH_GetString` equivalent — not `real_settings.cpp`'s x64
    `GetLocalizedString()` stub, which just echoes the key back and would
    never match). **This unblocks Auto-Mantle's own detection DEPENDENCY**
    specifically (see item 17 below for the feature itself) — at the time
    this hook first shipped, no visual glyph-icon substitution was drawn yet;
    see item 18 below for the same-day follow-up that changed this for three
    hint families. Full honest scope, discovery trail, and what's still
    missing: `re_notes/x64_migration/drawtext_hook_x64.md`. Build-verified,
    not yet live-tested. See `re_notes/known_issues_x64.md` issue #1 for the
    full trail.
17. **Auto-Mantle's real `+gostand`-forcing feature wired on x64**, ships off
    by default matching `-x86`'s exact default (`AutoMantleEnabled=0`).
    Composes `IsSprintActiveX64()` from three already-existing x64 tracking
    variables (`g_sprintKbuttonActiveX64`, `GetRealStanceX64()`,
    `g_adsHeldX64`) — an exact parity port of `-x86`'s own `IsSprintActive()`,
    no new reverse-engineering needed, since Sprint's earlier move to a real
    kbutton (item 8's own Sprint work) turned out to relocate the pieces this
    needed rather than remove them. Wired together with item 16's Mantle-hint
    detection into the same real `+gostand` usercmd bit (0x400) Jump already
    uses, with the same 750ms cooldown and forward-stick-cone check `-x86`
    ships. Build-verified, not yet live-tested — see
    `re_notes/known_issues_x64.md` issue #1 for the full trail.
18. **Real glyph-icon visual substitution now draws on x64 for three hint
    families** (same day as item 16, a follow-up pass on top of it): Mantle,
    weapon pickup/swap/pickup-health, and grenade throwback now suppress the
    native hint text and draw this project's own icon+text instead —
    `RequestCustomHintOverlay` actually gets called from x64 for the first
    time. All three are confirmed, via fresh decompile, to flow through the
    same `FUN_14029a2b0` draw call item 16 hooks; detection stays a purely
    structural match against the real, live-resolved reference-key template
    (`PLATFORM_PICKUPNEWWEAPON`/`SWAPWEAPONS`/`PICKUPHEALTH`/
    `THROWBACKGRENADE`, same technique as Mantle), so no font-name filtering
    was needed. A new function, `TryGetPickupGlyphAssetName`, resolves the
    pickup family's icon via the same physical key Reload's own icon already
    uses. **Buy-station, Survival ready-up, Reload, Sentry-Place, and menu
    corner hints remain native/unmodified** — buy-station and ready-up have
    no known reference-key template even on `-x86` (blocked on x64's still-
    unconfirmed `Font_s` `fontName` offset); Reload is confirmed to flow
    through a completely different native draw function this hook can't
    observe; Sentry-Place's own reference string wasn't found anywhere in
    the x64 binary. No position/scale alignment tuning was ported either —
    on-screen alignment for the three working cases is unverified pending
    live test. Full trail: `re_notes/x64_migration/drawtext_hook_x64.md`.
    Build-verified, not yet live-tested. **Superseded — see What's New
    item 22 below**: Reload and every menu corner hint are now covered too.
19. **Highlighted-item A-glyph (menu list navigation) and the F2/F3 in-game
    glyph-position editor wired to real x64 menu-focus tracking** — a
    separate system from item 17's gameplay-hint icon substitution: this one
    draws an A-button icon on whichever native menu list item is currently
    highlighted, using the same manually-calibrated position table `-x86`
    already ships, and the F2/F3 editor is the tool used to build/extend
    that table. Both features only ever depended on one shared debounced
    focus-tracking function, whose x64 branch was still a stub predating the
    real x64 itemDef-array walk built for item 5's own Options-screen
    trigger — now routed to that same, already-working implementation via
    two new thin `extern "C"` wrappers (the functions live in an anonymous
    namespace in a different translation unit). No new reverse engineering.
    Build-verified, not yet live-tested — see `re_notes/known_issues_x64.md`
    issue #1 and `re_notes/x64_feature_parity_audit.md` rows #35/#36.
20. **ADS zoom-aware look-slowdown ported to x64** (`GetAdsLookRateScaleX64`),
    closing parity audit row #3. Its two real dependencies — x64 equivalents
    of `Dvar_FindVar` and `GetEffectiveFov` — were genuinely unresolved RE
    targets until this pass: found via this project's own established
    dvar-value-discovery chain (`FUN_1402c3890`/`FUN_140069e60`, full trail
    `re_notes/x64_migration/getEffectiveFov_dvarFindVar_x64.md`). The formula
    itself is a byte-for-byte port of `-x86`'s own `GetAdsLookRateScale`
    (the power-curve scale plus the close-range taper for low-zoom
    weapons), wired into `Hook_MovementTick`'s Look pre-hook. The same
    `Dvar_FindVar` resolution also closed a second, unrelated gap in the
    same pass: Survival ready-up's `IsInSurvivalMode()` gate (item 12 above),
    previously omitted, now wired at `SendSyntheticF5X64`'s call site.
    Build-verified, not yet live-tested.

21. **Custom mouse cursor overlay ported to x64**, closing parity audit row
    #37 — previously an honest early-return stub (see Fixed item 4 below).
    The real native cursor-visible-flag and UI-state globals were found via
    fresh RE (`kCursorGateSignature`, `analog_input_hooks_x64.cpp`), cross-
    confirmed two independent ways: they're read by a function structurally
    identical to x86's own native cursor-draw dispatcher (same gate/switch
    shape, same literal strings, same `"ui_cursor"` asset), AND they sit at
    the exact same struct offsets from their UI-context base as x86's own
    equivalents do from theirs. A second, previously-invisible gap found
    along the way — the function's menu-active check was silently always
    false on x64, which alone would have kept the cursor from ever drawing
    outside the glyph-position editor — is fixed too. Build-verified,
    not yet live-tested.

22. **Quit/Leaderboards/Game-Summary menu-hint glyphs, plus Special-Ops-modal/
    Friends-list Friends-suppression, now wired on x64** — a menu-hint parity
    follow-up on top of items 16/18/19. The prior claim (this file and this
    hook's own in-code comment) that these depended on "x86-only menu-focus/
    itemDef infrastructure not yet ported to x64" was re-checked against
    `-x86`'s own originals and found stale: `looksLikeCornerHintRow` reads
    only the draw call's own raw screen position (no itemDef dependency at
    all); Quit/Leaderboards/Game-Summary are plain resolved-template string
    compares, same class as the already-working Back/Friends; and the
    Friends-suppression logic needs the focused item's raw NAME, not the
    `(group,index,siblingCount,depth)` tuple the existing x64 menu-focus walk
    exposes — closed with a small, confident extension of that SAME
    already-working walk (`TryGetRealFocusedItemNameX64`, no new RE), then a
    direct port of `-x86`'s own four-iteration sticky-state algorithm on top
    of it. Quit/Leaderboards are gated on position (not font family, which
    x64 still can't confirm) — matching `-x86`'s own BUG-006 precedent that
    position, not font, is the real discriminator that stops a false match
    against a genuine navigable menu item sharing the same label. Nine hint
    categories now visually substitute in total. Build-verified, not yet
    live-tested. See `re_notes/known_issues_x64.md` issue #1 and
    `re_notes/x64_feature_parity_audit.md` row #34.

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
   sent; now sends it alongside the existing input logic. **Correction,
   2026-09-13 (live test): the underlying bug is NOT weapon-class-specific
   after all — Fire/ADS fails on the base pistol too, contradicting the
   sniper-specific framing this fix was built around.** See "Investigated,
   Not Yet Resolved" below.
4. **The on-screen cursor was silently non-functional.** It read raw,
   unguarded addresses left over from the 32-bit binary, which safely but
   silently failed against the 64-bit process instead of crashing — fixed
   by gating it off honestly pending a real x64 port of the underlying
   mechanism. **Superseded — see What's New item 21 above**: the real x64
   port has since shipped.
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
6. **Crash on every single launch (2026-09-13), same bug class as item 1
   above, recurring in new code.** A log line added the same day for the
   native text-draw hook's own localized-string-lookup resolve formatted a
   239-character literal into a 160-byte buffer — a guaranteed, not
   conditional, overflow, so the game failed to launch 100% of the time
   once that code path was built and deployed. Root-caused via a live
   crash dump (WinDbg/`cdb`) the same way item 1 was. Given the volume of
   new log lines added across the same session, swept every `sprintf_s`-
   into-fixed-buffer call site added that day rather than fixing only the
   confirmed crash — found and fixed one more guaranteed overflow
   (`InternalRenderScalePercent`'s own resolve log) and two sites
   interpolating raw, unbounded live-resolved game text without the
   truncation (`%.Ns`) this project already uses everywhere else for
   exactly this situation. See `re_notes/known_issues_x64.md` issue #1 for
   the full trail.

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
3. **Full RE trail for the x64 text-draw hook discovery**
   (`re_notes/x64_migration/drawtext_hook_x64.md`) — the `RawStringScan.java`
   → `DecompileAt.java` → `FindCallers.java` → `DumpSigBytes.java` chain
   applied to find `FUN_14029a2b0` (item 16 above) via the same
   "anchor on a real reference-key string, trace forward to the draw call"
   technique x86's own original discovery used, plus a real, independently-
   confirmed x64 `SEH_GetString` equivalent (`FUN_14029f120`).
4. **SMAW lock-on vs. aircraft (Goalpost, `known_issues.md` issue #27 Bug
   #8/task #29) closed as confirmed NOT a bug**, resolving one of this
   project's longest-open Campaign killstreak questions with zero native
   RE needed. Starting from GSC (per this project's own "start from script
   logic first" methodology, using `xensik/gsc-tool`) found the SMAW is
   never referenced by name anywhere in Goalpost's own scripts at all; the
   real answer was in the weapon's own native data file instead —
   `weapons/smaw_nolock` sets `lockonSupported\0`/`guidedMissileType\None`
   directly, a deliberately dumb-fire-only weapon configuration, distinct
   from the genuinely lock-on-capable `weapons/iw5_smaw_mp`
   (`lockonSupported\1`/`guidedMissileType\Sidewinder`) found elsewhere in
   this project's asset dumps. Applies identically on `-x86`/`-x64` and
   regardless of input device — the `.ff` zone/weapon-data files are game
   content, not part of the recompiled native binary.

### Investigated, Not Yet Resolved
1. **Fire and/or ADS fails — first live playtest of the x64 build,
   2026-09-13, confirmed a real bug, not just a "not yet live-tested" fix
   attempt.** Originally reported and investigated as sniper-class-
   specific (What's New item 3 above); the live test found it happens on
   the base pistol too, directly contradicting the sniper-specific
   framing that fix attempt was built around. The `g_notifyBindDispatch`
   fix itself (a real client->server reliable-command notify controller
   Fire/ADS was skipping) may still be valid — it's additive and
   inert-if-wrong by design — but its own reasoning ("a sniper-class
   bolt-action/scope state machine needs this notify") no longer explains
   the actual, now-confirmed-broader symptom. Root cause not yet
   re-established; needs a more precise report (is it Fire, ADS, or both;
   constant or intermittent; which weapons confirmed affected) before
   further RE. See `re_notes/known_issues_x64.md`'s 2026-09-13 correction
   round for the full trail.
2. **Gameplay controller-glyph icon overlays — PARTIAL, not fully resolved
   (updated, item 22 above).** Items 18/19/21/22 now draw real icons/tracking
   for Mantle, weapon pickup/swap/pickup-health, grenade throwback, Reload,
   every menu corner hint (Back/Friends/Quit/Leaderboards/Game Summary), the
   Special-Ops-modal/Friends-list Friends-suppression logic, the
   highlighted-item A-glyph, the F2/F3 glyph-position editor, and the custom
   cursor. **Still genuinely native/unmodified**: buy-station's "Hold F to
   use Weapon Armory," Survival's ready-up prompt (F5), and turret placement
   (Sentry-Place) — buy-station and ready-up are blocked on x64's real
   `Font_s` `fontName` offset, still unconfirmed (needed for
   `IsGameplayHintFont`-style filtering, since neither has a known
   reference-key template even on `-x86`, and a dedicated investigation
   could not resolve the offset via decompile); Sentry-Place's own reference
   string wasn't found anywhere in the x64 binary. See
   `re_notes/x64_migration/drawtext_hook_x64.md` for the exact scope and why
   each remaining case is blocked.
3. **FXAA and a forced-MSAA option** don't exist on either line — checked
   directly, and neither was ever actually built even on the old `-x86`
   line, only ever planned. Real future work, not a regression.
