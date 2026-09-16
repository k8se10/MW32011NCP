# Patch Notes

All notable changes to the project, per release. See
`re_notes/known_issues_x64.md` for the full, actively-tracked issue list and
reverse-engineering trail behind each entry. The prior `-x86` line's own
patch history is preserved in
[`legacy-x86-docs/PATCHNOTES.md`](legacy-x86-docs/PATCHNOTES.md).

---

## v0.0.1-x64 — Unreleased

**Summary:** The first release on the `-x64` line, rebuilding this project
from scratch against MW3's recompiled 64-bit binaries. The first real
playtest of this build has now happened: every core gameplay control except
D-pad actionslot is confirmed working live, along with motion blur, main-menu
navigation, and glyph-icon substitution. That same playtest found and closed
several real, previously-undiscovered bugs — most notably an x64-specific
regression where a movement-tick early-return meant to skip a no-op write
instead silently disabled Fire, ADS, Reload, and most other controls whenever
the stick was centered (the actual cause of "Fire/ADS randomly fails," not
the earlier sniper-specific theory), plus three separate `sprintf_s` buffer
overflows that made the build fail to launch entirely. A full feature-parity
audit against the `-x86` line (`re_notes/x64_feature_parity_audit.md`), later
extended with a full git-history sweep, found and closed dozens of real gaps
the project's own documentation had missed — vibration, the visual-enhancement
suite (including motion blur's own real trigger hook, found and wired only
after the gate that depended on it had already been live-tested silent), the
native text-draw hook nine glyph-icon categories now substitute through, and
Sprint's silent regression to x86's own deprecated pre-kbutton design, among
many others. On direct instruction, every Campaign killstreak-type system and
outstanding Campaign issue with a real history of being broken was also
investigated from GSC script logic first: DPV/Goalpost mortar/Goalpost M2
turret aiming (never fixed on either architecture, now fixed with a real
shared root cause), Campaign QTE/scripted-sequence button presses (Jump
falling through the "Dust to Dust" elevator, now fixed via a synthetic
keypress, the same technique already proven for Survival's ready-up),
cutscene-skip audio (fixed on both `-x86` and `-x64`, with x64's own version
turning out worse than x86's ever was), AC-130 zoom sensitivity (fixed) and
gun-type switching (investigated, honestly still open), and SMAW's lock-on
(confirmed to have never been a bug at all — the weapon's own data file has
it compiled out). Predator Missile's launch is now confirmed live; its
post-fire guidance remains open, more thoroughly mapped than ever, with a
safe diagnostic shipped rather than a guess. The Custom Options screen's
real vanilla-setting tabs remain a known, deliberately deferred gap — the
INI config already covers everything this mod needs to expose. **This
release has not shipped** — see `README.md` for the current release gate
(parity with the `-x86` line's final state) and
`re_notes/known_issues_x64.md` issue #1 for live, detailed status on every
item below.

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

23. **DPV/Goalpost mortar/Goalpost M2 turret aim — root cause found and a
    fix implemented for the first time on EITHER architecture.** This
    controller-aim bug never worked on the `-x86` line either — genuinely
    new ground, not a parity port. The engine routes aim during these three
    mounted-weapon sequences through a separate per-frame function
    (`FUN_14007de20`) that this project's normal Look hook never runs
    during, so controller aim input had nowhere to go — confirmed via a
    fresh decompile, and also correcting an earlier wrong guess for which
    x64 function was responsible. Fixed with a new, dedicated hook
    (`Hook_MountedAimTick`) that feeds right-stick input into the correct
    native fields. Build-verified, **not yet live-tested** — the mechanism
    is high-confidence; sensitivity/sign are a starting guess pending an
    actual DPV/Goalpost playtest. See `re_notes/known_issues.md` issue #30
    and `re_notes/known_issues_x64.md` for the full trail.
17. **On-screen Multiplayer status warning.** Launching under `iw5mp.exe`
    now shows a real, must-see on-screen warning — through the same
    notifier system "Controller Connected"/"MW32011NCP Started" already
    use, but drawn as a dismiss-required, centered warning-yellow modal
    instead of an ordinary auto-expiring toast, so it can't be missed or
    silently replaced by a routine startup toast racing it (a real gap
    fixed the same day: ordinary toasts previously could clobber an active
    must-see warning outright). **Updated 2026-09-16**: originally read
    "Multiplayer has no functionality working right now" — updated to
    "Multiplayer has no functionality beyond the netcode security
    protections right now" once the `security/` component's netcode-fix
    hooks were confirmed live-installing and firing under `iw5mp.exe` too
    (see the 2026-09-15 MP-hardening entries above). Gameplay hooks still
    aren't installed under MP; the security protections genuinely are
    active. The message swaps in place to "Multiplayer is in pre-alpha and
    will contain bugs and issues. It is not on par with Campaign/Survival."
    once MP gets real partial gameplay support — no new code path needed,
    just a one-line text change when that day comes.
18. **"K+M safe mode" config toggle.** A new, hot-reloadable
    `[General] DisableControllerInput` INI key disables ALL controller/mod-
    side INPUT injection (movement, look, Fire, ADS, Reload, Weapnext,
    Melee, Lethal, Tactical, Jump, Interact, D-pad, CrouchProne, Scoreboard,
    menu navigation, vibration) in one flip, added since keyboard/mouse
    testing has always been comparatively light for this project and this
    gives K+M players a clean way to opt out of any input-side regression
    without losing anything else. Config loading/hot-reload and every
    visual-enhancement feature (motion blur, FSR, render scale, forced
    shadows/lighting) are completely unaffected — those live entirely in
    the render path. Off by default; real keyboard/mouse input always keeps
    working regardless of this setting. Build-verified, not yet live-tested.
19. **Frame-pacing limiter — issue #99's third attempt, ported with credit
    from an external reference implementation.** New `[Video]
    FramePacingEnabled` INI key applies a high-resolution, adaptively-
    corrected wait at the end of every real frame (`Hook_EndScene`), capping
    to the game's own existing `com_maxfps` — structurally different from
    both prior failed attempts: it never writes `com_maxfps` (the second
    attempt's own confirmed failure — the engine treats that dvar
    differently for gameplay than menus) and doesn't use a blind
    fixed-interval wait (the first attempt's own failure). The pacing
    algorithm is ported, with credit, from
    [legoliamneeson/MW3_Standalone_D3D9_Project](https://github.com/legoliamneeson/MW3_Standalone_D3D9_Project)
    — see `README.md`'s Credits section and `frame_pacing_x64.cpp`'s own
    header comment for the full attribution and what specifically was and
    wasn't reused. Off by default; build-verified, **not yet live-tested**.
20. **Wait-coalescing/archive-priority-boost, ported with credit from the
    same external reference implementation.** New `[Video]
    WaitCoalescingEnabled` INI key replaces the game's own coarse-resolution
    `Sleep(1)`/`WaitForSingleObject(1)` busy-polls with a real
    high-resolution wait, but ONLY at three specific, signature-verified
    native call sites (the render thread's own poll, the backend thread's
    own poll, and an archive/job worker's idle wait) — every other
    Sleep/Wait caller in the process, including this mod's own threads, is
    untouched. Also temporarily boosts an archive-loading worker thread's
    priority during a detected read burst, restoring it once idle. Ported
    from [legoliamneeson/MW3_Standalone_D3D9_Project](https://github.com/legoliamneeson/MW3_Standalone_D3D9_Project)
    — see `README.md`'s Credits section and `wait_coalescing_x64.cpp`'s own
    header comment for the full attribution. Off by default; build-verified,
    **not yet live-tested**.
21. **Persistent `.iwd` archive read cache, ported with credit from the same
    external reference implementation.** New `[Video] IwdReadAccelEnabled`
    INI key maps each `.iwd` archive file read-only into this process once,
    on first open, and serves every subsequent read the game's own `.iwd`
    streaming loader makes against it — a real, signature-verified native
    call site, not a blanket "any `.iwd` read" — directly from that mapped
    view instead of a real disk I/O syscall. Deliberately does NOT port the
    source project's own lower-level CRT `_read`/file-descriptor-table path:
    that piece's exact internal struct layout couldn't be independently
    verified against this binary the way every other signature here was,
    and getting it wrong risks real memory corruption rather than just a
    missed optimization — this stays at the real, documented Win32 API
    layer only (`CreateFileA/W`, `ReadFile`, `SetFilePointer(Ex)`,
    `CloseHandle`). Ported from
    [legoliamneeson/MW3_Standalone_D3D9_Project](https://github.com/legoliamneeson/MW3_Standalone_D3D9_Project)
    — see `README.md`'s Credits section and `iwd_read_cache_x64.cpp`'s own
    header comment for the full attribution. Off by default; build-verified,
    **not yet live-tested**.

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
7. **A third launch crash from the same bug class, introduced by the
   glyph-position fix below after this same day's own sweep had already
   run.** Confirmed via a second live crash dump. The lesson this
   recurrence forced: a same-day buffer-safety sweep doesn't retroactively
   cover code written after it runs — this needs to be checked per-commit
   going forward, not as a periodic pass.
8. **Motion blur's real x64 trigger hook found and wired.** The
   2026-09-12 fix genuinely wired motion blur's three safety gates and
   its per-frame look-delta feed, but its only real trigger was an
   x86-only raw-`__asm` engine hook that never compiled for x64 at all —
   the gate was armed, nothing ever pulled it, and the parity audit's own
   "FIXED" verdict was a real overclaim that never checked for the
   trigger specifically. Found the real x64 equivalent
   (`FUN_14018def0`) via a new, reusable Ghidra self-recursive-function
   scanner (13,295 functions checked, one real structural match) — unlike
   x86, it uses a plain fastcall convention, so no naked-asm hook was
   needed at all. Live-confirmed 2026-09-14.
9. **Glyph-icon substitution positioning, fixed in two passes.** The
   text-draw hook's captured draw coordinates were wrongly assumed to
   already be the final screen-pixel position — the real transform
   happens in a native function called AFTER this hook's own
   interception point, confirmed via fresh disassembly, producing
   invisible (Mantle) or top-of-screen (Interact/Reload) icons depending
   on how the wrong position happened to land. Fixed by calling the real
   native transform directly. A second pass then ported `-x86`'s own
   already-live-tested empirical nudge constants for the Mantle hint
   specifically, as a first-pass fine-alignment correction on top of the
   now-fixed transform.
10. **Custom mouse cursor overlay wasn't showing at the true main menu.**
    Confirmed via direct log correlation and a fresh decompile: the
    native menu-open state the cursor's own gate depends on has a real
    static writer for every other menu-open case (pause, briefing, buy-
    station, etc.) except the true main menu, which has zero callers
    anywhere in the binary — not a logic bug, a genuine gap in what the
    game itself sets. Fixed by OR-ing in a second, already-proven x64
    signal (menu-stack depth) that correctly covers the main menu too,
    without touching the already-working in-game pause case.
11. **DPV/Goalpost mortar/Goalpost M2 turret aim** — see What's New item 23.
12. **AC-130 gunship-camera look sensitivity now scales with zoom.**
    Previously never scaled to the gunship's own camera zoom (felt "mega
    sensitive" when zoomed in) — the existing ADS look-slowdown formula
    was only ever triggered while a weapon-ADS flag was set, which the
    gunship sequence never sets. Fixed by widening the trigger condition
    to also cover any other real native zoom source (confirmed via
    disassembly that the FOV query already generically covers turret
    zoom), carefully bounded so ordinary hipfire and the already-correct
    ADS case are unaffected. Gun-type switching (105mm/40mm/25mm) was
    also investigated in depth — confirmed entirely GSC/data-driven with
    no native dispatch case to hook, correctly left unfixed rather than
    guessed at against an already-working feature. See
    `re_notes/known_issues.md` issue #40.
13. **Campaign scripted sequences (QTEs) ignoring controller input
    entirely — root cause found and fixed, unifying two previously
    separate reports.** Jump falling through the "Dust to Dust"
    elevator/chopper QTE and a direct "X does nothing during a QTE"
    report share one cause: the script's own detection
    (`notifyoncommand("playerjump", ...)`) only fires on the engine's
    real command-dispatch chain, never on raw usercmd/kbutton state —
    which is exactly how this project's controller Jump works, so the
    script genuinely never learns the jump happened. Fixed using the
    same technique already proven for Survival's own ready-up: a real
    synthetic keypress fired alongside (not instead of) the existing
    input, functionally identical to what a real keyboard player already
    produces. Melee/Lethal/Tactical likely share this bug class but
    weren't part of the live report and weren't touched this pass. See
    `re_notes/known_issues.md` issues #75/#108.
14. **Cutscene-skip audio fixed on both `-x86` and `-x64`.** The real
    engine has a genuine three-way branch for Start's key handler
    depending on cinematic state; x86's own controller handling had
    quietly drifted from that design and unconditionally forced the
    pause menu open regardless of cinematic state (the original
    audio-persists bug); x64's own version was more severe — it always
    called a generic toggle with no cinematic-skip case at all, so
    Start silently did nothing during an actual cutscene. Both now
    route through the real skip chain the engine itself uses. One
    honest, undischarged gap on both platforms: if a given cutscene
    is a GSC-scripted in-engine cinematic rather than a true Bink FMV,
    neither fix touches it — no live-readable flag for that case has
    ever been found. See `re_notes/known_issues.md` issue #98.
15. **SMAW lock-on vs. aircraft** — see Groundwork below; confirmed not
    a bug, not a fix.
16. **Survival ready-up (F5) now shows its own real prompt on `-x64` and
    suppresses the native one, closing a live-reported gap ("ready up
    works but prompt needs to be shown and suppress the old").** The
    native text-draw hook now detects the real "Press F5 to ready up"
    hint and replaces it in place with this project's own icon+text
    ("Hold F5..." — the real verb for how this project's own mechanism
    actually works, a hold not a tap), at the same real screen position
    the native prompt would have drawn (the same accurate draw-location
    transform Mantle/Pickup/Throwback/Reload substitution already uses,
    now applied to this hint too). x86's own font-based safety check
    that protects this text match from false-positiving elsewhere isn't
    available on x64 (a genuine, already-documented RE blocker); a
    different, already-resolved real signal (Survival-mode detection)
    stands in for it instead. QTE prompts and the buy-station hint
    remain unported for the same underlying reason — neither has an
    available substitute signal. See `re_notes/known_issues_x64.md`
    issue #1's newest round.
17. **Multiplayer (`iw5mp.exe`) no longer crashes navigating menus —
    real groundwork for MP support: gameplay hooks are now gated by
    which game executable actually loaded this DLL.** Live-reported:
    `iw5mp.exe` loaded this DLL fine (XInput polling and other
    exe-agnostic init succeed, hence "controller connected" showing
    even under Multiplayer) but crashed navigating menus. Root cause:
    `iw5sp.exe` and `iw5mp.exe` share the same install directory and
    therefore the same deployed `d3d9.dll`, but every one of this
    project's several thousand lines of signature-scanned gameplay
    hooks was found and verified against `iw5sp.exe` ONLY — this
    project's own standing policy has always been that the two
    binaries are separate reverse-engineering efforts with no assumed
    address/signature parity, but nothing actually enforced that at
    runtime. Hook installation ran completely unconditionally
    regardless of which binary loaded the DLL, so at least one
    SP-verified signature was very likely spuriously matching unrelated
    bytes somewhere in `iw5mp.exe`'s own, differently-compiled code and
    installing a hook at a location that behaves completely differently
    there. Fixed by detecting the real loading executable
    (`GetModuleFileNameA` against the process's own main module,
    compared against the two known real binary names) before any hook
    installation runs: gameplay hooks now only install under confirmed
    `iw5sp.exe`; `iw5mp.exe` (and any unrecognized executable, as a
    fail-safe) skips hook installation entirely while every exe-agnostic
    feature (XInput polling, the plugin loader — including the netcode-
    security plugin, which is meant to protect MP too) continues to run
    normally. This is groundwork, not full MP support: no gameplay hooks
    for `iw5mp.exe` exist yet at all (see `CLAUDE.md`'s MP scope
    decision — static RE first, opt-in-only live/injection work once it
    starts) — this change stops the wrong binary's hooks from ever being
    attempted, it doesn't add new ones.
18. **Real regression from item 17 above, caught the same day via a live MP
    test: the netcode-security plugin's hooks silently failed to install
    under `iw5mp.exe` for an entire real Team Deathmatch session.** A live
    `proxy_d3d9.log` capture showed every one of that plugin's signatures
    resolving correctly, followed by `MH_CreateHook = 2`
    (`MH_ERROR_NOT_INITIALIZED`) — `MH_Initialize()` had never run before
    the plugin's own hook-install attempt. Root cause: item 17's own gating
    change correctly stopped gameplay hooks from installing under
    `iw5mp.exe`, but that installer happened to be the only thing that
    called `MH_Initialize()` before the plugin loader runs — a real gap
    that change didn't anticipate, since it only reasoned about gameplay-
    hook safety, not this separate subsystem's shared MinHook dependency.
    Fixed by calling the already-idempotent `MH_Initialize()`
    unconditionally, before both the SP/MP branch and the plugin loader,
    guaranteeing it always runs regardless of which binary loaded the DLL.
    **Confirmed live** — a follow-up MP session's own `proxy_d3d9.log`
    showed the plugin's hooks installing successfully this time.
19. **Intermittent keyboard Sprint interruptions under Multiplayer, live-
    reported and root-caused the same day.** Direct report following a real
    MP session: Sprint randomly stops triggering, duration dropping to
    under a second, confirmed reproducing across two separate matches (TDM
    and Domination) and confirmed NOT a vanilla issue (only happens with
    this mod running). Traced to `SendPeriodicActivationNudgeX64`
    (`d3d9_hook.cpp`) — a real, repeating (every 2 seconds, for the whole
    session) focus-reassertion workaround (`WM_ACTIVATE`/`WM_SETFOCUS` plus
    real `SetForegroundWindow`/`SetActiveWindow`/`SetFocus`) that was built
    and only ever confirmed necessary for `iw5sp.exe`'s own "needs an
    initial click" issue, but was running completely unconditionally for
    both binaries since 2026-09-04 — MP's own tolerance for it was never
    investigated. A focus-reassertion firing while a key is actively held
    is a plausible, well-reasoned trigger for an engine to treat a
    continuous hold as a fresh press, matching the exact symptom. Fixed by
    gating the call to `iw5sp.exe` only, leaving SP's own already-proven
    behavior completely unchanged. **Confirmed live** — direct user
    confirmation ("fixed") after a follow-up MP session with this fix
    deployed.
20. **Weapon name in the interact/pickup hint now correctly shows as part
    of the substituted overlay, live-confirmed fixed.** Root-caused via a
    live diagnostic and direct user correction: the weapon name (e.g.
    "Model 1887") was never part of the raw text this project's own
    substitution intercepts — it draws through a completely separate
    native call. x86 has a dedicated, already-shipped mechanism for
    exactly this (issue #48/#49: remember the suppressed hint's font +
    row, treat the next matching call as its live continuation, append it
    to the same overlay) that was simply never ported to x64. Ported
    directly — **confirmed live** by the user immediately after deploy.
21. **Back(B)/Friends/GameSummary menu corner-hint position check added,**
    matching the same real precedent already fixed for Quit/Leaderboards
    (a bare text-content match with no position check can hijack a
    genuine navigable menu item sharing the same label). Investigated
    further after direct correction that this did NOT resolve the
    pause-menu flicker specifically — a live diagnostic was shipped
    instead of a second guess; root cause still open, see
    `re_notes/known_issues_x64.md`.
22. **Native cursor draw was never suppressed on x64 — the game's own
    default cursor rendered alongside this project's custom cursor
    overlay.** The 2026-09-13 custom-cursor port only ever resolved WHEN
    to draw our own cursor, never suppressed the native one. x86 has a
    real, dedicated fix (hooks the shared quad-draw primitive the native
    cursor draws through, scoped by exact return address to just that one
    call site) that was never ported. Ported directly. Build-verified,
    not yet independently re-confirmed live.
23. **CRITICAL: pressing B during active gameplay wrongly paused the
    game.** A same-day fix for B not fully closing the pause menu had
    introduced a stale-flag bug — once the flag it relied on went out of
    sync with the real game state, any later ordinary B press during
    normal, unpaused play would silently open the pause menu. Rather than
    patch that flag-tracking approach again (its second real regression
    in one day), both Start's pause open/close and B's menu-back handling
    now synthesize a real Escape keypress instead — the same native key a
    keyboard player already uses for both directions, with the engine's
    own state deciding what to do with it, removing the need for this
    project to track that state itself. See
    `re_notes/known_issues_x64.md` for the full trail.
24. **"Needs an initial click at launch" fixed at its real root cause,
    replacing the pause/unpause workaround.** Decompiling the native
    pause-toggle chain end to end found the real mechanism: unpausing
    calls a native function that force-releases every kbutton the
    engine's own bookkeeping thinks is still held — a genuine stuck-input
    bug, not a window-focus/activation issue. The fix now calls that
    native "release everything" sweep directly, once per level, with no
    pause menu ever opening or closing. See `re_notes/known_issues_x64.md`
    for the full decompile trail.
25. **CRITICAL: both SP and MP failed to launch at all.** A config-summary
    log line's own buffer had grown past its limit as new config keys
    were added incrementally over a long session — this UCRT fails fast
    on a real `sprintf_s` overflow rather than truncating, crashing
    inside `DllMain` before anything else in the mod could run. The same
    bug class as three earlier incidents this project has already hit and
    fixed. Buffer widened with a generous margin; live-confirmed both
    binaries load correctly again. See `re_notes/known_issues_x64.md`.
26. **The real "camera jumps on the first real input at launch" bug found
    and fixed — a native engine gap, not a controller issue at all.** The
    earlier kbutton-release fix (item 24) turned out to address a real
    but separate bug; this one happens on keyboard/mouse too, with no
    controller involved. Root cause: the native mouse-delta baseline is
    never seeded before the first real gameplay tick, so the first delta
    computed is the cursor's own raw screen position applied straight to
    the camera as one large jump. Fixed by seeding the same native
    baseline function ourselves, once per level, to the cursor's current
    position — zero visible movement, pure root-cause fix. **Live-confirmed**
    together with item 27 below. See `re_notes/known_issues_x64.md`.
27. **The missing third piece of the "needs an initial input at launch"
    fix, live-confirmed.** Direct testing found the kbutton-release and
    mouse-baseline fixes above, while both real and correct, didn't fully
    close the bug on their own — a real pause-button press was still
    needed. Added a real, message-queue-routed synthetic Escape keypress
    (the same already-proven-safe mechanism this project uses for other
    menu interaction) at the same per-level trigger point. **The combined
    fix (items 24, 26, and 27 together) is directly confirmed by the user
    ("seamless") — the entire "needs an initial input at launch" bug
    family (stuck kbuttons, camera jump, and the underlying missing input
    event) is resolved, replacing the 2026-09-04 pause/unpause automation
    workaround for good.** Real bonus: a separate, long-standing issue on
    Campaign/Survival mission **restart** (not just first level load) is
    also fixed by this same change, with zero extra code — the fix's own
    trigger detects any transition into live gameplay, not specifically
    "process just launched." See `re_notes/known_issues_x64.md`.

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
5. **Ko-fi donation link added.** `README.md` and the Nexus page copy
   (`nexus/description.md`/`description.bbcode.txt`) now carry a Support
   section with a Ko-fi button (https://ko-fi.com/officialk8) — purely
   optional, no gated content or features tied to it.
6. **`README.md` given an OpenAssetTools-style badge header** (icon+title,
   a shields.io badge row: release/version, real build status, last commit,
   license, Ko-fi) backed by a real new CI workflow (see Groundwork below) —
   deliberately no fabricated "checks passing" claim until that workflow
   actually existed.
7. **`README.md`'s "Known gaps" section restructured**: was a ~200-line
   wall of interleaved prose (including two already-resolved items still
   sitting in it); now a priority-sorted table (highest-impact/most-blocking
   first) with the full original investigation detail preserved underneath
   in per-item collapsible sections — no content removed, just no longer
   forced into the main reading flow. The top component table and "What
   works right now" table also restructured (the latter split into two
   properly single-purpose tables instead of two unrelated lists forced
   into misleading table rows) for the same reason.
8. **GitHub wiki is now version-controlled and auto-synced.** `wiki/*.md` in
   this repo is the new single source of truth for every wiki page (see
   `wiki/README.md`); a new CI workflow (Groundwork below) republishes it to
   the real GitHub wiki automatically on every push to `main` that touches
   it. Closes a real, previously-manual gap `CLAUDE.md`'s own "Keeping this
   file current" section already documented once (the wiki sitting stuck
   three releases behind with nothing tracking it). Seeded from the wiki's
   actual current live content; `Home.md`, `Known-Issues.md`,
   `Changelogs.md`, and `_Footer.md` were brought current to today's status
   in the same pass (they'd drifted to pre-NCP-redefinition wording); the
   remaining, lower-drift reference pages (Configuration, Compatibility,
   Controller Setup, Installation Guide, Troubleshooting, FAQs, Technical
   Documentation, Development Notes) were carried over as-is.
9. **MP parity release-cadence standard recorded in `README.md`'s own
   tables.** Since Multiplayer never gates a release (already true), it's
   now explicitly allowed to lag Campaign/Survival by 2-4 releases through
   the `v0.4.0-x64` beta milestone — the top component table's
   Release-gating column and the Multiplayer section's own scope paragraph
   both spell out the concrete standard instead of just "fast-follow."
   Explicitly conditional: revisited if Campaign/Survival itself reaches
   full completion before `v0.4.0-x64` ships. Full decision record:
   `CLAUDE.md`'s 2026-09-15 Version Timeline entry.

### Groundwork
1. **Two new CI workflows.** `.github/workflows/build.yml` — real MSVC
   builds (Release x64) of every component that actually ships to players
   (`proxy_d3d9`, `security/proxy_d3d9`, `security/tools/
   ncp_plugin_netcode_fixes`) on push/PR to `main`, backing `README.md`'s
   real build-status badge. Deliberately does not build `tools/iw5oat` —
   dev-only tooling, never shipped, with its own known premake-generated
   build fragility (see `re_notes/known_issues_x64.md`) out of scope for a
   basic buildability check. `.github/workflows/wiki-sync.yml` — publishes
   `wiki/*.md` to the real GitHub wiki on every push to `main` that touches
   it (see Documentation above). Requires "Read and write permissions" for
   the default `GITHUB_TOKEN` under this repo's Settings → Actions →
   General → Workflow permissions — a one-time manual repo setting, not
   something either workflow file can set for itself.
2. **`signature_scan.h`/`.cpp`** — the runtime AOB byte-pattern scanner this
   entire architecture is built on: parses wildcarded hex patterns, walks
   the game's own PE headers, fails loudly on a zero or ambiguous match.
3. New Ghidra tooling for x64 RE work, including raw-byte reference scanners
   for tracking down indirect references static analysis alone misses.
4. **Full RE trail for the x64 text-draw hook discovery**
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
5. **Real, project-wide GSC-extraction blocker found: OpenAssetTools'
   Unlinker cannot load any real-content zone from this install anymore.**
   Both the already-vendored v0.31.0 and a freshly-downloaded v0.33.0 (the
   latest public release) reproducibly crash (access violation, zero log
   output) loading `ny_harbor.ff`, `hamburg.ff`, and `so_stealth_prague.ff`
   — three different zones, three very different sizes — while a near-
   empty thin-loader zone loads cleanly with either version, ruling out a
   general tool-broken theory. Very likely the 2026-09-03 x64 recompile
   changed the zone/fastfile container format in a way neither current
   Unlinker release parses. This blocks GSC extraction from any real
   content zone project-wide, not just for the investigation that surfaced
   it — flagged here so a future session doesn't re-discover it the
   expensive way. See `re_notes/known_issues_x64.md`'s 2026-09-14 entry.
6. **Full `mw3ncp_config.ini` consumer audit — 120 keys across 15
   sections checked, not just for whether they parse (already confirmed
   arch-neutral), but whether anything on x64 actually acts on each
   value.** This is the exact bug shape today's own session already found
   repeatedly (motion blur, vibration, gyro-aim: config exists, gate
   reads fine, nothing was ever wired to consume it). Found exactly one
   genuine gap — an already-superseded, off-by-default glyph-substitution
   mechanism with no practical impact — and zero vestigial/dead keys.
   See `re_notes/known_issues_x64.md`'s 2026-09-14 entry.
7. **Predator Missile's post-fire guidance phase mapped further than
   either architecture has ever had it, real diagnostic shipped instead
   of a guess.** Independently re-confirmed the guidance script does zero
   per-frame input reads (steering is 100% native), then fully traced the
   real x64 native call chain via fresh decompile down to the exact
   struct offsets and angle-decode math. A cross-reference against the
   concurrent DPV/mortar/turret investigation found the flag this bug
   depends on is structurally distinct from the one that bug hinges on —
   real evidence the guidance phase may already receive controller look
   input correctly, just never provable statically. Shipped a safe,
   cheap, rate-limited diagnostic hook rather than a guessed fix on a
   feature that has never worked on any architecture. See
   `re_notes/known_issues.md` issue #30.
8. **`HudFontIdLoggingX64` diagnostic toggle** — an opt-in, read-only
   live-data-gathering tool for this project's two remaining genuinely
   blocked (not just unattempted) glyph-substitution gaps: buy-station
   (needs `Font_s.fontName`'s real x64 offset, unconfirmed after a
   dedicated static RE pass) and Sentry-Place (its reference string was
   never found anywhere in the x64 binary). Logs the resolved on-screen
   text plus the raw font pointer and a short hex dump at it, for every
   native text draw, so a real live session near either prompt captures
   genuine data instead of another round of static guessing. Mirrors
   x86's own established `HudFontIdLogging` technique. Default off in the
   shipped config template; turned on in this session's own live config
   so the next play session captures it automatically.
9. **`tools/iw5oat` (dev-only, never shipped): fixed a real crash-causing
   bug in `AssetInfoCollector` — two dependency-tracking functions were
   missing the same null-name guard `AssetLoader` already has, causing an
   implicit `std::string(nullptr)` construction that crashes inside
   `strlen`.** Found via a new, safe self-dump technique (a temporary
   in-process `MiniDumpWriteDump` handler, since live x64dbg attach is
   currently confirmed unsafe on this machine) rather than a live
   debugger. See `re_notes/x64_migration/fastfile_format_research.md`
   SS5.46 for the full trail — a second, different crash was found one
   layer deeper and remains open.
10. **`tools/iw5oat`: the 40+ round-old Unlinker "invalid block" parser
    bug root-caused and fixed.** Native decompile (`FUN_140096980`/
    `FUN_140096a50`) found this fork's own `MSSChannelMap`/
    `MSSSpeakerLevels` struct declarations — inherited from upstream
    OpenAssetTools' own x86-era assumptions, never updated for this
    fork's x64 target — read 416 bytes where the real native format
    reads exactly 64. Fixed in the tracked struct header; live-verified
    real progress unblocking every tracked zone. See
    `re_notes/x64_migration/fastfile_format_research.md` SS5.40.
11. **`tools/iw5oat`: forward-reference handling in the zone parser
    extended to several more sibling resolution functions**, converting
    hard `throw`s (aborting the entire zone load) to warn-and-null
    degradation, matching an already-proven-safe precedent verified
    against a full 41-zone sweep. One attempt was live-tested, found to
    segfault, and reverted the same session — a real example of this
    exact code area's own standing caution about rushed changes. See
    `re_notes/x64_migration/fastfile_format_research.md` SS5.41-5.42.
12. **`tools/iw5oat`: real short-read detection.** Every `Load()` call
    site now checks its own actual byte count against the requested
    size and throws a real, actionable exception instead of silently
    corrupting zone data on a truncated read. Permanent hardening, not
    specific to any one bug.
13. **`tools/iw5oat`: a real, VirtualQuery-backed pointer-validity check
    (`Utils/PointerSanity.h`) replaced an earlier bit-pattern
    canonical-address heuristic that had a demonstrated blind spot
    (a garbage value that still falls in the canonical 48-bit range),
    applied at every point in the fastfile parser and XModel export
    pipeline a corrupted zone reference could reach an unchecked
    dereference.** Verified via native `iw5sp.exe` cross-referencing
    (`WeaponDef`, `XModel`, `XModelSurfs`, `XSurface` structs all
    confirmed byte-exact against native, ruling out struct-shape as the
    cause) and live self-dump crash tracing. `common_survival.ff` went
    from crashing after 13 guard catches to producing 260,000+ lines of
    real output with full asset loading completing and real
    xmodel/xanim assets exporting successfully; no regression against
    `sp_dubai.ff`. A separate, distinct stack-buffer-overrun remains
    open further into the XModel/glTF export path — every plausible
    struct and the bone-weight counting logic both checked out correct,
    so this needs a live debugger trace, not more static analysis, to
    pin down.
14. **First real live GSC-VM read-access hook installed and shipped:
    `Hook_VmNotify` (`analog_input_hooks_x64.cpp`), following the
    2026-09-16 policy reversal that unblocked reading live GSC-VM state
    from the main mod.** A read-only, log-and-call-through diagnostic on
    the real x64 `VM_Notify` equivalent (`re_notes/x64_migration/
    gsc_vm_native_functions_x64.md`'s own definitive-confidence finding,
    reached by decoding the real `notify` bytecode opcode's handler
    inside the confirmed interpreter loop) — injects nothing, observes
    every real notify call (owner ID + interned string ID) as it fires
    during actual play. Unlike this codebase's own usercmd-pipeline
    functions, `VM_Notify`'s prologue confirmed a perfectly standard
    Microsoft x64 calling convention, so a plain C++ MinHook detour was
    safe here with no raw `__asm` trampoline needed. **Live-confirmed the
    same day**: 57 real fires captured during actual play, varied real
    data.
15. **`Hook_VmNotify` extended with real interned-string resolution
    (`TryResolveGscInternedString`), so the log shows readable GSC
    identifier text alongside the raw stringId, not just the raw ID.**
    Found via the same native cross-referencing methodology, applied to
    GSC's own compiler source this time: located `OP_GetString = 0x0A`/
    `OP_GetIString = 0x37` in `xensik/gsc-tool`'s own published opcode
    table, then decompiled their shared handler inside the already-
    confirmed `VM_Execute` interpreter loop, revealing a real refcounted
    string-pool table (`tableBase + (stringId << 4)`, 16 bytes/entry,
    refcount at offset 0) behind a fixed-location pointer variable
    resolved at runtime via the existing `SigScan::ResolveRipRelative`
    utility. The read itself reuses this codebase's own established
    SEH-guarded memory-read pattern (`Plugin_ReadMemory`'s convention),
    capped at 63 characters, and only ever logged if every byte up to
    the terminator is printable ASCII. **One honest, explicitly
    unconfirmed detail**: the exact byte offset of the string TEXT
    within each 16-byte entry (hypothesized as offset+4, "inline right
    after the refcount") was not independently proven via disassembly —
    a more complex bucketed free-callback cast some doubt without
    disproving it — deliberately left for live testing to confirm or
    refute safely, since a wrong guess just fails the printable-ASCII
    check and falls back to raw-ID-only logging rather than crashing or
    misbehaving. Build-verified, deployed; not yet live-tested. A real,
    practical first target once live data comes in: correlating notify
    traffic against known Survival actions to finally identify
    ready-up's real native trigger, the original open mystery from
    issue #5.

### Investigated, Not Yet Resolved
1. ~~**Fire and/or ADS fails — first live playtest of the x64 build,
   2026-09-13, confirmed a real bug, not just a "not yet live-tested" fix
   attempt.** Originally reported and investigated as sniper-class-
   specific (What's New item 3 above); the live test found it happens on
   the base pistol too, directly contradicting the sniper-specific
   framing that fix attempt was built around.~~ **RESOLVED, 2026-09-13,
   same session.** Real root cause found: `Hook_MovementTick` had an
   early-return meant to skip a no-op movement-byte write, but it exited
   the entire function — silently gating Fire, ADS, Reload, Weapnext,
   Melee, Lethal, Tactical, Jump, Interact, D-pad, CrouchProne, Scoreboard,
   the Pause-open poll, and `Rumble_Tick()` behind "is the stick currently
   centered," exactly the condition true the instant a player stops
   moving to aim and fire. x86's own equivalent design calls every one of
   these as fully independent functions with no such gating, confirming
   this was a pure x64 regression from fusing the per-tick functions
   together during the migration, unrelated to weapon class. Fixed by
   scoping the early-out to just the movement write. Live-confirmed
   2026-09-14. The `g_notifyBindDispatch` fix from item 3 below remains
   in place — additive, inert-if-unneeded, not the actual cause but not
   wrong to have shipped either. See `re_notes/known_issues_x64.md`
   issue #1 for the full trail.
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
4. **The native low-health "You are hurt, get to cover" TEXT is missing
   on the current retail x64 build — confirmed to be Activision's own
   regression, not caused by this project.** A live-reported symptom
   ("i think the x64 update removed the get to cover message present in
   x86") was root-caused through direct elimination: the red-screen
   vignette itself still shows correctly, ruling out the class of bug
   that broke both together on `-x86` (issue #100); this project's own
   text-draw substitution logic was checked and confirmed to never
   suppress unmatched text; and, decisively, the text stays missing with
   this project's own `d3d9.dll` removed entirely and the real system DLL
   loading in its place — a genuinely vanilla, zero-mod-code test. Nothing
   this project ships can be the cause of a symptom that reproduces with
   the project uninstalled. Likely connected to this same session's own
   independent finding that the 2026-09-03 update changed real zone/asset
   content, not just the executables (see `re_notes/known_issues_x64.md`'s
   matching round). A real candidate for a future RESTORED feature (not a
   fix) once UI work is next prioritized — this project already has the
   native health-ratio detection and the text-overlay infrastructure this
   would need. See `re_notes/known_issues_x64.md`'s newest round for the
   full trail.
