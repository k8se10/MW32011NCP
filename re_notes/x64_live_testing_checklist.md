# x64 Live-Testing Checklist

**A living document — grows as new build-verified fixes land, shrinks as
they get live-confirmed.** Methodology, direct instruction (2026-09-12):
"our method this time is rapid push for parity then mass testing through
everything done" — this file is the single place tracking exactly what
still needs a real playtest before the `-x64` release gate can close. Every
item here is **build-verified (x64 `/t:Rebuild`, 0 errors, `dumpbin`-
confirmed x64, deployed)** but has never been exercised against the actual
running game. Check an item off only after a direct playtest confirms it —
not on build success alone.

**How to use this**: play a real session (Campaign or Survival as noted per
item), work down the list, mark each `[x]` with a one-line result. If
something's broken, leave it unchecked and note what actually happened —
that becomes a real bug report, not just "untested." Full technical trail
for any item lives in `re_notes/known_issues_x64.md` issue #1.

---

## Launch (check this FIRST, before anything else on this list)

- [ ] **Game actually launches** — 2026-09-13, a live report ("game doesn't
      launch") was root-caused to a guaranteed `sprintf_s` buffer overflow
      (`0xc0000409`, same bug class as `PATCHNOTES.md`'s Fixed item 1 from
      2026-09-05) in a log line added earlier the same day for the native
      text-draw hook. It was 100% reproducible on every launch, not
      intermittent. Fixed, plus a full sweep of every similar call site
      added that day (`re_notes/known_issues_x64.md`'s 2026-09-13 "game
      doesn't launch" round has the full trail). Build-verified only —
      **this specific fix has not itself been confirmed live yet**, since
      the crash itself is what was blocking every other item on this
      checklist from being testable at all. Confirm the game reaches a
      normal main-menu session with no crash before testing anything else
      below.

## Core gameplay (Campaign/Survival, `iw5sp.exe`)

- [ ] Sprint (L3) — **mechanism changed 2026-09-12** (was raw `pm_flags`-
      forcing, now a real kbutton). Confirm: sprint engages/disengages
      correctly, the native duration/recovery timer applies (no more
      infinite sprint), Extreme Conditioning's perk override applies on a
      mission that sets it, and the rising-edge "stand up from crouch/prone
      on sprint" behavior fires correctly.
- [ ] D-pad actionslot (all four directions) — confirm each of the 4
      real, loadout-driven actions fires correctly, and confirm no
      double-fire now that a menu-active gate was added alongside the new
      menu-navigation work.
- [ ] D-pad Left's squadmate-call-in fix (Survival) — confirm the
      synthetic-key path actually calls in an AI squadmate; confirm no
      regression to turret call-ins or the other 3 D-pad directions.
- [ ] Jump auto-stand — confirm jumping while crouched/prone stands the
      player up first, matching console behavior.
- [ ] Sniper-class Fire/ADS fix attempt — confirm Fire and ADS both work
      on sniper-class weapons specifically (the original bug: worked on
      other weapon classes, failed on snipers).
- [ ] Killstreak: Predator Missile launch (Survival buy-station, 2500,
      `remote_missile`) — **strengthened 2026-09-13, still not live-tested.**
      A dedicated static-RE pass confirmed controller Fire-down provably
      sends the byte-identical `"n 1"` reliable-command string a real x64
      keyboard `+attack` press already sends natively (same function,
      `FUN_14007fc00`, same argument, by construction — not just "the same
      mechanism as a different bug's fix" as previously documented). Confirm
      live: aim the missile camera (already known-working, shares the
      generic UAV-control system), press Fire, missile actually launches.
      If it does NOT launch despite this confirmation, the next RE angle is
      tracing `FUN_14007fb30`'s ring-buffer consumer server-side (who reads
      `"n %i"` off the queue and what it does with it) rather than
      re-litigating whether `"1"` is the right index — that part is now
      confirmed two independent ways. See `known_issues_x64.md`'s
      2026-09-13 Predator Missile round and parity audit row #16.
- [ ] CrouchProne (B) — confirm no regression now that a menu-active gate
      (`g_currentBPressTouchedMenuX64`) was added; B should still toggle
      real stance during gameplay and should NOT toggle stance when used
      to back out of an open menu.
- [ ] Survival ready-up (hold Y) — **new this session (2026-09-12), synthetic-
      F5 exception ported; `IsInSurvivalMode()` gate closed 2026-09-13.**
      Confirm: holding Y for ~740ms between Survival waves readies up (same
      synthetic `WM_KEYDOWN`/`WM_KEYUP` F5 via `PostMessageA` x86 already
      ships); a quick tap or a hold that falls short of the threshold still
      switches weapons instead; confirm `IsInSurvivalModeX64()` (real
      `mapname` dvar read, wired 2026-09-13) actually gates the fire
      correctly — no observable side effect outside Survival, AND ready-up
      still fires correctly during an actual Survival match (a wrong gate
      read would now silently suppress it entirely, the opposite failure
      mode from before this fix).
- [ ] ADS zoom-aware look-slowdown (`AdsSlowdownStrength`/`Baseline`/
      `AdsCloseRangeSlowdownStrength`) — **new this session (2026-09-13),
      closes parity audit row #3.** Confirm: aiming down sights with a zoom
      optic (ACOG/sniper) slows controller look sensitivity proportionally
      to the live FOV ratio, matching `-x86`'s confirmed-correct feel
      (issue #8/#44); confirm low-zoom weapons (pistols/red-dots, FOV ratio
      near 1.0) still get the close-range taper's extra slowdown; confirm
      no look-direction inversion at any `AdsSlowdownStrength` value (the
      exact x86 bug issue #8 root-caused and fixed, now riding the same
      power-curve formula on x64).
- [ ] Hold Breath (L3 while ADS'd, sniper-class) — **new this session
      (2026-09-12), ported (parity audit item #23, was previously
      completely absent).** Confirm: holding the Sprint bind while ADS'd
      on a sniper-class weapon produces the real sway-reduction/steadier-
      aim effect and accuracy degrades once breath runs out (same as
      `-x86`'s confirmed-live behavior); confirm the kbutton correctly
      releases on letting go of the bind or breaking ADS (watch
      specifically for any sign of x86's own "active flag latches, never
      clears" symptom recurring here, even though x64's struct is
      structurally a separate, dedicated kbutton_t and shouldn't need
      x86's own debounce/force-clear workaround); confirm ordinary
      hip-fire Sprint (not ADS'd) is unaffected.
- [ ] Back (scoreboard/`+scores`) — **new this session (2026-09-13),
      ported (parity audit row 30).** UNLIKE every other item on this list,
      the expected, CORRECT outcome is that holding Back does **nothing
      visible** in Campaign/Survival — confirmed by direct Xbox 360 console
      testimony (`known_issues.md` issue #28) that no scoreboard UI exists
      in SP at all, on any platform. This test is confirming the port is a
      correct no-op, not confirming a visible feature works — do not treat
      "nothing happened" as a failure here. Real value only confirmable
      once Multiplayer ships its own scoreboard. Watch for any unexpected
      side effect instead (a stuck TAB key state, interference with another
      control) — that WOULD be a real bug.

## Menu & UI navigation (new this session)

- [x] Native D-pad+A/B menu navigation — main menu: **CONFIRMED 2026-09-13**
      ("menus are on par from what i saw main menu wise").
- [ ] Native menu navigation — pause menu: same check, in-game.
- [ ] Native menu navigation — Options screen two-pane drill-down.
- [ ] Native menu navigation — Survival buy-station/armory lists,
      including slider value adjustment.
- [ ] B — menu-back (ESC-forward): backs out of an open menu one step,
      hardcoded to physical B regardless of layout preset.
- [ ] Custom Options screen — **real native trigger** (new this session):
      approach the pause/Campaign/Spec-Ops menu's own "Options" button
      with a controller and press A — does it open the custom screen
      without needing the LB+RB chord? Watch `proxy_d3d9.log` for
      `[x64-menufocus]`/`[x64-optmenu-realtrigger]` lines.
- [ ] Custom Options screen — panel/blur/list draw correctly, D-pad/A/B
      navigate and select rows, closing returns cleanly to the native
      menu with no regression to normal D-pad/A/B gameplay input after.
- [ ] Custom Options screen — LB+RB chord fallback still works if the
      real trigger doesn't fire for some reason.
- [x] **Custom Options screen — Controller and Custom Binds tabs only
      (KNOWN, not a bug to report)**: parity audit row #64 (2026-09-13)
      confirms the other 7 vanilla tabs (Look/Video/Audio/Voice/
      AdvancedVideo/Movement/Actions) are UI-only on x64 right now — every
      row displays a stub/unbound value and every edit is silently
      discarded, because `real_settings.cpp`'s actual dvar/keybind
      read+write layer is x86-only (deliberately stubbed to a safe no-op
      on x64 since 2026-09-04 to prevent a crash, not an oversight). Expect
      this exact symptom; it's a real, already-documented open item, not
      something to file as a new bug. Confirm it looks EXACTLY like that
      (silently inert, no crash, no visible error) — anything worse (a
      crash, a setting that appears to apply but doesn't actually take
      effect in-game) would be a new, different finding worth reporting.

## Vibration/rumble (new this session)

- [ ] Fire rumble — confirm it fires on weapon fire.
- [ ] Damage rumble — confirm it fires when taking damage.
- [ ] Confirm no regression to vanilla keyboard/mouse play (rumble should
      only ever add behavior, never interfere with unmodified input).

## DualSense gyro-aim (new this session — requires real DualSense hardware, preview/WIP on x86 too)

- [ ] With `[Gyro] Enabled=1`, confirm tilting the controller nudges the
      camera (yaw from Z-axis, pitch from X-axis per the current mapping —
      note if this feels backwards/wrong, the axis mapping is explicitly
      unverified on real hardware, copied from x86's own unconfirmed guess).
- [ ] `GyroOnlyWhileAds=1` — confirm gyro contribution only applies while
      ADS'd; `=0` — confirm it applies at all times.
- [ ] `GyroInvertYaw`/`GyroInvertPitch`/`InvertLook` — confirm each flips
      its respective axis correctly.
- [ ] Confirm gyro stacks additively with stick look (moving the right
      stick AND tilting the controller at once should combine, not fight).
- [ ] USB DualSense specifically — confirm gyro works at all (issue #76:
      never independently confirmed by a second tester, even on x86).

## Visual-enhancement suite (new this session — off by default, opt in via `mw3ncp_config.ini` to test)

- [ ] `InternalRenderScalePercent` — confirm it actually scales real GPU
      render cost (same test x86 used: framerate delta at 100% vs. a
      higher percentage).
- [ ] FSR 1.0 RCAS sharpening — confirm it activates in real gameplay and
      doesn't crash on loading screens or "quit to menu" (the exact
      crash class x86's own issues #103/#104 document — this is the
      highest-risk item on this whole list, test it deliberately).
- [ ] Camera motion blur — confirm it activates during real look-input
      movement and doesn't crash during exclusion-zone/killcam sequences
      (x86's own issue #96/#97 crash class).
- [ ] Confirm all three gates (menu-active, `clcState`, in-level) actually
      prevent the passes from running in menus/loading screens — the
      whole reason this took two prior blocked attempts.

## Menu-focus/glyph tracking (new this session)

- [ ] Watch `proxy_d3d9.log` for `[x64-menufocus-diag]` lines during real
      menu navigation — confirm `haveFocus`/`group`/`index` actually
      track real, changing focus state as you move between menu items
      (not stuck at `realGroup="" realIndex=-1` the way it was before
      this session's port).
- [ ] Note: this does NOT mean the gameplay-hint "Press [A]"-style icons
      draw — the native text-draw hook (see the next section) is a
      completely separate system, and as of 2026-09-13 only does detection,
      not substitution, for most hints. This item is just confirming the
      underlying focus-detection signal is real. (The highlighted-item
      A-glyph and F2/F3 editor below are a THIRD, separate system that DOES
      now draw off this same signal — see their own new section.)

## Highlighted-item A-glyph + F2/F3 glyph-position editor (new 2026-09-13)

**Separate system from both sections above** — this is the manually-
calibrated menu-list-highlight overlay (`kManualGlyphPositions`,
`ResetMenuListItemOrdinalForFrame`), not the native-hint text-draw hook.
Both of its consumers previously always saw "no focus" on x64 (dead stub);
now routed to the same real x64 itemDef-array walk the section above
confirms is live. See `re_notes/known_issues_x64.md` issue #1 and
`re_notes/x64_feature_parity_audit.md` rows #35/#36 for the full trail.

- [ ] Highlighted-item A-glyph: navigate to any menu list this project
      already has a calibrated `kManualGlyphPositions` entry for (e.g. the
      pause menu, `PAUSE_LIST`) and confirm an A-button icon now actually
      draws near the currently-highlighted item — previously silently never
      drew at all on x64. Confirm it tracks correctly as you move the
      highlight up/down the list, and does not visibly lag/snap onto the
      wrong item during a fast navigation burst (the 4-frame debounce should
      prevent this, same as `-x86`).
- [ ] Watch `proxy_d3d9.log` for `[manual-glyph-diag]` lines while doing the
      above — confirm `haveFocus=1`/`havePos=1` with a real, changing
      `realGroup`/`realIndex`, not permanently `haveFocus=0`.
- [ ] F2/F3 in-game glyph-position editor: enable `[Debug]
      GlyphPositionEditMode=1` in `mw3ncp_config.ini`, launch, press F2 in
      any menu. Confirm the on-screen status readout switches from
      "no real item focused | fallback: ..." to a real
      `focus=<group> d<depth> i<index>/<siblingCount>` line that updates as
      you move the highlight.
- [ ] With the editor active, click-drag the ICON and TEXT handles for a
      focused item and confirm both move independently and the on-screen
      coordinate readout updates live.
- [ ] Press F3 and confirm `exported_glyph_positions.txt` (next to the
      deployed `d3d9.dll`) contains a real, non-placeholder entry (not
      `0.0f, 0.0f`) for whatever item was focused/dragged.
- [ ] Confirm no regression to the shipped manual A-glyph draw while the F2
      editor is OFF (`glyphPositionEditMode=0`, the default) — this whole
      feature is opt-in and must not change default behavior.

## Native text-draw hook / Mantle-hint detection (new 2026-09-13)

- [ ] Watch `proxy_d3d9.log` for `[x64-drawtext] Text-draw hook fired` during
      any real gameplay/menu session — confirms the signature-scan ->
      MinHook-install -> detour pipeline works on this call site (should
      fire constantly, any time HUD/hint/menu text is drawn).
- [ ] Confirm no visible change to ANY on-screen text as a result of this
      hook existing — it's designed as a zero-behavior-change passthrough
      plus a read-only detection layer; any visible text difference (wrong
      position, missing text, garbled text) on any screen is a real
      regression to report, not expected behavior.
- [ ] Confirm `[x64-drawtext] Localized-string lookup resolved` appears in
      the log at startup (the direct-call resolve for Mantle-hint
      detection's own dependency) — if it's missing/FATAL instead, Mantle
      detection silently never works even though the hook itself is fine.
- [ ] Watch for the one-shot `[x64-drawtext] Mantle-hint structural match
      confirmed` log line specifically while standing at a real mantleable
      ledge — this is the direct confirmation that
      `IsMantleHintCurrentlyShowingX64()` actually goes true for a real
      ledge (not just that the hook fires at all). If the hook-fired line
      appears but this one never does anywhere in a session that definitely
      showed a mantle prompt, that's a real bug to report (the structural
      match itself, or the live-resolved template, is wrong).

## Real glyph-icon visual SUBSTITUTION (new 2026-09-13, same day follow-up)

**Nine cases now visually substitute (Mantle, Pickup/Swap/PickupHealth,
Throwback, Reload/low-ammo, and menu corner hints Back/Friends/Quit/
Leaderboards/Game-Summary) — buy-station, Survival ready-up, Sentry-Place
still render native, unmodified text (see "Not testable" section below for
the honest list of what's NOT covered and why).**

**POSITION FIX SHIPPED, same day, NOT yet live-tested (build-verified only
— see `known_issues_x64.md`'s newest 2026-09-13 round for the full root-
cause trail).** Root cause: `Hook_DrawTextX64`'s own x/y were pre-transform,
draw-context-local coordinates, not the final screen-pixel position they
were assumed to be — fixed via `ComputeRealDrawPositionX64`, which now
calls the real native position transform directly. **Watch for the new
one-shot `[x64-drawtext-pos] raw=(...) alignH=... alignV=... ->
REAL-TRANSFORM=(...)` log line** (or `RAW-FALLBACK(sig-unresolved-or-
raised)` if the fix's own two new signatures failed to resolve — if you
see the fallback variant, the position bug below is NOT fixed this run)
on the very first substituted hint of any kind — confirms whether the fix
is actually active before judging on-screen placement.

- [ ] Reach a real mantleable ledge — **was BROKEN (live-confirmed
      2026-09-13, native hint text suppression worked but no substituted
      icon was visible — position bug, Mantle uses `centerOnScreen=false`
      so a bad coordinate pushed it off-frame entirely). Position fix
      shipped same day, re-test needed** — confirm the icon is now visible
      near the real ledge/arrow sprite (exact pixel alignment not expected
      yet, no nudge constants ported — see the "HONEST CAVEAT" in this
      hook's own header comment).
- [ ] Pick up a new weapon / swap weapons / pick up health — **was BROKEN
      (live-confirmed 2026-09-13, text rendered at the very top of the
      screen instead of near the real prompt). Position fix shipped same
      day, re-test needed** — confirm text now renders in the correct
      general screen area (near the weapon HUD row), not pixel-perfect yet.
- [ ] Throw back an enemy grenade — shares the identical position pipeline
      as the confirmed-broken-then-fixed cases above — re-test alongside
      them now that the position fix has shipped.
- [ ] **Reload with low ammo** — **was BROKEN (live-confirmed 2026-09-13,
      same top-of-screen symptom as Interact). Position fix shipped same
      day, re-test needed.** (The structural-reachability question this
      item used to describe — whether Reload's text is even visible to
      this hook at all — was already resolved and correct; this was
      always the position bug, not a reachability issue.)
- [ ] **Menu corner hints (Back/Friends)** — open a menu that shows a
      corner-hint row and confirm Back/Friends now draw as real
      controller-glyph icons instead of native `"^2ESC^7"`/`"^2F^7"` text.
      Not previously believed attempted this pass; turned out to already be
      covered by the same draw-hook investigation that fixed Reload.
- [ ] **Quit (main menu)** — open the main menu's Quit confirmation and
      confirm the native "Quit" corner legend is replaced with a real B-glyph
      icon (`RequestMenuHintOverlay`, no "^N...^7" span needed for this one
      since it's a bare literal-text match). Watch for `[x64-drawtext] Menu
      corner-hint structural match confirmed` — this specific case does NOT
      log a `kind=` for Quit/Leaderboards (only the Back/Friends/GameSummary
      span-gated block logs `kind=`), so absence of native "Quit" text plus
      presence of the B icon is the real confirmation. **Also confirm the
      Special Ops hub's own separate all-caps "QUIT" item is UNAFFECTED**
      (case-sensitive match, same as x86, deliberately excludes it).
- [ ] **Leaderboards (main menu)** — confirm the native "Leaderboards ^2Right
      Mouse^7/^2F1^7" corner hint is replaced with a real Back/Select-button
      icon (`PhysicalInput::Back`, distinct from Quit's B/ESC icon).
- [ ] **Game Summary (post-match screen)** — confirm the native "Game
      Summary ^2G^7" hint is replaced with a real X-glyph icon. Watch for
      `kind=GameSummary` in the `[x64-drawtext] Menu corner-hint structural
      match confirmed` log line.
- [ ] **Friends-suppression (Special Ops mode-picker / Friends list)** — new
      this session (2026-09-13, menu-hint parity follow-up), genuinely
      untested. Open the Special Ops hub, enter the Chaos/Mission/Survival
      mode-picker (and the on-disk/DLC content-picker one level deeper) and
      confirm the native "Friends ^2F^7" hint does NOT show (this project's
      own Back hint should show instead/alongside). Separately, open the
      Friends list itself and confirm the native "Friends" hint is also
      suppressed there. **This is the least-confident item in this whole
      section** — x86's own version of this exact logic (`IsInsideSpecOpsNestedModal`)
      went through FOUR versions before landing on the current sticky-state
      algorithm (see that function's own header comment,
      `analog_input_hooks.cpp`) after two earlier attempts each had a real,
      live-reported false positive/negative; the x64 port is a faithful
      byte-for-byte port of that same v4 algorithm, but has never itself
      been live-exercised, so the SAME class of edge case x86 needed two
      rounds to find could plausibly still be lurking here. If Friends
      shows when it shouldn't (or Back fails to show when Friends is
      correctly suppressed), report exactly which screen/list you were on.
- [ ] **Position/alignment is UNVERIFIED and UNTUNED** — no empirical nudge
      constants were ported for x64 (x86's own alignment took several rounds
      of live-tested correction). Expect the icon/text to potentially be
      noticeably offset from where it "should" sit relative to the mantle
      arrow sprite/pickup prompt/corner-hint row; report roughly how far off
      and in which direction so a future pass can add the equivalent nudge
      constants.
- [ ] Confirm buy-station's "Hold F to use Weapon Armory," Survival's
      ready-up prompt, and turret placement all STILL render as plain native
      text, completely unchanged — these are explicitly NOT covered by this
      pass (see "Not testable" below for why) and should show zero visible
      difference from before.

## Auto-Mantle's real `+gostand`-forcing feature (new 2026-09-13, ships OFF by default)

- [ ] Set `[Movement] AutoMantleEnabled=1` in `mw3ncp_config.ini` first --
      this feature is strictly opt-in, confirm it does NOTHING at the
      default `=0` before testing the enabled path.
- [ ] Watch `proxy_d3d9.log` for `[automantle-diag-x64] sprintActive=.. mantleHintShowing=..`
      while sprinting toward a real mantleable ledge -- confirm
      `sprintActive=1` and `mantleHintShowing=1` are BOTH observed at the
      same time at some point (if `mantleHintShowing` never goes true while
      `sprintActive=1`, that points at the native hint not rendering during
      a real sprint state, same open question x86's own diagnostic was
      built to catch -- see analog_input_hooks.cpp's own comment).
- [ ] With both true and the left stick held forward (within
      `AutoMantleForwardConeDegrees`/`AutoMantleMinStickMagnitude` of
      straight ahead), confirm the player actually mantles the ledge
      without pressing Jump.
- [ ] **Regression check, same class x86 hit live 2026-08-03** ("the sprint
      mantle is borked... it jumps always when trying to sprint"): confirm
      sprinting forward with NOTHING mantleable nearby does NOT cause
      repeated/spammed jumping. This is the single highest-risk regression
      for this feature -- test it deliberately, not just the happy path.
- [ ] Confirm the ~750ms cooldown holds -- shouldn't be possible to trigger
      two mantles from one continuous ledge-hint-showing window faster than
      that.
- [ ] Confirm vanilla keyboard/mouse play is unaffected with the feature
      enabled (this project's own strict-additive standard, CLAUDE.md SS7).

## Custom mouse cursor overlay (new 2026-09-13, closes parity audit row #37)

- [ ] With keyboard/mouse as the active input method, open the main menu
      and confirm this mod's own custom cursor (`cursor_arrow` texture)
      draws over glyph icons and other overlay elements, tracking real
      mouse movement correctly at the current resolution (test at 1920x1080
      and at least one non-16:9 resolution, e.g. 800x600, per the position-
      scaling history this feature has -- see `known_issues.md` issue #52).
- [ ] Confirm the cursor correctly disappears the moment a controller is
      used (within ~300ms, per `IsControllerActiveInputMethod`'s recency
      window) and reappears once real mouse movement resumes.
- [ ] Confirm the cursor does NOT draw during ordinary active gameplay with
      no menu open (the real regression class issue #55 originally caught)
      -- this depends on the newly-fixed `IsMenuActiveX64_Exported()`
      branch, not just the two resolved gate addresses.
- [ ] Confirm the cursor DOES draw correctly inside Survival buy-station/
      armory menus and the pause menu (states where x86's own uiState
      exclusion list intentionally allows it).
- [ ] Watch `proxy_d3d9.log` for `[x64-cursor] Cursor gate resolved:
      visFlag=... uiState=...` at startup (confirms both signatures
      resolved) and `[cursor-gate-diag]`/`[cursor-pos-diag]` lines during
      play (confirms real values are changing sensibly, not stuck).
- [ ] If the cursor never appears at all, check for `[x64-cursor] FATAL`
      in the log first -- that means `kCursorGateSignature` failed to
      resolve or match this exact build (a real signature-scan miss, not a
      gating bug) and should be reported as its own issue, not conflated
      with the gating logic above.

## Plugin API / security component

- [ ] Confirm `proxy_d3d9.log` shows `mw32011nsp_security.dll` greenlit-
      loading regardless of `[Plugins] Enabled` (already confirmed once
      earlier this session — re-confirm after the latest rebuild/merge).
- [ ] Confirm the P2P fix (Finding 1, `iw5sp.exe`) actually installs —
      watch for `[nsp-p2p-fix] Installed` in the log (not the earlier
      `SteamNetworking() returned null` failure the retry-loop fix this
      session was meant to resolve).
- [ ] RGB Text example plugin — confirm it still loads and renders when
      manually opted in (`[Plugins] Enabled=1`), unaffected by the
      security-plugin/merge work.

## Not yet dispatched / paused (session token-budget constraint, 2026-09-12)

Not testable yet — no real investigation happened, work was paused before
starting rather than found blocked. Genuinely open, not attempted:

- **Gameplay glyph-icon text-draw hook — RESOLVED 2026-09-13, see its own
  new section above.** The hook itself and Mantle-hint detection are now
  build-verified and live-testable (see the "Native text-draw hook /
  Mantle-hint detection" section above). **Visual glyph-icon SUBSTITUTION —
  PARTIALLY shipped later the same day**, see the new "Real glyph-icon
  visual SUBSTITUTION" section above: Mantle/Pickup-Swap-PickupHealth/
  Throwback grenade/Reload/menu corner hints (Back/Friends/Quit/Leaderboards/
  Game-Summary, the last three plus the Special-Ops/Friends-suppression
  logic added in a THIRD same-day follow-up pass) now visually substitute
  (nine cases total); buy-station and Survival ready-up were investigated in
  depth the same day (font-name filtering was attempted and genuinely could
  not be confirmed, see the "Not testable" section below); Sentry-Place
  remains genuinely blocked for its own separate reason (see below).
  Auto-Mantle's own `+gostand`-forcing feature shipped later the same day --
  see its own new checklist section above, no longer blocked.
- Back's `+scores` scoreboard synthesis port to x64 — small, cheap, well-
  understood (the function already exists arch-clean on x86, just needs
  wiring in). Expected test outcome once ported: confirm it does nothing
  visible in SP (correct, matches confirmed real console behavior) — real
  value arrives once Multiplayer ships its own scoreboard.

## Not testable — investigated and found genuinely blocked, not implemented

- **Buy-station glyph** ("Hold F to use Weapon Armory") and **Survival
  ready-up glyph** (F5) — genuinely blocked, not skipped: neither has a known
  reference-key template even on x86 (x86 gates them via `IsGameplayHintFont`
  font-name filtering instead). A dedicated 2026-09-13 follow-up session
  spent real, multi-angle effort trying to independently confirm x64's
  `Font_s.fontName` offset via decompile (string scan, full load/cache-chain
  trace, audit of every known payload consumer) and could NOT confirm it —
  a genuine negative result, not a skipped step; per this project's own
  "no unconfirmed-offset OOB read" standard, no fontName-gated substitution
  was wired. See `re_notes/x64_migration/drawtext_hook_x64.md`'s "Stage (d)"
  and `known_issues_x64.md`'s matching 2026-09-13 update for the full trail.
- **Sentry-Place (turret placement) glyph** — its own reference string
  (`"SENTRY_PLACE"`) was searched for across the ENTIRE x64 binary and found
  zero times. Genuinely unresolved, not a priority choice.

(Reload and menu corner hints (Back/Friends), previously listed here as
blocked/not-attempted, were both found to already be reachable through the
same hooked draw function and moved to the live-testable "Real glyph-icon
visual SUBSTITUTION" section above, 2026-09-13.)

(Auto-Mantle, the prior sole entry here, moved to its own live-testable
checklist section above 2026-09-13 once both its detection dependency and
its actual `+gostand`-forcing feature shipped.)

## Multiplayer (`iw5mp.exe`) — separate track, not part of this release gate

Nothing here is live-testable yet — MP work is static-RE-only per this
project's own locked ordering policy (no live process attach authorized
yet). Not part of this checklist until that changes; see
`re_notes/iw5mp_x64.md` for MP's own status.

---

## Completed / removed from this list

*(Move items here once live-confirmed, with the date and what was actually
observed — don't just delete them, keep the record.)*

- None yet.
