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
- [ ] CrouchProne (B) — confirm no regression now that a menu-active gate
      (`g_currentBPressTouchedMenuX64`) was added; B should still toggle
      real stance during gameplay and should NOT toggle stance when used
      to back out of an open menu.
- [ ] Survival ready-up (hold Y) — **new this session (2026-09-12), synthetic-
      F5 exception ported.** Confirm: holding Y for ~740ms between Survival
      waves readies up (same synthetic `WM_KEYDOWN`/`WM_KEYUP` F5 via
      `PostMessageA` x86 already ships); a quick tap or a hold that falls
      short of the threshold still switches weapons instead; confirm no
      observable side effect from the missing `IsInSurvivalMode()` gate
      outside Survival (expected none, but unconfirmed against real
      hardware on this binary).
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

- [ ] Native D-pad+A/B menu navigation — main menu: can you navigate and
      select with D-pad/A alone, no mouse/keyboard?
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
- [ ] Note: this does NOT yet mean glyph icons themselves draw — that's a
      separate, larger, not-yet-attempted RE task (the native text-draw
      hook). Don't expect to see "Press [A]"-style icons yet; this item
      is just confirming the underlying focus-detection signal is real.

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

- Gameplay glyph-icon text-draw hook (x86's `Hook_DrawGlyphText` x64
  equivalent) — the single missing piece blocking glyph icons, hint
  prompts, the F2/F3 editor, the custom cursor, and Auto-Mantle. A real,
  large RE task — resume when budget allows.
- Back's `+scores` scoreboard synthesis port to x64 — small, cheap, well-
  understood (the function already exists arch-clean on x86, just needs
  wiring in). Expected test outcome once ported: confirm it does nothing
  visible in SP (correct, matches confirmed real console behavior) — real
  value arrives once Multiplayer ships its own scoreboard.

## Not testable — investigated and found genuinely blocked, not implemented

- **Auto-Mantle (while sprinting)** — confirmed BLOCKED (2026-09-12), not a
  port that was skipped. Depends on `IsMantleHintCurrentlyShowing()`, which
  x86 detects by hooking the native hint *text-draw* call and pattern-
  matching the rendered string (`Hook_DrawGlyphText`) — not a native engine
  flag this project reads directly. `Hook_DrawGlyphText`'s x64 equivalent
  doesn't exist yet (the same separate, larger RE task blocking gameplay
  glyph icons generally). Nothing to test here until that's ported — see
  `re_notes/known_issues_x64.md` issue #1 for the full dependency trace.

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
