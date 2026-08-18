# Patch Notes

All notable changes to the project, per release. See `re_notes/known_issues.md` for the
full, actively-tracked issue list and `re_notes/iw5sp.md` for the underlying
reverse-engineering trail behind each entry.

---

## Unreleased

---

## v0.3.3 — Alpha (2026-08-18) — Extended-session stutter fixed; vibration and co-op rumble fixes; DualSense Bluetooth preview (broken)

**Summary:** The headline fix: real, extended-session performance stuttering — three
separate, evidence-backed causes found and fixed (a log file that was never trimmed
between launches, a controller-poll thread that never stopped rescanning every
device every tick, and a diagnostic log whose dedup logic didn't actually dedup) —
user-confirmed "much better" afterward, though one residual dip (looking toward
enemies) is parked, not resolved, pending real GPU-side profiling. Also fixes
vibration getting stuck on indefinitely across a pause/menu, and a real co-op bug
where damage-rumble could trigger off a teammate's hits instead of your own. Ships
a first, PREVIEW/WIP pass at DualSense Bluetooth support — **stick input does not
work correctly over Bluetooth, confirmed live; USB has not been independently
confirmed working either.** See below for the full itemized list, including menu
glyph calibration work and new dev-only diagnostic tooling.

### What's New
1. **PREVIEW/WIP, KNOWN BROKEN over Bluetooth: DualSense rumble output + input-parsing
  groundwork (issue #76 follow-up).** Neither existed before this pass. Rumble output
  is new for BOTH USB and Bluetooth -- previously `Controller_SetVibration` silently
  no-op'd for any DualSense; Bluetooth's own output framing (CRC32-checksummed,
  ported from Sony's own mainline Linux kernel driver rather than an unverifiable
  community table) is real, implemented code. **Bluetooth STICK INPUT does not
  currently work correctly on real hardware, confirmed live -- do not rely on it for
  gameplay over Bluetooth.** **USB has NOT been independently confirmed working
  either** -- untouched by this session's Bluetooth-specific bugs/fixes, but per
  `re_notes/known_issues.md` issue #76's own status line, the whole DualSense backend
  (USB included) remains "implemented, not live-tested against real hardware" --
  no live tester has confirmed USB input/rumble actually work yet. See "Investigated,
  Not Yet Resolved" below for the full Bluetooth account.
2. **`kManualGlyphPositions` (issue #51) updated with real dragged data from a live
  in-game click-and-drag calibration session covering every menu screen reachable
  from the main menu** (not requiring an active mission/match), using the editor
  above: `game_select_button` (main menu) recalibrated with real positions;
  `OPTIONS_LIST` extended from 3 to all 7 real tabs (previously a known gap); two
  entirely new screens added for `SO_LEVELS_BUTTON_LIST` at depth=2 (16 items) and
  depth=3 (14 of 16 items -- 2 still uncalibrated); `SPECOPS_BUTTON_LIST` and
  `SWF_BUTTON_LIST` confirmed correct as-is at additional depths (no table change
  needed). `OPTIONS_LIST` kept at depth=-1 (reachable from more than one navigation
  depth depending on entry point) rather than narrowed to the one depth it happened
  to be captured at -- `SO_LEVELS_BUTTON_LIST`'s two new depth-specific entries stay
  depth-gated on purpose, since they're genuinely different real screens sharing one
  group name, not the same screen reached differently. Genuinely in-game-only
  screens (`PAUSE_LIST`, Survival's `WEAPON_POPUP`, `RESUME_POPUP`) were NOT part of
  this pass -- their existing entries predate this tool and still need a dedicated
  in-mission calibration session to re-verify. Menu-screen calibration is otherwise
  considered largely complete as of this pass. Two confirmed gaps remain, documented
  in `kManualGlyphPositions`' own comment: a Callsign/emblem-card selection screen
  ("cardIcon"/"cardTitle" groups, visited but never dragged) and Survival's DLC map
  select screen, which is a genuine architectural gap -- none of this project's three
  selection-tracking mechanisms detect a focused item there at all, confirmed live,
  not just unconfirmed. The Callsign/emblem-card selection screen ("cardIcon"/
  "cardTitle" groups) is explicitly disabled for now rather than left to silently
  draw nothing by omission: it needs the icon positioned relative to each selected
  item's own real on-screen box (bottom-right corner for emblems, right of the box
  for the callsign name list), which this project can't do yet -- no known way to
  read a real itemDef's box/rect, only its name and flags. **Also resolved a longstanding, explicitly-flagged gap**: the
  "New Game overwrite" confirmation popup (`SWF_COMMON_DESC_RESIZE_POPUP_NAME`) has
  always used a meaningfully different position than the quit-confirm/video-restore
  variants sharing the same group name, but no way to tell them apart was ever found
  -- turns out `requiredDepth=1` is the disambiguating signal, now calibrated with
  real dragged data instead of being left uncorrected.

### Fixed
1. **Controller-glyph icons could visibly snap onto a different item's position for
  a moment during completely ordinary play, not just while calibrating** (issue
  #51 follow-up, found while building the editor above). Root cause: the real
  focus read every part of this system relies on (a direct itemDef-array memory
  read) can briefly flicker between the correct index and a stale one for a few
  frames during a menu transition -- already visible elsewhere in this project's
  own diagnostics (`haveFocus`/`siblingCount` alternating frame-to-frame even
  while sitting completely still). The shipped overlay was reading that signal
  raw, every frame, so a transient misread could make an already-correctly-
  positioned icon jump onto a completely different item's calibrated position for
  an instant before snapping back. Fixed with a shared debounce
  (`TryGetStableFocusedGroupAndIndex`) requiring several consecutive frames of
  agreement before either the real overlay or the editor treats a new item as the
  actual focus target.
2. **The Special Ops map-select screen specifically kept jumping between two real
  positions even with the debounce above** -- a different, deeper bug: this
  exact screen's real menu-stack depth (`GetMenuStackDepth()`) genuinely,
  repeatedly flip-flops between 2, 3, and 4 for the identical on-screen list
  (same real group/index/sibling count, only depth differs), likely because some
  other UI element -- a live map-preview/description panel -- intermittently
  pushes/pops itself on the real menu stack in the background without the player
  navigating anywhere. `kManualGlyphPositions` had three separate, slightly
  different `SO_LEVELS_BUTTON_LIST` entries (depths 2/3/4) from being calibrated
  independently while depth happened to read differently each time -- merged all
  three to carry identical values, so the resolved position can no longer visibly
  differ no matter which of the three depths the flicker lands on.
3. **Auto-mantle (issue #62) confirmed working for the first time since it was
  requested back on 2026-08-03** — `[Movement] AutoMantleEnabled` (still off by
  default, opt-in by design) previously shipped as a documented non-functional
  feature after two earlier fix rounds could never be live-confirmed. Root cause,
  found this pass: the flag this feature's whole gate reads
  (`g_mantleHintDrawnThisFrame`) was only ever being set from inside a block that
  ALSO required this project's own icon lookup to succeed — silently coupling
  "is a real mantleable ledge here" to "did our own icon resolve," an unrelated
  concern. Decoupled; auto-mantle now fires purely off the real native mantle-hint
  detection, independent of icon-resolution success. Direct confirmation after the
  fix: **"it works now."**
4. **The mantle prompt's text/icon row was sitting noticeably below the real mantle
  arrow sprite it's meant to align with** — measured against a screenshot and
  nudged up (in design-space units, so it stays correct at any resolution, not a
  hardcoded real-pixel offset); one follow-up correction after an initial
  overshoot.
5. **Pickup/throwback/buy-station and similar gameplay hints were sitting
  noticeably too low, confirmed via two real before/after screenshots of the
  same pickup prompt.** Root cause: issue #70's "round 7" fix (2026-08-08) traded
  a real ~26-unit downward shift at 16:9 (the resolution this project is
  actually played at) for better proportional consistency across other
  resolutions, and was never live-confirmed against a real screenshot before
  shipping. Corrected using the user's own directly-measured gap (~10px) rather
  than re-deriving from the old formula's own imperfect math. Mantle's own
  vertical position is now driven by a single, fully independent constant
  instead of a shared value plus a mantle-specific offset on top of it, so
  tuning this shared value for pickup/throwback can never again silently drag
  mantle's own position along with it (an intermediate attempt at this fix hit
  exactly that problem before being corrected). Needed a second, same-day
  10px correction on top of the first (still too low after round 1) before
  matching the reference screenshot. The grenade-throwback hint's icon is also
  reported visibly squished (aspect-ratio distorted) — separate bug, not yet
  investigated, needs its own screenshot.
6. **The Reload prompt had silently drifted out of sync with the pickup/buy-station
  hint row it's meant to match**, since 2026-08-08 (issue #70's "round 7" position
  fix corrected the general hint-row formula but never updated Reload's own
  separately-hardcoded row constant to match) — found while investigating the
  mantle report above. Recalculated to match the current formula.
7. **Menu-glyph icons in a vertical list could visibly jitter a few pixels left/right
  between adjacent items** instead of reading as one aligned column, from ordinary
  per-item capture noise in `kManualGlyphPositions`. Any items whose X position falls
  within 15px of each other in the same table entry now snap to the rightmost of that
  group at lookup time (a runtime normalization, so it self-applies to every existing
  and future captured entry rather than needing every table hand-flattened).
  Deliberately scoped to one entry's own item list, so genuinely separated items
  (e.g. the main menu's 3 far-apart tiles, or a popup's fixed-X Yes/No pair) are
  untouched.
8. **Controller vibration could get stuck on indefinitely if triggered right before a
  pause, loading screen, or menu.** Root cause: the decay/expiry check that turns
  rumble off after its own scheduled duration only ran on the gameplay-simulation
  tick, which a genuine pause halts entirely (the same class of dead-tick problem
  already solved for pause-menu input) — so the physical motor kept buzzing for the
  whole paused duration instead of cutting off on schedule. Fixed by also running the
  same per-event expiry check from the always-on menu tick; each event still keeps
  its own real duration (a gunshot's short pulse vs. a longer scripted event), just
  now reliably enforced regardless of pause state.
9. **Random performance hitching during extended play sessions** (reported against an
  older version too, and live-witnessed on stream) — root cause: `proxy_d3d9.log` was
  opened in append ("a") mode, so it was never trimmed between launches. A real
  install's log had grown to 2.6GB / 22.5 million lines across every session since
  the mod was installed, not just one sitting. Appending to an already-huge,
  likely-fragmented file is a far better fit for hitching that's random and gets
  worse the longer/more you've played (cumulatively, across the file's whole
  lifetime) than any session-local cause. Fixed by opening in truncate ("w") mode, so
  every launch starts from a small, fresh log. If you still see hitching after this,
  delete your existing `proxy_d3d9.log` once (it won't be trimmed retroactively) and
  let us know.
  - **Follow-up (2026-08-18): the log-truncate fix alone wasn't the whole story.**
    Real A/B testing against v0.2.2 (rebuilt from source and deployed for direct
    comparison) confirmed the felt jitter is meaningfully worse on current builds
    than on v0.2.2 — real, not imagined. Two further real causes found and fixed
    the same day: (1) the background controller-poll thread continuously rescanned
    up to 4 XInput slots plus DualSense every ~4ms tick regardless of whether the
    active device had changed — now determines the active controller/slot ONCE at
    boot and locks onto it for the session (steady-state cost drops to exactly one
    real read per tick); switching to a different XInput slot or between XInput/
    DualSense now needs a relaunch to be picked up, reconnecting the SAME device
    does not. (2) `HudGlyphPositionLogging`'s dedup compared only against the
    single most-recently-logged string, not a real seen-set, so a normal HUD
    screen with multiple text elements re-logged almost every frame — one combat
    session produced 423,063 lines from this alone (91% of the whole log file),
    directly explaining a separate live report ("stutter when we get shot or are
    approaching enemies," i.e. exactly when HUD text churns fastest). Fixed with a
    real 64-entry seen-set; this diagnostic defaults OFF regardless. A custom
    per-frame benchmark tool (`FrametimeBenchmarkLogging`, `[Experimental]`) was
    also built this pass to catch cases like these directly instead of guessing.
    **User-confirmed after these three fixes: "much better."** **One residual
    symptom is explicitly PARKED, not resolved**: a felt dip specifically when
    looking toward enemies (real capture shows no correlation with any of this
    project's own code -- leading theory is genuine GPU-bound rendering cost,
    not CPU-side, which this project's own tooling can't cleanly measure). See
    `re_notes/known_issues.md` issue #79 for the full account and recommended
    next step.
10. **Survival co-op: damage-taken rumble could trigger off a TEAMMATE's hits, not
  your own.** Root cause, self-documented as a known risk when originally written
  but not confirmed until now: the health-polling code that drives damage-rumble
  always read player-entity array index 0, with a comment assuming "SP is always
  player index 0" — true for Campaign, but Survival co-op has two real player
  entities, and a client player (not the host) sits at a different index. Finding
  the real "which index is THIS client" mechanism needs further RE, not done this
  pass (won't guess/hardcode an unconfirmed value). Interim mitigation: damage-
  rumble now detects a second real player entity (2-player co-op) and disables
  itself in that case entirely, rather than risk rumbling for the wrong player's
  hits — fire-rumble (driven off your own weapon-fire, not this same array read)
  is unaffected either way.

### Groundwork
1. **Dev tool: in-game click-and-drag menu-glyph position editor** (issue #51
  follow-up). `kManualGlyphPositions` (the per-screen A-glyph position table) was
  originally calibrated via expensive `MiniDumpWriteDump` snapshots + raw memory
  scans, slow enough that several real screens were left uncovered (deeper
  `LEVELS_BUTTON_LIST` sub-lists, `OPTIONS_LIST` tabs past index 2, keybind-editing
  screens, `PAUSE_LIST`). Reuses the same click-and-drag handle UX already proven
  live for the harness-only controller-diagram editor (issue #66), but wired into
  the real game this time: with `[Experimental] GlyphPositionEditMode=1` set,
  press F2 in-game (same key/two-step convention as the harness tool) to activate
  live dragging — the currently-focused real menu item's glyph icon becomes
  draggable with the mouse, and the live coordinate readout next to it is an
  independently-draggable second handle (so it can be moved clear of other
  icons/text while calibrating); F3 exports every icon position touched that
  session to `exported_glyph_positions.txt`, ready to paste into
  `kManualGlyphPositions`. The verified-groups allowlist gate no longer blocks
  the draw for any group with a manual table entry, so results are visible
  immediately on every screen without a separate allowlist edit. Both the icon and
  its live coordinate readout are independently draggable, visible harness-style
  translucent bounding boxes (matching the real hit-test radius exactly). While
  the editor is active, the mouse is fully isolated from the real game: every
  mouse message (move/clicks/wheel) is swallowed before reaching the real menu's
  WndProc, AND a process-wide GetCursorPos hook freezes what the real menu's own
  per-frame polling sees (this engine has no DirectInput at all, so its hover/
  hit-testing reads raw cursor position directly, independent of the message
  queue) -- so the mouse only ever interacts with this project's own drag
  handles, never the game underneath them. Fixed a real coordinate-space bug where
  dragging silently did nothing: mouse position was being converted with the wrong
  function (one built for native draw-call positions, already in viewport pixels,
  not raw window-client mouse pixels), which happened to no-op whenever the
  viewport already matched the 1920x1080 design reference -- clicks landed exactly
  at the raw mouse position with zero conversion, missing the drag handles
  entirely at any window size other than a coincidental 1:1 match. Fixed a
  duplicate-icon bug: the shipped, un-dragged icon draw kept redrawing every
  frame right alongside the editor's own dragged one once they were far enough
  apart to no longer merge into a single draw slot -- the shipped draw for the
  currently-focused item is now suppressed for as long as the editor is actively
  toggled on, so only the (possibly repositioned) one shows. When no real item can
  be detected at all (some screens, e.g. keybind-editing lists, genuinely don't
  populate the itemDef shape this whole system depends on), the status readout now
  falls back to showing the older, separate selection-tracking signals
  (`g_currentSelGroupName`/`g_focusedItemName`) instead of just going blank, so
  there's something to read/log for screens the primary detection can't see at
  all. Fixed a real crash on F3 export: the success-log message buffer was
  undersized for its own formatted string, so `sprintf_s` correctly detected the
  overflow and terminated the
  process outright (by design, not a memory-corruption bug) -- the exported file
  itself always wrote and closed successfully beforehand, only the trailing log
  line crashed. dev-only — not a player-facing feature, and never active unless
  both explicitly enabled in config AND toggled on in-game with F2.
2. **New dev tool, `tools/ui_harness`: a real `.menu` file parser/expression-evaluator/
  renderer.** Built to debug controller-glyph positions on Survival's between-round
  buy screens without racing the round timer live in-game. Parses and evaluates all
  319 real `.menu` files in the extracted zone dump (tokenizer, block parser, a real
  expression AST evaluator against a fake/editable game-state, `tablelookup()`/CSV
  wiring, real material/DDS texture rendering), using a coordinate transform that was
  reverse-engineered from the real binary AND confirmed against a live capture (not
  assumed) -- see `re_notes/iw5sp.md`. Dev-only, never shipped or run inside the real
  game; the harness is a separate standalone executable. Visual fidelity is
  incomplete -- most itemDef background materials in the static asset extraction
  don't resolve, so most of the UI still renders as debug outlines rather than real
  textures; this is a data-completeness gap in the extraction, not a bug in the
  renderer itself.
3. **Two new `[Experimental]` config flags, both OFF by default, both real diagnostic
  capability added to the shipped DLL this pass**: `CaptureRuntimeMenuAssets` hooks
  the real, already-confirmed-safe `FindOrLoadAsset`/`CreateTexture` calls to dump
  real material textures to disk (name-tagged) while playing, feeding the harness
  tool above with real assets instead of the incomplete static extraction --
  **implemented, NOT yet confirmed to actually produce correctly-named files against
  a real play session.** `FrametimeBenchmarkLogging` writes a real per-frame CSV
  (frame-to-frame time plus a cost breakdown of this project's own hooks) -- this is
  the tool that actually found causes 1-2 of the stutter fix above, so it's
  live-confirmed working, not speculative.

### Investigated, Not Yet Resolved
1. **Bluetooth DualSense: stick input is garbled/unusable on real hardware**
  (`re_notes/known_issues.md` issue #77). Three real, evidence-backed bugs were found
  and fixed this pass (Y-axis inversion; an XInput-vs-DualSense poll-priority fight,
  most likely Steam Input contending for the same physical device; missing Bluetooth
  input-report CRC32 validation) — the live symptom was reported unchanged after all
  three. Zero CRC failures have been logged since the check was added, ruling out
  transport-level corruption specifically, but not the byte-offset/payload
  interpretation for this specific pairing. See issue #77 for the full account and
  the recommended next step (a raw-byte diagnostic, not yet implemented). USB is
  unaffected by these Bluetooth-specific bugs, but has not been independently
  confirmed working either -- see issue #76.
2. **Survival's dog/hyena melee-struggle pin-down prompts, investigated** (`re_notes/known_issues.md`
  issue #78). Found and decoded the real assets: `hud_dog_melee`/`hud_hyena_melee`
  (genuinely Survival-relevant, referenced in `common_survival.zone`) turn out to be
  plain animal-silhouette icons with no keyboard key baked in — not the button
  prompt itself, almost certainly an "attacked by X" indicator. The actual
  "press E to escape" text most likely exists as an ordinary separate string that
  may already flow through this project's existing glyph pipeline like every other
  interact prompt — genuinely unconfirmed, needs a live dog/hyena encounter with
  existing diagnostic logging enabled to check. Deliberately not implemented
  blind — see issue #78 for the recommended next step.

---

---

## v0.3.2 — Alpha (2026-08-16) — Controller-glyph icons actually work now; native DualSense/gyro preview

**Summary:** Every "no controller glyphs" report this project has ever received, going back to v0.3.0, traced to one cause: `glyphIconOverlayEnabled`, the glyph overlay's own master enable flag, was hardcoded off and never flipped on for release — not a hardware quirk, not Steam Overlay, not a memory-address theory. Finding that took 16 days and two separate investigation rounds (issues #71 and #74), including two real-but-partial fixes along the way (an XInput controller-slot bug, a font-detection gap) and two externally-sourced architectural theories that both turned out unnecessary. Fixed and confirmed live via direct reproduction, with the flag removed from the config schema entirely so upgrading players can't stay stuck on it — see `re_notes/known_issues.md` issue #74 for the full postmortem and `CODE_STANDARDS.md`'s new "Debugging Methodology" section for the standing rules adopted as a result. Also ships a first, PREVIEW/WIP pass at native DualSense (PS5 controller) support and gyro-aim, off by default and not yet live-tested against real hardware.

### What's New
1. **PREVIEW/WIP, off by default: native DualSense (PS5 controller) support via a new
  raw-HID backend, bypassing Steam Input entirely, plus a first pass at native
  gyro-aim.** This project's controller layer was XInput-only before this — a
  DualSense has no native XInput driver on Windows at all, so it was only ever
  visible through a third-party translator (Steam Input, DS4Windows), and real
  feedback (see issue #74 below) found that translation unreliable specifically for
  this mod. Now opens the DualSense directly over raw HID and reads its real input
  report itself — sticks/buttons/triggers work exactly like an XInput pad to every
  other part of this project, no other code needed to change. Gyro-aim rides on the
  same connection (`[Gyro] Enabled=1` in `mw3ncp_config.ini`), additive on top of
  right-stick look. **Not yet live-tested against real hardware** — implemented
  against a real reference implementation's confirmed byte offsets, not this
  project's own hardware, since none is available to the developer; gyro units are
  raw/uncalibrated and the axis mapping is a best-effort guess with invert toggles
  to correct it if wrong. USB only — Bluetooth DualSense isn't handled yet.
  **CRITICAL, found and fixed same day: the first build of this backend broke game
  boot entirely (a Windows loader-lock deadlock between this project's background
  controller-poll thread and the game's own Direct3DCreate9 DLL loading), live-
  reported within hours and root-caused via direct log evidence — fixed by refusing
  to attempt DualSense device enumeration until the game's own D3D device is
  confirmed to already exist.** See `re_notes/known_issues.md` issue #76 for the
  full writeup and what's still unverified.

### Fixed
1. **Root cause found and CONFIRMED LIVE for the "no glyphs at all, mod doesn't
  know it" reports: the controller-glyph overlay's own master enable flag
  defaulted OFF in every release since v0.3.0, and was never exposed in any
  shipped `.ini` or in-game UI.** `glyphIconOverlayEnabled` was left off as an
  internal "first live-test round" flag from the day it was written
  (2026-07-31) and never flipped on for release — `ShouldDrawGlyphOverlay()`
  required it to be true before anything else (including `ForceGlyphOverlay`,
  the one escape hatch this project has ever told users about) even got a
  chance to matter, so glyphs could never draw on a genuinely fresh install
  regardless of resolution, language, controller slot, or Steam Input state.
  Reproduced directly on a second, fresh-install PC, confirmed straight from
  that machine's `proxy_d3d9.log` config dump, and confirmed fixed on a
  relaunch of the rebuilt DLL. See `re_notes/known_issues.md` issue #74 for
  the full writeup, including why the external memory-shifting/D3D-state
  theories investigated first weren't the real cause.
2. **The flag itself is now removed entirely, not just re-defaulted, to close a
  config-migration hazard the first fix would have missed for existing
  players.** Every prior install had already written a literal
  `GlyphIconOverlay=0` to its own `mw3ncp_config.ini` — just changing the
  in-code default would have kept getting silently overridden back to broken
  for anyone upgrading rather than installing fresh, with no obvious reason
  why. `glyphIconOverlayEnabled` and its `[Experimental] GlyphIconOverlay` ini
  key are gone from the config schema; the overlay is now controlled solely by
  `ForceGlyphOverlay`/active-input-method detection. The config schema version
  was bumped so every existing player's ini gets rewritten on next launch and
  the stale key is physically removed from their file, not just ignored.
  **See `re_notes/known_issues.md` issue #74's postmortem** for how this
  specific bug — a config flag this project itself hardcoded off — took 16
  days and two investigation rounds to actually find, and the standing
  debugging-methodology rules added to `CODE_STANDARDS.md` as a result.

---

## v0.3.1.h1 — Hotfix (2026-08-09) — Mouse-lag regression, non-16:9 scaling/glyph-visibility, Options-screen crash

**Summary:** Feature-free hotfix — releases with only fixes, no new scope, use a `.hN` suffix on the release they're patching rather than a new minor version number. Headline fixes: a critical mouse-movement-correlated FPS drop (confirmed live, fully fixed) and controller-glyph icons never appearing at all on any non-16:9 resolution (confirmed root cause via direct log evidence across two separate bug classes — a font-detection gap and a position-formula bug — not guessed). Also included: a real crash-risk fix in the still-preview/WIP Options screen and a log-volume fix for long sessions. **Honesty note**: the mouse-lag fix is confirmed live; the non-16:9 scaling/glyph fixes went through several live-tested-wrong rounds (see `re_notes/known_issues.md` issue #70) and haven't had an explicit "fully correct across every resolution" confirmation yet; the Options-screen crash fix and log-slimming change ship on code-review confidence alone, same as v0.3.1's own equivalent fixes.

### What's New
1. **Controller connect/disconnect now shows a real on-screen notification.**
  Unplugging/replugging a pad mid-session (or a wireless pad's battery
  cutting out) now shows "Controller Connected"/"Controller Disconnected"
  via this project's existing toast notification, instead of silently
  changing behavior with no visible feedback. No toast on first launch
  before a controller was ever known to be connected.
2. **`proxy_d3d9.log` could grow unbounded (one real session hit ~22GB).**
  Performance/log-slimming pass targeting worst-case circa-2008 hardware
  found two real causes: the logger flushed to disk on every single call
  (a real synchronous-write cost, worse on slow storage), and two leftover
  research-diagnostic log lines fired unconditionally every frame/every
  menu item with no gating. Logging now flushes at most once a second
  (with a crash-safe flush-on-exception handler so a hard crash still
  leaves a fully flushed log — "we still need conclusive logs"), one dead
  diagnostic (its hypothesis was refuted in the same session it was added)
  was deleted outright, and the other was moved behind a new
  `[Experimental] ListItemPositionLogging` flag (default off). See
  `re_notes/known_issues.md` issue #67.
3. **PREVIEW/WIP feature fix: real crash risk in the custom Options screen —
  the general-purpose white fill texture and every one of its 14 text
  caches were never released across a device recreation.** Found via a
  general rendering-code health audit, same bug class as the already-fixed
  blur-shader crash: after a real `vid_restart` (e.g. from confirming a
  staged Advanced Video setting), nearly everything the Options screen
  draws — background, row highlights, sliders, toggles, all text — would
  bind a texture handle belonging to a destroyed device on the very next
  frame it's open. See `re_notes/known_issues.md` issue #72.

---

### Fixed
1. **CRITICAL, CONFIRMED LIVE: severe mouse-movement-correlated FPS drop ("drops
  to 4fps"), introduced earlier the same day by the XInput multi-slot fix
  below.** Root cause: this project's `WndProc` hook polls the gamepad on
  EVERY window message, not once per frame — `WM_MOUSEMOVE` alone can fire
  dozens of times per rendered frame while dragging the mouse, and the
  multi-slot scan below made each of those polls up to 4x more expensive.
  `XInputGetState` on a disconnected slot has real, well-documented latency —
  the mouse-move message flood maximized how often that cost fired. Fixed by
  moving all real XInput calls onto a dedicated background thread, polling on
  its own schedule fully decoupled from the message pump — confirmed live:
  "the mouse lag GONE." See `re_notes/known_issues.md` issue #71.
2. **Corner hints and gameplay hints (pickup/reload/mantle/etc.) distorted and
  mispositioned at non-16:9 resolutions — two distinct real bugs found and
  fixed, after a first attempt made things worse.** A first fix (a temporary
  device viewport swap) was live-tested and confirmed worse, fully reverted.
  Bug 1 (size): a fixed-aspect glyph icon was stretched non-uniformly on any
  non-16:9 screen because element SIZE used the same non-uniform per-axis
  scale as position — fixed with one uniform scale for size, leaving
  position math untouched. Bug 2 (position, found after further live
  testing at 4:3): several hints — Quit, Leaderboards, Friends/Back-
  shortcut, and every gameplay hint except Reload — build their position
  from the real native hint's own already-correctly-placed screen
  coordinates, but that value then gets scaled a SECOND time by this
  project's own drawing code, landing them "way up to the left" on anything
  non-16:9. Fixed by converting the real value back to a design-space
  equivalent before it re-enters the same pipeline. A third, related bug in
  the same family: the check that decides whether a hint row *is* a Quit/
  Leaderboards corner hint at all compared the real screen coordinate
  directly against a design-space constant, so on non-16:9 it could silently
  misclassify the row — fixed by converting before comparing. Neither fix
  changes any device state. **Fourth bug, confirmed live ("but now you broke
  16:9"): the conversion above computed its scale from the game window's
  client-rect size, but the code that later re-scales the result uses the
  real D3D9 device's own viewport size — two different answers to "what's
  the current resolution" that only ever agreed by coincidence.** Fixed by
  routing the conversion through the same real device the draw call uses,
  so both sides always agree, at any resolution or window mode. **Fifth
  bug, confirmed live immediately after ("now back and leaderboard are
  broken"): an attempt to also "fix" the one fixed reference position (the
  synthetic Back hint) by re-deriving it against an assumed capture
  resolution was itself wrong** — this project's own logs showed the real
  capture-time viewport was 1920x1080 (this project's design-space
  reference) the whole time, not the monitor resolution the original
  comment named, so that value needed no conversion at all and re-deriving
  it just moved it (and, since Quit/Leaderboards' row-detection reads the
  same constant, broke their detection too). Reverted to the original raw
  value. **Sixth bug, live-reported right after #73's font fix made these
  hints visible at low resolutions for the first time ("position drifts on
  pickup/throwback/mantle prompts"): two small empirical position nudges
  (6px vertical, 82px horizontal for mantle only) were added as a FIXED
  number of real pixels, not a proportional amount** — a small fraction of
  a 1920px-wide screen but a much bigger one at 640px wide, so the nudge
  increasingly overshot the real element it's meant to align against as
  resolution dropped. Fixed by applying both nudges in design-space units
  instead, so they scale down proportionally with resolution like
  everything else. **Seventh bug, confirmed via log immediately after
  ("still far too vertically high on low res"): the vertical position
  formula multiplied a real screen coordinate by a native per-draw-call
  scale value that turns out to vary by FONT TIER, not by screen
  proportion** — comparing the same native hint text at two resolutions,
  the real coordinate alone tracked the screen height almost perfectly
  (~0.665 and ~0.782 of height at both 1920x1080 and 640x480), but
  multiplying it by that scale value dragged the low-res result far
  further up the screen than intended. Fixed by using the real coordinate
  directly, without that multiply. See `re_notes/known_issues.md` issue #70.
3. **Pickup weapon and throw grenade controller prompts didn't show at all at
  4:3 resolutions, while Reload kept working — confirmed root cause via
  this project's own log, not guessed.** The real engine draws the same
  native hint text with a different, smaller font depending on resolution —
  confirmed via log to be `fonts/bigFont` at 800x600 and `fonts/normalFont`
  at 640x480, versus `fonts/extraBigFont` at 16:9 — none of which except
  the 16:9 one were in this project's allowlist for "should this hint get a
  controller icon." With the font unrecognized, the whole replacement was
  silently skipped and the native PC-keybind text showed instead, reading
  as "no prompt" to a controller player. Reload was unaffected because its
  own font doesn't change with resolution and was already allowlisted.
  Fixed by adding both missing font names — this looks like a per-resolution
  font-tier system, so an as-yet-untested resolution could still need a
  further addition if reported. See `re_notes/known_issues.md` issue #73.
4. **Custom mouse cursor position reported "way off / unusable" at non-16:9
  resolutions — not yet fixed.** The existing window-to-viewport ratio math
  looks architecturally sound on inspection, so rather than guess a third
  time this session, diagnostic-only logging (`[cursor-pos-diag]`) was added
  to capture real values on the next repro before attempting a real fix. See
  `re_notes/known_issues.md` issue #70.
5. **Some players saw no controller-glyph icons at all, even on English with
  default settings — real bug found, likely root cause.** Every real XInput
  read in this project was hardcoded to user index 0, never scanning other
  slots — a controller assigned to a different slot (a second pad, a tool like
  x360ce occupying slot 0 with its own virtual device, Steam Input
  renumbering) looked identical to "no controller at all." Now scans all 4
  slots, preferring whichever is actively showing real input when multiple
  legitimate controllers are connected at once, instead of just grabbing the
  first one found. Not reproducible on the developer's own machine, so not
  yet confirmed as the full fix — see `re_notes/known_issues.md` issue #71.

### Groundwork
1. **New diagnostic: `[Experimental] ForceGlyphOverlay`** (`mw3ncp_config.ini`,
  default off). Draws the glyph/hint overlay regardless of whether a
  controller is currently detected as active — helps narrow down whether a
  "no glyphs" report is the XInput-slot issue above or something else
  entirely. See `re_notes/known_issues.md` issue #71.

---

## v0.3.1 — Alpha (2026-08-06) — Multi-language glyph fix + Options screen preview

**Summary:** Controller-glyph icons were silently broken for every non-English game language — root-caused against the game's own real localization resolver and fixed properly, confirmed live. Also ships a from-scratch custom Options screen replacement covering every real vanilla tab plus real keybind rebinding — real, working code (compiles clean, logically sound, follows every proven pattern already live elsewhere in this project) but shipped as an explicit preview/WIP feature (off by default, ini-only, no in-game toggle) since it hasn't been played yet; see `re_notes/known_issues.md` issue #66 before enabling `[Options] UseCustomOptionsScreen=1`. Intended as a stabilization release, same as v0.2.2 — no further release planned for up to ~2 weeks barring a critical issue.

### What's New
1. **PREVIEW/WIP, off by default: custom Options screen now covers every real vanilla tab, plus real keybind
  rebinding, a new custom controller-bindings tab, and a real apply/restart flow.**
  All 7 real vanilla tabs (Look, Video, Audio, Voice, Advanced Video, Movement,
  Actions) are live now, not just Controller/Look/Voice — every setting kind
  (numeric sliders, toggles, real dropdown lists, keybinds) is editable. Several
  Advanced Video settings were corrected from a wrong type to their real one
  (Anti-Aliasing, Ambient Occlusion, texture quality tiers) using their exact
  real value lists read straight from the game's own menu files, not guessed —
  so adjusting them can never write an invalid value. Movement/Actions/Look/
  Voice's 35 real keybinds can be rebound with a real "press any key" capture,
  not just displayed. A brand-new "CUSTOM BINDS" tab lets you assign any
  controller button to any action individually, for layouts the 4 built-in
  presets don't cover. Backing out with an unconfirmed Video/Audio/Advanced
  Video change now shows a real "Apply Settings?" prompt — rebuilt against the
  actual real menu file to match the console version exactly: a real navigable
  Yes/No list (not a bare A/B button prompt), defaulting focus to "No" just
  like the original. The Button Layout row also gained a 5th "CUSTOM" option
  that previews and selects the new custom bindings. Real toggle-switch and
  slider-bar graphics replace bare "0"/"1"/raw-number text, Anti-Aliasing/
  Ambient Occlusion/texture-quality rows show their real label ("2X," "Extra,"
  etc.) instead of a raw stored number, and the tab bar now scrolls to stay
  within the panel's own left edge instead of overflowing past it. Not yet
  live-tested — see `re_notes/known_issues.md` issue #66.
2. **PREVIEW/WIP: Options row list now scrolls, and Custom Binds moved into a Controller
  subsection.** Tabs with more rows than fit on screen (e.g. Movement's 16)
  now scroll to keep the selected row visible, with small "more above/below"
  hints, instead of overflowing past the description line. "CUSTOM BINDS" is
  no longer its own top-level tab — it's now a drill-down reached from a row
  on the Controller tab, same convention as the existing Stick/Button Layout
  drill-downs. Not yet live-tested — see `re_notes/known_issues.md` issue #66.
3. **PREVIEW/WIP: custom in-game options overlay, invoked from the real pause menu's own "Options" button.** New
  fully-drawn settings screen, invoked by drawing an extra "MW32011NCP
  Options" row below the real native `OPTIONS_LIST` menu's own last item via
  the existing overlay layer (the real menu's own item count/content is never
  touched — see issue #23 on why direct menu-content injection is unsafe).
  Pressing A on the extra row opens a custom-drawn panel with 6 real, working
  settings: Sensitivity Horizontal/Vertical, Invert Look, Vibration, Stick
  Layout, Button Layout — adjustable with Left/Right, closable with B, and
  persisted live to `mw3ncp_config.ini` via a new `SaveModConfig()`. Round 1
  worked functionally but was live-rejected on look/feel; round 2's visual
  fixes were themselves still rejected because round 3 found a genuine
  rendering bug (crushing all overlay text horizontally, present since round
  1) plus a visibility/gating bug plus an oversized panel, all fixed in round
  3 and confirmed working live ("much better"). Round 4 makes the opened
  panel fullscreen, per request, with the settings list occupying a left
  content column and the remaining screen space reserved (left blank, not
  built yet) for a future visual-mapping/controller-diagram feature.

  **Scope pivoted the same day**: rather than extending the real Options
  screen with an extra row, this is now being rebuilt as a FULL replacement
  of the entire Options flow — every real vanilla setting (not just this
  mod's own), gated by a new `[Options] UseCustomOptionsScreen` config
  toggle (`ConfigVersion` 12→13, **default off**), with `mw3ncp_config.ini`
  mirroring every real vanilla setting as a genuine local settings backup
  independent of Steam Cloud sync issues. Groundwork landed this pass: real
  engine accessors for every dvar type plus the full 256-slot keybind table
  (`real_settings.h`/`.cpp`), the complete settings catalog covering all 7
  real tabs including every Movement/Actions keybind (`vanilla_settings_table.h`),
  and the ini mirror's read/write bridge (`vanilla_settings_sync.h`/`.cpp`,
  `SyncVanillaSettingsToIni`/`RestoreVanillaSettingsFromIni`). Also added the
  "Apply Settings?" deferred-write mechanism for restart-required settings
  (`staged_settings.h`/`.cpp`) — a pending value is held locally and only
  written to the real dvar (plus a real `vid_restart`/`snd_restart`) once
  committed, so cancelling never leaves a real setting half-changed.

  **A full render-suppression approach (hiding the real Options screen
  entirely instead of drawing over it) was built and live-tested the same
  day — it prevented the game from launching at all and was immediately
  reverted** (the hook code is kept, disabled, for the record). Reverted to
  the original lower-risk plan instead: draw fully over the real screen and
  claim all input while open, with a full-screen dim background matching the
  real engine's own popup-dimming technique (checked directly — there's no
  dedicated background image asset for this kind of screen to reuse; the
  real menu dims the live paused game view instead of showing static art).

  On that foundation, the replacement screen is now genuinely tabbed:
  **Controller** (this mod's original 6 settings, unchanged), **Look**, and
  **Voice** (real vanilla settings, switched with LB/RB) are live in this
  pass, each showing only its fully-editable settings for now — keybind rows
  and settings without extracted enum choices are deliberately left for a
  later round rather than half-built. The `[Options] UseCustomOptionsScreen`
  toggle now actually gates the whole feature (previously built but unused).

  **Confirmed working live, then simplified further the same session**: "the
  button should be called from the native options button we no longer need
  the individual mw32011ncp options seperate." The entry-chip row is now
  GONE entirely — the screen is invoked directly from the real pause menu's
  own "Options" button (confirmed via `pausedmenu.menu`: itemDef
  `PAUSE_LIST_1`), intercepted before the real Options screen ever opens.
  Backing out with B reveals the still-open real pause menu underneath,
  same as the real Options screen would.

  **Footer button prompts now use real controller-glyph icons, not text.**
  The bottom legend previously read as plain text ("LB/RB TABS", "A TOGGLE",
  etc.) — it now draws the actual Xbox 360/Xbox Modern/PlayStation glyph icon
  (matching the player's real `GlyphStyle` setting) before each label, same
  as every other button hint this project draws elsewhere in-game.

  **Full visual restyle to match the real console screen, plus new Stick/Button
  Layout drill-down screens.** Rebuilt against real console reference
  screenshots: the panel is now a solid, edge-to-edge left-hand slab (not a
  floating bordered box over a full-screen dim) with the live paused game
  visible past its right edge, right-aligned labels with values sitting over
  that game view, a real gradient highlight bar, an inline controller-glyph
  icon on the focused row, a per-row description line, and a small corner
  "Back" hint replacing the old full-width footer legend. Selecting Stick
  Layout or Button Layout now opens a real sub-screen — its own option list
  plus a controller diagram with labeled leader lines that update live per
  preset, reading the same real routing/button-map data the game itself
  uses rather than a hand-authored label table. The diagram now uses a real
  product photo of the player's actual controller type (Xbox 360/Xbox
  Modern/PlayStation, matching `GlyphStyle`) instead of a procedural
  circle-and-rect schematic, and each label draws the real glyph for its
  own currently-bound button right next to shortened, clearer text, sized
  and positioned to actually fit on screen. Not yet live-tested.

  See `re_notes/known_issues.md` issue #66 and
  `re_notes/options_menu_full_map.md` for the full research trail.
4. **PREVIEW/WIP: blurred background on the custom Options screen's right-hand side**, matching
  the real console reference (which dims/blurs the live paused view behind the
  panel — this project's PC build previously showed it completely sharp, which
  clashed visibly with real gameplay HUD text bleeding through). Downsamples the
  real backbuffer into a small render-target texture, then runs a real compiled
  9-tap `ps_2_0` pixel shader over it (`re_notes/shaders/options_blur.hlsl`,
  offline-compiled via `fxc.exe`, embedded as bytecode — no runtime shader
  compiler linked). Not yet live-tested. See `re_notes/known_issues.md` issue #66.

### Fixed
1. **PREVIEW/WIP feature fix: custom Options screen could crash the whole game after applying a staged
  Advanced Video setting.** Reopening the pause menu after confirming "Apply
  Settings?" (which fires a real `vid_restart`, fully recreating the D3D9
  device) crashed the game — the cached background-blur pixel shader wasn't
  released across that recreation, so the next frame tried to bind a shader
  handle belonging to a device that no longer existed. Not yet live-tested —
  see `re_notes/known_issues.md` issue #66.
2. **Controller-glyph icons never appearing for non-English game languages —
  fixed and confirmed live.** Community-reported (Nexus, v0.3.0): an Italian
  player never saw any controller icons, in menus or gameplay. Root cause:
  several glyph-detection sites matched the game's own on-screen, LOCALIZED
  text against a hardcoded English word instead of a language-independent
  signal — e.g. the Reload reminder was only recognized if its text was
  literally "Reload", and the Quit/Leaderboards corner-hints the same way.
  Properly fixed, not worked around: found and wired in the game's own real
  localized-string resolver (`GetLocalizedString`, confirmed via
  disassembly), so detection now compares against whatever text the
  CURRENTLY ACTIVE game language actually renders — correct automatically
  for every supported language, no per-language guessing. **Confirmed live**
  after switching the game's language locally to retest, then two more
  real bugs surfaced by that same retest were found and fixed the same day:
  a Survival end-of-wave bonus prompt getting hijacked into the Reload
  template under a language-driven layout shift (fixed by dropping an
  unsafe position-based fallback check, keeping only the language-correct
  text comparison), and the mantle/vault hint's icon failing to show because
  its key name ("Space") IS actually translated for hint-text substitution,
  contrary to an earlier assumption — fixed with a new structural template
  match that never needs to know what the translated key name says. The
  grenade-throwback hint had the identical risk (its own combo bind text
  contains the English word "or") and was proactively fixed the same way,
  before it was even confirmed broken live. A full audit then found and
  fixed one more real case (turret placement) and hardened three menu
  shortcuts (Friends/Game Summary/Back) that weren't confirmed broken but
  are now strictly more robust regardless, using a newly generalized
  version of the same technique that any future hint of this kind can
  reuse with one new constant instead of a fresh investigation. **Known,
  deprioritized gap, not fixed**: some menu glyph icons can land in a
  slightly wrong POSITION under other languages (this project's own manual
  per-screen position tables were calibrated against English text length) —
  icon SELECTION is correct, only fine positioning on a few screens isn't
  yet. Full translation of this mod's OWN added text (Options screen
  labels, etc.) is a distinct, much larger effort tracked separately. See
  `re_notes/known_issues.md` issues #68 and #69.
3. **PREVIEW/WIP feature fix: custom Options screen crash on open, fixed same day it shipped.** The
  first background-blur implementation called `GetSurfaceLevel` through the
  device's own vtable instead of the target texture's — a genuinely different
  COM interface at that vtable index, which invoked the wrong real function
  with the wrong argument count and corrupted the stack. Root-caused and fixed
  as part of the real-shader rewrite above; not yet live-tested. See
  `re_notes/known_issues.md` issue #66.
4. **Vertical look sensitivity default corrected, confirmed live.** Real MW3
  console has only one Sensitivity slider (no independent vertical control at
  all) at roughly 55-60% of the horizontal turn rate — this project's own
  `SensitivityVertical` default (200 against `SensitivityHorizontal`'s 250, an
  ~80% ratio) had wrongly assumed MW3 matched other console CoD titles' fully
  separate sliders. Corrected default to `145` (~58%), confirmed as the right
  feel via direct live retest after two rounds (an initial 75/~30% estimate
  from feel alone tested "way too slow" first). `ConfigVersion` bumped 10→12.
  See `re_notes/known_issues.md` issue #65.

---

---

## v0.3.0 — Alpha (2026-08-03) — Controller-glyph icons, vibration, auto-mantle (disabled)

**Summary:** The most significant release to date by code volume — 8,057 lines changed overall, 5,725 of them in `proxy_d3d9/src` alone, more than double the prior record (v0.2.2→v0.2.5's 2,739 source lines). A brand-new vibration subsystem built from zero, controller-glyph icons for real in-game interact hints and menu corner hints (the longest-running single bug investigation in this project's history, the A-glyph saga, ~15 rounds across three days), and the full crouch/UI bug batch from the first public Survival co-op stream all landed here. Auto-mantle while sprinting is implemented but NOT WORKING and ships disabled — treat it as not implemented, not a working opt-in. Everything below accumulated since v0.2.5 (2026-07-31).

**Known gaps in this release, stated plainly rather than left implicit:**
- **Auto-mantle while sprinting is NOT WORKING and shipped disabled
  (`AutoMantleEnabled=0`).** Implemented, live-tested twice, still doesn't
  reliably fire near a real ledge — see its own entry below and
  `re_notes/known_issues.md` issue #62. Treat as not implemented for this
  release, not as a working opt-in feature.
- **Vibration doesn't register hits absorbed by Survival's Body Armor** (a
  separate value from real health this project hasn't located yet) — only
  real health loss and weapon fire produce rumble. See issue #63.
- **Vibration strength is maxed out in software** (`FireIntensity=1.0`, the
  real ceiling) and may still read as weak on some controllers — likely a
  hardware/motor difference at that point, not something a config value can
  fix further. See issue #63.
- Highlighted-item A-glyph in menu lists only shows on a short, explicit
  verified allowlist (main menu, Campaign hub, two popups) — every other
  screen is a deliberate, documented gap, not a bug. See issue #51.
- Per-animation-step reload vibration is a committed future feature, not
  started. See issue #63.

### What's New
1. **Versions before v0.2.2 are no longer publicly distributed.** As a further
  VAC-risk mitigation step beyond v0.2.2's own aim-assist removal, all six
  releases that ever shipped with that code compiled in (`v0.1.0-prealpha`
  through `v0.2.1`) have been unpublished from GitHub Releases (converted to
  drafts) — nothing was deleted, and every git tag/commit remains fully
  available in this repository. **v0.2.2 is now the oldest supported/
  distributed version.** See `re_notes/known_issues.md` issue #15 and
  `README.md`'s updated security notice.
2. **On-screen notification text no longer depends on Barlow Condensed being
  installed system-wide.** The actual Barlow Condensed SemiBold (Regular + Italic)
  font files are now embedded directly in the DLL (`proxy_d3d9/proxy_d3d9.rc`) and
  loaded as a private, in-process-only font via `AddFontMemResourceEx` at startup
  — previously the font was requested from GDI by face name only, which silently
  fell back to a default system font if Barlow Condensed wasn't installed. Fully
  self-contained now; nothing extra to install. SIL Open Font License 1.1,
  credited in README.md's Credits section.
3. **Controller-glyph icons for real in-game interact hints, confirmed live
  (`re_notes/known_issues.md` issue #48).** After two overlay-alignment attempts
  landed visibly off, the game's own hint text is now fully suppressed and this
  project draws the whole thing itself (prefix text + a real controller-glyph icon
  + suffix text, own embedded font, matching the active `[Bindings] GlyphStyle`).
  Covers weapon pickup/swap, buy-station, mantle, Reload (a console-style pulsing
  icon with static "Press X To Reload" text), grenade throwback (RB/R1), and
  Survival's ready-up prompt (F5→Y). Correctly suppressed while paused.
4. **Controller-glyph icons extended to menu UI corner hints (Back/Friends),
  confirmed live (`re_notes/known_issues.md` issue #50).** Same technique as
  the in-game hints, through a separate menu-specific bind mapping (ESC→B,
  F→Y, Enter→A — fixed regardless of gameplay `ButtonLayout`, per explicit
  design, unlike the in-game hints above). Supports multiple simultaneous menu
  hints (MW3 shows several at once, unlike a single in-game interact hint). Y
  now genuinely opens the Friends list (previously the glyph swap was cosmetic
  only) via a synthesized keypress, the same technique already used for
  Survival's ready-up.
5. **All controller glyph/hint overlays now hide automatically whenever
  keyboard/mouse becomes the active input method**, mirroring console (never a
  cursor and button-prompt glyphs on screen together). Shares the exact same
  input-method signal the custom cursor overlay already used for its own
  visibility (issue #55), extracted into one function so the two can never
  disagree. See `re_notes/known_issues.md` issue #61.
6. **Auto-mantle while sprinting — NOT WORKING, shipped disabled
  (`AutoMantleEnabled=0`).** Attempted this release: automatically mantle
  over obstacles while sprinting and pushing the left stick fully forward, no
  separate Jump press needed, matching modern CoD titles' own sprint-mantle
  behavior, driving the same real native mantle command Jump already uses.
  New `[Movement]` config section exists (`AutoMantleEnabled`/
  `AutoMantleForwardConeDegrees`/`AutoMantleMinStickMagnitude`) but the
  feature itself does not reliably work: **round 1** made the player jump
  continuously on flat ground (the mantle command and Jump are literally the
  same usercmd bit, so gating on sprint+stick alone with no real ledge check
  wasn't safe) — fixed by gating on the game's own real mantle-hint-visible
  signal instead. **Round 2, live-retested the same day: swung the other way
  and never fires at all near a real ledge** — traced to a timing gap between
  the render hook that sets the hint-visible signal and the gameplay-tick
  hook that reads it, addressed with a timestamp+grace-window fix and a
  diagnostic log. **Still not confirmed working after round 2.** Treat this
  as an unfinished, not-implemented feature for v0.3.0 — shipped off by
  default and should stay off until a live test confirms it actually fires.
  See `re_notes/known_issues.md` issue #62.
7. **Controller vibration/rumble, reimplemented from scratch (issue #24).**
  Previously shipped once, then disabled the same day after crashing the game
  at startup (the original hooks targeted generic, multi-purpose native
  functions with inconsistent real argument shapes across callers). Fire
  rumble now hooks a single-purpose function found via a runtime byte-pattern
  scan; damage rumble avoids hooking entirely, detected via a per-frame local-
  player health poll instead, with guards against regen/perk/respawn false
  positives. **Four rounds of live-reported weakness, three of them fixed the
  same day:** (1) this project only ever loaded `xinput9_1_0.dll`, a legacy
  compatibility DLL whose `XInputSetState` is a documented no-op for real
  vibration on most installs — fixed by trying `xinput1_4`/`xinput1_3` first;
  (2) even with a real signal, it still felt "extremely weak" — raised
  `FireIntensity` 0.25→0.55, `FireDurationMs` 60→90, `DamagePerPoint`
  0.03→0.05; (3) **still "extremely weak, almost impossible to feel" after
  round 2.** Real ERM vibration motors have physical spin-up lag (~50-100ms)
  before reaching a speed you can feel — the original decay curve commanded
  the peak strength only at t=0 and ramped straight down for the pulse's
  entire duration, so a short pulse could finish decaying before the physical
  motor ever caught up. Added a sustain-then-release envelope
  (`kRumbleSustainFraction`), raised strengths again, and added a
  rate-limited `[rumble-diag]` trigger-cadence log; (4) **live-retested —
  "better now but kinda weak."** Raised `FireIntensity` to its own ceiling
  (`1.0` — `XInputSetState`'s real motor-speed fields are already commanded
  at their maximum here, there is no higher value to send), extended
  `FireDurationMs` to 180 and the sustain fraction to 0.7, and bumped
  `DamagePerPoint`/`DamageDurationMs` proportionally. **Known gap: if still
  weak at these values on the same physical controller, this is very likely
  the controller's own hardware (weaker real motor response, common on
  third-party/Bluetooth pads), not a remaining software fix** — not yet
  independently confirmed either way. `ConfigVersion` bumped 8→10 across all
  four rounds.
8. **Known gap, not fixed this release: damage-rumble doesn't register hits
  absorbed by Survival's purchasable Body Armor** (a separate value from real
  health this project hasn't located yet — armor is tracked outside the
  entity struct's health field entirely). A diagnostic scan
  (`[Experimental] ArmorFieldScanLogging`, default off) was added and run
  once live: it correctly ruled out one loud false-positive candidate
  (`entity+0x58`, confirmed to be current ammo count, now excluded from the
  scan) but didn't land on a confirmed armor field in that capture. Needs
  another live Survival session with armor purchased and a few armored hits
  taken, then the log, before this can be resolved. See
  `re_notes/known_issues.md` issue #63.

### Fixed
1. **CRITICAL: changing display mode crashed the whole game.** Root cause: this
  engine destroys and fully recreates its Direct3D device on a display-mode
  change instead of calling `Reset()` on the existing one — every texture this
  project had cached against the old device was left dangling, corrupting the
  next frame. Fixed by releasing all cached textures whenever a new device is
  detected. See `re_notes/known_issues.md` issue #48.
2. **Controller-glyph hints were positioned/sized for 1920x1080 only** and broke
  at other resolutions (reported at 1440p). Root cause: the window's client
  rect isn't reliable ground truth for this engine's real backbuffer size —
  now read directly from the D3D9 device's own viewport instead. See issue #48.
3. **Grenade throwback and Survival ready-up glyphs always showed RB/Y
  regardless of the configured `ButtonLayout`**, showing the wrong button for
  anyone not on the default layout. Now resolve through the same
  layout-aware path every other gameplay glyph already uses. See issue #50.
4. **Menu corner hint showed "Friends" instead of "Back" while inside Special
  Ops' modal popups, RESOLVED (`re_notes/known_issues.md` issue #50).**
  Three earlier fix attempts were tried and ruled out live (documented as
  "investigated, not resolved" in an earlier pass) before issue #51's
  research unblocked the real fix: `getfocuseditemname()`, a safe, single-
  caller native signal none of the earlier attempts had available. Also
  fixed the same underlying screen's "Friends" hint persisting while the
  Friends list itself was open, and added a B glyph to the main title
  screen's "Quit" (visual-only — input already worked via B's existing
  ESC-forward).
5. **Custom mouse cursor overlay (`re_notes/known_issues.md` issue #52).**
  This project's own glyph/hint overlays draw at end-of-frame, after the
  game's own native software cursor — meaning the native cursor rendered
  UNDER this project's icons wherever they overlapped. Fixed by suppressing
  the native cursor's draw (a return-address-gated hook so only that one
  call site among 31 shared callers is affected) and redrawing a custom
  cursor as the literal last thing drawn each frame. Position required
  ruling out three wrong theories in turn (an internal engine global in an
  unknown coordinate space; `GetCursorPos`, broken by a DPI-awareness
  mismatch; raw `WM_MOUSEMOVE`, broken by this engine rendering to a
  differently-sized backbuffer than the actual game window) before a
  two-point corner calibration found the real fix. User-supplied cursor art.
6. **Custom cursor stopped tracking mouse movement after a display-mode
  change**, while every other overlay element kept working. Root cause: a
  display-mode change creates an entirely new game window (confirmed via a
  direct `hwnd` comparison in the logs), not just a new D3D9 device on the
  same window — contradicting this project's own long-standing "the game has
  one window" assumption. The old window stayed subclassed for input
  tracking; the new one never got wired up. Fixed by re-subclassing whenever
  the window handle actually changes. General infrastructure fix, not
  cursor-specific. See `re_notes/known_issues.md` issue #52.
7. **Highlighted-item A-glyph in vertical list menus — long investigation,
  currently gated to a verified-only allowlist (`re_notes/known_issues.md`
  issue #51, full round-by-round trail there, ~15 rounds across 2026-08-01
  through 08-03).** Draws the A/select icon after a focused list item's own
  native text. Root cause behind most rounds: the focus signal
  (`getfocuseditemname()`) doesn't stay in sync with real navigation on every
  screen — replaced with a direct itemDef-memory read
  (`TryGetRealFocusedGroupAndIndex`) that needs no script cooperation. Other
  fixed bugs along the way: nested-modal mispositioning (popup over a dimmed
  background screen), several per-screen calibration misses (main-menu row,
  Choose Content Pack's bottom-anchored variant, Survival's map-select
  screen), and a corner-hint slot cap that starved the icon on one screen. An
  automatic self-calibrating positioning mechanism was tried and live-tested
  unreliable, reverted (kept in code, disabled). **Release policy:** the
  glyph only shows on an explicit, user-confirmed allowlist now (main menu,
  Campaign hub, Leave Lobby/Choose Content Pack popups) rather than every
  screen with a table entry, since an audit found several "done" screens were
  never actually re-confirmed. Coverage extended significantly via a 63-
  capture live-memory-dump batch (weapon-armory browsing, Barracks tabs,
  leaderboards, pause menu, in-game popups, Campaign mission sub-list) — see
  issue #51 for what's covered vs. still open.
8. **Investigated, not yet re-enabled (superseded by the reimplementation
  further below): vibration/rumble's original crash-on-startup cause.**
  Confirmed both replacement hook targets safe via decompile. See issue #24.
9. **Survival ready-up prompt read "Press Y" instead of "Hold Y", and could
  vanish this project's own hint text/glyphs entirely** when it overlapped
  another gameplay hint (single shared render slot). Gave ready-up/
  interact/Reload independent slots plus one explicit suppression rule. See
  issue #54.
10. **Custom cursor overlay stayed visible during real gameplay** with no
  mouse/keyboard input — four live-reported regressions fixed same day
  (controller-activity tracking centralized so menu nav counts as activity;
  a `WM_MOUSEMOVE` deadzone to filter native jitter; ultimately requiring
  this project's own "a real menu is open" signal before drawing at all,
  since the native `uiState` exclusion list it originally checked never
  actually covered plain gameplay). Confirmed fixed live. Buy-station cursor
  re-centering is a separate, still-open half of the same report. See issue
  #55.
11. **Menu text replacement (Quit's B-glyph, Leaderboards' controller binding)
  could hijack an unrelated menu item sharing the same label.** Fixed by
  requiring the match to also sit at the known corner-hint row position, not
  text content alone. See issue #56.
12. **Jump from crouch/prone didn't stand the player up first**, same root
  cause class as Sprint's own earlier fix. Now calls the real `ToggleStance`
  toggle on Jump's rising edge. See issue #59.
13. **CROUCH INPUT OCCASIONALLY STOPPED RESPONDING (regression), root cause
  confirmed live and fixed.** A stale "this is the menu's press" latch
  (`g_currentBPressTouchedMenu`) was only ever cleared by a separate,
  independently-polled edge tracker that could desync from the real button
  state — every crouch press was silently eaten once that happened, with no
  way to self-correct. This also explains the earlier "restored after
  melee/knifing" reports (coincidental timing, never causal). Fixed by
  giving `InjectControllerButtons`' own reliable rising-edge detection a
  second, independent path to clear the same latch. **Confirmed fixed live.**
  See issue #53.
14. **Turret placement prompt showed mouse text ("Press Left Mouse...") on
  controller** — missing a glyph-resolution alias for that literal display
  text. See issue #58; not yet live-verified.

### Documentation
1. **Corrected several stale entries found by a full sweep of
  `re_notes/known_issues.md`'s other open/investigating issues (2026-08-01).**
  Issue #23's planned real controller-options menu name was corrected from
  an unverified guess (`pc_options_controls_ingame`) to the actual
  zone-dumped name (`pc_options_controls`), and its `OpenMenuByName` hook
  is now noted as de-risked (run live without incident during issues
  #50/#51's work). Issue #7's Back/`+scores` and killstreak-Fire rows, and
  issue #28's own status line, no longer contradict each other or the
  actual confirmed-live results from issues #28/#29. Issues #34, #38, and
  #39 (font-glyph-injection mechanisms) are marked Resolved by supersession
  — issue #48/#50 shipped controller-glyph icons via a completely different
  mechanism (independent overlay quads) instead, so the font-injection path
  those three entries were chasing is moot rather than merely stalled.
  Issue #35's glyph-substitution half is similarly noted as orphaned by the
  same supersession, while its own diagnostic bug stays open on its own
  merits. Issue #38 also gained a note flagging a direct contradiction
  between its own static call-graph finding ("`FUN_00690c80` has exactly 2
  callers, neither menu-related") and this session's live evidence that
  menu text does go through that exact function — left as an open
  discrepancy, not silently resolved either way.

---

### Investigated, Not Yet Resolved
1. **Co-op money-sharing has no dedicated controller prompt.** A genuine gap
  (missed controller conversion, not broken functionality) but needs a live
  `proxy_d3d9.log`/screenshot capture of the real native prompt text before
  implementing — every other hint in this project needed that same ground
  truth to get right the first time rather than guessing. See
  `re_notes/known_issues.md` issue #57.
2. **Freeze while loading a map, crash reporter, then recovered.** Checked
  the full session's `proxy_d3d9.log` — no exception marker, no repeated
  device recreation, very little gameplay logged (consistent with the
  freeze happening early). Not linked to any of this session's changes;
  needs a repro with more detail before a real cause can be identified. See
  `re_notes/known_issues.md` issue #60.
3. **A-glyph could attach to the wrong item when a popup/modal is open on top
  of another menu** (e.g. the on-disk/DLC content picker over the Special
  Ops hub) — the background screen's own dimmed items get counted alongside
  the popup's real content, since nothing currently distinguishes which
  layer a given menu-text draw belongs to. Four dedicated Ghidra passes
  confirmed real engine structures (a genuine menu stack, and the active
  menu's own real item-count field) but couldn't close the specific link
  from an item to its screen position. Shipped a safe interim mitigation
  (suppress the glyph entirely whenever more than one menu is stacked,
  rather than risk a wrong position) plus live diagnostic logging to test a
  promising but unverified hypothesis. See `re_notes/known_issues.md` issue
  #51.

---

## v0.2.5 — Alpha (2026-07-31) — Hotfix: crouch/stance reliability + on-screen notifications

**Summary:** Hotfix release, renumbered from v0.2.3 the same day (this heading and every cross-reference to it were originally written as "v0.2.3"; no content changed as part of the rename itself) once the on-screen-notification work was folded in. Crouch/prone intermittently failing to fire is fully fixed across two rounds, both user-confirmed live, and a critical regression (couldn't fire while holding breath on a sniper) is fixed and user-confirmed live — these are the only actual behavior changes in this release; the research/investigation entries below accumulated since v0.2.2 and are included for completeness. Also ships this project's first working per-frame render capability, powering new startup/config-hot-reload on-screen notifications, plus split horizontal/vertical look sensitivity, a new close-range ADS slowdown knob for pistols/iron sights (issue #44, not yet live-confirmed at final values), and config auto-migration across upgrades.

### What's New
1. **On-screen top-right notifications: startup message + config hot-reload,
  five rounds same day, all user-confirmed live (2026-07-31, user-requested
  QoL, issue #47).** This project's first working per-frame render capability
  (`overlay_hud.cpp`/`.h`, hooks `EndScene` — confirmed genuinely alive,
  unlike the already-dead `Present`). Shows a startup message for 15 seconds
  on launch (a 1-in-3 roll picks one of four "vibes" variants homaging WaW's
  real documented hidden dev clan-tag codes — Gold, Rainbow, Sweep, plus a
  "Thanks For Supporting" text, issue #37), and a matching message whenever
  `mw3ncp_config.ini` changes on disk. **Config hot-reload is a genuinely new
  capability**, not just the message — config was load-once-at-startup-only
  before this. Text uses italic Barlow Condensed with a real black outline
  (`[Overlay] FontItalic` toggles italic); `[Overlay] TestCycleAllVariants`
  (strictly a testing toggle, default off) cycles every variant on demand.
  Real iteration to get here, each caught by an actual live test: GDI-on-
  backbuffer drawing failed on multisampled (AA) displays; the textured-quad
  rewrite that fixed that corrupted the intro cutscene via a leftover shader;
  the first Sweep/Gold visuals needed their own follow-up fixes once actually
  seen live (Sweep was ignoring the text's alpha entirely, painting a hard
  rectangular bar instead of a masked glint; Gold was a flat single color
  with no shading, reading as plain yellow — both fixed, Gold now a real
  light-to-dark gradient). Bumped `ConfigVersion` a 4th time (existing
  configs were already at the current version, which — unlike a version
  behind — meant new `[Overlay]` keys never got written into the real file
  even though the compiled defaults still applied correctly in memory; a real
  gap in the schema-versioning approach, not just an edge case). Full trail
  in `re_notes/known_issues.md` issue #47.
2. **Config auto-migration — existing `mw3ncp_config.ini` files carry settings
  forward across key renames AND retuned defaults instead of silently resetting
  (issue #45, 2026-07-31).** A new internal `[Meta] ConfigVersion` marker
  (`mod_config.cpp`) tracks schema revision; version-gated migration blocks run
  before the normal key reads, then the file is rewritten in the current format
  via the existing `WriteDefaultConfig()` (preserves every other tuned setting
  too). `ReadFloatWithDefaultRetune()` extends this to plain compiled-default
  changes (not just renames): only adopts a new default if the file's value
  still exactly matches the OLD default, otherwise respects it as a deliberate
  customization. Used three times the same day as `ConfigVersion` climbed
  0→1→2→3 (once for the sensitivity split below, twice more for issue #44's own
  two-round ADS-slowdown false start) — each retune correctly reached files
  already migrated by the prior round. Verified via standalone tests against
  copies of this install's own real config at each stage, plus a synthetic
  customized-value case. Builds clean; full in-game `LoadModConfig()` path not
  yet live-tested end-to-end. Full trail in `re_notes/known_issues.md` issue
  #45.
3. **Separate horizontal/vertical look sensitivity (task #14 follow-up, user request
  2026-07-31).** `[Look] Sensitivity` was one shared value driving both stick
  axes; split into `SensitivityHorizontal`/`SensitivityVertical`, matching
  console CoD titles' own independent sliders. `InjectControllerLookAngles` now
  computes yaw/pitch rates separately (still sharing the same ADS-zoom-slowdown/
  acceleration-ramp scale factors). An existing single-key config's value is
  carried over to both new keys via the config auto-migration above (issue #45).
4. **Retargeted the glyph-patch mechanism tests at `fonts/hudBigFont`, backed by real
  usage data (task #6/#34).** A live playtest of this session's `hud-font-id`
  diagnostic tallied every real font drawn during a long, clean Survival session:
  `hudBigFont` (7929 uses) dominates by a wide margin over `smallFont` (4860),
  `hudSmallFont` (2277), `extraBigFont` (1648), `objectiveFont` (1360), and the
  previously (wrongly) targeted `bigFont` (117, confirmed genuinely rare). Added
  `InjectFontStructDebugTest_HudBigFont` (`LB+RB+X`) and
  `InjectFontGlyphPatchTest_HudBigFont` (`LB+RB+B`) — identical, already-proven-safe
  mechanisms to the existing `bigfont` versions, just retargeted, on their own
  distinct combos so nothing collides. The `bigfont` versions are untouched. Builds
  clean; not yet live-tested. Rebuilding real new glyph pixel content for hudBigFont
  (as opposed to the borrowed-UV mechanism test) is a separate, still-unstarted step.
5. **One-shot visibility test for the hudBigFont glyph-patch, `LB+RB+Y` (task #6/#34
  follow-up).** The glyph-array patch (`LB+RB+B`) inserts a real, valid glyph entry
  into the live font, but nothing in the game actually draws text containing that
  codepoint, so the insert could never be visually confirmed on screen. Added
  `InjectFontGlyphVisibilityTest_HudBigFont`: hold `LB+RB+Y` (a third, distinct combo)
  to arm the already-installed, already-safe `Hook_DrawGlyphText` hook to rewrite a
  LOCAL COPY of the very next real hudBigFont draw call's text (appending the
  patched-in codepoint) and forward that copy instead of the original — the real
  string the game owns is only ever read, never written. Fires exactly once, falls
  back to a normal unmodified draw call on any exception. Requires the `LB+RB+B` patch
  test to have already run successfully this session; logs a clear message and still
  consumes the one-shot trigger if it hasn't. Tagged `[hudbigfont-visibility-test]`.
  Builds clean (0 warnings/0 errors); not yet live-tested. See
  `re_notes/known_issues.md` issue #34 for the full research trail, including the
  rejected alternative (piggybacking on a console-command anchor, found to not route
  through this draw path at all).
6. **ADS look-slowdown — pistols/iron sights barely slowed vs. 3x+ scopes, fixed
  via a genuinely separate close-range knob after two false starts (issue #44,
  2026-07-31, three rounds same day).** Live feedback: the zoom-aware ADS
  slowdown felt "heavily skewed towards 3x scopes." **Round 1** lowered
  `AdsSlowdownBaseline`'s default `0.65`→`0.45`, but the deployed
  `mw3ncp_config.ini` already had an explicit `0.65`, so the new default never
  reached the file (a plain compiled-default change can't override an existing
  explicit value the way a key rename can). **Round 2** added
  `ReadFloatWithDefaultRetune()` to fix that — `0.45` then reached the pistol,
  but made 3x+ scopes feel "too harsh": a single shared multiplicative constant
  scales every zoom ratio by the same relative percentage, so it can never fix
  low zoom without also over-slowing high zoom. **Round 3 (the real fix)**
  reverted `AdsSlowdownBaseline` to `0.65` and added a genuinely separate
  `AdsCloseRangeSlowdownStrength` (default `0.35`) that only meaningfully
  affects `ratio` close to `1.0` and decays to negligible at any real optic's
  zoom level, restoring high-zoom feel while still slowing pistols
  independently. Required bumping `ConfigVersion` to `3` to correct
  already-migrated files off the abandoned `0.45`. Verified via standalone
  tests at every round against copies of this install's own real config.
  Builds clean; **final values not yet live-confirmed together.** Full detail
  in `re_notes/known_issues.md` issue #44.
7. **Level-load-safe glyph-font-extension trigger (`Hook_FUN_0053cbc0`), task #6/#23
  follow-up.** New permanent hook on `FUN_0053cbc0` (a real, ordinary function
  confirmed via fresh disassembly — plain prologue/epilogue, no thunk involved —
  that fires exactly once per real level load/restart/checkpoint-reload). Read-only
  for now: calls the original completely unmodified, then logs every call with a
  running counter and the real map/mission name. This is the actual fix for the
  glyph-font-extension's known timing problem (loading real font content from the
  WndProc/`SetTimer` tick risks a black-screen GPU-resource-creation cascade) —
  riding inside a real engine-issued level-load call instead puts the load in the
  correct safe timing window. The already-implemented, idempotent
  `InstallGlyphFontExtension()` splice call is present in the hook but **left
  commented out/disabled by default** pending live confirmation of the diagnostic
  above — this project has crashed live twice before from adjacent boot/zone-loading
  mistakes, so "confirmed via disassembly" alone isn't this project's bar for
  shipping a mutating call enabled. Builds clean. **Not yet live-tested.** See
  `re_notes/known_issues.md` issue #23 for the full trail, including why the
  previous session's address-recovery approach (`Hook_FUN_00679680`) turned out to
  be solving a problem that didn't need solving.
8. **Bind-resolver text hook (`FUN_0061f6f0`), LOG-ONLY first pass (task #6/#35).**
  A new permanent hook now forwards every real bind-hint-text resolution
  (e.g. the interact prompts behind "Press [E] to pick up") through to the
  unmodified real game logic exactly as before, then logs what it observed
  (the resolved key/bind context and the resolved display text) to
  `proxy_d3d9.log`, deduped so an on-screen hint logs once per change rather
  than every frame. **No player-visible behavior changes at all in this
  pass** — no glyph substitution happens yet, this is deliberately the safe,
  incremental first step (following two earlier hooks that crashed the game
  live without one — see `re_notes/known_issues.md` issues #24 and #22/#30)
  toward real controller-glyph button prompts. New `[Experimental]
  BindResolverHookLogging` toggle in `mw3ncp_config.ini` (default on) silences
  the logging without touching the hook itself. Builds clean. **Live-tested
  (2026-07-21): safe — installs cleanly and a full play session ran with zero
  regression to boot or gameplay — but the captured `ECX`/output-buffer values
  read as implausible (0 and a bogus 0x100 pointer), so the logged data isn't
  usable yet.** See `re_notes/known_issues.md` issue #35 for the full trail and
  the follow-up needed before this is a foundation for real glyph substitution.
9. **Live HUD-text font identification hook (task #6/#34 follow-up).** The
  glyph-patch mechanism test's font target (`fonts/bigfont`) was already known to
  be wrong for real interact-hint/HUD text; static tracing this pass followed the
  real chain from the weapon-pickup hint builder down through a deferred
  render-command queue to the actual glyph-draw call, confirming the font is
  threaded as an explicit argument from a generic, data-driven HUD-element
  pipeline rather than selected via the menu system's `textfont` mechanism — but
  stopped short of the ultimate origin. Rather than keep chasing the static
  trace, a new permanent, read-only hook on `FUN_00690c80` (confirmed ordinary,
  safe to hook) now logs the real font name in use for on-screen HUD/menu text
  whenever it changes, which will empirically reveal the real interact-hint font
  the next time someone plays. New `[Experimental] HudFontIdLogging` toggle
  (default on). Builds clean. **Not yet live-tested.** See
  `re_notes/known_issues.md` issue #34 for the full trail.

### Fixed
1. **CRITICAL: can't fire while holding breath on a sniper (issue #46,
  2026-07-31).** Live-reported: Hold Breath + Fire together on a sniper just
  didn't work. Root cause found by cross-referencing this project's own
  existing research rather than fresh RE: issue #6 already established
  Hold Breath's "kbutton" address (`0xA98C04`) is literally Fire's own real
  kbutton's `down[1]` field, not an independent kbutton at all — and
  `kHoldBreathBindIndex` turned out to be `17`, IDENTICAL to Fire's own
  `kAttackBindIndex` (also `17`, defined in an unrelated part of the file,
  never cross-checked). Every Hold Breath engagement was writing the exact
  same bind-index Fire itself uses into Fire's own kbutton state, breaking the
  down/up edge-transition Fire's fire logic depends on. Fixed by changing
  `kHoldBreathBindIndex` to `18` (the only bind-index not already claimed by
  ADS/Reload/Sprint/Fire) — a single-constant fix, not a workaround. **User
  confirmed live: sniper + Hold Breath + Fire now works.** Full detail in
  `re_notes/known_issues.md` issue #46.
2. **Crouch/prone intermittently not firing on B — FULLY FIXED, two rounds
  same day, both user-confirmed live (issue #27 Bug #2 / issue #42, first CTA
  on dev resuming 2026-07-31).** A B tap/hold occasionally didn't change
  stance at all, no error, no feedback. **Round 1**: `ToggleStance()`'s two
  guard bytes can silently no-op the call; `RequestStanceToggle()`/
  `ProcessPendingStanceRetry()` verify and retry against the real stance field
  (up to 500ms) — fixed repeated intermittent failures during play, but the
  very first crouch attempt after launch still needed an initial "click"
  (connected to issue #1, day one of the mod, same bug class). **Round 2**:
  fresh Ghidra research confirmed the guard bytes are a genuine
  `IsStanceLocked()` pair with real native side effects (`FUN_0057d430`, the
  per-frame function this project's own movement hook sits on top of, forces
  stance to 0 and forces usercmd crouch/prone bits while locked), but no
  writer to either byte was found anywhere in the binary. Shipped
  `SendSyntheticActivationClick()` (`d3d9_hook.cpp`) instead — feeds a
  synthetic `WM_ACTIVATE`/`WM_SETFOCUS`/click sequence directly into the
  game's real `WndProc` (no OS focus stolen), testing the user's own "force
  focus through the engine" idea directly. **User confirmed live: fresh
  launch, never clicked the window, crouch fired on the first attempt.** May
  have also fixed issue #1 as a side effect (not independently re-tested).
  Full trail in `re_notes/known_issues.md` issues #1, #27, and #42.
3. **Glyph-visibility mechanism (task #6/#34): root cause fixed, pending live
  confirmation.** A real live test of the glyph-array patch plus the draw-string-append
  visibility hook (`LB+RB+B` / `LB+RB+Y`,
  `InjectFontGlyphVisibilityTest_HudBigFont`/`Hook_DrawGlyphText`) ran clean
  end-to-end (no crash, no exception — log confirmed the modified copy was built and
  forwarded) but produced no visible glyph on screen. Disassembled the real draw chain
  (`FUN_0047dfa0`, `FUN_00690c80`, `FUN_004db3e0`/`FUN_005323c0`, `FUN_00691ca0`/
  `FUN_0051b100`) fresh via this repo's own headless Ghidra scripts against the
  existing `MW3.gpr` project. Ruled out a stale glyph cache (the lookup is a genuine
  live per-character search against the real array on every call) and ruled out a
  signed-char bug (every stage in the chain treats the byte as unsigned; the
  float-typed hand-off between functions is a bit-preserving reinterpret, not a value
  conversion). Found the real cause one level upstream: `FUN_00690c80`'s draw loop is
  gated by an explicit character-count parameter (`param_10`) captured once, at
  enqueue time, by the HUD text ring-buffer writer (`FUN_0051b100`'s own `strlen`
  call) and replayed unchanged by the reader (`FUN_00691ca0`) on every subsequent
  draw — our hook on the draw call itself fires downstream of that round trip, so
  appending a byte to a local string copy without also incrementing that same
  captured count guarantees the loop exits exactly one character short of the
  appended byte, silently, every time. Also found byte `0x81` (the codepoint
  originally used by `InjectFontGlyphPatchTest_HudBigFont` before the
  runtime-discovery fix) has one narrow, locale/case-mode-dependent corruption path
  in `FUN_004db3e0` that codepoint `0xA0` (the codepoint actually in use now) does
  not — good independent confirmation `0xA0` was the right choice. **Fix applied**:
  the visibility-test hook now forwards `param_10 + 1` (instead of the original,
  unmutated `param_10`) alongside the already-appended string byte, matching the real
  draw loop's gate to the actual appended-string length. Builds clean (0 warnings/0
  errors, Release/Win32). **Not yet live-tested** — a clean build only confirms the
  code compiles, not that the glyph actually renders; the remaining step is holding
  `LB+RB+B` then `LB+RB+Y` again in a live session and visually confirming the
  borrowed 'A' glyph appears. Full trail in `re_notes/known_issues.md` issue #34.
  **Retested 2026-07-22, still no visible glyph — but the retest itself was
  invalid, not the fix**: the `param_10 + 1` fix was built inside an isolated git
  worktree, whose `proxy_d3d9.vcxproj` (`OutDir` = `..\..\` relative to
  `proxy_d3d9/`) resolved to that worktree's own directory tree, not the real
  game root — so the deployed `d3d9.dll` next to `iw5sp.exe` was still the
  pre-fix build when the retest happened (confirmed via file timestamp: deployed
  DLL was dated the prior day, hours before the fix was even written). Rebuilt
  directly from the real checkout, which correctly deployed the fix-containing
  DLL to the game root. **Still not live-tested against the actual, correctly-
  deployed fix** — that test is still pending. See `CONTRIBUTING.md`'s Building
  section for the now-documented worktree/`OutDir` gotcha so this doesn't recur.
4. **hudBigFont glyph-patch mechanism test hardcoded codepoint 0x81, which collided
  with a real existing glyph and silently never fired (task #6/#34 follow-up).** A
  live playtest of `InjectFontGlyphPatchTest_HudBigFont` (`LB+RB+B`) logged
  `"codepoint 0x81 already exists at index 128 -- aborting, nothing to insert"` —
  unlike `fonts/bigfont`, `fonts/hudBigFont` already has a real glyph at 0x81 (this
  font has 254 real glyph entries total, likely full extended-Latin coverage for
  localization), so the insert-and-repoint path never actually ran. The abort-on-
  collision logic itself was correct and intentional (never clobber a real existing
  glyph) — the bug was purely a bad hardcoded "surely unused" assumption. Fixed by
  scanning for a genuinely free codepoint at runtime instead: starting from 0x81, the
  existing sorted-insertion-point search now also walks a `candidate` codepoint,
  bumping it past any real entry it collides with, so the first entry greater than
  the (possibly bumped) candidate both proves it's free and gives the correct
  insertion index in one pass. Logs now report whichever codepoint was actually
  chosen instead of a hardcoded reference; if every codepoint from 0x81-0xFF is
  somehow taken, it logs a clear abort instead of looping forever or writing out of
  bounds. Also widened the function's log buffer (`char buf[200]` -> `char buf[400]`)
  since the final "patch applied" message is now built via `sprintf_s` with a runtime
  value and is long enough (~330 chars) that the old 200-byte buffer would have made
  `sprintf_s` overflow and abort. Only `InjectFontGlyphPatchTest_HudBigFont` changed
  — the original `fonts/bigfont` test still hardcodes 0x81, which is fine there since
  bigfont has no glyph at that codepoint. Builds clean (0 warnings/0 errors);
  **live-verified**: a follow-up playtest confirmed the fix works — log showed `"built
  replacement array (254 -> 255 entries), inserted codepoint 0xA0 at index 159,
  repointing live Font_s now"`, no crash. hudBigFont's real coverage turned out to run
  contiguously from 0x81-0x9F with no gaps, so the free codepoint found (0xA0) was
  higher than the 0x82 originally predicted. See `re_notes/known_issues.md` issue #34
  for the full trail.
5. **Bind-resolver hook (`FUN_0061f6f0`) logged implausible data for one real caller;
  root-caused and fixed (task #6/#35 follow-up).** The live-test above traced to a
  genuine, disassembly-confirmed cause, not a bug in the hook's own register-reading:
  one of `FUN_0061f6f0`'s 4 real callers, `FUN_00622970` (a key-rebind-capture UI —
  "waiting for the next physical key press to bind"), pushes its arguments in a
  different shape than the other 3 (its real buffer pointer at `[esp+4]`, the
  buffer's *size* at `[esp+8]`) — the reverse of the convention this hook assumed,
  which is why `[esp+8]` read as the literal `0x100`. `BindResolverLogAfterCall` now
  detects this specific caller by its confirmed real return address and logs a
  one-time note instead of misleading placeholder data; the hook's behavior for the
  3 genuine hint-resolution callers is unchanged. Also fixed an independent dedup bug
  found in the same pass: repeated identical log lines weren't being suppressed
  whenever the buffer read as implausible, because the old dedup check was itself
  gated behind a successful-read flag that never got set in that case — fixed by
  comparing/updating on the logged text directly, unconditionally. Builds clean;
  **not yet live-tested** (this fix's confidence comes from tracing both callers'
  real disassembly instruction-by-instruction, not a fresh playtest) — see
  `re_notes/known_issues.md` issue #35 for the full trail.

### Documentation
1. **Corrected the button-glyph font-patch test's target-font assumption (task #34).**
  The 2026-07-18 plan picked `fonts/bigfont` as "the best single guess for menu-title
  text" for the still-untested LB+RB+A glyph-patch mechanism test. A fresh Ghidra
  decompile of the real `textfont`-value-to-font selector, cross-checked against a
  tally of every real `textfont` line across all 512 dumped `.menu` files, proves that
  guess wrong: the main menu's real title/button text uses smallfont/hudsmallfont, not
  bigfont. Bigfont is real but is used in only 3 places anywhere in `ui.ff`, all on the
  brightness-calibration screen, which the game only opens once per player profile
  ever — not the repeatable, always-visible test vehicle earlier notes assumed. No
  code behavior changed (the struct-layout diagnostic is font-agnostic and stays
  targeted at bigfont); added correction comments in `analog_input_hooks.cpp` and a
  full writeup, including the resolved `textfont` enum table, in
  `re_notes/ui_assets.md`. The mechanism test still cannot be visually confirmed yet —
  see `re_notes/known_issues.md` issue #34 for what's still needed.

---

### Groundwork
1. **Bind-resolver glyph-substitution groundwork, OFF by default (task #6).** Built
  the key-name-text → controller-glyph-codepoint substitution logic that the
  bind-resolver hook (`FUN_0061f6f0`) needs to eventually show real button icons in
  interact hints, plus a new `GlyphStyle` config option (`Xbox360`/`XboxModern`/
  `PlayStation`, same pattern as `ButtonLayout`/`StickLayout`) so players can pick
  their preferred icon look independent of physical controller brand. New
  `[Experimental] BindResolverGlyphSubstitution` toggle, **default off on purpose** —
  no font asset the game can currently load renders these codepoints yet (see
  `re_notes/known_issues.md` issue #23's still-open safe-loading problem), so
  enabling this today would show missing-glyph boxes instead of readable text. Also
  adds a small `Controller_IsConnected()` helper (`controller_input.h`/`.cpp`) since
  no "is a controller currently active" flag existed anywhere in this codebase
  before. Builds clean; not live-tested (feature is inert by default regardless) —
  see `re_notes/known_issues.md` issue #35 for the full design trail, including a
  real, honestly-documented gap (no Xbox360-style left-stick/right-stick-click icon
  assets exist, so Sprint/Melee glyphs are unmapped for that one style).
2. **Boot-thunk resolution diagnostic** (`analog_input_hooks.cpp`, `Hook_FUN_00679680`),
  task #23 follow-up toward a real native controller options menu. Read-only,
  wired live: hooks `FUN_00679680` (a real, ordinary function, confirmed safely
  trampolineable — not the `FUN_004ca310` incremental-link thunk that crashed the
  game live in a prior attempt, see `re_notes/known_issues.md` issues #22/#30),
  lets the original run completely unmodified, then logs (a) the existing plan's
  `DAT_008501e8`-based formula and (b) a more direct, more reliable reading — the
  raw bytes at the real `LoadZones` call site itself, decoded as a `CALL rel32` —
  which reveals whether the engine's own MSVC-incremental-link self-patching
  behavior has replaced that call site with the true resolved `LoadZones` address.
  Zero mutation of the zone-loading path itself. Builds clean (0 warnings/0
  errors, full rebuild). **Live-tested (2026-07-21), reproduced across two
  launches: safe (zero regression), but the self-patch theory is REFUTED at
  this specific call site** — the decoded target is still the raw thunk, not
  a resolved address. A genuine negative result, not a bug; the real
  address-recovery approach for the actual options-menu splice needs
  rethinking before that work can proceed. See `re_notes/known_issues.md`
  issue #23 for full detail, including the correction to the original plan's
  `DAT_008501e8` formula found while implementing this.

### Investigated, Not Yet Resolved
1. **Roadmap idea, not implemented: the pre-native Sprint implementation's
  "sprint while crouched" side effect, recalled 2026-07-31 (issue #43).**
  Before Sprint moved onto the real `+sprint` kbutton (issue #6/#9), the
  earlier raw `pm_flags`-bit-forcing implementation had a side effect of
  letting the player sprint while crouched — not real vanilla MW3 behavior,
  and not reproduced by the current native-kbutton Sprint. Not a bug, not
  part of the mod today, and reviving the old bit-forcing approach generally
  is off the table (it's what fought real engine state elsewhere and got
  replaced) — logged purely as a memorable data point in case a deliberate,
  properly-built future feature (e.g. a real tac-sprint/crouch-sprint
  variant) is ever wanted. No code changed. Full detail in
  `re_notes/known_issues.md` issue #43.
2. **AC-130 (Iron Lady / Fire Mission) — confirmed working on controller except
  gun-type switching; gunship camera zoom sensitivity flagged for later (task
  #7, issue #40, live playtest 2026-07-23).** User-confirmed live: flight/
  camera control and firing all work fully on controller, no fallback needed.
  Two open gaps logged: (1) switching between the gunship's cannon types
  (105mm/40mm/25mm) doesn't work on controller — real native trigger
  (kbutton, raw-keycode dispatch, or GSC notify) not yet found; (2) look
  sensitivity isn't scaled to the gunship camera's own zoom level, feeling
  overly sensitive when zoomed in versus un-zoomed — scoped specifically to
  this camera, not general weapon ADS, and parked on the roadmap, not yet
  investigated. No code changed, no RE performed yet — this is the finding
  only. Full detail in `re_notes/known_issues.md` issue #40.
3. **Font-zone injection (`InstallGlyphFontExtension`, the real-new-art glyph
  mechanism) — its own enable precondition is now confirmed met live, but it's
  still targeting the wrong font (task #23/#34/#38, issue #39).** This project has
  TWO separate glyph mechanisms: the already-tested array-patch-with-borrowed-glyph
  technique (issue #34, hudBigFont), and this one — loading a genuinely NEW,
  `Linker.exe`-built font zone with REAL new glyph art via `LoadZones`, then
  repointing a real font's `glyphs`/`material`/`glyphCount` fields at it. The
  splice call (`InstallGlyphFontExtension()`) has been commented out pending "a
  read-only diagnostic confirmed live across at least one real level load" — a
  fresh read of `proxy_d3d9.log` (both sessions today) shows the level-load hook
  it depends on (`Hook_FUN_0053cbc0`) HAS now fired cleanly, twice, with the
  correct map name and zero regression afterward — that precondition is met and
  wasn't previously recognized as such. Materials (which a font necessarily
  carries) trigger a real, already-root-caused D3D9 GPU-resource-creation cascade
  if loaded from the wrong timing context (the cause of both a prior boot-splice
  crash and a black-screen-flash bug) — this is exactly why the call was moved to
  this specific, now-confirmed-safe level-load hook in the first place. **Still
  targets `fonts/bigfont`** (confirmed the rarest font in the whole UI, per issue
  #38) — needs retargeting to build against `fonts/smallFont` before it's worth
  enabling, or the mechanism will prove out on a font players essentially never
  see. No code changed this pass — pure research/documentation. Full trail,
  including the concrete 4-step next-implementation sequence, in
  `re_notes/known_issues.md` issue #39.
4. **Controller-glyph feature retargeted at the real UI (menu entries + interact
  prompts), away from the hudBigFont ammo-counter test bed (task #34/#35, research
  pass, issue #38).** Consolidated this project's own prior RE findings into one
  picture: menu-entry text is font-selected via a `textfont` int (`smallfont`
  dominates real usage 4243:866 over `hudbigfont` in a full 512-menu corpus tally;
  `bigfont`, the original mechanism-test target, is the rarest font in the game);
  interact-hint text (weapon pickups, stance prompts) uses a separate, data-driven
  font argument but funnels through the exact same universal draw call
  (`FUN_00690c80`) already reverse-engineered against the hudBigFont ammo counter —
  so that pipeline knowledge (including the `param_10` ring-buffer gotcha) directly
  transfers, no re-derivation needed. **New finding**: the real bind-resolver
  glyph-substitution mechanism (already built, off by default) substitutes a bind's
  key-name text *before* it's measured/enqueued, unlike the hudBigFont diagnostic
  test's after-the-fact string append — meaning the diagnostic's still-open
  no-render bug may be specific to its own test technique, not a defect blocking the
  real feature. Recommended next steps: solve the real font-loading blocker first,
  retarget any further mechanism testing at `smallFont` (the real dominant UI font,
  not `hudBigFont`), then test via the real bind-resolver pathway end-to-end rather
  than the manual test combos. No code changed this pass — pure research/
  consolidation. Full trail in `re_notes/known_issues.md` issue #38.
5. **Bind-resolver glyph-substitution safety re-verified fresh via Ghidra (task
  #35 follow-up).** Rigorously re-checked (not just re-read) the claim that the
  real bind-resolver substitution mechanism sidesteps issue #34's ring-buffer
  no-render bug. Fresh decompiles confirm: `FUN_00433a10` (the `&&N` splicing
  engine) treats the substituted bind-text value as a pure byte-for-byte copy
  with zero content interpretation beyond its own null terminator, and measures
  that value's length fresh, synchronously, on every call — there is no
  separate capture-once/replay-later step analogous to issue #34's
  `FUN_0051b100`, so the ordering safety is structurally guaranteed, not just
  probable. **Real gap found**: the provisional glyph-codepoint table
  (`0x82`-`0xA9`) has never been individually checked against the same
  `FUN_004db3e0` locale/case-folding corruption pattern that ruled out `0x81`
  and ruled in `0xA0` earlier — recommend vetting each provisional codepoint (or
  simply remapping the table onto a run of already-confirmed-clean codepoints)
  before enabling the feature for real. No code changed, no rebuild needed.
  Full trail in `re_notes/known_issues.md` issue #35.
6. **Correction to the above (task #38, follow-up pass, same day): menu-entry text
  does NOT share the interact-hint pipeline after all.** The prior pass's tentative
  "menu text likely shares `FUN_00690c80`" conclusion was based on indirect runtime
  evidence and flagged as unconfirmed; a static call-graph enumeration (every real
  caller of `FUN_00690c80` and of the glyph-lookup function `FUN_0047dfa0`, via
  Ghidra headless) settled it definitively instead: `FUN_00690c80` has exactly 2
  real callers — the known ring-buffer consumer, and a developer performance-
  overlay function, nothing menu-related — and `FUN_0047dfa0` has exactly 6, none
  in the menu/itemDef module. Menu text uses a fully separate rendering path start
  to finish (confirmed a real itemDef-text call chain: `FUN_0061e0f0` →
  `FUN_005181e0` font resolve → `FUN_00429dc0` → `FUN_004e9350` measure — the actual
  glyph-emit call past this point wasn't found this pass). Practical upshot: the
  `param_10` ring-buffer fix and all existing array-patch groundwork apply to
  interact-hint text (issue #35's bind-resolver path) but NOT to menu item labels,
  which need their own, still-unmapped draw-call investigation before any glyph
  substitution can target menus specifically. No code changed. Full trail in
  `re_notes/known_issues.md` issue #38's "CORRECTION" section.
7. **WaW-style animated dev clan tags — new roadmap idea, feasibility research only
  (issue #37).** Brought over World at War's ~22 hidden animated "dev" clan-tag magic
  words (`....`, `****`, `MOVE`, `RAIN`, `CYCL`, `CYLN`, etc. — real, citable subset
  documented, not a complete roster) as a candidate feature for this project, built
  from scratch as our own native-code implementation rather than porting WaW's
  original save-glitch mechanism. Confirmed MW3's own real clan-tag system
  (`eliteClanTagText`/`clanPrefix`) is Activision Elite-branded **networked
  lobby-session presence data**, not a plain local string — a bad fit given SP/
  Survival's offline scope and the Elite backend already being dead. The one local,
  natively-populated candidate found (`self.playername`) is too narrowly scoped
  (one pre-mission lobby screen) and not well-understood enough (native population
  point untraced) to build on yet. Recommendation: build a fully separate,
  project-owned overlay (own config toggle, own animation timer, same "build our own
  layer" precedent as the Sprint stamina timer) instead of reusing either native
  path. **Biggest blocker, generic to any future "draw something custom every
  frame" feature in this project, not just this one**: no per-frame D3D9 render hook
  is confirmed alive today — `Present` is confirmed dead (Steam Overlay suspected),
  and the untried alternative (`EndScene`, or piggybacking the game's own native
  HUD-text-draw call from an existing tick hook) needs its own dedicated
  investigation first. No code shipped this pass — pure research/scoping, see
  `re_notes/known_issues.md` issue #37 for the full trail, sources, and integration
  plan.
8. **Bind-resolver hook (task #6/#35): residual garbage-log occurrence, root cause not
  found.** A real playtest after the `FUN_00622970` return-address fix showed the fix
  working far better (1 garbage line all session vs. 51+ before), but that one
  occurrence still wasn't caught by the skip check. A fresh, from-scratch Ghidra
  re-verification confirmed exactly 4 real callers exist (no 5th), `FUN_00622970` has
  only its one known call site (no second), and the return-address constant
  (`0x006229AC`) is exactly correct — ruling out the obvious explanations. Root cause
  remains unconfirmed (a message-pump-driven reentrancy is suspected but not proven).
  Shipped a concrete improvement regardless: the diagnostic log line now includes the
  actual observed return address, so the next occurrence will show real data instead
  of requiring another round of inference.

---

## v0.2.2 — Alpha (2026-07-20) — Risk-mitigation release

**Summary:** Risk-mitigation release. Aim assist (rotational friction + magnetism, reading live entity/target memory) has been permanently removed, not just disabled, following deep VAC/anti-cheat research this session — including a previously-unknown Demonware `bdAntiCheat` system found compiled into both `iw5sp.exe` and `iw5mp.exe`. Converged on one decisive finding: real-world anti-cheat ban risk for a proxy-`d3d9.dll` project tracks the DEPTH of what the proxy does once loaded, not the loading technique — ReShade-style visual-only proxies have an essentially clean ban record, ENB-style proxies that "go deeper" into gameplay/system state carry real, documented ban history. This project's own core input-remapping work (movement/look/buttons) only writes real input values into real input structures and never reads gameplay-entity memory; aim assist did, putting it in that riskier ENB-like category regardless of how console-authentic the intent was, with no way to make it safer without changing what it fundamentally is. Cut entirely rather than reworked — full research trail in `re_notes/known_issues.md` issue #33. No other player-facing feature changes beyond v0.2.1.

**⚠️ If you're on v0.2.1 or earlier**: those builds shipped with the aim-assist
code compiled into the DLL, disabled by default but present in the binary.
Whether that presence alone meaningfully raised VAC exposure over this release
is **not confirmed — no ban has been reported — but is genuinely suspected
enough that upgrading to v0.2.2 or later is recommended.** Treat this as a real,
disclosed risk, not a confirmed incident.

### What's New
1. **Aim assist (rotational friction + magnetism target correction) — permanently
  deleted, source and config both.** Was never shipped functional (broken target
  classification, disabled by default since v0.1.2) and is now gone entirely
  rather than fixed — see the risk-mitigation reasoning above. Removed:
  `analog_input_hooks.cpp`'s entire aim-assist implementation (entity array
  reads, target classification, friction/magnetism math) and its call site in
  `InjectControllerLookAngles`; `mod_config.h`/`.cpp`'s `[AimAssist]` section
  (`Enabled`/`Range`/`ConeDegrees`/`FrictionStrength`/`MagnetismDegreesPerSecond`)
  and all INI read/write/log support. An existing `mw3ncp_config.ini` with a
  leftover `[AimAssist]` section is harmless — those keys are simply no longer
  read.
2. **Automated Nexus Mods file upload on release.** New GitHub Actions
  workflow (`.github/workflows/nexus-upload.yml`) fires on every published
  GitHub Release, downloads that release's zip asset, and pushes it to
  Nexus as a new file version via Nexus's official Upload API action —
  keeps the GitHub and Nexus downloads in sync without a manual re-upload
  step. Covers the file only; page text (description/changelog/credits)
  still needs manual updates per `nexus/README.md`'s checklist.

---

### Documentation
1. Added a prominent security notice to `README.md`'s top and status banner
  covering the above, and corrected every aim-assist reference throughout the
  file (Status at a glance, Scorecard, Feature List, config table, Known
  Limitations, the Plutonium warning) to reflect permanent removal rather than
  "disabled/non-functional."
2. Added `nexus/` — the source-of-truth for this project's Nexus Mods page
  (summary, BBCode description, BBCode changelog, page metadata), kept
  alongside `README.md`/`PATCHNOTES.md` so the Nexus listing can't quietly
  drift out of sync with the project's actual current status. See
  `nexus/README.md` for the per-release update checklist.

---

## v0.2.1 — Alpha (2026-07-20)

**Summary:** Two headline items, both confirmed live. A console-accurate look acceleration ramp (corrected to 33ms, one 30fps engine frame, after an initial 200ms guess from external research proved wrong) — tied to this old engine's own locked tick rate, not an arbitrary wall-clock duration. Hold Breath (L3 while ADS'd) is fully fixed after an extensive live-debugging saga (two failed direct-kbutton attempts, a detour through a 4th key-synthesis exception, and finally a live memory readback that isolated the real bug — a single kbutton_t byte that never followed `KeyUp`) — now shipping as genuinely native input again, dropping this project's "OS-level input emulation exceptions" count back to 3 (Survival ready-up's F5, D-pad Left's `'4'`, Back's TAB).

### What's New
1. **Look acceleration ramp, matching real console MW2/Black Ops behavior
  (2026-07-19/2026-07-20, issue #32).** This project's controller look
  previously had zero acceleration/smoothing — a flat, instant rate, by
  original design. External research found that MW2 and Black Ops (same
  IW-engine lineage immediately around MW3) applied a real linear
  turn-speed ramp on every stick input. Implemented as a new
  `[Look] AccelerationRampMs` config value, **set active by default for
  live playtest** rather than opt-in-only — 0 disables it entirely for a
  clean revert. First shipped at 200ms (the ~0.2s figure from the external
  research); **live-tested against real hardware across many values
  (2026-07-20) and confirmed 200ms was wrong** — the real ramp is tied to
  this old engine's own locked 30fps tick (33.33ms/frame), not an
  arbitrary wall-clock duration. **Default corrected to 33ms (one engine
  frame), confirmed live as the right feel — this is now the shipped
  default.**
2. **Hold Breath (L3 while ADS'd), two direct-kbutton attempts both failed
  live, fixed via a 4th key-synthesis exception instead (2026-07-19, task
  #24).** First attempt drove the real kbutton (`0xA98C04`) directly via
  `CallKbuttonDown`/`CallKbuttonUp` — live playtest found it engages once
  and never releases. Second attempt root-caused a real, confirmed bug in
  the engine's own `KeyUp` function (a second kbutton flag byte, `+0x11`,
  that `KeyDown` sets but `KeyUp` structurally never clears) and manually
  zeroed it ourselves — **still confirmed stuck live**, meaning that real
  bug wasn't the (or wasn't the only) actual cause. **Real fix**: stopped
  driving the kbutton directly altogether and synthesized a real Shift
  keypress instead (the same real bind a keyboard player's Shift press
  takes), only while ADS'd — the fourth exception to this project's "no
  OS-level input emulation" rule, alongside Survival ready-up, D-pad Left's
  squadmate call-in, and Back's scoreboard. Sprint's own kbutton path was
  also updated to exclude ADS, so the two paths can never double-claim the
  same kbutton simultaneously (a real Shift press also fires Sprint's
  kbutton natively, same as it does for a real keyboard player, harmlessly
  ignored by the engine while aiming). **Still confirmed stuck live even
  after this fix** — surprisingly, reproduced on PURE keyboard/mouse with
  zero controller involvement at all, which should have made every one of
  this project's controller-gated features a complete no-op. Root-caused
  to an unrelated cause entirely: two research-only diagnostic hooks added
  earlier this session for the Predator Missile guidance investigation
  (`Hook_ControlsLinkTo`, `Hook_MissileGuidanceDispatch`) run every frame
  unconditionally and were corrupting shared engine state. **Disabled both
  — Hold Breath confirmed working live afterward**, including the toughest
  test (controller ADS held simultaneously with a real keyboard Shift
  press). Disabling those two hooks let a RETEST of the plain direct-kbutton
  approach happen — **still confirmed stuck**, proving a second, independent
  bug also existed. A dedicated Ghidra pass found the real, definitive
  cause: `0xA98C04` was never an independent kbutton at all — it's
  literally Fire's own `down[1]` slot (`0xA98C00 + 4`), and its `+0x10`/
  `+0x11` fields physically coincide with a completely unrelated function
  (`FUN_0057dc90`, the per-frame simple-bind reader) that unconditionally
  zeroes that exact byte every single frame for its own unrelated bind.
  Two genuine engine subsystems unknowingly share one memory region — this
  can never be made reliable via direct kbutton calls, confirming the 4th
  key-synthesis exception (now reinstated) is the only real fix, not a
  workaround. **User-confirmed as the adequate permanent fix — task #24
  closed.** See `re_notes/known_issues.md` issues #6 and #30 for the full
  trail — Predator Missile guidance work (task #30/#31) is now blocked on
  finding a safer replacement for the two disabled diagnostic hooks before
  it can resume.
3. **Real font-extension mechanism for button glyphs, attempted and DISABLED
  before ever being live-tested (2026-07-19, task #6/#31).** Rebuilt the
  extended glyph font under entirely unique asset names
  (`bigfont_glyph_ext.ff`) to avoid the same-name asset-interning collision
  that blocked the earlier boot-splice and menu-override attempts, and
  implemented loading it via a direct (non-hooked) zone-load call from the
  same safe tick the project's own `roundtrip.ff` test already proved safe,
  then repointing the real font's material/glyph fields at the loaded
  extension. Caught a direct contradiction with already-documented research
  before shipping it: the new zone contains materials, and loading
  material-bearing content from that same tick was already found to
  trigger unsafe D3D9 GPU-resource creation outside the engine's own
  frame/thread discipline. Disabled before the user ever tested it. See
  `re_notes/known_issues.md` for the full trail and the one remaining safe
  path this hasn't tried yet (routing through a real level-load
  transition).
4. **Boot-time zone splice for the extended button-glyph font, attempted and
  DISABLED after a confirmed live crash (2026-07-19, task #31/#6).** Hooked
  the real zone-loading entry point (`FUN_004ca310`) to splice this
  project's extended `bigfont_ext` font zone into the real boot-time zone
  queue. Built clean, but the actual live boot crashed — same crash
  signature as the earlier rumble-hook crash (every hook reports
  successful install, then an immediate detach with zero gameplay activity
  ever logged, before this hook's own splice log line could even appear).
  **Disabled** (code kept, not shipped active) and the zone file removed
  from the live install to fully revert. Root cause not yet found — this
  is only the "get the font asset loaded" half of real button-glyph
  rendering in any case; the bind-resolver hook that actually swaps hint
  text for a glyph codepoint is separate, still-unstarted work (see
  `re_notes/ui_assets.md` and `re_notes/known_issues.md`).

### Fixed
1. **Hold Breath regressed live ("perma on"), 40ms native-transition debounce
  added (2026-07-20, task #24 reopened).** After being closed 2026-07-19,
  Hold Breath got stuck on again live. The diagnostic log for that session
  showed this project's own tracking behaving correctly this time (a clean
  synthetic-Shift `UP` was sent, no stuck-true state on our side) — a
  different failure mode than the original bug. User confirmed releasing L3
  did nothing once stuck, describing it as behaving "like even native,"
  meaning the native breath-hold state itself latched on despite our release
  firing correctly. The same log showed bursts of synthetic key transitions
  landing inside the same or adjacent engine frame — faster than the
  30fps-locked engine (33.33ms/frame, see the look-ramp fix above, found the
  same day) can be assumed to cleanly process; a `WM_KEYUP` posted too soon
  behind a `WM_KEYDOWN` is a plausible way for the native handler to
  silently drop the release. Added a 40ms debounce around
  `SendSyntheticHoldBreathKey` so a transition is only actually sent once at
  least one engine frame has passed since the last one sent, coalescing any
  faster flicker instead of forwarding it. Builds clean — **not yet
  live-tested**; if stuck-on recurs even with the debounce, this theory is
  wrong and the real native consumer of the key state needs further RE.
2. **Debounce theory falsified live, escalated PostMessage -> SendInput
  (2026-07-20, task #24 still open).** The debounce did not fix it — a
  retest logged a single, cleanly-spaced key cycle (well past the 40ms
  debounce, no flicker at all) that still reportedly latched on, and going
  in/out of ADS afterward didn't clear it. New theory: `PostMessage` only
  queues a window message, it never touches the OS-level keyboard state
  table `GetKeyState`/`GetAsyncKeyState` read — fine for the other 3
  one-shot key-synthesis exceptions, but Hold Breath needs a SUSTAINED
  "is this key currently down" read every frame, which the native code may
  be checking via a real keystate poll instead of `WM_KEYUP`. Switched to
  `SendInput` (a real `INPUT_KEYBOARD` event), which does update that OS
  keystate table, gated on the game holding OS foreground focus since
  `SendInput` is system-wide rather than window-scoped. Builds clean —
  **not yet live-tested**.
3. **SendInput ALSO confirmed stuck live, diagnostic-first pivot instead of a
  third blind fix (2026-07-20, task #24 still open).** Two different
  transport mechanisms (PostMessage, SendInput) now fail identically,
  meaning transport was never the actual problem — something after the key
  event reaches the native engine is at fault. User's own hypothesis: this
  project's own Sprint-kbutton code (driven directly on the same `0xA98CCC`
  the native dispatch also touches internally whenever the synthetic Shift
  lands) might be the real interference, not Hold Breath's own known-
  corrupted `0xA98C04` alias — a lingering Sprint kbutton down[] slot would
  explain a 100%-reproducible stuck symptom better than a timing race would.
  Rather than guess a third fix, added a real-memory kbutton_t readback
  (`ReadKbutton`/`AppendKbuttonSnapshots`) logging `down[0]`/`down[1]`/
  `active`/the `+0x11` flag for BOTH addresses on every Hold Breath
  transition, and widened the heartbeat to cover the WHOLE ADS window
  instead of going silent the instant our own tracking releases — the
  previous heartbeat left exactly the window where the effect is reported
  stuck completely uninstrumented. Builds clean. **Diagnostic only — not
  yet live-tested.**
4. **Diagnostic returned a conclusive answer, force-clear fix shipped
  (2026-07-20, task #24 still open).** The readback log ruled out the
  Sprint-kbutton theory — `0xA98CCC` toggles cleanly the entire session —
  and isolated the real culprit: `0xA98C04`'s (Hold Breath's alias) `active`
  byte (+0x10) latches to `1` on the very first release and never returns to
  `0` again, even though its own `down0`/`down1` keep cycling correctly the
  whole time. Added `ClearHoldBreathActiveFlag()`, force-clearing that byte
  right after every synthetic release plus a continual per-frame self-heal
  while Hold Breath isn't supposed to be engaged. Builds clean — **not yet
  live-tested**. A pure native (no key-synthesis) variant is the natural
  next step if this holds up, held off shipping in the same build to avoid
  confounding which change actually fixed it.
5. **CONFIRMED FIXED LIVE — task #24 closed (2026-07-20).** User confirmed
  the force-clear resolved Hold Breath completely. Also corrected this
  project's own description of the effect: it's aim STEADYING while breath
  is held (not weapon-sway reduction), with accuracy dropping noticeably
  once breath runs out — the user had zero control over the state once it
  locked on, matching the "active flag latched forever" root cause exactly.
6. **Third native attempt for Hold Breath, user-requested — CONFIRMED WORKING
  LIVE, task #24 permanently closed (2026-07-20).** With the real fix
  (force-clearing `0xA98C04`'s `+0x10` byte) already confirmed working via
  the synthetic-Shift path, tried driving the kbutton directly again
  instead — same approach as the first (failed) attempt, now paired with
  the proven `+0x10` clear that attempt never had. User confirmed: "it
  works natively as intended." Hold Breath now drops the 4th key-synthesis
  exception entirely — genuinely native input again, not emulation. Removed
  the now-dead `SendSyntheticHoldBreathKey`/`SendInput` code as cleanup
  (fully recoverable from git history if ever needed).

### Documentation
1. Noted user-reported (Reddit, 2026-07-19, unverified by this project)
  reports that this project's proxy `d3d9.dll` works against retail Steam
  MW3 running under Proton on Steam Deck/Linux, including a direct report
  of testing it live on real Deck hardware — added to `README.md`'s client
  compatibility table as community-confirmed-but-project-untested, not a
  supported claim.

---

### Groundwork
1. **Font-struct read-only diagnostic (2026-07-19, task #6/#31/#32 follow-up).**
  After the boot-splice crash above, a 6-fork research pass found a completely
  different, lower-risk mechanism for button glyphs: patch the real
  `fonts/bigFont` object in memory after it loads normally, instead of
  intercepting the boot-time zone queue at all. `InjectFontStructDebugTest()`
  is the first step — calls the real `FindOrLoadFont` for the already-loaded,
  cached font and logs its confirmed struct fields (glyph count, glyph array,
  material pointers) and a few real glyph entries to `proxy_d3d9.log`, gated
  behind the same obscure LB+RB-held-2s combo as the disabled zone-load test.
  Zero mutation, zero boot-path hooking. Builds clean — **live test still
  needed** to confirm the struct layout against real memory before any patch
  is attempted.

---

## v0.2.0 — Alpha (2026-07-19)

**Summary:** This project's first release tagged as a real milestone rather than incremental groundwork, and its first non-pre-release GitHub tag. Sprint migrated onto its real native kbutton (found via a from-bytecode-to-native-delivery reverse-engineering pass through `notifyonplayercommand`'s real GSC command-queue chain), which made its entire custom stamina/cooldown timer layer redundant and resolved the Extreme Conditioning perk override for free. 3 of Survival's 4 real killstreaks are now confirmed working (Predator Missile launch, Precision Airstrike, AI squadmate call-in). Real D-pad/A menu navigation now covers the main menu and title screen, not just in-game menus. Also included: a first vibration/rumble implementation that crashed the game at startup (root-caused and disabled, not shipped broken), and a fully proven, implementation-ready button-glyph asset/build pipeline (not yet wired into rendering). See **Status at a glance** in `README.md` for the full, explicit fully-working/partial/not-implemented breakdown this release introduces.

### What's New
1. **Predator Missile guidance-phase input: real per-frame reader chain
  found, diagnostic deployed (2026-07-19, task #30 follow-up).** User
  asked to re-investigate via the killstreak's own GSC rather than
  further native-side flag-guessing. Full re-read of `1555.gsc`'s
  guidance-phase loop confirms there is NO per-frame input read at the
  GSC level at all — it's a plain abort-condition poll; steering is 100%
  native. A whole-binary scan for the literal scalar `0x80000` (the bit
  `controlslinkto`'s native implementation sets on `clientStruct+0xc`)
  found the real per-frame dispatcher, `FUN_004554d0` — confirmed via its
  own caller (`FUN_00644ed0`, the Pmove-tick function) — which, when
  linked, tail-jumps into `FUN_006423d0`, reading 3 floats from
  `pml+0xc`/`+0x10`/`+0x14` (Pmove-locals, NOT the real `usercmd_t` this
  project's look hook writes to) and angle-wrapping them into
  `clientStruct+0x10c`/`+0x110`/`+0x114`. **This REFUTES the earlier
  `cmd+0x3e`/`0x3f` theory (issue #30) as the mechanism for this specific
  bug** — that theory's `+0x1094` bit is a different address from the
  `clientStruct+0xc` bit `controlslinkto` actually sets. Whether
  `pml+0xc/+0x10/+0x14` already receives this project's look input via some
  earlier copy step, or needs a direct write, wasn't resolved statically
  in the time available — a new log-and-forward diagnostic
  (`Hook_MissileGuidanceDispatch`, gated on the link bit so it's silent
  during normal play) logs both sides side by side, so the next real
  missile flight will show which hypothesis is correct. Builds clean (0
  warnings/0 errors, full rebuild). Not yet live-tested. See
  `re_notes/known_issues.md` issue #30's 2026-07-19 correction for the
  full trail.
2. **Controller vibration/rumble (2026-07-18, task #17).** No native
  rumble infrastructure exists in this build at all — entirely this
  project's own `XInputSetState` output, driven off two real, disassembly-
  confirmed native notify choke points: a short pulse on every real
  shot fired, and a damage-scaled pulse when the local player takes
  damage (filtered via the same "has a client struct" entity field the
  real `notifyonplayercommand` registration already gates on). New
  `[Vibration]` config section (`Enabled`, fire/damage intensity and
  duration). Known limitation: the local-player filter doesn't yet
  exclude a co-op partner's entity in 2-player Survival. Explosions, melee
  hits, killstreak activation, and low-ammo rumble are real leads (a
  ~600-entry GSC notify-event table already found includes
  `"explode"`/`"grenade_fire"`/`"missile_fire"`) but not yet wired up.
  **Correction, same day: live-tested and found to crash the game at
  startup.** Both native hook targets (`FUN_004895b0`/`FUN_0044cdb0`)
  turned out to be generic dispatchers with genuinely variable real
  argument counts across their call sites (confirmed via a disassembly-
  based push-count survey of every real caller) — the fixed-signature
  detour corrupted stack reads for unrelated boot-time events on most of
  them, crashing before any gameplay frame. **Disabled**: the
  `Rumble_Install()` call site is commented out (code kept, not deleted).
  A safer reimplementation is planned against a single-call-site-safe
  target (`FUN_0045e320` for fire) and health-polling (rather than a hook)
  for damage. See `re_notes/known_issues.md` issue #24. **Doc-audit
  finding, 2026-07-19 (flagged, not fixed — no code touched this pass):**
  `[Vibration] Enabled` still defaults to `true`, the exact same bug class
  already found and fixed once for `[AimAssist] Enabled` (v0.1.2).
  Currently harmless since `Rumble_Install()` is disabled with nothing to
  gate, but it's a landmine for the eventual reimplementation — flip this
  default to `false` in the same pass that re-enables real rumble hooks,
  not as an afterthought.
3. **`[Experimental]` config section (2026-07-18)** — a new pattern for
  individually-toggleable, not-yet-fully-proven behaviors, so a live
  hypothesis under test can be flipped off via the INI without a
  recompile if it turns out to be wrong. First entry:
  `FireNotifyQueueKick` (see the Fire/killstreak entry below).
4. **`[Experimental] SprintStaminaBypassForTesting` (2026-07-19, task #9) —
  ADDED THEN REMOVED THE SAME DAY.** Added specifically to isolate Sprint's
  real-`+sprint`-kbutton migration (see Changed below) for live testing by
  skipping this project's own stamina/cooldown timer entirely. Live-testing
  confirmed the kbutton migration works AND that the underlying custom
  timer this toggle bypassed is now permanently redundant (see the Changed
  entry below) — with that timer gone entirely, there's nothing left for
  this toggle to bypass, so it was removed the same session rather than
  left around as dead config surface.
5. **Fire (RT) rewired off the raw usercmd bit onto the real `+attack`
  kbutton (2026-07-18, task #7).** Killstreak work started with Predator
  Missile: the GSC trace done earlier this session found its launch is
  gated behind `notifyonplayercommand("launch_remote_missile", "+attack")`,
  which fires on real bind/command dispatch, not on a raw usercmd bit being
  forced — the standing hypothesis for why the missile's camera/view
  worked but launch didn't reliably. `+attack`'s real kbutton_t address
  (`0x00A98C00`) was already sitting in an existing bit-correlation table
  from 2026-07-14, so this reused the same `CallKbuttonDown`/
  `CallKbuttonUp` mechanism already proven live for ADS/Reload — a full
  replace, not additive, same precedent as the crouch/prone migration off
  raw bit-forcing. Builds clean (0 warnings/0 errors). **Live-tested same
  day: half confirmed, half refuted.** Regular gunfire — CONFIRMED no
  regression, shooting still works normally. Predator Missile launch —
  CONFIRMED still broken, unchanged. The kbutton-level fix stays (it's
  real and correct, gunfire depends on it) but the hypothesis that a
  kbutton_t `KeyDown` call alone would reach `notifyonplayercommand`'s
  native trigger is disproven — that trigger point is still unfound. See
  `known_issues.md` issue #29.
6. **Sprint (L3) migrated off raw `pm_flags` bit-forcing onto the real
  `+sprint` kbutton (2026-07-19, task #9).** Three prior live-memdiff
  searches for Sprint's real kbutton (twice via whole-heap correlation,
  once via live write-testing, once via a targeted static-range scan) had
  all come back negative and this was believed a genuine dead end (see
  `re_notes/iw5sp.md`, "Sprint's real kbutton — PARKED"). Found instead via
  a completely different, purely static technique needing no live game
  process: reconstructed `FUN_00438710`'s real 77-entry jump table by raw
  dword walk (the decompiler's own switch recovery only partially resolved
  it) and cross-referenced it against the real static 81-entry canonical
  bind-name table `FUN_005330a0` scans — confirmed the table's index IS
  `FUN_00438710`'s case number, four independent ways (`+attack`=1,
  `weapnext`=66, `togglecrouch`=72, ADS's `+toggleads_throw`=59-60,
  matching its already-confirmed `0xA98CB8` kbutton exactly). Case 61-62 =
  `"+sprint"`/`"-sprint"`, driving a dedicated kbutton at `0xA98CCC` —
  independently cross-confirmed because the real default SHIFT bind
  (`"+breath_sprint"`, case 9-10) disassembles to two kbutton calls, one on
  a newly-found `0xA98C04` (very likely Hold Breath's own kbutton, a live
  lead for task #24) and a second on this exact same `0xA98CCC`. Sprint now
  drives this kbutton via `CallKbuttonDown`/`CallKbuttonUp` (same mechanism
  as ADS/Reload/Fire), gated on `IsSprintActive()`. The old pm_flags-forcing
  mechanism (`InjectControllerSprintPmFlags`/`ReassertSprintPmFlags`, hooks
  on `FUN_00644ed0`/`FUN_00643ce0`) was removed entirely, not just disabled —
  full replace, same precedent as Fire's migration above. Builds clean (0
  warnings/0 errors, full rebuild). **LIVE-CONFIRMED WORKING, same day
  (2026-07-19).** User report: "this fixes multiple issues, having native
  sprint means no workaround needed for stamina and regen as its embedded
  naturally by the engine[,] same for extreme conditioning[,] fixed by this
  100%." Driving the real kbutton means the engine's own native sprint
  duration/recovery timer now applies automatically, INCLUDING Extreme
  Conditioning's real duration override — with zero separate detection code
  needed. **As a direct consequence, this project's entire custom stamina/
  cooldown timer layer (maintained since 2026-07-15 specifically to work
  around the previous pm_flags-forcing approach bypassing the real timer)
  is now dead weight and has been removed in the same pass**: `g_sprintStamina`/
  `g_sprintWinded`/`g_sprintCooldownRemaining`/`g_sprintLastTickMs`, the
  `player_sprintUnlimited`-dvar bypass (redundant — the real kbutton already
  respects that dvar natively, same as real keyboard sprint does), the
  `[Sprint]` config section (`MaxStaminaSeconds`/`RegenSeconds`), the
  just-added `[Experimental] SprintStaminaBypassForTesting` toggle (see
  Added above), and the `GetRealSprintValue`/`LogSprintDiag` diagnostic code
  that had been investigating whether a real native timer existed at all
  (see `known_issues.md` issue #6, 2026-07-16) — all gone. Also resolves
  task #9/#24's previously-open "Extreme Conditioning perk override" item:
  no override code was ever needed, since the real kbutton makes it a
  native, automatic consequence rather than something this project has to
  detect and apply itself. See `known_issues.md` issue #6's 2026-07-19
  update for the full disassembly trail and this removal.

### Fixed
1. **Sprint (L3) no longer force-stands the player while ADS'd (2026-07-18,
  task #24).** `InjectControllerSprint`'s auto-stand-from-crouch/prone call
  was firing unconditionally on any Sprint rising edge, including while
  aiming down sights with a sniper — breaking the player's crouch/prone
  cover the instant they tried to use Hold Breath (which shares the same
  physical bind as Sprint on console, `+breath_sprint`). Now gated on
  `!g_adsHeld`. Builds clean, not yet live-tested. **Known simplification**:
  gates on "ADS'd with any weapon," not specifically "ADS'd with a
  sniper-class weapon" (no clean native weapon-class query was available),
  so Sprint's rising edge is now a stance no-op for every ADS'd weapon, not
  just snipers — real console ADS+Sprint interaction on non-sniper weapons
  wasn't independently verified and should be checked during live testing.
  Hold Breath's actual sway-reduction feature remains unimplemented (see
  Investigated section below).
2. **Full documentation pass across README.md/CONTRIBUTING.md/iw5sp.md
  (2026-07-18)** to close out the remaining Back/slider/exception-count
  drift the earlier targeted fixes didn't fully catch — found by
  systematically checking every claim rather than just the ones already
  flagged. Fixed: the top-of-file status summary (still said Back
  unassigned and sliders unadjustable), the D-pad and Survival ready-up
  feature descriptions ("one/second of two exceptions" → "of three"), the
  "Why native, not an emulator" section (only described ONE exception and
  claimed it was "the only place in the whole mod that does this" — wrong
  even before Back was added, since it never mentioned D-pad Left's
  exception either; rewritten to list all three), the squadmate call-in
  killstreak-table entry (still shown as an open "known bug" despite issue
  #14 documenting it fixed), the architecture diagram ("two exceptions" →
  three, added Back/TAB), the keyboard/mouse fallback list (still named
  sliders and Back as requiring keyboard), `CONTRIBUTING.md`'s own
  "two existing exceptions" rule text, and a stale Plutonium `iw5sp.exe`
  "~175KB smaller" figure in README's own client-compatibility table (the
  correct 2,320-byte figure had already been fixed in `known_issues.md`
  but never copied over here). Also added a superseding pointer to the old
  dead-end Back attempt recorded in `iw5sp.md`.
3. **Corrected a stale "Campaign mostly untested" claim in README's
  killstreak sections (2026-07-18)**, in two places — the control-map
  table's Killstreaks row and the dedicated killstreak table's own intro
  line. Both were written before this session's Campaign playtest (8/17
  missions tested, see `re_notes/compatibility_matrix.md`) and were never
  updated. Also clarified the dedicated killstreak table is specifically
  Survival's buy-station roster, distinct from the newer Campaign-mission
  killstreak-type weapon systems table added earlier this session.
4. **Documentation-drift correction pass, three items (2026-07-18).**
  Verified against the actual source (not assumed from prior docs) that
  Back's real `+scores` implementation (a third key-synthesis exception,
  synthetic TAB keypress, same technique as Survival ready-up and D-pad
  Left's squadmate call-in) was already fully written and wired up as of
  2026-07-17 — confirmed by rebuilding the project clean (0 warnings, 0
  errors) — but was never reflected anywhere: `known_issues.md`'s own
  summary still said "first of two" exceptions (now "first of three," new
  issue #28 documents the implementation), `README.md`'s control map still
  showed Back as "unassigned, not yet implemented," and task #5 stayed
  "pending" the whole time. Also corrected two related stale claims
  surfaced along the way, both confirmed live by the user: buy-station/
  armory D-pad navigation is 100% confirmed working (was marked "believed,
  not verified"), and slider-type settings VALUE adjustment via Left/Right
  is confirmed working (was marked "unsolved" — the original claim was
  based on one native function found via decompile without checking
  whether the `.menu` files' own script-level key handlers, already proven
  for options-pane drilling, also covered sliders directly). Back itself
  remains implemented-but-not-yet-separately-live-confirmed, not
  overclaimed as done.
5. **Corrected a stale, self-contradicting README row (2026-07-18).** The
  "Current control map" table's "Menu/UI navigation" row said "Not yet
  implemented — mouse/keyboard still required," directly contradicting
  the same table's own pause-menu row two lines above (✅ Confirmed) and
  task #22's full write-up further down the same file — never updated
  when that work actually landed and was confirmed live. Split into four
  accurate rows: D-pad+A menu navigation (✅ confirmed), buy-station/armory
  navigation (🟡 believed working, not separately live-verified), slider
  value adjustment (⬜ still unsolved), and button-glyph prompts (⬜ not
  started) — matching what `known_issues.md` issue #22 actually says.

### Documentation
1. **Full documentation consistency pass across README.md/PATCHNOTES.md
  (2026-07-19), ahead of tagging this build toward v0.2.0.** Reconciled every
  doc against this session's actual findings rather than carrying forward
  stale claims:
  - **Mission mis-attribution corrected everywhere**: the mortar-fire and
    mounted-turret-difficulty bugs were filed under "Back on the Grid"/
    `dubai.ff` across multiple prior sessions and multiple docs
    (`README.md`'s compatibility table and killstreak table, `known_issues.md`
    issues #26/#27, live-tracker tasks #26/#27). A dedicated zone-
    identification pass found the real mission/zone is **Goalpost**/
    `hamburg.ff` (matching the mortar impact-FX table and the player-operable
    M1A1 turret actually present there) — "Back on the Grid" is untested and
    was wrongly given credit for both a pass ("fully compatible") and a fail
    (these two bugs) in different places. All four surfaces corrected, with
    an explicit "(corrected 2026-07-19)" note left in place rather than
    silently rewriting history.
  - **Killstreak status updated for real, live-confirmed progress**: README's
    Survival killstreak table and Campaign killstreak-type-weapon-system
    table both updated — Predator Missile launch is fixed (see the `"n 1"`
    fix below), Precision Airstrike is confirmed fully working (a
    smoke-grenade-throw mechanic, not a HUD/cursor system), and AI squadmate
    call-in stays confirmed. Predator Missile's post-fire guidance aim is
    now called out as a separate, still-open bug rather than folded into a
    generic "partial."
  - **Vibration/rumble's own "Added" entry (above) corrected in place**,
    same day it was written, once live testing showed it crashes the game at
    startup — see that entry for the root cause and the disable.
  - **Scorecard recomputed**: feature-completeness matrix moved 37/50→42/50
    given killstreaks (1→4 of 4), button-glyph prompts (2→3 of 4, full build
    pipeline now proven), and the options-menu implementation plan (1→2 of
    4); raw-functionality methodology's killstreak dataset and mission-
    compatibility dataset both recomputed against the corrected numbers.
  - **Second, deeper audit pass (2026-07-19): every commit since the last
    pre-session push (`418333f`) re-verified diff-by-diff against the
    docs, not just re-reading the prior summary.** Found and fixed several
    real gaps:
    - README's Configuration table listed every `[Look]`/`[Stance]`/
      `[Interact]`/`[Survival]`/`[Sprint]`/`[Bindings]`/`[AimAssist]` key
      but was missing the `[Vibration]` and `[Experimental]` sections
      entirely, even though both were added to the actual config this
      session — added all 7 missing rows.
    - `[Vibration] Enabled` still defaults to `true` in `mod_config.h`/the
      generated INI — the same bug CLASS already found and fixed once for
      `[AimAssist] Enabled` (v0.1.2). Currently harmless (nothing can gate
      on it while `Rumble_Install()` stays disabled) but flagged as a
      landmine for the eventual reimplementation. See the Vibration/rumble
      entry above and `known_issues.md` issue #24.
    - **`re_notes/killstreak_reference.md` was never updated with the
      `"n 1"` Predator Missile launch fix** — its Campaign and Survival
      tables both still read "currently broken"/"hypothesis REFUTED, real
      fix still not found," directly contradicting README/PATCHNOTES'
      "launch confirmed working live" status. Also still said "Back on the
      Grid" for the mortar/turret rows. Both corrected, with the squadmate
      call-in rows also upgraded from unlabeled to explicit ✅ CONFIRMED
      WORKING to match README.
    - `known_issues.md` issue #29's own HEADING still read "Predator
      Missile hypothesis REFUTED" while its own body, several hundred
      lines later, documents the fix and a live "CONFIRMED" launch —
      self-contradicting within the same file. Heading corrected to
      reflect the current, superseding status.
    - **README's Campaign compatibility summary row was left un-recomputed
      after the Goalpost correction**: table said "8 tested / 4 full / 4
      partial / 9 untested," but Goalpost moving from ✅ to ⚠️ (and "Back on
      the Grid" already having reset to ❓ untested in an earlier session)
      means the real count is **7 tested / 3 full / 4 partial / 10
      untested** — fixed. This also changed the Raw Functionality
      scorecard's own methodology (Campaign-compatibility dataset
      recomputed 75%→71%, Campaign killstreak-weapon-system dataset
      recomputed against README's own table 83%→69%, overall Raw
      Functionality score 83→**77/100**) — Feature Completeness (~84/100)
      is a separate axis and unaffected.
    - Everything else audited (all 4 commits' code diffs against their own
      commit messages, `re_notes/ui_assets.md`, `survival_mode_overview.md`,
      `survival_wave_scaling.md`, `iw5sp.md` spot checks) matched what was
      actually implemented — no further gaps found.
2. **Added a scorecard to README.md**: raw functionality (~80/100, from the
  control map, `compatibility_matrix.md`, and `killstreak_reference.md`)
  and a feature-completeness matrix (~74/100, SP/Survival scope).
  Multiplayer is explicitly excluded from both scores rather than blended
  in at a misleadingly low weight, since it's a separate phase that hasn't
  started at all. **Iterated same day, twice**: (1) feature completeness
  was initially computed from the live task-tracking list (29 tasks,
  ~57/100) — flagged as not viable, since that list is an ever-expanding
  scratchpad where every newly-found bug adds another entry, making its
  completed/total ratio get worse the more thoroughly this project tests
  itself; recomputed from a curated named-system list instead (climbed to
  ~75-85/100 across a couple of revisions as genuinely-missing foundational
  items, like the injection/hooking layer itself and raw controller
  detection, were found by scanning the full commit history rather than
  just this file's own Feature List). (2) That flat list was then itself
  replaced with the current, more granular matrix — a single "aim assist:
  partial" row and a single "stick layout presets: done" row don't carry
  equal real-world weight, so large remaining systems (killstreaks, aim
  assist, the real options menu, vibration, button glyphs) are now broken
  into their own atomic done/not-done sub-items instead of one lightly-
  weighted line each, landing at 37/50 ≈ 74/100.
3. **`TacticalLefty` button layout preset CONFIRMED CORRECT against real
  hardware (2026-07-19).** This was the one open accuracy question left in
  the button-layout-presets system (task #15) — all four presets
  (`Default`/`Tactical`/`Lefty`/`TacticalLefty`) were reconstructed from the
  known-unchanged CoD4→MW2→MW3 console control scheme, but `TacticalLefty`
  specifically (Lefty with Tactical's face-button swap layered on top) had
  never been independently verified. User confirmation closes this out —
  updated the caveat text in `mod_config.h`/`mod_config.cpp` (the generated
  INI's own comments) and `README.md` accordingly, no functional code
  changed.
4. **Added `re_notes/killstreak_reference.md` and a "Killstreak support"
  section to `README.md` (2026-07-18).** Two clearly-separated parts: a
  real, first-party controller-support status table for the killstreak-
  type weapon systems actually encountered and tested during this
  session's Campaign playtest (boat/UGV/door-gun/SMAW dumb-fire working;
  DPV aim, mortar fire, turret difficulty, SMAW lock-on, and Predator
  Missile's fire input each tracked with their specific known-issue/task
  number) — plus a separate, clearly-labeled MP killstreak reference list
  (3 strike packages, ~20+ rewards) sourced from public research, kept
  strictly as forward-planning material since MP work hasn't started.
5. **Added a condensed compatibility summary table to `README.md`**, and
  wired `re_notes/compatibility_matrix.md` into the project's documented
  ownership model: `CODE_STANDARDS.md`'s "Documentation Standards" section
  now names it as the file that owns per-mission/per-mode live playtest
  status (alongside `iw5sp.md` for RE trail, `known_issues.md` for the
  issue list, `PATCHNOTES.md` for the changelog), and `CONTRIBUTING.md`'s
  "Reporting bugs" section now points contributors there before filing a
  mission-specific report, and welcomes PRs that add/correct compatibility
  entries. Keeps the new matrix file from being an orphaned addition —
  every doc that should reference it now does.
6. **Simplified Survival tracking in `compatibility_matrix.md` to a single
  overall entry instead of 16 per-map rows (2026-07-18, user direction)**:
  unlike Campaign/Special Ops, Survival's controller support is
  map-independent (same input/engine hooks apply regardless of map), and
  live testing across maps confirms this — works well overall, with one
  known issue (Predator missile killstreak, cross-referenced to the
  existing task #7/issue #10).
7. **Possible 8th playtest bug, NOT YET CONFIRMED (2026-07-18)**: SMAW may
  have failed to lock onto an aircraft target in "Goalpost" — but per the
  user's own follow-up, the target may be a non-targetable scripted
  entity, which would make this a non-issue rather than a real bug. Needs
  a same-target keyboard comparison before status changes either way
  (task #29). Logged as `known_issues.md` issue #27 bug #8, explicitly
  flagged as unconfirmed rather than treated as a defect.
8. **First live Campaign playtest session (2026-07-17/18) — 7 bugs found
  across Act 1 through the start of Act 2 (missions 2-9), against
  everything else in that range confirmed fully working.** Logged in
  full as `known_issues.md` issue #27: DPV aim broken in Hunter Killer
  (bug #1); crouch intermittently fails to fire, ~2%, recovers after
  pause/unpause (bug #2); Hold Breath never implemented + L3 wrongly
  force-stands the player while ADS+crouched with a sniper (bug #3, task
  #24); movement hook bypasses the scripted player-freeze during
  Turbulence's plane-breakup sequence, potentially systemic (bug #4, task
  #25); mortar aim works but fire doesn't in Back on the Grid (bug #5,
  task #26); mounted-turret sequence feels harder on controller in the
  same mission, cause unconfirmed and flagged for dedicated deep
  investigation (bug #6, task #27); Interact didn't fire for a
  `+usereload`-gated tank-exit prompt in Mind the Gap (bug #7, task #28,
  confirmed root cause). **User confirmed (2026-07-18) every other
  mission in the missions-2-through-9 range was fully compatible** —
  Persona Non Grata's UGV, Davis Family Vacation, Goalpost, and Return to
  Sender's door gun all playable with zero fallback, alongside the
  partially-working missions' unaffected sequences (Hunter Killer's boat,
  Mind the Gap's opening aerial sequence). Mission 1 (Black Tuesday) and
  mission 10 onward (Bag and Drag, where the session paused) remain
  untested.
9. **Added `re_notes/compatibility_matrix.md`**: a new, living per-
  mission/per-map controller-compatibility tracker (Campaign by mission,
  Special Ops and Survival each as individual entries), separate from
  `known_issues.md`'s technical RE trail — this file answers "what's
  actually been tested and how did it go," `known_issues.md` stays the
  place for the underlying bug/fix detail. Seeded with this session's
  playtest results; Special Ops and Survival rows scaffolded from this
  install's own real zone files but not yet tested.
10. **Full-breadth engine research pass** (killstreaks, weapons, perks, HUD/UI, AI/vehicles,
  physics/health, and a research-only pass on `iw5mp.exe`) — no code changes, pure
  groundwork. Headline findings: a real, actionable hypothesis for Predator missile's
  partial-working state (task #7); confirmation that turret and AI-squadmate call-ins
  are genuinely separate script systems, not two branches of one (correcting task
  #13's own framing) with the squadmate bug's divergence point narrowed to a single
  unresolved function; Extreme Conditioning's native detection (task #9) confirmed
  genuinely parked, not just unstarted; a real, unambiguous god-mode bit found for
  task #20; and confirmation that MP (`iw5mp.exe`) shares the same core architecture
  as SP (same `usercmd_t` layout, same class of registration function) without yet
  resolving CLAUDE.md's open anti-cheat question, which any future MP work still
  needs first. Full detail in `re_notes/iw5sp.md` and `re_notes/known_issues.md`
  issue #26.
11. Renamed the project's folder from `MW3 Survival and Campaign Controller
  Support/` to `MW32011NCP/` (same repo/git history) to match the project's actual
  GitHub name. A new sibling project, `MW32011NSP` (netcode/security modernization),
  now also lives at the game install root — `re_notes/` cross-referencing between the
  two is now a standing policy, see `CLAUDE.md`.
12. **Corrected two factual errors in issue #25's Plutonium client-compatibility
  survey (2026-07-17, later session).** The "`iw5sp.exe` ~175KB smaller" figure
  was wrong — direct re-measurement (prompted by the sibling `MW32011NSP`
  project's own netcode research) found the actual size delta is 2,320 bytes;
  the ~175KB figure was very likely a byte-difference COUNT (175,411
  individual differing positions) mistaken for an overall file-size
  difference. Also added a cross-reference: `MW32011NSP` found that since
  Plutonium's `iw5mp.exe` is byte-identical to retail (already recorded here),
  a client-side netcode vulnerability they confirmed in retail `iw5mp.exe` is
  present on Plutonium MP installs too — Plutonium's routing-through-their-
  own-servers mitigation only covers server-side code. Full detail in
  `re_notes/known_issues.md` issue #25.

---

### Investigated, Not Yet Resolved
1. **Predator Missile post-fire missile-guidance sequence: movement breaks
  on controller (2026-07-18, live-reported).** During the phase where the
  player controls the flying missile in flight (shares the real
  UAV-control system), controller movement input breaks. Not yet fixed.
  See the major research finding immediately below — this turned out to
  have a much more concrete, unifying explanation than the original
  "scripted-freeze" framing.
2. **Major research finding: a third, previously-unknown analog-input
  channel (`cmd+0x3e`/`0x3f`) likely explains FOUR separately-tracked bugs
  at once (2026-07-18, task #25 deep dive, `known_issues.md` issue #30).**
  Decompiling the engine's real per-frame orchestrator revealed it has (at
  least) 3 control-mode branches — menu-active, a mounted/aim-only mode
  that routes real mouse-delta into a THIRD analog byte pair
  (`cmd+0x3e`/`0x3f`, distinct from normal movement and normal look), and
  vehicle steering — none of which this project's controller hooks are aware
  of, since they only ever write the normal movement/look fields. The
  mounted/aim-only branch is a strong, evidence-backed unifying candidate
  for DPV aiming not working, the mounted-turret feeling too hard, AND
  today's Predator Missile guidance bug above — potentially one fix
  instead of three separate investigations. Not yet implemented; a new
  task (#30 in the live tracker) captures the concrete implementation
  plan. Mortar fire appears to be a genuinely separate mechanism (see
  below), not covered by this finding.
3. **Turret damage/difficulty in "Back on the Grid" *(mission attribution
  corrected 2026-07-19 — this is actually Goalpost, see the Docs entry
  above)*: the health-regen hypothesis is REFUTED (2026-07-18, task #27, now
  closed).** Dumped the
  real mission zone and confirmed the mission DOES use a real
  faster-regen buff mechanic in two other scripted set-pieces — just never
  on the turret sequence. No turret-specific damage/regen logic exists in
  the mission's own scripts at all. The likely real explanation is the
  same missing-aim-channel issue described above (no aim assist +
  imprecise mounted aim), not a missing survivability mechanic.
4. **Mortar fire ("Back on the Grid" *(mission attribution corrected
  2026-07-19 — this is actually Goalpost, see the Docs entry above)*) will
  very likely still be broken
  after the Fire rewrite above (2026-07-18, task #26) — do not assume it
  was fixed for free.** Confirmed the mortar (`bog_mortar`) is deliberately
  excluded from the engine's generic vehicle-fire pipeline, and — more
  importantly — the turret in the same mission already fired correctly
  under the OLD raw-usercmd-bit Fire, which is real evidence mortar and
  turret don't share a fire mechanism (otherwise both would have failed
  identically before today's change). The mortar's own fire-control
  script wasn't located this pass (hash-named, no distinguishing string).
5. **Killstreak catalog correction (2026-07-18): the previously-assumed
  6-item killstreak list was wrong.** Re-extracting the real buy-station
  economy CSV directly shows Survival only ever sells 4 real killstreaks
  (`remote_missile`, `precision_airstrike`, `friendly_support_delta`,
  `friendly_support_riotshield`) — `stealth_airstrike`/`carepackage_c4`/
  `carepackage_ammo` don't exist as purchasable items at all (dead/
  vestigial precache-only content). Also resolved: `precision_airstrike`
  turns out to use a genuinely different, THIRD input mechanism (a native
  UI-style placement-marker API, not gated by `notifyonplayercommand` at
  all — may already work via this project's existing D-pad+A menu navigation,
  worth a live test); and the standing hypothesis that AI squadmate
  call-ins (`friendly_support_delta`/`riotshield`) have a per-type code
  divergence bug is REFUTED — both run byte-for-byte identical spawn
  logic, differing only in a cosmetic HUD icon. See
  `re_notes/killstreak_reference.md`'s corrected roster table.
6. **`notifyonplayercommand`'s native trigger point: reframed, not found
  (2026-07-18).** A full decompile of the entire input-dispatch chain
  found it is purely numeric with zero bind-name-string logic anywhere,
  and a raw byte-level scan confirmed the literal strings
  `"notifyonplayercommand"`/`"playercommand"` don't exist anywhere in the
  binary's static data. Conclusion: there is almost certainly no native
  "keypress pushes a notify" trigger to find — it's very likely a
  GSC-VM-internal builtin (bytecode polls bind state itself), the same
  architecture already confirmed for `hasperk` elsewhere in this project.
  **Polling-frequency ruled out same day**: user confirmed from prior
  play that holding Fire for a long duration still never launches the
  missile, closing off the "our held press doesn't last long enough for a
  slow poll" theory. The real kbutton_t this project writes to is either never
  read by whatever GSC-VM intrinsic backs `notifyonplayercommand`, or some
  other precondition is unmet — next step is GSC bytecode/opcode-level
  analysis of `1555.gsc`'s compiled `.gscbin`, not further native
  dispatch-chain RE.
7. **Second research wave, same day — five more forks, real progress on
  several fronts (`known_issues.md` issues #29/#30/#31):**
  - **Bytecode-level breakthrough on `notifyonplayercommand`.** Using
    `gsc-tool`'s own open-source engine tables, confirmed
    `notifyonplayercommand` compiles to a real, findable opcode (`0x8D`,
    `OP_CallBuiltinMethod2`) + method ID (`0x82A5`) — found 7 times in
    `1555.gsc`'s actual compiled bytecode, matching every known call site
    exactly. Also confirmed `notifyoncommand` (the bare/global variant
    `friendly_support_called` uses) is a SEPARATE builtin entirely
    (function ID `0x00D`), not the same mechanism with an optional
    receiver — a real architectural distinction missed until now. Native
    dispatch table (how the ID resolves to an actual function) not yet
    found — needs the GSC interpreter's opcode-dispatch loop located in
    Ghidra, a well-defined next step rather than an open question.
  - **Turret's "success" reframed — it was never evidence about notify
    gates.** Decompiled `FUN_0057a930` (previously unresolved) and found
    it's just a weapon-select fallback, not killstreak-specific. All
    three weapon-type killstreaks (sentry, `remote_missile`,
    `precision_airstrike`) ride a real native `weapon_change` event fired
    by an ordinary weapon switch — completely bypassing any notify-gate
    mechanism. Squadmate call-ins aren't registered in that same
    dispatcher at all, so they never get this free ride — explaining the
    working/broken split without it being evidence either way about
    synthetic input reaching notify gates.
  - **Squadmate call-in failure: a second, concretely-evidenced
    explanation found.** A full grep sweep of all 240 decompiled scripts'
    notify-call sites traced `friendly_support_called`'s real spawn logic
    to an explicit defensive early-return when a Survival map lacks
    `drop_path_start` structs — a genuine, silent, map-dependent no-op
    completely independent of input device. Stacks with (doesn't replace)
    the notify-reachability theory above as a candidate cause.
  - **`precision_airstrike` may already partially work today.** Its
    artillery-marker cursor movement was traced to `FUN_0057df60` — the
    same function a parallel pass had flagged as "vehicle steering," now
    understood to be a shared mode dispatcher (mode 1 = artillery cursor,
    mode 2 = actual vehicle driving). Mode 1's cursor math reuses the
    exact same raw mouse-delta source normal look already feeds — user
    confirmed the real in-game mechanic (aim + confirm, like a smoke
    marker throw) is consistent with this being a real-time cursor rather
    than a menu interaction, reinforcing that controller aiming may
    already work with zero new code. Only the confirm/Fire-detection step
    is still unlocated. Worth a live test before writing anything.
  - **Predator Missile's Campaign appearance confirmed and corrected.**
    "Down the Rabbit Hole" (`rescue_2.ff`) runs the LITERAL SAME compiled
    `1554.gscbin`/`1555.gscbin` scripts as Survival's version — a
    Survival-side fix for Fire's `notifyonplayercommand` reachability
    fixes both simultaneously, not two separate problems. Also corrected
    a stale, unverified claim that "Black Tuesday" also uses this
    killstreak — checked the two best zone candidates, found no supporting
    evidence, removed from `killstreak_reference.md`.

---

## v0.1.3 (2026-07-17)

**Summary:** The biggest research release so far, alongside one real shipped feature: real, native D-pad/A menu navigation, confirmed working live across the main menu, pause menu, and options screens. Everything else this release is deep groundwork — a controller-options-menu injection mechanism blocked on a genuine architectural limit (with a promising fix already found), a likely static-analysis solution to aim assist's classification problem (not yet live-verified, still disabled), real vibration trigger points, a complete keycode reference, and an MW3-client-compatibility survey that surfaced a concrete anti-cheat risk worth knowing about before ever pairing this project with Plutonium multiplayer. The zone/menu-injection debug trigger built during this research is disabled for this build (real, working test code, just not a finished player-facing feature yet) — see `re_notes/known_issues.md` issue #23.

### What's New
1. **Real native controller menu navigation (task #22): D-pad + A, confirmed working
  live across the main menu, pause menu, and options screens.** Extracted the game's
  own plain-text `.menu` UI definitions from `zone/english/ui.ff`/`ui_mp.ff` via
  OpenAssetTools, and decompiled the real key-event handler chain
  (`FUN_00541020` → `FUN_004d9850`/`ForwardKeyToMenu` → `FUN_004dfd30`) to find the
  actual keycodes real keyboard input uses for menu interaction — the same generic
  `ForwardKeyToMenu` call B's ESC-back already used turned out to forward ANY
  keycode, not just ESC. D-pad Up/Down send the real "previous/next item" alt-
  keycodes (`0x9a`/`0x9b`); D-pad Left/Right send the real, *separate* keycodes
  (`0x9c`/`0x9d`) that options-style two-pane screens (category list + that
  category's settings) specifically recognize for drilling in/out between panes —
  confirmed by finding the actual `execKeyInt 156`/`157` handlers in
  `ui/pc_options_video.menu` and matching them, rather than assuming Left/Right
  should behave the same as Up/Down. A sends real Enter (`0xd`) for select/activate.
  D-pad's normal gameplay actionslot dispatch and A's normal Jump are both
  suppressed while a menu is open so they can't mean two things at once. An initial
  guess using the standard idTech `K_UPARROW`/`K_DOWNARROW` constants (128/129) was
  live-tested and found completely wrong — the real values were read directly out of
  the decompiled dispatcher instead. See `re_notes/known_issues.md` issue #22 and
  `re_notes/iw5sp.md` for the full trail.
2. **`AdsSlowdownBaseline`** — a new `[Look]` config value multiplied on top of the
  existing zoom-proportional ADS slowdown curve. Live feedback: the pure
  `ratio^strength` curve gave almost no slowdown at all on low-zoom optics (iron
  sights/red dots), since the zoom ratio itself stays too close to `1.0` for any
  power of it to produce a noticeable effect, regardless of strength. This baseline
  applies real slowdown even at minimal zoom while preserving the proportional shape
  on top for higher-magnification optics. Default `0.65` (started at `0.85`, further
  lowered after live testing showed more slowdown felt better even at minimal zoom).
  Same safety guarantee as strength — guarded `>= 0.0`, so the combined scale factor
  can never go negative/invert at any value.
3. **Default `AdsSlowdownStrength` raised from `1.0` to `1.75`** (via `1.5` first, then
  further refined live). Confirmed live to feel closer to real console controller CoD
  than exactly proportional (`1.0`).
4. **Default `[Interact] HoldThresholdMs` lowered from `740` to `300`.** Confirmed live
  to feel better than the original 740ms default. Comment wording also simplified to
  describe the observed net effect (a quick tap reloads, same as console) rather than
  the underlying two-mechanism split (Interact's hold-gated usercmd bit vs. Reload's
  own always-instant kbutton, which fires on every press regardless of hold duration
  and is what actually produces the "quick tap reloads" behavior).

Both defaults only affect freshly-generated `mw3ncp_config.ini` files — existing
configs keep whatever value is already in them; edit by hand to pick up new defaults.

---

### Fixed
1. **Aim assist's config default was `true`, not `false` — every brand-new install
  would have shipped with the confirmed-broken aimbot silently turned on.**
  `mod_config.h`'s `aimAssistEnabled` struct default was `true` since the feature
  was first added; every doc (README, this changelog, `known_issues.md`) said the
  opposite the whole time (`Enabled=0`, "disabled by default"). Went undetected
  because the config file only gets freshly generated when none exists yet — this
  development machine already had a hand-corrected `Enabled=0` on disk from
  earlier testing, which masked the bug locally the entire time. Caught during a
  pre-release check (explicitly asked for, right before packaging v0.1.3) and
  confirmed via full trace: the struct default is the only place this value is
  ever set for a fresh config, `WriteDefaultConfig` writes it verbatim, and an
  existing file's saved value is otherwise correctly respected on later launches.
  Fixed to `false`, rebuilt, verified via full code trace (not just "should be
  fixed"). **A real lesson for this project going forward**: an already-populated
  local dev config can mask exactly this class of default-value bug — worth a
  fresh-install check (rename/delete the local config, confirm what gets
  regenerated) before any future release, not just before this one.
2. **Start's pause/unpause could desync from the real game state if the player also
  used keyboard ESC.** `InjectControllerPauseMenu` tracked its own `g_paused` bool,
  updated only on a controller Start press — the same class of "manually-tracked
  copy can drift from the engine's own real state" bug the crouch/prone rewrite
  (see below) was built to eliminate, just not caught in this specific function at
  the time. Keyboard ESC also natively opens/closes the pause menu (keyboard/mouse
  stays fully supported alongside controller), so a player switching between the
  two could leave `g_paused` believing the wrong thing, making the next controller
  Start press act on stale state — potentially eating that press with no visible
  effect. Found during a pre-release code review, ahead of v0.1.3. Fixed the same
  way the crouch/prone rewrite fixed its own version of this bug: reads the real
  `cl_paused` dvar directly via the existing `GetDvarInt` helper instead of
  trusting a local copy, eliminating the desync class entirely rather than
  patching around it.
3. **Sprint's stamina timer could compute a huge, bogus time delta after a
  controller disconnect/reconnect.** The tick-baseline timestamp only got
  refreshed on ticks where a controller was actually present — a disconnect while
  sprint was held, followed by a reconnect, computed `dt` across the entire
  disconnected duration on the next tick. Self-correcting in practice (clamps
  stamina to 0, marks winded with the normal cooldown — not a hang or crash), but
  an avoidable inconsistency with the exact pattern this function had already
  established for a different case (the `player_sprintUnlimited` bypass path
  right below it). Found during the same pre-release review; fixed to refresh the
  baseline on the no-controller path too.
4. **Two `[Sprint]` config values had no lower-bound guard, unlike every other
  tunable float in the same file.** Hand-editing `RegenSeconds=0` alone produces a
  divide-by-zero (harmless — clamped away the same tick); `MaxStaminaSeconds=0`
  together with it makes it `0/0`, permanently setting the stamina value to `NaN`
  (the existing `>= 0` clamp is always false for `NaN`, so it never
  self-corrects). Only reachable via manual config editing, not normal play — same
  found-during-pre-release-review batch. Fixed with the same clamp-on-read pattern
  already used for every other config value in this function.
5. **B didn't exit the pause menu.** The ESC-forward logic (`InjectControllerMenuBack`)
  was only ever wired into the per-frame gameplay tick, which stops running entirely
  while genuinely paused — so B's menu-back action never fired in the one state it
  exists to handle. Now also driven from the same always-running WndProc/timer tick
  that already handles Start's open/close.
6. **Crouch fired unexpectedly when exiting pause with B.** A side effect of the fix
  above: B is also the crouch/prone button, and its tap/hold tracking went stale while
  paused, so the same press that closed the menu looked like a fresh crouch tap the
  instant gameplay resumed. Fixed by tracking, per B press, whether it ever touched an
  open menu, and suppressing crouch/prone for that press if so — scoped to the actual
  current press rather than any menu open/close in general, so it can't suppress a
  genuine crouch/prone elsewhere. See `re_notes/known_issues.md` issue #13.
7. **Pausing while a buy-station menu was open left it stacked underneath the pause
  menu**, and unpausing would have dropped the player back inside it instead of into
  plain gameplay. Start now auto-closes any other open menu (the same real ESC-forward
  mechanism B itself uses) before opening the pause menu, so pause always opens cleanly
  on top of gameplay and unpausing always returns straight to it.
8. **D-pad Left (Survival AI-squadmate call-in) failed 100% of the time**, while turret
  call-ins on the same slot worked fine. Confirmed unique to Survival, not a general
  regression — real keyboard `'4'` (the same bind) worked correctly the whole time.
  Fixed by synthesizing a real key press for `'4'` instead of calling the native
  weapon-switch function directly, for D-pad Left only (the other three D-pad
  directions are unchanged). Same category of workaround as Survival ready-up's F5
  synthesis, not a general policy change — see `re_notes/known_issues.md` issue #14.
9. **Turret couldn't be un-toggled once deployed via D-pad Left.** Turns out to be a
  genuine bug in the old direct-call implementation, not a native limitation — the real
  `+actionslot4` behavior is a plain press-to-toggle, but the old call pair only ever
  drove the "deploy" side. Fixed for free by the same key-synthesis change above, since
  it now goes through the real dispatcher's own toggle logic.

### Documentation
1. **Surveyed the real hint-text content behind pickup/reload/interact-style
  prompts** ahead of wiring the controller-glyph resolver hook (task #6's other
  half). Found three genuinely different substitution mechanisms in the real
  localized strings, not one: `&&1`-token strings (the entire weapon-pickup/perk/
  stance-hint family), a separate `[{+command}]` syntax embedded directly in some
  strings (confirmed NOT handled by the same `&&N` engine), and literal hardcoded
  PC-only text (`[Right Mouse]`/`[Left Mouse]`) that no resolver hook can fix at
  all. Also confirmed Reload has no hint text/bind token whatsoever — it's a
  plain HUD element, nothing to glyph-swap. **Follow-up (2026-07-17): traced the
  `[{+command}]` mechanism fully — good news, it routes through the exact same
  `FUN_0061f6f0` bind-resolver the `&&N` path already uses, so one hook covers
  both, not two.** See `re_notes/ui_assets.md`.
2. **Complete real keycode reference recovered (95 entries)** — traced the real
  resolution chain from the bind-resolver down to the raw `{name, keynum}` table
  the game itself uses, ending years of finding individual keycodes ad hoc one at
  a time. Notably includes `AUX1`-`AUX16`, the idTech/Quake3-lineage joystick-
  button placeholder range — structurally present and bindable, unused since this
  build has no XInput import. New reusable script,
  `re_notes/ghidra_scripts/DumpKeynamesTable.java`. See `re_notes/iw5sp.md`.
3. **GSC mission-scripting architecture survey.** Cataloged the ~140 real `.ff`
  zones (10 Campaign missions, 15 Spec Ops missions, 18 Spec Ops Survival maps,
  shared/common code) and found the Survival/Spec Ops buy-station economy is
  **data-driven, not GSC-driven** — a single CSV (`sp/survival_armories.csv`)
  defines every weapon/attachment/perk/killstreak with price and wave-gate, no
  scattered purchase logic to reverse-engineer. Surfaced the full real perk/
  killstreak roster and the `maps\<levelname>::main()` mission-entry convention.
  See `re_notes/iw5sp.md`.
4. Committed `re_notes/ghidra_scripts/FindStrideArrayBase.java` (used during the aim-
  assist entity-classification investigation, task #16) — a general-purpose static-
  analysis tool independent of that investigation's outcome, so kept regardless.
5. **Re-extracted `assets/button_glyphs/` from a cleaner, user-trimmed source sheet**
  (still pure source-art groundwork, not yet wired into any rendering code — see
  task #6's other half). Replaces the original 106-icon set (all platforms/
  generations, with one unresolved text-bleed issue on `ps4_circle`) with 47 icons
  across three slimmed style groups (Xbox 360/Classic, Xbox Modern, PlayStation)
  plus universal D-pad/stick-direction indicators. Re-extracted with a proper
  connected-component labeler instead of row/column-band heuristics, so the old
  clipping/text-bleed issues can't recur — every icon's bounding box now comes
  directly from its own alpha-channel content. D-pad only ships one real icon
  (`dpad_up`); the other three directions are the same asset rotated
  90°/180°/270° programmatically, per the user's explicit design intent (D-pad
  glyphs are visually identical across brands, no need for four separate source
  crops). See `re_notes/ui_assets.md` for the full naming scheme and method.
6. **First implementation of aim assist (rotational friction + magnetism, task #16)
  — EXPERIMENTAL, NOT FUNCTIONAL, DISABLED BY DEFAULT. Must stay disabled for any
  public/release build.** The native aim-assist system turned out to be shared math
  bots use to aim at the player, not a player-facing feature (MW3 PC genuinely has no
  mouse aim-assist) — so this is built entirely from scratch instead: real entity
  position data plus our own targeting and curve math (the curve shape recovered
  from this game's own `aim_assist/view_input_0.graph` asset), applied directly on
  top of the same look-angle globals controller look already writes to. New
  `[AimAssist]` config section (`Enabled`, `Range`, `ConeDegrees`,
  `FrictionStrength`, `MagnetismDegreesPerSecond`). Live-tested across several
  tuning passes: the core math (angle error, friction curve, magnetism) is confirmed
  correct via diagnostic logging, but the target-validity filter (currently
  movement-based — a real prop never moves, a living AI's position does) is
  genuinely broken in practice — it oscillates between multiple
  simultaneously-moving things (a real enemy, a settling ragdoll, a thrown grenade),
  not just imprecise. **Intended** to ship with `Enabled=0` — see the Fixed entry
  below, this wasn't actually true until a bug found right before release was
  corrected. Do not flip this on outside active development until a real
  type/health-based classification replaces the movement heuristic — see
  `re_notes/known_issues.md` issue #15.

### Investigated, Not Yet Resolved
1. **`dllmain.cpp`'s generic export-forwarding stubs have no null-pointer guard,
  accepted as a known, low-risk limitation, not fixed.** Found during the same
  pre-release review as the fixes above: `FORWARD_STUB`'s naked tail-jump forwards
  ~15 obscure real `d3d9.dll` exports (`D3DPERF_*`, `PSGPError`, etc.) without
  checking the resolved pointer is non-null first — if the real system `d3d9.dll`
  were ever missing one AND the game somehow called it, this jumps through a null
  pointer and crashes. Deliberately not fixed: these are intentionally
  unknown-arity stdcall/cdecl exports (the whole point of the tail-jump approach
  is not needing to know each one's real signature), so a "graceful" fallback
  can't safely `ret` without knowing how many bytes of the caller's stack to
  clean up — a naive guard would either need the real signature anyway (defeating
  the point) or risk corrupting the caller's stack on return, worse than the
  crash it would guard against. Real-world risk is low: these are standard
  exports present on essentially any genuine Windows `d3d9.dll`.
  `Direct3DCreate9` — the one export MW3 unconditionally needs — is correctly
  guarded elsewhere (`ResolveRealExports` returns `false` and `DllMain` aborts
  entirely if that specific one is missing).
2. **Real controller options menu (task #23): native zone/menu injection pipeline
  built and confirmed working for bare content, blocked on a real architectural
  limit for real content — but a structurally-sound fix was found the same day.**
  Built and live-confirmed an entirely in-memory mechanism to inject a
  custom-compiled `.menu` asset into the running game via its own real
  zone-loading system — a bare custom menuDef genuinely rendered in the real
  pause menu's own slot, real `ui.ff` never touched on disk. Real menu content
  (anything with a background material, virtually all of it) turned out to be
  fundamentally unsafe via LIVE injection: loading a material triggers a genuine
  D3D9 GPU-resource-creation cascade unsafe outside the engine's own controlled
  loading context, with no workaround available from a live hook. **Follow-up
  research the same day found a real fix, not a workaround**: `LoadZones` has
  exactly 4 real callers in the whole binary; two of them (a level-load call site
  and a boot-time/main-menu call site) have enough stack-array headroom to safely
  append our own zone entry via a single hook, distinguishing real callers by
  return address. This loads our content through the engine's own genuinely safe
  context instead of a live hook — recommended next implementation step, before
  the `ui.ff`-on-disk-replacement fallback (whose backup/hash-verify safety net,
  `tools/ff_installer/backup_and_verify.ps1`, is already built and live-tested).
  Full trail in `re_notes/known_issues.md` issue #23 and `re_notes/iw5sp.md`.
3. **Aim assist target classification — likely solved statically, not yet
  live-verified.** Following a lead from the cragson/mw3-surviv0r reference repo's
  own aimbot source, found strong static evidence of a real, second entity array
  in our own binary (base `0x01197AD8`, stride `0x270`) carrying type/health
  fields — but the assumed link from our existing entity array to it (a
  hypothesized clientnum field) produced garbage values in a live test and was
  disproven. **Follow-up research found the array doesn't need that link at
  all**: a real checkpoint/save-deserialization function proves it's fixed-
  capacity (2048 slots), independently walkable via its own parallel validity-
  flag array, with zero dependency on `centity`. The same function independently
  confirms type `13` is the real AI-actor type (from our own vanilla binary, not
  just the reference repo). `type==13 && health>0` is genuine native
  classification with no movement heuristic needed — very likely the actual fix
  for the oscillation bug that's kept this disabled all along. Still needs a live
  diagnostic pass before shipping. See `re_notes/known_issues.md` issue #15.
4. **Vibration/rumble (task #17) — real trigger points found, not yet
  implemented.** No native vibration infrastructure exists (confirmed empty
  search), so output has to be entirely our own `XInputSetState` calls — research
  found confirmed, hookable native events for weapon fire (a single clean choke
  point, fires per-shot for both semi/full-auto) and player/entity damage
  (carries the literal damage amount, usable for intensity scaling; needs a
  local-player filter, not yet resolved). Explosions, melee, and killstreak
  activation not yet traced. See `re_notes/known_issues.md` issue #24.
5. **MW3 client compatibility survey (Plutonium/AlterWare/DeckOps) — research
  only.** Long-term goal is supporting other MW3 clients, not just retail Steam.
  Plutonium (installed locally, directly compared): `iw5mp.exe` is byte-identical
  to retail, but **its anti-cheat is confirmed to ban DLL injection/memory
  access** — do not use this project with Plutonium MP. `iw5sp.exe` differs
  significantly from retail. AlterWare IW5-Mod (SP+Spec Ops specific, its own
  separate binary, not yet acquired) looks like the most promising third-party
  target given this project's SP-first scope and no known anti-cheat concern.
  DeckOps isn't a separate client — it wraps Plutonium for Steam Deck, inheriting
  its anti-cheat risk. See the new **Client compatibility** section in README.md
  and `re_notes/known_issues.md` issue #25.

---

## v0.1.2 (2026-07-16)

**Summary:** Mostly a documentation-accuracy release: several features already present since v0.1.1 (or earlier) had never been written up in the README or a changelog at all, and a proofread pass against the actual source found real inaccuracies in the ones that were. One small functional fix included (INI comment text only — no behavior change).

### What's New
1. **`mw3ncp_config.ini`** — self-generating configuration file, written next to the
  DLL the first time the project runs, with every option pre-filled at its default value
  and a comment explaining it. Covers `[Look]` (sensitivity, ADS slowdown strength,
  invert look), `[Stance]` (B hold-vs-tap threshold), `[Interact]` (hold threshold),
  `[Survival]` (ready-up hold threshold), `[Sprint]` (stamina/regen seconds), and
  `[Bindings]` (button layout, stick layout, trigger flip). No live-reload yet —
  changes take effect on next launch. See README's **Configuration & customization**
  section for the full key reference.
2. **Button layout presets** — `Default` / `Tactical` / `Lefty` / `TacticalLefty`,
  reconstructed from the unchanged CoD4→MW2→MW3 console control scheme (not
  independently verified against real hardware yet — `TacticalLefty` in particular
  may need a correction pass; see README for the full per-preset table).
3. **Stick layout presets** — `Default` / `Southpaw` / `Legacy` / `LegacySouthpaw`.
  `Legacy` swaps only the horizontal axes between sticks (left stick keeps
  forward/back but turns instead of strafing; right stick keeps look up/down but
  strafes instead of turning) — the historical CoD4-era scheme, not a full stick
  swap.
4. **`FlipTriggers`** — an independent toggle that swaps RT↔RB and LT↔LB, layered on
  top of whichever button layout is active.
5. **Invert Look** — the OG console option, flips vertical look.
6. **Interact (X) requires a hold, not an instant tap** (task #11) — a press released
  before the threshold (740ms default, configurable) simply does nothing; Reload (a
  separate real kbutton on the same physical button) is completely unaffected and
  still fires instantly. This was already implemented but wasn't reflected in the
  task list or any doc until now.
7. **ADS look-slowdown fix and the Crouch/Prone real-toggle rewrite** (see v0.1.1's
  own entry below for what these actually fixed) are now covered by full mechanic
  tables in README (the stance ladder's tap/hold-per-state table, and Sprint's
  ready/sprinting/winded/regenerating state machine) — neither had a full
  state-transition writeup anywhere before this pass.

### Fixed
1. **Three inaccurate setting descriptions**, found during the proofread pass above,
  corrected in both `mw3ncp_config.ini`'s self-generated comments
  (`mod_config.cpp`'s `WriteDefaultConfig`) and the matching README prose:
  - `[Look] Sensitivity` was described as "right-stick" unconditionally — it's
    actually whichever stick `StickLayout` currently routes to look, not always the
    right stick.
  - `[Stance] ProneHoldThresholdMs` described "hold" as simply "go prone," which is
    wrong for the Prone→hold transition specifically (that one stands you back up,
    the reverse). Corrected to describe the full 3-state ladder.
  - `[Interact] HoldThresholdMs` incorrectly claimed a quick tap "switches weapons
    instead" — that's Y/ready-up's behavior, not Interact's. A quick Interact tap
    does nothing; Reload (separate, same physical button) is unaffected either way.
  - Rebuilt `d3d9.dll` so the corrected INI comments actually ship (no other
    behavior change in this build vs. v0.1.1).

---

---

## v0.1.1 (2026-07-16)

**Summary:** Fixes a real keyboard/mouse sprint regression (the controller Sprint hooks were unconditionally clearing the real sprint bit every tick, even with no controller connected) and a look-slowdown formula that could invert look direction on deep zooms. Crouch/Prone rewired onto the real native toggle, fixing a stuck-prone bug along the way. B now backs out of menus like ESC.

### What's New
1. **B backs out of menus like ESC.** B now forwards a real ESC keypress
  (`FUN_004d9850`) to whatever menu is currently active — the same real mechanism the
  engine's own key handler uses for ESC generically, not something pause-specific —
  so B backs out one level in the main menu, closes the pause menu (same as Start),
  or exits any other open menu. Hardcoded to physical B regardless of button-layout
  preset.
2. **`tools/memdiff` gained a `poke` mode** (write-test a candidate memory address's
  behavioral effect live, with a configurable lead-in countdown) and a `rangewatch`
  mode (live-correlate a real key/button against one fixed, already-known-real
  address range instead of the whole process heap) — dev-only diagnostic tooling,
  not shipped as part of the project itself. Rebuilt as x64 (was x86, which started
  hitting its own ~2GB address-space ceiling once heap-scan caps were widened).
3. **Keyboard/mouse deprioritized as a primary input path, not removed.** A direct
  consequence of the regression above: keyboard/mouse remains functionally required
  for menu navigation, Back, and most killstreak call-ins (none of which are
  controller-native yet), but is no longer verified to the same live-reproduction
  bar controller features get going forward. Controller is the primary,
  actively-verified input method with this project installed. See
  `re_notes/known_issues.md` issues #10-#11.

### Fixed
1. **Keyboard/mouse sprint regression.** The controller Sprint hooks
  (`InjectControllerSprintPmFlags`/`ReassertSprintPmFlags`) are wired directly into a
  real per-tick engine entry point and ran unconditionally — with no controller
  engaging sprint, they unconditionally cleared the real `pm_flags` sprint bit every
  tick, silently breaking vanilla keyboard Shift-to-sprint regardless of whether a
  controller was plugged in at all (a fully-unplugged controller, or one connected
  but idle, both triggered it). Fixed with bit-ownership tracking: the hooks now only
  ever clear a bit they set themselves, leaving real keyboard/native input completely
  untouched otherwise.
2. **ADS look-slowdown could invert look direction on deep zooms.** The slowdown
  formula was a linear blend (`1 - strength*(1-ratio)`) that went negative — inverting
  look — for any configured strength above 1.0 once the zoom ratio dropped low enough
  (a real ACOG-level zoom, not an edge case). Not a native engine bug, not FPU
  corruption (both theories investigated and ruled out via diagnostic logging) — just
  the formula's own shape. Fixed by switching to a power curve (`ratio^strength`),
  which can never go negative for any non-negative strength while still allowing a
  stronger-than-proportional slowdown at high strength values.
3. **Crouch/Prone rewired to the real native togglecrouch/toggleprone toggle**,
  replacing the project's own tracked stance state and per-frame bit-forcing. Fixes a real
  stuck-prone bug (a Campaign session neither B nor Sprint could recover from stance
  lock, but real keyboard Ctrl could) and, as a side effect, a separate game-breaking
  bug where using the Predator missile killstreak while prone left the player
  permanently stuck prone. The stance ladder's user-facing behavior (tap/hold →
  crouch/prone, see README) is unchanged — only the underlying implementation, which
  no longer has a separate copy of stance state that can desync from the engine's own.

### Groundwork
1. **106 controller button-glyph icons extracted** (`assets/button_glyphs/`) covering
  Xbox 360, Xbox One, Xbox Series X|S, PS3, PS4, PS5, D-pad/stick-direction
  indicators, and shared/extra buttons — source art groundwork for native
  controller-glyph button prompts. **Not yet wired into any rendering code** — this
  is asset preparation only; see `re_notes/ui_assets.md` for the two remaining
  pieces of implementation work (a bind-text-resolver hook, and getting the art into
  a font the game will actually render) and one known outstanding polish issue (a
  faint stray text fragment in a couple of PS4 icons).

### Investigated, Not Yet Resolved
1. **Sprint's real `+breath_sprint` kbutton — parked.** Three independent techniques
  (whole-process heap correlation, live write-testing the strongest candidates, and
  a targeted scan restricted to the confirmed-real kbutton neighborhood used by
  ADS/Reload) all came back negative. Controller Sprint keeps its existing
  `pm_flags`-forcing implementation. Full trail in `re_notes/iw5sp.md`.
2. **Real native sprint duration/cooldown timer — found, but unobservable while our
  own hook drives sprint.** Traced the real sprint-meter HUD render path to
  `FUN_004b9350`, a genuine current/max stamina-ratio function — but it early-exits
  to a flat baseline whenever `pm_flags` bit `0x4000` is already set, which this
  project's own Sprint hook forces unconditionally every tick. So the real timer can't
  be observed (or benefited from) as long as sprint is driven by forcing that bit
  directly rather than through the real native trigger path. Switching to whatever
  real `kbutton_t`/command actually engages sprint (once found — see the parked
  `+breath_sprint` search above) would make the project's own sprint naturally subject
  to the real timer, perk multipliers, and Extreme Conditioning, without needing to
  replicate any of it by hand — a real architecture change, not attempted yet, and
  current sprint behavior is already confirmed working well so this needs a
  deliberate decision before touching it. Full trail in `re_notes/iw5sp.md`.

---

---

## v0.1.0-prealpha (2026-07-15)

**Summary:** Initial pre-alpha release. Analog movement, look, and most buttons confirmed working live against `iw5sp.exe` (Campaign/Survival) -- see the list below for exactly what's covered. Known limitations at this release: Back unassigned, killstreaks need per-killstreak work, full menu/UI navigation not implemented, Multiplayer not started.

### What's New
1. Analog movement (left stick) and look (right stick), both driven through real
  engine calls, not mouse/keyboard emulation.
2. Fire (RT), ADS (LT, true hold via real `kbutton_t` calls), Melee (R3),
  Tactical/Lethal (LB/RB), Jump (A).
3. Crouch/Prone stance ladder (B) — tap toggles crouch, hold goes prone.
4. Interact + Reload (X) — real, context-sensitive `kbutton_t`.
5. Sprint (L3) — real `pm_flags` bit, auto-stands from crouch/prone first, with a
  real stamina/cooldown model (4s sprint, 2s cooldown), correctly bypassed during
  missions that live-set `player_sprintUnlimited`.
6. Weapon switch (Y) — real `weapnext` dispatch.
7. Start — opens and closes the pause menu via real engine calls, working even while
  the game's own gameplay-simulation tick halts during pause.
8. D-pad (all 4 directions) — real `+actionslot 1-4` dispatch, data-driven by
  loadout.
9. Survival ready-up (hold Y ~740ms between waves) — the one deliberate exception to
  this project's native-only approach (synthesizes a real F5 keypress; the real native
  trigger was never found despite an extensive search).
10. Buy-station + pause interaction fix (a real native engine bug, not ours, that
  could permanently break all input until level reload).

Known limitations at this release: Back unassigned, killstreaks need per-killstreak
work, full menu/UI navigation not implemented, Multiplayer not started.

