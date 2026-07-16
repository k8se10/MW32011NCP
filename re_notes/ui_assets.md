# Controller UI assets — scope note (2026-07-14)

## Locked scope clarification (user, 2026-07-14)
"Native" support means more than analog movement/look/aim-assist — it also means:
1. **In-game/menu button-prompt icons swap to controller glyphs** (Xbox-style
   A/B/X/Y/LB/RB/LT/RT/D-pad, not just relabeled keyboard hints) when a controller is
   the active input device, matching the console build's behavior.
2. **A real controller options menu/screen** (sensitivity, invert Y, deadzone, stick
   layout preset, vibration) — not just "it works," a proper settings UI for it.
This is folded into task #6 (controller menu/UI navigation) scope, not a separate
later add-on.

## Asset investigation (2026-07-14)
Checked whether the shipped PC game files already contain unused Xbox 360/PS3 button
icon art (would save having to author new icon assets from scratch). Searched every
`.iwd` archive (readable directly with `7z`, since `.iwd` is plain zip) for
controller-icon-sounding image names.

**Found (confirmed real, in `main/iw_04.iwd` and `main/iw_05.iwd`):**
- `images/ps3_lstick.iwi`, `images/ps3_rstick.iwi` — PS3 analog stick icons.
- `images/ui_button_xenon_dpad_64x64.iwi` — Xbox 360 ("Xenon" was its internal
  codename) D-pad button icon, `ui_` prefix confirms it's a real UI element, not a
  world texture.
- Also many `dpad_*`/`iw5_dpad_*` images — but these are the **killstreak-selection
  radial-wheel icons** (ac130/uav/predator missile/etc.), not controller-button
  prompts — same feature exists on PC via a keybind, unrelated to this investigation
  despite the "dpad" naming.

**NOT found in `.iwd`:** no full Xbox A/B/X/Y or LB/RB/LT/RT set, no PS3
Cross/Circle/Square/Triangle set, no generic "controller button" HUD prompt icons
(the kind that would replace "Press [E]" with a button glyph in-game).

**Not yet checked: `.ff` fast-file zone archives** (`zone/english/*.ff`,
`zone/dlc/*.ff`) — these are IW's proprietary compressed format, not plain zip, so
`7z` can't list them directly. UI textures actually used by the live menu system are
often baked into a fastfile zone (`code_pre_gfx.ff`, `ui_mp.ff`, `common.ff` are
likely candidates by name) rather than left loose in `.iwd`. **This is where the rest
of a full button-icon set most likely lives, if it exists at all** — needs a
CoD/IW5-specific fastfile unpacker tool (community tools like Wraith/Greyhound exist
for later IW-engine titles; IW5/MW3-specific compatibility not yet confirmed) before
this can be checked. Not yet done.

## How MW3 actually renders button-prompt hints (2026-07-16 research pass)

Investigated whether hint text (`"Press [E] to interact"`-style) is drawn as an image,
or substituted as a glyph character from a custom icon font (the common IW-engine/
Quake3-descended pattern — "buttons rendered as a font"). **Partially confirmed, with
a hard blocker:**

- **Font system is real and texture-atlas based, not `.ttf`.** `FUN_00619020`
  (decompiled) registers every UI/HUD font by name via
  `thunk_FUN_0045d040("fonts/bigfont"|"smallfont"|"consolefont"|"boldfont"|
  "normalfont"|"extrabigfont"|"objectivefont"|"hudbigfont"|"hudsmallfont", 0)` — the
  real `RegisterFont`-equivalent. `FUN_00618b80` separately registers the
  `ui_smallFont`/`ui_bigFont`/`ui_extraBigFont` scale dvars. The actual glyph atlas
  images are real, loose files (found via `7z l`): `images/gamefonts_pc.iwi` (in
  `main/localized_english_iw00.iwd`) and `images/devfonts_pc.iwi` (dev-only, in
  `main/iw_01.iwd`) — but only ONE such atlas exists per platform; no separate Xbox/
  PS3 glyph atlas is loose anywhere in the `.iwd`s.
- **The `"&&N"` placeholder-substitution engine is real and is exactly the
  described mechanism** — `FUN_00433a10` (decompiled) scans a localized string for
  `"&&"` + a digit and replaces it with the Nth arg from an arg-array, called via
  `FUN_005098e0` from hint-builders like `FUN_00568110` (weapon-pickup/swap hint).
  Localized keys carrying these tokens (confirmed via string search + code xrefs):
  `PLATFORM_PICKUPNEWWEAPON`, `PLATFORM_SWAPWEAPONS`, `PLATFORM_THROWBACKGRENADE`,
  `PLATFORM_MANTLE`, `PLATFORM_RELOAD`, `PLATFORM_STANCEHINT_STAND`/`_CROUCH`/
  `_PRONE`, `PLATFORM_HOLD_BREATH`, `PLATFORM_VEH_*`.
- **The blocker: every branch of the bind→text resolver is plain-text-only.**
  `FUN_0061f6f0` (→ `FUN_0061f590`/`FUN_004d6da0`) resolves a bound command (e.g.
  `"+activate"`) to 0/1/2 bound keys and returns either `"KEY_UNBOUND"`, a single
  key-name string, or `"%s KEY_OR %s"` for two bindings — **plain text in every
  branch, no device check, no glyph-codepoint path at all.** A full string search for
  controller keynum constants (`K_BUTTON`, `K_STICK`, `K_DPAD`, `K_XBOX`, `keynum`,
  etc.) found **zero genuine hits anywhere in the binary** — consistent with the
  project's original finding that this PC build has no controller input path
  whatsoever, extending all the way to the UI/hint layer too.

**Implication:** implementing real controller-glyph hints needs two independent
pieces of work, not one: (1) hook `FUN_0061f6f0` to return a private-use-area glyph
character instead of plain text when a controller is active, and (2) get actual
button-glyph art into a font the game will render at that call site (`hudbigfont`/
`objectivefont` per `FUN_00619020`) — via either the still-unopened `.ff` zone
archives (in case a console glyph atlas is hiding there) or freshly authored/sourced
art. Neither piece has been started yet; see below for what SOURCE art now exists.

## Glyph source art — acquired and extracted (2026-07-16)

User supplied a full reference sheet (all platforms: Xbox 360, Xbox One, Xbox Series
X|S, PS3, PS4, PS5, D-pad/stick-direction indicators, shared/extra buttons) as a
single PNG with a real alpha channel (transparent background, not a flat color to
chroma-key). **106 individual icons extracted and committed** to
`assets/button_glyphs/` (not yet wired into any rendering code — pure source-art
groundwork for now):

- Extraction script (ad-hoc, not part of the shipped mod, not currently checked into
  the repo as a reusable tool — lives only in this session's scratch dir) used the
  source image's real alpha channel directly (`alpha > 40` = real content, not the
  faint background "grain" texture noise) rather than color-keying, since some
  legitimate icons (Xbox One/Series bumpers, View/Menu buttons) are themselves
  black-filled and would be indistinguishable from a black-keyed background.
- Per-icon bounding boxes are detected individually (per-column vertical content
  scan, taking the icon's own content run rather than a shared per-row Y assumption)
  — needed because icon height varies within a row (D-pad/triggers are taller than
  face buttons) and label-to-icon spacing isn't perfectly uniform column to column;
  an initial shared-row-band approach clipped tall icons and let label text bleed
  into some crops before this was caught.
- Layout quirk handled: the D-pad-directions/stick-directions row and the extra-
  buttons row have icon-THEN-label ordering (label below the icon), the opposite of
  every controller row (label-then-icon, label above) — needed a first-vs-last-run
  selector per row rather than one universal rule, plus explicit exclusion of each
  row's own group-title text band (a third text layer only present on those two
  rows) from the search window.
- **Known outstanding polish issue, not yet resolved:** a small number of PS4 icons
  (at least `ps4_circle`) still show a faint stray text fragment bleeding in from
  adjacent title text in that specific column — an isolated case where no clean
  background gap existed between the title-text fragment and the icon in that exact
  column's pixel slice, defeating the run-detection gap check. Not blocking (icon is
  still recognizable), but worth a manual touch-up pass later.

## Real menu-state groundwork already in place (2026-07-16, from the B/pause work)

Task #6 (full controller menu/UI navigation — D-pad/stick item selection, buy
stations, options) hasn't been started, but the B-as-menu-back and Start-pause
fixes landed this session (`analog_input_hooks.cpp`, see `known_issues.md`
issue #13) already found and validated two of its real prerequisites, purely
as a side effect of fixing live-reported bugs, not dedicated task #6 work:

- **`IsMenuActive()`** — the real per-player "a menu is currently open" gate
  bit (`0x10` at `0xB36210`). Confirmed live to correctly detect the pause
  menu, the main menu, and buy-station menus alike (not pause-specific).
  Whatever eventually drives D-pad/stick item navigation will need this same
  gate to know when it should even be active.
- **`ForwardKeyToMenu` (`FUN_004d9850`)** — the real call that forwards an
  arbitrary keycode to whatever menu is currently open, confirmed live for
  keycode `0x1b` (ESC/back). The same mechanism should generalize to other
  keycodes (arrow keys, Enter) for real D-pad/stick-driven item selection —
  this hasn't been tried yet, but it's the same call, not a new one to find.
- **The always-running WndProc/`SetTimer` tick** (`InjectMenuInputTick`,
  `d3d9_hook.cpp`) — proven necessary because menu-facing input has to keep
  working while the gameplay-simulation tick is halted (paused, or any menu
  open). Any future menu-navigation polling belongs on this same tick, not
  the gameplay one.

None of this is buy-station/options-menu navigation itself — no D-pad/stick
item selection exists yet — but the three pieces above are exactly the
foundation task #6 needs to build on, already found and live-verified rather
than left as open questions.

## Open questions / next steps for task #6
- Actually wire up the two rendering pieces above (bind-text-resolver hook +
  in-game-visible glyph font) — not started.
- Get a working `.ff` unpacker for this exact game version and check for a fuller
  console button-icon set inside the zone files, in case one already exists (would
  save relying solely on the freshly-sourced art above for the final in-game look).
- Decide on a `GlyphStyle` config option (Xbox 360 / Xbox One / Series X|S / PS3 /
  PS4 / PS5), similar to the existing `ButtonLayout`/`StickLayout` config, so players
  can pick their preferred prompt look independent of which physical controller
  they're using (XInput doesn't distinguish Xbox-branded from PlayStation-branded
  controllers on Windows, so this can't be auto-detected from the input API alone).
