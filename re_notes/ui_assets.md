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
- **Superseded 2026-07-17** — see the section below. The original 106-icon set's
  text-bleed issue is resolved by starting from a cleaner source sheet instead of
  patching the extraction heuristics further.

## Glyph source art — re-extracted from a slimmed, user-trimmed sheet (2026-07-17)

The original 106-icon set (all platforms, all generations) had too much real-world
duplication for this mod's purposes (Xbox One/Series and PS4/PS5 button art are
functionally near-identical for glyph-prompt purposes) and one unresolved text-bleed
polish issue (see above). The user manually trimmed the source reference sheet down
to three representative style groups and removed unwanted label text, saved as
`buttons.png` at the game install root — same real-alpha-channel PNG format as
before (`Format32bppArgb`, transparent background at `A=0`, opaque icon content at
`A=255`), 1536×1024.

**Extraction rebuilt from scratch as a proper connected-component labeler** (not
row/column-band heuristics, which is what caused the old text-bleed issue) — a
small C# tool (`csc.exe`-compiled, not checked into the repo as a shipped tool,
lives in this session's scratch dir), flood-fills every region of `alpha > 30`
pixels using 8-connectivity, discards blobs under 25px (antialiasing dust), and
crops each surviving blob straight out of the source bitmap with a 3px padding
margin. This finds each icon's real bounding box directly from its own pixel
content — no assumptions about row height, label position, or shared bands, so it
can't clip a tall icon or bleed in a neighboring label the way the old approach
did. **44 icons extracted cleanly, spot-checked individually (including the
specific icon that bled text last time, `ps_circle`, now clean) — zero clipping or
bleed found.**

**Layout confirmed and named:**
- **Row 1 — Xbox 360/Classic:** `xbox360_{a,b,x,y,lb,rb,lt,rt,back,start}` (10)
- **Row 2 — Xbox Modern (One/Series):** `xboxmodern_{a,b,x,y,lb,rb,lt,rt,view,menu,ls,rs}` (12)
- **Row 3 — PlayStation (PS4/PS5-era):** `ps_{cross,circle,triangle,square,l1,r1,l2,r2,create,options,touchpad,l3,r3}` (13)
  — `ps_create`/`ps_options` confirmed with the user (not Share/Options or
  Select/Start) — PS5-style Create (left, hamburger + left-triangle glyph) and
  Options (right, hamburger + right-triangle glyph).
- **Row 4 — universal, brand-independent:** `dpad_up` (1) — the user's explicit
  design intent: D-pad only needs ONE real icon, the other three directions are the
  identical asset rotated 90°/180°/270°, generated programmatically
  (`dpad_right`/`dpad_down`/`dpad_left`, `Bitmap.RotateFlip`) rather than as
  separate source crops — confirmed correct by inspection (highlight moves to the
  correct segment in each rotation). Plus `stick_{ls,rs}_{up,down,left,right}` (8)
  — generic stick-direction indicators, same asset regardless of controller brand.

**Total: 47 files** (44 extracted + 3 generated rotations), replacing the old
106-file set entirely in `assets/button_glyphs/`. Still not wired into any
rendering code — this remains pure source-art groundwork for the eventual
bind-resolver-hook + custom-font work described above (task #6's other half,
`FUN_0061f6f0` hook + glyph font, still unstarted).

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

## Hint-text content survey — what actually needs replacing (2026-07-17)

Pulled the real English localized string content (via the existing OpenAssetTools
zone-dump pipeline, `zone_dump/english/localizedstrings/code_post_gfx.str` — 297
`PLATFORM_*` entries) to see exactly what text the pickup/reload/interact-style
hints actually contain, ahead of wiring the `FUN_0061f6f0` bind-resolver hook.
Found **three genuinely different substitution mechanisms** in the string data,
not one:

1. **`&&1`-token strings — confirmed mechanism, this is what the planned resolver
   hook covers.** Traced the real hint-builder for weapon pickup/swap,
   `FUN_00568110` (found via `FindCallers` on `FUN_004fafd0`, a thin wrapper around
   `FUN_0061f6f0`): resolves a bind command (`"+activate"`/`"+frag"`) to display
   text via `FUN_004fafd0`, picks the right localized key, splices the resolved
   text in via `FUN_005098e0` → `FUN_00433a10` (the `&&N` engine, confirmed via
   full decompile to ONLY handle `"&&"` + digit — nothing else). Real text:
   - `PLATFORM_PICKUPNEWWEAPON` = `"Press^3 &&1 ^7to pick up"` (`+activate`)
   - `PLATFORM_SWAPWEAPONS` = `"Press^3 &&1 ^7to swap for"` (`+activate`)
   - `PLATFORM_THROWBACKGRENADE` = `"^3&&1 ^7throw back"` (`+frag`)
   - `PLATFORM_MANTLE` = `"Press^3 &&1 ^7to  "` (`+gostand`)
   - `PLATFORM_STANCEHINT_STAND/_CROUCH/_PRONE/_JUMP` = `"Press^3 &&1 ^7to
     stand/crouch/go prone/jump"`
   - `PLATFORM_HOLD_BREATH` = `"Hold^3 &&1 ^7to steady"`
   - `PLATFORM_PICK_UP_{JUGGERNAUT,MARTYRDOM,STOPPING_POWER,DOUBLE_TAP,
     LAST_STAND,SLEIGHT_OF_HAND}` = `"Press^3 &&1 ^7to pick up <perk>"`

   This is the entire "picking up weapons/perks, interacting with pickups" family
   — all of it resolves through `FUN_0061f6f0`, so the planned single hook covers
   all of it at once.

2. **Reload has no hint text at all.** `PLATFORM_RELOAD` is literally just
   `"Reload"` — no token, no bind reference. The reload prompt is apparently a HUD
   ammo-counter/icon element, not a discoverability text prompt — nothing to
   glyph-swap here.

3. **A second, SEPARATE mechanism: `[{+command}]` embedded directly in the
   localized string itself**, e.g. `PLATFORM_GET_THUMPER` = `"[{+activate}]Thumper"`,
   plus `PLATFORM_RESUPPLY`, `PLATFORM_GET_KIT`, `PLATFORM_REVIVE`,
   `PLATFORM_GET_KILLSTREAK`, `PLATFORM_DETONATE`, `PLATFORM_HOLD_TO_USE/_DROP`.
   Confirmed via full decompile that `FUN_00433a10` (the `&&N` engine) does NOT
   handle this syntax — it's a genuinely different resolver. **RESOLVED
   2026-07-17: good news, no second hook point needed.** Traced the real chain:
   `FUN_005519d0` is the general `[{...}]` bracket-token scanner (splices
   whatever it captures between `[{` and `}]` out to `FUN_00622020`), and
   `FUN_00622020` itself calls `FUN_0061f6f0()` FIRST — the exact same
   bind-resolver function the `&&N` path already calls via its
   `FUN_004fafd0`/`FUN_004be070` wrappers. Only on failure (unbound) does it fall
   through to two unrelated tutorial-timer special cases and a `"KEY_UNBOUND"`
   fallback. **Both mechanisms are calls to the same `FUN_0061f6f0` address — one
   hook covers both**, though the detour needs to handle two different
   calling-convention shapes at the call site (the `&&N` path passes the command
   name as an explicit stack arg via its wrappers; the `[{...}]` path passes it
   through a register, consistent with this codebase's recurring register-
   convention pattern elsewhere — not yet confirmed to the exact register, would
   need a live test before wiring the actual detour).

4. **A third category no resolver hook can fix**: literal hardcoded PC-only text
   baked directly into the string, e.g. `PLATFORM_USE_BUTTONLOOK_TO_AIM` =
   `"Hold ^3[Right Mouse]^7 to aim"`, `PLATFORM_VEH_FIRE` = `"[Left Mouse] Fire"`.
   No token, no bind reference — these need actual string replacement/localization
   overrides if they're ever to say something controller-appropriate, not a
   resolver hook.

## Font pipeline — major finding, real tool-supported path found (2026-07-18)

Both open blockers from the "Implication" section above have real answers now.

**Dormant console glyph atlas: CLOSED, clean negative result (2026-07-18,
final pass).** Checked across `common.ff`, `code_post_gfx.ff`,
`common_specialops.ff`, `common_survival.ff`, `dubai.ff`, `berlin.ff`,
`rescue_2.ff` (earlier pass), plus `code_pre_gfx.ff`, `code_pre_gfx_mp.ff`,
`ui_mp.ff`, `localized_ui_mp.ff`, and 4 representative `patch_*.ff` zones
(`patch`/`patch_mp`/`patch_specialops`/`patch_survival`, this pass) —
**no real A/B/X/Y or PlayStation face-button icon set exists anywhere in
this retail PC build's shipped data.** The MP-side dump has a LARGER
killstreak-icon roster than SP (ac130/advanced_uav/attack_helicopter/ims/
reaper/sam_turret/sentry_gun/stealth_bomber/talon/uav/etc.) but every one
is a killstreak-selector icon, same category as the already-known
`dpad_killstreak_*` set — not a controller button prompt. No `fonts/`
directory at all exists in the combined 395-image/406-material asset set
checked. **The user-supplied glyph art (`assets/button_glyphs/`) is
genuinely the only source for this feature — there's nothing dormant to
recover.** (Not literally exhaustive: ~25 remaining per-map `patch_mp_*`/
`patch_so_*` zones weren't individually dumped, vanishingly unlikely to
differ from the generic patches already checked.) Re-verified
`FUN_0061f6f0` (the bind resolver) is still confirmed plain-text-only in
every branch, zero glyph-codepoint path — no change to the 2026-07-16
finding.

**Font asset format and build tooling: fully resolved, this is the real
breakthrough.** OpenAssetTools' font format is documented and human-editable:
`fonts/*.json` (schema `font.v1.json`) is a flat
`{material, glowMaterial, pixelHeight, glyphs:[{letter, x0,y0,dx,pixelWidth,
pixelHeight,s0,t0,s1,t1}]}` — `letter` accepts arbitrary integer codepoints
(confirmed via real existing entries using raw ints up to 173, not just
printable ASCII), and each glyph is just a UV rect into an atlas texture
referenced by name from a separate material JSON
(`materials/fonts/gamefonts_pc.json` → `textures[0].image = "gamefonts_pc"`).
**`D:\Tools\OpenAssetTools\extracted\` also has `Linker.exe`** (the
Unlinker/extraction tool's build counterpart, previously not noticed) and
`ImageConverter.exe` (confirmed `--iw5` flag support) — a full, offline,
tool-supported pipeline exists end to end:
1. Convert `assets/button_glyphs/*.png` (the 47-file set above) to IW5 image
   format via `ImageConverter.exe --iw5`.
2. Wrap the converted images in a new/extended material JSON, following the
   real `gamefonts_pc.json` pattern.
3. Add glyph entries pointing at UNUSED byte codepoints — the render path
   uses single-byte extended-ASCII strings, not wide chars, so there's no
   true Unicode private-use area available as originally assumed, but
   unused low bytes (e.g. `0x81`, `0x8D`) work — to a copy of `hudBigFont`/
   `objectiveFont`'s `font.v1.json`.
4. `Linker.exe -l <existingZone> <newProject>` builds a real, loadable `.ff`
   that references existing game assets alongside the new font/material/
   images — this mod's target for the actual asset build step.

**Runtime-load precedent already exists in this mod's own code.**
`analog_input_hooks.cpp`'s `zoneload-test` code already calls the real
native `FUN_004ca310` (load-zone-by-name) then `FUN_004adc60` (find-asset-
by-path — used there for a menuList) — the same two-step pattern (load a
`Linker`-built zone, then find/register the new asset by path) should
generalize to a font asset.

**RESOLVED (2026-07-18): the Font-equivalent function was already sitting
in this codebase, no new RE needed.** `FUN_004adc60` and the already-used
font-boot-registration function `FUN_0045d040` are both thin wrappers
over the SAME generic asset-lookup function, `FUN_004ff000(assetType,
path, flag)` — confirmed via decompile: `FUN_004adc60` hardcodes type
`0x19` (menu), `FUN_0045d040` hardcodes type `0x18` (font). `FUN_00619020`
(the real font boot-registration function, already documented above)
calls `thunk_FUN_0045d040("fonts/bigfont", 0)` etc. for all 9 stock
fonts — same call shape as `FUN_004adc60`'s own documented real site.
**Calling convention**: `__cdecl`, one meaningful stack argument (the
path string) — confirmed via fresh disassembly of `FUN_004ff000` itself
(139 instructions, plain stack-arg entry, no register-passed args),
consistent with this project's own already-documented convention for
`FUN_004adc60`/`FUN_0045d040` (`analog_input_hooks.cpp`'s existing
`FindOrLoadMenuList` declaration).

**Concrete next implementation step**: after loading a `Linker`-built
zone via the existing `FUN_004ca310` pattern, call
`FUN_0045d040("fonts/<yourfontname>", 0)` — a
`FindOrLoadFontFn`/`FindOrLoadFont` wrapper mirroring the existing
`FindOrLoadMenuList` declaration exactly, just retargeted to
`0x0045d040`. **Not yet investigated**: `FUN_004ff000`'s full internal
logic and the exact returned-pointer shape for the `Font` type
specifically (vs. `MenuList`'s known `{int count; menuDef_t** menus}`
shape) — would need a live test or an actual built font asset to inspect
once one exists.

**Recommendation, revised**: font-injection is now concretely tool-supported
rather than purely theoretical, and is recommended over the earlier
"image-overlay on top of hint text" alternative — font injection reuses the
game's own existing text-layout/positioning system for free, avoiding new
placement math an image-overlay approach would need. Not verified live, not
implemented — this is a build-pipeline finding, not proof it works end to
end in the running game.

### Build pipeline PROVEN end-to-end (2026-07-18)

A dedicated pass actually ran the full theorized pipeline (PNG → IW5
image → material → font → linked `.ff`) rather than leaving it as
theory. **Result: it works. A real, valid `.ff` file was built**
(`testfont.ff`, 867 bytes, 0 warnings/0 errors) containing a custom
controller-glyph image, material, and font, linked against the real
game's `code_post_gfx.ff` for the shared techniqueset. Six real gaps in
the theorized process were found and fixed along the way — genuinely
useful, since a future implementation pass would otherwise rediscover
each of these the hard way:

1. **Zone source syntax**: real zone files use single-comma with a path
   prefix — `font,fonts/name` — NOT the double-comma `font,,name`
   pattern (that's the `material,,name` convention, don't copy it
   across asset types). Also needs a `>game,IW5` header line.
2. **Material path convention**: a font's `material`/`glowMaterial`
   fields need the `fonts/` prefix (`"fonts/test_glyph_font_material"`),
   matching where the file physically sits
   (`materials/fonts/*.json`) — confirmed against the real
   `fonts/gamefonts_pc` material.
3. **`stateBitsEntry` must be EXACTLY 54 entries** — the material schema
   silently rejects any other length (`"StateBitsEntry size is not
   54"`).
4. **Image format must be `.iwi`, not `.dds`.** `ImageConverter.exe
   --iw5`'s real output format is `.iwi` — a `.dds` in the same folder
   is silently ignored (`"Missing asset"`), even though real DUMPED
   images are `.dds` (that's `Unlinker`'s extraction format, not
   `Linker`'s expected build input — easy to conflate).
5. **`techniqueset "2d"` must be resolved from a real base zone** via
   `-l "<path>/code_post_gfx.ff"` — confirms the `-l`-against-a-real-zone
   pattern already established for the options-menu injection research
   (issue #23) is the correct general approach, not specific to menus.
6. **A real font asset requires all 96 standard glyphs (codepoints
   32-127 inclusive) present** — a font with only a custom glyph is
   rejected (`"must contain all 96 required letters"`). **This is a hard
   schema requirement, not just a nice-to-have** — confirms the existing
   plan (extend a copy of a real font like `hudBigFont`, don't build a
   minimal custom-only one) is the only viable approach.

**Exact working command**: `Linker.exe --verbose -b fonttest -l
"<game_root>/zone/english/code_post_gfx.ff" testfont`, project laid out
at `fonttest/zone_source/testfont.zone` +
`fonttest/zone_raw/testfont/{fonts,materials/fonts,images}/`.

**Not attempted** (no game access): loading this built `.ff` into the
actual running game — this confirms the BUILD side works; RUNTIME
loading via `FUN_004ca310`/`FUN_0045d040` (the separate, already-resolved
font-lookup research above) is still unverified live. That live test is
now the single remaining step before button-glyph rendering can be
considered implementation-ready.

### `FUN_0061f6f0`'s real calling convention, disassembly-confirmed (2026-07-18)

Given the same-day rumble-hook crash (a fixed signature on a generic
dispatcher called with genuinely different real argument counts elsewhere
— see `known_issues.md` issue #24), this project's bind-resolver hook
plan for glyphs was independently re-verified via raw disassembly before
any hook gets written, not assumed safe. **Result: this one IS safe to
hook, a structurally different situation from the rumble dispatchers.**

`FUN_0061f6f0`'s real signature is a register+stack hybrid, NOT the plain
`__thiscall`/`__cdecl` a decompiler guess would suggest:
- `EAX` (register) = a context value, forwarded unchanged into both
  internal resolve calls.
- `ECX` (register) = the bind-name/command context (becomes `EBX`
  internally) — the actual "resolve this bind" argument.
- `[esp+4]` = pushed by every caller but never read anywhere in the
  function body — genuinely dead/unused.
- `[esp+8]` = output buffer pointer (used as `dest` for the real
  string-copy calls).
- `[esp+0xc]` = a bool flag ("limit to 1 bind").
- Plain `RET` (caller-cleanup for the stack portion).

**Both real call paths confirmed to conform to this exact convention**,
just via different internal mechanics: the `&&N`-token path
(`FUN_004fafd0`/`FUN_004be070`) explicitly re-loads `EAX`/`ECX` from its
own stack params before calling; the `[{...}]`-bracket path
(`FUN_00622020`, reached via `FUN_005519d0`) instead THREADS `EAX`/`ECX`
through unmodified via register-preservation from further up the call
chain (no re-derivation). Different asm shape, same real calling
convention for `FUN_0061f6f0` itself — which is what actually matters
for a detour on this one function.

**Recommended hook implementation**: a custom `__asm`-trampoline detour
(same style as this project's existing `CallKbuttonDown`/`CallKbuttonUp`
— NOT a plain C++ function pointer, since `EAX`/`ECX` are real register
args a naive `__cdecl` signature would misread). Call the real trampoline
first (so real text still resolves, for any fallback need), then
overwrite the output buffer at `[esp+8]` with a glyph codepoint sequence
when this mod's own controller-active state is true (a plain global bool
already maintained elsewhere in the DLL — safe to read from any hook
context).

**Not resolved, flagged rather than guessed**: `EAX`'s "contextA" value's
ultimate semantic identity was never fully traced — doesn't block a
blanket "controller active → show glyph" hook, but would matter if the
glyph substitution ever needs to be conditional per-bind rather than
global.

### The real bind-storage/lookup system, fully reconciled (2026-07-18)

A dedicated pass traced `FUN_0061f6f0`'s full resolution chain downward
to answer "how does the game know what key is bound to this action" —
and found a genuine unifying structural fact: **three things this
project had been treating as separate, possibly-overlapping tables are
actually ONE canonical numeric-ID space viewed from different angles.**

Chain traced: `FUN_0061f590` (a narrow context-aliasing table for 6
ambiguous dual-purpose binds like `+breath_sprint`/`+sprint_zoom`, not a
general resolver) → `FUN_004d6da0` (thin trampoline) → `FUN_0057e770`
(real per-call entry, writes `KEY_UNBOUND` or up to two key-name strings)
→ **`FUN_0057e640`, the real reverse-lookup function**:
```
FUN_005330a0(bindNameString) -> numeric ID   ; 0 = unbound
loop keycodes 0..0xFF (per-player table, base 0xA98E4C, stride 0xD28):
    if DAT_00a98e4c[keycode] == that ID: record this keycode as bound
    (stop after 2 matches)
```

**`FUN_005330a0`** (already known from the `notifyonplayercommand`
investigation as a linear scan over the 81-entry bind-name table at
`0x00929fa0`, confirmed index 1 = `"+attack"`) and **the raw-keycode
dispatch table** (`DAT_00a98e4c`, previously read forward as "the
`FUN_00438710` case number" for weapnext/D-pad/crouch dispatch) are NOT
two separate systems — they share ONE canonical ID: `+attack`'s
independently-confirmed `FUN_00438710` case (1/2) matches its
`FUN_005330a0` table index (1) exactly, not a coincidence. This one ID is
used for: (a) forward dispatch (keycode → ID → `FUN_00438710` case),
(b) `notifyonplayercommand` delivery matching (`Cmd_Argv` parsed as this
ID), and (c) this reverse "what key is bound to X" lookup.

**Complete, reconciled picture**: `bind KEY "+command"` lines in
`players2/config.cfg` populate the per-player raw-keycode table
(`DAT_00a98e4c`) at startup — each keycode slot stores the command's
canonical ID (same ID space as the 81-entry name table). `FUN_0061f6f0`'s
whole chain answers "what key is bound to X" via a classic reverse scan
over that same forward-mapping table — exactly the Quake3-family pattern
suspected but never located. **The 32-entry kbutton table
(`~0x0092a014`) remains genuinely separate and unrelated** — confirmed
elsewhere to have no runtime lookup role at all, just a registration
list.

**Practical implication**: doesn't change the existing glyph-hook plan
(still hook `FUN_0061f6f0` itself, already confirmed safe) — but the
ENTIRE resolver chain underneath it is now a single, coherent, fully
understood system, not three overlapping unknowns. One loose end: which
exact register carries the bind-name string into `FUN_005330a0` wasn't
nailed down to the final hop (`FUN_0061f6f0`→`FUN_004d6da0`) — likely
`EAX` (untouched through the rest of the chain, consistent with but not
confirmed identical to the "contextA" register flagged unresolved in
`FUN_0061f6f0` itself above) — only matters if a future need arises to
call this chain directly rather than just reading its output.

## Killstreak radial-wheel UI — checked, doesn't exist (2026-07-18)

The `dpad_killstreak_*` icon assets (found via `sp/survival_armories.csv`'s
icon column) raised a real question: does MW3 Survival have a classic CoD
radial equipment-wheel (hold to open, angle-select, release to confirm)?
**No — confirmed absent via two independent checks.** A case-insensitive
grep for "wheel"/"radial" across every real `.menu` file in the full
extracted `ui/` tree (all zones dumped this session) returned zero matches.
The actual asset those icons render into, `ui/dpad.menu`, is a **passive
HUD overlay**, not an interactive menu — `visible` gated on being in normal
gameplay (`!ui_active()`, `!usingvehicle()`, etc.), four `itemDef`s (one
per D-pad direction) that just show an icon + `keybinding("+actionslot N")`
text for whatever's currently equipped in that slot, gated on
`actionslotusable(N)`. No cursor position, no angle/stick logic, no
select/confirm action anywhere in it.

**Real killstreak/perk SELECTION happens entirely in the ordinary
buy-station LIST menus** (`ui/scriptmenus/survival_armory_airsupport.menu`,
`ui/ui/survival_armory_frontend_root.menu`) — plain `type 1` button items
with the same generic button-nav-group pattern this mod's existing D-pad+A
`InjectControllerMenuNav` (task #22) already handles, confirmed live.
**No new dedicated wheel-input code is needed** — equipping a killstreak is
"buy it at the list menu, it now shows in the `dpad.menu` HUD overlay,"
already fully covered. One adjacent, unexplored note:
`survival_armory_frontend_root.menu`'s top-level category tiles use a
`type 20` item, distinct from the `type 1` buttons already confirmed
working — not checked whether that specific item type needs its own
navigation handling.

## Main menu / title screen — CONFIRMED LIVE (2026-07-18)

Investigated whether the main menu/title screen (mode select, profile,
etc.) uses the SAME generic menu system this mod's D-pad+A navigation
already drives for pause/buy-station menus, or something structurally
different. **CONFIRMED LIVE by the user (2026-07-18)**: main menu, title
screen, buy-station screens, settings/options menus, and mission-intro
"press A to skip" prompts all work correctly with controller — "everything
to my knowledge works as it should in ui." This upgrades the entire "~150
presumed-covered" bucket in the full menu inventory below from
architecturally-consistent-but-unverified to real, user-confirmed
behavior across every UI surface actually exercised so far. The
`FUN_0057e480`/gate-bit/`FUN_004d9850` architecture (below) is the
correct explanation for WHY this works, not just a plausible theory.

**Original research, kept for the technical trail — strong
source-grounded evidence for "same system," independently confirmed
correct by the live result above:**
- This mod's `InjectMenuInputTick()` (driving `InjectControllerMenuNav`)
  runs off the WndProc/`SetTimer` subclass hook, installed in
  `Hook_CreateDevice` as soon as the real D3D9 device exists — before any
  level ever loads, and completely independent of the gameplay-simulation
  tick. A DIFFERENT, older finding ("hooks don't fire at all at the
  front-end") is about that old gameplay-tick path specifically and
  predates the WndProc tick by a day — does not apply here.
- `IsMenuActive()` (the `0x00B36210` bit `0x10` gate) is a flat,
  unconditional read with no level-loaded dependency anywhere in this
  mod's own code.
- `OpenMenuByName` (`FUN_00544a50`) is confirmed generic and name-keyed,
  not pause-specific — live-tested previously by opening `"pausedmenu"` by
  name, nothing about its signature implies special-casing for in-game
  menus only.
- A real front-end "menu-scripting system" (a table of callable UI-
  expression function names used by `.menu` files for profile/party/
  splitscreen logic) is independently confirmed to exist — consistent
  with one shared generic `.menu`/menu-registry architecture across every
  screen, standard for this engine family.

**Remaining gap**: whether the native front-end menu-OPEN code path
actually sets the same `0x00B36210` bit the pause/buy-station open paths
do was not traced in Ghidra this pass — the one thing standing between
"strong architectural case" and "confirmed." **Cheapest next step: just
try D-pad+A navigation at the actual main menu next time the game is
launched** — if it works, this task closes for free; if not, that's the
concrete thing to go trace natively.

**Other UI surfaces flagged, not checked**: a real "leaderboards" asset
folder exists (`code_post_gfx.ff`) but its menu-registration mechanism was
never investigated. End-of-mission summary/stats screens, loading-screen
tip displays, and save/load screens were neither found nor ruled out this
pass.

## Full menu inventory — 165 real menus catalogued (2026-07-18)

A dedicated audit dumped `ui.ff` fresh and enumerated every real menu asset,
to turn "full controller UI control" into a bounded, scoped punch list.
**165 real menus in `ui.ff`** (SP-side); `ui_mp.ff` contains **zero
menus** — it's a supplementary image/string zone, real MP menu content
lives in a zone this SP-focused project hasn't dumped yet (out of current
scope).

| Category | Count | Status |
|---|---|---|
| Main menu / mode-select / level-select | 44 | Presumed covered by the existing gate-bit + `FUN_004d9850` mechanism, not individually verified |
| Options/settings | 14 | Presumed covered; `pc_options_controls` is the one already directly targeted by task #23's injection work |
| Survival buy-station/armory | 12 | **Confirmed working live** (task #22) |
| Save/load + profile | 12 | Presumed covered EXCEPT `save_name_popmenu` — real text-entry field, needs dedicated design |
| Leaderboards | 30 | Presumed covered (read-only lists), not verified — low real-world priority, retail matchmaking/leaderboard backends are long dead |
| Facebook/social/online | 32 | Presumed covered except 2 text-entry popups — near-zero practical value, backend (Elite/Facebook integration) killed years ago, safe to deprioritize entirely |
| Misc (stats, warnings, dev menus) | 5 | Presumed covered |

**~13 menus total need real text-entry UI work** (not just live-verification
of the existing forwarding mechanism) — a genuine "needs new design"
category, not a "needs testing" one: `save_name_popmenu`,
`popup_facebook_pc_username`/`_password`, `popup_serverpassword`/
`popup_joinpassword`, `popup_callsign`/`popup_playername`/
`popup_use_elite_title`/`popup_use_elite_tag`, plus a few more in the same
families. These would need a real on-screen-keyboard-style D-pad text-entry
UI, not simple list/slider forwarding.

**Bottom line for scoping "full control"**: of 165 real SP-side menus,
~150 are plausibly already covered by the existing mechanism (unverified
but architecturally consistent with the confirmed-working cases), ~13 need
dedicated text-entry design work, and ~62 (leaderboards + social) are
low-priority given dead backend infrastructure. **No menu found that looks
structurally different from the already-reverse-engineered registry
system** — no evidence of a second, incompatible menu architecture
anywhere in `ui.ff`.

## First-launch welcome message — real MOTD system found, permanently disabled, safe to repurpose (2026-07-18)

Investigated whether MW3's real "message of the day"/featured-content
system (dead along with the rest of the original backend) is a safe
surface to repurpose for a custom first-launch "here's what this mod
does" welcome message. **Confirmed: yes, a real dedicated system exists
and is genuinely, permanently inert — a clean target.**

Two separate MOTD-shaped systems exist, don't conflate:
1. **Elite Clan MOTD** (`eliteclan_getmotd()`, tied to the dead Elite
   backend, only reachable from `page_elite_clan.menu`) — not relevant, a
   social/clan feature.
2. **The real target: a dedicated single-player MOTD system**, confirmed
   via a literal format string in `iw5sp.exe`: **`motd-sp-%s.txt`**
   (almost certainly a per-language file originally fetched from an
   Activision-hosted URL). Corroborating strings:
   `"Insufficient space for motd"` (a fixed-size local buffer — plain
   text, not a webview), `"Throttle time between motd update calls"`,
   localized fallback text `PLATFORM_NOMOTD` = *"Go online to get Modern
   Warfare 3 news and updates"* / `PLATFORM_NOMOTD_MP` = *"Welcome to
   Modern Warfare 3 multiplayer"* (shown when the fetch fails).

**The real display widget**: menu item `type 20` with a `newsfeed 1`
flag, backed by material `motd_ticker_bg` — a genuine scrolling-marquee
text item (a dev test harness, `menu_tickertest.menu`, confirms
`type 20`/`newsfeed`/`speed` are real, working properties). **This exact
ticker itemDef pair is wired into every real front-end/lobby menu
checked** (`coop_lobby.menu`, `specops_barracks.menu`,
`popup_callsign.menu`, `survival_armory_frontend_root.menu` — identical
boilerplate in all four) — **but both items carry `visible when(0)`, a
hardcoded compile-time-constant false, not a dvar/runtime check.**
Permanently, unconditionally disabled at the menu-definition level,
regardless of network/backend state — genuinely inert and safe to touch.

**Recommendation**: don't repurpose the real `motd-sp-%s.txt` fetch
pipeline itself (unknown URL base, throttling logic, buffer-size
constraints, plumbed through dead network code that would need
intercepting). Instead, use this mod's own already-proven
`RegisterMenu`/`OpenMenuByName` injection pipeline (task #23's
groundwork) to load a MODIFIED COPY of one of these menus (or a new
standalone popup) with `visible when(0)` flipped to `1` (gated on a
simple first-launch flag in `mw3ncp_config.ini`) and the ticker's text
hardcoded to a static welcome string — sidesteps the dead backend
entirely, reuses a real, already-styled native UI element, needs no
network/buffer-size RE.

**Not yet chased**: the exact native function that populates the
`newsfeed` item's live text buffer at runtime, and whether flipping
`visible when(0)` needs a compiled `.menu` edit (via the `Linker.exe`
pipeline documented above) vs. a simpler in-memory patch of the loaded
menuDef's boolean field once it's registered.

### Content draft (2026-07-18)

Researched MW3's own real popup-text conventions for a realistic size/
tone target: `ui/ui/offensive_warning.menu` (a real, shipped MW3 popup)
is 260 units wide, body text at `textscale 0.375` in a 244-wide
autowrapped box — real body text pulled from `code_post_gfx.str`
(`MENU_SP_OFFENSIVE_TITLE`/`_SKIP_2`) is ~26 chars of title + ~148 chars
of body + a ~58-char parenthetical footnote. That's the realistic target
shape: one short title, one short paragraph, an optional smaller
footnote — not a wall of text.

Cross-checked against this mod's own actual documented state (`README.md`,
`PATCHNOTES.md`) as of 2026-07-18 — current version **v0.1.3,
pre-alpha** — to avoid overclaiming. No real GitHub/Reddit/Discord URL is
documented anywhere in the repo; don't invent one.

**Three draft variations, closest-to-real-size first:**

```
Draft C (recommended — closest match to offensive_warning's real
proportions, lowest layout-fit risk):
^3Native Controller Mod - v0.1.3
^7Full analog movement, look, and menu navigation are live. A few
systems (killstreaks, aim assist, vibration) are still in progress.

Draft A (adds a footnote line, slightly longer):
^3MW3 Controller Mod - v0.1.3 (Pre-Alpha)
^7Native controller support for Campaign & Survival - not a keyboard/
mouse emulator. Movement, look, combat, menus, and pause all work with
a real controller today.
^8(Still in progress: some killstreaks, aim assist, and vibration
aren't finished yet - keep a keyboard nearby for menus that need it.)

Draft B (leads with the limitation instead of the feature list):
^3Welcome - Controller Mod Active (v0.1.3)
^7This is a pre-alpha native controller mod - most of Campaign and
Survival plays great with a pad, but it's not finished. Killstreaks,
aim assist, and vibration are still being built.
^8Something feel off? It's probably a known gap, not your controller.
```

All three deliberately avoid: claiming aim assist works (it doesn't,
disabled by default — see `feedback_aim_assist_disabled_public` memory
convention), claiming any specific killstreak works, and any fabricated
URL. Pick one (Draft C is safest for layout fit) once the technical
injection mechanism above is actually implemented and live-tested.

## WaW-style colored/animated clan tags — investigated, real risk found, not a quick win (2026-07-18)

Investigated whether Call of Duty: World at War's real "type a magic word
as your clan tag for a colored/animated effect" feature (`GOLD`/`RAIN`/
`CYCL`/etc. — confirmed real via public research: WaW let players enter
color names for solid-colored tags and special codes for scrolling/
rainbow/laser/bouncing animated effects, a feature removed in every later
CoD title) survived dormant in MW3, or could be rebuilt using MW3's own
systems.

**No WaW-specific dead code survived** — direct string search for the
exact WaW magic words found nothing beyond ordinary dictionary-word
noise.

**MW3's real clan-tag system is entirely Activision Elite-branded** —
confirmed strings: `use_eliteclan_tag`, `eliteClanTagText`,
`clear_eliteclan_tag`, `clanPrefix`, dozens of `elite_clan_*`/
`eliteclan_*` strings (get/retry/throttle, `elite_clan_get_motd`,
`starteliteclan`). Every clan-tag-adjacent string found ties to the dead
Elite social platform, same backend already confirmed dead elsewhere in
this project. Whether `popup_callsign` (a separate, non-Elite menu, per
the earlier menu-inventory pass) can be used purely offline without
touching the dead Elite fetch path is NOT confirmed — needs Ghidra
tracing of that menu's native storage call.

**Real risk for "just type `^1` in your tag" as an easy win**:
`stripColorsFromString` is a real, confirmed function in the binary — a
genuine color-stripping utility. NOT confirmed which fields it's applied
to (could be player names/tags specifically as an anti-troll measure, or
could be unrelated, e.g. log-file sanitization). The already-confirmed
`^1`-`^7` color renderer works for ordinary HUD/menu text elsewhere in
this project's research, but whether that extends to clan-tag/callsign
display specifically, or gets stripped first, is a genuine open question,
not a solved one.

**Animated effects are NOT a simple extension of static color.** The one
confirmed per-frame animated-text primitive in this binary (the MOTD
ticker, `type 20`/`newsfeed`, found separately this session) is a 2D
screen-space menu widget — in-game nameplates/HUD tags render through a
completely different 3D world-space path. Adapting the ticker mechanism
to nameplate rendering would be genuinely new implementation work, not a
reuse of an existing system.

**Recommendation**: static color (if the `stripColorsFromString` risk
clears) is the realistic near-term target; animated tags are a separate,
much bigger, unstarted undertaking — don't scope them together. **Not yet
chased, needed before any implementation**: the exact native draw
function for clan tags/callsigns (does it share the ordinary UI
color-code parser or not), whether `popup_callsign` genuinely works
offline, and which fields `stripColorsFromString` is actually wired to —
all three need Ghidra work, not string search.

**MAJOR COMPLICATION found (2026-07-18, deeper Ghidra pass): clan-tag
storage is networked SESSION data, not simple local storage.** Decompiled
`FUN_00580250`/`FUN_00581be0` (the only real cross-references to
`eliteClanTagText`) — both implement a bitstream session-SYNC protocol:
per-session-member slot data (`clanPrefix`, `useEliteClanTag`,
`eliteClanTagText`, plus MP/SO stats) read/written via a generic
dvar-style accessor, diffed against cached per-slot values, and pushed as
outgoing NETWORK delta strings (`"ectatx \"%s\""`, `"ecuta %i"`) when
changed. **The clan tag lives as networked lobby-member presence state,
not a plain saved string** — this is a materially bigger blocker than
"maybe it gets color-stripped." Also: `popup_callsign` (`FUN_0054fe20`)
is confirmed grouped with `"menu_xboxlive"`/`"menu_xboxlive_privatelobby"`
in a live-session-state gate — strong evidence it's part of the online-
session UI flow, not an isolated local-only menu; whether it can function
at all with the backend dead is unresolved. `stripColorsFromString` is
confirmed GSC-invocable-only (a method-table entry, zero direct native
call sites) and never called anywhere in the 317-file SP/Survival GSC
corpus — a non-issue within SP/Survival scope specifically, but this says
nothing about MP-side content (where clan tags actually matter), which
this project hasn't dumped.

**Revised recommendation**: before continuing to fight the dead
Elite-session system head-on, determine whether ANY local-only player-
name/tag field exists in SP/Survival — Campaign/Survival are largely
offline single-player, so the game must have SOME non-networked
player-name concept for HUD/save purposes, which could be a much better
integration point than the networked clan-tag system.

**Found (2026-07-18, follow-up pass): `self.playername`, a real,
likely-native-sourced GSC entity field, distinct from the networked
Elite clan-tag system.** Confirmed usage: `137.gsc:562` —
`self._id_1819 settext(self.playername)`, inside the Special Ops co-op
pre-mission "waiting for players" ready-up screen; `181.gsc:704` —
`var_3._id_16C6["name"] = var_3.playername`. Both display it via an
ordinary 2D HUD text-string builtin — the same general category of
element as other UI/HUD text already confirmed elsewhere in this project
to use the real `^1`-`^7` color-code renderer (not independently
re-verified for THIS specific field this pass). **No GSC-side assignment
of `self.playername` exists anywhere in the 317-file corpus** — read-only
from script's perspective, strongly implying native engine auto-sync
(the common id-engine pattern of entity fields like `self.health` being
populated directly from the internal player struct), structurally
distinct from the clan-tag system's explicit bitstream-sync writes. No
native `PlayerName`/`profileName`-style dvar string found (zero hits) —
the real native population point wasn't traced.

**Genuinely useful finding for animation feasibility**: this display path
is confirmed 2D HUD only, NOT the elusive 3D world-space nameplate
renderer — actually a BETTER fit for animation than the 3D path would
have been, since it's the same general category of element as the
already-confirmed animated MOTD ticker (`type 20`/`newsfeed`).

**Caveat, not resolved**: the only confirmed use of `self.playername` is
a PRE-MISSION LOBBY screen (both players' names before a session starts),
not an in-combat nameplate — a general "shows during actual gameplay"
use is not yet confirmed to exist. Whether a REMOTE co-op partner's
`playername` is itself network-synced (even if the local player's own
value is locally sourced) is also unconfirmed. **Not yet reached**: the
field's exact native population function/entity-struct offset, whether
it truly bypasses `stripColorsFromString`, and a broader search for
other display sites beyond the two found.

## Open questions / next steps for task #6

**Status as of 2026-07-18: every major research question is now resolved.
This is implementation-ready** — remaining items are build/wire-up work
and one live test, not open unknowns.

- ~~Locate the `Font`-asset equivalent of `FUN_004adc60`~~ **RESOLVED**:
  it's `FUN_0045d040` (`"fonts/<name>", 0`), already used elsewhere in
  this binary for font boot-registration, same calling convention as the
  already-proven `FindOrLoadMenuList` pattern.
- ~~Build a real test font via the pipeline and confirm it builds~~
  **RESOLVED**: the full PNG→image→material→font→`.ff` pipeline is
  proven to work end-to-end, a real `testfont.ff` was built successfully
  (6 real schema gotchas found and documented above). **Not yet done**:
  the actual RUNTIME load test in the live game — this is now the single
  remaining unverified step.
- ~~Wire up the bind-text-resolver hook (`FUN_0061f6f0`)~~ **Calling
  convention RESOLVED** (register+stack hybrid, confirmed safe to hook,
  unlike the rumble dispatchers that crashed the game the same day) —
  the hook itself still needs to be WRITTEN, this is now a build task,
  not a research question.
- ~~Check remaining zones for a dormant console glyph atlas~~ **RESOLVED,
  clean negative**: checked every zone type including MP and patches —
  no dormant atlas exists anywhere; `assets/button_glyphs/` is
  confirmed the only source.
- **Remaining real implementation work**: extend a copy of a real font
  (e.g. `hudBigFont`) with the custom glyphs rather than building a
  font from scratch (a hard schema requirement — real fonts need all 96
  standard codepoints present, confirmed via the build-pipeline test).
- Decide on a `GlyphStyle` config option (Xbox 360 / Xbox One / Series X|S / PS3 /
  PS4 / PS5), similar to the existing `ButtonLayout`/`StickLayout` config, so players
  can pick their preferred prompt look independent of which physical controller
  they're using (XInput doesn't distinguish Xbox-branded from PlayStation-branded
  controllers on Windows, so this can't be auto-detected from the input API alone).

### Concrete live-test plan (2026-07-18) — two separate proof points, not one combined test

Designed to visually prove the pipeline via the main menu, per the user's
own suggestion. **Real structural finding along the way**: the main
menu's static button-list text (`textfont 3`, real dump of
`ui/ui/main_campaign.menu` — `main.menu` itself is just a router that
immediately opens `main_campaign` for `gameMode == "sp"`) is a plain
`locstring()` call, NOT routed through the bind-resolver
(`FUN_0061f6f0`) at all — so the confirmed-safe resolver hook can't
inject a glyph into it. This splits the test into two independent
halves rather than one:

**Half 1 — font-override-safety test, ON the main menu:**
1. Override target: `fonts/bigfont` (best single guess for menu-title
   text; if wrong, the failure mode is "no visible change," not a
   crash — safe to iterate on).
2. Build the override per the already-proven pipeline: fresh
   `Unlinker.exe` dump of whichever zone owns `bigfont` (likely
   `code_post_gfx.ff`) to extract its real image/material/glyph-table,
   faithfully copy all 96 required glyphs (hard schema requirement),
   add ONE new glyph at codepoint `0x81` pointing at a converted
   `assets/button_glyphs/xbox360_a.png` (`ImageConverter.exe --iw5`),
   same `Linker.exe -l <base_zone> <project>` technique already proven
   to build a valid `.ff`.
3. Splice into the boot-time zone queue via a `FUN_004ca310` detour
   targeting `FUN_00679680`'s caller specifically (read the return
   address, only inject for that one caller) — the first real
   IMPLEMENTATION of the mechanism issue #23 already proposed for the
   options-menu work.
4. **Real unresolved risk, not solved yet**: registration ORDER. If
   `FUN_004ff000` (the generic asset-lookup function) uses "first
   registered wins" for duplicate names, the spliced zone must queue
   BEFORE `ui.ff`'s own `fonts/bigfont` registration; if "last wins,"
   after. Not confirmed either way — first guess: queue LAST (matches a
   common "mod overrides win" pattern), gated behind an easy config
   toggle (same `[Experimental]` pattern already established) so it can
   be flipped off instantly if wrong.
5. **Success criterion achievable with ZERO new hook code beyond the
   splice itself**: the main menu still renders ALL its normal text
   correctly with this override active — validates the splice+override
   pipeline is safe, independent of whether the new glyph is visible.

**Half 2 — glyph-visibility test, in actual GAMEPLAY, not the main menu:**
The main menu's one bind-hint-shaped element
(`@PLATFORM_FRIENDS_SHORTCUT`, a real `execKey "f"` hint) is gated on
`isusersignedintolive()` — almost certainly invisible offline, not a
reliable test vehicle. **Recommended instead**: hook `FUN_0061f6f0`
(confirmed-safe calling convention, see above) to unconditionally
append codepoint `0x81` to any resolved bind-hint, then check a real,
always-visible Campaign interact prompt (e.g. "Press [E]/[F]" hints) —
this is this mod's actual real target use case anyway, not a menu-
specific detour, and doesn't depend on the main-menu font-override
question at all.

**Bottom line**: these two halves are independently testable and
should be implemented/verified separately, not combined into one main-
menu test — that combination is blocked by a real structural fact
(static menu text isn't resolver-hooked), not a flaw in the plan.

### Boot-zone splice: pressure-tested, conditional GO (2026-07-18)

Given this is the project's first-ever boot-sequence hook, and given
the same-day rumble-hook crash, the splice plan was independently
pressure-tested (not just planned) before implementation — MinHook
safety on a small target, multi-invocation idempotency, a real compiled
simulation of the array-splice logic, return-address caller
identification, and an honest risk comparison against the rumble crash.

- **MinHook on a tiny function: confirmed safe, not a real concern.**
  MinHook's trampoline generation walks whole instructions until it has
  ≥5 bytes, never truncates mid-instruction — this project's own
  already-working hooks on similarly small targets (`FUN_0057de60`,
  `FUN_00644ed0`, `FUN_00643ce0`) are existing proof this class of
  target hooks fine in this exact binary/toolchain.
- **Idempotency is a REAL requirement, confirmed by an actual compiled
  test**: a second hook firing (e.g. on a device-creation retry) without
  a guard silently inserts a DUPLICATE entry in a second unused slot.
  Fix: a single static `bool` guard, set only after a successful splice
  — confirmed working in the test.
- **Real, compiled simulation of the splice logic** (Python, run for
  real, not just described): correctly finds the first unused slot in
  both a 5-slot/2-unused and 10-slot/4-unused scenario, leaves all other
  slots byte-for-byte unchanged, and — critically — correctly REFUSES to
  write and leaves the buffer untouched when the array is completely
  full, rather than corrupting an existing entry. **This fail-safe
  no-splice-but-still-forward path is mandatory in the real
  implementation.** Caveat: the simulation assumes a zero `namePtr`
  sentinel and a 12-byte `{u32,u32,u32}` entry layout — both
  characterizations from prior research, NOT independently reconfirmed
  by this pass; the real struct layout must be confirmed before this
  logic is trusted as-is.
- **Return-address caller ID: standard and reliable** — `[ESP]` at true
  function entry is always the real return address, guaranteed by `CALL`
  semantics. Safe as long as `FUN_00679680` reaches `FUN_004ca310` via a
  single direct `CALL` (not an indirect/computed jump) — needs
  confirming, not assumed.
- **Risk vs. today's rumble crash: genuinely lower, different class.**
  The rumble crash came from hooking a GENERIC multi-purpose dispatcher
  (95/4 real call sites, genuinely varying argument shapes by
  construction). `FUN_004ca310` is a narrow zone-loading utility with
  ~4 callers, each with its own single consistent call shape — closer to
  `FUN_0045e320` (confirmed single-call-site-safe) than to the
  dispatchers that crashed. Residual risk is confirming the two items
  below, not an inherent shape problem.

**GO, conditional on two items from the companion prep fork being
CONFIRMED before implementation, not assumed**: (1) `FUN_004ca310`'s own
real calling convention, (2) the real `{name,flags,unk}` entry struct
layout and unused-slot sentinel convention. Implementation must include
all four safety pieces above (idempotency guard, fail-safe no-splice
path, return-address equality check, confirmed struct layout) — each is
a cheap, mechanical addition once the two confirmations land, not an
open design question.

### Both conditions CONFIRMED, real asset built (2026-07-18) — implementation-ready

**`FUN_004ca310`'s real calling convention, fully confirmed via
disassembly.** The function itself is genuinely tiny (2 instructions:
`CALL 0x00463430; JMP EAX` — a resolver-then-tail-jump), confirming
MinHook can hook it cleanly (its own trampoline preserves this exact
sequence, same as every other hook in this codebase). Real signature,
confirmed at ALL 4 real callers directly (not cited from a prior
summary): `FUN_004ca310(entry_array, count, flag)` — 3 plain stack args,
CONSISTENT across every call site, no register-passed args, no variable
arity (structurally safe, unlike the rumble dispatchers). Real callers:
`FUN_00679680` (the boot-time queue — `int[30]` local array, 10 entries
× 3 ints, confirms the "10-slot" characterization exactly; calls
`FUN_004ca310` TWICE, reusing the buffer for two batches — both call
sites are within this one function's address range, so a single
return-address-range check catches both), `FUN_0067a690`,
`FUN_00481e50`, `FUN_0053cbc0` (the other 3 real callers). **Entry
format confirmed**: `{namePtr_or_int, typeFlag, 0}` triples, 12
bytes/entry — matches the pressure-test simulation's assumed layout
exactly.

**Real extended `fonts/bigfont` successfully built** —
`bigfont_ext.ff` (3296 bytes, 0 warnings/0 errors). Real blocker hit and
solved along the way: the real `bigfont` shares ONE shared texture atlas
(`gamefonts_pc`, 512×1104 DXT5) across ~191 real glyphs (covers extended
Latin, not just the 96 required ASCII codepoints) — no usable blank
region existed for the new 70×71 glyph (67% of the atlas is transparent
but scattered in sub-16px gaps). **Solution**: extended the canvas
height (1104→1184px), pasted the new glyph into the added strip, and
mathematically rescaled all ~191 existing glyphs' `t0`/`t1` UVs by the
exact ratio `1104/1184` — lossless and exact (new UV = old UV ×
old_height/new_height), not guessed. `s0`/`s1` needed no change (width
unchanged). New glyph at codepoint `129`/`0x81`. **New tooling gotcha,
not previously documented**: `ImageConverter.exe` REJECTS `.png` input
directly (`"ERROR: Unsupported extension .png"`) — must go through
`.dds` first (Pillow can decode/encode `gamefonts_pc.dds` cleanly).
Image/font/both materials (`fonts/gamefonts_pc`,
`fonts/gamefonts_pc_glow`) are all named IDENTICALLY to the real
originals — confirmed a genuine drop-in override candidate.

**This is now fully implementation-ready** — both blocking confirmations
are in, a real working asset exists on disk (copied into the repo at
`assets/zones/bigfont_ext.ff`), and the splice-hook safety requirements
are fully specified. **Final precision check complete (2026-07-18)**:
`FUN_00679680` calls `FUN_004ca310` twice — Call 1 (`0x006796e6`,
return address `0x006796EB`) is CONDITIONAL, gated on a global being
non-zero, `flag=1`; Call 2 (`0x006797bd`, return address `0x006797C2`)
is UNCONDITIONAL (the function's natural fall-through, always executes),
`flag=0`. **Recommend splicing into Call 2** (return address
`0x006797C2`) — always runs, and its own real neighboring entries
(`DAT_021d2e78`/`DAT_021d2e7c`) establish `typeFlag=1` as the correct
per-entry value for a plain zone name in that exact batch. The outer
`flag` parameter's own semantic meaning was investigated and came back a
genuine dead end (`FUN_004ca310`'s tail-jump target turned out to be
generic relocation/thunk infrastructure, not zone-loading logic) —
doesn't block implementation, just means `flag=0` should be treated as
"whatever this batch already uses," not further interpreted.

Next step is writing the actual C++ `FUN_004ca310` detour in the mod
(return-address check against `0x006797C2` specifically, idempotency
guard, fail-safe no-splice-if-full path, `{"assets/zones/bigfont_ext",
1, 0}`-shaped entry). **Not yet attempted**: loading this into the
running game — the build side is proven, live-loading is the one
remaining unverified step.
